#requires -Version 7
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$task=Join-Path $repo 'manual-artifacts/task-records/studio-rx-tail-causal-50b5ffe1-20261025'
function ReadJson([string]$Relative){Get-Content -LiteralPath (Join-Path $task $Relative) -Raw|ConvertFrom-Json}
function Identity([string]$Path){$p=(Resolve-Path -LiteralPath $Path).Path;[ordered]@{path=[IO.Path]::GetRelativePath($repo,$p).Replace('\','/');sha256=(Get-FileHash -LiteralPath $p).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $p).Length}}
$legacy=ReadJson 'historical-rx-analysis.json';$aa=ReadJson 'aa-analysis.json';$longAA=ReadJson 'long-aa-analysis.json';$long=ReadJson 'long-analysis.json'
$correct=ReadJson 'candidate-correctness-20261024T221025889Z-7326a0/equivalence.json'
$profile=ReadJson 'commit-allocation-profile-20261024T222338448Z-d7a95c/profile-analysis.json'
$production=ReadJson 'production-unchanged.json';$delivery=ReadJson 'final-delivery-audit.json'
if(!$legacy.reject -or $aa.reject -or $longAA.reject -or $long.reject){throw 'Report assumptions do not match preserved analyses'}
if($production.changed -ne 0 -or $delivery.files.Count -ne 12 -or !$profile.exact_sites_calls_bytes_peak -or $correct.status -cne 'passed'){throw 'Missing source/delivery/correctness/profile evidence'}
$paths=@('baseline/receipt.json','baseline/source-manifest.json','baseline/library-manifest.json','baseline/binary-manifest.json','B0-long-inputs.json','B1-long-inputs.json','aa-plan.json','aa-progress.json','aa-analysis.json','historical-rx-analysis.json','long-plan.json','long-progress.json','long-aa-analysis.json','long-analysis.json','production-unchanged.json','final-delivery-audit.json','candidate-correctness-20261024T221025889Z-7326a0/equivalence.json','commit-allocation-profile-20261024T221819549Z-898b1b/profile-analysis.json','commit-allocation-profile-20261024T222338448Z-d7a95c/profile-analysis.json')
$identities=@($paths|ForEach-Object{Identity (Join-Path $task $_)})
$attempts=@(Get-ChildItem -LiteralPath $task -Directory|Where-Object {$_.Name -match '^(aa-|long-(AA|RXOff|RXOn|PrimaryOff)-)'}|ForEach-Object {
 $guardPath=Join-Path $_.FullName 'host-guard.json';$resultPath=Join-Path $_.FullName 'result.json';$guard=Get-Content -LiteralPath $guardPath -Raw|ConvertFrom-Json
 $ids=@(Identity $guardPath);$result=$null
 if(Test-Path -LiteralPath $resultPath){$result=Get-Content -LiteralPath $resultPath -Raw|ConvertFrom-Json;$ids+=Identity $resultPath;$ids+=Identity (Join-Path $_.FullName 'driver.stdout.log');$ids+=Identity (Join-Path $_.FullName 'driver.stderr.log')}
 @{directory=$_.Name;measured=($null -ne $result);eligible=($null -ne $result -and $guard.busy_events.Count -eq 0 -and $result.exit_code -eq 0);busy_observations=$guard.busy_events.Count;identities=$ids}
})
$drivers=@(foreach($variant in @('B0','B1')){foreach($kind in @('rx_long_','rx_profile_symbols_')){$name=$kind+$variant.ToLower();Identity (Join-Path $task "isolated-fixtures/$variant/$name/bin/$name.exe");Identity (Join-Path $task "isolated-fixtures/$variant/$name/fixture-project.json")}})
$record=[ordered]@{
 schema='hlclient.studio-rx-tail-causal.50b5ffe1.v1';created_utc=[DateTime]::UtcNow.ToString('o');
 outcome='C_causal_unresolved_B0_preserved';production_fix='rejected_and_withdrawn';
 explanation='Original strict p99 rejection is retained. Neither identical-hash A/A rejected B0. The one declared1024-RX method passes CPU gates including release but cannot identify the operation responsible for the original64-event excess or prove that gate failure was noise. No accepted production change/new EXE.';
 previous_candidate='Exact FNV uint32 padding fold only; not Euler/quaternion or history candidate';
 history=@{original_rx_comparisons=$legacy.comparisons;original_rx_gate=$legacy.gates;original_rejected=$legacy.reject};
 original_AA=@{processes=6;reject=$aa.reject;comparisons=$aa.comparisons;gates=$aa.gates};
 long_AA=@{processes=6;reject=$longAA.reject;summary=$longAA.summary};
 bounded_new_method=@{processes=24;pairs=3;new_RX_per_run=1024;no_RX_per_RX_run=3072;reject=$long.reject;summary=$long.summary;memory=$long.memory;lifecycle_subset=$long.lifecycle;witnesses=$long.bounded_long_event_witnesses;full_frame='not_measured';sampling_independence='Only3 process pairs/condition; adjacent events correlated';claim='CPU method gates passed, not causal closure/production acceptance'};
 correctness=$correct;commit_profile=@{exact_sites_calls_bytes_peak=$profile.exact_sites_calls_bytes_peak;summaries=$profile.summaries;sites=$profile.sites;timing_eligible=$false;limitation='Expanded allocation header, stack capture and PDB perturb profiling; requested live bytes not RSS; optimized inline frames can resolve to nearest public symbols'};
 method_fix='Private harness: add explicit local owning-frame release and same-event nonoverlapping parent+release sum; preserve old scopes, separate cold preparation and teardown subset';
 unresolved='Wall duration inside observe_entities owning stage.87 allocations/279318 bytes/87 frees, including unchanged239760-byte follow owner clone; no extra site/calls/bytes/peak in B1. Original events have no operation traces; no scheduler/allocator/driver cause asserted.';
 active_source=$production;delivery=$delivery;drivers=$drivers;artifacts=$identities;timing_attempts=$attempts;
 verification=@{focused_release_B0=@{executed=39;skipped=1;failures=0};focused_release_B1=@{executed=39;skipped=1;failures=0};independent_oracle=@{ordered_records=1024;assertions_each=6112;exact=$true};full_release='not_run_no_accepted_production_fix';Debug='not_run';core_OFF='not_run';alternate_module='not_run';architecture_guard='not_run';OpenGL_Null_GL33='not_run';launcher_preflight='not_run';MSAA_regression_matrix='not_run';ASan_build='not_run';ASan_runtime='not_run';git_diff_check=@{sandbox='blocked_by_submodule_metadata_access_exit128';read_only_escalated_retry='passed_exit0'};setup_failures='One private profiling build:missing<array>, source/build logs retained; then numeric-stack profile retained, symbolized link/run separate after timings'};
 safety=@{staging_commit_push='none';branch_reset_clean_restore_stash='none';live_gameplay_Steam_auth_HLDS_sessions=0;remote_execution=0;settings_auth_logs_resources_shared_test_data='untouched';new_EXE_delivered=$false;manual_acceptance='not_run';new_user_test_B0_requested=$false};
 hardware_limits=@{target_Win10='not_run';MSAA4_total_frame_gain='not_measured';target100FPS='unproven';no_quality_or_frequency_reduction=$true}
}
$output=Join-Path $task 'rx-tail-investigation.json';$record|ConvertTo-Json -Depth 32|Set-Content -LiteralPath $output
Identity $output|ConvertTo-Json -Depth 5
