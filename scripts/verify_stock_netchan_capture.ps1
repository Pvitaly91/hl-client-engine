<#
.SYNOPSIS
Runs a bounded loopback stock-client/stock-HLDS netchan capture.

.DESCRIPTION
This script is an orchestration and metadata verifier, not a UDP relay. It
starts only the explicitly supplied user-owned client, HLDS, and relay. The
relay listens on 127.0.0.1 at -Port and forwards to HLDS on 127.0.0.1 at
-Port + 1. All redirected output stays under the gitignored
manual-artifacts/netchan-captures directory.

The relay executable or PowerShell script must accept these arguments:
-ListenAddress, -ListenPort, -UpstreamAddress, -UpstreamPort,
-OutputDirectory, -SummaryPath, -MaxPackets, -MaxBytes, and -TimeoutSeconds.
It must use separate client/upstream sockets, preserve one upstream socket,
validate the exact upstream endpoint, and stop within all three supplied
bounds.

On successful bounded completion it must exit zero and write at -SummaryPath a
JSON object with schema "hlclient.stock-netchan-capture-summary.v1",
completion "bounded_complete", bounded non-negative packetCount, byteCount,
postAcceptPacketCount, and elapsedMilliseconds values, plus Boolean true values
for acceptObserved, sequencedObserved, sameUpstreamSocket, and
exactEndpointValidation. The summary must contain metadata only, never packet,
authentication, identity, or payload bytes; no other properties are accepted.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string] $ClientPath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string] $HldsPath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string] $RelayPath,

    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string] $Game = 'valve',

    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string] $Map = 'boot_camp',

    [ValidateRange(1024, 65534)]
    [int] $Port = 27128,

    [ValidateRange(1, 512)]
    [int] $CapturePacketCount = 64,

    [ValidateRange(4096, 4194304)]
    [int] $CaptureByteLimit = 1048576,

    [ValidateRange(5, 120)]
    [int] $TimeoutSeconds = 30,

    [ValidateRange(1, 30)]
    [int] $ServerStartupSeconds = 5
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-ExplicitFile {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Path,

        [Parameter(Mandatory = $true)]
        [string] $Label
    )

    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction Stop
    if ($resolved.Provider.Name -ne 'FileSystem') {
        throw "$Label must be an explicit filesystem path"
    }
    $item = Get-Item -LiteralPath $resolved.Path -Force
    if ($item.PSIsContainer) {
        throw "$Label must name a file, not a directory"
    }
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label must not be a symbolic link or reparse point"
    }
    return [IO.Path]::GetFullPath($item.FullName)
}

function Quote-NativePathArgument {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Path
    )

    if ($Path.Contains('"')) {
        throw 'A native process path argument cannot contain a quotation mark'
    }
    return '"' + $Path + '"'
}

function New-OwnedProcessRecord {
    param(
        [Parameter(Mandatory = $true)]
        [Diagnostics.Process] $Process,

        [Parameter(Mandatory = $true)]
        [string] $ExpectedExecutable
    )

    $Process.Refresh()
    return [pscustomobject]@{
        Id = $Process.Id
        StartTimeUtc = $Process.StartTime.ToUniversalTime()
        ExpectedExecutable = [IO.Path]::GetFullPath($ExpectedExecutable)
    }
}

function Stop-VerifiedOwnedProcess {
    param(
        [AllowNull()]
        [object] $Record
    )

    if ($null -eq $Record) {
        return
    }

    $current = Get-Process -Id $Record.Id -ErrorAction SilentlyContinue
    if ($null -eq $current) {
        return
    }

    try {
        $currentPath = [IO.Path]::GetFullPath($current.Path)
        $currentStartTime = $current.StartTime.ToUniversalTime()
    } catch {
        Write-Warning "PID $($Record.Id) could not be identity-checked and was not stopped"
        return
    }

    $samePath = [string]::Equals(
        $currentPath,
        $Record.ExpectedExecutable,
        [StringComparison]::OrdinalIgnoreCase)
    $sameStartTime = $currentStartTime -eq $Record.StartTimeUtc
    if (-not $samePath -or -not $sameStartTime) {
        Write-Warning "PID $($Record.Id) no longer matches the started process and was not stopped"
        return
    }

    try {
        $current.Kill()
        $null = $current.WaitForExit(5000)
    } catch [InvalidOperationException] {
        # The verified process exited between the identity check and Kill().
    } catch {
        Write-Warning "Verified PID $($Record.Id) could not be stopped"
    }
}

function Get-RequiredSummaryProperty {
    param(
        [Parameter(Mandatory = $true)]
        [object] $Summary,

        [Parameter(Mandatory = $true)]
        [string] $Name
    )

    $property = $Summary.PSObject.Properties[$Name]
    if ($null -eq $property) {
        throw "Relay summary is missing required property '$Name'"
    }
    return $property.Value
}

function Get-SummaryInteger {
    param(
        [Parameter(Mandatory = $true)]
        [object] $Summary,

        [Parameter(Mandatory = $true)]
        [string] $Name
    )

    $text = [string](Get-RequiredSummaryProperty -Summary $Summary -Name $Name)
    $value = 0L
    $valid = [long]::TryParse(
        $text,
        [Globalization.NumberStyles]::None,
        [Globalization.CultureInfo]::InvariantCulture,
        [ref] $value)
    if (-not $valid) {
        throw "Relay summary property '$Name' must be a non-negative integer"
    }
    return $value
}

function Assert-SummaryBoolean {
    param(
        [Parameter(Mandatory = $true)]
        [object] $Summary,

        [Parameter(Mandatory = $true)]
        [string] $Name
    )

    $value = Get-RequiredSummaryProperty -Summary $Summary -Name $Name
    if ($value -isnot [bool] -or -not $value) {
        throw "Relay summary property '$Name' must be true"
    }
}

$resolvedClient = Resolve-ExplicitFile -Path $ClientPath -Label 'ClientPath'
$resolvedHlds = Resolve-ExplicitFile -Path $HldsPath -Label 'HldsPath'
$resolvedRelay = Resolve-ExplicitFile -Path $RelayPath -Label 'RelayPath'

if ([IO.Path]::GetExtension($resolvedClient) -ine '.exe') {
    throw 'ClientPath must name an executable file'
}
if ([IO.Path]::GetExtension($resolvedHlds) -ine '.exe') {
    throw 'HldsPath must name an executable file'
}
$relayExtension = [IO.Path]::GetExtension($resolvedRelay)
if ($relayExtension -ine '.exe' -and $relayExtension -ine '.ps1') {
    throw 'RelayPath must name an .exe or .ps1 bounded relay'
}

$loopbackAddress = '127.0.0.1'
$upstreamPort = $Port + 1
$clientEndpoint = "${loopbackAddress}:$Port"
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifactRoot = Join-Path $repositoryRoot 'manual-artifacts\netchan-captures'
$null = New-Item -ItemType Directory -Path $artifactRoot -Force
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$runRoot = Join-Path $artifactRoot "verified-$timestamp"
$summaryPath = Join-Path $runRoot 'summary.json'
if (Test-Path -LiteralPath $runRoot) {
    throw "Refusing to reuse an existing capture directory: $runRoot"
}
$null = New-Item -ItemType Directory -Path $runRoot

$serverRecord = $null
$relayRecord = $null
$clientRecord = $null

try {
    $serverStdout = Join-Path $runRoot 'hlds.stdout.log'
    $serverStderr = Join-Path $runRoot 'hlds.stderr.log'
    $serverArguments = @(
        '-console',
        '-game', $Game,
        '-port', $upstreamPort.ToString([Globalization.CultureInfo]::InvariantCulture),
        '+ip', $loopbackAddress,
        '+sv_lan', '1',
        '+maxplayers', '2',
        '+map', $Map
    )
    $serverProcess = Start-Process `
        -FilePath $resolvedHlds `
        -ArgumentList $serverArguments `
        -WorkingDirectory (Split-Path -Parent $resolvedHlds) `
        -RedirectStandardOutput $serverStdout `
        -RedirectStandardError $serverStderr `
        -WindowStyle Hidden `
        -PassThru
    $serverRecord = New-OwnedProcessRecord `
        -Process $serverProcess `
        -ExpectedExecutable $resolvedHlds

    $serverDeadline = [DateTime]::UtcNow.AddSeconds($ServerStartupSeconds)
    while ([DateTime]::UtcNow -lt $serverDeadline) {
        if ($serverProcess.HasExited) {
            throw "The explicitly supplied HLDS process exited during startup. See $runRoot"
        }
        Start-Sleep -Milliseconds 250
    }

    $relayCommonArguments = @(
        '-ListenAddress', $loopbackAddress,
        '-ListenPort', $Port.ToString([Globalization.CultureInfo]::InvariantCulture),
        '-UpstreamAddress', $loopbackAddress,
        '-UpstreamPort', $upstreamPort.ToString([Globalization.CultureInfo]::InvariantCulture),
        '-OutputDirectory', (Quote-NativePathArgument $runRoot),
        '-SummaryPath', (Quote-NativePathArgument $summaryPath),
        '-MaxPackets', $CapturePacketCount.ToString([Globalization.CultureInfo]::InvariantCulture),
        '-MaxBytes', $CaptureByteLimit.ToString([Globalization.CultureInfo]::InvariantCulture),
        '-TimeoutSeconds', $TimeoutSeconds.ToString([Globalization.CultureInfo]::InvariantCulture)
    )

    $relayExecutable = $resolvedRelay
    $relayArguments = $relayCommonArguments
    if ($relayExtension -ieq '.ps1') {
        $relayExecutable = (Get-Process -Id $PID).Path
        $relayArguments = @(
            '-NoLogo',
            '-NoProfile',
            '-NonInteractive',
            '-File', (Quote-NativePathArgument $resolvedRelay)
        ) + $relayCommonArguments
    }

    $relayStdout = Join-Path $runRoot 'relay.stdout.log'
    $relayStderr = Join-Path $runRoot 'relay.stderr.log'
    $relayProcess = Start-Process `
        -FilePath $relayExecutable `
        -ArgumentList $relayArguments `
        -WorkingDirectory (Split-Path -Parent $resolvedRelay) `
        -RedirectStandardOutput $relayStdout `
        -RedirectStandardError $relayStderr `
        -WindowStyle Hidden `
        -PassThru
    $relayRecord = New-OwnedProcessRecord `
        -Process $relayProcess `
        -ExpectedExecutable $relayExecutable

    Start-Sleep -Milliseconds 250
    if ($relayProcess.HasExited) {
        throw "The explicitly supplied relay exited before client startup. See $runRoot"
    }

    $clientStdout = Join-Path $runRoot 'hl.stdout.log'
    $clientStderr = Join-Path $runRoot 'hl.stderr.log'
    $clientArguments = @(
        '-console',
        '-game', $Game,
        '+connect', $clientEndpoint
    )
    $clientProcess = Start-Process `
        -FilePath $resolvedClient `
        -ArgumentList $clientArguments `
        -WorkingDirectory (Split-Path -Parent $resolvedClient) `
        -RedirectStandardOutput $clientStdout `
        -RedirectStandardError $clientStderr `
        -WindowStyle Hidden `
        -PassThru
    $clientRecord = New-OwnedProcessRecord `
        -Process $clientProcess `
        -ExpectedExecutable $resolvedClient

    $relayWaitMilliseconds = ($TimeoutSeconds + 5) * 1000
    if (-not $relayProcess.WaitForExit($relayWaitMilliseconds)) {
        throw "The bounded relay did not exit within its timeout and grace period. See $runRoot"
    }
    if ($relayProcess.ExitCode -ne 0) {
        throw "The bounded relay exited with code $($relayProcess.ExitCode). See $runRoot"
    }

    if (-not (Test-Path -LiteralPath $summaryPath -PathType Leaf)) {
        throw "The relay did not create its required summary. See $runRoot"
    }
    $summaryFile = Get-Item -LiteralPath $summaryPath
    if (($summaryFile.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "The relay summary must not be a reparse point. See $runRoot"
    }
    if ($summaryFile.Length -le 0 -or $summaryFile.Length -gt 65536) {
        throw "The relay summary is empty or exceeds the 64 KiB metadata bound. See $runRoot"
    }

    try {
        $summaryText = Get-Content -LiteralPath $summaryPath -Raw
        $summary = $summaryText | ConvertFrom-Json -ErrorAction Stop
    } catch {
        throw "The relay summary is not valid bounded metadata. See $runRoot"
    }
    $allowedSummaryProperties = @(
        'schema',
        'completion',
        'packetCount',
        'byteCount',
        'postAcceptPacketCount',
        'elapsedMilliseconds',
        'acceptObserved',
        'sequencedObserved',
        'sameUpstreamSocket',
        'exactEndpointValidation'
    )
    foreach ($property in $summary.PSObject.Properties) {
        if ($property.Name -notin $allowedSummaryProperties) {
            throw "The relay summary contains non-metadata fields. See $runRoot"
        }
    }
    $schema = Get-RequiredSummaryProperty -Summary $summary -Name 'schema'
    $completion = Get-RequiredSummaryProperty -Summary $summary -Name 'completion'
    if ($schema -ne 'hlclient.stock-netchan-capture-summary.v1') {
        throw "The relay summary uses an unsupported schema. See $runRoot"
    }
    if ($completion -ne 'bounded_complete') {
        throw "The relay did not report bounded completion. See $runRoot"
    }

    $packetCount = Get-SummaryInteger -Summary $summary -Name 'packetCount'
    $byteCount = Get-SummaryInteger -Summary $summary -Name 'byteCount'
    $postAcceptPacketCount = Get-SummaryInteger `
        -Summary $summary `
        -Name 'postAcceptPacketCount'
    $elapsedMilliseconds = Get-SummaryInteger `
        -Summary $summary `
        -Name 'elapsedMilliseconds'

    if ($packetCount -lt 1 -or $packetCount -gt $CapturePacketCount) {
        throw "The relay-reported packet count is outside the requested bound. See $runRoot"
    }
    if ($byteCount -lt 1 -or $byteCount -gt $CaptureByteLimit) {
        throw "The relay-reported byte count is outside the requested bound. See $runRoot"
    }
    if ($postAcceptPacketCount -lt 1 -or $postAcceptPacketCount -gt $packetCount) {
        throw "The relay did not report a bounded post-ACCEPT packet set. See $runRoot"
    }
    if ($elapsedMilliseconds -gt ($TimeoutSeconds * 1000L)) {
        throw "The relay-reported capture duration exceeds the requested bound. See $runRoot"
    }

    Assert-SummaryBoolean -Summary $summary -Name 'acceptObserved'
    Assert-SummaryBoolean -Summary $summary -Name 'sequencedObserved'
    Assert-SummaryBoolean -Summary $summary -Name 'sameUpstreamSocket'
    Assert-SummaryBoolean -Summary $summary -Name 'exactEndpointValidation'

    Write-Host 'Bounded stock netchan capture completed.'
    Write-Host (
        'Relay summary: packets={0}; bytes={1}; post-ACCEPT packets={2}; elapsed={3} ms' -f
        $packetCount,
        $byteCount,
        $postAcceptPacketCount,
        $elapsedMilliseconds)
    Write-Host "Ignored artifacts: $runRoot"
    Write-Host (
        'This wrapper does not claim project-client stock compatibility ' +
        'or decode payload bytes.')
} finally {
    Stop-VerifiedOwnedProcess -Record $clientRecord
    Stop-VerifiedOwnedProcess -Record $relayRecord
    Stop-VerifiedOwnedProcess -Record $serverRecord
}
