<#
.SYNOPSIS
Runs one bounded private-loopback stock netchan-fragment experiment.

.DESCRIPTION
The wrapper starts only the explicitly supplied Valve-signed Half-Life client,
Valve-signed HLDS launcher, and byte-preserving relay. The stock client is
started minimized because a hidden window can stall its engine loop before the
command-line connect. No public address, arbitrary packet edit, raw capture,
authentication byte, identity byte, or payload byte is accepted or retained.

The relay interface is deliberately narrow:
  -ListenPort, -ServerPort, -OutputDirectory, -Scenario,
  -TimeoutSeconds, -MaximumPackets, -MaximumPostAcceptPackets,
  -MaximumDatagramBytes, and -MaximumTotalBytes.

The immutable client channel endpoint must be learned from the first canonical
getchallenge leg. A relay may ignore at most four datagrams from any other
loopback source without forwarding, decoding, hashing, storing, or counting
them as scenario traffic; the fifth must fail closed. Only metadata.json may be
written under the wrapper-created, gitignored run directory.
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
    [int]$Port = 27420,

    [ValidateSet(
        'baseline',
        'drop-middle-fragment',
        'duplicate-fragment',
        'reorder-fragments',
        'drop-first-fragment',
        'drop-final-fragment',
        'second-transfer')]
    [string]$Scenario = 'baseline',

    [ValidateRange(8, 60)]
    [int]$TimeoutSeconds = 45
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$maximumPackets = 320
$maximumPostAcceptPackets = 280
$maximumDatagramBytes = 2048
$maximumTotalBytes = 524288
$maximumWrongSourceDatagrams = 4
$maximumTransfers = 16
$maximumFragmentsPerTransfer = 64
$maximumTransferBytes = 65536
$loopbackAddress = '127.0.0.1'
$serverPort = $Port + 1
$serverRecord = $null
$relayRecord = $null
$clientRecord = $null
$runRoot = $null
$runSucceeded = $false
$resultSummaryLine = $null
$resultArtifactLine = $null

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
    $pending = [Collections.Generic.Queue[string]]::new()
    $pending.Enqueue([IO.Path]::GetFullPath($Path))
    while ($pending.Count -gt 0) {
        $directory = $pending.Dequeue()
        foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force)) {
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "$Label must not contain a reparse point."
            }
            if ($item.PSIsContainer) {
                $pending.Enqueue([IO.Path]::GetFullPath($item.FullName))
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
    if ($signature.Status -ne 'Valid' -or $null -eq $signature.SignerCertificate -or
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
    if ($null -eq $current -or $current.HasExited) { return $false }
    try {
        $currentPath = [IO.Path]::GetFullPath($current.Path).TrimEnd('\')
        $expectedPath = [IO.Path]::GetFullPath($Record.ExpectedExecutable).TrimEnd('\')
        $startDeltaMilliseconds = [Math]::Abs((
            $current.StartTime.ToUniversalTime() - $Record.StartTimeUtc).TotalMilliseconds)
        return [bool]($currentPath -ieq $expectedPath -and $startDeltaMilliseconds -le 2.0)
    }
    catch { return $false }
}

function Stop-VerifiedOwnedProcess {
    param([AllowNull()][object]$Record)
    if ($null -eq $Record) { return }
    $current = Get-Process -Id $Record.Id -ErrorAction SilentlyContinue
    if ($null -eq $current -or $current.HasExited) { return }
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

function Get-LiveStockProcessSnapshot {
    $snapshot = [Collections.Generic.List[object]]::new()
    foreach ($process in @(Get-Process -Name hl,hlds -ErrorAction SilentlyContinue |
            Where-Object { -not $_.HasExited })) {
        try {
            $snapshot.Add([pscustomobject]@{
                Id = [int]$process.Id
                Name = [string]$process.ProcessName
                ExecutablePath = [IO.Path]::GetFullPath($process.Path)
                StartTimeUtc = $process.StartTime.ToUniversalTime()
            })
        }
        catch {
            throw 'Unable to establish the exact path/start identity of a live hl.exe or hlds.exe process.'
        }
    }
    return @($snapshot)
}

function Get-PostCleanupGateFailures {
    param(
        [AllowEmptyCollection()][object[]]$LiveStockSnapshot,
        [AllowEmptyCollection()][object[]]$OccupiedEndpoints
    )
    $failures = [Collections.Generic.List[string]]::new()
    if (@($LiveStockSnapshot).Count -ne 0) {
        $processNames = @($LiveStockSnapshot | ForEach-Object Name |
            Sort-Object -Unique) -join ','
        $message = (('Post-cleanup stock-process gate found {0} live process(es) ({1}); ' +
            'no unrecorded process was terminated.') -f
            @($LiveStockSnapshot).Count, $processNames)
        $failures.Add($message)
    }
    if (@($OccupiedEndpoints).Count -ne 0) {
        $ports = @($OccupiedEndpoints | ForEach-Object LocalPort |
            Sort-Object -Unique) -join ','
        $message = ('Post-cleanup UDP-port gate found an occupied selected port ({0}).' -f
            $ports)
        $failures.Add($message)
    }
    return @($failures)
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
        [long]$Maximum,
        [bool]$AllowNull = $false
    )
    $actual = Get-RequiredProperty -Value $Value -Name $Name -Label $Label
    if ($null -eq $actual) {
        if ($AllowNull) { return $null }
        throw "$Label property '$Name' must not be null."
    }
    if ($actual -isnot [int] -and $actual -isnot [long]) {
        throw "$Label property '$Name' must be a JSON integer."
    }
    $number = [long]$actual
    if ($number -lt $Minimum -or $number -gt $Maximum) {
        throw "$Label property '$Name' is outside its bound."
    }
    return $number
}

function Get-Boolean {
    param([object]$Value, [string]$Name, [string]$Label, [bool]$AllowNull = $false)
    $actual = Get-RequiredProperty -Value $Value -Name $Name -Label $Label
    if ($null -eq $actual) {
        if ($AllowNull) { return $null }
        throw "$Label property '$Name' must not be null."
    }
    if ($actual -isnot [bool]) { throw "$Label property '$Name' must be Boolean." }
    return [bool]$actual
}

function Assert-TrueBoolean {
    param([object]$Value, [string]$Name, [string]$Label)
    if (-not (Get-Boolean -Value $Value -Name $Name -Label $Label)) {
        throw "$Label property '$Name' must be true."
    }
}

function Assert-HexDigest {
    param([object]$Value, [string]$Name, [string]$Label)
    $digest = Get-RequiredProperty -Value $Value -Name $Name -Label $Label
    if ($digest -isnot [string] -or $digest -notmatch '^[0-9A-F]{64}$') {
        throw "$Label property '$Name' must be an uppercase SHA-256 digest."
    }
}

$resolvedRelay = Resolve-ExplicitFile -Path $RelayPath -Label 'RelayPath'
$resolvedClient = Resolve-ExplicitFile -Path $HalfLifePath -Label 'HalfLifePath'
$resolvedHlds = Resolve-ExplicitFile -Path $HldsPath -Label 'HldsPath'
if ([IO.Path]::GetFileName($resolvedClient) -ine 'hl.exe') {
    throw 'HalfLifePath must explicitly name hl.exe.'
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

$preexistingStock = @(Get-LiveStockProcessSnapshot)
if ($preexistingStock.Count -ne 0) {
    throw 'Refusing to run while an unrelated hl.exe or hlds.exe process exists.'
}
foreach ($selectedPort in @($Port, $serverPort)) {
    if (Get-NetUDPEndpoint -LocalPort $selectedPort -ErrorAction SilentlyContinue) {
        throw "Selected private loopback UDP port $selectedPort is already occupied."
    }
}

$scenarioMap = @{
    'baseline' = 'Baseline'
    'drop-middle-fragment' = 'DropMiddleFragment'
    'duplicate-fragment' = 'DuplicateFragment'
    'reorder-fragments' = 'ReorderFragments'
    'drop-first-fragment' = 'DropFirstFragment'
    'drop-final-fragment' = 'DropFinalFragment'
    'second-transfer' = 'SecondTransfer'
}
$relayScenario = $scenarioMap[$Scenario]
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifactRoot = [IO.Path]::GetFullPath((
    Join-Path $repositoryRoot 'manual-artifacts\netchan-fragment-captures'))
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
      -WindowStyle Minimized -PassThru
    $clientRecord = New-OwnedProcessRecord -Process $clientProcess `
        -ExpectedExecutable $resolvedClient
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
        throw 'Owned relay exceeded its timeout plus five-second grace period.'
    }
    if ($relayProcess.ExitCode -ne 0) {
        throw "Owned relay exited with code $($relayProcess.ExitCode)."
    }

    Assert-NoDescendantReparsePoint -Path $runRoot -Label 'Run root'
    $items = @(Get-ChildItem -LiteralPath $runRoot -Force -Recurse)
    if ($items.Count -ne 1 -or $items[0].PSIsContainer -or
        $items[0].Name -ne 'metadata.json' -or
        ($items[0].Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Relay output contained a forbidden file; only metadata.json is allowed.'
    }
    if ($items[0].Length -le 0 -or $items[0].Length -gt 2097152) {
        throw 'Relay metadata is empty or exceeds two MiB.'
    }
    $summary = Get-Content -Raw -LiteralPath $summaryPath |
        ConvertFrom-Json -ErrorAction Stop

    $summaryProperties = @(
        'schema', 'profile', 'scenario', 'completion', 'loopback_only',
        'byte_preserving_relay', 'same_upstream_socket',
        'exact_server_endpoint_validation',
        'client_endpoint_learned_from_canonical_getchallenge',
        'raw_packet_bytes_stored', 'packet_count', 'post_accept_packet_count',
        'total_bytes', 'maximum_packets', 'maximum_post_accept_packets',
        'maximum_datagram_bytes', 'maximum_total_bytes', 'timeout_seconds',
        'elapsed_milliseconds', 'connect_seen', 'accept_seen',
        'scenario_mutation_count', 'ignored_wrong_source_count',
        'held_packet_at_end', 'transfers', 'fragment_acknowledgements',
        'events', 'actions'
    )
    Assert-ExactProperties -Value $summary -Allowed $summaryProperties -Label 'summary'
    if ((Get-RequiredProperty $summary 'schema' 'summary') -ne
        'hlclient.stock-fragment-netchan-metadata.v1') {
        throw 'Relay summary uses an unsupported schema.'
    }
    if ((Get-RequiredProperty $summary 'profile' 'summary') -ne
        'stock-hl-1.1.1.1-to-stock-hlds-protocol48-build10210') {
        throw 'Relay summary uses an unsupported stock profile.'
    }
    if ((Get-RequiredProperty $summary 'scenario' 'summary') -ne $relayScenario) {
        throw 'Relay summary scenario does not match the requested scenario.'
    }
    if ((Get-RequiredProperty $summary 'completion' 'summary') -ne 'bounded_complete') {
        throw 'Relay scenario did not reach its bounded proof condition.'
    }
    foreach ($name in @(
            'loopback_only', 'byte_preserving_relay', 'same_upstream_socket',
            'exact_server_endpoint_validation',
            'client_endpoint_learned_from_canonical_getchallenge',
            'connect_seen', 'accept_seen')) {
        Assert-TrueBoolean -Value $summary -Name $name -Label 'summary'
    }
    if (Get-Boolean $summary 'raw_packet_bytes_stored' 'summary') {
        throw 'Relay must report raw_packet_bytes_stored=false.'
    }
    if (Get-Boolean $summary 'held_packet_at_end' 'summary') {
        throw 'Relay retained a held packet at bounded completion.'
    }

    $packetCount = Get-BoundedInteger $summary 'packet_count' 'summary' 1 $maximumPackets
    $postAcceptCount = Get-BoundedInteger $summary 'post_accept_packet_count' `
        'summary' 1 $maximumPostAcceptPackets
    $byteCount = Get-BoundedInteger $summary 'total_bytes' 'summary' 1 $maximumTotalBytes
    $elapsedMilliseconds = Get-BoundedInteger $summary 'elapsed_milliseconds' `
        'summary' 0 ($TimeoutSeconds * 1000L)
    $mutationCount = Get-BoundedInteger $summary 'scenario_mutation_count' `
        'summary' 0 1
    $wrongSourceCount = Get-BoundedInteger $summary 'ignored_wrong_source_count' `
        'summary' 0 $maximumWrongSourceDatagrams
    if ($postAcceptCount -gt $packetCount) {
        throw 'post_accept_packet_count exceeds packet_count.'
    }
    $reportedBounds = [ordered]@{
        maximum_packets = $maximumPackets
        maximum_post_accept_packets = $maximumPostAcceptPackets
        maximum_datagram_bytes = $maximumDatagramBytes
        maximum_total_bytes = $maximumTotalBytes
        timeout_seconds = $TimeoutSeconds
    }
    foreach ($boundName in $reportedBounds.Keys) {
        $expected = [long]$reportedBounds[$boundName]
        $reported = Get-BoundedInteger $summary $boundName 'summary' $expected $expected
        if ($reported -ne $expected) { throw 'Relay changed a requested hard bound.' }
    }

    $events = @(Get-RequiredProperty $summary 'events' 'summary')
    $actions = @(Get-RequiredProperty $summary 'actions' 'summary')
    $transfers = @(Get-RequiredProperty $summary 'transfers' 'summary')
    $ackLinks = @(Get-RequiredProperty $summary 'fragment_acknowledgements' 'summary')
    if ($events.Count -ne $packetCount) {
        throw 'Summary event count does not match packet_count.'
    }
    if ($transfers.Count -lt 1 -or $transfers.Count -gt $maximumTransfers) {
        throw 'Summary transfer count is outside its bound.'
    }

    $eventProperties = @(
        'order', 'direction', 'elapsed_microseconds', 'bytes', 'class',
        'sequence', 'reliable_present', 'fragmented', 'acknowledgement',
        'reliable_ack', 'decoded_body_bytes', 'transformed_complete_words',
        'unchanged_tail_bytes', 'descriptor_area_bytes', 'payload_area_bytes',
        'fragment_slots'
    )
    $slotAbsentProperties = @('slot', 'present')
    $slotPresentProperties = @(
        'slot', 'present', 'fragment_id', 'packed_index', 'packed_count',
        'offset', 'length', 'payload_sha256'
    )
    $summedBytes = 0L
    $fragmentEvents = [Collections.Generic.List[object]]::new()
    for ($index = 0; $index -lt $events.Count; ++$index) {
        $event = $events[$index]
        Assert-ExactProperties $event $eventProperties 'event'
        if ($event.direction -notin @('c2s', 's2c')) {
            throw 'Event direction is not from the fixed allowlist.'
        }
        if ([string]$event.class -notmatch
            '^(connectionless-(header-only|0x[0-9a-f]{2})|sequenced|sequenced-short|malformed-short)$') {
            throw 'Event class is not from the fixed allowlist.'
        }
        $order = Get-BoundedInteger $event 'order' 'event' 1 $packetCount
        if ($order -ne ($index + 1)) { throw 'Event order is not contiguous and one-based.' }
        $null = Get-BoundedInteger $event 'elapsed_microseconds' 'event' 0 `
            ($TimeoutSeconds * 1000000L)
        $eventBytes = Get-BoundedInteger $event 'bytes' 'event' 1 $maximumDatagramBytes
        $summedBytes += $eventBytes
        $sequence = Get-BoundedInteger $event 'sequence' 'event' 0 1073741823 $true
        $reliablePresent = Get-Boolean $event 'reliable_present' 'event' $true
        $fragmented = Get-Boolean $event 'fragmented' 'event' $true
        $acknowledgement = Get-BoundedInteger $event 'acknowledgement' 'event' `
            0 1073741823 $true
        $reliableAck = Get-Boolean $event 'reliable_ack' 'event' $true
        $decodedBodyBytes = Get-BoundedInteger $event 'decoded_body_bytes' 'event' `
            0 ($maximumDatagramBytes - 8) $true
        $completeWords = Get-BoundedInteger $event 'transformed_complete_words' `
            'event' 0 $maximumDatagramBytes $true
        $tailBytes = Get-BoundedInteger $event 'unchanged_tail_bytes' 'event' 0 3 $true
        $descriptorBytes = Get-BoundedInteger $event 'descriptor_area_bytes' `
            'event' 2 18 $true
        $payloadBytes = Get-BoundedInteger $event 'payload_area_bytes' 'event' `
            0 $maximumDatagramBytes $true
        if ($event.class -eq 'sequenced') {
            if ($null -eq $sequence -or $null -eq $reliablePresent -or
                $null -eq $fragmented -or $null -eq $acknowledgement -or
                $null -eq $reliableAck -or $null -eq $decodedBodyBytes -or
                $null -eq $completeWords -or $null -eq $tailBytes) {
                throw 'Sequenced event lacks complete bounded header metadata.'
            }
            if ($eventBytes -ne ($decodedBodyBytes + 8)) {
                throw 'Sequenced event byte/body accounting is inconsistent.'
            }
            if ($completeWords -ne [Math]::Floor($decodedBodyBytes / 4) -or
                $tailBytes -ne ($decodedBodyBytes % 4)) {
                throw 'Transform word/tail accounting is inconsistent.'
            }
        }
        elseif ($null -ne $sequence -or $null -ne $reliablePresent -or
            $null -ne $fragmented -or $null -ne $acknowledgement -or
            $null -ne $reliableAck -or $null -ne $decodedBodyBytes -or
            $null -ne $completeWords -or $null -ne $tailBytes) {
            throw 'Non-sequenced event contains sequenced header metadata.'
        }
        if ($fragmented -eq $true) {
            if (-not $reliablePresent) {
                throw 'Observed stock fragment lacks reliable-present bit 31.'
            }
            if ($null -eq $descriptorBytes -or $null -eq $payloadBytes -or
                $descriptorBytes + $payloadBytes -ne $decodedBodyBytes) {
                throw 'Fragment descriptor/payload boundary is inconsistent.'
            }
            $slots = @($event.fragment_slots)
            if ($slots.Count -ne 2) { throw 'Fragment event must contain exactly two slots.' }
            $presentCount = 0
            for ($slotIndex = 0; $slotIndex -lt 2; ++$slotIndex) {
                $slot = $slots[$slotIndex]
                $present = Get-Boolean $slot 'present' 'fragment slot'
                if ((Get-BoundedInteger $slot 'slot' 'fragment slot' 0 1) -ne $slotIndex) {
                    throw 'Fragment slots are not in canonical order.'
                }
                if (-not $present) {
                    Assert-ExactProperties $slot $slotAbsentProperties 'fragment slot'
                    continue
                }
                ++$presentCount
                Assert-ExactProperties $slot $slotPresentProperties 'fragment slot'
                $fragmentId = Get-BoundedInteger $slot 'fragment_id' 'fragment slot' 1 4294967295
                $packedIndex = Get-BoundedInteger $slot 'packed_index' 'fragment slot' `
                    1 65535
                $packedCount = Get-BoundedInteger $slot 'packed_count' 'fragment slot' `
                    1 $maximumFragmentsPerTransfer
                $offset = Get-BoundedInteger $slot 'offset' 'fragment slot' 0 65535
                $length = Get-BoundedInteger $slot 'length' 'fragment slot' 1 65535
                Assert-HexDigest $slot 'payload_sha256' 'fragment slot'
                if ($packedIndex -gt $packedCount) {
                    throw 'Packed fragment index exceeds its count.'
                }
                $expectedId = ([long]$packedIndex * 65536L) + [long]$packedCount
                if ($fragmentId -ne $expectedId) {
                    throw 'Fragment ID is not the confirmed packed index/count value.'
                }
                if ($offset -gt $payloadBytes -or $length -gt ($payloadBytes - $offset)) {
                    throw 'Fragment descriptor range exceeds decoded payload area.'
                }
            }
            if ($presentCount -ne 1 -or -not $slots[0].present -or $slots[1].present) {
                throw 'Supported stock profile requires slot 0 present and slot 1 absent.'
            }
            if ($descriptorBytes -ne 10) {
                throw 'Supported stock profile requires a ten-byte descriptor area.'
            }
            $fragmentEvents.Add($event)
        }
        else {
            if ($null -ne $event.fragment_slots -or $null -ne $descriptorBytes -or
                $null -ne $payloadBytes) {
                throw 'Non-fragment event contains fragment metadata.'
            }
        }
    }
    if ($summedBytes -ne $byteCount) {
        throw 'Sum of event byte counts does not match total_bytes.'
    }
    if ($fragmentEvents.Count -lt 1) { throw 'Scenario observed no fragment packet.' }

    $transferProperties = @(
        'ordinal', 'stream', 'declared_count', 'reassembled_bytes',
        'reassembled_sha256', 'standard_bzip2_signature',
        'standard_gzip_signature', 'standard_zlib_header',
        'observed_in_index_order'
    )
    for ($index = 0; $index -lt $transfers.Count; ++$index) {
        $transfer = $transfers[$index]
        Assert-ExactProperties $transfer $transferProperties 'transfer'
        if ((Get-BoundedInteger $transfer 'ordinal' 'transfer' 1 $maximumTransfers) -ne
            ($index + 1)) { throw 'Transfer ordinals are not contiguous.' }
        if ((Get-BoundedInteger $transfer 'stream' 'transfer' 0 1) -ne 0) {
            throw 'Supported capture profile completes only stream slot 0.'
        }
        $null = Get-BoundedInteger $transfer 'declared_count' 'transfer' 1 `
            $maximumFragmentsPerTransfer
        $null = Get-BoundedInteger $transfer 'reassembled_bytes' 'transfer' 1 `
            $maximumTransferBytes
        Assert-HexDigest $transfer 'reassembled_sha256' 'transfer'
        foreach ($compressionName in @(
                'standard_bzip2_signature', 'standard_gzip_signature',
                'standard_zlib_header')) {
            if (Get-Boolean $transfer $compressionName 'transfer') {
                throw 'Observed transfer uses an unsupported compression signature.'
            }
        }
        Assert-TrueBoolean $transfer 'observed_in_index_order' 'transfer'
    }

    $ackProperties = @(
        'fragment_event_order', 'transfer_ordinal', 'fragment_index',
        'fragment_sequence', 'ack_event_order', 'acknowledgement',
        'reliable_ack', 'elapsed_microseconds'
    )
    foreach ($link in $ackLinks) {
        Assert-ExactProperties $link $ackProperties 'fragment acknowledgement'
        $fragmentOrder = Get-BoundedInteger $link 'fragment_event_order' `
            'fragment acknowledgement' 1 $packetCount
        $ackOrder = Get-BoundedInteger $link 'ack_event_order' `
            'fragment acknowledgement' 1 $packetCount
        $null = Get-BoundedInteger $link 'transfer_ordinal' `
            'fragment acknowledgement' 1 $maximumTransfers
        $null = Get-BoundedInteger $link 'fragment_index' `
            'fragment acknowledgement' 1 $maximumFragmentsPerTransfer
        $fragmentSequence = Get-BoundedInteger $link 'fragment_sequence' `
            'fragment acknowledgement' 0 1073741823
        $ackNumber = Get-BoundedInteger $link 'acknowledgement' `
            'fragment acknowledgement' 0 1073741823
        $null = Get-Boolean $link 'reliable_ack' 'fragment acknowledgement'
        $null = Get-BoundedInteger $link 'elapsed_microseconds' `
            'fragment acknowledgement' 0 ($TimeoutSeconds * 1000000L)
        $fragmentEvent = $events[$fragmentOrder - 1]
        $ackEvent = $events[$ackOrder - 1]
        if ($fragmentEvent.direction -ne 's2c' -or -not $fragmentEvent.fragmented -or
            $ackEvent.direction -ne 'c2s' -or $ackEvent.class -ne 'sequenced' -or
            $fragmentSequence -ne [long]$fragmentEvent.sequence -or
            $ackNumber -ne [long]$ackEvent.acknowledgement -or
            $ackNumber -lt $fragmentSequence -or $ackOrder -le $fragmentOrder) {
            throw 'Fragment acknowledgement link is inconsistent with its events.'
        }
    }

    $actionProperties = @(
        'elapsed_microseconds', 'action', 'event_order', 'related_event_order'
    )
    $requiredActions = @{
        'baseline' = @{}
        'drop-middle-fragment' = @{ 'drop-first-middle-fragment-transmission' = 1 }
        'duplicate-fragment' = @{ 'forward-middle-fragment-twice' = 1 }
        'reorder-fragments' = @{
            'retain-copy-of-forwarded-fragment-2' = 1
            'replay-fragment-2-after-fragment-3' = 1
        }
        'drop-first-fragment' = @{ 'drop-first-fragment-transmission' = 1 }
        'drop-final-fragment' = @{ 'drop-final-fragment-transmission' = 1 }
        'second-transfer' = @{
            'retain-copy-of-first-transfer-final-fragment' = 1
            'replay-first-transfer-final-after-second-transfer-start' = 1
        }
    }[$Scenario]
    $allMutationActions = @(
        'drop-first-middle-fragment-transmission',
        'forward-middle-fragment-twice',
        'retain-copy-of-forwarded-fragment-2',
        'replay-fragment-2-after-fragment-3',
        'drop-first-fragment-transmission',
        'drop-final-fragment-transmission',
        'retain-copy-of-first-transfer-final-fragment',
        'replay-first-transfer-final-after-second-transfer-start'
    )
    $allowedActions = @('forward-once') + @($requiredActions.Keys)
    $supplementalNames = @(
        'retain-copy-of-forwarded-fragment-2',
        'replay-fragment-2-after-fragment-3',
        'retain-copy-of-first-transfer-final-fragment',
        'replay-first-transfer-final-after-second-transfer-start'
    )
    $supplementalCount = 0
    foreach ($name in $supplementalNames) {
        if ($requiredActions.ContainsKey($name)) {
            $supplementalCount += [int]$requiredActions[$name]
        }
    }
    if ($actions.Count -ne ($packetCount + $supplementalCount)) {
        throw 'Action count does not provide exact per-event accounting.'
    }
    foreach ($action in $actions) {
        Assert-ExactProperties $action $actionProperties 'action'
        if ($action.action -notin $allowedActions) {
            throw 'Relay action is not allowed for the selected scenario.'
        }
        $null = Get-BoundedInteger $action 'elapsed_microseconds' 'action' 0 `
            ($TimeoutSeconds * 1000000L)
        $null = Get-BoundedInteger $action 'event_order' 'action' 1 $packetCount
        $related = Get-BoundedInteger $action 'related_event_order' 'action' `
            1 $packetCount $true
        if ($action.action -like 'replay-*') {
            if ($null -eq $related) { throw 'Replay action lacks its retained event link.' }
        }
        elseif ($null -ne $related) {
            throw 'Only a replay action may contain related_event_order.'
        }
    }
    foreach ($name in $allMutationActions) {
        $expected = if ($requiredActions.ContainsKey($name)) {
            [int]$requiredActions[$name]
        } else { 0 }
        if (@($actions | Where-Object action -eq $name).Count -ne $expected) {
            throw "Relay action '$name' has invalid scenario cardinality."
        }
    }
    $primaryNames = @(
        'forward-once', 'drop-first-middle-fragment-transmission',
        'forward-middle-fragment-twice', 'drop-first-fragment-transmission',
        'drop-final-fragment-transmission'
    )
    $primaryActions = @($actions | Where-Object action -in $primaryNames)
    $primaryGroups = @($primaryActions | Group-Object event_order)
    if ($primaryActions.Count -ne $packetCount -or
        $primaryGroups.Count -ne $packetCount -or
        @($primaryGroups | Where-Object Count -ne 1).Count -ne 0) {
        throw 'Every captured event must have exactly one primary relay action.'
    }
    $expectedMutationCount = if ($Scenario -eq 'baseline') { 0L } else { 1L }
    if ($mutationCount -ne $expectedMutationCount) {
        throw 'scenario_mutation_count does not match the fixed scenario policy.'
    }

    $serverFragments = @($fragmentEvents | Where-Object direction -eq 's2c')
    if ($serverFragments.Count -lt 1) { throw 'Scenario has no stock server fragment.' }
    $firstStart = @($serverFragments | Where-Object {
        $_.fragment_slots[0].packed_index -eq 1
    } | Select-Object -First 1)
    if ($firstStart.Count -ne 1) { throw 'First transfer lacks index 1.' }
    $firstCount = [int]$firstStart[0].fragment_slots[0].packed_count
    if ($firstCount -lt 2) { throw 'First stock server transfer is unexpectedly single-fragment.' }
    $firstTransfer = [Collections.Generic.List[object]]::new()
    $firstCompleteSeen = $false
    foreach ($fragment in $serverFragments) {
        if ($firstCompleteSeen -and
            $fragment.fragment_slots[0].packed_index -eq 1) { break }
        if ($fragment.fragment_slots[0].packed_count -ne $firstCount) {
            throw 'Packed count changed before first transfer completion.'
        }
        $firstTransfer.Add($fragment)
        $seenIndices = @($firstTransfer | ForEach-Object {
            [int]$_.fragment_slots[0].packed_index
        } | Sort-Object -Unique)
        $firstCompleteSeen = $seenIndices.Count -eq $firstCount
    }
    if (-not $firstCompleteSeen) { throw 'First normal transfer did not complete.' }
    if ([int]$transfers[0].declared_count -ne $firstCount) {
        throw 'First transfer summary count does not match its fragment events.'
    }
    $firstUniqueBytes = 0L
    for ($fragmentIndex = 1; $fragmentIndex -le $firstCount; ++$fragmentIndex) {
        $firstForIndex = @($firstTransfer | Where-Object {
            $_.fragment_slots[0].packed_index -eq $fragmentIndex
        } | Select-Object -First 1)
        if ($firstForIndex.Count -ne 1) {
            throw 'First transfer summary lacks a declared fragment index.'
        }
        $firstUniqueBytes += [long]$firstForIndex[0].fragment_slots[0].length
    }
    if ([long]$transfers[0].reassembled_bytes -ne $firstUniqueBytes) {
        throw 'First transfer summary byte count does not match its unique fragments.'
    }
    for ($fragmentIndex = 1; $fragmentIndex -le $firstCount; ++$fragmentIndex) {
        $observed = @($firstTransfer | Where-Object {
            $_.fragment_slots[0].packed_index -eq $fragmentIndex
        })
        $expectedCount = 1
        if (($Scenario -eq 'drop-first-fragment' -and $fragmentIndex -eq 1) -or
            ($Scenario -eq 'drop-final-fragment' -and $fragmentIndex -eq $firstCount) -or
            ($Scenario -eq 'drop-middle-fragment' -and
             $fragmentIndex -eq [int][Math]::Ceiling($firstCount / 2.0))) {
            $expectedCount = 2
        }
        if ($observed.Count -ne $expectedCount) {
            throw 'First-transfer fragment transmission count violates scenario policy.'
        }
        foreach ($item in $observed) {
            if ($item.fragment_slots[0].offset -ne 0 -or
                $item.fragment_slots[0].length -gt 1024) {
                throw 'Observed stock normal fragment exceeds the confirmed offset/chunk profile.'
            }
            if ($fragmentIndex -lt $firstCount -and
                $item.fragment_slots[0].length -ne 1024) {
                throw 'A non-final stock normal fragment is not 1,024 bytes.'
            }
        }
        if ($observed.Count -eq 2) {
            $first = $observed[0].fragment_slots[0]
            $retry = $observed[1].fragment_slots[0]
            if ($observed[1].sequence -le $observed[0].sequence -or
                $retry.fragment_id -ne $first.fragment_id -or
                $retry.offset -ne $first.offset -or $retry.length -ne $first.length -or
                $retry.payload_sha256 -ne $first.payload_sha256) {
                throw 'Dropped fragment was not retried identically under a fresh sequence.'
            }
        }
    }

    $firstLinks = @($ackLinks | Where-Object transfer_ordinal -eq 1 |
        Sort-Object fragment_index)
    if ($firstLinks.Count -ne $firstCount) {
        throw 'First transfer lacks one covering ACK link per admitted fragment.'
    }
    for ($index = 0; $index -lt $firstLinks.Count; ++$index) {
        if ([int]$firstLinks[$index].fragment_index -ne ($index + 1)) {
            throw 'First-transfer ACK links do not cover indices in order.'
        }
        $expectedBit = (($index % 2) -eq 0)
        if ([bool]$firstLinks[$index].reliable_ack -ne $expectedBit) {
            throw 'First-transfer reliable ACK generation does not alternate from one.'
        }
    }

    if ($Scenario -in @(
            'drop-middle-fragment', 'drop-first-fragment', 'drop-final-fragment')) {
        $dropActionName = switch ($Scenario) {
            'drop-middle-fragment' { 'drop-first-middle-fragment-transmission' }
            'drop-first-fragment' { 'drop-first-fragment-transmission' }
            'drop-final-fragment' { 'drop-final-fragment-transmission' }
        }
        $expectedDroppedIndex = switch ($Scenario) {
            'drop-middle-fragment' { [int][Math]::Ceiling($firstCount / 2.0) }
            'drop-first-fragment' { 1 }
            'drop-final-fragment' { $firstCount }
        }
        $action = @($actions | Where-Object action -eq $dropActionName)[0]
        $target = $events[[int]$action.event_order - 1]
        $observed = @($firstTransfer | Where-Object {
            $_.fragment_slots[0].packed_index -eq $expectedDroppedIndex
        })
        if ($target.direction -ne 's2c' -or -not $target.fragmented -or
            $target.fragment_slots[0].packed_count -ne $firstCount -or
            $target.fragment_slots[0].packed_index -ne $expectedDroppedIndex -or
            $observed.Count -ne 2 -or
            [int]$target.order -ne [int]$observed[0].order) {
            throw 'Drop action does not target the first transmission of the required T1 fragment.'
        }
        $retry = $observed[1]
        $expectedGeneration = (($expectedDroppedIndex % 2) -eq 1)
        $gapAcks = @($events | Where-Object {
            $_.order -gt $target.order -and $_.order -lt $retry.order -and
            $_.direction -eq 'c2s' -and $_.class -eq 'sequenced' -and
            [long]$_.acknowledgement -ge [long]$target.sequence -and
            [bool]$_.reliable_ack -ne $expectedGeneration
        })
        $prematureMatchingAcks = @($events | Where-Object {
            $_.order -gt $target.order -and $_.order -lt $retry.order -and
            $_.direction -eq 'c2s' -and $_.class -eq 'sequenced' -and
            [long]$_.acknowledgement -ge [long]$target.sequence -and
            [bool]$_.reliable_ack -eq $expectedGeneration
        })
        if ($gapAcks.Count -lt 1 -or $prematureMatchingAcks.Count -ne 0) {
            throw 'Dropped fragment retry lacks the confirmed advancing wrong-generation ACK gap.'
        }
        $retryLink = @($firstLinks | Where-Object {
            $_.fragment_index -eq $expectedDroppedIndex
        })
        if ($retryLink.Count -ne 1 -or
            [int]$retryLink[0].fragment_event_order -ne [int]$retry.order) {
            throw 'Dropped fragment covering ACK is not linked to its fresh-sequence retry.'
        }
    }
    elseif ($Scenario -eq 'duplicate-fragment') {
        $action = @($actions | Where-Object action -eq 'forward-middle-fragment-twice')[0]
        $target = $events[[int]$action.event_order - 1]
        $middleIndex = [int][Math]::Ceiling($firstCount / 2.0)
        $middleEvents = @($firstTransfer | Where-Object {
            $_.fragment_slots[0].packed_index -eq $middleIndex
        })
        if ($target.direction -ne 's2c' -or -not $target.fragmented -or
            $target.fragment_slots[0].packed_count -ne $firstCount -or
            $target.fragment_slots[0].packed_index -ne $middleIndex -or
            $middleEvents.Count -ne 1 -or
            [int]$target.order -ne [int]$middleEvents[0].order) {
            throw 'Duplicate action does not target the first transfer middle fragment.'
        }
    }
    elseif ($Scenario -eq 'reorder-fragments') {
        $retain = @($actions | Where-Object {
            $_.action -eq 'retain-copy-of-forwarded-fragment-2'
        })[0]
        $replay = @($actions | Where-Object {
            $_.action -eq 'replay-fragment-2-after-fragment-3'
        })[0]
        $retainedEvent = $events[[int]$retain.event_order - 1]
        $replayTrigger = $events[[int]$replay.event_order - 1]
        $secondEvents = @($firstTransfer | Where-Object {
            $_.fragment_slots[0].packed_index -eq 2
        })
        $thirdEvents = @($firstTransfer | Where-Object {
            $_.fragment_slots[0].packed_index -eq 3
        })
        if ($retainedEvent.direction -ne 's2c' -or
            $replayTrigger.direction -ne 's2c' -or
            $retainedEvent.fragment_slots[0].packed_count -ne $firstCount -or
            $replayTrigger.fragment_slots[0].packed_count -ne $firstCount -or
            $retainedEvent.fragment_slots[0].packed_index -ne 2 -or
            $replayTrigger.fragment_slots[0].packed_index -ne 3 -or
            $secondEvents.Count -ne 1 -or $thirdEvents.Count -ne 1 -or
            [int]$retainedEvent.order -ne [int]$secondEvents[0].order -or
            [int]$replayTrigger.order -ne [int]$thirdEvents[0].order -or
            [int]$replay.related_event_order -ne [int]$retain.event_order -or
            [long]$replay.elapsed_microseconds -le [long]$retain.elapsed_microseconds) {
            throw 'Reorder scenario is not an exact old-index-2 replay after index 3.'
        }
    }
    elseif ($Scenario -eq 'second-transfer') {
        if ($transfers.Count -lt 2) { throw 'Second-transfer scenario completed fewer than two transfers.' }
        $retain = @($actions | Where-Object {
            $_.action -eq 'retain-copy-of-first-transfer-final-fragment'
        })[0]
        $replay = @($actions | Where-Object {
            $_.action -eq 'replay-first-transfer-final-after-second-transfer-start'
        })[0]
        $retainedEvent = $events[[int]$retain.event_order - 1]
        $replayTrigger = $events[[int]$replay.event_order - 1]
        $secondStarts = @($serverFragments | Where-Object {
            $_.order -gt $firstTransfer[$firstTransfer.Count - 1].order -and
            $_.fragment_slots[0].packed_index -eq 1
        } | Select-Object -First 1)
        $firstFinalEvents = @($firstTransfer | Where-Object {
            $_.fragment_slots[0].packed_index -eq $firstCount
        })
        if ($retainedEvent.direction -ne 's2c' -or
            $replayTrigger.direction -ne 's2c' -or
            $firstFinalEvents.Count -ne 1 -or $secondStarts.Count -ne 1 -or
            [int]$retainedEvent.order -ne [int]$firstFinalEvents[0].order -or
            [int]$replayTrigger.order -ne [int]$secondStarts[0].order -or
            $retainedEvent.fragment_slots[0].packed_index -ne $firstCount -or
            $replayTrigger.fragment_slots[0].packed_index -ne 1 -or
            $replayTrigger.order -le $retainedEvent.order -or
            [int]$replay.related_event_order -ne [int]$retain.event_order -or
            [long]$replay.elapsed_microseconds -le [long]$retain.elapsed_microseconds) {
            throw 'Second-transfer replay is not linked from completed T1 into T2 start.'
        }
        $firstFinalLink = @($firstLinks | Where-Object fragment_index -eq $firstCount)
        $secondStartLink = @($ackLinks | Where-Object {
            $_.fragment_event_order -eq $replayTrigger.order
        })
        if ($firstFinalLink.Count -ne 1 -or $secondStartLink.Count -ne 1 -or
            [bool]$secondStartLink[0].reliable_ack -eq
                [bool]$firstFinalLink[0].reliable_ack) {
            throw 'Old T1 replay did not preserve the opposite admitted T2 generation.'
        }
    }

    $runSucceeded = $true
    $resultSummaryLine = ('Scenario={0}; packets={1}; fragments={2}; transfers={3}; wrongSourceIgnored={4}; elapsedMs={5}' -f
        $Scenario, $packetCount, $fragmentEvents.Count, $transfers.Count,
        $wrongSourceCount, $elapsedMilliseconds)
    $resultArtifactLine = "Metadata-only ignored artifact: $summaryPath"
}
finally {
    $cleanupErrors = [Collections.Generic.List[string]]::new()
    foreach ($ownedRecord in @($clientRecord, $relayRecord, $serverRecord)) {
        try { Stop-VerifiedOwnedProcess -Record $ownedRecord }
        catch { $cleanupErrors.Add($_.Exception.Message) }
    }
    $postCleanupStock = @()
    $occupiedEndpoints = @()
    try { $postCleanupStock = @(Get-LiveStockProcessSnapshot) }
    catch { $cleanupErrors.Add($_.Exception.Message) }
    try {
        $occupiedEndpoints = @(Get-NetUDPEndpoint -LocalPort @($Port, $serverPort) `
            -ErrorAction SilentlyContinue)
    }
    catch { $cleanupErrors.Add('Unable to establish the post-cleanup UDP-port state.') }
    foreach ($gateFailure in @(Get-PostCleanupGateFailures `
            -LiveStockSnapshot $postCleanupStock `
            -OccupiedEndpoints $occupiedEndpoints)) {
        $cleanupErrors.Add($gateFailure)
    }
    if ($cleanupErrors.Count -gt 0) { $runSucceeded = $false }
    if (-not $runSucceeded -and $null -ne $runRoot -and
        (Test-Path -LiteralPath $runRoot) -and
        $runRoot.StartsWith(
            $artifactRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        try {
            Assert-NoReparsePointInExistingPath -Path $artifactRoot -Label 'Artifact root'
            Assert-NoReparsePointInExistingPath -Path $runRoot -Label 'Run root'
            Assert-NoDescendantReparsePoint -Path $runRoot -Label 'Run root'
            Remove-Item -LiteralPath $runRoot -Recurse -Force
        }
        catch { $cleanupErrors.Add('Rejected metadata cleanup failed: ' + $_.Exception.Message) }
    }
    if ($cleanupErrors.Count -gt 0) {
        throw ('Owned-process cleanup failed: ' + ($cleanupErrors -join ' | '))
    }
}

Write-Output 'Bounded stock netchan-fragment capture completed.'
Write-Output $resultSummaryLine
Write-Output $resultArtifactLine
