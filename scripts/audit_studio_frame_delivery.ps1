#requires -Version 7
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$task=Join-Path $repo 'manual-artifacts/task-records/studio-pose-frame-build-50b5ffe1-20261024'
$expected=Get-Content (Join-Path $task 'baseline/binary-manifest.json') -Raw|ConvertFrom-Json
$rows=@(foreach($root in @((Join-Path $repo 'build/bin/Release'),(Join-Path $repo 'dist/HLClientEngine'),'\\192.168.0.104\storage\HLClient-Win10-Test')){
 foreach($entry in $expected){
  $path=Join-Path $root $entry.path;$f=Get-Item -LiteralPath $path -ErrorAction Stop;$hash=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()
  if($hash -cne $entry.sha256 -or $f.Length -ne $entry.bytes){throw "Unexpected active bytes: $path"}
  [ordered]@{path=$path;sha256=$hash;bytes=$f.Length}
 }
})
$zip=Join-Path $repo 'dist/HLClientEngine-Win10.zip';$hash=(Get-FileHash -LiteralPath $zip).Hash.ToLowerInvariant()
if($hash -cne '67b9b79ae631b34efb747cbc42f7f144fb6eaab5b1253da190007e9e66515412'){throw 'Canonical ZIP changed'}
[ordered]@{status='all12_binaries_and_ZIP_exact_actual_B0';utc=[DateTime]::UtcNow.ToString('o');files=$rows;zip=@{sha256=$hash;bytes=(Get-Item -LiteralPath $zip).Length};writes=0;new_client_delivered=$false;settings_logs_auth_test_data='untouched'}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $task 'final-delivery-audit.json')
'All12 shipped files and canonical ZIP unchanged'
