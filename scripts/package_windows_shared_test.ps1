#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ReleaseDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$BasePackageDirectory=(Join-Path $PSScriptRoot '..\dist\HLClientEngine')
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=[IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
$release=[IO.Path]::GetFullPath($ReleaseDirectory).TrimEnd('\')
$base=[IO.Path]::GetFullPath($BasePackageDirectory).TrimEnd('\')
if(Test-Path -LiteralPath $output){throw 'Use a fresh output directory; preserve existing test logs.'}
foreach($required in @('HLClientLauncher.exe','hlclient.exe','SDL3.dll','HLClientSystemDiagnostics.exe')){
    if(!(Test-Path -LiteralPath (Join-Path $release $required) -PathType Leaf)){throw "Missing Release input: $required"}
}
New-Item -ItemType Directory -Path $output | Out-Null
$baseArchive=(Get-FileHash -LiteralPath (Join-Path $base 'SHA256SUMS.txt') -Algorithm SHA256).Hash
$leaves=@('HLClientLauncher.exe','hlclient.exe','SDL3.dll','HLClientSystemDiagnostics.exe')
foreach($source in (Get-ChildItem -LiteralPath $base -Recurse -File)){
    $relative=[IO.Path]::GetRelativePath($base,$source.FullName)
    if($relative -in @('BUILD_INFO.json','SHA256SUMS.txt','START_HERE_UK.txt') -or $relative -in $leaves){continue}
    $target=Join-Path $output $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $source.FullName -Destination $target
}
foreach($leaf in $leaves){Copy-Item -LiteralPath (Join-Path $release $leaf) -Destination (Join-Path $output $leaf)}
foreach($leaf in @('HLClientSharedTest.txt','SHARED_TEST_UK.txt')){
    Copy-Item -LiteralPath (Join-Path $repository "packaging\windows10\$leaf") -Destination (Join-Path $output $leaf)
}
$identities=@(Get-ChildItem -LiteralPath $output -Recurse -File | Sort-Object FullName | ForEach-Object {
    [ordered]@{path=[IO.Path]::GetRelativePath($output,$_.FullName).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
$metadata=[ordered]@{
    schema='hlclient.shared-windows-test-package.v1';created_utc=[DateTime]::UtcNow.ToString('o')
    git_head=(& git -C $repository rev-parse HEAD);working_tree_candidate=$true;gameplay_acceptance=$false
    original_portable_manifest_sha256=$baseArchive
    entrypoint='HLClientLauncher.exe';storage='test-data/<computer>-<user>'
    windows10_runtime='awaiting_user_test';credentials_included=$false
    requirements=@('Windows 10 x64/WOW64','SSE2','Vendor OpenGL 3.3 Core driver','Local Steam Half-Life')
    files=$identities
}
$utf8=[Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $output 'BUILD_INFO.json'),($metadata|ConvertTo-Json -Depth 12),$utf8)
$sums=@(Get-ChildItem -LiteralPath $output -Recurse -File | Sort-Object FullName | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash,[IO.Path]::GetRelativePath($output,$_.FullName).Replace('\','/')
})
[IO.File]::WriteAllText((Join-Path $output 'SHA256SUMS.txt'),(($sums -join "`n")+"`n"),$utf8)
[pscustomobject]@{package=$output;files=@(Get-ChildItem -LiteralPath $output -Recurse -File).Count;client_sha256=(Get-FileHash -LiteralPath (Join-Path $output 'hlclient.exe')).Hash;launcher_sha256=(Get-FileHash -LiteralPath (Join-Path $output 'HLClientLauncher.exe')).Hash}
