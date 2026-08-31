#requires -Version 5.1

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$approve = Join-Path $PSScriptRoot 'approve_stock_runtime_external_targets.ps1'
$fixture = Join-Path ([IO.Path]::GetTempPath()) (
    'hlclient-external-approval-test-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($fixture) | Out-Null
try {
    $fixtureScripts = Join-Path $fixture 'scripts'
    [IO.Directory]::CreateDirectory($fixtureScripts) | Out-Null
    $fixtureApprove = Join-Path $fixtureScripts `
        'approve_stock_runtime_external_targets.ps1'
    Copy-Item -LiteralPath $approve -Destination $fixtureApprove
    $approve = $fixtureApprove
    $review = Join-Path $fixture `
        'manual-artifacts\stock-runtime-source-review\0123456789abcdef0123456789abcdef'
    [IO.Directory]::CreateDirectory($review) | Out-Null
    $fake = Join-Path $fixture 'fake-review-tool.ps1'
    @'
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$ToolArguments)
$expected = @('--approve', '--review-root', $env:HLCLIENT_TEST_REVIEW,
    '--approval-phrase', 'HLCLIENT_APPROVE_REVIEWED_EXTERNAL_TARGETS_V1')
if ($env:HLCLIENT_TEST_EXPLICIT_LIFETIME -ceq 'true') {
    $expected += @('--lifetime-hours', '12')
}
if (($ToolArguments -join "`n") -cne ($expected -join "`n")) { exit 42 }
$manifest = Join-Path $env:HLCLIENT_TEST_REVIEW 'external-target-approval.json'
[IO.File]::WriteAllText($manifest, '{}')
$lifetime = if ($env:HLCLIENT_TEST_EXPLICIT_LIFETIME -ceq 'true') { '12' } else { '24' }
$lines = @(
    '[source-review] schema=hlclient.stock-runtime-external-target-approval.v1',
    "[source-review] lifetime-hours=$lifetime",
    '[source-review] private-handoff=local-only',
    '[source-review] result=success')
switch -CaseSensitive ($env:HLCLIENT_TEST_VARIANT) {
    'missing' {
        $lines = @($lines | Where-Object {
                $_ -cne '[source-review] private-handoff=local-only' })
    }
    'duplicate' { $lines += '[source-review] result=success' }
    'extra' { $lines += '[source-review] legacy-status=success' }
    'wrong-mode' { $lines += '[source-review] target-count=2' }
    'legacy' {
        $lines[0] = '[source-review] schema=hlclient.stock-runtime-external-target-approval.v0'
    }
    'inconsistent' { $lines[1] = '[source-review] lifetime-hours=13' }
}
Write-Output $lines
exit 0
'@ | Set-Content -LiteralPath $fake -Encoding UTF8
    $env:HLCLIENT_TEST_REVIEW = [IO.Path]::GetFullPath($review).TrimEnd('\', '/')

    function Invoke-ApprovalFixture {
        param([switch]$UseDefaultLifetime)
        if ($UseDefaultLifetime) {
            $env:HLCLIENT_TEST_EXPLICIT_LIFETIME = 'false'
            return @(& $approve -ReviewRoot $review `
                    -ConfirmExternalMaterialization `
                        'HLCLIENT_APPROVE_REVIEWED_EXTERNAL_TARGETS_V1' `
                    -ReviewToolPath $fake)
        }
        $env:HLCLIENT_TEST_EXPLICIT_LIFETIME = 'true'
        return @(& $approve -ReviewRoot $review `
                -ConfirmExternalMaterialization `
                    'HLCLIENT_APPROVE_REVIEWED_EXTERNAL_TARGETS_V1' `
                -LifetimeHours 12 -ReviewToolPath $fake)
    }
    function Assert-ApprovalRejected {
        param([Parameter(Mandatory = $true)][string]$Variant)
        $env:HLCLIENT_TEST_VARIANT = $Variant
        $rejected = $false
        try { Invoke-ApprovalFixture 2>$null | Out-Null } catch { $rejected = $true }
        if (-not $rejected) {
            throw "Approval wrapper accepted the $Variant success-contract fixture."
        }
    }

    $env:HLCLIENT_TEST_VARIANT = 'success'
    $env:HLCLIENT_TEST_EXPLICIT_LIFETIME = 'true'
    $wrongPhraseRejected = $false
    try {
        & $approve -ReviewRoot $review -ConfirmExternalMaterialization 'wrong' `
            -ReviewToolPath $fake 2>$null | Out-Null
    } catch { $wrongPhraseRejected = $true }
    if (-not $wrongPhraseRejected) { throw 'Approval wrapper accepted an inexact phrase.' }

    $lines = @(Invoke-ApprovalFixture)
    if (@($lines | Where-Object { $_ -ceq '[source-review] result=success' }).Count -ne 1 -or
        @($lines | Where-Object { $_ -ceq '[source-review] private-handoff=local-only' }).Count -ne 1 -or
        ($lines -join "`n").Contains($fixture)) {
        throw 'Approval wrapper did not preserve bounded status or disclosed a private path.'
    }
    $defaultLines = @(Invoke-ApprovalFixture -UseDefaultLifetime)
    if (@($defaultLines | Where-Object {
                $_ -ceq '[source-review] lifetime-hours=24' }).Count -ne 1) {
        throw 'Approval wrapper did not enforce the exact default lifetime contract.'
    }
    foreach ($variant in @(
            'missing', 'duplicate', 'extra', 'wrong-mode', 'legacy',
            'inconsistent')) {
        Assert-ApprovalRejected $variant
    }
    Write-Output '[stock-external-approval-test] exact-success-contract=verified'
    Write-Output '[stock-external-approval-test] result=success'
} finally {
    foreach ($name in @(
            'HLCLIENT_TEST_REVIEW', 'HLCLIENT_TEST_EXPLICIT_LIFETIME',
            'HLCLIENT_TEST_VARIANT')) {
        Remove-Item ("Env:" + $name) -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $fixture) { [IO.Directory]::Delete($fixture, $true) }
}
$global:LASTEXITCODE = 0
