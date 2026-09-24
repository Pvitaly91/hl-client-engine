#requires -Version 5.1

<#
.SYNOPSIS
Runs one bounded stock Half-Life client/server smoke in the existing research copy.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [AllowEmptyString()]
    [string]$ConfirmFunctionalSmoke,

    [ValidateNotNullOrEmpty()]
    [string]$ResearchHalfLifeRoot = 'D:\DEV\HLCLIENT-RESEARCH\Half-Life',

    [ValidateSet('valve')]
    [string]$Game = 'valve',

    [ValidateSet('boot_camp')]
    [string]$Map = 'boot_camp',

    [ValidateNotNullOrEmpty()]
    [string]$BuildBin = '',

    [ValidateNotNullOrEmpty()]
    [string]$AppManifestPath = 'D:\Steam\steamapps\appmanifest_70.acf',

    [ValidateRange(60, 90)]
    [int]$MaximumDurationSeconds = 90
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$confirmationToken = 'HLCLIENT_LOCAL_RESEARCH_COPY_SMOKE_V1'
if ($ConfirmFunctionalSmoke -cne $confirmationToken) {
    Write-Output '[research-copy-smoke] mode=local_research_copy_smoke_v1'
    Write-Output '[research-copy-smoke] explicit_opt_in=required'
    Write-Output '[research-copy-smoke] processes_started=0'
    Write-Output '[research-copy-smoke] files_written=0'
    Write-Output '[research-copy-smoke] network_operations=0'
    Write-Output '[research-copy-smoke] wfp_sessions_started=0'
    Write-Output '[research-copy-smoke] evidence_eligible=false'
    throw 'Functional smoke requires the exact explicit confirmation token.'
}

$repositoryRoot = [IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
if ([string]::IsNullOrWhiteSpace($BuildBin)) {
    $BuildBin = Join-Path $repositoryRoot 'build\bin\Release'
}
$buildRoot = [IO.Path]::GetFullPath($BuildBin).TrimEnd('\', '/')
$research = [IO.Path]::GetFullPath($ResearchHalfLifeRoot).TrimEnd('\', '/')
$captureScript = Join-Path $PSScriptRoot 'capture_stock_runtime_state.ps1'
$relay = Join-Path $buildRoot 'hlclient_stock_runtime_capture.exe'
$guard = Join-Path $buildRoot 'hlclient_stock_runtime_isolation_guard.exe'
$client = Join-Path $research 'hl.exe'
$server = Join-Path $research 'hlds.exe'
$outputRoot = Join-Path $repositoryRoot 'manual-artifacts\research-copy-smoke'

foreach ($required in @($captureScript, $relay, $guard, $client, $server,
        $AppManifestPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required functional-smoke file is absent: $required"
    }
}

function Get-FreeLoopbackUdpPort {
    param([Collections.Generic.HashSet[int]]$Excluded)
    for ($attempt = 0; $attempt -lt 32; $attempt++) {
        $socket = [Net.Sockets.Socket]::new(
            [Net.Sockets.AddressFamily]::InterNetwork,
            [Net.Sockets.SocketType]::Dgram,
            [Net.Sockets.ProtocolType]::Udp)
        try {
            $socket.ExclusiveAddressUse = $true
            $socket.Bind([Net.IPEndPoint]::new([Net.IPAddress]::Loopback, 0))
            $port = ([Net.IPEndPoint]$socket.LocalEndPoint).Port
            if ($port -ge 1024 -and $port -le 65534 -and
                -not $Excluded.Contains($port)) {
                [void]$Excluded.Add($port)
                return $port
            }
        } finally {
            $socket.Dispose()
        }
    }
    throw 'Unable to select a distinct free loopback UDP port.'
}

$excludedPorts = [Collections.Generic.HashSet[int]]::new()
$serverPort = Get-FreeLoopbackUdpPort $excludedPorts
$excludedRelayPort = Get-FreeLoopbackUdpPort $excludedPorts

Write-Output '[research-copy-smoke] mode=local_research_copy_smoke_v1'
Write-Output '[research-copy-smoke] purpose=functional_smoke'
Write-Output '[research-copy-smoke] evidence_eligible=false'
Write-Output '[research-copy-smoke] route=direct_loopback'
Write-Output ("[research-copy-smoke] server_endpoint=127.0.0.1:{0}" -f
    $serverPort)

& $captureScript -FunctionalSmoke `
    -ConfirmFunctionalSmoke $confirmationToken `
    -ResearchHalfLifeRoot $research `
    -ClientPath $client `
    -HldsPath $server `
    -CaptureToolPath $relay `
    -NetworkIsolationGuardPath $guard `
    -AppManifestPath $AppManifestPath `
    -Game $Game `
    -Map $Map `
    -RelayPort $excludedRelayPort `
    -ServerPort $serverPort `
    -OutputRoot $outputRoot `
    -MaximumDurationSeconds $MaximumDurationSeconds `
    -ServerProfileId 'steam-hlds-10210-no-mode-banner-v1'
