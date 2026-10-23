#requires -Version 7.0
<#
.SYNOPSIS
Offline smoke of an already extracted native Windows transfer package.
.DESCRIPTION
Build-host tooling only; this script is not a dependency of the shipped ZIP.
Uses immutable production test contracts. No connect attempt, Steam API, stock
assets, account, server, firewall change, or installed runtime is required.
The native launcher's known-folder lookup is not redirected by environment
variables. Its --offline-ui-check contract deliberately skips settings reads
and writes; that is not presented as a real new Windows user profile test.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackageDirectory,
    [Parameter(Mandatory)][string]$EvidencePath,
    [string]$ScratchParent = [IO.Path]::GetTempPath()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$package = [IO.Path]::GetFullPath($PackageDirectory).TrimEnd('\')
$evidence = [IO.Path]::GetFullPath($EvidencePath)
$windows = [Environment]::GetFolderPath('Windows').TrimEnd('\')
if (!$windows -or !(Test-Path -LiteralPath $package -PathType Container)) { throw 'Windows and an existing extracted package are required.' }
if ($package -eq $repo -or $package.StartsWith($repo + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'The tested package must be extracted outside the source repository.' }
if ($package -notmatch ' ' -or $package -notmatch '[^\x00-\x7f]') { throw 'Use an extracted package path containing a space and a non-ASCII character.' }
if ($evidence.StartsWith($package + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Evidence must not modify the tested package.' }
$scratchBase = [IO.Path]::GetFullPath($ScratchParent)
if (!(Test-Path -LiteralPath $scratchBase -PathType Container)) { throw 'Scratch parent does not exist.' }
$scratch = Join-Path $scratchBase ('hlc-transfer-smoke-' + [guid]::NewGuid().ToString('N'))
$profile = Join-Path $scratch 'Controlled profile'
$working = Join-Path $scratch 'Unrelated working directory'
$local = Join-Path $profile 'AppData\Local'
$temp = Join-Path $local 'Temp'
New-Item -ItemType Directory -Path $working,$temp,(Split-Path -Parent $evidence) -Force | Out-Null
$systemPath = "$windows\System32;$windows;$windows\System32\Wbem"
$environmentOverrides = [ordered]@{ PATH=$systemPath; LOCALAPPDATA=$local; APPDATA=(Join-Path $profile 'AppData\Roaming'); USERPROFILE=$profile; TEMP=$temp; TMP=$temp; HLCLIENT_SMOKE_TEST_FRAMES='120' }
New-Item -ItemType Directory -Path $environmentOverrides.APPDATA -Force | Out-Null
$removedEnvironmentKeys = @('SteamAppId','SteamGameId','SteamOverlayGameId','SteamPath','SteamClientLaunch','_NT_SYMBOL_PATH','SDL_VIDEODRIVER','SDL_RENDER_DRIVER','__COMPAT_LAYER','HLCLIENT_ASSET_ROOT','HLCLIENT_STOCK_ROOT')
$cases = [Collections.Generic.List[object]]::new()
$owned = [Collections.Generic.List[Diagnostics.Process]]::new()
$complete = $false
$failure = $null
$startupFailureLogging = [ordered]@{ status='not_run'; diagnostics_disabled=$null; child_exit=$null; zip_verified=$false }

function Assert-Transfer([bool]$Condition,[string]$Message) { if (!$Condition) { throw $Message } }
function Write-TransferJson([string]$Path,$Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 40), [Text.UTF8Encoding]::new($false))
}
function Get-PackageFiles {
    return @(Get-ChildItem -LiteralPath $package -File -Recurse | Sort-Object FullName | ForEach-Object {
        [pscustomobject]@{ path=[IO.Path]::GetRelativePath($package,$_.FullName); bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    })
}
function Get-SettingsSnapshot {
    # Observe only identity, never read or copy the user's settings contents.
    $path = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'HLClientEngine\launcher.json'
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $file=Get-Item -LiteralPath $path
        return [pscustomobject]@{ exists=$true; bytes=$file.Length; sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash; last_write_utc=$file.LastWriteTimeUtc.ToString('o') }
    }
    return [pscustomobject]@{ exists=$false }
}
function Start-Owned([string]$Name,[string]$Executable,[string[]]$Arguments,[string]$FixtureDirectory='') {
    if ($FixtureDirectory) {
        $FixtureDirectory=[IO.Path]::GetFullPath($FixtureDirectory).TrimEnd('\')
        Assert-Transfer ($FixtureDirectory.StartsWith($scratch+'\',[StringComparison]::OrdinalIgnoreCase)) 'An offline fixture must be inside this test run scratch directory.'
    }
    $spec=[Diagnostics.ProcessStartInfo]::new()
    $spec.FileName=$Executable; $spec.WorkingDirectory=$working
    $spec.UseShellExecute=$false; $spec.CreateNoWindow=$true
    $spec.RedirectStandardOutput=$true; $spec.RedirectStandardError=$true
    foreach ($key in $environmentOverrides.Keys) { $spec.Environment[$key]=[string]$environmentOverrides[$key] }
    foreach ($key in $removedEnvironmentKeys) { [void]$spec.Environment.Remove($key) }
    foreach ($argument in $Arguments) { [void]$spec.ArgumentList.Add($argument) }
    $process=[Diagnostics.Process]::new(); $process.StartInfo=$spec
    $started=[DateTime]::UtcNow
    if (!$process.Start()) { throw "Could not start owned offline case $Name" }
    $owned.Add($process)
    return [pscustomobject]@{ name=$Name; process=$process; started=$started; arguments=$Arguments; executable=$Executable; fixture_directory=$FixtureDirectory; stdout=$process.StandardOutput.ReadToEndAsync(); stderr=$process.StandardError.ReadToEndAsync(); modules=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase); module_errors=[Collections.Generic.HashSet[string]]::new(); samples=0 }
}
function Sample-Owned($Run) {
    try {
        $Run.process.Refresh()
        if (!$Run.process.HasExited) {
            foreach ($module in $Run.process.Modules) { [void]$Run.modules.Add($module.FileName) }
            $Run.samples++
        }
    } catch { [void]$Run.module_errors.Add($_.Exception.GetType().Name + ': ' + $_.Exception.Message) }
}
function Finish-Owned($Run,[int]$TimeoutMilliseconds=30000) {
    $watch=[Diagnostics.Stopwatch]::StartNew()
    do {
        Sample-Owned $Run
        if ($Run.process.WaitForExit(10)) { break }
        if ($watch.ElapsedMilliseconds -ge $TimeoutMilliseconds) {
            # Only this exact Process object created by Start-Owned is eligible.
            $Run.process.Kill(); $Run.process.WaitForExit()
            throw "$($Run.name) exceeded its offline bound. Owned PID $($Run.process.Id) was stopped."
        }
    } while ($true)
    $stdout=$Run.stdout.GetAwaiter().GetResult(); $stderr=$Run.stderr.GetAwaiter().GetResult()
    $modules=@($Run.modules | Sort-Object | ForEach-Object {
        $origin=if ($_.StartsWith($package+'\',[StringComparison]::OrdinalIgnoreCase)) {'package'} elseif ($Run.fixture_directory -and $_.StartsWith($Run.fixture_directory+'\',[StringComparison]::OrdinalIgnoreCase)) {'owned_hash_verified_fixture'} elseif ($_.StartsWith($windows+'\',[StringComparison]::OrdinalIgnoreCase)) {'windows_system_or_driver_store'} else {'outside_package_and_windows'}
        [pscustomobject]@{ path=$_; origin=$origin }
    })
    $result=[pscustomobject]@{ name=$Run.name; executable=$Run.executable; arguments=@($Run.arguments); cwd=$working; fixture_directory=$Run.fixture_directory; started_utc=$Run.started.ToString('o'); finished_utc=[DateTime]::UtcNow.ToString('o'); exit_code=$Run.process.ExitCode; stdout=$stdout; stderr=$stderr; loaded_module_samples=$Run.samples; loaded_modules=$modules; module_observation_errors=@($Run.module_errors | ForEach-Object { $_ }); status='observed' }
    $cases.Add($result)
    Assert-Transfer (@($modules | Where-Object origin -eq 'outside_package_and_windows').Count -eq 0) "$($Run.name) loaded a non-system module outside the package."
    Write-Host "$($Run.name): exit=$($result.exit_code), module_samples=$($result.loaded_module_samples)"
    return $result
}
function Invoke-Case([string]$Name,[string]$Executable,[string[]]$Arguments,[int]$ExpectedExit,[string]$RequiredPattern='',[string]$FixtureDirectory='') {
    $result=Finish-Owned (Start-Owned $Name $Executable $Arguments $FixtureDirectory)
    Assert-Transfer ($result.exit_code -eq $ExpectedExit) "${Name}: unexpected exit $($result.exit_code); $($result.stderr)"
    if ($RequiredPattern) { Assert-Transfer (($result.stdout+"`n"+$result.stderr) -match $RequiredPattern) "$Name did not emit its required receipt." }
    $result.status='passed'
    return $result
}

$beforeFiles=Get-PackageFiles
$beforeSettings=Get-SettingsSnapshot
try {
    $client=Join-Path $package 'hlclient.exe'
    $launcher=Join-Path $package 'HLClientLauncher.exe'
    $helper=Join-Path $package 'HLClientSystemDiagnostics.exe'
    foreach ($file in @($client,$launcher,$helper,(Join-Path $package 'SDL3.dll'))) { Assert-Transfer (Test-Path -LiteralPath $file -PathType Leaf) "Required packaged binary missing: $file" }
    [void](Invoke-Case 'packaged_client_version' $client @('--version') 0)
    [void](Invoke-Case 'packaged_client_help' $client @('--help') 0 'Usage: hlclient')
    [void](Invoke-Case 'packaged_client_null' $client @('--renderer','null') 0)
    $idle=Invoke-Case 'packaged_client_offline_opengl_idle' $client @('--open-client','--width','640','--height','480','--window-mode','windowed','--msaa','off','--vsync','0') 0 '\[console-application\] first_idle_frame_presented=true start_idle=true'
    Assert-Transfer (($idle.stdout+"`n"+$idle.stderr) -notmatch '\[net\]|getchallenge|live_visual_startup|steam_api_initializ|provider_begin|\[session-close\]|Half-Life game directory validated|Asset pipeline initialized|console_background=') 'Offline idle unexpectedly entered asset/Steam/network/session work.'
    Assert-Transfer (@($idle.loaded_modules | Where-Object { $_.origin -eq 'package' -and [IO.Path]::GetFileName($_.path) -ieq 'SDL3.dll' }).Count -eq 1) 'Packaged SDL3 was not observed loaded by the normal renderer.'
    [void](Invoke-Case 'packaged_client_missing_assets' $client @('--renderer','null','--basedir',(Join-Path $scratch 'Missing Half Life')) 1 'does not exist|not.*directory|directory.*not|unavailable|missing')
    # Argument validation happens before a transport exists. No packet is sent.
    [void](Invoke-Case 'packaged_client_missing_runtime_argument' $client @('--renderer','null','--connect','127.0.0.1:27015','--stop-after','connect-response','--auth-provider','steam') 2 'requires --steam-api-runtime')
    $ui=Invoke-Case 'packaged_native_launcher_offline_controls' $launcher @('--offline-ui-check') 0 'launcher_scroll_check=passed'
    Assert-Transfer (@($ui.loaded_modules | Where-Object { $_.origin -eq 'package' -and [IO.Path]::GetFileName($_.path) -ieq 'SDL3.dll' }).Count -eq 1) 'Packaged SDL3 was not observed loaded by the native launcher.'
    # Exercise the real adjacent child process through the launcher. This
    # immutable contract bypasses saved configuration, forces an invalid SDL
    # video driver for its child only, and never reaches Steam/assets/network.
    # Both expensive diagnostic toggles remain off; startup logs must survive.
    $startup=Invoke-Case 'packaged_native_launcher_startup_failure_logs' $launcher @('--offline-startup-check') 0 'launcher_startup_check=passed'
    $startupReceipts=@([regex]::Matches($startup.stdout,'(?m)^launcher_startup_check_json=(\{[^\r\n]*\})\r?$') | ForEach-Object { $_.Groups[1].Value | ConvertFrom-Json })
    Assert-Transfer ($startupReceipts.Count -eq 1) 'Expected one structured native startup-check receipt.'
    $startupReceipt=$startupReceipts[0]
    Assert-Transfer ($startupReceipt.child_exit -eq 1 -and !$startupReceipt.diagnostics) 'Startup contract did not observe the intended child failure with diagnostics disabled.'
    $startupDirectory=[IO.Path]::GetFullPath([string]$startupReceipt.startup_log).TrimEnd('\')
    $startupZip=[IO.Path]::GetFullPath([string]$startupReceipt.export_zip)
    Assert-Transfer ($startupDirectory.StartsWith($temp+'\',[StringComparison]::OrdinalIgnoreCase)) 'Startup contract did not use its controlled test TEMP directory.'
    Assert-Transfer ($startupZip.StartsWith($temp+'\',[StringComparison]::OrdinalIgnoreCase)) 'Exported startup ZIP is outside the controlled test TEMP directory.'
    foreach ($logName in @('launcher-startup.log','client-startup.log')) {
        $logPath=Join-Path $startupDirectory $logName
        Assert-Transfer (Test-Path -LiteralPath $logPath -PathType Leaf) "Always-on startup log missing: $logName"
        Assert-Transfer ((Get-Item -LiteralPath $logPath).Length -gt 0) "Startup log is empty: $logName"
    }
    $clientStartup=Get-Content -LiteralPath (Join-Path $startupDirectory 'client-startup.log') -Raw
    Assert-Transfer ($clientStartup -match '(?i)fatal|hlclient-offline-missing-driver|SDL.*(?:unavailable|failed|error)') 'The child startup failure text was discarded from its copyable log.'
    Assert-Transfer (Test-Path -LiteralPath $startupZip -PathType Leaf) 'Copyable startup ZIP missing.'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $startupArchive=[IO.Compression.ZipFile]::OpenRead($startupZip)
    try {
        $startupEntries=@($startupArchive.Entries | ForEach-Object FullName)
        foreach ($logName in @('launcher-startup.log','client-startup.log')) {
            Assert-Transfer (@($startupEntries | Where-Object { ($_ -split '/')[-1] -ceq $logName }).Count -eq 1) "Export does not contain one $logName."
        }
        Assert-Transfer (@($startupEntries | Where-Object { $_ -match '(?i)auth|ticket|launch-config|launcher\.json|\.dmp$' }).Count -eq 0) 'Startup export contains an auth/config/dump file outside its intended scope.'
        $clientStartupEntry=@($startupArchive.Entries | Where-Object { ($_.FullName -split '/')[-1] -ceq 'client-startup.log' })[0]
        Assert-Transfer ($clientStartupEntry.Length -gt 0 -and $clientStartupEntry.Length -le 128KB) 'Exported client startup log has an invalid bound.'
        $clientStartupReader=[IO.StreamReader]::new($clientStartupEntry.Open(),[Text.Encoding]::UTF8)
        try { $exportedClientStartup=$clientStartupReader.ReadToEnd() } finally { $clientStartupReader.Dispose() }
        Assert-Transfer ($exportedClientStartup -ceq $clientStartup) 'Exported startup log payload differs from the recorded child error.'
    } finally { $startupArchive.Dispose() }
    $startup | Add-Member -NotePropertyName startup_check -NotePropertyValue ([pscustomobject]@{ child_exit=$startupReceipt.child_exit; diagnostics=$startupReceipt.diagnostics; startup_log=$startupDirectory; export_zip=$startupZip; exported_entries=$startupEntries })
    $startupFailureLogging = [ordered]@{ status='passed'; diagnostics_disabled=$true; child_exit=$startupReceipt.child_exit; zip_verified=$true; startup_log=$startupDirectory; export_zip=$startupZip; exported_entries=$startupEntries }
    # A missing delayed SDL DLL must fail after the launcher's early native
    # journal is available, rather than an opaque loader dialog before entry.
    # Copy only exact package bytes to our own disposable fixture. The tested
    # package and installed DLL search directories remain untouched.
    $missingSdlFixture=Join-Path $scratch 'Owned missing SDL fixture'
    New-Item -ItemType Directory -Path $missingSdlFixture -Force | Out-Null
    foreach ($fixtureName in @('HLClientLauncher.exe','msvcp140.dll','msvcp140_atomic_wait.dll','msvcp140_2.dll','vcruntime140.dll')) {
        $fixtureSource=Join-Path $package $fixtureName
        if (!(Test-Path -LiteralPath $fixtureSource -PathType Leaf)) { continue }
        $fixtureDestination=Join-Path $missingSdlFixture $fixtureName
        Copy-Item -LiteralPath $fixtureSource -Destination $fixtureDestination
        Assert-Transfer ((Get-FileHash -LiteralPath $fixtureSource -Algorithm SHA256).Hash -ceq (Get-FileHash -LiteralPath $fixtureDestination -Algorithm SHA256).Hash) 'Missing-SDL fixture copy identity mismatch.'
    }
    $missingSdl=Invoke-Case 'native_launcher_missing_adjacent_sdl' (Join-Path $missingSdlFixture 'HLClientLauncher.exe') @('--offline-ui-check') 2 'launcher_startup_failure_json=' $missingSdlFixture
    $missingSdlReceipts=@([regex]::Matches(($missingSdl.stdout+"`n"+$missingSdl.stderr),'(?m)^launcher_startup_failure_json=(\{[^\r\n]*\})\r?$') | ForEach-Object { $_.Groups[1].Value | ConvertFrom-Json })
    Assert-Transfer ($missingSdlReceipts.Count -eq 1) 'Missing-SDL failure did not report one copyable startup log location.'
    $missingSdlDirectory=[IO.Path]::GetFullPath([string]$missingSdlReceipts[0].startup_log).TrimEnd('\')
    $missingSdlZip=[IO.Path]::GetFullPath([string]$missingSdlReceipts[0].export_zip)
    Assert-Transfer ($missingSdlDirectory.StartsWith($temp+'\',[StringComparison]::OrdinalIgnoreCase)) 'Missing-SDL journal did not use the controlled test TEMP directory.'
    $missingSdlLog=Get-Content -LiteralPath (Join-Path $missingSdlDirectory 'launcher-startup.log') -Raw
    Assert-Transfer ($missingSdlLog -match 'stage=loading_sdl' -and $missingSdlLog -match 'sdl_library_load_failed' -and $missingSdlLog -match 'native_error=(?:126|2)(?:\s|$)') 'Missing-SDL journal lacks its concrete loader failure.'
    Assert-Transfer (Test-Path -LiteralPath $missingSdlZip -PathType Leaf) 'Missing-SDL startup ZIP was not exported.'
    Assert-Transfer (@($missingSdl.loaded_modules | Where-Object { [IO.Path]::GetFileName($_.path) -ieq 'SDL3.dll' }).Count -eq 0) 'Missing-SDL fixture unexpectedly loaded SDL from another directory.'
    $startupFailureLogging['missing_sdl_before_ui']='passed'
    $missingSdl | Add-Member -NotePropertyName startup_check -NotePropertyValue ([pscustomobject]@{ startup_log=$missingSdlDirectory; export_zip=$missingSdlZip; reason='sdl_library_load_failed'; expected_exit=2 })
    # Browser/download metadata must not change the byte identity of our
    # unsigned project executables. Mark only exact copies in owned scratch;
    # the package, source binaries and the user's downloaded files are untouched.
    # Running the real launcher/child contract also exercises the suspended
    # child's image verification, rather than only a standalone hash helper.
    $downloadFixture=Join-Path $scratch 'Owned downloaded executable fixture'
    New-Item -ItemType Directory -Path $downloadFixture -Force | Out-Null
    $downloadFixtureFiles=@(foreach ($fixtureName in @('HLClientLauncher.exe','hlclient.exe','HLClientSystemDiagnostics.exe','SDL3.dll','msvcp140.dll','msvcp140_atomic_wait.dll','msvcp140_2.dll','vcruntime140.dll')) {
        $fixtureSource=Join-Path $package $fixtureName
        $fixtureDestination=Join-Path $downloadFixture $fixtureName
        Assert-Transfer (Test-Path -LiteralPath $fixtureSource -PathType Leaf) "Downloaded-executable fixture source missing: $fixtureName"
        $fixtureHash=(Get-FileHash -LiteralPath $fixtureSource -Algorithm SHA256).Hash
        Copy-Item -LiteralPath $fixtureSource -Destination $fixtureDestination
        Assert-Transfer ((Get-FileHash -LiteralPath $fixtureDestination -Algorithm SHA256).Hash -ceq $fixtureHash) "Downloaded-executable fixture copy identity mismatch: $fixtureName"
        [pscustomobject]@{ name=$fixtureName; path=$fixtureDestination; sha256=$fixtureHash }
    })
    $downloadZone="[ZoneTransfer]`r`nZoneId=3`r`n"
    $downloadExecutables=@('hlclient.exe','HLClientLauncher.exe')
    foreach ($fixtureName in $downloadExecutables) {
        $fixtureExecutable=Join-Path $downloadFixture $fixtureName
        Set-Content -LiteralPath $fixtureExecutable -Stream Zone.Identifier -Value $downloadZone -Encoding ascii -NoNewline
        Assert-Transfer ((Get-Content -LiteralPath $fixtureExecutable -Stream Zone.Identifier -Raw) -ceq $downloadZone) "Download marker was not recorded exactly: $fixtureName"
        $fixtureIdentity=@($downloadFixtureFiles | Where-Object name -ceq $fixtureName)[0]
        Assert-Transfer ((Get-FileHash -LiteralPath $fixtureExecutable -Algorithm SHA256).Hash -ceq $fixtureIdentity.sha256) "Download marker changed executable bytes: $fixtureName"
    }
    $downloaded=Invoke-Case 'native_launcher_downloaded_executables_startup_failure_logs' (Join-Path $downloadFixture 'HLClientLauncher.exe') @('--offline-startup-check') 0 'launcher_startup_check=passed' $downloadFixture
    $downloadedReceipts=@([regex]::Matches($downloaded.stdout,'(?m)^launcher_startup_check_json=(\{[^\r\n]*\})\r?$') | ForEach-Object { $_.Groups[1].Value | ConvertFrom-Json })
    Assert-Transfer ($downloadedReceipts.Count -eq 1) 'Downloaded-executable startup check did not emit one structured receipt.'
    $downloadedReceipt=$downloadedReceipts[0]
    Assert-Transfer ($downloadedReceipt.child_exit -eq 1 -and !$downloadedReceipt.diagnostics) 'Downloaded-executable check did not reach the intended child SDL failure with diagnostics disabled.'
    $downloadedLogDirectory=[IO.Path]::GetFullPath([string]$downloadedReceipt.startup_log).TrimEnd('\')
    $downloadedZip=[IO.Path]::GetFullPath([string]$downloadedReceipt.export_zip)
    Assert-Transfer ($downloadedLogDirectory.StartsWith($temp+'\',[StringComparison]::OrdinalIgnoreCase)) 'Downloaded-executable journal is outside controlled test TEMP.'
    Assert-Transfer ($downloadedZip.StartsWith($temp+'\',[StringComparison]::OrdinalIgnoreCase)) 'Downloaded-executable export is outside controlled test TEMP.'
    $downloadedClientLog=Get-Content -LiteralPath (Join-Path $downloadedLogDirectory 'client-startup.log') -Raw
    Assert-Transfer ($downloadedClientLog -match '\[fatal\]' -and $downloadedClientLog -match 'hlclient-offline-missing-driver') 'Download metadata prevented the intended real child startup failure from being captured.'
    Assert-Transfer (Test-Path -LiteralPath $downloadedZip -PathType Leaf) 'Downloaded-executable startup ZIP was not exported.'
    $downloadedArchive=[IO.Compression.ZipFile]::OpenRead($downloadedZip)
    try {
        $downloadedEntries=@($downloadedArchive.Entries | Where-Object { ($_.FullName -split '/')[-1] -ceq 'client-startup.log' })
        Assert-Transfer ($downloadedEntries.Count -eq 1 -and $downloadedEntries[0].Length -gt 0 -and $downloadedEntries[0].Length -le 128KB) 'Downloaded-executable export lacks one bounded child startup log.'
        $downloadedReader=[IO.StreamReader]::new($downloadedEntries[0].Open(),[Text.Encoding]::UTF8)
        try { $downloadedExportedLog=$downloadedReader.ReadToEnd() } finally { $downloadedReader.Dispose() }
        Assert-Transfer ($downloadedExportedLog -ceq $downloadedClientLog) 'Downloaded-executable export discarded or changed the child fatal error.'
    } finally { $downloadedArchive.Dispose() }
    foreach ($fixtureIdentity in $downloadFixtureFiles) {
        Assert-Transfer ((Get-FileHash -LiteralPath $fixtureIdentity.path -Algorithm SHA256).Hash -ceq $fixtureIdentity.sha256) "Downloaded-executable check modified runtime bytes: $($fixtureIdentity.name)"
    }
    foreach ($fixtureName in $downloadExecutables) {
        Assert-Transfer ((Get-Content -LiteralPath (Join-Path $downloadFixture $fixtureName) -Stream Zone.Identifier -Raw) -ceq $downloadZone) "Downloaded-executable check removed or changed download metadata: $fixtureName"
    }
    $downloaded | Add-Member -NotePropertyName startup_check -NotePropertyValue ([pscustomobject]@{ child_exit=$downloadedReceipt.child_exit; diagnostics=$downloadedReceipt.diagnostics; startup_log=$downloadedLogDirectory; export_zip=$downloadedZip; source_file_hashes_retained=$true; marked_executables=$downloadExecutables; zone_identifier_retained=$true; zone_identifier=$downloadZone; verified_fixture_files=$downloadFixtureFiles })
    $startupFailureLogging['downloaded_executable_zone_identifier']='passed'
    [void](Invoke-Case 'packaged_optional_helper_invalid_arguments' $helper @() 2)
    $runId=[guid]::NewGuid().ToString('N')
    $runRoot=Join-Path (Join-Path $local 'HLClientEngine\runs') $runId
    New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
    $helperRun=Start-Owned 'packaged_optional_helper_preclient_partial' $helper @('--await-client','--run-root',$runRoot,'--run-id',$runId)
    $ready=Join-Path $runRoot 'system-helper-ready.marker'
    $deadline=[DateTime]::UtcNow.AddSeconds(20)
    while (!(Test-Path -LiteralPath $ready -PathType Leaf)) {
        Sample-Owned $helperRun
        Assert-Transfer (!$helperRun.process.HasExited) 'Helper exited before its readiness marker.'
        Assert-Transfer ([DateTime]::UtcNow -lt $deadline) 'Helper readiness timeout.'
        Start-Sleep -Milliseconds 20
    }
    $readyValue=Get-Content -LiteralPath $ready -Raw | ConvertFrom-Json
    Assert-Transfer ($readyValue.run_id -ceq $runId) 'Helper readiness identity mismatch.'
    Write-TransferJson (Join-Path $runRoot 'launcher-finished.marker') @{ run_id=$runId; report_publication=$false }
    $helperResult=Finish-Owned $helperRun
    Assert-Transfer ($helperResult.exit_code -eq 0) 'Helper did not finish its controlled preclient partial package.'
    Assert-Transfer (@($helperResult.loaded_modules | Where-Object { $_.path -ieq $helper -and $_.origin -eq 'package' }).Count -eq 1) 'The packaged helper image was not observed loaded.'
    $manifestPath=Join-Path $runRoot 'system-diagnostics\manifest.json'
    $archivePath=Join-Path $runRoot 'system-diagnostics.zip'
    Assert-Transfer (Test-Path -LiteralPath $manifestPath -PathType Leaf) 'Writable helper journal missing.'
    Assert-Transfer (Test-Path -LiteralPath $archivePath -PathType Leaf) 'Helper partial diagnostic ZIP missing.'
    $helperManifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    Assert-Transfer ($helperManifest.run_id -ceq $runId) 'Helper manifest identity mismatch.'
    Assert-Transfer ($helperManifest.sources.wpr.trace_status -eq 'not_started') 'Offline helper unexpectedly started trace capture.'
    $helperResult.status='passed'
    $afterFiles=Get-PackageFiles
    Assert-Transfer (($beforeFiles | ConvertTo-Json -Compress) -ceq ($afterFiles | ConvertTo-Json -Compress)) 'Offline smoke modified package files.'
    $afterSettings=Get-SettingsSnapshot
    Assert-Transfer (($beforeSettings | ConvertTo-Json -Compress) -ceq ($afterSettings | ConvertTo-Json -Compress)) 'Offline smoke modified existing native launcher settings.'
    $complete=$true
} catch {
    $failure=$_.Exception.Message
} finally {
    foreach ($process in $owned) {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
    $report=[ordered]@{
        schema='hlclient.windows-portable-transfer-smoke.v1'; status=$(if($complete){'passed'}else{'failed'}); failure=$failure
        host_os=[Environment]::OSVersion.VersionString; windows10_runtime='not_run'; build_host_test_only=$true
        package_directory=$package; evidence_path=$evidence; scratch_directory=$scratch
        package_bytes_before=$beforeFiles; package_bytes_after=@(Get-PackageFiles)
        environment_overrides=$environmentOverrides; removed_environment_keys=$removedEnvironmentKeys
        clean_profile_mode='native --offline-ui-check bypasses settings load/save; bounded --open-client skips user config; not a newly created Windows account'
        known_folder_environment_redirection='not_claimed; native SHGetKnownFolderPath ignores LOCALAPPDATA override'
        launcher_child_binding='native --offline-startup-check exercises the actual adjacent child with both diagnostics disabled, no saved configuration and an intentionally unavailable video driver; actual UI button connect not exercised'
        helper_binding='packaged adjacent helper executable directly exercised; native launcher button opt-in not exercised'
        startup_failure_logging=$startupFailureLogging
        journals='always-on native startup journal, child failure text and copyable ZIP exercised with expensive diagnostics disabled; helper own run-root under controlled LocalAppData; full gameplay receipts and F8 require later user test'
        clean_machine_dependency_verdict='not_proved_by_dev_host_execution; combine with independent PE/import/app-local dependency audit'
        live_connections=0; steam_initializations=0; owned_servers=0; user_settings_before=$beforeSettings; user_settings_after=(Get-SettingsSnapshot)
        tests=@($cases.ToArray()); retained_scratch=$true
    }
    Write-TransferJson $evidence $report
}
if (!$complete) { throw "Portable transfer smoke failed: $failure. Evidence: $evidence" }
Write-Output "PASS extracted native portable package smoke. Evidence: $evidence"
