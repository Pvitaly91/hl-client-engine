# No-stock fixture: only validate arguments and publish a synthetic report.
[CmdletBinding()]
param(
    [switch]$FunctionalSmoke, [switch]$ProjectClientStockSignon,
    [string]$ProjectClientStop, [string]$ProjectClientLiveInput,
    [string]$ProjectClientPrediction, [string]$ConfirmFunctionalSmoke,
    [string]$SteamApiRuntimePath, [string]$AppManifestPath,
    [string]$ResearchHalfLifeRoot, [string]$ClientPath, [string]$HldsPath,
    [string]$CaptureToolPath, [string]$NetworkIsolationGuardPath,
    [string]$OutputRoot, [string]$ServerProfileId,
    [string]$Game, [string]$Map, [int]$ServerPort, [int]$RelayPort,
    [int]$MaximumDurationSeconds
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$release = Join-Path $root 'build\bin\Release'
$expected = @{
    FunctionalSmoke = $true; ProjectClientStockSignon = $true
    ProjectClientStop = 'live-visual-control'; ProjectClientLiveInput = 'keyboard-mouse'
    ConfirmFunctionalSmoke = 'HLCLIENT_LOCAL_RESEARCH_COPY_SMOKE_V1'
    SteamApiRuntimePath = Join-Path $root 'Steam apps\common\Half-Life\steam_api.dll'
    AppManifestPath = Join-Path $root 'Steam apps\appmanifest_70.acf'
    ResearchHalfLifeRoot = Join-Path $root 'research game'
    ClientPath = Join-Path $release 'hlclient.exe'
    HldsPath = Join-Path $root 'research game\hlds.exe'
    CaptureToolPath = Join-Path $release 'hlclient_stock_runtime_capture.exe'
    NetworkIsolationGuardPath = Join-Path $release 'hlclient_stock_runtime_isolation_guard.exe'
    OutputRoot = Join-Path $root 'manual-artifacts\research-copy-smoke'
    ServerProfileId = 'steam-hlds-10210-no-mode-banner-v1'
    Game = 'valve'; Map = 'boot_camp'; ServerPort = 27243; RelayPort = 27242
    MaximumDurationSeconds = 90
}
foreach ($key in $expected.Keys) {
    if ((Get-Variable -Name $key -ValueOnly) -ne $expected[$key]) { throw "Wrong argument: $key" }
}
if ($ProjectClientPrediction -notin @('reference', 'off')) { throw 'Wrong prediction' }
[Console]::Error.WriteLine('fixture stderr visible')
if ($env:HLCLIENT_MANUAL_TEST_CASE -eq 'throw') { throw 'fixture terminating failure' }
if ($env:HLCLIENT_MANUAL_TEST_CASE -eq 'silent') { exit 9 }
$runId = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
$reportDirectory = Join-Path $OutputRoot $runId
[void][IO.Directory]::CreateDirectory($reportDirectory)
[IO.File]::WriteAllText((Join-Path $reportDirectory 'functional-smoke-wrapper.json'), '{"result":"no_stock_fixture"}')
# A newer, unrelated report must never be selected by the launcher.
$unrelated = Join-Path $OutputRoot 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'
[void][IO.Directory]::CreateDirectory($unrelated)
[IO.File]::WriteAllText((Join-Path $unrelated 'functional-smoke-wrapper.json'), '{}')
Write-Output "fixture arguments verified: prediction=$ProjectClientPrediction"
Write-Output "[research-copy-smoke] run_id=$runId"
if ($env:HLCLIENT_MANUAL_TEST_CASE -eq 'ambiguous') {
    Write-Output '[research-copy-smoke] run_id=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'
}
Write-Output '[research-copy-smoke] result=no_stock_fixture'
if ($ProjectClientPrediction -eq 'off') { exit 23 }
exit 0
