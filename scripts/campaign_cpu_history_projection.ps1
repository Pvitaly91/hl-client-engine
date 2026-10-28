#requires -Version 7
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$task=Join-Path $PSScriptRoot '../manual-artifacts/task-records/cpu-history-projection-50b5ffe1-20261024'
$planPath=Join-Path $task 'cpu-campaign-plan.json'
if(Test-Path -LiteralPath $planPath){throw 'Campaign already exists; retain it'}
$plan=[ordered]@{schema='hlclient.cpu-history.campaign.v1';repeats=3;samples=256;warmup=64;
 primary='present and parent_visual_update_subset';primary_scope_limit='Excludes packet parsing, prediction, render and swap; not full FPS';
 gate='Median paired p50 gain must exceed max(20us,B0 p50 span)+5us in expensive steady case, at least 2/3 positive. No recurring >=2/3 p50/p95/p99 regressions beyond own max(50us,5% B0 median,B0 quantile span); allocation/bytes and exact output signatures must not regress. Small/RX/diagnostics controls retained. No timing retries without independent cause.';
 candidate='Exact bit-equal rotation endpoint conversion reuse; every current-time pose/slerp still evaluated';
 cases=@(@{actors=4;history=2;fixed=1},@{actors=16;history=16;fixed=1},@{actors=16;history=16;fixed=0});live_sessions=0;builds_during_timing=0}
$plan|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $planPath
$paths=[Collections.Generic.List[object]]::new()
foreach($case in $plan.cases) {foreach($diag in @(0,1)) {for($pair=0;$pair -lt 3;++$pair) {
 $order=if($pair%2 -eq 0){@('B0','B1')}else{@('B1','B0')}
 foreach($variant in $order) {
  $name='hp_cpu_'+$variant.ToLower()
  $exe=Join-Path $task "isolated-fixtures/$variant/$name/bin/$name.exe"
  $label="cpu-$variant-a$($case.actors)-h$($case.history)-f$($case.fixed)-d$diag-pair$pair"
  $path=& (Join-Path $PSScriptRoot 'run_cpu_history_projection.ps1') -Executable $exe -Label $label -Actors @($case.actors) -Histories @($case.history) -Diagnostics $diag -Profile 0 -Fixed $case.fixed
  $paths.Add(@{variant=$variant;actors=$case.actors;history=$case.history;fixed=$case.fixed;diagnostics=$diag;pair=$pair;path=$path})
  $paths|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $task 'cpu-campaign-progress.json')
 }
}}}
'CPU campaign complete'
