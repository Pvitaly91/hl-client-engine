#requires -Version 5.1

param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$CaptureScriptPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$capture = [IO.Path]::GetFullPath($CaptureScriptPath)
$repository = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $capture) '..'))
$outputRoot = Join-Path $repository `
    'manual-artifacts\stock-runtime-server-profile-private'
$beforeExists = Test-Path -LiteralPath $outputRoot
$beforeCount = if ($beforeExists) {
    @(Get-ChildItem -LiteralPath $outputRoot -Force).Count
} else { 0 }
$hostExecutable = [IO.Path]::GetFullPath((Get-Process -Id $PID).Path)
$previousPreference = $ErrorActionPreference
try {
    # Windows PowerShell 5.1 promotes a child native stderr record to a
    # non-terminating NativeCommandError. Capture that bounded record as part
    # of the expected denial without allowing host behavior to short-circuit
    # the mutation-free assertions below.
    $ErrorActionPreference = 'Continue'
    $lines = @(& $hostExecutable -NoLogo -NoProfile -NonInteractive `
        -ExecutionPolicy Bypass -File $capture `
        -PrivateDiagnoseServerProfile `
        -ConfirmPrivateDiagnostic 'HLCLIENT_PRIVATE_HLDS_BANNER_DIAGNOSTIC_WRONG' `
        -ResearchHalfLifeRoot 'Z:\not-resolved-research' `
        -ClientPath 'Z:\not-resolved-research\hl.exe' `
        -HldsPath 'Z:\not-resolved-research\hlds.exe' `
        -CaptureToolPath 'Z:\not-resolved-tool.exe' `
        -NetworkIsolationGuardPath 'Z:\not-resolved-guard.exe' `
        -AppManifestPath 'Z:\not-resolved-manifest.acf' `
        -Game valve -Map boot_camp `
        -OutputRoot $outputRoot 2>&1)
    $childExit = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $previousPreference
}
$afterExists = Test-Path -LiteralPath $outputRoot
$afterCount = if ($afterExists) {
    @(Get-ChildItem -LiteralPath $outputRoot -Force).Count
} else { 0 }
$text = $lines -join "`n"
if ($childExit -eq 0 -or
    $text -notmatch '\[stock-runtime-capture\] processes-started=0' -or
    $text -notmatch '\[stock-runtime-capture\] files-written=0' -or
    $text -notmatch '\[stock-runtime-capture\] wfp-sessions-started=0' -or
    $text -notmatch '\[stock-runtime-capture\] capture-runs-created=0' -or
    $beforeExists -ne $afterExists -or $beforeCount -ne $afterCount) {
    throw 'Private diagnostic token denial was not mutation-free.'
}
Write-Output '[stock-server-private-test] wrong-token=denied'
Write-Output '[stock-server-private-test] processes-started=0'
Write-Output '[stock-server-private-test] wfp-sessions-started=0'
Write-Output '[stock-server-private-test] files-written=0'
Write-Output '[stock-server-private-test] result=success'
