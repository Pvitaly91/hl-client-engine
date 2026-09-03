#requires -Version 5.1

<#
.SYNOPSIS
Runs or resumes the bounded 24-slot stock-runtime first-observation campaign.

.DESCRIPTION
The campaign is sequential and uses the same explicit, case-sensitive active
capture opt-in as one capture. Existing accepted publications are checked and
assigned deterministically to the first still-unfilled matching matrix slot;
incomplete runs are never reinterpreted or overwritten. A metadata-only
campaign manifest is atomically refreshed after every attempt. No evidence JSON
is created here.
#>
[CmdletBinding(DefaultParameterSetName = 'Capture')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [switch]$EnableActiveCapture,
    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidateNotNullOrEmpty()]
    [string]$ConfirmActiveCapture,
    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidateNotNullOrEmpty()]
    [string]$ResearchHalfLifeRoot,
    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidateNotNullOrEmpty()]
    [string]$ClientPath,
    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidateNotNullOrEmpty()]
    [string]$HldsPath,
    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidateNotNullOrEmpty()]
    [string]$CaptureToolPath,
    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidateNotNullOrEmpty()]
    [string]$NetworkIsolationGuardPath,
    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidateNotNullOrEmpty()]
    [string]$CheckerPath,
    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidateNotNullOrEmpty()]
    [string]$AppManifestPath,
    [Parameter(ParameterSetName = 'Capture')][ValidateNotNullOrEmpty()]
    [string]$OutputRoot = '.\manual-artifacts\stock-runtime',
    [Parameter(ParameterSetName = 'Capture')][ValidateRange(1024, 65486)]
    [int]$FirstRelayPort = 27140,
    [Parameter(ParameterSetName = 'Capture')][ValidateRange(30, 300)]
    [int]$BaselineDurationSeconds = 45,
    [Parameter(ParameterSetName = 'Capture')][ValidateRange(30, 300)]
    [int]$IdleDurationSeconds = 60,

    [Parameter(Mandatory = $true, ParameterSetName = 'Policy')]
    [switch]$ValidateCampaignAggregationPolicy,

    [Parameter(Mandatory = $true, ParameterSetName = 'UnboundRecoveryPolicy')]
    [switch]$ValidateUnboundCanaryRecoveryPolicy
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$canaryManifestSchema = 'hlclient.stock-runtime-pre-campaign-canary.v1'

function Test-ExternalTargetMetadata {
    param([Int64]$Count, [string]$Profile)
    return (($Count -eq 0 -and $Profile -ceq 'none') -or
        ($Count -gt 0 -and
            $Profile -ceq 'reviewed-non-executable-v1'))
}

function Test-ExternalTargetBindingMatches {
    param(
        [Int64]$Count, [string]$Profile,
        [Int64]$ExpectedCount, [string]$ExpectedProfile)
    return ((Test-ExternalTargetMetadata $Count $Profile) -and
        (Test-ExternalTargetMetadata $ExpectedCount $ExpectedProfile) -and
        $Count -eq $ExpectedCount -and $Profile -ceq $ExpectedProfile)
}

function Get-CanaryBindingSha256 {
    param([object]$Value)
    $canonical = @(
        "schema=$([string]$Value.schema)",
        "implementation_commit=$([string]$Value.implementation_commit)",
        "run_id=$([string]$Value.run_id)",
        "map_category=$([string]$Value.map_category)",
        "scenario=$([string]$Value.scenario)",
        "external_target_profile=$([string]$Value.external_target_profile)",
        "external_target_count=$([string]$Value.external_target_count)",
        "accepted_evidence_run=$(([bool]$Value.accepted_evidence_run).ToString().ToLowerInvariant())",
        "delivered_sequenced_s2c_count=$([string]$Value.delivered_sequenced_s2c_count)",
        "exact_boundary_count=$([string]$Value.exact_boundary_count)",
        "runtime_candidate_count=$([string]$Value.runtime_candidate_count)",
        "candidate_stability=$([string]$Value.candidate_stability)",
        "profile_fingerprint=$([string]$Value.profile_fingerprint)",
        "transport_structural_sha256=$([string]$Value.transport_structural_sha256)",
        "replay_structural_sha256=$([string]$Value.replay_structural_sha256)",
        "checker_output_sha256=$([string]$Value.checker_output_sha256)",
        "accepted_before_campaign=$(([bool]$Value.accepted_before_campaign).ToString().ToLowerInvariant())",
        "counted_in_campaign=$(([bool]$Value.counted_in_campaign).ToString().ToLowerInvariant())") -join '|'
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash(
                    [Text.UTF8Encoding]::new($false, $true).GetBytes(
                        $canonical)))).Replace('-', '').ToLowerInvariant()
    } finally { $sha.Dispose() }
}

function New-CanaryManifestValue {
    param([object]$Binding)
    $value = [ordered]@{
        schema = $canaryManifestSchema
        implementation_commit = [string]$Binding.ImplementationCommit
        run_id = [string]$Binding.RunId
        map_category = 'boot_camp'
        scenario = 'baseline'
        external_target_profile = [string]$Binding.ExternalTargetProfile
        external_target_count = [Int64]$Binding.ExternalTargetCount
        accepted_evidence_run = $true
        delivered_sequenced_s2c_count = [Int64]$Binding.DeliveredSequencedS2c
        exact_boundary_count = 1
        runtime_candidate_count = 1
        candidate_stability = 'single_observation'
        profile_fingerprint = [string]$Binding.ProfileFingerprint
        transport_structural_sha256 = [string]$Binding.TransportStructuralHash
        replay_structural_sha256 = [string]$Binding.ReplayStructuralHash
        checker_output_sha256 = [string]$Binding.CheckerOutputHash
        accepted_before_campaign = $true
        counted_in_campaign = $false
    }
    $value.canary_structural_sha256 = Get-CanaryBindingSha256 $value
    return [pscustomobject]$value
}

function Assert-CanaryManifestContract {
    param([object]$Manifest, [object]$Expected)
    $names = @(
        'schema', 'implementation_commit', 'run_id', 'map_category', 'scenario',
        'external_target_profile', 'external_target_count',
        'accepted_evidence_run', 'delivered_sequenced_s2c_count',
        'exact_boundary_count', 'runtime_candidate_count', 'candidate_stability',
        'profile_fingerprint', 'transport_structural_sha256',
        'replay_structural_sha256', 'checker_output_sha256',
        'accepted_before_campaign', 'counted_in_campaign',
        'canary_structural_sha256')
    $actualNames = @($Manifest.PSObject.Properties.Name)
    if ($actualNames.Count -ne $names.Count -or
        @($actualNames | Where-Object { $names -cnotcontains $_ }).Count -ne 0) {
        throw 'Pre-campaign canary manifest has an inexact property set.'
    }
    if ([string]$Manifest.schema -cne $canaryManifestSchema -or
        [string]$Manifest.implementation_commit -cnotmatch '^[0-9a-f]{40}$' -or
        [string]$Manifest.run_id -cnotmatch '^[0-9a-f]{32}$' -or
        [string]$Manifest.map_category -cne 'boot_camp' -or
        [string]$Manifest.scenario -cne 'baseline' -or
        [string]$Manifest.external_target_count -cnotmatch '^(?:0|[1-9][0-9]*)$' -or
        [Int64]$Manifest.external_target_count -gt 4096 -or
        -not (Test-ExternalTargetMetadata `
            ([Int64]$Manifest.external_target_count) `
            ([string]$Manifest.external_target_profile)) -or
        $Manifest.accepted_evidence_run -isnot [bool] -or
        -not [bool]$Manifest.accepted_evidence_run -or
        [string]$Manifest.delivered_sequenced_s2c_count -cnotmatch
            '^(?:[1-9][0-9]{2,})$' -or
        [Int64]$Manifest.delivered_sequenced_s2c_count -lt 100 -or
        [Int64]$Manifest.delivered_sequenced_s2c_count -gt 131072 -or
        [string]$Manifest.exact_boundary_count -cne '1' -or
        [string]$Manifest.runtime_candidate_count -cne '1' -or
        [string]$Manifest.candidate_stability -cne 'single_observation' -or
        [string]$Manifest.profile_fingerprint -cnotmatch '^[0-9a-f]{64}$' -or
        [string]$Manifest.transport_structural_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        [string]$Manifest.replay_structural_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        [string]$Manifest.checker_output_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        $Manifest.accepted_before_campaign -isnot [bool] -or
        -not [bool]$Manifest.accepted_before_campaign -or
        $Manifest.counted_in_campaign -isnot [bool] -or
        [bool]$Manifest.counted_in_campaign -or
        [string]$Manifest.canary_structural_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        [string]$Manifest.canary_structural_sha256 -cne
            (Get-CanaryBindingSha256 $Manifest)) {
        throw 'Pre-campaign canary manifest violates its exact policy.'
    }
    foreach ($name in $names) {
        if ([string]$Manifest.$name -cne [string]$Expected.$name) {
            throw "Pre-campaign canary manifest disagrees at '$name'."
        }
    }
}

function Get-CheckerInteger {
    param([object]$Values, [string]$Name, [Int64]$Minimum, [Int64]$Maximum)
    if ($null -eq $Values -or -not $Values.ContainsKey($Name)) {
        throw "Checker output lacks '$Name'."
    }
    $text = [string]$Values[$Name]
    [Int64]$number = 0
    if ($text -cnotmatch '^(?:0|[1-9][0-9]*)$' -or
        -not [Int64]::TryParse($text, [ref]$number) -or
        $number -lt $Minimum -or $number -gt $Maximum) {
        throw "Checker output '$Name' is outside its bound."
    }
    return $number
}

function Assert-NotReparsePointItem {
    param([object]$Item, [string]$Label)
    if (($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label is a reparse point."
    }
}

function Assert-NoReparsePointInExistingPath {
    param([string]$Path, [string]$Label)
    $full = [IO.Path]::GetFullPath($Path)
    $pathRoot = [IO.Path]::GetPathRoot($full)
    $current = $pathRoot
    foreach ($component in @($full.Substring($pathRoot.Length) -split '[\\/]' |
            Where-Object { $_ })) {
        $current = [IO.Path]::Combine($current, $component)
        if (-not (Test-Path -LiteralPath $current)) { break }
        Assert-NotReparsePointItem (Get-Item -LiteralPath $current -Force) $Label
    }
}

function Assert-OnlyDefaultDataStream {
    param([string]$Path, [string]$Label)
    $streams = @(Get-Item -LiteralPath $Path -Stream * -ErrorAction Stop)
    if (@($streams | Where-Object { $_.Stream -cne ':$DATA' }).Count -ne 0) {
        throw "$Label contains an alternate data stream."
    }
}

function Assert-NoHardLink {
    param([string]$Path, [string]$Label)
    $item = Get-Item -LiteralPath $Path -Force
    $property = $item.PSObject.Properties['LinkType']
    if ($null -eq $property -or
        -not [string]::IsNullOrEmpty([string]$property.Value)) {
        throw "$Label must be an unlinked regular file."
    }
}

function Get-CampaignAttributedPacketCounts {
    param([object]$Values, [string]$Scenario)
    if ($Scenario -ceq 'reconnect') {
        [Int64]$c2s = Get-CheckerInteger $Values 'sequenced-c2s' 0 131072
        [Int64]$s2c = Get-CheckerInteger $Values 'sequenced-s2c' 0 131072
        [Int64]$tail = Get-CheckerInteger $Values `
            'retired-generation-a-server-tail-packets' 0 65536
        [Int64]$generationC2s =
            (Get-CheckerInteger $Values 'generation-a-client-to-server-packets' 0 65536) +
            (Get-CheckerInteger $Values 'generation-b-client-to-server-packets' 0 65536)
        [Int64]$generationS2c =
            (Get-CheckerInteger $Values 'generation-a-server-to-client-packets' 0 65536) +
            (Get-CheckerInteger $Values 'generation-b-server-to-client-packets' 0 65536)
        if ($c2s -gt $generationC2s -or $s2c -gt $generationS2c) {
            throw 'Reconnect replay packet totals exceed generation-attributed populations.'
        }
        return [pscustomobject]@{ C2s = $c2s; S2c = $s2c; RetiredATail = $tail }
    }
    if ($Values.ContainsKey('retired-generation-a-server-tail-packets')) {
        throw 'Non-reconnect checker output contains a reconnect tail claim.'
    }
    [Int64]$c2s = Get-CheckerInteger $Values `
        'delivered-sequenced-c2s' 0 131072
    [Int64]$s2c = Get-CheckerInteger $Values `
        'delivered-sequenced-s2c' 0 131072
    return [pscustomobject]@{ C2s = $c2s; S2c = $s2c; RetiredATail = 0 }
}

function Get-CampaignFailurePublication {
    param([string]$FailureCategory)
    if ($FailureCategory -ceq 'bounded-session-incomplete') {
        return 'incomplete'
    }
    return 'rejected'
}

function Test-CampaignFailureBlocksResume {
    param([string]$FailureCategory)
    return (Get-CampaignFailurePublication $FailureCategory) -cne 'incomplete'
}

function Assert-CampaignResumeAllowed {
    param([object]$State)
    $property = $State.PSObject.Properties['ResumeBlockingFailureCount']
    if ($null -eq $property -or $property.Value -isnot [int] -or
        [int]$property.Value -lt 0) {
        throw 'Campaign state lacks a bounded resume-failure count.'
    }
    if ([int]$property.Value -ne 0) {
        throw ('Campaign resume is blocked by a retained rejected/fatal run. ' +
            'Only bounded-session-incomplete may continue to a missing slot; ' +
            'preserve this campaign for diagnosis and start a fresh campaign.')
    }
}

function Get-SuccessfulWalkerLines {
    param([string[]]$Lines, [object]$ExitCode)
    if ($ExitCode -isnot [int] -or [int]$ExitCode -ne 0) {
        throw 'Independent transport walker exited unsuccessfully.'
    }
    return $Lines
}

function Get-CanaryResumeDisposition {
    param(
        [int]$RunCount,
        [int]$ManifestCount,
        [bool]$CampaignHasState)
    if ($RunCount -lt 0 -or $RunCount -gt 1 -or
        $ManifestCount -lt 0 -or $ManifestCount -gt 1) {
        return 'invalid'
    }
    if ($ManifestCount -eq 1) {
        return $(if ($RunCount -eq 1) { 'bound-reuse' } else { 'invalid' })
    }
    if ($CampaignHasState) { return 'campaign-without-canary' }
    if ($RunCount -eq 1) { return 'unbound-quarantine' }
    return 'fresh-capture'
}

function Get-PreCampaignCanaryFailureCategory {
    param([string[]]$CaptureOutput)
    $failure = @($CaptureOutput | Where-Object {
            $_ -match '^\[stock-runtime-capture\] failure-category=[A-Za-z0-9_.:-]+$' } |
        Select-Object -Last 1)
    if ($failure.Count -eq 1) {
        return $failure[0].Substring(
            '[stock-runtime-capture] failure-category='.Length)
    }
    return 'pre_campaign_canary_failed'
}

function Remove-NewEmptyUnboundCanaryRun {
    param([string]$CanaryRoot, [string]$RunId)
    if ($RunId -cnotmatch '^[0-9a-f]{32}$') { return $false }
    try {
        [Hlclient.StockRuntimeDirectoryCapability]::DeleteExactEmptyChildDirectory(
            $CanaryRoot, $RunId)
        return $true
    } catch {
        return $false
    }
}

function Write-PreCampaignCanaryFailureContract {
    param(
        [string]$FailureCategory,
        [ValidateSet('exact', 'preserved', 'not-created')]
        [string]$EmptyUnboundCleanup)
    Write-Output "[stock-runtime-campaign] canary-failure-category=$FailureCategory"
    Write-Output "[stock-runtime-campaign] empty-unbound-cleanup=$EmptyUnboundCleanup"
    Write-Output '[stock-runtime-campaign] canary-accepted=false'
    Write-Output '[stock-runtime-campaign] campaign-runs-started=0'
}

if ($PSCmdlet.ParameterSetName -ceq 'UnboundRecoveryPolicy') {
    $captureScriptForFixture = Join-Path $PSScriptRoot 'capture_stock_runtime_state.ps1'
    $bootstrap = @(& $captureScriptForFixture -InitializeDirectoryCapability |
        ForEach-Object { $_.ToString() })
    if ($bootstrap -cnotcontains
            '[stock-runtime-capture] directory-capability=initialized' -or
        $null -eq ('Hlclient.StockRuntimeDirectoryCapability' -as [type])) {
        throw 'Unbound-canary fixture capability bootstrap failed.'
    }

    $temporaryParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    $fixtureRoot = Join-Path $temporaryParent `
        ('hlclient-stock-runtime-unbound-fixture-' + [guid]::NewGuid().ToString('N'))
    $emptyRunId = '11111111111111111111111111111111'
    $nonemptyRunId = '22222222222222222222222222222222'
    $emptyRun = Join-Path $fixtureRoot $emptyRunId
    $nonemptyRun = Join-Path $fixtureRoot $nonemptyRunId
    $marker = Join-Path $nonemptyRun 'diagnostic.marker'
    if (-not $fixtureRoot.StartsWith(
            $temporaryParent + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase) -or
        (Test-Path -LiteralPath $fixtureRoot)) {
        throw 'Unbound-canary fixture root is unsafe.'
    }

    $emptyCleanup = $false
    $nonemptyPreserved = $false
    try {
        [IO.Directory]::CreateDirectory($fixtureRoot) | Out-Null
        [IO.Directory]::CreateDirectory($emptyRun) | Out-Null
        $fakeCaptureOutput = @(
            '[stock-runtime-capture] active-capture=failed',
            '[stock-runtime-capture] failure-category=client-ready-not-observed',
            '[stock-runtime-capture] accepted-evidence-run=false',
            '[stock-runtime-capture] result=failed')
        $category = Get-PreCampaignCanaryFailureCategory $fakeCaptureOutput
        $emptyCleanup = Remove-NewEmptyUnboundCanaryRun $fixtureRoot $emptyRunId
        $contract = @(Write-PreCampaignCanaryFailureContract $category `
                $(if ($emptyCleanup) { 'exact' } else { 'preserved' }))
        if ($category -cne 'client-ready-not-observed' -or
            -not $emptyCleanup -or (Test-Path -LiteralPath $emptyRun) -or
            $contract -cnotcontains
                '[stock-runtime-campaign] canary-failure-category=client-ready-not-observed' -or
            $contract -cnotcontains
                '[stock-runtime-campaign] empty-unbound-cleanup=exact') {
            throw 'Empty unbound canary fixture did not surface and clean exactly.'
        }

        [IO.Directory]::CreateDirectory($nonemptyRun) | Out-Null
        [IO.File]::WriteAllBytes($marker, [byte[]]@(0x5a))
        $nonemptyRemoved = Remove-NewEmptyUnboundCanaryRun `
            $fixtureRoot $nonemptyRunId
        $nonemptyPreserved = -not $nonemptyRemoved -and
            (Test-Path -LiteralPath $marker -PathType Leaf)
        if (-not $nonemptyPreserved) {
            throw 'Nonempty unbound canary fixture was not preserved.'
        }
    } finally {
        if (Test-Path -LiteralPath $marker -PathType Leaf) {
            [IO.File]::Delete($marker)
        }
        foreach ($knownRun in @($emptyRun, $nonemptyRun)) {
            if (Test-Path -LiteralPath $knownRun -PathType Container) {
                if (@(Get-ChildItem -LiteralPath $knownRun -Force).Count -ne 0) {
                    throw 'Unbound-canary fixture cleanup encountered drift.'
                }
                [IO.Directory]::Delete($knownRun, $false)
            }
        }
        if (Test-Path -LiteralPath $fixtureRoot -PathType Container) {
            if (@(Get-ChildItem -LiteralPath $fixtureRoot -Force).Count -ne 0) {
                throw 'Unbound-canary fixture root cleanup encountered drift.'
            }
            [IO.Directory]::Delete($fixtureRoot, $false)
        }
    }
    Write-Output '[stock-runtime-unbound-canary-test] failure-category=client-ready-not-observed'
    Write-Output "[stock-runtime-unbound-canary-test] empty-cleanup=$($emptyCleanup.ToString().ToLowerInvariant())"
    Write-Output "[stock-runtime-unbound-canary-test] nonempty-preserved=$($nonemptyPreserved.ToString().ToLowerInvariant())"
    Write-Output '[stock-runtime-unbound-canary-test] accepted-run-rebound=false'
    Write-Output '[stock-runtime-unbound-canary-test] result=success'
    return
}

if ($PSCmdlet.ParameterSetName -ceq 'Policy') {
    $fake = @{
        'sequenced-c2s' = '110'; 'sequenced-s2c' = '120'
        'generation-a-client-to-server-packets' = '60'
        'generation-b-client-to-server-packets' = '60'
        'generation-a-server-to-client-packets' = '70'
        'generation-b-server-to-client-packets' = '70'
        'retired-generation-a-server-tail-packets' = '900'
    }
    $counts = Get-CampaignAttributedPacketCounts $fake 'reconnect'
    if ($counts.C2s -ne 110 -or $counts.S2c -ne 120 -or
        $counts.RetiredATail -ne 900 -or ($counts.S2c + $counts.RetiredATail) -ne 1020) {
        throw 'Generation-attributed policy self-test failed its valid case.'
    }
    $inflated = $fake.Clone()
    $inflated['sequenced-s2c'] = '1020'
    $inflationRejected = $false
    try { [void](Get-CampaignAttributedPacketCounts $inflated 'reconnect') }
    catch { $inflationRejected = $true }
    if (-not $inflationRejected) {
        throw 'Generation-attributed policy accepted retired-tail inflation.'
    }
    $reparseRejected = $false
    try {
        Assert-NotReparsePointItem ([pscustomobject]@{
                Attributes = [IO.FileAttributes]::Directory -bor
                    [IO.FileAttributes]::ReparsePoint
            }) 'fake campaign run'
    } catch { $reparseRejected = $true }
    if (-not $reparseRejected) {
        throw 'Campaign run reparse-point policy failed open.'
    }
    $short = $fake.Clone()
    $short['sequenced-s2c'] = '80'
    $shortCounts = Get-CampaignAttributedPacketCounts $short 'reconnect'
    if ($shortCounts.S2c -ge 100 -or
        ($shortCounts.S2c + $shortCounts.RetiredATail) -lt 100) {
        throw 'Per-run threshold policy did not isolate replay traffic from the tail.'
    }
    if ((Get-CampaignFailurePublication 'bounded-session-incomplete') -cne
            'incomplete' -or
        (Get-CampaignFailurePublication 'client-ready-not-observed') -cne
            'rejected' -or
        (Get-CampaignFailurePublication 'timeout') -cne 'rejected') {
        throw 'Typed campaign failure publication policy diverged.'
    }
    $fatalResumeCategories = @(
        'network_isolation_privilege_required',
        'research_restoration_failed',
        'external_steam_state_changed',
        'version_profile_mismatch',
        'raw_artifact_integrity_failed')
    $fatalResumeRejections = @($fatalResumeCategories | Where-Object {
            Test-CampaignFailureBlocksResume $_
        }).Count
    if ($fatalResumeRejections -ne $fatalResumeCategories.Count -or
        (Test-CampaignFailureBlocksResume 'bounded-session-incomplete')) {
        throw 'Fatal campaign resume-category policy failed open.'
    }
    Assert-CampaignResumeAllowed ([pscustomobject]@{
            ResumeBlockingFailureCount = 0 })
    $fatalResumeStateRejections = 0
    try {
        Assert-CampaignResumeAllowed ([pscustomobject]@{
                ResumeBlockingFailureCount = 1 })
    } catch { $fatalResumeStateRejections++ }
    if ($fatalResumeStateRejections -ne 1) {
        throw 'Retained fatal campaign state did not block resume.'
    }
    [void](Get-SuccessfulWalkerLines @('[stock-runtime-walk] result=success') 0)
    $walkerExitRejections = 0
    try {
        [void](Get-SuccessfulWalkerLines `
            @('[stock-runtime-walk] result=success') 17)
    } catch { $walkerExitRejections++ }
    if ($walkerExitRejections -ne 1) {
        throw 'Independent walker nonzero-exit policy failed open.'
    }
    $fakeCanaryBinding = [pscustomobject]@{
        ImplementationCommit = '1' * 40
        RunId = '2' * 32
        DeliveredSequencedS2c = 100
        ProfileFingerprint = '3' * 64
        TransportStructuralHash = '4' * 64
        ReplayStructuralHash = '5' * 64
        CheckerOutputHash = '6' * 64
        ExternalTargetProfile = 'none'
        ExternalTargetCount = 0
    }
    $fakeCanary = New-CanaryManifestValue $fakeCanaryBinding
    Assert-CanaryManifestContract $fakeCanary $fakeCanary
    $reviewedCanary = $fakeCanary | ConvertTo-Json -Depth 4 | ConvertFrom-Json
    $reviewedCanary.external_target_profile = 'reviewed-non-executable-v1'
    $reviewedCanary.external_target_count = 1
    $reviewedCanary.canary_structural_sha256 =
        Get-CanaryBindingSha256 $reviewedCanary
    Assert-CanaryManifestContract $reviewedCanary $reviewedCanary
    $externalTargetMetadataRejections = 0
    foreach ($mutation in @(
            { param($value) $value.external_target_profile =
                'syntactically-valid-unknown'; $value.external_target_count = 1 },
            { param($value) $value.external_target_profile =
                'reviewed-non-executable-v1'; $value.external_target_count = 0 },
            { param($value) $value.external_target_profile =
                'none'; $value.external_target_count = 1 },
            { param($value) $value.external_target_profile =
                'syntactically-valid-unknown'; $value.external_target_count = 0 })) {
        $mutated = $fakeCanary | ConvertTo-Json -Depth 4 | ConvertFrom-Json
        & $mutation $mutated
        $mutated.canary_structural_sha256 = Get-CanaryBindingSha256 $mutated
        try { Assert-CanaryManifestContract $mutated $mutated }
        catch { $externalTargetMetadataRejections++ }
    }
    if ($externalTargetMetadataRejections -ne 4) {
        throw 'Pre-campaign canary external-target metadata policy failed open.'
    }
    $mixedExternalTargetBindingRejections = 0
    foreach ($binding in @(
            [pscustomobject]@{ Count = 1; Profile =
                'reviewed-non-executable-v1'; ExpectedCount = 0;
                ExpectedProfile = 'none' },
            [pscustomobject]@{ Count = 2; Profile =
                'reviewed-non-executable-v1'; ExpectedCount = 1;
                ExpectedProfile = 'reviewed-non-executable-v1' })) {
        if (-not (Test-ExternalTargetBindingMatches $binding.Count `
                $binding.Profile $binding.ExpectedCount `
                $binding.ExpectedProfile)) {
            $mixedExternalTargetBindingRejections++
        }
    }
    if ($mixedExternalTargetBindingRejections -ne 2) {
        throw 'Mixed campaign external-target binding policy failed open.'
    }
    $canaryMutationRejections = 0
    foreach ($mutation in @(
            { param($value) $value.run_id = '7' * 32 },
            { param($value) $value.accepted_evidence_run = $false },
            { param($value) $value.delivered_sequenced_s2c_count = 99 },
            { param($value) $value.canary_structural_sha256 = '8' * 64 })) {
        $mutated = $fakeCanary | ConvertTo-Json -Depth 4 | ConvertFrom-Json
        & $mutation $mutated
        try { Assert-CanaryManifestContract $mutated $fakeCanary }
        catch { $canaryMutationRejections++ }
    }
    if ($canaryMutationRejections -ne 4) {
        throw 'Pre-campaign canary mutation policy failed open.'
    }
    if ((Get-CanaryResumeDisposition 1 0 $false) -cne
            'unbound-quarantine' -or
        (Get-CanaryResumeDisposition 0 0 $false) -cne 'fresh-capture' -or
        (Get-CanaryResumeDisposition 1 1 $true) -cne 'bound-reuse') {
        throw 'Pre-campaign canary resume policy failed open.'
    }
    Write-Output '[stock-runtime-campaign-policy] attributed-reconnect-packets=verified'
    Write-Output '[stock-runtime-campaign-policy] retired-tail-inflation-rejections=1'
    Write-Output '[stock-runtime-campaign-policy] run-reparse-rejections=1'
    Write-Output '[stock-runtime-campaign-policy] failure-publication-mutations=3'
    Write-Output "[stock-runtime-campaign-policy] fatal-resume-category-rejections=$fatalResumeRejections"
    Write-Output "[stock-runtime-campaign-policy] fatal-resume-state-rejections=$fatalResumeStateRejections"
    Write-Output "[stock-runtime-campaign-policy] walker-nonzero-exit-rejections=$walkerExitRejections"
    Write-Output '[stock-runtime-campaign-policy] external-target-metadata-acceptances=2'
    Write-Output "[stock-runtime-campaign-policy] external-target-metadata-rejections=$externalTargetMetadataRejections"
    Write-Output "[stock-runtime-campaign-policy] mixed-external-target-binding-rejections=$mixedExternalTargetBindingRejections"
    Write-Output '[stock-runtime-campaign-policy] canary-mutation-rejections=4'
    Write-Output '[stock-runtime-campaign-policy] unbound-canary-rebind-rejections=1'
    Write-Output '[stock-runtime-campaign-policy] files-written=0'
    Write-Output '[stock-runtime-campaign-policy] result=success'
    return
}

$requiredToken = 'HLCLIENT_STOCK_RUNTIME_ACTIVE_CAPTURE_V1'
if (-not $EnableActiveCapture -or $ConfirmActiveCapture -cne $requiredToken) {
    Write-Output '[stock-runtime-campaign] active-capture=explicit-opt-in-required'
    Write-Output '[stock-runtime-campaign] attempted-runs=0'
    throw 'First-observation campaign requires the exact explicit active-capture token.'
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
$requiredImplementationSubject =
    'Complete stock runtime capture campaign lifecycle'
$requiredOutputRoot = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'manual-artifacts\stock-runtime')).TrimEnd('\', '/')
$requiredCanaryOutputRoot = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'manual-artifacts\stock-runtime-canary')).TrimEnd('\', '/')
$output = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\', '/')
if ($output -ine $requiredOutputRoot) {
    throw 'OutputRoot must be the exact repository manual-artifacts/stock-runtime root.'
}
if (Test-Path -LiteralPath $output) {
    if (-not (Test-Path -LiteralPath $output -PathType Container)) {
        throw 'Campaign output root exists but is not a directory.'
    }
}
Assert-NoReparsePointInExistingPath $output 'Campaign output root'
$canaryOutput = $requiredCanaryOutputRoot
$captureScript = Join-Path $PSScriptRoot 'capture_stock_runtime_state.ps1'
$capabilityBootstrap = @(& $captureScript -InitializeDirectoryCapability |
    ForEach-Object { $_.ToString() })
if ($capabilityBootstrap -cnotcontains
        '[stock-runtime-capture] directory-capability=initialized' -or
    $capabilityBootstrap -cnotcontains
        '[stock-runtime-capture] files-written=0' -or
    $capabilityBootstrap -cnotcontains
        '[stock-runtime-capture] processes-started=0' -or
    $capabilityBootstrap -cnotcontains '[stock-runtime-capture] result=success' -or
    $null -eq ('Hlclient.StockRuntimeDirectoryCapability' -as [type])) {
    throw 'Retained directory capability bootstrap failed.'
}
$checker = [IO.Path]::GetFullPath($CheckerPath)
if (-not (Test-Path -LiteralPath $checker -PathType Leaf) -or
    [IO.Path]::GetFileName($checker) -cne 'hlclient_stock_runtime_check.exe' -or
    -not $checker.StartsWith(
        $repositoryRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'CheckerPath must name the repository-built stock-runtime checker.'
}
$walkerPath = [IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot 'walk_stock_runtime_transport.ps1'))
if (-not (Test-Path -LiteralPath $walkerPath -PathType Leaf) -or
    -not $walkerPath.StartsWith(
        $repositoryRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Repository independent transport walker is absent or untrusted.'
}

$matrix = [Collections.Generic.List[object]]::new()
function Add-CampaignRuns {
    param([string]$Map, [string]$Scenario, [int]$Count, [int]$Duration)
    for ($slot = 0; $slot -lt $Count; $slot++) {
        [void]$matrix.Add([pscustomobject]@{
                Map = $Map; Scenario = $Scenario; Slot = $slot
                Duration = $Duration })
    }
}
Add-CampaignRuns boot_camp baseline 6 $BaselineDurationSeconds
Add-CampaignRuns crossfire baseline 4 $BaselineDurationSeconds
Add-CampaignRuns stalkyard baseline 4 $BaselineDurationSeconds
Add-CampaignRuns crossfire idle-runtime 4 $IdleDurationSeconds
Add-CampaignRuns boot_camp drop-server-to-client-transport-ordinal 2 $BaselineDurationSeconds
Add-CampaignRuns crossfire duplicate-server-to-client-transport-ordinal 1 $BaselineDurationSeconds
Add-CampaignRuns stalkyard reorder-server-to-client-transport-ordinal 1 $BaselineDurationSeconds
Add-CampaignRuns boot_camp reconnect 2 $IdleDurationSeconds
if ($matrix.Count -ne 24) { throw 'Internal campaign matrix does not contain 24 slots.' }

function Read-BoundedJson {
    param(
        [string]$Path, [int]$MaximumBytes, [string]$Label,
        [string]$AnchorPath)
    $parent = [IO.Path]::GetFullPath((Split-Path -Parent $Path)).TrimEnd('\', '/')
    $leaf = [IO.Path]::GetFileName($Path)
    $capability = New-RetainedDirectoryCapability $parent $AnchorPath $Label
    try {
        return Read-CapabilityJson $capability $leaf $MaximumBytes $Label
    } finally {
        $capability.Dispose()
    }
}

function New-RetainedDirectoryCapability {
    param([string]$Path, [string]$AnchorPath, [string]$Label)
    try {
        return [Hlclient.StockRuntimeDirectoryCapability]::Open(
            $Path, $AnchorPath)
    } catch {
        throw "$Label retained identity could not be acquired: $($_.Exception.Message)"
    }
}

function Read-CapabilityJson {
    param(
        [object]$Capability, [string]$LeafName,
        [int]$MaximumBytes, [string]$Label)
    try {
        $bytes = $Capability.ReadExistingFile($LeafName, $MaximumBytes)
        $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes)
        return $text | ConvertFrom-Json
    } catch {
        throw "$Label retained-handle read failed: $($_.Exception.Message)"
    }
}

function Test-ExactBytes {
    param([byte[]]$Left, [byte[]]$Right)
    if ($null -eq $Left -or $null -eq $Right -or
        $Left.Length -ne $Right.Length) { return $false }
    for ($index = 0; $index -lt $Left.Length; $index++) {
        if ($Left[$index] -ne $Right[$index]) { return $false }
    }
    return $true
}

function Get-StringSha256 {
    param([string]$Value)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString(
                $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Value)))).Replace(
            '-', '').ToLowerInvariant()
    } finally { $sha.Dispose() }
}

function Assert-CampaignImplementationCommit {
    param([string]$Commit)
    if ($Commit -cnotmatch '^[0-9a-f]{40}$' -or
        $Commit -ceq '0000000000000000000000000000000000000000') {
        throw 'Campaign implementation commit is not a canonical nonzero SHA.'
    }
    $git = Get-Command git.exe -ErrorAction Stop
    $kind = @(& $git.Source -C $repositoryRoot cat-file -t $Commit 2>$null)
    if ($LASTEXITCODE -ne 0 -or $kind.Count -ne 1 -or $kind[0] -cne 'commit') {
        throw 'Campaign implementation commit does not resolve to a commit.'
    }
    $subject = @(& $git.Source -C $repositoryRoot show -s --format=%s `
        $Commit 2>$null)
    if ($LASTEXITCODE -ne 0 -or $subject.Count -ne 1 -or
        $subject[0] -cne $requiredImplementationSubject) {
        throw 'Campaign implementation commit has the wrong exact subject.'
    }
    & $git.Source -C $repositoryRoot merge-base --is-ancestor $Commit HEAD `
        2>$null
    if ($LASTEXITCODE -ne 0) {
        throw 'Campaign implementation commit is not an ancestor of HEAD.'
    }
}

function Resolve-CampaignImplementationCommit {
    $manifestPath = Join-Path $output 'campaign-manifest.json'
    if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
        $anchors = @(Get-ChildItem -LiteralPath $output -Force -Directory |
            Where-Object { $_.Name -cmatch '^[0-9a-f]{32}$' } |
            Sort-Object Name | Select-Object -First 1)
        if ($anchors.Count -ne 1) {
            throw 'Campaign manifest lacks one retained-identity run anchor.'
        }
        $manifest = Read-BoundedJson $manifestPath 131072 `
            'campaign manifest' $anchors[0].FullName
        if ([string]$manifest.schema -cne
                'hlclient.stock-runtime-first-campaign.v1') {
            throw 'Existing campaign manifest has the wrong schema.'
        }
        $pinned = [string]$manifest.implementation_commit
        Assert-CampaignImplementationCommit $pinned
        return $pinned
    }

    $git = Get-Command git.exe -ErrorAction Stop
    $candidates = @(& $git.Source -C $repositoryRoot log HEAD --format=%H `
        --fixed-strings --grep=$requiredImplementationSubject 2>$null)
    if ($LASTEXITCODE -ne 0 -or $candidates.Count -gt 4096) {
        throw 'Campaign implementation commit search failed.'
    }
    foreach ($candidate in $candidates) {
        if ($candidate -notmatch '^[0-9a-f]{40}$') { continue }
        $subject = @(& $git.Source -C $repositoryRoot show -s --format=%s `
            $candidate 2>$null)
        if ($LASTEXITCODE -eq 0 -and $subject.Count -eq 1 -and
            $subject[0] -ceq $requiredImplementationSubject) {
            Assert-CampaignImplementationCommit $candidate
            return $candidate
        }
    }
    throw 'No ancestor has the exact campaign implementation commit subject.'
}

function Invoke-FirstObservationChecker {
    param([string]$RunRoot)
    $saved = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = @(& $checker --capture-root $RunRoot --scenario first-observation `
            2>&1 | ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $saved }
    return [pscustomobject]@{ Lines = $lines; ExitCode = $exitCode }
}

$checkerOutputKeys = @(
    'profile', 'transport-valid', 'sequenced-c2s', 'sequenced-s2c',
    'fragments', 'duplicate-packets', 'old-packets',
    'delivered-sequenced-c2s', 'delivered-sequenced-s2c',
    'delivered-fragment-datagrams', 'reassembled', 'decompressed',
    'signon-replay', 'post-resource-boundary', 'boundary-payload-ordinal',
    'boundary-observed-ordinal', 'boundary-delivery-ordinal',
    'boundary-byte-offset', 'boundary-bit-offset', 'boundary-source-sequence',
    'boundary-source-payload-bytes', 'boundary-source-payload-bits',
    'boundary-next-unconsumed-bits', 'boundary-reassembled',
    'boundary-decompressed', 'boundary-byte-aligned', 'candidate-bit-width',
    'first-candidate', 'candidate-recurrence', 'candidate-stability',
    'accepted-run', 'publication-ready', 'result', 'structural-hash',
    'replay-structural-hash', 'connection-generation-count',
    'exact-boundary-count', 'runtime-candidate-count', 'generation-distinct',
    'candidate-conflict', 'retired-generation-a-tail-sink',
    'retired-generation-a-server-tail-packets',
    'generation-b-sequenced-after-fresh-accept')
$generationOutputSuffixes = @(
    'first-observed-ordinal', 'last-observed-ordinal',
    'connectionless-exchanges', 'first-sequenced-packet-ordinal',
    'client-to-server-packets', 'server-to-client-packets',
    'boundary-payload-ordinal', 'boundary-observed-ordinal',
    'boundary-delivery-ordinal', 'boundary-byte-offset', 'boundary-bit-offset',
    'boundary-source-sequence', 'boundary-source-payload-bytes',
    'boundary-source-payload-bits', 'boundary-next-unconsumed-bits',
    'boundary-reassembled', 'boundary-decompressed', 'boundary-byte-aligned',
    'candidate-bit-width', 'first-candidate', 'candidate-body-consumed',
    'candidate-semantic-category-assigned', 'replay-structural-hash')
foreach ($generationLabel in @('a', 'b')) {
    foreach ($suffix in $generationOutputSuffixes) {
        $checkerOutputKeys += "generation-$generationLabel-$suffix"
    }
}

function Convert-CheckerOutput {
    param(
        [string[]]$Lines,
        [string]$Label,
        [string[]]$AllowedKeys = $checkerOutputKeys,
        [string]$Prefix = '[stock-runtime] ')
    if ($Lines.Count -gt 128 -or
        [Text.Encoding]::UTF8.GetByteCount(($Lines -join "`n")) -gt 65536) {
        throw "$Label output exceeded its bound."
    }
    $values = [Collections.Generic.Dictionary[string, string]]::new(
        [StringComparer]::Ordinal)
    foreach ($line in $Lines) {
        if ($line.Length -gt 1024 -or
            $line -cnotmatch ('^' + [regex]::Escape($Prefix) +
                '(?<key>[a-z0-9-]+)=(?<value>[A-Za-z0-9_.:/-]{1,256})$')) {
            throw "$Label emitted a non-contract line."
        }
        $key = $Matches.key
        if ($AllowedKeys -cnotcontains $key -or $values.ContainsKey($key)) {
            throw "$Label emitted an unknown or duplicate key."
        }
        $values.Add($key, $Matches.value)
    }
    return $values
}

function Invoke-IndependentTransportWalker {
    param(
        [string]$RunRoot,
        [Collections.Generic.Dictionary[string, string]]$CheckerValues,
        [bool]$Reconnect)
    $arguments = @(
        '-CaptureRoot', $RunRoot,
        '-BoundaryPayloadOrdinal', $CheckerValues['boundary-payload-ordinal'],
        '-BoundaryObservedOrdinal', $CheckerValues['boundary-observed-ordinal'],
        '-BoundaryDeliveryOrdinal', $CheckerValues['boundary-delivery-ordinal'],
        '-BoundaryByteOffset', $CheckerValues['boundary-byte-offset'],
        '-BoundaryBitOffset', $CheckerValues['boundary-bit-offset'],
        '-BoundarySourceSequence', $CheckerValues['boundary-source-sequence'],
        '-BoundarySourcePayloadBytes',
            $CheckerValues['boundary-source-payload-bytes'],
        '-BoundarySourcePayloadBits',
            $CheckerValues['boundary-source-payload-bits'],
        '-BoundaryNextUnconsumedBits',
            $CheckerValues['boundary-next-unconsumed-bits'],
        '-BoundaryReassembled', $CheckerValues['boundary-reassembled'],
        '-BoundaryDecompressed', $CheckerValues['boundary-decompressed'],
        '-CandidateBitWidth', $CheckerValues['candidate-bit-width'],
        '-FirstCandidate', $CheckerValues['first-candidate'])
    if ($Reconnect) {
        $arguments += @(
            '-GenerationBBoundaryPayloadOrdinal',
                $CheckerValues['generation-b-boundary-payload-ordinal'],
            '-GenerationBBoundaryObservedOrdinal',
                $CheckerValues['generation-b-boundary-observed-ordinal'],
            '-GenerationBBoundaryDeliveryOrdinal',
                $CheckerValues['generation-b-boundary-delivery-ordinal'],
            '-GenerationBBoundaryByteOffset',
                $CheckerValues['generation-b-boundary-byte-offset'],
            '-GenerationBBoundaryBitOffset',
                $CheckerValues['generation-b-boundary-bit-offset'],
            '-GenerationBBoundarySourceSequence',
                $CheckerValues['generation-b-boundary-source-sequence'],
            '-GenerationBBoundarySourcePayloadBytes',
                $CheckerValues['generation-b-boundary-source-payload-bytes'],
            '-GenerationBBoundarySourcePayloadBits',
                $CheckerValues['generation-b-boundary-source-payload-bits'],
            '-GenerationBBoundaryNextUnconsumedBits',
                $CheckerValues['generation-b-boundary-next-unconsumed-bits'],
            '-GenerationBBoundaryReassembled',
                $CheckerValues['generation-b-boundary-reassembled'],
            '-GenerationBBoundaryDecompressed',
                $CheckerValues['generation-b-boundary-decompressed'],
            '-GenerationBCandidateBitWidth',
                $CheckerValues['generation-b-candidate-bit-width'],
            '-GenerationBFirstCandidate',
                $CheckerValues['generation-b-first-candidate'])
    }
    $saved = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $LASTEXITCODE = 0
        $lines = @(& $walkerPath @arguments 2>&1 |
            ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $saved }
    if ($lines.Count -gt 128 -or
        [Text.Encoding]::UTF8.GetByteCount(($lines -join "`n")) -gt 65536) {
        throw 'Independent transport walker output exceeded its bound.'
    }
    return ,(Get-SuccessfulWalkerLines $lines $exitCode)
}

function Assert-IndependentWalkerAgreement {
    param(
        [string]$RunRoot, [string]$RunId,
        [Collections.Generic.Dictionary[string, string]]$CheckerValues,
        [string]$Scenario, [string]$ExpectedExternalTargetProfile,
        [Int64]$ExpectedExternalTargetCount)
    $reconnect = $Scenario -ceq 'reconnect'
    $first = Invoke-IndependentTransportWalker $RunRoot $CheckerValues $reconnect
    $second = Invoke-IndependentTransportWalker $RunRoot $CheckerValues $reconnect
    if (($first -join "`n") -cne ($second -join "`n")) {
        throw 'Independent transport walker is non-deterministic.'
    }
    $keys = @(
        'run-id', 'journal-entries', 'raw-datagrams', 'raw-bytes',
        'observed-c2s', 'observed-s2c', 'delivered-c2s', 'delivered-s2c',
        'observed-connectionless-c2s', 'observed-connectionless-s2c',
        'observed-sequenced-c2s', 'observed-sequenced-s2c',
        'observed-fragment-datagrams', 'observed-reliable-datagrams',
        'delivered-connectionless-c2s', 'delivered-connectionless-s2c',
        'delivered-sequenced-c2s', 'delivered-sequenced-s2c',
        'delivered-fragment-datagrams', 'delivered-reliable-datagrams',
        'wrong-source-datagrams', 'emitted-datagrams',
        'last-observed-timestamp-us',
        'last-delivered-sequenced-s2c-timestamp-us', 'transport-complete',
        'observed-delivered-policy', 'final-manifest',
        'external-target-profile', 'external-target-count',
        'post-resource-boundary', 'boundary-payload-ordinal',
        'boundary-observed-ordinal', 'boundary-delivery-ordinal',
        'boundary-byte-offset', 'boundary-bit-offset',
        'boundary-source-sequence', 'boundary-source-payload-bytes',
        'boundary-source-payload-bits', 'boundary-next-unconsumed-bits',
        'boundary-reassembled', 'boundary-decompressed',
        'boundary-byte-aligned', 'candidate-bit-width', 'first-candidate',
        'replay-structural-hash', 'result')
    if ($reconnect) {
        $keys += @(
            'connection-generation-count', 'exact-boundary-count',
            'runtime-candidate-count', 'generation-distinct',
            'candidate-conflict', 'candidate-recurrence',
            'candidate-stability', 'retired-generation-a-tail-sink',
            'retired-generation-a-server-tail-packets',
            'generation-b-sequenced-after-fresh-accept')
        foreach ($label in @('a', 'b')) {
            foreach ($suffix in $generationOutputSuffixes) {
                $keys += "generation-$label-$suffix"
            }
        }
    }
    $walker = Convert-CheckerOutput $first 'independent transport walker' `
        $keys '[stock-runtime-walk] '
    if ($walker.Count -ne $keys.Count -or
        $walker['run-id'] -cne $RunId -or
        $walker['result'] -cne 'success' -or
        $walker['final-manifest'] -cne 'accepted' -or
        $walker['transport-complete'] -cne 'true' -or
        $walker['wrong-source-datagrams'] -cne '0' -or
        $walker['external-target-profile'] -cne
            $ExpectedExternalTargetProfile -or
        $walker['external-target-count'] -cne
            [string]$ExpectedExternalTargetCount -or
        $walker['post-resource-boundary'] -cne 'observed') {
        throw 'Independent walker did not validate the accepted publication.'
    }
    foreach ($key in @(
            'delivered-sequenced-c2s', 'delivered-sequenced-s2c',
            'delivered-fragment-datagrams', 'boundary-payload-ordinal',
            'boundary-observed-ordinal', 'boundary-delivery-ordinal',
            'boundary-byte-offset', 'boundary-bit-offset',
            'boundary-source-sequence', 'boundary-source-payload-bytes',
            'boundary-source-payload-bits', 'boundary-next-unconsumed-bits',
            'boundary-reassembled', 'boundary-decompressed',
            'boundary-byte-aligned', 'candidate-bit-width', 'first-candidate',
            'replay-structural-hash')) {
        if ($walker[$key] -cne $CheckerValues[$key]) {
            throw "Independent checker/walker field '$key' disagrees."
        }
    }
    if ($reconnect) {
        foreach ($key in @(
                'connection-generation-count', 'exact-boundary-count',
                'runtime-candidate-count', 'generation-distinct',
                'candidate-conflict', 'candidate-recurrence',
                'candidate-stability', 'retired-generation-a-tail-sink',
                'retired-generation-a-server-tail-packets',
                'generation-b-sequenced-after-fresh-accept')) {
            if ($walker[$key] -cne $CheckerValues[$key]) {
                throw "Independent reconnect aggregate '$key' disagrees."
            }
        }
        foreach ($label in @('a', 'b')) {
            foreach ($suffix in $generationOutputSuffixes) {
                $key = "generation-$label-$suffix"
                if ($walker[$key] -cne $CheckerValues[$key]) {
                    throw "Independent reconnect generation '$key' disagrees."
                }
            }
        }
    }
}

function Get-CanaryRootState {
    if (-not (Test-Path -LiteralPath $canaryOutput)) {
        return [pscustomobject]@{ Runs = @(); Manifests = @() }
    }
    if (-not (Test-Path -LiteralPath $canaryOutput -PathType Container)) {
        throw 'Pre-campaign canary root is not a directory.'
    }
    Assert-NoReparsePointInExistingPath $canaryOutput `
        'Pre-campaign canary root'
    $entries = @(Get-ChildItem -LiteralPath $canaryOutput -Force |
        Sort-Object Name)
    if ($entries.Count -gt 2) {
        throw 'Pre-campaign canary root exceeds its exact entry bound.'
    }
    foreach ($entry in $entries) {
        Assert-NotReparsePointItem $entry 'Pre-campaign canary entry'
    }
    $runs = @($entries | Where-Object {
            $_.PSIsContainer -and $_.Name -cmatch '^[0-9a-f]{32}$' })
    $manifests = @($entries | Where-Object {
            -not $_.PSIsContainer -and $_.Name -ceq 'canary-manifest.json' })
    if ($runs.Count -gt 1 -or $manifests.Count -gt 1 -or
        @($entries | Where-Object {
                ($_.PSIsContainer -and $_.Name -cnotmatch '^[0-9a-f]{32}$') -or
                (-not $_.PSIsContainer -and
                    $_.Name -cne 'canary-manifest.json') }).Count -ne 0) {
        throw 'Pre-campaign canary root contains an unknown or ambiguous entry.'
    }
    if ($manifests.Count -eq 1) {
        if ($manifests[0].Length -lt 2 -or $manifests[0].Length -gt 32768) {
            throw 'Pre-campaign canary manifest length is outside its bound.'
        }
        Assert-OnlyDefaultDataStream $manifests[0].FullName `
            'Pre-campaign canary manifest'
        Assert-NoHardLink $manifests[0].FullName `
            'Pre-campaign canary manifest'
    }
    return [pscustomobject]@{ Runs = $runs; Manifests = $manifests }
}

function Get-CanaryBindingForRun {
    param([object]$Directory, [string]$ImplementationCommit)
    $runCapability = New-RetainedDirectoryCapability `
        $Directory.FullName (Join-Path $Directory.FullName 'raw') `
        'Pre-campaign canary run'
    try {
        $manifest = Read-CapabilityJson $runCapability `
            'research-run-metadata.json' 262144 `
            'pre-campaign canary run manifest'
        if ([string]$manifest.schema -cne 'hlclient.stock-runtime-research-run.v1' -or
            [string]$manifest.run_id -cne $Directory.Name -or
            [string]$manifest.map_category -cne 'boot_camp' -or
            [string]$manifest.scenario -cne 'baseline' -or
            $manifest.accepted_evidence_run -isnot [bool] -or
            -not [bool]$manifest.accepted_evidence_run -or
            $manifest.accepted_transport_run -isnot [bool] -or
            -not [bool]$manifest.accepted_transport_run -or
            [string]$manifest.failure_category -cne 'none') {
            throw 'Pre-campaign canary run is not one accepted boot_camp/baseline publication.'
        }
        $externalTargetCount = Get-StrictInteger $manifest `
            external_target_count 0 4096
        if (-not (Test-ExternalTargetMetadata $externalTargetCount `
                ([string]$manifest.external_target_profile))) {
            throw 'Pre-campaign canary external-target metadata is invalid.'
        }
        $firstChecked = Invoke-FirstObservationChecker $Directory.FullName
        $secondChecked = Invoke-FirstObservationChecker $Directory.FullName
        if ($firstChecked.ExitCode -ne 0 -or $secondChecked.ExitCode -ne 0 -or
            ($firstChecked.Lines -join "`n") -cne
                ($secondChecked.Lines -join "`n") -or
            -not $runCapability.Revalidate()) {
            throw 'Pre-campaign canary checker is unsuccessful or non-deterministic.'
        }
        $values = Convert-CheckerOutput $firstChecked.Lines `
            'Pre-campaign canary checker'
        if ($values['accepted-run'] -cne 'true' -or
            $values['publication-ready'] -cne 'true' -or
            $values['result'] -cne 'first-observation' -or
            $values['transport-valid'] -cne 'true' -or
            $values['signon-replay'] -cne 'complete' -or
            $values['post-resource-boundary'] -cne 'observed' -or
            $values['candidate-recurrence'] -cne '1' -or
            $values['candidate-stability'] -cne 'single_observation' -or
            $values.ContainsKey('connection-generation-count') -or
            $values['structural-hash'] -cnotmatch '^[0-9a-f]{64}$' -or
            $values['replay-structural-hash'] -cnotmatch '^[0-9a-f]{64}$') {
            throw 'Pre-campaign canary checker did not prove the exact observation gate.'
        }
        Assert-IndependentWalkerAgreement $Directory.FullName `
            $Directory.Name $values 'baseline' `
            ([string]$manifest.external_target_profile) $externalTargetCount
        if (-not $runCapability.Revalidate()) {
            throw 'Pre-campaign canary identity changed during checker/walker validation.'
        }
        $packets = Get-CampaignAttributedPacketCounts $values 'baseline'
        if ($packets.S2c -lt 100) {
            throw 'Pre-campaign canary has fewer than 100 delivered sequenced S2C packets.'
        }
        $version = Read-CapabilityJson $runCapability `
            'version-observation.json' 65536 `
            'pre-campaign canary version observation'
        $profileText = '{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}' -f
            [string]$version.client_file_version,
            [string]$version.server_launcher_version,
            [string]$version.server_engine_version,
            [string]$version.protocol, [string]$version.server_build,
            [string]$version.steam_build_id,
            [string]$version.client_profile_fingerprint,
            [string]$version.server_profile_fingerprint
        return [pscustomobject]@{
            ImplementationCommit = $ImplementationCommit
            RunId = $Directory.Name
            DeliveredSequencedS2c = [Int64]$packets.S2c
            ProfileFingerprint = Get-StringSha256 $profileText
            TransportStructuralHash = [string]$values['structural-hash']
            ReplayStructuralHash = [string]$values['replay-structural-hash']
            CheckerOutputHash = Get-StringSha256 ($firstChecked.Lines -join "`n")
            ExternalTargetProfile = [string]$manifest.external_target_profile
            ExternalTargetCount = $externalTargetCount
        }
    } finally { $runCapability.Dispose() }
}

function Write-CanaryManifest {
    param([object]$Manifest, [object]$Capability)
    $state = Get-CanaryRootState
    if ($state.Runs.Count -ne 1 -or $state.Manifests.Count -ne 0 -or
        $state.Runs[0].Name -cne [string]$Manifest.run_id) {
        throw 'Pre-campaign canary publication state changed before binding.'
    }
    $text = ($Manifest | ConvertTo-Json -Depth 4) + "`n"
    if ([Text.Encoding]::UTF8.GetByteCount($text) -gt 32768) {
        throw 'Pre-campaign canary manifest exceeds its byte bound.'
    }
    if ($null -eq $Capability -or -not $Capability.Revalidate()) {
        throw 'Pre-campaign canary publication capability is invalid.'
    }
    $Capability.PublishNewFile(
        'canary-manifest.json',
        [Text.UTF8Encoding]::new($false, $true).GetBytes($text))
}

function Test-CampaignRootHasState {
    if (-not (Test-Path -LiteralPath $output)) { return $false }
    $entries = @(Get-ChildItem -LiteralPath $output -Force)
    return $entries.Count -ne 0
}

function Confirm-PreCampaignCanary {
    param([string]$ImplementationCommit)
    $state = Get-CanaryRootState
    $campaignHasState = Test-CampaignRootHasState
    $disposition = Get-CanaryResumeDisposition `
        $state.Runs.Count $state.Manifests.Count $campaignHasState
    if ($disposition -ceq 'bound-reuse') {
        if ($state.Runs.Count -ne 1) {
            throw 'Bound pre-campaign canary does not have exactly one run.'
        }
        $capability = New-RetainedDirectoryCapability `
            $canaryOutput $state.Runs[0].FullName `
            'Pre-campaign canary root'
        try {
            $binding = Get-CanaryBindingForRun `
                $state.Runs[0] $ImplementationCommit
            $expected = New-CanaryManifestValue $binding
            $published = Read-CapabilityJson $capability `
                'canary-manifest.json' 32768 `
                'pre-campaign canary manifest'
            Assert-CanaryManifestContract $published $expected
            return $binding
        } finally { $capability.Dispose() }
    }
    if ($disposition -ceq 'campaign-without-canary') {
        throw 'Campaign state exists without a bound pre-campaign canary.'
    }
    if ($disposition -ceq 'unbound-quarantine') {
        $exception = [InvalidOperationException]::new(
            'An unbound pre-campaign canary run is quarantined and is never recovered or rebound. Preserve it for diagnosis, then remove the ignored stock-runtime-canary root and rerun the campaign command to capture a fresh bound canary.')
        $exception.Data['CanaryFailureCategory'] = 'unbound_canary_state'
        $exception.Data['EmptyUnboundCleanup'] = 'preserved'
        throw $exception
    }
    if ($disposition -cne 'fresh-capture') {
        throw 'Pre-campaign canary resume state is invalid.'
    }

    if (-not (Test-LoopbackUdpPortPairAvailable `
            $FirstRelayPort ($FirstRelayPort + 1))) {
        throw 'Deterministic pre-campaign canary port pair is unavailable.'
    }

    $captureOutput = [Collections.Generic.List[string]]::new()
    try {
        & $captureScript `
            -EnableActiveCapture `
            -ConfirmActiveCapture $ConfirmActiveCapture `
            -PreCampaignCanary `
            -ResearchHalfLifeRoot $ResearchHalfLifeRoot `
            -ClientPath $ClientPath `
            -HldsPath $HldsPath `
            -CaptureToolPath $CaptureToolPath `
            -NetworkIsolationGuardPath $NetworkIsolationGuardPath `
            -AppManifestPath $AppManifestPath `
            -Game valve `
            -Map boot_camp `
            -Scenario baseline `
            -RelayPort $FirstRelayPort `
            -ServerPort ($FirstRelayPort + 1) `
            -OutputRoot $canaryOutput `
            -MaximumDurationSeconds $BaselineDurationSeconds |
            ForEach-Object { [void]$captureOutput.Add($_.ToString()) }
    } catch {
        $failedState = Get-CanaryRootState
        $typed = Get-PreCampaignCanaryFailureCategory $captureOutput
        $cleanup = 'preserved'
        $message = 'Pre-campaign canary failed; the 24-slot campaign was not started.'
        if ($failedState.Runs.Count -eq 0 -and
            $failedState.Manifests.Count -eq 0) {
            $cleanup = 'not-created'
        } elseif ($failedState.Runs.Count -eq 1 -and
            $failedState.Manifests.Count -eq 0) {
            if (Remove-NewEmptyUnboundCanaryRun `
                    $canaryOutput $failedState.Runs[0].Name) {
                $cleanup = 'exact'
            }
        } else {
            $message = 'Pre-campaign canary failure left an ambiguous publication.'
        }
        $exception = [InvalidOperationException]::new($message, $_.Exception)
        $exception.Data['CanaryFailureCategory'] = $typed
        $exception.Data['EmptyUnboundCleanup'] = $cleanup
        throw $exception
    }
    $completedState = Get-CanaryRootState
    $runLines = @($captureOutput | Where-Object {
            $_ -match '^\[stock-runtime-capture\] run-id=[0-9a-f]{32}$' })
    if ($completedState.Runs.Count -ne 1 -or
        $completedState.Manifests.Count -ne 0 -or $runLines.Count -ne 1 -or
        $captureOutput -cnotcontains
            '[stock-runtime-capture] accepted-evidence-run=true' -or
        $completedState.Runs[0].Name -cne
            $runLines[0].Substring('[stock-runtime-capture] run-id='.Length)) {
        throw 'Pre-campaign canary did not publish one unambiguous accepted run.'
    }
    $capability = New-RetainedDirectoryCapability `
        $canaryOutput $completedState.Runs[0].FullName `
        'Pre-campaign canary root'
    try {
        $binding = Get-CanaryBindingForRun `
            $completedState.Runs[0] $ImplementationCommit
        $expected = New-CanaryManifestValue $binding
        Write-CanaryManifest $expected $capability
        $persisted = Read-CapabilityJson $capability `
            'canary-manifest.json' 32768 `
            'pre-campaign canary manifest'
        Assert-CanaryManifestContract $persisted $expected
        return $binding
    } finally { $capability.Dispose() }
}

function Assert-CampaignProfileMatchesCanary {
    param([object]$State, [object]$Canary)
    if ([string]$State.ProfileFingerprint -cne 'evidence_pending' -and
        [string]$State.ProfileFingerprint -cne
            [string]$Canary.ProfileFingerprint) {
        throw 'Campaign stock profile differs from the accepted pre-campaign canary.'
    }
    if ([string]$State.ExternalTargetProfile -cne
            [string]$Canary.ExternalTargetProfile -or
        [Int64]$State.ExternalTargetCount -ne
            [Int64]$Canary.ExternalTargetCount) {
        throw 'Campaign external-target binding differs from the accepted pre-campaign canary.'
    }
}

$campaignSummaryOutputKeys = @(
    'profile', 'external-target-profile', 'external-target-count',
    'accepted', 'rejected', 'incomplete', 'pending',
    'sequenced-c2s', 'sequenced-s2c', 'reassembled', 'decompressed',
    'boundaries', 'candidates', 'reconnect-generations',
    'candidate-stability', 'threshold', 'implementation-commit',
    'structural-hash', 'result')

function Get-RunDirectories {
    if (-not (Test-Path -LiteralPath $output)) { return @() }
    $entries = @(Get-ChildItem -LiteralPath $output -Force | Sort-Object Name)
    $unknown = @($entries | Where-Object {
            (-not $_.PSIsContainer -and $_.Name -cne 'campaign-manifest.json') -or
            ($_.PSIsContainer -and $_.Name -cnotmatch '^[0-9a-f]{32}$') })
    if ($unknown.Count -ne 0) {
        throw 'Campaign root contains an unknown or non-canonical entry.'
    }
    foreach ($entry in $entries) {
        Assert-NotReparsePointItem $entry 'Campaign root entry'
    }
    $campaignManifest = @($entries | Where-Object {
            $_.Name -ceq 'campaign-manifest.json' })
    if ($campaignManifest.Count -gt 1 -or
        ($campaignManifest.Count -eq 1 -and
            ($campaignManifest[0].PSIsContainer -or
                $campaignManifest[0].Length -gt 131072))) {
        throw 'Campaign manifest entry is not one bounded regular file.'
    }
    if ($campaignManifest.Count -eq 1) {
        Assert-OnlyDefaultDataStream $campaignManifest[0].FullName `
            'Campaign manifest'
        Assert-NoHardLink $campaignManifest[0].FullName 'Campaign manifest'
    }
    $runs = @($entries | Where-Object { $_.PSIsContainer })
    if ($runs.Count -gt 4096) { throw 'Campaign run count exceeds its bound.' }
    return $runs
}

function New-EmptyCounts {
    return @{
        'boot_camp|baseline' = 0
        'crossfire|baseline' = 0
        'stalkyard|baseline' = 0
        'crossfire|idle-runtime' = 0
        'boot_camp|drop-server-to-client-transport-ordinal' = 0
        'crossfire|duplicate-server-to-client-transport-ordinal' = 0
        'stalkyard|reorder-server-to-client-transport-ordinal' = 0
        'boot_camp|reconnect' = 0
    }
}

function Get-TargetForKey {
    param([string]$Key)
    switch -CaseSensitive ($Key) {
        'boot_camp|baseline' { return 6 }
        'crossfire|baseline' { return 4 }
        'stalkyard|baseline' { return 4 }
        'crossfire|idle-runtime' { return 4 }
        'boot_camp|drop-server-to-client-transport-ordinal' { return 2 }
        'crossfire|duplicate-server-to-client-transport-ordinal' { return 1 }
        'stalkyard|reorder-server-to-client-transport-ordinal' { return 1 }
        'boot_camp|reconnect' { return 2 }
        default { return 0 }
    }
}

function Get-CampaignState {
    param([object]$Canary)
    $counts = New-EmptyCounts
    $accepted = 0; $rejected = 0; $incomplete = 0
    [Int64]$sequencedC2s = 0; [Int64]$sequencedS2c = 0
    [Int64]$reassembled = 0; [Int64]$decompressed = 0
    [Int64]$boundaries = 0; [Int64]$candidates = 0
    [Int64]$reconnectGenerations = 0
    $candidateProfile = $null; $candidateConflict = $false
    $profileFingerprint = $null
    $walkerValidatedRunIds = [Collections.Generic.List[string]]::new()
    $runs = @(Get-RunDirectories)

    foreach ($directory in $runs) {
        $runCapability = $null
        try {
            $runCapability = New-RetainedDirectoryCapability `
                $directory.FullName (Join-Path $directory.FullName 'raw') `
                'Campaign run'
            $manifest = Read-CapabilityJson $runCapability `
                'research-run-metadata.json' 262144 'research run manifest'
            if ([string]$manifest.schema -cne 'hlclient.stock-runtime-research-run.v1' -or
                [string]$manifest.run_id -cne $directory.Name -or
                $manifest.accepted_evidence_run -isnot [bool] -or
                $manifest.accepted_transport_run -isnot [bool]) {
                throw 'Run manifest identity is invalid.'
            }
            $runExternalTargetCount = Get-StrictInteger $manifest `
                external_target_count 0 4096
            if (-not (Test-ExternalTargetBindingMatches `
                    $runExternalTargetCount `
                    ([string]$manifest.external_target_profile) `
                    ([Int64]$Canary.ExternalTargetCount) `
                    ([string]$Canary.ExternalTargetProfile))) {
                throw 'Run manifest external-target metadata is invalid.'
            }
            if (-not $manifest.accepted_evidence_run) {
                if ([bool]$manifest.accepted_transport_run) {
                    throw 'Non-accepted evidence run claims accepted transport.'
                }
                $failure = [string]$manifest.failure_category
                if ([string]::IsNullOrWhiteSpace($failure) -or $failure -ceq 'none') {
                    throw 'Non-accepted run lacks a typed failure.'
                }
                if ((Get-CampaignFailurePublication $failure) -ceq 'incomplete') {
                    $incomplete++
                } else { $rejected++ }
                continue
            }

            $firstChecked = Invoke-FirstObservationChecker $directory.FullName
            $secondChecked = Invoke-FirstObservationChecker $directory.FullName
            if ($firstChecked.ExitCode -ne 0 -or $secondChecked.ExitCode -ne 0 -or
                ($firstChecked.Lines -join "`n") -cne
                    ($secondChecked.Lines -join "`n")) {
                throw 'Accepted publication checker is unsuccessful or non-deterministic.'
            }
            $checkerValues = Convert-CheckerOutput $firstChecked.Lines `
                'Accepted publication checker'
            if ($checkerValues['accepted-run'] -cne 'true' -or
                $checkerValues['result'] -cne 'first-observation' -or
                $checkerValues['transport-valid'] -cne 'true' -or
                $checkerValues['signon-replay'] -cne 'complete' -or
                $checkerValues['post-resource-boundary'] -cne 'observed' -or
                $checkerValues['profile'] -cne
                    'stock_protocol_48_build_10210_evidence_pending') {
                throw 'Accepted publication failed its checker.'
            }
            Assert-IndependentWalkerAgreement $directory.FullName `
                $directory.Name $checkerValues ([string]$manifest.scenario) `
                ([string]$Canary.ExternalTargetProfile) `
                ([Int64]$Canary.ExternalTargetCount)
            if (-not $runCapability.Revalidate()) {
                throw 'Campaign run identity changed during checker/walker validation.'
            }
            $version = Read-CapabilityJson $runCapability `
                'version-observation.json' 65536 'version observation'
            $profileText = '{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}' -f
                [string]$version.client_file_version,
                [string]$version.server_launcher_version,
                [string]$version.server_engine_version,
                [string]$version.protocol, [string]$version.server_build,
                [string]$version.steam_build_id,
                [string]$version.client_profile_fingerprint,
                [string]$version.server_profile_fingerprint
            $runProfile = Get-StringSha256 $profileText

            $key = '{0}|{1}' -f [string]$manifest.map_category,
                [string]$manifest.scenario
            $target = Get-TargetForKey $key
            if ($target -eq 0 -or $counts[$key] -ge $target) {
                $rejected++
                continue
            }
            $attributedPackets = Get-CampaignAttributedPacketCounts `
                $checkerValues ([string]$manifest.scenario)
            [Int64]$runC2s = $attributedPackets.C2s
            [Int64]$runS2c = $attributedPackets.S2c
            [Int64]$runReassembled = Get-CheckerInteger `
                $checkerValues 'reassembled' 0 131072
            [Int64]$runDecompressed = Get-CheckerInteger `
                $checkerValues 'decompressed' 0 131072
            if ($runS2c -lt 100) { $rejected++; continue }

            $generationCount = 1; $boundaryCount = 1; $candidateCount = 1
            if ([string]$manifest.scenario -ceq 'reconnect') {
                $generationProperty = $manifest.PSObject.Properties['connection_generation_count']
                $boundaryProperty = $manifest.PSObject.Properties['exact_boundary_count']
                $candidateProperty = $manifest.PSObject.Properties['runtime_candidate_count']
                $distinctProperty = $manifest.PSObject.Properties['generation_distinct']
                $conflictProperty = $manifest.PSObject.Properties['candidate_conflict']
                if ($null -eq $generationProperty -or $null -eq $boundaryProperty -or
                    $null -eq $candidateProperty -or $null -eq $distinctProperty -or
                    $null -eq $conflictProperty -or
                    [Int64]$generationProperty.Value -ne 2 -or
                    [Int64]$boundaryProperty.Value -ne 2 -or
                    [Int64]$candidateProperty.Value -ne 2 -or
                    $distinctProperty.Value -cne $true -or
                    $conflictProperty.Value -cne $false) {
                    $rejected++
                    continue
                }
                if ($checkerValues['connection-generation-count'] -cne '2' -or
                    $checkerValues['exact-boundary-count'] -cne '2' -or
                    $checkerValues['runtime-candidate-count'] -cne '2' -or
                    $checkerValues['generation-distinct'] -cne 'true' -or
                    $checkerValues['candidate-conflict'] -cne 'false' -or
                    $checkerValues['candidate-recurrence'] -cne '2' -or
                    $checkerValues['candidate-stability'] -cne 'stable_observation' -or
                    $checkerValues['retired-generation-a-tail-sink'] -cne
                        'routing_only' -or
                    $checkerValues['generation-b-sequenced-after-fresh-accept'] -cne
                        'true') {
                    $rejected++
                    continue
                }
                $generationCount = 2; $boundaryCount = 2; $candidateCount = 2
            } elseif ($checkerValues.ContainsKey('connection-generation-count')) {
                $rejected++
                continue
            }
            if ($null -eq $profileFingerprint) {
                $profileFingerprint = $runProfile
            } elseif ($profileFingerprint -cne $runProfile) {
                $rejected++
                continue
            }
            $currentCandidate = '{0}|{1}|{2}|{3}' -f
                [string]$checkerValues['boundary-bit-offset'],
                [string]$checkerValues['boundary-byte-aligned'],
                [string]$checkerValues['candidate-bit-width'],
                [string]$checkerValues['first-candidate']
            if ($null -eq $candidateProfile) { $candidateProfile = $currentCandidate }
            elseif ($candidateProfile -cne $currentCandidate) { $candidateConflict = $true }

            $counts[$key]++
            $accepted++
            $sequencedC2s += $runC2s
            $sequencedS2c += $runS2c
            $reassembled += $runReassembled
            $decompressed += $runDecompressed
            $boundaries += $boundaryCount
            $candidates += $candidateCount
            if ([string]$manifest.scenario -ceq 'reconnect') {
                $reconnectGenerations += $generationCount
            }
            [void]$walkerValidatedRunIds.Add($directory.Name)
        } catch { $rejected++ }
        finally {
            if ($null -ne $runCapability) { $runCapability.Dispose() }
        }
    }

    $pending = 24 - $accepted
    $threshold = if ($candidateConflict) { 'conflicting' }
        elseif ($accepted -eq 24 -and $pending -eq 0 -and
            $sequencedS2c -ge 1000 -and $reconnectGenerations -ge 4 -and
            $boundaries -ge 26 -and $candidates -ge 26) { 'passed' }
        else { 'pending' }
    return [pscustomobject]@{
        Runs = $runs; Counts = $counts; Accepted = $accepted
        Rejected = $rejected; Incomplete = $incomplete; Pending = $pending
        SequencedC2s = $sequencedC2s; SequencedS2c = $sequencedS2c
        Reassembled = $reassembled; Decompressed = $decompressed
        Boundaries = $boundaries; Candidates = $candidates
        ReconnectGenerations = $reconnectGenerations
        CandidateStability = $(if ($candidateConflict) { 'candidate_conflicting' }
            elseif ($candidates -ge 2) { 'stable_observation' }
            else { 'evidence_pending' })
        Threshold = $threshold
        ProfileFingerprint = $(if ($null -eq $profileFingerprint) {
                'evidence_pending' } else { $profileFingerprint })
        ExternalTargetProfile = [string]$Canary.ExternalTargetProfile
        ExternalTargetCount = [Int64]$Canary.ExternalTargetCount
        CampaignStructuralHash = 'evidence_pending'
        WalkerValidatedRunIds = $walkerValidatedRunIds.ToArray()
        ResumeBlockingFailureCount = [int]$rejected
    }
}

function Get-CampaignManifestBytes {
    param([object]$State)
    if ([string]$State.CampaignStructuralHash -cnotmatch '^[0-9a-f]{64}$') {
        throw 'Campaign structural hash is not ready for publication.'
    }
    $requiredMatrix = @()
    foreach ($key in @($State.Counts.Keys | Sort-Object)) {
        $parts = $key.Split('|')
        $requiredMatrix += [ordered]@{
            map_category = $parts[0]; scenario = $parts[1]
            required_runs = Get-TargetForKey $key
            accepted_runs = [int]$State.Counts[$key]
        }
    }
    $manifest = [ordered]@{
        schema = 'hlclient.stock-runtime-first-campaign.v1'
        implementation_commit = $implementationCommit
        profile_fingerprint = [string]$State.ProfileFingerprint
        external_target_profile = [string]$State.ExternalTargetProfile
        external_target_count = [Int64]$State.ExternalTargetCount
        required_matrix = $requiredMatrix
        attempted_slots = [int]$State.Runs.Count
        accepted_slots = [int]$State.Accepted
        rejected_slots = [int]$State.Rejected
        incomplete_slots = [int]$State.Incomplete
        pending_slots = [int]$State.Pending
        packet_totals = [ordered]@{
            sequenced_c2s = [Int64]$State.SequencedC2s
            sequenced_s2c = [Int64]$State.SequencedS2c
            reassembled = [Int64]$State.Reassembled
            decompressed = [Int64]$State.Decompressed
        }
        boundary_totals = [ordered]@{
            exact = [Int64]$State.Boundaries
            candidates = [Int64]$State.Candidates
            reconnect_generations = [Int64]$State.ReconnectGenerations
        }
        candidate_stability = [string]$State.CandidateStability
        threshold_status = [string]$State.Threshold
        campaign_structural_sha256 = [string]$State.CampaignStructuralHash
    }
    $text = ($manifest | ConvertTo-Json -Depth 8) + "`n"
    if ([Text.Encoding]::UTF8.GetByteCount($text) -gt 131072) {
        throw 'Campaign manifest exceeds its byte bound.'
    }
    return [Text.UTF8Encoding]::new($false, $true).GetBytes($text)
}

function Invoke-CampaignSummaryChecker {
    param([object]$State, [switch]$Refresh)
    $checkerArguments = @('--capture-root', $output, '--scenario',
        'campaign-summary')
    if ($Refresh) {
        $checkerArguments += @(
            '--campaign-refresh-implementation-commit', $implementationCommit,
            '--campaign-external-target-profile',
                [string]$State.ExternalTargetProfile,
            '--campaign-external-target-count',
                [string]$State.ExternalTargetCount)
    }
    foreach ($runId in @($State.WalkerValidatedRunIds | Sort-Object)) {
        if ([string]$runId -cnotmatch '^[0-9a-f]{32}$') {
            throw 'Walker-validated campaign run ID is malformed.'
        }
        $checkerArguments += @('--independent-walker-validated-run', $runId)
    }
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = @(& $checker @checkerArguments 2>&1 |
            ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    return [pscustomobject]@{ Lines = $lines; ExitCode = $exitCode }
}

function Assert-CampaignSummaryMatchesState {
    param([object]$Values, [object]$State)
    if ($Values.Count -ne $campaignSummaryOutputKeys.Count -or
        $Values['profile'] -cne
            'stock_protocol_48_build_10210_evidence_pending' -or
        $Values['external-target-profile'] -cne
            [string]$State.ExternalTargetProfile -or
        $Values['external-target-count'] -cne
            [string]$State.ExternalTargetCount -or
        $Values['result'] -cne 'campaign-summary' -or
        $Values['accepted'] -cne [string]$State.Accepted -or
        $Values['rejected'] -cne [string]$State.Rejected -or
        $Values['incomplete'] -cne [string]$State.Incomplete -or
        $Values['pending'] -cne [string]$State.Pending -or
        $Values['sequenced-c2s'] -cne [string]$State.SequencedC2s -or
        $Values['sequenced-s2c'] -cne [string]$State.SequencedS2c -or
        $Values['reassembled'] -cne [string]$State.Reassembled -or
        $Values['decompressed'] -cne [string]$State.Decompressed -or
        $Values['boundaries'] -cne [string]$State.Boundaries -or
        $Values['candidates'] -cne [string]$State.Candidates -or
        $Values['reconnect-generations'] -cne
            [string]$State.ReconnectGenerations -or
        $Values['candidate-stability'] -cne
            [string]$State.CandidateStability -or
        $Values['threshold'] -cne [string]$State.Threshold -or
        $Values['implementation-commit'] -cne $implementationCommit -or
        $Values['structural-hash'] -cnotmatch '^[0-9a-f]{64}$') {
        throw 'Campaign checker summary disagrees with the runner state.'
    }
}

function Get-DeterministicCampaignSummary {
    param([object]$State, [switch]$Refresh)
    $first = Invoke-CampaignSummaryChecker $State -Refresh:$Refresh
    $second = Invoke-CampaignSummaryChecker $State -Refresh:$Refresh
    if ($first.ExitCode -ne 0 -or $second.ExitCode -ne 0 -or
        ($first.Lines -join "`n") -cne ($second.Lines -join "`n")) {
        throw 'Campaign checker summary is unsuccessful or non-deterministic.'
    }
    $values = Convert-CheckerOutput $first.Lines 'Campaign checker summary' `
        $campaignSummaryOutputKeys
    Assert-CampaignSummaryMatchesState $values $State
    return [pscustomobject]@{ Lines = $first.Lines; Values = $values }
}

function New-CampaignPublicationSession {
    param([object]$State)
    if ($State.Runs.Count -eq 0) {
        if (Test-Path -LiteralPath (Join-Path $output 'campaign-manifest.json')) {
            throw 'Empty campaign unexpectedly has a persisted manifest.'
        }
        return [pscustomobject]@{
            Capability = $null
            PriorBytes = $null
        }
    }
    $anchor = @($State.Runs | Sort-Object Name | Select-Object -First 1)
    if ($anchor.Count -ne 1) {
        throw 'Campaign validation lacks one retained-identity anchor.'
    }
    $capability = New-RetainedDirectoryCapability `
        $output $anchor[0].FullName 'Campaign root'
    try {
        try {
            $prior = $capability.ReadExistingFile(
                'campaign-manifest.json', 131072)
        } catch {
            Write-Output '[stock-runtime-campaign] failure-category=campaign_progress_manifest_missing_or_unsafe'
            Write-Output '[stock-runtime-campaign] new-campaign-runs-started=0'
            throw 'Campaign run directories exist without one safe progress manifest. This ambiguous crash/mutation window is quarantined and cannot be resumed automatically.'
        }
        $persisted = Get-DeterministicCampaignSummary $State
        $confirmed = $capability.ReadExistingFile(
            'campaign-manifest.json', 131072)
        if (-not (Test-ExactBytes $prior $confirmed) -or
            -not $capability.Revalidate()) {
            throw 'Existing campaign manifest changed during independent validation.'
        }
        return [pscustomobject]@{
            Capability = $capability
            PriorBytes = $confirmed
        }
    } catch {
        $capability.Dispose()
        throw
    }
}

function Publish-CampaignManifest {
    param([object]$State, [object]$Session)
    if ($State.Runs.Count -eq 0) {
        if ($null -ne $Session.Capability -or
            $null -ne $Session.PriorBytes -or
            (Test-Path -LiteralPath (Join-Path $output 'campaign-manifest.json'))) {
            throw 'Empty campaign publication state is not exact.'
        }
        return $null
    }
    if ($null -eq $Session.Capability) {
        $anchor = @($State.Runs | Sort-Object Name | Select-Object -First 1)
        if ($anchor.Count -ne 1) {
            throw 'Campaign first publication lacks one retained-identity anchor.'
        }
        $Session.Capability = New-RetainedDirectoryCapability `
            $output $anchor[0].FullName 'Campaign root'
    }
    $capability = $Session.Capability
    if (-not $capability.Revalidate()) {
        throw 'Campaign manifest publication capability is invalid.'
    }
    $refreshed = Get-DeterministicCampaignSummary $State -Refresh
    $State.CampaignStructuralHash = [string]$refreshed.Values['structural-hash']
    [byte[]]$nextBytes = Get-CampaignManifestBytes $State
    if ($null -eq $Session.PriorBytes) {
        $capability.PublishNewFile('campaign-manifest.json', $nextBytes)
    } else {
        $capability.PublishReplacingFile(
            'campaign-manifest.json', $Session.PriorBytes, $nextBytes)
    }
    [byte[]]$persistedBytes = $capability.ReadExistingFile(
        'campaign-manifest.json', 131072)
    if (-not (Test-ExactBytes $nextBytes $persistedBytes)) {
        throw 'Persisted campaign manifest differs from the exact publication bytes.'
    }
    $persisted = Get-DeterministicCampaignSummary $State
    [byte[]]$confirmedBytes = $capability.ReadExistingFile(
        'campaign-manifest.json', 131072)
    if (-not (Test-ExactBytes $persistedBytes $confirmedBytes) -or
        ($persisted.Lines -join "`n") -cne ($refreshed.Lines -join "`n") -or
        $persisted.Values['structural-hash'] -cne
            [string]$State.CampaignStructuralHash -or
        -not $capability.Revalidate()) {
        throw 'Persisted campaign manifest does not bind the refreshed summary.'
    }
    $Session.PriorBytes = $confirmedBytes
    return $persisted
}

function Test-LoopbackUdpPortPairAvailable {
    param([int]$RelayPort, [int]$ServerPort)
    $relay = $null; $server = $null
    try {
        $relay = [Net.Sockets.UdpClient]::new()
        $relay.ExclusiveAddressUse = $true
        $relay.Client.Bind([Net.IPEndPoint]::new(
                [Net.IPAddress]::Loopback, $RelayPort))
        $server = [Net.Sockets.UdpClient]::new()
        $server.ExclusiveAddressUse = $true
        $server.Client.Bind([Net.IPEndPoint]::new(
                [Net.IPAddress]::Loopback, $ServerPort))
        return $true
    } catch { return $false }
    finally {
        if ($null -ne $server) { $server.Dispose() }
        if ($null -ne $relay) { $relay.Dispose() }
    }
}

$implementationCommit = Resolve-CampaignImplementationCommit
try {
    $canaryBinding = Confirm-PreCampaignCanary $implementationCommit
} catch {
    $failureCategory = 'pre_campaign_canary_failed'
    $emptyUnboundCleanup = 'preserved'
    if ($_.Exception.Data.Contains('CanaryFailureCategory')) {
        $failureCategory = [string]$_.Exception.Data['CanaryFailureCategory']
    }
    if ($_.Exception.Data.Contains('EmptyUnboundCleanup')) {
        $emptyUnboundCleanup = [string]$_.Exception.Data['EmptyUnboundCleanup']
    }
    Write-PreCampaignCanaryFailureContract `
        $failureCategory $emptyUnboundCleanup
    throw
}
Write-Output "[stock-runtime-campaign] canary-run-id=$($canaryBinding.RunId)"
Write-Output '[stock-runtime-campaign] canary-accepted=true'
Write-Output '[stock-runtime-campaign] canary-counted-in-matrix=false'
$state = Get-CampaignState $canaryBinding
Assert-CampaignResumeAllowed $state
Assert-CampaignProfileMatchesCanary $state $canaryBinding
$publicationSession = New-CampaignPublicationSession $state
try {
    if ($state.Threshold -ceq 'passed') {
        Write-Output '[stock-runtime-campaign] resume=already-complete'
    }

    while ($state.Threshold -cne 'passed') {
    if ($state.Threshold -ceq 'conflicting') {
        throw 'Campaign has conflicting neutral candidate observations; evidence remains blocked.'
    }
    $case = $null
    foreach ($candidateCase in $matrix) {
        $key = '{0}|{1}' -f $candidateCase.Map, $candidateCase.Scenario
        if ($candidateCase.Slot -ge [int]$state.Counts[$key]) {
            $case = $candidateCase
            break
        }
    }
    if ($null -eq $case) { throw 'Campaign has no fillable slot but threshold is pending.' }

    $attemptOrdinal = [int]$state.Runs.Count
    if ($attemptOrdinal -ge 256) { throw 'Campaign attempt bound is exhausted.' }
    $relayPort = $FirstRelayPort + 2 + ($attemptOrdinal * 2)
    $serverPort = $relayPort + 1
    if ($serverPort -gt 65535) { throw 'Deterministic campaign port range is exhausted.' }
    if (-not (Test-LoopbackUdpPortPairAvailable $relayPort $serverPort)) {
        throw 'Deterministic loopback campaign port pair is unavailable.'
    }

    $captureOutput = [Collections.Generic.List[string]]::new()
    try {
        & $captureScript `
            -EnableActiveCapture `
            -ConfirmActiveCapture $ConfirmActiveCapture `
            -ResearchHalfLifeRoot $ResearchHalfLifeRoot `
            -ClientPath $ClientPath `
            -HldsPath $HldsPath `
            -CaptureToolPath $CaptureToolPath `
            -NetworkIsolationGuardPath $NetworkIsolationGuardPath `
            -AppManifestPath $AppManifestPath `
            -Game valve `
            -Map $case.Map `
            -Scenario $case.Scenario `
            -RelayPort $relayPort `
            -ServerPort $serverPort `
            -OutputRoot $output `
            -MaximumDurationSeconds $case.Duration |
            ForEach-Object { [void]$captureOutput.Add($_.ToString()) }
        $runLines = @($captureOutput | Where-Object {
                $_ -match '^\[stock-runtime-capture\] run-id=[0-9a-f]{32}$' })
        if ($runLines.Count -ne 1 -or
            $captureOutput -cnotcontains '[stock-runtime-capture] accepted-evidence-run=true') {
            throw 'Capture did not publish exactly one accepted run ID.'
        }
        $runId = $runLines[0].Substring('[stock-runtime-capture] run-id='.Length)
        $newRunRoot = Join-Path $output $runId
        $checked = Invoke-FirstObservationChecker $newRunRoot
        if ($checked.ExitCode -ne 0 -or
            $checked.Lines -cnotcontains '[stock-runtime] accepted-run=true') {
            throw 'Final checker did not accept the newly published run.'
        }
        $checkedValues = Convert-CheckerOutput $checked.Lines `
            'newly published run checker'
        Assert-IndependentWalkerAgreement $newRunRoot $runId $checkedValues `
            ([string]$case.Scenario) `
            ([string]$canaryBinding.ExternalTargetProfile) `
            ([Int64]$canaryBinding.ExternalTargetCount)
    } catch {
        $state = Get-CampaignState $canaryBinding
        Assert-CampaignProfileMatchesCanary $state $canaryBinding
        [void](Publish-CampaignManifest $state $publicationSession)
        $failure = @($captureOutput | Where-Object {
                $_ -match '^\[stock-runtime-capture\] failure-category=[A-Za-z0-9_.:-]+$' } |
            Select-Object -Last 1)
        $typed = if ($failure.Count -eq 1) {
            $failure[0].Substring('[stock-runtime-capture] failure-category='.Length)
        } else { 'campaign_run_failed' }
        Write-Output "[stock-runtime-campaign] failure-category=$typed"
        Write-Output "[stock-runtime-campaign] attempted-runs=$($state.Runs.Count)"
        Write-Output "[stock-runtime-campaign] accepted-runs=$($state.Accepted)"
        Write-Output "[stock-runtime-campaign] rejected-runs=$($state.Rejected)"
        Write-Output "[stock-runtime-campaign] incomplete-runs=$($state.Incomplete)"
        Write-Output "[stock-runtime-campaign] pending-runs=$($state.Pending)"
        Write-Output '[stock-runtime-campaign] result=failed'
        Assert-CampaignResumeAllowed $state
        throw 'Campaign stopped fail-closed; rerun the same command to resume missing slots.'
    }

    $state = Get-CampaignState $canaryBinding
    Assert-CampaignProfileMatchesCanary $state $canaryBinding
    [void](Publish-CampaignManifest $state $publicationSession)
    Assert-CampaignResumeAllowed $state
    Write-Output ("[stock-runtime-campaign] scenario={0} map={1} accepted={2}/24 pending={3}" -f
        $case.Scenario, $case.Map, $state.Accepted, $state.Pending)
    }

    $finalSummary = Get-DeterministicCampaignSummary $state
    if ($finalSummary.Values['threshold'] -cne 'passed' -or
        $finalSummary.Values['result'] -cne 'campaign-summary') {
        throw 'Final campaign summary is unsuccessful or non-deterministic.'
    }

    Write-Output "[stock-runtime-campaign] attempted-runs=$($state.Runs.Count)"
    Write-Output "[stock-runtime-campaign] accepted-runs=$($state.Accepted)"
    Write-Output "[stock-runtime-campaign] rejected-runs=$($state.Rejected)"
    Write-Output "[stock-runtime-campaign] incomplete-runs=$($state.Incomplete)"
    Write-Output "[stock-runtime-campaign] pending-runs=$($state.Pending)"
    Write-Output "[stock-runtime-campaign] sequenced-s2c=$($state.SequencedS2c)"
    Write-Output "[stock-runtime-campaign] boundaries=$($state.Boundaries)"
    Write-Output "[stock-runtime-campaign] candidates=$($state.Candidates)"
    Write-Output "[stock-runtime-campaign] reconnect-generations=$($state.ReconnectGenerations)"
    Write-Output "[stock-runtime-campaign] implementation-commit=$implementationCommit"
    Write-Output "[stock-runtime-campaign] structural-hash=$($state.CampaignStructuralHash)"
    Write-Output '[stock-runtime-campaign] input-automation-used=false'
    Write-Output '[stock-runtime-campaign] evidence-json-written=false'
    Write-Output '[stock-runtime-campaign] result=success'
} finally {
    if ($null -ne $publicationSession -and
        $null -ne $publicationSession.Capability) {
        $publicationSession.Capability.Dispose()
    }
}
