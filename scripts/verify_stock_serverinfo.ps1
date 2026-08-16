<#
.SYNOPSIS
Runs or validates one bounded private-loopback stock server-info experiment.

.DESCRIPTION
The live mode starts only explicit Valve-signed stock binaries and a supplied
byte-preserving relay. The relay may persist exactly one metadata.json whose
schema contains sanitized, evidence-gated fields and structural offsets only.
Raw packets, authentication/identity bytes, opaque field values, map-list
text, server command text, and the `sendres` tail are forbidden in metadata.
Ignored research inputs may exist outside the accepted projection directory,
but an accepted verifier output directory is restricted to one metadata.json
and may contain no raw payload. ValidateMetadata mode performs the same schema
and directory-content checks offline. Cleanup fields attest the capture
wrapper's preflight and post-stop gates; capture mode independently stops only
the recorded process identities and verifies the process/port gates before it
reads metadata, while finally repeats that identity-bound cleanup on all exits.
#>
[CmdletBinding(DefaultParameterSetName = 'Capture')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidateNotNullOrEmpty()]
    [string]$RelayPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidateNotNullOrEmpty()]
    [string]$HalfLifePath,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidateNotNullOrEmpty()]
    [string]$HldsPath,

    [Parameter(ParameterSetName = 'Capture')]
    [string]$PythonPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'ValidateMetadata')]
    [ValidateNotNullOrEmpty()]
    [string]$ValidateMetadataPath,

    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$Game = 'valve',

    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$Map = 'boot_camp',

    [ValidatePattern('^$|^[A-Za-z0-9_-]+$')]
    [string]$PrimeMap = '',

    [ValidatePattern('^$|^[A-Za-z0-9_-]{1,64}$')]
    [string]$Hostname = '',

    [ValidateRange(1, 32)]
    [int]$MaxPlayers = 2,

    [ValidateRange(1024, 65534)]
    [int]$Port = 27620,

    [ValidateSet(
        'baseline',
        'different-map',
        'different-maxplayers',
        'first-client',
        'second-client',
        'server-restart',
        'map-change',
        'changed-hostname')]
    [string]$Scenario = 'baseline',

    [ValidateRange(8, 60)]
    [int]$TimeoutSeconds = 40
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$maximumPackets = 320
$maximumPostAcceptPackets = 280
$maximumDatagramBytes = 2048
$maximumTotalBytes = 524288
$maximumWrongSourceDatagrams = 4
$expectedValveSignerSubject = 'CN=Valve Corp., O=Valve Corp., L=Bellevue, S=Washington, C=US'
$expectedValveSignerThumbprint = '935767D66FAD4AD2D1F03A095C49370DC74DF607'
$serverPort = $Port + 1
$relayPort = $Port
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifactRoot = [IO.Path]::GetFullPath((
    Join-Path $repositoryRoot 'manual-artifacts\serverinfo-captures'))
$pwshPath = [IO.Path]::GetFullPath((Get-Process -Id $PID).Path)
$runDirectory = $null
$runSucceeded = $false
$serverRecord = $null
$relayRecord = $null
$firstClientRecord = $null
$clientRecord = $null
$loopback = [Net.IPAddress]::Loopback
$serverEndpoint = [Net.IPEndPoint]::new($loopback, $serverPort)

if ($Game -cne 'valve') {
    throw 'This verifier is restricted to the evidence-backed stock Valve profile.'
}
if ($Scenario -eq 'map-change' -and -not $PrimeMap) {
    throw 'The map-change scenario requires an explicit PrimeMap.'
}
if ($Scenario -eq 'second-client' -and $MaxPlayers -lt 2) {
    throw 'The second-client scenario requires MaxPlayers of at least 2.'
}

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
    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction Stop
    if ($resolved.Provider.Name -ne 'FileSystem') {
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
}

function Quote-NativePathArgument {
    param([string]$Path)
    if ($Path.Contains('"')) { throw 'A native path argument cannot contain a quote.' }
    return '"' + $Path + '"'
}

function Assert-ExactProperties {
    param([object]$Value, [string[]]$Allowed, [string]$Label)
    if ($null -eq $Value) { throw "$Label must be present." }
    $actualNames = @($Value.PSObject.Properties | ForEach-Object { $_.Name })
    foreach ($property in $Value.PSObject.Properties) {
        if ($Allowed -cnotcontains $property.Name) {
            throw "$Label contains forbidden property '$($property.Name)'."
        }
    }
    foreach ($required in $Allowed) {
        if ($actualNames -cnotcontains $required) {
            throw "$Label is missing required property '$required'."
        }
    }
}

function Get-BoundedInteger {
    param(
        [object]$Value,
        [string]$Name,
        [string]$Label,
        [long]$Minimum,
        [long]$Maximum
    )
    $actual = $Value.PSObject.Properties[$Name].Value
    if ($actual -isnot [int] -and $actual -isnot [long]) {
        throw "$Label property '$Name' must be a JSON integer."
    }
    $number = [long]$actual
    if ($number -lt $Minimum -or $number -gt $Maximum) {
        throw "$Label property '$Name' is outside its bound."
    }
    return $number
}

function Get-StrictBoolean {
    param([object]$Value, [string]$Name, [string]$Label)
    $actual = $Value.PSObject.Properties[$Name].Value
    if ($actual -isnot [bool]) {
        throw "$Label property '$Name' must be Boolean."
    }
    return [bool]$actual
}

function Assert-ExactString {
    param([object]$Value, [string]$Name, [string]$Expected, [string]$Label)
    $actual = $Value.PSObject.Properties[$Name].Value
    if ($actual -isnot [string] -or $actual -cne $Expected) {
        throw "$Label property '$Name' has an unexpected value."
    }
}

function Assert-SanitizedString {
    param(
        [object]$Value,
        [string]$Name,
        [int]$MaximumBytes,
        [string]$Label
    )
    $actual = $Value.PSObject.Properties[$Name].Value
    if ($actual -isnot [string] -or $actual.Length -gt $MaximumBytes -or
        $actual -notmatch '^[\x20-\x7e]*$') {
        throw "$Label property '$Name' is not bounded printable ASCII."
    }
    return [string]$actual
}

function Assert-NullableInteger {
    param([object]$Value, [string]$Name, [string]$Label, [long]$Maximum)
    $actual = $Value.PSObject.Properties[$Name].Value
    if ($null -eq $actual) { return $null }
    if ($actual -isnot [int] -and $actual -isnot [long]) {
        throw "$Label property '$Name' must be null or a JSON integer."
    }
    $number = [long]$actual
    if ($number -lt 0 -or $number -gt $Maximum) {
        throw "$Label property '$Name' is outside its bound."
    }
    return $number
}

function Assert-AcceptedServerInfoMetadata {
    param([object]$Metadata)

    Assert-ExactProperties -Value $Metadata -Label 'metadata' -Allowed @(
        'schema', 'profile', 'scenario', 'completion', 'versions',
        'process_contract', 'transport', 'initial_request', 'server_info',
        'post_server_info', 'resource_boundary', 'client_action', 'cleanup')
    Assert-ExactString $Metadata 'schema' `
        'hlclient.stock-serverinfo-metadata.v1' 'metadata'
    Assert-ExactString $Metadata 'profile' `
        'stock-hl-1.1.1.1-to-stock-hlds-protocol48-build10210' 'metadata'
    Assert-ExactString $Metadata 'scenario' $Scenario 'metadata'
    Assert-ExactString $Metadata 'completion' 'bounded_complete' 'metadata'

    $versions = $Metadata.versions
    Assert-ExactProperties $versions @(
        'client_versioninfo', 'server_launcher_versioninfo', 'server_protocol',
        'server_build', 'client_signature_valid', 'server_signature_valid') 'versions'
    Assert-ExactString $versions 'client_versioninfo' '1.1.1.1' 'versions'
    Assert-ExactString $versions 'server_launcher_versioninfo' '4.1.1.1' 'versions'
    if ((Get-BoundedInteger $versions 'server_protocol' 'versions' 0 255) -ne 48 -or
        (Get-BoundedInteger $versions 'server_build' 'versions' 0 1000000) -ne 10210 -or
        -not (Get-StrictBoolean $versions 'client_signature_valid' 'versions') -or
        -not (Get-StrictBoolean $versions 'server_signature_valid' 'versions')) {
        throw 'Version profile does not match the accepted stock reference.'
    }

    $contract = $Metadata.process_contract
    Assert-ExactProperties $contract @(
        'loopback_only', 'byte_preserving_relay', 'same_upstream_socket',
        'exact_server_endpoint_validation',
        'client_endpoint_learned_from_canonical_getchallenge',
        'raw_packet_bytes_stored', 'raw_service_payload_stored',
        'raw_research_artifacts_ignored',
        'authentication_bytes_stored', 'raw_server_command_text_stored',
        'owns_exact_process_identities', 'kills_only_owned_processes') `
        'process_contract'
    foreach ($requiredTrue in @(
            'loopback_only', 'byte_preserving_relay', 'same_upstream_socket',
            'exact_server_endpoint_validation',
            'client_endpoint_learned_from_canonical_getchallenge',
            'raw_research_artifacts_ignored',
            'owns_exact_process_identities', 'kills_only_owned_processes')) {
        if (-not (Get-StrictBoolean $contract $requiredTrue 'process_contract')) {
            throw "process_contract property '$requiredTrue' must be true."
        }
    }
    foreach ($requiredFalse in @(
            'raw_packet_bytes_stored', 'authentication_bytes_stored',
            'raw_server_command_text_stored')) {
        if (Get-StrictBoolean $contract $requiredFalse 'process_contract') {
            throw "process_contract property '$requiredFalse' must be false."
        }
    }
    if (Get-StrictBoolean $contract 'raw_service_payload_stored' `
            'process_contract') {
        throw 'Accepted metadata must not report a stored raw service payload.'
    }

    $transport = $Metadata.transport
    Assert-ExactProperties $transport @(
        'packet_count', 'post_accept_packet_count', 'total_bytes',
        'maximum_packets', 'maximum_post_accept_packets',
        'maximum_datagram_bytes', 'maximum_total_bytes', 'timeout_seconds',
        'wrong_source_count') 'transport'
    [void](Get-BoundedInteger $transport 'packet_count' 'transport' 1 $maximumPackets)
    [void](Get-BoundedInteger $transport 'post_accept_packet_count' 'transport' 1 `
        $maximumPostAcceptPackets)
    [void](Get-BoundedInteger $transport 'total_bytes' 'transport' 1 $maximumTotalBytes)
    if ((Get-BoundedInteger $transport 'maximum_packets' 'transport' 1 10000) -ne
            $maximumPackets -or
        (Get-BoundedInteger $transport 'maximum_post_accept_packets' 'transport' 1 10000) -ne
            $maximumPostAcceptPackets -or
        (Get-BoundedInteger $transport 'maximum_datagram_bytes' 'transport' 576 65535) -ne
            $maximumDatagramBytes -or
        (Get-BoundedInteger $transport 'maximum_total_bytes' 'transport' 1 10485760) -ne
            $maximumTotalBytes -or
        (Get-BoundedInteger $transport 'timeout_seconds' 'transport' 8 60) -ne
            $TimeoutSeconds -or
        (Get-BoundedInteger $transport 'wrong_source_count' 'transport' 0 `
            $maximumWrongSourceDatagrams) -ne 0) {
        throw 'Transport bounds do not match the accepted capture contract.'
    }

    $request = $Metadata.initial_request
    Assert-ExactProperties $request @(
        'opcode', 'command', 'semantic_bytes', 'decoded_body_bytes',
        'padding_count', 'semantic_queue_count') 'initial_request'
    if ((Get-BoundedInteger $request 'opcode' 'initial_request' 0 255) -ne 3 -or
        (Get-BoundedInteger $request 'semantic_bytes' 'initial_request' 1 32) -ne 5 -or
        (Get-BoundedInteger $request 'decoded_body_bytes' 'initial_request' 1 32) -ne 8 -or
        (Get-BoundedInteger $request 'padding_count' 'initial_request' 0 8) -ne 3 -or
        (Get-BoundedInteger $request 'semantic_queue_count' 'initial_request' 0 8) -ne 1) {
        throw 'Initial request does not match the accepted exact shape.'
    }
    Assert-ExactString $request 'command' 'new' 'initial_request'

    $serverInfo = $Metadata.server_info
    Assert-ExactProperties $serverInfo @(
        'opcode', 'byte_offset', 'body_start', 'body_bytes',
        'bytes_consumed_including_opcode', 'service_payload_bytes',
        'fixed_prefix_bytes', 'protocol', 'map_start_ordinal_candidate',
        'opaque_map_mode_u32_present', 'opaque_fixed_field_bytes',
        'opaque_fixed_field_value_stored', 'maximum_clients',
        'client_index_confirmed', 'client_index', 'multi_client_mode',
        'game_directory', 'game_directory_bytes', 'hostname', 'hostname_bytes',
        'map_name', 'map_name_bytes', 'opaque_map_list_bytes',
        'opaque_map_list_value_stored', 'final_control_value') 'server_info'
    if ((Get-BoundedInteger $serverInfo 'opcode' 'server_info' 0 255) -ne 11 -or
        (Get-BoundedInteger $serverInfo 'fixed_prefix_bytes' 'server_info' 0 1024) -ne 31 -or
        (Get-BoundedInteger $serverInfo 'protocol' 'server_info' 0 255) -ne 48 -or
        (Get-BoundedInteger $serverInfo 'opaque_fixed_field_bytes' 'server_info' 0 64) -ne 16 -or
        (Get-BoundedInteger $serverInfo 'final_control_value' 'server_info' 0 255) -ne 0) {
        throw 'Server-info fixed layout does not match the accepted profile.'
    }
    if (-not (Get-StrictBoolean $serverInfo 'opaque_map_mode_u32_present' 'server_info') -or
        (Get-StrictBoolean $serverInfo 'opaque_fixed_field_value_stored' 'server_info') -or
        (Get-StrictBoolean $serverInfo 'opaque_map_list_value_stored' 'server_info')) {
        throw 'Opaque server-info fields violate the metadata policy.'
    }
    $mapStartOrdinalCandidate = Get-BoundedInteger $serverInfo `
        'map_start_ordinal_candidate' 'server_info' 1 ([uint32]::MaxValue)
    if ($Scenario -eq 'map-change' -and $mapStartOrdinalCandidate -lt 2) {
        throw 'A completed map-change scenario must show a later map-start candidate.'
    }
    if ($Scenario -ne 'map-change' -and $mapStartOrdinalCandidate -ne 1) {
        throw 'A fresh stock-server scenario must show map-start candidate one.'
    }
    $maximumClients = Get-BoundedInteger $serverInfo 'maximum_clients' `
        'server_info' 1 32
    $multiClientMode = Get-StrictBoolean $serverInfo 'multi_client_mode' 'server_info'
    if ($multiClientMode -ne ($maximumClients -gt 1)) {
        throw 'multi_client_mode does not match the controlled max-player evidence.'
    }
    $clientIndexConfirmed = Get-StrictBoolean $serverInfo 'client_index_confirmed' `
        'server_info'
    $clientIndex = Assert-NullableInteger $serverInfo 'client_index' 'server_info' 31
    if ($clientIndexConfirmed -or $null -ne $clientIndex) {
        throw 'This evidence profile must not publish an unconfirmed client index.'
    }
    if ($Scenario -eq 'second-client') {
        throw 'No bounded second-client stock run completed for this evidence profile.'
    }

    $gameValue = Assert-SanitizedString $serverInfo 'game_directory' 64 'server_info'
    $hostnameValue = Assert-SanitizedString $serverInfo 'hostname' 256 'server_info'
    $mapValue = Assert-SanitizedString $serverInfo 'map_name' 260 'server_info'
    if ($gameValue -cne $Game -or $mapValue -cne ('maps/' + $Map + '.bsp')) {
        throw 'Sanitized game/map fields do not match the controlled launch values.'
    }
    if ($Hostname -and $hostnameValue -cne $Hostname) {
        throw 'Sanitized hostname does not match the controlled launch value.'
    }
    if ($Scenario -eq 'changed-hostname' -and -not $Hostname) {
        throw 'A changed-hostname scenario requires an explicit expected Hostname.'
    }
    $gameBytes = Get-BoundedInteger $serverInfo 'game_directory_bytes' 'server_info' 0 64
    $hostnameBytes = Get-BoundedInteger $serverInfo 'hostname_bytes' 'server_info' 0 256
    $mapBytes = Get-BoundedInteger $serverInfo 'map_name_bytes' 'server_info' 0 260
    $mapListBytes = Get-BoundedInteger $serverInfo 'opaque_map_list_bytes' `
        'server_info' 0 4096
    if ($gameBytes -ne $gameValue.Length -or $hostnameBytes -ne $hostnameValue.Length -or
        $mapBytes -ne $mapValue.Length) {
        throw 'Server-info string byte counts do not match their ASCII metadata.'
    }
    $bodyBytes = Get-BoundedInteger $serverInfo 'body_bytes' 'server_info' 36 8192
    $expectedBodyBytes = 31 + ($gameBytes + 1) + ($hostnameBytes + 1) +
        ($mapBytes + 1) + ($mapListBytes + 1) + 1
    if ($bodyBytes -ne $expectedBodyBytes) {
        throw 'Server-info body length does not match the exact four-string layout.'
    }
    $serverInfoOffset = Get-BoundedInteger $serverInfo 'byte_offset' 'server_info' 0 1048575
    $bodyStart = Get-BoundedInteger $serverInfo 'body_start' 'server_info' 1 1048576
    $bytesConsumed = Get-BoundedInteger $serverInfo 'bytes_consumed_including_opcode' `
        'server_info' 1 8193
    $payloadBytes = Get-BoundedInteger $serverInfo 'service_payload_bytes' `
        'server_info' 1 1048576
    if ($bodyStart -ne ($serverInfoOffset + 1) -or
        $bytesConsumed -ne ($bodyBytes + 1) -or
        ($bodyStart + $bodyBytes) -gt $payloadBytes) {
        throw 'Server-info cursor accounting is inconsistent.'
    }

    $post = $Metadata.post_server_info
    Assert-ExactProperties $post @(
        'opcode', 'byte_offset', 'body_bytes', 'string_bytes', 'control_value') `
        'post_server_info'
    $postOffset = Get-BoundedInteger $post 'byte_offset' 'post_server_info' 0 1048575
    if ((Get-BoundedInteger $post 'opcode' 'post_server_info' 0 255) -ne 54 -or
        (Get-BoundedInteger $post 'body_bytes' 'post_server_info' 0 4096) -ne 2 -or
        (Get-BoundedInteger $post 'string_bytes' 'post_server_info' 0 4096) -ne 0 -or
        (Get-BoundedInteger $post 'control_value' 'post_server_info' 0 255) -ne 0 -or
        $postOffset -ne ($bodyStart + $bodyBytes)) {
        throw 'Post-server-info control does not match the exact captured layout.'
    }

    $boundary = $Metadata.resource_boundary
    Assert-ExactProperties $boundary @(
        'opcode', 'byte_offset', 'remaining_body_bytes', 'direction',
        'evidence_status', 'body_unconsumed') 'resource_boundary'
    $boundaryOffset = Get-BoundedInteger $boundary 'byte_offset' `
        'resource_boundary' 0 1048575
    $remaining = Get-BoundedInteger $boundary 'remaining_body_bytes' `
        'resource_boundary' 1 1048575
    if ((Get-BoundedInteger $boundary 'opcode' 'resource_boundary' 0 255) -ne 14 -or
        $boundaryOffset -ne ($postOffset + 3) -or
        ($boundaryOffset + 1 + $remaining) -ne $payloadBytes -or
        -not (Get-StrictBoolean $boundary 'body_unconsumed' 'resource_boundary')) {
        throw 'Complex boundary cursor does not match the exact captured position.'
    }
    Assert-ExactString $boundary 'direction' 'server_message' 'resource_boundary'
    Assert-ExactString $boundary 'evidence_status' `
        'confirmed_pre_resource_boundary_body_pending' 'resource_boundary'

    $action = $Metadata.client_action
    Assert-ExactProperties $action @(
        'reliable_message_count', 'opcode', 'command', 'terminator_offset',
        'trailing_bytes', 'tail_unparsed', 'after_complete_first_batch',
        'covers_final_fragment_ack', 'project_generated') 'client_action'
    $actionCount = Get-BoundedInteger $action 'reliable_message_count' 'client_action' 0 1
    $actionOpcode = Assert-NullableInteger $action 'opcode' 'client_action' 255
    $terminator = Assert-NullableInteger $action 'terminator_offset' 'client_action' 4096
    $trailing = Assert-NullableInteger $action 'trailing_bytes' 'client_action' 4096
    $expectedActionCount = if ($multiClientMode) { 1 } else { 0 }
    if ($actionCount -ne $expectedActionCount) {
        throw 'Client resource-action observation does not match the controlled single/multi-client profile.'
    }
    if (Get-StrictBoolean $action 'project_generated' 'client_action') {
        throw 'Stock metadata must not claim a project-generated resource action.'
    }
    if ($actionCount -eq 1) {
        Assert-ExactString $action 'command' 'sendres' 'client_action'
        if ($actionOpcode -ne 3 -or $terminator -ne 8 -or $trailing -ne 28 -or
            -not (Get-StrictBoolean $action 'tail_unparsed' 'client_action') -or
            -not (Get-StrictBoolean $action 'after_complete_first_batch' 'client_action') -or
            -not (Get-StrictBoolean $action 'covers_final_fragment_ack' 'client_action')) {
            throw 'Observed sendres action does not match the bounded structural evidence.'
        }
    }
    else {
        if ($null -ne $action.command -or $null -ne $actionOpcode -or
            $null -ne $terminator -or $null -ne $trailing) {
            throw 'Absent client action must not publish command fields.'
        }
        foreach ($name in @('tail_unparsed', 'after_complete_first_batch',
                'covers_final_fragment_ack')) {
            if (Get-StrictBoolean $action $name 'client_action') {
                throw "Absent client action property '$name' must be false."
            }
        }
    }

    $cleanup = $Metadata.cleanup
    Assert-ExactProperties $cleanup @(
        'capture_preflight_stock_processes', 'capture_post_stop_stock_processes',
        'capture_preflight_selected_ports', 'capture_post_stop_selected_ports') `
        'cleanup'
    foreach ($name in @(
            'capture_preflight_stock_processes', 'capture_post_stop_stock_processes',
            'capture_preflight_selected_ports', 'capture_post_stop_selected_ports')) {
        if ((Get-BoundedInteger $cleanup $name 'cleanup' 0 1024) -ne 0) {
            throw "Cleanup property '$name' must be zero."
        }
    }

    return [pscustomobject]@{
        PacketCount = [int]$transport.packet_count
        ServerInfoOffset = [int]$serverInfoOffset
        BodyBytes = [int]$bodyBytes
        BoundaryOffset = [int]$boundaryOffset
        ClientActionCount = [int]$actionCount
    }
}

function New-OwnedProcessRecord {
    param([Diagnostics.Process]$Process, [string]$ExpectedExecutable)
    $Process.Refresh()
    return [pscustomobject]@{
        Id = $Process.Id
        StartTimeUtc = $Process.StartTime.ToUniversalTime()
        ExpectedExecutable = [IO.Path]::GetFullPath($ExpectedExecutable)
    }
}

function Test-OwnedProcessIdentity {
    param([object]$Record)
    if ($null -eq $Record) { return $false }
    $current = Get-Process -Id $Record.Id -ErrorAction SilentlyContinue
    if ($null -eq $current) { return $false }
    try {
        $currentPath = [IO.Path]::GetFullPath($current.Path).TrimEnd('\')
        $expectedPath = [IO.Path]::GetFullPath($Record.ExpectedExecutable).TrimEnd('\')
        $startDelta = [Math]::Abs((
            $current.StartTime.ToUniversalTime() - $Record.StartTimeUtc).TotalMilliseconds)
        return $currentPath -ieq $expectedPath -and $startDelta -le 2.0
    }
    catch { return $false }
}

function Stop-VerifiedOwnedProcess {
    param([object]$Record)
    if ($null -eq $Record) { return }
    $current = Get-Process -Id $Record.Id -ErrorAction SilentlyContinue
    if ($null -eq $current) { return }
    if (-not (Test-OwnedProcessIdentity $Record)) {
        throw "Refusing to terminate PID $($Record.Id): identity mismatch."
    }
    try {
        $current.Kill()
        if (-not $current.WaitForExit(5000)) {
            throw "Verified owned PID $($Record.Id) did not exit within five seconds."
        }
    }
    catch [InvalidOperationException] { }
}

function Stop-StartedProcessHandle {
    param([Diagnostics.Process]$Process)
    if ($null -eq $Process) { return }
    try {
        $Process.Refresh()
        if ($Process.HasExited) { return }
        $Process.Kill()
        if (-not $Process.WaitForExit(5000)) {
            throw 'A just-started owned process did not exit within five seconds.'
        }
    }
    catch [InvalidOperationException] { }
}

function Test-ServerReady {
    $socket = [Net.Sockets.Socket]::new(
        [Net.Sockets.AddressFamily]::InterNetwork,
        [Net.Sockets.SocketType]::Dgram,
        [Net.Sockets.ProtocolType]::Udp)
    try {
        $socket.Bind([Net.IPEndPoint]::new($loopback, 0))
        $request = [byte[]](@(0xff, 0xff, 0xff, 0xff) +
            [Text.Encoding]::ASCII.GetBytes("getchallenge steam`n"))
        [void]$socket.SendTo($request, $serverEndpoint)
        return $socket.Poll(500000, [Net.Sockets.SelectMode]::SelectRead)
    }
    finally { $socket.Dispose() }
}

if ($PSCmdlet.ParameterSetName -eq 'ValidateMetadata') {
    $metadataPath = Resolve-ExplicitFile $ValidateMetadataPath 'Metadata'
    $metadataItem = Get-Item -LiteralPath $metadataPath -Force
    if ($metadataItem.Name -cne 'metadata.json' -or $metadataItem.Length -le 0 -or
        $metadataItem.Length -gt 262144) {
        throw 'Metadata must be a non-empty metadata.json no larger than 256 KiB.'
    }
    Assert-MetadataOnlyDirectory $metadataPath 'Accepted metadata directory'
    $metadata = Get-Content -Raw -LiteralPath $metadataPath |
        ConvertFrom-Json -ErrorAction Stop
    $validated = Assert-AcceptedServerInfoMetadata $metadata
    Write-Output (
        'metadata-valid scenario={0} packets={1} serverinfo={2}+{3} boundary=14@{4} clientAction={5}' -f
        $Scenario, $validated.PacketCount, $validated.ServerInfoOffset,
        $validated.BodyBytes, $validated.BoundaryOffset,
        $validated.ClientActionCount)
    return
}

$resolvedRelay = Resolve-ExplicitFile $RelayPath 'Relay'
$hlPath = Resolve-ExplicitFile $HalfLifePath 'HalfLifePath'
$hldsPath = Resolve-ExplicitFile $HldsPath 'HldsPath'
$resolvedPython = $null
if ($PythonPath) { $resolvedPython = Resolve-ExplicitFile $PythonPath 'PythonPath' }
$relayExtension = [IO.Path]::GetExtension($resolvedRelay)
if ($relayExtension -ine '.ps1' -and $relayExtension -ine '.exe') {
    throw 'RelayPath must explicitly name a .ps1 or .exe relay.'
}
if ([IO.Path]::GetFileName($hlPath) -ine 'hl.exe' -or
    [IO.Path]::GetFileName($hldsPath) -ine 'hlds.exe') {
    throw 'Stock executable paths must explicitly name hl.exe and hlds.exe.'
}
foreach ($binary in @($hlPath, $hldsPath)) {
    $signature = Get-AuthenticodeSignature -LiteralPath $binary
    if ($signature.Status -ne 'Valid' -or $null -eq $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -cne $expectedValveSignerSubject -or
        $signature.SignerCertificate.Thumbprint -cne $expectedValveSignerThumbprint) {
        throw "Reference binary does not match the captured Valve signer profile: $binary"
    }
}
if ((Get-Item -LiteralPath $hlPath).VersionInfo.FileVersion -notmatch '^1, 1, 1, 1$' -or
    (Get-Item -LiteralPath $hldsPath).VersionInfo.FileVersion -notmatch '^4, 1, 1, 1$') {
    throw 'Reference executable VERSIONINFO does not match the accepted profile.'
}
if (@(Get-Process -Name hl,hlds -ErrorAction SilentlyContinue).Count -ne 0) {
    throw 'Refusing to run while an unrelated hl.exe or hlds.exe exists.'
}
foreach ($selectedPort in @($relayPort, $serverPort)) {
    if (Get-NetUDPEndpoint -LocalPort $selectedPort -ErrorAction SilentlyContinue) {
        throw "Selected private loopback UDP port $selectedPort is occupied."
    }
}
Assert-NoReparsePointInExistingPath $artifactRoot 'Artifact root'
if (-not (Test-Path -LiteralPath $artifactRoot)) {
    [IO.Directory]::CreateDirectory($artifactRoot) | Out-Null
}
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$runDirectory = Join-Path $artifactRoot ($Scenario + '-' + $timestamp)
if (Test-Path -LiteralPath $runDirectory) {
    throw 'Refusing to reuse an existing capture directory.'
}
[IO.Directory]::CreateDirectory($runDirectory) | Out-Null

try {
    $serverArguments = @(
        '-console', '-game', $Game, '-nomaster', '+ip', '127.0.0.1',
        '+maxplayers', $MaxPlayers.ToString(
            [Globalization.CultureInfo]::InvariantCulture))
    if ($PrimeMap) { $serverArguments += @('+map', $PrimeMap) }
    $serverArguments += @('+map', $Map)
    if ($Hostname) { $serverArguments += @('+hostname', $Hostname) }
    $serverArguments += @(
        '-port', $serverPort.ToString([Globalization.CultureInfo]::InvariantCulture),
        '+sv_lan', '1')
    $serverProcess = Start-Process -FilePath $hldsPath -ArgumentList $serverArguments `
        -WorkingDirectory (Split-Path -Parent $hldsPath) -WindowStyle Hidden -PassThru
    try { $serverRecord = New-OwnedProcessRecord $serverProcess $hldsPath }
    catch {
        Stop-StartedProcessHandle $serverProcess
        throw
    }
    $identityDeadline = [DateTime]::UtcNow.AddSeconds(2)
    while (-not (Test-OwnedProcessIdentity $serverRecord)) {
        if ($serverProcess.HasExited -or [DateTime]::UtcNow -ge $identityDeadline) {
            throw 'Owned stock server failed its immediate identity check.'
        }
        Start-Sleep -Milliseconds 50
    }
    $readyDeadline = [DateTime]::UtcNow.AddSeconds(15)
    while (-not (Test-ServerReady)) {
        if ($serverProcess.HasExited) { throw 'Owned stock server exited during startup.' }
        if ([DateTime]::UtcNow -ge $readyDeadline) {
            throw 'Owned stock server startup timed out.'
        }
        Start-Sleep -Milliseconds 250
    }

    if ($Scenario -eq 'second-client') {
        $directEndpoint = '127.0.0.1:{0}' -f $serverPort
        $firstProcess = Start-Process -FilePath $hlPath -ArgumentList @(
            '-game', $Game, '-console', '-allowmultiple', '-windowed',
            '-w', '640', '-h', '480', '+connect', $directEndpoint) `
            -WorkingDirectory (Split-Path -Parent $hlPath) `
            -RedirectStandardOutput 'NUL' -RedirectStandardError '\\.\NUL' `
            -WindowStyle Minimized -PassThru
        try { $firstClientRecord = New-OwnedProcessRecord $firstProcess $hlPath }
        catch {
            Stop-StartedProcessHandle $firstProcess
            throw
        }
        $firstDeadline = [DateTime]::UtcNow.AddSeconds(2)
        while (-not (Test-OwnedProcessIdentity $firstClientRecord)) {
            if ($firstProcess.HasExited -or [DateTime]::UtcNow -ge $firstDeadline) {
                throw 'Owned first client failed its immediate identity check.'
            }
            Start-Sleep -Milliseconds 50
        }
        Start-Sleep -Seconds 5
        if (-not (Test-OwnedProcessIdentity $firstClientRecord)) {
            throw 'Owned first client did not remain alive for the second-client probe.'
        }
    }

    $relayArguments = @(
        '-ListenPort', $relayPort.ToString([Globalization.CultureInfo]::InvariantCulture),
        '-ServerPort', $serverPort.ToString([Globalization.CultureInfo]::InvariantCulture),
        '-OutputDirectory', (Quote-NativePathArgument $runDirectory),
        '-Game', $Game, '-Map', $Map,
        '-MaxPlayers', $MaxPlayers.ToString([Globalization.CultureInfo]::InvariantCulture),
        '-Scenario', $Scenario,
        '-TimeoutSeconds', $TimeoutSeconds.ToString([Globalization.CultureInfo]::InvariantCulture),
        '-MaximumPackets', $maximumPackets.ToString([Globalization.CultureInfo]::InvariantCulture),
        '-MaximumPostAcceptPackets', $maximumPostAcceptPackets.ToString(
            [Globalization.CultureInfo]::InvariantCulture),
        '-MaximumDatagramBytes', $maximumDatagramBytes.ToString(
            [Globalization.CultureInfo]::InvariantCulture),
        '-MaximumTotalBytes', $maximumTotalBytes.ToString(
            [Globalization.CultureInfo]::InvariantCulture))
    if ($PrimeMap) { $relayArguments += @('-PrimeMap', $PrimeMap) }
    if ($Hostname) { $relayArguments += @('-Hostname', $Hostname) }
    if ($resolvedPython) {
        $relayArguments = @('-PythonPath', (Quote-NativePathArgument $resolvedPython)) +
            $relayArguments
    }
    $relayExecutable = $resolvedRelay
    if ($relayExtension -ieq '.ps1') {
        $relayExecutable = $pwshPath
        $relayArguments = @(
            '-NoLogo', '-NoProfile', '-NonInteractive', '-File',
            (Quote-NativePathArgument $resolvedRelay)) + $relayArguments
    }
    $relayProcess = Start-Process -FilePath $relayExecutable `
        -ArgumentList $relayArguments -WorkingDirectory (Split-Path -Parent $resolvedRelay) `
        -RedirectStandardOutput 'NUL' -RedirectStandardError '\\.\NUL' `
        -WindowStyle Hidden -PassThru
    try { $relayRecord = New-OwnedProcessRecord $relayProcess $relayExecutable }
    catch {
        Stop-StartedProcessHandle $relayProcess
        throw
    }
    $relayDeadline = [DateTime]::UtcNow.AddSeconds(2)
    while (-not (Test-OwnedProcessIdentity $relayRecord)) {
        if ($relayProcess.HasExited -or [DateTime]::UtcNow -ge $relayDeadline) {
            throw 'Owned relay failed its immediate identity check.'
        }
        Start-Sleep -Milliseconds 50
    }
    $bindDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        if ($relayProcess.HasExited) { throw 'Owned relay exited before binding.' }
        $ready = Get-NetUDPEndpoint -LocalAddress '127.0.0.1' -LocalPort $relayPort `
            -ErrorAction SilentlyContinue | Where-Object OwningProcess -eq $relayProcess.Id
        if ($null -ne $ready) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $bindDeadline)
    if ($null -eq $ready) { throw 'Owned relay failed its bounded bind wait.' }

    $connectEndpoint = '127.0.0.1:{0}' -f $relayPort
    $clientProcess = Start-Process -FilePath $hlPath -ArgumentList @(
        '-game', $Game, '-console', '-allowmultiple', '-windowed',
        '-w', '640', '-h', '480', '+connect', $connectEndpoint) `
        -WorkingDirectory (Split-Path -Parent $hlPath) `
        -RedirectStandardOutput 'NUL' -RedirectStandardError '\\.\NUL' `
        -WindowStyle Minimized -PassThru
    try { $clientRecord = New-OwnedProcessRecord $clientProcess $hlPath }
    catch {
        Stop-StartedProcessHandle $clientProcess
        throw
    }
    $clientDeadline = [DateTime]::UtcNow.AddSeconds(2)
    while (-not (Test-OwnedProcessIdentity $clientRecord)) {
        if ($clientProcess.HasExited -or [DateTime]::UtcNow -ge $clientDeadline) {
            throw 'Owned stock client failed its immediate identity check.'
        }
        Start-Sleep -Milliseconds 50
    }
    if (-not $relayProcess.WaitForExit(($TimeoutSeconds + 5) * 1000)) {
        throw 'Owned relay exceeded its bounded lifetime plus grace period.'
    }
    if ($relayProcess.ExitCode -ne 0) {
        throw "Owned relay failed with exit code $($relayProcess.ExitCode)."
    }

    Stop-VerifiedOwnedProcess $clientRecord
    $clientRecord = $null
    Stop-VerifiedOwnedProcess $firstClientRecord
    $firstClientRecord = $null
    Stop-VerifiedOwnedProcess $relayRecord
    $relayRecord = $null
    Stop-VerifiedOwnedProcess $serverRecord
    $serverRecord = $null
    if (@(Get-Process -Name hl,hlds -ErrorAction SilentlyContinue).Count -ne 0) {
        throw 'Post-capture gate found an unowned stock process; it was not terminated.'
    }
    foreach ($selectedPort in @($relayPort, $serverPort)) {
        if (Get-NetUDPEndpoint -LocalPort $selectedPort -ErrorAction SilentlyContinue) {
            throw "Post-capture gate found UDP port $selectedPort occupied."
        }
    }

    $metadataPath = Join-Path $runDirectory 'metadata.json'
    Assert-MetadataOnlyDirectory $metadataPath 'Run directory'
    $metadataItem = Get-Item -LiteralPath $metadataPath -Force
    if ($metadataItem.Name -cne 'metadata.json' -or $metadataItem.Length -le 0 -or
        $metadataItem.Length -gt 262144) {
        throw 'Relay output must contain one bounded metadata.json.'
    }
    Assert-OnlyDefaultDataStream $metadataPath 'Relay metadata'
    $metadata = Get-Content -Raw -LiteralPath $metadataPath |
        ConvertFrom-Json -ErrorAction Stop
    $validated = Assert-AcceptedServerInfoMetadata $metadata
    $runSucceeded = $true
    Write-Output (
        'capture-complete scenario={0} packets={1} serverinfo={2}+{3} boundary=14@{4} clientAction={5}' -f
        $Scenario, $validated.PacketCount, $validated.ServerInfoOffset,
        $validated.BodyBytes, $validated.BoundaryOffset,
        $validated.ClientActionCount)
    Write-Output ('metadata-only ignored artifact: {0}' -f $metadataPath)
}
finally {
    $cleanupErrors = [Collections.Generic.List[string]]::new()
    foreach ($record in @($clientRecord, $firstClientRecord, $relayRecord, $serverRecord)) {
        try { Stop-VerifiedOwnedProcess $record }
        catch { $cleanupErrors.Add($_.Exception.Message) }
    }
    if (@(Get-Process -Name hl,hlds -ErrorAction SilentlyContinue).Count -ne 0) {
        $cleanupErrors.Add(
            'Post-cleanup gate found an unowned stock process; it was not terminated.')
    }
    foreach ($selectedPort in @($relayPort, $serverPort)) {
        if (Get-NetUDPEndpoint -LocalPort $selectedPort -ErrorAction SilentlyContinue) {
            $cleanupErrors.Add("Post-cleanup gate found UDP port $selectedPort occupied.")
        }
    }
    if (-not $runSucceeded -and $null -ne $runDirectory -and
        (Test-Path -LiteralPath $runDirectory) -and
        $runDirectory.StartsWith(
            $artifactRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        try {
            Assert-NoReparsePointInExistingPath $artifactRoot 'Artifact root'
            Assert-NoReparsePointInExistingPath $runDirectory 'Run directory'
            Assert-NoDescendantReparsePoint $runDirectory 'Run directory'
            Remove-Item -LiteralPath $runDirectory -Recurse -Force
        }
        catch { $cleanupErrors.Add('Rejected metadata cleanup failed.') }
    }
    if ($cleanupErrors.Count -gt 0) {
        throw ('Owned-process cleanup failed: ' + ($cleanupErrors -join ' | '))
    }
}
