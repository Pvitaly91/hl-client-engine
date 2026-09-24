#requires -Version 7.0
<# .SYNOPSIS
Restore pinned sources, build the existing Win32 preset and run offline tests.
No game installation, private corpus, Steam or elevated rights are required.
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [ValidateRange(1, 32)][int]$Parallel = 4,
    [switch]$TestOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $IsWindows) { throw 'The reference build requires Windows and Visual Studio 2022 C++.' }
$root = Split-Path -Parent $PSScriptRoot
$cmake = Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue
if ($null -eq $cmake) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install VS2022 Desktop development with C++, CMake tools, and a Windows SDK.' }
    $installation = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'VS2022 C++ toolchain not found.' }
    $cmakePath = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
} else { $cmakePath = $cmake.Source }
if (-not (Test-Path -LiteralPath $cmakePath)) { throw 'Install CMake 3.25+ or Visual Studio CMake tools and expose cmake on PATH.' }
$ctest = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed (exit $LASTEXITCODE)." }
}
$previousWfp = $env:HLCLIENT_RUN_WFP_CAPABILITY_TEST
try {
    Push-Location -LiteralPath $root
    # The only live-capability opt-in in the registered tests must stay disabled.
    $env:HLCLIENT_RUN_WFP_CAPABILITY_TEST = '0'
    if (-not $TestOnly) {
        Invoke-Checked git @('-c', "safe.directory=$root", '-c', "safe.directory=$root/third_party/halflife-sdk", 'submodule', 'update', '--init', '--recursive')
        Invoke-Checked $cmakePath @('--preset', 'vs2022-win32')
        Invoke-Checked $cmakePath @('--build', 'build', '--config', $Configuration, '--parallel', [string]$Parallel)
    }
    # Audited registration: own fixtures/local peers, no stock/live CLI campaign.
    # WFP opt-in tests report capability skips, never count as successful runs.
    Invoke-Checked $ctest @('--test-dir', 'build', '-C', $Configuration, '--output-on-failure', '--parallel', [string]$Parallel)
    $bin = Join-Path $root "build\bin\$Configuration"
    Invoke-Checked (Join-Path $bin 'hlclient.exe') @('--version')
    Invoke-Checked (Join-Path $bin 'hlclient.exe') @('--help')
    Invoke-Checked (Join-Path $bin 'hlclient.exe') @('--renderer', 'null')
    Invoke-Checked (Join-Path $bin 'hlclient_client_move_check.exe') @('--self-test-transport')
    Invoke-Checked (Join-Path $bin 'hlclient_stock_runtime_orchestrator.exe') @('--validate-functional-log-observation')
    foreach ($test in @('test_g_jump_duck_summary.ps1', 'test_h1_speed_summary.ps1')) {
        Invoke-Checked (Join-Path $PSHOME 'pwsh.exe') @('-NoProfile', '-File', (Join-Path $PSScriptRoot $test))
    }
    if ($Configuration -eq 'Release') {
        Invoke-Checked (Join-Path $PSHOME 'pwsh.exe') @('-NoProfile', '-File', (Join-Path $PSScriptRoot 'test_h2_manual_launch.ps1'))
        Invoke-Checked (Join-Path $PSHOME 'pwsh.exe') @('-NoProfile', '-File', (Join-Path $root 'Start-HLClient-H2-Manual.ps1'), '-CheckOnly', '-SourceOnly')
    }
    Write-Host "Source build/offline verification passed: $Configuration. External game/live validation NOT RUN."
} finally {
    Pop-Location
    $env:HLCLIENT_RUN_WFP_CAPABILITY_TEST = $previousWfp
}
