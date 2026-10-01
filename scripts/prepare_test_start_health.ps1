#requires -Version 7.0
[CmdletBinding()]
param([string]$CMake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$deps=Join-Path $root 'build/test-start-health-deps'
[void][IO.Directory]::CreateDirectory($deps)
$asset=Join-Path $deps 'metamod-p-v1.21p109-win32.tar.xz'
$url='https://github.com/Bots-United/metamod-p/releases/download/v1.21p109/metamod-p-v1.21p109-win32.tar.xz'
$sha='564838D9A42032EE3BC44EEB2616896E0789C3CE8D7A033D81F3F795B67E31ED'
if (-not (Test-Path -LiteralPath $asset)) { Invoke-WebRequest $url -OutFile $asset }
if ((Get-FileHash -LiteralPath $asset).Hash -cne $sha) { throw 'Official Metamod release SHA256 mismatch' }
& tar -xf $asset -C $deps
if ($LASTEXITCODE -ne 0) { throw 'Metamod extraction failed' }
$build=Join-Path $root 'build/test-start-health'
& $CMake -S (Join-Path $root 'test_server/start_health') -B $build -G 'Visual Studio 17 2022' -A Win32
if ($LASTEXITCODE -ne 0) { throw 'Test addon configure failed' }
foreach ($config in @('Debug','Release')) {
    & $CMake --build $build --config $config --parallel 2
    if ($LASTEXITCODE -ne 0) { throw 'Test addon build failed' }
    & (Join-Path $build "$config/start_health_offline_test.exe") (Join-Path $build "$config/hlclient_test_start_health.dll")
    if ($LASTEXITCODE -ne 0) { throw 'Test addon fake-engine regression failed' }
}
$sources=@(foreach ($relative in @('CMakeLists.txt','plugin.cpp','exports.def','offline_test.cpp')) {
    $path="test_server/start_health/$relative"
    @{path=$path;sha256=(Get-FileHash -LiteralPath (Join-Path $root $path)).Hash}
})
@{schema='hlclient.test-start-health-components.v1';sources=$sources
  helper_sha256=(Get-FileHash (Join-Path $build 'Release/hlclient_test_start_health.dll')).Hash
  upstream_version='v1.21p109';upstream_commit='dc4f6d8d6271658268a19e22cbc88706f9c1c78e'
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $build 'verified-components.json')
Import-Module (Join-Path $PSScriptRoot 'test_start_health_profile.psm1') -Force
[void](Get-TestStartHealthComponents $root)
Write-Host 'Test server components verified offline. Research/Steam installations unchanged; live=not_run.'
