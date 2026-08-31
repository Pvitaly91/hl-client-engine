#requires -Version 5.1

<#
.SYNOPSIS
Inspects or safely materializes a Half-Life research tree.

.DESCRIPTION
The project-owned Windows helper performs handle-based topology inspection and
copy-by-verified-handle materialization. InspectSourceTopology is strictly
read-only and never resolves, creates, or writes the requested destination.
Materialization publishes a new destination atomically and retains the v1
isolation marker while writing the stricter preparation manifest v3.
#>
[CmdletBinding(DefaultParameterSetName = 'Materialize')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Materialize')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Inspect')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Review')]
    [ValidateNotNullOrEmpty()]
    [string]$SourceHalfLifeRoot,

    [Parameter(Mandatory = $true, ParameterSetName = 'Materialize')]
    [Parameter(ParameterSetName = 'Inspect')]
    [ValidateNotNullOrEmpty()]
    [string]$DestinationHalfLifeRoot,

    [Parameter(Mandatory = $true, ParameterSetName = 'Inspect')]
    [switch]$InspectSourceTopology,

    [Parameter(Mandatory = $true, ParameterSetName = 'Review')]
    [switch]$ReviewExternalTargets,

    [Parameter(Mandatory = $true, ParameterSetName = 'Review')]
    [ValidateNotNullOrEmpty()]
    [string]$ReviewOutputRoot,

    [Parameter(ParameterSetName = 'Review')]
    [ValidateRange(1, 1000000)]
    [int]$MaximumExternalEntries,

    [Parameter(ParameterSetName = 'Review')]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$MaximumExternalBytes,

    [Parameter(ParameterSetName = 'Materialize')]
    [ValidateNotNullOrEmpty()]
    [string]$ExternalTargetApprovalManifest,

    [Parameter(ParameterSetName = 'Materialize')]
    [Parameter(ParameterSetName = 'Inspect')]
    [ValidateNotNullOrEmpty()]
    [string]$ResearchCopyToolPath,

    [Parameter(ParameterSetName = 'Review')]
    [ValidateNotNullOrEmpty()]
    [string]$ReviewToolPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-ResearchCopyTool {
    if (-not [string]::IsNullOrWhiteSpace($ResearchCopyToolPath)) {
        if (-not (Test-Path -LiteralPath $ResearchCopyToolPath -PathType Leaf)) {
            throw 'The explicitly selected research-copy helper is unavailable.'
        }
        return [IO.Path]::GetFullPath($ResearchCopyToolPath)
    }

    $repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    foreach ($candidate in @(
            (Join-Path $repositoryRoot 'build\bin\Debug\hlclient_stock_research_copy.exe'),
            (Join-Path $repositoryRoot 'build\bin\Release\hlclient_stock_research_copy.exe'),
            (Join-Path $repositoryRoot 'build-asan\bin\Release\hlclient_stock_research_copy.exe'))) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }
    throw 'Build hlclient_stock_research_copy before running the preparation helper.'
}

function ConvertFrom-CanonicalUnsignedDecimal {
    param(
        [Parameter(Mandatory = $true)][string]$Value,
        [Parameter(Mandatory = $true)][string]$Field,
        [UInt64]$Maximum = [UInt64]::MaxValue)

    if ($Value -cnotmatch '^(?:0|[1-9][0-9]*)$') {
        throw "The helper returned a non-canonical $Field value."
    }
    [UInt64]$parsed = 0
    if (-not [UInt64]::TryParse(
            $Value, [Globalization.NumberStyles]::None,
            [Globalization.CultureInfo]::InvariantCulture, [ref]$parsed) -or
        $parsed -gt $Maximum) {
        throw "The helper returned an out-of-range $Field value."
    }
    return $parsed
}

function Get-RequiredRecord {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Records,
        [Parameter(Mandatory = $true)][string]$Key)

    if (-not $Records.ContainsKey($Key)) {
        throw "The helper omitted the required $Key success record."
    }
    return [string]$Records[$Key]
}

function Assert-ResearchCopySuccessContract {
    param(
        [Parameter(Mandatory = $true)][string[]]$Lines,
        [Parameter(Mandatory = $true)]
        [ValidateSet('Inspect', 'Materialize')][string]$Mode,
        [Parameter(Mandatory = $true)][bool]$HasExternalApproval)

    $topologyValues = @(
        'ordinary_tree',
        'source_path_ancestor_reparse',
        'source_root_reparse',
        'source_internal_directory_junction',
        'source_internal_directory_symlink',
        'source_internal_file_symlink',
        'source_internal_mount_point',
        'source_file_hardlink',
        'source_alternate_data_stream',
        'source_subst_drive',
        'source_unc_path',
        'source_remote_volume',
        'source_unsupported_reparse_tag',
        'source_link_target_outside_root',
        'source_link_cycle',
        'source_link_depth_exceeded',
        'source_entry_limit_exceeded',
        'source_byte_limit_exceeded',
        'source_reviewed_external_target')
    $inspectKeys = @(
        'research-copy/root-reparse',
        'research-copy/internal-reparse-count',
        'research-copy/hardlink-count',
        'research-copy/ads-count',
        'research-copy/contained-target-count',
        'research-copy/escaped-target-count',
        'research-copy/result')
    $materializeKeys = @(
        'research-copy/root-reparse',
        'research-copy/internal-reparse-count',
        'research-copy/hardlink-count',
        'research-copy/ads-count',
        'research-copy/contained-target-count',
        'research-copy/escaped-target-count',
        'research-copy/preparation-status',
        'research-copy/destination-reparse-count',
        'research-copy/destination-hardlink-count',
        'research-copy/destination-ads-count',
        'research-copy/source-changed',
        'research-copy/external-targets-changed',
        'research-copy/external-target-count',
        'research-copy/external-target-profile',
        'research-copy/research-copy-evidence-eligible',
        'research-copy/copied-entry-count',
        'research-copy/materialized-link-count',
        'research-copy/materialized-hardlink-count',
        'research-copy/marker',
        'research-copy/result',
        'stock-runtime-prepare/source-modified',
        'stock-runtime-prepare/copied-launchers',
        'stock-runtime-prepare/copied-entry-count',
        'stock-runtime-prepare/marker',
        'stock-runtime-prepare/result')
    $allowedKeys = if ($Mode -ceq 'Inspect') { $inspectKeys } else { $materializeKeys }
    $records = @{}
    $topologies = [Collections.Generic.List[string]]::new()
    foreach ($line in $Lines) {
        if ($line -cnotmatch '^\[(research-copy|stock-runtime-prepare)\] ([a-z0-9-]+)=([A-Za-z0-9_.-]+)$') {
            throw 'The research-copy helper emitted a malformed success record.'
        }
        $prefix = $Matches[1]
        $name = $Matches[2]
        $value = $Matches[3]
        if ($prefix -ceq 'research-copy' -and $name -ceq 'topology') {
            if ($topologyValues -cnotcontains $value -or
                $topologies.Contains($value)) {
                throw 'The research-copy helper returned an invalid or duplicate topology record.'
            }
            $topologies.Add($value)
            continue
        }
        $key = "$prefix/$name"
        if ($allowedKeys -cnotcontains $key) {
            throw 'The research-copy helper emitted a success record outside the selected mode contract.'
        }
        if ($records.ContainsKey($key)) {
            throw "The research-copy helper duplicated the $key success record."
        }
        $records[$key] = $value
    }
    if ($topologies.Count -lt 1 -or $topologies.Count -gt $topologyValues.Count) {
        throw 'The research-copy helper omitted or exceeded its bounded topology records.'
    }
    foreach ($key in $allowedKeys) {
        [void](Get-RequiredRecord -Records $records -Key $key)
    }
    if ($records.Count -ne $allowedKeys.Count) {
        throw 'The research-copy helper returned an inexact success record set.'
    }

    $rootReparse = Get-RequiredRecord $records 'research-copy/root-reparse'
    if ($rootReparse -cnotin @('true', 'false')) {
        throw 'The research-copy helper returned an invalid root-reparse value.'
    }
    foreach ($key in @(
            'research-copy/internal-reparse-count',
            'research-copy/hardlink-count',
            'research-copy/ads-count',
            'research-copy/contained-target-count',
            'research-copy/escaped-target-count')) {
        [void](ConvertFrom-CanonicalUnsignedDecimal `
                (Get-RequiredRecord $records $key) $key)
    }

    if ($Mode -ceq 'Inspect') {
        if ((Get-RequiredRecord $records 'research-copy/result') -cnotin
                @('safe', 'unsafe')) {
            throw 'The research-copy helper returned an invalid inspection result.'
        }
        return
    }

    foreach ($key in @(
            'research-copy/destination-reparse-count',
            'research-copy/destination-hardlink-count',
            'research-copy/destination-ads-count')) {
        if ((ConvertFrom-CanonicalUnsignedDecimal `
                    (Get-RequiredRecord $records $key) $key) -ne 0) {
            throw 'The research-copy helper reported a linked destination on success.'
        }
    }
    foreach ($key in @(
            'research-copy/copied-entry-count',
            'research-copy/materialized-link-count',
            'research-copy/materialized-hardlink-count',
            'stock-runtime-prepare/copied-entry-count')) {
        [void](ConvertFrom-CanonicalUnsignedDecimal `
                (Get-RequiredRecord $records $key) $key)
    }
    if ((Get-RequiredRecord $records 'research-copy/source-changed') -cne 'false' -or
        (Get-RequiredRecord $records 'research-copy/external-targets-changed') -cne 'false' -or
        (Get-RequiredRecord $records 'research-copy/research-copy-evidence-eligible') -cne 'true' -or
        (Get-RequiredRecord $records 'research-copy/result') -cne 'success' -or
        (Get-RequiredRecord $records 'stock-runtime-prepare/source-modified') -cne 'false' -or
        (Get-RequiredRecord $records 'stock-runtime-prepare/copied-launchers') -cne '2' -or
        (Get-RequiredRecord $records 'stock-runtime-prepare/result') -cne 'success') {
        throw 'The research-copy helper returned an inconsistent materialization success record.'
    }
    $marker = 'HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1'
    if ((Get-RequiredRecord $records 'research-copy/marker') -cne $marker -or
        (Get-RequiredRecord $records 'stock-runtime-prepare/marker') -cne $marker) {
        throw 'The research-copy helper returned an invalid isolation marker.'
    }
    $copiedEntries = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $records 'research-copy/copied-entry-count') `
        'research-copy/copied-entry-count'
    $legacyCopiedEntries = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $records 'stock-runtime-prepare/copied-entry-count') `
        'stock-runtime-prepare/copied-entry-count'
    if ($copiedEntries -ne $legacyCopiedEntries) {
        throw 'The research-copy helper returned inconsistent copied-entry counts.'
    }
    $externalCount = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $records 'research-copy/external-target-count') `
        'research-copy/external-target-count' 4096
    $escapedCount = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $records 'research-copy/escaped-target-count') `
        'research-copy/escaped-target-count' 4096
    $materializedLinkCount = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $records 'research-copy/materialized-link-count') `
        'research-copy/materialized-link-count'
    if ($externalCount -gt $materializedLinkCount -or
        $externalCount -ne $escapedCount) {
        throw 'The research-copy helper returned inconsistent external-target counts.'
    }
    if ($HasExternalApproval) {
        if ($externalCount -lt 1 -or
            (Get-RequiredRecord $records 'research-copy/preparation-status') -cne
                'exact-reviewed-materialized-copy-verified' -or
            (Get-RequiredRecord $records 'research-copy/external-target-profile') -cne
                'reviewed-non-executable-v1') {
            throw 'The research-copy helper returned the wrong reviewed materialization profile.'
        }
    } elseif ($externalCount -ne 0 -or
        (Get-RequiredRecord $records 'research-copy/preparation-status') -cne
            'exact-materialized-copy-verified' -or
        (Get-RequiredRecord $records 'research-copy/external-target-profile') -cne 'none') {
        throw 'The research-copy helper returned the wrong ordinary materialization profile.'
    }
}

function Invoke-BoundedResearchCopyTool {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)]
        [ValidateSet('Inspect', 'Materialize')][string]$Mode,
        [bool]$HasExternalApproval = $false)

    $tool = Resolve-ResearchCopyTool
    $lines = @(& $tool @Arguments 2>&1 | ForEach-Object { $_.ToString() })
    $exitCode = $LASTEXITCODE
    if ($lines.Count -gt 64) {
        throw 'The research-copy helper exceeded its public output line bound.'
    }
    foreach ($line in $lines) {
        if ($line.Length -gt 512 -or
            $line -cnotmatch '^\[(?:research-copy|stock-runtime-prepare)\] [a-z0-9-]+=[A-Za-z0-9_.-]+$') {
            throw 'The research-copy helper emitted non-bounded public output.'
        }
    }
    if ($exitCode -ne 0) {
        throw "The research-copy helper failed with exit code $exitCode."
    }
    Assert-ResearchCopySuccessContract -Lines $lines -Mode $Mode `
        -HasExternalApproval $HasExternalApproval
    Write-Output $lines
}

function Resolve-ExternalTargetReviewTool {
    if (-not [string]::IsNullOrWhiteSpace($ReviewToolPath)) {
        if (-not (Test-Path -LiteralPath $ReviewToolPath -PathType Leaf)) {
            throw 'The explicitly selected external-target review helper is unavailable.'
        }
        return [IO.Path]::GetFullPath($ReviewToolPath)
    }

    $repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    foreach ($candidate in @(
            (Join-Path $repositoryRoot 'build\bin\Debug\hlclient_stock_external_target_review.exe'),
            (Join-Path $repositoryRoot 'build\bin\Release\hlclient_stock_external_target_review.exe'),
            (Join-Path $repositoryRoot 'build-asan\bin\Release\hlclient_stock_external_target_review.exe'))) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }
    throw 'Build hlclient_stock_external_target_review before reviewing external targets.'
}

function Assert-ExternalTargetReviewV1SuccessContract {
    param([Parameter(Mandatory = $true)][string[]]$Lines)

    $summaryKeys = @(
        'schema',
        'escaped-targets',
        'eligible',
        'ineligible',
        'unknown',
        'executable-targets',
        'mutable-data-targets',
        'target-count',
        'eligible-target-count',
        'all-targets-eligible',
        'review-id',
        'result')
    $targetKeys = @(
        'classification',
        'entry-count',
        'byte-count',
        'executable-count',
        'script-count',
        'mutable-state-count',
        'nested-link-count',
        'eligible')
    $classifications = @(
        'eligible_non_executable_asset_tree',
        'contains_executable_code',
        'contains_script_or_command',
        'contains_mutable_user_state',
        'another_application_tree',
        'operating_system_tree',
        'temporary_or_cache_tree',
        'remote_or_device_target',
        'nested_external_link',
        'unsupported_reparse_topology',
        'content_limit_exceeded',
        'changed_during_review',
        'unknown')
    $summary = @{}
    $targets = @{}
    foreach ($line in $Lines) {
        if ($line -cmatch '^\[source-review\] target-([1-9][0-9]*)-([a-z][a-z0-9-]*)=([A-Za-z0-9_.-]+)$') {
            $ordinalText = $Matches[1]
            $name = $Matches[2]
            $value = $Matches[3]
            if ($targetKeys -cnotcontains $name) {
                throw 'The external-target review helper returned an extra per-target record.'
            }
            $ordinal = ConvertFrom-CanonicalUnsignedDecimal `
                $ordinalText 'target ordinal' 4096
            $key = "$ordinal/$name"
            if ($targets.ContainsKey($key)) {
                throw 'The external-target review helper duplicated a per-target record.'
            }
            $targets[$key] = $value
            continue
        }
        if ($line -cmatch '^\[source-review\] ([a-z][a-z0-9-]*)=([A-Za-z0-9_.-]+)$') {
            $name = $Matches[1]
            $value = $Matches[2]
            if ($summaryKeys -cnotcontains $name -or $summary.ContainsKey($name)) {
                throw 'The external-target review helper returned an extra or duplicate summary record.'
            }
            $summary[$name] = $value
            continue
        }
        throw 'The external-target review helper emitted a malformed success record.'
    }
    foreach ($key in $summaryKeys) {
        [void](Get-RequiredRecord -Records $summary -Key $key)
    }
    if ($summary.Count -ne $summaryKeys.Count) {
        throw 'The external-target review helper returned an inexact summary record set.'
    }
    if ((Get-RequiredRecord $summary 'schema') -cne
            'hlclient.stock-runtime-external-target-review.v1' -or
        (Get-RequiredRecord $summary 'review-id') -cnotmatch '^[0-9a-f]{32}$') {
        throw 'The external-target review helper returned an invalid review identity.'
    }
    $targetCount = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $summary 'target-count') 'target-count' 4096
    if ($targetCount -lt 1) {
        throw 'The external-target review helper returned an empty review on success.'
    }
    $escapedCount = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $summary 'escaped-targets') 'escaped-targets' 4096
    $eligibleCount = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $summary 'eligible') 'eligible' 4096
    $eligibleTargetCount = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $summary 'eligible-target-count') `
        'eligible-target-count' 4096
    $ineligibleCount = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $summary 'ineligible') 'ineligible' 4096
    $unknownCount = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $summary 'unknown') 'unknown' 4096
    $executableCount = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $summary 'executable-targets') `
        'executable-targets'
    $mutableCount = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $summary 'mutable-data-targets') `
        'mutable-data-targets'
    if ($escapedCount -ne $targetCount -or
        $eligibleCount -ne $eligibleTargetCount -or
        ([Decimal]$eligibleCount + [Decimal]$ineligibleCount +
            [Decimal]$unknownCount) -ne [Decimal]$targetCount) {
        throw 'The external-target review helper returned inconsistent summary counts.'
    }

    [UInt64]$observedEligible = 0
    [UInt64]$observedUnknown = 0
    [Decimal]$observedExecutable = 0
    [Decimal]$observedMutable = 0
    for ([UInt64]$ordinal = 1; $ordinal -le $targetCount; ++$ordinal) {
        foreach ($name in $targetKeys) {
            [void](Get-RequiredRecord -Records $targets -Key "$ordinal/$name")
        }
        $classification = Get-RequiredRecord $targets "$ordinal/classification"
        if ($classifications -cnotcontains $classification) {
            throw 'The external-target review helper returned an invalid target classification.'
        }
        $targetEligible = Get-RequiredRecord $targets "$ordinal/eligible"
        if ($targetEligible -cnotin @('true', 'false')) {
            throw 'The external-target review helper returned an invalid target eligibility value.'
        }
        foreach ($name in @(
                'entry-count', 'byte-count', 'executable-count',
                'script-count', 'mutable-state-count', 'nested-link-count')) {
            [void](ConvertFrom-CanonicalUnsignedDecimal `
                    (Get-RequiredRecord $targets "$ordinal/$name") `
                    "target $name")
        }
        $targetExecutable = ConvertFrom-CanonicalUnsignedDecimal `
            (Get-RequiredRecord $targets "$ordinal/executable-count") `
            'target executable-count'
        $targetScript = ConvertFrom-CanonicalUnsignedDecimal `
            (Get-RequiredRecord $targets "$ordinal/script-count") `
            'target script-count'
        $targetMutable = ConvertFrom-CanonicalUnsignedDecimal `
            (Get-RequiredRecord $targets "$ordinal/mutable-state-count") `
            'target mutable-state-count'
        $targetNested = ConvertFrom-CanonicalUnsignedDecimal `
            (Get-RequiredRecord $targets "$ordinal/nested-link-count") `
            'target nested-link-count'
        $isEligibleClassification = $classification -ceq
            'eligible_non_executable_asset_tree'
        if (($targetEligible -ceq 'true') -ne $isEligibleClassification -or
            ($isEligibleClassification -and
                ($targetExecutable -ne 0 -or $targetScript -ne 0 -or
                    $targetMutable -ne 0 -or $targetNested -ne 0))) {
            throw 'The external-target review helper returned an inconsistent target classification.'
        }
        if ($targetEligible -ceq 'true') { ++$observedEligible }
        if ($classification -ceq 'unknown') { ++$observedUnknown }
        $observedExecutable += [Decimal]$targetExecutable
        $observedMutable += [Decimal]$targetMutable
    }
    if ($targets.Count -ne ([int]$targetCount * $targetKeys.Count) -or
        $observedEligible -ne $eligibleCount -or
        $observedUnknown -ne $unknownCount -or
        $observedExecutable -ne [Decimal]$executableCount -or
        $observedMutable -ne [Decimal]$mutableCount) {
        throw 'The external-target review helper returned inconsistent target totals.'
    }
    $allEligible = Get-RequiredRecord $summary 'all-targets-eligible'
    $result = Get-RequiredRecord $summary 'result'
    $expectedAllEligible = $eligibleCount -eq $targetCount
    if ($allEligible -cnotin @('true', 'false') -or
        (($allEligible -ceq 'true') -ne $expectedAllEligible) -or
        ($expectedAllEligible -and $result -cne 'success') -or
        (-not $expectedAllEligible -and $result -cne 'ineligible')) {
        throw 'The external-target review helper returned an inconsistent final result.'
    }
}

function Assert-ExternalTargetReviewV2SuccessContract {
    param([Parameter(Mandatory = $true)][string[]]$Lines)

    $summaryKeys = @(
        'schema', 'escaped-targets', 'completed-targets', 'eligible',
        'ineligible', 'incomplete', 'unknown', 'executable-targets',
        'mutable-data-targets', 'target-count', 'eligible-target-count',
        'all-targets-eligible', 'review-id', 'result')
    $targetKeys = @(
        'classification', 'tag-category', 'expression-kind', 'reachability',
        'failure-phase', 'native-error-category', 'inventory', 'entry-count',
        'byte-count', 'executable-count', 'script-count',
        'mutable-state-count', 'nested-link-count', 'eligible')
    $classifications = @(
        'eligible_non_executable_asset_tree', 'contains_executable_code',
        'contains_script_or_command', 'contains_mutable_user_state',
        'another_application_tree', 'operating_system_tree',
        'temporary_or_cache_tree', 'remote_or_device_target',
        'nested_external_link', 'unsupported_reparse_topology',
        'content_limit_exceeded', 'changed_during_review', 'unknown')
    $tagCategories = @(
        'none', 'mount_point', 'symbolic_link', 'app_exec_link',
        'cloud_placeholder', 'cloud_placeholder_variant', 'wci',
        'wci_tombstone', 'wof', 'dedup', 'hsm', 'dfs', 'sis',
        'projected_file_system', 'microsoft_name_surrogate_other',
        'microsoft_non_name_surrogate_other', 'third_party_name_surrogate',
        'third_party_other', 'malformed_or_unreadable')
    $expressionKinds = @(
        'none', 'relative_path', 'drive_absolute_path', 'volume_guid_path',
        'nt_object_manager_path', 'unc_path', 'device_path',
        'app_execution_alias', 'opaque_non_path_payload', 'malformed')
    $reachabilityValues = @(
        'reachable', 'target_path_not_found', 'target_component_not_found',
        'target_volume_not_found', 'target_access_denied',
        'target_not_directory', 'target_remote_or_device', 'target_cycle',
        'target_depth_exceeded', 'target_changed',
        'target_open_failed_other', 'not_applicable')
    $failurePhases = @(
        'none', 'source_link_open', 'reparse_payload_read',
        'reparse_payload_decode', 'target_expression_parse', 'target_open',
        'target_identity', 'target_inventory', 'nested_entry_open',
        'nested_reparse_decode', 'post_inventory_revalidation')
    $nativeCategories = @(
        'none', 'file_not_found', 'path_not_found', 'invalid_name',
        'bad_pathname', 'bad_network_path', 'bad_network_name',
        'volume_not_ready', 'device_not_exist', 'device_not_connected',
        'access_denied', 'cannot_access_file', 'not_directory',
        'reparse_tag_invalid', 'reparse_tag_mismatch',
        'reparse_point_encountered', 'circular_dependency',
        'too_many_links', 'other')

    $summary = @{}
    $targets = @{}
    foreach ($line in $Lines) {
        if ($line -cmatch '^\[source-review\] target-([1-9][0-9]*)-([a-z][a-z0-9-]*)=([A-Za-z0-9_.-]+)$') {
            $ordinal = ConvertFrom-CanonicalUnsignedDecimal `
                $Matches[1] 'target ordinal' 4096
            $name = $Matches[2]
            if ($targetKeys -cnotcontains $name) {
                throw 'The v2 external-target review helper returned an extra per-target record.'
            }
            $key = "$ordinal/$name"
            if ($targets.ContainsKey($key)) {
                throw 'The v2 external-target review helper duplicated a per-target record.'
            }
            $targets[$key] = $Matches[3]
            continue
        }
        if ($line -cmatch '^\[source-review\] ([a-z][a-z0-9-]*)=([A-Za-z0-9_.-]+)$') {
            $name = $Matches[1]
            if ($summaryKeys -cnotcontains $name -or $summary.ContainsKey($name)) {
                throw 'The v2 external-target review helper returned an extra or duplicate summary record.'
            }
            $summary[$name] = $Matches[2]
            continue
        }
        throw 'The v2 external-target review helper emitted a malformed success record.'
    }
    foreach ($key in $summaryKeys) {
        [void](Get-RequiredRecord -Records $summary -Key $key)
    }
    if ($summary.Count -ne $summaryKeys.Count -or
        (Get-RequiredRecord $summary 'schema') -cne
            'hlclient.stock-runtime-external-target-review.v2' -or
        (Get-RequiredRecord $summary 'review-id') -cnotmatch '^[0-9a-f]{32}$') {
        throw 'The v2 external-target review helper returned an invalid summary identity.'
    }

    $targetCount = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $summary 'target-count') 'target-count' 4096
    $escapedCount = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $summary 'escaped-targets') 'escaped-targets' 4096
    $completedCount = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $summary 'completed-targets') 'completed-targets' 4096
    $eligibleCount = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $summary 'eligible') 'eligible' 4096
    $eligibleTargetCount = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $summary 'eligible-target-count') `
        'eligible-target-count' 4096
    $ineligibleCount = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $summary 'ineligible') 'ineligible' 4096
    $incompleteCount = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $summary 'incomplete') 'incomplete' 4096
    $unknownCount = ConvertFrom-CanonicalUnsignedDecimal `
        (Get-RequiredRecord $summary 'unknown') 'unknown' 4096
    if ($targetCount -lt 1 -or $escapedCount -ne $targetCount -or
        $completedCount -ne $targetCount -or $incompleteCount -ne 0 -or
        $eligibleCount -ne $eligibleTargetCount -or
        ([Decimal]$eligibleCount + [Decimal]$ineligibleCount) -ne
            [Decimal]$targetCount -or $unknownCount -gt $ineligibleCount) {
        throw 'The v2 external-target review helper returned inconsistent summary counts.'
    }

    [UInt64]$observedEligible = 0
    [UInt64]$observedUnknown = 0
    [Decimal]$observedExecutable = 0
    [Decimal]$observedMutable = 0
    $allInventoriesAvailable = $true
    for ([UInt64]$ordinal = 1; $ordinal -le $targetCount; ++$ordinal) {
        foreach ($name in $targetKeys) {
            [void](Get-RequiredRecord -Records $targets -Key "$ordinal/$name")
        }
        $classification = Get-RequiredRecord $targets "$ordinal/classification"
        $tagCategory = Get-RequiredRecord $targets "$ordinal/tag-category"
        $expressionKind = Get-RequiredRecord $targets "$ordinal/expression-kind"
        $reachability = Get-RequiredRecord $targets "$ordinal/reachability"
        $failurePhase = Get-RequiredRecord $targets "$ordinal/failure-phase"
        $nativeCategory = Get-RequiredRecord $targets "$ordinal/native-error-category"
        $inventory = Get-RequiredRecord $targets "$ordinal/inventory"
        $targetEligible = Get-RequiredRecord $targets "$ordinal/eligible"
        if ($classifications -cnotcontains $classification -or
            $tagCategories -cnotcontains $tagCategory -or
            $expressionKinds -cnotcontains $expressionKind -or
            $reachabilityValues -cnotcontains $reachability -or
            $failurePhases -cnotcontains $failurePhase -or
            $nativeCategories -cnotcontains $nativeCategory -or
            $inventory -cnotin @('available', 'unavailable') -or
            $targetEligible -cnotin @('true', 'false')) {
            throw 'The v2 external-target review helper returned an invalid typed target record.'
        }
        $isEligibleClassification = $classification -ceq
            'eligible_non_executable_asset_tree'
        $isReachable = $reachability -ceq 'reachable'
        $isNonPathExpression = $expressionKind -cin @(
            'none', 'app_execution_alias', 'opaque_non_path_payload',
            'malformed')
        if (($targetEligible -ceq 'true') -ne $isEligibleClassification -or
            $tagCategory -ceq 'none' -or
            ($inventory -ceq 'available' -and -not $isReachable) -or
            (-not $isReachable -and $inventory -ne 'unavailable') -or
            ($isNonPathExpression -and
                ($isReachable -or $nativeCategory -cne 'none')) -or
            ($targetEligible -ceq 'true' -and
                ($inventory -cne 'available' -or $reachability -cne 'reachable' -or
                    $failurePhase -cne 'none' -or $nativeCategory -cne 'none'))) {
            throw 'The v2 external-target review helper returned an inconsistent eligibility record.'
        }
        $numericNames = @(
            'entry-count', 'byte-count', 'executable-count', 'script-count',
            'mutable-state-count', 'nested-link-count')
        if ($inventory -ceq 'unavailable') {
            $allInventoriesAvailable = $false
            foreach ($name in $numericNames) {
                if ((Get-RequiredRecord $targets "$ordinal/$name") -cne 'unavailable') {
                    throw 'The v2 external-target review helper substituted a numeric unavailable inventory value.'
                }
            }
        } else {
            foreach ($name in $numericNames) {
                [void](ConvertFrom-CanonicalUnsignedDecimal `
                        (Get-RequiredRecord $targets "$ordinal/$name") `
                        "target $name")
            }
            $targetExecutable = ConvertFrom-CanonicalUnsignedDecimal `
                (Get-RequiredRecord $targets "$ordinal/executable-count") `
                'target executable-count'
            $targetScript = ConvertFrom-CanonicalUnsignedDecimal `
                (Get-RequiredRecord $targets "$ordinal/script-count") `
                'target script-count'
            $targetMutable = ConvertFrom-CanonicalUnsignedDecimal `
                (Get-RequiredRecord $targets "$ordinal/mutable-state-count") `
                'target mutable-state-count'
            $targetNested = ConvertFrom-CanonicalUnsignedDecimal `
                (Get-RequiredRecord $targets "$ordinal/nested-link-count") `
                'target nested-link-count'
            if ($targetEligible -ceq 'true' -and
                ($targetExecutable -ne 0 -or $targetScript -ne 0 -or
                    $targetMutable -ne 0 -or $targetNested -ne 0)) {
                throw 'The v2 external-target review helper returned executable or mutable eligible content.'
            }
            $observedExecutable += [Decimal]$targetExecutable
            $observedMutable += [Decimal]$targetMutable
        }
        if ($targetEligible -ceq 'true') { ++$observedEligible }
        if ($classification -ceq 'unknown') { ++$observedUnknown }
    }
    if ($targets.Count -ne ([int]$targetCount * $targetKeys.Count) -or
        $observedEligible -ne $eligibleCount -or
        $observedUnknown -ne $unknownCount) {
        throw 'The v2 external-target review helper returned inconsistent target totals.'
    }

    $summaryExecutable = Get-RequiredRecord $summary 'executable-targets'
    $summaryMutable = Get-RequiredRecord $summary 'mutable-data-targets'
    if ($allInventoriesAvailable) {
        if ((ConvertFrom-CanonicalUnsignedDecimal $summaryExecutable `
                    'executable-targets') -ne $observedExecutable -or
            (ConvertFrom-CanonicalUnsignedDecimal $summaryMutable `
                    'mutable-data-targets') -ne $observedMutable) {
            throw 'The v2 external-target review helper returned inconsistent inventory totals.'
        }
    } elseif ($summaryExecutable -cne 'unavailable' -or
        $summaryMutable -cne 'unavailable') {
        throw 'The v2 external-target review helper substituted zero for unavailable summary inventory.'
    }

    $allEligible = Get-RequiredRecord $summary 'all-targets-eligible'
    $result = Get-RequiredRecord $summary 'result'
    $expectedAllEligible = $eligibleCount -eq $targetCount
    if ($allEligible -cnotin @('true', 'false') -or
        (($allEligible -ceq 'true') -ne $expectedAllEligible) -or
        ($expectedAllEligible -and $result -cne 'success') -or
        (-not $expectedAllEligible -and $result -cne 'ineligible')) {
        throw 'The v2 external-target review helper returned an inconsistent final result.'
    }
}

function Assert-ExternalTargetReviewSuccessContract {
    param([Parameter(Mandatory = $true)][string[]]$Lines)

    $schemas = @($Lines | Where-Object {
            $_ -cmatch '^\[source-review\] schema=hlclient\.stock-runtime-external-target-review\.v[12]$'
        })
    if ($schemas.Count -ne 1) {
        throw 'The external-target review helper omitted or duplicated its schema.'
    }
    if ($schemas[0] -ceq
            '[source-review] schema=hlclient.stock-runtime-external-target-review.v1') {
        Assert-ExternalTargetReviewV1SuccessContract -Lines $Lines
    } else {
        Assert-ExternalTargetReviewV2SuccessContract -Lines $Lines
    }
}

function Invoke-BoundedExternalTargetReviewTool {
    param([string[]]$Arguments)

    $tool = Resolve-ExternalTargetReviewTool
    $lines = @(& $tool @Arguments 2>&1 | ForEach-Object { $_.ToString() })
    $exitCode = $LASTEXITCODE
    $maximumReviewLines = 14 + (14 * 4096)
    if ($lines.Count -gt $maximumReviewLines) {
        throw 'The external-target review helper exceeded its public output line bound.'
    }
    foreach ($line in $lines) {
        if ($line.Length -gt 512 -or
            $line -cnotmatch '^\[source-review\] [a-z0-9-]+=[A-Za-z0-9_.-]+$') {
            throw 'The external-target review helper emitted non-bounded public output.'
        }
    }
    if ($exitCode -ne 0) {
        throw "The external-target review helper failed with exit code $exitCode."
    }
    Assert-ExternalTargetReviewSuccessContract -Lines $lines
    Write-Output $lines
}

$source = [IO.Path]::GetFullPath($SourceHalfLifeRoot).TrimEnd('\', '/')
if ($PSCmdlet.ParameterSetName -ceq 'Inspect') {
    # Deliberately do not resolve, inspect, create, or otherwise touch the
    # destination in diagnostic mode. The parameter remains accepted so the
    # original two-path manual command is backwards compatible.
    Invoke-BoundedResearchCopyTool -Mode Inspect -Arguments @(
        '--inspect-source-topology', '--source-root', $source)
    return
}


if ($PSCmdlet.ParameterSetName -ceq 'Review') {
    $reviewOutput = [IO.Path]::GetFullPath($ReviewOutputRoot).TrimEnd('\', '/')
    $repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $requiredReviewOutput = [IO.Path]::GetFullPath((Join-Path `
            $repositoryRoot 'manual-artifacts\stock-runtime-source-review')).TrimEnd('\', '/')
    if (-not [string]::Equals(
            $reviewOutput, $requiredReviewOutput,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'ReviewOutputRoot must be the repository-local manual-artifacts/stock-runtime-source-review directory.'
    }
    $reviewArguments = [Collections.Generic.List[string]]::new()
    foreach ($argument in @(
            '--review', '--source-root', $source,
            '--output-parent', $reviewOutput)) {
        $reviewArguments.Add($argument)
    }
    if ($PSBoundParameters.ContainsKey('MaximumExternalEntries')) {
        $reviewArguments.Add('--maximum-entries')
        $reviewArguments.Add($MaximumExternalEntries.ToString(
                [Globalization.CultureInfo]::InvariantCulture))
    }
    if ($PSBoundParameters.ContainsKey('MaximumExternalBytes')) {
        $reviewArguments.Add('--maximum-bytes')
        $reviewArguments.Add($MaximumExternalBytes.ToString(
                [Globalization.CultureInfo]::InvariantCulture))
    }
    Invoke-BoundedExternalTargetReviewTool -Arguments $reviewArguments.ToArray()
    return
}

if ([string]::IsNullOrWhiteSpace($DestinationHalfLifeRoot)) {
    throw 'DestinationHalfLifeRoot is required for materialization.'
}
$destination =
    [IO.Path]::GetFullPath($DestinationHalfLifeRoot).TrimEnd('\', '/')
$materializeArguments = [Collections.Generic.List[string]]::new()
foreach ($argument in @(
    '--materialize', '--source-root', $source,
    '--destination-root', $destination)) {
    $materializeArguments.Add($argument)
}
if ($PSBoundParameters.ContainsKey('ExternalTargetApprovalManifest')) {
    $approvalManifest = [IO.Path]::GetFullPath(
        $ExternalTargetApprovalManifest).TrimEnd('\', '/')
    $repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $requiredReviewParent = [IO.Path]::GetFullPath((Join-Path `
            $repositoryRoot 'manual-artifacts\stock-runtime-source-review')).TrimEnd('\', '/')
    $approvalReviewRoot = Split-Path -Parent $approvalManifest
    if ((Split-Path -Leaf $approvalManifest) -cne
            'external-target-approval.json' -or
        (Split-Path -Leaf $approvalReviewRoot) -cnotmatch '^[0-9a-f]{32}$' -or
        -not [string]::Equals(
            (Split-Path -Parent $approvalReviewRoot), $requiredReviewParent,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'ExternalTargetApprovalManifest must be the exact local approval artifact for a repository review ID.'
    }
    $materializeArguments.Add('--external-target-approval-manifest')
    $materializeArguments.Add($approvalManifest)
}
Invoke-BoundedResearchCopyTool -Mode Materialize `
    -HasExternalApproval ($PSBoundParameters.ContainsKey(
        'ExternalTargetApprovalManifest')) `
    -Arguments $materializeArguments.ToArray()
