#requires -Version 5.1

<#
.SYNOPSIS
Runs the bounded M4.7.1.1 first-observation campaign sequentially.

.DESCRIPTION
The campaign has the same explicit, case-sensitive active-capture opt-in as one
capture. It never stores the token, stops on the first safety/restoration/drift
failure, and reports only accepted counts actually confirmed by the final
checker. No movement input automation is used.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [switch]$EnableActiveCapture,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ConfirmActiveCapture,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ResearchHalfLifeRoot,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ClientPath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$HldsPath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$CaptureToolPath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$NetworkIsolationGuardPath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$CheckerPath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$AppManifestPath,

    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]$OutputRoot = '.\manual-artifacts\stock-runtime',

    [Parameter()]
    [ValidateRange(1024, 65486)]
    [int]$FirstRelayPort = 27140,

    [Parameter()]
    [ValidateRange(30, 300)]
    [int]$BaselineDurationSeconds = 45,

    [Parameter()]
    [ValidateRange(30, 300)]
    [int]$IdleDurationSeconds = 60
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$requiredToken = 'HLCLIENT_STOCK_RUNTIME_ACTIVE_CAPTURE_V1'
if (-not $EnableActiveCapture -or $ConfirmActiveCapture -cne $requiredToken) {
    Write-Output '[stock-runtime-campaign] active-capture=explicit-opt-in-required'
    Write-Output '[stock-runtime-campaign] attempted-runs=0'
    throw 'First-observation campaign requires the exact explicit active-capture token.'
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
$captureScript = Join-Path $PSScriptRoot 'capture_stock_runtime_state.ps1'
$checker = [IO.Path]::GetFullPath($CheckerPath)
if (-not (Test-Path -LiteralPath $checker -PathType Leaf) -or
    [IO.Path]::GetFileName($checker) -cne 'hlclient_stock_runtime_check.exe' -or
    -not $checker.StartsWith(
        $repositoryRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'CheckerPath must name the repository-built stock-runtime checker.'
}

$matrix = [Collections.Generic.List[object]]::new()
function Add-CampaignRuns {
    param([string]$Map, [string]$Scenario, [int]$Count, [int]$Duration)
    for ($index = 0; $index -lt $Count; $index++) {
        [void]$matrix.Add([pscustomobject]@{
                Map = $Map; Scenario = $Scenario; Duration = $Duration })
    }
}
Add-CampaignRuns boot_camp baseline 6 $BaselineDurationSeconds
Add-CampaignRuns crossfire baseline 4 $BaselineDurationSeconds
Add-CampaignRuns stalkyard baseline 4 $BaselineDurationSeconds
Add-CampaignRuns crossfire idle-runtime 4 $IdleDurationSeconds
Add-CampaignRuns boot_camp drop-server-to-client-transport-ordinal 2 $BaselineDurationSeconds
Add-CampaignRuns crossfire duplicate-server-to-client-transport-ordinal 1 $BaselineDurationSeconds
Add-CampaignRuns stalkyard reorder-server-to-client-transport-ordinal 1 $BaselineDurationSeconds
Add-CampaignRuns boot_camp reconnect 2 $IdleDurationSeconds
if ($matrix.Count -ne 24) { throw 'Internal campaign matrix does not contain 24 runs.' }

$counts = @{
    baseline = 0; 'idle-runtime' = 0; reconnect = 0
    'drop-server-to-client-transport-ordinal' = 0
    'duplicate-server-to-client-transport-ordinal' = 0
    'reorder-server-to-client-transport-ordinal' = 0
}
$attempted = 0
$accepted = 0
$rejected = 0
$incomplete = 0
$pending = 0

for ($ordinal = 0; $ordinal -lt $matrix.Count; $ordinal++) {
    $case = $matrix[$ordinal]
    $relayPort = $FirstRelayPort + ($ordinal * 2)
    $serverPort = $relayPort + 1
    $attempted++
    $output = [Collections.Generic.List[string]]::new()
    try {
        & $captureScript `
            -EnableActiveCapture `
            -ConfirmActiveCapture $ConfirmActiveCapture `
            -ResearchHalfLifeRoot $ResearchHalfLifeRoot `
            -ClientPath $ClientPath `
            -HldsPath $HldsPath `
            -CaptureToolPath $CaptureToolPath `
            -NetworkIsolationGuardPath $NetworkIsolationGuardPath `
            -AppManifestPath $AppManifestPath `
            -Game valve `
            -Map $case.Map `
            -Scenario $case.Scenario `
            -RelayPort $relayPort `
            -ServerPort $serverPort `
            -OutputRoot $OutputRoot `
            -MaximumDurationSeconds $case.Duration |
            ForEach-Object { [void]$output.Add($_.ToString()) }
        $runLines = @($output | Where-Object {
                $_ -match '^\[stock-runtime-capture\] run-id=[0-9a-f]{32}$' })
        if ($runLines.Count -ne 1 -or
            $output -cnotcontains '[stock-runtime-capture] accepted-evidence-run=true') {
            throw 'Capture did not publish exactly one accepted run ID.'
        }
        $runId = $runLines[0].Substring('[stock-runtime-capture] run-id='.Length)
        $runRoot = Join-Path ([IO.Path]::GetFullPath($OutputRoot)) $runId
        $saved = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            $checkerOutput = @(& $checker --capture-root $runRoot `
                --scenario first-observation 2>&1 | ForEach-Object { $_.ToString() })
            $checkerExit = $LASTEXITCODE
        } finally { $ErrorActionPreference = $saved }
        if ($checkerExit -ne 0 -or
            $checkerOutput -notcontains '[stock-runtime] accepted-run=true' -or
            $checkerOutput -notcontains '[stock-runtime] result=first-observation') {
            throw 'Final checker did not accept the newly published run.'
        }
        $accepted++
        $counts[$case.Scenario]++
        Write-Output ("[stock-runtime-campaign] run={0}/24 scenario={1} map={2} accepted=true" -f
            ($ordinal + 1), $case.Scenario, $case.Map)
    } catch {
        $failure = 'campaign_run_failed'
        $failureLine = @($output | Where-Object {
                $_ -match '^\[stock-runtime-capture\] failure-category=[A-Za-z0-9_.:-]+$' } |
            Select-Object -Last 1)
        if ($failureLine.Count -eq 1) {
            $failure = $failureLine[0].Substring(
                '[stock-runtime-capture] failure-category='.Length)
        }
        if ($failure -match '(?:_pending$|capability_unavailable)') {
            $pending++
            $incomplete++
            Write-Output "[stock-runtime-campaign] pending-scenario=$($case.Scenario)"
        } elseif ($failure -match '(?:incomplete|timeout|early_exit)') { $incomplete++ }
        else { $rejected++ }
        Write-Output "[stock-runtime-campaign] failure-category=$failure"
        Write-Output "[stock-runtime-campaign] attempted-runs=$attempted"
        Write-Output "[stock-runtime-campaign] accepted-runs=$accepted"
        Write-Output "[stock-runtime-campaign] rejected-runs=$rejected"
        Write-Output "[stock-runtime-campaign] incomplete-runs=$incomplete"
        Write-Output "[stock-runtime-campaign] pending-runs=$pending"
        Write-Output "[stock-runtime-campaign] unattempted-runs=$($matrix.Count - $attempted)"
        Write-Output ("[stock-runtime-campaign] result={0}" -f
            $(if ($pending -ne 0) { 'pending' } else { 'failed' }))
        throw 'Campaign stopped on the first non-accepted run; no later stock process was started.'
    }
}

Write-Output "[stock-runtime-campaign] baseline-accepted=$($counts.baseline)"
Write-Output "[stock-runtime-campaign] idle-accepted=$($counts['idle-runtime'])"
Write-Output "[stock-runtime-campaign] reconnect-accepted=$($counts.reconnect)"
Write-Output ("[stock-runtime-campaign] perturbation-accepted={0}" -f
    ($counts['drop-server-to-client-transport-ordinal'] +
        $counts['duplicate-server-to-client-transport-ordinal'] +
        $counts['reorder-server-to-client-transport-ordinal']))
Write-Output "[stock-runtime-campaign] attempted-runs=$attempted"
Write-Output "[stock-runtime-campaign] accepted-runs=$accepted"
Write-Output "[stock-runtime-campaign] rejected-runs=$rejected"
Write-Output "[stock-runtime-campaign] incomplete-runs=$incomplete"
Write-Output "[stock-runtime-campaign] pending-runs=$pending"
Write-Output '[stock-runtime-campaign] unattempted-runs=0'
Write-Output '[stock-runtime-campaign] input-automation-used=false'
Write-Output '[stock-runtime-campaign] result=success'
