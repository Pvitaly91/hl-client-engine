#requires -Version 7
param([switch]$Controlled)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$task=Join-Path $PSScriptRoot '../manual-artifacts/task-records/studio-pose-frame-build-50b5ffe1-20261024'
$prefix=if($Controlled){'controlled'}else{'final'}
$runs=@(Get-Content (Join-Path $task ($prefix+'-cpu-progress.json')) -Raw|ConvertFrom-Json)
if($runs.Count -ne 48){throw 'Expected all48 declared processes'}
function Median($values){$v=@($values|Sort-Object);return [double]$v[[int][Math]::Floor(($v.Count-1)/2)]}
$rows=[Collections.Generic.List[object]]::new();$memory=[Collections.Generic.List[object]]::new();$flags=[Collections.Generic.List[object]]::new()
foreach($run in $runs){
 $root=[string]$run.path;$result=Get-Content (Join-Path $root 'result.json') -Raw|ConvertFrom-Json
 $guard=Get-Content (Join-Path $root 'host-guard.json') -Raw|ConvertFrom-Json
 if($result.exit_code -ne 0 -or $guard.busy_events.Count){throw 'Failed or contaminated run'}
 foreach($line in Get-Content (Join-Path $root 'driver.stdout.log')){
  if($line -like '[[]history-cost[]]*'){
   $r=$line.Substring($line.IndexOf('{'))|ConvertFrom-Json
   $rows.Add([pscustomobject]@{variant=$run.variant;pair=$run.pair;actors=$r.actors;history=$r.history;fixed=$r.fixed_history;diag=$r.diagnostics;scope=$r.scope;p50=[double]$r.p50_us;p95=[double]$r.p95_us;p99=[double]$r.p99_us;max=[double]$r.max_us;signature=$r.signature;no_rx_pose_changes=$r.no_rx_pose_changes})
  }elseif($line -like '[[]history-operations[]]*'){
   $r=$line.Substring($line.IndexOf('{'))|ConvertFrom-Json
   $memory.Add([pscustomobject]@{variant=$run.variant;pair=$run.pair;actors=$run.actors;history=$run.history;fixed=$run.fixed;diag=$run.diagnostics;commit_calls=$r.allocations[0].calls;commit_bytes=$r.allocations[0].bytes;present_calls=$r.allocations[1].calls;present_bytes=$r.allocations[1].bytes;new_rx_frames=$r.new_rx_frames;matrix_capacity=$r.matrix_capacity_high_water;capture_bytes=$r.capture_bytes;cold_prepare_us=$r.cold_prepare_us;cold_calls=$r.cold_calls;cold_bytes=$r.cold_bytes})
  }
 }
}
$summary=[Collections.Generic.List[object]]::new();$primaryPass=$true
foreach($group in $rows|Group-Object actors,history,fixed,diag,scope){
 $b=@($group.Group|Where-Object variant -EQ B0|Sort-Object pair);$c=@($group.Group|Where-Object variant -EQ B1|Sort-Object pair)
 if($b.Count -ne 3 -or $c.Count -ne 3){throw 'Unpaired final data'}
 for($i=0;$i -lt 3;++$i){if($b[$i].signature -cne $c[$i].signature -or $b[$i].no_rx_pose_changes -ne $c[$i].no_rx_pose_changes){throw 'Exact outputs/witnesses differ'}}
 $entry=[ordered]@{actors=$b[0].actors;history=$b[0].history;fixed=$b[0].fixed;diag=$b[0].diag;scope=$b[0].scope}
 foreach($metric in @('p50','p95','p99','max')){
  $bv=@($b|ForEach-Object{[double]$_.$metric});$cv=@($c|ForEach-Object{[double]$_.$metric})
  $gain=@(for($i=0;$i -lt 3;++$i){$bv[$i]-$cv[$i]})
  $span=[double]($bv|Measure-Object -Maximum).Maximum-[double]($bv|Measure-Object -Minimum).Minimum
  $gate=[Math]::Max(50.0,[Math]::Max((Median $bv)*.05,$span))
  $bad=@($gain|Where-Object {$_ -lt -$gate}).Count
  $entry[$metric]=@{B0_median_us=Median $bv;B1_median_us=Median $cv;paired_gain_us=Median $gain;gains=$gain;B0_span_us=$span;regression_gate_us=$gate;regression_pairs=$bad;B0_values=$bv;B1_values=$cv}
  if($metric -ne 'max' -and $bad -ge 2){$flags.Add(@{case=$group.Name;metric=$metric;regression_pairs=$bad})}
 }
 if($entry.actors -eq 16 -and $entry.fixed -eq 1 -and $entry.diag -eq 0 -and $entry.scope -in @('present','parent_visual_update_subset')){
  $gate=[Math]::Max(30.0,[Math]::Max($entry.p50.B0_span_us,$entry.p50.B0_median_us*.05))+10.0
  $pass=$entry.p50.paired_gain_us -gt $gate -and @($entry.p50.gains|Where-Object {$_ -gt 0}).Count -ge 2
  $entry['primary_gate_us']=$gate;$entry['primary_pass']=$pass;$primaryPass=$primaryPass -and $pass
 }
 $summary.Add($entry)
}
foreach($g in $memory|Group-Object actors,history,fixed,diag,pair){
 $b=@($g.Group|Where-Object variant -EQ B0)[0];$c=@($g.Group|Where-Object variant -EQ B1)[0]
 foreach($key in @('commit_calls','commit_bytes','present_calls','present_bytes','matrix_capacity','capture_bytes','cold_calls','cold_bytes')){
  if($c.$key -gt $b.$key){$flags.Add(@{case=$g.Name;metric=$key;B0=$b.$key;B1=$c.$key})}
 }
 if($c.new_rx_frames -ne $b.new_rx_frames){throw 'RX workload mismatch'}
}
$accepted=$primaryPass -and $flags.Count -eq 0
[ordered]@{schema='hlclient.studio-frame.final-analysis.v1';status=$(if($accepted){'CPU_accepted_pending_graphics_and_quality'}else{'CPU_not_accepted'});CPU_accepted=$accepted;processes=48;screen_data_pooled=$false;exact_outputs='all48 signatures/noRX witnesses match';regression_flags=$flags;scopes=$summary;memory_cold=$memory;target_hardware_gain='not_run';MSAA4_total_frame_gain='not_run';manual_acceptance='not_run';controlled_native_path=[bool]$Controlled}|ConvertTo-Json -Depth 18|Set-Content (Join-Path $task ($prefix+'-cpu-analysis.json'))
$summary|Where-Object {$_.scope -eq 'present'}|ForEach-Object {[pscustomobject]@{actors=$_.actors;fixed=$_.fixed;diag=$_.diag;B0=$_.p50.B0_median_us;B1=$_.p50.B1_median_us;paired_gain=$_.p50.paired_gain_us}}
"CPU accepted=$accepted; flags=$($flags.Count)"
