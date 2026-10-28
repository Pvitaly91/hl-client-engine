#requires -Version 7
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$task=Join-Path $PSScriptRoot '../manual-artifacts/task-records/cpu-history-projection-50b5ffe1-20261024'
$runs=@(Get-Content -LiteralPath (Join-Path $task 'cpu-campaign-progress.json') -Raw|ConvertFrom-Json)
if($runs.Count -ne 36){throw "Expected 36 planned processes, got $($runs.Count)"}
function Median($v){$s=@($v|Sort-Object);return [double]$s[[int][Math]::Floor(($s.Count-1)/2)]}
$rows=[Collections.Generic.List[object]]::new();$ops=[Collections.Generic.List[object]]::new()
foreach($run in $runs) {
 $root=[string]$run.path
 $guard=Get-Content -LiteralPath (Join-Path $root 'host-guard.json') -Raw|ConvertFrom-Json
 if($guard.busy_events.Count -ne 0){throw 'Contaminated timed attempt'}
 $result=Get-Content -LiteralPath (Get-ChildItem $root -Filter '*.result.json'|Select-Object -First 1).FullName -Raw|ConvertFrom-Json
 if($result.exit -ne 0){throw 'Failed fixture'}
 foreach($line in Get-Content -LiteralPath (Get-ChildItem $root -Filter '*.stdout.log'|Select-Object -First 1).FullName) {
  if($line -like '[[]history-cost[]]*') {
   $r=$line.Substring($line.IndexOf('{'))|ConvertFrom-Json
   if($r.scope -notin @('commit','present','local_host_viewmodel','diagnostic_collection','parent_visual_update_subset',
      'commit_new_rx','present_new_rx','parent_new_rx','parent_no_rx')){continue}
   $rows.Add([pscustomobject]@{variant=$run.variant;pair=$run.pair;actors=$r.actors;history=$r.history;fixed=$r.fixed_history;diag=$r.diagnostics;scope=$r.scope;p50=$r.p50_us;p95=$r.p95_us;p99=$r.p99_us;max=$r.max_us;signature=$r.signature;no_rx_pose_changes=$r.no_rx_pose_changes})
  } elseif($line -like '[[]history-operations[]]*') {
   $r=$line.Substring($line.IndexOf('{'))|ConvertFrom-Json
   $ops.Add([pscustomobject]@{variant=$run.variant;pair=$run.pair;actors=$run.actors;history=$run.history;fixed=$run.fixed;diag=$run.diagnostics;commit_calls=$r.allocations[0].calls;commit_bytes=$r.allocations[0].bytes;present_calls=$r.allocations[1].calls;present_bytes=$r.allocations[1].bytes;new_rx_frames=$r.new_rx_frames;matrix_capacity=$r.matrix_capacity_high_water;capture_bytes=$r.capture_bytes})
  }
 }
}
$summary=[Collections.Generic.List[object]]::new();$bad=[Collections.Generic.List[object]]::new()
foreach($group in $rows|Group-Object actors,history,fixed,diag,scope) {
 $g=@($group.Group);$b=@($g|Where-Object variant -EQ B0|Sort-Object pair);$c=@($g|Where-Object variant -EQ B1|Sort-Object pair)
 if($b.Count -ne 3 -or $c.Count -ne 3){throw 'Unpaired data'}
 for($i=0;$i -lt 3;++$i){if($b[$i].signature -cne $c[$i].signature -or $b[$i].no_rx_pose_changes -ne $c[$i].no_rx_pose_changes){throw 'Exact output/time witness mismatch'}}
 $q=[ordered]@{actors=$b[0].actors;history=$b[0].history;fixed=$b[0].fixed;diag=$b[0].diag;scope=$b[0].scope}
 foreach($metric in @('p50','p95','p99','max')) {
  $bv=@($b|ForEach-Object{[double]$_.$metric});$cv=@($c|ForEach-Object{[double]$_.$metric})
  $gain=@(for($i=0;$i -lt 3;++$i){$bv[$i]-$cv[$i]})
  $span=($bv|Measure-Object -Maximum).Maximum-($bv|Measure-Object -Minimum).Minimum
  $gate=[Math]::Max(50,[Math]::Max((Median $bv)*.05,$span))
  $flags=@($gain|Where-Object {$_ -lt -$gate}).Count
  $q[$metric]=@{B0_median_us=Median $bv;B1_median_us=Median $cv;paired_gain_us=Median $gain;B0_span_us=$span;gain_values=$gain;regression_gate_us=$gate;flags=$flags}
  if($metric -ne 'max' -and $flags -ge 2){$bad.Add(@{case=$group.Name;metric=$metric;flags=$flags})}
 }
 $summary.Add($q)
}
foreach($g in $ops|Group-Object actors,history,fixed,diag,pair) {
 $b=@($g.Group|Where-Object variant -EQ B0)[0];$c=@($g.Group|Where-Object variant -EQ B1)[0]
 foreach($key in @('commit_calls','commit_bytes','present_calls','present_bytes','new_rx_frames','matrix_capacity','capture_bytes')) {
  if($c.$key -gt $b.$key){$bad.Add(@{case=$g.Name;metric=$key;B0=$b.$key;B1=$c.$key})}
 }
}
$primary=@($summary|Where-Object { $_.actors -eq 16 -and $_.fixed -eq 1 -and $_.diag -eq 0 -and $_.scope -in @('present','parent_visual_update_subset') })
$gainPassed=$primary.Count -eq 2
foreach($p in $primary){$gate=[Math]::Max(20,$p.p50.B0_span_us)+5;$p['primary_gate_us']=$gate;if($p.p50.paired_gain_us -le $gate -or @($p.p50.gain_values|Where-Object {$_ -gt 0}).Count -lt 2){$gainPassed=$false}}
$result=[ordered]@{schema='hlclient.cpu-history.analysis.v1';processes=36;exact_signatures='passed';regression_flags=$bad;CPU_time_gain=$(if($gainPassed -and $bad.Count -eq 0){'established_in_owned_fixture'}else{'not_established'});accepted_for_graphics_controls=($gainPassed -and $bad.Count -eq 0);scopes=$summary;operations=$ops;live_sessions=0;MSAA4_total_frame_gain='not_run';target_Win10_100FPS='unproven';manual_acceptance='not_run'}
$result|ConvertTo-Json -Depth 15|Set-Content -LiteralPath (Join-Path $task 'cpu-campaign-analysis.json')
$summary|Where-Object {$_.scope -in @('present','parent_visual_update_subset','commit_new_rx')}|ForEach-Object {[pscustomobject]@{actors=$_.actors;fixed=$_.fixed;diag=$_.diag;scope=$_.scope;B0=$_.p50.B0_median_us;B1=$_.p50.B1_median_us;gain=$_.p50.paired_gain_us}}
$result.CPU_time_gain
