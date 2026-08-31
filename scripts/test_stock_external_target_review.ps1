#requires -Version 5.1

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$prepare = Join-Path $PSScriptRoot 'prepare_stock_runtime_research_copy.ps1'
$fixture = Join-Path ([IO.Path]::GetTempPath()) (
    'hlclient-external-review-test-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($fixture) | Out-Null
try {
    $fixtureScripts = Join-Path $fixture 'scripts'
    [IO.Directory]::CreateDirectory($fixtureScripts) | Out-Null
    $fixturePrepare = Join-Path $fixtureScripts `
        'prepare_stock_runtime_research_copy.ps1'
    Copy-Item -LiteralPath $prepare -Destination $fixturePrepare
    $prepare = $fixturePrepare
    $source = Join-Path $fixture 'fake-source'
    $output = Join-Path $fixture 'manual-artifacts\stock-runtime-source-review'
    [IO.Directory]::CreateDirectory($source) | Out-Null
    [IO.Directory]::CreateDirectory($output) | Out-Null
    $fake = Join-Path $fixture 'fake-review-tool.ps1'
    @'
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$ToolArguments)
$expected = @('--review', '--source-root', $env:HLCLIENT_TEST_SOURCE,
    '--output-parent', $env:HLCLIENT_TEST_OUTPUT,
    '--maximum-entries', '17', '--maximum-bytes', '4096')
if (($ToolArguments -join "`n") -cne ($expected -join "`n")) { exit 41 }
$reviewRoot = Join-Path $env:HLCLIENT_TEST_OUTPUT '0123456789abcdef0123456789abcdef'
[IO.Directory]::CreateDirectory($reviewRoot) | Out-Null
$targetCount = if ([string]::IsNullOrWhiteSpace($env:HLCLIENT_TEST_TARGET_COUNT)) {
    2
} else {
    [int]$env:HLCLIENT_TEST_TARGET_COUNT
}
$lines = @(
    '[source-review] schema=hlclient.stock-runtime-external-target-review.v1',
    "[source-review] escaped-targets=$targetCount",
    "[source-review] eligible=$targetCount",
    '[source-review] ineligible=0',
    '[source-review] unknown=0',
    '[source-review] executable-targets=0',
    '[source-review] mutable-data-targets=0',
    "[source-review] target-count=$targetCount",
    "[source-review] eligible-target-count=$targetCount",
    '[source-review] all-targets-eligible=true')
for ($ordinal = 1; $ordinal -le $targetCount; ++$ordinal) {
    $lines += @(
        "[source-review] target-$ordinal-classification=eligible_non_executable_asset_tree",
        "[source-review] target-$ordinal-entry-count=2",
        "[source-review] target-$ordinal-byte-count=20",
        "[source-review] target-$ordinal-executable-count=0",
        "[source-review] target-$ordinal-script-count=0",
        "[source-review] target-$ordinal-mutable-state-count=0",
        "[source-review] target-$ordinal-nested-link-count=0",
        "[source-review] target-$ordinal-eligible=true")
}
$lines += @(
    '[source-review] review-id=0123456789abcdef0123456789abcdef',
    '[source-review] result=success')
switch -CaseSensitive ($env:HLCLIENT_TEST_VARIANT) {
    'missing' {
        $lines = @($lines | Where-Object {
                $_ -cne '[source-review] target-2-byte-count=20' })
    }
    'duplicate' { $lines += '[source-review] result=success' }
    'extra' { $lines += '[source-review] legacy-status=success' }
    'wrong-mode' { $lines += '[source-review] private-handoff=local-only' }
    'legacy' {
        $lines = @($lines | ForEach-Object {
                if ($_ -ceq '[source-review] schema=hlclient.stock-runtime-external-target-review.v1') {
                    '[source-review] schema=hlclient.stock-runtime-external-target-review.v0'
                } else { $_ }
            })
    }
    'inconsistent' {
        $lines = @($lines | ForEach-Object {
                if ($_ -ceq "[source-review] eligible=$targetCount") {
                    '[source-review] eligible=1'
                } else { $_ }
            })
    }
}
Write-Output $lines
exit 0
'@ | Set-Content -LiteralPath $fake -Encoding UTF8
    $env:HLCLIENT_TEST_SOURCE = [IO.Path]::GetFullPath($source).TrimEnd('\', '/')
    $env:HLCLIENT_TEST_OUTPUT = [IO.Path]::GetFullPath($output).TrimEnd('\', '/')

    function Invoke-ReviewFixture {
        return @(& $prepare -ReviewExternalTargets `
                -SourceHalfLifeRoot $source -ReviewOutputRoot $output `
                -MaximumExternalEntries 17 -MaximumExternalBytes 4096 `
                -ReviewToolPath $fake)
    }
    function Assert-ReviewRejected {
        param([Parameter(Mandatory = $true)][string]$Variant)
        $env:HLCLIENT_TEST_VARIANT = $Variant
        $rejected = $false
        try { Invoke-ReviewFixture 2>$null | Out-Null } catch { $rejected = $true }
        if (-not $rejected) {
            throw "Review wrapper accepted the $Variant success-contract fixture."
        }
    }

    $env:HLCLIENT_TEST_VARIANT = 'success'
    $env:HLCLIENT_TEST_TARGET_COUNT = '2'
    $lines = @(Invoke-ReviewFixture)
    if (@($lines | Where-Object { $_ -ceq '[source-review] target-count=2' }).Count -ne 1 -or
        @($lines | Where-Object { $_ -ceq '[source-review] review-id=0123456789abcdef0123456789abcdef' }).Count -ne 1 -or
        ($lines -join "`n").Contains($fixture)) {
        throw 'Review wrapper did not preserve bounded status or disclosed a private path.'
    }
    $env:HLCLIENT_TEST_TARGET_COUNT = '7'
    $boundedLines = @(Invoke-ReviewFixture)
    if ($boundedLines.Count -le 64 -or
        @($boundedLines | Where-Object {
                $_ -ceq '[source-review] target-7-eligible=true' }).Count -ne 1) {
        throw 'Review wrapper did not honor the explicit multi-target output bound.'
    }
    $env:HLCLIENT_TEST_TARGET_COUNT = '2'
    foreach ($variant in @(
            'missing', 'duplicate', 'extra', 'wrong-mode', 'legacy',
            'inconsistent')) {
        Assert-ReviewRejected $variant
    }
    Write-Output '[stock-external-review-test] exact-success-contract=verified'
    Write-Output '[stock-external-review-test] result=success'
} finally {
    foreach ($name in @(
            'HLCLIENT_TEST_SOURCE', 'HLCLIENT_TEST_OUTPUT',
            'HLCLIENT_TEST_VARIANT', 'HLCLIENT_TEST_TARGET_COUNT')) {
        Remove-Item ("Env:" + $name) -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $fixture) { [IO.Directory]::Delete($fixture, $true) }
}
$global:LASTEXITCODE = 0
