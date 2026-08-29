#requires -Version 5.1

<#
.SYNOPSIS
Checks the exact zero-stock-process, zero-write stock-runtime pending boundary.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
$verifier = Join-Path $PSScriptRoot 'verify_stock_runtime_state.ps1'
$evidence = Join-Path $repositoryRoot 'docs\evidence\GOLDSRC_STOCK_RUNTIME_STATE.json'
$manualRoot = Join-Path $repositoryRoot 'manual-artifacts\stock-runtime'

function Get-PathObservation {
    param([string]$Path)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if ($null -eq $item) { return 'absent' }
    if ($item.PSIsContainer) {
        return 'directory|' + $item.CreationTimeUtc.Ticks + '|' + $item.LastWriteTimeUtc.Ticks
    }
    return 'file|' + $item.Length + '|' + $item.CreationTimeUtc.Ticks + '|' +
        $item.LastWriteTimeUtc.Ticks + '|' +
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

$beforeEvidence = Get-PathObservation $evidence
$beforeManual = Get-PathObservation $manualRoot
$beforeGoldSrc = @(Get-Process -Name @('hl', 'hlds') -ErrorAction SilentlyContinue |
    ForEach-Object { $_.Id } | Sort-Object)
$output = @(& $verifier -ValidateEvidencePending | ForEach-Object { $_.ToString() })
$afterGoldSrc = @(Get-Process -Name @('hl', 'hlds') -ErrorAction SilentlyContinue |
    ForEach-Object { $_.Id } | Sort-Object)

$required = @(
    '[stock-runtime-verify] accepted-runs=0',
    '[stock-runtime-verify] client-version=not-observed',
    '[stock-runtime-verify] server-version-protocol-build=not-observed',
    '[stock-runtime-verify] restoration=not-run',
    '[stock-runtime-verify] runtime-ready=evidence_pending',
    '[stock-runtime-verify] stock-processes-started=0',
    '[stock-runtime-verify] files-written=0',
    '[stock-runtime-verify] result=evidence_pending')
foreach ($line in $required) {
    if ($output -cnotcontains $line) { throw "Pending verifier lacks '$line'." }
}
if ((Get-PathObservation $evidence) -cne $beforeEvidence -or
    (Get-PathObservation $manualRoot) -cne $beforeManual) {
    throw 'Pending verifier changed an evidence or raw-artifact path.'
}
if ((@($beforeGoldSrc) -join ',') -cne (@($afterGoldSrc) -join ',')) {
    throw 'Pending verifier changed the stock process set.'
}

Write-Output '[stock-runtime-pending-test] accepted-runs=0'
Write-Output '[stock-runtime-pending-test] files-written=0'
Write-Output '[stock-runtime-pending-test] result=success'
