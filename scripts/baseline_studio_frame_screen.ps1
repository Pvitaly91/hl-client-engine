#requires -Version 7
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$task=Join-Path $PSScriptRoot '../manual-artifacts/task-records/studio-pose-frame-build-50b5ffe1-20261024'
$plan=Join-Path $task 'screen-plan.json';if(Test-Path $plan){throw 'Preserve existing screen plan'}
$b0=Join-Path $task '../cpu-history-projection-50b5ffe1-20261024/isolated-fixtures/B0/hp_cpu_b0/bin/hp_cpu_b0.exe'
if((Get-FileHash $b0).Hash -ne 'B2633A250090C373561BA3E79D4AECCB80ADF399B96926ED6EC6F805EDBFD3A1'){throw 'B0 identity mismatch'}
[ordered]@{schema='hlclient.studio-frame.screen.v1';candidate='Exact FNV uint32 zero-padding fold; no allocation/model/animation changes';actors=16;history=16;diagnostics=0;baseline_repeats=3;screen_pairs=2;warmup=64;samples=256;
 gain_gate='max(30us,B0 p50 span,5% B0 median)+10us, baseline-only; both present/parent must beat gate in both screening cases in paired median; screening is not final acceptance';
 tail_gate='recurring >=2/3 paired p50/p95/p99 regressions beyond own max(50us,5% B0 median,B0 quantile span) blocks final acceptance; raw max retained';
 memory_gate='No added allocation calls/bytes, no new retained cache/pose fields; cold and lifetime controls retained';
 final_controls='3 interleaved pairs, reversed order,4/2 +8/16 +16/16 noRX and16/16 RX every4 renders; Diagnostics Off/On; same gates frozen here; no pooling screening with final campaign';
 full_FPS='not measured by CPU subset';live_sessions=0}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $plan
$runs=[Collections.Generic.List[object]]::new()
foreach($fixed in @(1,0)){for($r=0;$r -lt 3;++$r){
 $path=& (Join-Path $PSScriptRoot 'run_studio_frame_fixture.ps1') -Executable $b0 -Label "screen-baseline-f$fixed-r$r" -Fixed $fixed
 $runs.Add(@{fixed=$fixed;repeat=$r;path=$path})
 $runs|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $task 'screen-baseline-runs.json')
}}
$gates=[Collections.Generic.List[object]]::new()
foreach($fixed in @(1,0)){foreach($scope in @('present','parent_visual_update_subset')){
 $values=@(foreach($run in $runs|Where-Object fixed -EQ $fixed){foreach($line in Get-Content (Join-Path ([string]$run.path) 'driver.stdout.log')){if($line -like '[[]history-cost[]]*'){$row=$line.Substring($line.IndexOf('{'))|ConvertFrom-Json;if($row.scope -eq $scope){[double]$row.p50_us}}}})
 if($values.Count -ne 3){throw 'Expected three baseline p50 values'}
 $sorted=@($values|Sort-Object);$span=$sorted[-1]-$sorted[0]
 $gate=[Math]::Max(30.0,[Math]::Max($span,$sorted[1]*.05))+10.0
 $gates.Add(@{fixed=$fixed;scope=$scope;baseline_p50_us=$values;median_us=$sorted[1];span_us=$span;gate_us=$gate})
}}
$gates|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $task 'screen-baseline-gates.json')
$gates|Format-Table
