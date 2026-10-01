# No-stock fixture: only validate arguments and publish a synthetic report.
[CmdletBinding()]
param(
    [switch]$FunctionalSmoke, [switch]$ProjectClientStockSignon,
    [ValidateSet('Fast','Strict')][string]$ValidationMode,
    [string]$ProjectClientStop, [string]$ProjectClientLiveInput,
    [string]$ProjectClientPrediction, [string]$ConfirmFunctionalSmoke,
    [switch]$ProjectClientMuteGlockFireSound,
    [switch]$RemoteAudioPeer,
    [int]$ProjectClientDurationSeconds,
    [switch]$ProjectClientNoTimeLimit,
    [string]$SteamApiRuntimePath, [string]$AppManifestPath,
    [string]$ResearchHalfLifeRoot, [string]$ClientPath, [string]$HldsPath,
    [string]$CaptureToolPath, [string]$NetworkIsolationGuardPath,
    [string]$OutputRoot, [string]$ServerProfileId,
    [string]$Game, [string]$Map, [int]$ServerPort, [int]$RelayPort,
    [int]$MaximumDurationSeconds,
    [ValidateSet(50)][int]$TestStartHealth = 0
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$release = Join-Path $root 'build\bin\Release'
$expected = @{
    FunctionalSmoke = $true; ProjectClientStockSignon = $true
    ProjectClientStop = 'live-visual-control'; ProjectClientLiveInput = $env:HLCLIENT_MANUAL_TEST_INPUT
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
    Game = 'valve'; Map = $env:HLCLIENT_MANUAL_TEST_MAP; ServerPort = 27243; RelayPort = 27242
    MaximumDurationSeconds = 90
}
foreach ($key in $expected.Keys) {
    if ((Get-Variable -Name $key -ValueOnly) -ne $expected[$key]) { throw "Wrong argument: $key" }
}
if ($ProjectClientPrediction -notin @('reference', 'off')) { throw 'Wrong prediction' }
if ($ValidationMode -notin @('Fast','Strict')) { throw 'ValidationMode argv lost' }
if ($ValidationMode -cne $env:HLCLIENT_MANUAL_TEST_VALIDATION) { throw 'wrong ValidationMode forwarded' }
if ($TestStartHealth -ne [int]$env:HLCLIENT_MANUAL_TEST_START_HEALTH) { throw 'TestStartHealth argv lost' }
if ($ProjectClientMuteGlockFireSound.IsPresent -ne ($env:HLCLIENT_MANUAL_TEST_MUTE_FIRE -eq 'true')) { throw 'MuteGlockFireSound argv lost' }
if ($RemoteAudioPeer.IsPresent -ne ($env:HLCLIENT_MANUAL_TEST_REMOTE_PEER -eq 'true')) { throw 'RemoteAudioPeer argv lost' }
if ($ProjectClientDurationSeconds -ne [int]$env:HLCLIENT_MANUAL_TEST_DURATION) { throw 'Duration argv lost' }
if ($ProjectClientNoTimeLimit.IsPresent -ne ($env:HLCLIENT_MANUAL_TEST_UNLIMITED -eq 'true')) { throw 'NoTimeLimit argv lost' }
if ($TestStartHealth -eq 50 -and ($Map -ne 'crossfire' -or $ProjectClientPrediction -ne 'reference' -or $ProjectClientLiveInput -ne 'keyboard-mouse')) { throw 'Incompatible test health argv' }
[Console]::Error.WriteLine('fixture stderr visible')
if ($env:HLCLIENT_MANUAL_TEST_CASE -eq 'throw') { throw 'fixture terminating failure' }
if ($env:HLCLIENT_MANUAL_TEST_CASE -eq 'silent') { exit 9 }
$runId = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
$reportDirectory = Join-Path $OutputRoot $runId
[void][IO.Directory]::CreateDirectory($reportDirectory)
$reportName = if ($env:HLCLIENT_MANUAL_TEST_CASE -eq 'incomplete') {
    'functional-smoke-wrapper.incomplete.json'
} else { 'functional-smoke-wrapper.json' }
$reportPath = Join-Path $reportDirectory $reportName
[IO.File]::WriteAllText($reportPath, '{"result":"no_stock_fixture"}')
# A newer, unrelated report must never be selected by the launcher.
$unrelated = Join-Path $OutputRoot 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'
[void][IO.Directory]::CreateDirectory($unrelated)
[IO.File]::WriteAllText((Join-Path $unrelated 'functional-smoke-wrapper.json'), '{}')
Write-Output "fixture arguments verified: prediction=$ProjectClientPrediction map=$Map input=$ProjectClientLiveInput mute-fire=$($ProjectClientMuteGlockFireSound.IsPresent)"
Write-Output "fixture timing verified: seconds=$ProjectClientDurationSeconds unlimited=$($ProjectClientNoTimeLimit.IsPresent)"
Write-Output "[research-copy-smoke] run_id=$runId"
Write-Output ("[research-copy-smoke] report_path={0}" -f $(if (
    $env:HLCLIENT_MANUAL_TEST_CASE -eq 'foreign-report') {
        Join-Path $unrelated 'functional-smoke-wrapper.json'
    } else { $reportPath }))
if ($env:HLCLIENT_MANUAL_TEST_CASE -eq 'ambiguous') {
    Write-Output '[research-copy-smoke] run_id=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'
}
Write-Output '[research-copy-smoke] result=no_stock_fixture'
if ($env:HLCLIENT_MANUAL_TEST_CASE -in @('incomplete', 'foreign-report')) {
    Write-Output '[research-copy-smoke] application_primary_error=runtime_record_failed'
    Write-Output '[research-copy-smoke] application_control_error=unsupported_opcode'
    Write-Output '[research-copy-smoke] restoration_status=unknown'
    exit 2
}
if ($ProjectClientPrediction -eq 'off') { exit 23 }
exit 0
