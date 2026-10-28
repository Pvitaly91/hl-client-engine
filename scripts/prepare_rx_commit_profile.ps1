#requires -Version 7
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$task=Join-Path $PSScriptRoot '../manual-artifacts/task-records/studio-rx-tail-causal-50b5ffe1-20261025'
$old=Join-Path $task '../studio-pose-frame-build-50b5ffe1-20261024'
foreach($variant in @('B0','B1')){
 $include=if($variant -eq 'B0'){Join-Path $task 'baseline/frozen/include'}else{Join-Path $task 'candidate-include'}
 $libs=if($variant -eq 'B0'){Join-Path $task 'baseline/Release-libs'}else{Join-Path $task 'candidate-libs'}
 & (Join-Path $task 'prepare-frozen-msaa4-fixture.ps1') -Variant $variant -TargetName "rx_profile_symbols_$($variant.ToLower())" -FrozenInclude $include -FrozenLibraries $libs -HarnessPath (Join-Path $task 'commit-profile/test_runtime_replay_local_assets.cpp') -HelperPath (Join-Path $task 'commit-profile/profile_allocation.cpp') -Defines @('HLCLIENT_HISTORY_COST_FIXTURE=1') -SupportRoot (Join-Path $old 'controlled-support') -ExtraLibraries @('Dbghelp.lib') -ProfileSymbols
}
