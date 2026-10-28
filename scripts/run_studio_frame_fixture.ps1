#requires -Version 7
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$Label,
 [int]$Actors=16,[int]$History=16,[int]$Diagnostics=0,[int]$Fixed=1,[int]$DetailTimers=0,[int]$Stacks=0,
 [string]$Filter='[history-projection-cost]')
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$task=Join-Path $PSScriptRoot '../manual-artifacts/task-records/studio-pose-frame-build-50b5ffe1-20261024'
. (Join-Path $task 'quality-common.ps1')
. (Join-Path $task '../win10-msaa4-50b5ffe1-20261024/continuation-host-guard.ps1')
$exe=(Resolve-Path -LiteralPath $Executable).Path;$root=New-QualityAttempt $Label;$guard=Start-ContinuationGuard
try {
 Assert-ContinuationIdle $guard
 $env:HLC_COST_ACTORS="$Actors";$env:HLC_COST_HISTORY="$History";$env:HLC_COST_DIAG="$Diagnostics";$env:HLC_COST_PROFILE='0';$env:HLC_COST_FIXED="$Fixed"
 $env:HLC_DETAIL_TIMERS="$DetailTimers";$env:HLC_DETAIL_STACKS="$Stacks"
 $out=Join-Path $root 'driver.stdout.log';$err=Join-Path $root 'driver.stderr.log'
 $p=Start-Process -FilePath $exe -ArgumentList @($Filter,'--reporter','console') -WorkingDirectory $script:QualityRepository -WindowStyle Hidden -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
 $guard.own_pid=$p.Id
 while(!$p.HasExited){Read-ContinuationEvents $guard -ProbeName ($p.ProcessName+'.exe');[Threading.Thread]::Sleep(25)}
 $p.WaitForExit();$guard.own_pid=0
 $record=[ordered]@{status=$(if($p.ExitCode -eq 0){'passed'}else{'failed'});exit_code=$p.ExitCode;executable=$exe;executable_sha256=Get-QualityHash $exe;stdout_sha256=Get-QualityHash $out;stderr_sha256=Get-QualityHash $err;actors=$Actors;history=$History;diagnostics=$Diagnostics;fixed=$Fixed;detail_timers=$DetailTimers;allocation_stacks=$Stacks;filter=$Filter}
 $record|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $root 'result.json')
 if($p.ExitCode -ne 0){throw 'Fixture failed; attempt retained'}
 Assert-ContinuationIdle $guard
} finally {Stop-ContinuationGuard $guard;$guard|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $root 'host-guard.json');Write-Output $root}
