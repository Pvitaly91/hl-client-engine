#requires -Version 5.1

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ResearchCopyToolPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$prepareScript = Join-Path $PSScriptRoot 'prepare_stock_runtime_research_copy.ps1'
if (-not (Test-Path -LiteralPath $ResearchCopyToolPath -PathType Leaf)) {
    throw 'Research-copy helper is unavailable.'
}

$temporaryBase = [IO.Path]::GetFullPath((
    Join-Path $repositoryRoot 'build\test-artifacts'
)).TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar)
$fixture = [IO.Path]::GetFullPath((Join-Path $temporaryBase (
    'hlclient-research-copy-script-' + [Guid]::NewGuid().ToString('N'))
))
$temporaryPrefix = $temporaryBase + [IO.Path]::DirectorySeparatorChar
if (-not $fixture.StartsWith(
        $temporaryPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    -not ([IO.Path]::GetFileName($fixture)).StartsWith(
        'hlclient-research-copy-script-',
        [StringComparison]::Ordinal)) {
    throw 'Refusing to create a research-copy fixture outside the temp root.'
}
[IO.Directory]::CreateDirectory($fixture) | Out-Null
$junctions = [Collections.Generic.List[string]]::new()

function New-StockFixture {
    param([string]$Root)
    [IO.Directory]::CreateDirectory((Join-Path $Root 'valve\maps')) | Out-Null
    [IO.File]::WriteAllText((Join-Path $Root 'hl.exe'), 'fake-client')
    [IO.File]::WriteAllText((Join-Path $Root 'hlds.exe'), 'fake-server')
    [IO.File]::WriteAllText(
        (Join-Path $Root 'valve\maps\boot_camp.bsp'), 'fake-map')
}

function Assert-Contains {
    param([string[]]$Lines, [string]$Expected)
    if (@($Lines | Where-Object { $_ -ceq $Expected }).Count -eq 0) {
        throw "Missing bounded helper result: $Expected"
    }
}

try {
    $ordinary = Join-Path $fixture 'ordinary'
    $inspectionDestination = Join-Path $fixture 'must-not-exist'
    New-StockFixture $ordinary
    $inspection = @(& $prepareScript `
            -InspectSourceTopology `
            -SourceHalfLifeRoot $ordinary `
            -DestinationHalfLifeRoot $inspectionDestination `
            -ResearchCopyToolPath $ResearchCopyToolPath)
    Assert-Contains $inspection '[research-copy] topology=ordinary_tree'
    Assert-Contains $inspection '[research-copy] result=safe'
    if (Test-Path -LiteralPath $inspectionDestination) {
        throw 'InspectSourceTopology created or touched the destination.'
    }
    if (($inspection -join "`n").Contains($fixture)) {
        throw 'Diagnostic output disclosed a fixture path.'
    }

    $ordinaryCopy = Join-Path $fixture 'ordinary-copy'
    $copy = @(& $prepareScript `
            -SourceHalfLifeRoot $ordinary `
            -DestinationHalfLifeRoot $ordinaryCopy `
            -ResearchCopyToolPath $ResearchCopyToolPath)
    Assert-Contains $copy `
        '[research-copy] preparation-status=exact-materialized-copy-verified'
    Assert-Contains $copy '[research-copy] destination-reparse-count=0'
    Assert-Contains $copy '[research-copy] destination-hardlink-count=0'
    Assert-Contains $copy '[research-copy] destination-ads-count=0'
    Assert-Contains $copy '[stock-runtime-prepare] result=success'
    if (-not (Test-Path -LiteralPath (
                Join-Path $ordinaryCopy '.hlclient-research-pending'
            ) -PathType Leaf) -or
        -not (Test-Path -LiteralPath (
                Join-Path $ordinaryCopy '.hlclient-research-isolated'
            ) -PathType Leaf)) {
        throw 'Preparation pending/commit publication state is incomplete.'
    }
    $manifest = Get-Content -Raw -LiteralPath (
        Join-Path $ordinaryCopy '.hlclient-research-preparation.json') |
        ConvertFrom-Json
    if ($manifest.schema -cne 'hlclient.stock-runtime-research-preparation.v3' -or
        $manifest.marker -cne 'HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1' -or
        $manifest.preparation_profile -cne 'ordinary-or-contained-v3' -or
        $manifest.approved_external_materialized_link_count -ne 0 -or
        $manifest.external_approval_sha256 -cne ('0' * 64) -or
        $manifest.external_classification_summary -cne 'none' -or
        $manifest.executable_target_count -ne 0 -or
        $manifest.mutable_state_target_count -ne 0 -or
        $manifest.source_unchanged_status -cne 'verified' -or
        $manifest.external_targets_unchanged_status -cne 'verified' -or
        $manifest.evidence_eligibility -cne 'eligible' -or
        $manifest.external_target_profile -cne 'none' -or
        $manifest.paths_recorded -ne $false -or
        $manifest.preparation_status -cne
            'exact-materialized-copy-verified') {
        throw 'Preparation manifest v3 is invalid.'
    }

    $physicalParent = Join-Path $fixture 'physical-parent'
    $physicalSource = Join-Path $physicalParent 'Half-Life'
    New-StockFixture $physicalSource
    $rootJunction = Join-Path $fixture 'root-junction'
    New-Item -ItemType Junction -Path $rootJunction -Target $physicalSource |
        Out-Null
    $junctions.Add($rootJunction)
    $rootTopology = @(& $prepareScript `
            -InspectSourceTopology `
            -SourceHalfLifeRoot $rootJunction `
            -DestinationHalfLifeRoot (Join-Path $fixture 'root-copy') `
            -ResearchCopyToolPath $ResearchCopyToolPath)
    Assert-Contains $rootTopology '[research-copy] topology=source_root_reparse'
    Assert-Contains $rootTopology '[research-copy] root-reparse=true'
    Assert-Contains $rootTopology '[research-copy] result=safe'

    $external = Join-Path $fixture 'external'
    [IO.Directory]::CreateDirectory($external) | Out-Null
    [IO.File]::WriteAllText((Join-Path $external 'outside.bin'), 'outside')
    $escape = Join-Path $ordinary 'escape'
    New-Item -ItemType Junction -Path $escape -Target $external | Out-Null
    $junctions.Add($escape)
    $unsafe = @(& $prepareScript `
            -InspectSourceTopology `
            -SourceHalfLifeRoot $ordinary `
            -DestinationHalfLifeRoot (Join-Path $fixture 'unsafe-copy') `
            -ResearchCopyToolPath $ResearchCopyToolPath)
    Assert-Contains $unsafe `
        '[research-copy] topology=source_link_target_outside_root'
    Assert-Contains $unsafe '[research-copy] escaped-target-count=1'
    Assert-Contains $unsafe '[research-copy] result=unsafe'

    $unsafeCopy = Join-Path $fixture 'unsafe-copy'
    $failed = $false
    try {
        & $prepareScript `
            -SourceHalfLifeRoot $ordinary `
            -DestinationHalfLifeRoot $unsafeCopy `
            -ResearchCopyToolPath $ResearchCopyToolPath 2>$null | Out-Null
    } catch {
        $failed = $true
    }
    if (-not $failed -or (Test-Path -LiteralPath $unsafeCopy)) {
        throw 'External junction materialization did not fail transactionally.'
    }

    Write-Output '[research-copy-test] ordinary=passed'
    Write-Output '[research-copy-test] root-junction=passed'
    Write-Output '[research-copy-test] external-target=passed'
    Write-Output '[research-copy-test] v1-marker-v3-manifest=passed'
    Write-Output '[research-copy-test] result=success'
} finally {
    foreach ($junction in @($junctions)) {
        if (Test-Path -LiteralPath $junction) {
            [IO.Directory]::Delete($junction)
        }
    }
    if (Test-Path -LiteralPath $fixture) {
        $cleanupTarget = [IO.Path]::GetFullPath($fixture)
        if ($cleanupTarget -cne $fixture -or
            -not $cleanupTarget.StartsWith(
                $temporaryPrefix, [StringComparison]::OrdinalIgnoreCase) -or
            -not ([IO.Path]::GetFileName($cleanupTarget)).StartsWith(
                'hlclient-research-copy-script-',
                [StringComparison]::Ordinal)) {
            throw 'Refusing unsafe research-copy fixture cleanup.'
        }
        [IO.Directory]::Delete($cleanupTarget, $true)
    }
}

# The rejected unsafe-copy case intentionally leaves the native helper's exit
# code at one. This script completed successfully, so do not leak that expected
# child status to a same-process caller such as a GitHub Actions pwsh step.
$global:LASTEXITCODE = 0
