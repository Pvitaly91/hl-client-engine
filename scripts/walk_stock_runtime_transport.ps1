#requires -Version 5.1

<#
.SYNOPSIS
Independently validates one stock-runtime transport journal and raw inventory.

.DESCRIPTION
This metadata walker does not invoke the production checker. It validates the
JSONL schema, ordinal/reference geometry, raw filenames, sizes and SHA-256,
then independently classifies connectionless and sequenced datagrams and the
sequenced header flags. It never prints packet bytes or endpoint addresses.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$CaptureRoot,

    [Parameter()][ValidateRange(-1, 65536)]
    [Int64]$BoundaryPayloadOrdinal = -1,
    [Parameter()][ValidateRange(-1, 65535)]
    [Int64]$BoundaryObservedOrdinal = -1,
    [Parameter()][ValidateRange(-1, 131071)]
    [Int64]$BoundaryDeliveryOrdinal = -1,
    [Parameter()][ValidateRange(-1, 1048576)]
    [Int64]$BoundaryByteOffset = -1,
    [Parameter()][ValidateRange(-1, 7)]
    [Int64]$BoundaryBitOffset = -1,
    [Parameter()][ValidateRange(-1, 1073741823)]
    [Int64]$BoundarySourceSequence = -1,
    [Parameter()][ValidateRange(-1, 1048576)]
    [Int64]$BoundarySourcePayloadBytes = -1,
    [Parameter()][ValidateRange(-1, 8388608)]
    [Int64]$BoundarySourcePayloadBits = -1,
    [Parameter()][ValidateRange(-1, 8388608)]
    [Int64]$BoundaryNextUnconsumedBits = -1,
    [Parameter()][ValidateSet('', 'true', 'false')]
    [string]$BoundaryReassembled = '',
    [Parameter()][ValidateSet('', 'true', 'false')]
    [string]$BoundaryDecompressed = '',
    [Parameter()][ValidateRange(0, 8)]
    [Int64]$CandidateBitWidth = 0,
    [Parameter()][AllowEmptyString()]
    [string]$FirstCandidate = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$maximumEntries = 65536
$maximumJournalBytes = 67108864
$maximumLineBytes = 4096
$maximumPayloadBytes = 65507
$maximumTotalRawBytes = [Int64]536870912
$journalSchema = 'hlclient.stock-runtime-transport-journal.v1'
$requiredProperties = @(
    'schema', 'observed_ordinal', 'direction', 'direction_ordinal',
    'relative_timestamp_us', 'payload_byte_count', 'raw_filename',
    'source_role', 'destination_role', 'action', 'hold_state',
    'emitted_ordinals', 'delivered', 'wrong_source', 'sha256')
$allowedActions = @('forward', 'drop', 'duplicate', 'hold_for_delay', 'hold_for_reorder')
$allowedHoldStates = @('none', 'held', 'released', 'unresolved')
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
$requiredCaptureParent = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'manual-artifacts\stock-runtime')).TrimEnd('\', '/')

function Assert-NoReparsePointInExistingPath {
    param([string]$Path, [string]$Label)
    $full = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetPathRoot($full)
    $current = $root
    foreach ($component in @($full.Substring($root.Length) -split '[\\/]' |
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
    $linkType = $item.PSObject.Properties['LinkType']
    if ($null -eq $linkType -or -not [string]::IsNullOrEmpty([string]$linkType.Value)) {
        throw "$Label must be an unlinked regular file."
    }
}

function Get-StrictInteger {
    param([object]$Value, [string]$Name, [Int64]$Minimum, [Int64]$Maximum)
    $property = $Value.PSObject.Properties[$Name]
    if ($null -eq $property -or $property.Value -is [bool] -or
        $property.Value -isnot [ValueType]) {
        throw "Journal property $Name is not an integer."
    }
    [Int64]$number = $property.Value
    if ([double]$property.Value -ne [double]$number -or
        $number -lt $Minimum -or $number -gt $Maximum) {
        throw "Journal property $Name is outside its bound."
    }
    return $number
}

function Get-StringSha256 {
    param([string]$Value)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Value)
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    } finally { $sha.Dispose() }
}

function Get-UInt32LittleEndian {
    param([byte[]]$Bytes, [int]$Offset)
    return [uint32]$Bytes[$Offset] -bor
        ([uint32]$Bytes[$Offset + 1] -shl 8) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 16) -bor
        ([uint32]$Bytes[$Offset + 3] -shl 24)
}

function Decode-NetchanPayload {
    param([byte[]]$Datagram, [uint32]$NumericSequence)
    $decoded = [byte[]]::new($Datagram.Length - 8)
    [Array]::Copy($Datagram, 8, $decoded, 0, $decoded.Length)
    $table = [byte[]](
        0x05, 0x61, 0x7a, 0xed, 0x1b, 0xca, 0x0d, 0x9b,
        0x4a, 0xf1, 0x64, 0xc7, 0xb5, 0x8e, 0xdf, 0xa0)
    [uint64]$key = $NumericSequence -band 0xff
    [uint64]$inverseKey = [uint64]0xffffffff - $key
    $wordCount = [Math]::Floor($decoded.Length / 4)
    for ($wordIndex = 0; $wordIndex -lt $wordCount; $wordIndex++) {
        $offset = $wordIndex * 4
        [uint64]$value = Get-UInt32LittleEndian $decoded $offset
        $value = ($value -bxor $key) -band [uint64]0xffffffff
        for ($byteIndex = 0; $byteIndex -lt 4; $byteIndex++) {
            $shiftedIndex = [uint64]$byteIndex -shl $byteIndex
            $tableValue = [uint64]$table[(($wordIndex -band 15) + $byteIndex) -band 15]
            $mask = ([uint64]0xa5 -bor $shiftedIndex -bor
                [uint64]$byteIndex -bor $tableValue) -band 0xff
            $value = ($value -bxor ($mask -shl ($byteIndex * 8))) -band
                [uint64]0xffffffff
        }
        $swapped = (($value -band 0x000000ff) -shl 24) -bor
            (($value -band 0x0000ff00) -shl 8) -bor
            (($value -band 0x00ff0000) -shr 8) -bor
            (($value -band 0xff000000) -shr 24)
        $value = ($swapped -bxor $inverseKey) -band [uint64]0xffffffff
        for ($byteIndex = 0; $byteIndex -lt 4; $byteIndex++) {
            $decoded[$offset + $byteIndex] = [byte](
                ($value -shr ($byteIndex * 8)) -band 0xff)
        }
    }
    return ,$decoded
}

function Get-NetchanGeometry {
    param([byte[]]$Bytes)
    $isConnectionless = $Bytes.Length -ge 4 -and $Bytes[0] -eq 0xff -and
        $Bytes[1] -eq 0xff -and $Bytes[2] -eq 0xff -and $Bytes[3] -eq 0xff
    if ($isConnectionless) {
        return [pscustomobject]@{
            Classification = 'connectionless'; Fragmented = $false
            Reliable = $false; FragmentDescriptorCount = 0
            Sequence = $null; PayloadByteCount = $Bytes.Length - 4
        }
    }
    if ($Bytes.Length -lt 8) { throw 'Datagram is shorter than the netchan header.' }
    if ($Bytes.Length -gt 16384) {
        throw 'Sequenced datagram exceeds the established netchan codec bound.'
    }
    [uint32]$sequenceWord = Get-UInt32LittleEndian $Bytes 0
    [uint32]$acknowledgementWord = Get-UInt32LittleEndian $Bytes 4
    if ($sequenceWord -eq [uint32]0xfffffffe -or
        ($acknowledgementWord -band [uint32]0x40000000) -ne 0) {
        throw 'Datagram uses an unsupported special/reserved netchan word.'
    }
    [uint32]$numericSequence = $sequenceWord -band [uint32]0x3fffffff
    $fragmented = ($sequenceWord -band [uint32]0x40000000) -ne 0
    $reliable = ($sequenceWord -band [uint32]0x80000000) -ne 0
    $descriptorCount = 0
    if ($fragmented) {
        $body = Decode-NetchanPayload $Bytes $numericSequence
        $cursor = 0
        $ranges = [Collections.Generic.List[object]]::new()
        for ($slot = 0; $slot -lt 2; $slot++) {
            if ($cursor -ge $body.Length) { throw 'Fragment presence geometry is truncated.' }
            $presence = $body[$cursor]
            $cursor++
            if ($presence -gt 1) { throw 'Fragment presence value is invalid.' }
            if ($presence -eq 0) { continue }
            if ($cursor -gt ($body.Length - 8)) { throw 'Fragment descriptor is truncated.' }
            [uint32]$packed = Get-UInt32LittleEndian $body $cursor
            $offset = [uint16]($body[$cursor + 4] -bor ($body[$cursor + 5] -shl 8))
            $length = [uint16]($body[$cursor + 6] -bor ($body[$cursor + 7] -shl 8))
            $cursor += 8
            $fragmentIndex = [uint16]($packed -shr 16)
            $fragmentCount = [uint16]($packed -band 0xffff)
            if ($fragmentIndex -eq 0 -or $fragmentCount -eq 0 -or
                $fragmentIndex -gt $fragmentCount -or $length -eq 0 -or
                ([Int64]$offset + [Int64]$length) -gt 65535) {
                throw 'Fragment ID/count/length geometry is invalid.'
            }
            [void]$ranges.Add([pscustomobject]@{
                    Offset = [Int64]$offset; Length = [Int64]$length })
            $descriptorCount++
        }
        if ($descriptorCount -eq 0) {
            throw 'Fragment flag is set without a present descriptor.'
        }
        [Int64]$payloadLength = $body.Length - $cursor
        [Int64]$expectedOffset = 0
        foreach ($range in @($ranges | Sort-Object Offset)) {
            if ($range.Offset -ne $expectedOffset -or
                $range.Offset -gt ($payloadLength - $range.Length)) {
                throw 'Fragment payload ranges are overlapping, gapped or out of bounds.'
            }
            $expectedOffset += $range.Length
        }
    }
    return [pscustomobject]@{
        Classification = 'sequenced'; Fragmented = $fragmented
        Reliable = $reliable; FragmentDescriptorCount = $descriptorCount
        Sequence = [Int64]$numericSequence
        PayloadByteCount = $(if ($fragmented) { $null } else { $Bytes.Length - 8 })
    }
}

function New-BoundaryMetadata {
    param(
        [Int64]$PayloadOrdinal, [Int64]$ObservedOrdinal,
        [Int64]$DeliveryOrdinal, [Int64]$ByteOffset, [Int64]$BitOffset,
        [Int64]$SourceSequence, [Int64]$SourcePayloadBytes,
        [Int64]$SourcePayloadBits, [Int64]$NextUnconsumedBits,
        [bool]$Reassembled, [bool]$Decompressed,
        [Int64]$BitWidth, [string]$Candidate)
    return [pscustomobject]@{
        PayloadOrdinal = $PayloadOrdinal
        ObservedOrdinal = $ObservedOrdinal
        DeliveryOrdinal = $DeliveryOrdinal
        ByteOffset = $ByteOffset
        BitOffset = $BitOffset
        SourceSequence = $SourceSequence
        SourcePayloadBytes = $SourcePayloadBytes
        SourcePayloadBits = $SourcePayloadBits
        NextUnconsumedBits = $NextUnconsumedBits
        Reassembled = $Reassembled
        Decompressed = $Decompressed
        CandidateBitWidth = $BitWidth
        FirstCandidate = $Candidate
    }
}

$boundaryArgumentCount = @(
    $BoundaryPayloadOrdinal -ge 0, $BoundaryObservedOrdinal -ge 0,
    $BoundaryDeliveryOrdinal -ge 0, $BoundaryByteOffset -ge 0,
    $BoundaryBitOffset -ge 0, $BoundarySourceSequence -ge 0,
    $BoundarySourcePayloadBytes -ge 0, $BoundarySourcePayloadBits -ge 0,
    $BoundaryNextUnconsumedBits -ge 0,
    -not [string]::IsNullOrEmpty($BoundaryReassembled),
    -not [string]::IsNullOrEmpty($BoundaryDecompressed),
    $CandidateBitWidth -gt 0, -not [string]::IsNullOrEmpty($FirstCandidate) |
        Where-Object { $_ }).Count
if ($boundaryArgumentCount -ne 0 -and $boundaryArgumentCount -ne 13) {
    throw 'Exact boundary metadata arguments must be supplied as one complete set.'
}
$requestedBoundary = $null
if ($boundaryArgumentCount -eq 13) {
    $requestedBoundary = New-BoundaryMetadata `
        $BoundaryPayloadOrdinal $BoundaryObservedOrdinal $BoundaryDeliveryOrdinal `
        $BoundaryByteOffset $BoundaryBitOffset $BoundarySourceSequence `
        $BoundarySourcePayloadBytes $BoundarySourcePayloadBits `
        $BoundaryNextUnconsumedBits ($BoundaryReassembled -ceq 'true') `
        ($BoundaryDecompressed -ceq 'true') $CandidateBitWidth $FirstCandidate
}

$root = [IO.Path]::GetFullPath($CaptureRoot).TrimEnd('\', '/')
if (-not (Test-Path -LiteralPath $root -PathType Container) -or
    [IO.Path]::GetFileName($root) -cnotmatch '^[0-9a-f]{32}$' -or
    [IO.Path]::GetFullPath((Split-Path -Parent $root)).TrimEnd('\', '/') -ine
        $requiredCaptureParent) {
    throw 'CaptureRoot must be an exact run child of repository manual-artifacts/stock-runtime.'
}
Assert-NoReparsePointInExistingPath $root 'capture run'
$runItem = Get-Item -LiteralPath $root -Force
if (($runItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'CaptureRoot must not be a reparse point.'
}
$runId = $runItem.Name
$journalPath = Join-Path $root 'transport-journal.jsonl'
$rawRoot = Join-Path $root 'raw'
if (-not (Test-Path -LiteralPath $journalPath -PathType Leaf)) {
    throw 'transport-journal.jsonl is absent.'
}
if (-not (Test-Path -LiteralPath $rawRoot -PathType Container)) {
    throw 'raw directory is absent.'
}
Assert-NoReparsePointInExistingPath $journalPath 'transport journal'
Assert-NoReparsePointInExistingPath $rawRoot 'raw inventory'
Assert-OnlyDefaultDataStream $journalPath 'transport journal'
Assert-NoHardLink $journalPath 'transport journal'
$journalItem = Get-Item -LiteralPath $journalPath -Force
if ($journalItem.Length -lt 1 -or $journalItem.Length -gt $maximumJournalBytes) {
    throw 'Transport journal length is outside its bound.'
}

$lines = @(Get-Content -LiteralPath $journalPath -Encoding UTF8)
if ($lines.Count -lt 1 -or $lines.Count -gt $maximumEntries) {
    throw 'Transport journal cardinality is outside its bound.'
}
$rawFiles = @(Get-ChildItem -LiteralPath $rawRoot -Force)
if ($rawFiles.Count -ne $lines.Count) {
    throw 'Journal/raw cardinality mismatch.'
}
foreach ($rawItem in $rawFiles) {
    if ($rawItem.PSIsContainer -or
        ($rawItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
        $rawItem.Name -cnotmatch '^[0-9]{8}-(?:c2s|s2c)\.bin$') {
        throw 'Raw inventory contains an unexpected entry.'
    }
}

$seenRaw = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$emissions = [Collections.Generic.List[Int64]]::new()
[Int64]$totalRawBytes = 0
[Int64]$lastTimestamp = -1
$directionCounts = @{ c2s = [Int64]0; s2c = [Int64]0 }
$observedConnectionless = @{ c2s = [Int64]0; s2c = [Int64]0 }
$observedSequenced = @{ c2s = [Int64]0; s2c = [Int64]0 }
$deliveredConnectionless = @{ c2s = [Int64]0; s2c = [Int64]0 }
$deliveredSequenced = @{ c2s = [Int64]0; s2c = [Int64]0 }
[Int64]$observedFragments = 0
[Int64]$deliveredFragments = 0
[Int64]$observedReliable = 0
[Int64]$deliveredReliable = 0
[Int64]$deliveredC2s = 0
[Int64]$deliveredS2c = 0
[Int64]$wrongSourceCount = 0
$deliveryRecords = [Collections.Generic.Dictionary[Int64, object]]::new()
$semanticEntries = [Collections.Generic.List[object]]::new()
$transportComplete = $true
[Int64]$lastDeliveredSequencedS2cTimestampUs = -1

for ($index = 0; $index -lt $lines.Count; $index++) {
    $line = [string]$lines[$index]
    if ([Text.Encoding]::UTF8.GetByteCount($line) -lt 2 -or
        [Text.Encoding]::UTF8.GetByteCount($line) -gt $maximumLineBytes) {
        throw "Journal line $index is outside its byte bound."
    }
    $names = @([regex]::Matches($line, '"(?<name>[a-z0-9_]+)"\s*:') |
        ForEach-Object { $_.Groups['name'].Value })
    if ($names.Count -ne $requiredProperties.Count -or
        @($names | Sort-Object -Unique).Count -ne $requiredProperties.Count) {
        throw "Journal line $index has duplicate, missing or unknown properties."
    }
    foreach ($name in $names) {
        if ($requiredProperties -cnotcontains $name) {
            throw "Journal line $index has unknown property $name."
        }
    }
    try { $entry = $line | ConvertFrom-Json -ErrorAction Stop }
    catch { throw "Journal line $index is invalid JSON." }
    if ([string]$entry.schema -cne $journalSchema) {
        throw "Journal line $index has the wrong schema."
    }
    if ((Get-StrictInteger $entry observed_ordinal 0 ($maximumEntries - 1)) -ne $index) {
        throw "Journal observed ordinals are not contiguous at $index."
    }
    $direction = [string]$entry.direction
    if ($direction -cne 'client_to_server' -and
        $direction -cne 'server_to_client') {
        throw "Journal line $index has an invalid direction."
    }
    $shortDirection = if ($direction -ceq 'client_to_server') { 'c2s' } else { 's2c' }
    $directionCounts[$shortDirection]++
    if ((Get-StrictInteger $entry direction_ordinal 1 $maximumEntries) -ne
        $directionCounts[$shortDirection]) {
        throw "Journal direction ordinals are not contiguous at $index."
    }
    $timestamp = Get-StrictInteger $entry relative_timestamp_us 0 300000000
    if ($timestamp -lt $lastTimestamp) { throw 'Journal timestamps are not monotonic.' }
    $lastTimestamp = $timestamp
    $payloadBytes = Get-StrictInteger $entry payload_byte_count 1 $maximumPayloadBytes
    if ($totalRawBytes -gt ($maximumTotalRawBytes - $payloadBytes)) {
        throw 'Raw inventory exceeds its total byte bound.'
    }
    $totalRawBytes += $payloadBytes
    $expectedName = '{0:D8}-{1}.bin' -f $index, $shortDirection
    if ([string]$entry.raw_filename -cne $expectedName -or
        -not $seenRaw.Add($expectedName)) {
        throw "Journal raw filename is invalid at $index."
    }
    $expectedSource = if ($shortDirection -ceq 'c2s') {
        'research_client'
    } else { 'research_server' }
    $expectedDestination = if ($shortDirection -ceq 'c2s') {
        'research_server'
    } else { 'research_client' }
    if ([string]$entry.destination_role -cne $expectedDestination -or
        (-not [bool]$entry.wrong_source -and
            [string]$entry.source_role -cne $expectedSource) -or
        ([bool]$entry.wrong_source -and
            [string]$entry.source_role -cne 'unexpected_source')) {
        throw "Journal roles disagree with direction/source admission at $index."
    }
    if ($allowedActions -cnotcontains [string]$entry.action -or
        $allowedHoldStates -cnotcontains [string]$entry.hold_state -or
        $entry.delivered -isnot [bool] -or $entry.wrong_source -isnot [bool]) {
        throw "Journal action/state is invalid at $index."
    }
    if ($entry.wrong_source) {
        $wrongSourceCount++
        $transportComplete = $false
    }
    $entryEmissions = @($entry.emitted_ordinals)
    if ($entryEmissions.Count -gt 2) { throw 'Journal entry exceeds its emission bound.' }
    foreach ($emission in $entryEmissions) {
        if ($emission -is [bool] -or $emission -isnot [ValueType]) {
            throw 'Journal emission ordinal is not an integer.'
        }
        [Int64]$emissionValue = $emission
        if ([double]$emission -ne [double]$emissionValue -or
            $emissionValue -lt 0 -or $emissionValue -ge ($maximumEntries * 2)) {
            throw 'Journal emission ordinal is outside its bound.'
        }
        [void]$emissions.Add($emissionValue)
    }
    for ($emissionIndex = 1; $emissionIndex -lt $entryEmissions.Count;
        $emissionIndex++) {
        if ([Int64]$entryEmissions[$emissionIndex] -ne
            ([Int64]$entryEmissions[$emissionIndex - 1] + 1)) {
            throw 'One datagram duplicate emissions are not consecutive.'
        }
    }
    if ([bool]$entry.delivered -ne ($entryEmissions.Count -ne 0)) {
        throw "Journal delivered state disagrees with emissions at $index."
    }
    $action = [string]$entry.action
    $holdState = [string]$entry.hold_state
    if ($action -ceq 'forward') {
        $requiredEmissions = if ($entry.wrong_source) { 0 } else { 1 }
        if ($holdState -cne 'none' -or
            $entryEmissions.Count -ne $requiredEmissions) {
            throw 'Forward journal action has invalid hold/emission state.'
        }
    } elseif ($action -ceq 'drop') {
        if ($holdState -cne 'none' -or $entryEmissions.Count -ne 0) {
            throw 'Drop journal action has invalid hold/emission state.'
        }
    } elseif ($action -ceq 'duplicate') {
        if ($entry.wrong_source -or $holdState -cne 'none' -or
            $entryEmissions.Count -ne 2) {
            throw 'Duplicate journal action must own two emissions.'
        }
    } else {
        if ($entry.wrong_source) {
            throw 'Wrong-source journal entry cannot enter a held action.'
        }
        if ($holdState -ceq 'held') {
            if ($entryEmissions.Count -ne 0) {
                throw 'Held journal action cannot own an emission.'
            }
            $transportComplete = $false
        } elseif ($holdState -ceq 'unresolved') {
            if ($entryEmissions.Count -gt 1) {
                throw 'Unresolved journal action exceeds its deadline emission bound.'
            }
            $transportComplete = $false
        } elseif ($holdState -cne 'released' -or $entryEmissions.Count -ne 1) {
            throw 'Held journal action has invalid hold/emission state.'
        }
    }
    if ($entry.delivered) {
        if ($shortDirection -ceq 'c2s') { $deliveredC2s += $entryEmissions.Count }
        else { $deliveredS2c += $entryEmissions.Count }
    }

    $rawPath = Join-Path $rawRoot $expectedName
    Assert-NoReparsePointInExistingPath $rawPath 'raw datagram'
    Assert-OnlyDefaultDataStream $rawPath 'raw datagram'
    Assert-NoHardLink $rawPath 'raw datagram'
    $raw = Get-Item -LiteralPath $rawPath -Force
    if ($raw.Length -ne $payloadBytes -or
        [string]$entry.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        (Get-FileHash -LiteralPath $rawPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne
            [string]$entry.sha256) {
        throw "Raw size or SHA-256 mismatch at $index."
    }
    $bytes = [IO.File]::ReadAllBytes($rawPath)
    $geometry = Get-NetchanGeometry $bytes
    if ($geometry.Classification -ceq 'connectionless') {
        $observedConnectionless[$shortDirection]++
    } else {
        $observedSequenced[$shortDirection]++
        if ($geometry.Fragmented) { $observedFragments++ }
        if ($geometry.Reliable) { $observedReliable++ }
    }
    foreach ($emission in $entryEmissions) {
        [Int64]$emissionValue = $emission
        if ($deliveryRecords.ContainsKey($emissionValue)) {
            throw 'Emission ordinal is referenced more than once.'
        }
        $deliveryRecords.Add($emissionValue, [pscustomobject]@{
                Path = $rawPath; Direction = $shortDirection
                ObservedOrdinal = [Int64]$index
                RelativeTimestampUs = [Int64]$timestamp })
    }
    [void]$semanticEntries.Add([pscustomobject]@{
            Direction = $shortDirection
            Action = $action
            HoldState = $holdState
            Emissions = @($entryEmissions | ForEach-Object { [Int64]$_ })
        })
}

# A released hold is paired with the next observed datagram in the same
# direction. Delay emits the held bytes first; reorder emits the successor
# first. A deadline-released delay may have no successor, but reorder cannot.
for ($index = 0; $index -lt $semanticEntries.Count; $index++) {
    $held = $semanticEntries[$index]
    if (($held.Action -cne 'hold_for_delay' -and
            $held.Action -cne 'hold_for_reorder') -or
        $held.HoldState -cne 'released') {
        continue
    }
    $successor = $null
    for ($candidateIndex = $index + 1;
        $candidateIndex -lt $semanticEntries.Count; $candidateIndex++) {
        if ($semanticEntries[$candidateIndex].Direction -ceq $held.Direction) {
            $successor = $semanticEntries[$candidateIndex]
            break
        }
    }
    if ($null -eq $successor) {
        if ($held.Action -ceq 'hold_for_delay') { continue }
        throw 'Released reorder has no same-direction successor.'
    }
    if ($successor.Action -cne 'forward' -or
        $successor.Emissions.Count -ne 1 -or $held.Emissions.Count -ne 1) {
        throw 'Held datagram has no exact same-direction release successor.'
    }
    $delayOrder = [Int64]$held.Emissions[0] -lt [Int64]$successor.Emissions[0]
    if (($held.Action -ceq 'hold_for_delay' -and -not $delayOrder) -or
        ($held.Action -ceq 'hold_for_reorder' -and $delayOrder)) {
        throw 'Held-datagram emission order contradicts the selected action.'
    }
}

$orderedEmissions = @($emissions | Sort-Object)
for ($index = 0; $index -lt $orderedEmissions.Count; $index++) {
    if ($orderedEmissions[$index] -ne $index) {
        throw 'Emitted ordinals are not one contiguous peer-visible sequence.'
    }
}

for ([Int64]$index = 0; $index -lt $emissions.Count; $index++) {
    if (-not $deliveryRecords.ContainsKey($index)) {
        throw 'Delivered stream lacks a contiguous emission record.'
    }
    $record = $deliveryRecords[$index]
    $geometry = Get-NetchanGeometry ([IO.File]::ReadAllBytes($record.Path))
    if ($geometry.Classification -ceq 'connectionless') {
        $deliveredConnectionless[$record.Direction]++
    } else {
        $deliveredSequenced[$record.Direction]++
        if ($geometry.Fragmented) { $deliveredFragments++ }
        if ($geometry.Reliable) { $deliveredReliable++ }
        if ($record.Direction -ceq 's2c' -and
            $record.RelativeTimestampUs -gt $lastDeliveredSequencedS2cTimestampUs) {
            $lastDeliveredSequencedS2cTimestampUs = $record.RelativeTimestampUs
        }
    }
}

$finalManifestState = 'absent-prepublication'
$boundaryState = 'not-published'
$boundary = $requestedBoundary
$finalManifestPath = Join-Path $root 'research-run-metadata.json'
if (Test-Path -LiteralPath $finalManifestPath -PathType Leaf) {
    Assert-NoReparsePointInExistingPath $finalManifestPath 'research run manifest'
    Assert-OnlyDefaultDataStream $finalManifestPath 'research run manifest'
    Assert-NoHardLink $finalManifestPath 'research run manifest'
    $manifestItem = Get-Item -LiteralPath $finalManifestPath -Force
    if ($manifestItem.Length -lt 2 -or $manifestItem.Length -gt 131072) {
        throw 'Research run manifest length is outside its bound.'
    }
    try {
        $manifest = Get-Content -Raw -LiteralPath $finalManifestPath -Encoding UTF8 |
            ConvertFrom-Json
    } catch { throw 'Research run manifest is invalid JSON.' }
    if ([string]$manifest.schema -cne 'hlclient.stock-runtime-research-run.v1' -or
        [string]$manifest.run_id -cne $runId) {
        throw 'Research run manifest identity is invalid.'
    }
    if ($manifest.accepted_evidence_run -isnot [bool] -or
        $manifest.accepted_transport_run -isnot [bool]) {
        throw 'Research run manifest acceptance fields are not Boolean.'
    }
    if ($manifest.accepted_evidence_run) {
        if ((Get-StrictInteger $manifest raw_datagram_count 1 $maximumEntries) -ne
                $rawFiles.Count -or
            (Get-StrictInteger $manifest journal_entry_count 1 $maximumEntries) -ne
                $lines.Count -or
            (Get-StrictInteger $manifest delivered_sequenced_c2s_count 0 ($maximumEntries * 2)) -ne
                $deliveredSequenced.c2s -or
            (Get-StrictInteger $manifest delivered_sequenced_s2c_count 0 ($maximumEntries * 2)) -ne
                $deliveredSequenced.s2c -or
            (Get-StrictInteger $manifest delivered_fragment_datagram_count 0 ($maximumEntries * 2)) -ne
                $deliveredFragments -or
            (Get-StrictInteger $manifest last_observed_transport_timestamp_us `
                0 300000000) -ne $lastTimestamp -or
            (Get-StrictInteger $manifest last_delivered_sequenced_s2c_timestamp_us `
                0 300000000) -ne $lastDeliveredSequencedS2cTimestampUs -or
            -not $manifest.accepted_transport_run -or -not $transportComplete -or
            $wrongSourceCount -ne 0 -or
            [string]$manifest.restoration_status -cne 'exact' -or
            [string]$manifest.external_drift_status -cne 'none' -or
            [string]$manifest.post_resource_boundary_status -cne 'observed' -or
            $manifest.post_resource_reassembled -isnot [bool] -or
            $manifest.post_resource_decompressed -isnot [bool] -or
            $manifest.post_resource_boundary_byte_aligned -isnot [bool] -or
            [string]$manifest.first_observation_status -cne 'observed' -or
            [string]$manifest.first_candidate -cnotmatch
                '^(?:[0-9]{1,3}|bit-prefix:[0-9]{1,3})$' -or
            [int]([string]$manifest.first_candidate -replace '^bit-prefix:', '') -gt 255 -or
            (Get-StrictInteger $manifest first_candidate_recurrence 1 1) -ne 1 -or
            [string]$manifest.transport_structural_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
            [string]$manifest.replay_structural_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
            [string]$manifest.candidate_stability -cne 'single_observation') {
            throw 'Accepted research run manifest disagrees with walker geometry/gates.'
        }
        $manifestBoundary = New-BoundaryMetadata `
            (Get-StrictInteger $manifest post_resource_replay_payload_ordinal 0 65536) `
            (Get-StrictInteger $manifest post_resource_corpus_observed_ordinal 0 65535) `
            (Get-StrictInteger $manifest post_resource_delivery_ordinal 0 131071) `
            (Get-StrictInteger $manifest post_resource_byte_offset 0 1048576) `
            (Get-StrictInteger $manifest post_resource_bit_offset 0 7) `
            (Get-StrictInteger $manifest post_resource_source_sequence 0 1073741823) `
            (Get-StrictInteger $manifest post_resource_source_payload_bytes 1 1048576) `
            (Get-StrictInteger $manifest post_resource_source_payload_bits 8 8388608) `
            (Get-StrictInteger $manifest post_resource_next_unconsumed_bits 1 8388608) `
            ([bool]$manifest.post_resource_reassembled) `
            ([bool]$manifest.post_resource_decompressed) `
            (Get-StrictInteger $manifest first_candidate_bit_width 1 8) `
            ([string]$manifest.first_candidate)
        if ($null -ne $boundary -and
            ($boundary | ConvertTo-Json -Compress) -cne
                ($manifestBoundary | ConvertTo-Json -Compress)) {
            throw 'Caller boundary metadata disagrees with the final manifest.'
        }
        $boundary = $manifestBoundary
        $finalManifestState = 'accepted'
    } else {
        if ([string]::IsNullOrWhiteSpace([string]$manifest.failure_category) -or
            [string]$manifest.failure_category -ceq 'none') {
            throw 'Incomplete/rejected run lacks a typed failure category.'
        }
        $finalManifestState = 'not-accepted'
    }
    $boundaryState = [string]$manifest.post_resource_boundary_status
}

$boundaryOutput = [ordered]@{
    PayloadOrdinal = 'unavailable'; ObservedOrdinal = 'unavailable'
    DeliveryOrdinal = 'unavailable'; ByteOffset = 'unavailable'
    BitOffset = 'unavailable'; SourceSequence = 'unavailable'
    SourcePayloadBytes = 'unavailable'; SourcePayloadBits = 'unavailable'
    NextUnconsumedBits = 'unavailable'; Reassembled = 'unavailable'
    Decompressed = 'unavailable'; ByteAligned = 'unavailable'
    CandidateBitWidth = 'unavailable'; FirstCandidate = 'unavailable'
    ReplayStructuralHash = 'unavailable'
}
if ($null -ne $boundary) {
    if ($boundary.PayloadOrdinal -lt 0 -or $boundary.PayloadOrdinal -gt 65536 -or
        $boundary.ObservedOrdinal -lt 0 -or $boundary.ObservedOrdinal -ge $lines.Count -or
        $boundary.DeliveryOrdinal -lt 0 -or
        -not $deliveryRecords.ContainsKey($boundary.DeliveryOrdinal) -or
        $boundary.ByteOffset -lt 0 -or $boundary.BitOffset -lt 0 -or
        $boundary.BitOffset -gt 7 -or $boundary.SourcePayloadBytes -lt 1 -or
        $boundary.SourcePayloadBits -ne ($boundary.SourcePayloadBytes * 8) -or
        (($boundary.ByteOffset * 8) + $boundary.BitOffset +
            $boundary.NextUnconsumedBits) -ne $boundary.SourcePayloadBits -or
        $boundary.NextUnconsumedBits -lt 1 -or
        $boundary.CandidateBitWidth -lt 1 -or $boundary.CandidateBitWidth -gt 8 -or
        $boundary.CandidateBitWidth -gt $boundary.NextUnconsumedBits -or
        [string]$boundary.FirstCandidate -cnotmatch
            '^(?:[0-9]{1,3}|bit-prefix:[0-9]{1,3})$' -or
        [int]([string]$boundary.FirstCandidate -replace '^bit-prefix:', '') -gt 255 -or
        ((-not ([string]$boundary.FirstCandidate).StartsWith('bit-prefix:')) -and
            ($boundary.BitOffset -ne 0 -or $boundary.CandidateBitWidth -ne 8)) -or
        (([string]$boundary.FirstCandidate).StartsWith('bit-prefix:') -and
            ($boundary.BitOffset -eq 0 -or
                [int]([string]$boundary.FirstCandidate).Substring(11) -ge
                    [Math]::Pow(2, $boundary.CandidateBitWidth)))) {
        throw 'Exact post-resource boundary has inconsistent bounded geometry.'
    }
    $sourceRecord = $deliveryRecords[$boundary.DeliveryOrdinal]
    if ($sourceRecord.Direction -cne 's2c' -or
        $sourceRecord.ObservedOrdinal -ne $boundary.ObservedOrdinal) {
        throw 'Exact post-resource boundary does not reference its delivered S2C owner.'
    }
    $sourceGeometry = Get-NetchanGeometry ([IO.File]::ReadAllBytes($sourceRecord.Path))
    if ($sourceGeometry.Classification -cne 'sequenced' -or
        $sourceGeometry.Sequence -ne $boundary.SourceSequence -or
        ($boundary.Reassembled -and -not $sourceGeometry.Fragmented) -or
        (-not $boundary.Reassembled -and -not $boundary.Decompressed -and
            -not $sourceGeometry.Fragmented -and
            $sourceGeometry.PayloadByteCount -ne $boundary.SourcePayloadBytes)) {
        throw 'Exact post-resource boundary source provenance disagrees with the independently decoded netchan datagram.'
    }
    $canonical = 'hlclient.stock-runtime-replay-structure.v1' +
        "|run=$runId" +
        "|replay-payload=$($boundary.PayloadOrdinal)" +
        "|observed=$($boundary.ObservedOrdinal)" +
        "|delivery=$($boundary.DeliveryOrdinal)" +
        "|byte=$($boundary.ByteOffset)" +
        "|bit=$($boundary.BitOffset)" +
        "|source-sequence=$($boundary.SourceSequence)" +
        "|source-bytes=$($boundary.SourcePayloadBytes)" +
        "|source-bits=$($boundary.SourcePayloadBits)" +
        "|remaining-bits=$($boundary.NextUnconsumedBits)" +
        ('|reassembled=' + $(if ($boundary.Reassembled) { 'true' } else { 'false' })) +
        ('|decompressed=' + $(if ($boundary.Decompressed) { 'true' } else { 'false' })) +
        "|candidate-width=$($boundary.CandidateBitWidth)" +
        "|candidate=$($boundary.FirstCandidate)"
    $boundaryOutput.PayloadOrdinal = [string]$boundary.PayloadOrdinal
    $boundaryOutput.ObservedOrdinal = [string]$boundary.ObservedOrdinal
    $boundaryOutput.DeliveryOrdinal = [string]$boundary.DeliveryOrdinal
    $boundaryOutput.ByteOffset = [string]$boundary.ByteOffset
    $boundaryOutput.BitOffset = [string]$boundary.BitOffset
    $boundaryOutput.SourceSequence = [string]$boundary.SourceSequence
    $boundaryOutput.SourcePayloadBytes = [string]$boundary.SourcePayloadBytes
    $boundaryOutput.SourcePayloadBits = [string]$boundary.SourcePayloadBits
    $boundaryOutput.NextUnconsumedBits = [string]$boundary.NextUnconsumedBits
    $boundaryOutput.Reassembled = $(if ($boundary.Reassembled) { 'true' } else { 'false' })
    $boundaryOutput.Decompressed = $(if ($boundary.Decompressed) { 'true' } else { 'false' })
    $boundaryOutput.ByteAligned = $(if ($boundary.BitOffset -eq 0) { 'true' } else { 'false' })
    $boundaryOutput.CandidateBitWidth = [string]$boundary.CandidateBitWidth
    $boundaryOutput.FirstCandidate = [string]$boundary.FirstCandidate
    $boundaryOutput.ReplayStructuralHash = Get-StringSha256 $canonical
    $boundaryState = 'observed'
    if ($finalManifestState -ceq 'accepted' -and
        ($boundaryOutput.ReplayStructuralHash -cne
            [string]$manifest.replay_structural_sha256 -or
         ([bool]$manifest.post_resource_boundary_byte_aligned) -ne
            ($boundary.BitOffset -eq 0))) {
        throw 'Independent replay structural hash disagrees with the final manifest.'
    }
}

Write-Output "[stock-runtime-walk] run-id=$runId"
Write-Output "[stock-runtime-walk] journal-entries=$($lines.Count)"
Write-Output "[stock-runtime-walk] raw-datagrams=$($rawFiles.Count)"
Write-Output "[stock-runtime-walk] raw-bytes=$totalRawBytes"
Write-Output "[stock-runtime-walk] observed-c2s=$($directionCounts.c2s)"
Write-Output "[stock-runtime-walk] observed-s2c=$($directionCounts.s2c)"
Write-Output "[stock-runtime-walk] delivered-c2s=$deliveredC2s"
Write-Output "[stock-runtime-walk] delivered-s2c=$deliveredS2c"
Write-Output "[stock-runtime-walk] observed-connectionless-c2s=$($observedConnectionless.c2s)"
Write-Output "[stock-runtime-walk] observed-connectionless-s2c=$($observedConnectionless.s2c)"
Write-Output "[stock-runtime-walk] observed-sequenced-c2s=$($observedSequenced.c2s)"
Write-Output "[stock-runtime-walk] observed-sequenced-s2c=$($observedSequenced.s2c)"
Write-Output "[stock-runtime-walk] observed-fragment-datagrams=$observedFragments"
Write-Output "[stock-runtime-walk] observed-reliable-datagrams=$observedReliable"
Write-Output "[stock-runtime-walk] delivered-connectionless-c2s=$($deliveredConnectionless.c2s)"
Write-Output "[stock-runtime-walk] delivered-connectionless-s2c=$($deliveredConnectionless.s2c)"
Write-Output "[stock-runtime-walk] delivered-sequenced-c2s=$($deliveredSequenced.c2s)"
Write-Output "[stock-runtime-walk] delivered-sequenced-s2c=$($deliveredSequenced.s2c)"
Write-Output "[stock-runtime-walk] delivered-fragment-datagrams=$deliveredFragments"
Write-Output "[stock-runtime-walk] delivered-reliable-datagrams=$deliveredReliable"
Write-Output "[stock-runtime-walk] wrong-source-datagrams=$wrongSourceCount"
Write-Output "[stock-runtime-walk] emitted-datagrams=$($emissions.Count)"
Write-Output "[stock-runtime-walk] last-observed-timestamp-us=$lastTimestamp"
Write-Output "[stock-runtime-walk] last-delivered-sequenced-s2c-timestamp-us=$lastDeliveredSequencedS2cTimestampUs"
Write-Output ("[stock-runtime-walk] transport-complete=" +
    $(if ($transportComplete) { 'true' } else { 'false' }))
Write-Output '[stock-runtime-walk] observed-delivered-policy=distinct'
Write-Output "[stock-runtime-walk] final-manifest=$finalManifestState"
Write-Output "[stock-runtime-walk] post-resource-boundary=$boundaryState"
Write-Output "[stock-runtime-walk] boundary-payload-ordinal=$($boundaryOutput.PayloadOrdinal)"
Write-Output "[stock-runtime-walk] boundary-observed-ordinal=$($boundaryOutput.ObservedOrdinal)"
Write-Output "[stock-runtime-walk] boundary-delivery-ordinal=$($boundaryOutput.DeliveryOrdinal)"
Write-Output "[stock-runtime-walk] boundary-byte-offset=$($boundaryOutput.ByteOffset)"
Write-Output "[stock-runtime-walk] boundary-bit-offset=$($boundaryOutput.BitOffset)"
Write-Output "[stock-runtime-walk] boundary-source-sequence=$($boundaryOutput.SourceSequence)"
Write-Output "[stock-runtime-walk] boundary-source-payload-bytes=$($boundaryOutput.SourcePayloadBytes)"
Write-Output "[stock-runtime-walk] boundary-source-payload-bits=$($boundaryOutput.SourcePayloadBits)"
Write-Output "[stock-runtime-walk] boundary-next-unconsumed-bits=$($boundaryOutput.NextUnconsumedBits)"
Write-Output "[stock-runtime-walk] boundary-reassembled=$($boundaryOutput.Reassembled)"
Write-Output "[stock-runtime-walk] boundary-decompressed=$($boundaryOutput.Decompressed)"
Write-Output "[stock-runtime-walk] boundary-byte-aligned=$($boundaryOutput.ByteAligned)"
Write-Output "[stock-runtime-walk] candidate-bit-width=$($boundaryOutput.CandidateBitWidth)"
Write-Output "[stock-runtime-walk] first-candidate=$($boundaryOutput.FirstCandidate)"
Write-Output "[stock-runtime-walk] replay-structural-hash=$($boundaryOutput.ReplayStructuralHash)"
Write-Output '[stock-runtime-walk] result=success'
