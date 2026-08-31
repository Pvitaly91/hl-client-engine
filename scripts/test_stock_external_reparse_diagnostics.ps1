#requires -Version 5.1

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$sourcePrepare = Join-Path $PSScriptRoot 'prepare_stock_runtime_research_copy.ps1'
$fixture = Join-Path ([IO.Path]::GetTempPath()) (
    'hlclient-reparse-review-test-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($fixture) | Out-Null
try {
    $fixtureScripts = Join-Path $fixture 'scripts'
    [IO.Directory]::CreateDirectory($fixtureScripts) | Out-Null
    $prepare = Join-Path $fixtureScripts 'prepare_stock_runtime_research_copy.ps1'
    Copy-Item -LiteralPath $sourcePrepare -Destination $prepare
    $source = Join-Path $fixture 'fake-source'
    $output = Join-Path $fixture 'manual-artifacts\stock-runtime-source-review'
    [IO.Directory]::CreateDirectory($source) | Out-Null
    [IO.Directory]::CreateDirectory($output) | Out-Null
    $fake = Join-Path $fixture 'fake-reparse-review-tool.ps1'
    @'
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$ToolArguments)
$expected = @('--review', '--source-root', $env:HLCLIENT_REPARSE_TEST_SOURCE,
    '--output-parent', $env:HLCLIENT_REPARSE_TEST_OUTPUT)
if (($ToolArguments -join "`n") -cne ($expected -join "`n")) { exit 41 }
$reviewRoot = Join-Path $env:HLCLIENT_REPARSE_TEST_OUTPUT `
    'fedcba9876543210fedcba9876543210'
[IO.Directory]::CreateDirectory($reviewRoot) | Out-Null
$lines = @(
    '[source-review] schema=hlclient.stock-runtime-external-target-review.v2',
    '[source-review] escaped-targets=2',
    '[source-review] completed-targets=2',
    '[source-review] eligible=0',
    '[source-review] ineligible=2',
    '[source-review] incomplete=0',
    '[source-review] unknown=0',
    '[source-review] executable-targets=unavailable',
    '[source-review] mutable-data-targets=unavailable',
    '[source-review] target-count=2',
    '[source-review] eligible-target-count=0',
    '[source-review] all-targets-eligible=false',
    '[source-review] target-1-classification=unsupported_reparse_topology',
    '[source-review] target-1-tag-category=mount_point',
    '[source-review] target-1-expression-kind=nt_object_manager_path',
    '[source-review] target-1-reachability=target_component_not_found',
    '[source-review] target-1-failure-phase=target_open',
    '[source-review] target-1-native-error-category=path_not_found',
    '[source-review] target-1-inventory=unavailable',
    '[source-review] target-1-entry-count=unavailable',
    '[source-review] target-1-byte-count=unavailable',
    '[source-review] target-1-executable-count=unavailable',
    '[source-review] target-1-script-count=unavailable',
    '[source-review] target-1-mutable-state-count=unavailable',
    '[source-review] target-1-nested-link-count=unavailable',
    '[source-review] target-1-eligible=false',
    '[source-review] target-2-classification=unsupported_reparse_topology',
    '[source-review] target-2-tag-category=symbolic_link',
    '[source-review] target-2-expression-kind=drive_absolute_path',
    '[source-review] target-2-reachability=target_path_not_found',
    '[source-review] target-2-failure-phase=target_open',
    '[source-review] target-2-native-error-category=file_not_found',
    '[source-review] target-2-inventory=unavailable',
    '[source-review] target-2-entry-count=unavailable',
    '[source-review] target-2-byte-count=unavailable',
    '[source-review] target-2-executable-count=unavailable',
    '[source-review] target-2-script-count=unavailable',
    '[source-review] target-2-mutable-state-count=unavailable',
    '[source-review] target-2-nested-link-count=unavailable',
    '[source-review] target-2-eligible=false',
    '[source-review] review-id=fedcba9876543210fedcba9876543210',
    '[source-review] result=ineligible')
switch -CaseSensitive ($env:HLCLIENT_REPARSE_TEST_VARIANT) {
    'zero-for-unavailable' {
        $lines = @($lines | ForEach-Object {
                if ($_ -ceq '[source-review] target-1-entry-count=unavailable') {
                    '[source-review] target-1-entry-count=0'
                } else { $_ }
            })
    }
    'missing-typed-field' {
        $lines = @($lines | Where-Object {
                $_ -cne '[source-review] target-2-reachability=target_path_not_found'
            })
    }
    'extra-private-field' {
        $lines += '[source-review] target-1-native-error=3'
    }
    'inconsistent-completion' {
        $lines = @($lines | ForEach-Object {
                if ($_ -ceq '[source-review] completed-targets=2') {
                    '[source-review] completed-targets=1'
                } else { $_ }
            })
    }
    'unreachable-inventory-available' {
        $lines = @($lines | ForEach-Object {
                if ($_ -ceq '[source-review] target-1-inventory=unavailable') {
                    '[source-review] target-1-inventory=available'
                } elseif ($_ -cmatch '^\[source-review\] target-1-(entry-count|byte-count|executable-count|script-count|mutable-state-count|nested-link-count)=unavailable$') {
                    $_ -replace '=unavailable$', '=0'
                } else { $_ }
            })
    }
}
Write-Output $lines
exit 0
'@ | Set-Content -LiteralPath $fake -Encoding UTF8

    $env:HLCLIENT_REPARSE_TEST_SOURCE =
        [IO.Path]::GetFullPath($source).TrimEnd('\', '/')
    $env:HLCLIENT_REPARSE_TEST_OUTPUT =
        [IO.Path]::GetFullPath($output).TrimEnd('\', '/')

    function Invoke-ReparseReviewFixture {
        return @(& $prepare -ReviewExternalTargets `
                -SourceHalfLifeRoot $source -ReviewOutputRoot $output `
                -ReviewToolPath $fake)
    }
    function Assert-ReparseReviewRejected {
        param([Parameter(Mandatory = $true)][string]$Variant)
        $env:HLCLIENT_REPARSE_TEST_VARIANT = $Variant
        $rejected = $false
        try {
            Invoke-ReparseReviewFixture 2>$null | Out-Null
        } catch {
            $rejected = $true
        }
        if (-not $rejected) {
            throw "Review wrapper accepted the $Variant v2 contract fixture."
        }
    }

    $env:HLCLIENT_REPARSE_TEST_VARIANT = 'success'
    $lines = @(Invoke-ReparseReviewFixture)
    foreach ($required in @(
            '[source-review] target-1-tag-category=mount_point',
            '[source-review] target-1-entry-count=unavailable',
            '[source-review] target-2-reachability=target_path_not_found',
            '[source-review] result=ineligible')) {
        if (@($lines | Where-Object { $_ -ceq $required }).Count -ne 1) {
            throw 'Review wrapper did not preserve the exact v2 diagnostic.'
        }
    }
    if (($lines -join "`n").Contains($fixture)) {
        throw 'Review wrapper disclosed a private fixture path.'
    }
    foreach ($variant in @(
            'zero-for-unavailable', 'missing-typed-field',
            'extra-private-field', 'inconsistent-completion',
            'unreachable-inventory-available')) {
        Assert-ReparseReviewRejected $variant
    }

    Write-Output '[stock-external-reparse-test] path-free-v2=verified'
    Write-Output '[stock-external-reparse-test] unavailable-not-zero=verified'
    Write-Output '[stock-external-reparse-test] result=success'
} finally {
    foreach ($name in @(
            'HLCLIENT_REPARSE_TEST_SOURCE', 'HLCLIENT_REPARSE_TEST_OUTPUT',
            'HLCLIENT_REPARSE_TEST_VARIANT')) {
        Remove-Item ("Env:" + $name) -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $fixture) {
        [IO.Directory]::Delete($fixture, $true)
    }
}
$global:LASTEXITCODE = 0
