<#
.SYNOPSIS
Runs one bounded private-loopback stock reliable-netchan experiment.

.DESCRIPTION
The wrapper starts only the explicitly supplied Valve-signed Half-Life client,
Valve-signed HLDS launcher, and byte-preserving relay. It never accepts a
public address or an arbitrary packet-editing operation. The relay must write
only metadata.json under the wrapper-created, gitignored run directory; raw
packet, authentication, identity, and payload bytes are forbidden.

The relay interface is deliberately narrow:
  -ListenPort, -ServerPort, -OutputDirectory, -Scenario,
  -TimeoutSeconds, -MaximumPackets, -MaximumPostAcceptPackets,
  -MaximumDatagramBytes, and -MaximumTotalBytes.

Its summary must use schema
"hlclient.stock-reliable-netchan-metadata.v1" and the strict property sets
validated below. A scenario that does not report "bounded_complete" fails.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$RelayPath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$HalfLifePath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$HldsPath,

    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$Game = 'valve',

    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$Map = 'boot_camp',

    [ValidateRange(1024, 65534)]
    [int]$Port = 27320,

    [ValidateSet(
        'baseline',
        'drop-first-client-reliable',
        'drop-first-server-ack',
        'duplicate-client-reliable',
        'delay-stale-ack')]
    [string]$Scenario = 'baseline',

    [ValidateRange(8, 45)]
    [int]$TimeoutSeconds = 30
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$maximumPackets = 256
$maximumPostAcceptPackets = 200
$maximumDatagramBytes = 2048
$maximumTotalBytes = 262144
$loopbackAddress = '127.0.0.1'
$serverPort = $Port + 1
$serverRecord = $null
$relayRecord = $null
$clientRecord = $null
$runRoot = $null
$runSucceeded = $false

function Resolve-ExplicitFile {
    param([string]$Path, [string]$Label)

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
    $rootItem = Get-Item -LiteralPath $pathRoot -Force
    if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label path root must not be a reparse point."
    }

    $relativePath = $fullPath.Substring($pathRoot.Length)
    foreach ($component in @($relativePath -split '[\\/]' | Where-Object { $_ })) {
        $currentPath = [IO.Path]::Combine($currentPath, $component)
        if (-not (Test-Path -LiteralPath $currentPath)) { continue }
        $item = Get-Item -LiteralPath $currentPath -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Label path chain must not contain a reparse point."
        }
    }
}

function Assert-NoDescendantReparsePoint {
    param([string]$Path, [string]$Label)

    $pendingDirectories = [Collections.Generic.Queue[string]]::new()
    $pendingDirectories.Enqueue([IO.Path]::GetFullPath($Path))
    while ($pendingDirectories.Count -gt 0) {
        $directory = $pendingDirectories.Dequeue()
        foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force)) {
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "$Label must not contain a reparse point."
            }
            if ($item.PSIsContainer) {
                $pendingDirectories.Enqueue([IO.Path]::GetFullPath($item.FullName))
            }
        }
    }
}

function Quote-NativePathArgument {
    param([string]$Path)
    if ($Path.Contains('"')) { throw 'A native path argument cannot contain a quotation mark.' }
    return '"' + $Path + '"'
}

function Assert-ValveSignature {
    param([string]$Path, [string]$Label)
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid' -or
        $null -eq $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -notmatch 'Valve') {
        throw "$Label must be a validly Valve-signed executable."
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
    param([AllowNull()][object]$Record)
    if ($null -eq $Record) { return $false }
    $current = Get-Process -Id $Record.Id -ErrorAction SilentlyContinue
    if ($null -eq $current) { return $false }
    try {
        $cimRecord = Get-CimInstance Win32_Process -Filter (
            'ProcessId = {0}' -f $Record.Id) -ErrorAction Stop
        if ($null -eq $cimRecord -or
            [string]::IsNullOrWhiteSpace($cimRecord.ExecutablePath)) {
            return $false
        }
        $currentPath = [IO.Path]::GetFullPath(
            [string]$cimRecord.ExecutablePath).TrimEnd('\')
        $expectedPath = [IO.Path]::GetFullPath(
            [string]$Record.ExpectedExecutable).TrimEnd('\')
        $startDeltaMilliseconds = [Math]::Abs((
            $current.StartTime.ToUniversalTime() -
            $Record.StartTimeUtc).TotalMilliseconds)
        $pathMatches = $currentPath -ieq $expectedPath
        $startTimeMatches = $startDeltaMilliseconds -le 2.0
        return [bool]($pathMatches -and $startTimeMatches)
    }
    catch { return $false }
}

function Stop-VerifiedOwnedProcess {
    param([AllowNull()][object]$Record)
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
    catch [InvalidOperationException] {
        # The verified process exited after the identity check.
    }
}

function Test-ServerReady {
    param([Net.IPEndPoint]$ServerEndpoint)
    $socket = [Net.Sockets.Socket]::new(
        [Net.Sockets.AddressFamily]::InterNetwork,
        [Net.Sockets.SocketType]::Dgram,
        [Net.Sockets.ProtocolType]::Udp)
    try {
        $socket.Bind([Net.IPEndPoint]::new([Net.IPAddress]::Loopback, 0))
        $request = [byte[]](@(0xff, 0xff, 0xff, 0xff) +
            [Text.Encoding]::ASCII.GetBytes("getchallenge steam`n"))
        [void]$socket.SendTo($request, $ServerEndpoint)
        return $socket.Poll(500000, [Net.Sockets.SelectMode]::SelectRead)
    }
    finally { $socket.Dispose() }
}

function Assert-ExactProperties {
    param([object]$Value, [string[]]$Allowed, [string]$Label)
    foreach ($property in $Value.PSObject.Properties) {
        if ($property.Name -notin $Allowed) {
            throw "$Label contains forbidden property '$($property.Name)'."
        }
    }
}

function Get-RequiredProperty {
    param([object]$Value, [string]$Name, [string]$Label)
    $property = $Value.PSObject.Properties[$Name]
    if ($null -eq $property) { throw "$Label is missing required property '$Name'." }
    return $property.Value
}

function Get-BoundedInteger {
    param(
        [object]$Value,
        [string]$Name,
        [string]$Label,
        [long]$Minimum,
        [long]$Maximum
    )
    $actual = Get-RequiredProperty -Value $Value -Name $Name -Label $Label
    if ($actual -isnot [int] -and $actual -isnot [long]) {
        throw "$Label property '$Name' must be a JSON integer."
    }
    $parsed = [long]$actual
    if ($parsed -lt $Minimum -or $parsed -gt $Maximum) {
        throw "$Label property '$Name' is outside its bound."
    }
    return $parsed
}

function Assert-NullableBoundedInteger {
    param(
        [object]$Value,
        [string]$Name,
        [string]$Label,
        [long]$Minimum,
        [long]$Maximum,
        [bool]$AllowNull = $false
    )
    $actual = Get-RequiredProperty -Value $Value -Name $Name -Label $Label
    if ($null -eq $actual) {
        if ($AllowNull) { return }
        throw "$Label property '$Name' must not be null."
    }
    if ($actual -isnot [int] -and $actual -isnot [long]) {
        throw "$Label property '$Name' must be a JSON integer or allowed null."
    }
    $number = [long]$actual
    if ($number -lt $Minimum -or $number -gt $Maximum) {
        throw "$Label property '$Name' is outside its bound."
    }
}

function Assert-NullableBoolean {
    param(
        [object]$Value,
        [string]$Name,
        [string]$Label,
        [bool]$AllowNull = $false
    )
    $actual = Get-RequiredProperty -Value $Value -Name $Name -Label $Label
    if ($null -eq $actual) {
        if ($AllowNull) { return }
        throw "$Label property '$Name' must not be null."
    }
    if ($actual -isnot [bool]) {
        throw "$Label property '$Name' must be a Boolean or allowed null."
    }
}

function Assert-TrueBoolean {
    param([object]$Value, [string]$Name, [string]$Label)
    $actual = Get-RequiredProperty -Value $Value -Name $Name -Label $Label
    if ($actual -isnot [bool] -or -not $actual) {
        throw "$Label property '$Name' must be true."
    }
}

$resolvedRelay = Resolve-ExplicitFile -Path $RelayPath -Label 'RelayPath'
$resolvedClient = Resolve-ExplicitFile -Path $HalfLifePath -Label 'HalfLifePath'
$resolvedHlds = Resolve-ExplicitFile -Path $HldsPath -Label 'HldsPath'

if ([IO.Path]::GetExtension($resolvedClient) -ine '.exe') {
    throw 'HalfLifePath must name hl.exe.'
}
if ([IO.Path]::GetFileName($resolvedClient) -ine 'hl.exe') {
    throw 'HalfLifePath must explicitly name hl.exe.'
}
if ([IO.Path]::GetExtension($resolvedHlds) -ine '.exe') {
    throw 'HldsPath must name hlds.exe.'
}
if ([IO.Path]::GetFileName($resolvedHlds) -ine 'hlds.exe') {
    throw 'HldsPath must explicitly name hlds.exe.'
}
$relayExtension = [IO.Path]::GetExtension($resolvedRelay)
if ($relayExtension -ine '.exe' -and $relayExtension -ine '.ps1') {
    throw 'RelayPath must name an .exe or .ps1 bounded relay.'
}

Assert-ValveSignature -Path $resolvedClient -Label 'HalfLifePath'
Assert-ValveSignature -Path $resolvedHlds -Label 'HldsPath'
if ((Get-Item -LiteralPath $resolvedClient).VersionInfo.FileVersion -notmatch '^1, 1, 1, 1$') {
    throw 'HalfLifePath is not the stock 1.1.1.1 reference client.'
}

foreach ($selectedPort in @($Port, $serverPort)) {
    if (Get-NetUDPEndpoint -LocalPort $selectedPort -ErrorAction SilentlyContinue) {
        throw "Selected private test UDP port $selectedPort is already occupied."
    }
}

$scenarioMap = @{
    'baseline' = 'Baseline'
    'drop-first-client-reliable' = 'DropFirstClientReliable'
    'drop-first-server-ack' = 'DropFirstServerAck'
    'duplicate-client-reliable' = 'DuplicateFirstClientReliable'
    'delay-stale-ack' = 'ReplayStaleAck'
}
$relayScenario = $scenarioMap[$Scenario]
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifactRoot = [IO.Path]::GetFullPath((
    Join-Path $repositoryRoot 'manual-artifacts\netchan-reliable-captures'))
Assert-NoReparsePointInExistingPath -Path $artifactRoot -Label 'Artifact root'
[IO.Directory]::CreateDirectory($artifactRoot) | Out-Null
Assert-NoReparsePointInExistingPath -Path $artifactRoot -Label 'Artifact root'
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$runRoot = [IO.Path]::GetFullPath((Join-Path $artifactRoot "verified-$Scenario-$timestamp"))
if (-not $runRoot.StartsWith(
    $artifactRoot + [IO.Path]::DirectorySeparatorChar,
    [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Computed run directory escaped the bounded artifact root.'
}
if (Test-Path -LiteralPath $runRoot) { throw 'Refusing to reuse a run directory.' }
[IO.Directory]::CreateDirectory($runRoot) | Out-Null
$summaryPath = Join-Path $runRoot 'metadata.json'
$serverEndpoint = [Net.IPEndPoint]::new([Net.IPAddress]::Loopback, $serverPort)

try {
    $serverProcess = Start-Process -FilePath $resolvedHlds -ArgumentList @(
        '-console', '-game', $Game, '-nomaster', '+ip', $loopbackAddress,
        '+maxplayers', '2', '+map', $Map,
        '-port', $serverPort.ToString([Globalization.CultureInfo]::InvariantCulture),
        '+sv_lan', '1'
    ) -WorkingDirectory (Split-Path -Parent $resolvedHlds) -WindowStyle Hidden -PassThru
    $serverRecord = New-OwnedProcessRecord -Process $serverProcess `
        -ExpectedExecutable $resolvedHlds
    if (-not (Test-OwnedProcessIdentity -Record $serverRecord)) {
        throw 'Owned stock HLDS failed its immediate identity check.'
    }

    $serverDeadline = [DateTime]::UtcNow.AddSeconds(15)
    while (-not (Test-ServerReady -ServerEndpoint $serverEndpoint)) {
        if ($serverProcess.HasExited) { throw 'Owned stock HLDS exited during startup.' }
        if ([DateTime]::UtcNow -ge $serverDeadline) {
            throw 'Owned stock HLDS startup timed out.'
        }
        Start-Sleep -Milliseconds 250
    }

    $relayArguments = @(
        '-ListenPort', $Port.ToString([Globalization.CultureInfo]::InvariantCulture),
        '-ServerPort', $serverPort.ToString([Globalization.CultureInfo]::InvariantCulture),
        '-OutputDirectory', (Quote-NativePathArgument -Path $runRoot),
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
    $relayExecutable = $resolvedRelay
    if ($relayExtension -ieq '.ps1') {
        $relayExecutable = [IO.Path]::GetFullPath((Get-Process -Id $PID).Path)
        $relayArguments = @(
            '-NoLogo', '-NoProfile', '-NonInteractive', '-File',
            (Quote-NativePathArgument -Path $resolvedRelay)
        ) + $relayArguments
    }

    $relayProcess = Start-Process -FilePath $relayExecutable `
        -ArgumentList $relayArguments -WorkingDirectory (Split-Path -Parent $resolvedRelay) `
        -RedirectStandardOutput 'NUL' -RedirectStandardError '\\.\NUL' `
        -WindowStyle Hidden -PassThru
    $relayRecord = New-OwnedProcessRecord -Process $relayProcess `
        -ExpectedExecutable $relayExecutable
    if (-not (Test-OwnedProcessIdentity -Record $relayRecord)) {
        throw 'Owned bounded relay failed its immediate identity check.'
    }

    $relayDeadline = [DateTime]::UtcNow.AddSeconds(5)
    $relayReady = $null
    do {
        if ($relayProcess.HasExited) { throw 'Owned relay exited before binding.' }
        $relayReady = Get-NetUDPEndpoint -LocalAddress $loopbackAddress -LocalPort $Port `
            -ErrorAction SilentlyContinue | Where-Object OwningProcess -eq $relayProcess.Id
        if ($null -ne $relayReady) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $relayDeadline)
    if ($null -eq $relayReady) { throw 'Owned relay failed its bounded bind wait.' }

    $clientEndpoint = "${loopbackAddress}:$Port"
    $clientProcess = Start-Process -FilePath $resolvedClient -ArgumentList @(
        '-game', $Game, '-console', '-windowed', '-w', '640', '-h', '480',
        '+connect', $clientEndpoint
    ) -WorkingDirectory (Split-Path -Parent $resolvedClient) `
      -RedirectStandardOutput 'NUL' -RedirectStandardError '\\.\NUL' `
      -WindowStyle Hidden -PassThru
    $clientRecord = New-OwnedProcessRecord -Process $clientProcess `
        -ExpectedExecutable $resolvedClient
    if (-not (Test-OwnedProcessIdentity -Record $clientRecord)) {
        throw 'Owned stock client failed its immediate identity check.'
    }

    if (-not $relayProcess.WaitForExit(($TimeoutSeconds + 5) * 1000)) {
        throw 'Owned relay exceeded its timeout plus five-second grace period.'
    }
    if ($relayProcess.ExitCode -ne 0) {
        throw "Owned relay exited with code $($relayProcess.ExitCode)."
    }

    $items = @(Get-ChildItem -LiteralPath $runRoot -Force -Recurse)
    if ($items.Count -ne 1 -or $items[0].PSIsContainer -or
        $items[0].Name -ne 'metadata.json' -or
        ($items[0].Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Relay output contained a forbidden file; only metadata.json is allowed.'
    }
    if ($items[0].Length -le 0 -or $items[0].Length -gt 1048576) {
        throw 'Relay metadata is empty or exceeds one MiB.'
    }
    $summary = Get-Content -Raw -LiteralPath $summaryPath | ConvertFrom-Json -ErrorAction Stop

    $summaryProperties = @(
        'schema', 'profile', 'scenario', 'completion', 'loopback_only',
        'same_upstream_socket', 'exact_server_endpoint_validation',
        'raw_packet_bytes_stored', 'elapsed_milliseconds', 'packet_count',
        'post_accept_packet_count', 'total_bytes', 'client_reliable_observations',
        'client_sequenced_after_accept', 'server_sequenced_dropped',
        'maximum_packets', 'maximum_post_accept_packets', 'maximum_datagram_bytes',
        'maximum_total_bytes', 'timeout_seconds', 'connect_seen', 'accept_seen',
        'events', 'actions'
    )
    Assert-ExactProperties -Value $summary -Allowed $summaryProperties -Label 'summary'
    if ((Get-RequiredProperty -Value $summary -Name 'schema' -Label 'summary') -ne
        'hlclient.stock-reliable-netchan-metadata.v1') {
        throw 'Relay summary uses an unsupported schema.'
    }
    if ((Get-RequiredProperty -Value $summary -Name 'scenario' -Label 'summary') -ne
        $relayScenario) {
        throw 'Relay summary scenario does not match the requested scenario.'
    }
    if ((Get-RequiredProperty -Value $summary -Name 'profile' -Label 'summary') -ne
        'stock-hl-1.1.1.1-to-stock-hlds-protocol48-build10210') {
        throw 'Relay summary uses an unsupported stock profile.'
    }
    if ((Get-RequiredProperty -Value $summary -Name 'completion' -Label 'summary') -ne
        'bounded_complete') {
        throw 'Relay scenario did not reach its bounded proof condition.'
    }
    Assert-TrueBoolean -Value $summary -Name 'loopback_only' -Label 'summary'
    Assert-TrueBoolean -Value $summary -Name 'same_upstream_socket' -Label 'summary'
    Assert-TrueBoolean -Value $summary -Name 'exact_server_endpoint_validation' -Label 'summary'
    Assert-TrueBoolean -Value $summary -Name 'connect_seen' -Label 'summary'
    Assert-TrueBoolean -Value $summary -Name 'accept_seen' -Label 'summary'
    $rawStored = Get-RequiredProperty -Value $summary -Name 'raw_packet_bytes_stored' `
        -Label 'summary'
    if ($rawStored -isnot [bool] -or $rawStored) {
        throw 'Relay must report raw_packet_bytes_stored=false.'
    }

    $packetCount = Get-BoundedInteger -Value $summary -Name 'packet_count' `
        -Label 'summary' -Minimum 1 -Maximum $maximumPackets
    $postAcceptCount = Get-BoundedInteger -Value $summary -Name 'post_accept_packet_count' `
        -Label 'summary' -Minimum 1 -Maximum $maximumPostAcceptPackets
    if ($postAcceptCount -gt $packetCount) {
        throw 'post_accept_packet_count exceeds packet_count.'
    }
    $byteCount = Get-BoundedInteger -Value $summary -Name 'total_bytes' `
        -Label 'summary' -Minimum 1 -Maximum $maximumTotalBytes
    $elapsedMilliseconds = Get-BoundedInteger -Value $summary -Name 'elapsed_milliseconds' `
        -Label 'summary' -Minimum 0 -Maximum ($TimeoutSeconds * 1000L)
    $reliableObservations = Get-BoundedInteger -Value $summary `
        -Name 'client_reliable_observations' -Label 'summary' -Minimum 1 -Maximum $packetCount
    $null = Get-BoundedInteger -Value $summary -Name 'client_sequenced_after_accept' `
        -Label 'summary' -Minimum 1 -Maximum $packetCount
    $serverSequencedDropped = Get-BoundedInteger -Value $summary `
        -Name 'server_sequenced_dropped' `
        -Label 'summary' -Minimum 0 -Maximum $packetCount
    $expectedServerSequencedDropped = if ($Scenario -eq 'drop-first-server-ack') {
        1L
    } else {
        0L
    }
    if ($serverSequencedDropped -ne $expectedServerSequencedDropped) {
        throw 'server_sequenced_dropped does not match the fixed scenario policy.'
    }
    $reportedBounds = [ordered]@{
        maximum_packets = $maximumPackets
        maximum_post_accept_packets = $maximumPostAcceptPackets
        maximum_datagram_bytes = $maximumDatagramBytes
        maximum_total_bytes = $maximumTotalBytes
        timeout_seconds = $TimeoutSeconds
    }
    foreach ($boundName in $reportedBounds.Keys) {
        $expectedBound = [long]$reportedBounds[$boundName]
        $reported = Get-BoundedInteger -Value $summary -Name $boundName `
            -Label 'summary' -Minimum $expectedBound -Maximum $expectedBound
        if ($reported -ne $expectedBound) { throw 'Relay changed a requested hard bound.' }
    }

    $events = @(Get-RequiredProperty -Value $summary -Name 'events' -Label 'summary')
    $actions = @(Get-RequiredProperty -Value $summary -Name 'actions' -Label 'summary')
    if ($events.Count -ne $packetCount) { throw 'Summary event count does not match packet_count.' }

    $eventProperties = @(
        'order', 'direction', 'elapsed_microseconds', 'bytes', 'class', 'sequence',
        'reliable_present', 'fragmented', 'acknowledgement', 'reliable_ack',
        'decoded_payload_bytes', 'client_reliable_ordinal',
        'equals_previous_client_reliable',
        'common_prefix_with_previous_client_reliable',
        'common_suffix_with_previous_client_reliable'
    )
    $summedEventBytes = 0L
    $observedReliableEvents = 0L
    for ($eventIndex = 0; $eventIndex -lt $events.Count; ++$eventIndex) {
        $event = $events[$eventIndex]
        Assert-ExactProperties -Value $event -Allowed $eventProperties -Label 'event'
        if ($event.direction -notin @('c2s', 's2c')) {
            throw 'Event direction is not from the fixed allowlist.'
        }
        if ([string]$event.class -notmatch
            '^(connectionless-(header-only|0x[0-9a-f]{2})|sequenced|sequenced-short|malformed-short)$') {
            throw 'Event class is not from the fixed allowlist.'
        }
        Assert-NullableBoundedInteger -Value $event -Name 'order' -Label 'event' `
            -Minimum 1 -Maximum $packetCount
        Assert-NullableBoundedInteger -Value $event -Name 'elapsed_microseconds' `
            -Label 'event' -Minimum 0 -Maximum ($TimeoutSeconds * 1000000L)
        Assert-NullableBoundedInteger -Value $event -Name 'bytes' -Label 'event' `
            -Minimum 1 -Maximum $maximumDatagramBytes
        if ([long]$event.order -ne ($eventIndex + 1)) {
            throw 'Event order is not contiguous and one-based.'
        }
        $summedEventBytes += [long]$event.bytes
        Assert-NullableBoundedInteger -Value $event -Name 'sequence' -Label 'event' `
            -Minimum 0 -Maximum 1073741823 -AllowNull $true
        Assert-NullableBoolean -Value $event -Name 'reliable_present' -Label 'event' `
            -AllowNull $true
        Assert-NullableBoolean -Value $event -Name 'fragmented' -Label 'event' `
            -AllowNull $true
        Assert-NullableBoundedInteger -Value $event -Name 'acknowledgement' -Label 'event' `
            -Minimum 0 -Maximum 1073741823 -AllowNull $true
        Assert-NullableBoolean -Value $event -Name 'reliable_ack' -Label 'event' `
            -AllowNull $true
        Assert-NullableBoundedInteger -Value $event -Name 'decoded_payload_bytes' `
            -Label 'event' -Minimum 0 -Maximum ($maximumDatagramBytes - 8) -AllowNull $true
        Assert-NullableBoundedInteger -Value $event -Name 'client_reliable_ordinal' `
            -Label 'event' -Minimum 1 -Maximum $packetCount -AllowNull $true
        Assert-NullableBoolean -Value $event -Name 'equals_previous_client_reliable' `
            -Label 'event' -AllowNull $true
        Assert-NullableBoundedInteger -Value $event `
            -Name 'common_prefix_with_previous_client_reliable' -Label 'event' `
            -Minimum 0 -Maximum ($maximumDatagramBytes - 8) -AllowNull $true
        Assert-NullableBoundedInteger -Value $event `
            -Name 'common_suffix_with_previous_client_reliable' -Label 'event' `
            -Minimum 0 -Maximum ($maximumDatagramBytes - 8) -AllowNull $true
        if ($event.class -eq 'sequenced' -and
            ($null -eq $event.sequence -or
             $null -eq $event.reliable_present -or
             $null -eq $event.fragmented -or
             $null -eq $event.acknowledgement -or
             $null -eq $event.reliable_ack -or
             $null -eq $event.decoded_payload_bytes)) {
            throw 'A sequenced event must contain complete decoded header metadata.'
        }
        if ($event.direction -eq 'c2s' -and $event.class -eq 'sequenced' -and
            $event.reliable_present -eq $true -and
            $null -eq $event.client_reliable_ordinal) {
            throw 'A client reliable event must contain its bounded ordinal.'
        }
        if ($event.direction -eq 'c2s' -and $event.class -eq 'sequenced' -and
            $event.reliable_present) {
            ++$observedReliableEvents
        }
    }
    if ($summedEventBytes -ne $byteCount) {
        throw 'Sum of event byte counts does not match total_bytes.'
    }
    if ($observedReliableEvents -ne $reliableObservations) {
        throw 'Observed reliable event count does not match summary metadata.'
    }

    $actionProperties = @(
        'elapsed_microseconds', 'action', 'event_order', 'related_event_order'
    )
    $mutationActions = @(
        'drop-first-client-reliable',
        'drop-first-server-ack',
        'duplicate-first-client-reliable',
        'retain-copy-of-first-server-ack',
        'replay-stale-server-ack-after-second-client-reliable'
    )
    $requiredMutationCounts = @{
        'baseline' = @{}
        'drop-first-client-reliable' = @{
            'drop-first-client-reliable' = 1
        }
        'drop-first-server-ack' = @{
            'drop-first-server-ack' = 1
        }
        'duplicate-client-reliable' = @{
            'duplicate-first-client-reliable' = 1
        }
        'delay-stale-ack' = @{
            'retain-copy-of-first-server-ack' = 1
            'replay-stale-server-ack-after-second-client-reliable' = 1
        }
    }[$Scenario]
    $allowedActions = @('forward-once') + @($requiredMutationCounts.Keys)
    $supplementalActions = @(
        'retain-copy-of-first-server-ack',
        'replay-stale-server-ack-after-second-client-reliable'
    )
    $expectedSupplementalCount = 0
    foreach ($supplementalAction in $supplementalActions) {
        if ($requiredMutationCounts.ContainsKey($supplementalAction)) {
            $expectedSupplementalCount += [int]$requiredMutationCounts[$supplementalAction]
        }
    }
    $expectedActionCount = $packetCount + $expectedSupplementalCount
    if ($actions.Count -ne $expectedActionCount) {
        throw 'Summary action count does not match complete scenario accounting.'
    }
    foreach ($action in $actions) {
        Assert-ExactProperties -Value $action -Allowed $actionProperties -Label 'action'
        if ($action.action -notin $allowedActions) {
            throw 'Relay action is not allowed for the selected scenario.'
        }
        Assert-NullableBoundedInteger -Value $action -Name 'elapsed_microseconds' `
            -Label 'action' -Minimum 0 -Maximum ($TimeoutSeconds * 1000000L)
        Assert-NullableBoundedInteger -Value $action -Name 'event_order' -Label 'action' `
            -Minimum 1 -Maximum $packetCount
        Assert-NullableBoundedInteger -Value $action -Name 'related_event_order' `
            -Label 'action' -Minimum 1 -Maximum $packetCount -AllowNull $true
        if ($action.action -eq
            'replay-stale-server-ack-after-second-client-reliable') {
            if ($null -eq $action.related_event_order) {
                throw 'Stale-ACK replay must identify the retained ACK event.'
            }
        } elseif ($null -ne $action.related_event_order) {
            throw 'Only the stale-ACK replay action may identify a related event.'
        }
    }

    foreach ($mutationAction in $mutationActions) {
        $expectedCount = if ($requiredMutationCounts.ContainsKey($mutationAction)) {
            [int]$requiredMutationCounts[$mutationAction]
        } else {
            0
        }
        $actualCount = @($actions | Where-Object action -eq $mutationAction).Count
        if ($actualCount -ne $expectedCount) {
            throw "Relay mutation action '$mutationAction' has invalid cardinality."
        }
    }
    $primaryActionNames = @(
        'forward-once',
        'drop-first-client-reliable',
        'drop-first-server-ack',
        'duplicate-first-client-reliable'
    )
    $primaryActions = @($actions | Where-Object action -in $primaryActionNames)
    $primaryActionOrders = @($primaryActions | Group-Object event_order)
    if ($primaryActions.Count -ne $packetCount -or
        $primaryActionOrders.Count -ne $packetCount -or
        @($primaryActionOrders | Where-Object Count -ne 1).Count -ne 0) {
        throw 'Every captured event must have exactly one primary relay action.'
    }
    $firstClientReliableEvents = @(
        $events | Where-Object {
            $_.direction -eq 'c2s' -and
            $_.class -eq 'sequenced' -and
            $_.reliable_present -eq $true -and
            [int]$_.client_reliable_ordinal -eq 1
        })
    if ($firstClientReliableEvents.Count -ne 1) {
        throw 'Summary must identify exactly one first client reliable event.'
    }
    $firstClientReliableEvent = $firstClientReliableEvents[0]
    $firstCoveringGenerationOneAck = @(
        $events | Where-Object {
            $_.direction -eq 's2c' -and
            $_.class -eq 'sequenced' -and
            $null -ne $_.acknowledgement -and
            [long]$_.acknowledgement -ge [long]$firstClientReliableEvent.sequence -and
            $_.reliable_ack -eq $true
        } | Select-Object -First 1)
    if ($Scenario -in @(
            'drop-first-client-reliable',
            'duplicate-client-reliable')) {
        $clientMutationName = @($requiredMutationCounts.Keys)[0]
        $clientMutation = @(
            $actions | Where-Object action -eq $clientMutationName)[0]
        $clientMutationEvent = $events[[int]$clientMutation.event_order - 1]
        if ($clientMutationEvent.direction -ne 'c2s' -or
            $clientMutationEvent.class -ne 'sequenced' -or
            $clientMutationEvent.reliable_present -ne $true -or
            [int]$clientMutationEvent.client_reliable_ordinal -ne 1) {
            throw 'Client reliable mutation does not target the first reliable event.'
        }
    } elseif ($Scenario -eq 'drop-first-server-ack') {
        if ($firstCoveringGenerationOneAck.Count -ne 1) {
            throw 'Drop-server scenario has no first covering generation-1 ACK.'
        }
        $serverDrop = @(
            $actions | Where-Object action -eq 'drop-first-server-ack')[0]
        $serverDropEvent = $events[[int]$serverDrop.event_order - 1]
        if ($serverDropEvent.direction -ne 's2c' -or
            $serverDropEvent.class -ne 'sequenced' -or
            [int]$serverDrop.event_order -ne
                [int]$firstCoveringGenerationOneAck[0].order) {
            throw 'Server ACK mutation does not target the first covering generation-1 ACK.'
        }
    } elseif ($Scenario -eq 'delay-stale-ack') {
        if ($firstCoveringGenerationOneAck.Count -ne 1) {
            throw 'Stale-ACK scenario has no first covering generation-1 ACK.'
        }
        $retained = @(
            $actions | Where-Object action -eq 'retain-copy-of-first-server-ack')[0]
        $replayed = @(
            $actions |
                Where-Object action -eq
                    'replay-stale-server-ack-after-second-client-reliable')[0]
        $retainedEvent = $events[[int]$retained.event_order - 1]
        $replayTriggerEvent = $events[[int]$replayed.event_order - 1]
        if ($retainedEvent.direction -ne 's2c' -or
            $retainedEvent.class -ne 'sequenced' -or
            [int]$retained.event_order -ne
                [int]$firstCoveringGenerationOneAck[0].order -or
            [int]$replayed.related_event_order -ne [int]$retained.event_order -or
            [int]$replayed.event_order -le [int]$retained.event_order -or
            [long]$replayed.elapsed_microseconds -le
                [long]$retained.elapsed_microseconds -or
            $replayTriggerEvent.direction -ne 'c2s' -or
            $replayTriggerEvent.class -ne 'sequenced' -or
            $replayTriggerEvent.reliable_present -ne $true -or
            [int]$replayTriggerEvent.client_reliable_ordinal -ne 2) {
            throw 'Stale ACK replay is not linked after the second client reliable event.'
        }
    }
    if ($reliableObservations -lt 2) {
        throw 'Scenario did not observe the required two reliable lifecycle points.'
    }

    $runSucceeded = $true
    Write-Output 'Bounded stock reliable-netchan capture completed.'
    Write-Output ('Scenario={0}; packets={1}; postAccept={2}; bytes={3}; elapsedMs={4}; reliable={5}' -f
        $Scenario, $packetCount, $postAcceptCount, $byteCount,
        $elapsedMilliseconds, $reliableObservations)
    Write-Output "Metadata-only ignored artifact: $summaryPath"
}
finally {
    $cleanupErrors = [Collections.Generic.List[string]]::new()
    foreach ($ownedRecord in @($clientRecord, $relayRecord, $serverRecord)) {
        try { Stop-VerifiedOwnedProcess -Record $ownedRecord }
        catch { $cleanupErrors.Add($_.Exception.Message) }
    }

    if (-not $runSucceeded -and $null -ne $runRoot -and
        (Test-Path -LiteralPath $runRoot) -and
        $runRoot.StartsWith(
            $artifactRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        Assert-NoReparsePointInExistingPath -Path $artifactRoot -Label 'Artifact root'
        Assert-NoReparsePointInExistingPath -Path $runRoot -Label 'Run root'
        Assert-NoDescendantReparsePoint -Path $runRoot -Label 'Run root'
        Remove-Item -LiteralPath $runRoot -Recurse -Force
    }
    if ($cleanupErrors.Count -gt 0) {
        throw ('Owned-process cleanup failed: ' + ($cleanupErrors -join ' | '))
    }
}
