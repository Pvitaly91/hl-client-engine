#requires -Version 7
param([switch]$Resume)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$task=Join-Path $PSScriptRoot '../manual-artifacts/task-records/studio-rx-tail-causal-50b5ffe1-20261025'
$old=Join-Path $task '../studio-pose-frame-build-50b5ffe1-20261024'
$exe=(Resolve-Path (Join-Path $old 'isolated-fixtures/B0/frame_controlled_b0/bin/frame_controlled_b0.exe')).Path
$hash=(Get-FileHash -LiteralPath $exe).Hash.ToLowerInvariant()
if($hash -cne '773684acb04817238f282b1e9bacef6934c3f5fb9940afc8c7f12f7e754049e3'){throw 'Wrong original controlled B0 driver'}
$plan=Join-Path $task 'aa-plan.json'
if((Test-Path -LiteralPath $plan) -and !$Resume){throw 'A/A already declared; use Resume only to complete unmeasured slots'}
if(!$Resume){
[ordered]@{schema='hlclient.rx-tail.aa.v1';declared_utc=[DateTime]::UtcNow.ToString('o');pairs=3;processes=6;order=@('A0,A1','A1,A0','A0,A1');executable=$exe;sha256=$hash;actors=16;history=16;diagnostics=1;fixed=0;warmup=64;measured_render=256;valid_new_rx=64;valid_no_rx=192;quantile='sort ascending; floor((N-1)*q), no interpolation';gate='Original control: max(50us,5% A0 median,A0 run-quantile span); rejection at >=2 of 3 paired regressions for p50/p95/p99; max reported not gate';environment='HLC_FIXTURE_PAD_PID=1; HLC_FIXTURE_PID absent; same original clocks/input/instrumentation';live_sessions=0;B1_evaluation=$false}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $plan
}
[Environment]::SetEnvironmentVariable('HLC_FIXTURE_PID',[NullString]::Value,'Process');$env:HLC_FIXTURE_PAD_PID='1'
$runs=[Collections.Generic.List[object]]::new()
$progress=Join-Path $task 'aa-progress.json'
if(Test-Path -LiteralPath $progress){foreach($r in @(Get-Content -LiteralPath $progress -Raw|ConvertFrom-Json)){$runs.Add($r)}}
for($pair=0;$pair -lt 3;++$pair){
 $order=if($pair%2 -eq 0){@('A0','A1')}else{@('A1','A0')}
 foreach($label in $order){
  if(@($runs|Where-Object {$_.pair -eq $pair -and $_.variant -eq $label}).Count){continue}
  $path=& (Join-Path $PSScriptRoot 'run_rx_tail_fixture.ps1') -Executable $exe -Label "aa-$label-pair$pair" -Diagnostics 1 -Fixed 0
  $runs.Add(@{variant=$label;pair=$pair;path=$path})
  $runs|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $task 'aa-progress.json')
 }
}
'Declared A/A six processes complete'
