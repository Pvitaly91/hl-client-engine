#requires -Version 7
param([switch]$AA)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$task=Join-Path $PSScriptRoot '../manual-artifacts/task-records/studio-rx-tail-causal-50b5ffe1-20261025'
$old=Join-Path $task '../studio-pose-frame-build-50b5ffe1-20261024'
$progress=if($AA){Join-Path $task 'aa-progress.json'}else{Join-Path $old 'controlled-cpu-progress.json'}
$runs=@(Get-Content -LiteralPath $progress -Raw|ConvertFrom-Json)
if($AA -and $runs.Count -ne 6){throw 'A/A is incomplete'}
$baseline=if($AA){'A0'}else{'B0'};$candidate=if($AA){'A1'}else{'B1'}
$rows=[Collections.Generic.List[object]]::new();$memory=[Collections.Generic.List[object]]::new()
foreach($r in $runs){
 if(!$AA -and ($r.actors -ne 16 -or $r.history -ne 16 -or $r.fixed -ne 0 -or $r.diagnostics -ne 1)){continue}
 $result=Get-Content -LiteralPath (Join-Path $r.path 'result.json') -Raw|ConvertFrom-Json
 $guard=Get-Content -LiteralPath (Join-Path $r.path 'host-guard.json') -Raw|ConvertFrom-Json
 if($result.exit_code -ne 0 -or $guard.busy_events.Count){throw 'Failed/contaminated record'}
 if($AA -and $result.executable_sha256 -cne '773684acb04817238f282b1e9bacef6934c3f5fb9940afc8c7f12f7e754049e3'){throw 'A/A hash changed'}
 foreach($line in Get-Content -LiteralPath (Join-Path $r.path 'driver.stdout.log')){
  if($line -like '[[]history-operations[]]*'){$m=$line.Substring($line.IndexOf('{'))|ConvertFrom-Json;$memory.Add(@{variant=$r.variant;pair=$r.pair;values=$m})}
  if($line -notlike '[[]history-cost[]]*'){continue}
  $v=$line.Substring($line.IndexOf('{'))|ConvertFrom-Json
  if($v.actual_models -ne 1 -or $v.profile -ne 0 -or $v.actors -ne 16 -or $v.history -ne 16 -or $v.fixed_history -ne 0 -or $v.diagnostics -ne 1){throw 'Workload/instrumentation does not match original RX On case'}
  $sorted=@($v.raw_us|Sort-Object);if($sorted.Count -ne $v.samples){throw 'Raw count mismatch'}
  foreach($q in @(50,95,99)){
   $expected=[double]$sorted[[int][Math]::Floor(($sorted.Count-1)*$q/100)]
   if($expected -ne $v."p${q}_us"){throw 'Serialized quantile does not match raw lower-order statistic'}
  }
  if($v.scope -in @('commit_new_rx','present_new_rx','parent_new_rx','parent_no_rx')){
   if(($v.scope -eq 'parent_no_rx' -and $v.samples -ne 192) -or ($v.scope -ne 'parent_no_rx' -and $v.samples -ne 64)){throw 'Unexpected new-RX/no-RX count'}
   $rows.Add(@{variant=$r.variant;pair=$r.pair;scope=$v.scope;count=$sorted.Count;metrics=@{p50=$v.p50_us;p95=$v.p95_us;p99=$v.p99_us;max=$v.max_us};signature=$v.signature;no_rx_pose_changes=$v.no_rx_pose_changes;source=$r.path;raw=$v.raw_us})
  }
 }
}
function Median($x){$v=@($x|Sort-Object);[double]$v[[int][Math]::Floor(($v.Count-1)/2)]}
$comparisons=[Collections.Generic.List[object]]::new();$gates=[Collections.Generic.List[object]]::new()
foreach($scope in @('commit_new_rx','present_new_rx','parent_new_rx','parent_no_rx')){
 $b=@($rows|Where-Object {$_.scope -eq $scope -and $_.variant -eq $baseline}|Sort-Object pair)
 $c=@($rows|Where-Object {$_.scope -eq $scope -and $_.variant -eq $candidate}|Sort-Object pair)
 if($b.Count -ne 3 -or $c.Count -ne 3){throw 'Expected three pairs per scope'}
 for($i=0;$i -lt 3;++$i){if($b[$i].signature -cne $c[$i].signature -or $b[$i].no_rx_pose_changes -ne $c[$i].no_rx_pose_changes){throw 'Observable mismatch'}}
 foreach($metric in @('p50','p95','p99','max')){
  $bv=@($b|ForEach-Object{[double]$_.metrics[$metric]});$span=($bv|Measure-Object -Maximum).Maximum-($bv|Measure-Object -Minimum).Minimum
  $gate=[Math]::Max(50,[Math]::Max((Median $bv)*.05,$span));$bad=0
  for($i=0;$i -lt 3;++$i){
   $v=[double]$c[$i].metrics[$metric];$delta=$v-$bv[$i];if($delta -gt $gate){++$bad}
   $comparisons.Add(@{scope=$scope;pair=$i;metric=$metric;baseline=$baseline;candidate=$candidate;baseline_us=$bv[$i];candidate_us=$v;delta_us=$delta;delta_percent=100*$delta/$bv[$i];gate_us=$gate;over_gate=($delta -gt $gate);valid_count=$b[$i].count})
  }
  $gates.Add(@{scope=$scope;metric=$metric;gate_us=$gate;baseline_span_us=$span;regression_pairs=$bad;reject=($metric -ne 'max' -and $bad -ge 2)})
 }
}
$name=if($AA){'aa-analysis.json'}else{'historical-rx-analysis.json'}
[ordered]@{schema='hlclient.rx-tail.exact-record-analysis.v1';kind=$(if($AA){'identical_executable_AA'}else{'retained_original_B0_B1_rejection'});runs=$rows;comparisons=$comparisons;gates=$gates;memory=$memory;reject=(@($gates|Where-Object reject).Count -gt 0);sample_independence='Adjacent observations correlated; three process pairs, not 192 independent runs';prior_rejection_preserved=$true}|ConvertTo-Json -Depth 20|Set-Content -LiteralPath (Join-Path $task $name)
$comparisons|ForEach-Object{[pscustomobject]$_}|Where-Object scope -EQ commit_new_rx|Sort-Object pair,metric|Format-Table pair,metric,baseline_us,candidate_us,delta_us,delta_percent,gate_us,valid_count
$gates|Where-Object reject|ConvertTo-Json -Depth 5
