#requires -Version 7
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$task=Join-Path $PSScriptRoot '../manual-artifacts/task-records/studio-pose-frame-build-50b5ffe1-20261024'
if(Test-Path (Join-Path $task 'screen-runs.json')){throw 'Screening already exists'}
$runs=[Collections.Generic.List[object]]::new()
foreach($fixed in @(1,0)){for($pair=0;$pair -lt 2;++$pair){
 $order=if($pair -eq 0){@('B0','B1')}else{@('B1','B0')}
 foreach($variant in $order){
  $name='frame_cpu_'+$variant.ToLower();$exe=Join-Path $task "isolated-fixtures/$variant/$name/bin/$name.exe"
  $path=& (Join-Path $PSScriptRoot 'run_studio_frame_fixture.ps1') -Executable $exe -Label "screen-$variant-f$fixed-pair$pair" -Fixed $fixed
  $runs.Add(@{variant=$variant;fixed=$fixed;pair=$pair;path=$path})
  $runs|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $task 'screen-runs.json')
 }
}}
$gates=Get-Content (Join-Path $task 'screen-baseline-gates.json') -Raw|ConvertFrom-Json
$rows=@(foreach($run in $runs){foreach($line in Get-Content (Join-Path ([string]$run.path) 'driver.stdout.log')){
 if($line -like '[[]history-cost[]]*'){$r=$line.Substring($line.IndexOf('{'))|ConvertFrom-Json;[pscustomobject]@{variant=$run.variant;fixed=$run.fixed;pair=$run.pair;scope=$r.scope;p50=$r.p50_us;p95=$r.p95_us;p99=$r.p99_us;max=$r.max_us;signature=$r.signature}}
}})
$checks=[Collections.Generic.List[object]]::new();$promising=$true
foreach($gate in $gates){
 $b=@($rows|Where-Object {$_.variant -eq 'B0' -and $_.fixed -eq $gate.fixed -and $_.scope -eq $gate.scope}|Sort-Object pair)
 $c=@($rows|Where-Object {$_.variant -eq 'B1' -and $_.fixed -eq $gate.fixed -and $_.scope -eq $gate.scope}|Sort-Object pair)
 if($b.Count -ne 2 -or $c.Count -ne 2){throw 'Missing screening pair'}
 $gains=@(for($i=0;$i -lt 2;++$i){if($b[$i].signature -cne $c[$i].signature){throw 'Screen exact output mismatch'};[double]$b[$i].p50-[double]$c[$i].p50})
 $pass=@($gains|Where-Object {$_ -gt $gate.gate_us}).Count -eq 2
 $promising=$promising -and $pass
 $checks.Add(@{fixed=$gate.fixed;scope=$gate.scope;gate_us=$gate.gate_us;B0=$b;B1=$c;paired_p50_gain_us=$gains;pass=$pass})
}
[ordered]@{status=$(if($promising){'promising_screen_not_final'}else{'not_established'});promising=$promising;checks=$checks;full_acceptance='not_run';live_sessions=0}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $task 'screen-decision.json')
$checks|ForEach-Object {[pscustomobject]@{fixed=$_.fixed;scope=$_.scope;gain=$_.paired_p50_gain_us;gate=$_.gate_us;pass=$_.pass}}
