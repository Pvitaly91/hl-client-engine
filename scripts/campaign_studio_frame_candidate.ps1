#requires -Version 7
param([switch]$Controlled)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$task=Join-Path $PSScriptRoot '../manual-artifacts/task-records/studio-pose-frame-build-50b5ffe1-20261024'
$prefix=if($Controlled){'controlled'}else{'final'}
if(Test-Path (Join-Path $task ($prefix+'-cpu-plan.json'))){throw 'Preserve existing campaign'}
if($Controlled){
 $proof=Get-Content (Join-Path $task 'cold-path-causal-proof.json') -Raw|ConvertFrom-Json
 if($proof[1].cold_calls-$proof[0].cold_calls -ne 6 -or $proof[1].cold_bytes-$proof[0].cold_bytes -ne 1988 -or $proof[2].cold_calls -ne $proof[3].cold_calls -or $proof[2].cold_bytes -ne $proof[3].cold_bytes){throw 'Cold path cause not reproduced'}
 [Environment]::SetEnvironmentVariable('HLC_FIXTURE_PID',[NullString]::Value,'Process');$env:HLC_FIXTURE_PAD_PID='1'
}
$screen=Get-Content (Join-Path $task 'screen-decision.json') -Raw|ConvertFrom-Json
if(!$screen.promising){throw 'Full campaign requires promising integrated screen'}
$cases=@(@{actors=4;history=2;fixed=1},@{actors=8;history=16;fixed=1},@{actors=16;history=16;fixed=1},@{actors=16;history=16;fixed=0})
[ordered]@{schema='hlclient.studio-frame.final-cpu.v1';cases=$cases;diagnostics=@(0,1);pairs=3;processes=48;warmup=64;samples=256;
 gates='Exactly screen-plan.json formula, no change after candidate results. Primary16/16 Off noRX present+parent gate=max(30us,B0 p50 span,5% B0 median)+10us and at least2/3 positive. Controls no recurring>=2/3 p50/p95/p99 regressions beyond own max(50us,5% B0 median,B0 quantile span). Allocation calls/bytes/capacity cannot grow. Max/cold raw retained.';
 screen_data_pooled=$false;profiling_in_timed_driver=$false;live_sessions=0;controlled_native_path=[bool]$Controlled;
 reason=$(if($Controlled){'One causal confirmation:4/5-digit fixture PID reproduces exactly6 calls/1988 bytes; fixed-width native temp path makes cold inputs comparable. Original48 runs and strict failure retained. Production source and zero-growth memory/timing gates unchanged.'}else{'Initial declared final campaign'})}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $task ($prefix+'-cpu-plan.json'))
$runs=[Collections.Generic.List[object]]::new()
foreach($case in $cases){foreach($diag in @(0,1)){for($pair=0;$pair -lt 3;++$pair){
 $order=if($pair%2 -eq 0){@('B0','B1')}else{@('B1','B0')}
 foreach($variant in $order){
  $name='frame_'+$prefix+'_'+$variant.ToLower();$exe=Join-Path $task "isolated-fixtures/$variant/$name/bin/$name.exe"
  $label="$prefix-$variant-a$($case.actors)-h$($case.history)-f$($case.fixed)-d$diag-pair$pair"
  $path=& (Join-Path $PSScriptRoot 'run_studio_frame_fixture.ps1') -Executable $exe -Label $label -Actors $case.actors -History $case.history -Diagnostics $diag -Fixed $case.fixed
  $runs.Add(@{variant=$variant;actors=$case.actors;history=$case.history;fixed=$case.fixed;diagnostics=$diag;pair=$pair;path=$path})
  $runs|ConvertTo-Json -Depth 8|Set-Content (Join-Path $task ($prefix+'-cpu-progress.json'))
 }
}}}
'Final CPU campaign complete'
