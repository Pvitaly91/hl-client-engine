#requires -Version 5.1

<#
.SYNOPSIS
Verifies a bounded stock-runtime first-observation corpus.

.DESCRIPTION
Enumerates exact ignored run IDs, validates final manifests/attestations, runs
the production checker twice, runs the independent PowerShell walker and
compares structural summaries. It never creates evidence or prints raw bytes.
Threshold failure is explicit and leaves the evidence JSON absent.
#>
[CmdletBinding(DefaultParameterSetName = 'Corpus')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Corpus')]
    [ValidateNotNullOrEmpty()]
    [string]$CaptureRoot,

    [Parameter(Mandatory = $true, ParameterSetName = 'Corpus')]
    [ValidateNotNullOrEmpty()]
    [string]$CheckerPath,

    [Parameter(ParameterSetName = 'Corpus')]
    [ValidateRange(1, 4096)]
    [int]$MinimumAcceptedRuns = 24,

    [Parameter(ParameterSetName = 'Corpus')]
    [ValidateRange(1, 100000000)]
    [Int64]$MinimumSequencedServerPackets = 1000,

    [Parameter(Mandatory = $true, ParameterSetName = 'EvidencePolicy')]
    [switch]$ValidateEvidencePolicy
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
$requiredCaptureRoot = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'manual-artifacts\stock-runtime')).TrimEnd('\', '/')
$evidencePath = Join-Path $repositoryRoot `
    'docs\evidence\GOLDSRC_STOCK_RUNTIME_FIRST_OBSERVATIONS.json'
$walkerPath = Join-Path $PSScriptRoot 'walk_stock_runtime_transport.ps1'
$maximumRuns = 4096

function Assert-NoReparsePointInExistingPath {
    param([string]$Path, [string]$Label)
    $full = [IO.Path]::GetFullPath($Path)
    $pathRoot = [IO.Path]::GetPathRoot($full)
    $current = $pathRoot
    foreach ($component in @($full.Substring($pathRoot.Length) -split '[\\/]' |
            Where-Object { $_ })) {
        $current = [IO.Path]::Combine($current, $component)
        if (-not (Test-Path -LiteralPath $current)) { break }
        $item = Get-Item -LiteralPath $current -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Label contains a reparse point."
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

function Assert-NoHardLink {
    param([string]$Path, [string]$Label)
    $item = Get-Item -LiteralPath $Path -Force
    $property = $item.PSObject.Properties['LinkType']
    if ($null -eq $property -or -not [string]::IsNullOrEmpty([string]$property.Value)) {
        throw "$Label must be an unlinked regular file."
    }
}

function Read-BoundedJson {
    param([string]$Path, [int]$MaximumBytes, [string]$Label)
    Assert-NoReparsePointInExistingPath $Path $Label
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Label is absent." }
    Assert-OnlyDefaultDataStream $Path $Label
    Assert-NoHardLink $Path $Label
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.Length -lt 2 -or $item.Length -gt $MaximumBytes) {
        throw "$Label length is outside its bound."
    }
    try { return Get-Content -Raw -LiteralPath $Path -Encoding UTF8 | ConvertFrom-Json }
    catch { throw "$Label is invalid JSON." }
}

function Assert-ExactFilePair {
    param([string]$StagedPath, [string]$FinalPath, [string]$Label)
    $staged = Get-Item -LiteralPath $StagedPath -Force
    $final = Get-Item -LiteralPath $FinalPath -Force
    if ($staged.Length -ne $final.Length -or
        (Get-FileHash -LiteralPath $StagedPath -Algorithm SHA256).Hash -cne
            (Get-FileHash -LiteralPath $FinalPath -Algorithm SHA256).Hash) {
        throw "$Label staged/final leaves are not byte-identical."
    }
}

function Get-StrictInteger {
    param([object]$Value, [string]$Name, [Int64]$Minimum, [Int64]$Maximum)
    $property = $Value.PSObject.Properties[$Name]
    if ($null -eq $property -or $property.Value -is [bool] -or
        $property.Value -isnot [ValueType]) { throw "$Name is not an integer." }
    [Int64]$number = $property.Value
    if ([double]$property.Value -ne [double]$number -or
        $number -lt $Minimum -or $number -gt $Maximum) {
        throw "$Name is outside its bound."
    }
    return $number
}

function Assert-ExactProperties {
    param([object]$Value, [string[]]$Allowed, [string]$Label)
    if ($null -eq $Value -or $Value -is [ValueType] -or $Value -is [string]) {
        throw "$Label must be a JSON object."
    }
    $names = @($Value.PSObject.Properties.Name)
    if ($names.Count -ne $Allowed.Count -or
        @($names | Sort-Object -Unique).Count -ne $Allowed.Count) {
        throw "$Label has duplicate, missing or unknown properties."
    }
    foreach ($name in $names) {
        if ($Allowed -cnotcontains $name) {
            throw "$Label contains forbidden property '$name'."
        }
    }
}

function Assert-SanitizedEvidenceText {
    param([string]$Text)
    # The exact object allowlists below are the primary policy. These lexical
    # checks additionally fail closed on accidental sensitive field names or
    # values before any evidence is accepted.
    $propertyNames = @([regex]::Matches(
            $Text, '"(?<name>[A-Za-z0-9_-]+)"\s*:') |
        ForEach-Object { $_.Groups['name'].Value })
    $forbiddenProperty = @($propertyNames | Where-Object {
            $_ -match '(?i)(?:^|[_-])(?:raw|auth(?:entication)?|ticket|player|steam[_-]?id|user[_-]?id|identity|fingerprint|path|(?:ip|ipv4|ipv6)[_-]?address|port|config|process[_-]?log|entity|screenshot)(?:$|[_-])'
        }).Count -ne 0
    if ($forbiddenProperty -or
        $Text -match '(?i)(?:[A-Z]:\\|/Users/|\\Users\\|steamapps[\\/]|https?://|\b(?:127\.0\.0\.1|10\.[0-9.]+|192\.168\.[0-9.]+|172\.(?:1[6-9]|2[0-9]|3[01])\.[0-9.]+|::1)\b)' -or
        $Text -match '(?i)"[^"]*(?:steam ticket|authentication bytes|player name|raw payload|process log|private config|screenshot)[^"]*"') {
        throw 'First-observation evidence contains a forbidden field or value.'
    }
}

function Assert-FirstObservationGeometry {
    param(
        [object]$Boundary,
        [Int64]$CandidateBitWidth,
        [string]$FirstCandidate,
        [string]$Label)
    [void](Get-StrictInteger $Boundary replay_payload_ordinal 0 65536)
    [void](Get-StrictInteger $Boundary corpus_observed_ordinal 0 65535)
    [void](Get-StrictInteger $Boundary delivery_ordinal 0 131071)
    [void](Get-StrictInteger $Boundary byte_offset 0 1048576)
    [void](Get-StrictInteger $Boundary bit_offset 0 7)
    [void](Get-StrictInteger $Boundary source_netchan_sequence 0 1073741823)
    [void](Get-StrictInteger $Boundary source_payload_byte_count 1 1048576)
    [void](Get-StrictInteger $Boundary source_payload_bit_count 8 8388608)
    [void](Get-StrictInteger $Boundary next_unconsumed_bit_count 1 8388608)
    if ($Boundary.reassembled -isnot [bool] -or
        $Boundary.decompressed -isnot [bool] -or
        $Boundary.byte_aligned -isnot [bool] -or
        [Int64]$Boundary.bit_offset -gt 7 -or
        [Int64]$Boundary.source_payload_byte_count -lt 1 -or
        [Int64]$Boundary.source_payload_bit_count -ne
            ([Int64]$Boundary.source_payload_byte_count * 8) -or
        (([Int64]$Boundary.byte_offset * 8) + [Int64]$Boundary.bit_offset +
            [Int64]$Boundary.next_unconsumed_bit_count) -ne
                [Int64]$Boundary.source_payload_bit_count -or
        [Int64]$Boundary.next_unconsumed_bit_count -lt 1 -or
        [bool]$Boundary.byte_aligned -ne ([Int64]$Boundary.bit_offset -eq 0) -or
        $CandidateBitWidth -lt 1 -or $CandidateBitWidth -gt 8 -or
        $CandidateBitWidth -gt [Int64]$Boundary.next_unconsumed_bit_count -or
        $FirstCandidate -cnotmatch '^(?:[0-9]{1,3}|bit-prefix:[0-9]{1,3})$' -or
        [int]($FirstCandidate -replace '^bit-prefix:', '') -gt 255 -or
        (-not $FirstCandidate.StartsWith('bit-prefix:') -and
            (-not [bool]$Boundary.byte_aligned -or $CandidateBitWidth -ne 8)) -or
        ($FirstCandidate.StartsWith('bit-prefix:') -and
            ([bool]$Boundary.byte_aligned -or
                [int]$FirstCandidate.Substring(11) -ge
                    [Math]::Pow(2, $CandidateBitWidth)))) {
        throw "$Label has inconsistent exact cursor/prefix-width geometry."
    }
}

function Convert-PrefixedOutputToValues {
    param([string[]]$Lines, [string]$Prefix, [string[]]$AllowedKeys, [string]$Label)
    if ($Lines.Count -gt 128 -or
        [Text.Encoding]::UTF8.GetByteCount(($Lines -join "`n")) -gt 65536) {
        throw "$Label output exceeded its bound."
    }
    $values = [Collections.Generic.Dictionary[string, string]]::new(
        [StringComparer]::Ordinal)
    foreach ($line in $Lines) {
        if ($line.Length -gt 1024 -or
            $line -cnotmatch ('^' + [regex]::Escape($Prefix) +
                '(?<key>[a-z0-9-]+)=(?<value>[A-Za-z0-9_.:/-]{1,256})$')) {
            throw "$Label emitted a non-contract line."
        }
        $key = $Matches.key
        if ($AllowedKeys -cnotcontains $key -or $values.ContainsKey($key)) {
            throw "$Label emitted an unknown or duplicate key."
        }
        $values.Add($key, $Matches.value)
    }
    return $values
}

function Invoke-Checker {
    param([string]$Path, [string]$RunRoot)
    $saved = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = @(& $Path --capture-root $RunRoot --scenario first-observation 2>&1 |
            ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $saved }
    return [pscustomobject]@{ Lines = $lines; ExitCode = $exitCode }
}

function Get-FirstCandidateStabilityProfile {
    param([object]$Values)
    $keys = @(
        'boundary-bit-offset', 'boundary-byte-aligned',
        'candidate-bit-width', 'first-candidate')
    foreach ($key in $keys) {
        if ($null -eq $Values -or -not $Values.ContainsKey($key)) {
            throw "Candidate stability input lacks '$key'."
        }
    }
    # Transport/delivery ordinals, netchan sequence, payload size and absolute
    # byte offset are occurrence geometry.  They legitimately vary by run and
    # map.  Cross-run candidate identity retains only exact bit alignment,
    # observed prefix width and the neutral candidate representation; the
    # version profile is compared independently below.
    return @($keys | ForEach-Object { [string]$Values[$_] }) -join '|'
}

$canonicalRuntimeScenarios = @(
    'baseline', 'idle-runtime', 'reconnect',
    'drop-server-to-client-transport-ordinal',
    'duplicate-server-to-client-transport-ordinal',
    'reorder-server-to-client-transport-ordinal')
$legacyScenarioAliases = @{
    'drop-server-runtime' = 'drop-server-to-client-transport-ordinal'
    'duplicate-server-runtime' = 'duplicate-server-to-client-transport-ordinal'
    'reorder-server-runtime' = 'reorder-server-to-client-transport-ordinal'
}
$stockRuntimeMapCategories = @('boot_camp', 'crossfire', 'stalkyard')

function Get-CanonicalRuntimeScenario {
    param([string]$Scenario, [string]$Label)
    if ($legacyScenarioAliases.ContainsKey($Scenario)) {
        return [string]$legacyScenarioAliases[$Scenario]
    }
    if ($canonicalRuntimeScenarios -ccontains $Scenario) {
        return $Scenario
    }
    throw "$Label is outside the exact runtime scenario/alias allowlist."
}

function Get-StrictOutputInteger {
    param(
        [object]$Values, [string]$Name, [Int64]$Minimum,
        [Int64]$Maximum, [string]$Label)
    if ($null -eq $Values -or -not $Values.ContainsKey($Name)) {
        throw "$Label lacks '$Name'."
    }
    $text = [string]$Values[$Name]
    [Int64]$number = 0
    if ($text -cnotmatch '^(?:0|[1-9][0-9]*)$' -or
        -not [Int64]::TryParse(
            $text, [Globalization.NumberStyles]::None,
            [Globalization.CultureInfo]::InvariantCulture, [ref]$number) -or
        $number -lt $Minimum -or $number -gt $Maximum) {
        throw "$Label '$Name' is outside its integer contract."
    }
    return $number
}

function Assert-RunEvidenceBindings {
    param(
        [object]$Run, [object]$Capture, [object]$Version,
        [object]$CheckerValues, [object]$WalkerValues)

    $canonicalScenario = Get-CanonicalRuntimeScenario `
        ([string]$Run.scenario) 'Final manifest scenario'
    $capturedScenario = Get-CanonicalRuntimeScenario `
        ([string]$Capture.scenario) 'Capture metadata scenario'
    if ($canonicalScenario -cne $capturedScenario) {
        throw 'Final manifest scenario disagrees with capture metadata.'
    }
    $mapCategory = [string]$Run.map_category
    if ($stockRuntimeMapCategories -cnotcontains $mapCategory -or
        [string]$Version.map_category -cne $mapCategory) {
        throw 'Final manifest map category disagrees with the immutable version/run observation.'
    }

    $rawDatagrams = Get-StrictInteger $Run raw_datagram_count 1 65536
    $journalEntries = Get-StrictInteger $Run journal_entry_count 1 65536
    $captureObserved = Get-StrictInteger $Capture observed_datagrams 1 65536
    $captureMaximumDurationMs = Get-StrictInteger $Capture `
        maximum_duration_ms 1 300000
    $captureRawBytes = Get-StrictInteger $Capture observed_raw_bytes 1 536870912
    $captureClient = Get-StrictInteger $Capture client_packets 0 65536
    $captureServer = Get-StrictInteger $Capture server_packets 0 65536
    $captureEmitted = Get-StrictInteger $Capture emitted_datagrams 1 131072
    $walkerRaw = Get-StrictOutputInteger $WalkerValues 'raw-datagrams' 1 65536 `
        'Independent walker'
    $walkerJournal = Get-StrictOutputInteger $WalkerValues 'journal-entries' 1 65536 `
        'Independent walker'
    $walkerRawBytes = Get-StrictOutputInteger $WalkerValues 'raw-bytes' 1 536870912 `
        'Independent walker'
    $walkerObservedC2s = Get-StrictOutputInteger $WalkerValues 'observed-c2s' 0 65536 `
        'Independent walker'
    $walkerObservedS2c = Get-StrictOutputInteger $WalkerValues 'observed-s2c' 0 65536 `
        'Independent walker'
    $walkerEmitted = Get-StrictOutputInteger $WalkerValues 'emitted-datagrams' 1 131072 `
        'Independent walker'
    if ($rawDatagrams -ne $journalEntries -or
        $rawDatagrams -ne $captureObserved -or
        $rawDatagrams -ne $walkerRaw -or
        $journalEntries -ne $walkerJournal -or
        $captureRawBytes -ne $walkerRawBytes -or
        $captureClient -ne $walkerObservedC2s -or
        $captureServer -ne $walkerObservedS2c -or
        $captureEmitted -ne $walkerEmitted) {
        throw 'Final manifest, capture metadata and walker raw/journal counters disagree.'
    }

    $sequencedC2s = Get-StrictInteger $Run delivered_sequenced_c2s_count 0 131072
    $sequencedS2c = Get-StrictInteger $Run delivered_sequenced_s2c_count 0 131072
    $fragmentDatagrams = Get-StrictInteger $Run `
        delivered_fragment_datagram_count 0 131072
    $reassembledPayloads = Get-StrictInteger $Run reassembled_payload_count 0 131072
    $decompressedPayloads = Get-StrictInteger $Run decompressed_payload_count 0 131072
    $checkerReplaySequencedC2s = Get-StrictOutputInteger $CheckerValues 'sequenced-c2s' `
        0 131072 'Production checker'
    $checkerReplaySequencedS2c = Get-StrictOutputInteger $CheckerValues 'sequenced-s2c' `
        0 131072 'Production checker'
    $checkerReplayFragments = Get-StrictOutputInteger $CheckerValues 'fragments' `
        0 131072 'Production checker'
    $checkerReplayDuplicates = Get-StrictOutputInteger $CheckerValues `
        'duplicate-packets' 0 131072 'Production checker'
    $checkerReplayOld = Get-StrictOutputInteger $CheckerValues `
        'old-packets' 0 131072 'Production checker'
    $checkerDeliveredSequencedC2s = Get-StrictOutputInteger $CheckerValues `
        'delivered-sequenced-c2s' 0 131072 'Production checker'
    $checkerDeliveredSequencedS2c = Get-StrictOutputInteger $CheckerValues `
        'delivered-sequenced-s2c' 0 131072 'Production checker'
    $checkerDeliveredFragments = Get-StrictOutputInteger $CheckerValues `
        'delivered-fragment-datagrams' `
        0 131072 'Production checker'
    $checkerReassembled = Get-StrictOutputInteger $CheckerValues 'reassembled' `
        0 131072 'Production checker'
    $checkerDecompressed = Get-StrictOutputInteger $CheckerValues 'decompressed' `
        0 131072 'Production checker'
    $walkerSequencedC2s = Get-StrictOutputInteger $WalkerValues `
        'delivered-sequenced-c2s' 0 131072 'Independent walker'
    $walkerSequencedS2c = Get-StrictOutputInteger $WalkerValues `
        'delivered-sequenced-s2c' 0 131072 'Independent walker'
    $walkerFragments = Get-StrictOutputInteger $WalkerValues `
        'delivered-fragment-datagrams' 0 131072 'Independent walker'
    if ($sequencedC2s -ne $checkerDeliveredSequencedC2s -or
        $sequencedC2s -ne $walkerSequencedC2s -or
        $sequencedS2c -ne $checkerDeliveredSequencedS2c -or
        $sequencedS2c -ne $walkerSequencedS2c -or
        $fragmentDatagrams -ne $checkerDeliveredFragments -or
        $fragmentDatagrams -ne $walkerFragments -or
        $reassembledPayloads -ne $checkerReassembled -or
        $decompressedPayloads -ne $checkerDecompressed) {
        throw 'Final manifest transport/replay counters disagree with recomputed facts.'
    }
    if ($checkerReplaySequencedC2s -gt $checkerDeliveredSequencedC2s -or
        $checkerReplaySequencedS2c -gt $checkerDeliveredSequencedS2c -or
        $checkerReplayFragments -gt $checkerDeliveredFragments -or
        ($checkerReplaySequencedC2s + $checkerReplaySequencedS2c +
            $checkerReplayDuplicates + $checkerReplayOld) -ne
            ($checkerDeliveredSequencedC2s + $checkerDeliveredSequencedS2c)) {
        throw 'Replay accepted/suppressed accounting disagrees with delivered transport populations.'
    }

    $transportHash = [string]$Run.transport_structural_sha256
    $replayHash = [string]$Run.replay_structural_sha256
    if ($transportHash -cnotmatch '^[0-9a-f]{64}$' -or
        $transportHash -cne [string]$CheckerValues['structural-hash'] -or
        $replayHash -cnotmatch '^[0-9a-f]{64}$' -or
        $replayHash -cne [string]$CheckerValues['replay-structural-hash'] -or
        $replayHash -cne [string]$WalkerValues['replay-structural-hash']) {
        throw 'Final manifest structural hashes disagree with checker/walker facts.'
    }

    $lastObserved = Get-StrictInteger $Run `
        last_observed_transport_timestamp_us 0 300000000
    $lastDeliveredS2c = Get-StrictInteger $Run `
        last_delivered_sequenced_s2c_timestamp_us 0 300000000
    $durationMs = Get-StrictInteger $Run duration_ms 0 390000
    $walkerLastObserved = Get-StrictOutputInteger $WalkerValues `
        'last-observed-timestamp-us' 0 300000000 'Independent walker'
    $walkerLastDeliveredS2c = Get-StrictOutputInteger $WalkerValues `
        'last-delivered-sequenced-s2c-timestamp-us' 0 300000000 `
        'Independent walker'
    if ($lastObserved -ne $walkerLastObserved -or
        $lastDeliveredS2c -ne $walkerLastDeliveredS2c) {
        throw 'Final manifest timestamps disagree with the independently walked journal.'
    }
    if ([string]$Run.client_ready_status -cne 'true' -or
        $lastObserved -gt (($durationMs + 1) * 1000) -or
        $durationMs -gt ($captureMaximumDurationMs + 90000)) {
        throw 'Final manifest duration/readiness is inconsistent with capture bounds.'
    }
    if ([string]$Run.candidate_stability -cne 'single_observation' -or
        [string]$CheckerValues['candidate-stability'] -cne
            'single_observation') {
        throw 'Per-run candidate stability disagrees with the checker boundary.'
    }

    return [pscustomobject]@{
        CanonicalScenario = $canonicalScenario
        MapCategory = $mapCategory
        RawDatagrams = $walkerRaw
        SequencedC2s = $checkerDeliveredSequencedC2s
        SequencedS2c = $checkerDeliveredSequencedS2c
        FragmentDatagrams = $checkerDeliveredFragments
        ReplayAcceptedSequencedC2s = $checkerReplaySequencedC2s
        ReplayAcceptedSequencedS2c = $checkerReplaySequencedS2c
        ReplayAcceptedFragmentDatagrams = $checkerReplayFragments
        ReplayDuplicatePackets = $checkerReplayDuplicates
        ReplayOldPackets = $checkerReplayOld
        ReassembledPayloads = $checkerReassembled
        DecompressedPayloads = $checkerDecompressed
        LastObservedTimestampUs = $walkerLastObserved
        LastDeliveredSequencedS2cTimestampUs = $walkerLastDeliveredS2c
        DurationMs = $durationMs
        ClientReady = $true
        TransportStructuralHash = $transportHash
        ReplayStructuralHash = $replayHash
        CandidateStability = 'single_observation'
        ClientFileVersion = [string]$Version.client_file_version
        ServerLauncherVersion = [string]$Version.server_launcher_version
        ServerEngineVersion = [string]$Version.server_engine_version
        Protocol = [Int64]$Version.protocol
        ServerBuild = [Int64]$Version.server_build
        SteamBuildId = [Int64]$Version.steam_build_id
        ClientProfileFingerprint = [string]$Version.client_profile_fingerprint
        ServerProfileFingerprint = [string]$Version.server_profile_fingerprint
    }
}

if ($PSCmdlet.ParameterSetName -ceq 'EvidencePolicy') {
    $topLevelKeys = @(
        'schema', 'implementation_commit', 'stock_profile', 'isolation_profile',
        'run_counts', 'map_scenario_ordinals', 'transport_counts', 'boundary',
        'candidate', 'transport_structural_hashes', 'replay_structural_hashes',
        'restoration')
    $validShape = [ordered]@{}
    foreach ($key in $topLevelKeys) { $validShape[$key] = $null }
    Assert-ExactProperties ([pscustomobject]$validShape) $topLevelKeys `
        'policy self-test valid shape'
    Assert-SanitizedEvidenceText (
        ([pscustomobject]$validShape | ConvertTo-Json -Depth 4 -Compress))

    $forbiddenRejected = 0
    $forbiddenShape = [ordered]@{}
    foreach ($key in $topLevelKeys) { $forbiddenShape[$key] = $null }
    $forbiddenShape['raw_payload'] = '00'
    try {
        Assert-ExactProperties ([pscustomobject]$forbiddenShape) $topLevelKeys `
            'policy self-test forbidden shape'
    } catch { $forbiddenRejected++ }
    try { Assert-SanitizedEvidenceText '{"schema":"v1","raw_payload":"00"}' }
    catch { $forbiddenRejected++ }
    if ($forbiddenRejected -ne 2) {
        throw 'Evidence policy did not reject both forbidden-key paths.'
    }

    $validBoundary = [pscustomobject]@{
        replay_payload_ordinal = 4; corpus_observed_ordinal = 10
        delivery_ordinal = 9; byte_offset = 2; bit_offset = 3
        source_netchan_sequence = 7; source_payload_byte_count = 4
        source_payload_bit_count = 32; next_unconsumed_bit_count = 13
        reassembled = $false; decompressed = $false; byte_aligned = $false
    }
    Assert-FirstObservationGeometry $validBoundary 5 'bit-prefix:17' `
        'policy self-test valid boundary'
    $geometryRejected = 0
    $cursorMismatch = $validBoundary.PSObject.Copy()
    $cursorMismatch.next_unconsumed_bit_count = 12
    try { Assert-FirstObservationGeometry $cursorMismatch 5 'bit-prefix:17' `
            'policy self-test cursor mismatch' }
    catch { $geometryRejected++ }
    try { Assert-FirstObservationGeometry $validBoundary 4 'bit-prefix:17' `
            'policy self-test prefix-width mismatch' }
    catch { $geometryRejected++ }
    $remainingTooSmall = $validBoundary.PSObject.Copy()
    $remainingTooSmall.byte_offset = 3
    $remainingTooSmall.bit_offset = 4
    $remainingTooSmall.next_unconsumed_bit_count = 4
    try { Assert-FirstObservationGeometry $remainingTooSmall 5 'bit-prefix:17' `
            'policy self-test candidate exceeds remaining bits' }
    catch { $geometryRejected++ }
    if ($geometryRejected -ne 3) {
        throw 'Evidence policy did not reject cursor/prefix/remaining-width mismatches.'
    }
    $firstProfile = @{
        'boundary-bit-offset' = '3'; 'boundary-byte-aligned' = 'false'
        'candidate-bit-width' = '5'; 'first-candidate' = 'bit-prefix:17'
        'boundary-payload-ordinal' = '4'; 'boundary-delivery-ordinal' = '9'
    }
    $otherOccurrence = @{
        'boundary-bit-offset' = '3'; 'boundary-byte-aligned' = 'false'
        'candidate-bit-width' = '5'; 'first-candidate' = 'bit-prefix:17'
        'boundary-payload-ordinal' = '91'; 'boundary-delivery-ordinal' = '117'
    }
    if ((Get-FirstCandidateStabilityProfile $firstProfile) -cne
        (Get-FirstCandidateStabilityProfile $otherOccurrence)) {
        throw 'Run-specific transport ordinals contaminated candidate stability.'
    }
    $differentAlignment = $otherOccurrence.Clone()
    $differentAlignment['boundary-bit-offset'] = '4'
    $alignmentRejected = [int](
        (Get-FirstCandidateStabilityProfile $firstProfile) -cne
        (Get-FirstCandidateStabilityProfile $differentAlignment))
    if ($alignmentRejected -ne 1) {
        throw 'Candidate stability did not retain exact bit alignment.'
    }

    $bindingRun = [pscustomobject]@{
        scenario = 'drop-server-to-client-transport-ordinal'
        map_category = 'boot_camp'
        raw_datagram_count = 10; journal_entry_count = 10
        delivered_sequenced_c2s_count = 3
        delivered_sequenced_s2c_count = 5
        delivered_fragment_datagram_count = 2
        reassembled_payload_count = 1
        decompressed_payload_count = 2
        transport_structural_sha256 = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
        replay_structural_sha256 = 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'
        duration_ms = 45000; client_ready_status = 'true'
        candidate_stability = 'single_observation'
        last_observed_transport_timestamp_us = 30000000
        last_delivered_sequenced_s2c_timestamp_us = 29900000
    }
    $bindingCapture = [pscustomobject]@{
        scenario = 'drop-server-runtime'; observed_datagrams = 10
        maximum_duration_ms = 45000
        observed_raw_bytes = 500; client_packets = 4; server_packets = 6
        emitted_datagrams = 10
    }
    $bindingVersion = [pscustomobject]@{
        map_category = 'boot_camp'
        client_file_version = '1.1.1.1'
        server_launcher_version = '4.1.1.1'
        server_engine_version = '1.1.2.2'
        protocol = 48
        server_build = 10210
        steam_build_id = 15961492
        client_profile_fingerprint =
            'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc'
        server_profile_fingerprint =
            'dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd'
    }
    $bindingChecker = @{
        'sequenced-c2s' = '3'; 'sequenced-s2c' = '5'; 'fragments' = '2'
        'duplicate-packets' = '0'; 'old-packets' = '0'
        'delivered-sequenced-c2s' = '3'; 'delivered-sequenced-s2c' = '5'
        'delivered-fragment-datagrams' = '2'
        'reassembled' = '1'; 'decompressed' = '2'
        'candidate-stability' = 'single_observation'
        'structural-hash' = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
        'replay-structural-hash' = 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'
    }
    $bindingWalker = @{
        'raw-datagrams' = '10'; 'journal-entries' = '10'; 'raw-bytes' = '500'
        'observed-c2s' = '4'; 'observed-s2c' = '6'; 'emitted-datagrams' = '10'
        'delivered-sequenced-c2s' = '3'; 'delivered-sequenced-s2c' = '5'
        'delivered-fragment-datagrams' = '2'
        'last-observed-timestamp-us' = '30000000'
        'last-delivered-sequenced-s2c-timestamp-us' = '29900000'
        'replay-structural-hash' = 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'
    }
    [void](Assert-RunEvidenceBindings $bindingRun $bindingCapture `
        $bindingVersion $bindingChecker $bindingWalker)

    # Duplicate and reordered-old delivery emissions are journal facts even
    # though replay suppresses them before incrementing accepted-new counts.
    $replaySuppressionAccepted = 0
    $duplicateChecker = $bindingChecker.Clone()
    $duplicateChecker['sequenced-s2c'] = '4'
    $duplicateChecker['fragments'] = '1'
    $duplicateChecker['duplicate-packets'] = '1'
    try {
        [void](Assert-RunEvidenceBindings $bindingRun $bindingCapture `
            $bindingVersion $duplicateChecker $bindingWalker)
        $replaySuppressionAccepted++
    } catch {}
    $reorderedOldChecker = $bindingChecker.Clone()
    $reorderedOldChecker['sequenced-s2c'] = '4'
    $reorderedOldChecker['old-packets'] = '1'
    try {
        [void](Assert-RunEvidenceBindings $bindingRun $bindingCapture `
            $bindingVersion $reorderedOldChecker $bindingWalker)
        $replaySuppressionAccepted++
    } catch {}
    if ($replaySuppressionAccepted -ne 2) {
        throw 'Binding policy rejected valid duplicate/reordered-old replay suppression.'
    }

    function Test-BindingMutationRejected {
        param([scriptblock]$Mutation)
        $mutatedRun = $bindingRun.PSObject.Copy()
        $mutatedCapture = $bindingCapture.PSObject.Copy()
        $mutatedVersion = $bindingVersion.PSObject.Copy()
        $mutatedChecker = $bindingChecker.Clone()
        $mutatedWalker = $bindingWalker.Clone()
        try {
            & $Mutation $mutatedRun $mutatedCapture $mutatedVersion `
                $mutatedChecker $mutatedWalker
            [void](Assert-RunEvidenceBindings $mutatedRun $mutatedCapture `
                $mutatedVersion $mutatedChecker $mutatedWalker)
            return 0
        } catch { return 1 }
    }

    $scenarioBindingRejected = 0
    $scenarioBindingRejected += Test-BindingMutationRejected {
        param($run) $run.scenario = 'baseline'
    }
    $scenarioBindingRejected += Test-BindingMutationRejected {
        param($run, $capture) $capture.scenario = 'drop-server'
    }
    if ($scenarioBindingRejected -ne 2) {
        throw 'Binding policy did not reject scenario mismatch/unknown alias.'
    }
    $mapBindingRejected = Test-BindingMutationRejected {
        param($run, $capture, $version) $version.map_category = 'crossfire'
    }
    if ($mapBindingRejected -ne 1) {
        throw 'Binding policy did not reject independently attested map mismatch.'
    }

    $manifestCounterRejected = 0
    foreach ($counterName in @(
            'raw_datagram_count', 'journal_entry_count',
            'delivered_sequenced_c2s_count',
            'delivered_sequenced_s2c_count',
            'delivered_fragment_datagram_count', 'reassembled_payload_count',
            'decompressed_payload_count')) {
        $manifestCounterRejected += Test-BindingMutationRejected {
            param($run) $run.$counterName = [Int64]$run.$counterName + 1
        }
    }
    if ($manifestCounterRejected -ne 7) {
        throw 'Binding policy did not reject every final-manifest counter family.'
    }
    $sourceCounterRejected = 0
    $sourceCounterRejected += Test-BindingMutationRejected {
        param($run, $capture) $capture.observed_raw_bytes = 501
    }
    $sourceCounterRejected += Test-BindingMutationRejected {
        param($run, $capture, $version, $checker, $walker)
        $walker['observed-c2s'] = '5'
    }
    $sourceCounterRejected += Test-BindingMutationRejected {
        param($run, $capture, $version, $checker) $checker['reassembled'] = '2'
    }
    if ($sourceCounterRejected -ne 3) {
        throw 'Binding policy did not reject mutated capture/walker/checker counters.'
    }
    $replayCounterBoundRejected = Test-BindingMutationRejected {
        param($run, $capture, $version, $checker)
        $checker['sequenced-s2c'] = '6'
    }
    if ($replayCounterBoundRejected -ne 1) {
        throw 'Binding policy did not reject replay accepted-new count above delivery count.'
    }
    $replayAccountingRejected = 0
    $replayAccountingRejected += Test-BindingMutationRejected {
        param($run, $capture, $version, $checker)
        $checker['sequenced-s2c'] = '4'
    }
    $replayAccountingRejected += Test-BindingMutationRejected {
        param($run, $capture, $version, $checker)
        $checker['duplicate-packets'] = '1'
    }
    if ($replayAccountingRejected -ne 2) {
        throw 'Binding policy did not reject omitted/extra replay suppression accounting.'
    }

    $hashBindingRejected = 0
    $hashBindingRejected += Test-BindingMutationRejected {
        param($run) $run.transport_structural_sha256 =
            'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc'
    }
    $hashBindingRejected += Test-BindingMutationRejected {
        param($run) $run.replay_structural_sha256 =
            'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc'
    }
    $hashBindingRejected += Test-BindingMutationRejected {
        param($run, $capture, $version, $checker)
        $checker['structural-hash'] =
            'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc'
    }
    $hashBindingRejected += Test-BindingMutationRejected {
        param($run, $capture, $version, $checker, $walker)
        $walker['replay-structural-hash'] =
            'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc'
    }
    if ($hashBindingRejected -ne 4) {
        throw 'Binding policy did not reject manifest/checker/walker hash mutations.'
    }

    $timestampBindingRejected = 0
    $timestampBindingRejected += Test-BindingMutationRejected {
        param($run) $run.last_observed_transport_timestamp_us = 30000001
    }
    $timestampBindingRejected += Test-BindingMutationRejected {
        param($run) $run.last_delivered_sequenced_s2c_timestamp_us = 29900001
    }
    if ($timestampBindingRejected -ne 2) {
        throw 'Binding policy did not reject both final-manifest timestamps.'
    }
    $candidateStabilityRejected = Test-BindingMutationRejected {
        param($run) $run.candidate_stability = 'stable_observation'
    }
    if ($candidateStabilityRejected -ne 1) {
        throw 'Binding policy did not reject per-run cross-run stability forgery.'
    }
    Write-Output '[stock-runtime-evidence-policy] forbidden-key-rejections=2'
    Write-Output '[stock-runtime-evidence-policy] cursor-width-rejections=3'
    Write-Output '[stock-runtime-evidence-policy] candidate-alignment-rejections=1'
    Write-Output '[stock-runtime-evidence-policy] scenario-binding-rejections=2'
    Write-Output '[stock-runtime-evidence-policy] map-binding-rejections=1'
    Write-Output '[stock-runtime-evidence-policy] manifest-counter-binding-rejections=7'
    Write-Output '[stock-runtime-evidence-policy] source-counter-binding-rejections=3'
    Write-Output '[stock-runtime-evidence-policy] replay-suppression-binding-acceptances=2'
    Write-Output '[stock-runtime-evidence-policy] replay-counter-bound-rejections=1'
    Write-Output '[stock-runtime-evidence-policy] replay-accounting-rejections=2'
    Write-Output '[stock-runtime-evidence-policy] hash-binding-rejections=4'
    Write-Output '[stock-runtime-evidence-policy] timestamp-binding-rejections=2'
    Write-Output '[stock-runtime-evidence-policy] candidate-stability-binding-rejections=1'
    Write-Output '[stock-runtime-evidence-policy] files-written=0'
    Write-Output '[stock-runtime-evidence-policy] result=success'
    return
}

$root = [IO.Path]::GetFullPath($CaptureRoot).TrimEnd('\', '/')
if ($root -ine $requiredCaptureRoot -or
    -not (Test-Path -LiteralPath $root -PathType Container)) {
    throw 'CaptureRoot must be the exact existing repository manual-artifacts/stock-runtime root.'
}
Assert-NoReparsePointInExistingPath $root 'capture corpus root'
$checker = [IO.Path]::GetFullPath($CheckerPath)
if (-not (Test-Path -LiteralPath $checker -PathType Leaf) -or
    [IO.Path]::GetFileName($checker) -cne 'hlclient_stock_runtime_check.exe' -or
    -not $checker.StartsWith(
        $repositoryRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'CheckerPath must name the repository-built stock-runtime checker.'
}
Assert-NoReparsePointInExistingPath $checker 'stock runtime checker'
Assert-OnlyDefaultDataStream $checker 'stock runtime checker'
Assert-NoHardLink $checker 'stock runtime checker'
if (-not (Test-Path -LiteralPath $walkerPath -PathType Leaf)) {
    throw 'Independent transport walker is absent.'
}

$gitIgnore = Join-Path $repositoryRoot '.gitignore'
if ((Get-Content -Raw -LiteralPath $gitIgnore) -cnotmatch '(?m)^/manual-artifacts/\s*$') {
    throw 'Repository-wide manual-artifacts ignore rule is absent.'
}
$git = Get-Command git.exe -ErrorAction Stop
$tracked = @(& $git.Source -C $repositoryRoot ls-files -- `
    'manual-artifacts/stock-runtime' 2>$null)
if ($LASTEXITCODE -ne 0 -or $tracked.Count -ne 0) {
    throw 'Raw stock-runtime artifacts are tracked or Git index inspection failed.'
}

$entries = @(Get-ChildItem -LiteralPath $root -Force | Sort-Object Name)
if ($entries.Count -gt $maximumRuns) { throw 'Capture corpus exceeds its run bound.' }
if (@($entries | Where-Object {
            -not $_.PSIsContainer -or $_.Name -cnotmatch '^[0-9a-f]{32}$' }).Count -ne 0) {
    throw 'Capture corpus root contains a non-run entry.'
}

$accepted = 0
$rejected = 0
$incomplete = 0
[Int64]$rawDatagrams = 0
[Int64]$sequencedC2s = 0
[Int64]$sequencedS2c = 0
[Int64]$fragments = 0
[Int64]$reassembled = 0
[Int64]$decompressed = 0
[Int64]$postResource = 0
$scenarioCounts = @{
    'boot_camp|baseline' = 0; 'crossfire|baseline' = 0; 'stalkyard|baseline' = 0
    'crossfire|idle-runtime' = 0; 'boot_camp|reconnect' = 0
    'boot_camp|drop-server-to-client-transport-ordinal' = 0
    'crossfire|duplicate-server-to-client-transport-ordinal' = 0
    'stalkyard|reorder-server-to-client-transport-ordinal' = 0
}
$candidateProfile = $null
$boundaryEvidence = $null
$versionProfile = $null
$versionEvidence = $null
$isolationEvidence = $null
$candidateConflict = $false
$versionConflict = $false
$walkerAgreements = 0
$checkerDeterminism = 0
$transportStructuralHashes = [Collections.Generic.List[string]]::new()
$replayStructuralHashes = [Collections.Generic.List[string]]::new()

$checkerKeys = @(
    'profile', 'transport-valid', 'sequenced-c2s', 'sequenced-s2c',
    'fragments', 'duplicate-packets', 'old-packets',
    'delivered-sequenced-c2s', 'delivered-sequenced-s2c',
    'delivered-fragment-datagrams', 'reassembled', 'decompressed', 'signon-replay',
    'post-resource-boundary', 'boundary-payload-ordinal',
    'boundary-observed-ordinal', 'boundary-delivery-ordinal',
    'boundary-byte-offset', 'boundary-bit-offset',
    'boundary-source-sequence', 'boundary-source-payload-bytes',
    'boundary-source-payload-bits', 'boundary-next-unconsumed-bits',
    'boundary-reassembled', 'boundary-decompressed',
    'boundary-byte-aligned', 'candidate-bit-width', 'first-candidate',
    'candidate-recurrence', 'candidate-stability', 'accepted-run',
    'publication-ready', 'result', 'structural-hash',
    'replay-structural-hash')
$walkerKeys = @(
    'run-id', 'journal-entries', 'raw-datagrams', 'raw-bytes',
    'observed-c2s', 'observed-s2c', 'delivered-c2s', 'delivered-s2c',
    'observed-connectionless-c2s', 'observed-connectionless-s2c',
    'observed-sequenced-c2s', 'observed-sequenced-s2c',
    'observed-fragment-datagrams', 'observed-reliable-datagrams',
    'delivered-connectionless-c2s', 'delivered-connectionless-s2c',
    'delivered-sequenced-c2s', 'delivered-sequenced-s2c',
    'delivered-fragment-datagrams', 'delivered-reliable-datagrams',
    'wrong-source-datagrams', 'emitted-datagrams', 'transport-complete',
    'last-observed-timestamp-us', 'last-delivered-sequenced-s2c-timestamp-us',
    'observed-delivered-policy', 'final-manifest',
    'post-resource-boundary', 'boundary-payload-ordinal',
    'boundary-observed-ordinal', 'boundary-delivery-ordinal',
    'boundary-byte-offset', 'boundary-bit-offset',
    'boundary-source-sequence', 'boundary-source-payload-bytes',
    'boundary-source-payload-bits', 'boundary-next-unconsumed-bits',
    'boundary-reassembled', 'boundary-decompressed',
    'boundary-byte-aligned', 'candidate-bit-width', 'first-candidate',
    'replay-structural-hash', 'result')

foreach ($directory in $entries) {
    try {
        Assert-NoReparsePointInExistingPath $directory.FullName 'capture run'
        $allowedEntries = @(
            'capture-metadata.json', 'research-run-metadata.json',
            'version-observation.staged.json',
            'isolation-attestation.staged.json',
            'restoration-attestation.staged.json',
            'version-observation.json', 'isolation-attestation.json',
            'restoration-attestation.json', 'transport-journal.jsonl', 'raw', 'logs')
        $children = @(Get-ChildItem -LiteralPath $directory.FullName -Force)
        if (@($children | Where-Object { $allowedEntries -cnotcontains $_.Name }).Count -ne 0 -or
            @($allowedEntries | Where-Object {
                    -not (Test-Path -LiteralPath (Join-Path $directory.FullName $_)) }).Count -ne 0) {
            throw 'Run directory structure is incomplete or contains an unknown entry.'
        }
        $capture = Read-BoundedJson (Join-Path $directory.FullName 'capture-metadata.json') `
            1048576 'capture metadata'
        Assert-ExactProperties $capture @(
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
            'one_upstream_socket', 'exact_source_validation',
            'payload_rewritten', 'raw_datagrams_stored',
            'accepted_evidence_run') 'capture metadata'
        if ([string]$capture.schema -cne 'hlclient.stock-runtime-capture-metadata.v1') {
            throw 'Capture metadata v1 compatibility is absent.'
        }
        $run = Read-BoundedJson (Join-Path $directory.FullName 'research-run-metadata.json') `
            131072 'research run manifest'
        Assert-ExactProperties $run @(
            'schema', 'run_id', 'scenario', 'map_category', 'duration_ms',
            'isolation_status', 'process_ownership_status',
            'version_profile_status', 'relay_status', 'client_ready_status',
            'restoration_status', 'external_drift_status',
            'raw_datagram_count', 'journal_entry_count',
            'delivered_sequenced_c2s_count',
            'delivered_sequenced_s2c_count',
            'delivered_fragment_datagram_count', 'reassembled_payload_count',
            'decompressed_payload_count', 'offline_replay_status',
            'post_resource_boundary_status',
            'post_resource_replay_payload_ordinal',
            'post_resource_corpus_observed_ordinal',
            'post_resource_delivery_ordinal', 'post_resource_byte_offset',
            'post_resource_bit_offset', 'post_resource_source_sequence',
            'post_resource_source_payload_bytes',
            'post_resource_source_payload_bits',
            'post_resource_next_unconsumed_bits',
            'post_resource_reassembled', 'post_resource_decompressed',
            'post_resource_boundary_byte_aligned',
            'first_observation_status', 'first_candidate',
            'first_candidate_bit_width', 'first_candidate_recurrence',
            'transport_structural_sha256', 'replay_structural_sha256',
            'last_delivered_sequenced_s2c_timestamp_us',
            'last_observed_transport_timestamp_us', 'candidate_stability',
            'accepted_transport_run', 'accepted_evidence_run',
            'failure_category') 'research run manifest'
        $version = Read-BoundedJson (Join-Path $directory.FullName 'version-observation.json') `
            65536 'version observation'
        $stagedVersion = Read-BoundedJson `
            (Join-Path $directory.FullName 'version-observation.staged.json') `
            65536 'staged version observation'
        $versionKeys = @(
            'schema', 'map_category', 'client_file_version',
            'client_pe_machine', 'client_signature',
            'client_profile_fingerprint', 'server_launcher_version',
            'server_pe_machine', 'server_signature',
            'server_profile_fingerprint', 'steam_app_id', 'steam_build_id',
            'server_engine_version', 'protocol', 'server_build',
            'evidence_status')
        Assert-ExactProperties $version $versionKeys 'version observation'
        Assert-ExactProperties $stagedVersion $versionKeys `
            'staged version observation'
        $isolation = Read-BoundedJson (Join-Path $directory.FullName 'isolation-attestation.json') `
            65536 'isolation attestation'
        $stagedIsolation = Read-BoundedJson `
            (Join-Path $directory.FullName 'isolation-attestation.staged.json') `
            65536 'staged isolation attestation'
        $isolationKeys = @(
            'schema', 'session_type', 'persistent_rule_count',
            'ipv4_loopback', 'ipv6_loopback', 'non_loopback_canary',
            'cleanup_status', 'evidence_status')
        Assert-ExactProperties $isolation $isolationKeys 'isolation attestation'
        Assert-ExactProperties $stagedIsolation $isolationKeys `
            'staged isolation attestation'
        $restoration = Read-BoundedJson (Join-Path $directory.FullName 'restoration-attestation.json') `
            131072 'restoration attestation'
        $stagedRestoration = Read-BoundedJson `
            (Join-Path $directory.FullName 'restoration-attestation.staged.json') `
            131072 'staged restoration attestation'
        $restorationKeys = @(
            'schema', 'external_file_drift', 'snapshot_entry_count',
            'pre_manifest_sha256', 'post_manifest_sha256',
            'external_snapshot_entry_count', 'external_pre_manifest_sha256',
            'external_post_manifest_sha256', 'created_files_removed',
            'protected_paths_included', 'owned_processes_stopped',
            'input_automation_used', 'input_events_injected',
            'orchestrator_exit_code', 'restoration_status')
        Assert-ExactProperties $restoration $restorationKeys `
            'restoration attestation'
        Assert-ExactProperties $stagedRestoration $restorationKeys `
            'staged restoration attestation'
        Assert-ExactFilePair `
            (Join-Path $directory.FullName 'version-observation.staged.json') `
            (Join-Path $directory.FullName 'version-observation.json') `
            'version observation'
        Assert-ExactFilePair `
            (Join-Path $directory.FullName 'isolation-attestation.staged.json') `
            (Join-Path $directory.FullName 'isolation-attestation.json') `
            'isolation attestation'
        Assert-ExactFilePair `
            (Join-Path $directory.FullName 'restoration-attestation.staged.json') `
            (Join-Path $directory.FullName 'restoration-attestation.json') `
            'restoration attestation'
        if ([string]$run.schema -cne 'hlclient.stock-runtime-research-run.v1' -or
            [string]$run.run_id -cne $directory.Name -or
            $run.accepted_evidence_run -isnot [bool] -or
            $run.accepted_transport_run -isnot [bool]) {
            throw 'Research run manifest is invalid.'
        }
        if (-not $run.accepted_evidence_run) {
            if ([string]::IsNullOrWhiteSpace([string]$run.failure_category) -or
                [string]$run.failure_category -ceq 'none') {
                throw 'Non-accepted run lacks a typed failure category.'
            }
            $incomplete++
            continue
        }
        if (-not $run.accepted_transport_run -or
            [string]$run.restoration_status -cne 'exact' -or
            [string]$run.external_drift_status -cne 'none' -or
            [string]$run.offline_replay_status -cne 'success' -or
            [string]$run.post_resource_boundary_status -cne 'observed' -or
            [string]$run.first_observation_status -cne 'observed') {
            throw 'Accepted run does not satisfy all final gates.'
        }
        $canonicalScenario = Get-CanonicalRuntimeScenario `
            ([string]$run.scenario) 'Final manifest scenario'
        if ($canonicalScenario -ceq 'reconnect') {
            throw 'Reconnect acceptance is pending until two controlled session generations and both exact boundaries are attested.'
        }
        if ([string]$version.schema -cne 'hlclient.stock-runtime-version-observation.v1' -or
            $stockRuntimeMapCategories -cnotcontains [string]$version.map_category -or
            [string]$version.client_file_version -cne '1.1.1.1' -or
            [string]$version.client_pe_machine -cne 'x86' -or
            [string]$version.client_signature -cne 'valid' -or
            [string]$version.server_launcher_version -cne '4.1.1.1' -or
            [string]$version.server_pe_machine -cne 'x86' -or
            [string]$version.server_signature -cne 'valid' -or
            [Int64]$version.steam_app_id -ne 70 -or
            [Int64]$version.steam_build_id -ne 15961492 -or
            [string]$version.server_engine_version -cne '1.1.2.2' -or
            [Int64]$version.protocol -ne 48 -or [Int64]$version.server_build -ne 10210 -or
            [string]$version.client_profile_fingerprint -cnotmatch '^[0-9a-f]{64}$' -or
            [string]$version.server_profile_fingerprint -cnotmatch '^[0-9a-f]{64}$' -or
            [string]$version.evidence_status -cne 'observed') {
            throw 'Version/profile observation is not accepted.'
        }
        if ([string]$isolation.schema -cne 'hlclient.stock-runtime-isolation-attestation.v1' -or
            [string]$isolation.session_type -cne 'dynamic' -or
            [Int64]$isolation.persistent_rule_count -ne 0 -or
            [string]$isolation.ipv4_loopback -cne 'allowed' -or
            @('allowed', 'capability_unavailable') -cnotcontains
                [string]$isolation.ipv6_loopback -or
            [string]$isolation.non_loopback_canary -cne 'denied_os_classified' -or
            [string]$isolation.cleanup_status -cne 'exact' -or
            [string]$isolation.evidence_status -cne 'observed') {
            throw 'Isolation attestation is not accepted.'
        }
        $currentIsolationEvidence = [ordered]@{
            session_type = [string]$isolation.session_type
            persistent_rule_count = [Int64]$isolation.persistent_rule_count
            ipv4_loopback = [string]$isolation.ipv4_loopback
            ipv6_loopback = [string]$isolation.ipv6_loopback
            non_loopback_canary = [string]$isolation.non_loopback_canary
            cleanup_status = [string]$isolation.cleanup_status
        }
        if ($null -eq $isolationEvidence) {
            $isolationEvidence = $currentIsolationEvidence
        } elseif (($isolationEvidence | ConvertTo-Json -Compress) -cne
            ($currentIsolationEvidence | ConvertTo-Json -Compress)) {
            throw 'Isolation profile conflicts cross-run.'
        }
        if ([string]$restoration.schema -cne 'hlclient.stock-runtime-restoration.v1' -or
            [string]$restoration.restoration_status -cne 'exact' -or
            [string]$restoration.external_file_drift -cne 'none' -or
            [string]$restoration.pre_manifest_sha256 -cne
                [string]$restoration.post_manifest_sha256 -or
            $restoration.created_files_removed -cne $true -or
            $restoration.owned_processes_stopped -cne $true -or
            $restoration.input_automation_used -cne $false) {
            throw 'Restoration attestation is not accepted.'
        }

        $first = Invoke-Checker $checker $directory.FullName
        $second = Invoke-Checker $checker $directory.FullName
        if ($first.ExitCode -ne 0 -or $second.ExitCode -ne 0 -or
            ($first.Lines -join "`n") -cne ($second.Lines -join "`n")) {
            throw 'Production checker is unsuccessful or non-deterministic.'
        }
        $checkerValues = Convert-PrefixedOutputToValues $first.Lines `
            '[stock-runtime] ' $checkerKeys 'production checker'
        if ($checkerValues['profile'] -cne
                'stock_protocol_48_build_10210_evidence_pending' -or
            $checkerValues['accepted-run'] -cne 'true' -or
            $checkerValues['result'] -cne 'first-observation' -or
            $checkerValues['transport-valid'] -cne 'true' -or
            $checkerValues['signon-replay'] -cne 'complete' -or
            $checkerValues['post-resource-boundary'] -cne 'observed' -or
            $checkerValues['boundary-reassembled'] -cnotmatch '^(?:true|false)$' -or
            $checkerValues['boundary-decompressed'] -cnotmatch '^(?:true|false)$' -or
            $checkerValues['boundary-byte-aligned'] -cnotmatch '^(?:true|false)$' -or
            $checkerValues['first-candidate'] -cnotmatch
                '^(?:[0-9]{1,3}|bit-prefix:[0-9]{1,3})$' -or
            [int]($checkerValues['first-candidate'] -replace '^bit-prefix:', '') -gt 255 -or
            $checkerValues['candidate-recurrence'] -cne '1' -or
            $checkerValues['publication-ready'] -cne 'true' -or
            $checkerValues['structural-hash'] -cnotmatch '^[0-9a-f]{64}$' -or
            $checkerValues['replay-structural-hash'] -cnotmatch '^[0-9a-f]{64}$') {
            throw 'Production checker did not accept the run.'
        }
        foreach ($countKey in @('boundary-payload-ordinal',
                'boundary-observed-ordinal', 'boundary-delivery-ordinal',
                'boundary-byte-offset', 'boundary-source-sequence',
                'boundary-source-payload-bytes', 'boundary-source-payload-bits',
                'boundary-next-unconsumed-bits', 'candidate-bit-width')) {
            [Int64]$value = 0
            if (-not [Int64]::TryParse($checkerValues[$countKey], [ref]$value) -or
                $value -lt 0 -or $value -gt 8388608) {
                throw "Production checker $countKey is outside its bound."
            }
        }
        [Int64]$boundaryBitOffset = 0
        if (-not [Int64]::TryParse(
                $checkerValues['boundary-bit-offset'], [ref]$boundaryBitOffset) -or
            $boundaryBitOffset -lt 0 -or $boundaryBitOffset -gt 7) {
            throw 'Production checker boundary bit offset is invalid.'
        }
        [Int64]$sourcePayloadBytes = $checkerValues['boundary-source-payload-bytes']
        [Int64]$sourcePayloadBits = $checkerValues['boundary-source-payload-bits']
        [Int64]$boundaryByteOffset = $checkerValues['boundary-byte-offset']
        [Int64]$remainingBits = $checkerValues['boundary-next-unconsumed-bits']
        [Int64]$candidateBitWidth = $checkerValues['candidate-bit-width']
        if ($sourcePayloadBits -ne ($sourcePayloadBytes * 8) -or
            (($boundaryByteOffset * 8) + $boundaryBitOffset + $remainingBits) -ne
                $sourcePayloadBits -or
            ($checkerValues['boundary-byte-aligned'] -ceq 'true') -ne
                ($boundaryBitOffset -eq 0) -or
            $candidateBitWidth -lt 1 -or $candidateBitWidth -gt 8 -or
            $candidateBitWidth -gt $remainingBits -or
            (($checkerValues['boundary-byte-aligned'] -ceq 'true') -and
                $candidateBitWidth -ne 8) -or
            ($checkerValues['first-candidate'].StartsWith('bit-prefix:') -and
                [int]($checkerValues['first-candidate'].Substring(11)) -ge
                    [Math]::Pow(2, $candidateBitWidth))) {
            throw 'Production checker cursor/candidate geometry is inconsistent.'
        }
        $checkerDeterminism++

        $walkerLines = @(& $walkerPath -CaptureRoot $directory.FullName |
            ForEach-Object { $_.ToString() })
        $walkerValues = Convert-PrefixedOutputToValues $walkerLines `
            '[stock-runtime-walk] ' $walkerKeys 'independent walker'
        if ($walkerValues['result'] -cne 'success' -or
            $walkerValues['final-manifest'] -cne 'accepted' -or
            $walkerValues['wrong-source-datagrams'] -cne '0' -or
            $walkerValues['transport-complete'] -cne 'true' -or
            $walkerValues['delivered-sequenced-c2s'] -cne $checkerValues['delivered-sequenced-c2s'] -or
            $walkerValues['delivered-sequenced-s2c'] -cne $checkerValues['delivered-sequenced-s2c'] -or
            $walkerValues['delivered-fragment-datagrams'] -cne $checkerValues['delivered-fragment-datagrams'] -or
            $walkerValues['boundary-payload-ordinal'] -cne $checkerValues['boundary-payload-ordinal'] -or
            $walkerValues['boundary-observed-ordinal'] -cne $checkerValues['boundary-observed-ordinal'] -or
            $walkerValues['boundary-delivery-ordinal'] -cne $checkerValues['boundary-delivery-ordinal'] -or
            $walkerValues['boundary-byte-offset'] -cne $checkerValues['boundary-byte-offset'] -or
            $walkerValues['boundary-bit-offset'] -cne $checkerValues['boundary-bit-offset'] -or
            $walkerValues['boundary-source-sequence'] -cne $checkerValues['boundary-source-sequence'] -or
            $walkerValues['boundary-source-payload-bytes'] -cne $checkerValues['boundary-source-payload-bytes'] -or
            $walkerValues['boundary-source-payload-bits'] -cne $checkerValues['boundary-source-payload-bits'] -or
            $walkerValues['boundary-next-unconsumed-bits'] -cne $checkerValues['boundary-next-unconsumed-bits'] -or
            $walkerValues['boundary-reassembled'] -cne $checkerValues['boundary-reassembled'] -or
            $walkerValues['boundary-decompressed'] -cne $checkerValues['boundary-decompressed'] -or
            $walkerValues['boundary-byte-aligned'] -cne $checkerValues['boundary-byte-aligned'] -or
            $walkerValues['candidate-bit-width'] -cne $checkerValues['candidate-bit-width'] -or
            $walkerValues['first-candidate'] -cne $checkerValues['first-candidate'] -or
            $walkerValues['replay-structural-hash'] -cne $checkerValues['replay-structural-hash']) {
            throw 'Independent walker and production checker disagree.'
        }
        $binding = Assert-RunEvidenceBindings $run $capture $version `
            $checkerValues $walkerValues
        $walkerAgreements++

        # The checker deliberately receives one run at a time, so it can only
        # publish single_observation.  Cross-run stability belongs to this
        # independent campaign verifier and is established by identical
        # boundary geometry/candidate under one exact version profile with no
        # contradictory complete run.
        if ($checkerValues['candidate-stability'] -cne 'single_observation') {
            throw 'Per-run checker claimed unsupported cross-run stability.'
        }
        $candidate = Get-FirstCandidateStabilityProfile $checkerValues
        if ($null -eq $candidateProfile) { $candidateProfile = $candidate }
        elseif ($candidateProfile -cne $candidate) {
            $candidateConflict = $true
            throw 'First candidate conflicts cross-run.'
        }
        if ($null -eq $boundaryEvidence) {
            $boundaryEvidence = [ordered]@{
                replay_payload_ordinal = [Int64]$checkerValues['boundary-payload-ordinal']
                corpus_observed_ordinal = [Int64]$checkerValues['boundary-observed-ordinal']
                delivery_ordinal = [Int64]$checkerValues['boundary-delivery-ordinal']
                byte_offset = [Int64]$checkerValues['boundary-byte-offset']
                bit_offset = [Int64]$checkerValues['boundary-bit-offset']
                source_netchan_sequence = [Int64]$checkerValues['boundary-source-sequence']
                source_payload_byte_count = [Int64]$checkerValues['boundary-source-payload-bytes']
                source_payload_bit_count = [Int64]$checkerValues['boundary-source-payload-bits']
                next_unconsumed_bit_count = [Int64]$checkerValues['boundary-next-unconsumed-bits']
                reassembled = $checkerValues['boundary-reassembled'] -ceq 'true'
                decompressed = $checkerValues['boundary-decompressed'] -ceq 'true'
                byte_aligned = $checkerValues['boundary-byte-aligned'] -ceq 'true'
            }
        }
        $profile = '{0}|{1}|{2}|{3}|{4}|{5}' -f
            $binding.ClientFileVersion, $binding.ServerLauncherVersion,
            $binding.ServerEngineVersion, $binding.Protocol,
            $binding.ServerBuild, ($binding.SteamBuildId.ToString() + '|' +
                $binding.ClientProfileFingerprint + '|' +
                $binding.ServerProfileFingerprint)
        if ($null -eq $versionProfile) { $versionProfile = $profile }
        elseif ($versionProfile -cne $profile) {
            $versionConflict = $true
            throw 'Version profile conflicts cross-run.'
        }

        if ($null -eq $versionEvidence) {
            $versionEvidence = [ordered]@{
                client_file_version = [string]$binding.ClientFileVersion
                server_launcher_version = [string]$binding.ServerLauncherVersion
                server_engine_version = [string]$binding.ServerEngineVersion
                protocol = [Int64]$binding.Protocol
                server_build = [Int64]$binding.ServerBuild
                app_build = [Int64]$binding.SteamBuildId
            }
        }

        $canonicalScenario = [string]$binding.CanonicalScenario
        $key = '{0}|{1}' -f [string]$binding.MapCategory, $canonicalScenario
        if (-not $scenarioCounts.ContainsKey($key)) {
            throw 'Accepted run is outside the first-observation campaign matrix.'
        }
        $runSequencedC2s = [Int64]$binding.SequencedC2s
        $runSequencedS2c = [Int64]$binding.SequencedS2c
        $durationMs = [Int64]$binding.DurationMs
        if (($canonicalScenario -ceq 'baseline' -or
                $canonicalScenario -ceq 'idle-runtime') -and
            $durationMs -lt 30000) {
            throw 'Accepted baseline/idle run is shorter than 30 seconds.'
        }
        if (($canonicalScenario -ceq 'baseline' -or
                $canonicalScenario -ceq 'idle-runtime') -and
            [Int64]$binding.LastObservedTimestampUs -lt 30000000) {
            throw 'Accepted baseline/idle run is shorter than 30 seconds on the capture transport clock.'
        }
        if (($canonicalScenario -ceq 'baseline' -or
                $canonicalScenario -ceq 'idle-runtime') -and
            $runSequencedS2c -lt 100) {
            throw 'Accepted baseline/idle run is below its 100-packet S2C gate.'
        }
        if (($canonicalScenario -ceq 'drop-server-to-client-transport-ordinal' -or
                $canonicalScenario -ceq 'duplicate-server-to-client-transport-ordinal' -or
                $canonicalScenario -ceq 'reorder-server-to-client-transport-ordinal') -and
            $runSequencedS2c -lt 1) {
            throw 'Accepted perturbation run lacks delivered sequenced server traffic.'
        }
        if ($canonicalScenario -ceq 'idle-runtime') {
            $lastLiveS2cUs = [Int64]$binding.LastDeliveredSequencedS2cTimestampUs
            [Int64]$minimumLiveThroughUs = [Math]::Max(
                25000000, ($durationMs - 5000) * 1000)
            if ($lastLiveS2cUs -lt 30000000 -or
                $lastLiveS2cUs -lt $minimumLiveThroughUs -or
                -not [bool]$binding.ClientReady) {
                throw 'Accepted idle run did not remain client-ready with S2C traffic through its final five seconds.'
            }
        }
        $scenarioCounts[$key]++
        $accepted++
        $rawDatagrams += [Int64]$binding.RawDatagrams
        $sequencedC2s += $runSequencedC2s
        $sequencedS2c += $runSequencedS2c
        $fragments += [Int64]$binding.FragmentDatagrams
        $reassembled += [Int64]$binding.ReassembledPayloads
        $decompressed += [Int64]$binding.DecompressedPayloads
        $postResource++
        [void]$transportStructuralHashes.Add(
            [string]$binding.TransportStructuralHash)
        [void]$replayStructuralHashes.Add([string]$binding.ReplayStructuralHash)
    } catch {
        $rejected++
    }
}

$baselineAccepted = $scenarioCounts['boot_camp|baseline'] +
    $scenarioCounts['crossfire|baseline'] + $scenarioCounts['stalkyard|baseline']
$idleAccepted = $scenarioCounts['crossfire|idle-runtime']
$reconnectAccepted = $scenarioCounts['boot_camp|reconnect']
$perturbationAccepted =
    $scenarioCounts['boot_camp|drop-server-to-client-transport-ordinal'] +
    $scenarioCounts['crossfire|duplicate-server-to-client-transport-ordinal'] +
    $scenarioCounts['stalkyard|reorder-server-to-client-transport-ordinal']
$matrixReady = $scenarioCounts['boot_camp|baseline'] -ge 6 -and
    $scenarioCounts['crossfire|baseline'] -ge 4 -and
    $scenarioCounts['stalkyard|baseline'] -ge 4 -and $idleAccepted -ge 4 -and
    $reconnectAccepted -ge 2 -and
    $scenarioCounts['boot_camp|drop-server-to-client-transport-ordinal'] -ge 2 -and
    $scenarioCounts['crossfire|duplicate-server-to-client-transport-ordinal'] -ge 1 -and
    $scenarioCounts['stalkyard|reorder-server-to-client-transport-ordinal'] -ge 1
$thresholdReady = $accepted -ge $MinimumAcceptedRuns -and $matrixReady -and
    $sequencedS2c -ge $MinimumSequencedServerPackets -and
    $postResource -ge $MinimumAcceptedRuns -and $null -ne $candidateProfile -and
    -not $candidateConflict -and -not $versionConflict

Write-Output "[stock-runtime-first-verify] accepted-runs=$accepted"
Write-Output "[stock-runtime-first-verify] rejected-runs=$rejected"
Write-Output "[stock-runtime-first-verify] incomplete-runs=$incomplete"
Write-Output "[stock-runtime-first-verify] baseline-accepted=$baselineAccepted"
Write-Output "[stock-runtime-first-verify] idle-accepted=$idleAccepted"
Write-Output "[stock-runtime-first-verify] reconnect-accepted=$reconnectAccepted"
Write-Output "[stock-runtime-first-verify] perturbation-accepted=$perturbationAccepted"
Write-Output "[stock-runtime-first-verify] raw-datagrams=$rawDatagrams"
Write-Output "[stock-runtime-first-verify] sequenced-c2s=$sequencedC2s"
Write-Output "[stock-runtime-first-verify] sequenced-s2c=$sequencedS2c"
Write-Output "[stock-runtime-first-verify] fragments=$fragments"
Write-Output "[stock-runtime-first-verify] reassembled=$reassembled"
Write-Output "[stock-runtime-first-verify] decompressed=$decompressed"
Write-Output "[stock-runtime-first-verify] post-resource-observations=$postResource"
Write-Output ("[stock-runtime-first-verify] candidate-cross-run-stability={0}" -f
    $(if ($accepted -ge $MinimumAcceptedRuns -and $null -ne $candidateProfile -and
            -not $candidateConflict -and -not $versionConflict) {
            'stable_observation'
        } else { 'evidence_pending' }))
Write-Output "[stock-runtime-first-verify] checker-deterministic-runs=$checkerDeterminism"
Write-Output "[stock-runtime-first-verify] walker-agreements=$walkerAgreements"

if (-not $thresholdReady) {
    if (Test-Path -LiteralPath $evidencePath) {
        throw 'First-observation evidence JSON exists before its threshold passes.'
    }
    Write-Output '[stock-runtime-first-verify] evidence-threshold=pending'
    Write-Output '[stock-runtime-first-verify] evidence-json=absent'
    Write-Output '[stock-runtime-first-verify] result=evidence_pending'
    throw 'Stock runtime first-observation evidence threshold is not met.'
}

if (-not (Test-Path -LiteralPath $evidencePath -PathType Leaf)) {
    Write-Output '[stock-runtime-first-verify] evidence-threshold=passed'
    Write-Output '[stock-runtime-first-verify] evidence-json=pending-publication'
    throw 'Threshold passed, but the sanitized evidence-only publication is absent.'
}
$evidenceText = Get-Content -Raw -LiteralPath $evidencePath -Encoding UTF8
Assert-SanitizedEvidenceText $evidenceText
$evidence = Read-BoundedJson $evidencePath 1048576 'first-observation evidence'
$evidenceKeys = @(
    'schema', 'implementation_commit', 'stock_profile', 'isolation_profile',
    'run_counts', 'map_scenario_ordinals', 'transport_counts', 'boundary',
    'candidate', 'transport_structural_hashes', 'replay_structural_hashes',
    'restoration')
Assert-ExactProperties $evidence $evidenceKeys 'first-observation evidence'
if ([string]$evidence.schema -cne
        'hlclient.goldsrc-stock-runtime-first-observations.v1' -or
    [string]$evidence.implementation_commit -cnotmatch '^[0-9a-f]{40}$') {
    throw 'First-observation evidence identity is invalid.'
}
$implementationCommit = [string]$evidence.implementation_commit
$commitExists = @(& $git.Source -C $repositoryRoot cat-file -t `
    $implementationCommit 2>$null)
if ($LASTEXITCODE -ne 0 -or $commitExists.Count -ne 1 -or
    $commitExists[0] -cne 'commit') {
    throw 'Evidence implementation commit does not resolve to a commit.'
}
$commitMessage = @(& $git.Source -C $repositoryRoot show -s --format=%s `
    $implementationCommit 2>$null)
if ($LASTEXITCODE -ne 0 -or $commitMessage.Count -ne 1 -or
    $commitMessage[0] -cne 'Enable isolated stock runtime capture') {
    throw 'Evidence implementation commit has the wrong implementation message.'
}
& $git.Source -C $repositoryRoot merge-base --is-ancestor `
    $implementationCommit HEAD 2>$null
if ($LASTEXITCODE -ne 0) {
    throw 'Evidence implementation commit is not an ancestor of the verified checkout.'
}

Assert-ExactProperties $evidence.stock_profile @(
    'client_file_version', 'server_launcher_version',
    'server_engine_version', 'protocol', 'server_build', 'app_build') `
    'evidence stock profile'
if (($evidence.stock_profile | ConvertTo-Json -Compress) -cne
    ($versionEvidence | ConvertTo-Json -Compress)) {
    throw 'Evidence stock profile disagrees with accepted runs.'
}
Assert-ExactProperties $evidence.isolation_profile @(
    'session_type', 'persistent_rule_count', 'ipv4_loopback', 'ipv6_loopback',
    'non_loopback_canary', 'cleanup_status') 'evidence isolation profile'
if (($evidence.isolation_profile | ConvertTo-Json -Compress) -cne
    ($isolationEvidence | ConvertTo-Json -Compress)) {
    throw 'Evidence isolation profile disagrees with accepted runs.'
}
Assert-ExactProperties $evidence.run_counts @(
    'accepted', 'rejected', 'incomplete') 'evidence run counts'
if ((Get-StrictInteger $evidence.run_counts accepted `
        $MinimumAcceptedRuns $maximumRuns) -ne $accepted -or
    (Get-StrictInteger $evidence.run_counts rejected 0 $maximumRuns) -ne $rejected -or
    (Get-StrictInteger $evidence.run_counts incomplete 0 $maximumRuns) -ne $incomplete) {
    throw 'Evidence run counts disagree with the verified corpus.'
}

$expectedOrdinals = @(
    [ordered]@{ map_ordinal = 0; scenario_ordinal = 0; map_category = 'boot_camp'; scenario = 'baseline'; accepted_runs = $scenarioCounts['boot_camp|baseline'] },
    [ordered]@{ map_ordinal = 1; scenario_ordinal = 0; map_category = 'crossfire'; scenario = 'baseline'; accepted_runs = $scenarioCounts['crossfire|baseline'] },
    [ordered]@{ map_ordinal = 2; scenario_ordinal = 0; map_category = 'stalkyard'; scenario = 'baseline'; accepted_runs = $scenarioCounts['stalkyard|baseline'] },
    [ordered]@{ map_ordinal = 1; scenario_ordinal = 1; map_category = 'crossfire'; scenario = 'idle-runtime'; accepted_runs = $scenarioCounts['crossfire|idle-runtime'] },
    [ordered]@{ map_ordinal = 0; scenario_ordinal = 2; map_category = 'boot_camp'; scenario = 'reconnect'; accepted_runs = $scenarioCounts['boot_camp|reconnect'] },
    [ordered]@{ map_ordinal = 0; scenario_ordinal = 3; map_category = 'boot_camp'; scenario = 'drop-server-to-client-transport-ordinal'; accepted_runs = $scenarioCounts['boot_camp|drop-server-to-client-transport-ordinal'] },
    [ordered]@{ map_ordinal = 1; scenario_ordinal = 4; map_category = 'crossfire'; scenario = 'duplicate-server-to-client-transport-ordinal'; accepted_runs = $scenarioCounts['crossfire|duplicate-server-to-client-transport-ordinal'] },
    [ordered]@{ map_ordinal = 2; scenario_ordinal = 5; map_category = 'stalkyard'; scenario = 'reorder-server-to-client-transport-ordinal'; accepted_runs = $scenarioCounts['stalkyard|reorder-server-to-client-transport-ordinal'] })
$actualOrdinals = @($evidence.map_scenario_ordinals)
if ($actualOrdinals.Count -ne $expectedOrdinals.Count) {
    throw 'Evidence map/scenario ordinal cardinality is invalid.'
}
for ($index = 0; $index -lt $actualOrdinals.Count; $index++) {
    Assert-ExactProperties $actualOrdinals[$index] @(
        'map_ordinal', 'scenario_ordinal', 'map_category', 'scenario',
        'accepted_runs') "evidence map/scenario ordinal $index"
    foreach ($integerName in @('map_ordinal', 'scenario_ordinal', 'accepted_runs')) {
        [void](Get-StrictInteger $actualOrdinals[$index] $integerName 0 $maximumRuns)
    }
    if (($actualOrdinals[$index] | ConvertTo-Json -Compress) -cne
        ($expectedOrdinals[$index] | ConvertTo-Json -Compress)) {
        throw "Evidence map/scenario ordinal $index disagrees with the corpus."
    }
}

Assert-ExactProperties $evidence.transport_counts @(
    'sequenced_c2s', 'sequenced_s2c', 'reassembled_payloads',
    'decompressed_payloads') 'evidence transport counts'
if ((Get-StrictInteger $evidence.transport_counts sequenced_c2s 0 100000000) -ne
        $sequencedC2s -or
    (Get-StrictInteger $evidence.transport_counts sequenced_s2c `
        $MinimumSequencedServerPackets 100000000) -ne $sequencedS2c -or
    (Get-StrictInteger $evidence.transport_counts reassembled_payloads 0 100000000) -ne
        $reassembled -or
    (Get-StrictInteger $evidence.transport_counts decompressed_payloads 0 100000000) -ne
        $decompressed) {
    throw 'Evidence transport/replay counts disagree with the corpus.'
}

$boundaryKeys = @(
    'replay_payload_ordinal', 'corpus_observed_ordinal', 'delivery_ordinal',
    'byte_offset', 'bit_offset', 'source_netchan_sequence',
    'source_payload_byte_count', 'source_payload_bit_count',
    'next_unconsumed_bit_count', 'reassembled', 'decompressed', 'byte_aligned')
Assert-ExactProperties $evidence.boundary $boundaryKeys 'evidence boundary'
Assert-FirstObservationGeometry $evidence.boundary `
    (Get-StrictInteger $evidence.candidate bit_width 1 8) `
    ([string]$evidence.candidate.representation) 'evidence boundary/candidate'
if (($evidence.boundary | ConvertTo-Json -Compress) -cne
    ($boundaryEvidence | ConvertTo-Json -Compress)) {
    throw 'Evidence exact post-resource boundary disagrees with accepted runs.'
}

Assert-ExactProperties $evidence.candidate @(
    'neutral_name', 'representation', 'bit_width', 'recurrence_count',
    'stability', 'semantic_status', 'body_consumed') 'evidence candidate'
if ([string]$evidence.candidate.neutral_name -cne
        'first_post_resource_runtime_candidate' -or
    [string]$evidence.candidate.representation -cne
        $checkerValues['first-candidate'] -or
    (Get-StrictInteger $evidence.candidate bit_width 1 8) -ne
        [Int64]$checkerValues['candidate-bit-width'] -or
    (Get-StrictInteger $evidence.candidate recurrence_count `
        $MinimumAcceptedRuns $maximumRuns) -ne $accepted -or
    [string]$evidence.candidate.stability -cne 'stable_observation' -or
    [string]$evidence.candidate.semantic_status -cne 'unassigned' -or
    $evidence.candidate.body_consumed -cne $false) {
    throw 'Evidence candidate metadata/recurrence is invalid.'
}

$expectedTransportHashes = @($transportStructuralHashes | Sort-Object)
$expectedReplayHashes = @($replayStructuralHashes | Sort-Object)
$actualTransportHashes = @($evidence.transport_structural_hashes)
$actualReplayHashes = @($evidence.replay_structural_hashes)
if ($actualTransportHashes.Count -ne $accepted -or
    $actualReplayHashes.Count -ne $accepted -or
    @($actualTransportHashes | Where-Object { $_ -isnot [string] -or
            $_ -cnotmatch '^[0-9a-f]{64}$' }).Count -ne 0 -or
    @($actualReplayHashes | Where-Object { $_ -isnot [string] -or
            $_ -cnotmatch '^[0-9a-f]{64}$' }).Count -ne 0 -or
    (@($actualTransportHashes | Sort-Object) -join '|') -cne
        ($expectedTransportHashes -join '|') -or
    (@($actualReplayHashes | Sort-Object) -join '|') -cne
        ($expectedReplayHashes -join '|')) {
    throw 'Evidence structural hash sets disagree with accepted runs.'
}

Assert-ExactProperties $evidence.restoration @(
    'status', 'external_drift') 'evidence restoration'
if ([string]$evidence.restoration.status -cne 'exact' -or
    [string]$evidence.restoration.external_drift -cne 'none') {
    throw 'Evidence restoration/drift profile is invalid.'
}
Write-Output '[stock-runtime-first-verify] evidence-threshold=passed'
Write-Output '[stock-runtime-first-verify] evidence-json=valid'
Write-Output '[stock-runtime-first-verify] raw-artifacts-ignored=true'
Write-Output '[stock-runtime-first-verify] result=success'
