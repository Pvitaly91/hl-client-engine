[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ToolPath,

    [Parameter(Mandatory = $true)]
    [string]$AbsentRunRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$tool = [IO.Path]::GetFullPath($ToolPath)
if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
    throw "hlclient executable is absent: $tool"
}
$absent = [IO.Path]::GetFullPath($AbsentRunRoot)
if (Test-Path -LiteralPath $absent) {
    throw "The fail-closed capture fixture path unexpectedly exists: $absent"
}

$lines = @(& $tool --renderer null --runtime-replay-capture $absent 2>&1 |
    ForEach-Object { [string]$_ })
$exitCode = $LASTEXITCODE
$output = $lines -join "`n"
if ($exitCode -eq 0) {
    throw 'Missing functional capture unexpectedly returned success.'
}
if ($output -notmatch 'runtime_replay_capture error=corpus_load_failed') {
    throw 'Application did not report the typed capture-load boundary.'
}
if ($output -match 'Asset pipeline initialized|Half-Life game directory validated|\[net\]') {
    throw 'Capture-backed replay crossed a forbidden resource or network boundary.'
}

Write-Output '[runtime-replay-capture-cli] input=explicit-functional-capture'
Write-Output '[runtime-replay-capture-cli] missing-corpus=failed-closed'
Write-Output '[runtime-replay-capture-cli] network-resource-initialization=absent'
Write-Output '[runtime-replay-capture-cli] result=success'
