#requires -Version 7
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$task=Join-Path $repo 'manual-artifacts/task-records/studio-pose-frame-build-50b5ffe1-20261024'
function Read-Record([string]$name){Get-Content -LiteralPath (Join-Path $task $name) -Raw|ConvertFrom-Json -AsHashtable}
function Identity([string]$path){$f=Get-Item -LiteralPath $path;[ordered]@{path=[IO.Path]::GetRelativePath($repo,$f.FullName).Replace('\','/');sha256=(Get-FileHash -LiteralPath $f.FullName).Hash.ToLowerInvariant();bytes=$f.Length}}
function Read-Prefixed([string]$name,[string]$prefix){@(Get-Content -LiteralPath (Join-Path $task $name)|Where-Object {$_.StartsWith($prefix,[StringComparison]::Ordinal)}|ForEach-Object {$_.Substring($_.IndexOf('{'))|ConvertFrom-Json -AsHashtable})}
$manifest=Read-Record 'baseline/source-manifest.json'
$production=@($manifest|Where-Object {$_.path -match '^(apps|include|src)/'})
foreach($f in $production){if((Get-FileHash -LiteralPath (Join-Path $repo $f.path)).Hash -ine $f.sha256){throw 'Production not restored to actual B0'}}
$tests=@(Get-ChildItem $task -Directory -Filter 'withdrawal-frame-Release-*'|ForEach-Object {Get-ChildItem $_.FullName -Filter '*.result.json'}|ForEach-Object {Get-Content -LiteralPath $_.FullName -Raw|ConvertFrom-Json -AsHashtable})
$oracle=Get-ChildItem $task -Directory -Filter 'frame-oracle-*'|Select-Object -First 1
$initial=Read-Record 'final-cpu-analysis.json';$confirmation=Read-Record 'controlled-cpu-analysis.json'
if($confirmation.CPU_accepted){throw 'This withdrawal report requires the actual rejection result'}
$allRuns=@(Read-Record 'final-cpu-progress.json')+@(Read-Record 'controlled-cpu-progress.json')
$guards=@($allRuns|ForEach-Object {Get-Content -LiteralPath (Join-Path ([string]$_.path) 'host-guard.json') -Raw|ConvertFrom-Json -AsHashtable})
$drivers=@(foreach($name in @('frame_cpu_b0','frame_cpu_b1','frame_final_b0','frame_final_b1','frame_controlled_b0','frame_controlled_b1')){
 $v=if($name.EndsWith('b0')){'B0'}else{'B1'};Identity (Join-Path $task "isolated-fixtures/$v/$name/bin/$name.exe")
})
$report=[ordered]@{
 schema='hlclient.studio-pose-frame-build.50b5ffe1.v1';created_utc=[DateTime]::UtcNow.ToString('o');head='6a86ce0e3b826341c90bbcee8c603771f1dc58c8';branch='main'
 statuses=@{production_fix='rejected_and_withdrawn';integrated_CPU_gain='measured_p50_gain_not_qualified';MSAA4_total_frame_gain='not_run_CPU_rejected';target_Win10_100FPS='unproven';target_hardware_gain='not_run';manual_acceptance='not_run'}
 baseline=Read-Record 'baseline/receipt.json';withdrawal=Read-Record 'withdrawal-source-verification.json';drivers=$drivers
 identities=@(Identity (Join-Path $task 'baseline/source-manifest.json');Identity (Join-Path $task 'rejected-frame-candidate/manifest.json');Identity (Join-Path $task 'rejected-frame-candidate/entity_scene_render.cpp');Identity (Join-Path $repo 'src/entity_render/entity_scene_render.cpp');Identity (Join-Path $task 'controlled-support/local_resource_test_fixture.hpp');Identity (Join-Path $task 'controlled-harness/test_runtime_replay_local_assets.cpp'))
 compiler=@{toolset='MSVC14.34.31933/v143';target='Win32 Release/O2';ABI='original public headers; no added pose/result fields';profiling='Separate private projects with debug symbols; never linked into acceptance timing drivers'}
 historical=@{previous_63_cases='historical previous package; not new full suite';previous_full_suite='historical4190 passed/75 skipped, not current-candidate acceptance';historical_frame_inputs='not_retained_not_fabricated';old_shader_resolve_setup_campaigns_repeated=0;rejected_endpoint_reuse_restored=$false;A_D_preserved=$true}
 profiling=@{normal_path='RuntimeReplayLocalAssets -> StudioPoseEvaluator and EntityRenderFrameBuilder';private_copies_only=$true;normal_per_bone_clocks=0;per_pose_counter_growth_bytes=0;output_signature='3280725200226478204';overhead=@{timers_on_present_p50_us=1707.3;timers_off_same_private_driver_p50_us=1443.2;acceptance_driver_has_detailed_timers=$false};aggregate_scopes=Read-Prefixed 'detail-timers-B0-20261024T210213695Z-eec011/driver.stdout.log' '[studio-detail]';scope_limit='Inclusive totals, world+viewmodel frame calls; per-bone private-clock overhead included, not an additive budget or p50';allocation_summary=Read-Prefixed 'detail-allocations-B0-20261024T210215729Z-10a25e/driver.stdout.log' '[allocation-profile]';allocation_sites=Read-Prefixed 'detail-allocations-B0-20261024T210215729Z-10a25e/driver.stdout.log' '[allocation-site]';memory_limit='Tracked C++ requested allocation phases only; not RSS/full resident assets/general leak proof'}
 candidate=@{changed_function='StableHasher::add(uint32_t) in src/entity_render/entity_scene_render.cpp';change='Hash low4 bytes then multiply by FNV prime^4 for original4 zero-padding bytes, exactly modulo2^64';source_arithmetic='8 to5 modular multiplications per uint32, not a hardware counter';new_owners_caches_allocations=0;float_hash_order_validation='unchanged';current_time_pose_sampling='unchanged';worker_commit_destructor_work_shift=$false;retained_frames='owning immutable original lifecycle'}
 oracle=(Get-Content -LiteralPath (Join-Path $oracle.FullName 'equivalence.json') -Raw|ConvertFrom-Json -AsHashtable)
 oracle_cases=@{ordered_records=1024;B0_assertions=6112;B1_assertions=6112;inputs='finite/signed-zero/subnormal/nearby/extreme patterns; NaN/Inf and identity/skin/pose/opacity/blend/entity errors;5 delayed readers';float_tolerances='not_changed'}
 screening=@{plan=Read-Record 'screen-plan.json';baseline_gates=Read-Record 'screen-baseline-gates.json';decision=Read-Record 'screen-decision.json';not_final_acceptance=$true}
 initial_campaign=@{plan=Read-Record 'final-cpu-plan.json';analysis=$initial;retained_strict_status='CPU_not_accepted';runs=48;warmed_samples=12288;reason_for_controlled_confirmation='Cold fixture temp path PID width, causally reproduced exactly6 calls/1988 bytes; not production allocation growth'}
 cold_causal_proof=Read-Record 'cold-path-causal-proof.json';controlled_path_proof=Read-Record 'controlled-root-length-proof.json'
 confirmation=@{plan=Read-Record 'controlled-cpu-plan.json';analysis=$confirmation;runs=48;warmed_samples=12288;prior_data_pooled=$false;gates_relaxed=$false;production_changed_for_confirmation=$false}
 acceptance_blocker=@{case='16actors/16history/RXevery4/DiagnosticsOn/commit_new_rx';metric='p99';B0_us=@(99.5,77.2,90.1);B1_us=@(157.9,103.3,225.6);paired_regression_us=@(58.4,26.1,135.5);gate_us=50.0;failed_pairs=2;required_rejection=$true;max_B0_us=@(107.3,82.0,115.2);max_B1_us=@(206.2,474.9,708.4);causal_attribution='not_established_not_assumed_noise';favorable_retry_after_blocker=$false}
 guard=@{timed_campaign_processes=96;busy_events=@($guards|ForEach-Object {$_.busy_events}).Count;maximum_poll_gap_ms=($guards.max_poll_gap_ms|Measure-Object -Maximum).Maximum;limitation='Polling cannot rule out a process wholly between polls or all OS load';foreign_processes_stopped=0;builds_during_timings=0}
 tests=@{fresh_post_withdrawal_Release=$tests;fresh_executed_cases=58;fresh_optional_skips=1;new_full_Release_Debug_suite='not_run_no_accepted_candidate';core_alternate_architecture_launcher='not_run_no_accepted_candidate';OpenGL_Null_MSAA_controls='not_run_CPU_gate_rejected';ASan_build='not_run';ASan_runtime='not_run';library_withdrawal_build_exit_code=0}
 setup_retries=@('Private profiling sandbox C1902; identical toolset/flags elevated retry passed; logs retained','Private aligned Header C4324 fixed with explicit Win32 padding; /WX unchanged','Screen wrapper == token repaired to -eq before0 timed runs','One controlled confirmation after exact cold-path cause proof; original48-run failure and all raw data preserved')
 deliveries=Read-Record 'final-delivery-audit.json';final_checks=Read-Record 'final-readonly-check.json';safety=@{staging_commit_push='none';branch_switch_reset_clean_stash_restore='none';new_live_gameplay_Steam_auth_HLDS_sessions=0;remote_execution=0;Windows_driver_power_server_settings='unchanged';settings_logs_auth_shared_test_data='untouched';new_user_test_requested=$false}
 scope_limits=@{CPU='Present/parent visual subset, not whole main update/render/swap';GPU='No new performance claim';target_PC='No measurement; share is not remote-execution permission';unclosed='Specific RX DiagnosticsOn commit p99; do not ship rejected signature fold as stable before causal closure'}
}
$destination=Join-Path $repo 'docs/evidence/CPU_STUDIO_POSE_FRAME_BUILD_50B5FFE1.json'
$report|ConvertTo-Json -Depth 40|Set-Content -LiteralPath $destination -Encoding utf8NoBOM
Identity $destination|ConvertTo-Json
