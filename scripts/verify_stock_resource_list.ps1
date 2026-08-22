#requires -Version 5.1

<#
.SYNOPSIS
Projects and validates bounded metadata from the ignored stock opcode-43 corpus.

.DESCRIPTION
This verifier is deliberately offline. It never launches hl.exe, hlds.exe, a
relay, or the project client. Raw service payloads remain below ignored
manual-artifacts roots; only sanitized structural metadata is written to the
tracked evidence projection.

The separate ValidateResearchRoot parameter set is a fail-closed preflight for
a user-supplied isolated Half-Life copy. It rejects Steam-library paths and has
no unsafe override. Passing that preflight still does not launch a process.

.PARAMETER ProjectEvidenceSet
Parse the exact existing ignored corpus and replace only the tracked sanitized
metadata projection. This is also the default parameter set.

.PARAMETER ValidateMetadataPath
Validate the one exact tracked projection file without rewriting it.

.PARAMETER ValidateMetadataSetRoot
Resolve the canonical projection below the exact tracked evidence directory
and validate that its schema and values match this verifier.

.PARAMETER ValidateResearchRoot
Select the read-only isolated research-copy preflight. No process is launched.

.PARAMETER ResearchHalfLifeRoot
Explicit isolated Half-Life copy to preflight. Primary and managed Steam roots
are rejected without an override.

.EXAMPLE
.\scripts\verify_stock_resource_list.ps1 -ProjectEvidenceSet

.EXAMPLE
.\scripts\verify_stock_resource_list.ps1 -ValidateMetadataSetRoot `
  .\docs\evidence

.EXAMPLE
.\scripts\verify_stock_resource_list.ps1 -ValidateResearchRoot `
  -ResearchHalfLifeRoot C:\research\Half-Life-isolated
#>

[CmdletBinding(DefaultParameterSetName = 'Project')]
param(
    [Parameter(ParameterSetName = 'Project')]
    [switch]$ProjectEvidenceSet,

    [Parameter(Mandatory = $true, ParameterSetName = 'Validate')]
    [ValidateNotNullOrEmpty()]
    [string]$ValidateMetadataPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'ValidateSet')]
    [ValidateNotNullOrEmpty()]
    [string]$ValidateMetadataSetRoot,

    [Parameter(Mandatory = $true, ParameterSetName = 'Isolation')]
    [switch]$ValidateResearchRoot,

    [Parameter(Mandatory = $true, ParameterSetName = 'Isolation')]
    [ValidateNotNullOrEmpty()]
    [string]$ResearchHalfLifeRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$scriptPath = [IO.Path]::GetFullPath($MyInvocation.MyCommand.Path)
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $scriptPath) '..'))
$manualRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'manual-artifacts'))
$movevarsSourceRoot = [IO.Path]::GetFullPath((Join-Path $manualRoot 'movevars-captures'))
$transitionSourceRoot = [IO.Path]::GetFullPath((Join-Path $manualRoot 'resource-transition-captures'))
$resourceListRoot = [IO.Path]::GetFullPath((Join-Path $manualRoot 'resource-list-captures'))
$projectionRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'docs/evidence'))
$projectionPath = [IO.Path]::GetFullPath((Join-Path $projectionRoot `
    'GOLDSRC_RESOURCE_LIST_STOCK.json'))

$maximumServicePayloadBytes = 262144
$maximumCaptureMetadataBytes = 262144
$maximumClientBodyBytes = 4096
$maximumResourceCount = 1024
$maximumResourceNameBytes = 1024
$maximumTotalNameBytes = 1048576
$maximumProjectionBytes = 1048576
$expectedPayloadCount = 54
$expectedNormalResponseCount = 51
$expectedCoalescedResponseCount = 3
$expectedResponseFragmentSha256 =
    '451A85ADDBF2B6B2D05E9F424BDFCF711655803706EF6D59B116299D7B5D17C9'
$expectedMaxPlayersOneFirstPayloadSha256 =
    '4B471507120A85056658E05B2422488A1B478759203795BF7C5ADBED060F7A2C'
$expectedValveCustomHeaderSha256 =
    '63ACE5ED6BC60867CE033B429C50EBCEAD683A85AD656688F3EBB3553FA974C1'
$isolationMarkerName = '.hlclient-research-isolated'
$isolationMarkerText = 'HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1'

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

function Get-NormalizedVerifierSha256 {
    $text = [IO.File]::ReadAllText($scriptPath)
    $normalized = $text.Replace("`r`n", "`n").Replace("`r", "`n")
    $encoding = New-Object Text.UTF8Encoding($false)
    return Get-Sha256Hex -Bytes $encoding.GetBytes($normalized)
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
        if (-not (Test-Path -LiteralPath $current)) { continue }
        Assert-NoReparsePoint -Path $current -Label $Label
    }
}

function Assert-OnlyDefaultDataStream {
    param([string]$Path, [string]$Label)
    if ($PSVersionTable.PSVersion.Major -lt 5) { return }
    $streams = @(Get-Item -LiteralPath $Path -Stream * -ErrorAction SilentlyContinue)
    if ($streams.Count -gt 0 -and
        @($streams | Where-Object { $_.Stream -cne ':$DATA' }).Count -ne 0) {
        throw "$Label must contain only the default data stream."
    }
}

function Assert-PathBelowRoot {
    param([string]$Path, [string]$Root, [string]$Label)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/') +
        [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($fullRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label is outside its exact bounded root."
    }
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
            throw "$Label contains an unexpected property '$name'."
        }
    }
}

function Assert-ExactCountTable {
    param(
        [object]$Value,
        [Collections.IDictionary]$Expected,
        [string]$Label)
    Assert-ExactProperties -Value $Value -Allowed @(
        $Expected.Keys | ForEach-Object { [string]$_ }) -Label $Label
    foreach ($key in $Expected.Keys) {
        $name = [string]$key
        $property = $Value.PSObject.Properties[$name]
        if ($null -eq $property -or
            [int64]$property.Value -ne [int64]$Expected[$key]) {
            throw "$Label has an unexpected '$name' count."
        }
    }
}

function Assert-ExactScalarValues {
    param(
        [object]$Value,
        [Collections.IDictionary]$Expected,
        [string]$Label)
    foreach ($key in $Expected.Keys) {
        $name = [string]$key
        $property = $Value.PSObject.Properties[$name]
        if ($null -eq $property) {
            throw "$Label lacks '$name'."
        }
        $actual = $property.Value
        $expectedValue = $Expected[$key]
        if ($expectedValue -is [string]) {
            if ([string]$actual -cne [string]$expectedValue) {
                throw "$Label has an unexpected '$name' value."
            }
        }
        elseif ($expectedValue -is [bool]) {
            if ([bool]$actual -cne [bool]$expectedValue) {
                throw "$Label has an unexpected '$name' value."
            }
        }
        elseif ([decimal]$actual -ne [decimal]$expectedValue) {
            throw "$Label has an unexpected '$name' value."
        }
    }
}

function Resolve-ExactFile {
    param(
        [string]$Directory,
        [string]$Name,
        [int]$MaximumBytes,
        [string]$Label)
    $candidate = [IO.Path]::GetFullPath((Join-Path $Directory $Name))
    Assert-PathBelowRoot -Path $candidate -Root $Directory -Label $Label
    if ([IO.Path]::GetFullPath((Split-Path -Parent $candidate)) -cne
        [IO.Path]::GetFullPath($Directory)) {
        throw "$Label must be an immediate child of its bounded run directory."
    }
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "$Label is absent."
    }
    Assert-NoReparsePoint -Path $candidate -Label $Label
    Assert-OnlyDefaultDataStream -Path $candidate -Label $Label
    $length = (Get-Item -LiteralPath $candidate -Force).Length
    if ($length -lt 1 -or $length -gt $MaximumBytes) {
        throw "$Label is outside its byte bound."
    }
    return $candidate
}

function Read-U32Le {
    param([byte[]]$Bytes, [int]$Offset)
    if ($Offset -lt 0 -or $Offset + 4 -gt $Bytes.Length) {
        throw 'u32le read exceeds the bounded input.'
    }
    return [uint32]([uint32]$Bytes[$Offset] -bor
        ([uint32]$Bytes[$Offset + 1] -shl 8) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 16) -bor
        ([uint32]$Bytes[$Offset + 3] -shl 24))
}

function Read-LsbBits {
    param(
        [byte[]]$Bytes,
        [ref]$Cursor,
        [int]$Width,
        [string]$Label)
    if ($Width -lt 1 -or $Width -gt 32) {
        throw "$Label requests an invalid bit width."
    }
    $start = [int64]$Cursor.Value
    $available = [int64]$Bytes.Length * 8
    if ($start -lt 0 -or $start + $Width -gt $available) {
        throw "$Label is truncated at bit $start."
    }
    $value = [uint32]0
    for ($index = 0; $index -lt $Width; ++$index) {
        $position = $start + $index
        $byteIndex = [int][Math]::Floor($position / 8)
        $bitIndex = [int]($position % 8)
        $bit = ([uint32]$Bytes[$byteIndex] -shr $bitIndex) -band 1
        $value = $value -bor ($bit -shl $index)
    }
    $Cursor.Value = $start + $Width
    return $value
}

function Get-NameCategory {
    param([string]$Name)
    if ($Name -cmatch '\.wav$') { return 'sound-wav' }
    if ($Name -cmatch '\.mdl$') { return 'model-mdl' }
    if ($Name -cmatch '\.bsp$') { return 'map-bsp' }
    if ($Name -cmatch '\.sc$') { return 'event-script' }
    if ($Name -cmatch '\.spr$') { return 'sprite' }
    if ($Name -cmatch '\.wad$') { return 'wad' }
    return 'other-standard'
}

function Convert-CountTable {
    param([Collections.IDictionary]$Table)
    $ordered = [ordered]@{}
    foreach ($key in @($Table.Keys | Sort-Object { [int]$_ })) {
        $ordered[[string]$key] = [int]$Table[$key]
    }
    return [pscustomobject]$ordered
}

function Convert-StringCountTable {
    param([Collections.IDictionary]$Table)
    $ordered = [ordered]@{}
    foreach ($key in @($Table.Keys | Sort-Object)) {
        $ordered[[string]$key] = [int]$Table[$key]
    }
    return [pscustomobject]$ordered
}

function Get-ResourceListProjection {
    param([string]$PayloadPath)
    $bytes = [IO.File]::ReadAllBytes($PayloadPath)
    if ($bytes.Length -lt 11 -or $bytes.Length -gt $maximumServicePayloadBytes) {
        throw 'Resource service payload is outside the verifier bound.'
    }
    if ($bytes[0] -ne 45 -or $bytes[9] -ne 43) {
        throw 'Resource service payload lacks exact opcode45/body/opcode43 framing.'
    }
    if ((Read-U32Le -Bytes $bytes -Offset 5) -ne 0) {
        throw 'Opcode-45 second field is not the required zero value.'
    }

    $cursor = [ref][int64]80
    $count = [int](Read-LsbBits -Bytes $bytes -Cursor $cursor -Width 12 `
        -Label 'resource count')
    if ($count -lt 1 -or $count -gt $maximumResourceCount) {
        throw 'Resource count is outside the bounded standard profile.'
    }

    $types = @{}
    $flags = @{}
    $categories = @{}
    $identity = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    $exactNames = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    $duplicateNames = 0
    $totalNameBytes = 0
    $minimumNameBytes = [int]::MaxValue
    $maximumNameBytes = 0
    $minimumIndex = [int]::MaxValue
    $maximumIndex = 0
    $minimumRawSize = [uint32]::MaxValue
    $maximumRawSize = [uint32]0
    $minimumKnownSize = [uint32]::MaxValue
    $maximumKnownSize = [uint32]0
    $rawSizeTotal = [uint64]0
    $knownSizeTotal = [uint64]0
    $opaqueMaximumSizeCodeCount = 0

    for ($ordinal = 0; $ordinal -lt $count; ++$ordinal) {
        $type = [int](Read-LsbBits -Bytes $bytes -Cursor $cursor -Width 4 `
            -Label "entry $ordinal type")
        $nameBytes = [Collections.Generic.List[byte]]::new()
        while ($true) {
            $value = [byte](Read-LsbBits -Bytes $bytes -Cursor $cursor -Width 8 `
                -Label "entry $ordinal name")
            if ($value -eq 0) { break }
            if ($nameBytes.Count -ge $maximumResourceNameBytes) {
                throw "Entry $ordinal name exceeds the verifier bound."
            }
            $nameBytes.Add($value)
            if ($totalNameBytes + $nameBytes.Count -gt $maximumTotalNameBytes) {
                throw 'Resource names exceed the verifier aggregate bound.'
            }
        }
        if ($nameBytes.Count -lt 1) {
            throw "Entry $ordinal has an empty name outside the observed profile."
        }
        foreach ($value in $nameBytes) {
            if ($value -lt 0x20 -or $value -gt 0x7e) {
                throw "Entry $ordinal name is outside the observed printable-ASCII profile."
            }
        }
        $nameArray = $nameBytes.ToArray()
        $name = [Text.Encoding]::ASCII.GetString($nameArray)
        $index = [int](Read-LsbBits -Bytes $bytes -Cursor $cursor -Width 12 `
            -Label "entry $ordinal index")
        $rawSize = [uint32](Read-LsbBits -Bytes $bytes -Cursor $cursor -Width 24 `
            -Label "entry $ordinal raw size")
        $flagValue = [int](Read-LsbBits -Bytes $bytes -Cursor $cursor -Width 4 `
            -Label "entry $ordinal flags/profile")

        if (@(0, 2, 3, 4, 5) -cnotcontains $type) {
            throw "Entry $ordinal type is outside the captured standard profile."
        }
        if (@(0, 1) -cnotcontains $flagValue) {
            throw "Entry $ordinal flags/profile slot is outside the observed 0/1 set."
        }
        $identityKey = '{0}:{1}' -f $type, $index
        if (-not $identity.Add($identityKey)) {
            throw "Entry $ordinal duplicates the confirmed (type,index) identity."
        }
        if (-not $exactNames.Add($name)) { ++$duplicateNames }

        if (-not $types.ContainsKey($type)) { $types[$type] = 0 }
        ++$types[$type]
        if (-not $flags.ContainsKey($flagValue)) { $flags[$flagValue] = 0 }
        ++$flags[$flagValue]
        $category = Get-NameCategory -Name $name
        if (-not $categories.ContainsKey($category)) { $categories[$category] = 0 }
        ++$categories[$category]

        $totalNameBytes += $nameBytes.Count
        $minimumNameBytes = [Math]::Min($minimumNameBytes, $nameBytes.Count)
        $maximumNameBytes = [Math]::Max($maximumNameBytes, $nameBytes.Count)
        $minimumIndex = [Math]::Min($minimumIndex, $index)
        $maximumIndex = [Math]::Max($maximumIndex, $index)
        if ($rawSize -lt $minimumRawSize) { $minimumRawSize = $rawSize }
        if ($rawSize -gt $maximumRawSize) { $maximumRawSize = $rawSize }
        $rawSizeTotal += [uint64]$rawSize
        if ($rawSize -eq 0x00ffffff) {
            ++$opaqueMaximumSizeCodeCount
        }
        else {
            if ($rawSize -lt $minimumKnownSize) { $minimumKnownSize = $rawSize }
            if ($rawSize -gt $maximumKnownSize) { $maximumKnownSize = $rawSize }
            $knownSizeTotal += [uint64]$rawSize
        }
    }

    $entryEndBit = [int64]$cursor.Value
    $totalBits = [int64]$bytes.Length * 8
    $terminalFillBits = [int]($totalBits - $entryEndBit)
    if ($terminalFillBits -lt 1 -or $terminalFillBits -gt 8) {
        throw 'Resource list does not end with the observed 1..8-bit terminal fill.'
    }
    for ($position = $entryEndBit; $position -lt $totalBits; ++$position) {
        $byteIndex = [int][Math]::Floor($position / 8)
        $bitIndex = [int]($position % 8)
        if ((($bytes[$byteIndex] -shr $bitIndex) -band 1) -ne 0) {
            throw 'Resource list terminal fill contains a nonzero bit.'
        }
    }
    if ($minimumKnownSize -eq [uint32]::MaxValue) {
        $minimumKnownSize = [uint32]0
    }

    $body = [byte[]]::new($bytes.Length - 10)
    [Array]::Copy($bytes, 10, $body, 0, $body.Length)
    return [pscustomobject]@{
        PayloadBytes = $bytes.Length
        Opcode45Ordinal = Read-U32Le -Bytes $bytes -Offset 1
        Count = $count
        EntryEndBit = $entryEndBit
        TerminalFillBits = $terminalFillBits
        BitsConsumedFromOpcode43 = $totalBits - 72
        BytesConsumedFromOpcode43 = $bytes.Length - 9
        TypeCounts = Convert-CountTable -Table $types
        FlagCounts = Convert-CountTable -Table $flags
        CategoryCounts = Convert-StringCountTable -Table $categories
        MinimumNameBytes = $minimumNameBytes
        MaximumNameBytes = $maximumNameBytes
        TotalNameBytes = $totalNameBytes
        MinimumIndex = $minimumIndex
        MaximumIndex = $maximumIndex
        MinimumRawSize = [uint64]$minimumRawSize
        MaximumRawSize = [uint64]$maximumRawSize
        MinimumKnownSize = [uint64]$minimumKnownSize
        MaximumKnownSize = [uint64]$maximumKnownSize
        RawSizeTotal = $rawSizeTotal
        KnownSizeTotal = $knownSizeTotal
        OpaqueMaximumSizeCodeCount = $opaqueMaximumSizeCodeCount
        DuplicateNameCount = $duplicateNames
        BodySha256 = Get-Sha256Hex -Bytes $body
    }
}

function Assert-CaptureContract {
    param([string]$RunDirectory)
    $metadataPath = Resolve-ExactFile -Directory $RunDirectory -Name 'metadata.json' `
        -MaximumBytes $maximumCaptureMetadataBytes -Label 'capture metadata'
    $metadata = Get-Content -Raw -LiteralPath $metadataPath |
        ConvertFrom-Json -ErrorAction Stop
    if ($metadata.schema -cne 'hlclient.stock-movevars-capture-metadata.v1' -or
        $metadata.profile -cne
            'stock-hl-1.1.1.1-to-stock-hlds-protocol48-build10210' -or
        $metadata.completion -cne 'bounded_complete' -or
        $metadata.loopback_only -cne $true -or
        $metadata.byte_preserving_relay -cne $true -or
        $metadata.same_upstream_socket -cne $true -or
        $metadata.exact_server_endpoint_validation -cne $true -or
        $metadata.raw_packet_bytes_stored -cne $false -or
        [int]$metadata.maximum_packets -gt 400 -or
        [int]$metadata.maximum_datagram_bytes -gt 2048 -or
        [int]$metadata.maximum_total_bytes -gt 524288 -or
        [int]$metadata.timeout_seconds -gt 60) {
        throw 'Capture metadata violates the bounded stock-corpus contract.'
    }
}

function Get-ResponseBoundaryProjection {
    param([string]$RunDirectory)
    $candidates = @(Get-ChildItem -LiteralPath $RunDirectory -Force -File |
        Where-Object { $_.Name -cmatch '^research-post-boundary-client-[0-9]{3}\.bin$' } |
        Sort-Object Name)
    if ($candidates.Count -lt 1 -or $candidates.Count -gt 64) {
        throw 'Client reliable-body evidence count is outside the verifier bound.'
    }
    $normal = 0
    $coalescedLengths = [Collections.Generic.List[int]]::new()
    foreach ($candidate in $candidates) {
        Assert-NoReparsePoint -Path $candidate.FullName -Label 'client reliable body'
        Assert-OnlyDefaultDataStream -Path $candidate.FullName -Label 'client reliable body'
        if ($candidate.Length -lt 1 -or $candidate.Length -gt $maximumClientBodyBytes) {
            throw 'Client reliable body is outside the verifier bound.'
        }
        $body = [IO.File]::ReadAllBytes($candidate.FullName)
        if ($body.Length -eq 62 -and $body[0] -eq 1) {
            $fragment = [byte[]]::new(41)
            [Array]::Copy($body, 10, $fragment, 0, 41)
            if ($fragment[0] -ne 5 -or
                (Get-Sha256Hex -Bytes $fragment) -cne
                $expectedResponseFragmentSha256) {
                throw 'Normal response fragment hash changed.'
            }
            ++$normal
        }
    }
    if ($normal -gt 1) {
        throw 'Run contains more than one normal response-fragment carrier.'
    }
    if ($normal -eq 0) {
        foreach ($candidate in $candidates) {
            $body = [IO.File]::ReadAllBytes($candidate.FullName)
            if ($body[0] -eq 1 -and @(64, 66, 68) -ccontains $body.Length) {
                $coalescedLengths.Add($body.Length)
            }
        }
        if ($coalescedLengths.Count -ne 1) {
            throw 'Run lacks one bounded normal or coalesced response boundary.'
        }
    }
    return [pscustomobject]@{
        Normal = $normal -eq 1
        CoalescedLength = if ($coalescedLengths.Count -eq 1) {
            $coalescedLengths[0]
        }
        else { 0 }
    }
}

function Get-ProfileName {
    param([int]$PayloadBytes, [int]$Count)
    if ($PayloadBytes -eq 10713 -and $Count -eq 540) { return 'boot_camp-standard' }
    if ($PayloadBytes -eq 12169 -and $Count -eq 607) { return 'crossfire-standard' }
    if ($PayloadBytes -eq 10815 -and $Count -eq 532) { return 'stalkyard-standard' }
    throw 'Payload geometry does not match an accepted standard profile.'
}

function Get-ObjectFingerprint {
    param([object]$Value)
    $encoding = New-Object Text.UTF8Encoding($false)
    $json = $Value | ConvertTo-Json -Depth 8 -Compress
    return Get-Sha256Hex -Bytes $encoding.GetBytes($json)
}

function Get-MaxPlayersOneDifferentialProjection {
    if (-not (Test-Path -LiteralPath $resourceListRoot -PathType Container)) {
        throw 'Resource-list evidence root is absent.'
    }
    Assert-NoReparsePoint -Path $resourceListRoot -Label 'resource-list evidence root'
    $directories = @(Get-ChildItem -LiteralPath $resourceListRoot -Force -Directory |
        Where-Object { $_.Name -cmatch
            '^m312-maxplayers1-[ab]-baseline-20260823-[0-9]{6}-[0-9]{3}$' } |
        Sort-Object Name)
    if ($directories.Count -ne 2) {
        throw 'Expected exactly two bounded maxplayers=1 differential runs.'
    }

    $variants = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    foreach ($directory in $directories) {
        Assert-NoReparsePoint -Path $directory.FullName `
            -Label 'maxplayers=1 differential run'
        if ($directory.Name -cnotmatch '^m312-maxplayers1-(?<variant>[ab])-') {
            throw 'Maxplayers=1 differential run ID is invalid.'
        }
        [void]$variants.Add($Matches['variant'])

        $metadataPath = Resolve-ExactFile -Directory $directory.FullName `
            -Name 'metadata.json' -MaximumBytes $maximumCaptureMetadataBytes `
            -Label 'maxplayers=1 capture metadata'
        $metadata = Get-Content -Raw -LiteralPath $metadataPath |
            ConvertFrom-Json -ErrorAction Stop
        Assert-ExactProperties -Value $metadata -Allowed @(
            'schema', 'scenario', 'safe_error', 'raw_packet_bytes_stored') `
            -Label 'maxplayers=1 capture metadata'
        Assert-ExactScalarValues -Value $metadata -Expected ([ordered]@{
                schema = 'hlclient.stock-initial-signon-failure.v1'
                scenario = 'Baseline'
                safe_error =
                    "Scenario 'Baseline' did not reach its bounded proof condition."
                raw_packet_bytes_stored = $false
            }) -Label 'maxplayers=1 capture metadata'

        $firstPayloadPath = Resolve-ExactFile -Directory $directory.FullName `
            -Name 'research-service-payload.bin' `
            -MaximumBytes $maximumServicePayloadBytes `
            -Label 'maxplayers=1 first service payload'
        if ((Get-Item -LiteralPath $firstPayloadPath -Force).Length -ne 7395 -or
            (Get-FileSha256Hex -Path $firstPayloadPath) -cne
                $expectedMaxPlayersOneFirstPayloadSha256) {
            throw 'Maxplayers=1 first service payload geometry changed.'
        }
        if (Test-Path -LiteralPath (Join-Path $directory.FullName `
                'research-resource-service-payload.bin')) {
            throw 'Maxplayers=1 differential unexpectedly contains a resource payload.'
        }
        $postBoundary = @(Get-ChildItem -LiteralPath $directory.FullName -Force -File |
            Where-Object { $_.Name -cmatch
                '^research-post-boundary-client-[0-9]{3}\.bin$' })
        if ($postBoundary.Count -ne 0) {
            throw 'Maxplayers=1 differential unexpectedly contains a list response.'
        }
    }
    if ($variants.Count -ne 2 -or -not $variants.Contains('a') -or
        -not $variants.Contains('b')) {
        throw 'Maxplayers=1 differential variants are incomplete.'
    }
    $liveStockProcesses = @(Get-Process -Name 'hl', 'hlds' `
        -ErrorAction SilentlyContinue)
    if ($liveStockProcesses.Count -ne 0) {
        throw 'Stock hl/hlds process remains live during offline projection.'
    }

    return [pscustomobject][ordered]@{
        profile = 'maxplayers-1-bounded-pre-resource-outcome'
        bounded_runs = 2
        source_metadata_schema = 'hlclient.stock-initial-signon-failure.v1'
        first_service_payload_bytes = 7395
        first_service_payload_sha256 = $expectedMaxPlayersOneFirstPayloadSha256
        resource_service_payloads = 0
        post_list_client_bodies = 0
        result = 'no-resource-transition-or-list-observed'
        grammar_and_count = 'not-applicable-list-not-reached'
        parser_failure = $false
        live_hl_hlds_processes_at_projection = $liveStockProcesses.Count
    }
}

function Project-EvidenceSet {
    $maxPlayersOneDifferential = Get-MaxPlayersOneDifferentialProjection
    $sourceRoots = @($movevarsSourceRoot, $transitionSourceRoot)
    foreach ($root in $sourceRoots) {
        if (-not (Test-Path -LiteralPath $root -PathType Container)) {
            throw "Required ignored source root is absent: $root"
        }
        Assert-NoReparsePoint -Path $root -Label 'source root'
    }

    $runs = [Collections.Generic.List[object]]::new()
    foreach ($root in $sourceRoots) {
        foreach ($directory in @(Get-ChildItem -LiteralPath $root -Force -Directory |
                Sort-Object Name)) {
            $payloadCandidate = Join-Path $directory.FullName `
                'research-resource-service-payload.bin'
            if (-not (Test-Path -LiteralPath $payloadCandidate -PathType Leaf)) {
                continue
            }
            if ($directory.Name -cnotmatch
                '^m(?:244|311)-[a-z0-9-]{1,120}-20260822-[0-9]{6}-[0-9]{3}$') {
                throw 'Source run ID is outside the sanitized allowlist profile.'
            }
            Assert-NoReparsePoint -Path $directory.FullName -Label 'source run'
            Assert-CaptureContract -RunDirectory $directory.FullName
            $payloadPath = Resolve-ExactFile -Directory $directory.FullName `
                -Name 'research-resource-service-payload.bin' `
                -MaximumBytes $maximumServicePayloadBytes -Label 'resource service payload'
            $list = Get-ResourceListProjection -PayloadPath $payloadPath
            $response = Get-ResponseBoundaryProjection -RunDirectory $directory.FullName
            $runs.Add([pscustomobject]@{
                RootKind = if ($root -ceq $movevarsSourceRoot) {
                    'movevars-captures'
                }
                else { 'resource-transition-captures' }
                RunId = $directory.Name
                Profile = Get-ProfileName -PayloadBytes $list.PayloadBytes `
                    -Count $list.Count
                List = $list
                Response = $response
            })
        }
    }
    if ($runs.Count -ne $expectedPayloadCount) {
        throw "Expected $expectedPayloadCount parseable payloads, found $($runs.Count)."
    }

    $expectedProfiles = [ordered]@{
        'boot_camp-standard' = [pscustomobject]@{
            Runs = 50; PayloadBytes = 10713; Count = 540; FillBits = 4;
            OpaqueMaximumSizeCodeCount = 0
        }
        'crossfire-standard' = [pscustomobject]@{
            Runs = 2; PayloadBytes = 12169; Count = 607; FillBits = 8;
            OpaqueMaximumSizeCodeCount = 7
        }
        'stalkyard-standard' = [pscustomobject]@{
            Runs = 2; PayloadBytes = 10815; Count = 532; FillBits = 4;
            OpaqueMaximumSizeCodeCount = 0
        }
    }
    $profileProjection = [Collections.Generic.List[object]]::new()
    foreach ($profileName in $expectedProfiles.Keys) {
        $expected = $expectedProfiles[$profileName]
        $members = @($runs | Where-Object { $_.Profile -ceq $profileName })
        if ($members.Count -ne $expected.Runs) {
            throw "Profile '$profileName' run count changed."
        }
        $first = $members[0].List
        if ($first.PayloadBytes -ne $expected.PayloadBytes -or
            $first.Count -ne $expected.Count -or
            $first.TerminalFillBits -ne $expected.FillBits -or
            $first.OpaqueMaximumSizeCodeCount -ne
                $expected.OpaqueMaximumSizeCodeCount) {
            throw "Profile '$profileName' exact geometry changed."
        }
        $stableCandidate = [pscustomobject]@{
            Count = $first.Count
            EntryEndBit = $first.EntryEndBit
            TerminalFillBits = $first.TerminalFillBits
            TypeCounts = $first.TypeCounts
            FlagCounts = $first.FlagCounts
            CategoryCounts = $first.CategoryCounts
            MinimumNameBytes = $first.MinimumNameBytes
            MaximumNameBytes = $first.MaximumNameBytes
            TotalNameBytes = $first.TotalNameBytes
            MinimumIndex = $first.MinimumIndex
            MaximumIndex = $first.MaximumIndex
            MinimumRawSize = $first.MinimumRawSize
            MaximumRawSize = $first.MaximumRawSize
            MinimumKnownSize = $first.MinimumKnownSize
            MaximumKnownSize = $first.MaximumKnownSize
            RawSizeTotal = $first.RawSizeTotal
            KnownSizeTotal = $first.KnownSizeTotal
            OpaqueMaximumSizeCodeCount = $first.OpaqueMaximumSizeCodeCount
            DuplicateNameCount = $first.DuplicateNameCount
            BodySha256 = $first.BodySha256
        }
        $fingerprint = Get-ObjectFingerprint -Value $stableCandidate
        foreach ($member in $members) {
            $candidate = [pscustomobject]@{
                Count = $member.List.Count
                EntryEndBit = $member.List.EntryEndBit
                TerminalFillBits = $member.List.TerminalFillBits
                TypeCounts = $member.List.TypeCounts
                FlagCounts = $member.List.FlagCounts
                CategoryCounts = $member.List.CategoryCounts
                MinimumNameBytes = $member.List.MinimumNameBytes
                MaximumNameBytes = $member.List.MaximumNameBytes
                TotalNameBytes = $member.List.TotalNameBytes
                MinimumIndex = $member.List.MinimumIndex
                MaximumIndex = $member.List.MaximumIndex
                MinimumRawSize = $member.List.MinimumRawSize
                MaximumRawSize = $member.List.MaximumRawSize
                MinimumKnownSize = $member.List.MinimumKnownSize
                MaximumKnownSize = $member.List.MaximumKnownSize
                RawSizeTotal = $member.List.RawSizeTotal
                KnownSizeTotal = $member.List.KnownSizeTotal
                OpaqueMaximumSizeCodeCount =
                    $member.List.OpaqueMaximumSizeCodeCount
                DuplicateNameCount = $member.List.DuplicateNameCount
                BodySha256 = $member.List.BodySha256
            }
            if ((Get-ObjectFingerprint -Value $candidate) -cne $fingerprint) {
                throw "Profile '$profileName' is not structurally stable."
            }
        }
        $profileProjection.Add([pscustomobject][ordered]@{
            name = $profileName
            accepted_runs = $members.Count
            payload_bytes = $first.PayloadBytes
            resource_count = $first.Count
            entry_end_absolute_bit = $first.EntryEndBit
            terminal_zero_fill_bits = $first.TerminalFillBits
            opcode43_bits_consumed = $first.BitsConsumedFromOpcode43
            opcode43_bytes_covered = $first.BytesConsumedFromOpcode43
            ordered_body_sha256 = $first.BodySha256
            ordered_structure_sha256 = $fingerprint
            type_counts = $first.TypeCounts
            filename_category_counts = $first.CategoryCounts
            flags_profile_counts = $first.FlagCounts
            name_length_min = $first.MinimumNameBytes
            name_length_max = $first.MaximumNameBytes
            total_name_bytes = $first.TotalNameBytes
            index_min = $first.MinimumIndex
            index_max = $first.MaximumIndex
            raw_size_code_min = $first.MinimumRawSize
            raw_size_code_max = $first.MaximumRawSize
            known_size_min = $first.MinimumKnownSize
            known_size_max = $first.MaximumKnownSize
            raw_size_code_total = $first.RawSizeTotal
            known_size_total = $first.KnownSizeTotal
            opaque_0xffffff_count = $first.OpaqueMaximumSizeCodeCount
            duplicate_type_index_count = 0
            duplicate_exact_name_count = $first.DuplicateNameCount
        })
    }

    $normalResponses = @($runs | Where-Object { $_.Response.Normal }).Count
    $coalesced = @($runs | Where-Object { -not $_.Response.Normal })
    if ($normalResponses -ne $expectedNormalResponseCount -or
        $coalesced.Count -ne $expectedCoalescedResponseCount) {
        throw 'Client response-boundary evidence counts changed.'
    }
    $coalescedLengths = @($coalesced | ForEach-Object {
            [int]$_.Response.CoalescedLength } | Sort-Object)
    if (($coalescedLengths -join ',') -cne '64,66,68') {
        throw 'Coalesced response-boundary lengths changed.'
    }

    $customHeaderPath = Join-Path $repositoryRoot 'third_party/halflife-sdk/engine/custom.h'
    if (-not (Test-Path -LiteralPath $customHeaderPath -PathType Leaf) -or
        (Get-Item -LiteralPath $customHeaderPath -Force).Length -ne 3724) {
        throw 'Pinned public Valve resource header is absent or changed.'
    }
    Assert-NoReparsePoint -Path $customHeaderPath `
        -Label 'pinned public Valve resource header'
    if ((Get-FileSha256Hex -Path $customHeaderPath) -cne
        $expectedValveCustomHeaderSha256) {
        throw 'Pinned public Valve resource header hash changed.'
    }
    $customHeader = Get-Content -Raw -LiteralPath $customHeaderPath
    if ($customHeader -notmatch 't_sound\s*=\s*0' -or
        $customHeader -notmatch 't_eventscript' -or
        $customHeader -notmatch 't_world' -or
        $customHeader -notmatch 'MAX_QPATH\s+64' -or
        $customHeader -notmatch 'rgucMD5_hash\s*\[\s*16\s*\]' -or
        $customHeader -notmatch 'COM_SizeofResourceList') {
        throw 'Pinned public Valve resource-header cross-check failed.'
    }

    return [pscustomobject][ordered]@{
        schema = 'hlclient.stock-resource-list-evidence.v1'
        profile = 'stock-hl-1.1.1.1-to-stock-hlds-protocol48-build10210'
        verifier_normalized_sha256 = Get-NormalizedVerifierSha256
        methodology = [pscustomobject][ordered]@{
            mode = 'offline-existing-ignored-artifacts'
            tracked_sanitized_projection = $true
            ignored_raw_sources = $true
            transport = 'private-ipv4-loopback-udp'
            byte_preserving_relay = $true
            one_upstream_socket = $true
            exact_endpoint_validation = $true
            packet_byte_time_bounds = $true
            stock_processes_started_by_verifier = $false
            source_payload_count = $runs.Count
            no_opcode_scanning = $true
        }
        research_root_policy = [pscustomobject][ordered]@{
            accepted_capture_mode = 'user-supplied-isolated-copy-only'
            primary_steam_install = 'rejected-no-override'
            explicit_preflight_parameter = 'ResearchHalfLifeRoot'
            isolation_marker = $isolationMarkerName
            mutable_state_minimum = @(
                'config.cfg', 'userconfig.cfg', 'autoexec.cfg', 'custom.hpk',
                'server.cfg', 'listenserver.cfg', 'server-cfg-and-log-files')
            external_file_drift = 'none'
            drift_basis = 'offline-verifier-launched-no-process'
        }
        public_header_crosscheck = [pscustomobject][ordered]@{
            source = 'third_party/halflife-sdk/engine/custom.h'
            pinned_sdk_commit = 'b1b5cf5892918535619b2937bb927e46cb097ba1'
            header_sha256 = $expectedValveCustomHeaderSha256
            type_categories_present = $true
            max_qpath_64_present = $true
            resource_struct_present = $true
            digest_array_16_present = $true
            resource_list_size_declaration_present = $true
            numeric_opcode43_constant_present = $false
            wire_layout_used_from_struct = $false
        }
        semantic_gate = [pscustomobject][ordered]@{
            opcode = 43
            semantic_name = 'ResourceListMessage'
            standard_profile = 'completed'
            custom_profile = 'unsupported-pending'
            evidence = '54-exact-parses-three-coherent-map-profiles'
        }
        corpus_notes = [pscustomobject][ordered]@{
            historical_run_label_mismatch =
                'm244-sky-night-run-ids-capture-config-identifies-stalkyard'
            accepted_map_profiles = @('boot_camp', 'crossfire', 'stalkyard')
        }
        differential_outcomes = [pscustomobject][ordered]@{
            maxplayers1 = $maxPlayersOneDifferential
            maxplayers8 = [pscustomobject][ordered]@{
                full_list_runs_minimum = 2
                result = 'standard-resource-list-observed'
                grammar_gate_effect = 'supports-standard-multiplayer-profile'
            }
        }
        wire_grammar = [pscustomobject][ordered]@{
            bit_order = 'least-significant-bit-first'
            opcode43_absolute_byte_offset = 9
            body_absolute_bit_offset = 80
            count_width_bits = 12
            entry_type_slot_bits = 4
            name_encoding = 'nul-terminated-8-bit-units-at-current-bit-cursor'
            index_width_bits = 12
            raw_size_code_width_bits = 24
            flags_profile_slot_bits = 4
            per_entry_alignment = 'none-entry-starts-alternate-bit0-bit4'
            terminal_fill = '1-to-8-zero-bits-exact-end-of-payload'
            exact_post_list_condition = 'end-of-payload'
        }
        field_evidence = @(
            [pscustomobject][ordered]@{ field = 'count'; width_bits = 12; confidence = 'confirmed'; public = $true },
            [pscustomobject][ordered]@{ field = 'type_slot'; width_bits = 4; confidence = 'confirmed-slot-known-values'; public = $true },
            [pscustomobject][ordered]@{ field = 'name'; width_bits = 8; confidence = 'confirmed'; public = $true },
            [pscustomobject][ordered]@{ field = 'index'; width_bits = 12; confidence = 'confirmed'; public = $true },
            [pscustomobject][ordered]@{ field = 'raw_size_code'; width_bits = 24; confidence = 'confirmed-width-semantics-bounded'; public = $true },
            [pscustomobject][ordered]@{ field = 'flags_profile_slot'; width_bits = 4; confidence = 'confirmed-slot-bit-semantics-opaque'; public = $true },
            [pscustomobject][ordered]@{ field = 'optional_custom_metadata'; width_bits = 0; confidence = 'pending-unobserved'; public = $false },
            [pscustomobject][ordered]@{ field = 'terminal_zero_fill'; width_bits = 0; confidence = 'confirmed-values-padding-vs-marker-strongly-inferred'; public = $true }
        )
        resource_types = @(
            [pscustomobject][ordered]@{ value = 0; name = 'sound'; confidence = 'confirmed'; observed = $true },
            [pscustomobject][ordered]@{ value = 1; name = 'skin'; confidence = 'public-header-unobserved'; observed = $false },
            [pscustomobject][ordered]@{ value = 2; name = 'model'; confidence = 'confirmed-includes-map-model-profile'; observed = $true },
            [pscustomobject][ordered]@{ value = 3; name = 'decal'; confidence = 'confirmed'; observed = $true },
            [pscustomobject][ordered]@{ value = 4; name = 'generic'; confidence = 'confirmed'; observed = $true },
            [pscustomobject][ordered]@{ value = 5; name = 'event_script'; confidence = 'confirmed'; observed = $true },
            [pscustomobject][ordered]@{ value = 6; name = 'world'; confidence = 'public-header-unobserved'; observed = $false }
        )
        standard_profiles = @($profileProjection)
        client_response_boundary = [pscustomobject][ordered]@{
            action_kind = 'neutral-required-resource-response'
            semantic_name = 'pending-no-official-opcode-mapping'
            normal_runs = $normalResponses
            coalesced_runs = $coalesced.Count
            normal_reliable_body_bytes = 62
            normal_fragment_descriptor_bytes = 10
            normal_semantic_fragment_bytes = 41
            normal_semantic_candidate_opcode = 5
            normal_semantic_fragment_sha256 = $expectedResponseFragmentSha256
            contemporaneous_tail_bytes = 11
            coalesced_reliable_body_bytes = $coalescedLengths
            builder_generated = $false
        }
        aggregate = [pscustomobject][ordered]@{
            parseable_payloads = $runs.Count
            boot_camp_runs = @($runs | Where-Object Profile -ceq 'boot_camp-standard').Count
            crossfire_runs = @($runs | Where-Object Profile -ceq 'crossfire-standard').Count
            stalkyard_runs = @($runs | Where-Object Profile -ceq 'stalkyard-standard').Count
            resource_count_min = 532
            resource_count_max = 607
            observed_type_values = @(0, 2, 3, 4, 5)
            observed_flags_profile_values = @(0, 1)
            custom_entries_observed = 0
            maxplayers1_runs = 2
            maxplayers1_result = 'no-resource-transition-or-list-observed'
            maxplayers8_full_list_runs_minimum = 2
            raw_names_projected = $false
            digest_bytes_projected = $false
            payload_bytes_projected = $false
        }
    }
}

function Assert-ProjectionMetadata {
    param([object]$Metadata)
    Assert-ExactProperties -Value $Metadata -Allowed @(
        'schema', 'profile', 'verifier_normalized_sha256', 'methodology',
        'research_root_policy', 'public_header_crosscheck', 'semantic_gate',
        'corpus_notes', 'differential_outcomes', 'wire_grammar',
        'field_evidence', 'resource_types', 'standard_profiles',
        'client_response_boundary', 'aggregate') -Label 'projection'
    Assert-ExactScalarValues -Value $Metadata -Expected ([ordered]@{
            schema = 'hlclient.stock-resource-list-evidence.v1'
            profile = 'stock-hl-1.1.1.1-to-stock-hlds-protocol48-build10210'
            verifier_normalized_sha256 = Get-NormalizedVerifierSha256
        }) -Label 'projection identity'

    Assert-ExactProperties -Value $Metadata.methodology -Allowed @(
        'mode', 'tracked_sanitized_projection', 'ignored_raw_sources',
        'transport', 'byte_preserving_relay', 'one_upstream_socket',
        'exact_endpoint_validation', 'packet_byte_time_bounds',
        'stock_processes_started_by_verifier', 'source_payload_count',
        'no_opcode_scanning') -Label 'methodology'
    Assert-ExactScalarValues -Value $Metadata.methodology -Expected ([ordered]@{
            mode = 'offline-existing-ignored-artifacts'
            tracked_sanitized_projection = $true
            ignored_raw_sources = $true
            transport = 'private-ipv4-loopback-udp'
            byte_preserving_relay = $true
            one_upstream_socket = $true
            exact_endpoint_validation = $true
            packet_byte_time_bounds = $true
            stock_processes_started_by_verifier = $false
            source_payload_count = 54
            no_opcode_scanning = $true
        }) -Label 'methodology'

    Assert-ExactProperties -Value $Metadata.research_root_policy -Allowed @(
        'accepted_capture_mode', 'primary_steam_install',
        'explicit_preflight_parameter', 'isolation_marker',
        'mutable_state_minimum', 'external_file_drift', 'drift_basis') `
        -Label 'research-root policy'
    Assert-ExactScalarValues -Value $Metadata.research_root_policy `
        -Expected ([ordered]@{
            accepted_capture_mode = 'user-supplied-isolated-copy-only'
            primary_steam_install = 'rejected-no-override'
            explicit_preflight_parameter = 'ResearchHalfLifeRoot'
            isolation_marker = '.hlclient-research-isolated'
            external_file_drift = 'none'
            drift_basis = 'offline-verifier-launched-no-process'
        }) -Label 'research-root policy'
    if ((@($Metadata.research_root_policy.mutable_state_minimum) -join ',') -cne
        'config.cfg,userconfig.cfg,autoexec.cfg,custom.hpk,server.cfg,' +
        'listenserver.cfg,server-cfg-and-log-files') {
        throw 'Research-root mutable-state inventory is invalid.'
    }

    Assert-ExactProperties -Value $Metadata.public_header_crosscheck -Allowed @(
        'source', 'pinned_sdk_commit', 'header_sha256',
        'type_categories_present', 'max_qpath_64_present',
        'resource_struct_present', 'digest_array_16_present',
        'resource_list_size_declaration_present',
        'numeric_opcode43_constant_present', 'wire_layout_used_from_struct') `
        -Label 'public-header cross-check'
    Assert-ExactScalarValues -Value $Metadata.public_header_crosscheck `
        -Expected ([ordered]@{
            source = 'third_party/halflife-sdk/engine/custom.h'
            pinned_sdk_commit = 'b1b5cf5892918535619b2937bb927e46cb097ba1'
            header_sha256 = $expectedValveCustomHeaderSha256
            type_categories_present = $true
            max_qpath_64_present = $true
            resource_struct_present = $true
            digest_array_16_present = $true
            resource_list_size_declaration_present = $true
            numeric_opcode43_constant_present = $false
            wire_layout_used_from_struct = $false
        }) -Label 'public-header cross-check'

    Assert-ExactProperties -Value $Metadata.semantic_gate -Allowed @(
        'opcode', 'semantic_name', 'standard_profile', 'custom_profile',
        'evidence') -Label 'semantic gate'
    Assert-ExactScalarValues -Value $Metadata.semantic_gate -Expected ([ordered]@{
            opcode = 43
            semantic_name = 'ResourceListMessage'
            standard_profile = 'completed'
            custom_profile = 'unsupported-pending'
            evidence = '54-exact-parses-three-coherent-map-profiles'
        }) -Label 'semantic gate'

    Assert-ExactProperties -Value $Metadata.corpus_notes -Allowed @(
        'historical_run_label_mismatch', 'accepted_map_profiles') `
        -Label 'corpus notes'
    Assert-ExactScalarValues -Value $Metadata.corpus_notes -Expected ([ordered]@{
            historical_run_label_mismatch =
                'm244-sky-night-run-ids-capture-config-identifies-stalkyard'
        }) -Label 'corpus notes'
    if ((@($Metadata.corpus_notes.accepted_map_profiles) -join ',') -cne
        'boot_camp,crossfire,stalkyard') {
        throw 'Projection accepted map profiles are invalid.'
    }

    Assert-ExactProperties -Value $Metadata.differential_outcomes -Allowed @(
        'maxplayers1', 'maxplayers8') -Label 'differential outcomes'
    Assert-ExactProperties -Value $Metadata.differential_outcomes.maxplayers1 `
        -Allowed @(
            'profile', 'bounded_runs', 'source_metadata_schema',
            'first_service_payload_bytes', 'first_service_payload_sha256',
            'resource_service_payloads', 'post_list_client_bodies', 'result',
            'grammar_and_count', 'parser_failure',
            'live_hl_hlds_processes_at_projection') `
        -Label 'maxplayers=1 differential'
    Assert-ExactScalarValues `
        -Value $Metadata.differential_outcomes.maxplayers1 `
        -Expected ([ordered]@{
            profile = 'maxplayers-1-bounded-pre-resource-outcome'
            bounded_runs = 2
            source_metadata_schema = 'hlclient.stock-initial-signon-failure.v1'
            first_service_payload_bytes = 7395
            first_service_payload_sha256 =
                $expectedMaxPlayersOneFirstPayloadSha256
            resource_service_payloads = 0
            post_list_client_bodies = 0
            result = 'no-resource-transition-or-list-observed'
            grammar_and_count = 'not-applicable-list-not-reached'
            parser_failure = $false
            live_hl_hlds_processes_at_projection = 0
        }) -Label 'maxplayers=1 differential'
    Assert-ExactProperties -Value $Metadata.differential_outcomes.maxplayers8 `
        -Allowed @(
            'full_list_runs_minimum', 'result', 'grammar_gate_effect') `
        -Label 'maxplayers=8 differential'
    Assert-ExactScalarValues `
        -Value $Metadata.differential_outcomes.maxplayers8 `
        -Expected ([ordered]@{
            full_list_runs_minimum = 2
            result = 'standard-resource-list-observed'
            grammar_gate_effect = 'supports-standard-multiplayer-profile'
        }) -Label 'maxplayers=8 differential'

    Assert-ExactProperties -Value $Metadata.wire_grammar -Allowed @(
        'bit_order', 'opcode43_absolute_byte_offset',
        'body_absolute_bit_offset', 'count_width_bits',
        'entry_type_slot_bits', 'name_encoding', 'index_width_bits',
        'raw_size_code_width_bits', 'flags_profile_slot_bits',
        'per_entry_alignment', 'terminal_fill',
        'exact_post_list_condition') -Label 'wire grammar'
    Assert-ExactScalarValues -Value $Metadata.wire_grammar -Expected ([ordered]@{
            bit_order = 'least-significant-bit-first'
            opcode43_absolute_byte_offset = 9
            body_absolute_bit_offset = 80
            count_width_bits = 12
            entry_type_slot_bits = 4
            name_encoding = 'nul-terminated-8-bit-units-at-current-bit-cursor'
            index_width_bits = 12
            raw_size_code_width_bits = 24
            flags_profile_slot_bits = 4
            per_entry_alignment = 'none-entry-starts-alternate-bit0-bit4'
            terminal_fill = '1-to-8-zero-bits-exact-end-of-payload'
            exact_post_list_condition = 'end-of-payload'
        }) -Label 'wire grammar'

    $fieldEvidence = @($Metadata.field_evidence)
    $expectedFieldEvidence = @(
        [ordered]@{ field = 'count'; width_bits = 12; confidence = 'confirmed'; public = $true },
        [ordered]@{ field = 'type_slot'; width_bits = 4; confidence = 'confirmed-slot-known-values'; public = $true },
        [ordered]@{ field = 'name'; width_bits = 8; confidence = 'confirmed'; public = $true },
        [ordered]@{ field = 'index'; width_bits = 12; confidence = 'confirmed'; public = $true },
        [ordered]@{ field = 'raw_size_code'; width_bits = 24; confidence = 'confirmed-width-semantics-bounded'; public = $true },
        [ordered]@{ field = 'flags_profile_slot'; width_bits = 4; confidence = 'confirmed-slot-bit-semantics-opaque'; public = $true },
        [ordered]@{ field = 'optional_custom_metadata'; width_bits = 0; confidence = 'pending-unobserved'; public = $false },
        [ordered]@{ field = 'terminal_zero_fill'; width_bits = 0; confidence = 'confirmed-values-padding-vs-marker-strongly-inferred'; public = $true })
    if ($fieldEvidence.Count -ne $expectedFieldEvidence.Count) {
        throw 'Projection field-evidence count is invalid.'
    }
    for ($index = 0; $index -lt $fieldEvidence.Count; ++$index) {
        Assert-ExactProperties -Value $fieldEvidence[$index] -Allowed @(
            'field', 'width_bits', 'confidence', 'public') `
            -Label "field evidence $index"
        Assert-ExactScalarValues -Value $fieldEvidence[$index] `
            -Expected $expectedFieldEvidence[$index] -Label "field evidence $index"
    }

    $resourceTypes = @($Metadata.resource_types)
    $expectedResourceTypes = @(
        [ordered]@{ value = 0; name = 'sound'; confidence = 'confirmed'; observed = $true },
        [ordered]@{ value = 1; name = 'skin'; confidence = 'public-header-unobserved'; observed = $false },
        [ordered]@{ value = 2; name = 'model'; confidence = 'confirmed-includes-map-model-profile'; observed = $true },
        [ordered]@{ value = 3; name = 'decal'; confidence = 'confirmed'; observed = $true },
        [ordered]@{ value = 4; name = 'generic'; confidence = 'confirmed'; observed = $true },
        [ordered]@{ value = 5; name = 'event_script'; confidence = 'confirmed'; observed = $true },
        [ordered]@{ value = 6; name = 'world'; confidence = 'public-header-unobserved'; observed = $false })
    if ($resourceTypes.Count -ne $expectedResourceTypes.Count) {
        throw 'Projection resource-type count is invalid.'
    }
    for ($index = 0; $index -lt $resourceTypes.Count; ++$index) {
        Assert-ExactProperties -Value $resourceTypes[$index] -Allowed @(
            'value', 'name', 'confidence', 'observed') `
            -Label "resource type $index"
        Assert-ExactScalarValues -Value $resourceTypes[$index] `
            -Expected $expectedResourceTypes[$index] -Label "resource type $index"
    }

    $profiles = @($Metadata.standard_profiles)
    if ($profiles.Count -ne 3) { throw 'Projection profile count is invalid.' }
    $expectedProfiles = [ordered]@{
        'boot_camp-standard' = [pscustomobject]@{
            Scalars = [ordered]@{
                accepted_runs = 50; payload_bytes = 10713; resource_count = 540
                entry_end_absolute_bit = 85700; terminal_zero_fill_bits = 4
                opcode43_bits_consumed = 85632; opcode43_bytes_covered = 10704
                ordered_body_sha256 = 'BA4318498B7A49FFCE648611538975B075EE9320B88D7A2EB51F450499997197'
                ordered_structure_sha256 = 'DFA7FC53207805F55073B37AFFB6D5EBF36F931E865C6688B6BC04DD1F5B2D21'
                name_length_min = 2; name_length_max = 26
                total_name_bytes = 7191; index_min = 0; index_max = 221
                raw_size_code_min = 0; raw_size_code_max = 2705704
                known_size_min = 0; known_size_max = 2705704
                raw_size_code_total = 8557630; known_size_total = 8557630
                opaque_0xffffff_count = 0; duplicate_type_index_count = 0
                duplicate_exact_name_count = 0
            }
            Types = [ordered]@{ '0' = 171; '2' = 129; '3' = 222; '5' = 18 }
            Categories = [ordered]@{
                'event-script' = 18; 'map-bsp' = 1; 'model-mdl' = 69
                'other-standard' = 265; 'sound-wav' = 171; sprite = 16
            }
            Flags = [ordered]@{ '0' = 393; '1' = 147 }
        }
        'crossfire-standard' = [pscustomobject]@{
            Scalars = [ordered]@{
                accepted_runs = 2; payload_bytes = 12169; resource_count = 607
                entry_end_absolute_bit = 97344; terminal_zero_fill_bits = 8
                opcode43_bits_consumed = 97280; opcode43_bytes_covered = 12160
                ordered_body_sha256 = 'D434E105020E7A214B67DA731620D09644697756DFE6C05D6C17743FF37F7456'
                ordered_structure_sha256 = '6A540D80E66DF5A440ACF15C3B73413A3FA1EFDDAEBC28523E0E864C4D794779'
                name_length_min = 2; name_length_max = 37
                total_name_bytes = 8211; index_min = 0; index_max = 221
                raw_size_code_min = 0; raw_size_code_max = 16777215
                known_size_min = 0; known_size_max = 1241704
                raw_size_code_total = 129258167; known_size_total = 11817662
                opaque_0xffffff_count = 7; duplicate_type_index_count = 0
                duplicate_exact_name_count = 0
            }
            Types = [ordered]@{
                '0' = 184; '2' = 158; '3' = 222; '4' = 25; '5' = 18
            }
            Categories = [ordered]@{
                'event-script' = 18; 'map-bsp' = 1; 'model-mdl' = 80
                'other-standard' = 307; 'sound-wav' = 184; sprite = 17
            }
            Flags = [ordered]@{ '0' = 406; '1' = 201 }
        }
        'stalkyard-standard' = [pscustomobject]@{
            Scalars = [ordered]@{
                accepted_runs = 2; payload_bytes = 10815; resource_count = 532
                entry_end_absolute_bit = 86516; terminal_zero_fill_bits = 4
                opcode43_bits_consumed = 86448; opcode43_bytes_covered = 10806
                ordered_body_sha256 = 'CE74503D20B9A49AA5F3243BFA05EBDA25C4421F25BD45F0FA2EDB39FB90473B'
                ordered_structure_sha256 = 'A4AF9531606E0D5FD908164EA132F4E645E0CEFB6D67678519B83E6FFA975806'
                name_length_min = 2; name_length_max = 26
                total_name_bytes = 7345; index_min = 0; index_max = 221
                raw_size_code_min = 0; raw_size_code_max = 700408
                known_size_min = 0; known_size_max = 700408
                raw_size_code_total = 6718544; known_size_total = 6718544
                opaque_0xffffff_count = 0; duplicate_type_index_count = 0
                duplicate_exact_name_count = 0
            }
            Types = [ordered]@{ '0' = 180; '2' = 112; '3' = 222; '5' = 18 }
            Categories = [ordered]@{
                'event-script' = 18; 'map-bsp' = 1; 'model-mdl' = 69
                'other-standard' = 246; 'sound-wav' = 180; sprite = 18
            }
            Flags = [ordered]@{ '0' = 402; '1' = 130 }
        }
    }
    $profileProperties = @(
        'name', 'accepted_runs', 'payload_bytes', 'resource_count',
        'entry_end_absolute_bit', 'terminal_zero_fill_bits',
        'opcode43_bits_consumed', 'opcode43_bytes_covered',
        'ordered_body_sha256', 'ordered_structure_sha256', 'type_counts',
        'filename_category_counts', 'flags_profile_counts', 'name_length_min',
        'name_length_max', 'total_name_bytes', 'index_min', 'index_max',
        'raw_size_code_min', 'raw_size_code_max', 'known_size_min',
        'known_size_max', 'raw_size_code_total', 'known_size_total',
        'opaque_0xffffff_count', 'duplicate_type_index_count',
        'duplicate_exact_name_count')
    foreach ($profile in $profiles) {
        $profileName = [string]$profile.name
        if (-not $expectedProfiles.Contains($profileName)) {
            throw 'Projection contains an unknown standard profile.'
        }
        Assert-ExactProperties -Value $profile -Allowed $profileProperties `
            -Label "standard profile $profileName"
        $expectedProfile = $expectedProfiles[$profileName]
        Assert-ExactScalarValues -Value $profile -Expected $expectedProfile.Scalars `
            -Label "standard profile $profileName"
        Assert-ExactCountTable -Value $profile.type_counts `
            -Expected $expectedProfile.Types -Label "$profileName type counts"
        Assert-ExactCountTable -Value $profile.filename_category_counts `
            -Expected $expectedProfile.Categories `
            -Label "$profileName filename-category counts"
        Assert-ExactCountTable -Value $profile.flags_profile_counts `
            -Expected $expectedProfile.Flags -Label "$profileName flags counts"
    }

    Assert-ExactProperties -Value $Metadata.client_response_boundary -Allowed @(
        'action_kind', 'semantic_name', 'normal_runs', 'coalesced_runs',
        'normal_reliable_body_bytes', 'normal_fragment_descriptor_bytes',
        'normal_semantic_fragment_bytes', 'normal_semantic_candidate_opcode',
        'normal_semantic_fragment_sha256', 'contemporaneous_tail_bytes',
        'coalesced_reliable_body_bytes', 'builder_generated') `
        -Label 'client-response boundary'
    Assert-ExactScalarValues -Value $Metadata.client_response_boundary `
        -Expected ([ordered]@{
            action_kind = 'neutral-required-resource-response'
            semantic_name = 'pending-no-official-opcode-mapping'
            normal_runs = 51; coalesced_runs = 3
            normal_reliable_body_bytes = 62
            normal_fragment_descriptor_bytes = 10
            normal_semantic_fragment_bytes = 41
            normal_semantic_candidate_opcode = 5
            normal_semantic_fragment_sha256 = $expectedResponseFragmentSha256
            contemporaneous_tail_bytes = 11; builder_generated = $false
        }) -Label 'client-response boundary'
    if ((@($Metadata.client_response_boundary.coalesced_reliable_body_bytes) `
            -join ',') -cne '64,66,68') {
        throw 'Projection coalesced response-boundary lengths are invalid.'
    }

    Assert-ExactProperties -Value $Metadata.aggregate -Allowed @(
        'parseable_payloads', 'boot_camp_runs', 'crossfire_runs',
        'stalkyard_runs', 'resource_count_min', 'resource_count_max',
        'observed_type_values', 'observed_flags_profile_values',
        'custom_entries_observed', 'maxplayers1_runs', 'maxplayers1_result',
        'maxplayers8_full_list_runs_minimum', 'raw_names_projected',
        'digest_bytes_projected', 'payload_bytes_projected') -Label 'aggregate'
    Assert-ExactScalarValues -Value $Metadata.aggregate -Expected ([ordered]@{
            parseable_payloads = 54; boot_camp_runs = 50; crossfire_runs = 2
            stalkyard_runs = 2; resource_count_min = 532
            resource_count_max = 607; custom_entries_observed = 0
            maxplayers1_runs = 2
            maxplayers1_result = 'no-resource-transition-or-list-observed'
            maxplayers8_full_list_runs_minimum = 2
            raw_names_projected = $false; digest_bytes_projected = $false
            payload_bytes_projected = $false
        }) -Label 'aggregate'
    if ((@($Metadata.aggregate.observed_type_values) -join ',') -cne
            '0,2,3,4,5' -or
        (@($Metadata.aggregate.observed_flags_profile_values) -join ',') -cne
            '0,1') {
        throw 'Projection aggregate observed-value sets are invalid.'
    }
}

function Get-KnownSteamRoots {
    $roots = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($registryPath in @(
            'HKCU:\Software\Valve\Steam',
            'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam',
            'HKLM:\SOFTWARE\Valve\Steam')) {
        try {
            $value = Get-ItemProperty -LiteralPath $registryPath -ErrorAction Stop
            foreach ($property in @('SteamPath', 'InstallPath')) {
                if ($null -ne $value.$property -and
                    -not [string]::IsNullOrWhiteSpace([string]$value.$property)) {
                    [void]$roots.Add([IO.Path]::GetFullPath([string]$value.$property))
                }
            }
        }
        catch {
            # Absence is allowed; path-shape rejection below remains mandatory.
        }
    }
    foreach ($candidate in @(
            $(if (${env:ProgramFiles(x86)}) {
                Join-Path ${env:ProgramFiles(x86)} 'Steam'
            }),
            $(if ($env:ProgramFiles) { Join-Path $env:ProgramFiles 'Steam' }))) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Container)) {
            [void]$roots.Add([IO.Path]::GetFullPath($candidate))
        }
    }
    $steamRoots = @($roots)
    foreach ($steamRoot in $steamRoots) {
        $libraryFile = Join-Path $steamRoot 'steamapps/libraryfolders.vdf'
        if (-not (Test-Path -LiteralPath $libraryFile -PathType Leaf)) { continue }
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
        catch {
            throw 'Unable to establish the configured Steam-library set.'
        }
    }
    return @($roots)
}

function Assert-IsolatedResearchRoot {
    param([string]$Path)
    $inputPath = [IO.Path]::GetFullPath($Path)
    Assert-NoReparsePointInExistingPath -Path $inputPath -Label 'ResearchHalfLifeRoot'
    $resolved = Resolve-Path -LiteralPath $inputPath -ErrorAction Stop
    if ($resolved.Provider.Name -cne 'FileSystem') {
        throw 'ResearchHalfLifeRoot must be a filesystem directory.'
    }
    $root = [IO.Path]::GetFullPath($resolved.Path).TrimEnd('\', '/')
    if (-not (Test-Path -LiteralPath $root -PathType Container)) {
        throw 'ResearchHalfLifeRoot must be a directory.'
    }
    Assert-NoReparsePoint -Path $root -Label 'ResearchHalfLifeRoot'
    if ($root -match '(?i)(?:^|[\\/])steamapps(?:[\\/]|$)') {
        throw 'Primary or managed Steam-library roots are never accepted for research.'
    }
    foreach ($steamRoot in @(Get-KnownSteamRoots)) {
        $normalizedSteam = [IO.Path]::GetFullPath($steamRoot).TrimEnd('\', '/')
        if ($root.Equals($normalizedSteam, [StringComparison]::OrdinalIgnoreCase) -or
            $root.StartsWith(
                $normalizedSteam + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw 'ResearchHalfLifeRoot resolves inside a configured Steam library.'
        }
    }
    $marker = Join-Path $root $isolationMarkerName
    if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) {
        throw "Isolated research copy lacks required marker $isolationMarkerName."
    }
    Assert-NoReparsePoint -Path $marker -Label 'isolation marker'
    Assert-OnlyDefaultDataStream -Path $marker -Label 'isolation marker'
    if ((Get-Item -LiteralPath $marker -Force).Length -gt 128) {
        throw 'Isolated research marker is outside its byte bound.'
    }
    if ((Get-Content -Raw -LiteralPath $marker).Trim() -cne $isolationMarkerText) {
        throw 'Isolated research marker content is invalid.'
    }
    foreach ($relative in @('hl.exe', 'hlds.exe', 'valve')) {
        $candidate = [IO.Path]::GetFullPath((Join-Path $root $relative))
        Assert-PathBelowRoot -Path $candidate -Root $root -Label $relative
        $requiredPathType = if ($relative -ceq 'valve') { 'Container' } else { 'Leaf' }
        if (-not (Test-Path -LiteralPath $candidate -PathType $requiredPathType)) {
            throw "Isolated research copy is missing $relative."
        }
        Assert-NoReparsePoint -Path $candidate -Label $relative
        if ($requiredPathType -ceq 'Leaf') {
            Assert-OnlyDefaultDataStream -Path $candidate -Label $relative
        }
    }
    $hl = Get-Item -LiteralPath (Join-Path $root 'hl.exe') -Force
    $hlds = Get-Item -LiteralPath (Join-Path $root 'hlds.exe') -Force
    if ($hl.VersionInfo.FileVersion -cne '1, 1, 1, 1' -or
        $hlds.VersionInfo.FileVersion -cne '4, 1, 1, 1') {
        throw 'Isolated research binaries do not match the accepted stock versions.'
    }
    foreach ($binary in @($hl.FullName, $hlds.FullName)) {
        $signature = Get-AuthenticodeSignature -LiteralPath $binary
        if ($signature.Status -ne 'Valid' -or
            $null -eq $signature.SignerCertificate -or
            $signature.SignerCertificate.Subject -cnotmatch '^CN=Valve Corp\.(?:,|$)') {
            throw 'Isolated research binary is not validly Valve-signed.'
        }
    }
    return $true
}

$gitIgnorePath = Join-Path $repositoryRoot '.gitignore'
if (-not (Test-Path -LiteralPath $gitIgnorePath -PathType Leaf) -or
    (Get-Content -Raw -LiteralPath $gitIgnorePath) -cnotmatch
        '(?m)^/manual-artifacts/\s*$') {
    throw 'Verifier requires the repository-wide /manual-artifacts/ ignore rule.'
}

if ($PSCmdlet.ParameterSetName -eq 'Isolation') {
    [void](Assert-IsolatedResearchRoot -Path $ResearchHalfLifeRoot)
    Write-Output (
        'research-root-valid isolated-copy=true primary-steam=false ' +
        'processes-started=0 external-file-drift=none')
    return
}

if ($PSCmdlet.ParameterSetName -eq 'Validate' -or
    $PSCmdlet.ParameterSetName -eq 'ValidateSet') {
    $path = if ($PSCmdlet.ParameterSetName -eq 'ValidateSet') {
        $root = [IO.Path]::GetFullPath($ValidateMetadataSetRoot)
        if ($root -cne $projectionRoot) {
            throw 'Projection-set validation requires the exact tracked evidence root.'
        }
        if (-not (Test-Path -LiteralPath $root -PathType Container)) {
            throw 'Projection root is absent.'
        }
        Assert-NoReparsePoint -Path $root -Label 'projection root'
        Join-Path $root 'GOLDSRC_RESOURCE_LIST_STOCK.json'
    }
    else { [IO.Path]::GetFullPath($ValidateMetadataPath) }
    if ($path -cne $projectionPath) {
        throw 'Metadata validation requires the exact tracked projection path.'
    }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
        (Get-Item -LiteralPath $path).Length -gt $maximumProjectionBytes) {
        throw 'Projection metadata is absent or outside its byte bound.'
    }
    Assert-NoReparsePoint -Path $path -Label 'projection metadata'
    Assert-OnlyDefaultDataStream -Path $path -Label 'projection metadata'
    $metadata = Get-Content -Raw -LiteralPath $path |
        ConvertFrom-Json -ErrorAction Stop
    Assert-ProjectionMetadata -Metadata $metadata
    Write-Output (
        'metadata-valid payloads=54 profiles=50/2/2 opcode43=resource-list-standard ' +
        'maxplayers1=2/no-list responses=51+3 custom=pending ' +
        'external-file-drift=none')
    return
}

$metadata = Project-EvidenceSet
Assert-ProjectionMetadata -Metadata $metadata
Assert-NoReparsePointInExistingPath -Path $projectionRoot `
    -Label 'tracked projection root'
if (-not (Test-Path -LiteralPath $projectionRoot -PathType Container)) {
    [IO.Directory]::CreateDirectory($projectionRoot) | Out-Null
}
Assert-NoReparsePoint -Path $projectionRoot -Label 'projection root'
$temporaryPath = $projectionPath + '.tmp'
if (Test-Path -LiteralPath $temporaryPath) {
    throw 'Refusing to overwrite an unexpected temporary projection file.'
}
$json = $metadata | ConvertTo-Json -Depth 16
$encoding = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText($temporaryPath, $json + "`r`n", $encoding)
try {
    Move-Item -LiteralPath $temporaryPath -Destination $projectionPath -Force
}
finally {
    if (Test-Path -LiteralPath $temporaryPath) {
        Remove-Item -LiteralPath $temporaryPath -Force
    }
}
Write-Output (
    'projection-valid payloads=54 profiles=50/2/2 opcode43=resource-list-standard ' +
    'maxplayers1=2/no-list responses=51+3 custom=pending ' +
    'external-file-drift=none')
