# Optional owned server profile. Importing/CheckOnly performs no downloads,
# server launch, Steam initialization, WFP activation or research mutation.
Set-StrictMode -Version Latest
$script:HealthMetaSha = '16B849F1CBF1503266A1B89D9B4ED38807091FABE724A8607D544338D155A005'
function Get-TestStartHealthComponents {
    param([string]$RepositoryRoot, [switch]$PathsOnly)
    $meta = Join-Path $RepositoryRoot 'build/test-start-health-deps/addons/metamod/dlls/metamod.dll'
    $helper = Join-Path $RepositoryRoot 'build/test-start-health/Release/hlclient_test_start_health.dll'
    $receiptPath = Join-Path $RepositoryRoot 'build/test-start-health/verified-components.json'
    foreach ($path in @($meta, $helper, $receiptPath)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "test_start_health_component_missing: $path; run scripts/prepare_test_start_health.ps1 offline first." }
        if ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'test_start_health_component_reparse' }
    }
    if ((Get-Item -LiteralPath $receiptPath).Length -gt 16384) { throw 'test_start_health_receipt_bound' }
    # Launcher checks presence; the owning runner verifies bytes once before
    # using the prepared components. No persistent size/mtime hash cache.
    if ($PathsOnly) { return @{Meta=$meta; Helper=$helper} }
    $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    if ($receipt.schema -cne 'hlclient.test-start-health-components.v1' -or
        (Get-FileHash -LiteralPath $meta).Hash -cne $script:HealthMetaSha -or
        (Get-FileHash -LiteralPath $helper).Hash -cne $receipt.helper_sha256) { throw 'test_start_health_component_hash_mismatch' }
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($source in $receipt.sources) {
        if ($source.path -cnotmatch '^test_server/start_health/(CMakeLists.txt|plugin.cpp|exports.def|offline_test.cpp)$') { throw 'test_start_health_receipt_source_invalid' }
        if (-not $seen.Add([string]$source.path)) { throw 'test_start_health_receipt_duplicate_source' }
        if ((Get-FileHash -LiteralPath (Join-Path $RepositoryRoot $source.path)).Hash -cne $source.sha256) { throw 'test_start_health_helper_stale' }
    }
    if (@($receipt.sources).Count -ne 4) { throw 'test_start_health_receipt_sources_missing' }
    return @{Meta=$meta; MetaSha=$script:HealthMetaSha; Helper=$helper; HelperSha=$receipt.helper_sha256}
}
function New-TestStartHealthProfile {
    param([string]$RepositoryRoot, [string]$ResearchRoot, [string]$RunId)
    if ($RunId -cnotmatch '^[0-9a-f]{32}$') { throw 'test_start_health_run_invalid' }
    $components = Get-TestStartHealthComponents $RepositoryRoot
    $liblist = Join-Path $ResearchRoot 'valve/liblist.gam'
    if ((Get-Item -LiteralPath $liblist).Length -gt 65536) { throw 'test_start_health_liblist_bound' }
    $content = [IO.File]::ReadAllText($liblist)
    $matches = [regex]::Matches($content, '(?m)^gamedll\s+"dlls[\\/]hl\.dll"\s*\r?$')
    if ($matches.Count -ne 1) { throw 'test_start_health_original_gamedll_contract_invalid' }
    # Metamod startup can precede execution of +localinfo. Provide its
    # documented default config as well, without overwriting another setup.
    $startupConfig = Join-Path $ResearchRoot 'valve/addons/metamod/config.ini'
    if (Test-Path -LiteralPath $startupConfig) { throw 'test_start_health_startup_config_exists' }
    $relative = "addons/hlclient_test50/$RunId"
    $directory = Join-Path $ResearchRoot "valve/$relative"
    foreach ($candidate in @($directory,$startupConfig)) {
        $probe=[IO.Path]::GetFullPath($candidate)
        while ($probe) {
            if ((Test-Path -LiteralPath $probe) -and
                ((Get-Item -LiteralPath $probe -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'test_start_health_profile_reparse' }
            $probe=Split-Path -Parent $probe
        }
    }
    if (Test-Path -LiteralPath $directory) { throw 'test_start_health_run_profile_exists' }
    # The caller must already hold the established full restoration guard.
    # Each source was hashed above; copies are checked again before publication.
    [void][IO.Directory]::CreateDirectory($directory)
    Copy-Item -LiteralPath $components.Meta -Destination (Join-Path $directory 'metamod.dll')
    Copy-Item -LiteralPath $components.Helper -Destination (Join-Path $directory 'hlclient_test_start_health.dll')
    if ((Get-FileHash (Join-Path $directory 'metamod.dll')).Hash -cne $script:HealthMetaSha -or
        (Get-FileHash (Join-Path $directory 'hlclient_test_start_health.dll')).Hash -cne $components.HelperSha) { throw 'test_start_health_copy_hash_mismatch' }
    [IO.File]::WriteAllText((Join-Path $directory 'plugins.ini'), "win32 $relative/hlclient_test_start_health.dll`n", [Text.Encoding]::ASCII)
    [IO.File]::WriteAllText((Join-Path $directory 'config.ini'), "gamedll dlls/hl.dll`nplugins_file $relative/plugins.ini`n", [Text.Encoding]::ASCII)
    [IO.File]::AppendAllText((Join-Path $directory 'config.ini'), "exec_cfg $relative/empty.cfg`n", [Text.Encoding]::ASCII)
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $startupConfig))
    Copy-Item -LiteralPath (Join-Path $directory 'config.ini') -Destination $startupConfig
    [IO.File]::WriteAllText((Join-Path $directory 'empty.cfg'), "// Test profile: no automatic commands.`n", [Text.Encoding]::ASCII)
    $modified = [regex]::Replace($content, '(?m)^gamedll\s+"dlls[\\/]hl\.dll"\s*\r?$', "gamedll `"$relative/metamod.dll`"")
    [IO.File]::WriteAllText($liblist, $modified, [Text.UTF8Encoding]::new($false))
    return @{Profile='test-server-assisted'; RunId=$RunId; Relative=$relative; HelperSha=$components.HelperSha}
}
function Get-TestStartHealthEvidence {
    param([string]$RunRoot, [string]$RunId)
    $server = Join-Path $RunRoot 'logs/server-diagnostic-redacted.log'
    $client = Join-Path $RunRoot 'logs/client-diagnostic-redacted.log'
    foreach ($path in @($server,$client)) { if ((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).Length -gt 65536) { throw 'test_start_health_evidence_bound' } }
    $serverText = if (Test-Path -LiteralPath $server) { [IO.File]::ReadAllText($server) } else { '' }
    $clientText = if (Test-Path -LiteralPath $client) { [IO.File]::ReadAllText($client) } else { '' }
    $serverMatch = [regex]::Matches($serverText, "\[hlclient-test-health\] run=$RunId requested_start_health=50 server_setup_applied=true max_health=100 max_health_source=server_entvars")
    $clientMatch = [regex]::Matches($clientText, '\[test-start-health\] requested_start_health=50 client_observed_health=50 source=fresh_clientdata result=ready')
    $helperFailure = [regex]::Match($serverText,
        "\[hlclient-test-health\] run=$RunId requested_start_health=50 server_setup_applied=(identity_rejected|slot_rejected|ambiguous_identity|spawn_validation_failed|target_not_matched|connection_binding_missing) max_health=unavailable max_health_source=unavailable")
    $configurationMatches = [regex]::Matches($serverText,
        '(?m)^\[hlclient-test-health-config\] reason=(ready|run_missing|run_invalid|profile_missing|profile_mismatch|globals_unavailable|deathmatch_unavailable|client_limit_invalid|map_mismatch)\r?$')
    $configurationReason = if ($configurationMatches.Count) { $configurationMatches[-1].Groups[1].Value } else { 'unavailable' }
    return [ordered]@{
        profile='test-server-assisted'; requested_start_health=50
        helper_attached=$serverText.Contains('[hlclient-test-health-stage] stage=attached')
        helper_server_activated=$serverText.Contains('[hlclient-test-health-stage] stage=server_activated configured=true')
        helper_configuration_reason=$configurationReason
        server_setup_applied=($serverMatch.Count -eq 1)
        client_observed_health=$(if ($clientMatch.Count -eq 1) { 50 }
            elseif ($clientText.Contains('[test-start-health] requested_start_health=50 client_observed_health=100 source=fresh_clientdata result=pending')) { 100 } else { $null })
        max_health=$(if ($serverMatch.Count -eq 1) { 100 } else { $null })
        max_health_source=$(if ($serverMatch.Count -eq 1) { 'server_entvars' } else { 'unavailable' })
        result=$(if ($serverMatch.Count -eq 1 -and $clientMatch.Count -eq 1) { 'confirmed' } else { 'test_start_health_not_confirmed' })
        failure_reason=$(if ($serverMatch.Count -eq 1 -and $clientMatch.Count -eq 1) { 'none' }
            elseif ($serverMatch.Count -gt 1) { 'duplicate_server_application_evidence' }
            elseif ($clientText.Contains('fresh_server_50_not_observed')) { 'fresh_server_50_not_observed' }
            elseif ($helperFailure.Success) { $helperFailure.Groups[1].Value }
            elseif ($configurationReason -notin @('ready','unavailable')) { "helper_configuration_$configurationReason" }
            elseif ($serverMatch.Count -ne 1) { 'server_application_not_confirmed' }
            else { 'client_fresh_50_not_confirmed' })
    }
}
Export-ModuleMember -Function Get-TestStartHealthComponents, New-TestStartHealthProfile, Get-TestStartHealthEvidence
