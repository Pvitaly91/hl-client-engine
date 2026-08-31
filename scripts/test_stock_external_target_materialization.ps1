#requires -Version 5.1

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$prepare = Join-Path $PSScriptRoot 'prepare_stock_runtime_research_copy.ps1'
$fixture = Join-Path ([IO.Path]::GetTempPath()) (
    'hlclient-external-materialization-test-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($fixture) | Out-Null
try {
    $fixtureScripts = Join-Path $fixture 'scripts'
    [IO.Directory]::CreateDirectory($fixtureScripts) | Out-Null
    $fixturePrepare = Join-Path $fixtureScripts `
        'prepare_stock_runtime_research_copy.ps1'
    Copy-Item -LiteralPath $prepare -Destination $fixturePrepare
    $prepare = $fixturePrepare
    $source = Join-Path $fixture 'fake-source'
    $destination = Join-Path $fixture 'fake-destination'
    $approval = Join-Path $fixture `
        'manual-artifacts\stock-runtime-source-review\0123456789abcdef0123456789abcdef\external-target-approval.json'
    [IO.Directory]::CreateDirectory($source) | Out-Null
    [IO.Directory]::CreateDirectory((Split-Path -Parent $approval)) | Out-Null
    [IO.File]::WriteAllText($approval, '{}')
    $fake = Join-Path $fixture 'fake-copy-tool.ps1'
    @'
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$ToolArguments)
switch -CaseSensitive ($env:HLCLIENT_TEST_MODE) {
    'inspect' {
        $expected = @('--inspect-source-topology', '--source-root',
            $env:HLCLIENT_TEST_SOURCE)
        $lines = @(
            '[research-copy] topology=ordinary_tree',
            '[research-copy] root-reparse=false',
            '[research-copy] internal-reparse-count=0',
            '[research-copy] hardlink-count=0',
            '[research-copy] ads-count=0',
            '[research-copy] contained-target-count=0',
            '[research-copy] escaped-target-count=0',
            '[research-copy] result=safe')
    }
    'ordinary' {
        $expected = @('--materialize', '--source-root',
            $env:HLCLIENT_TEST_SOURCE, '--destination-root',
            $env:HLCLIENT_TEST_DESTINATION)
        $lines = @(
            '[research-copy] topology=ordinary_tree',
            '[research-copy] root-reparse=false',
            '[research-copy] internal-reparse-count=0',
            '[research-copy] hardlink-count=0',
            '[research-copy] ads-count=0',
            '[research-copy] contained-target-count=0',
            '[research-copy] escaped-target-count=0',
            '[research-copy] preparation-status=exact-materialized-copy-verified',
            '[research-copy] destination-reparse-count=0',
            '[research-copy] destination-hardlink-count=0',
            '[research-copy] destination-ads-count=0',
            '[research-copy] source-changed=false',
            '[research-copy] external-targets-changed=false',
            '[research-copy] external-target-count=0',
            '[research-copy] external-target-profile=none',
            '[research-copy] research-copy-evidence-eligible=true',
            '[research-copy] copied-entry-count=7',
            '[research-copy] materialized-link-count=0',
            '[research-copy] materialized-hardlink-count=0',
            '[research-copy] marker=HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1',
            '[research-copy] result=success',
            '[stock-runtime-prepare] source-modified=false',
            '[stock-runtime-prepare] copied-launchers=2',
            '[stock-runtime-prepare] copied-entry-count=7',
            '[stock-runtime-prepare] marker=HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1',
            '[stock-runtime-prepare] result=success')
    }
    'reviewed' {
        $expected = @('--materialize', '--source-root',
            $env:HLCLIENT_TEST_SOURCE, '--destination-root',
            $env:HLCLIENT_TEST_DESTINATION,
            '--external-target-approval-manifest', $env:HLCLIENT_TEST_APPROVAL)
        $lines = @(
            '[research-copy] topology=source_link_target_outside_root',
            '[research-copy] root-reparse=false',
            '[research-copy] internal-reparse-count=2',
            '[research-copy] hardlink-count=0',
            '[research-copy] ads-count=0',
            '[research-copy] contained-target-count=0',
            '[research-copy] escaped-target-count=2',
            '[research-copy] preparation-status=exact-reviewed-materialized-copy-verified',
            '[research-copy] destination-reparse-count=0',
            '[research-copy] destination-hardlink-count=0',
            '[research-copy] destination-ads-count=0',
            '[research-copy] source-changed=false',
            '[research-copy] external-targets-changed=false',
            '[research-copy] external-target-count=2',
            '[research-copy] external-target-profile=reviewed-non-executable-v1',
            '[research-copy] research-copy-evidence-eligible=true',
            '[research-copy] copied-entry-count=7',
            '[research-copy] materialized-link-count=2',
            '[research-copy] materialized-hardlink-count=0',
            '[research-copy] marker=HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1',
            '[research-copy] result=success',
            '[stock-runtime-prepare] source-modified=false',
            '[stock-runtime-prepare] copied-launchers=2',
            '[stock-runtime-prepare] copied-entry-count=7',
            '[stock-runtime-prepare] marker=HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1',
            '[stock-runtime-prepare] result=success')
    }
    default { exit 44 }
}
if (($ToolArguments -join "`n") -cne ($expected -join "`n")) { exit 43 }
switch -CaseSensitive ($env:HLCLIENT_TEST_VARIANT) {
    'missing' {
        $lines = @($lines | Where-Object {
                $_ -cne '[research-copy] destination-ads-count=0' })
    }
    'duplicate' { $lines += '[research-copy] result=success' }
    'extra' { $lines += '[research-copy] legacy-status=success' }
    'wrong-mode' {
        $lines = @($lines | ForEach-Object {
                if ($_ -ceq '[research-copy] preparation-status=exact-reviewed-materialized-copy-verified') {
                    '[research-copy] preparation-status=exact-materialized-copy-verified'
                } elseif ($_ -ceq '[research-copy] preparation-status=exact-materialized-copy-verified') {
                    '[research-copy] preparation-status=exact-reviewed-materialized-copy-verified'
                } else { $_ }
            })
    }
    'legacy' {
        $lines = @($lines | ForEach-Object {
                if ($_ -ceq '[research-copy] marker=HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1') {
                    '[research-copy] marker=HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V0'
                } else { $_ }
            })
    }
    'inconsistent' {
        $lines = @($lines | ForEach-Object {
                if ($_ -ceq '[stock-runtime-prepare] copied-entry-count=7') {
                    '[stock-runtime-prepare] copied-entry-count=6'
                } else { $_ }
            })
    }
    'inspect-missing' {
        $lines = @($lines | Where-Object {
                $_ -cne '[research-copy] hardlink-count=0' })
    }
    'inspect-duplicate' { $lines += '[research-copy] topology=ordinary_tree' }
    'inspect-extra' { $lines += '[research-copy] copied-entry-count=7' }
    'inspect-wrong-mode' {
        $lines = @($lines | ForEach-Object {
                if ($_ -ceq '[research-copy] result=safe') {
                    '[research-copy] result=success'
                } else { $_ }
            })
    }
    'inspect-legacy' {
        $lines = @($lines | ForEach-Object {
                if ($_ -ceq '[research-copy] topology=ordinary_tree') {
                    '[research-copy] topology=ordinary-tree'
                } else { $_ }
            })
    }
}
Write-Output $lines
exit 0
'@ | Set-Content -LiteralPath $fake -Encoding UTF8
    $env:HLCLIENT_TEST_SOURCE = [IO.Path]::GetFullPath($source).TrimEnd('\', '/')
    $env:HLCLIENT_TEST_DESTINATION = [IO.Path]::GetFullPath($destination).TrimEnd('\', '/')
    $env:HLCLIENT_TEST_APPROVAL = [IO.Path]::GetFullPath($approval).TrimEnd('\', '/')

    function Invoke-CopyFixture {
        param(
            [Parameter(Mandatory = $true)]
            [ValidateSet('inspect', 'ordinary', 'reviewed')][string]$Mode)
        $env:HLCLIENT_TEST_MODE = $Mode
        if ($Mode -ceq 'inspect') {
            return @(& $prepare -InspectSourceTopology `
                    -SourceHalfLifeRoot $source -ResearchCopyToolPath $fake)
        }
        if ($Mode -ceq 'ordinary') {
            return @(& $prepare -SourceHalfLifeRoot $source `
                    -DestinationHalfLifeRoot $destination `
                    -ResearchCopyToolPath $fake)
        }
        return @(& $prepare -SourceHalfLifeRoot $source `
                -DestinationHalfLifeRoot $destination `
                -ExternalTargetApprovalManifest $approval `
                -ResearchCopyToolPath $fake)
    }
    function Assert-CopyRejected {
        param(
            [Parameter(Mandatory = $true)][string]$Mode,
            [Parameter(Mandatory = $true)][string]$Variant)
        $env:HLCLIENT_TEST_VARIANT = $Variant
        $rejected = $false
        try { Invoke-CopyFixture $Mode 2>$null | Out-Null } catch { $rejected = $true }
        if (-not $rejected) {
            throw "Research-copy wrapper accepted the $Mode/$Variant success-contract fixture."
        }
    }

    $env:HLCLIENT_TEST_VARIANT = 'success'
    $reviewedLines = @(Invoke-CopyFixture reviewed)
    if (@($reviewedLines | Where-Object {
                $_ -ceq '[research-copy] preparation-status=exact-reviewed-materialized-copy-verified' }).Count -ne 1 -or
        @($reviewedLines | Where-Object {
                $_ -ceq '[stock-runtime-prepare] result=success' }).Count -ne 1 -or
        ($reviewedLines -join "`n").Contains($fixture)) {
        throw 'Materialization wrapper failed to forward the private approval manifest safely.'
    }
    $ordinaryLines = @(Invoke-CopyFixture ordinary)
    if (@($ordinaryLines | Where-Object {
                $_ -ceq '[research-copy] external-target-profile=none' }).Count -ne 1) {
        throw 'Materialization wrapper rejected the exact ordinary-copy profile.'
    }
    $inspectLines = @(Invoke-CopyFixture inspect)
    if (@($inspectLines | Where-Object {
                $_ -ceq '[research-copy] result=safe' }).Count -ne 1) {
        throw 'Research-copy wrapper rejected the exact inspection profile.'
    }

    foreach ($variant in @(
            'missing', 'duplicate', 'extra', 'wrong-mode', 'legacy',
            'inconsistent')) {
        Assert-CopyRejected reviewed $variant
    }
    Assert-CopyRejected ordinary 'wrong-mode'
    foreach ($variant in @(
            'inspect-missing', 'inspect-duplicate', 'inspect-extra',
            'inspect-wrong-mode', 'inspect-legacy')) {
        Assert-CopyRejected inspect $variant
    }
    Write-Output '[stock-external-materialization-test] exact-success-contract=verified'
    Write-Output '[stock-external-materialization-test] result=success'
} finally {
    foreach ($name in @(
            'HLCLIENT_TEST_SOURCE', 'HLCLIENT_TEST_DESTINATION',
            'HLCLIENT_TEST_APPROVAL', 'HLCLIENT_TEST_MODE',
            'HLCLIENT_TEST_VARIANT')) {
        Remove-Item ("Env:" + $name) -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $fixture) { [IO.Directory]::Delete($fixture, $true) }
}
$global:LASTEXITCODE = 0
