#requires -Version 7
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$task=Join-Path $PSScriptRoot '../manual-artifacts/task-records/studio-rx-tail-causal-50b5ffe1-20261025'
$old=Join-Path $task '../studio-pose-frame-build-50b5ffe1-20261024'
$receipts=[Collections.Generic.List[object]]::new()
foreach($variant in @('B0','B1')){
 $include=if($variant -eq 'B0'){Join-Path $task 'baseline/frozen/include'}else{Join-Path $task 'candidate-include'}
 $libs=if($variant -eq 'B0'){Join-Path $task 'baseline/Release-libs'}else{Join-Path $task 'candidate-libs'}
 $manifest=foreach($entry in @(Get-Content -LiteralPath (Join-Path $task 'baseline/library-manifest.json') -Raw|ConvertFrom-Json)){
  $name=[IO.Path]::GetFileName($entry.path);$path=Join-Path $libs $name;$hash=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()
  $expected=if($variant -eq 'B1' -and $name -ceq 'hlclient_entity_scene_render.lib'){'9e0b12d11d38bf43999b96848ddbe4f8c68f1324f81c6a540f104715ad3c913c'}else{$entry.sha256}
  if($hash -cne $expected){throw "Library not frozen: $variant/$name"}
  @{path=$path;sha256=$hash;bytes=(Get-Item -LiteralPath $path).Length}
 }
 $headers=foreach($entry in @(Get-Content -LiteralPath (Join-Path $task 'baseline/source-manifest.json') -Raw|ConvertFrom-Json|Where-Object {$_.path -like 'include/*'})){
  $path=Join-Path $include $entry.path.Substring(8);$hash=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()
  if($hash -cne $entry.sha256){throw "Header not frozen: $variant/$path"};@{path=$path;sha256=$hash}
 }
 @{variant=$variant;libraries=$manifest;headers=$headers}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $task "$variant-long-inputs.json")
 & (Join-Path $task 'prepare-frozen-msaa4-fixture.ps1') -Variant $variant -TargetName "rx_long_$($variant.ToLower())" -FrozenInclude $include -FrozenLibraries $libs -HarnessPath (Join-Path $task 'long-harness/test_runtime_replay_local_assets.cpp') -HelperPath (Join-Path $task 'long-harness/history_projection_allocation.cpp') -Defines @('HLCLIENT_HISTORY_COST_FIXTURE=1') -SupportRoot (Join-Path $old 'controlled-support')
}
