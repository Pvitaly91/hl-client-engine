#requires -Version 7
param([switch]$DeclareOnly,[switch]$Resume)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$task=Join-Path $PSScriptRoot '../manual-artifacts/task-records/studio-rx-tail-causal-50b5ffe1-20261025'
$planPath=Join-Path $task 'long-plan.json'
if(!$Resume){
 if(Test-Path -LiteralPath $planPath){throw 'Long campaign already declared; preserve all slots'}
 $aa=Get-Content -LiteralPath (Join-Path $task 'aa-analysis.json') -Raw|ConvertFrom-Json
 if($aa.reject){throw 'Original A/A failed; diagnose that method before declaration'}
 [ordered]@{schema='hlclient.rx-tail.long-plan.v1';declared_utc=[DateTime]::UtcNow.ToString('o');
 original_AA='Six identical-hash runs: no recurring rejection. This does NOT disprove original B1 regression.';
 why_changed='Original commit p99 uses second largest of64; increase valid RX count without changing gate formula or order statistic. Old parent excluded local owning frame destruction; add separate release and per-event nonoverlapping sum.';
 pairs=3;total_processes=24;phases=@('long_AA_DiagnosticsOn:6','long_B0_B1_DiagnosticsOff:6','long_B0_B1_DiagnosticsOn:6','original_noRX_primary_Off_with_release:6');
 order=@('B0,B1','B1,B0','B0,B1');warmup_render=64;RX_measured_render=4096;valid_new_RX_per_run=1024;valid_no_RX_per_run=3072;primary_noRX_render=256;
 time='same scripted application5ms/RX20ms clocks, real steady_clock timing, diagnostics internal wall clock unchanged';
 quantile='lower order statistic floor((N-1)*q), unchanged; N1024 p99 index1012, eleven observations above it';
 instrumentation='Same single TU, same global new/delete counters B0/B1. Counters count allocations/requested bytes/frees, NOT retained byte size. Nonowning old/new frame observations, at most16 post-run witnesses, no stacks or frame JSON in timers.';
 gates='Original max(50us,5% baseline run-quantile median,baseline run-quantile span); recurring >=2/3 worse p50/p95/p99 rejects in ANY commit/parent/release/owned scope. max reported not a gate. Primary noRX Off present+parent+owned p50 gain must exceed max(30us,5% B0median,B0span)+10us and be positive >=2/3. Exact signatures/noRX time witnesses and memory capacities/allocation calls/bytes may not worsen. Cold separate, not discarded.';
 method_validity='Long A/A before B1; recurring A/A rejection stops B1 evaluation. No pooling across modes or with original runs. Correlated adjacent samples are not independent runs; only3 process pairs per condition. All outliers retained; no campaign retry on timing failure.';
 stop='Any foreign build/test/game invalidates slot and stops campaign; retain contaminated record and only resume unmeasured slot after idle. No retiming of valid failed slot.';
 full_frame='not measured: CPU visual update subset excludes rendering/swap/GPU and fixture oracle. Owned sum is sum of nonoverlapping same-event durations, not quantile addition.';
 profiling='Detailed allocation stack profiling separate if required; no profiling in these runs';production_source='unchanged B0';active_deliveries='unchanged B0';live_sessions=0;git_mutations='none'}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $planPath
}
if($DeclareOnly){exit 0}
$runs=[Collections.Generic.List[object]]::new();$progress=Join-Path $task 'long-progress.json'
if(Test-Path -LiteralPath $progress){foreach($r in @(Get-Content -LiteralPath $progress -Raw|ConvertFrom-Json)){$runs.Add($r)}}
[Environment]::SetEnvironmentVariable('HLC_FIXTURE_PID',[NullString]::Value,'Process');$env:HLC_FIXTURE_PAD_PID='1'
foreach($phase in @('AA','RXOff','RXOn','PrimaryOff')){
 for($pair=0;$pair -lt 3;++$pair){
  $order=if($pair%2 -eq 0){@('B0','B1')}else{@('B1','B0')}
  foreach($variant in $order){
   if(@($runs|Where-Object {$_.phase -eq $phase -and $_.pair -eq $pair -and $_.variant -eq $variant}).Count){continue}
   $binaryVariant=if($phase -eq 'AA'){'B0'}else{$variant};$diag=if($phase -in @('AA','RXOn')){1}else{0};$fixed=if($phase -eq 'PrimaryOff'){1}else{0}
   $name='rx_long_'+$binaryVariant.ToLower();$exe=Join-Path $task "isolated-fixtures/$binaryVariant/$name/bin/$name.exe"
   $path=& (Join-Path $PSScriptRoot 'run_rx_tail_fixture.ps1') -Executable $exe -Label "long-$phase-$variant-pair$pair" -Diagnostics $diag -Fixed $fixed
   $runs.Add(@{phase=$phase;variant=$variant;binary_variant=$binaryVariant;pair=$pair;diagnostics=$diag;fixed=$fixed;path=$path})
   $runs|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $progress
  }
 }
 if($phase -eq 'AA'){
  & (Join-Path $PSScriptRoot 'analyze_rx_tail_long.ps1') -AAOnly
  $method=Get-Content -LiteralPath (Join-Path $task 'long-aa-analysis.json') -Raw|ConvertFrom-Json
  if($method.reject){throw 'New method A/A rejected identical B0. B1 phase not started; no repeat-until-pass.'}
 }
}
'Long campaign complete:24 fixed processes'
