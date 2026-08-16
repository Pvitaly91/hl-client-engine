<#
.SYNOPSIS
Projects or validates bounded stock GoldSrc delta-description metadata.

.DESCRIPTION
Project mode reads one ignored, canonical decompressed service payload produced
by the accepted private-loopback sign-on capture workflow. It resumes at the
exact opcode-14 cursor published by the independently validated server-info
projection, parses the complete bounded delta-description sequence in memory,
and writes exactly one sanitized metadata.json below the ignored
manual-artifacts/delta-description-captures directory.

The projection contains selected public schema names, counts, masks, message
sizes, and hashes of canonical field definitions. It never stores field names,
raw payload bytes, authentication bytes, opaque server-info values, or the body
following opcode 44. ValidateMetadata mode performs the same strict schema and
profile checks without reading a raw payload.
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
$sourceRoot = [IO.Path]::GetFullPath((
    Join-Path $manualArtifactRoot 'signon-captures'))
$projectionRoot = [IO.Path]::GetFullPath((
    Join-Path $manualArtifactRoot 'delta-description-captures'))
$maximumPayloadBytes = 1048576
$maximumSchemas = 32
$maximumFieldsPerSchema = 256
$maximumTotalFields = 4096
$maximumNameBytes = 64
$expectedSchema = 'hlclient.stock-delta-description-metadata.v1'
$expectedProfile = 'stock-hl-1.1.1.1-to-stock-hlds-protocol48-build10210'
$allowedScenarios = @(
    'baseline',
    'different-map',
    'different-maxplayers',
    'first-client',
    'server-restart',
    'map-change',
    'changed-hostname')

$expectedSchemas = @(
    [pscustomobject]@{
        name = 'event_t'; field_count = 14; message_bytes = 380
        body_bits = 3032; padding_bits = 6
        field_type_masks = @('8', '2147483652', '2147483656')
        field_masks = @(127)
        field_definition_sha256 = '13decc0c313dfbf4051b391274e86aa5c864bfff95fd8ead5df81dbcdbccd54a'
    },
    [pscustomobject]@{
        name = 'weapon_data_t'; field_count = 20; message_bytes = 613
        body_bits = 4896; padding_bits = 4
        field_type_masks = @('4', '8', '2147483652', '2147483656')
        field_masks = @(123, 127)
        field_definition_sha256 = '10a9179bf3e173ce6f76837896353cff5e4d6b290a9ad274ac22ccf7292fe634'
    },
    [pscustomobject]@{
        name = 'usercmd_t'; field_count = 15; message_bytes = 454
        body_bits = 3624; padding_bits = 3
        field_type_masks = @('1', '2', '8', '16', '2147483652')
        field_masks = @(123, 127)
        field_definition_sha256 = 'b8c141bafd07bcdc0172134e05e5760cb7865b9e81ea1a2def4890ff1b132d1d'
    },
    [pscustomobject]@{
        name = 'custom_entity_state_t'; field_count = 19; message_bytes = 539
        body_bits = 4304; padding_bits = 7
        field_type_masks = @('1', '4', '8', '2147483652')
        field_masks = @(127)
        field_definition_sha256 = '6cd1de0f96888894295d01ecf62e786daa906835533c2bce054ad2d7782099bd'
    },
    [pscustomobject]@{
        name = 'entity_state_player_t'; field_count = 49; message_bytes = 1368
        body_bits = 10936; padding_bits = 5
        field_type_masks = @('1', '2', '4', '8', '16', '32', '2147483650', '2147483652')
        field_masks = @(127)
        field_definition_sha256 = 'a32ff3c622ead85132d14f6df32d4e31516467a7f61dfe8e445db7f9c1d7ff53'
    },
    [pscustomobject]@{
        name = 'entity_state_t'; field_count = 52; message_bytes = 1454
        body_bits = 11624; padding_bits = 4
        field_type_masks = @('1', '2', '4', '8', '16', '32', '64', '2147483650', '2147483652')
        field_masks = @(127)
        field_definition_sha256 = '190880fa1deddb3fc0767d4c88f4db0cdf93296ac19342afb58eb36d995dcf8e'
    },
    [pscustomobject]@{
        name = 'clientdata_t'; field_count = 50; message_bytes = 1386
        body_bits = 11080; padding_bits = 2
        field_type_masks = @('4', '8', '128', '2147483652', '2147483656')
        field_masks = @(123, 127)
        field_definition_sha256 = 'eef782b1b6a89b2175e5f8ae3e4c01a1aca4977ae1f25ec04bf5572e12384c1a'
    })

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

function Assert-ExactStringArray {
    param([object[]]$Actual, [string[]]$Expected, [string]$Label)
    if ($Actual.Count -ne $Expected.Count) {
        throw "$Label has an unexpected element count."
    }
    for ($index = 0; $index -lt $Expected.Count; ++$index) {
        if ([string]$Actual[$index] -cne $Expected[$index]) {
            throw "$Label differs at index $index."
        }
    }
}

function Assert-ExactIntegerArray {
    param([object[]]$Actual, [int[]]$Expected, [string]$Label)
    if ($Actual.Count -ne $Expected.Count) {
        throw "$Label has an unexpected element count."
    }
    for ($index = 0; $index -lt $Expected.Count; ++$index) {
        if ([int]$Actual[$index] -ne $Expected[$index]) {
            throw "$Label differs at index $index."
        }
    }
}

function Get-Sha256Hex {
    param([byte[]]$Bytes)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return (($sha.ComputeHash($Bytes) | ForEach-Object {
                    $_.ToString('x2')
                }) -join '')
    } finally {
        $sha.Dispose()
    }
}

function Get-TextSha256Hex {
    param([string]$Text)
    return Get-Sha256Hex -Bytes ([Text.Encoding]::UTF8.GetBytes($Text))
}

function Read-DeltaProjection {
    param([byte[]]$Payload, [int]$BoundaryOffset)

    if ($Payload.Length -eq 0 -or $Payload.Length -gt $maximumPayloadBytes) {
        throw 'Canonical service payload is outside the project safety bound.'
    }
    if ($BoundaryOffset -lt 0 -or $BoundaryOffset -ge $Payload.Length -or
        $Payload[$BoundaryOffset] -ne 14) {
        throw 'The supplied exact boundary is not opcode 14.'
    }

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
        $typeMasks = [Collections.Generic.HashSet[string]]::new(
            [StringComparer]::Ordinal)
        $fieldMasks = [Collections.Generic.HashSet[int]]::new()
        for ($fieldIndex = 0; $fieldIndex -lt $fieldCount; ++$fieldIndex) {
            $maskByteCount = [int](Read-BoundedBits -Width 3)
            if ($maskByteCount -ne 1) {
                throw 'Unexpected delta field-mask byte count.'
            }
            $mask = [int](Read-BoundedBits -Width 8)
            if ($mask -ne 0x7F -and $mask -ne 0x7B) {
                throw 'Unknown or incomplete delta field mask.'
            }
            [void]$fieldMasks.Add($mask)

            $fieldType = [uint32](Read-BoundedBits -Width 32)
            $fieldName = Read-BoundedName
            $fieldOffset = if (($mask -band 0x04) -ne 0) {
                [int](Read-BoundedBits -Width 16)
            } else { 0 }
            $storageSize = [int](Read-BoundedBits -Width 8)
            $significantBits = [int](Read-BoundedBits -Width 8)
            $premultiplyWire = [uint32](Read-BoundedBits -Width 32)
            $postmultiplyWire = [uint32](Read-BoundedBits -Width 32)
            if ($storageSize -ne 1 -or $significantBits -lt 1 -or
                $significantBits -gt 32 -or $premultiplyWire -eq 0 -or
                $postmultiplyWire -eq 0) {
                throw 'Delta numeric metadata violates the confirmed profile.'
            }
            [void]$typeMasks.Add(([uint64]$fieldType).ToString())
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
        $sortedTypeMasks = @($typeMasks | Sort-Object {
                [uint64]$_
            })
        $sortedFieldMasks = @($fieldMasks | Sort-Object)
        $schemas.Add([pscustomobject][ordered]@{
            index = $schemas.Count
            name = $schemaName
            schema_name_sha256 = Get-TextSha256Hex -Text $schemaName
            field_count = $fieldCount
            field_type_masks = $sortedTypeMasks
            field_masks = $sortedFieldMasks
            field_definition_sha256 = Get-TextSha256Hex -Text (
                $canonicalFields -join "`n")
            message_offset = $messageOpcodeOffset
            message_bits = ($nextOpcodeOffset * 8) - ($messageOpcodeOffset * 8)
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
                bits_consumed = $reader.Cursor - (($BoundaryOffset + 1) * 8)
                bytes_consumed = $nextOpcodeOffset - $BoundaryOffset
                next_opcode = $nextOpcode
                next_opcode_offset = $nextOpcodeOffset
                remaining_body_bytes = $Payload.Length - ($nextOpcodeOffset + 1)
            }
        }
    }
}

function Assert-ProjectionProfile {
    param([object]$Projection)
    if ($Projection.schemas.Count -ne $expectedSchemas.Count -or
        [int]$Projection.total_fields -ne 219 -or
        [int]$Projection.bytes_consumed -ne 6194 -or
        [int]$Projection.next_opcode -ne 44 -or
        [int]$Projection.remaining_body_bytes -lt 1) {
        throw 'Delta projection does not match the confirmed aggregate profile.'
    }
    for ($index = 0; $index -lt $expectedSchemas.Count; ++$index) {
        $actual = $Projection.schemas[$index]
        $expected = $expectedSchemas[$index]
        if ($actual.name -cne $expected.name -or
            [int]$actual.field_count -ne $expected.field_count -or
            [int]$actual.message_bytes -ne $expected.message_bytes -or
            [int]$actual.body_bits -ne $expected.body_bits -or
            [int]$actual.padding_bits -ne $expected.padding_bits -or
            $actual.field_definition_sha256 -cne
                $expected.field_definition_sha256) {
            throw "Delta schema profile mismatch at index $index."
        }
        Assert-ExactStringArray -Actual @($actual.field_type_masks) `
            -Expected $expected.field_type_masks `
            -Label "delta schema $index type masks"
        Assert-ExactIntegerArray -Actual @($actual.field_masks) `
            -Expected $expected.field_masks `
            -Label "delta schema $index field masks"
    }
}

function Assert-Metadata {
    param([object]$Metadata)
    Assert-ExactProperties -Value $Metadata -Allowed @(
        'schema', 'profile', 'scenario', 'completion', 'source_run_id',
        'versions', 'safety', 'delta_stream', 'schemas',
        'post_delta_boundary', 'stability') -Label 'metadata'
    if ($Metadata.schema -cne $expectedSchema -or
        $Metadata.profile -cne $expectedProfile -or
        $Metadata.completion -cne 'bounded_complete' -or
        $allowedScenarios -cnotcontains [string]$Metadata.scenario -or
        [string]$Metadata.source_run_id -notmatch '^[A-Za-z0-9_-]{1,128}$') {
        throw 'Metadata identity or completion is invalid.'
    }

    Assert-ExactProperties -Value $Metadata.versions -Allowed @(
        'client_versioninfo', 'server_launcher_versioninfo',
        'server_protocol', 'server_build', 'client_signature_valid',
        'server_signature_valid') -Label 'versions'
    if ($Metadata.versions.client_versioninfo -cne '1.1.1.1' -or
        $Metadata.versions.server_launcher_versioninfo -cne '4.1.1.1' -or
        [int]$Metadata.versions.server_protocol -ne 48 -or
        [int]$Metadata.versions.server_build -ne 10210 -or
        $Metadata.versions.client_signature_valid -cne $true -or
        $Metadata.versions.server_signature_valid -cne $true) {
        throw 'Stock version profile is invalid.'
    }

    Assert-ExactProperties -Value $Metadata.safety -Allowed @(
        'private_ipv4_loopback', 'byte_preserving_relay',
        'same_upstream_socket', 'exact_endpoints', 'hard_bounds',
        'source_payload_gitignored', 'projection_metadata_only',
        'raw_packet_bytes_stored', 'raw_service_payload_stored',
        'authentication_bytes_stored', 'field_names_stored',
        'post_delta_body_stored') -Label 'safety'
    foreach ($name in @(
            'private_ipv4_loopback', 'byte_preserving_relay',
            'same_upstream_socket', 'exact_endpoints', 'hard_bounds',
            'source_payload_gitignored', 'projection_metadata_only')) {
        if ($Metadata.safety.$name -cne $true) {
            throw "Safety property '$name' must be true."
        }
    }
    foreach ($name in @(
            'raw_packet_bytes_stored', 'raw_service_payload_stored',
            'authentication_bytes_stored', 'field_names_stored',
            'post_delta_body_stored')) {
        if ($Metadata.safety.$name -cne $false) {
            throw "Safety property '$name' must be false."
        }
    }

    Assert-ExactProperties -Value $Metadata.delta_stream -Allowed @(
        'opcode', 'semantic_category', 'semantic_evidence', 'byte_offset',
        'bit_order', 'schema_name_encoding', 'field_count_encoding',
        'field_definition_layout', 'multiplier_encoding', 'schema_count',
        'total_field_count', 'bits_consumed', 'bytes_consumed') `
        -Label 'delta_stream'
    if ([int]$Metadata.delta_stream.opcode -ne 14 -or
        $Metadata.delta_stream.semantic_category -cne 'delta_description' -or
        $Metadata.delta_stream.semantic_evidence -cne
            'stock_capture_and_public_valve_hlsdk_cross_check' -or
        [int]$Metadata.delta_stream.byte_offset -lt 0 -or
        $Metadata.delta_stream.bit_order -cne
            'least-significant-bit-first-within-byte' -or
        $Metadata.delta_stream.schema_name_encoding -cne
            'byte-aligned-ascii-nul' -or
        $Metadata.delta_stream.field_count_encoding -cne 'u16le' -or
        $Metadata.delta_stream.field_definition_layout -cne
            'lsb-bitpacked-conditional-mask' -or
        $Metadata.delta_stream.multiplier_encoding -cne
            'u32-fixed-point-divisor-4000' -or
        [int]$Metadata.delta_stream.schema_count -ne 7 -or
        [int]$Metadata.delta_stream.total_field_count -ne 219 -or
        [int]$Metadata.delta_stream.bytes_consumed -ne 6194 -or
        [int64]$Metadata.delta_stream.bits_consumed -ne
            [int64]$Metadata.delta_stream.bytes_consumed * 8) {
        throw 'Delta stream profile is invalid.'
    }

    if (@($Metadata.schemas).Count -ne $expectedSchemas.Count) {
        throw 'Metadata schema count is invalid.'
    }
    $expectedMessageOffset = [int]$Metadata.delta_stream.byte_offset
    for ($index = 0; $index -lt $expectedSchemas.Count; ++$index) {
        $actual = $Metadata.schemas[$index]
        $expected = $expectedSchemas[$index]
        Assert-ExactProperties -Value $actual -Allowed @(
            'index', 'name', 'schema_name_sha256', 'field_count',
            'field_type_masks', 'field_masks', 'field_definition_sha256',
            'message_offset', 'message_bits', 'body_bits', 'message_bytes',
            'padding_bits') -Label "schemas[$index]"
        if ([int]$actual.index -ne $index -or $actual.name -cne $expected.name -or
            $actual.schema_name_sha256 -cne
                (Get-TextSha256Hex -Text $expected.name) -or
            [int]$actual.field_count -ne $expected.field_count -or
            [int]$actual.message_bytes -ne $expected.message_bytes -or
            [int]$actual.message_offset -ne $expectedMessageOffset -or
            [int64]$actual.message_bits -ne
                [int64]$actual.message_bytes * 8 -or
            [int]$actual.body_bits -ne $expected.body_bits -or
            [int]$actual.padding_bits -ne $expected.padding_bits -or
            $actual.field_definition_sha256 -cne
                $expected.field_definition_sha256) {
            throw "Metadata schema profile mismatch at index $index."
        }
        Assert-ExactStringArray -Actual @($actual.field_type_masks) `
            -Expected $expected.field_type_masks `
            -Label "schemas[$index].field_type_masks"
        Assert-ExactIntegerArray -Actual @($actual.field_masks) `
            -Expected $expected.field_masks `
            -Label "schemas[$index].field_masks"
        $expectedMessageOffset += [int]$actual.message_bytes
    }

    Assert-ExactProperties -Value $Metadata.post_delta_boundary -Allowed @(
        'opcode', 'byte_offset', 'remaining_body_bytes', 'direction',
        'category', 'evidence_status', 'body_unconsumed') `
        -Label 'post_delta_boundary'
    if ([int]$Metadata.post_delta_boundary.opcode -ne 44 -or
        [int]$Metadata.post_delta_boundary.byte_offset -ne
            $expectedMessageOffset -or
        [int]$Metadata.post_delta_boundary.remaining_body_bytes -lt 1 -or
        $Metadata.post_delta_boundary.direction -cne 'server_message' -or
        $Metadata.post_delta_boundary.category -cne
            'stock_observed_opcode_44' -or
        $Metadata.post_delta_boundary.evidence_status -cne
            'stock_confirmed_opcode_44_body_unconsumed' -or
        $Metadata.post_delta_boundary.body_unconsumed -cne $true) {
        throw 'Post-delta boundary is invalid or overclaimed.'
    }

    Assert-ExactProperties -Value $Metadata.stability -Allowed @(
        'schema_order_stable', 'field_definitions_stable',
        'maps_compared', 'maximum_clients_compared',
        'clean_restart_runs', 'same_process_map_change_runs',
        'accepted_projection_count') -Label 'stability'
    if ($Metadata.stability.schema_order_stable -cne $true -or
        $Metadata.stability.field_definitions_stable -cne $true -or
        @($Metadata.stability.maps_compared).Count -ne 2 -or
        @($Metadata.stability.maximum_clients_compared).Count -ne 3 -or
        [int]$Metadata.stability.clean_restart_runs -lt 2 -or
        [int]$Metadata.stability.same_process_map_change_runs -lt 1 -or
        [int]$Metadata.stability.accepted_projection_count -ne 16) {
        throw 'Cross-run stability ledger is invalid.'
    }
    Assert-ExactStringArray -Actual @($Metadata.stability.maps_compared) `
        -Expected @('maps/boot_camp.bsp', 'maps/crossfire.bsp') `
        -Label 'stability.maps_compared'
    Assert-ExactIntegerArray `
        -Actual @($Metadata.stability.maximum_clients_compared) `
        -Expected @(1, 2, 8) `
        -Label 'stability.maximum_clients_compared'
}

if ($PSCmdlet.ParameterSetName -eq 'ValidateMetadataSet') {
    $setRoot = Resolve-ExplicitDirectory -Path $ValidateMetadataSetRoot `
        -Label 'delta projection set'
    if ($setRoot -cne $projectionRoot) {
        throw 'Projection-set validation requires the exact ignored delta root.'
    }
    Assert-NoDescendantReparsePoint -Path $setRoot `
        -Label 'delta projection set'
    $unexpectedRootFiles = @(Get-ChildItem -LiteralPath $setRoot -Force -File |
        Where-Object { $_.Name -cne 'analyze-stock-delta.ps1' })
    if ($unexpectedRootFiles.Count -ne 0) {
        throw 'Delta projection root contains an unexpected file.'
    }
    $directories = @(Get-ChildItem -LiteralPath $setRoot -Force -Directory |
        Sort-Object Name)
    if ($directories.Count -ne 16) {
        throw 'Delta projection set must contain exactly 16 accepted runs.'
    }
    $scenarioCounts = @{}
    foreach ($directory in $directories) {
        $metadataPath = Join-Path $directory.FullName 'metadata.json'
        Assert-MetadataOnlyDirectory -MetadataPath $metadataPath `
            -Label "delta projection '$($directory.Name)'"
        $metadata = Get-Content -LiteralPath $metadataPath -Raw |
            ConvertFrom-Json -ErrorAction Stop
        Assert-Metadata -Metadata $metadata
        if ($metadata.source_run_id -cne $directory.Name) {
            throw 'Projection source identifier must match its directory.'
        }
        $scenario = [string]$metadata.scenario
        if (-not $scenarioCounts.ContainsKey($scenario)) {
            $scenarioCounts[$scenario] = 0
        }
        ++$scenarioCounts[$scenario]
    }
    $expectedScenarioCounts = @{
        baseline = 6
        'first-client' = 1
        'different-map' = 2
        'different-maxplayers' = 4
        'changed-hostname' = 2
        'map-change' = 1
    }
    if ($scenarioCounts.Count -ne $expectedScenarioCounts.Count) {
        throw 'Projection-set scenario coverage is incomplete.'
    }
    foreach ($scenario in $expectedScenarioCounts.Keys) {
        if (-not $scenarioCounts.ContainsKey($scenario) -or
            $scenarioCounts[$scenario] -ne $expectedScenarioCounts[$scenario]) {
            throw "Projection-set count for '$scenario' is invalid."
        }
    }
    Write-Output 'metadata-set-valid runs=16 schemas=7 fields=219 boundary=44'
    return
}

if ($PSCmdlet.ParameterSetName -eq 'ValidateMetadata') {
    $metadataPath = Resolve-ExplicitFile -Path $ValidateMetadataPath `
        -Label 'delta metadata'
    Assert-PathBelowRoot -Path $metadataPath -Root $projectionRoot `
        -Label 'delta metadata'
    Assert-MetadataOnlyDirectory -MetadataPath $metadataPath `
        -Label 'delta projection directory'
    $metadata = Get-Content -LiteralPath $metadataPath -Raw |
        ConvertFrom-Json -ErrorAction Stop
    Assert-Metadata -Metadata $metadata
    Write-Output ("metadata-valid {0} schemas={1} fields={2} boundary={3}@{4}" -f
        $metadata.source_run_id, $metadata.delta_stream.schema_count,
        $metadata.delta_stream.total_field_count,
        $metadata.post_delta_boundary.opcode,
        $metadata.post_delta_boundary.byte_offset)
    return
}

$sourceDirectory = Resolve-ExplicitDirectory -Path $SourceRunDirectory `
    -Label 'source stock run'
Assert-PathBelowRoot -Path $sourceDirectory -Root $sourceRoot `
    -Label 'source stock run'
$sourceRunId = Split-Path -Leaf $sourceDirectory
if ($sourceRunId -notmatch '^[A-Za-z0-9_-]{1,128}$') {
    throw 'Source run directory has an unsafe identifier.'
}
$payloadPath = Resolve-ExplicitFile -Path (
    Join-Path $sourceDirectory 'research-service-payload.bin') `
    -Label 'ignored canonical service payload'
$sourceMetadataPath = Resolve-ExplicitFile -Path (
    Join-Path $sourceDirectory 'serverinfo-projection\metadata.json') `
    -Label 'source server-info projection'
$sourceMetadata = Get-Content -LiteralPath $sourceMetadataPath -Raw |
    ConvertFrom-Json -ErrorAction Stop
if ($sourceMetadata.schema -cne 'hlclient.stock-serverinfo-metadata.v1' -or
    $sourceMetadata.profile -cne $expectedProfile -or
    $sourceMetadata.completion -cne 'bounded_complete' -or
    $allowedScenarios -cnotcontains [string]$sourceMetadata.scenario -or
    $sourceMetadata.process_contract.raw_packet_bytes_stored -cne $false -or
    $sourceMetadata.process_contract.raw_service_payload_stored -cne $false -or
    $sourceMetadata.process_contract.raw_research_artifacts_ignored -cne $true -or
    $sourceMetadata.process_contract.loopback_only -cne $true -or
    $sourceMetadata.process_contract.byte_preserving_relay -cne $true -or
    $sourceMetadata.process_contract.same_upstream_socket -cne $true -or
    $sourceMetadata.process_contract.exact_server_endpoint_validation -cne $true -or
    [int]$sourceMetadata.transport.maximum_packets -gt 320 -or
    [int]$sourceMetadata.transport.maximum_post_accept_packets -gt 280 -or
    [int]$sourceMetadata.transport.maximum_datagram_bytes -gt 2048 -or
    [int64]$sourceMetadata.transport.maximum_total_bytes -gt 524288 -or
    [int]$sourceMetadata.transport.timeout_seconds -gt 45 -or
    $sourceMetadata.resource_boundary.opcode -ne 14 -or
    $sourceMetadata.resource_boundary.body_unconsumed -cne $true) {
    throw 'Source server-info projection is not an accepted bounded input.'
}

$payload = [IO.File]::ReadAllBytes($payloadPath)
$boundaryOffset = [int]$sourceMetadata.resource_boundary.byte_offset
$projection = Read-DeltaProjection -Payload $payload `
    -BoundaryOffset $boundaryOffset
Assert-ProjectionProfile -Projection $projection

$metadata = [pscustomobject][ordered]@{
    schema = $expectedSchema
    profile = $expectedProfile
    scenario = [string]$sourceMetadata.scenario
    completion = 'bounded_complete'
    source_run_id = $sourceRunId
    versions = [pscustomobject][ordered]@{
        client_versioninfo = [string]$sourceMetadata.versions.client_versioninfo
        server_launcher_versioninfo =
            [string]$sourceMetadata.versions.server_launcher_versioninfo
        server_protocol = [int]$sourceMetadata.versions.server_protocol
        server_build = [int]$sourceMetadata.versions.server_build
        client_signature_valid =
            [bool]$sourceMetadata.versions.client_signature_valid
        server_signature_valid =
            [bool]$sourceMetadata.versions.server_signature_valid
    }
    safety = [pscustomobject][ordered]@{
        private_ipv4_loopback = $true
        byte_preserving_relay = $true
        same_upstream_socket = $true
        exact_endpoints = $true
        hard_bounds = $true
        source_payload_gitignored = $true
        projection_metadata_only = $true
        raw_packet_bytes_stored = $false
        raw_service_payload_stored = $false
        authentication_bytes_stored = $false
        field_names_stored = $false
        post_delta_body_stored = $false
    }
    delta_stream = [pscustomobject][ordered]@{
        opcode = 14
        semantic_category = 'delta_description'
        semantic_evidence =
            'stock_capture_and_public_valve_hlsdk_cross_check'
        byte_offset = $boundaryOffset
        bit_order = 'least-significant-bit-first-within-byte'
        schema_name_encoding = 'byte-aligned-ascii-nul'
        field_count_encoding = 'u16le'
        field_definition_layout = 'lsb-bitpacked-conditional-mask'
        multiplier_encoding = 'u32-fixed-point-divisor-4000'
        schema_count = $projection.schemas.Count
        total_field_count = $projection.total_fields
        bits_consumed = $projection.bits_consumed
        bytes_consumed = $projection.bytes_consumed
    }
    schemas = $projection.schemas
    post_delta_boundary = [pscustomobject][ordered]@{
        opcode = $projection.next_opcode
        byte_offset = $projection.next_opcode_offset
        remaining_body_bytes = $projection.remaining_body_bytes
        direction = 'server_message'
        category = 'stock_observed_opcode_44'
        evidence_status = 'stock_confirmed_opcode_44_body_unconsumed'
        body_unconsumed = $true
    }
    stability = [pscustomobject][ordered]@{
        schema_order_stable = $true
        field_definitions_stable = $true
        maps_compared = @('maps/boot_camp.bsp', 'maps/crossfire.bsp')
        maximum_clients_compared = @(1, 2, 8)
        clean_restart_runs = 6
        same_process_map_change_runs = 1
        accepted_projection_count = 16
    }
}
Assert-Metadata -Metadata $metadata

if (-not (Test-Path -LiteralPath $projectionRoot)) {
    Assert-NoReparsePointInExistingPath -Path $projectionRoot `
        -Label 'delta projection root'
    [void](New-Item -ItemType Directory -Path $projectionRoot)
}
$outputDirectory = [IO.Path]::GetFullPath((Join-Path $projectionRoot $sourceRunId))
Assert-PathBelowRoot -Path $outputDirectory -Root $projectionRoot `
    -Label 'delta projection output'
if (Test-Path -LiteralPath $outputDirectory) {
    Assert-NoReparsePointInExistingPath -Path $outputDirectory `
        -Label 'delta projection output'
    $existing = @(Get-ChildItem -LiteralPath $outputDirectory -Force)
    if ($existing.Count -gt 1 -or
        ($existing.Count -eq 1 -and
            ($existing[0].PSIsContainer -or $existing[0].Name -cne 'metadata.json'))) {
        throw 'Existing delta projection output contains an unexpected artifact.'
    }
} else {
    [void](New-Item -ItemType Directory -Path $outputDirectory)
}
$metadataPath = Join-Path $outputDirectory 'metadata.json'
$temporaryPath = Join-Path $outputDirectory 'metadata.json.tmp'
if (Test-Path -LiteralPath $temporaryPath) {
    throw 'Refusing to overwrite an unexpected temporary projection file.'
}
$json = $metadata | ConvertTo-Json -Depth 8
$encoding = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText($temporaryPath, $json + "`n", $encoding)
try {
    Move-Item -LiteralPath $temporaryPath -Destination $metadataPath -Force
} finally {
    if (Test-Path -LiteralPath $temporaryPath) {
        Remove-Item -LiteralPath $temporaryPath -Force
    }
}
Assert-MetadataOnlyDirectory -MetadataPath $metadataPath `
    -Label 'delta projection directory'
Write-Output ("projection-valid {0} schemas={1} fields={2} boundary={3}@{4}" -f
    $sourceRunId, $metadata.delta_stream.schema_count,
    $metadata.delta_stream.total_field_count,
    $metadata.post_delta_boundary.opcode,
    $metadata.post_delta_boundary.byte_offset)
