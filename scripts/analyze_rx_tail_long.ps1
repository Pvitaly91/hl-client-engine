#requires -Version 7
param([switch]$AAOnly)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$task=Join-Path $PSScriptRoot '../manual-artifacts/task-records/studio-rx-tail-causal-50b5ffe1-20261025'
$runs=@(Get-Content -LiteralPath (Join-Path $task 'long-progress.json') -Raw|ConvertFrom-Json)
if($AAOnly){$runs=@($runs|Where-Object phase -EQ AA);if($runs.Count -ne 6){throw 'Expected six declared A/A processes'}}
elseif($runs.Count -ne 24){throw 'Expected all24 declared processes; incomplete campaign is not acceptance'}
$rows=[Collections.Generic.List[object]]::new();$memory=[Collections.Generic.List[object]]::new();$witnesses=[Collections.Generic.List[object]]::new();$lifecycle=[Collections.Generic.List[object]]::new()
foreach($run in $runs){
 $result=Get-Content -LiteralPath (Join-Path $run.path 'result.json') -Raw|ConvertFrom-Json
 $guard=Get-Content -LiteralPath (Join-Path $run.path 'host-guard.json') -Raw|ConvertFrom-Json
 if($result.exit_code -ne 0 -or $guard.busy_events.Count){throw 'Failed or contaminated process'}
 $name='rx_long_'+$run.binary_variant.ToLower();$ready=Get-Content -LiteralPath (Join-Path $task "isolated-fixtures/$($run.binary_variant)/$name/ready.json") -Raw|ConvertFrom-Json
 if($result.executable_sha256 -ine $ready.executable_sha256){throw 'Driver hash changed'}
 foreach($line in Get-Content -LiteralPath (Join-Path $run.path 'driver.stdout.log')){
  if($line -like '[[]history-cost[]]*'){
   $v=$line.Substring($line.IndexOf('{'))|ConvertFrom-Json
   if($v.actual_models -ne 1 -or $v.profile -ne 0 -or $v.actors -ne 16 -or $v.history -ne 16 -or [int]$v.fixed_history -ne [int]$run.fixed -or [int]$v.diagnostics -ne [int]$run.diagnostics){throw 'Wrong workload/instrumentation'}
   $sorted=@($v.raw_us|Sort-Object);if($sorted.Count -ne $v.samples){throw 'Raw count mismatch'}
   foreach($q in @(50,95,99)){if([double]$sorted[[int][Math]::Floor(($sorted.Count-1)*$q/100)] -ne [double]$v."p${q}_us"){throw 'Quantile mismatch'}}
   $expected=if($run.fixed){256}elseif($v.scope -eq 'parent_no_rx' -or $v.scope -eq 'owned_no_rx_sum'){3072}elseif($v.scope -in @('commit_new_rx','present_new_rx','parent_new_rx','owned_new_rx_sum')){1024}else{4096}
   if($sorted.Count -ne $expected){throw "Wrong count: $($v.scope)=$($sorted.Count), expected$expected"}
   # Original commit includes empty no-RX checks. Never use it as RX evidence.
   if($v.scope -eq 'commit'){continue}
   $rows.Add([pscustomobject]@{phase=$run.phase;variant=$run.variant;binary_variant=$run.binary_variant;pair=$run.pair;scope=$v.scope;samples=$v.samples;p50=[double]$v.p50_us;p95=[double]$v.p95_us;p99=[double]$v.p99_us;max=[double]$v.max_us;signature=$v.signature;no_rx_pose_changes=$v.no_rx_pose_changes;raw_us=$v.raw_us;source=$run.path})
  }elseif($line -like '[[]history-operations[]]*'){
   $v=$line.Substring($line.IndexOf('{'))|ConvertFrom-Json
   if($v.new_rx_frames -ne $(if($run.fixed){0}else{1024})){throw 'RX operation count mismatch'}
   $memory.Add([pscustomobject]@{phase=$run.phase;variant=$run.variant;pair=$run.pair;commit_calls=$v.allocations[0].calls;commit_bytes=$v.allocations[0].bytes;present_calls=$v.allocations[1].calls;present_bytes=$v.allocations[1].bytes;matrix_capacity=$v.matrix_capacity_high_water;capture_bytes=$v.capture_bytes;cold_calls=$v.cold_calls;cold_bytes=$v.cold_bytes;cold_prepare_us=$v.cold_prepare_us;cold_root_length=$v.cold_root_length})
  }elseif($line -like '[[]rx-tail-witness[]]*'){$v=$line.Substring($line.IndexOf('{'))|ConvertFrom-Json;$witnesses.Add(@{phase=$run.phase;variant=$run.variant;pair=$run.pair;witness=$v})}
  elseif($line -like '[[]rx-tail-lifecycle[]]*'){$v=$line.Substring($line.IndexOf('{'))|ConvertFrom-Json;$lifecycle.Add(@{phase=$run.phase;variant=$run.variant;pair=$run.pair;values=$v})}
 }
}
function Median($values){$v=@($values|Sort-Object);[double]$v[[int][Math]::Floor(($v.Count-1)/2)]}
$summary=[Collections.Generic.List[object]]::new();$flags=[Collections.Generic.List[object]]::new()
foreach($g in $rows|Group-Object phase,scope){
 $b=@($g.Group|Where-Object variant -EQ B0|Sort-Object pair);$c=@($g.Group|Where-Object variant -EQ B1|Sort-Object pair)
 if($b.Count -ne 3 -or $c.Count -ne 3){throw 'Unpaired scope'}
 for($i=0;$i -lt 3;++$i){if($b[$i].signature -cne $c[$i].signature -or $b[$i].no_rx_pose_changes -ne $c[$i].no_rx_pose_changes){throw 'Output/time witness mismatch'}}
 foreach($metric in @('p50','p95','p99','max')){
  $bv=@($b|ForEach-Object{[double]$_.$metric});$cv=@($c|ForEach-Object{[double]$_.$metric});$delta=@(for($i=0;$i -lt 3;++$i){$cv[$i]-$bv[$i]})
  $span=($bv|Measure-Object -Maximum).Maximum-($bv|Measure-Object -Minimum).Minimum;$median=Median $bv
  $gate=[Math]::Max(50.0,[Math]::Max(.05*$median,$span));$bad=@($delta|Where-Object {$_ -gt $gate}).Count
  $entry=[ordered]@{phase=$b[0].phase;scope=$b[0].scope;metric=$metric;samples_per_run=$b[0].samples;B0_values=$bv;B1_values=$cv;delta_us=$delta;delta_percent=@(for($i=0;$i -lt 3;++$i){100*$delta[$i]/$bv[$i]});median_paired_gain_us=-(Median $delta);B0_span_us=$span;gate_us=$gate;regression_pairs=$bad;reject=($metric -ne 'max' -and $bad -ge 2)}
  if($entry.reject){$flags.Add($entry)}
  if($entry.phase -eq 'PrimaryOff' -and $metric -eq 'p50' -and $entry.scope -in @('present','parent_visual_update_subset','owned_visual_sum')){
   $entry.primary_gain_gate_us=[Math]::Max(30.0,[Math]::Max(.05*$median,$span))+10
   $entry.primary_pass=$entry.median_paired_gain_us -gt $entry.primary_gain_gate_us -and @($delta|Where-Object {$_ -lt 0}).Count -ge 2
   if(!$entry.primary_pass){$flags.Add(@{reason='primary_gain_not_qualified';values=$entry})}
  }
  $summary.Add([pscustomobject]$entry)
 }
}
foreach($g in $memory|Group-Object phase,pair){
 $b=@($g.Group|Where-Object variant -EQ B0)[0];$c=@($g.Group|Where-Object variant -EQ B1)[0]
 foreach($key in @('commit_calls','commit_bytes','present_calls','present_bytes','matrix_capacity','capture_bytes','cold_calls','cold_bytes')){
  if($c.$key -gt $b.$key){$flags.Add(@{case=$g.Name;reason='memory_growth';metric=$key;B0=$b.$key;B1=$c.$key})}
 }
}
$name=if($AAOnly){'long-aa-analysis.json'}else{'long-analysis.json'}
[ordered]@{schema='hlclient.rx-tail.long-analysis.v1';processes=$runs.Count;reject=($flags.Count -gt 0);flags=$flags;summary=$summary;memory=$memory;bounded_long_event_witnesses=$witnesses;lifecycle=$lifecycle;scopes=$rows;historical_rejection='retained';full_frame='not measured';independent_repetitions=3;adjacent_RX_samples_are_not_independent=$true}|ConvertTo-Json -Depth 24|Set-Content -LiteralPath (Join-Path $task $name)
$summary|Where-Object {$_.scope -in @('commit_new_rx','parent_new_rx','owned_new_rx_sum') -and $_.metric -eq 'p99'}|Select-Object phase,scope,B0_values,B1_values,delta_us,gate_us,regression_pairs|Format-Table -AutoSize
"Rejected=$($flags.Count -gt 0); flags=$($flags.Count)"
