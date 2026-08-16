<#
.SYNOPSIS
Runs one bounded private-loopback stock initial-sign-on experiment.

.DESCRIPTION
Starts only explicit Valve-signed stock binaries and a user-supplied bounded
byte-preserving relay. The relay may persist exactly one metadata.json with
whitelisted structural facts and the narrowly validated printable `new`
fixture. Raw packets, auth/identity bytes, server text, and arbitrary payload
bytes are forbidden.
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

    [ValidateRange(1024, 65534)]
    [int]$Port = 27520,

    [ValidateSet(
        'baseline',
        'drop-initial-request',
        'drop-request-ack',
        'duplicate-server-batch')]
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
$scenarioMap = @{
    'baseline' = 'Baseline'
    'drop-initial-request' = 'DropInitialRequest'
    'drop-request-ack' = 'DropRequestAck'
    'duplicate-server-batch' = 'DuplicateServerBatch'
}
$relayScenario = $scenarioMap[$Scenario]
$serverPort = $Port + 1
$relayPort = $Port
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifactRoot = [IO.Path]::GetFullPath((
    Join-Path $repositoryRoot 'manual-artifacts\signon-captures'))
$pwshPath = [IO.Path]::GetFullPath((Get-Process -Id $PID).Path)
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$runDirectory = $null
$runSucceeded = $false

$serverRecord = $null
$relayRecord = $null
$clientRecord = $null
$loopback = [Net.IPAddress]::Loopback
$serverEndpoint = [Net.IPEndPoint]::new($loopback, $serverPort)

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
    return [IO.Path]::GetFullPath($item.FullName)
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

function Quote-NativePathArgument {
    param([string]$Path)
    if ($Path.Contains('"')) { throw 'A native path argument cannot contain a quote.' }
    return '"' + $Path + '"'
}

function Assert-ExactProperties {
    param([object]$Value, [string[]]$Allowed, [string]$Label)
    foreach ($property in $Value.PSObject.Properties) {
        if ($property.Name -notin $Allowed) {
            throw "$Label contains forbidden property '$($property.Name)'."
        }
    }
    foreach ($required in $Allowed) {
        if ($null -eq $Value.PSObject.Properties[$required]) {
            throw "$Label is missing required property '$required'."
        }
    }
}

function Get-BoundedInteger {
    param([object]$Value, [string]$Name, [string]$Label, [long]$Minimum, [long]$Maximum)
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
    if ($actual -isnot [bool]) { throw "$Label property '$Name' must be Boolean." }
    return [bool]$actual
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
    if (-not (Test-OwnedProcessIdentity -Record $Record)) {
        throw "Refusing to terminate PID $($Record.Id): owned-process identity mismatch."
    }
    try {
        $current.Kill()
        if (-not $current.WaitForExit(5000)) {
            throw "Verified owned PID $($Record.Id) did not exit within five seconds."
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

function Assert-AcceptedSignonMetadata {
    param([object]$Metadata, [string]$RelayScenario)
    $metadata = $Metadata
    $topProperties = @(
        'schema', 'profile', 'scenario', 'completion', 'loopback_only',
        'byte_preserving_relay', 'same_upstream_socket',
        'exact_server_endpoint_validation',
        'client_endpoint_learned_from_canonical_getchallenge',
        'raw_packet_bytes_stored', 'packet_count', 'post_accept_packet_count',
        'total_bytes', 'maximum_packets', 'maximum_post_accept_packets',
        'maximum_datagram_bytes', 'maximum_total_bytes', 'timeout_seconds',
        'elapsed_milliseconds', 'connect_seen', 'accept_seen',
        'scenario_mutation_count', 'ignored_wrong_source_count',
        'held_packet_at_end', 'initial_request',
        'initial_request_transmissions', 'request_acknowledgements',
        'first_service_boundary', 'duplicate_batch_datagrams_replayed',
        'post_boundary_client_reliable_transmissions', 'transfers',
        'fragment_acknowledgements', 'events', 'actions'
    )
    Assert-ExactProperties $metadata $topProperties 'metadata'
    if ($metadata.schema -cne 'hlclient.stock-initial-signon-metadata.v1' -or
        $metadata.profile -cne
            'stock-hl-1.1.1.1-to-stock-hlds-protocol48-build10210' -or
        $metadata.scenario -cne $relayScenario -or
        $metadata.completion -cne 'bounded_complete') {
        throw 'Relay metadata does not match the exact accepted stock profile.'
    }
    foreach ($name in @(
            'loopback_only', 'byte_preserving_relay', 'same_upstream_socket',
            'exact_server_endpoint_validation',
            'client_endpoint_learned_from_canonical_getchallenge',
            'connect_seen', 'accept_seen')) {
        if (-not (Get-StrictBoolean $metadata $name 'metadata')) {
            throw "Metadata property '$name' must be true."
        }
    }
    if ((Get-StrictBoolean $metadata 'raw_packet_bytes_stored' 'metadata') -or
        (Get-StrictBoolean $metadata 'held_packet_at_end' 'metadata')) {
        throw 'Relay retained or persisted a packet byte buffer.'
    }
    $packetCount = Get-BoundedInteger $metadata 'packet_count' 'metadata' 1 $maximumPackets
    $postAcceptPacketCount = Get-BoundedInteger $metadata `
        'post_accept_packet_count' 'metadata' `
        1 $maximumPostAcceptPackets
    $totalBytes = Get-BoundedInteger $metadata 'total_bytes' 'metadata' `
        1 $maximumTotalBytes
    $null = Get-BoundedInteger $metadata 'elapsed_milliseconds' 'metadata' `
        1 ($TimeoutSeconds * 1000L)
    if ((Get-BoundedInteger $metadata 'maximum_packets' 'metadata' 1 $maximumPackets) -ne
            $maximumPackets -or
        (Get-BoundedInteger $metadata 'maximum_post_accept_packets' 'metadata' 1 `
            $maximumPostAcceptPackets) -ne $maximumPostAcceptPackets -or
        (Get-BoundedInteger $metadata 'maximum_datagram_bytes' 'metadata' 1 `
            $maximumDatagramBytes) -ne $maximumDatagramBytes -or
        (Get-BoundedInteger $metadata 'maximum_total_bytes' 'metadata' 1 `
            $maximumTotalBytes) -ne $maximumTotalBytes -or
        (Get-BoundedInteger $metadata 'timeout_seconds' 'metadata' 1 60) -ne
            $TimeoutSeconds) {
        throw 'Relay did not echo the exact requested bounds.'
    }
    $wrongSourceCount = Get-BoundedInteger $metadata 'ignored_wrong_source_count' `
        'metadata' 0 $maximumWrongSourceDatagrams
    $expectedMutation = if ($Scenario -eq 'baseline') { 0L } else { 1L }
    if ((Get-BoundedInteger $metadata 'scenario_mutation_count' 'metadata' 0 1) -ne
            $expectedMutation) {
        throw 'Scenario mutation cardinality is not exact.'
    }

    $request = $metadata.initial_request
    Assert-ExactProperties $request @(
        'opcode', 'command_ascii', 'terminator', 'terminator_offset',
        'padding_byte', 'padding_count', 'decoded_body_bytes',
        'canonical_bytes_hex', 'canonical_sha256') 'initial_request'
    if ((Get-BoundedInteger $request 'opcode' 'initial_request' 0 255) -ne 3 -or
        $request.command_ascii -cne 'new' -or
        (Get-BoundedInteger $request 'terminator' 'initial_request' 0 0) -ne 0 -or
        (Get-BoundedInteger $request 'terminator_offset' 'initial_request' 4 4) -ne 4 -or
        (Get-BoundedInteger $request 'padding_byte' 'initial_request' 1 1) -ne 1 -or
        (Get-BoundedInteger $request 'padding_count' 'initial_request' 3 3) -ne 3 -or
        (Get-BoundedInteger $request 'decoded_body_bytes' 'initial_request' 8 8) -ne 8 -or
        $request.canonical_bytes_hex -cne '036E657700010101' -or
        $request.canonical_sha256 -cne
            '490B1E83546E7FE1DA018154A89254354BAE54559EE52DACA6FBA95E437E1F0E') {
        throw 'Initial request is not the exact sanitized stock fixture.'
    }

    $transmissions = @($metadata.initial_request_transmissions)
    $expectedTransmissions = if ($Scenario -eq 'drop-initial-request') { 2 } else { 1 }
    if ($transmissions.Count -ne $expectedTransmissions) {
        throw 'Initial request transmission cardinality is not exact.'
    }
    foreach ($transmission in $transmissions) {
        Assert-ExactProperties $transmission @(
            'event_order', 'sequence', 'acknowledgement',
            'reliable_presence', 'forwarded') 'request_transmission'
        $null = Get-BoundedInteger $transmission 'event_order' `
            'request_transmission' 1 $packetCount
        $null = Get-BoundedInteger $transmission 'sequence' `
            'request_transmission' 1 1073741823
        $null = Get-BoundedInteger $transmission 'acknowledgement' `
            'request_transmission' 0 1073741823
        if (-not (Get-StrictBoolean $transmission 'reliable_presence' `
                'request_transmission')) {
            throw 'Initial request transmission lacks reliable presence.'
        }
        $null = Get-StrictBoolean $transmission 'forwarded' 'request_transmission'
    }
    if ([long]$transmissions[0].sequence -ne 1 -or
        [long]$transmissions[0].acknowledgement -ne 0 -or
        [long]$transmissions[0].event_order -ne 5) {
        throw 'First semantic request is not the first sequenced post-ACCEPT packet.'
    }
    if ($Scenario -eq 'drop-initial-request') {
        if ([bool]$transmissions[0].forwarded -or -not [bool]$transmissions[1].forwarded -or
            [long]$transmissions[1].sequence -le [long]$transmissions[0].sequence -or
            [long]$transmissions[1].acknowledgement -ne 1 -or
            [long]$transmissions[1].event_order -le
                [long]$transmissions[0].event_order) {
            throw 'Dropped request lacks one fresh-sequence forwarded retransmission.'
        }
    }
    elseif (-not [bool]$transmissions[0].forwarded) {
        throw 'Non-drop scenario did not forward the initial request.'
    }

    $requestAcks = @($metadata.request_acknowledgements)
    if ($requestAcks.Count -lt 1 -or $requestAcks.Count -gt 64) {
        throw 'Request ACK observations are outside their bound.'
    }
    for ($ackIndex = 0; $ackIndex -lt $requestAcks.Count; ++$ackIndex) {
        $ack = $requestAcks[$ackIndex]
        Assert-ExactProperties $ack @(
            'event_order', 'server_sequence', 'acknowledgement',
            'reliable_ack', 'forwarded') 'request_ack'
        $null = Get-BoundedInteger $ack 'event_order' 'request_ack' 1 $packetCount
        $null = Get-BoundedInteger $ack 'server_sequence' 'request_ack' 1 1073741823
        $null = Get-BoundedInteger $ack 'acknowledgement' 'request_ack' 1 1073741823
        if (-not (Get-StrictBoolean $ack 'reliable_ack' 'request_ack')) {
            throw 'Request ACK generation changed from one.'
        }
        $forwarded = Get-StrictBoolean $ack 'forwarded' 'request_ack'
        $expectedForwarded = -not (
            $Scenario -eq 'drop-request-ack' -and $ackIndex -eq 0)
        if ($forwarded -ne $expectedForwarded) {
            throw 'Request ACK forwarding metadata does not match the scenario.'
        }
    }
    if ($Scenario -eq 'drop-request-ack') {
        if ($requestAcks.Count -lt 2 -or [bool]$requestAcks[0].forwarded -or
            -not [bool]$requestAcks[1].forwarded -or
            [long]$requestAcks[0].server_sequence -ne 1 -or
            [long]$requestAcks[1].server_sequence -ne 2) {
            throw 'Dropped request ACK lacks one fresh-sequence forwarded retry.'
        }
    }
    elseif ($Scenario -ne 'drop-initial-request' -and
        [long]$requestAcks[0].server_sequence -ne 1) {
        throw 'First covering request ACK did not use the initial server sequence.'
    }

    $boundary = $metadata.first_service_boundary
    Assert-ExactProperties $boundary @(
        'envelope', 'envelope_bytes', 'compressed_transfer_bytes',
        'standard_bzip2_stream_bytes', 'trailing_compressed_bytes',
        'service_payload_bytes', 'simple_messages', 'boundary_opcode',
        'byte_offset', 'opcode_bytes_consumed', 'remaining_byte_count',
        'payload_byte_count', 'byte_aligned',
        'explicit_total_length_field_observed') 'service_boundary'
    if ($boundary.envelope -cne 'BZ2-NUL-plus-standard-bzip2' -or
        (Get-BoundedInteger $boundary 'envelope_bytes' 'service_boundary' 4 4) -ne 4 -or
        (Get-BoundedInteger $boundary 'compressed_transfer_bytes' `
            'service_boundary' 4186 4186) -ne 4186 -or
        (Get-BoundedInteger $boundary 'standard_bzip2_stream_bytes' `
            'service_boundary' 4182 4182) -ne 4182 -or
        (Get-BoundedInteger $boundary 'trailing_compressed_bytes' `
            'service_boundary' 0 0) -ne 0 -or
        (Get-BoundedInteger $boundary 'service_payload_bytes' `
            'service_boundary' 7480 7480) -ne 7480 -or
        (Get-BoundedInteger $boundary 'boundary_opcode' `
            'service_boundary' 11 11) -ne 11 -or
        (Get-BoundedInteger $boundary 'byte_offset' `
            'service_boundary' 42 42) -ne 42 -or
        (Get-BoundedInteger $boundary 'opcode_bytes_consumed' `
            'service_boundary' 1 1) -ne 1 -or
        (Get-BoundedInteger $boundary 'remaining_byte_count' `
            'service_boundary' 7437 7437) -ne 7437 -or
        (Get-BoundedInteger $boundary 'payload_byte_count' `
            'service_boundary' 7480 7480) -ne 7480 -or
        -not (Get-StrictBoolean $boundary 'byte_aligned' 'service_boundary') -or
        (Get-StrictBoolean $boundary 'explicit_total_length_field_observed' `
            'service_boundary')) {
        throw 'Service boundary does not match the accepted stock fixture.'
    }
    $simple = @($boundary.simple_messages)
    if ($simple.Count -ne 1) { throw 'Expected exactly one simple pre-boundary message.' }
    Assert-ExactProperties $simple[0] @(
        'opcode', 'byte_offset', 'layout', 'bytes_consumed', 'string_bytes',
        'nul_terminated', 'contains_escape', 'control_byte_count') 'simple_message'
    if ((Get-BoundedInteger $simple[0] 'opcode' 'simple_message' 8 8) -ne 8 -or
        (Get-BoundedInteger $simple[0] 'byte_offset' 'simple_message' 0 0) -ne 0 -or
        $simple[0].layout -cne 'bounded-nul-terminated-text' -or
        (Get-BoundedInteger $simple[0] 'bytes_consumed' 'simple_message' 42 42) -ne 42 -or
        (Get-BoundedInteger $simple[0] 'string_bytes' 'simple_message' 40 40) -ne 40 -or
        -not (Get-StrictBoolean $simple[0] 'nul_terminated' 'simple_message') -or
        (Get-StrictBoolean $simple[0] 'contains_escape' 'simple_message') -or
        (Get-BoundedInteger $simple[0] 'control_byte_count' `
            'simple_message' 4 4) -ne 4) {
        throw 'Simple pre-boundary message layout changed.'
    }

    $transfers = @($metadata.transfers)
    if ($transfers.Count -lt 1 -or $transfers.Count -gt 16) {
        throw 'Completed transfer count is outside its bound.'
    }
    foreach ($transfer in $transfers) {
        Assert-ExactProperties $transfer @(
            'ordinal', 'stream', 'declared_count', 'reassembled_bytes',
            'reassembled_sha256', 'standard_bzip2_signature',
            'standard_gzip_signature', 'standard_zlib_header',
            'observed_in_index_order') 'transfer'
        $null = Get-BoundedInteger $transfer 'ordinal' 'transfer' 1 16
        $null = Get-BoundedInteger $transfer 'stream' 'transfer' 0 1
        $null = Get-BoundedInteger $transfer 'declared_count' 'transfer' 1 64
        $null = Get-BoundedInteger $transfer 'reassembled_bytes' 'transfer' 1 65536
        if ($transfer.reassembled_sha256 -cnotmatch '^[0-9A-F]{64}$') {
            throw 'Transfer digest is malformed.'
        }
        $null = Get-StrictBoolean $transfer 'standard_bzip2_signature' 'transfer'
        $null = Get-StrictBoolean $transfer 'standard_gzip_signature' 'transfer'
        $null = Get-StrictBoolean $transfer 'standard_zlib_header' 'transfer'
        $null = Get-StrictBoolean $transfer 'observed_in_index_order' 'transfer'
    }
    Assert-ExactProperties $transfers[0] @(
        'ordinal', 'stream', 'declared_count', 'reassembled_bytes',
        'reassembled_sha256', 'standard_bzip2_signature',
        'standard_gzip_signature', 'standard_zlib_header',
        'observed_in_index_order') 'transfer'
    if ((Get-BoundedInteger $transfers[0] 'ordinal' 'transfer' 1 1) -ne 1 -or
        (Get-BoundedInteger $transfers[0] 'stream' 'transfer' 0 0) -ne 0 -or
        (Get-BoundedInteger $transfers[0] 'declared_count' 'transfer' 5 5) -ne 5 -or
        (Get-BoundedInteger $transfers[0] 'reassembled_bytes' `
            'transfer' 4186 4186) -ne 4186 -or
        $transfers[0].reassembled_sha256 -cnotmatch '^[0-9A-F]{64}$' -or
        (Get-StrictBoolean $transfers[0] 'standard_bzip2_signature' 'transfer') -or
        (Get-StrictBoolean $transfers[0] 'standard_gzip_signature' 'transfer') -or
        (Get-StrictBoolean $transfers[0] 'standard_zlib_header' 'transfer') -or
        -not (Get-StrictBoolean $transfers[0] 'observed_in_index_order' 'transfer')) {
        throw 'First transfer summary changed from the accepted stock profile.'
    }

    $duplicateCount = Get-BoundedInteger $metadata `
        'duplicate_batch_datagrams_replayed' 'metadata' 0 5
    if (($Scenario -eq 'duplicate-server-batch' -and $duplicateCount -ne 5) -or
        ($Scenario -ne 'duplicate-server-batch' -and $duplicateCount -ne 0)) {
        throw 'Duplicate-batch replay cardinality is not exact.'
    }
    $postBoundary = @($metadata.post_boundary_client_reliable_transmissions)
    if ($postBoundary.Count -ne 1) {
        throw 'Stock client produced an unexpected number of post-boundary reliable responses.'
    }
    Assert-ExactProperties $postBoundary[0] @(
        'event_order', 'sequence', 'decoded_body_bytes',
        'decoded_body_sha256') 'post_boundary_reliable'
    $null = Get-BoundedInteger $postBoundary[0] 'event_order' `
        'post_boundary_reliable' 1 $packetCount
    $null = Get-BoundedInteger $postBoundary[0] 'sequence' `
        'post_boundary_reliable' 1 1073741823
    if ((Get-BoundedInteger $postBoundary[0] 'decoded_body_bytes' `
            'post_boundary_reliable' 37 37) -ne 37 -or
        $postBoundary[0].decoded_body_sha256 -cnotmatch '^[0-9A-F]{64}$') {
        throw 'Post-boundary reliable response metadata changed.'
    }

    $events = @($metadata.events)
    if ($events.Count -ne $packetCount) {
        throw 'Event count does not equal packet_count.'
    }
    $connectionlessDirections = @('c2s', 's2c', 'c2s', 's2c')
    $connectionlessClasses = @(
        'connectionless-0x67', 'connectionless-0x41',
        'connectionless-0x63', 'connectionless-0x42')
    $eventByteTotal = 0L
    for ($eventIndex = 0; $eventIndex -lt $events.Count; ++$eventIndex) {
        $event = $events[$eventIndex]
        Assert-ExactProperties $event @(
            'order', 'direction', 'elapsed_microseconds', 'bytes', 'class',
            'sequence', 'reliable_present', 'fragmented', 'acknowledgement',
            'reliable_ack', 'decoded_body_bytes', 'transformed_complete_words',
            'unchanged_tail_bytes', 'descriptor_area_bytes',
            'payload_area_bytes', 'fragment_slots') 'event'
        if ($event.direction -cnotin @('c2s', 's2c') -or
            $event.class -cnotmatch '^(connectionless-0x[0-9a-f]{2}|sequenced)$') {
            throw 'Event direction or class is outside the narrow schema.'
        }
        if ((Get-BoundedInteger $event 'order' 'event' 1 $packetCount) -ne
                ($eventIndex + 1)) {
            throw 'Event order is not contiguous.'
        }
        $null = Get-BoundedInteger $event 'elapsed_microseconds' 'event' `
            0 ($TimeoutSeconds * 1000000L)
        $eventBytes = Get-BoundedInteger $event 'bytes' 'event' `
            1 $maximumDatagramBytes
        $eventByteTotal += $eventBytes
        if ($eventIndex -lt 4) {
            if ($event.direction -cne $connectionlessDirections[$eventIndex] -or
                $event.class -cne $connectionlessClasses[$eventIndex]) {
                throw 'Connectionless getchallenge/connect/ACCEPT order changed.'
            }
            if ($eventIndex -eq 0 -and $eventBytes -ne 23) {
                throw 'Endpoint-learning getchallenge datagram is not canonical length.'
            }
        }
        elseif ($event.class -cne 'sequenced') {
            throw 'A connectionless packet appeared after ACCEPT.'
        }
        if ($event.class -ne 'sequenced') {
            foreach ($name in @(
                    'sequence', 'reliable_present', 'fragmented', 'acknowledgement',
                    'reliable_ack', 'decoded_body_bytes',
                    'transformed_complete_words', 'unchanged_tail_bytes',
                    'descriptor_area_bytes', 'payload_area_bytes',
                    'fragment_slots')) {
                if ($null -ne $event.PSObject.Properties[$name].Value) {
                    throw 'Connectionless event contains sequenced metadata.'
                }
            }
            continue
        }
        $null = Get-BoundedInteger $event 'sequence' 'event' 1 1073741823
        $null = Get-StrictBoolean $event 'reliable_present' 'event'
        $isFragmented = Get-StrictBoolean $event 'fragmented' 'event'
        $null = Get-BoundedInteger $event 'acknowledgement' 'event' 0 1073741823
        $null = Get-StrictBoolean $event 'reliable_ack' 'event'
        $bodyBytes = Get-BoundedInteger $event 'decoded_body_bytes' 'event' 0 `
            ($maximumDatagramBytes - 8)
        if ($eventBytes -ne ($bodyBytes + 8)) {
            throw 'Sequenced datagram length does not match its decoded body length.'
        }
        $null = Get-BoundedInteger $event 'transformed_complete_words' `
            'event' 0 ([Math]::Floor(($maximumDatagramBytes - 8) / 4))
        $null = Get-BoundedInteger $event 'unchanged_tail_bytes' 'event' 0 3
        if (-not $isFragmented) {
            foreach ($name in @(
                    'descriptor_area_bytes', 'payload_area_bytes',
                    'fragment_slots')) {
                if ($null -ne $event.PSObject.Properties[$name].Value) {
                    throw 'Unfragmented event contains fragment metadata.'
                }
            }
            continue
        }
        $descriptorBytes = Get-BoundedInteger $event `
            'descriptor_area_bytes' 'event' 2 18
        $payloadBytes = Get-BoundedInteger $event `
            'payload_area_bytes' 'event' 1 $bodyBytes
        if (($descriptorBytes + $payloadBytes) -ne $bodyBytes) {
            throw 'Fragment descriptor and payload areas do not cover the decoded body.'
        }
        if (@($event.fragment_slots).Count -ne 2) {
            throw 'Fragment event does not contain exactly two descriptor slots.'
        }
        if ($null -eq $event.fragment_slots) { continue }
        for ($slotIndex = 0; $slotIndex -lt 2; ++$slotIndex) {
            $slot = @($event.fragment_slots)[$slotIndex]
            if ([bool]$slot.present) {
                Assert-ExactProperties $slot @(
                    'slot', 'present', 'fragment_id', 'packed_index',
                    'packed_count', 'offset', 'length',
                    'payload_sha256') 'fragment_slot'
                if (-not (Get-StrictBoolean $slot 'present' 'fragment_slot')) {
                    throw 'Present fragment slot reports present=false.'
                }
                if ((Get-BoundedInteger $slot 'slot' 'fragment_slot' 0 1) -ne
                        $slotIndex) {
                    throw 'Fragment slot order is not canonical.'
                }
                $null = Get-BoundedInteger $slot 'fragment_id' `
                    'fragment_slot' 1 4294967295
                $packedIndex = Get-BoundedInteger $slot `
                    'packed_index' 'fragment_slot' 1 64
                $packedCount = Get-BoundedInteger $slot `
                    'packed_count' 'fragment_slot' 1 64
                if ($packedIndex -gt $packedCount) {
                    throw 'Fragment index exceeds its declared count.'
                }
                $null = Get-BoundedInteger $slot 'offset' 'fragment_slot' 0 2048
                $null = Get-BoundedInteger $slot 'length' 'fragment_slot' 1 1024
                if ($slot.payload_sha256 -cnotmatch '^[0-9A-F]{64}$') {
                    throw 'Fragment slot digest is malformed.'
                }
            }
            else {
                Assert-ExactProperties $slot @('slot', 'present') 'absent_fragment_slot'
                if ((Get-BoundedInteger $slot 'slot' `
                        'absent_fragment_slot' 0 1) -ne $slotIndex) {
                    throw 'Absent fragment slot order is not canonical.'
                }
                if (Get-StrictBoolean $slot 'present' 'absent_fragment_slot') {
                    throw 'Absent fragment slot reports present=true.'
                }
            }
        }
    }
    if ($postAcceptPacketCount -ne ($packetCount - 4) -or
        $eventByteTotal -ne $totalBytes) {
        throw 'Event cardinality or byte accounting is inconsistent.'
    }

    foreach ($transmission in $transmissions) {
        $event = $events[[int]$transmission.event_order - 1]
        if ($event.direction -cne 'c2s' -or $event.class -cne 'sequenced' -or
            [long]$event.sequence -ne [long]$transmission.sequence -or
            [long]$event.acknowledgement -ne [long]$transmission.acknowledgement -or
            -not [bool]$event.reliable_present -or
            [long]$event.decoded_body_bytes -ne 8) {
            throw 'Initial request transmission does not match its packet event.'
        }
    }
    foreach ($ack in $requestAcks) {
        $event = $events[[int]$ack.event_order - 1]
        if ($event.direction -cne 's2c' -or $event.class -cne 'sequenced' -or
            [long]$event.sequence -ne [long]$ack.server_sequence -or
            [long]$event.acknowledgement -ne [long]$ack.acknowledgement -or
            -not [bool]$event.reliable_ack) {
            throw 'Request ACK metadata does not match its packet event.'
        }
    }
    $firstRequestAckEvent = $events[[int]$requestAcks[0].event_order - 1]
    $firstRequestAckSlot = @($firstRequestAckEvent.fragment_slots)[0]
    if (-not [bool]$firstRequestAckEvent.fragmented -or
        $null -eq $firstRequestAckSlot -or
        -not [bool]$firstRequestAckSlot.present -or
        [long]$firstRequestAckSlot.packed_index -ne 1 -or
        [long]$firstRequestAckSlot.packed_count -ne 5 -or
        [long]$firstRequestAckSlot.offset -ne 0 -or
        [long]$firstRequestAckSlot.length -ne 1024) {
        throw 'First request ACK is not carried by first-transfer fragment one.'
    }
    $requestAckFragmentRetryEventOrder = $null
    if ($Scenario -eq 'drop-request-ack') {
        $secondRequestAckEvent = $events[[int]$requestAcks[1].event_order - 1]
        if ([bool]$secondRequestAckEvent.fragmented -or
            [long]$secondRequestAckEvent.decoded_body_bytes -ne 8 -or
            [bool]$secondRequestAckEvent.reliable_present) {
            throw 'Forwarded request-ACK retry is not the observed transport-only packet.'
        }
        $fragmentRetries = @($events | Where-Object {
                $_.class -ceq 'sequenced' -and [bool]$_.fragmented -and
                [long]$_.order -gt [long]$firstRequestAckEvent.order -and
                $null -ne $_.fragment_slots -and
                [bool]$_.fragment_slots[0].present -and
                [long]$_.fragment_slots[0].fragment_id -eq
                    [long]$firstRequestAckSlot.fragment_id -and
                [long]$_.fragment_slots[0].packed_index -eq 1 -and
                [long]$_.fragment_slots[0].packed_count -eq 5 -and
                [long]$_.fragment_slots[0].offset -eq 0 -and
                [long]$_.fragment_slots[0].length -eq 1024 -and
                $_.fragment_slots[0].payload_sha256 -ceq
                    $firstRequestAckSlot.payload_sha256
            })
        if ($fragmentRetries.Count -ne 1) {
            throw 'Dropped first fragment lacks one exact later fragment-data retry.'
        }
        $requestAckFragmentRetryEventOrder = [long]$fragmentRetries[0].order
    }
    $allowedActions = @(
        'forward-once', 'drop-first-initial-request-transmission',
        'drop-first-covering-request-ack-packet',
        'replay-complete-first-server-batch')
    $actions = @($metadata.actions)
    $expectedActionCount = $packetCount +
        $(if ($Scenario -eq 'duplicate-server-batch') { 1 } else { 0 })
    if ($actions.Count -ne $expectedActionCount) {
        throw 'Action count does not match exact per-event accounting.'
    }
    foreach ($action in $actions) {
        Assert-ExactProperties $action @(
            'elapsed_microseconds', 'action', 'event_order',
            'related_event_order') 'action'
        if ($action.action -cnotin $allowedActions) {
            throw 'Relay action is outside the narrow scenario schema.'
        }
        $actionElapsed = Get-BoundedInteger $action `
            'elapsed_microseconds' 'action' `
            0 ($TimeoutSeconds * 1000000L)
        $actionEventOrder = Get-BoundedInteger $action `
            'event_order' 'action' 1 $packetCount
        if ($null -ne $action.related_event_order) {
            throw 'Accepted stock actions must not carry an unbounded relationship.'
        }
        if ($actionElapsed -lt
            [long]$events[[int]$actionEventOrder - 1].elapsed_microseconds) {
            throw 'Relay action predates its packet event.'
        }
    }
    $mutationActions = @(
        'drop-first-initial-request-transmission',
        'drop-first-covering-request-ack-packet',
        'replay-complete-first-server-batch')
    $requiredMutationAction = switch ($Scenario) {
        'drop-initial-request' { 'drop-first-initial-request-transmission' }
        'drop-request-ack' { 'drop-first-covering-request-ack-packet' }
        'duplicate-server-batch' { 'replay-complete-first-server-batch' }
        default { $null }
    }
    foreach ($name in $mutationActions) {
        $expected = if ($name -eq $requiredMutationAction) { 1 } else { 0 }
        if (@($metadata.actions | Where-Object {
                    $_.action -ceq $name
                }).Count -ne $expected) {
            throw "Mutation action '$name' has invalid scenario cardinality."
        }
    }
    $primaryActions = @($actions | Where-Object {
            $_.action -cne 'replay-complete-first-server-batch'
        })
    if ($primaryActions.Count -ne $packetCount) {
        throw 'Each captured packet must have exactly one primary action.'
    }
    for ($actionIndex = 0; $actionIndex -lt $primaryActions.Count; ++$actionIndex) {
        $action = $primaryActions[$actionIndex]
        $eventOrder = $actionIndex + 1
        if ([long]$action.event_order -ne $eventOrder) {
            throw 'Primary actions do not cover packet events contiguously.'
        }
        $expectedAction = 'forward-once'
        if ($Scenario -eq 'drop-initial-request' -and
            $eventOrder -eq [long]$transmissions[0].event_order) {
            $expectedAction = 'drop-first-initial-request-transmission'
        }
        elseif ($Scenario -eq 'drop-request-ack' -and
            $eventOrder -eq [long]$requestAcks[0].event_order) {
            $expectedAction = 'drop-first-covering-request-ack-packet'
        }
        if ($action.action -cne $expectedAction) {
            throw 'Primary action does not match its exact scenario event.'
        }
    }
    foreach ($transmission in $transmissions) {
        $forwardedByAction = $primaryActions[
            [int]$transmission.event_order - 1].action -ceq 'forward-once'
        if ([bool]$transmission.forwarded -ne $forwardedByAction) {
            throw 'Request transmission forwarding disagrees with its action.'
        }
    }
    foreach ($ack in $requestAcks) {
        $forwardedByAction = $primaryActions[
            [int]$ack.event_order - 1].action -ceq 'forward-once'
        if ([bool]$ack.forwarded -ne $forwardedByAction) {
            throw 'Request ACK forwarding disagrees with its action.'
        }
    }

    $ackLinks = @($metadata.fragment_acknowledgements)
    if ($ackLinks.Count -lt 5 -or $ackLinks.Count -gt 64) {
        throw 'Fragment ACK link count is outside its bound.'
    }
    foreach ($link in $ackLinks) {
        Assert-ExactProperties $link @(
            'fragment_event_order', 'transfer_ordinal', 'fragment_index',
            'fragment_sequence', 'ack_event_order', 'acknowledgement',
            'reliable_ack', 'elapsed_microseconds') 'fragment_ack'
        $null = Get-BoundedInteger $link 'fragment_event_order' `
            'fragment_ack' 1 $packetCount
        $null = Get-BoundedInteger $link 'transfer_ordinal' 'fragment_ack' 1 16
        $null = Get-BoundedInteger $link 'fragment_index' 'fragment_ack' 1 64
        $null = Get-BoundedInteger $link 'fragment_sequence' `
            'fragment_ack' 1 1073741823
        $null = Get-BoundedInteger $link 'ack_event_order' `
            'fragment_ack' 1 $packetCount
        $null = Get-BoundedInteger $link 'acknowledgement' `
            'fragment_ack' 1 1073741823
        $null = Get-StrictBoolean $link 'reliable_ack' 'fragment_ack'
        $linkElapsed = Get-BoundedInteger $link 'elapsed_microseconds' `
            'fragment_ack' 0 ($TimeoutSeconds * 1000000L)
        $fragmentEvent = $events[[int]$link.fragment_event_order - 1]
        $ackEvent = $events[[int]$link.ack_event_order - 1]
        $matchingSlots = @($fragmentEvent.fragment_slots | Where-Object {
                [bool]$_.present -and
                [long]$_.packed_index -eq [long]$link.fragment_index
            })
        if ($fragmentEvent.direction -cne 's2c' -or
            -not [bool]$fragmentEvent.fragmented -or
            [long]$fragmentEvent.sequence -ne [long]$link.fragment_sequence -or
            $matchingSlots.Count -ne 1 -or
            $ackEvent.direction -cne 'c2s' -or $ackEvent.class -cne 'sequenced' -or
            [long]$link.ack_event_order -le [long]$link.fragment_event_order -or
            [long]$ackEvent.acknowledgement -ne [long]$link.acknowledgement -or
            [bool]$ackEvent.reliable_ack -ne [bool]$link.reliable_ack -or
            $linkElapsed -ne ([long]$ackEvent.elapsed_microseconds -
                [long]$fragmentEvent.elapsed_microseconds)) {
            throw 'Fragment ACK link does not match its packet events.'
        }
    }
    $firstTransferLinks = @($ackLinks | Where-Object transfer_ordinal -eq 1 |
        Sort-Object fragment_index)
    if ($firstTransferLinks.Count -ne 5) {
        throw 'First transfer lacks exactly five fragment ACK links.'
    }
    for ($index = 0; $index -lt $firstTransferLinks.Count; ++$index) {
        $link = $firstTransferLinks[$index]
        if ((Get-BoundedInteger $link 'transfer_ordinal' 'fragment_ack' 1 1) -ne 1 -or
            (Get-BoundedInteger $link 'fragment_index' 'fragment_ack' 1 5) -ne
                ($index + 1) -or
            [bool]$link.reliable_ack -ne (($index % 2) -eq 0)) {
            throw 'Fragment ACK order/generation changed.'
        }
    }
    if ([long]$requestAcks[0].event_order -ge
        [long]$firstTransferLinks[4].fragment_event_order) {
        throw 'Request ACK did not precede complete first-transfer reassembly.'
    }
    if ($Scenario -eq 'drop-request-ack' -and
        [long]$firstTransferLinks[0].fragment_event_order -ne
            [long]$requestAckFragmentRetryEventOrder) {
        throw 'First accepted fragment is not the exact retry of the dropped fragment.'
    }
    if ($Scenario -eq 'duplicate-server-batch') {
        $replayActions = @($actions | Where-Object {
                $_.action -ceq 'replay-complete-first-server-batch'
            })
        if ($replayActions.Count -ne 1 -or
            [long]$replayActions[0].event_order -ne
                [long]$firstTransferLinks[4].fragment_event_order) {
            throw 'Complete first-server-batch replay is not tied to transfer completion.'
        }
    }

    return [pscustomobject]@{
        PacketCount = $packetCount
        TransmissionCount = $transmissions.Count
        WrongSourceCount = $wrongSourceCount
    }
}

if ($PSCmdlet.ParameterSetName -eq 'ValidateMetadata') {
    $validationPath = Resolve-ExplicitFile `
        -Path $ValidateMetadataPath -Label 'ValidateMetadataPath'
    if ([IO.Path]::GetExtension($validationPath) -cne '.json') {
        throw 'ValidateMetadataPath must name a .json file.'
    }
    $validationItem = Get-Item -LiteralPath $validationPath -Force
    Assert-OnlyDefaultDataStream -Path $validationPath -Label 'Validation metadata'
    if ($validationItem.Length -le 0 -or $validationItem.Length -gt 2097152) {
        throw 'Validation metadata is empty or exceeds two MiB.'
    }
    $validationMetadata = Get-Content -Raw -LiteralPath $validationPath |
        ConvertFrom-Json -ErrorAction Stop
    $validationResult = Assert-AcceptedSignonMetadata `
        -Metadata $validationMetadata -RelayScenario $relayScenario
    Write-Output (
        'metadata-valid scenario={0} packets={1} requestTx={2} boundary=11@42' -f
        $Scenario, $validationResult.PacketCount,
        $validationResult.TransmissionCount)
    return
}

$resolvedRelay = Resolve-ExplicitFile -Path $RelayPath -Label 'RelayPath'
$hlPath = Resolve-ExplicitFile -Path $HalfLifePath -Label 'HalfLifePath'
$hldsPath = Resolve-ExplicitFile -Path $HldsPath -Label 'HldsPath'
$resolvedPython = $null
if ($PythonPath) {
    $resolvedPython = Resolve-ExplicitFile -Path $PythonPath -Label 'PythonPath'
    if ([IO.Path]::GetFileName($resolvedPython) -ine 'python.exe') {
        throw 'PythonPath must explicitly name python.exe.'
    }
}
if ([IO.Path]::GetFileName($hlPath) -ine 'hl.exe') {
    throw 'HalfLifePath must explicitly name hl.exe.'
}
if ([IO.Path]::GetFileName($hldsPath) -ine 'hlds.exe') {
    throw 'HldsPath must explicitly name hlds.exe.'
}
$relayExtension = [IO.Path]::GetExtension($resolvedRelay)
if ($relayExtension -ine '.ps1' -and $relayExtension -ine '.exe') {
    throw 'RelayPath must explicitly name a .ps1 or .exe bounded relay.'
}

foreach ($resolvedPath in @($resolvedRelay, $hlPath, $hldsPath, $resolvedPython)) {
    if ($null -ne $resolvedPath) {
        Assert-NoReparsePointInExistingPath -Path $resolvedPath -Label 'Explicit input'
    }
}

$existing = @(Get-Process -Name hl,hlds -ErrorAction SilentlyContinue |
    Where-Object { -not $_.HasExited })
if ($existing.Count -ne 0) {
    throw 'Refusing to run while an unrelated hl.exe or hlds.exe process exists.'
}
foreach ($port in @($serverPort, $relayPort)) {
    if (Get-NetUDPEndpoint -LocalPort $port -ErrorAction SilentlyContinue) {
        throw "Selected private loopback UDP port $port is already occupied."
    }
}
foreach ($binary in @($hlPath, $hldsPath)) {
    $signature = Get-AuthenticodeSignature -LiteralPath $binary
    if ($signature.Status -ne 'Valid' -or $null -eq $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -cnotmatch '^CN=Valve Corp\.(?:,|$)') {
        throw "Reference binary is not validly Valve-signed: $binary"
    }
}
if ((Get-Item -LiteralPath $hlPath).VersionInfo.FileVersion -cne '1, 1, 1, 1') {
    throw 'Reference client version is not 1.1.1.1.'
}
# This is the signed launcher VERSIONINFO. The controlled server profile reports
# engine 1.1.2.2 / Protocol 48 / build 10210 separately on its protocol surface.
if ((Get-Item -LiteralPath $hldsPath).VersionInfo.FileVersion -cne '4, 1, 1, 1') {
    throw 'Reference hlds.exe launcher VERSIONINFO is not 4.1.1.1.'
}

Assert-NoReparsePointInExistingPath -Path $artifactRoot -Label 'Artifact root'
[IO.Directory]::CreateDirectory($artifactRoot) | Out-Null
Assert-NoReparsePointInExistingPath -Path $artifactRoot -Label 'Artifact root'
$runDirectory = [IO.Path]::GetFullPath((Join-Path $artifactRoot (
    'verified-' + $Scenario + '-' + $timestamp)))
if (-not $runDirectory.StartsWith(
        $artifactRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Computed run directory escaped the bounded artifact root.'
}
if (Test-Path -LiteralPath $runDirectory) {
    throw 'Refusing to reuse an existing sign-on capture directory.'
}
[IO.Directory]::CreateDirectory($runDirectory) | Out-Null

try {
    $serverProcess = Start-Process -FilePath $hldsPath -ArgumentList @(
        '-console', '-game', $Game, '-nomaster', '+ip', '127.0.0.1',
        '+maxplayers', '2', '+map', $Map,
        '-port', $serverPort.ToString([Globalization.CultureInfo]::InvariantCulture),
        '+sv_lan', '1'
    ) -WorkingDirectory (Split-Path -Parent $hldsPath) -WindowStyle Hidden -PassThru
    $serverRecord = New-OwnedProcessRecord -Process $serverProcess -ExpectedExecutable $hldsPath
    $serverIdentityDeadline = [DateTime]::UtcNow.AddSeconds(2)
    $serverIdentityVerified = $false
    do {
        if ($serverProcess.HasExited) { break }
        $serverIdentityVerified = Test-OwnedProcessIdentity -Record $serverRecord
        if ($serverIdentityVerified) { break }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $serverIdentityDeadline)
    if (-not $serverIdentityVerified) {
        throw 'Owned stock server failed its immediate identity check.'
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while (-not (Test-ServerReady)) {
        if ($serverProcess.HasExited) { throw 'Owned stock server exited during startup.' }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Owned stock server startup timed out.' }
        Start-Sleep -Milliseconds 250
    }

    $relayArguments = @(
        '-ListenPort', $relayPort.ToString([Globalization.CultureInfo]::InvariantCulture),
        '-ServerPort', $serverPort.ToString([Globalization.CultureInfo]::InvariantCulture),
        '-OutputDirectory', (Quote-NativePathArgument -Path $runDirectory),
        '-Scenario', $relayScenario,
        '-TimeoutSeconds', $TimeoutSeconds.ToString([Globalization.CultureInfo]::InvariantCulture),
        '-MaximumPackets', $maximumPackets.ToString([Globalization.CultureInfo]::InvariantCulture),
        '-MaximumPostAcceptPackets', $maximumPostAcceptPackets.ToString(
            [Globalization.CultureInfo]::InvariantCulture),
        '-MaximumDatagramBytes', $maximumDatagramBytes.ToString(
            [Globalization.CultureInfo]::InvariantCulture),
        '-MaximumTotalBytes', $maximumTotalBytes.ToString(
            [Globalization.CultureInfo]::InvariantCulture)
    )
    if ($resolvedPython) {
        $relayArguments = @(
            '-PythonPath', (Quote-NativePathArgument -Path $resolvedPython)
        ) + $relayArguments
    }
    $relayExecutable = $resolvedRelay
    if ($relayExtension -ieq '.ps1') {
        $relayExecutable = $pwshPath
        $relayArguments = @(
            '-NoLogo', '-NoProfile', '-NonInteractive', '-File',
            (Quote-NativePathArgument -Path $resolvedRelay)
        ) + $relayArguments
    }
    $relayProcess = Start-Process -FilePath $relayExecutable `
      -ArgumentList $relayArguments -WorkingDirectory (Split-Path -Parent $resolvedRelay) `
      -RedirectStandardOutput 'NUL' `
      -RedirectStandardError '\\.\NUL' -WindowStyle Hidden -PassThru
    $relayRecord = New-OwnedProcessRecord -Process $relayProcess `
        -ExpectedExecutable $relayExecutable
    $relayIdentityDeadline = [DateTime]::UtcNow.AddSeconds(2)
    $relayIdentityVerified = $false
    do {
        if ($relayProcess.HasExited) { break }
        $relayIdentityVerified = Test-OwnedProcessIdentity -Record $relayRecord
        if ($relayIdentityVerified) { break }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $relayIdentityDeadline)
    if (-not $relayIdentityVerified) {
        throw 'Owned relay failed its immediate identity check.'
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    $ready = $null
    do {
        if ($relayProcess.HasExited) { throw 'Owned relay exited before binding.' }
        $ready = Get-NetUDPEndpoint -LocalAddress '127.0.0.1' -LocalPort $relayPort `
            -ErrorAction SilentlyContinue | Where-Object OwningProcess -eq $relayProcess.Id
        if ($null -ne $ready) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($null -eq $ready) { throw 'Owned relay failed its bounded bind wait.' }

    $connectEndpoint = '127.0.0.1:{0}' -f $relayPort
    $clientProcess = Start-Process -FilePath $hlPath -ArgumentList @(
        '-game', $Game, '-console', '-windowed', '-w', '640', '-h', '480',
        '+connect', $connectEndpoint
    ) -WorkingDirectory (Split-Path -Parent $hlPath) -RedirectStandardOutput 'NUL' `
      -RedirectStandardError '\\.\NUL' -WindowStyle Minimized -PassThru
    $clientRecord = New-OwnedProcessRecord -Process $clientProcess -ExpectedExecutable $hlPath
    $identityDeadline = [DateTime]::UtcNow.AddSeconds(2)
    $clientIdentityVerified = $false
    do {
        if ($clientProcess.HasExited) { break }
        $clientIdentityVerified = Test-OwnedProcessIdentity -Record $clientRecord
        if ($clientIdentityVerified) { break }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $identityDeadline)
    if (-not $clientIdentityVerified) {
        throw 'Owned stock client failed its immediate identity check.'
    }

    if (-not $relayProcess.WaitForExit(($TimeoutSeconds + 5) * 1000)) {
        throw 'Owned relay exceeded its bounded lifetime plus grace period.'
    }
    if ($relayProcess.ExitCode -ne 0) {
        throw "Owned relay failed with exit code $($relayProcess.ExitCode)."
    }

    Assert-NoDescendantReparsePoint -Path $runDirectory -Label 'Run directory'
    $items = @(Get-ChildItem -LiteralPath $runDirectory -Force -Recurse)
    if ($items.Count -ne 1 -or $items[0].PSIsContainer -or
        $items[0].Name -cne 'metadata.json' -or
        $items[0].Extension -cne '.json') {
        throw 'Relay output must contain exactly one metadata.json file.'
    }
    if ($items[0].Length -le 0 -or $items[0].Length -gt 2097152) {
        throw 'Relay metadata is empty or exceeds two MiB.'
    }
    $metadataPath = $items[0].FullName
    Assert-OnlyDefaultDataStream -Path $metadataPath -Label 'Relay metadata'
    $metadata = Get-Content -Raw -LiteralPath $metadataPath |
        ConvertFrom-Json -ErrorAction Stop
    $validated = Assert-AcceptedSignonMetadata -Metadata $metadata `
        -RelayScenario $relayScenario

    $runSucceeded = $true
    Write-Output (
        'capture-complete scenario={0} packets={1} requestTx={2} boundary=11@42 wrongSourceIgnored={3}' -f
        $Scenario, $validated.PacketCount, $validated.TransmissionCount,
        $validated.WrongSourceCount)
    Write-Output ('metadata-only ignored artifact: {0}' -f $metadataPath)
}
finally {
    $cleanupErrors = [Collections.Generic.List[string]]::new()
    foreach ($record in @($clientRecord, $relayRecord, $serverRecord)) {
        try { Stop-VerifiedOwnedProcess -Record $record }
        catch { $cleanupErrors.Add($_.Exception.Message) }
    }
    try {
        $unexpected = @(Get-Process -Name hl,hlds -ErrorAction SilentlyContinue |
            Where-Object { -not $_.HasExited })
        if ($unexpected.Count -ne 0) {
            $cleanupErrors.Add(
                'Post-cleanup gate found an unowned stock process; it was not terminated.')
        }
    }
    catch { $cleanupErrors.Add('Unable to establish post-cleanup stock process state.') }
    try {
        foreach ($selectedPort in @($serverPort, $relayPort)) {
            if (Get-NetUDPEndpoint -LocalPort $selectedPort -ErrorAction SilentlyContinue) {
                $cleanupErrors.Add(
                    "Post-cleanup gate found selected UDP port $selectedPort occupied.")
            }
        }
    }
    catch { $cleanupErrors.Add('Unable to establish post-cleanup UDP port state.') }
    if ($cleanupErrors.Count -gt 0) { $runSucceeded = $false }
    if (-not $runSucceeded -and $null -ne $runDirectory -and
        $null -ne $artifactRoot -and (Test-Path -LiteralPath $runDirectory) -and
        $runDirectory.StartsWith(
            $artifactRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        try {
            Assert-NoReparsePointInExistingPath -Path $artifactRoot -Label 'Artifact root'
            Assert-NoReparsePointInExistingPath -Path $runDirectory -Label 'Run directory'
            Assert-NoDescendantReparsePoint -Path $runDirectory -Label 'Run directory'
            Remove-Item -LiteralPath $runDirectory -Recurse -Force
        }
        catch {
            $cleanupErrors.Add('Rejected metadata cleanup failed: ' + $_.Exception.Message)
        }
    }
    if ($cleanupErrors.Count -gt 0) {
        throw ('Owned-process cleanup failed: ' + ($cleanupErrors -join ' | '))
    }
}
