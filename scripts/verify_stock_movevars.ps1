<#
.SYNOPSIS
Projects or validates bounded stock GoldSrc opcode-44 movement metadata.

.DESCRIPTION
Project mode reads the two ignored canonical service payloads emitted by the
private-loopback signed-stock capture harness. It walks the first service
    stream from byte zero, parses the already-confirmed server-info and seven
    delta-description messages, decodes opcode 44 at the exact cursor, validates
    the confirmed simple trailing messages, and stops immediately after checking
    numeric opcode 13 at the exact cursor. Its body stays wholly unconsumed. The
    script then validates the separately framed
post-sendres stock transfer: numeric opcode 45 plus its exact eight-byte body,
followed by numeric opcode 43 at byte offset nine with that body unconsumed.

Only one sanitized metadata.json is written below the ignored projection root.
Raw service, packet, user-info, RCON, authentication, resource-list, and opaque
   bytes are never copied into the projection. Numeric opcode 43 deliberately
   remains a neutral post-sendres observed boundary because the pinned public
   Valve HLSDK does not expose a numeric service-message constant that
   independently maps it to a resource list.
#>
[CmdletBinding(DefaultParameterSetName = 'Project')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Project')]
    [ValidateNotNullOrEmpty()]
    [string]$SourceRunDirectory,

    [Parameter(Mandatory = $true, ParameterSetName = 'ValidateMetadata')]
    [ValidateNotNullOrEmpty()]
    [string]$ValidateMetadataPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'ValidateMetadataSet')]
    [ValidateNotNullOrEmpty()]
    [string]$ValidateMetadataSetRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$manualArtifactRoot = [IO.Path]::GetFullPath((
    Join-Path $repositoryRoot 'manual-artifacts'))
$captureRoot = [IO.Path]::GetFullPath((
    Join-Path $manualArtifactRoot 'movevars-captures'))
$projectionRoot = [IO.Path]::GetFullPath((
    Join-Path $captureRoot 'projections'))
$expectedSchema = 'hlclient.stock-movevars-metadata.v1'
$expectedProfile = 'stock-hl-1.1.1.1-to-stock-hlds-protocol48-build10210'
$maximumPayloadBytes = 262144
$maximumMetadataBytes = 524288
$maximumMoveVarsStringBytes = 64
$maximumServiceStringBytes = 4096
$maximumFloatMagnitude = 1000000.0

$expectedDeltaSchemas = @(
    [pscustomobject]@{
        name = 'event_t'; field_count = 14; message_bytes = 380
        body_bits = 3032; padding_bits = 6
        field_definition_sha256 = '13decc0c313dfbf4051b391274e86aa5c864bfff95fd8ead5df81dbcdbccd54a'
    },
    [pscustomobject]@{
        name = 'weapon_data_t'; field_count = 20; message_bytes = 613
        body_bits = 4896; padding_bits = 4
        field_definition_sha256 = '10a9179bf3e173ce6f76837896353cff5e4d6b290a9ad274ac22ccf7292fe634'
    },
    [pscustomobject]@{
        name = 'usercmd_t'; field_count = 15; message_bytes = 454
        body_bits = 3624; padding_bits = 3
        field_definition_sha256 = 'b8c141bafd07bcdc0172134e05e5760cb7865b9e81ea1a2def4890ff1b132d1d'
    },
    [pscustomobject]@{
        name = 'custom_entity_state_t'; field_count = 19; message_bytes = 539
        body_bits = 4304; padding_bits = 7
        field_definition_sha256 = '6cd1de0f96888894295d01ecf62e786daa906835533c2bce054ad2d7782099bd'
    },
    [pscustomobject]@{
        name = 'entity_state_player_t'; field_count = 49; message_bytes = 1368
        body_bits = 10936; padding_bits = 5
        field_definition_sha256 = 'a32ff3c622ead85132d14f6df32d4e31516467a7f61dfe8e445db7f9c1d7ff53'
    },
    [pscustomobject]@{
        name = 'entity_state_t'; field_count = 52; message_bytes = 1454
        body_bits = 11624; padding_bits = 4
        field_definition_sha256 = '190880fa1deddb3fc0767d4c88f4db0cdf93296ac19342afb58eb36d995dcf8e'
    },
    [pscustomobject]@{
        name = 'clientdata_t'; field_count = 50; message_bytes = 1386
        body_bits = 11080; padding_bits = 2
        field_definition_sha256 = 'eef782b1b6a89b2175e5f8ae3e4c01a1aca4977ae1f25ec04bf5572e12384c1a'
    })

$floatFieldDefinitions = @(
    [pscustomobject]@{ name = 'gravity'; offset = 0; confirmation_basis = 'controlled_single_field' },
    [pscustomobject]@{ name = 'stop_speed'; offset = 4; confirmation_basis = 'exact_capture_plus_pinned_valve_header' },
    [pscustomobject]@{ name = 'maximum_speed'; offset = 8; confirmation_basis = 'controlled_single_field' },
    [pscustomobject]@{ name = 'spectator_maximum_speed'; offset = 12; confirmation_basis = 'exact_capture_plus_pinned_valve_header' },
    [pscustomobject]@{ name = 'acceleration'; offset = 16; confirmation_basis = 'controlled_single_field' },
    [pscustomobject]@{ name = 'air_acceleration'; offset = 20; confirmation_basis = 'controlled_single_field' },
    [pscustomobject]@{ name = 'water_acceleration'; offset = 24; confirmation_basis = 'exact_capture_plus_pinned_valve_header' },
    [pscustomobject]@{ name = 'friction'; offset = 28; confirmation_basis = 'controlled_single_field' },
    [pscustomobject]@{ name = 'edge_friction'; offset = 32; confirmation_basis = 'exact_capture_plus_pinned_valve_header' },
    [pscustomobject]@{ name = 'water_friction'; offset = 36; confirmation_basis = 'exact_capture_plus_pinned_valve_header' },
    [pscustomobject]@{ name = 'entity_gravity'; offset = 40; confirmation_basis = 'exact_capture_plus_pinned_valve_header' },
    [pscustomobject]@{ name = 'bounce'; offset = 44; confirmation_basis = 'exact_capture_plus_pinned_valve_header' },
    [pscustomobject]@{ name = 'step_size'; offset = 48; confirmation_basis = 'controlled_single_field' },
    [pscustomobject]@{ name = 'maximum_velocity'; offset = 52; confirmation_basis = 'controlled_single_field' },
    [pscustomobject]@{ name = 'z_maximum'; offset = 56; confirmation_basis = 'exact_capture_plus_pinned_valve_header' },
    [pscustomobject]@{ name = 'wave_height'; offset = 60; confirmation_basis = 'exact_capture_plus_pinned_valve_header' },
    [pscustomobject]@{ name = 'roll_angle'; offset = 65; confirmation_basis = 'exact_capture_plus_pinned_valve_header' },
    [pscustomobject]@{ name = 'roll_speed'; offset = 69; confirmation_basis = 'exact_capture_plus_pinned_valve_header' },
    [pscustomobject]@{ name = 'sky_color_r'; offset = 73; confirmation_basis = 'exact_capture_plus_pinned_valve_header' },
    [pscustomobject]@{ name = 'sky_color_g'; offset = 77; confirmation_basis = 'exact_capture_plus_pinned_valve_header' },
    [pscustomobject]@{ name = 'sky_color_b'; offset = 81; confirmation_basis = 'exact_capture_plus_pinned_valve_header' },
    [pscustomobject]@{ name = 'sky_vector_x'; offset = 85; confirmation_basis = 'exact_capture_plus_pinned_valve_header' },
    [pscustomobject]@{ name = 'sky_vector_y'; offset = 89; confirmation_basis = 'exact_capture_plus_pinned_valve_header' },
    [pscustomobject]@{ name = 'sky_vector_z'; offset = 93; confirmation_basis = 'exact_capture_plus_pinned_valve_header' })

$baselineValues = @{
    gravity = '800'; stop_speed = '100'; maximum_speed = '270'
    spectator_maximum_speed = '500'; acceleration = '10'; air_acceleration = '10'
    water_acceleration = '10'; friction = '4'; edge_friction = '2'
    water_friction = '1'; entity_gravity = '1'; bounce = '1'; step_size = '18'
    maximum_velocity = '2000'; z_maximum = '4096'; wave_height = '0'
    roll_angle = '2'; roll_speed = '200'; sky_color_r = '360'; sky_color_g = '318'
    sky_color_b = '245'; sky_vector_x = '0.258819'; sky_vector_y = '0'
    sky_vector_z = '-0.965926'
}

$expectedOpcode39DeclaredSizeCounts = @(
    [pscustomobject]@{ signed_value = -1; count = 13 },
    [pscustomobject]@{ signed_value = 0; count = 2 },
    [pscustomobject]@{ signed_value = 1; count = 10 },
    [pscustomobject]@{ signed_value = 2; count = 4 },
    [pscustomobject]@{ signed_value = 3; count = 2 },
    [pscustomobject]@{ signed_value = 4; count = 1 },
    [pscustomobject]@{ signed_value = 6; count = 1 },
    [pscustomobject]@{ signed_value = 8; count = 1 },
    [pscustomobject]@{ signed_value = 9; count = 1 },
    [pscustomobject]@{ signed_value = 10; count = 1 },
    [pscustomobject]@{ signed_value = 12; count = 1 })

function Assert-NoReparsePointInExistingPath {
    param([string]$Path, [string]$Label)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $pathRoot = [IO.Path]::GetPathRoot($fullPath)
    $currentPath = $pathRoot
    foreach ($component in @($fullPath.Substring($pathRoot.Length) -split '[\\/]' |
            Where-Object { $_ })) {
        $currentPath = [IO.Path]::Combine($currentPath, $component)
        if (-not (Test-Path -LiteralPath $currentPath)) { continue }
        if (((Get-Item -LiteralPath $currentPath -Force).Attributes -band
                [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Label path chain must not contain a reparse point."
        }
    }
}

function Assert-NoDescendantReparsePoint {
    param([string]$Path, [string]$Label)
    foreach ($item in @(Get-ChildItem -LiteralPath $Path -Force -Recurse)) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Label must not contain a reparse point."
        }
    }
}

function Assert-OnlyDefaultDataStream {
    param([string]$Path, [string]$Label)
    $streams = @(Get-Item -LiteralPath $Path -Stream '*' -ErrorAction Stop)
    if ($streams.Count -ne 1 -or $streams[0].Stream -cne ':$DATA') {
        throw "$Label must contain only its default NTFS data stream."
    }
}

function Resolve-ExplicitFile {
    param([string]$Path, [string]$Label)
    $inputFullPath = [IO.Path]::GetFullPath($Path)
    Assert-NoReparsePointInExistingPath -Path $inputFullPath -Label $Label
    $resolved = Resolve-Path -LiteralPath $inputFullPath -ErrorAction Stop
    if ($resolved.Provider.Name -cne 'FileSystem') {
        throw "$Label must be an explicit filesystem path."
    }
    $item = Get-Item -LiteralPath $resolved.Path -Force
    if ($item.PSIsContainer) { throw "$Label must name a file." }
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label must not be a symbolic link or reparse point."
    }
    Assert-OnlyDefaultDataStream -Path $item.FullName -Label $Label
    return [IO.Path]::GetFullPath($item.FullName)
}

function Resolve-ExplicitDirectory {
    param([string]$Path, [string]$Label)
    $inputFullPath = [IO.Path]::GetFullPath($Path)
    Assert-NoReparsePointInExistingPath -Path $inputFullPath -Label $Label
    $resolved = Resolve-Path -LiteralPath $inputFullPath -ErrorAction Stop
    if ($resolved.Provider.Name -cne 'FileSystem') {
        throw "$Label must be an explicit filesystem path."
    }
    $item = Get-Item -LiteralPath $resolved.Path -Force
    if (-not $item.PSIsContainer) { throw "$Label must name a directory." }
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label must not be a symbolic link or reparse point."
    }
    return [IO.Path]::GetFullPath($item.FullName)
}

function Assert-PathBelowRoot {
    param([string]$Path, [string]$Root, [string]$Label)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd([char]'\', [char]'/') +
        [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($fullRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must remain below the approved ignored artifact root."
    }
}

function Assert-MetadataOnlyDirectory {
    param([string]$MetadataPath, [string]$Label)
    $directory = [IO.Path]::GetFullPath((Split-Path -Parent $MetadataPath))
    Assert-NoReparsePointInExistingPath -Path $directory -Label $Label
    Assert-NoDescendantReparsePoint -Path $directory -Label $Label
    $items = @(Get-ChildItem -LiteralPath $directory -Force -Recurse)
    if ($items.Count -ne 1 -or $items[0].PSIsContainer -or
        [IO.Path]::GetFullPath($items[0].FullName) -cne
            [IO.Path]::GetFullPath($MetadataPath)) {
        throw "$Label must contain exactly one metadata.json and no other artifact."
    }
    Assert-OnlyDefaultDataStream -Path $MetadataPath -Label $Label
}

function Assert-ExactProperties {
    param([object]$Value, [string[]]$Allowed, [string]$Label)
    if ($null -eq $Value) { throw "$Label must be present." }
    $actual = @($Value.PSObject.Properties | ForEach-Object { $_.Name })
    foreach ($name in $actual) {
        if ($Allowed -cnotcontains $name) {
            throw "$Label has unexpected property '$name'."
        }
    }
    foreach ($name in $Allowed) {
        if ($actual -cnotcontains $name) {
            throw "$Label is missing property '$name'."
        }
    }
}

function Assert-Opcode39DeclaredSizeCounts {
    param([object[]]$Counts)
    if ($Counts.Count -ne $expectedOpcode39DeclaredSizeCounts.Count) {
        throw 'Numeric opcode-39 signed declared-size profile is incomplete.'
    }
    $total = 0
    for ($index = 0; $index -lt $Counts.Count; ++$index) {
        $entry = $Counts[$index]
        $expected = $expectedOpcode39DeclaredSizeCounts[$index]
        Assert-ExactProperties -Value $entry -Allowed @('signed_value', 'count') `
            -Label "opcode39_declared_size_counts[$index]"
        if ([int]$entry.signed_value -ne [int]$expected.signed_value -or
            [int]$entry.count -ne [int]$expected.count -or
            [int]$entry.signed_value -lt -128 -or
            [int]$entry.signed_value -gt 127 -or [int]$entry.count -lt 1) {
            throw 'Numeric opcode-39 signed declared-size profile differs from stock.'
        }
        $total += [int]$entry.count
    }
    if ($total -ne 37 -or [int]$Counts[0].signed_value -ne -1 -or
        [int]$Counts[0].count -ne 13) {
        throw 'Numeric opcode-39 signed declared-size totals are invalid.'
    }
}

function Get-Sha256Hex {
    param([byte[]]$Bytes)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return (($sha.ComputeHash($Bytes) | ForEach-Object {
                    $_.ToString('x2')
                }) -join '')
    }
    finally { $sha.Dispose() }
}

function Get-TextSha256Hex {
    param([string]$Text)
    return Get-Sha256Hex -Bytes ([Text.Encoding]::UTF8.GetBytes($Text))
}

function Read-U16Le {
    param([byte[]]$Bytes, [int]$Offset)
    if ($Offset -lt 0 -or $Offset + 2 -gt $Bytes.Length) {
        throw 'Truncated u16le field.'
    }
    return [uint16]([uint16]$Bytes[$Offset] -bor
        ([uint16]$Bytes[$Offset + 1] -shl 8))
}

function Read-U32Le {
    param([byte[]]$Bytes, [int]$Offset)
    if ($Offset -lt 0 -or $Offset + 4 -gt $Bytes.Length) {
        throw 'Truncated u32le field.'
    }
    return [uint32]([uint32]$Bytes[$Offset] -bor
        ([uint32]$Bytes[$Offset + 1] -shl 8) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 16) -bor
        ([uint32]$Bytes[$Offset + 3] -shl 24))
}

function Read-F32LeProjection {
    param([byte[]]$Bytes, [int]$Offset)
    [uint32]$bits = Read-U32Le -Bytes $Bytes -Offset $Offset
    $encoded = [BitConverter]::GetBytes($bits)
    if (-not [BitConverter]::IsLittleEndian) { [Array]::Reverse($encoded) }
    [single]$value = [BitConverter]::ToSingle($encoded, 0)
    if ([single]::IsNaN($value) -or [single]::IsInfinity($value) -or
        [Math]::Abs([double]$value) -gt $maximumFloatMagnitude) {
        throw 'Movevars float is non-finite or exceeds the evidence safety bound.'
    }
    return [pscustomobject]@{
        Value = $value
        ValueText = $value.ToString('R', [Globalization.CultureInfo]::InvariantCulture)
    }
}

function Read-BoundedNulField {
    param(
        [byte[]]$Bytes,
        [ref]$Position,
        [int]$MaximumBytes,
        [string]$Label,
        [bool]$RequirePrintableAscii)
    $start = [int]$Position.Value
    if ($start -lt 0 -or $start -ge $Bytes.Length) {
        throw "$Label starts outside the bounded payload."
    }
    $available = $Bytes.Length - $start
    $scan = [Math]::Min($available, $MaximumBytes + 1)
    $terminator = -1
    for ($index = 0; $index -lt $scan; ++$index) {
        $value = $Bytes[$start + $index]
        if ($value -eq 0) { $terminator = $start + $index; break }
        if ($RequirePrintableAscii -and ($value -lt 0x20 -or $value -gt 0x7e)) {
            throw "$Label contains a non-printable byte."
        }
    }
    if ($terminator -lt 0) { throw "$Label is unterminated or over its bound." }
    $length = $terminator - $start
    $fieldBytes = [byte[]]::new($length)
    if ($length -gt 0) { [Array]::Copy($Bytes, $start, $fieldBytes, 0, $length) }
    $Position.Value = $terminator + 1
    return [pscustomobject]@{
        Start = $start
        Length = $length
        BytesConsumed = $length + 1
        Bytes = $fieldBytes
    }
}

function Get-ScenarioProfile {
    param([string]$SourceRunId)
    $suffix = '-baseline-[0-9]{8}-[0-9]{6}-[0-9]{3}$'
    switch -Regex -CaseSensitive ($SourceRunId) {
        ('^m244-baseline-(?:a|b|c|d|e-retry|f)' + $suffix) { return 'baseline' }
        ('^m244-gravity400-[ab]' + $suffix) { return 'gravity-400' }
        ('^m244-maxspeed320-[ab]' + $suffix) { return 'maxspeed-320' }
        ('^m244-accelerate12-[ab]' + $suffix) { return 'accelerate-12' }
        ('^m244-airaccelerate15-[ab]' + $suffix) { return 'airaccelerate-15' }
        ('^m244-friction6-[ab]' + $suffix) { return 'friction-6' }
        ('^m244-stepsize24-[ab]' + $suffix) { return 'stepsize-24' }
        ('^m244-maxvelocity3000-[ab]' + $suffix) { return 'maxvelocity-3000' }
        ('^m244-footsteps-mp0-[ab]' + $suffix) { return 'footsteps-0' }
        ('^m244-sky-night-(?:a|b-retry)' + $suffix) { return 'sky-night' }
        ('^m244-map-crossfire-[ab]' + $suffix) { return 'different-map' }
        ('^m244-mapchange-(?:crossfire|stalkyard)-a' + $suffix) { return 'map-change' }
        default { throw 'Source run identifier is not in the accepted formal evidence ledger.' }
    }
}

function Get-RequestedTargetMap {
    param([string]$Scenario, [string]$SourceRunId)
    switch ($Scenario) {
        'sky-night' { return 'stalkyard' }
        'different-map' { return 'crossfire' }
        'map-change' {
            if ($SourceRunId -cmatch '^m244-mapchange-crossfire-a-') {
                return 'crossfire'
            }
            if ($SourceRunId -cmatch '^m244-mapchange-stalkyard-a-') {
                return 'stalkyard'
            }
            throw 'Same-process map-change run lacks an accepted target map.'
        }
        default { return 'boot_camp' }
    }
}

function Read-DeltaProjection {
    param([byte[]]$Payload, [int]$BoundaryOffset)

    if ($Payload.Length -eq 0 -or $Payload.Length -gt $maximumPayloadBytes) {
        throw 'Canonical service payload is outside the project safety bound.'
    }
    if ($BoundaryOffset -lt 0 -or $BoundaryOffset -ge $Payload.Length -or
        $Payload[$BoundaryOffset] -ne 14) {
        throw 'The exact delta boundary is not opcode 14.'
    }

    $maximumSchemas = 32
    $maximumFieldsPerSchema = 256
    $maximumTotalFields = 4096
    $maximumNameBytes = 64
    $reader = @{ Cursor = ($BoundaryOffset + 1) * 8 }
    $bitLimit = $Payload.Length * 8

    function Read-BoundedBits {
        param([ValidateRange(0, 32)][int]$Width)
        if ($reader.Cursor + $Width -gt $bitLimit) {
            throw 'Delta-description bit stream is truncated.'
        }
        [uint64]$value = 0
        for ($index = 0; $index -lt $Width; ++$index) {
            $position = $reader.Cursor + $index
            $bit = ($Payload[$position -shr 3] -shr ($position -band 7)) -band 1
            $value = $value -bor ([uint64]$bit -shl $index)
        }
        $reader.Cursor += $Width
        return $value
    }

    function Read-BoundedName {
        $bytes = [Collections.Generic.List[byte]]::new()
        while ($bytes.Count -le $maximumNameBytes) {
            $value = [int](Read-BoundedBits -Width 8)
            if ($value -eq 0) {
                return [Text.Encoding]::ASCII.GetString($bytes.ToArray())
            }
            if ($value -lt 32 -or $value -gt 126) {
                throw 'Delta name contains a non-terminal-safe byte.'
            }
            $bytes.Add([byte]$value)
        }
        throw 'Delta name exceeds the project safety bound.'
    }

    $schemas = [Collections.Generic.List[object]]::new()
    $totalFields = 0
    while ($true) {
        if ($schemas.Count -ge $maximumSchemas) {
            throw 'Delta schema count exceeds the project safety bound.'
        }
        $messageOpcodeOffset = [int]($reader.Cursor / 8) - 1
        $schemaStartBit = $reader.Cursor
        $schemaName = Read-BoundedName
        $fieldCount = [int](Read-BoundedBits -Width 16)
        if ($fieldCount -lt 1 -or $fieldCount -gt $maximumFieldsPerSchema -or
            $totalFields + $fieldCount -gt $maximumTotalFields) {
            throw 'Delta field count exceeds the project safety bound.'
        }
        $totalFields += $fieldCount

        $canonicalFields = [Collections.Generic.List[string]]::new()
        for ($fieldIndex = 0; $fieldIndex -lt $fieldCount; ++$fieldIndex) {
            $maskByteCount = [int](Read-BoundedBits -Width 3)
            if ($maskByteCount -ne 1) {
                throw 'Unexpected delta field-mask byte count.'
            }
            $mask = [int](Read-BoundedBits -Width 8)
            if ($mask -ne 0x7F -and $mask -ne 0x7B) {
                throw 'Unknown or incomplete delta field mask.'
            }
            $fieldType = [uint32](Read-BoundedBits -Width 32)
            $fieldName = Read-BoundedName
            $fieldOffset = if (($mask -band 0x04) -ne 0) {
                [int](Read-BoundedBits -Width 16)
            }
            else { 0 }
            $storageSize = [int](Read-BoundedBits -Width 8)
            $significantBits = [int](Read-BoundedBits -Width 8)
            $premultiplyWire = [uint32](Read-BoundedBits -Width 32)
            $postmultiplyWire = [uint32](Read-BoundedBits -Width 32)
            if ($storageSize -ne 1 -or $significantBits -lt 1 -or
                $significantBits -gt 32 -or $premultiplyWire -eq 0 -or
                $postmultiplyWire -eq 0) {
                throw 'Delta numeric metadata violates the confirmed profile.'
            }
            $canonicalFields.Add(('{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}' -f
                    $fieldName, ([uint64]$fieldType), $fieldOffset,
                    $storageSize, $significantBits, ([uint64]$premultiplyWire),
                    ([uint64]$postmultiplyWire), $mask))
        }

        $paddingBits = (8 - ($reader.Cursor -band 7)) -band 7
        if ($paddingBits -ne 0 -and
            [int](Read-BoundedBits -Width $paddingBits) -ne 0) {
            throw 'Delta message alignment padding must be zero.'
        }
        $nextOpcodeOffset = [int]($reader.Cursor / 8)
        $nextOpcode = [int](Read-BoundedBits -Width 8)
        $schemas.Add([pscustomobject][ordered]@{
            index = $schemas.Count
            name = $schemaName
            field_count = $fieldCount
            field_definition_sha256 = Get-TextSha256Hex -Text (
                $canonicalFields -join "`n")
            message_offset = $messageOpcodeOffset
            body_bits = ($nextOpcodeOffset * 8) - $schemaStartBit
            message_bytes = $nextOpcodeOffset - $messageOpcodeOffset
            padding_bits = $paddingBits
        })

        if ($nextOpcode -ne 14) {
            if ($nextOpcode -ne 44) {
                throw 'The exact post-delta opcode differs from the confirmed profile.'
            }
            return [pscustomobject]@{
                schemas = $schemas.ToArray()
                total_fields = $totalFields
                bytes_consumed = $nextOpcodeOffset - $BoundaryOffset
                next_opcode = $nextOpcode
                next_opcode_offset = $nextOpcodeOffset
            }
        }
    }
}

function Assert-DeltaProjection {
    param([object]$Projection, [int]$ExpectedOffset)
    if (@($Projection.schemas).Count -ne $expectedDeltaSchemas.Count -or
        [int]$Projection.total_fields -ne 219 -or
        [int]$Projection.bytes_consumed -ne 6194 -or
        [int]$Projection.next_opcode -ne 44) {
        throw 'Delta projection does not match the accepted aggregate profile.'
    }
    $expectedMessageOffset = $ExpectedOffset
    for ($index = 0; $index -lt $expectedDeltaSchemas.Count; ++$index) {
        $actual = $Projection.schemas[$index]
        $expected = $expectedDeltaSchemas[$index]
        if ($actual.name -cne $expected.name -or
            [int]$actual.field_count -ne [int]$expected.field_count -or
            [int]$actual.message_offset -ne $expectedMessageOffset -or
            [int]$actual.message_bytes -ne [int]$expected.message_bytes -or
            [int]$actual.body_bits -ne [int]$expected.body_bits -or
            [int]$actual.padding_bits -ne [int]$expected.padding_bits -or
            $actual.field_definition_sha256 -cne
                $expected.field_definition_sha256) {
            throw "Delta schema profile mismatch at index $index."
        }
        $expectedMessageOffset += [int]$expected.message_bytes
    }
    if ([int]$Projection.next_opcode_offset -ne $expectedMessageOffset) {
        throw 'Delta exact next-opcode cursor is inconsistent.'
    }
}

function Read-FirstServiceProjection {
    param(
        [byte[]]$Payload,
        [object]$CaptureMetadata,
        [object]$RunConfig,
        [string]$Scenario)

    if ($Payload.Length -lt 1 -or $Payload.Length -gt $maximumPayloadBytes) {
        throw 'First canonical service payload is outside the safety bound.'
    }
    $position = 0
    if ($Payload[$position] -ne 8) {
        throw 'First canonical service payload does not start with opcode 8.'
    }
    ++$position
    $textPosition = [ref]$position
    [void](Read-BoundedNulField -Bytes $Payload -Position $textPosition `
        -MaximumBytes 1024 -Label 'opcode-8 text' -RequirePrintableAscii $false)
    $position = [int]$textPosition.Value
    if ($position -ne [int]$CaptureMetadata.first_service_boundary.byte_offset -or
        $position -ge $Payload.Length -or $Payload[$position] -ne 11) {
        throw 'Capture metadata and exact opcode-11 cursor disagree.'
    }

    $serverInfoOffset = $position
    ++$position
    $serverBodyStart = $position
    [uint32]$protocol = Read-U32Le -Bytes $Payload -Offset $position
    $position += 4
    [uint32]$mapStartOrdinal = Read-U32Le -Bytes $Payload -Offset $position
    $position += 4
    [void](Read-U32Le -Bytes $Payload -Offset $position)
    $position += 4
    if ($position + 16 + 3 -gt $Payload.Length) {
        throw 'Server-info fixed prefix is truncated.'
    }
    $position += 16
    $maximumClients = [int]$Payload[$position]
    ++$position
    ++$position # cursor-only one-byte field
    $multiClientFlag = [int]$Payload[$position]
    ++$position
    if ($protocol -ne 48 -or $maximumClients -lt 1 -or $maximumClients -gt 32 -or
        $multiClientFlag -notin @(0, 1) -or
        (($maximumClients -gt 1) -ne ($multiClientFlag -eq 1))) {
        throw 'Server-info fixed profile differs from the accepted stock profile.'
    }

    $gamePosition = [ref]$position
    $gameField = Read-BoundedNulField -Bytes $Payload -Position $gamePosition `
        -MaximumBytes $maximumServiceStringBytes -Label 'server-info game directory' `
        -RequirePrintableAscii $true
    $position = [int]$gamePosition.Value
    $hostPosition = [ref]$position
    [void](Read-BoundedNulField -Bytes $Payload -Position $hostPosition `
        -MaximumBytes $maximumServiceStringBytes -Label 'server-info hostname' `
        -RequirePrintableAscii $true)
    $position = [int]$hostPosition.Value
    $mapPosition = [ref]$position
    $mapField = Read-BoundedNulField -Bytes $Payload -Position $mapPosition `
        -MaximumBytes $maximumServiceStringBytes -Label 'server-info map path' `
        -RequirePrintableAscii $true
    $position = [int]$mapPosition.Value
    $opaquePosition = [ref]$position
    [void](Read-BoundedNulField -Bytes $Payload -Position $opaquePosition `
        -MaximumBytes $maximumServiceStringBytes -Label 'server-info cursor-only string' `
        -RequirePrintableAscii $false)
    $position = [int]$opaquePosition.Value
    if ($position -ge $Payload.Length -or $Payload[$position] -ne 0) {
        throw 'Server-info reserved byte differs from confirmed zero.'
    }
    ++$position
    $gameDirectory = [Text.Encoding]::ASCII.GetString($gameField.Bytes)
    $mapPath = [Text.Encoding]::ASCII.GetString($mapField.Bytes)
    $expectedCapturedMap = if ($Scenario -ceq 'map-change') {
        [string]$RunConfig.prime_map
    }
    else { [string]$RunConfig.map }
    if ($gameDirectory -cne 'valve' -or
        $mapPath -cne ('maps/' + $expectedCapturedMap + '.bsp')) {
        throw 'Server-info sanitized map/game fields disagree with the run config.'
    }
    $expectedOrdinal = if ([string]$RunConfig.prime_map) { 2 } else { 1 }
    if ([uint32]$mapStartOrdinal -ne [uint32]$expectedOrdinal) {
        throw 'Server-info map-start ordinal differs from the controlled launch.'
    }

    $serverInfoBodyBytes = $position - $serverBodyStart
    $postServerInfoOffset = $position
    if ($position + 3 -gt $Payload.Length -or $Payload[$position] -ne 54 -or
        $Payload[$position + 1] -ne 0 -or $Payload[$position + 2] -ne 0) {
        throw 'Exact post-server-info opcode-54 control differs from the profile.'
    }
    $position += 3
    $deltaOffset = $position
    $delta = Read-DeltaProjection -Payload $Payload -BoundaryOffset $deltaOffset
    Assert-DeltaProjection -Projection $delta -ExpectedOffset $deltaOffset
    $moveVarsOffset = [int]$delta.next_opcode_offset
    if ($moveVarsOffset -ge $Payload.Length -or $Payload[$moveVarsOffset] -ne 44) {
        throw 'Exact post-delta cursor is not numeric opcode 44.'
    }

    $bodyStart = $moveVarsOffset + 1
    if ($bodyStart + 98 -gt $Payload.Length) {
        throw 'Opcode-44 fixed movement/environment prefix is truncated.'
    }
    $fields = [Collections.Generic.List[object]]::new()
    foreach ($definition in @($floatFieldDefinitions | Where-Object offset -lt 64 |
            Sort-Object offset)) {
        $value = Read-F32LeProjection -Bytes $Payload `
            -Offset ($bodyStart + [int]$definition.offset)
        $fields.Add([pscustomobject][ordered]@{
            name = [string]$definition.name
            body_offset = [int]$definition.offset
            byte_width = 4
            encoding = 'ieee754-binary32'
            endianness = 'little'
            observed_value = $value.ValueText
            confidence = 'confirmed'
            confirmation_basis = [string]$definition.confirmation_basis
            public_api_exposure = $true
        })
    }

    $footsteps = [int]$Payload[$bodyStart + 64]
    if ($footsteps -notin @(0, 1)) {
        throw 'Opcode-44 footsteps byte is outside the captured 0/1 set.'
    }
    $fields.Add([pscustomobject][ordered]@{
        name = 'footsteps'
        body_offset = 64
        byte_width = 1
        encoding = 'u8-enum-0-or-1'
        endianness = 'not-applicable'
        observed_value = $footsteps.ToString(
            [Globalization.CultureInfo]::InvariantCulture)
        confidence = 'confirmed'
        confirmation_basis = 'controlled_single_field'
        public_api_exposure = $true
    })

    foreach ($definition in @($floatFieldDefinitions | Where-Object offset -gt 64 |
            Sort-Object offset)) {
        $value = Read-F32LeProjection -Bytes $Payload `
            -Offset ($bodyStart + [int]$definition.offset)
        $fields.Add([pscustomobject][ordered]@{
            name = [string]$definition.name
            body_offset = [int]$definition.offset
            byte_width = 4
            encoding = 'ieee754-binary32'
            endianness = 'little'
            observed_value = $value.ValueText
            confidence = 'confirmed'
            confirmation_basis = [string]$definition.confirmation_basis
            public_api_exposure = $true
        })
    }

    $skyPositionValue = $bodyStart + 97
    $skyPosition = [ref]$skyPositionValue
    $skyField = Read-BoundedNulField -Bytes $Payload -Position $skyPosition `
        -MaximumBytes $maximumMoveVarsStringBytes -Label 'opcode-44 sky name' `
        -RequirePrintableAscii $true
    $position = [int]$skyPosition.Value
    $skyName = [Text.Encoding]::ASCII.GetString($skyField.Bytes)
    if ($skyName -notmatch '^[A-Za-z0-9_-]{1,64}$') {
        throw 'Opcode-44 sky name is outside the terminal-safe evidence profile.'
    }
    $fields.Add([pscustomobject][ordered]@{
        name = 'sky_name'
        body_offset = 97
        byte_width = [int]$skyField.BytesConsumed
        encoding = 'ascii-nul'
        endianness = 'not-applicable'
        observed_value = $skyName
        confidence = 'confirmed'
        confirmation_basis = 'exact_capture_plus_pinned_valve_header'
        public_api_exposure = $true
    })
    $moveVarsBodyBytes = $position - $bodyStart
    if ($moveVarsBodyBytes -ne 97 + $skyField.BytesConsumed) {
        throw 'Opcode-44 body-size rule is inconsistent.'
    }

    $opcode32Offset = $position
    if ($position + 3 -gt $Payload.Length -or $Payload[$position] -ne 32 -or
        $Payload[$position + 1] -ne 0 -or $Payload[$position + 2] -ne 0) {
        throw 'Confirmed numeric opcode-32 fixed control is absent or changed.'
    }
    $position += 3
    $opcode5Offset = $position
    if ($position + 3 -gt $Payload.Length -or $Payload[$position] -ne 5 -or
        (Read-U16Le -Bytes $Payload -Offset ($position + 1)) -ne 1) {
        throw 'Confirmed numeric opcode-5 u16le control is absent or changed.'
    }
    $position += 3

    $userMessageStart = $position
    $userMessageCount = 0
    $userMessageSizeCounts = @{}
    while ($position -lt $Payload.Length -and $Payload[$position] -eq 39) {
        if ($userMessageCount -ge 64 -or $position + 19 -gt $Payload.Length) {
            throw 'Numeric opcode-39 definition block exceeds its exact bound.'
        }
        $expectedIndex = 100 - $userMessageCount
        if ([int]$Payload[$position + 1] -ne $expectedIndex) {
            throw 'Numeric opcode-39 definition indices are not the captured sequence.'
        }
        $declaredSizeWire = [int]$Payload[$position + 2]
        $declaredSizeSigned = if ($declaredSizeWire -ge 128) {
            $declaredSizeWire - 256
        }
        else { $declaredSizeWire }
        if (-not $userMessageSizeCounts.ContainsKey($declaredSizeSigned)) {
            $userMessageSizeCounts[$declaredSizeSigned] = 0
        }
        ++$userMessageSizeCounts[$declaredSizeSigned]
        $nameStart = $position + 3
        $nul = -1
        for ($index = 0; $index -lt 16; ++$index) {
            $value = $Payload[$nameStart + $index]
            if ($value -eq 0 -and $nul -lt 0) { $nul = $index; continue }
            if ($nul -lt 0 -and ($value -lt 0x20 -or $value -gt 0x7e)) {
                throw 'Numeric opcode-39 fixed name contains a non-printable byte.'
            }
            if ($nul -ge 0 -and $value -ne 0) {
                throw 'Numeric opcode-39 fixed name padding is non-zero.'
            }
        }
        if ($nul -lt 1) { throw 'Numeric opcode-39 fixed name is not NUL-terminated.' }
        $position += 19
        ++$userMessageCount
    }
    if ($userMessageCount -ne 37) {
        throw 'Numeric opcode-39 definition count differs from the captured profile.'
    }
    if ($userMessageSizeCounts.Count -ne $expectedOpcode39DeclaredSizeCounts.Count) {
        throw 'Numeric opcode-39 signed declared-size set differs from stock.'
    }
    $userMessageSizeProfile = @($expectedOpcode39DeclaredSizeCounts |
        ForEach-Object {
            [pscustomobject][ordered]@{
                signed_value = [int]$_.signed_value
                count = if ($userMessageSizeCounts.ContainsKey([int]$_.signed_value)) {
                    [int]$userMessageSizeCounts[[int]$_.signed_value]
                }
                else { 0 }
            }
        })
    Assert-Opcode39DeclaredSizeCounts -Counts $userMessageSizeProfile
    $userMessageBytes = [byte[]]::new($position - $userMessageStart)
    [Array]::Copy($Payload, $userMessageStart, $userMessageBytes, 0,
        $userMessageBytes.Length)

    $stringControls = [Collections.Generic.List[object]]::new()
    while ($position -lt $Payload.Length -and $Payload[$position] -eq 9) {
        if ($stringControls.Count -ge 8) {
            throw 'Numeric opcode-9 control count exceeds its evidence bound.'
        }
        $messageOffset = $position
        ++$position
        $stringPosition = [ref]$position
        $stringField = Read-BoundedNulField -Bytes $Payload -Position $stringPosition `
            -MaximumBytes 1024 -Label 'numeric opcode-9 string' `
            -RequirePrintableAscii $false
        $position = [int]$stringPosition.Value
        $stringControls.Add([pscustomobject][ordered]@{
            opcode = 9
            byte_offset = $messageOffset
            string_bytes = [int]$stringField.Length
            string_sha256 = Get-Sha256Hex -Bytes $stringField.Bytes
            bytes_consumed = $position - $messageOffset
        })
    }
    if ($stringControls.Count -ne 3) {
        throw 'Numeric opcode-9 control count differs from the captured profile.'
    }

    $opcode13Offset = $position
    if ($position -ge $Payload.Length -or $Payload[$position] -ne 13) {
        throw 'Exact first unconfirmed boundary is not numeric opcode 13.'
    }
    $opcode13RemainingBodyBytes = $Payload.Length - ($opcode13Offset + 1)

    return [pscustomobject]@{
        server_info_offset = $serverInfoOffset
        server_info_body_bytes = $serverInfoBodyBytes
        map_start_ordinal = [uint32]$mapStartOrdinal
        maximum_clients = $maximumClients
        post_server_info_offset = $postServerInfoOffset
        delta_offset = $deltaOffset
        delta = $delta
        movevars_offset = $moveVarsOffset
        movevars_body_bytes = $moveVarsBodyBytes
        movevars_message_bytes = $moveVarsBodyBytes + 1
        movevars_end_offset = $moveVarsOffset + $moveVarsBodyBytes + 1
        fields = $fields.ToArray()
        footsteps = $footsteps
        sky_name = $skyName
        opcode32_offset = $opcode32Offset
        opcode5_offset = $opcode5Offset
        opcode39_offset = $userMessageStart
        opcode39_count = $userMessageCount
        opcode39_bytes = $userMessageBytes.Length
        opcode39_sha256 = Get-Sha256Hex -Bytes $userMessageBytes
        opcode39_declared_size_counts = $userMessageSizeProfile
        opcode9_controls = $stringControls.ToArray()
        opcode13 = 13
        opcode13_offset = $opcode13Offset
        opcode13_remaining_body_bytes = $opcode13RemainingBodyBytes
        payload_bytes = $Payload.Length
    }
}

function Read-SecondServiceProjection {
    param([byte[]]$Payload, [uint32]$ExpectedOrdinal)
    if ($Payload.Length -lt 11 -or $Payload.Length -gt $maximumPayloadBytes) {
        throw 'Second canonical service payload is outside the safety bound.'
    }
    if ($Payload[0] -ne 45) {
        throw 'Second canonical service payload does not start at numeric opcode 45.'
    }
    [uint32]$ordinal = Read-U32Le -Bytes $Payload -Offset 1
    [uint32]$fixedZero = Read-U32Le -Bytes $Payload -Offset 5
    if ($ordinal -ne $ExpectedOrdinal -or $fixedZero -ne 0) {
        throw 'Numeric opcode-45 fixed body differs from the controlled profile.'
    }
    if ($Payload[9] -ne 43) {
        throw 'Exact post-opcode-45 boundary is not numeric opcode 43.'
    }
    return [pscustomobject]@{
        opcode45_offset = 0
        opcode45_body_bytes = 8
        opcode45_bytes_consumed = 9
        map_start_ordinal = $ordinal
        fixed_zero_value = $fixedZero
        boundary_opcode = 43
        boundary_offset = 9
        remaining_body_bytes = $Payload.Length - 10
        payload_bytes = $Payload.Length
    }
}

function Assert-SourceCaptureContract {
    param(
        [string]$SourceDirectory,
        [string]$SourceRunId,
        [string]$Scenario)

    Assert-NoDescendantReparsePoint -Path $SourceDirectory -Label 'source stock run'
    $items = @(Get-ChildItem -LiteralPath $SourceDirectory -Force)
    if (@($items | Where-Object PSIsContainer).Count -ne 0) {
        throw 'Source stock run must not contain child directories.'
    }
    foreach ($item in $items) {
        if ($item.Name -cnotin @(
                'metadata.json', 'research-run-config.json',
                'research-service-payload.bin',
                'research-resource-service-payload.bin',
                'research-server-stdout.txt', 'research-server-stderr.txt') -and
            $item.Name -cnotmatch '^research-post-boundary-client-[0-9]{3}\.bin$') {
            throw "Source stock run contains unexpected file '$($item.Name)'."
        }
        Assert-OnlyDefaultDataStream -Path $item.FullName `
            -Label "source file '$($item.Name)'"
    }

    $requiredNames = @(
        'metadata.json', 'research-run-config.json',
        'research-service-payload.bin',
        'research-resource-service-payload.bin')
    foreach ($name in $requiredNames) {
        if (@($items | Where-Object Name -CEQ $name).Count -ne 1) {
            throw "Source stock run must contain exactly one '$name'."
        }
    }
    $clientBodies = @($items | Where-Object {
            $_.Name -cmatch '^research-post-boundary-client-[0-9]{3}\.bin$'
        } | Sort-Object Name)
    if ($clientBodies.Count -lt 1 -or $clientBodies.Count -gt 8) {
        throw 'Source stock run has an invalid bounded client-body count.'
    }
    foreach ($body in $clientBodies) {
        if ($body.Length -lt 1 -or $body.Length -gt 4096) {
            throw 'Ignored stock client body is outside its safety bound.'
        }
    }

    $metadataPath = Resolve-ExplicitFile -Path (
        Join-Path $SourceDirectory 'metadata.json') -Label 'capture metadata'
    if ((Get-Item -LiteralPath $metadataPath).Length -gt $maximumMetadataBytes) {
        throw 'Capture metadata exceeds its safety bound.'
    }
    $capture = Get-Content -Raw -LiteralPath $metadataPath |
        ConvertFrom-Json -ErrorAction Stop
    Assert-ExactProperties -Value $capture -Allowed @(
        'schema', 'profile', 'scenario', 'completion', 'loopback_only',
        'byte_preserving_relay', 'same_upstream_socket',
        'exact_server_endpoint_validation',
        'client_endpoint_learned_from_canonical_getchallenge',
        'raw_packet_bytes_stored', 'packet_count', 'post_accept_packet_count',
        'total_bytes', 'maximum_packets', 'maximum_post_accept_packets',
        'maximum_datagram_bytes', 'maximum_total_bytes', 'timeout_seconds',
        'elapsed_milliseconds', 'connect_seen', 'accept_seen',
        'scenario_mutation_count', 'ignored_wrong_source_count',
        'held_packet_at_end', 'initial_request', 'initial_request_transmissions',
        'request_acknowledgements', 'first_service_boundary',
        'second_service_boundary', 'duplicate_batch_datagrams_replayed',
        'post_boundary_client_reliable_transmissions', 'transfers',
        'fragment_acknowledgements', 'events', 'actions') `
        -Label 'capture metadata'
    if ($capture.schema -cne 'hlclient.stock-movevars-capture-metadata.v1' -or
        $capture.profile -cne $expectedProfile -or
        $capture.scenario -cne 'Baseline' -or
        $capture.completion -cne 'bounded_complete') {
        throw 'Capture metadata identity/completion is invalid.'
    }
    foreach ($name in @(
            'loopback_only', 'byte_preserving_relay', 'same_upstream_socket',
            'exact_server_endpoint_validation',
            'client_endpoint_learned_from_canonical_getchallenge',
            'connect_seen', 'accept_seen')) {
        if ($capture.$name -cne $true) {
            throw "Capture contract '$name' must be true."
        }
    }
    foreach ($name in @(
            'raw_packet_bytes_stored', 'held_packet_at_end')) {
        if ($capture.$name -cne $false) {
            throw "Capture contract '$name' must be false."
        }
    }
    if ([int]$capture.packet_count -lt 1 -or [int]$capture.packet_count -gt 400 -or
        [int]$capture.post_accept_packet_count -lt 1 -or
        [int]$capture.post_accept_packet_count -gt 350 -or
        [int64]$capture.total_bytes -lt 1 -or
        [int64]$capture.total_bytes -gt 524288 -or
        [int]$capture.maximum_packets -gt 400 -or
        [int]$capture.maximum_post_accept_packets -gt 350 -or
        [int]$capture.maximum_datagram_bytes -gt 2048 -or
        [int64]$capture.maximum_total_bytes -gt 524288 -or
        [int]$capture.timeout_seconds -gt 45 -or
        [int]$capture.scenario_mutation_count -ne 0 -or
        [int]$capture.ignored_wrong_source_count -ne 0 -or
        [int]$capture.duplicate_batch_datagrams_replayed -ne 0) {
        throw 'Capture transport counters exceed the accepted hard bounds.'
    }
    if (@($capture.events).Count -ne [int]$capture.packet_count -or
        @($capture.actions).Count -ne [int]$capture.packet_count) {
        throw 'Capture event/action ledgers are incomplete.'
    }

    Assert-ExactProperties -Value $capture.initial_request -Allowed @(
        'opcode', 'command_ascii', 'terminator', 'terminator_offset',
        'padding_byte', 'padding_count', 'decoded_body_bytes',
        'canonical_bytes_hex', 'canonical_sha256') -Label 'initial_request'
    if ([int]$capture.initial_request.opcode -ne 3 -or
        $capture.initial_request.command_ascii -cne 'new' -or
        [int]$capture.initial_request.terminator -ne 0 -or
        [int]$capture.initial_request.terminator_offset -ne 4 -or
        [int]$capture.initial_request.padding_byte -ne 1 -or
        [int]$capture.initial_request.padding_count -ne 3 -or
        [int]$capture.initial_request.decoded_body_bytes -ne 8 -or
        $capture.initial_request.canonical_bytes_hex -cnotmatch '^[0-9A-F]{16}$' -or
        $capture.initial_request.canonical_sha256 -cne
            '490B1E83546E7FE1DA018154A89254354BAE54559EE52DACA6FBA95E437E1F0E') {
        throw 'Capture initial request differs from its exact accepted fixture.'
    }

    $firstPayloadPath = Resolve-ExplicitFile -Path (
        Join-Path $SourceDirectory 'research-service-payload.bin') `
        -Label 'ignored first canonical service payload'
    $secondPayloadPath = Resolve-ExplicitFile -Path (
        Join-Path $SourceDirectory 'research-resource-service-payload.bin') `
        -Label 'ignored second canonical service payload'
    $firstLength = (Get-Item -LiteralPath $firstPayloadPath).Length
    $secondLength = (Get-Item -LiteralPath $secondPayloadPath).Length
    if ($firstLength -lt 1 -or $firstLength -gt $maximumPayloadBytes -or
        $secondLength -lt 1 -or $secondLength -gt $maximumPayloadBytes) {
        throw 'Ignored canonical payload size is outside the safety bound.'
    }

    Assert-ExactProperties -Value $capture.first_service_boundary -Allowed @(
        'envelope', 'envelope_bytes', 'compressed_transfer_bytes',
        'standard_bzip2_stream_bytes', 'trailing_compressed_bytes',
        'service_payload_bytes', 'simple_messages', 'boundary_opcode',
        'byte_offset', 'opcode_bytes_consumed', 'remaining_byte_count',
        'payload_byte_count', 'byte_aligned',
        'explicit_total_length_field_observed') -Label 'first_service_boundary'
    if ($capture.first_service_boundary.envelope -cne
            'BZ2-NUL-plus-standard-bzip2' -or
        [int]$capture.first_service_boundary.envelope_bytes -ne 4 -or
        [int]$capture.first_service_boundary.trailing_compressed_bytes -ne 0 -or
        [int]$capture.first_service_boundary.service_payload_bytes -ne $firstLength -or
        [int]$capture.first_service_boundary.payload_byte_count -ne $firstLength -or
        [int]$capture.first_service_boundary.boundary_opcode -ne 11 -or
        [int]$capture.first_service_boundary.byte_offset -ne 42 -or
        [int]$capture.first_service_boundary.opcode_bytes_consumed -ne 1 -or
        $capture.first_service_boundary.byte_aligned -cne $true -or
        $capture.first_service_boundary.explicit_total_length_field_observed -cne
            $false) {
        throw 'First service-transfer boundary metadata is invalid.'
    }

    Assert-ExactProperties -Value $capture.second_service_boundary -Allowed @(
        'envelope', 'compressed_transfer_bytes', 'trailing_compressed_bytes',
        'service_payload_bytes', 'first_opcode', 'first_opcode_offset',
        'first_opcode_body_unconsumed') -Label 'second_service_boundary'
    if ($capture.second_service_boundary.envelope -cne
            'BZ2-NUL-plus-standard-bzip2' -or
        [int]$capture.second_service_boundary.trailing_compressed_bytes -ne 0 -or
        [int]$capture.second_service_boundary.service_payload_bytes -ne $secondLength -or
        [int]$capture.second_service_boundary.first_opcode -ne 45 -or
        [int]$capture.second_service_boundary.first_opcode_offset -ne 0 -or
        $capture.second_service_boundary.first_opcode_body_unconsumed -cne $true) {
        throw 'Second service-transfer boundary metadata is invalid.'
    }
    $transfers = @($capture.transfers)
    if ($transfers.Count -lt 2 -or $transfers.Count -gt 3) {
        throw 'Capture must contain the two projected transfers and at most one bounded later transfer.'
    }
    for ($index = 0; $index -lt $transfers.Count; ++$index) {
        Assert-ExactProperties -Value $transfers[$index] -Allowed @(
            'ordinal', 'stream', 'declared_count', 'reassembled_bytes',
            'reassembled_sha256', 'standard_bzip2_signature',
            'standard_gzip_signature', 'standard_zlib_header',
            'observed_in_index_order') -Label "transfers[$index]"
        if ([int]$transfers[$index].ordinal -ne $index + 1 -or
            [int]$transfers[$index].stream -ne 0 -or
            [int]$transfers[$index].declared_count -lt 1 -or
            [int]$transfers[$index].declared_count -gt 32 -or
            $transfers[$index].observed_in_index_order -cne $true -or
            $transfers[$index].standard_bzip2_signature -cne $false -or
            $transfers[$index].standard_gzip_signature -cne $false -or
            $transfers[$index].standard_zlib_header -cne $false) {
            throw "Transfer $index metadata is invalid."
        }
    }

    $configPath = Resolve-ExplicitFile -Path (
        Join-Path $SourceDirectory 'research-run-config.json') -Label 'run config'
    if ((Get-Item -LiteralPath $configPath).Length -gt 4096) {
        throw 'Run config exceeds its safety bound.'
    }
    $config = Get-Content -Raw -LiteralPath $configPath |
        ConvertFrom-Json -ErrorAction Stop
    Assert-ExactProperties -Value $config -Allowed @(
        'map', 'prime_map', 'max_players', 'client_ordinal', 'hostname',
        'cvar_name', 'cvar_value', 'cvar_rcon_applied',
        'cvar_console_evidence', 'scenario', 'server_port', 'relay_port') `
        -Label 'run config'
    if ($config.scenario -cne 'Baseline' -or [int]$config.max_players -ne 2 -or
        [int]$config.client_ordinal -ne 1 -or $config.hostname -cne 'Half-Life' -or
        [int]$config.server_port -lt 1024 -or [int]$config.server_port -gt 65534 -or
        [int]$config.relay_port -lt 1024 -or [int]$config.relay_port -gt 65534 -or
        [int]$config.server_port -eq [int]$config.relay_port) {
        throw 'Run config fixed profile is invalid.'
    }

    $expectedMap = Get-RequestedTargetMap -Scenario $Scenario `
        -SourceRunId $SourceRunId
    $expectedPrimeMap = ''
    $expectedCvar = ''
    $expectedValue = ''
    switch ($Scenario) {
        'gravity-400' { $expectedCvar = 'sv_gravity'; $expectedValue = '400' }
        'maxspeed-320' { $expectedCvar = 'sv_maxspeed'; $expectedValue = '320' }
        'accelerate-12' { $expectedCvar = 'sv_accelerate'; $expectedValue = '12' }
        'airaccelerate-15' { $expectedCvar = 'sv_airaccelerate'; $expectedValue = '15' }
        'friction-6' { $expectedCvar = 'sv_friction'; $expectedValue = '6' }
        'stepsize-24' { $expectedCvar = 'sv_stepsize'; $expectedValue = '24' }
        'maxvelocity-3000' { $expectedCvar = 'sv_maxvelocity'; $expectedValue = '3000' }
        'footsteps-0' { $expectedCvar = 'mp_footsteps'; $expectedValue = '0' }
        'map-change' {
            $expectedPrimeMap = 'boot_camp'
        }
    }
    if ($config.map -cne $expectedMap -or $config.prime_map -cne $expectedPrimeMap -or
        $config.cvar_name -cne $expectedCvar -or
        $config.cvar_value -cne $expectedValue) {
        throw 'Run config does not match its accepted scenario ledger.'
    }
    if ($expectedCvar) {
        if ($config.cvar_rcon_applied -cne $true) {
            throw 'Controlled cvar scenario lacks successful RCON application.'
        }
        Assert-ExactProperties -Value $config.cvar_console_evidence -Allowed @(
            'transport', 'exact_server_endpoint', 'requested_name',
            'requested_value', 'query_confirmed_exact_value',
            'set_response_sha256', 'query_response_sha256',
            'raw_console_response_stored') -Label 'cvar_console_evidence'
        $evidence = $config.cvar_console_evidence
        if ($evidence.transport -cne 'private-ipv4-loopback-udp-rcon' -or
            $evidence.exact_server_endpoint -cne $true -or
            $evidence.requested_name -cne $expectedCvar -or
            $evidence.requested_value -cne $expectedValue -or
            $evidence.query_confirmed_exact_value -cne $true -or
            $evidence.raw_console_response_stored -cne $false -or
            $evidence.set_response_sha256 -cnotmatch '^[0-9A-F]{64}$' -or
            $evidence.query_response_sha256 -cnotmatch '^[0-9A-F]{64}$') {
            throw 'Sanitized RCON console evidence is invalid.'
        }
    }
    elseif ($config.cvar_rcon_applied -cne $false -or
        $null -ne $config.cvar_console_evidence) {
        throw 'Non-cvar scenario unexpectedly contains RCON evidence.'
    }

    $firstClientBody = [IO.File]::ReadAllBytes($clientBodies[0].FullName)
    $sendres = [byte[]](@(3) + [Text.Encoding]::ASCII.GetBytes('sendres') + @(0))
    if ($firstClientBody.Length -lt $sendres.Length) {
        throw 'First post-boundary stock client body is too short for sendres.'
    }
    for ($index = 0; $index -lt $sendres.Length; ++$index) {
        if ($firstClientBody[$index] -ne $sendres[$index]) {
            throw 'First post-boundary stock client body lacks exact sendres prefix.'
        }
    }
    $transmissionMetadata = @($capture.post_boundary_client_reliable_transmissions)
    if ($transmissionMetadata.Count -ne $clientBodies.Count -or
        $transmissionMetadata.Count -lt 1) {
        throw 'Post-boundary client transmission ledger is incomplete.'
    }
    if ([string]$transmissionMetadata[0].decoded_body_sha256 -cne
        (Get-Sha256Hex -Bytes $firstClientBody).ToUpperInvariant()) {
        throw 'First stock sendres body hash disagrees with capture metadata.'
    }

    return [pscustomobject]@{
        Capture = $capture
        Config = $config
        FirstPayloadPath = $firstPayloadPath
        SecondPayloadPath = $secondPayloadPath
        StockSendresBodyBytes = $firstClientBody.Length
        StockSendresPrefixBytes = $sendres.Length
    }
}

function Get-ExpectedMoveVarsProfile {
    param([string]$Scenario, [string]$SourceRunId)
    $values = @{}
    foreach ($key in $baselineValues.Keys) { $values[$key] = $baselineValues[$key] }
    $footsteps = '1'
    $skyName = 'desert'
    switch ($Scenario) {
        'gravity-400' { $values.gravity = '400' }
        'maxspeed-320' { $values.maximum_speed = '320' }
        'accelerate-12' { $values.acceleration = '12' }
        'airaccelerate-15' { $values.air_acceleration = '15' }
        'friction-6' { $values.friction = '6' }
        'stepsize-24' { $values.step_size = '24' }
        'maxvelocity-3000' { $values.maximum_velocity = '3000' }
        'footsteps-0' { $footsteps = '0' }
        'different-map' {
            $values.sky_color_r = '210'
            $values.sky_color_g = '205'
            $values.sky_color_b = '183'
            $values.sky_vector_x = '-0.26496'
            $values.sky_vector_y = '0.424024'
            $values.sky_vector_z = '-0.866025'
        }
        'sky-night' {
            $values.sky_color_r = '120'
            $values.sky_color_g = '127'
            $values.sky_color_b = '172'
            $values.sky_vector_x = '-0.122788'
            $values.sky_vector_y = '0.122788'
            $values.sky_vector_z = '-0.984808'
            $skyName = 'night'
        }
        'map-change' {
            # Both accepted ordinal-2 run configurations name a requested target,
            # while captured ServerInfo and opcode 44 retain the prime boot_camp
            # profile. Keep the requests explicit so a future target-specific
            # capture cannot silently validate against this observed behavior.
            if ($SourceRunId -cnotmatch
                    '^m244-mapchange-(?:crossfire|stalkyard)-a-') {
                throw 'Same-process map-change MoveVars profile is not accepted.'
            }
        }
    }
    return [pscustomobject]@{
        Values = $values
        Footsteps = $footsteps
        SkyName = $skyName
    }
}

function Assert-ScenarioMoveVarsFields {
    param([object[]]$Fields, [string]$Scenario, [string]$SourceRunId)
    if ($Fields.Count -ne 26) { throw 'Movevars field table must contain 26 fields.' }
    $expectedProfile = Get-ExpectedMoveVarsProfile -Scenario $Scenario `
        -SourceRunId $SourceRunId
    $expectedOrder = @(
        @($floatFieldDefinitions | Where-Object offset -lt 64 | Sort-Object offset |
            ForEach-Object name) +
        @('footsteps') +
        @($floatFieldDefinitions | Where-Object offset -gt 64 | Sort-Object offset |
            ForEach-Object name) +
        @('sky_name'))
    for ($index = 0; $index -lt $Fields.Count; ++$index) {
        $field = $Fields[$index]
        Assert-ExactProperties -Value $field -Allowed @(
            'name', 'body_offset', 'byte_width', 'encoding', 'endianness',
            'observed_value', 'confidence', 'confirmation_basis',
            'public_api_exposure') -Label "fields[$index]"
        if ($field.name -cne $expectedOrder[$index] -or
            $field.public_api_exposure -cne $true) {
            throw "Movevars field order/exposure mismatch at index $index."
        }
        if ($field.name -ceq 'footsteps') {
            if ([int]$field.body_offset -ne 64 -or [int]$field.byte_width -ne 1 -or
                $field.encoding -cne 'u8-enum-0-or-1' -or
                $field.endianness -cne 'not-applicable' -or
                $field.observed_value -cne $expectedProfile.Footsteps -or
                $field.confidence -cne 'confirmed' -or
                $field.confirmation_basis -cne 'controlled_single_field') {
                throw 'Footsteps field evidence/profile is invalid.'
            }
            continue
        }
        if ($field.name -ceq 'sky_name') {
            if ([int]$field.body_offset -ne 97 -or
                [int]$field.byte_width -ne $expectedProfile.SkyName.Length + 1 -or
                $field.encoding -cne 'ascii-nul' -or
                $field.endianness -cne 'not-applicable' -or
                $field.observed_value -cne $expectedProfile.SkyName -or
                $field.confidence -cne 'confirmed' -or
                $field.confirmation_basis -cne
                    'exact_capture_plus_pinned_valve_header') {
                throw 'Sky-name field evidence/profile is invalid.'
            }
            continue
        }
        $definition = @($floatFieldDefinitions | Where-Object name -CEQ $field.name)
        if ($definition.Count -ne 1) {
            throw "Unknown float field '$($field.name)'."
        }
        $expectedValue = [string]$expectedProfile.Values[$field.name]
        if ([int]$field.body_offset -ne [int]$definition[0].offset -or
            [int]$field.byte_width -ne 4 -or
            $field.encoding -cne 'ieee754-binary32' -or
            $field.endianness -cne 'little' -or
            $field.observed_value -cne $expectedValue -or
            $field.confidence -cne 'confirmed' -or
            $field.confirmation_basis -cne
                [string]$definition[0].confirmation_basis) {
            throw "Float field '$($field.name)' differs from its evidence profile."
        }
    }
}

function New-ProjectionMetadata {
    param(
        [string]$SourceRunId,
        [string]$Scenario,
        [object]$Source,
        [object]$First,
        [object]$Second)

    $requestedTargetMap = Get-RequestedTargetMap -Scenario $Scenario `
        -SourceRunId $SourceRunId
    $primeMap = if ($Scenario -ceq 'map-change') { 'boot_camp' } else { $null }
    $moveVarsProfileRelation = if ($Scenario -ceq 'map-change') {
        'matches-prime-map-profile-after-same-process-change'
    }
    else { 'fresh-process-target-map-profile' }

    return [pscustomobject][ordered]@{
        schema = $expectedSchema
        profile = $expectedProfile
        scenario = $Scenario
        completion = 'bounded_complete'
        source_run_id = $SourceRunId
        map_context = [pscustomobject][ordered]@{
            requested_target_map = $requestedTargetMap
            prime_map = $primeMap
            captured_server_info_map = if ($Scenario -ceq 'map-change') {
                'boot_camp'
            }
            else { $requestedTargetMap }
            movevars_profile_relation = $moveVarsProfileRelation
        }
        versions = [pscustomobject][ordered]@{
            client_versioninfo = '1.1.1.1'
            server_launcher_versioninfo = '4.1.1.1'
            server_protocol = 48
            server_build = 10210
            client_signature_valid = $true
            server_signature_valid = $true
        }
        safety = [pscustomobject][ordered]@{
            private_ipv4_loopback = $true
            byte_preserving_relay = $true
            same_upstream_socket = $true
            exact_endpoints = $true
            hard_packet_byte_time_bounds = $true
            exact_owned_process_cleanup = $true
            source_payloads_gitignored = $true
            projection_metadata_only = $true
            raw_packet_bytes_stored = $false
            raw_service_payloads_stored = $false
            authentication_bytes_stored = $false
            userinfo_bytes_stored = $false
            raw_rcon_response_stored = $false
            opcode43_body_stored = $false
        }
        service_pipeline = [pscustomobject][ordered]@{
            first_payload_bytes = [int]$First.payload_bytes
            server_info_opcode = 11
            server_info_offset = [int]$First.server_info_offset
            server_info_body_bytes = [int]$First.server_info_body_bytes
            map_start_ordinal = [uint32]$First.map_start_ordinal
            delta_opcode = 14
            delta_offset = [int]$First.delta_offset
            delta_schema_count = @($First.delta.schemas).Count
            delta_field_count = [int]$First.delta.total_fields
            delta_bytes_consumed = [int]$First.delta.bytes_consumed
            movevars_opcode = 44
            movevars_offset = [int]$First.movevars_offset
        }
        movevars = [pscustomobject][ordered]@{
            opcode = 44
            semantic_name = 'move_vars'
            semantic_evidence =
                'controlled-stock-capture-and-public-valve-hlsdk-cross-check'
            byte_offset = [int]$First.movevars_offset
            body_size_rule = '97-fixed-bytes-plus-sky-name-bytes-plus-nul'
            body_bytes = [int]$First.movevars_body_bytes
            message_bytes = [int]$First.movevars_message_bytes
            bytes_consumed = [int]$First.movevars_message_bytes
            numeric_field_count = 24
            numeric_encoding = 'ieee754-binary32'
            numeric_endianness = 'little'
            finite_float_required = $true
            absolute_float_safety_bound = '1000000'
            boolean_field_count = 1
            boolean_encoding = 'u8-enum-0-or-1'
            string_field_count = 1
            string_encoding = 'ascii-nul'
            default_string_limit = 64
            hard_string_cap = 256
            reserved_field_count = 0
            exact_body_end = $true
        }
        fields = @($First.fields)
        post_movevars_first_payload = [pscustomobject][ordered]@{
            opcode32_offset = [int]$First.opcode32_offset
            opcode32_body_bytes = 2
            opcode5_offset = [int]$First.opcode5_offset
            opcode5_body_encoding = 'u16le'
            opcode5_value = 1
            opcode39_offset = [int]$First.opcode39_offset
            opcode39_count = [int]$First.opcode39_count
            opcode39_message_bytes = 19
            opcode39_block_bytes = [int]$First.opcode39_bytes
            opcode39_block_sha256 = [string]$First.opcode39_sha256
            opcode39_declared_size_encoding = 'i8-twos-complement'
            opcode39_declared_size_counts = @($First.opcode39_declared_size_counts)
            opcode9_controls = @($First.opcode9_controls)
            opcode13 = 13
            opcode13_offset = [int]$First.opcode13_offset
            opcode13_remaining_body_bytes = [int]$First.opcode13_remaining_body_bytes
            opcode13_body_unconsumed = $true
        }
        stock_reference_transition = [pscustomobject][ordered]@{
            stock_client_sendres_observed = $true
            exact_sendres_prefix_bytes = [int]$Source.StockSendresPrefixBytes
            first_reliable_body_bytes = [int]$Source.StockSendresBodyBytes
            second_transfer_observed_after_sendres = $true
            captured_transfer_count = @($Source.Capture.transfers).Count
            additional_unprojected_transfer_count =
                @($Source.Capture.transfers).Count - 2
            project_generated_sendres = $false
            project_resource_response_generated = $false
        }
        post_sendres_transfer = [pscustomobject][ordered]@{
            payload_bytes = [int]$Second.payload_bytes
            opcode45_offset = 0
            opcode45_body_bytes = 8
            opcode45_layout = 'u32le-map-start-ordinal-plus-u32le-confirmed-zero'
            opcode45_map_start_ordinal = [uint32]$Second.map_start_ordinal
            opcode45_fixed_zero = [uint32]$Second.fixed_zero_value
            bytes_consumed_before_boundary = 9
        }
        post_sendres_observed_boundary = [pscustomobject][ordered]@{
            opcode = 43
            byte_offset = [int]$Second.boundary_offset
            remaining_body_bytes = [int]$Second.remaining_body_bytes
            direction = 'server_message'
            category = 'stock-observed-post-sendres-opcode-43'
            evidence_status =
                'strong-stock-behavior-public-numeric-constant-cross-check-pending'
            semantic_name = $null
            typed_resource_list = $false
            pinned_public_numeric_constant_present = $false
            body_unconsumed = $true
        }
        stability = [pscustomobject][ordered]@{
            accepted_run_count = 28
            clean_restart_baseline_runs = 6
            accepted_runs_per_controlled_variant = 2
            accepted_different_map_runs = 2
            accepted_same_process_map_change_runs = 2
            maps_compared = @('boot_camp', 'crossfire', 'stalkyard')
            opcode45_boundary_offset_stable = $true
        }
    }
}

function Assert-ProjectionMetadata {
    param([object]$Metadata)
    Assert-ExactProperties -Value $Metadata -Allowed @(
        'schema', 'profile', 'scenario', 'completion', 'source_run_id',
        'map_context', 'versions', 'safety', 'service_pipeline', 'movevars', 'fields',
        'post_movevars_first_payload', 'stock_reference_transition',
        'post_sendres_transfer', 'post_sendres_observed_boundary', 'stability') `
        -Label 'metadata'
    if ($Metadata.schema -cne $expectedSchema -or
        $Metadata.profile -cne $expectedProfile -or
        $Metadata.completion -cne 'bounded_complete' -or
        [string]$Metadata.source_run_id -notmatch '^[A-Za-z0-9_-]{1,128}$') {
        throw 'Projection metadata identity/completion is invalid.'
    }
    $expectedScenario = Get-ScenarioProfile -SourceRunId (
        [string]$Metadata.source_run_id)
    if ($Metadata.scenario -cne $expectedScenario) {
        throw 'Projection scenario differs from its formal source-run ledger.'
    }

    $mapContext = $Metadata.map_context
    Assert-ExactProperties -Value $mapContext -Allowed @(
        'requested_target_map', 'prime_map', 'captured_server_info_map',
        'movevars_profile_relation') `
        -Label 'map_context'
    $expectedTargetMap = Get-RequestedTargetMap -Scenario $expectedScenario `
        -SourceRunId ([string]$Metadata.source_run_id)
    if ($mapContext.requested_target_map -cne $expectedTargetMap) {
        throw 'Projection requested target map differs from its formal source run.'
    }
    if ($expectedScenario -ceq 'map-change') {
        if ($mapContext.prime_map -cne 'boot_camp' -or
            $mapContext.captured_server_info_map -cne 'boot_camp' -or
            $mapContext.movevars_profile_relation -cne
                'matches-prime-map-profile-after-same-process-change') {
            throw 'Same-process map-change profile relation is invalid.'
        }
    }
    elseif ($null -ne $mapContext.prime_map -or
        $mapContext.captured_server_info_map -cne $expectedTargetMap -or
        $mapContext.movevars_profile_relation -cne
            'fresh-process-target-map-profile') {
        throw 'Fresh-process map profile relation is invalid.'
    }

    Assert-ExactProperties -Value $Metadata.versions -Allowed @(
        'client_versioninfo', 'server_launcher_versioninfo', 'server_protocol',
        'server_build', 'client_signature_valid', 'server_signature_valid') `
        -Label 'versions'
    if ($Metadata.versions.client_versioninfo -cne '1.1.1.1' -or
        $Metadata.versions.server_launcher_versioninfo -cne '4.1.1.1' -or
        [int]$Metadata.versions.server_protocol -ne 48 -or
        [int]$Metadata.versions.server_build -ne 10210 -or
        $Metadata.versions.client_signature_valid -cne $true -or
        $Metadata.versions.server_signature_valid -cne $true) {
        throw 'Projection stock version profile is invalid.'
    }

    Assert-ExactProperties -Value $Metadata.safety -Allowed @(
        'private_ipv4_loopback', 'byte_preserving_relay', 'same_upstream_socket',
        'exact_endpoints', 'hard_packet_byte_time_bounds',
        'exact_owned_process_cleanup', 'source_payloads_gitignored',
        'projection_metadata_only', 'raw_packet_bytes_stored',
        'raw_service_payloads_stored', 'authentication_bytes_stored',
        'userinfo_bytes_stored', 'raw_rcon_response_stored',
        'opcode43_body_stored') -Label 'safety'
    foreach ($name in @(
            'private_ipv4_loopback', 'byte_preserving_relay',
            'same_upstream_socket', 'exact_endpoints',
            'hard_packet_byte_time_bounds', 'exact_owned_process_cleanup',
            'source_payloads_gitignored', 'projection_metadata_only')) {
        if ($Metadata.safety.$name -cne $true) {
            throw "Projection safety property '$name' must be true."
        }
    }
    foreach ($name in @(
            'raw_packet_bytes_stored', 'raw_service_payloads_stored',
            'authentication_bytes_stored', 'userinfo_bytes_stored',
            'raw_rcon_response_stored', 'opcode43_body_stored')) {
        if ($Metadata.safety.$name -cne $false) {
            throw "Projection safety property '$name' must be false."
        }
    }

    $pipeline = $Metadata.service_pipeline
    Assert-ExactProperties -Value $pipeline -Allowed @(
        'first_payload_bytes', 'server_info_opcode', 'server_info_offset',
        'server_info_body_bytes', 'map_start_ordinal', 'delta_opcode',
        'delta_offset', 'delta_schema_count', 'delta_field_count',
        'delta_bytes_consumed', 'movevars_opcode', 'movevars_offset') `
        -Label 'service_pipeline'
    $firstPayloadBytes = [int]$pipeline.first_payload_bytes
    $expectedOrdinal = if ($expectedScenario -ceq 'map-change') { 2 } else { 1 }
    if ($firstPayloadBytes -lt 1 -or $firstPayloadBytes -gt $maximumPayloadBytes -or
        [int]$pipeline.server_info_opcode -ne 11 -or
        [int]$pipeline.server_info_offset -ne 42 -or
        [int]$pipeline.server_info_body_bytes -lt 36 -or
        [int]$pipeline.server_info_body_bytes -gt 8192 -or
        [uint32]$pipeline.map_start_ordinal -ne [uint32]$expectedOrdinal -or
        [int]$pipeline.delta_opcode -ne 14 -or
        [int]$pipeline.delta_schema_count -ne 7 -or
        [int]$pipeline.delta_field_count -ne 219 -or
        [int]$pipeline.delta_bytes_consumed -ne 6194 -or
        [int]$pipeline.movevars_opcode -ne 44 -or
        [int]$pipeline.movevars_offset -ne
            ([int]$pipeline.delta_offset + 6194)) {
        throw 'Projection service pipeline profile is invalid.'
    }

    $movevars = $Metadata.movevars
    Assert-ExactProperties -Value $movevars -Allowed @(
        'opcode', 'semantic_name', 'semantic_evidence', 'byte_offset',
        'body_size_rule', 'body_bytes', 'message_bytes', 'bytes_consumed',
        'numeric_field_count', 'numeric_encoding', 'numeric_endianness',
        'finite_float_required', 'absolute_float_safety_bound',
        'boolean_field_count', 'boolean_encoding', 'string_field_count',
        'string_encoding', 'default_string_limit', 'hard_string_cap',
        'reserved_field_count', 'exact_body_end') -Label 'movevars'
    $expectedBodyBytes = if ($expectedScenario -ceq 'sky-night') { 103 } else { 104 }
    if ([int]$movevars.opcode -ne 44 -or $movevars.semantic_name -cne 'move_vars' -or
        $movevars.semantic_evidence -cne
            'controlled-stock-capture-and-public-valve-hlsdk-cross-check' -or
        [int]$movevars.byte_offset -ne [int]$pipeline.movevars_offset -or
        $movevars.body_size_rule -cne
            '97-fixed-bytes-plus-sky-name-bytes-plus-nul' -or
        [int]$movevars.body_bytes -ne $expectedBodyBytes -or
        [int]$movevars.message_bytes -ne $expectedBodyBytes + 1 -or
        [int]$movevars.bytes_consumed -ne [int]$movevars.message_bytes -or
        [int]$movevars.numeric_field_count -ne 24 -or
        $movevars.numeric_encoding -cne 'ieee754-binary32' -or
        $movevars.numeric_endianness -cne 'little' -or
        $movevars.finite_float_required -cne $true -or
        $movevars.absolute_float_safety_bound -cne '1000000' -or
        [int]$movevars.boolean_field_count -ne 1 -or
        $movevars.boolean_encoding -cne 'u8-enum-0-or-1' -or
        [int]$movevars.string_field_count -ne 1 -or
        $movevars.string_encoding -cne 'ascii-nul' -or
        [int]$movevars.default_string_limit -ne 64 -or
        [int]$movevars.hard_string_cap -ne 256 -or
        [int]$movevars.reserved_field_count -ne 0 -or
        $movevars.exact_body_end -cne $true) {
        throw 'Movevars grammar metadata is invalid.'
    }
    Assert-ScenarioMoveVarsFields -Fields @($Metadata.fields) `
        -Scenario $expectedScenario -SourceRunId ([string]$Metadata.source_run_id)

    $post = $Metadata.post_movevars_first_payload
    Assert-ExactProperties -Value $post -Allowed @(
        'opcode32_offset', 'opcode32_body_bytes', 'opcode5_offset',
        'opcode5_body_encoding', 'opcode5_value', 'opcode39_offset',
        'opcode39_count', 'opcode39_message_bytes', 'opcode39_block_bytes',
        'opcode39_block_sha256', 'opcode39_declared_size_encoding',
        'opcode39_declared_size_counts', 'opcode9_controls', 'opcode13',
        'opcode13_offset',
        'opcode13_remaining_body_bytes', 'opcode13_body_unconsumed') `
        -Label 'post_movevars_first_payload'
    $expectedOpcode32 = [int]$movevars.byte_offset + [int]$movevars.message_bytes
    if ([int]$post.opcode32_offset -ne $expectedOpcode32 -or
        [int]$post.opcode32_body_bytes -ne 2 -or
        [int]$post.opcode5_offset -ne $expectedOpcode32 + 3 -or
        $post.opcode5_body_encoding -cne 'u16le' -or
        [int]$post.opcode5_value -ne 1 -or
        [int]$post.opcode39_offset -ne $expectedOpcode32 + 6 -or
        [int]$post.opcode39_count -ne 37 -or
        [int]$post.opcode39_message_bytes -ne 19 -or
        [int]$post.opcode39_block_bytes -ne 703 -or
        $post.opcode39_block_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        $post.opcode39_declared_size_encoding -cne 'i8-twos-complement' -or
        [int]$post.opcode13 -ne 13 -or
        $post.opcode13_body_unconsumed -cne $true) {
        throw 'Confirmed first-payload post-movevars controls are invalid.'
    }
    Assert-Opcode39DeclaredSizeCounts `
        -Counts @($post.opcode39_declared_size_counts)
    $controlCursor = [int]$post.opcode39_offset + [int]$post.opcode39_block_bytes
    $opcode9Controls = @($post.opcode9_controls)
    if ($opcode9Controls.Count -ne 3) {
        throw 'Projection must contain exactly three sanitized opcode-9 controls.'
    }
    for ($index = 0; $index -lt $opcode9Controls.Count; ++$index) {
        $control = $opcode9Controls[$index]
        Assert-ExactProperties -Value $control -Allowed @(
            'opcode', 'byte_offset', 'string_bytes', 'string_sha256',
            'bytes_consumed') -Label "opcode9_controls[$index]"
        if ([int]$control.opcode -ne 9 -or [int]$control.byte_offset -ne $controlCursor -or
            [int]$control.string_bytes -lt 0 -or [int]$control.string_bytes -gt 1024 -or
            [int]$control.bytes_consumed -ne [int]$control.string_bytes + 2 -or
            $control.string_sha256 -cnotmatch '^[0-9a-f]{64}$') {
            throw "Sanitized opcode-9 control $index is invalid."
        }
        $controlCursor += [int]$control.bytes_consumed
    }
    if ([int]$post.opcode13_offset -ne $controlCursor -or
        [int]$post.opcode13_remaining_body_bytes -ne
            $firstPayloadBytes - ([int]$post.opcode13_offset + 1) -or
        [int]$post.opcode13_remaining_body_bytes -lt 1) {
        throw 'Neutral opcode-13 boundary cursor/accounting is invalid.'
    }

    $transition = $Metadata.stock_reference_transition
    Assert-ExactProperties -Value $transition -Allowed @(
        'stock_client_sendres_observed', 'exact_sendres_prefix_bytes',
        'first_reliable_body_bytes', 'second_transfer_observed_after_sendres',
        'captured_transfer_count', 'additional_unprojected_transfer_count',
        'project_generated_sendres', 'project_resource_response_generated') `
        -Label 'stock_reference_transition'
    if ($transition.stock_client_sendres_observed -cne $true -or
        [int]$transition.exact_sendres_prefix_bytes -ne 9 -or
        [int]$transition.first_reliable_body_bytes -lt 9 -or
        [int]$transition.first_reliable_body_bytes -gt 4096 -or
        $transition.second_transfer_observed_after_sendres -cne $true -or
        [int]$transition.captured_transfer_count -lt 2 -or
        [int]$transition.captured_transfer_count -gt 3 -or
        [int]$transition.additional_unprojected_transfer_count -ne
            [int]$transition.captured_transfer_count - 2 -or
        $transition.project_generated_sendres -cne $false -or
        $transition.project_resource_response_generated -cne $false) {
        throw 'Stock-only sendres transition metadata is invalid.'
    }

    $second = $Metadata.post_sendres_transfer
    Assert-ExactProperties -Value $second -Allowed @(
        'payload_bytes', 'opcode45_offset', 'opcode45_body_bytes',
        'opcode45_layout', 'opcode45_map_start_ordinal',
        'opcode45_fixed_zero', 'bytes_consumed_before_boundary') `
        -Label 'post_sendres_transfer'
    if ([int]$second.payload_bytes -lt 11 -or
        [int]$second.payload_bytes -gt $maximumPayloadBytes -or
        [int]$second.opcode45_offset -ne 0 -or
        [int]$second.opcode45_body_bytes -ne 8 -or
        $second.opcode45_layout -cne
            'u32le-map-start-ordinal-plus-u32le-confirmed-zero' -or
        [uint32]$second.opcode45_map_start_ordinal -ne [uint32]$expectedOrdinal -or
        [uint32]$second.opcode45_fixed_zero -ne 0 -or
        [int]$second.bytes_consumed_before_boundary -ne 9) {
        throw 'Post-sendres numeric opcode-45 metadata is invalid.'
    }

    $boundary = $Metadata.post_sendres_observed_boundary
    Assert-ExactProperties -Value $boundary -Allowed @(
        'opcode', 'byte_offset', 'remaining_body_bytes', 'direction',
        'category', 'evidence_status', 'semantic_name', 'typed_resource_list',
        'pinned_public_numeric_constant_present', 'body_unconsumed') `
        -Label 'post_sendres_observed_boundary'
    if ([int]$boundary.opcode -ne 43 -or [int]$boundary.byte_offset -ne 9 -or
        [int]$boundary.remaining_body_bytes -ne [int]$second.payload_bytes - 10 -or
        $boundary.direction -cne 'server_message' -or
        $boundary.category -cne 'stock-observed-post-sendres-opcode-43' -or
        $boundary.evidence_status -cne
            'strong-stock-behavior-public-numeric-constant-cross-check-pending' -or
        $null -ne $boundary.semantic_name -or
        $boundary.typed_resource_list -cne $false -or
        $boundary.pinned_public_numeric_constant_present -cne $false -or
        $boundary.body_unconsumed -cne $true) {
        throw 'Neutral post-sendres opcode-43 boundary metadata is invalid.'
    }

    $stability = $Metadata.stability
    Assert-ExactProperties -Value $stability -Allowed @(
        'accepted_run_count', 'clean_restart_baseline_runs',
        'accepted_runs_per_controlled_variant', 'accepted_different_map_runs',
        'accepted_same_process_map_change_runs', 'maps_compared',
        'opcode45_boundary_offset_stable') -Label 'stability'
    if ([int]$stability.accepted_run_count -ne 28 -or
        [int]$stability.clean_restart_baseline_runs -ne 6 -or
        [int]$stability.accepted_runs_per_controlled_variant -ne 2 -or
        [int]$stability.accepted_different_map_runs -ne 2 -or
        [int]$stability.accepted_same_process_map_change_runs -ne 2 -or
        $stability.opcode45_boundary_offset_stable -cne $true -or
        @($stability.maps_compared).Count -ne 3 -or
        [string]$stability.maps_compared[0] -cne 'boot_camp' -or
        [string]$stability.maps_compared[1] -cne 'crossfire' -or
        [string]$stability.maps_compared[2] -cne 'stalkyard') {
        throw 'Projection stability ledger is invalid.'
    }
}

if ($PSCmdlet.ParameterSetName -eq 'ValidateMetadataSet') {
    $setRoot = Resolve-ExplicitDirectory -Path $ValidateMetadataSetRoot `
        -Label 'movevars projection set'
    if ($setRoot -cne $projectionRoot) {
        throw 'Projection-set validation requires the exact ignored projection root.'
    }
    Assert-NoDescendantReparsePoint -Path $setRoot `
        -Label 'movevars projection set'
    if (@(Get-ChildItem -LiteralPath $setRoot -Force -File).Count -ne 0) {
        throw 'Movevars projection root must not contain files.'
    }
    $directories = @(Get-ChildItem -LiteralPath $setRoot -Force -Directory |
        Sort-Object Name)
    if ($directories.Count -ne 28) {
        throw 'Movevars projection set must contain exactly 28 accepted runs.'
    }
    $scenarioCounts = @{}
    $evidenceGroupCounts = @{}
    $evidenceGroupFingerprints = @{}
    foreach ($directory in $directories) {
        $metadataPath = Join-Path $directory.FullName 'metadata.json'
        Assert-MetadataOnlyDirectory -MetadataPath $metadataPath `
            -Label "movevars projection '$($directory.Name)'"
        if ((Get-Item -LiteralPath $metadataPath).Length -gt $maximumMetadataBytes) {
            throw 'Movevars projection metadata exceeds its safety bound.'
        }
        $metadata = Get-Content -Raw -LiteralPath $metadataPath |
            ConvertFrom-Json -ErrorAction Stop
        Assert-ProjectionMetadata -Metadata $metadata
        if ($metadata.source_run_id -cne $directory.Name) {
            throw 'Projection source identifier must match its directory.'
        }
        $scenario = [string]$metadata.scenario
        if (-not $scenarioCounts.ContainsKey($scenario)) {
            $scenarioCounts[$scenario] = 0
        }
        ++$scenarioCounts[$scenario]
        $evidenceGroup = if ($scenario -ceq 'map-change') {
            'map-change:' + [string]$metadata.map_context.requested_target_map
        }
        else { $scenario }
        if (-not $evidenceGroupCounts.ContainsKey($evidenceGroup)) {
            $evidenceGroupCounts[$evidenceGroup] = 0
            $evidenceGroupFingerprints[$evidenceGroup] =
                [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        }
        ++$evidenceGroupCounts[$evidenceGroup]
        $fingerprint = (@($metadata.fields | ForEach-Object {
                    '{0}={1}' -f $_.name, $_.observed_value
                }) -join '|')
        [void]$evidenceGroupFingerprints[$evidenceGroup].Add($fingerprint)
    }
    $expectedCounts = @{
        baseline = 6
        'gravity-400' = 2
        'maxspeed-320' = 2
        'accelerate-12' = 2
        'airaccelerate-15' = 2
        'friction-6' = 2
        'stepsize-24' = 2
        'maxvelocity-3000' = 2
        'footsteps-0' = 2
        'sky-night' = 2
        'different-map' = 2
        'map-change' = 2
    }
    if ($scenarioCounts.Count -ne $expectedCounts.Count) {
        throw 'Movevars projection scenario coverage is incomplete.'
    }
    foreach ($scenario in $expectedCounts.Keys) {
        if (-not $scenarioCounts.ContainsKey($scenario) -or
            $scenarioCounts[$scenario] -ne $expectedCounts[$scenario]) {
            throw "Movevars scenario '$scenario' has an invalid accepted-run count."
        }
    }
    $expectedEvidenceGroupCounts = @{
        baseline = 6
        'gravity-400' = 2
        'maxspeed-320' = 2
        'accelerate-12' = 2
        'airaccelerate-15' = 2
        'friction-6' = 2
        'stepsize-24' = 2
        'maxvelocity-3000' = 2
        'footsteps-0' = 2
        'sky-night' = 2
        'different-map' = 2
        'map-change:crossfire' = 1
        'map-change:stalkyard' = 1
    }
    if ($evidenceGroupCounts.Count -ne $expectedEvidenceGroupCounts.Count) {
        throw 'Movevars target-aware evidence grouping is incomplete.'
    }
    foreach ($group in $expectedEvidenceGroupCounts.Keys) {
        if (-not $evidenceGroupCounts.ContainsKey($group) -or
            $evidenceGroupCounts[$group] -ne $expectedEvidenceGroupCounts[$group] -or
            $evidenceGroupFingerprints[$group].Count -ne 1) {
            throw "Movevars evidence group '$group' has an invalid deterministic profile."
        }
    }
    Write-Output (
        'metadata-set-valid runs=28 baselines=6 fields=26 movevars=44 ' +
        'map-change-target-groups=2 first-boundary=13 ' +
        'second-observed-boundary=43-neutral')
    return
}

if ($PSCmdlet.ParameterSetName -eq 'ValidateMetadata') {
    $metadataPath = Resolve-ExplicitFile -Path $ValidateMetadataPath `
        -Label 'movevars metadata'
    Assert-PathBelowRoot -Path $metadataPath -Root $projectionRoot `
        -Label 'movevars metadata'
    if ((Get-Item -LiteralPath $metadataPath).Name -cne 'metadata.json' -or
        (Get-Item -LiteralPath $metadataPath).Length -gt $maximumMetadataBytes) {
        throw 'Movevars metadata path/name/size is invalid.'
    }
    Assert-MetadataOnlyDirectory -MetadataPath $metadataPath `
        -Label 'movevars projection directory'
    $metadata = Get-Content -Raw -LiteralPath $metadataPath |
        ConvertFrom-Json -ErrorAction Stop
    Assert-ProjectionMetadata -Metadata $metadata
    Write-Output ((
        'metadata-valid {0} scenario={1} movevars={2}@{3} first-boundary=13@{4} ' +
        'second-observed-boundary=43@9-neutral') -f
        $metadata.source_run_id, $metadata.scenario, $metadata.movevars.opcode,
        $metadata.movevars.byte_offset,
        $metadata.post_movevars_first_payload.opcode13_offset)
    return
}

$sourceDirectory = Resolve-ExplicitDirectory -Path $SourceRunDirectory `
    -Label 'source stock run'
Assert-PathBelowRoot -Path $sourceDirectory -Root $captureRoot `
    -Label 'source stock run'
if ([IO.Path]::GetFullPath((Split-Path -Parent $sourceDirectory)) -cne $captureRoot) {
    throw 'Source stock run must be an immediate child of the ignored capture root.'
}
$sourceRunId = Split-Path -Leaf $sourceDirectory
$scenario = Get-ScenarioProfile -SourceRunId $sourceRunId
$source = Assert-SourceCaptureContract -SourceDirectory $sourceDirectory `
    -SourceRunId $sourceRunId -Scenario $scenario
$firstPayload = [IO.File]::ReadAllBytes($source.FirstPayloadPath)
$secondPayload = [IO.File]::ReadAllBytes($source.SecondPayloadPath)
$first = Read-FirstServiceProjection -Payload $firstPayload `
    -CaptureMetadata $source.Capture -RunConfig $source.Config -Scenario $scenario
Assert-ScenarioMoveVarsFields -Fields @($first.fields) -Scenario $scenario `
    -SourceRunId $sourceRunId
$second = Read-SecondServiceProjection -Payload $secondPayload `
    -ExpectedOrdinal ([uint32]$first.map_start_ordinal)
$metadata = New-ProjectionMetadata -SourceRunId $sourceRunId `
    -Scenario $scenario -Source $source -First $first -Second $second
Assert-ProjectionMetadata -Metadata $metadata

if (-not (Test-Path -LiteralPath $projectionRoot)) {
    Assert-NoReparsePointInExistingPath -Path $projectionRoot `
        -Label 'movevars projection root'
    [void](New-Item -ItemType Directory -Path $projectionRoot)
}
$outputDirectory = [IO.Path]::GetFullPath((Join-Path $projectionRoot $sourceRunId))
Assert-PathBelowRoot -Path $outputDirectory -Root $projectionRoot `
    -Label 'movevars projection output'
if (Test-Path -LiteralPath $outputDirectory) {
    Assert-NoReparsePointInExistingPath -Path $outputDirectory `
        -Label 'movevars projection output'
    $existing = @(Get-ChildItem -LiteralPath $outputDirectory -Force)
    if ($existing.Count -gt 1 -or
        ($existing.Count -eq 1 -and
            ($existing[0].PSIsContainer -or $existing[0].Name -cne 'metadata.json'))) {
        throw 'Existing movevars projection contains an unexpected artifact.'
    }
    if ($existing.Count -eq 1) {
        if (($existing[0].Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Existing movevars projection metadata must not be a reparse point.'
        }
        Assert-OnlyDefaultDataStream -Path $existing[0].FullName `
            -Label 'existing movevars projection metadata'
    }
}
else {
    [void](New-Item -ItemType Directory -Path $outputDirectory)
}
$metadataPath = Join-Path $outputDirectory 'metadata.json'
$temporaryPath = Join-Path $outputDirectory 'metadata.json.tmp'
if (Test-Path -LiteralPath $temporaryPath) {
    throw 'Refusing to overwrite an unexpected temporary projection file.'
}
$json = $metadata | ConvertTo-Json -Depth 10
$encoding = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText($temporaryPath, $json + "`n", $encoding)
try {
    Move-Item -LiteralPath $temporaryPath -Destination $metadataPath -Force
}
finally {
    if (Test-Path -LiteralPath $temporaryPath) {
        Remove-Item -LiteralPath $temporaryPath -Force
    }
}
Assert-MetadataOnlyDirectory -MetadataPath $metadataPath `
    -Label 'movevars projection directory'
Write-Output ((
    'projection-valid {0} scenario={1} movevars=44@{2} body={3} ' +
    'first-boundary=13@{4} second-observed-boundary=43@9-neutral') -f
    $sourceRunId, $scenario, $metadata.movevars.byte_offset,
    $metadata.movevars.body_bytes,
    $metadata.post_movevars_first_payload.opcode13_offset)
