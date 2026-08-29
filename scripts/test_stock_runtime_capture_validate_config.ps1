#requires -Version 5.1

<#
.SYNOPSIS
Checks the capture executable's zero-I/O --validate-config contract.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$CaptureToolPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
$tool = [IO.Path]::GetFullPath($CaptureToolPath)
$manualRoot = Join-Path $repositoryRoot 'manual-artifacts\stock-runtime'
$evidence = Join-Path $repositoryRoot 'docs\evidence\GOLDSRC_STOCK_RUNTIME_STATE.json'
if (-not (Test-Path -LiteralPath $tool -PathType Leaf) -or
    [IO.Path]::GetFileName($tool) -cne 'hlclient_stock_runtime_capture.exe' -or
    -not $tool.StartsWith(
        $repositoryRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'CaptureToolPath must name the repository-built capture executable.'
}

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

$beforeManual = Get-PathObservation $manualRoot
$beforeEvidence = Get-PathObservation $evidence
$output = @(& $tool --validate-config 2>&1 | ForEach-Object { $_.ToString() })
if ($LASTEXITCODE -ne 0) { throw "Capture config validation exited $LASTEXITCODE." }
$required = @(
    '[stock-runtime-capture] configuration=valid',
    '[stock-runtime-capture] sockets-opened=0',
    '[stock-runtime-capture] files-written=0',
    '[stock-runtime-capture] processes-started=0',
    '[stock-runtime-capture] result=success')
foreach ($line in $required) {
    if ($output -cnotcontains $line) { throw "Capture validator lacks '$line'." }
}
if ((Get-PathObservation $manualRoot) -cne $beforeManual -or
    (Get-PathObservation $evidence) -cne $beforeEvidence) {
    throw 'Capture config validation changed an output path.'
}

$savedErrorActionPreference = $ErrorActionPreference
try {
    # The rejected native invocation writes its usage line to stderr. Windows
    # PowerShell 5.1 wraps that line as a non-terminating NativeCommandError;
    # keep it in the asserted output instead of turning the expected failure
    # into a script-level terminating error.
    $ErrorActionPreference = 'Continue'
    $invalidOutput = @(& $tool --validate-config --validate-config 2>&1 |
        ForEach-Object { $_.ToString() })
    $invalidExitCode = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $savedErrorActionPreference
}
if ($invalidExitCode -ne 2 -or
    $invalidOutput -notcontains 'Usage: hlclient_stock_runtime_capture --validate-config [limit options]') {
    throw 'Capture validator did not reject a duplicate option with exit code 2.'
}

Write-Output '[stock-runtime-capture-cli-test] sockets-opened=0'
Write-Output '[stock-runtime-capture-cli-test] files-written=0'
Write-Output '[stock-runtime-capture-cli-test] result=success'
