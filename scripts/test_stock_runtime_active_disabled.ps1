#requires -Version 5.1

<#
.SYNOPSIS
Checks that every active stock-runtime request fails before launch or output.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
$capture = Join-Path $PSScriptRoot 'capture_stock_runtime_state.ps1'
$manualRoot = Join-Path $repositoryRoot 'manual-artifacts\stock-runtime'
$evidence = Join-Path $repositoryRoot 'docs\evidence\GOLDSRC_STOCK_RUNTIME_STATE.json'

function Get-PathObservation {
    param([string]$Path)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if ($null -eq $item) { return 'absent' }
    if (($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0) {
        return 'directory|' + $item.CreationTimeUtc.Ticks + '|' +
            $item.LastWriteTimeUtc.Ticks
    }
    return 'file|' + $item.Length + '|' + $item.CreationTimeUtc.Ticks + '|' +
        $item.LastWriteTimeUtc.Ticks + '|' +
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

$beforeManual = Get-PathObservation $manualRoot
$beforeEvidence = Get-PathObservation $evidence
$beforeStock = @(Get-Process -Name @('hl', 'hlds') -ErrorAction SilentlyContinue |
    ForEach-Object Id | Sort-Object)
$output = [Collections.Generic.List[string]]::new()
$threw = $false
$exceptionText = ''
try {
    & $capture `
        -ResearchHalfLifeRoot 'Z:\absent-stock-runtime-research' `
        -ClientPath 'Z:\absent-stock-runtime-research\hl.exe' `
        -HldsPath 'Z:\absent-stock-runtime-research\hlds.exe' `
        -CaptureToolPath 'Z:\absent-stock-runtime-research\capture.exe' `
        -Game valve -Map boot_camp -Scenario baseline |
        ForEach-Object { [void]$output.Add($_.ToString()) }
} catch {
    $threw = $true
    $exceptionText = $_.Exception.Message
}
$afterStock = @(Get-Process -Name @('hl', 'hlds') -ErrorAction SilentlyContinue |
    ForEach-Object Id | Sort-Object)

if (-not $threw -or
    $exceptionText -cnotmatch '^Active stock-runtime capture is evidence_pending') {
    throw 'Active stock-runtime mode did not fail with its explicit pending gate.'
}
foreach ($line in @(
        '[stock-runtime-capture] active-capture=evidence_pending',
        '[stock-runtime-capture] os-outbound-isolation=not-implemented',
        '[stock-runtime-capture] app-engine-protocol-build=not-observed',
        '[stock-runtime-capture] owned-processes-started=0',
        '[stock-runtime-capture] files-written=0')) {
    if ($output -cnotcontains $line) {
        throw "Active pending output lacks '$line'."
    }
}
if ((Get-PathObservation $manualRoot) -cne $beforeManual -or
    (Get-PathObservation $evidence) -cne $beforeEvidence) {
    throw 'Active pending gate changed an output path.'
}
if ((@($beforeStock) -join ',') -cne (@($afterStock) -join ',')) {
    throw 'Active pending gate changed the stock process set.'
}

Write-Output '[stock-runtime-active-disabled-test] processes-started=0'
Write-Output '[stock-runtime-active-disabled-test] files-written=0'
Write-Output '[stock-runtime-active-disabled-test] result=success'
