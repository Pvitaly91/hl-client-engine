#requires -Version 5.1

<#
.SYNOPSIS
Validates isolated stock entity-snapshot research and sanitized evidence.

.DESCRIPTION
The default mode is intentionally useful while active stock evidence is
pending: it verifies that no tracked stock-evidence file has been fabricated.

ValidateResearchRoot performs a read-only, fail-closed preflight of an
explicit isolated Half-Life copy. The guided `-Game valve -Map <map>` form is
also a read-only preflight while the reviewed capture harness and per-run typed
observation trace are absent. ProjectAcceptedCaptures and tracked projection
validation remain deliberately disabled until that trace can prove scenario
semantics rather than trusting self-declared labels. Raw packet/message bodies,
authentication material, identity data, endpoints, and game values are never
projected.

InvokeCaptureHarness is a future-facing guarded wrapper for an independently
reviewed capture harness. The harness executable must be a canonical repository
descendant and its SHA-256 must be supplied. The wrapper snapshots the isolated
research tree, backs up mutable client/server state, and restores that state in
an unconditional finally path. It never launches a stock binary directly.

.PARAMETER ValidateEvidencePending
Validate the current zero-active-capture state without writing any file. This
is the default parameter set.

.PARAMETER ValidateResearchRoot
Validate an isolated research root without starting any process.

.PARAMETER ResearchHalfLifeRoot
Explicit isolated Half-Life root containing the exact isolation marker.

.PARAMETER ClientPath
Explicit path to the isolated stock client. It must resolve canonically to
ResearchHalfLifeRoot\hl.exe; a different descendant is rejected.

.PARAMETER HldsPath
Explicit path to the isolated stock server. It must resolve canonically to
ResearchHalfLifeRoot\hlds.exe; a different descendant is rejected.

.PARAMETER ProjectAcceptedCaptures
Reserved and disabled until a reviewed capture harness publishes per-run typed
scenario observations.

.PARAMETER ValidateMetadataPath
Reserved and disabled while tracked projection promotion is evidence-pending.

.PARAMETER InvokeCaptureHarness
Run a hash-pinned repository-local capture harness under restoration guards.
This mode is not required for validation-only evidence-pending use.

.EXAMPLE
.\scripts\verify_stock_entity_snapshots.ps1 -ValidateEvidencePending

.EXAMPLE
.\scripts\verify_stock_entity_snapshots.ps1 -ValidateResearchRoot `
  -ResearchHalfLifeRoot C:\research\Half-Life-isolated `
  -ClientPath C:\research\Half-Life-isolated\hl.exe `
  -HldsPath C:\research\Half-Life-isolated\hlds.exe

.EXAMPLE
.\scripts\verify_stock_entity_snapshots.ps1 `
  -ResearchHalfLifeRoot C:\research\Half-Life-isolated `
  -ClientPath C:\research\Half-Life-isolated\hl.exe `
  -HldsPath C:\research\Half-Life-isolated\hlds.exe `
  -Game valve -Map boot_camp

.EXAMPLE
.\scripts\verify_stock_entity_snapshots.ps1 -InvokeCaptureHarness `
  -ResearchHalfLifeRoot C:\research\Half-Life-isolated `
  -ClientPath C:\research\Half-Life-isolated\hl.exe `
  -HldsPath C:\research\Half-Life-isolated\hlds.exe `
  -CaptureHarnessPath .\build\hlclient_stock_entity_snapshot_capture.exe `
  -CaptureHarnessSha256 $reviewedHarnessSha256 -RunId $randomRunSha256 `
  -Scenario baseline-stationary
#>

[CmdletBinding(DefaultParameterSetName = 'Pending')]
param(
    [Parameter(ParameterSetName = 'Pending')]
    [switch]$ValidateEvidencePending,

    [Parameter(Mandatory = $true, ParameterSetName = 'Isolation')]
    [switch]$ValidateResearchRoot,

    [Parameter(Mandatory = $true, ParameterSetName = 'Project')]
    [switch]$ProjectAcceptedCaptures,

    [Parameter(Mandatory = $true, ParameterSetName = 'Validate')]
    [ValidateNotNullOrEmpty()]
    [string]$ValidateMetadataPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [switch]$InvokeCaptureHarness,

    [Parameter(Mandatory = $true, ParameterSetName = 'Isolation')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Guided')]
    [ValidateNotNullOrEmpty()]
    [string]$ResearchHalfLifeRoot,

    [Parameter(Mandatory = $true, ParameterSetName = 'Isolation')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Guided')]
    [ValidateNotNullOrEmpty()]
    [string]$ClientPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'Isolation')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Guided')]
    [ValidateNotNullOrEmpty()]
    [string]$HldsPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'Guided')]
    [ValidateSet('valve')]
    [string]$Game,

    [Parameter(Mandatory = $true, ParameterSetName = 'Guided')]
    [ValidateSet('boot_camp', 'crossfire', 'stalkyard')]
    [string]$Map,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidateNotNullOrEmpty()]
    [string]$CaptureHarnessPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$CaptureHarnessSha256,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$RunId,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidateSet(
        'baseline-stationary', 'map-boot-camp', 'map-crossfire',
        'map-stalkyard', 'client-movement', 'door-platform',
        'second-client', 'server-restart-reconnect',
        'drop-client-continuation-request', 'drop-covering-ack',
        'duplicate-server-signon-batch', 'drop-first-full-snapshot',
        'drop-delta-snapshot', 'replay-old-snapshot',
        'same-process-map-change')]
    [string]$Scenario,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(5, 120)]
    [int]$CaptureTimeoutSeconds = 90,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(64, 4096)]
    [int]$MaximumPackets = 2048,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(576, 65535)]
    [int]$MaximumPacketBytes = 4096,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1048576, 33554432)]
    [int]$MaximumCaptureBytes = 16777216
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$scriptPath = [IO.Path]::GetFullPath($MyInvocation.MyCommand.Path)
$repositoryRoot = [IO.Path]::GetFullPath(
    (Join-Path (Split-Path -Parent $scriptPath) '..')).TrimEnd('\', '/')
$manualRoot = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'manual-artifacts'))
$captureRoot = [IO.Path]::GetFullPath(
    (Join-Path $manualRoot 'entity-snapshot-captures'))
$projectionRoot = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'docs/evidence'))
$projectionPath = [IO.Path]::GetFullPath(
    (Join-Path $projectionRoot 'GOLDSRC_ENTITY_SNAPSHOT_STOCK.json'))
$candidatePath = [IO.Path]::GetFullPath(
    (Join-Path $captureRoot 'stock-entity-snapshot-projection-candidate.json'))

$isolationMarkerName = '.hlclient-research-isolated'
$isolationMarkerText = 'HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1'
$stockProfile = 'stock-hl-1.1.1.1-to-stock-hlds-protocol48-build10210'
$candidateSchema = 'hlclient.stock-entity-snapshot-projection-candidate.v1'
$projectionSchema = 'hlclient.stock-entity-snapshot-evidence.v1'
$captureMetadataSchema = 'hlclient.stock-entity-snapshot-capture-metadata.v1'
$restorationSchema = 'hlclient.stock-entity-snapshot-restoration.v1'

$maximumCandidateBytes = 2097152
$maximumProjectionBytes = 2097152
$maximumAttestationBytes = 4096
$maximumAcceptedRuns = 128
$maximumTranscriptMessages = 512
$maximumRequestLayouts = 64
$maximumNumericPositions = 64
$maximumSchemaCategories = 64
$maximumResearchEntries = 200000
$maximumResearchBytes = [Int64]17179869184

$requiredScenarioMinimums = [ordered]@{
    'baseline-stationary' = 6
    'map-boot-camp' = 2
    'map-crossfire' = 2
    'map-stalkyard' = 2
    'client-movement' = 2
    'server-restart-reconnect' = 2
    'drop-client-continuation-request' = 2
    'drop-covering-ack' = 2
    'duplicate-server-signon-batch' = 2
    'drop-first-full-snapshot' = 2
    'drop-delta-snapshot' = 2
    'replay-old-snapshot' = 2
}
$optionalScenarioMinimums = [ordered]@{
    'door-platform' = 2
    'second-client' = 2
    'same-process-map-change' = 2
}
$allowedScenarios = @($requiredScenarioMinimums.Keys) +
    @($optionalScenarioMinimums.Keys)
$allowedMaps = @('boot_camp', 'crossfire', 'stalkyard', 'not-applicable')
$expectedScenarioResults = [ordered]@{
    'baseline-stationary' = 'baseline-full-delta-three-later-observed'
    'map-boot-camp' = 'boot-camp-profile-observed'
    'map-crossfire' = 'crossfire-profile-observed'
    'map-stalkyard' = 'stalkyard-profile-observed'
    'client-movement' = 'origin-angle-delta-change-observed'
    'server-restart-reconnect' = 'generation-baseline-history-reset-observed'
    'drop-client-continuation-request' =
        'single-queue-driver-retransmission-observed'
    'drop-covering-ack' =
        'single-queue-driver-retransmission-after-ack-gap-observed'
    'duplicate-server-signon-batch' = 'duplicate-batch-idempotence-observed'
    'drop-first-full-snapshot' = 'dropped-full-recovery-observed'
    'drop-delta-snapshot' = 'dropped-delta-recovery-observed'
    'replay-old-snapshot' = 'old-replay-rejected-history-immutable'
    'door-platform' = 'brush-origin-angle-delta-change-observed'
    'second-client' = 'second-player-schema-add-remove-observed'
    'same-process-map-change' =
        'new-generation-invalidated-old-baseline-history-observed'
}
$expectedScenarioMaps = [ordered]@{
    'map-boot-camp' = 'boot_camp'
    'map-crossfire' = 'crossfire'
    'map-stalkyard' = 'stalkyard'
    'same-process-map-change' = 'not-applicable'
}
$expectedDuplicateDropBehavior = [ordered]@{
    client_request_retransmission =
        'single-queue-driver-retransmission-observed'
    covering_ack_retry =
        'single-queue-driver-retransmission-after-ack-gap-observed'
    duplicate_server_batch_idempotence =
        'duplicate-batch-idempotence-observed'
    dropped_full_snapshot_recovery = 'dropped-full-recovery-observed'
    dropped_delta_snapshot_recovery = 'dropped-delta-recovery-observed'
    old_snapshot_replay_policy = 'old-replay-rejected-history-immutable'
}

function Get-Sha256Hex {
    param([byte[]]$Bytes)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '')
    }
    finally { $sha.Dispose() }
}

function Get-FileSha256Hex {
    param([string]$Path)
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '')
    }
    finally {
        $sha.Dispose()
        $stream.Dispose()
    }
}

function Get-TextSha256Hex {
    param([string]$Text)
    $encoding = New-Object Text.UTF8Encoding($false)
    return Get-Sha256Hex -Bytes $encoding.GetBytes($Text)
}

function Get-CanonicalJsonSha256Hex {
    param([object]$Value)
    $json = ConvertTo-Json -InputObject $Value -Depth 32 -Compress
    return Get-TextSha256Hex -Text $json
}

function Assert-NoReparsePoint {
    param([string]$Path, [string]$Label)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label must not be a reparse point."
    }
}

function Assert-NoReparsePointInExistingPath {
    param([string]$Path, [string]$Label)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $pathRoot = [IO.Path]::GetPathRoot($fullPath)
    $current = $pathRoot
    foreach ($component in @($fullPath.Substring($pathRoot.Length) -split '[\\/]' |
            Where-Object { $_ })) {
        $current = [IO.Path]::Combine($current, $component)
        if (Test-Path -LiteralPath $current) {
            Assert-NoReparsePoint -Path $current -Label $Label
        }
    }
}

function Assert-NoDescendantReparsePoint {
    param([string]$Path, [string]$Label)
    [void](Get-BoundedDescendantItems -Path $Path -Label $Label `
        -MaximumEntries $maximumResearchEntries)
}

function Get-BoundedDescendantItems {
    param(
        [string]$Path,
        [string]$Label,
        [int]$MaximumEntries
    )
    if ($MaximumEntries -lt 0 -or
        $MaximumEntries -gt $maximumResearchEntries) {
        throw "$Label has an invalid enumeration bound."
    }
    $items = [Collections.Generic.List[object]]::new()
    Get-ChildItem -LiteralPath $Path -Force -Recurse | ForEach-Object {
        if ($items.Count -ge $MaximumEntries) {
            throw "$Label exceeds the descendant entry bound."
        }
        if (($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Label contains a reparse point."
        }
        [void]$items.Add($_)
    }
    return @($items)
}

function Assert-OnlyDefaultDataStream {
    param([string]$Path, [string]$Label)
    $streams = @(Get-Item -LiteralPath $Path -Stream * -ErrorAction SilentlyContinue)
    if ($streams.Count -gt 0 -and
        @($streams | Where-Object { $_.Stream -cne ':$DATA' }).Count -ne 0) {
        throw "$Label must contain only the default data stream."
    }
}

function Assert-PathBelowRoot {
    param([string]$Path, [string]$Root, [string]$Label)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    if (-not $fullPath.StartsWith(
            $fullRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must be a canonical descendant of its exact root."
    }
}

function Resolve-SafeMutationTarget {
    param(
        [string]$Path,
        [string]$Root,
        [string]$Label,
        [switch]$AllowRoot
    )
    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    if ($fullPath.Equals($fullRoot, [StringComparison]::OrdinalIgnoreCase)) {
        if (-not $AllowRoot) {
            throw "$Label must be below its exact root."
        }
    }
    else {
        Assert-PathBelowRoot -Path $fullPath -Root $fullRoot -Label $Label
    }
    Assert-NoReparsePointInExistingPath -Path $fullPath -Label $Label
    if (Test-Path -LiteralPath $fullPath) {
        Assert-NoReparsePoint -Path $fullPath -Label $Label
    }
    return $fullPath
}

function Resolve-CanonicalFile {
    param([string]$Path, [string]$Root, [string]$Label)
    $fullPath = [IO.Path]::GetFullPath($Path)
    Assert-PathBelowRoot -Path $fullPath -Root $Root -Label $Label
    Assert-NoReparsePointInExistingPath -Path $fullPath -Label $Label
    $resolved = Resolve-Path -LiteralPath $fullPath -ErrorAction Stop
    if ($resolved.Provider.Name -cne 'FileSystem' -or
        -not (Test-Path -LiteralPath $resolved.Path -PathType Leaf)) {
        throw "$Label must be a filesystem file."
    }
    $canonical = [IO.Path]::GetFullPath($resolved.Path)
    Assert-PathBelowRoot -Path $canonical -Root $Root -Label $Label
    Assert-NoReparsePoint -Path $canonical -Label $Label
    Assert-OnlyDefaultDataStream -Path $canonical -Label $Label
    return $canonical
}

function Assert-ExactProperties {
    param([object]$Value, [string[]]$Allowed, [string]$Label)
    if ($null -eq $Value) { throw "$Label is absent." }
    $actual = @($Value.PSObject.Properties | ForEach-Object { $_.Name })
    if ($actual.Count -ne $Allowed.Count) {
        throw "$Label has an unexpected property count."
    }
    foreach ($name in $actual) {
        if ($Allowed -cnotcontains $name) {
            throw "$Label contains unexpected property '$name'."
        }
    }
    foreach ($name in $Allowed) {
        if ($actual -cnotcontains $name) {
            throw "$Label lacks required property '$name'."
        }
    }
}

function Get-BoundedInteger {
    param(
        [object]$Value,
        [string]$Label,
        [Int64]$Minimum,
        [Int64]$Maximum)
    if ($null -eq $Value -or $Value -is [bool] -or
        $Value -is [float] -or $Value -is [double] -or
        $Value -is [decimal]) {
        throw "$Label must be an integral JSON number."
    }
    try { $number = [Int64]$Value }
    catch { throw "$Label must be an integral JSON number." }
    if ($number -lt $Minimum -or $number -gt $Maximum) {
        throw "$Label is outside its safety bound."
    }
    return $number
}

function Get-StrictBoolean {
    param([object]$Value, [string]$Label)
    if ($Value -isnot [bool]) { throw "$Label must be a JSON boolean." }
    return [bool]$Value
}

function Get-StrictToken {
    param([object]$Value, [string]$Label, [int]$MaximumLength = 64)
    $text = [string]$Value
    if ($text.Length -lt 1 -or $text.Length -gt $MaximumLength -or
        $text -cnotmatch '^[a-z0-9][a-z0-9-]*$') {
        throw "$Label is not a bounded metadata token."
    }
    return $text
}

function Get-StrictSha256 {
    param([object]$Value, [string]$Label)
    $text = [string]$Value
    if ($text -cnotmatch '^[0-9A-F]{64}$') {
        throw "$Label must be an uppercase SHA-256 value."
    }
    return $text
}

function Get-KnownSteamRoots {
    $roots = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($registryPath in @(
            'HKCU:\Software\Valve\Steam',
            'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam',
            'HKLM:\SOFTWARE\Valve\Steam')) {
        if (-not (Test-Path -LiteralPath $registryPath -PathType Container)) {
            continue
        }
        try { $value = Get-ItemProperty -LiteralPath $registryPath }
        catch { throw "Unable to inspect configured Steam root '$registryPath'." }
        foreach ($property in @('SteamPath', 'InstallPath')) {
            $entry = $value.PSObject.Properties[$property]
            if ($null -ne $entry -and
                -not [string]::IsNullOrWhiteSpace([string]$entry.Value)) {
                try {
                    [void]$roots.Add([IO.Path]::GetFullPath([string]$entry.Value))
                }
                catch { throw 'A configured Steam root is not a valid path.' }
            }
        }
    }
    foreach ($candidate in @(
            $(if (${env:ProgramFiles(x86)}) {
                Join-Path ${env:ProgramFiles(x86)} 'Steam'
            }),
            $(if ($env:ProgramFiles) {
                Join-Path $env:ProgramFiles 'Steam'
            }))) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Container)) {
            [void]$roots.Add([IO.Path]::GetFullPath($candidate))
        }
    }
    foreach ($steamRoot in @($roots)) {
        $libraryFile = [IO.Path]::Combine(
            $steamRoot, 'steamapps', 'libraryfolders.vdf')
        if (-not (Test-Path -LiteralPath $libraryFile -PathType Leaf)) {
            continue
        }
        Assert-NoReparsePointInExistingPath -Path $libraryFile `
            -Label 'Steam library manifest'
        if ((Get-Item -LiteralPath $libraryFile).Length -gt 1048576) {
            throw 'Steam library manifest exceeds its inspection bound.'
        }
        try {
            $text = Get-Content -Raw -LiteralPath $libraryFile
            foreach ($match in [regex]::Matches(
                    $text, '"path"\s+"(?<path>[^"]+)"',
                    [Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
                $libraryPath = $match.Groups['path'].Value.Replace('\\', '\')
                if (-not [string]::IsNullOrWhiteSpace($libraryPath)) {
                    [void]$roots.Add([IO.Path]::GetFullPath($libraryPath))
                }
            }
        }
        catch { throw 'Unable to establish the configured Steam-library set.' }
    }
    return @($roots)
}

function Assert-ValveStockBinary {
    param(
        [string]$Path,
        [string]$ExpectedFileVersion,
        [string]$Label)
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.VersionInfo.FileVersion -cne $ExpectedFileVersion) {
        throw "$Label does not match the accepted stock file version."
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid' -or
        $null -eq $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -cnotmatch
            '^CN=Valve Corp\.(?:,|$)') {
        throw "$Label is not validly Valve-signed."
    }
}

function Assert-IsolatedResearchRoot {
    param(
        [string]$Path,
        [string]$ExplicitClientPath,
        [string]$ExplicitHldsPath)
    $inputPath = [IO.Path]::GetFullPath($Path)
    Assert-NoReparsePointInExistingPath -Path $inputPath `
        -Label 'ResearchHalfLifeRoot'
    $resolved = Resolve-Path -LiteralPath $inputPath -ErrorAction Stop
    if ($resolved.Provider.Name -cne 'FileSystem' -or
        -not (Test-Path -LiteralPath $resolved.Path -PathType Container)) {
        throw 'ResearchHalfLifeRoot must be a filesystem directory.'
    }
    $root = [IO.Path]::GetFullPath($resolved.Path).TrimEnd('\', '/')
    Assert-NoReparsePoint -Path $root -Label 'ResearchHalfLifeRoot'
    if ($root -match '(?i)(?:^|[\\/])steamapps(?:[\\/]|$)') {
        throw 'Primary or managed Steam-library roots are never accepted.'
    }
    foreach ($steamRoot in @(Get-KnownSteamRoots)) {
        $normalizedSteam = [IO.Path]::GetFullPath($steamRoot).TrimEnd('\', '/')
        if ($root.Equals($normalizedSteam, [StringComparison]::OrdinalIgnoreCase) -or
            $root.StartsWith(
                $normalizedSteam + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw 'ResearchHalfLifeRoot resolves inside a configured Steam library.'
        }
        $primary = [IO.Path]::GetFullPath([IO.Path]::Combine(
            $normalizedSteam, 'steamapps', 'common', 'Half-Life')).TrimEnd('\', '/')
        if ($root.Equals($primary, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'The primary Half-Life installation is never accepted.'
        }
    }
    Assert-NoDescendantReparsePoint -Path $root -Label 'ResearchHalfLifeRoot'

    $marker = Resolve-CanonicalFile -Path (Join-Path $root $isolationMarkerName) `
        -Root $root -Label 'isolation marker'
    if ((Get-Item -LiteralPath $marker).Length -gt 128 -or
        (Get-Content -Raw -LiteralPath $marker).Trim() -cne
            $isolationMarkerText) {
        throw 'Isolated research marker content is invalid.'
    }
    $client = Resolve-CanonicalFile -Path $ExplicitClientPath `
        -Root $root -Label 'stock client'
    $server = Resolve-CanonicalFile -Path $ExplicitHldsPath `
        -Root $root -Label 'stock server'
    $requiredClient = [IO.Path]::GetFullPath((Join-Path $root 'hl.exe'))
    $requiredServer = [IO.Path]::GetFullPath((Join-Path $root 'hlds.exe'))
    if (-not $client.Equals(
            $requiredClient, [StringComparison]::OrdinalIgnoreCase) -or
        -not $server.Equals(
            $requiredServer, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Explicit stock paths must be the canonical root hl.exe and hlds.exe.'
    }
    $valveRoot = [IO.Path]::GetFullPath((Join-Path $root 'valve'))
    Assert-PathBelowRoot -Path $valveRoot -Root $root -Label 'valve directory'
    if (-not (Test-Path -LiteralPath $valveRoot -PathType Container)) {
        throw 'Isolated research copy lacks the valve directory.'
    }
    Assert-NoReparsePoint -Path $valveRoot -Label 'valve directory'
    Assert-ValveStockBinary -Path $client -ExpectedFileVersion '1, 1, 1, 1' `
        -Label 'stock client'
    Assert-ValveStockBinary -Path $server -ExpectedFileVersion '4, 1, 1, 1' `
        -Label 'stock server'
    return [pscustomobject]@{
        Root = $root
        Client = $client
        Server = $server
    }
}

function Get-LiveGoldSrcProcesses {
    $records = [Collections.Generic.List[object]]::new()
    foreach ($process in @(Get-Process -Name @('hl', 'hlds') `
            -ErrorAction SilentlyContinue)) {
        $path = $null
        try { $path = [IO.Path]::GetFullPath($process.Path) }
        catch { }
        $records.Add([pscustomobject]@{
            Id = $process.Id
            Name = $process.ProcessName
            Path = $path
        })
    }
    return @($records)
}

function Stop-OwnedResearchProcesses {
    param([object]$Research)
    $unowned = [Collections.Generic.List[object]]::new()
    foreach ($process in @(Get-LiveGoldSrcProcesses)) {
        if ($null -ne $process.Path -and
            ($process.Path.Equals(
                $Research.Client, [StringComparison]::OrdinalIgnoreCase) -or
             $process.Path.Equals(
                $Research.Server, [StringComparison]::OrdinalIgnoreCase))) {
            Stop-Process -Id $process.Id -Force -ErrorAction Stop
        }
        else { $unowned.Add($process) }
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        $remainingOwned = @(Get-LiveGoldSrcProcesses | Where-Object {
            $null -ne $_.Path -and
            ($_.Path.Equals(
                $Research.Client, [StringComparison]::OrdinalIgnoreCase) -or
             $_.Path.Equals(
                $Research.Server, [StringComparison]::OrdinalIgnoreCase))
        })
        if ($remainingOwned.Count -eq 0) { break }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($remainingOwned.Count -ne 0) {
        throw 'Owned isolated stock processes did not stop before restoration.'
    }
    if ($unowned.Count -ne 0) {
        throw 'An unowned GoldSrc process appeared during capture; it was not terminated.'
    }
}

function Get-RelativeResearchPath {
    param([string]$Path, [string]$Root)
    $rootPrefix = $Root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $Path.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Research snapshot entry escaped its root.'
    }
    return $Path.Substring($rootPrefix.Length).Replace('\', '/')
}

function Test-MutableResearchPath {
    param([string]$RelativePath, [bool]$IsDirectory)
    $path = $RelativePath.Replace('\', '/').ToLowerInvariant()
    if ($path -in @(
            'valve/config.cfg', 'valve/userconfig.cfg', 'valve/autoexec.cfg',
            'valve/custom.hpk', 'valve/server.cfg', 'valve/listenserver.cfg')) {
        return $true
    }
    if ($path -eq 'valve/logs' -or $path.StartsWith('valve/logs/') -or
        $path -eq 'valve/save' -or $path.StartsWith('valve/save/') -or
        $path -eq 'valve/config' -or $path.StartsWith('valve/config/')) {
        return $true
    }
    if (-not $IsDirectory -and $path.EndsWith('.vdf')) { return $true }
    return $false
}

function Get-ResearchSnapshot {
    param([string]$Root)
    $snapshotItems = @(Get-BoundedDescendantItems -Path $Root `
        -Label 'research snapshot root' `
        -MaximumEntries ($maximumResearchEntries - 1))
    $entries = [Collections.Generic.List[object]]::new()
    $rootItem = Get-Item -LiteralPath $Root -Force
    $entries.Add([pscustomobject]@{
        RelativePath = '.'
        Kind = 'directory'
        Length = [Int64]0
        LastWriteTimeUtcTicks = $rootItem.LastWriteTimeUtc.Ticks
        Sha256 = ''
        Attributes = [Int64]$rootItem.Attributes
        Mutable = $false
    })
    [Int64]$totalBytes = 0
    foreach ($item in @($snapshotItems | Sort-Object FullName)) {
        $relative = Get-RelativeResearchPath -Path $item.FullName -Root $Root
        $isDirectory = [bool]$item.PSIsContainer
        if (-not $isDirectory) {
            Assert-OnlyDefaultDataStream -Path $item.FullName `
                -Label 'research snapshot file'
        }
        [Int64]$length = if ($isDirectory) { 0 } else { $item.Length }
        $totalBytes += $length
        if ($totalBytes -gt $maximumResearchBytes) {
            throw 'Research root exceeds the snapshot byte bound.'
        }
        $entries.Add([pscustomobject]@{
            RelativePath = $relative
            Kind = $(if ($isDirectory) { 'directory' } else { 'file' })
            Length = $length
            LastWriteTimeUtcTicks = $item.LastWriteTimeUtc.Ticks
            Sha256 = $(if ($isDirectory) { '' } else {
                Get-FileSha256Hex -Path $item.FullName
            })
            Attributes = [Int64]$item.Attributes
            Mutable = Test-MutableResearchPath -RelativePath $relative `
                -IsDirectory $isDirectory
        })
    }
    $lines = @($entries | ForEach-Object {
        '{0}|{1}|{2}|{3}|{4}|{5}' -f $_.RelativePath, $_.Kind,
            $_.Length, $_.LastWriteTimeUtcTicks, $_.Sha256, $_.Attributes
    })
    return [pscustomobject]@{
        Entries = @($entries)
        TotalBytes = $totalBytes
        ManifestSha256 = Get-TextSha256Hex -Text ($lines -join "`n")
    }
}

function Backup-MutableResearchState {
    param([object]$Snapshot, [string]$Root, [string]$BackupRoot)
    $dataRoot = Join-Path $BackupRoot 'data'
    [IO.Directory]::CreateDirectory($dataRoot) | Out-Null
    foreach ($entry in @($Snapshot.Entries | Where-Object {
                $_.Mutable -and $_.Kind -ceq 'file'
            })) {
        $source = [IO.Path]::GetFullPath((Join-Path $Root $entry.RelativePath))
        $target = [IO.Path]::GetFullPath((Join-Path $dataRoot $entry.RelativePath))
        Assert-PathBelowRoot -Path $target -Root $dataRoot -Label 'backup target'
        $parent = Split-Path -Parent $target
        if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
            [IO.Directory]::CreateDirectory($parent) | Out-Null
        }
        Copy-Item -LiteralPath $source -Destination $target -Force
        if ((Get-FileSha256Hex -Path $target) -cne $entry.Sha256) {
            throw 'Mutable-state backup verification failed.'
        }
    }
}

function Restore-ResearchState {
    param([object]$Before, [string]$Root, [string]$BackupRoot)
    Assert-NoReparsePointInExistingPath -Path $Root `
        -Label 'post-capture research root'
    Assert-NoReparsePoint -Path $Root -Label 'post-capture research root'
    $currentItems = @(Get-BoundedDescendantItems -Path $Root `
        -Label 'post-capture research root' `
        -MaximumEntries $maximumResearchEntries |
        Sort-Object { $_.FullName.Length } -Descending)
    $beforeByPath = @{}
    foreach ($entry in @($Before.Entries)) {
        $beforeByPath[$entry.RelativePath.ToLowerInvariant()] = $entry
    }
    [int]$createdFileCount = 0
    [int]$createdRemovalCount = 0
    foreach ($item in $currentItems) {
        $relative = Get-RelativeResearchPath -Path $item.FullName -Root $Root
        if ($beforeByPath.ContainsKey($relative.ToLowerInvariant())) { continue }
        $target = Resolve-SafeMutationTarget -Path $item.FullName -Root $Root `
            -Label 'created research entry'
        $fresh = Get-Item -LiteralPath $target -Force
        if (-not $fresh.PSIsContainer) {
            $createdFileCount++
            $fresh.Attributes = [IO.FileAttributes]::Normal
            $target = Resolve-SafeMutationTarget -Path $target -Root $Root `
                -Label 'created research file removal'
            Remove-Item -LiteralPath $target -Force
            $createdRemovalCount++
        }
    }
    foreach ($item in $currentItems) {
        $relative = Get-RelativeResearchPath -Path $item.FullName -Root $Root
        if ($beforeByPath.ContainsKey($relative.ToLowerInvariant()) -or
            -not $item.PSIsContainer) {
            continue
        }
        $target = Resolve-SafeMutationTarget -Path $item.FullName -Root $Root `
            -Label 'created research directory removal'
        Remove-Item -LiteralPath $target -Force
    }

    $dataRoot = Join-Path $BackupRoot 'data'
    foreach ($entry in @($Before.Entries | Where-Object {
                $_.Mutable -and $_.Kind -ceq 'file'
            })) {
        $source = [IO.Path]::GetFullPath((Join-Path $dataRoot $entry.RelativePath))
        $target = [IO.Path]::GetFullPath((Join-Path $Root $entry.RelativePath))
        $source = Resolve-SafeMutationTarget -Path $source -Root $dataRoot `
            -Label 'restoration backup source'
        $target = Resolve-SafeMutationTarget -Path $target -Root $Root `
            -Label 'restore target'
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw 'Mutable-state backup is incomplete.'
        }
        Assert-OnlyDefaultDataStream -Path $source `
            -Label 'restoration backup source'
        $parent = Split-Path -Parent $target
        $parent = Resolve-SafeMutationTarget -Path $parent -Root $Root `
            -Label 'restore target parent' -AllowRoot
        if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
            [IO.Directory]::CreateDirectory($parent) | Out-Null
        }
        $parent = Resolve-SafeMutationTarget -Path $parent -Root $Root `
            -Label 'created restore target parent' -AllowRoot
        $target = Resolve-SafeMutationTarget -Path $target -Root $Root `
            -Label 'restore target before replacement'
        if (Test-Path -LiteralPath $target) {
            $existing = Get-Item -LiteralPath $target -Force
            if ($existing.PSIsContainer) {
                $target = Resolve-SafeMutationTarget -Path $target -Root $Root `
                    -Label 'restore target directory replacement'
                Remove-Item -LiteralPath $target -Force
            }
            else {
                $existing.Attributes = [IO.FileAttributes]::Normal
            }
        }
        $target = Resolve-SafeMutationTarget -Path $target -Root $Root `
            -Label 'restore target before copy'
        $source = Resolve-SafeMutationTarget -Path $source -Root $dataRoot `
            -Label 'restoration backup source before copy'
        Copy-Item -LiteralPath $source -Destination $target -Force
        $target = Resolve-SafeMutationTarget -Path $target -Root $Root `
            -Label 'restored mutable file'
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
            throw 'Mutable-state restoration did not create a regular file.'
        }
        $restored = Get-Item -LiteralPath $target -Force
        $restored.LastWriteTimeUtc = [DateTime]::new(
            [Int64]$entry.LastWriteTimeUtcTicks, [DateTimeKind]::Utc)
        $restored.Attributes = [IO.FileAttributes]([Int64]$entry.Attributes)
    }

    foreach ($entry in @($Before.Entries | Where-Object {
                $_.Kind -ceq 'directory'
            } | Sort-Object { $_.RelativePath.Length } -Descending)) {
        $target = if ($entry.RelativePath -ceq '.') {
            $Root
        }
        else { [IO.Path]::GetFullPath((Join-Path $Root $entry.RelativePath)) }
        $target = Resolve-SafeMutationTarget -Path $target -Root $Root `
            -Label 'research directory restoration target' -AllowRoot
        if (-not (Test-Path -LiteralPath $target -PathType Container)) {
            if (-not $entry.Mutable) {
                throw 'A pre-existing non-mutable research directory was removed.'
            }
            if (Test-Path -LiteralPath $target) {
                $existing = Get-Item -LiteralPath $target -Force
                $existing.Attributes = [IO.FileAttributes]::Normal
                $target = Resolve-SafeMutationTarget -Path $target -Root $Root `
                    -Label 'research directory file replacement'
                Remove-Item -LiteralPath $target -Force
            }
            [IO.Directory]::CreateDirectory($target) | Out-Null
        }
        $target = Resolve-SafeMutationTarget -Path $target -Root $Root `
            -Label 'restored research directory' -AllowRoot
        $restored = Get-Item -LiteralPath $target -Force
        $restored.LastWriteTimeUtc = [DateTime]::new(
            [Int64]$entry.LastWriteTimeUtcTicks, [DateTimeKind]::Utc)
        $restored.Attributes = [IO.FileAttributes]([Int64]$entry.Attributes)
    }

    $after = Get-ResearchSnapshot -Root $Root
    return [pscustomobject]@{
        After = $after
        RestoredEntryCount = @($Before.Entries | Where-Object {
                $_.Mutable -and $_.Kind -ceq 'file'
            }).Count
        CreatedFileCount = $createdFileCount
        CreatedFileRemovalCount = $createdRemovalCount
        Drift = $(if ($after.ManifestSha256 -ceq $Before.ManifestSha256) {
            'none'
        } else { 'detected' })
    }
}

function Assert-RestorationAttestation {
    param([string]$Path, [string]$ExpectedRunId)
    Assert-PathBelowRoot -Path $Path -Root $captureRoot `
        -Label 'restoration attestation'
    Assert-NoReparsePointInExistingPath -Path $Path `
        -Label 'restoration attestation'
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or
        (Get-Item -LiteralPath $Path).Length -gt $maximumAttestationBytes) {
        throw 'Restoration attestation is absent or outside its byte bound.'
    }
    Assert-NoReparsePoint -Path $Path -Label 'restoration attestation'
    Assert-OnlyDefaultDataStream -Path $Path -Label 'restoration attestation'
    $value = Get-Content -Raw -LiteralPath $Path |
        ConvertFrom-Json -ErrorAction Stop
    Assert-ExactProperties -Value $value -Allowed @(
        'schema', 'run_id', 'isolated_copy', 'external_file_drift',
        'snapshot_entry_count', 'restored_entry_count', 'created_file_count',
        'created_file_removal_count', 'pre_manifest_sha256',
        'post_manifest_sha256', 'mutable_scope', 'capture_process_exit_code') `
        -Label 'restoration attestation'
    if ([string]$value.schema -cne $restorationSchema -or
        [string]$value.run_id -cne $ExpectedRunId -or
        -not (Get-StrictBoolean $value.isolated_copy 'isolated-copy flag') -or
        [string]$value.external_file_drift -cne 'none') {
        throw 'Restoration attestation is not accepted.'
    }
    [void](Get-BoundedInteger $value.snapshot_entry_count `
        'snapshot entry count' 1 $maximumResearchEntries)
    [void](Get-BoundedInteger $value.restored_entry_count `
        'restored entry count' 0 $maximumResearchEntries)
    $created = Get-BoundedInteger $value.created_file_count `
        'created file count' 0 $maximumResearchEntries
    $removed = Get-BoundedInteger $value.created_file_removal_count `
        'created file removal count' 0 $maximumResearchEntries
    if ($created -ne $removed) {
        throw 'Restoration attestation did not remove every created file.'
    }
    $beforeHash = Get-StrictSha256 $value.pre_manifest_sha256 `
        'pre-run manifest hash'
    $afterHash = Get-StrictSha256 $value.post_manifest_sha256 `
        'post-run manifest hash'
    if ($beforeHash -cne $afterHash) {
        throw 'Restoration pre/post manifest hashes disagree.'
    }
    if ([string]$value.mutable_scope -cne
        'cfg-hpk-logs-save-config-vdf-and-created-files') {
        throw 'Restoration mutable scope is incomplete.'
    }
    [void](Get-BoundedInteger $value.capture_process_exit_code `
        'capture process exit code' 0 0)
    return $value
}

function Assert-CaptureMetadata {
    param(
        [string]$Path,
        [string]$ExpectedRunId,
        [string]$ExpectedScenario,
        [string]$ExpectedMap)
    $value = Read-BoundedJsonFile -Path $Path `
        -MaximumBytes $maximumCandidateBytes -Label 'capture metadata'
    Assert-ExactProperties -Value $value -Allowed @(
        'schema', 'profile', 'run_id', 'scenario', 'map_category',
        'accepted', 'stock_versions', 'capture_bounds', 'results', 'privacy') `
        -Label 'capture metadata'
    if ([string]$value.schema -cne $captureMetadataSchema -or
        [string]$value.profile -cne $stockProfile -or
        [string]$value.run_id -cne $ExpectedRunId -or
        [string]$value.scenario -cne $ExpectedScenario -or
        [string]$value.map_category -cne $ExpectedMap -or
        -not (Get-StrictBoolean $value.accepted 'capture accepted flag')) {
        throw 'Capture metadata identity/acceptance is invalid.'
    }
    if ($expectedScenarioMaps.Contains($ExpectedScenario)) {
        if ([string]$value.map_category -cne
            [string]$expectedScenarioMaps[$ExpectedScenario]) {
            throw 'Capture metadata scenario/map correlation is invalid.'
        }
    }
    elseif ([string]$value.map_category -ceq 'not-applicable') {
        throw 'This capture scenario requires one exact stock map category.'
    }
    Assert-ExactProperties -Value $value.stock_versions -Allowed @(
        'client_file_version', 'server_file_version', 'protocol',
        'server_build') -Label 'capture stock versions'
    if ([string]$value.stock_versions.client_file_version -cne '1, 1, 1, 1' -or
        [string]$value.stock_versions.server_file_version -cne '4, 1, 1, 1' -or
        (Get-BoundedInteger $value.stock_versions.protocol `
            'capture stock protocol' 48 48) -ne 48 -or
        (Get-BoundedInteger $value.stock_versions.server_build `
            'capture stock server build' 10210 10210) -ne 10210) {
        throw 'Capture metadata uses an unsupported stock version profile.'
    }
    Assert-ExactProperties -Value $value.capture_bounds -Allowed @(
        'private_ipv4_loopback', 'upstream_socket_count',
        'byte_preserving_forwarding', 'exact_endpoint_validation',
        'packet_limit', 'packet_byte_limit', 'total_byte_limit',
        'timeout_seconds', 'packet_rewriting') -Label 'capture bounds'
    if (-not (Get-StrictBoolean $value.capture_bounds.private_ipv4_loopback `
            'private IPv4 loopback flag') -or
        -not (Get-StrictBoolean $value.capture_bounds.byte_preserving_forwarding `
            'byte-preserving forwarding flag') -or
        -not (Get-StrictBoolean $value.capture_bounds.exact_endpoint_validation `
            'endpoint validation flag') -or
        (Get-StrictBoolean $value.capture_bounds.packet_rewriting `
            'packet rewriting flag')) {
        throw 'Capture metadata violates the relay safety policy.'
    }
    [void](Get-BoundedInteger $value.capture_bounds.upstream_socket_count `
        'upstream socket count' 1 1)
    [void](Get-BoundedInteger $value.capture_bounds.packet_limit `
        'packet limit' 64 4096)
    [void](Get-BoundedInteger $value.capture_bounds.packet_byte_limit `
        'packet byte limit' 576 65535)
    [void](Get-BoundedInteger $value.capture_bounds.total_byte_limit `
        'total capture byte limit' 1048576 33554432)
    [void](Get-BoundedInteger $value.capture_bounds.timeout_seconds `
        'capture timeout' 5 120)

    Assert-ExactProperties -Value $value.results -Allowed @(
        'signon_completed', 'baseline_count', 'full_snapshot_count',
        'later_snapshot_count', 'delta_snapshot_count',
        'semantic_message_count', 'client_request_count',
        'baseline_schema_count', 'full_entity_count',
        'delta_changed_count', 'delta_added_count', 'delta_removed_count',
        'message_reference_width_bits', 'snapshot_reference_width_bits',
        'clientdata_present', 'weapondata_present',
        'baseline_schema_distribution', 'signon_progression_sha256',
        'canonical_transcript_sha256', 'request_layouts_sha256',
        'scenario_result') `
        -Label 'capture results'
    if (-not (Get-StrictBoolean $value.results.signon_completed `
            'capture sign-on completion')) {
        throw 'Accepted capture did not complete stock sign-on.'
    }
    foreach ($field in @('clientdata_present', 'weapondata_present')) {
        [void](Get-StrictBoolean $value.results.$field "capture result $field")
    }
    $baselineCount = Get-BoundedInteger $value.results.baseline_count `
        'capture baseline count' 1 8192
    [void](Get-BoundedInteger $value.results.full_snapshot_count `
        'capture full snapshot count' 1 16)
    [void](Get-BoundedInteger $value.results.later_snapshot_count `
        'capture later snapshot count' 3 4096)
    [void](Get-BoundedInteger $value.results.delta_snapshot_count `
        'capture delta snapshot count' 1 4096)
    [void](Get-BoundedInteger $value.results.semantic_message_count `
        'capture semantic message count' 1 $maximumTranscriptMessages)
    [void](Get-BoundedInteger $value.results.client_request_count `
        'capture client request count' 1 $maximumRequestLayouts)
    $baselineSchemaCount = Get-BoundedInteger `
        $value.results.baseline_schema_count 'capture baseline schema count' `
        1 $maximumSchemaCategories
    [void](Get-BoundedInteger $value.results.full_entity_count `
        'capture full entity count' 1 8192)
    $deltaChangedCount = Get-BoundedInteger `
        $value.results.delta_changed_count 'capture result delta_changed_count' `
        0 8192
    $deltaAddedCount = Get-BoundedInteger `
        $value.results.delta_added_count 'capture result delta_added_count' `
        0 8192
    $deltaRemovedCount = Get-BoundedInteger `
        $value.results.delta_removed_count 'capture result delta_removed_count' `
        0 8192
    foreach ($field in @(
            'message_reference_width_bits', 'snapshot_reference_width_bits')) {
        [void](Get-BoundedInteger $value.results.$field `
            "capture result $field" 1 64)
    }
    $schemaDistribution = @($value.results.baseline_schema_distribution)
    if ($schemaDistribution.Count -ne $baselineSchemaCount) {
        throw 'Capture baseline schema distribution count is inconsistent.'
    }
    $schemaNames = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    [Int64]$distributedBaselineCount = 0
    foreach ($entry in $schemaDistribution) {
        Assert-ExactProperties -Value $entry -Allowed @('category', 'count') `
            -Label 'capture baseline schema distribution entry'
        $category = Get-StrictToken $entry.category `
            'capture baseline schema category'
        if (-not $schemaNames.Add($category)) {
            throw 'Capture baseline schema distribution has a duplicate category.'
        }
        $distributedBaselineCount += Get-BoundedInteger $entry.count `
            'capture baseline schema count' 1 8192
    }
    if ($distributedBaselineCount -ne $baselineCount) {
        throw 'Capture baseline schema distribution does not cover all baselines.'
    }
    foreach ($field in @(
            'signon_progression_sha256', 'canonical_transcript_sha256',
            'request_layouts_sha256')) {
        [void](Get-StrictSha256 $value.results.$field "capture result $field")
    }
    if ([string]$value.results.scenario_result -cne
        [string]$expectedScenarioResults[$ExpectedScenario]) {
        throw 'Capture metadata does not attest the selected stock scenario.'
    }
    if ($ExpectedScenario -cin @('client-movement', 'door-platform') -and
        $deltaChangedCount -lt 1) {
        throw 'Movement scenario did not attest a changed entity delta.'
    }
    if ($ExpectedScenario -ceq 'second-client' -and
        ($deltaChangedCount -lt 1 -or $deltaAddedCount -lt 1 -or
         $deltaRemovedCount -lt 1 -or $baselineSchemaCount -lt 2)) {
        throw 'Second-client scenario lacks schema/add/remove observations.'
    }
    Assert-ExactProperties -Value $value.privacy -Allowed @(
        'raw_packet_bodies', 'raw_snapshot_bodies', 'authentication_material',
        'identity_data', 'endpoint_data', 'game_values') -Label 'capture privacy'
    foreach ($property in $value.privacy.PSObject.Properties) {
        if (Get-StrictBoolean $property.Value "capture privacy $($property.Name)") {
            throw 'Capture metadata must not expose sensitive/raw values.'
        }
    }
    return $value
}

function Assert-TranscriptMessage {
    param([object]$Message, [int]$Ordinal)
    Assert-ExactProperties -Value $Message -Allowed @(
        'direction', 'netchan_sequence', 'reliable_generation', 'fragmented',
        'descriptor_present', 'descriptor_size_bytes', 'fragment_ordinal',
        'reliable_present', 'decompressed_payload_ordinal', 'opcode',
        'byte_start', 'bit_start', 'byte_end', 'bit_end', 'body_size_bits',
        'terminator', 'alignment_bits', 'category', 'static_field_count',
        'dynamic_field_count', 'static_field_geometry_sha256',
        'dynamic_field_geometry_sha256', 'next_byte', 'next_bit',
        'confidence') -Label "transcript message $Ordinal"
    if ([string]$Message.direction -cnotin @('client-to-server', 'server-to-client')) {
        throw "Transcript message $Ordinal has an invalid direction."
    }
    foreach ($field in @('netchan_sequence', 'reliable_generation')) {
        [void](Get-BoundedInteger $Message.$field "message $Ordinal $field" 0 2147483647)
    }
    $fragmented = Get-StrictBoolean $Message.fragmented `
        "message $Ordinal fragmented"
    $descriptorPresent = Get-StrictBoolean $Message.descriptor_present `
        "message $Ordinal descriptor present"
    $descriptorSize = Get-BoundedInteger $Message.descriptor_size_bytes `
        "message $Ordinal descriptor size" 0 64
    [void](Get-BoundedInteger $Message.fragment_ordinal `
        "message $Ordinal fragment ordinal" 0 4095)
    [void](Get-StrictBoolean $Message.reliable_present `
        "message $Ordinal reliable present")
    if ($fragmented -and (-not $descriptorPresent -or $descriptorSize -lt 1)) {
        throw "Transcript message $Ordinal lacks fragment descriptor metadata."
    }
    if (-not $fragmented -and ($descriptorPresent -or $descriptorSize -ne 0)) {
        throw "Transcript message $Ordinal has a descriptor without fragmentation."
    }
    [void](Get-BoundedInteger $Message.decompressed_payload_ordinal `
        "message $Ordinal decompressed payload ordinal" 0 4095)
    [void](Get-BoundedInteger $Message.opcode "message $Ordinal opcode" 0 255)
    $byteStart = Get-BoundedInteger $Message.byte_start `
        "message $Ordinal byte start" 0 16777216
    $bitStart = Get-BoundedInteger $Message.bit_start `
        "message $Ordinal bit start" 0 7
    $byteEnd = Get-BoundedInteger $Message.byte_end `
        "message $Ordinal byte end" 0 16777216
    $bitEnd = Get-BoundedInteger $Message.bit_end `
        "message $Ordinal bit end" 0 7
    $bodyBits = Get-BoundedInteger $Message.body_size_bits `
        "message $Ordinal body size" 0 134217728
    if (($byteEnd * 8 + $bitEnd) -lt ($byteStart * 8 + $bitStart) -or
        $bodyBits -gt (($byteEnd * 8 + $bitEnd) - ($byteStart * 8 + $bitStart))) {
        throw "Transcript message $Ordinal has invalid cursor geometry."
    }
    if ([string]$Message.terminator -cnotin @(
            'none', 'nul-byte', 'end-marker', 'bit-aligned-end')) {
        throw "Transcript message $Ordinal has an invalid terminator."
    }
    [void](Get-BoundedInteger $Message.alignment_bits `
        "message $Ordinal alignment" 0 7)
    [void](Get-StrictToken $Message.category "message $Ordinal category")
    foreach ($field in @('static_field_count', 'dynamic_field_count')) {
        [void](Get-BoundedInteger $Message.$field "message $Ordinal $field" 0 4096)
    }
    [void](Get-StrictSha256 $Message.static_field_geometry_sha256 `
        "message $Ordinal static-field geometry hash")
    [void](Get-StrictSha256 $Message.dynamic_field_geometry_sha256 `
        "message $Ordinal dynamic-field geometry hash")
    $nextByte = Get-BoundedInteger $Message.next_byte `
        "message $Ordinal next byte" 0 16777216
    $nextBit = Get-BoundedInteger $Message.next_bit `
        "message $Ordinal next bit" 0 7
    if ($nextByte -ne $byteEnd -or $nextBit -ne $bitEnd) {
        throw "Transcript message $Ordinal does not publish its exact next cursor."
    }
    if ([string]$Message.confidence -cnotin @(
            'stock-confirmed', 'public-valve-cross-check', 'evidence-pending')) {
        throw "Transcript message $Ordinal has an invalid confidence status."
    }
}

function Assert-RequestLayout {
    param([object]$Layout, [int]$Ordinal)
    Assert-ExactProperties -Value $Layout -Allowed @(
        'semantic_size', 'fixed_token_sha256', 'numeric_field_positions',
        'terminator', 'reliable_lifecycle', 'trigger_category',
        'layout_sha256') -Label "request layout $Ordinal"
    [void](Get-BoundedInteger $Layout.semantic_size `
        "request layout $Ordinal size" 1 4096)
    [void](Get-StrictSha256 $Layout.fixed_token_sha256 `
        "request layout $Ordinal fixed-token hash")
    $positions = @($Layout.numeric_field_positions)
    if ($positions.Count -gt $maximumNumericPositions) {
        throw "Request layout $Ordinal has too many numeric positions."
    }
    [Int64]$previous = -1
    foreach ($position in $positions) {
        $current = Get-BoundedInteger $position `
            "request layout $Ordinal numeric position" 0 4095
        if ($current -le $previous) {
            throw "Request layout $Ordinal numeric positions are not ordered/unique."
        }
        $previous = $current
    }
    if ([string]$Layout.terminator -cnotin @('nul-byte', 'newline', 'none')) {
        throw "Request layout $Ordinal has an invalid terminator."
    }
    if ([string]$Layout.reliable_lifecycle -cne
        'queued-once-driver-retries-covering-ack') {
        throw "Request layout $Ordinal has an invalid reliable lifecycle."
    }
    [void](Get-StrictToken $Layout.trigger_category `
        "request layout $Ordinal trigger category")
    [void](Get-StrictSha256 $Layout.layout_sha256 `
        "request layout $Ordinal layout hash")
}

function Assert-ProjectionCandidate {
    param([object]$Value, [bool]$IsTrackedProjection)
    Assert-ExactProperties -Value $Value -Allowed @(
        'schema', 'profile', 'project_commit', 'stock_versions',
        'accepted_runs', 'signon_progression', 'canonical_transcript',
        'request_layouts',
        'snapshot_summary', 'duplicate_drop_behavior', 'privacy',
        'external_file_drift') -Label 'entity-snapshot projection'
    $expectedSchema = if ($IsTrackedProjection) {
        $projectionSchema
    } else { $candidateSchema }
    if ([string]$Value.schema -cne $expectedSchema -or
        [string]$Value.profile -cne $stockProfile -or
        [string]$Value.project_commit -cnotmatch '^[0-9a-f]{40}$' -or
        [string]$Value.external_file_drift -cne 'none') {
        throw 'Entity-snapshot projection identity/drift is invalid.'
    }
    Assert-ExactProperties -Value $Value.stock_versions -Allowed @(
        'client_file_version', 'server_file_version', 'protocol',
        'server_build') -Label 'stock versions'
    if ([string]$Value.stock_versions.client_file_version -cne '1, 1, 1, 1' -or
        [string]$Value.stock_versions.server_file_version -cne '4, 1, 1, 1' -or
        (Get-BoundedInteger $Value.stock_versions.protocol 'stock protocol' 48 48) -ne 48 -or
        (Get-BoundedInteger $Value.stock_versions.server_build 'stock server build' 10210 10210) -ne 10210) {
        throw 'Projection uses an unsupported stock version profile.'
    }

    $runs = @($Value.accepted_runs)
    if ($runs.Count -lt 1 -or $runs.Count -gt $maximumAcceptedRuns) {
        throw 'Projection accepted-run count is outside its bound.'
    }
    $runIds = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    $scenarioCounts = @{}
    foreach ($scenarioName in $allowedScenarios) { $scenarioCounts[$scenarioName] = 0 }
    $captureResults = [Collections.Generic.List[object]]::new()
    foreach ($run in $runs) {
        Assert-ExactProperties -Value $run -Allowed @(
            'run_id', 'scenario', 'map_category', 'scenario_result',
            'capture_metadata_sha256', 'restoration_attestation_sha256') `
            -Label 'accepted run'
        $runIdValue = Get-StrictSha256 $run.run_id 'accepted run id'
        if (-not $runIds.Add($runIdValue)) {
            throw 'Projection contains a duplicate accepted run id.'
        }
        $scenarioName = [string]$run.scenario
        if ($scenarioName -cnotin $allowedScenarios) {
            throw 'Projection contains an unsupported stock scenario.'
        }
        $scenarioCounts[$scenarioName]++
        if ([string]$run.map_category -cnotin $allowedMaps) {
            throw 'Projection contains an unsupported map category.'
        }
        if ($expectedScenarioMaps.Contains($scenarioName)) {
            if ([string]$run.map_category -cne
                [string]$expectedScenarioMaps[$scenarioName]) {
                throw 'Projection scenario/map correlation is invalid.'
            }
        }
        elseif ([string]$run.map_category -ceq 'not-applicable') {
            throw 'Projection scenario requires one exact stock map category.'
        }
        if ([string]$run.scenario_result -cne
            [string]$expectedScenarioResults[$scenarioName]) {
            throw 'Projection run has an invalid scenario outcome.'
        }
        [void](Get-StrictSha256 $run.capture_metadata_sha256 `
            'capture metadata hash')
        $attestationHash = Get-StrictSha256 $run.restoration_attestation_sha256 `
            'restoration attestation hash'
        if (-not $IsTrackedProjection) {
            $runRoot = [IO.Path]::GetFullPath((Join-Path $captureRoot $runIdValue))
            Assert-PathBelowRoot -Path $runRoot -Root $captureRoot `
                -Label 'accepted run directory'
            if (-not (Test-Path -LiteralPath $runRoot -PathType Container)) {
                throw 'Accepted run directory is absent.'
            }
            Assert-NoReparsePointInExistingPath -Path $runRoot `
                -Label 'accepted run directory'
            Assert-NoDescendantReparsePoint -Path $runRoot `
                -Label 'accepted run directory'
            $captureMetadataPath = Join-Path $runRoot 'capture-metadata.json'
            $captureMetadata = Assert-CaptureMetadata -Path $captureMetadataPath `
                -ExpectedRunId $runIdValue -ExpectedScenario $scenarioName `
                -ExpectedMap ([string]$run.map_category)
            if ([string]$captureMetadata.results.scenario_result -cne
                [string]$run.scenario_result) {
                throw 'Candidate scenario outcome disagrees with capture metadata.'
            }
            $captureResults.Add($captureMetadata.results)
            if ((Get-FileSha256Hex -Path $captureMetadataPath) -cne
                [string]$run.capture_metadata_sha256) {
                throw 'Capture metadata hash disagrees with the candidate.'
            }
            $attestationPath = Join-Path $runRoot `
                'research-restoration-attestation.json'
            [void](Assert-RestorationAttestation -Path $attestationPath `
                -ExpectedRunId $runIdValue)
            if ((Get-FileSha256Hex -Path $attestationPath) -cne $attestationHash) {
                throw 'Restoration attestation hash disagrees with the candidate.'
            }
        }
    }
    foreach ($entry in $requiredScenarioMinimums.GetEnumerator()) {
        if ($scenarioCounts[$entry.Key] -lt $entry.Value) {
            throw "Accepted stock scenario '$($entry.Key)' is incomplete."
        }
    }
    foreach ($entry in $optionalScenarioMinimums.GetEnumerator()) {
        $count = $scenarioCounts[$entry.Key]
        if ($count -ne 0 -and $count -lt $entry.Value) {
            throw "Optional stock scenario '$($entry.Key)' is under-sampled."
        }
    }

    $progression = @($Value.signon_progression)
    if ($progression.Count -lt 1 -or $progression.Count -gt 32) {
        throw 'Sign-on progression count is outside its bound.'
    }
    $progressionTokens = [Collections.Generic.List[string]]::new()
    foreach ($entry in $progression) {
        $progressionTokens.Add((Get-StrictToken $entry 'sign-on progression token'))
    }
    foreach ($requiredToken in @(
            'challenge', 'connect', 'resource-signon', 'opcode5-response',
            'signon-complete', 'server-baselines', 'entity-full-snapshot',
            'entity-delta-snapshot')) {
        if ($progressionTokens -cnotcontains $requiredToken) {
            throw "Sign-on progression lacks required token '$requiredToken'."
        }
    }
    $orderedProgression = @(
        'challenge', 'connect', 'resource-signon', 'opcode5-response',
        'server-baselines', 'signon-complete', 'entity-full-snapshot',
        'entity-delta-snapshot')
    [int]$previousProgressionIndex = -1
    foreach ($requiredToken in $orderedProgression) {
        $currentProgressionIndex = $progressionTokens.IndexOf($requiredToken)
        if ($currentProgressionIndex -le $previousProgressionIndex) {
            throw 'Sign-on progression is not in accepted lifecycle order.'
        }
        $previousProgressionIndex = $currentProgressionIndex
    }

    $messages = @($Value.canonical_transcript)
    if ($messages.Count -lt 1 -or $messages.Count -gt $maximumTranscriptMessages) {
        throw 'Canonical transcript count is outside its bound.'
    }
    for ($index = 0; $index -lt $messages.Count; $index++) {
        Assert-TranscriptMessage -Message $messages[$index] -Ordinal $index
    }
    $layouts = @($Value.request_layouts)
    if ($layouts.Count -lt 1 -or $layouts.Count -gt $maximumRequestLayouts) {
        throw 'Client request-layout count is outside its bound.'
    }
    for ($index = 0; $index -lt $layouts.Count; $index++) {
        Assert-RequestLayout -Layout $layouts[$index] -Ordinal $index
    }
    if (-not $IsTrackedProjection) {
        $progressionHash = Get-CanonicalJsonSha256Hex -Value $progression
        $transcriptHash = Get-CanonicalJsonSha256Hex -Value $messages
        $requestLayoutsHash = Get-CanonicalJsonSha256Hex -Value $layouts
        foreach ($result in $captureResults) {
            if ([string]$result.signon_progression_sha256 -cne
                    $progressionHash -or
                [string]$result.canonical_transcript_sha256 -cne
                    $transcriptHash -or
                [string]$result.request_layouts_sha256 -cne
                    $requestLayoutsHash) {
                throw 'Candidate lifecycle structures disagree with a linked capture.'
            }
        }
    }

    Assert-ExactProperties -Value $Value.snapshot_summary -Allowed @(
        'baseline_count_min', 'baseline_count_max', 'schema_distribution',
        'full_entity_count_min', 'full_entity_count_max',
        'delta_snapshot_count_min', 'delta_changed_count_min',
        'delta_added_count_min', 'delta_removed_count_min',
        'message_reference_width_bits', 'snapshot_reference_width_bits',
        'clientdata_present', 'weapondata_present') -Label 'snapshot summary'
    $minimumBaseline = Get-BoundedInteger $Value.snapshot_summary.baseline_count_min `
        'minimum baseline count' 1 8192
    $maximumBaseline = Get-BoundedInteger $Value.snapshot_summary.baseline_count_max `
        'maximum baseline count' 1 8192
    $minimumEntities = Get-BoundedInteger $Value.snapshot_summary.full_entity_count_min `
        'minimum full entity count' 1 8192
    $maximumEntities = Get-BoundedInteger $Value.snapshot_summary.full_entity_count_max `
        'maximum full entity count' 1 8192
    if ($minimumBaseline -gt $maximumBaseline -or
        $minimumEntities -gt $maximumEntities) {
        throw 'Snapshot summary min/max ranges are invalid.'
    }
    $minimumDeltaSnapshots = Get-BoundedInteger `
        $Value.snapshot_summary.delta_snapshot_count_min `
        'snapshot summary delta_snapshot_count_min' 1 8192
    $minimumDeltaChanged = Get-BoundedInteger `
        $Value.snapshot_summary.delta_changed_count_min `
        'snapshot summary delta_changed_count_min' 0 8192
    $minimumDeltaAdded = Get-BoundedInteger `
        $Value.snapshot_summary.delta_added_count_min `
        'snapshot summary delta_added_count_min' 0 8192
    $minimumDeltaRemoved = Get-BoundedInteger `
        $Value.snapshot_summary.delta_removed_count_min `
        'snapshot summary delta_removed_count_min' 0 8192
    $messageReferenceWidth = Get-BoundedInteger `
        $Value.snapshot_summary.message_reference_width_bits `
        'snapshot summary message_reference_width_bits' 1 64
    $snapshotReferenceWidth = Get-BoundedInteger `
        $Value.snapshot_summary.snapshot_reference_width_bits `
        'snapshot summary snapshot_reference_width_bits' 1 64
    $clientdataPresent = Get-StrictBoolean `
        $Value.snapshot_summary.clientdata_present 'clientdata presence'
    $weapondataPresent = Get-StrictBoolean `
        $Value.snapshot_summary.weapondata_present 'weapondata presence'
    $schemaDistribution = @($Value.snapshot_summary.schema_distribution)
    if ($schemaDistribution.Count -lt 1 -or
        $schemaDistribution.Count -gt $maximumSchemaCategories) {
        throw 'Schema distribution count is outside its bound.'
    }
    $schemaNames = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    $schemaRanges = [Collections.Generic.Dictionary[string, object]]::new(
        [StringComparer]::Ordinal)
    foreach ($entry in $schemaDistribution) {
        Assert-ExactProperties -Value $entry -Allowed @(
            'category', 'count_min', 'count_max') -Label 'schema distribution entry'
        $category = Get-StrictToken $entry.category 'schema category'
        if (-not $schemaNames.Add($category)) {
            throw 'Schema distribution contains a duplicate category.'
        }
        $minimum = Get-BoundedInteger $entry.count_min `
            'schema minimum count' 0 8192
        $maximum = Get-BoundedInteger $entry.count_max `
            'schema maximum count' 0 8192
        if ($minimum -gt $maximum) {
            throw 'Schema distribution min/max range is invalid.'
        }
        $schemaRanges.Add($category, [pscustomobject]@{
            Minimum = $minimum
            Maximum = $maximum
        })
    }

    if (-not $IsTrackedProjection) {
        $baselineCounts = @($captureResults | ForEach-Object {
            [Int64]$_.baseline_count
        })
        $fullEntityCounts = @($captureResults | ForEach-Object {
            [Int64]$_.full_entity_count
        })
        $deltaSnapshotCounts = @($captureResults | ForEach-Object {
            [Int64]$_.delta_snapshot_count
        })
        $deltaChangedCounts = @($captureResults | ForEach-Object {
            [Int64]$_.delta_changed_count
        })
        $deltaAddedCounts = @($captureResults | ForEach-Object {
            [Int64]$_.delta_added_count
        })
        $deltaRemovedCounts = @($captureResults | ForEach-Object {
            [Int64]$_.delta_removed_count
        })
        $minimumOf = {
            param([object[]]$Values)
            return [Int64](($Values | Measure-Object -Minimum).Minimum)
        }
        $maximumOf = {
            param([object[]]$Values)
            return [Int64](($Values | Measure-Object -Maximum).Maximum)
        }
        if ((& $minimumOf $baselineCounts) -ne $minimumBaseline -or
            (& $maximumOf $baselineCounts) -ne $maximumBaseline -or
            (& $minimumOf $fullEntityCounts) -ne $minimumEntities -or
            (& $maximumOf $fullEntityCounts) -ne $maximumEntities -or
            (& $minimumOf $deltaSnapshotCounts) -ne $minimumDeltaSnapshots -or
            (& $minimumOf $deltaChangedCounts) -ne $minimumDeltaChanged -or
            (& $minimumOf $deltaAddedCounts) -ne $minimumDeltaAdded -or
            (& $minimumOf $deltaRemovedCounts) -ne $minimumDeltaRemoved) {
            throw 'Candidate snapshot summary disagrees with linked captures.'
        }

        $messageWidths = @($captureResults | ForEach-Object {
            [Int64]$_.message_reference_width_bits
        } | Select-Object -Unique)
        $snapshotWidths = @($captureResults | ForEach-Object {
            [Int64]$_.snapshot_reference_width_bits
        } | Select-Object -Unique)
        if ($messageWidths.Count -ne 1 -or
            $messageWidths[0] -ne $messageReferenceWidth -or
            $snapshotWidths.Count -ne 1 -or
            $snapshotWidths[0] -ne $snapshotReferenceWidth) {
            throw 'Candidate reference widths disagree with linked captures.'
        }

        $capturedClientdata = $false
        $capturedWeapondata = $false
        $observedSchemaNames = [Collections.Generic.HashSet[string]]::new(
            [StringComparer]::Ordinal)
        foreach ($result in $captureResults) {
            if (Get-StrictBoolean $result.clientdata_present `
                    'linked capture clientdata presence') {
                $capturedClientdata = $true
            }
            if (Get-StrictBoolean $result.weapondata_present `
                    'linked capture weapondata presence') {
                $capturedWeapondata = $true
            }
            foreach ($entry in @($result.baseline_schema_distribution)) {
                [void]$observedSchemaNames.Add([string]$entry.category)
            }
        }
        if ($capturedClientdata -ne $clientdataPresent -or
            $capturedWeapondata -ne $weapondataPresent) {
            throw 'Candidate client frame presence disagrees with linked captures.'
        }
        if ($observedSchemaNames.Count -ne $schemaRanges.Count) {
            throw 'Candidate schema categories disagree with linked captures.'
        }
        foreach ($category in $observedSchemaNames) {
            if (-not $schemaRanges.ContainsKey($category)) {
                throw 'Candidate omits an observed baseline schema category.'
            }
            $counts = [Collections.Generic.List[Int64]]::new()
            foreach ($result in $captureResults) {
                [Int64]$count = 0
                foreach ($entry in @($result.baseline_schema_distribution)) {
                    if ([string]$entry.category -ceq $category) {
                        $count = [Int64]$entry.count
                        break
                    }
                }
                $counts.Add($count)
            }
            $range = $schemaRanges[$category]
            if ((& $minimumOf @($counts)) -ne $range.Minimum -or
                (& $maximumOf @($counts)) -ne $range.Maximum) {
                throw 'Candidate schema distribution disagrees with linked captures.'
            }
        }
    }

    Assert-ExactProperties -Value $Value.duplicate_drop_behavior -Allowed @(
        'client_request_retransmission', 'covering_ack_retry',
        'duplicate_server_batch_idempotence', 'dropped_full_snapshot_recovery',
        'dropped_delta_snapshot_recovery', 'old_snapshot_replay_policy') `
        -Label 'duplicate/drop behavior'
    foreach ($property in $Value.duplicate_drop_behavior.PSObject.Properties) {
        $token = Get-StrictToken $property.Value `
            "duplicate/drop behavior $($property.Name)"
        if (-not $expectedDuplicateDropBehavior.Contains($property.Name) -or
            $token -cne [string]$expectedDuplicateDropBehavior[$property.Name]) {
            throw 'Projection duplicate/drop behavior is not the exact accepted outcome.'
        }
    }
    Assert-ExactProperties -Value $Value.privacy -Allowed @(
        'raw_packet_bodies', 'raw_snapshot_bodies', 'authentication_material',
        'identity_data', 'endpoint_data', 'game_values') -Label 'privacy projection'
    foreach ($property in $Value.privacy.PSObject.Properties) {
        if (Get-StrictBoolean $property.Value "privacy $($property.Name)") {
            throw 'Tracked projection must not contain sensitive/raw values.'
        }
    }
}

function Read-BoundedJsonFile {
    param([string]$Path, [int]$MaximumBytes, [string]$Label)
    Assert-NoReparsePointInExistingPath -Path $Path -Label $Label
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or
        (Get-Item -LiteralPath $Path).Length -gt $MaximumBytes) {
        throw "$Label is absent or outside its byte bound."
    }
    Assert-NoReparsePoint -Path $Path -Label $Label
    Assert-OnlyDefaultDataStream -Path $Path -Label $Label
    return Get-Content -Raw -LiteralPath $Path |
        ConvertFrom-Json -ErrorAction Stop
}

function Write-RestorationAttestation {
    param(
        [string]$RunRoot,
        [string]$RunIdentifier,
        [object]$Before,
        [object]$Restoration,
        [int]$ExitCode)
    if ($Restoration.Drift -cne 'none') {
        throw 'Research restoration detected external-file drift.'
    }
    $value = [pscustomobject][ordered]@{
        schema = $restorationSchema
        run_id = $RunIdentifier
        isolated_copy = $true
        external_file_drift = 'none'
        snapshot_entry_count = @($Before.Entries).Count
        restored_entry_count = $Restoration.RestoredEntryCount
        created_file_count = $Restoration.CreatedFileCount
        created_file_removal_count = $Restoration.CreatedFileRemovalCount
        pre_manifest_sha256 = $Before.ManifestSha256
        post_manifest_sha256 = $Restoration.After.ManifestSha256
        mutable_scope = 'cfg-hpk-logs-save-config-vdf-and-created-files'
        capture_process_exit_code = $ExitCode
    }
    $path = Join-Path $RunRoot 'research-restoration-attestation.json'
    $temporary = $path + '.tmp'
    if (Test-Path -LiteralPath $path) {
        throw 'Capture harness must not pre-create the restoration attestation.'
    }
    $encoding = New-Object Text.UTF8Encoding($false)
    try {
        [IO.File]::WriteAllText(
            $temporary, ($value | ConvertTo-Json -Depth 4) + "`r`n", $encoding)
        if ((Get-Item -LiteralPath $temporary).Length -gt $maximumAttestationBytes) {
            throw 'Generated restoration attestation exceeds its byte bound.'
        }
        Move-Item -LiteralPath $temporary -Destination $path
    }
    finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
    [void](Assert-RestorationAttestation -Path $path -ExpectedRunId $RunIdentifier)
}

$gitIgnorePath = Join-Path $repositoryRoot '.gitignore'
if (-not (Test-Path -LiteralPath $gitIgnorePath -PathType Leaf) -or
    (Get-Content -Raw -LiteralPath $gitIgnorePath) -cnotmatch
        '(?m)^/manual-artifacts/\s*$') {
    throw 'Verifier requires the repository-wide /manual-artifacts/ ignore rule.'
}

if ($PSCmdlet.ParameterSetName -ceq 'Pending') {
    if (Test-Path -LiteralPath $projectionPath) {
        throw 'Tracked stock evidence exists; validate it explicitly instead.'
    }
    Write-Output (
        'stock-entity-snapshot-evidence=pending accepted-active-stock-runs=0 ' +
        'tracked-evidence-created=false processes-started=0 ' +
        'external-file-drift=none')
    return
}

if ($PSCmdlet.ParameterSetName -ceq 'Guided') {
    $research = Assert-IsolatedResearchRoot -Path $ResearchHalfLifeRoot `
        -ExplicitClientPath $ClientPath -ExplicitHldsPath $HldsPath
    if (Test-Path -LiteralPath $projectionPath) {
        throw 'Unexpected tracked stock evidence exists while promotion is disabled.'
    }
    Write-Output (
        'research-root-valid isolated-copy=true primary-steam=false ' +
        'canonical-client=true canonical-hlds=true valve-signatures=valid ' +
        "game=$Game map=$Map stock-entity-snapshot-evidence=pending " +
        'capture-harness=absent typed-observation-trace=absent ' +
        'restoration-attestation=not-created processes-started=0 ' +
        'external-file-drift=none preflight-only=true')
    return
}

if ($PSCmdlet.ParameterSetName -ceq 'Isolation') {
    $research = Assert-IsolatedResearchRoot -Path $ResearchHalfLifeRoot `
        -ExplicitClientPath $ClientPath -ExplicitHldsPath $HldsPath
    Write-Output (
        'research-root-valid isolated-copy=true primary-steam=false ' +
        'canonical-client=true canonical-hlds=true valve-signatures=valid ' +
        'processes-started=0 preflight-only=true')
    return
}

if ($PSCmdlet.ParameterSetName -ceq 'Validate') {
    throw (
        'Tracked stock projection validation is disabled until a reviewed ' +
        'capture harness publishes per-run typed scenario observations.')
    $path = [IO.Path]::GetFullPath($ValidateMetadataPath)
    if ($path -cne $projectionPath) {
        throw 'Metadata validation requires the exact tracked evidence path.'
    }
    $metadata = Read-BoundedJsonFile -Path $path `
        -MaximumBytes $maximumProjectionBytes -Label 'tracked projection'
    Assert-ProjectionCandidate -Value $metadata -IsTrackedProjection $true
    Write-Output (
        'stock-entity-snapshot-metadata=valid raw-bodies=false auth=false ' +
        'identity=false external-file-drift=none')
    return
}

if ($PSCmdlet.ParameterSetName -ceq 'Project') {
    throw (
        'Stock projection is disabled until a reviewed capture harness ' +
        'publishes per-run typed scenario observations.')
    if (-not (Test-Path -LiteralPath $captureRoot -PathType Container)) {
        throw 'No restoration-attested stock entity-snapshot captures are available.'
    }
    Assert-NoReparsePointInExistingPath -Path $captureRoot `
        -Label 'ignored capture root'
    Assert-NoDescendantReparsePoint -Path $captureRoot `
        -Label 'ignored capture root'
    $candidate = Read-BoundedJsonFile -Path $candidatePath `
        -MaximumBytes $maximumCandidateBytes -Label 'sanitized projection candidate'
    Assert-ProjectionCandidate -Value $candidate -IsTrackedProjection $false

    $candidate.schema = $projectionSchema
    Assert-ProjectionCandidate -Value $candidate -IsTrackedProjection $true
    if (-not (Test-Path -LiteralPath $projectionRoot -PathType Container)) {
        [IO.Directory]::CreateDirectory($projectionRoot) | Out-Null
    }
    Assert-NoReparsePointInExistingPath -Path $projectionRoot `
        -Label 'tracked projection root'
    Assert-NoReparsePoint -Path $projectionRoot -Label 'tracked projection root'
    $temporary = $projectionPath + '.tmp'
    if (Test-Path -LiteralPath $temporary) {
        throw 'Refusing to overwrite an unexpected temporary projection file.'
    }
    $encoding = New-Object Text.UTF8Encoding($false)
    try {
        [IO.File]::WriteAllText(
            $temporary, ($candidate | ConvertTo-Json -Depth 16) + "`r`n", $encoding)
        if ((Get-Item -LiteralPath $temporary).Length -gt $maximumProjectionBytes) {
            throw 'Generated tracked projection exceeds its byte bound.'
        }
        $roundTrip = Read-BoundedJsonFile -Path $temporary `
            -MaximumBytes $maximumProjectionBytes -Label 'temporary projection'
        Assert-ProjectionCandidate -Value $roundTrip -IsTrackedProjection $true
        Move-Item -LiteralPath $temporary -Destination $projectionPath -Force
    }
    finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
    Write-Output (
        'stock-entity-snapshot-projection=created restoration-attested=true ' +
        'raw-bodies=false auth=false identity=false external-file-drift=none')
    return
}

if ($PSCmdlet.ParameterSetName -ceq 'Capture') {
    $research = Assert-IsolatedResearchRoot -Path $ResearchHalfLifeRoot `
        -ExplicitClientPath $ClientPath -ExplicitHldsPath $HldsPath
    if (@(Get-LiveGoldSrcProcesses).Count -ne 0) {
        throw 'Capture preflight requires no live hl.exe or hlds.exe process.'
    }
    $harness = Resolve-CanonicalFile -Path $CaptureHarnessPath `
        -Root $repositoryRoot -Label 'capture harness'
    if ([IO.Path]::GetFileName($harness) -cne
            'hlclient_stock_entity_snapshot_capture.exe' -or
        $harness.StartsWith(
            $research.Root + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase) -or
        (Get-FileSha256Hex -Path $harness) -cne
            $CaptureHarnessSha256.ToUpperInvariant()) {
        throw 'Capture harness is not the exact hash-pinned repository executable.'
    }
    if (-not (Test-Path -LiteralPath $captureRoot -PathType Container)) {
        [IO.Directory]::CreateDirectory($captureRoot) | Out-Null
    }
    Assert-NoReparsePointInExistingPath -Path $captureRoot `
        -Label 'ignored capture root'
    Assert-NoReparsePoint -Path $captureRoot -Label 'ignored capture root'
    $normalizedRunId = $RunId.ToUpperInvariant()
    $runRoot = [IO.Path]::GetFullPath((Join-Path $captureRoot $normalizedRunId))
    Assert-PathBelowRoot -Path $runRoot -Root $captureRoot `
        -Label 'capture run directory'
    if (Test-Path -LiteralPath $runRoot) {
        throw 'Capture run directory already exists.'
    }
    [IO.Directory]::CreateDirectory($runRoot) | Out-Null

    $temporaryBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    $backupRoot = [IO.Path]::Combine(
        $temporaryBase, '.hlclient-entity-restore-' + [Guid]::NewGuid().ToString('N'))
    Assert-PathBelowRoot -Path $backupRoot -Root $temporaryBase `
        -Label 'temporary restoration root'
    [IO.Directory]::CreateDirectory($backupRoot) | Out-Null
    $before = $null
    $restoration = $null
    $captureError = $null
    $researchSafeToRestore = $false
    [int]$exitCode = 255
    $process = $null
    try {
        $before = Get-ResearchSnapshot -Root $research.Root
        Backup-MutableResearchState -Snapshot $before -Root $research.Root `
            -BackupRoot $backupRoot
        $arguments = @(
            '--research-root', ('"{0}"' -f $research.Root),
            '--client', ('"{0}"' -f $research.Client),
            '--server', ('"{0}"' -f $research.Server),
            '--output-run-root', ('"{0}"' -f $runRoot),
            '--scenario', $Scenario,
            '--max-seconds', [string]$CaptureTimeoutSeconds,
            '--max-packets', [string]$MaximumPackets,
            '--max-packet-bytes', [string]$MaximumPacketBytes,
            '--max-total-bytes', [string]$MaximumCaptureBytes,
            '--private-ipv4-loopback-only', '--one-upstream-socket',
            '--byte-preserving', '--exact-endpoint-validation',
            '--no-packet-rewrite')
        $process = Start-Process -FilePath $harness -ArgumentList $arguments `
            -PassThru -WindowStyle Hidden
        if (-not $process.WaitForExit($CaptureTimeoutSeconds * 1000)) {
            throw 'Capture harness exceeded its bounded timeout.'
        }
        $exitCode = $process.ExitCode
        if ($exitCode -ne 0) {
            throw "Capture harness returned nonzero exit code $exitCode."
        }
    }
    catch { $captureError = $_ }
    finally {
        if ($null -ne $process -and -not $process.HasExited) {
            try {
                $process.Kill()
                if (-not $process.WaitForExit(5000)) {
                    throw 'Capture harness did not stop after termination.'
                }
            }
            catch {
                if ($null -eq $captureError) { $captureError = $_ }
            }
        }
        try {
            Stop-OwnedResearchProcesses -Research $research
            Assert-NoReparsePointInExistingPath -Path $research.Root `
                -Label 'post-capture research root'
            Assert-NoReparsePoint -Path $research.Root `
                -Label 'post-capture research root'
            Assert-NoDescendantReparsePoint -Path $research.Root `
                -Label 'post-capture research root'
            $researchSafeToRestore = $true
        }
        catch {
            if ($null -eq $captureError) { $captureError = $_ }
            else {
                $captureError = [Management.Automation.ErrorRecord]::new(
                    [InvalidOperationException]::new(
                        ($captureError.Exception.Message +
                        ' Process cleanup also failed: ' + $_.Exception.Message)),
                    'CaptureAndProcessCleanupFailure',
                    [Management.Automation.ErrorCategory]::InvalidResult,
                    $research.Root)
            }
        }
        if ($null -ne $before -and $researchSafeToRestore) {
            try {
                $restoration = Restore-ResearchState -Before $before `
                    -Root $research.Root -BackupRoot $backupRoot
            }
            catch {
                if ($null -eq $captureError) { $captureError = $_ }
                else {
                    $captureError = [Management.Automation.ErrorRecord]::new(
                        [InvalidOperationException]::new(
                            ($captureError.Exception.Message +
                            ' Restoration also failed: ' + $_.Exception.Message)),
                        'CaptureAndRestorationFailure',
                        [Management.Automation.ErrorCategory]::InvalidResult,
                        $research.Root)
                }
            }
        }
        if ($null -ne $restoration -and $restoration.Drift -ceq 'none' -and
            (Test-Path -LiteralPath $backupRoot)) {
            try {
                Assert-PathBelowRoot -Path $backupRoot -Root $temporaryBase `
                    -Label 'temporary restoration cleanup root'
                Assert-NoReparsePointInExistingPath -Path $backupRoot `
                    -Label 'temporary restoration cleanup root'
                Assert-NoReparsePoint -Path $backupRoot `
                    -Label 'temporary restoration cleanup root'
                Assert-NoDescendantReparsePoint -Path $backupRoot `
                    -Label 'temporary restoration cleanup root'
                Remove-Item -LiteralPath $backupRoot -Recurse -Force
            }
            catch {
                Write-Warning (
                    "Restoration backup retained for manual recovery at '$backupRoot'.")
                if ($null -eq $captureError) { $captureError = $_ }
                else {
                    $captureError = [Management.Automation.ErrorRecord]::new(
                        [InvalidOperationException]::new(
                            ($captureError.Exception.Message +
                            ' Backup cleanup also failed: ' +
                            $_.Exception.Message)),
                        'CaptureAndBackupCleanupFailure',
                        [Management.Automation.ErrorCategory]::InvalidResult,
                        $backupRoot)
                }
            }
        }
        elseif (Test-Path -LiteralPath $backupRoot) {
            Write-Warning (
                "Restoration backup retained for manual recovery at '$backupRoot'.")
        }
    }
    if ($null -eq $restoration -or $restoration.Drift -cne 'none') {
        if ($null -ne $captureError) { throw $captureError }
        throw 'Capture did not produce a successful restoration result.'
    }
    if ($null -ne $captureError) { throw $captureError }
    Write-RestorationAttestation -RunRoot $runRoot `
        -RunIdentifier $normalizedRunId -Before $before `
        -Restoration $restoration -ExitCode $exitCode
    Write-Output (
        'bounded-stock-entity-capture=completed restoration-attested=true ' +
        'external-file-drift=none tracked-evidence-created=false')
    return
}

throw 'Unsupported verifier parameter set.'
