#requires -Version 7
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$task=Join-Path $PSScriptRoot '../manual-artifacts/task-records/studio-rx-tail-causal-50b5ffe1-20261025'
. (Join-Path $task 'quality-common.ps1')
$old=Join-Path $task '../studio-pose-frame-build-50b5ffe1-20261024'
$root=New-QualityAttempt 'candidate-correctness'
$oracle=@{};$receipts=@()
foreach($variant in @('B0','B1')){
 $exe=Join-Path $old "isolated-fixtures/$variant/frame_cpu_$($variant.ToLower())/bin/frame_cpu_$($variant.ToLower()).exe"
 $expected=if($variant -eq 'B0'){'789851bd6e443c74d0d04fb6645c60a20b0853ff7da1bf1505bcfeb0e14300c9'}else{'4a50e21c99e254516b65e0722350afc4be03b03f399ea3a5ced55c7f811fe236'}
 if((Get-QualityHash $exe) -cne $expected){throw 'Frozen driver identity changed'}
 $receipts+=Invoke-QualityProcess -Name "$variant-oracle" -File $exe -Arguments @('[frame-hash-padding]','--reporter','console') -WorkingDirectory $script:QualityRepository -ResultRoot $root
 $oracle[$variant]=@(Get-Content -LiteralPath (Join-Path $root "$variant-oracle.stdout.log") | Where-Object {$_ -like '[[]frame-hash-oracle[]]*'})
 $junit=Join-Path $root "$variant-focused.junit.xml"
 $receipts+=Invoke-QualityProcess -Name "$variant-focused" -File $exe -Arguments @('[entity-render][frame]~[benchmark],[runtime-presentation]~[benchmark]','--reporter','junit','--out',$junit) -WorkingDirectory $script:QualityRepository -ResultRoot $root -Junit $junit -RequireCases
}
if($oracle.B0.Count -ne 1024 -or $oracle.B1.Count -ne 1024){throw 'Oracle count mismatch'}
for($i=0;$i -lt 1024;++$i){if($oracle.B0[$i] -cne $oracle.B1[$i]){throw "Oracle mismatch at $i"}}
[ordered]@{status='passed';ordered_records=1024;signatures_errors_order='exact';float_tolerances='unchanged';receipts=$receipts;live_sessions=0;candidate_library_sha256='9e0b12d11d38bf43999b96848ddbe4f8c68f1324f81c6a540f104715ad3c913c'}|ConvertTo-Json -Depth 14|Set-Content -LiteralPath (Join-Path $root 'equivalence.json')
$root
