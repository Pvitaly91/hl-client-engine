#requires -Version 5.1

<#
.SYNOPSIS
Validates the stock-runtime evidence boundary or an ignored local corpus.

.DESCRIPTION
The default mode is a zero-stock-process, zero-write validation of the honest
evidence-pending state. It does not create the raw artifact root or tracked
evidence JSON.

Corpus mode is read-only. It validates transport metadata, restoration
attestation structure, Git raw-artifact hygiene, and the checker's raw-file
inventory. It independently recomputes selected transport-metadata and raw-file
content hashes. It does not walk runtime grammar, compare decoder cursors, or
promote transport records to stock message evidence.
#>
[CmdletBinding(DefaultParameterSetName = 'Pending')]
param(
    [Parameter(ParameterSetName = 'Pending')]
    [switch]$ValidateEvidencePending,

    [Parameter(Mandatory = $true, ParameterSetName = 'Corpus')]
    [ValidateNotNullOrEmpty()]
    [string]$CaptureRoot,

    [Parameter(ParameterSetName = 'Corpus')]
    [ValidateNotNullOrEmpty()]
    [string]$CheckerPath = '.\build\bin\Debug\hlclient_stock_runtime_check.exe',

    [Parameter(ParameterSetName = 'Corpus')]
    [ValidatePattern('^1\.1\.1\.1$')]
    [string]$ExpectedClientVersion = '1.1.1.1',

    [Parameter(ParameterSetName = 'Corpus')]
    [ValidateRange(1, 255)]
    [int]$ExpectedProtocol = 48,

    [Parameter(ParameterSetName = 'Corpus')]
    [ValidateRange(1, 1000000)]
    [int]$ExpectedServerBuild = 10210,

    [Parameter(ParameterSetName = 'Corpus')]
    [ValidateRange(1, 128)]
    [int]$MinimumAcceptedRuns = 24,

    [Parameter(ParameterSetName = 'Corpus')]
    [ValidateRange(1, 1000000)]
    [int]$MinimumRuntimeUpdates = 1000
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
$manualRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'manual-artifacts')).TrimEnd('\', '/')
$requiredCaptureRoot = [IO.Path]::GetFullPath((Join-Path $manualRoot 'stock-runtime')).TrimEnd('\', '/')
$evidencePath = [IO.Path]::GetFullPath((
    Join-Path $repositoryRoot 'docs\evidence\GOLDSRC_STOCK_RUNTIME_STATE.json'))
$gitIgnorePath = Join-Path $repositoryRoot '.gitignore'
$maximumRuns = 128
$maximumMetadataBytes = 65536
$maximumAttestationBytes = 16384

function Assert-NoReparsePoint {
    param([string]$Path, [string]$Label)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label must not be a reparse point."
    }
}

function Assert-NoReparsePointInExistingPath {
    param([string]$Path, [string]$Label)
    $full = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetPathRoot($full)
    $current = $root
    foreach ($component in @($full.Substring($root.Length) -split '[\\/]' |
            Where-Object { $_ })) {
        $current = [IO.Path]::Combine($current, $component)
        if (Test-Path -LiteralPath $current) {
            Assert-NoReparsePoint $current $Label
        }
    }
}

function Assert-OnlyDefaultDataStream {
    param([string]$Path, [string]$Label)
    $streams = @(Get-Item -LiteralPath $Path -Stream * -ErrorAction Stop)
    if (@($streams | Where-Object { $_.Stream -cne ':$DATA' }).Count -ne 0) {
        throw "$Label contains an alternate data stream."
    }
}

function Assert-PathBelowRoot {
    param([string]$Path, [string]$Root, [string]$Label)
    $pathValue = [IO.Path]::GetFullPath($Path)
    $rootValue = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    if (-not $pathValue.StartsWith(
            $rootValue + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must be a canonical repository descendant."
    }
}

function Assert-NoHardLink {
    param([string]$Path, [string]$Label)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    $property = $item.PSObject.Properties['LinkType']
    if ($null -eq $property -or
        -not [string]::IsNullOrEmpty([string]$property.Value)) {
        throw "$Label must be a regular unlinked file."
    }
}

function Assert-ExactProperties {
    param([object]$Value, [string[]]$Names, [string]$Label)
    if ($null -eq $Value) { throw "$Label is absent." }
    $actual = @($Value.PSObject.Properties | ForEach-Object { $_.Name })
    if ($actual.Count -ne $Names.Count) { throw "$Label property count is not exact." }
    foreach ($name in $Names) {
        if ($actual -cnotcontains $name) { throw "$Label lacks '$name'." }
    }
    foreach ($name in $actual) {
        if ($Names -cnotcontains $name) { throw "$Label contains '$name'." }
    }
}

function Read-BoundedJson {
    param([string]$Path, [int]$MaximumBytes, [string]$Label)
    Assert-NoReparsePointInExistingPath $Path $Label
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Label is absent." }
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.Length -le 0 -or $item.Length -gt $MaximumBytes) {
        throw "$Label byte length is outside its bound."
    }
    Assert-NoReparsePoint $Path $Label
    Assert-OnlyDefaultDataStream $Path $Label
    return Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json -ErrorAction Stop
}

function Get-StrictInteger {
    param([object]$Value, [string]$Name, [Int64]$Minimum, [Int64]$Maximum)
    $property = $Value.PSObject.Properties[$Name]
    if ($null -eq $property -or
        ($property.Value -isnot [int] -and $property.Value -isnot [long])) {
        throw "Property '$Name' must be an integer."
    }
    [Int64]$number = $property.Value
    if ($number -lt $Minimum -or $number -gt $Maximum) {
        throw "Property '$Name' is outside its bound."
    }
    return $number
}

function Get-StrictBoolean {
    param([object]$Value, [string]$Name)
    $property = $Value.PSObject.Properties[$Name]
    if ($null -eq $property -or $property.Value -isnot [bool]) {
        throw "Property '$Name' must be Boolean."
    }
    return [bool]$property.Value
}

function Assert-CaptureMetadata {
    param([object]$Value)
    $names = @(
        'schema', 'profile', 'scenario', 'runtime_result', 'runtime_ready',
        'stock_versions', 'maximum_duration_ms', 'maximum_datagrams',
        'maximum_total_raw_bytes', 'maximum_payload_bytes',
        'maximum_reassembled_bytes', 'maximum_decompressed_bytes',
        'maximum_message_count', 'maximum_runtime_frames',
        'maximum_client_packets', 'maximum_server_packets',
        'observed_datagrams', 'observed_raw_bytes', 'client_packets',
        'server_packets', 'emitted_datagrams', 'emitted_bytes',
        'dropped_datagrams', 'duplicated_datagrams', 'delayed_datagrams',
        'ignored_wrong_source_datagrams', 'perturbation_count',
        'bounded_transport_complete', 'byte_preserving',
        'private_ipv4_loopback', 'one_learned_client_endpoint',
        'one_upstream_socket', 'exact_source_validation', 'payload_rewritten',
        'raw_datagrams_stored', 'accepted_evidence_run')
    Assert-ExactProperties $Value $names 'capture metadata'
    if ($Value.schema -cne 'hlclient.stock-runtime-capture-metadata.v1' -or
        $Value.profile -cne 'stock_protocol_48_build_10210_evidence_pending' -or
        $Value.runtime_result -cne 'evidence_pending' -or
        $Value.runtime_ready -cne 'evidence_pending' -or
        $Value.stock_versions -cne 'not_observed_by_capture_executable') {
        throw 'Capture metadata attempts to promote an unobserved stock profile.'
    }
    if ($Value.scenario -notmatch '^[a-z0-9-]{1,48}$') { throw 'Capture scenario is invalid.' }
    [Int64]$maximumDatagrams = Get-StrictInteger $Value maximum_datagrams 1 65536
    [Int64]$maximumRaw = Get-StrictInteger $Value maximum_total_raw_bytes 1 536870912
    [void](Get-StrictInteger $Value maximum_duration_ms 1 300000)
    [void](Get-StrictInteger $Value maximum_payload_bytes 1 65507)
    [void](Get-StrictInteger $Value maximum_reassembled_bytes 1 67108864)
    [void](Get-StrictInteger $Value maximum_decompressed_bytes 1 268435456)
    [void](Get-StrictInteger $Value maximum_message_count 1 65536)
    [void](Get-StrictInteger $Value maximum_runtime_frames 1 32768)
    [void](Get-StrictInteger $Value maximum_client_packets 1 65536)
    [void](Get-StrictInteger $Value maximum_server_packets 1 65536)
    [Int64]$observed = Get-StrictInteger $Value observed_datagrams 0 $maximumDatagrams
    [void](Get-StrictInteger $Value observed_raw_bytes 0 $maximumRaw)
    [Int64]$client = Get-StrictInteger $Value client_packets 0 $maximumDatagrams
    [Int64]$server = Get-StrictInteger $Value server_packets 0 $maximumDatagrams
    foreach ($field in @('emitted_datagrams', 'emitted_bytes', 'dropped_datagrams',
            'duplicated_datagrams', 'delayed_datagrams',
            'ignored_wrong_source_datagrams', 'perturbation_count')) {
        [void](Get-StrictInteger $Value $field 0 1073741824)
    }
    if ($client + $server -ne $observed) { throw 'Direction counts do not cover ingress.' }
    foreach ($field in @('bounded_transport_complete', 'byte_preserving',
            'private_ipv4_loopback', 'one_learned_client_endpoint',
            'one_upstream_socket', 'exact_source_validation', 'payload_rewritten',
            'raw_datagrams_stored', 'accepted_evidence_run')) {
        [void](Get-StrictBoolean $Value $field)
    }
    if ($Value.byte_preserving -cne $true -or
        $Value.private_ipv4_loopback -cne $true -or
        $Value.one_learned_client_endpoint -cne $true -or
        $Value.one_upstream_socket -cne $true -or
        $Value.exact_source_validation -cne $true -or
        $Value.payload_rewritten -cne $false -or
        $Value.raw_datagrams_stored -cne $true -or
        $Value.accepted_evidence_run -cne $false) {
        throw 'Capture transport policy is invalid.'
    }
}

function Assert-RestorationAttestation {
    param([object]$Value)
    Assert-ExactProperties $Value @(
        'schema', 'external_file_drift', 'snapshot_entry_count',
        'pre_manifest_sha256', 'post_manifest_sha256', 'created_files_removed',
        'owned_processes_stopped', 'input_automation_used',
        'input_events_injected', 'capture_process_exit_code') 'restoration attestation'
    if ($Value.schema -cne 'hlclient.stock-runtime-restoration.v1' -or
        $Value.external_file_drift -cne 'none' -or
        $Value.pre_manifest_sha256 -cnotmatch '^[0-9A-F]{64}$' -or
        $Value.post_manifest_sha256 -cne $Value.pre_manifest_sha256 -or
        $Value.created_files_removed -cne $true -or
        $Value.owned_processes_stopped -cne $true -or
        $Value.input_automation_used -cne $false -or
        (Get-StrictInteger $Value input_events_injected 0 0) -ne 0 -or
        (Get-StrictInteger $Value capture_process_exit_code 0 0) -ne 0) {
        throw 'Restoration attestation is not accepted.'
    }
    [void](Get-StrictInteger $Value snapshot_entry_count 1 200000)
}

function Get-PowerShellTransportMetadataHash {
    param([object]$Metadata)
    $canonical = @(
        $Metadata.schema, $Metadata.profile, $Metadata.scenario,
        $Metadata.observed_datagrams, $Metadata.client_packets,
        $Metadata.server_packets, $Metadata.emitted_datagrams,
        $Metadata.dropped_datagrams, $Metadata.duplicated_datagrams,
        $Metadata.delayed_datagrams, $Metadata.perturbation_count,
        $(if ($Metadata.bounded_transport_complete) { 1 } else { 0 })
    ) -join '|'
    $canonical += '|runtime=evidence_pending|authority=evidence_pending|ack=evidence_pending'
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($canonical)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

function Get-PowerShellRawInventoryHash {
    param([string]$RunRoot, [object]$Metadata)
    $rawRoot = Join-Path $RunRoot 'raw'
    Assert-NoReparsePointInExistingPath $rawRoot 'raw inventory root'
    if (-not (Test-Path -LiteralPath $rawRoot -PathType Container)) {
        throw 'Raw inventory root is absent.'
    }
    Assert-NoReparsePoint $rawRoot 'raw inventory root'
    $items = @(Get-ChildItem -LiteralPath $rawRoot -Force | Sort-Object Name)
    if ($items.Count -ne [Int64]$Metadata.observed_datagrams) {
        throw 'Raw inventory count differs from transport metadata.'
    }
    [Int64]$totalBytes = 0
    $records = @($items | ForEach-Object {
        $item = $_
        if (($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0 -or
            ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            $item.Name -cnotmatch '^[0-9]{8}-(?:c2s|s2c)\.bin$') {
            throw 'Raw inventory contains an unexpected entry.'
        }
        Assert-OnlyDefaultDataStream $item.FullName 'raw datagram'
        Assert-NoHardLink $item.FullName 'raw datagram'
        if ($item.Length -gt [Int64]$Metadata.maximum_payload_bytes -or
            $totalBytes -gt [Int64]$Metadata.maximum_total_raw_bytes - $item.Length) {
            throw 'Raw datagram inventory exceeds capture limits.'
        }
        $totalBytes += $item.Length
        '{0}|{1}|{2}' -f $item.Name, $item.Length,
            (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    })
    if ($totalBytes -ne [Int64]$Metadata.observed_raw_bytes) {
        throw 'Raw inventory byte count differs from transport metadata.'
    }
    $canonical = $records -join "`n"
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($canonical)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

if (-not (Test-Path -LiteralPath $gitIgnorePath -PathType Leaf) -or
    (Get-Content -Raw -LiteralPath $gitIgnorePath) -cnotmatch '(?m)^/manual-artifacts/\s*$') {
    throw 'Repository-wide manual-artifacts ignore rule is absent.'
}
$git = Get-Command git.exe -ErrorAction Stop
$trackedRaw = @(& $git.Source -C $repositoryRoot ls-files -- `
    'manual-artifacts/stock-runtime' 2>$null)
if ($LASTEXITCODE -ne 0) { throw 'Git raw-artifact index check failed.' }
if ($trackedRaw.Count -ne 0) {
    throw 'Raw stock-runtime artifacts are present in the Git index.'
}

if ($PSCmdlet.ParameterSetName -eq 'Pending') {
    if (Test-Path -LiteralPath $evidencePath) {
        throw 'Tracked stock-runtime evidence must remain absent at zero accepted runs.'
    }
    Write-Output '[stock-runtime-verify] accepted-runs=0'
    Write-Output '[stock-runtime-verify] rejected-runs=0'
    Write-Output '[stock-runtime-verify] incomplete-runs=0'
    Write-Output '[stock-runtime-verify] client-version=not-observed'
    Write-Output '[stock-runtime-verify] server-version-protocol-build=not-observed'
    Write-Output '[stock-runtime-verify] research-root=not-supplied-isolation-not-validated'
    Write-Output '[stock-runtime-verify] restoration=not-run'
    Write-Output '[stock-runtime-verify] checker=not-run-no-corpus'
    Write-Output '[stock-runtime-verify] transport-metadata-hash=not-run-no-corpus'
    Write-Output '[stock-runtime-verify] raw-content-hash=not-run-no-corpus'
    Write-Output '[stock-runtime-verify] runtime-decoder-walker=not-implemented'
    Write-Output '[stock-runtime-verify] runtime-ready=evidence_pending'
    Write-Output '[stock-runtime-verify] baseline-entity-clientdata-authority-ack=evidence_pending'
    Write-Output '[stock-runtime-verify] tracked-evidence-created=false'
    Write-Output '[stock-runtime-verify] stock-processes-started=0'
    Write-Output '[stock-runtime-verify] files-written=0'
    Write-Output '[stock-runtime-verify] external-file-drift=none-verifier-read-only'
    Write-Output '[stock-runtime-verify] result=evidence_pending'
    return
}

$root = [IO.Path]::GetFullPath($CaptureRoot).TrimEnd('\', '/')
if ($root -ine $requiredCaptureRoot -or
    -not (Test-Path -LiteralPath $root -PathType Container)) {
    throw 'CaptureRoot must be the exact existing manual-artifacts/stock-runtime root.'
}
Assert-NoReparsePointInExistingPath $root 'capture root'
Assert-NoReparsePoint $root 'capture root'
$checker = [IO.Path]::GetFullPath($CheckerPath)
if (-not (Test-Path -LiteralPath $checker -PathType Leaf) -or
    [IO.Path]::GetFileName($checker) -cne 'hlclient_stock_runtime_check.exe') {
    throw 'CheckerPath must name hlclient_stock_runtime_check.exe.'
}
Assert-NoReparsePointInExistingPath $checker 'stock runtime checker'
Assert-PathBelowRoot $checker $repositoryRoot 'stock runtime checker'
Assert-OnlyDefaultDataStream $checker 'stock runtime checker'
Assert-NoHardLink $checker 'stock runtime checker'

$directories = @(Get-ChildItem -LiteralPath $root -Force -Directory | Sort-Object Name)
if ($directories.Count -gt $maximumRuns) { throw 'Capture corpus exceeds its run bound.' }
$accepted = 0
$rejected = 0
$incomplete = 0
$runtimeUpdates = 0
$metadataHashAgreements = 0
$rawHashAgreements = 0
foreach ($directory in $directories) {
    if ($directory.Name -cnotmatch '^[0-9a-f]{32}$') { $rejected++; continue }
    try {
        Assert-NoReparsePoint $directory.FullName 'capture run'
        $metadata = Read-BoundedJson (Join-Path $directory.FullName 'capture-metadata.json') `
            $maximumMetadataBytes 'capture metadata'
        Assert-CaptureMetadata $metadata
        if (-not $metadata.bounded_transport_complete) {
            $incomplete++
            continue
        }
        $attestationPath = Join-Path $directory.FullName 'research-restoration-attestation.json'
        if (-not (Test-Path -LiteralPath $attestationPath -PathType Leaf)) {
            $incomplete++
            continue
        }
        $attestation = Read-BoundedJson $attestationPath $maximumAttestationBytes `
            'restoration attestation'
        Assert-RestorationAttestation $attestation
        $arguments = @('--capture-root', $directory.FullName, '--scenario', 'transcript')
        $checkerOutput = @(& $checker @arguments 2>&1 | ForEach-Object { $_.ToString() })
        if ($LASTEXITCODE -ne 0 -or
            $checkerOutput -notcontains '[stock-runtime] result=evidence_pending') {
            throw 'Production checker did not retain the pending boundary.'
        }
        $hashLines = @($checkerOutput | Where-Object {
            $_.StartsWith('[stock-runtime] structural-hash=')
        })
        if ($hashLines.Count -ne 1) { throw 'Checker structural hash is absent.' }
        $checkerHash = $hashLines[0].Substring('[stock-runtime] structural-hash='.Length)
        if ($checkerHash -cne (Get-PowerShellTransportMetadataHash $metadata)) {
            throw 'Selected transport-metadata hashes disagree.'
        }
        $metadataHashAgreements++
        $rawHashLines = @($checkerOutput | Where-Object {
            $_.StartsWith('[stock-runtime] raw-inventory-hash=')
        })
        if ($rawHashLines.Count -ne 1) {
            throw 'Checker raw-inventory hash is absent.'
        }
        $checkerRawHash = $rawHashLines[0].Substring(
            '[stock-runtime] raw-inventory-hash='.Length)
        if ($checkerRawHash -cne
            (Get-PowerShellRawInventoryHash $directory.FullName $metadata)) {
            throw 'Raw-file content hashes disagree.'
        }
        $rawHashAgreements++

        # Transport-only metadata deliberately has no observed app build,
        # server engine/protocol/build, exact runtime updates, or grammar. It
        # remains incomplete and cannot satisfy an evidence threshold.
        $incomplete++
    } catch {
        $rejected++
    }
}

Write-Output "[stock-runtime-verify] accepted-runs=$accepted"
Write-Output "[stock-runtime-verify] rejected-runs=$rejected"
Write-Output "[stock-runtime-verify] incomplete-runs=$incomplete"
Write-Output "[stock-runtime-verify] runtime-updates=$runtimeUpdates"
Write-Output "[stock-runtime-verify] transport-metadata-hash-agreements=$metadataHashAgreements"
Write-Output "[stock-runtime-verify] raw-content-hash-agreements=$rawHashAgreements"
Write-Output '[stock-runtime-verify] runtime-decoder-walker-agreements=0-not-implemented'
Write-Output "[stock-runtime-verify] expected-client-version=${ExpectedClientVersion}-not-observed"
Write-Output "[stock-runtime-verify] expected-protocol=${ExpectedProtocol}-not-observed"
Write-Output "[stock-runtime-verify] expected-server-build=${ExpectedServerBuild}-not-observed"
Write-Output '[stock-runtime-verify] tracked-evidence-created=false'
Write-Output '[stock-runtime-verify] result=evidence_pending-threshold-not-met'

if ($accepted -lt $MinimumAcceptedRuns -or $runtimeUpdates -lt $MinimumRuntimeUpdates) {
    throw 'Stock runtime evidence threshold is not met.'
}
throw 'Internal error: transport-only pending metadata must never satisfy stock evidence gates.'
