#requires -Version 5.1

<#
.SYNOPSIS
Exercises the stock-runtime evidence threshold with fake observations.

.DESCRIPTION
Runs in-memory threshold tests, the verifier's pure policy self-test and
disposable bounded-reader fixtures. It proves the 24/4/1000/26 gate without
creating raw artifacts or evidence.
#>
[CmdletBinding()]
param(
    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]$TestExecutable = '.\build\bin\Debug\hlclient_tests.exe'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
$testPath = [IO.Path]::GetFullPath($TestExecutable)
$verifierPath = Join-Path $PSScriptRoot 'verify_stock_runtime_first_observations.ps1'
$manualRoot = Join-Path $repositoryRoot 'manual-artifacts\stock-runtime'
$evidencePath = Join-Path $repositoryRoot `
    'docs\evidence\GOLDSRC_STOCK_RUNTIME_FIRST_OBSERVATIONS.json'

function Get-PathObservation {
    param([string]$Path)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if ($null -eq $item) { return 'absent' }
    if ($item.PSIsContainer) {
        return 'directory|' + $item.CreationTimeUtc.Ticks + '|' +
            $item.LastWriteTimeUtc.Ticks
    }
    return 'file|' + $item.Length + '|' +
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

if (-not $testPath.StartsWith(
        $repositoryRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase) -or
    -not (Test-Path -LiteralPath $testPath -PathType Leaf) -or
    [IO.Path]::GetFileName($testPath) -cne 'hlclient_tests.exe') {
    throw 'TestExecutable must name the repository-built hlclient_tests.exe.'
}
if (-not (Test-Path -LiteralPath $verifierPath -PathType Leaf)) {
    throw 'First-observation verifier is absent.'
}

$beforeManual = Get-PathObservation $manualRoot
$beforeEvidence = Get-PathObservation $evidencePath
$saved = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    $lines = @(& $testPath '[goldsrc][stock-runtime][campaign][threshold]' `
            '--reporter' 'compact' 2>&1 | ForEach-Object { $_.ToString() })
    $exitCode = $LASTEXITCODE
} finally { $ErrorActionPreference = $saved }
if ($exitCode -ne 0) {
    throw "Fake campaign threshold tests failed: $($lines -join ' ')"
}
$policyLines = @(& $verifierPath -ValidateEvidencePolicy |
    ForEach-Object { $_.ToString() })
if ($policyLines -cnotcontains '[stock-runtime-evidence-policy] files-written=0' -or
    $policyLines -cnotcontains
        '[stock-runtime-evidence-policy] implementation-commit-chain=exact-message-and-ancestor' -or
    $policyLines -cnotcontains
        '[stock-runtime-evidence-policy] failure-publication-mutations=3' -or
    $policyLines -cnotcontains
        '[stock-runtime-evidence-policy] campaign-identity-rejections=2' -or
    $policyLines -cnotcontains
        '[stock-runtime-evidence-policy] external-target-metadata-acceptances=2' -or
    $policyLines -cnotcontains
        '[stock-runtime-evidence-policy] external-target-metadata-rejections=4' -or
    $policyLines -cnotcontains
        '[stock-runtime-evidence-policy] canary-mutation-rejections=4' -or
    $policyLines -cnotcontains
        '[stock-runtime-evidence-policy] fatal-resume-category-rejections=5' -or
    $policyLines -cnotcontains
        '[stock-runtime-evidence-policy] fatal-resume-state-rejections=1' -or
    $policyLines -cnotcontains
        '[stock-runtime-evidence-policy] canary-walker-invocations=2' -or
    $policyLines -cnotcontains
        '[stock-runtime-evidence-policy] canary-walker-gate-rejections=3' -or
    $policyLines -cnotcontains
        '[stock-runtime-evidence-policy] rejected-overflow-candidate-binding-rejections=1' -or
    $policyLines -cnotcontains '[stock-runtime-evidence-policy] result=success') {
    throw 'Evidence policy self-test did not retain its zero-write contract.'
}
$readerLines = @(& $verifierPath -ValidateBoundedReadPolicy |
    ForEach-Object { $_.ToString() })
$requiredReaderLines = @(
    '[stock-runtime-bounded-evidence-reader] ordinary-acceptances=1',
    '[stock-runtime-bounded-evidence-reader] oversized-rejections=1',
    '[stock-runtime-bounded-evidence-reader] sparse-rejections=1',
    '[stock-runtime-bounded-evidence-reader] reparse-rejections=1',
    '[stock-runtime-bounded-evidence-reader] hardlink-rejections=1',
    '[stock-runtime-bounded-evidence-reader] cleanup=exact',
    '[stock-runtime-bounded-evidence-reader] result=success')
if ($readerLines.Count -ne $requiredReaderLines.Count -or
    @($requiredReaderLines | Where-Object {
            $readerLines -cnotcontains $_
        }).Count -ne 0) {
    throw 'Bounded evidence reader fixtures did not retain their exact contract.'
}
if ((Get-PathObservation $manualRoot) -cne $beforeManual -or
    (Get-PathObservation $evidencePath) -cne $beforeEvidence) {
    throw 'Fake threshold tests changed capture artifacts or evidence.'
}

Write-Output '[stock-runtime-evidence-threshold-test] corpus=fake-in-memory'
Write-Output '[stock-runtime-evidence-threshold-test] pre-campaign-canary-required=true'
Write-Output '[stock-runtime-evidence-threshold-test] accepted-runs-minimum=24'
Write-Output '[stock-runtime-evidence-threshold-test] reconnect-generations-minimum=4'
Write-Output '[stock-runtime-evidence-threshold-test] sequenced-s2c-minimum=1000'
Write-Output '[stock-runtime-evidence-threshold-test] boundaries-minimum=26'
Write-Output '[stock-runtime-evidence-threshold-test] candidates-minimum=26'
Write-Output '[stock-runtime-evidence-threshold-test] bounded-reader-fixtures=5'
Write-Output '[stock-runtime-evidence-threshold-test] evidence-written=0'
Write-Output '[stock-runtime-evidence-threshold-test] result=success'
