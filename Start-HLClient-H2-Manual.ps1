#requires -Version 5.1
<#
.SYNOPSIS
One manual H2 session through the existing owned/isolation/restoration runner.
.DESCRIPTION
Run in an administrative PowerShell 7 console. CheckOnly is read-only and
does not require elevation or invoke the runner. No automatic comparison run.
#>
[CmdletBinding()]
param(
    [ValidateSet('reference', 'off')]
    [string]$Prediction = 'reference',
    [switch]$CheckOnly,
    [switch]$SourceOnly,
    [ValidateNotNullOrEmpty()]
    [string]$ResearchHalfLifeRoot = 'D:\DEV\HLCLIENT-RESEARCH\Half-Life',
    [ValidateNotNullOrEmpty()]
    [string]$SteamAppsRoot = 'D:\Steam\steamapps'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-H2ManualConfiguration {
    param([string]$Root, [string]$Mode,
          [string]$ResearchRoot = 'D:\DEV\HLCLIENT-RESEARCH\Half-Life',
          [string]$SteamRoot = 'D:\Steam\steamapps')
    $release = Join-Path $Root 'build\bin\Release'
    $research = [IO.Path]::GetFullPath($ResearchRoot)
    $steamApps = [IO.Path]::GetFullPath($SteamRoot)
    return [ordered]@{
        FunctionalSmoke = $true
        ProjectClientStockSignon = $true
        ProjectClientStop = 'live-visual-control'
        ProjectClientLiveInput = 'keyboard-mouse'
        ProjectClientPrediction = $Mode
        ConfirmFunctionalSmoke = 'HLCLIENT_LOCAL_RESEARCH_COPY_SMOKE_V1'
        SteamApiRuntimePath = Join-Path $steamApps 'common\Half-Life\steam_api.dll'
        AppManifestPath = Join-Path $steamApps 'appmanifest_70.acf'
        ResearchHalfLifeRoot = $research
        ClientPath = Join-Path $release 'hlclient.exe'
        HldsPath = Join-Path $research 'hlds.exe'
        CaptureToolPath = Join-Path $release 'hlclient_stock_runtime_capture.exe'
        NetworkIsolationGuardPath = Join-Path $release 'hlclient_stock_runtime_isolation_guard.exe'
        OutputRoot = Join-Path $Root 'manual-artifacts\research-copy-smoke'
        ServerProfileId = 'steam-hlds-10210-no-mode-banner-v1'
        Game = 'valve'
        Map = 'boot_camp'
        ServerPort = 27243
        RelayPort = 27242
        MaximumDurationSeconds = 90
    }
}

function Assert-H2ManualFilesAndContract {
    param([string]$Runner, [System.Collections.IDictionary]$Configuration,
          [switch]$SourceOnly)
    $release = Split-Path -Parent $Configuration.ClientPath
    $scripts = Split-Path -Parent $Runner
    $required = @(
        $Runner, $Configuration.ClientPath,
        $Configuration.CaptureToolPath, $Configuration.NetworkIsolationGuardPath,
        (Join-Path $release 'hlclient_stock_runtime_orchestrator.exe'),
        (Join-Path $release 'hlclient_stock_runtime_check.exe'),
        (Join-Path $release 'SDL3.dll'),
        (Join-Path $scripts 'walk_stock_runtime_transport.ps1'),
        (Join-Path $scripts 'stock_steam_user_config_projection.ps1'),
        (Join-Path $scripts 'stock_external_drift.ps1')
    )
    if (-not $SourceOnly) { $required += @(
        $Configuration.HldsPath, $Configuration.SteamApiRuntimePath,
        $Configuration.AppManifestPath,
        (Join-Path $Configuration.ResearchHalfLifeRoot '.hlclient-research-isolated'),
        (Join-Path $Configuration.ResearchHalfLifeRoot 'valve\maps\boot_camp.bsp')
    ) }
    foreach ($path in $required) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required file is missing: $path. Restore the existing research/Steam installation or build the Release target; then rerun -CheckOnly."
        }
    }
    # Metadata inspection only: Get-Command does not execute the script body.
    $command = Get-Command -Name $Runner -CommandType ExternalScript
    $parameterSet = @($command.ParameterSets | Where-Object Name -EQ 'FunctionalSmoke')
    if ($parameterSet.Count -ne 1) { throw 'Managed runner FunctionalSmoke contract is absent. Restore the current runner.' }
    foreach ($key in $Configuration.Keys) {
        $parameter = @($parameterSet[0].Parameters | Where-Object Name -EQ $key)
        if ($parameter.Count -ne 1) { throw "Managed runner does not support FunctionalSmoke parameter $key." }
        foreach ($attribute in $parameter[0].Attributes) {
            if ($attribute -is [System.Management.Automation.ValidateSetAttribute] -and
                $Configuration[$key] -notin $attribute.ValidValues) {
                throw "Managed runner rejects $key=$($Configuration[$key])."
            }
            if ($attribute -is [System.Management.Automation.ValidateRangeAttribute] -and
                ($Configuration[$key] -lt $attribute.MinRange -or
                 $Configuration[$key] -gt $attribute.MaxRange)) {
                throw "Managed runner range rejects $key=$($Configuration[$key])."
            }
        }
    }
    foreach ($parameter in $parameterSet[0].Parameters) {
        if ($parameter.IsMandatory -and -not $Configuration.Contains($parameter.Name)) {
            throw "Managed runner now requires $($parameter.Name); update this wrapper before launching."
        }
    }
}

function Assert-H2ManualLivePreflight {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        $principal = [Security.Principal.WindowsPrincipal]::new($identity)
        if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
            throw 'Open PowerShell 7 with Run as administrator, then run this command again. No automatic elevation is performed.'
        }
    } finally { $identity.Dispose() }
    # Enumeration only; never bind a probe socket or terminate another owner.
    try {
        $occupied = @(Get-NetUDPEndpoint -ErrorAction Stop |
            Where-Object { $_.LocalPort -in @(27242, 27243) })
    } catch { throw "Cannot check UDP port ownership. Run from administrative PowerShell 7: $($_.Exception.Message)" }
    if ($occupied.Count -ne 0) {
        $owners = ($occupied | ForEach-Object { "$($_.LocalPort) (PID $($_.OwningProcess))" }) -join ', '
        throw "UDP ports are occupied: $owners. Close the owning session yourself and retry; no process was stopped."
    }
}

function Invoke-H2ManualManagedRunner {
    param([string]$PowerShell, [string]$Runner,
          [System.Collections.IDictionary]$Configuration)
    # Native -File arguments, not interpolated source or Invoke-Expression.
    $arguments = [Collections.Generic.List[string]]::new()
    foreach ($value in @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $Runner)) {
        $arguments.Add($value)
    }
    foreach ($key in $Configuration.Keys) {
        $arguments.Add('-' + $key)
        if ($Configuration[$key] -isnot [bool]) { $arguments.Add([string]$Configuration[$key]) }
        elseif (-not $Configuration[$key]) { throw "Unsupported false switch: $key" }
    }
    $runIds = [Collections.Generic.HashSet[string]]::new()
    $result = 'unavailable (runner did not publish a result)'
    # Nonzero native exit is data to propagate, not a PowerShell terminating error.
    $PSNativeCommandUseErrorActionPreference = $false
    $PSNativeCommandArgumentPassing = 'Standard'
    # Native commands update the global automatic variable. A function-local
    # LASTEXITCODE would shadow the fresh value with a stale/null value.
    $global:LASTEXITCODE = $null
    # Foreground child shares the console/Ctrl+C path. Stderr is inherited;
    # stdout is forwarded immediately while reading only the runner's exact ID.
    # No background process, timeout kill, cancellation handler or cleanup override.
    & $PowerShell @arguments | ForEach-Object {
        $line = [string]$_
        Write-Host $line
        if ($line -cmatch '^\[research-copy-smoke\] run_id=([0-9a-f]{32})$') {
            [void]$runIds.Add($Matches[1])
        }
        if ($line -cmatch '^\[research-copy-smoke\] result=(.+)$') { $result = $Matches[1] }
    }
    $runnerExitCode = $global:LASTEXITCODE
    if ($null -eq $runnerExitCode) { throw 'Managed PowerShell did not return an exit code.' }
    Write-Host "Managed result: $result; exit code: $runnerExitCode"
    if ($runIds.Count -eq 1) {
        $runId = @($runIds)[0]
        $report = Join-Path (Join-Path $Configuration.OutputRoot $runId) 'functional-smoke-wrapper.json'
        if (Test-Path -LiteralPath $report -PathType Leaf) { Write-Host "Run report: $report" }
        else { Write-Host "Run $runId did not publish a wrapper report: $report" }
    } else {
        Write-Host 'Run report unavailable: no unambiguous run ID from this invocation. No latest-folder guess was made.'
    }
    return [int]$runnerExitCode
}

if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Use PowerShell 7 (pwsh), not Windows PowerShell 5.1.' }
if (-not $IsWindows) { throw 'This managed launcher requires Windows and PowerShell 7.' }
if ($SourceOnly -and -not $CheckOnly) { throw 'SourceOnly requires CheckOnly; it cannot bypass live prerequisites.' }
$powerShell = Join-Path $PSHOME 'pwsh.exe'
$runner = Join-Path $PSScriptRoot 'scripts\capture_stock_runtime_state.ps1'
$configuration = Get-H2ManualConfiguration -Root $PSScriptRoot -Mode $Prediction -ResearchRoot $ResearchHalfLifeRoot -SteamRoot $SteamAppsRoot
Assert-H2ManualFilesAndContract -Runner $runner -Configuration $configuration -SourceOnly:$SourceOnly
Write-Host "HL Client Engine: prediction=$Prediction; input=keyboard-mouse; renderer=managed OpenGL; limit=90 seconds."
if ($CheckOnly) {
    Write-Host 'CheckOnly passed: files and FunctionalSmoke parameters validated. No runner, game, Steam API, WFP or sockets started.'
    Write-Host 'Administrator rights and current UDP ownership will be checked only for an actual launch.'
    if ($SourceOnly) { Write-Host 'External game/Steam files: NOT CHECKED (source-only build validation, not live readiness).' }
    return
}
Assert-H2ManualLivePreflight
Write-Host 'Click: capture mouse | WASD: move | mouse: look | Shift: slow walk'
Write-Host 'Escape: release mouse | close game window: finish the managed session'
Write-Host 'Space/Ctrl may switch prediction to server-sample fallback. Manual smoothness is not yet validated.'
Write-Host 'Wait for the managed runner to finish cleanup/restoration before closing this console.'
$exitCode = Invoke-H2ManualManagedRunner -PowerShell $powerShell -Runner $runner -Configuration $configuration
exit $exitCode
