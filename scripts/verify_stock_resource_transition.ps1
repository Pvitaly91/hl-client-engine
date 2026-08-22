#requires -Version 5.1

[CmdletBinding(DefaultParameterSetName = 'Project')]
param(
    [Parameter(ParameterSetName = 'Project')]
    [switch]$ProjectEvidenceSet,

    [Parameter(Mandatory = $true, ParameterSetName = 'Validate')]
    [string]$ValidateMetadataPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'ValidateSet')]
    [string]$ValidateMetadataSetRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$scriptPath = [IO.Path]::GetFullPath($MyInvocation.MyCommand.Path)
$repoRoot = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $scriptPath) '..'))
$manualRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'manual-artifacts'))
$movevarsRoot = [IO.Path]::GetFullPath((Join-Path $manualRoot 'movevars-captures'))
$signonRoot = [IO.Path]::GetFullPath((Join-Path $manualRoot 'signon-captures'))
$resourceRoot = [IO.Path]::GetFullPath((Join-Path $manualRoot 'resource-transition-captures'))
$projectionRoot = [IO.Path]::GetFullPath((Join-Path $resourceRoot 'projections'))
$projectionPath = [IO.Path]::GetFullPath((Join-Path $projectionRoot 'metadata.json'))
$maximumFirstPayloadBytes = 65536
$maximumSecondPayloadBytes = 262144
$maximumInfoStringBytes = 1024
$maximumMetadataBytes = 262144
$requestBytes = [byte[]]@(
    0x03, 0x73, 0x65, 0x6e, 0x64, 0x72, 0x65, 0x73, 0x00)
$requestSha256 = '1A9D246FF6AA7E401524881AEC9414F76A91AA6166BEF43D67DA0C0F9DFBC035'

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

function Assert-NoReparsePoint {
    param([string]$Path, [string]$Label)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label must not be a reparse point."
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

function Assert-SafeRunId {
    param([string]$Value)
    if ($Value -cnotmatch
        '^m(?:242|244|311)-[a-z0-9-]{1,96}-202608(?:16|22)-[0-9]{6}-[0-9]{3}$') {
        throw 'Projection source run ID is outside the sanitized allowlist profile.'
    }
}

function Assert-PathBelowRoot {
    param([string]$Path, [string]$Root, [string]$Label)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($fullRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label is outside its exact ignored root."
    }
}

function Resolve-SourceDirectory {
    param([string]$Root, [string]$RunId)
    $candidate = [IO.Path]::GetFullPath((Join-Path $Root $RunId))
    Assert-PathBelowRoot -Path $candidate -Root $Root -Label 'source run'
    if ([IO.Path]::GetFullPath((Split-Path -Parent $candidate)) -cne
        [IO.Path]::GetFullPath($Root)) {
        throw 'Source run must be an immediate child of its ignored capture root.'
    }
    if (-not (Test-Path -LiteralPath $candidate -PathType Container)) {
        throw "Required ignored source run is absent: $RunId"
    }
    Assert-NoReparsePoint -Path $candidate -Label 'source run'
    return $candidate
}

function Resolve-SourceFile {
    param([string]$Directory, [string]$Name, [int]$MaximumBytes)
    $candidate = [IO.Path]::GetFullPath((Join-Path $Directory $Name))
    Assert-PathBelowRoot -Path $candidate -Root $Directory -Label 'source file'
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "Required source file is absent: $Name"
    }
    Assert-NoReparsePoint -Path $candidate -Label 'source file'
    $length = (Get-Item -LiteralPath $candidate -Force).Length
    if ($length -lt 1 -or $length -gt $MaximumBytes) {
        throw "Source file '$Name' is outside its byte bound."
    }
    return $candidate
}

function Read-JsonFile {
    param([string]$Directory, [string]$Name, [int]$MaximumBytes)
    $path = Resolve-SourceFile -Directory $Directory -Name $Name `
        -MaximumBytes $MaximumBytes
    return Get-Content -Raw -LiteralPath $path | ConvertFrom-Json -ErrorAction Stop
}

function Read-U32Le {
    param([byte[]]$Bytes, [int]$Offset)
    if ($Offset -lt 0 -or $Offset + 4 -gt $Bytes.Length) {
        throw 'u32le read exceeds the bounded source.'
    }
    return [uint32]([uint32]$Bytes[$Offset] -bor
        ([uint32]$Bytes[$Offset + 1] -shl 8) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 16) -bor
        ([uint32]$Bytes[$Offset + 3] -shl 24))
}

function Read-I32Le {
    param([byte[]]$Bytes, [int]$Offset)
    $copy = [byte[]]::new(4)
    [Array]::Copy($Bytes, $Offset, $copy, 0, 4)
    if (-not [BitConverter]::IsLittleEndian) { [Array]::Reverse($copy) }
    return [BitConverter]::ToInt32($copy, 0)
}

function Test-BytePrefix {
    param([byte[]]$Bytes, [byte[]]$Prefix)
    if ($Bytes.Length -lt $Prefix.Length) { return $false }
    for ($index = 0; $index -lt $Prefix.Length; ++$index) {
        if ($Bytes[$index] -ne $Prefix[$index]) { return $false }
    }
    return $true
}

function Assert-CaptureContract {
    param([object]$Capture, [bool]$RequireSecondPhase)
    if ($Capture.completion -cne 'bounded_complete' -or
        $Capture.loopback_only -cne $true -or
        $Capture.byte_preserving_relay -cne $true -or
        $Capture.same_upstream_socket -cne $true -or
        $Capture.exact_server_endpoint_validation -cne $true -or
        $Capture.raw_packet_bytes_stored -cne $false) {
        throw 'Source capture violates the bounded private-loopback contract.'
    }
    if ($RequireSecondPhase) {
        if ($null -eq $Capture.second_service_boundary -or
            [int]$Capture.second_service_boundary.first_opcode -ne 45 -or
            $Capture.second_service_boundary.first_opcode_body_unconsumed -cne $true) {
            throw 'Source capture lacks the bounded second service transfer.'
        }
    }
}

function Read-InfoProjection {
    param([byte[]]$Payload, [int]$Start, [int]$Length)
    if ($Length -lt 1 -or $Length -gt $maximumInfoStringBytes) {
        throw 'Opcode-13 info string is outside its verifier bound.'
    }
    for ($index = 0; $index -lt $Length; ++$index) {
        $value = $Payload[$Start + $index]
        if ($value -lt 0x20 -or $value -gt 0x7e) {
            throw 'Observed stock info string is not printable ASCII.'
        }
    }
    $text = [Text.Encoding]::ASCII.GetString($Payload, $Start, $Length)
    $parts = $text.Split([char]0x5c, [StringSplitOptions]::None)
    if ($parts.Count -lt 3 -or $parts[0].Length -ne 0 -or
        (($parts.Count - 1) % 2) -ne 0) {
        throw 'Opcode-13 info string lacks exact leading-backslash key/value grammar.'
    }
    $ordinalKeys = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    $foldedKeys = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    $safeNames = @('name', 'model', 'topcolor', 'bottomcolor')
    $safe = [Collections.Generic.List[object]]::new()
    $orderedNames = [Collections.Generic.List[string]]::new()
    $privateCount = 0
    for ($index = 1; $index -lt $parts.Count; $index += 2) {
        $key = [string]$parts[$index]
        $value = [string]$parts[$index + 1]
        if ($key.Length -lt 1 -or $key.Length -gt 64 -or
            $value.Length -lt 1 -or $value.Length -gt 256) {
            throw 'Observed stock info key/value is outside the verifier bound.'
        }
        if (-not $ordinalKeys.Add($key) -or -not $foldedKeys.Add($key)) {
            throw 'Observed stock info string contains a duplicate/case-collision.'
        }
        $orderedNames.Add($key)
        if ($safeNames -ccontains $key) {
            $safe.Add([pscustomobject][ordered]@{
                name = $key
                key_bytes = $key.Length
                value_bytes = $value.Length
            })
        }
        else { ++$privateCount }
    }
    $expectedNames = @(
        'bottomcolor', 'cl_autowepswitch', 'cl_dlmax', 'cl_lc', 'cl_lw',
        'cl_updaterate', 'hud_classautokill', 'model', 'topcolor',
        'esevcmmx', 'rate', 'name', '*sid')
    if ($orderedNames.Count -ne $expectedNames.Count) {
        throw 'Observed stock info entry count changed.'
    }
    foreach ($expectedName in $expectedNames) {
        if (-not $ordinalKeys.Contains($expectedName)) {
            throw 'Observed stock info key set changed.'
        }
    }
    return [pscustomobject]@{
        Text = $text
        SafeProjection = @($safe)
        EntryCount = $orderedNames.Count
        PrivateEntryCount = $privateCount
    }
}

function Read-Opcode13Sequence {
    param([byte[]]$Payload, [int]$StartOffset, [int]$ExpectedMessageCount)
    if ($StartOffset -lt 0 -or $StartOffset -ge $Payload.Length -or
        $Payload[$StartOffset] -ne 13) {
        throw 'Exact predeclared opcode-13 cursor is invalid.'
    }
    $cursor = $StartOffset
    $messages = [Collections.Generic.List[object]]::new()
    $privateUserIds = [Collections.Generic.List[int]]::new()
    while ($cursor -lt $Payload.Length) {
        $messageOffset = $cursor
        if ($Payload[$cursor] -ne 13) {
            throw 'First batch has a non-opcode-13 byte at the exact continuation cursor.'
        }
        if ($cursor + 6 -gt $Payload.Length) {
            throw 'Opcode-13 fixed prefix is truncated.'
        }
        $clientIndex = [int]$Payload[$cursor + 1]
        if ($clientIndex -lt 0 -or $clientIndex -gt 31) {
            throw 'Observed stock client index is outside the 0..31 profile.'
        }
        $userId = Read-I32Le -Bytes $Payload -Offset ($cursor + 2)
        if ($userId -le 0) {
            throw 'Observed active-client user ID is not positive i32le.'
        }
        $infoStart = $cursor + 6
        $searchEnd = [Math]::Min(
            $Payload.Length, $infoStart + $maximumInfoStringBytes + 1)
        $terminator = -1
        for ($index = $infoStart; $index -lt $searchEnd; ++$index) {
            if ($Payload[$index] -eq 0) {
                $terminator = $index
                break
            }
        }
        if ($terminator -lt 0) {
            throw 'Opcode-13 info string lacks a bounded NUL terminator.'
        }
        $infoLength = $terminator - $infoStart
        $info = Read-InfoProjection -Payload $Payload -Start $infoStart `
            -Length $infoLength
        $digestOffset = $terminator + 1
        if ($digestOffset + 16 -gt $Payload.Length) {
            throw 'Opcode-13 fixed opaque suffix is truncated.'
        }
        $cursor = $digestOffset + 16
        $privateUserIds.Add($userId)
        $messages.Add([pscustomobject][ordered]@{
            byte_offset = $messageOffset
            body_bytes = $cursor - $messageOffset - 1
            message_bytes = $cursor - $messageOffset
            client_index = $clientIndex
            user_id_encoding = 'positive-i32le-private'
            user_id_redacted = $true
            info_string_offset = 6
            info_string_bytes = $infoLength
            info_string_terminator_bytes = 1
            info_entry_count = $info.EntryCount
            safe_keys = @($info.SafeProjection)
            private_key_count = $info.PrivateEntryCount
            opaque_suffix_offset = $digestOffset - $messageOffset
            opaque_suffix_bytes = 16
            opaque_suffix_private = $true
        })
    }
    if ($cursor -ne $Payload.Length -or
        $messages.Count -ne $ExpectedMessageCount) {
        throw 'Opcode-13 sequence does not terminate exactly at first-batch end.'
    }
    $indexes = @($messages | ForEach-Object { [int]$_.client_index })
    $unique = [Collections.Generic.HashSet[int]]::new()
    foreach ($index in $indexes) {
        if (-not $unique.Add($index)) {
            throw 'Observed stock first batch repeats a client index.'
        }
    }
    return [pscustomobject]@{
        Projection = [pscustomobject][ordered]@{
            opcode = 13
            semantic_name = 'user_info_update'
            first_opcode_offset = $StartOffset
            message_count = $messages.Count
            messages = @($messages)
            bytes_consumed = $Payload.Length - $StartOffset
            final_cursor = $Payload.Length
            remaining_bytes = 0
            terminal_condition = 'exact-end-of-first-service-payload'
            no_opcode_scanning = $true
        }
        PrivateUserIds = @($privateUserIds)
    }
}

function Get-TransitionRequestProjection {
    param([string]$Directory, [object]$Capture, [string]$Mode)
    $candidateFiles = @(Get-ChildItem -LiteralPath $Directory -Force -File |
        Where-Object { $_.Name -match '^research-post-boundary-client-[0-9]{3}\.bin$' } |
        Sort-Object Name)
    if ($candidateFiles.Count -lt 1 -or $candidateFiles.Count -gt 64) {
        throw 'Post-boundary reliable-body file count is outside its verifier bound.'
    }
    $matches = [Collections.Generic.List[object]]::new()
    foreach ($file in $candidateFiles) {
        Assert-NoReparsePoint -Path $file.FullName -Label 'post-boundary body'
        if ($file.Length -lt 1 -or $file.Length -gt 4096) {
            throw 'Post-boundary reliable body is outside its verifier bound.'
        }
        $body = [IO.File]::ReadAllBytes($file.FullName)
        if (Test-BytePrefix -Bytes $body -Prefix $requestBytes) {
            $matches.Add([pscustomobject]@{
                file = $file.Name
                body_bytes = $body.Length
                request_bytes = $requestBytes.Length
                contemporaneous_tail_bytes = $body.Length - $requestBytes.Length
            })
        }
    }
    $expectedTransmissions = if ($Mode -ceq 'drop-request') { 2 } else { 1 }
    if ($matches.Count -ne $expectedTransmissions) {
        throw 'Exact sendres-prefix transmission count is invalid.'
    }
    foreach ($match in $matches) {
        if ($match.request_bytes -ne 9 -or
            $match.contemporaneous_tail_bytes -ne 28) {
            throw 'Transition request prefix/tail classification changed.'
        }
    }
    $lifecycle = 'single-reliable-transmission'
    if ($Mode -ceq 'drop-request') {
        $transmissions = @($Capture.transition_request_transmissions)
        if ($transmissions.Count -ne 2 -or
            $transmissions[0].forwarded -cne $false -or
            $transmissions[1].forwarded -cne $true -or
            [uint64]$transmissions[1].sequence -le
                [uint64]$transmissions[0].sequence) {
            throw 'Dropped transition request did not use stock retransmission.'
        }
        $lifecycle = 'first-datagram-dropped-transport-retransmitted'
    }
    elseif ($Mode -ceq 'drop-ack') {
        $transmissions = @($Capture.transition_request_transmissions)
        $acks = @($Capture.transition_request_acknowledgements)
        if ($transmissions.Count -ne 1 -or $acks.Count -lt 2 -or
            $acks[0].forwarded -cne $false -or $acks[1].forwarded -cne $true -or
            $acks[0].reliable_ack -cne $false -or
            [uint64]$acks[0].acknowledgement -lt
                [uint64]$transmissions[0].sequence) {
            throw 'Dropped transition ACK lifecycle is invalid.'
        }
        $lifecycle = 'first-covering-ack-dropped-next-covering-ack-completed'
    }
    elseif ($Mode -ceq 'duplicate') {
        if ($Capture.duplicate_transition_datagram_forwarded -cne $true -or
            @($Capture.transition_request_transmissions).Count -ne 1 -or
            @($Capture.transfers).Count -ne 2) {
            throw 'Duplicate transition datagram was not suppressed semantically.'
        }
        $lifecycle = 'one-datagram-forwarded-twice-one-second-transfer'
    }
    return [pscustomobject][ordered]@{
        semantic_name = 'send_resources_request'
        opcode = 3
        command_ascii = 'sendres'
        command_case = 'lowercase-exact'
        terminator = 'single-nul'
        request_bytes = 9
        padding_bytes = 0
        canonical_sha256 = $requestSha256
        contemporaneous_tail_bytes = 28
        tail_classification = 'not-part-of-semantic-request'
        reliable_transmission_count = $matches.Count
        lifecycle = $lifecycle
    }
}

function Get-SecondTransferProjection {
    param([string]$Directory, [object]$Capture, [uint32]$ExpectedOrdinal)
    $path = Resolve-SourceFile -Directory $Directory `
        -Name 'research-resource-service-payload.bin' `
        -MaximumBytes $maximumSecondPayloadBytes
    $stream = [IO.File]::OpenRead($path)
    $prefix = [byte[]]::new(10)
    try {
        if ($stream.Length -lt 10 -or $stream.Length -gt $maximumSecondPayloadBytes) {
            throw 'Second service payload is outside its verifier bound.'
        }
        $read = $stream.Read($prefix, 0, $prefix.Length)
        if ($read -ne $prefix.Length) {
            throw 'Second service prefix is truncated.'
        }
        $payloadLength = [int]$stream.Length
    }
    finally { $stream.Dispose() }
    if ($prefix[0] -ne 45 -or $prefix[9] -ne 43) {
        throw 'Second service prefix is not exact opcode45/body/opcode43.'
    }
    $ordinal = Read-U32Le -Bytes $prefix -Offset 1
    $opaqueZero = Read-U32Le -Bytes $prefix -Offset 5
    if ($ordinal -ne $ExpectedOrdinal -or $opaqueZero -ne 0) {
        throw 'Opcode-45 fixed control fields changed.'
    }
    $transfer = @($Capture.transfers | Where-Object { [int]$_.ordinal -eq 2 })
    if ($transfer.Count -ne 1 -or [int]$transfer[0].declared_count -ne 6 -or
        [int]$transfer[0].reassembled_bytes -ne
            [int]$Capture.second_service_boundary.compressed_transfer_bytes -or
        [int]$Capture.second_service_boundary.service_payload_bytes -ne
            $payloadLength -or
        $Capture.second_service_boundary.envelope -cne
            'BZ2-NUL-plus-standard-bzip2' -or
        [int]$Capture.second_service_boundary.trailing_compressed_bytes -ne 0) {
        throw 'Second transfer fragmentation/compression metadata is invalid.'
    }
    return [pscustomobject][ordered]@{
        envelope = 'BZ2-NUL-plus-standard-bzip2'
        fragment_count = 6
        compressed_transfer_bytes = [int]$transfer[0].reassembled_bytes
        decompressed_payload_bytes = $payloadLength
        opcode45 = [pscustomobject][ordered]@{
            opcode = 45
            byte_offset = 0
            body_bytes = 8
            first_field_offset = 1
            first_field_encoding = 'u32le'
            first_field_semantic = 'map-start-ordinal'
            first_field_value = [int]$ordinal
            second_field_offset = 5
            second_field_encoding = 'u32le'
            second_field_observation = 'stable-zero-opaque'
            second_field_public = $false
            bytes_consumed = 9
        }
        opcode43_boundary = [pscustomobject][ordered]@{
            opcode = 43
            byte_offset = 9
            remaining_body_bytes = $payloadLength - 10
            body_unconsumed = $true
            body_bytes_read_by_verifier = 0
            semantic_gate = 'pending-no-pinned-numeric-opcode-constant'
            typed_resource_list = $false
        }
    }
}

function New-RunDescriptor {
    param(
        [string]$RootKind,
        [string]$RunId,
        [string]$Scenario,
        [int]$Opcode13Offset,
        [int]$MessageCount,
        [bool]$SecondPhase,
        [uint32]$MapOrdinal,
        [string]$TransitionMode,
        [bool]$RconCorrelation)
    return [pscustomobject]@{
        RootKind = $RootKind
        RunId = $RunId
        Scenario = $Scenario
        Opcode13Offset = $Opcode13Offset
        MessageCount = $MessageCount
        SecondPhase = $SecondPhase
        MapOrdinal = $MapOrdinal
        TransitionMode = $TransitionMode
        RconCorrelation = $RconCorrelation
    }
}

function Get-RootByKind {
    param([string]$Kind)
    switch ($Kind) {
        'movevars' { return $movevarsRoot }
        'signon' { return $signonRoot }
        'resource' { return $resourceRoot }
        default { throw 'Unknown source-root kind.' }
    }
}

function Project-AcceptedRun {
    param([object]$Descriptor)
    $root = Get-RootByKind -Kind $Descriptor.RootKind
    $directory = Resolve-SourceDirectory -Root $root -RunId $Descriptor.RunId
    $capture = Read-JsonFile -Directory $directory -Name 'metadata.json' `
        -MaximumBytes $maximumMetadataBytes
    Assert-CaptureContract -Capture $capture `
        -RequireSecondPhase $Descriptor.SecondPhase
    $payloadPath = Resolve-SourceFile -Directory $directory `
        -Name 'research-service-payload.bin' `
        -MaximumBytes $maximumFirstPayloadBytes
    $payload = [IO.File]::ReadAllBytes($payloadPath)
    $sequence = Read-Opcode13Sequence -Payload $payload `
        -StartOffset $Descriptor.Opcode13Offset `
        -ExpectedMessageCount $Descriptor.MessageCount
    $rconConfirmed = $false
    if ($Descriptor.RconCorrelation) {
        $config = Read-JsonFile -Directory $directory `
            -Name 'research-run-config.json' -MaximumBytes 16384
        $firstIds = @($config.first_client_user_ids)
        $postIds = @($config.post_capture_user_ids)
        $privateIds = @($sequence.PrivateUserIds)
        if ([int]$config.first_client_active_count -ne 1 -or
            [int]$config.post_capture_active_count -ne 2 -or
            $firstIds.Count -ne 1 -or $privateIds.Count -ne 2 -or
            [int]$firstIds[0] -ne [int]$privateIds[0] -or
            ([int]$privateIds[1] - [int]$privateIds[0]) -ne 1 -or
            @($postIds | Where-Object { [int]$_ -eq [int]$privateIds[1] }).Count -ne 1) {
            throw 'Private RCON userid correlation failed.'
        }
        $messages = @($sequence.Projection.messages)
        if ([int]$messages[0].client_index -ne 0 -or
            [int]$messages[1].client_index -ne 1) {
            throw 'Controlled zero-based client-index evidence failed.'
        }
        $rconConfirmed = $true
    }
    $request = $null
    $second = $null
    if ($Descriptor.SecondPhase) {
        $request = Get-TransitionRequestProjection -Directory $directory `
            -Capture $capture -Mode $Descriptor.TransitionMode
        $second = Get-SecondTransferProjection -Directory $directory `
            -Capture $capture -ExpectedOrdinal $Descriptor.MapOrdinal
    }
    return [pscustomobject][ordered]@{
        source_run_id = $Descriptor.RunId
        scenario = $Descriptor.Scenario
        evidence_status = 'accepted'
        first_payload_bytes = $payload.Length
        user_info_sequence = $sequence.Projection
        user_id_rcon_semantic_confirmation = $rconConfirmed
        resource_transition_request = $request
        second_transfer = $second
    }
}

function Get-FirstInfoPrivateTable {
    param([string]$Directory, [int]$Opcode13Offset)
    $path = Resolve-SourceFile -Directory $Directory `
        -Name 'research-service-payload.bin' `
        -MaximumBytes $maximumFirstPayloadBytes
    $bytes = [IO.File]::ReadAllBytes($path)
    if ($bytes[$Opcode13Offset] -ne 13) { throw 'Rejected-run opcode13 cursor changed.' }
    $start = $Opcode13Offset + 6
    $terminator = -1
    for ($index = $start; $index -lt [Math]::Min($bytes.Length, $start + 1025); ++$index) {
        if ($bytes[$index] -eq 0) { $terminator = $index; break }
    }
    if ($terminator -lt 0) { throw 'Rejected-run info string is unterminated.' }
    $info = Read-InfoProjection -Payload $bytes -Start $start `
        -Length ($terminator - $start)
    $parts = $info.Text.Split([char]0x5c, [StringSplitOptions]::None)
    $table = @{}
    for ($index = 1; $index -lt $parts.Count; $index += 2) {
        $table[$parts[$index]] = $parts[$index + 1]
    }
    return $table
}

function Project-RejectedControl {
    param([string]$RunId, [string]$Control, [string]$Key, [string]$ExpectedValue)
    $directory = Resolve-SourceDirectory -Root $resourceRoot -RunId $RunId
    $capture = Read-JsonFile -Directory $directory -Name 'metadata.json' `
        -MaximumBytes $maximumMetadataBytes
    Assert-CaptureContract -Capture $capture -RequireSecondPhase $true
    $table = Get-FirstInfoPrivateTable -Directory $directory -Opcode13Offset 7273
    if (-not $table.ContainsKey($Key) -or $table[$Key] -ceq $ExpectedValue) {
        throw 'A rejected controlled user-info attempt unexpectedly applied.'
    }
    return [pscustomobject][ordered]@{
        source_run_id = $RunId
        scenario = $Control
        evidence_status = 'rejected-requested-value-not-observed'
        attempted_key_name = $Key
        attempted_value_bytes = $ExpectedValue.Length
        raw_value_projected = $false
    }
}

function Project-IncompleteRun {
    param([string]$RunId, [string]$Scenario, [string]$ExpectedError)
    $directory = Resolve-SourceDirectory -Root $resourceRoot -RunId $RunId
    $capture = Read-JsonFile -Directory $directory -Name 'metadata.json' `
        -MaximumBytes $maximumMetadataBytes
    if ($capture.schema -cne 'hlclient.stock-initial-signon-failure.v1' -or
        $capture.raw_packet_bytes_stored -cne $false -or
        $capture.safe_error -cne $ExpectedError) {
        throw 'Incomplete-run ledger entry changed.'
    }
    return [pscustomobject][ordered]@{
        source_run_id = $RunId
        scenario = $Scenario
        evidence_status = 'incomplete-not-accepted'
        safe_error_category = if ($Scenario -ceq 'concurrent-second-client') {
            'no-connect-request'
        } else { 'bounded-proof-condition-not-reached' }
    }
}

function Assert-ProjectionMetadata {
    param([object]$Metadata)
    Assert-ExactProperties -Value $Metadata -Allowed @(
        'schema', 'profile', 'verifier_sha256', 'methodology',
        'public_header_crosscheck', 'accepted_runs',
        'rejected_controlled_runs', 'incomplete_runs', 'summary',
        'raw_payload_bytes_stored', 'raw_userinfo_values_stored',
        'raw_identity_fields_stored', 'opcode43_body_parsed',
        'resource_response_generated') -Label 'projection'
    Assert-ExactProperties -Value $Metadata.methodology -Allowed @(
        'transport', 'byte_preserving_relay', 'one_upstream_socket',
        'exact_endpoint_validation', 'packet_byte_time_bounds',
        'first_batch_cursor_policy', 'second_batch_read_policy',
        'pinned_header_role') -Label 'methodology'
    Assert-ExactProperties -Value $Metadata.public_header_crosscheck -Allowed @(
        'user_id_signed_int', 'server_assigned_userid_comment',
        'resource_struct_and_size_function_present',
        'numeric_opcode43_constant_present') -Label 'public_header_crosscheck'
    Assert-ExactProperties -Value $Metadata.summary -Allowed @(
        'accepted_run_count', 'transition_run_count',
        'rejected_controlled_run_count', 'incomplete_run_count',
        'clean_baseline_runs', 'maxplayers_1_runs', 'maxplayers_8_runs',
        'reconnect_rcon_correlated_runs', 'user_id_rcon_correlated_runs',
        'dropped_request_runs', 'dropped_ack_runs',
        'duplicate_request_runs', 'different_map_runs',
        'same_process_map_change_runs', 'single_message_first_batches',
        'two_message_first_batches', 'opcode13_semantic',
        'request_semantic', 'request_sha256', 'opcode45_body_bytes',
        'opcode43_offset', 'opcode43_semantic_gate') -Label 'summary'
    if ($Metadata.schema -cne 'hlclient.stock-resource-transition-evidence.v1' -or
        $Metadata.profile -cne
            'stock-hl-1.1.1.1-to-stock-hlds-protocol48-build10210' -or
        $Metadata.verifier_sha256 -cne (Get-FileSha256Hex -Path $scriptPath) -or
        $Metadata.raw_payload_bytes_stored -cne $false -or
        $Metadata.raw_userinfo_values_stored -cne $false -or
        $Metadata.raw_identity_fields_stored -cne $false -or
        $Metadata.opcode43_body_parsed -cne $false -or
        $Metadata.resource_response_generated -cne $false) {
        throw 'Projection top-level safety/profile metadata is invalid.'
    }
    if ($Metadata.verifier_sha256 -cnotmatch '^[0-9A-F]{64}$') {
        throw 'Projection verifier hash is invalid.'
    }
    if ($Metadata.methodology.transport -cne 'private-ipv4-loopback-udp' -or
        $Metadata.methodology.byte_preserving_relay -cne $true -or
        $Metadata.methodology.one_upstream_socket -cne $true -or
        $Metadata.methodology.exact_endpoint_validation -cne $true -or
        $Metadata.methodology.packet_byte_time_bounds -cne $true -or
        $Metadata.methodology.first_batch_cursor_policy -cne
            'predeclared-exact-cursor-no-scan' -or
        $Metadata.methodology.second_batch_read_policy -cne
            'read-only-opcode45-body-and-opcode43-byte' -or
        $Metadata.methodology.pinned_header_role -cne
            'secondary-cross-check-only') {
        throw 'Projection methodology metadata is invalid.'
    }
    if ($Metadata.public_header_crosscheck.user_id_signed_int -cne $true -or
        $Metadata.public_header_crosscheck.server_assigned_userid_comment -cne
            $true -or
        $Metadata.public_header_crosscheck.resource_struct_and_size_function_present -cne
            $true -or
        $Metadata.public_header_crosscheck.numeric_opcode43_constant_present -cne
            $false) {
        throw 'Projection public-header cross-check metadata is invalid.'
    }
    $accepted = @($Metadata.accepted_runs)
    if ($accepted.Count -ne 22) { throw 'Projection must contain 22 accepted runs.' }
    $scenarioCounts = @{}
    foreach ($run in $accepted) {
        Assert-SafeRunId -Value ([string]$run.source_run_id)
        Assert-ExactProperties -Value $run -Allowed @(
            'source_run_id', 'scenario', 'evidence_status',
            'first_payload_bytes', 'user_info_sequence',
            'user_id_rcon_semantic_confirmation',
            'resource_transition_request', 'second_transfer') `
            -Label 'accepted_run'
        Assert-ExactProperties -Value $run.user_info_sequence -Allowed @(
            'opcode', 'semantic_name', 'first_opcode_offset', 'message_count',
            'messages', 'bytes_consumed', 'final_cursor', 'remaining_bytes',
            'terminal_condition', 'no_opcode_scanning') `
            -Label 'user_info_sequence'
        $messages = @($run.user_info_sequence.messages)
        if ($run.evidence_status -cne 'accepted' -or
            [int]$run.user_info_sequence.opcode -ne 13 -or
            $run.user_info_sequence.semantic_name -cne 'user_info_update' -or
            [int]$run.user_info_sequence.message_count -ne $messages.Count -or
            [int]$run.user_info_sequence.bytes_consumed -ne
                ([int]$run.first_payload_bytes -
                    [int]$run.user_info_sequence.first_opcode_offset) -or
            [int]$run.user_info_sequence.final_cursor -ne
                [int]$run.first_payload_bytes -or
            [int]$run.user_info_sequence.remaining_bytes -ne 0 -or
            $run.user_info_sequence.terminal_condition -cne
                'exact-end-of-first-service-payload' -or
            $run.user_info_sequence.no_opcode_scanning -cne $true) {
            throw 'Accepted run contains invalid user-info metadata.'
        }
        if (-not $scenarioCounts.ContainsKey([string]$run.scenario)) {
            $scenarioCounts[[string]$run.scenario] = 0
        }
        ++$scenarioCounts[[string]$run.scenario]
        $messageCursor = [int]$run.user_info_sequence.first_opcode_offset
        $seenIndexes = [Collections.Generic.HashSet[int]]::new()
        foreach ($message in $messages) {
            Assert-ExactProperties -Value $message -Allowed @(
                'byte_offset', 'body_bytes', 'message_bytes', 'client_index',
                'user_id_encoding', 'user_id_redacted', 'info_string_offset',
                'info_string_bytes', 'info_string_terminator_bytes',
                'info_entry_count', 'safe_keys', 'private_key_count',
                'opaque_suffix_offset', 'opaque_suffix_bytes',
                'opaque_suffix_private') -Label 'user_info_message'
            $safeKeys = @($message.safe_keys)
            foreach ($safeKey in $safeKeys) {
                Assert-ExactProperties -Value $safeKey -Allowed @(
                    'name', 'key_bytes', 'value_bytes') -Label 'safe_key'
                if (@('name', 'model', 'topcolor', 'bottomcolor') -cnotcontains
                        [string]$safeKey.name -or
                    [int]$safeKey.key_bytes -ne
                        ([string]$safeKey.name).Length -or
                    [int]$safeKey.value_bytes -lt 1 -or
                    [int]$safeKey.value_bytes -gt 256) {
                    throw 'Accepted run contains an invalid safe-key projection.'
                }
            }
            if ($safeKeys.Count -ne 4 -or
                @($safeKeys | ForEach-Object { [string]$_.name } |
                    Sort-Object -Unique).Count -ne 4 -or
                [int]$message.byte_offset -ne $messageCursor -or
                [int]$message.body_bytes -ne
                    ([int]$message.message_bytes - 1) -or
                [int]$message.info_string_offset -ne 6 -or
                [int]$message.info_string_bytes -lt 1 -or
                [int]$message.info_string_bytes -gt $maximumInfoStringBytes -or
                [int]$message.info_string_terminator_bytes -ne 1 -or
                [int]$message.opaque_suffix_offset -ne
                    (7 + [int]$message.info_string_bytes) -or
                [int]$message.message_bytes -ne
                    ([int]$message.opaque_suffix_offset + 16) -or
                [int]$message.client_index -lt 0 -or
                [int]$message.client_index -gt 31 -or
                -not $seenIndexes.Add([int]$message.client_index) -or
                $message.user_id_encoding -cne 'positive-i32le-private' -or
                $message.user_id_redacted -cne $true -or
                $message.opaque_suffix_private -cne $true -or
                [int]$message.opaque_suffix_bytes -ne 16 -or
                [int]$message.info_entry_count -ne 13 -or
                [int]$message.private_key_count -ne 9) {
                throw 'Accepted run exposes or misstates private opcode-13 fields.'
            }
            $messageCursor += [int]$message.message_bytes
        }
        if ($messageCursor -ne [int]$run.first_payload_bytes) {
            throw 'Accepted run user-info messages do not end at the first-batch boundary.'
        }
    }
    $expectedCounts = @{
        baseline = 6
        'different-map' = 2
        'same-process-map-change' = 2
        'maxplayers-1' = 2
        'maxplayers-8' = 2
        'reconnect-overlap' = 2
        'drop-transition-request' = 2
        'drop-transition-ack' = 2
        'duplicate-transition-request' = 2
    }
    if ($scenarioCounts.Count -ne $expectedCounts.Count) {
        throw 'Accepted scenario set is incomplete.'
    }
    foreach ($name in $expectedCounts.Keys) {
        if (-not $scenarioCounts.ContainsKey($name) -or
            $scenarioCounts[$name] -ne $expectedCounts[$name]) {
            throw "Accepted scenario count is invalid: $name"
        }
    }
    $transitionRuns = @($accepted | Where-Object {
            $null -ne $_.resource_transition_request })
    if ($transitionRuns.Count -ne 18) {
        throw 'Projection must contain 18 complete resource transitions.'
    }
    foreach ($run in $transitionRuns) {
        Assert-ExactProperties -Value $run.resource_transition_request -Allowed @(
            'semantic_name', 'opcode', 'command_ascii', 'command_case',
            'terminator', 'request_bytes', 'padding_bytes',
            'canonical_sha256', 'contemporaneous_tail_bytes',
            'tail_classification', 'reliable_transmission_count',
            'lifecycle') -Label 'resource_transition_request'
        Assert-ExactProperties -Value $run.second_transfer -Allowed @(
            'envelope', 'fragment_count', 'compressed_transfer_bytes',
            'decompressed_payload_bytes', 'opcode45', 'opcode43_boundary') `
            -Label 'second_transfer'
        Assert-ExactProperties -Value $run.second_transfer.opcode45 -Allowed @(
            'opcode', 'byte_offset', 'body_bytes', 'first_field_offset',
            'first_field_encoding', 'first_field_semantic',
            'first_field_value', 'second_field_offset',
            'second_field_encoding', 'second_field_observation',
            'second_field_public', 'bytes_consumed') -Label 'opcode45'
        Assert-ExactProperties -Value $run.second_transfer.opcode43_boundary `
            -Allowed @('opcode', 'byte_offset', 'remaining_body_bytes',
                'body_unconsumed', 'body_bytes_read_by_verifier',
                'semantic_gate', 'typed_resource_list') -Label 'opcode43_boundary'
        $expectedTransmissionCount = if ($run.scenario -ceq
            'drop-transition-request') { 2 } else { 1 }
        $expectedLifecycle = switch ([string]$run.scenario) {
            'drop-transition-request' {
                'first-datagram-dropped-transport-retransmitted'
            }
            'drop-transition-ack' {
                'first-covering-ack-dropped-next-covering-ack-completed'
            }
            'duplicate-transition-request' {
                'one-datagram-forwarded-twice-one-second-transfer'
            }
            default { 'single-reliable-transmission' }
        }
        $expectedMapOrdinal = if ($run.scenario -ceq
            'same-process-map-change') { 2 } else { 1 }
        if ($run.resource_transition_request.semantic_name -cne
                'send_resources_request' -or
            [int]$run.resource_transition_request.opcode -ne 3 -or
            $run.resource_transition_request.command_ascii -cne 'sendres' -or
            $run.resource_transition_request.command_case -cne
                'lowercase-exact' -or
            $run.resource_transition_request.terminator -cne 'single-nul' -or
            $run.resource_transition_request.canonical_sha256 -cne $requestSha256 -or
            [int]$run.resource_transition_request.request_bytes -ne 9 -or
            [int]$run.resource_transition_request.padding_bytes -ne 0 -or
            [int]$run.resource_transition_request.contemporaneous_tail_bytes -ne 28 -or
            $run.resource_transition_request.tail_classification -cne
                'not-part-of-semantic-request' -or
            [int]$run.resource_transition_request.reliable_transmission_count -ne
                $expectedTransmissionCount -or
            $run.resource_transition_request.lifecycle -cne $expectedLifecycle -or
            $run.second_transfer.envelope -cne
                'BZ2-NUL-plus-standard-bzip2' -or
            [int]$run.second_transfer.fragment_count -ne 6 -or
            [int]$run.second_transfer.compressed_transfer_bytes -lt 1 -or
            [int]$run.second_transfer.compressed_transfer_bytes -gt
                $maximumSecondPayloadBytes -or
            [int]$run.second_transfer.decompressed_payload_bytes -lt 10 -or
            [int]$run.second_transfer.decompressed_payload_bytes -gt
                $maximumSecondPayloadBytes -or
            [int]$run.second_transfer.opcode45.opcode -ne 45 -or
            [int]$run.second_transfer.opcode45.byte_offset -ne 0 -or
            [int]$run.second_transfer.opcode45.body_bytes -ne 8 -or
            [int]$run.second_transfer.opcode45.first_field_offset -ne 1 -or
            $run.second_transfer.opcode45.first_field_encoding -cne 'u32le' -or
            $run.second_transfer.opcode45.first_field_semantic -cne
                'map-start-ordinal' -or
            [int]$run.second_transfer.opcode45.first_field_value -ne
                $expectedMapOrdinal -or
            [int]$run.second_transfer.opcode45.second_field_offset -ne 5 -or
            $run.second_transfer.opcode45.second_field_encoding -cne 'u32le' -or
            $run.second_transfer.opcode45.second_field_observation -cne
                'stable-zero-opaque' -or
            $run.second_transfer.opcode45.second_field_public -cne $false -or
            [int]$run.second_transfer.opcode45.bytes_consumed -ne 9 -or
            [int]$run.second_transfer.opcode43_boundary.opcode -ne 43 -or
            [int]$run.second_transfer.opcode43_boundary.byte_offset -ne 9 -or
            [int]$run.second_transfer.opcode43_boundary.remaining_body_bytes -ne
                ([int]$run.second_transfer.decompressed_payload_bytes - 10) -or
            $run.second_transfer.opcode43_boundary.body_unconsumed -cne $true -or
            [int]$run.second_transfer.opcode43_boundary.body_bytes_read_by_verifier -ne 0 -or
            $run.second_transfer.opcode43_boundary.semantic_gate -cne
                'pending-no-pinned-numeric-opcode-constant' -or
            $run.second_transfer.opcode43_boundary.typed_resource_list -cne $false) {
            throw 'Transition projection violates exact request/control/boundary evidence.'
        }
    }
    $rejected = @($Metadata.rejected_controlled_runs)
    foreach ($run in $rejected) {
        Assert-SafeRunId -Value ([string]$run.source_run_id)
        Assert-ExactProperties -Value $run -Allowed @(
            'source_run_id', 'scenario', 'evidence_status',
            'attempted_key_name', 'attempted_value_bytes',
            'raw_value_projected') -Label 'rejected_controlled_run'
        if ($run.evidence_status -cne
                'rejected-requested-value-not-observed' -or
            @('name', 'model', 'topcolor', 'bottomcolor') -cnotcontains
                [string]$run.attempted_key_name -or
            [int]$run.attempted_value_bytes -lt 1 -or
            [int]$run.attempted_value_bytes -gt 256 -or
            $run.raw_value_projected -cne $false) {
            throw 'Rejected controlled run projects a raw value.'
        }
    }
    if ($rejected.Count -ne 11 -or
        @($rejected | Where-Object scenario -eq 'different-player-name').Count -ne 6 -or
        @($rejected | Where-Object scenario -eq 'different-player-model').Count -ne 1 -or
        @($rejected | Where-Object scenario -eq 'different-top-color').Count -ne 2 -or
        @($rejected | Where-Object scenario -eq 'different-bottom-color').Count -ne 2) {
        throw 'Rejected controlled-run ledger is incomplete.'
    }
    foreach ($run in @($Metadata.incomplete_runs)) {
        Assert-SafeRunId -Value ([string]$run.source_run_id)
        Assert-ExactProperties -Value $run -Allowed @(
            'source_run_id', 'scenario', 'evidence_status',
            'safe_error_category') -Label 'incomplete_run'
        if ($run.evidence_status -cne 'incomplete-not-accepted' -or
            @('concurrent-second-client', 'reconnect-overlap') -cnotcontains
                [string]$run.scenario -or
            @('no-connect-request', 'bounded-proof-condition-not-reached') -cnotcontains
                [string]$run.safe_error_category) {
            throw 'Incomplete-run ledger contains unsafe metadata.'
        }
    }
    if (@($Metadata.incomplete_runs).Count -ne 2 -or
        [int]$Metadata.summary.accepted_run_count -ne 22 -or
        [int]$Metadata.summary.transition_run_count -ne 18 -or
        [int]$Metadata.summary.rejected_controlled_run_count -ne 11 -or
        [int]$Metadata.summary.incomplete_run_count -ne 2 -or
        [int]$Metadata.summary.single_message_first_batches -ne 20 -or
        [int]$Metadata.summary.two_message_first_batches -ne 2 -or
        [int]$Metadata.summary.clean_baseline_runs -ne 6 -or
        [int]$Metadata.summary.maxplayers_1_runs -ne 2 -or
        [int]$Metadata.summary.maxplayers_8_runs -ne 2 -or
        [int]$Metadata.summary.reconnect_rcon_correlated_runs -ne 2 -or
        [int]$Metadata.summary.user_id_rcon_correlated_runs -ne 2 -or
        [int]$Metadata.summary.dropped_request_runs -ne 2 -or
        [int]$Metadata.summary.dropped_ack_runs -ne 2 -or
        [int]$Metadata.summary.duplicate_request_runs -ne 2 -or
        [int]$Metadata.summary.different_map_runs -ne 2 -or
        [int]$Metadata.summary.same_process_map_change_runs -ne 2 -or
        $Metadata.summary.opcode13_semantic -cne 'user_info_update' -or
        $Metadata.summary.request_semantic -cne 'send_resources_request' -or
        $Metadata.summary.request_sha256 -cne $requestSha256 -or
        [int]$Metadata.summary.opcode45_body_bytes -ne 8 -or
        [int]$Metadata.summary.opcode43_offset -ne 9 -or
        $Metadata.summary.opcode43_semantic_gate -cne
            'pending-no-pinned-numeric-opcode-constant') {
        throw 'Projection aggregate summary is invalid.'
    }
}

$gitIgnorePath = [IO.Path]::GetFullPath((Join-Path $repoRoot '.gitignore'))
if (-not (Test-Path -LiteralPath $gitIgnorePath -PathType Leaf) -or
    -not ((Get-Content -Raw -LiteralPath $gitIgnorePath).Contains('/manual-artifacts/'))) {
    throw 'Verifier requires the repository-wide manual-artifacts ignore rule.'
}
if ((Get-Sha256Hex -Bytes $requestBytes) -cne $requestSha256) {
    throw 'Independently authored request fixture hash changed.'
}
$eiface = Get-Content -Raw -LiteralPath (
    Join-Path $repoRoot 'third_party/halflife-sdk/engine/eiface.h')
$comModel = Get-Content -Raw -LiteralPath (
    Join-Path $repoRoot 'third_party/halflife-sdk/common/com_model.h')
$customHeader = Get-Content -Raw -LiteralPath (
    Join-Path $repoRoot 'third_party/halflife-sdk/engine/custom.h')
if ($eiface -notmatch 'pfnGetPlayerUserId' -or
    $eiface -notmatch 'server assigned userid' -or
    $comModel -notmatch 'int\s+userid\s*;' -or
    $customHeader -notmatch 'COM_SizeofResourceList' -or
    $customHeader -notmatch 'struct\s+resource_s') {
    throw 'Pinned public Valve-header secondary cross-check failed.'
}

if ($PSCmdlet.ParameterSetName -eq 'Validate' -or
    $PSCmdlet.ParameterSetName -eq 'ValidateSet') {
    $path = if ($PSCmdlet.ParameterSetName -eq 'ValidateSet') {
        $root = [IO.Path]::GetFullPath($ValidateMetadataSetRoot)
        if ($root -cne $projectionRoot) {
            throw 'Projection-set validation requires the exact ignored projection root.'
        }
        Assert-NoReparsePoint -Path $root -Label 'projection root'
        $unexpected = @(Get-ChildItem -LiteralPath $root -Force |
            Where-Object { $_.Name -cne 'metadata.json' })
        if ($unexpected.Count -ne 0) {
            throw 'Projection root contains an unexpected artifact.'
        }
        Join-Path $root 'metadata.json'
    }
    else { [IO.Path]::GetFullPath($ValidateMetadataPath) }
    if ($path -cne $projectionPath) {
        throw 'Metadata validation requires the exact ignored projection path.'
    }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
        (Get-Item -LiteralPath $path).Length -gt $maximumMetadataBytes) {
        throw 'Projection metadata is absent or outside its byte bound.'
    }
    Assert-NoReparsePoint -Path $path -Label 'projection metadata'
    $metadata = Get-Content -Raw -LiteralPath $path |
        ConvertFrom-Json -ErrorAction Stop
    Assert-ProjectionMetadata -Metadata $metadata
    Write-Output (
        'metadata-valid accepted=22 transitions=18 rejected-controls=11 ' +
        'incomplete=2 opcode13=user-info-update request=sendres ' +
        'opcode45=8B opcode43=neutral-unconsumed')
    return
}

$acceptedDescriptors = @(
    (New-RunDescriptor movevars 'm244-baseline-a-baseline-20260822-192245-924' baseline 7273 1 $true 1 baseline $false),
    (New-RunDescriptor movevars 'm244-baseline-b-baseline-20260822-192306-373' baseline 7273 1 $true 1 baseline $false),
    (New-RunDescriptor movevars 'm244-baseline-c-baseline-20260822-193238-908' baseline 7273 1 $true 1 baseline $false),
    (New-RunDescriptor movevars 'm244-baseline-d-baseline-20260822-193257-243' baseline 7273 1 $true 1 baseline $false),
    (New-RunDescriptor movevars 'm244-baseline-e-retry-baseline-20260822-193345-409' baseline 7273 1 $true 1 baseline $false),
    (New-RunDescriptor movevars 'm244-baseline-f-baseline-20260822-193404-122' baseline 7273 1 $true 1 baseline $false),
    (New-RunDescriptor movevars 'm244-map-crossfire-a-baseline-20260822-193103-612' 'different-map' 7273 1 $true 1 baseline $false),
    (New-RunDescriptor movevars 'm244-map-crossfire-b-baseline-20260822-193120-150' 'different-map' 7273 1 $true 1 baseline $false),
    (New-RunDescriptor movevars 'm244-mapchange-crossfire-a-baseline-20260822-193137-013' 'same-process-map-change' 7273 1 $true 2 baseline $false),
    (New-RunDescriptor movevars 'm244-mapchange-stalkyard-a-baseline-20260822-193156-253' 'same-process-map-change' 7273 1 $true 2 baseline $false),
    (New-RunDescriptor signon 'm242-max1-baseline-20260816-174131-556' 'maxplayers-1' 7198 1 $false 0 none $false),
    (New-RunDescriptor signon 'm242-max1-baseline-20260816-175018-787' 'maxplayers-1' 7193 1 $false 0 none $false),
    (New-RunDescriptor signon 'm242-max8-baseline-20260816-174157-548' 'maxplayers-8' 7278 1 $false 0 none $false),
    (New-RunDescriptor signon 'm242-max8-baseline-20260816-175126-377' 'maxplayers-8' 7273 1 $false 0 none $false),
    (New-RunDescriptor resource 'm311-reconnect-confirm-b-baseline-20260822-215821-543' 'reconnect-overlap' 7273 2 $true 1 baseline $true),
    (New-RunDescriptor resource 'm311-reconnect-confirm-c-baseline-20260822-215951-255' 'reconnect-overlap' 7273 2 $true 1 baseline $true),
    (New-RunDescriptor resource 'm311-drop-transition-request-a-droptransitionrequest-20260822-214129-321' 'drop-transition-request' 7273 1 $true 1 'drop-request' $false),
    (New-RunDescriptor resource 'm311-drop-transition-request-b-droptransitionrequest-20260822-220117-227' 'drop-transition-request' 7273 1 $true 1 'drop-request' $false),
    (New-RunDescriptor resource 'm311-drop-transition-ack-a-droptransitionack-20260822-214210-895' 'drop-transition-ack' 7273 1 $true 1 'drop-ack' $false),
    (New-RunDescriptor resource 'm311-drop-transition-ack-b-droptransitionack-20260822-220145-327' 'drop-transition-ack' 7273 1 $true 1 'drop-ack' $false),
    (New-RunDescriptor resource 'm311-duplicate-transition-request-a-duplicatetransitionrequest-20260822-214251-677' 'duplicate-transition-request' 7273 1 $true 1 duplicate $false),
    (New-RunDescriptor resource 'm311-duplicate-transition-request-b-duplicatetransitionrequest-20260822-220214-620' 'duplicate-transition-request' 7273 1 $true 1 duplicate $false)
)

$accepted = [Collections.Generic.List[object]]::new()
foreach ($descriptor in $acceptedDescriptors) {
    try {
        $accepted.Add((Project-AcceptedRun -Descriptor $descriptor))
    }
    catch {
        throw "Accepted source '$($descriptor.RunId)' failed: $($_.Exception.Message)"
    }
}

$rejected = [Collections.Generic.List[object]]::new()
$rejected.Add((Project-RejectedControl 'm311-name-alpha-a-baseline-20260822-213516-694' 'different-player-name' name M311Alpha))
$rejected.Add((Project-RejectedControl 'm311-name-alpha-b-baseline-20260822-213623-316' 'different-player-name' name M311Alpha))
$rejected.Add((Project-RejectedControl 'm311-name-beta-probe-baseline-20260822-213808-034' 'different-player-name' name M311Beta))
$rejected.Add((Project-RejectedControl 'm311-name-command-diagnostic-baseline-20260822-213916-018' 'different-player-name' name M311Beta))
$rejected.Add((Project-RejectedControl 'm311-runtime-name-probe-baseline-20260822-214558-871' 'different-player-name' name M311Gamma))
$rejected.Add((Project-RejectedControl 'm311-commandline-inspect-baseline-20260822-214655-573' 'different-player-name' name M311Delta))
$rejected.Add((Project-RejectedControl 'm311-model-barney-probe-baseline-20260822-214333-334' 'different-player-model' model barney))
$rejected.Add((Project-RejectedControl 'm311-topcolor90-probe-a-baseline-20260822-220320-984' 'different-top-color' topcolor 90))
$rejected.Add((Project-RejectedControl 'm311-topcolor90-probe-b-baseline-20260822-220405-288' 'different-top-color' topcolor 90))
$rejected.Add((Project-RejectedControl 'm311-bottomcolor120-probe-a-baseline-20260822-220438-429' 'different-bottom-color' bottomcolor 120))
$rejected.Add((Project-RejectedControl 'm311-bottomcolor120-probe-b-baseline-20260822-220509-818' 'different-bottom-color' bottomcolor 120))

$incomplete = @(
    (Project-IncompleteRun 'm311-second-client-probe-a-baseline-20260822-214815-186' 'concurrent-second-client' 'Bounded run ended before a connect request.'),
    (Project-IncompleteRun 'm311-reconnect-confirm-a-baseline-20260822-215644-888' 'reconnect-overlap' "Scenario 'Baseline' did not reach its bounded proof condition.")
)

$singleCount = @($accepted | Where-Object {
        [int]$_.user_info_sequence.message_count -eq 1 }).Count
$doubleCount = @($accepted | Where-Object {
        [int]$_.user_info_sequence.message_count -eq 2 }).Count
$metadata = [pscustomobject][ordered]@{
    schema = 'hlclient.stock-resource-transition-evidence.v1'
    profile = 'stock-hl-1.1.1.1-to-stock-hlds-protocol48-build10210'
    verifier_sha256 = Get-FileSha256Hex -Path $scriptPath
    methodology = [pscustomobject][ordered]@{
        transport = 'private-ipv4-loopback-udp'
        byte_preserving_relay = $true
        one_upstream_socket = $true
        exact_endpoint_validation = $true
        packet_byte_time_bounds = $true
        first_batch_cursor_policy = 'predeclared-exact-cursor-no-scan'
        second_batch_read_policy = 'read-only-opcode45-body-and-opcode43-byte'
        pinned_header_role = 'secondary-cross-check-only'
    }
    public_header_crosscheck = [pscustomobject][ordered]@{
        user_id_signed_int = $true
        server_assigned_userid_comment = $true
        resource_struct_and_size_function_present = $true
        numeric_opcode43_constant_present = $false
    }
    accepted_runs = @($accepted)
    rejected_controlled_runs = @($rejected)
    incomplete_runs = $incomplete
    summary = [pscustomobject][ordered]@{
        accepted_run_count = $accepted.Count
        transition_run_count = @($accepted | Where-Object {
                $null -ne $_.resource_transition_request }).Count
        rejected_controlled_run_count = $rejected.Count
        incomplete_run_count = $incomplete.Count
        clean_baseline_runs = 6
        maxplayers_1_runs = 2
        maxplayers_8_runs = 2
        reconnect_rcon_correlated_runs = 2
        user_id_rcon_correlated_runs = 2
        dropped_request_runs = 2
        dropped_ack_runs = 2
        duplicate_request_runs = 2
        different_map_runs = 2
        same_process_map_change_runs = 2
        single_message_first_batches = $singleCount
        two_message_first_batches = $doubleCount
        opcode13_semantic = 'user_info_update'
        request_semantic = 'send_resources_request'
        request_sha256 = $requestSha256
        opcode45_body_bytes = 8
        opcode43_offset = 9
        opcode43_semantic_gate = 'pending-no-pinned-numeric-opcode-constant'
    }
    raw_payload_bytes_stored = $false
    raw_userinfo_values_stored = $false
    raw_identity_fields_stored = $false
    opcode43_body_parsed = $false
    resource_response_generated = $false
}
Assert-ProjectionMetadata -Metadata $metadata

if (-not (Test-Path -LiteralPath $projectionRoot -PathType Container)) {
    [IO.Directory]::CreateDirectory($projectionRoot) | Out-Null
}
Assert-NoReparsePoint -Path $projectionRoot -Label 'projection root'
$unexpected = @(Get-ChildItem -LiteralPath $projectionRoot -Force |
    Where-Object { $_.Name -notin @('metadata.json', 'metadata.json.tmp') })
if ($unexpected.Count -ne 0) {
    throw 'Projection root contains an unexpected artifact.'
}
$temporaryPath = Join-Path $projectionRoot 'metadata.json.tmp'
if (Test-Path -LiteralPath $temporaryPath) {
    throw 'Refusing to overwrite an unexpected temporary projection file.'
}
$json = $metadata | ConvertTo-Json -Depth 12
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
    'projection-valid accepted=22 transitions=18 rejected-controls=11 ' +
    'incomplete=2 opcode13=user-info-update request=sendres ' +
    'opcode45=8B opcode43=neutral-unconsumed')
