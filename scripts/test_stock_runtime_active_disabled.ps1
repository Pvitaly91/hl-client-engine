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
$evidence = Join-Path $repositoryRoot `
    'docs\evidence\GOLDSRC_STOCK_RUNTIME_FIRST_OBSERVATIONS.json'

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
$ownedNames = @(
    'hl', 'hlds', 'hlclient_stock_runtime_capture',
    'hlclient_stock_runtime_orchestrator', 'hlclient_stock_runtime_isolation_guard')
$beforeStock = @(Get-Process -Name $ownedNames -ErrorAction SilentlyContinue |
    ForEach-Object Id | Sort-Object)
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$beforeBackups = @(Get-ChildItem -LiteralPath $temporaryRoot -Force -Directory `
    -Filter 'hlclient-stock-runtime-restore-*' -ErrorAction SilentlyContinue |
    ForEach-Object Name | Sort-Object)
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
$afterStock = @(Get-Process -Name $ownedNames -ErrorAction SilentlyContinue |
    ForEach-Object Id | Sort-Object)
$afterBackups = @(Get-ChildItem -LiteralPath $temporaryRoot -Force -Directory `
    -Filter 'hlclient-stock-runtime-restore-*' -ErrorAction SilentlyContinue |
    ForEach-Object Name | Sort-Object)

if (-not $threw -or
    $exceptionText -cnotmatch '^Active stock-runtime capture requires the exact explicit confirmation token') {
    throw 'Active stock-runtime mode did not fail with its explicit opt-in gate.'
}
foreach ($line in @(
        '[stock-runtime-capture] active-capture=explicit-opt-in-required',
        '[stock-runtime-capture] processes-started=0',
        '[stock-runtime-capture] files-written=0',
        '[stock-runtime-capture] network-operations=0',
        '[stock-runtime-capture] wfp-sessions-started=0',
        '[stock-runtime-capture] capture-runs-created=0',
        '[stock-runtime-capture] restoration-backups-created=0')) {
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
if ((@($beforeBackups) -join ',') -cne (@($afterBackups) -join ',')) {
    throw 'Active opt-in gate created a restoration backup.'
}

Write-Output '[stock-runtime-active-disabled-test] processes-started=0'
Write-Output '[stock-runtime-active-disabled-test] files-written=0'
Write-Output '[stock-runtime-active-disabled-test] network-operations=0'
Write-Output '[stock-runtime-active-disabled-test] wfp-sessions-started=0'
Write-Output '[stock-runtime-active-disabled-test] restoration-backups-created=0'
Write-Output '[stock-runtime-active-disabled-test] result=success'
