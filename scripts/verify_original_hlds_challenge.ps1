[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $ClientPath,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^\d{1,3}(?:\.\d{1,3}){3}:\d{1,5}$')]
    [string] $Endpoint,

    [string] $HldsPath,

    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string] $Game = 'valve',

    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string] $Map = 'boot_camp',

    [ValidateRange(1, 65535)]
    [int] $Port = 27015,

    [ValidateRange(1, 120)]
    [int] $ServerStartupSeconds = 10
)

$ErrorActionPreference = 'Stop'

$resolvedClient = (Resolve-Path -LiteralPath $ClientPath).Path
$endpointSeparator = $Endpoint.LastIndexOf(':')
$endpointAddressText = $Endpoint.Substring(0, $endpointSeparator)
$endpointPort = [int] $Endpoint.Substring($endpointSeparator + 1)
$startedServer = $null
$artifactRoot = Join-Path $PSScriptRoot '..\manual-artifacts\original-hlds-challenge'
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runRoot = Join-Path $artifactRoot $timestamp
$null = New-Item -ItemType Directory -Path $runRoot -Force

try {
    if ($HldsPath) {
        $endpointAddress = [Net.IPAddress]::None
        if (-not [Net.IPAddress]::TryParse($endpointAddressText, [ref] $endpointAddress) -or
            $endpointAddress.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork) {
            throw "Endpoint must contain a valid IPv4 address"
        }
        $octets = $endpointAddress.GetAddressBytes()
        $isLoopbackOrPrivate =
            $octets[0] -eq 127 -or
            $octets[0] -eq 10 -or
            ($octets[0] -eq 172 -and $octets[1] -ge 16 -and $octets[1] -le 31) -or
            ($octets[0] -eq 192 -and $octets[1] -eq 168)
        if (-not $isLoopbackOrPrivate) {
            throw "Self-started HLDS verification is restricted to loopback or private LAN IPv4 endpoints"
        }
        if ($endpointPort -ne $Port) {
            throw "Endpoint port $endpointPort must match -Port $Port when the script starts HLDS"
        }

        $resolvedHlds = (Resolve-Path -LiteralPath $HldsPath).Path
        $hldsWorkingDirectory = Split-Path -Parent $resolvedHlds
        $serverStdout = Join-Path $runRoot 'hlds.stdout.log'
        $serverStderr = Join-Path $runRoot 'hlds.stderr.log'
        $serverArguments = @(
            '-console',
            '-game', $Game,
            '+maxplayers', '2',
            '+map', $Map,
            '-port', $Port.ToString([Globalization.CultureInfo]::InvariantCulture),
            '+sv_lan', '1'
        )

        $startedServer = Start-Process `
            -FilePath $resolvedHlds `
            -ArgumentList $serverArguments `
            -WorkingDirectory $hldsWorkingDirectory `
            -RedirectStandardOutput $serverStdout `
            -RedirectStandardError $serverStderr `
            -WindowStyle Hidden `
            -PassThru

        $deadline = [DateTime]::UtcNow.AddSeconds($ServerStartupSeconds)
        do {
            if ($startedServer.HasExited) {
                throw "The explicitly supplied HLDS process exited before verification. See $runRoot"
            }
            Start-Sleep -Milliseconds 250
        } while ([DateTime]::UtcNow -lt $deadline)
    }

    $clientLog = Join-Path $runRoot 'hlclient.log'
    $clientOutput = & $resolvedClient `
        --renderer null `
        --connect $Endpoint `
        --net-trace 2>&1 | Tee-Object -FilePath $clientLog
    $clientExitCode = $LASTEXITCODE
    $combinedOutput = $clientOutput -join "`n"

    if ($clientExitCode -ne 0) {
        throw "hlclient exited with code $clientExitCode. See $clientLog"
    }
    if ($combinedOutput -notmatch 'M1 challenge exchange completed') {
        throw "The challenge success marker was not present. See $clientLog"
    }
    if ($combinedOutput -notmatch 'Connect and sign-on are not implemented yet') {
        throw "The M1 scope marker was not present. See $clientLog"
    }

    $expectedEndpoint = [regex]::Escape($Endpoint)
    $expectedTx = "^\[info\] \[net\] TX $expectedEndpoint connectionless getchallenge, 23 bytes,.*preview=\\xFF\\xFF\\xFF\\xFFgetchallenge steam\\x0A$"
    $txLines = [regex]::Matches($combinedOutput, '(?m)^\[info\] \[net\] TX .*?$')
    if ($txLines.Count -eq 0) {
        throw "No traced getchallenge transmission was present. See $clientLog"
    }
    foreach ($txLine in $txLines) {
        if ($txLine.Value -notmatch $expectedTx) {
            throw "An unexpected M1 transmission was observed: $($txLine.Value)"
        }
    }

    Write-Host "Original HLDS challenge verification passed for $Endpoint"
    Write-Host "Artifacts: $runRoot"
} finally {
    if ($null -ne $startedServer -and -not $startedServer.HasExited) {
        Stop-Process -Id $startedServer.Id -ErrorAction SilentlyContinue
        $startedServer.WaitForExit(5000) | Out-Null
    }
}
