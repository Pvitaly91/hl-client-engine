#requires -Version 7
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$Label,
 [int[]]$Actors=@(4,8,16),[int[]]$Histories=@(2,16),[int]$Diagnostics=0,[int]$Profile=1,[int]$Fixed=1)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$task=Join-Path $PSScriptRoot '../manual-artifacts/task-records/cpu-history-projection-50b5ffe1-20261024'
. (Join-Path $task 'quality-common.ps1')
. (Join-Path $task '../win10-msaa4-50b5ffe1-20261024/continuation-host-guard.ps1')
$exe=(Resolve-Path -LiteralPath $Executable).Path
$result=New-QualityAttempt $Label
$guard=Start-ContinuationGuard
try {
 Assert-ContinuationIdle $guard
 Copy-Item -LiteralPath (Join-Path $task 'baseline/Release-binaries/SDL3.dll') -Destination (Join-Path (Split-Path $exe) 'SDL3.dll')
 foreach($actor in $Actors) {foreach($history in $Histories) {
  Assert-ContinuationIdle $guard
  $env:HLC_COST_ACTORS="$actor";$env:HLC_COST_HISTORY="$history";$env:HLC_COST_DIAG="$Diagnostics"
  $env:HLC_COST_PROFILE="$Profile";$env:HLC_COST_FIXED="$Fixed"
  $name="a$actor-h$history-d$Diagnostics-p$Profile-f$Fixed"
  $out=Join-Path $result ($name+'.stdout.log');$err=Join-Path $result ($name+'.stderr.log')
  $args=@('[history-projection-cost]','--reporter','console','--durations','no')
  $p=Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $script:QualityRepository -WindowStyle Hidden -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
  $guard.own_pid=$p.Id
  while(!$p.HasExited){Read-ContinuationEvents $guard -ProbeName ($p.ProcessName+'.exe');[Threading.Thread]::Sleep(30)}
  $p.WaitForExit();$guard.own_pid=0
  $record=[ordered]@{name=$name;exit=$p.ExitCode;exe_sha256=Get-QualityHash $exe;stdout_sha256=Get-QualityHash $out;stderr_sha256=Get-QualityHash $err;argv=$args}
  $record|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $result ($name+'.result.json'))
  if($p.ExitCode -ne 0){throw "Fixture failed: $name"}
  Assert-ContinuationIdle $guard
 }}
} finally {
 Stop-ContinuationGuard $guard
 $guard|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $result 'host-guard.json')
 Write-Output $result
}
