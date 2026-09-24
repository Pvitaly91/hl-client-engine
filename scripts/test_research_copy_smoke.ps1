#requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$launcher = Join-Path $PSScriptRoot 'run_research_copy_smoke.ps1'
$capture = Join-Path $PSScriptRoot 'capture_stock_runtime_state.ps1'
$orchestrator = Join-Path (Join-Path $PSScriptRoot '..') `
    'apps\hlclient_stock_runtime_orchestrator\main.cpp'
$ownedNames = @('hl', 'hlds', 'hlclient_stock_runtime_orchestrator',
    'hlclient_stock_runtime_isolation_guard')
$before = Get-Process -Name $ownedNames -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty Id | Sort-Object
$output = [Collections.Generic.List[string]]::new()
$threw = $false
try {
    & $launcher -ConfirmFunctionalSmoke wrong-token |
        ForEach-Object { [void]$output.Add([string]$_) }
} catch {
    $threw = $_.Exception.Message -ceq
        'Functional smoke requires the exact explicit confirmation token.'
}
$after = Get-Process -Name $ownedNames -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty Id | Sort-Object
if (-not $threw -or
    $output -cnotcontains '[research-copy-smoke] processes_started=0' -or
    $output -cnotcontains '[research-copy-smoke] files_written=0' -or
    $output -cnotcontains '[research-copy-smoke] network_operations=0' -or
    $output -cnotcontains '[research-copy-smoke] wfp_sessions_started=0' -or
    $output -cnotcontains '[research-copy-smoke] evidence_eligible=false') {
    throw 'Functional smoke explicit opt-in gate regressed.'
}
foreach ($processId in @($before)) {
    if ($after -notcontains $processId) {
        throw 'Functional smoke opt-in test terminated a pre-existing process.'
    }
}

$captureText = Get-Content -LiteralPath $capture -Raw
$orchestratorText = Get-Content -LiteralPath $orchestrator -Raw
foreach ($required in @(
        'local_research_copy_smoke_v1',
        'functional_observation_only',
        'evidence_eligible = $false',
        'requiredFunctionalSmokeRoot',
        "'live-usercmd-check'",
        "'live-visual-control'",
        '$projectClientUserCmdMode',
        '$projectClientVisualMode',
        'fresh_project_client_live_visual_control_integrated',
        'fresh_project_client_usercmd_server_motion_verified',
        'usercmd_server_motion_verified')) {
    if ($captureText -cnotmatch [regex]::Escape($required)) {
        throw "Functional wrapper policy marker is absent: $required"
    }
}
foreach ($required in @(
        'HLCLIENT_LOCAL_RESEARCH_COPY_SMOKE_V1',
        'functional-smoke.staged.json',
        '\"evidence_eligible\": ',
        '(project_mode ? "true" : "false")',
        'direct_loopback',
        'L"-steam", L"-game"',
        'L"+log", L"on"',
        'client-entered-game-observed',
        'functional-smoke.staged.json',
        'staged_after_process_cleanup',
        ' connected, address ',
        '--validate-functional-log-observation',
        'ProjectClientStop::live_usercmd_check',
        'ProjectClientStop::live_visual_control',
        'live_usercmd_check_ready',
        'fresh_project_client_live_visual_control_integrated',
        'fresh_project_client_usercmd_server_motion_verified',
        'server_log_offset_before_client_launch')) {
    if ($orchestratorText -cnotmatch [regex]::Escape($required)) {
        throw "Functional orchestrator policy marker is absent: $required"
    }
}
foreach ($strictMarker in @(
        'StockRuntimeCaptureOutputRole::pre_campaign_canary',
        'goldsrc::parse_stock_runtime_capture_scenario',
        'Observed HLDS profile is production-inactive')) {
    if (($captureText + $orchestratorText) -cnotmatch
            [regex]::Escape($strictMarker)) {
        throw "Strict evidence gate marker is absent: $strictMarker"
    }
}
if ([regex]::Matches(
        $captureText,
        [regex]::Escape(
            "'usercmd-movement-verified', 'live-visual-verified'")).Count -lt 2) {
    throw 'F completion key is absent from a strict wrapper status allowlist.'
}

Write-Output '[research-copy-smoke-test] explicit-opt-in=verified'
Write-Output '[research-copy-smoke-test] evidence-eligible=false'
Write-Output '[research-copy-smoke-test] strict-evidence-gates=retained'
Write-Output '[research-copy-smoke-test] result=success'
