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
    [ValidateSet('Fast', 'Strict')]
    [string]$ValidationMode = 'Fast',
    [ValidateSet('manual', 'damage-respawn-check')]
    [string]$Scenario = 'manual',
    [ValidateSet('crossfire', 'boot_camp', 'stalkyard')]
    [string]$Map = 'crossfire',
    [string]$ExternalMapBsp,
    [ValidateSet(50)]
    [int]$TestStartHealth = 0,
    [switch]$MuteGlockFireSound,
    [switch]$RemoteAudioPeer,
    [ValidateRange(1, 86400)]
    [int]$DurationSeconds = 45,
    [switch]$NoTimeLimit,
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
          [string]$SteamRoot = 'D:\Steam\steamapps',
          [ValidateSet('manual', 'damage-respawn-check')]
          [string]$SelectedScenario = 'manual',
          [string]$SelectedMap = 'crossfire',
          [string]$SelectedExternalMapBsp,
          [ValidateSet(0, 50)][int]$SelectedTestStartHealth = 0,
          [switch]$SelectedMuteGlockFireSound,
          [switch]$SelectedRemoteAudioPeer,
          [Nullable[int]]$SelectedDurationSeconds,
          [switch]$SelectedNoTimeLimit,
          [ValidateSet('Fast', 'Strict')][string]$SelectedValidationMode = 'Fast')
    if ($SelectedNoTimeLimit -and $null -ne $SelectedDurationSeconds) {
        throw 'DurationSeconds and NoTimeLimit are mutually exclusive.'
    }
    if (($SelectedNoTimeLimit -or $null -ne $SelectedDurationSeconds) -and $SelectedScenario -cne 'manual') {
        throw 'DurationSeconds/NoTimeLimit require Scenario manual; scripted checks retain their existing bounds.'
    }
    if ($null -ne $SelectedDurationSeconds -and ($SelectedDurationSeconds -lt 1 -or $SelectedDurationSeconds -gt 86400)) {
        throw 'DurationSeconds must be in range 1..86400.'
    }
    if (-not [string]::IsNullOrEmpty($SelectedExternalMapBsp)) {
        if ($SelectedScenario -cne 'manual' -or $SelectedValidationMode -cne 'Fast' -or
            $SelectedTestStartHealth -ne 0 -or $SelectedMap -cne 'crossfire') {
            throw 'ExternalMapBsp requires Scenario manual, ValidationMode Fast, no TestStartHealth and the unchanged default Map.'
        }
        $selected = Resolve-ExternalManualMapBsp $ResearchRoot $SelectedExternalMapBsp $Root
        $SelectedMap = $selected.Map
    } elseif ($SelectedMap -cnotin @('crossfire', 'boot_camp', 'stalkyard')) {
        throw 'Invalid Map: expected crossfire, boot_camp or stalkyard.'
    }
    if ($SelectedTestStartHealth -eq 50 -and ($SelectedScenario -cne 'manual' -or
        $SelectedMap -cne 'crossfire' -or $Mode -cne 'reference')) {
        throw 'TestStartHealth requires crossfire, manual keyboard-mouse and Prediction reference.'
    }
    if ($SelectedRemoteAudioPeer -and ($SelectedScenario -cne 'manual' -or
        $SelectedMap -cne 'crossfire' -or $SelectedTestStartHealth -ne 0 -or $SelectedExternalMapBsp)) {
        throw 'RemoteAudioPeer requires default crossfire, manual input and no health/external-map profile.'
    }
    $release = Join-Path $Root 'build\bin\Release'
    $research = [IO.Path]::GetFullPath($ResearchRoot)
    $steamApps = [IO.Path]::GetFullPath($SteamRoot)
    $configuration = [ordered]@{
        FunctionalSmoke = $true
        ValidationMode = $SelectedValidationMode
        ProjectClientStockSignon = $true
        ProjectClientStop = 'live-visual-control'
        ProjectClientLiveInput = $(if ($SelectedScenario -eq 'damage-respawn-check') {
            'scripted-damage-respawn-check'
        } else { 'keyboard-mouse' })
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
        Map = $(if ($SelectedExternalMapBsp) { $SelectedMap } else { $SelectedMap.ToLowerInvariant() })
        ServerPort = 27243
        RelayPort = 27242
        MaximumDurationSeconds = 90
    }
    if ($SelectedExternalMapBsp) { $configuration.ExternalManualMap = $true }
    if ($SelectedRemoteAudioPeer) { $configuration.RemoteAudioPeer = $true }
    if ($SelectedScenario -ceq 'manual') {
        if ($SelectedNoTimeLimit) { $configuration.ProjectClientNoTimeLimit = $true }
        else { $configuration.ProjectClientDurationSeconds = $(if ($null -ne $SelectedDurationSeconds) { $SelectedDurationSeconds } else { 45 }) }
    }
    if ($SelectedTestStartHealth -eq 50) { $configuration.TestStartHealth = 50 }
    if ($SelectedMuteGlockFireSound) {
        if ($SelectedScenario -cne 'manual') {
            throw 'MuteGlockFireSound requires the manual keyboard-mouse scenario.'
        }
        $configuration.ProjectClientMuteGlockFireSound = $true
    }
    return $configuration
}

function Assert-H2ManualFilesAndContract {
    param([string]$Runner, [System.Collections.IDictionary]$Configuration,
          [switch]$SourceOnly)
    if ($Configuration.Game -cne 'valve' -or
        ($Configuration.Map -cnotin @('crossfire', 'boot_camp', 'stalkyard') -and
         -not $Configuration.Contains('ExternalManualMap'))) {
        throw 'Invalid Map: expected an allowed virtual map basename for valve.'
    }
    $release = Split-Path -Parent $Configuration.ClientPath
    $scripts = Split-Path -Parent $Runner
    if ($Configuration.Contains('TestStartHealth')) {
        Import-Module (Join-Path $scripts 'test_start_health_profile.psm1') -Force
        [void](Get-TestStartHealthComponents (Split-Path -Parent $scripts) -PathsOnly:($Configuration.ValidationMode -eq 'Fast'))
    }
    $required = @(
        $Runner, $Configuration.ClientPath,
        $Configuration.CaptureToolPath, $Configuration.NetworkIsolationGuardPath,
        (Join-Path $release 'hlclient_stock_runtime_orchestrator.exe'),
        (Join-Path $release 'hlclient_stock_runtime_check.exe'),
        (Join-Path $release 'SDL3.dll'),
        (Join-Path $scripts 'walk_stock_runtime_transport.ps1'),
        (Join-Path $scripts 'stock_steam_user_config_projection.ps1'),
        (Join-Path $scripts 'stock_external_drift.ps1')
        (Join-Path $scripts 'stock_manual_fast.ps1')
    )
    if (-not $SourceOnly) { $required += @(
        $Configuration.HldsPath, $Configuration.SteamApiRuntimePath,
        $Configuration.AppManifestPath,
        (Join-Path $Configuration.ResearchHalfLifeRoot '.hlclient-research-isolated'),
        (Join-Path $Configuration.ResearchHalfLifeRoot ("valve\maps\{0}.bsp" -f $Configuration.Map))
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
    $reportPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
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
        if ($line -cmatch '^\[research-copy-smoke\] report_path=(.+)$') {
            [void]$reportPaths.Add($Matches[1])
        }
    }
    $runnerExitCode = $global:LASTEXITCODE
    if ($null -eq $runnerExitCode) { throw 'Managed PowerShell did not return an exit code.' }
    Write-Host "Managed result: $result; exit code: $runnerExitCode"
    if ($runIds.Count -eq 1) {
        $runId = @($runIds)[0]
        $runDirectory = Join-Path $Configuration.OutputRoot $runId
        $allowed = @((Join-Path $runDirectory 'functional-smoke-wrapper.json'),
            (Join-Path $runDirectory 'functional-smoke-wrapper.incomplete.json'))
        $report = if ($reportPaths.Count -eq 1) { @($reportPaths)[0] } else { $null }
        if ($null -ne $report -and $allowed -icontains $report -and
            (Test-Path -LiteralPath $report -PathType Leaf)) { Write-Host "Run report: $report" }
        else { Write-Host "Run $runId did not publish an unambiguous transaction-bound wrapper report." }
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
. (Join-Path $PSScriptRoot 'scripts\external_manual_map.ps1')
if ($PSBoundParameters.ContainsKey('ExternalMapBsp') -and [string]::IsNullOrWhiteSpace($ExternalMapBsp)) {
    throw 'external_map_not_supplied: ExternalMapBsp requires an explicit BSP path.'
}
$timingArguments = @{}
if ($PSBoundParameters.ContainsKey('DurationSeconds')) { $timingArguments.SelectedDurationSeconds = $DurationSeconds }
$configuration = Get-H2ManualConfiguration -Root $PSScriptRoot -Mode $Prediction -ResearchRoot $ResearchHalfLifeRoot -SteamRoot $SteamAppsRoot -SelectedScenario $Scenario -SelectedMap $Map -SelectedExternalMapBsp $ExternalMapBsp -SelectedTestStartHealth $TestStartHealth -SelectedMuteGlockFireSound:$MuteGlockFireSound -SelectedRemoteAudioPeer:$RemoteAudioPeer -SelectedNoTimeLimit:$NoTimeLimit -SelectedValidationMode $ValidationMode @timingArguments
if ($configuration.Contains('ExternalManualMap') -and $SourceOnly) {
    throw 'external_map_not_supplied: SourceOnly cannot validate external map assets.'
}
Assert-H2ManualFilesAndContract -Runner $runner -Configuration $configuration -SourceOnly:$SourceOnly
if ($configuration.Contains('ExternalManualMap')) {
    Invoke-ExternalManualMapPreflight $PSScriptRoot $configuration $ExternalMapBsp
}
Write-Host "HL Client Engine: map=$($configuration.Map); prediction=$Prediction; input=$($configuration.ProjectClientLiveInput); renderer=managed OpenGL."
if ($Scenario -eq 'manual') {
    if ($NoTimeLimit) { Write-Host 'Game duration: no time limit; close the client window to finish. Startup and cleanup retain bounded deadlines.' }
    else { Write-Host "Game duration limit: $($configuration.ProjectClientDurationSeconds) seconds; startup and cleanup have separate bounded deadlines." }
} else { Write-Host 'Scripted check: existing managed budget=90 seconds (unchanged).' }
Write-Host "Validation: $ValidationMode. $(if ($ValidationMode -eq 'Fast') {'Trusted prepared installation; managed-file restoration only, not full content attestation.'} else {'Full research inventory, backup and restoration.'})"
if ($TestStartHealth -eq 50) {
    Write-Host 'Optional profile=test-server-assisted; requested_start_health=50. Prepared components present; runner verifies bytes before use. Server application/client health are not yet observed.'
}
if ($MuteGlockFireSound) {
    Write-Host 'Audio diagnostic: local Glock fire sound muted; impact, casing, other weapon and server sounds remain enabled.'
}
if ($CheckOnly) {
    if ($RemoteAudioPeer -and -not $SourceOnly) {
        & (Join-Path $PSScriptRoot 'build\bin\Release\hlclient_stock_runtime_orchestrator.exe') --validate-remote-audio-peer-contract
        if ($LASTEXITCODE -ne 0) { throw 'RemoteAudioPeer native dry-run failed.' }
    }
    $scenariosToCheck = if ($configuration.Contains('ExternalManualMap')) { @('manual') } else { @('manual', 'damage-respawn-check') }
    foreach ($checkedScenario in $scenariosToCheck) {
        $checkedConfiguration = Get-H2ManualConfiguration -Root $PSScriptRoot -Mode $Prediction -ResearchRoot $ResearchHalfLifeRoot -SteamRoot $SteamAppsRoot -SelectedScenario $checkedScenario -SelectedMap $Map -SelectedExternalMapBsp $ExternalMapBsp -SelectedValidationMode $ValidationMode
        Assert-H2ManualFilesAndContract -Runner $runner -Configuration $checkedConfiguration -SourceOnly:$SourceOnly
    }
    Write-Host 'CheckOnly passed: files and FunctionalSmoke parameters validated. No runner, game, Steam API, WFP or sockets started.'
    Write-Host 'Administrator rights and current UDP ownership will be checked only for an actual launch.'
    if ($SourceOnly) { Write-Host 'External game/Steam files: NOT CHECKED (source-only build validation, not live readiness).' }
    return
}
if ($RemoteAudioPeer) {
    Write-Host 'Two clients: A=HLC_E9_A; B=HLC_E9_B. Audio follows the focused window; both clients have normal output. Same owned HLDS.'
    Write-Host 'Select B to move; A remains listener. Closing A ends both owned clients. Two-player authentication and listening: pending manual validation.'
}
Assert-H2ManualLivePreflight
if ($TestStartHealth -eq 50) {
    Write-Host 'Test-server-assisted profile: requested start HP=50, max HP=100. Ready only after fresh server confirmation; first spawn only.'
}
if ($Scenario -eq 'damage-respawn-check') {
    Write-Host 'C scripted check: one own-player self-kill, release/normal respawn input, new-life movement and weapon bindings.'
    Write-Host 'No combat damage/armor absorption proof; no automatic second session. C budget is tracked separately.'
}
Write-Host 'Click: capture mouse | WASD: move | mouse: look | Shift: slow walk'
Write-Host 'Space: jump | Ctrl: crouch | 1-5 / wheel: select an owned weapon'
Write-Host 'E: use | hold E: use a health or armor station (server decides availability)'
Write-Host 'Escape: release mouse | close game window: finish the managed session'
Write-Host 'First click: capture mouse  LMB after capture: primary attack  R: reload'
Write-Host 'Weapon model, server ammo and basic HUD are enabled; secondary attack is not enabled.'
Write-Host 'Wait for the managed runner to finish cleanup/restoration before closing this console.'
$exitCode = Invoke-H2ManualManagedRunner -PowerShell $powerShell -Runner $runner -Configuration $configuration
exit $exitCode
