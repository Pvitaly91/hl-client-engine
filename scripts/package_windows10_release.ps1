#requires -Version 7.0
param(
    [Parameter(Mandatory = $true)][string]$ReleaseDirectory,
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [string]$AcceptedRunDirectory,
    [switch]$LauncherRebuilt,
    [string]$LauncherChangeDescription,
    [switch]$ClientRebuilt,
    [string]$ClientChangeDescription,
    [ValidatePattern('^[A-Fa-f0-9]{64}$')]
    [string]$BaseClientSha256,
    [ValidatePattern('^[A-Fa-f0-9]{64}$')]
    [string]$ExpectedClientSha256 = 'E044684FE00E688F555988478E83E7DEF037EC35844C05F19E7B540C45CAA32C'
)

# Developer-side packaging only. The resulting package needs no PowerShell.
# Never builds, edits delivery/configuration, downloads files, or connects to a server.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$release = [IO.Path]::GetFullPath($ReleaseDirectory).TrimEnd('\')
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $repository 'build' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repository 'dist' }
$build = [IO.Path]::GetFullPath($BuildDirectory)
$output = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
$utf8 = [Text.UTF8Encoding]::new($false)
$programNames = @('HLClientLauncher.exe', 'hlclient.exe', 'SDL3.dll', 'HLClientSystemDiagnostics.exe')
if ($LauncherRebuilt -and [string]::IsNullOrWhiteSpace($LauncherChangeDescription)) {
    throw 'Describe the rebuilt launcher with -LauncherChangeDescription; accepted client identity remains mandatory.'
}
if ($ClientRebuilt) {
    if (-not $PSBoundParameters.ContainsKey('ExpectedClientSha256') -or
        [string]::IsNullOrWhiteSpace($ClientChangeDescription) -or
        [string]::IsNullOrWhiteSpace($BaseClientSha256)) {
        throw 'A rebuilt client requires explicit -ExpectedClientSha256, -BaseClientSha256 and -ClientChangeDescription.'
    }
    if ($BaseClientSha256 -ieq $ExpectedClientSha256) {
        throw 'A rebuilt client must differ from its recorded base client identity.'
    }
} elseif ($PSBoundParameters.ContainsKey('BaseClientSha256') -or
          $PSBoundParameters.ContainsKey('ClientChangeDescription')) {
    throw 'Client base/change metadata requires -ClientRebuilt.'
}

function Write-Json([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, (($Value | ConvertTo-Json -Depth 60) + "`n"), $utf8)
}
function Get-Identity([string]$Path) {
    $file = Get-Item -LiteralPath $Path -ErrorAction Stop
    if ($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Expected an ordinary file, not directory/reparse point: $Path"
    }
    [pscustomobject]@{ name = $file.Name; size = $file.Length; sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
}
function Copy-Exact([string]$Source, [string]$Destination) {
    $before = Get-Identity $Source
    if (Test-Path -LiteralPath $Destination) { throw "Refusing to overwrite staging file: $Destination" }
    Copy-Item -LiteralPath $Source -Destination $Destination
    $after = Get-Identity $Destination
    if ($before.sha256 -ne $after.sha256 -or $before.size -ne $after.size) { throw "Copy mismatch: $Source" }
}
function Get-RelativeFiles([string]$Root) {
    @(Get-ChildItem -LiteralPath $Root -Recurse -File | ForEach-Object {
        if ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point in package: $($_.FullName)" }
        $_.FullName.Substring($Root.Length + 1).Replace('\', '/')
    } | Sort-Object)
}
function Assert-Allowlist([string]$Root, [string[]]$Expected) {
    $actual = @(Get-RelativeFiles $Root)
    $difference = @(Compare-Object -ReferenceObject ($Expected | Sort-Object) -DifferenceObject $actual)
    if ($difference.Count) { throw "Package allowlist mismatch: $($difference | Out-String)" }
}
function Write-Checksums([string]$Root) {
    $lines = @(Get-RelativeFiles $Root | Where-Object { $_ -ne 'SHA256SUMS.txt' } | ForEach-Object {
        '{0}  {1}' -f (Get-FileHash -LiteralPath (Join-Path $Root $_) -Algorithm SHA256).Hash, $_
    })
    [IO.File]::WriteAllText((Join-Path $Root 'SHA256SUMS.txt'), (($lines -join "`n") + "`n"), $utf8)
}
function Verify-Checksums([string]$Root, [string[]]$Expected) {
    Assert-Allowlist $Root $Expected
    $seen = @{}
    foreach ($line in (Get-Content -LiteralPath (Join-Path $Root 'SHA256SUMS.txt'))) {
        if ($line -notmatch '^([A-F0-9]{64})  ([A-Za-z0-9_./-]+)$') { throw 'Malformed package checksum line.' }
        $hash = $Matches[1]; $relative = $Matches[2]
        if ($relative.Contains('..') -or $relative.StartsWith('/') -or $seen.ContainsKey($relative)) {
            throw "Unsafe/duplicate checksum entry: $relative"
        }
        $seen[$relative] = $true
        if ((Get-FileHash -LiteralPath (Join-Path $Root $relative) -Algorithm SHA256).Hash -ne $hash) {
            throw "Extracted checksum mismatch: $relative"
        }
    }
    if (@(Compare-Object ($Expected | Where-Object { $_ -ne 'SHA256SUMS.txt' } | Sort-Object) @($seen.Keys | Sort-Object)).Count) {
        throw 'Checksum file does not cover every payload file.'
    }
}
function New-PackageZip([string]$Root, [string]$Archive) {
    if (Test-Path -LiteralPath $Archive) { throw "Refusing to overwrite archive: $Archive" }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $stream = [IO.File]::Open($Archive, [IO.FileMode]::CreateNew)
    $zip = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create, $false)
    try {
        foreach ($name in (Get-RelativeFiles $Root)) {
            $entry = $zip.CreateEntry(('HLClientEngine/' + $name), [IO.Compression.CompressionLevel]::Optimal)
            # Stable ZIP metadata: input identities, not filesystem write times, govern provenance.
            $entry.LastWriteTime = [DateTimeOffset]::new(2026, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
            $inputStream = [IO.File]::OpenRead((Join-Path $Root $name))
            $entryStream = $entry.Open()
            try { $inputStream.CopyTo($entryStream) }
            finally { $entryStream.Dispose(); $inputStream.Dispose() }
        }
    }
    finally { $zip.Dispose(); $stream.Dispose() }
}
function Extract-Verified([string]$Archive, [string]$Destination, [string[]]$Expected) {
    if (Test-Path -LiteralPath $Destination) { throw 'Extraction destination must be fresh.' }
    [IO.Compression.ZipFile]::ExtractToDirectory($Archive, $Destination)
    $children = @(Get-ChildItem -LiteralPath $Destination -Force)
    if ($children.Count -ne 1 -or $children[0].Name -ne 'HLClientEngine' -or -not $children[0].PSIsContainer) {
        throw 'Unexpected ZIP top-level layout.'
    }
    $root = Join-Path $Destination 'HLClientEngine'
    Verify-Checksums $root $Expected
    return $root
}

if (-not (Test-Path -LiteralPath $release -PathType Container)) { throw 'Explicit input Release directory is missing.' }
if ($output.Equals($release, [StringComparison]::OrdinalIgnoreCase) -or
    $output.StartsWith($release + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Output must not be inside the input Release directory.'
}
$identities = @($programNames | ForEach-Object { Get-Identity (Join-Path $release $_) })
$client = @($identities | Where-Object name -eq 'hlclient.exe')[0]
if ($client.sha256 -ine $ExpectedClientSha256) { throw "Client does not match the explicitly expected identity: $($client.sha256)" }
$head = (& git -C $repository rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot establish source HEAD.' }
$branch = (& git -C $repository branch --show-current).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot establish source branch.' }
$gitStatus = @(& git -C $repository status --porcelain=v1 --untracked-files=all --ignore-submodules=all)
if ($LASTEXITCODE -ne 0) { throw 'Cannot establish source working-tree status.' }
& git -C $repository diff --check --ignore-submodules=all
if ($LASTEXITCODE -ne 0) { throw 'Source diff --check failed; no packaging changes committed.' }
$cache = Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt') -Raw
if ($cache -notmatch '(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$') { throw 'Build source binding missing.' }
$configuredSource = [IO.Path]::GetFullPath($Matches[1].Trim()).TrimEnd('\')
if (-not $configuredSource.Equals($repository, [StringComparison]::OrdinalIgnoreCase)) { throw 'Build tree belongs to a different source checkout.' }
if ($cache -notmatch '(?m)^CMAKE_GENERATOR_PLATFORM:INTERNAL=Win32\s*$') { throw 'Expected the existing Win32 build; no architecture conversion is allowed.' }

$acceptance = [ordered]@{
    client_sha256 = $client.sha256; supplied_expected_sha256 = $ExpectedClientSha256.ToUpperInvariant()
    status = 'user_supplied_identity_match'; run_id = $null
    scope = 'Previous PC gameplay only; no new Windows 10 or gameplay acceptance is implied.'
}
if ($ClientRebuilt) {
    $acceptance.status = 'rebuilt_candidate_identity_verified_runtime_pending'
    $acceptance.base_client_sha256 = $BaseClientSha256.ToUpperInvariant()
    $acceptance.scope = 'New client candidate. Earlier base-client runs do not validate this binary; Windows 10 and gameplay require new evidence.'
}
if ($AcceptedRunDirectory) {
    $accepted = [IO.Path]::GetFullPath($AcceptedRunDirectory).TrimEnd('\')
    if ([IO.Path]::GetFileName($accepted) -notmatch '^[a-fA-F0-9]{32}$') { throw 'Accepted run directory must have a 32-hex run ID.' }
    # Only identity/terminal facts are read. Never copy the report, endpoint, auth or account data.
    $launch = Get-Content -LiteralPath (Join-Path $accepted 'launch-manifest.json') -Raw | ConvertFrom-Json
    $report = Get-Content -LiteralPath (Join-Path $accepted 'external-run-report.json') -Raw | ConvertFrom-Json
    $acceptedLauncher = @($identities | Where-Object name -eq 'HLClientLauncher.exe')[0]
    $acceptedId = [IO.Path]::GetFileName($accepted)
    if ($launch.schema -ne 'hlclient.external-launch-identity.v1' -or $report.schema -ne 'hlclient.external-run.v1' -or
        $launch.run_id -ne $acceptedId -or $report.run_id -ne $acceptedId) { throw 'Accepted run schema/identity binding is invalid.' }
    if ($launch.client_sha256 -ine $client.sha256 -or $report.client_sha256 -ine $client.sha256 -or
        $launch.launcher_sha256 -ine $report.launcher_sha256) {
        throw 'Accepted run client or internal launcher identity binding is inconsistent.'
    }
    if ($report.client_exit -ne 0 -or $report.terminal_reason -ne 'application_closed' -or -not $report.gameplay_observed) {
        throw 'Accepted run does not establish a normal gameplay close.'
    }
    $acceptance.status = 'user_positive_run_identity_and_normal_close_verified'
    $acceptance.run_id = [IO.Path]::GetFileName($accepted)
    $acceptance.accepted_run_launcher_sha256 = $launch.launcher_sha256
    $acceptance.packaged_launcher_sha256 = $acceptedLauncher.sha256
    $acceptance.launcher_changed_since_accepted_run = $launch.launcher_sha256 -ine $acceptedLauncher.sha256
    if ($acceptance.launcher_changed_since_accepted_run) {
        $acceptance.status = 'user_positive_client_run_verified_packaged_launcher_changed'
        $acceptance.launcher_acceptance = 'Current launcher receives offline validation only; the earlier gameplay run does not validate this new launcher.'
    } else {
        $acceptance.launcher_acceptance = 'Launcher identity matches the accepted earlier run.'
    }
    $acceptance.client_exit_code = 0
    $acceptance.terminal_reason = 'application_closed'
    $acceptance.gameplay_observed = $true
}

$id = $client.sha256.Substring(0, 12).ToLowerInvariant() + '-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 6)
$name = 'HLClientEngine-Win10'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$archive = Join-Path $output ($name + '.zip')
$publishedDirectory = Join-Path $output 'HLClientEngine'
if ((Test-Path -LiteralPath $archive) -or (Test-Path -LiteralPath $publishedDirectory)) {
    throw 'Canonical package already exists. Retain it in the history folder before publishing the next verified package.'
}
$work = Join-Path $output ('.' + $name + '-' + $id + '.packaging')
New-Item -ItemType Directory -Path $work | Out-Null
$auditPath = Join-Path $work 'dependency-audit.json'
& (Join-Path $PSScriptRoot 'audit_windows_portable_dependencies.ps1') -ReleaseDirectory $release -BuildDirectory $build -OutputPath $auditPath -ExpectedClientSha256 $ExpectedClientSha256 | Out-Null
$audit = Get-Content -LiteralPath $auditPath -Raw | ConvertFrom-Json
if ($audit.status -ne 'passed') { throw 'Dependency audit did not pass.' }
$symbolResults = @($audit.files | Where-Object origin -eq 'explicit_release_directory' | ForEach-Object {
    $symbolRoot = Join-Path $work ('symbols\' + $_.sha256)
    New-Item -ItemType Directory -Path $symbolRoot -Force | Out-Null
    $hasCodeView = @($_.pe.debug_identities | Where-Object type -eq 2).Count -gt 0
    if ($hasCodeView) { throw "Matching PDB preservation needs explicit review for $($_.name); this transfer script will not guess." }
    $symbol = [ordered]@{ binary = $_.name; sha256 = $_.sha256; status = 'no_codeview_pdb_identity'; pdb_preserved = $false; note = 'Input PE has no CodeView/type2 PDB identity. No matching Release PDB can be established; existing Debug PDBs are not substituted. Input binary is copied unchanged.' }
    Write-Json (Join-Path $symbolRoot 'SYMBOL_STATUS.json') $symbol
    $symbol
})
$stage = Join-Path $work 'HLClientEngine'
New-Item -ItemType Directory -Path $stage | Out-Null
foreach ($identity in $identities) { Copy-Exact (Join-Path $release $identity.name) (Join-Path $stage $identity.name) }
$runtimeNames = @()
foreach ($runtime in $audit.required_app_local_runtime_files) {
    if ($runtime.name -notmatch '^(msvcp140(_atomic_wait|_[12])?|vcruntime140(_1)?)\.dll$') {
        throw "Runtime not in permitted Release CRT allowlist: $($runtime.name)"
    }
    if ((Get-Identity $runtime.source).sha256 -ne $runtime.sha256) { throw 'Runtime changed after dependency audit.' }
    Copy-Exact $runtime.source (Join-Path $stage $runtime.name)
    $runtimeNames += $runtime.name
}
$documents = @('START_HERE_UK.txt', 'THIRD_PARTY_NOTICES.txt')
foreach ($doc in $documents) { Copy-Exact (Join-Path $repository ('packaging\windows10\' + $doc)) (Join-Path $stage $doc) }
if ($ClientRebuilt) {
    # Generate candidate-specific provenance without changing the accepted-base
    # template or inventing manual acceptance of a newly compiled executable.
    $readmePath = Join-Path $stage 'START_HERE_UK.txt'
    $readme = [IO.File]::ReadAllText($readmePath)
    $provenancePattern = '(?s)Клієнт гри збережено без перебудови від прийнятої ігрової версії:.*?(?=Точні хеші, залежності)'
    if (-not [regex]::IsMatch($readme, $provenancePattern)) { throw 'Portable README provenance section was not found.' }
    $candidateNote = "Клієнт гри перебудовано як новий кандидат для перевірки.`nSHA-256 $($client.sha256).`nПопередній клієнт: $($BaseClientSha256.ToUpperInvariant()).`nПопередні ігрові тести не підтверджують приймання цього нового EXE.`n"
    $readme = [regex]::Replace($readme, $provenancePattern, $candidateNote)
    $readme = [regex]::Replace($readme,
        'Для першого тесту\r?\n   слабшого ПК рекомендується явно вибрати «Згладжування: Вимкнено»\.\r?\n   MSAA 2x/4x/8x можна перевірити після успішного запуску\.',
        'Для перевірки сумісності збережіть власні налаштування MSAA та VSync.')
    [IO.File]::WriteAllText($readmePath, $readme, $utf8)
}
$licenses = [ordered]@{
    'HL-Client-engine.txt' = (Join-Path $repository 'LICENSE')
    'SDL3.txt' = (Join-Path $build '_deps\sdl3-src\LICENSE.txt')
    'bzip2.txt' = (Join-Path $build '_deps\bzip2-src\LICENSE')
    'GLAD.txt' = (Join-Path $repository 'third_party\glad\LICENSE')
}
New-Item -ItemType Directory -Path (Join-Path $stage 'LICENSES') | Out-Null
foreach ($license in $licenses.GetEnumerator()) { Copy-Exact $license.Value (Join-Path $stage ('LICENSES\' + $license.Key)) }
$expected = @($programNames + $runtimeNames + $documents + @('BUILD_INFO.json', 'SHA256SUMS.txt') + @($licenses.Keys | ForEach-Object { 'LICENSES/' + $_ }))
$files = @(Get-RelativeFiles $stage | ForEach-Object {
    $entry = Get-Identity (Join-Path $stage $_)
    [ordered]@{ path = $_; bytes = $entry.size; sha256 = $entry.sha256 }
})
$info = [ordered]@{
    schema = 'hlclient-windows10-portable-release-v1'; package_id = $name + '-' + $id
    packaged_utc = [DateTime]::UtcNow.ToString('o'); binaries_rebuilt = ($LauncherRebuilt.IsPresent -or $ClientRebuilt.IsPresent)
    build_changes = [ordered]@{ client_rebuilt = $ClientRebuilt.IsPresent; client_preserved_byte_for_byte = (-not $ClientRebuilt.IsPresent); client_base_sha256 = $(if ($ClientRebuilt) { $BaseClientSha256.ToUpperInvariant() } else { $client.sha256 }); client_change_description = $ClientChangeDescription; launcher_rebuilt = $LauncherRebuilt.IsPresent; launcher_change_description = $LauncherChangeDescription; packaging_script_builds = 0 }
    source = [ordered]@{ repository = 'Pvitaly91/hl-client-engine'; head = $head; branch = $branch; dirty = ($gitStatus.Count -gt 0); status = $gitStatus; submodule_worktree_status = 'not_inspected; SDK reference is not packaged'; build_source_binding_verified = $true; note = 'Working-tree provenance, not a reproducible clean-HEAD build claim. Existing local edits preserved.' }
    accepted_base = $acceptance
    input = [ordered]@{ release_directory = $release; files = $identities; all_copied_byte_for_byte = $true; sdl = 'Exact companion from the named delivery directory. Accepted-run identity files bind client/launcher, not the SDL hash.' }
    architecture = 'x86 (Win32 PE32); not x64'; files = $files
    inventory_note = 'BUILD_INFO.json and SHA256SUMS.txt excluded from this embedded inventory to avoid self-reference; SHA256SUMS covers BUILD_INFO and every other payload file.'
    dependencies = $audit
    external = @('Installed licensed Half-Life with its game resources and official trusted x86 steam_api.dll', 'Running Steam and valid ownership/authentication', 'Windows and vendor OpenGL driver; not included')
    runtime_installation_required = $false
    resources = 'No external project shader/UI/config assets required. Shaders/UI code embedded; Valve console/map/model/sprite resources read from the explicitly selected Half-Life installation.'
    configuration = [ordered]@{ entrypoint = 'HLClientLauncher.exe'; launchers = 1; copied_settings = $false; writable_location = '%LOCALAPPDATA%\HLClientEngine'; runs = '%LOCALAPPDATA%\HLClientEngine\runs'; manual_resource_browse_available = $true; no_autoconnect = $true; diagnostics = 'Gameplay and CPU/GPU recording are optional checkboxes in the same native launcher. Basic startup failure logs remain available with both checkboxes disabled.'; startup_logs = '%LOCALAPPDATA%\HLClientEngine\startup; fallback %TEMP%\HLClientEngine\startup'; startup_log_export = 'Native launcher: Зберегти ZIP логів…' }
    symbols = [ordered]@{ status = 'matching_pdb_unavailable_not_packaged'; binaries = $symbolResults; note = 'Local symbol-status records retained separately by binary SHA. Debug PDBs are not substitutes and are never shipped.' }
    checks = [ordered]@{ dependency_audit = 'passed'; accepted_client_binary_identity = $(if ($ClientRebuilt -and -not $AcceptedRunDirectory) { 'not_applicable_rebuilt_candidate' } else { 'passed' }); supplied_client_binary_identity = 'passed'; initial_extracted_hashes = 'pending'; clean_folder_smoke = 'pending'; final_archive_hash_verification = 'required_before_packager_returns_ready'; windows10_runtime = 'not_run'; live_connections = 0; steam_auth_tests = 0; packaging_script_builds = 0; packaging_script_commits = 0; packaging_script_pushes = 0 }
}
Write-Json (Join-Path $stage 'BUILD_INFO.json') $info
Write-Checksums $stage
Assert-Allowlist $stage $expected
$candidateZip = Join-Path $work 'offline-smoke-input.zip'
New-PackageZip $stage $candidateZip
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('HLC transfer Unicode ' + [char]0x0422 + [char]0x0435 + [char]0x0441 + [char]0x0442 + ' ' + [guid]::NewGuid().ToString('N'))
$resolvedScratch = [IO.Path]::GetFullPath($scratch)
if ($resolvedScratch.StartsWith($repository + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Smoke extraction must be outside the repository.' }
New-Item -ItemType Directory -Path $scratch | Out-Null
$extracted = Extract-Verified $candidateZip (Join-Path $scratch 'first extraction') $expected
$smokePath = Join-Path $work 'clean-folder-smoke.json'
& (Join-Path $PSScriptRoot 'test_windows_portable_transfer.ps1') -PackageDirectory $extracted -EvidencePath $smokePath | Out-Null
$smoke = Get-Content -LiteralPath $smokePath -Raw | ConvertFrom-Json
if ($smoke.status -ne 'passed') { throw 'Extracted-package offline smoke failed; no ready archive produced.' }
$helperChecks = @(& (Join-Path $PSScriptRoot 'test_system_diagnostics.ps1') -HelperExecutable (Join-Path $extracted 'HLClientSystemDiagnostics.exe') -ScratchParent $scratch)
$helperReceipt = @($helperChecks | Where-Object { $_ -match '^PASS system-diagnostics offline fake-child checks=(\d+) live_game=0$' })
if ($helperReceipt.Count -ne 1 -or $helperReceipt[0] -notmatch 'checks=(\d+)') { throw 'Packaged helper fake-child checks did not pass.' }
$helperCheckCount = [int]$Matches[1]
[IO.File]::WriteAllText((Join-Path $work 'helper-fake-child-tests.txt'), (($helperChecks -join "`n") + "`n"), $utf8)
$info.checks.initial_extracted_hashes = 'passed'
$info.checks.clean_folder_smoke = 'passed'
$info.checks.packaged_helper_fake_child = [ordered]@{ status = 'passed'; assertions = $helperCheckCount; normal_close = $true; child_failure = $true; cancellation = $true; pid_identity_mismatch_rejected = $true; ready_before_client = $true; preclient_failure_partial = $true; live_game = 0 }
$info.checks.smoke_evidence = [ordered]@{
    host_os = $smoke.host_os; windows10_runtime = $smoke.windows10_runtime
    clean_profile_mode = $smoke.clean_profile_mode
    known_folder_environment_redirection = $smoke.known_folder_environment_redirection
    launcher_child_binding = $smoke.launcher_child_binding; helper_binding = $smoke.helper_binding
    startup_failure_logging = $smoke.startup_failure_logging
    journals = $smoke.journals; clean_machine_dependency_verdict = $smoke.clean_machine_dependency_verdict
    system_only_path = $true; package_files_unchanged = $true; existing_user_settings_unchanged = $true
    tests = @($smoke.tests | ForEach-Object {
        [ordered]@{ name = $_.name; status = $_.status; exit_code = $_.exit_code
            observed_package_modules = @($_.loaded_modules | Where-Object origin -eq 'package' | ForEach-Object { [IO.Path]::GetFileName($_.path) })
            outside_package_and_windows_modules = @($_.loaded_modules | Where-Object origin -eq 'outside_package_and_windows').Count }
    })
    full_evidence_shipped = $false
}
Write-Json (Join-Path $stage 'BUILD_INFO.json') $info
Write-Checksums $stage
New-PackageZip $stage $archive
$finalExtracted = Extract-Verified $archive (Join-Path $scratch 'final extraction') $expected
# Audit-time/source identity checks do not license concurrent input changes.
foreach ($identity in $identities) {
    if ((Get-Identity (Join-Path $release $identity.name)).sha256 -ne $identity.sha256 -or
        (Get-Identity (Join-Path $finalExtracted $identity.name)).sha256 -ne $identity.sha256) {
        throw "Input/extracted binary changed during packaging: $($identity.name)"
    }
}
New-Item -ItemType Directory -Path $publishedDirectory | Out-Null
foreach ($relative in $expected) {
    $destination = Join-Path $publishedDirectory $relative
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
    Copy-Exact (Join-Path $stage $relative) $destination
}
Verify-Checksums $publishedDirectory $expected
$result = [ordered]@{
    schema = 'hlclient-windows10-portable-packaging-receipt-v1'; status = 'ready'
    archive = $archive; bytes = (Get-Item -LiteralPath $archive).Length
    sha256 = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    package_directory = $publishedDirectory; staging_directory = $stage; extracted_directory = $finalExtracted
    clean_folder_smoke = 'passed'; final_extracted_allowlist_and_hashes = 'passed'
    windows10_runtime = 'not_run'; binaries_rebuilt = ($LauncherRebuilt.IsPresent -or $ClientRebuilt.IsPresent); client_rebuilt = $ClientRebuilt.IsPresent; unchanged_inputs = $true
    audit = $auditPath; smoke_evidence = $smokePath; file_count = $expected.Count
    limitations = @('Offline smoke on current development OS is not a clean Windows 10 runtime test.', 'Native launcher offline UI bypasses saved settings; not a newly created Windows account.', 'No gameplay/Steam authentication/server connections were performed.')
}
Write-Json (Join-Path $work 'packaging-receipt.json') $result
[pscustomobject]$result
