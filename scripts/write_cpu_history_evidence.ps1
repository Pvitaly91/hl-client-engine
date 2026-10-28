#requires -Version 7
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$task=Join-Path $repo 'manual-artifacts/task-records/cpu-history-projection-50b5ffe1-20261024'
function Read-Json([string]$name){Get-Content -LiteralPath (Join-Path $task $name) -Raw|ConvertFrom-Json -AsHashtable}
function Identity([string]$path) {
 $f=Get-Item -LiteralPath $path
 [ordered]@{path=[IO.Path]::GetRelativePath($repo,$f.FullName).Replace('\','/');bytes=$f.Length;sha256=(Get-FileHash -LiteralPath $f.FullName).Hash.ToLowerInvariant()}
}
$plan=Read-Json 'cpu-campaign-plan.json';$analysis=Read-Json 'cpu-campaign-analysis.json';$progress=Read-Json 'cpu-campaign-progress.json'
$tests=@(Get-ChildItem $task -Directory -Filter 'withdrawal-Release-focused-*'|ForEach-Object {Get-ChildItem $_.FullName -Filter '*.result.json'}|ForEach-Object {Get-Content -LiteralPath $_.FullName -Raw|ConvertFrom-Json -AsHashtable})
$sources=Read-Json 'baseline/source-manifest.json'
$production=@($sources|Where-Object {$_.path -like 'apps/*' -or $_.path -like 'include/*' -or $_.path -like 'src/*'})
foreach($file in $production){if((Get-FileHash -LiteralPath (Join-Path $repo $file.path)).Hash -ine $file.sha256){throw 'Production withdrawal verification failed'}}
$profile=[Collections.Generic.List[object]]::new()
foreach($f in Get-ChildItem (Join-Path $task 'profile-grid-gordon-20261024T200215748Z-469705') -Filter '*.stdout.log') {
 foreach($line in Get-Content -LiteralPath $f.FullName) {
  if($line -like '[[]history-cost[]]*') {$r=$line.Substring($line.IndexOf('{'))|ConvertFrom-Json -AsHashtable;$r.Remove('raw_us');$profile.Add($r)}
 }
}
$guards=@($progress|ForEach-Object{Get-Content -LiteralPath (Join-Path ([string]$_.path) 'host-guard.json') -Raw|ConvertFrom-Json -AsHashtable})
$assets=@('player/gordon/gordon.mdl','p_shotgun.mdl')|ForEach-Object {
 $f=Get-Item -LiteralPath (Join-Path 'D:/DEV/HLCLIENT-RESEARCH/Half-Life/valve/models' $_)
 [ordered]@{virtual_name='models/'+$_;bytes=$f.Length;sha256=(Get-FileHash -LiteralPath $f.FullName).Hash.ToLowerInvariant();identity_captured='final audit; timed outputs also compare exact imported frame signatures'}
}
$evidence=[ordered]@{
 schema='hlclient.cpu-history-projection.50b5ffe1.v1';written_utc=[DateTime]::UtcNow.ToString('o');head='6a86ce0e3b826341c90bbcee8c603771f1dc58c8';branch='main'
 statuses=@{profiling_complete='completed_for_declared_offline_workload';production_fix='rejected_and_withdrawn';CPU_time_gain='not_established';MSAA4_total_frame_gain='not_run_rejected_CPU_candidate';target_Win10_100FPS='unproven';manual_acceptance='not_run'}
 baseline=Read-Json 'baseline/receipt.json';source_restoration=Read-Json 'withdrawal-source-verification.json'
 historical=@{runs=@('0c29f4caaaad3ed789ab8ee1474e4f70','50b5ffe11be28794b66dcd524a34fc7d');AAOff_frame7775_Studio_ms=11.27;MSAA4_frame660_Studio_ms=27.05;MSAA4_frame660_CPUupdate_ms=38.35;exact_historical_entities='not_retained_not_fabricated';old_full_suite='4190 passed/75 skipped belongs to prior B0 only';GPU_runs_repeated=0;old_11_regressions='control regressions; not presumed pixel mismatches'}
 scope_limits=@{normal_path='RuntimeReplayLocalAssets::present_entities/project_entities';timers='inclusive_not_additive';parent='owned visual update subset; not full main CPU update/frame/FPS';noRX='sample time advances; whole presented frame not cached';world_entity='mutable module dependencies; no cache/callback skipping';allocations='C++ new/new[]/aligned-new requested bytes/calls, not RSS/live retained heap/complete CRT malloc';profile_counts='declared fixtures have all lifetime rows present; absent-row profiling counters not newly certified'}
 measured_profiles=$profile;assets=$assets;plan=$plan;CPU_analysis=$analysis
 identities=@(Identity (Join-Path $task 'isolated-fixtures/B0/hp_cpu_b0/bin/hp_cpu_b0.exe');Identity (Join-Path $task 'isolated-fixtures/B1/hp_cpu_b1/bin/hp_cpu_b1.exe');Identity (Join-Path $task 'cpu-campaign-analysis.json');Identity (Join-Path $task 'cpu-harness/test_runtime_replay_local_assets.cpp');Identity (Join-Path $task 'cpu-harness/test_studio_pose_evaluator.cpp');Identity (Join-Path $task 'rejected-rotation-candidate/manifest.json'))
 compiler=@{actual='MSVC14.34.31933/v143/Win32/Release';proof='actual .lastbuildstate and CL.read paths; no assumption from default toolset file'}
 candidate=@{change='within-one-bone/sample bit-identical Euler endpoint quaternion conversion reuse; slerp and validation unchanged';oracle=@{inputs=162;successful=90;expected_rejections=72;B0_B1_exact_signature='994319084254938540';B0_assertions=108;B1_assertions=288};parent_operation_witness=@{samples=256;actual_conversions=375337;actual_reuses=205271;without_reuse_conversions=580608;scope='parent poses only, not complete child/viewmodel path'};accepted=$false;rollback='apply_patch to actual saved B0; no git restore/reset/checkout'}
 guard=@{processes=36;busy_events=@($guards|ForEach-Object{$_.busy_events}).Count;maximum_poll_gap_ms=($guards.max_poll_gap_ms|Measure-Object -Maximum).Maximum;polling_limitation='A process wholly between polls can be missed';foreign_processes_stopped=0;builds_during_timing=0}
 new_tests=@{focused_Release=$tests;full_Debug_Release_suite='not_run_no_accepted_production_fix';ASan_build='not_run_this_package';ASan_runtime='not_run_this_package';core_off_architecture='not_run_no_accepted_production_fix';MSAA4_AAOff_MSAA8_MSAA2='not_run_CPU_gate_rejected';additional_policy_readiness_alternate_staging_controls='not_newly_certified_for_rejected_candidate'}
 setup_attempts=@('snapshot git --output argument repaired with re-verification','getenv warning-as-error fixed using _dupenv_s','first installed-player smoke failed with zero instances; excluded from timings; declared Gordon fixture passed','owned /m:2 build stalled after Checking File Globs; exact reason unproven; verified owned PID only stopped; /m:1 passed')
 SDL_recovery=Read-Json 'incidental-SDL-rollback.json';deliveries=Read-Json 'final-delivery-audit.json'
 final_checks=Read-Json 'final-readonly-check.json'
 safety=@{new_live_sessions=0;Steam_auth_HLDS_sessions=0;settings_resources_server_drivers_power='unchanged';shared_test_data='untouched';git_staging_commit_push='none';new_manual_test_requested=$false}
 next_measured_unclosed_stage=@{function='StudioPoseEvaluator::compose current-time local channel sampling/composition';inclusive_p50_us=615.2;channel_lookup_validation_trig_individual_cost='not_yet_established';secondary=@{EntityRenderFrameBuilder_build_us=314.3;child_attachment_us=219.1};not_recommended='complex history cache for <1% scan; returning rejected endpoint reuse without new causal proof'}
}
$destination=Join-Path $repo 'docs/evidence/CPU_HISTORY_PROJECTION_50B5FFE1.json'
$evidence|ConvertTo-Json -Depth 35|Set-Content -LiteralPath $destination -Encoding utf8NoBOM
Identity $destination|ConvertTo-Json
