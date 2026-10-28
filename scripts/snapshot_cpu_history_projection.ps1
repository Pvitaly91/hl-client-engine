#requires -Version 7.0
[CmdletBinding()]
param([string]$TaskDirectory='D:/DEV/CPP/HL-Client-engine/manual-artifacts/task-records/cpu-history-projection-50b5ffe1-20261024')
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo='D:/DEV/CPP/HL-Client-engine'
$task=[IO.Path]::GetFullPath($TaskDirectory)
if(!$task.StartsWith([IO.Path]::GetFullPath((Join-Path $repo 'manual-artifacts/task-records/')),[StringComparison]::OrdinalIgnoreCase)){throw 'Task must be confined to task-records'}
if(Test-Path -LiteralPath $task){throw 'Preserve existing task; no snapshot overwrite'}
$null=New-Item -ItemType Directory -Path $task
$base=Join-Path $task 'baseline';$null=New-Item -ItemType Directory -Path (Join-Path $base 'frozen')
$roots=@('apps','include','src','tests','scripts','packaging','cmake')|Where-Object {Test-Path -LiteralPath (Join-Path $repo $_)}
$paths=@(& rg --files @roots)+@('CMakeLists.txt','AGENTS.md')
if($LASTEXITCODE -ne 0){throw 'Source discovery failed'}
$source=@(foreach($relative in @($paths|Sort-Object -Unique)) {
 $file=Get-Item -LiteralPath (Join-Path $repo $relative)
 if($file.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Snapshot cannot follow reparse source'}
 $destination=Join-Path $base ('frozen/'+$relative)
 $null=New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force
 Copy-Item -LiteralPath $file.FullName -Destination $destination
 $hash=(Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant()
 if((Get-FileHash -LiteralPath $destination).Hash -ine $hash){throw 'Source snapshot changed during copy'}
 [ordered]@{path=$relative.Replace('\','/');sha256=$hash;bytes=$file.Length}
})
$source|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $base 'source-manifest.json') -Encoding utf8
$libRoot=Join-Path $base 'Release-libs';$null=New-Item -ItemType Directory -Path $libRoot
$libraries=@(foreach($file in Get-ChildItem -LiteralPath (Join-Path $repo 'build/lib/Release') -File -Filter '*.lib') {
 Copy-Item -LiteralPath $file.FullName -Destination $libRoot
 [ordered]@{path=$file.Name;sha256=(Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant();bytes=$file.Length}
})
$libraries|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $base 'library-manifest.json') -Encoding utf8
$binRoot=Join-Path $base 'Release-binaries';$null=New-Item -ItemType Directory -Path $binRoot
$binaries=@(foreach($name in @('hlclient.exe','HLClientLauncher.exe','HLClientSystemDiagnostics.exe','SDL3.dll')) {
 $file=Get-Item -LiteralPath (Join-Path $repo ('build/bin/Release/'+$name))
 Copy-Item -LiteralPath $file.FullName -Destination $binRoot
 [ordered]@{path=$name;sha256=(Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant();bytes=$file.Length}
})
$binaries|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $base 'binary-manifest.json') -Encoding utf8
$diffOutput=Join-Path $base 'working-tree.diff'
& git -C $repo diff --binary --ignore-submodules=all "--output=$diffOutput"
if($LASTEXITCODE -ne 0){throw 'Diff snapshot failed'}
& git -C $repo status --short --branch --ignore-submodules=all|Set-Content -LiteralPath (Join-Path $base 'git-status.txt') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $repo 'build/CMakeCache.txt') -Destination (Join-Path $base 'CMakeCache.txt')
[ordered]@{schema='hlclient.cpu-history-projection.baseline.v1';created_utc=[DateTime]::UtcNow.ToString('o');head=(& git -C $repo rev-parse HEAD).Trim();branch=(& git -C $repo branch --show-current).Trim();source_files=$source.Count;libraries=$libraries.Count;binaries=$binaries;normal_client_is_unchanged_B0=$true;last_full_test='Historical4190passed75skipped, originalB0, not a current-task test pass';new_live_sessions=0;git_mutations='none'}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $base 'receipt.json') -Encoding utf8
Write-Output $task
