#requires -Version 5.1

[CmdletBinding()]
param(
    [string]$WriterTraceToolPath = '',
    [string]$SmokeArtifactRoot = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$module = Join-Path $PSScriptRoot 'stock_writer_trace_handoff.psm1'
Import-Module $module -Force

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Save-SmokeArtifact {
    param([string]$Source, [string]$Name)
    if ([string]::IsNullOrWhiteSpace($SmokeArtifactRoot)) { return }
    if (-not (Test-Path -LiteralPath $SmokeArtifactRoot -PathType Container)) {
        [void][IO.Directory]::CreateDirectory($SmokeArtifactRoot)
    }
    Copy-Item -LiteralPath $Source -Destination (
        Join-Path -Path $SmokeArtifactRoot -ChildPath $Name)
}

$transaction = '0123456789abcdef0123456789abcdef'

function New-TraceFixture {
    param(
        [Int64]$ProviderEvents = 0,
        [Int64]$RelevantOperations = 0,
        [Int64]$Mutations = 0,
        [Int64]$TargetMutations = 0,
        [Int64]$ParentMutations = 0,
        [Int64]$MappedMutations = 0,
        [Int64]$CompletionRequired = 0,
        [Int64]$CompletionsObserved = 0,
        [Int64]$SuccessfulCompletions = 0,
        [Int64]$FailedCompletions = 0,
        [Int64]$EventsLost = 0,
        [Int64]$QueueDrops = 0,
        [Int64]$MapEvictions = 0,
        [Int64]$UnresolvedCandidates = 0,
        [bool]$Overflow = $false,
        [bool]$SchemaFailure = $false,
        [bool]$ProviderReady = $true,
        [bool]$ConsumerReady = $true,
        [bool]$TargetBinding = $true,
        [bool]$FilterBinding = $true,
        [bool]$PreexistingHandlesAbsent = $true,
        [bool]$NameCorrelation = $true,
        [bool]$NormalStop = $true
    )
    $captureComplete = $ProviderReady -and $ConsumerReady -and $NormalStop -and
        $EventsLost -eq 0 -and $QueueDrops -eq 0 -and -not $Overflow -and
        -not $SchemaFailure
    $targetReady = $TargetBinding -and $FilterBinding -and
        $PreexistingHandlesAbsent -and $NameCorrelation -and
        $MapEvictions -eq 0 -and $UnresolvedCandidates -eq 0
    $applicable = $Mutations -gt 0
    $correlationComplete = $applicable -and $MappedMutations -eq $Mutations -and
        $CompletionsObserved -eq $CompletionRequired
    $correlation = if (-not $applicable) { 'not_applicable' } elseif (
        $correlationComplete) { 'complete' } else { 'incomplete' }
    $writer = if (-not $applicable) {
        $(if ($captureComplete -and $targetReady) { 'not_observed' } else { 'unresolved' })
    } elseif ($correlationComplete) { 'attributed' } else { 'unresolved' }
    $result = if ($captureComplete -and $targetReady -and -not $applicable) {
        'complete_no_target_mutation_observed'
    } elseif ($captureComplete -and $targetReady -and $correlationComplete) {
        'complete_target_mutation_attributed'
    } else { 'incomplete' }
    return [pscustomobject][ordered]@{
        schema = 'hlclient.writer-trace.redacted.v2'
        contract_version = 2
        transaction_id = '0123456789abcdef0123456789abcdef'
        total_provider_events = $ProviderEvents
        relevant_operations = $RelevantOperations
        mutating_operations = $Mutations
        target_mutating_operations = $TargetMutations
        parent_mutating_operations = $ParentMutations
        process_mapped_mutations = $MappedMutations
        completion_required_mutations = $CompletionRequired
        mutation_completions_observed = $CompletionsObserved
        successful_completions = $SuccessfulCompletions
        failed_completions = $FailedCompletions
        events_lost = $EventsLost
        log_buffers_lost = 0
        realtime_buffers_lost = 0
        overflow = $Overflow
        schema_failure = $SchemaFailure
        schema_count = 0
        queue_drops = $QueueDrops
        correlation_map_evictions = $MapEvictions
        unresolved_mutation_candidates = $UnresolvedCandidates
        unresolved_target_mutation_candidates = $(if (
            $PreexistingHandlesAbsent) { 0 } else {
                [Math]::Max(1, $UnresolvedCandidates)
            })
        explicit_stop_success = $NormalStop
        session_absent_after_stop = $true
        capture_coverage = $(if ($captureComplete) { 'complete' } else { 'incomplete' })
        target_scope_observation = $(if ($targetReady) { 'ready' } else { 'incomplete' })
        correlation_applicable = $applicable
        mutation_correlation = $correlation
        writer_attribution = $writer
        snapshot_result = 'not_evaluated'
        target_mutation_observed = $applicable
        result = $result
        proofs = [pscustomobject][ordered]@{
            owned_session = $true
            provider_identity_validated = $true
            required_event_categories_enabled = $true
            provider_enabled = $ProviderReady
            consumer_ready_before_window = $ConsumerReady
            exact_target_binding = $TargetBinding
            target_filter_configuration_validated = $FilterBinding
            preexisting_target_handles_absent = $PreexistingHandlesAbsent
            name_file_object_correlation_suitable = $NameCorrelation
            observation_window_covered = $NormalStop
            normal_explicit_stop = $NormalStop
            drain_complete = $true
            quality_counters_present = $true
            consumer_exit_accounted = $true
            native_timeline_complete = $true
        }
    }
}

function Assert-Classification {
    param([object]$Trace, [string]$Coverage, [string]$TargetScope,
        [string]$Correlation, [string]$Writer, [string]$Label,
        [bool]$TerminalReceiptValid = $true,
        [bool]$TimelineComplete = $true)
    $actual = Get-StockWriterTraceClassification $Trace `
        -TerminalReceiptValid $TerminalReceiptValid `
        -TimelineComplete $TimelineComplete
    Assert-True ($actual.CaptureCoverage -ceq $Coverage) "${Label}_coverage"
    Assert-True ($actual.TargetScopeObservation -ceq $TargetScope) `
        "${Label}_target_scope"
    Assert-True ($actual.MutationCorrelation -ceq $Correlation) `
        "${Label}_correlation"
    Assert-True ($actual.WriterAttribution -ceq $Writer) "${Label}_writer"
    return $actual
}

# A/B: a validated empty or read-only window is a complete negative
# observation. Provider event count is deliberately zero in A.
$zero = New-TraceFixture
$zeroResult = Assert-Classification $zero complete ready not_applicable `
    not_observed 'zero_target_mutations'
Assert-True ($zeroResult.Result -ceq
    'complete_no_target_mutation_observed') 'zero_window_typed_result'
$readOnly = New-TraceFixture -ProviderEvents 2 -RelevantOperations 2
[void](Assert-Classification $readOnly complete ready not_applicable `
    not_observed 'read_only_window')

# C/D/E/F: readiness, binding and quality are independent from a zero count.
[void](Assert-Classification (New-TraceFixture -ProviderReady $false) `
    incomplete ready not_applicable unresolved 'provider_not_ready')
[void](Assert-Classification (New-TraceFixture -TargetBinding $false) `
    complete incomplete not_applicable unresolved 'wrong_target_binding')
[void](Assert-Classification (New-TraceFixture -FilterBinding $false) `
    complete incomplete not_applicable unresolved 'wrong_filter_binding')
[void](Assert-Classification (New-TraceFixture -EventsLost 1) `
    incomplete ready not_applicable unresolved 'events_lost')
$bufferLoss = New-TraceFixture
$bufferLoss.log_buffers_lost = 1
$bufferLoss.capture_coverage = 'incomplete'
$bufferLoss.writer_attribution = 'unresolved'
$bufferLoss.result = 'incomplete'
[void](Assert-Classification $bufferLoss incomplete ready not_applicable `
    unresolved 'buffer_loss')
[void](Assert-Classification (New-TraceFixture -Overflow $true) `
    incomplete ready not_applicable unresolved 'overflow')
[void](Assert-Classification (New-TraceFixture -QueueDrops 1) `
    incomplete ready not_applicable unresolved 'queue_drop')
[void](Assert-Classification (New-TraceFixture -MapEvictions 1) `
    complete incomplete not_applicable unresolved 'map_eviction')
[void](Assert-Classification (New-TraceFixture -SchemaFailure $true) `
    incomplete ready not_applicable unresolved 'schema_failure')

# G/H/I/K: applicability, process identity and completion status are separate.
[void](Assert-Classification (New-TraceFixture -ProviderEvents 3 `
        -RelevantOperations 1 -Mutations 1 -TargetMutations 1 `
        -CompletionRequired 1 -CompletionsObserved 1 `
        -SuccessfulCompletions 1) complete ready incomplete unresolved `
    'unmapped_mutation')
[void](Assert-Classification (New-TraceFixture -ProviderEvents 3 `
        -RelevantOperations 1 -Mutations 1 -TargetMutations 1 `
        -MappedMutations 1 -CompletionRequired 1 -CompletionsObserved 1 `
        -SuccessfulCompletions 1) complete ready complete attributed `
    'direct_write')
[void](Assert-Classification (New-TraceFixture -ProviderEvents 4 `
        -RelevantOperations 2 -Mutations 2 -TargetMutations 1 `
        -ParentMutations 1 -MappedMutations 2 -CompletionRequired 2 `
        -CompletionsObserved 2 -SuccessfulCompletions 2) complete ready `
    complete attributed 'temp_write_rename')
[void](Assert-Classification (New-TraceFixture -ProviderEvents 3 `
        -RelevantOperations 1 -Mutations 1 -TargetMutations 1 `
        -MappedMutations 1 -CompletionRequired 1 -CompletionsObserved 1 `
        -FailedCompletions 1) complete ready complete attributed `
    'failed_write_status')

# J/L: an open pre-capture handle and missing receipt/timeline fail closed.
[void](Assert-Classification (New-TraceFixture -UnresolvedCandidates 1 `
        -PreexistingHandlesAbsent $false -NameCorrelation $false) complete `
    incomplete not_applicable unresolved `
    'open_handle_name_gap')
[void](Assert-Classification $zero incomplete ready not_applicable unresolved `
    'terminal_receipt_missing' -TerminalReceiptValid $false)
[void](Assert-Classification $zero incomplete ready not_applicable unresolved `
    'timeline_gap' -TimelineComplete $false)
$missingCounter = New-TraceFixture
$missingCounter.PSObject.Properties.Remove('events_lost')
[void](Assert-Classification $missingCounter incomplete incomplete `
    not_evaluated unresolved 'missing_counter')

# N: v1 receipts retain their legacy interpretation but gain no v2 proof.
$legacy = [pscustomobject]@{
    schema = 'hlclient.writer-trace.redacted.v1'
    correlation_complete = $false
    events_lost = 0
    log_buffers_lost = 0
    realtime_buffers_lost = 0
    overflow = $false
    schema_failure = $false
    explicit_stop_success = $true
    session_absent_after_stop = $true
    mutating_operations = 0
    target_mutating_operations = 0
}
$legacyResult = Get-StockWriterTraceClassification $legacy
Assert-True ($legacyResult.CaptureCoverage -ceq 'unknown') `
    'legacy_receipt_silently_promoted'
Assert-True ($legacyResult.MutationCorrelation -ceq 'not_evaluated') `
    'legacy_applicability_invented'

$unknownRejected = $false
try {
    [void](Get-StockWriterTraceClassification ([pscustomobject]@{
                schema = 'hlclient.writer-trace.redacted.v999' }))
} catch { $unknownRejected = $true }
Assert-True $unknownRejected 'unknown_receipt_version_not_rejected'

# Fake-QPC timeline: equal neighbouring ticks are allowed; a gap is not.
$timeline = New-StockWriterTraceTimeline $transaction -ClockFrequency 10000000
$tick = 100
foreach ($event in @('prelaunch_ready','provider_enabled','consumer_ready',
        'trace_ready_validated','launch_released','stock_process_created',
        'stock_processes_stopped','tail_complete','trace_collection_stopped',
        'trace_finalized','restoration_started','post_inventory_started',
        'post_inventory_finished')) {
    Add-StockWriterTraceTimelineEvent $timeline $event -NowTicks $tick `
        -Source wrapper | Out-Null
    if ($event -cne 'provider_enabled') { ++$tick }
}
$timelineResult = Test-StockWriterTraceTimeline $timeline
Assert-True $timelineResult.Complete 'fake_clock_complete_timeline_rejected'
$gapTimeline = New-StockWriterTraceTimeline $transaction -ClockFrequency 10000000
foreach ($event in @('prelaunch_ready','provider_enabled')) {
    Add-StockWriterTraceTimelineEvent $gapTimeline $event -NowTicks 100 | Out-Null
}
Assert-True (-not (Test-StockWriterTraceTimeline $gapTimeline).Complete) `
    'fake_clock_timeline_gap_accepted'

$reportFixtureRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ('hlclient report path ' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($reportFixtureRoot)
try {
    $terminalPath = Join-StockWriterTraceReportPath `
        -Directory $reportFixtureRoot -ChildPath 'final receipt.json'
    $fakeOwner = [pscustomobject]@{
        Request = [pscustomobject]@{
            TransactionId = $transaction
            TerminalReceiptPath = $terminalPath
        }
        Trace = $zero
        FinalReceipt = 'validated'
        ExitCode = 0
        Failure = $null
        Timeline = $timeline
    }
    $reportingReceipt = Write-StockWriterTraceTerminalReceipt $fakeOwner `
        unchanged -ReportingFailure final_report_formatting_failed
    Assert-True (Test-Path -LiteralPath $reportingReceipt.Path -PathType Leaf) `
        'terminal_receipt_lost_on_reporting_failure'
    Assert-True ([string]$reportingReceipt.Receipt.result -ceq
        'complete_no_target_mutation_observed') `
        'reporting_failure_masked_transaction_result'
    Assert-True ([string]$reportingReceipt.Receipt.reporting_failure -ceq
        'final_report_formatting_failed') `
        'reporting_failure_not_separate'
    $positiveSnapshotPath = Join-StockWriterTraceReportPath `
        -Directory $reportFixtureRoot `
        -ChildPath 'positive unchanged receipt.json'
    $positiveOwner = [pscustomobject]@{
        Request = [pscustomobject]@{
            TransactionId = $transaction
            TerminalReceiptPath = $positiveSnapshotPath
        }
        Trace = New-TraceFixture -ProviderEvents 3 -RelevantOperations 1 `
            -Mutations 1 -TargetMutations 1 -MappedMutations 1 `
            -CompletionRequired 1 -CompletionsObserved 1 `
            -SuccessfulCompletions 1
        FinalReceipt = 'validated'
        ExitCode = 0
        Failure = $null
        Timeline = $timeline
    }
    $positiveSnapshotReceipt = Write-StockWriterTraceTerminalReceipt `
        $positiveOwner unchanged
    Assert-True ([string]$positiveSnapshotReceipt.Receipt.writer_attribution `
        -ceq 'attributed') 'unchanged_snapshot_erased_writer_attribution'
    Assert-True ([string]$positiveSnapshotReceipt.Receipt.snapshot_result `
        -ceq 'unchanged') 'writer_attribution_fabricated_snapshot_change'
} finally {
    Remove-Item -LiteralPath $reportFixtureRoot -Recurse -Force
}

# Fake-clock lifecycle: long pre-snapshot is outside the protocol, then the
# exact ordered receipts cover launch, zero owned processes, tail and drain.
$state = New-StockWriterTraceProtocolState $transaction `
    -TraceReadyTimeoutMilliseconds 5000 -TailMilliseconds 15000 `
    -DrainTimeoutMilliseconds 5000 -HardTraceDeadlineMilliseconds 60000
Assert-True ($state.State -ceq 'preparing') 'collector_active_during_pre_snapshot'
$state = Move-StockWriterTraceProtocolState $state prelaunch_ready $transaction 30000
$state = Move-StockWriterTraceProtocolState $state trace_ready $transaction 30100
$state = Move-StockWriterTraceProtocolState $state launch_released $transaction 30101
$state = Move-StockWriterTraceProtocolState $state stock_processes_stopped $transaction 40100
$state = Move-StockWriterTraceProtocolState $state stop_requested $transaction 55100
$state = Move-StockWriterTraceProtocolState $state trace_finalized $transaction 55200 `
    -Complete -CleanupExact
Assert-True ($state.State -ceq 'trace_finalized') 'normal_lifecycle_not_finalized'
Assert-True ($state.TraceCompleteness -ceq 'complete') 'normal_trace_not_complete'
Assert-True ($state.FakeStockLaunches -eq 1) 'launch_release_not_counted_once'
# A long post-scan starts only after this terminal state and cannot extend it.
$postScanAt = 180000
Assert-True ($postScanAt -gt $state.TraceFinalizedAt) 'post_scan_not_after_trace_finalization'

$delayed = New-StockWriterTraceProtocolState $transaction
$delayed = Move-StockWriterTraceProtocolState $delayed prelaunch_ready $transaction 0
$delayed = Move-StockWriterTraceProtocolState $delayed trace_ready $transaction 5001
Assert-True ($delayed.State -ceq 'failed') 'delayed_ready_not_rejected'
Assert-True ($delayed.FakeStockLaunches -eq 0) 'delayed_ready_released_launch'

$foreign = New-StockWriterTraceProtocolState $transaction
$foreign = Move-StockWriterTraceProtocolState $foreign prelaunch_ready `
    'ffffffffffffffffffffffffffffffff' 1
Assert-True ($foreign.Failure -ceq 'writer_trace_transaction_mismatch') `
    'foreign_transaction_receipt_not_rejected'

$shortTail = New-StockWriterTraceProtocolState $transaction
$shortTail = Move-StockWriterTraceProtocolState $shortTail prelaunch_ready $transaction 0
$shortTail = Move-StockWriterTraceProtocolState $shortTail trace_ready $transaction 1
$shortTail = Move-StockWriterTraceProtocolState $shortTail launch_released $transaction 2
$shortTail = Move-StockWriterTraceProtocolState $shortTail stock_processes_stopped $transaction 3
$shortTail = Move-StockWriterTraceProtocolState $shortTail stop_requested $transaction 14999
Assert-True ($shortTail.Failure -ceq 'writer_trace_tail_too_short') `
    'short_tail_not_rejected'

foreach ($failure in @('collector_crash','collector_deadline','closed_pipe')) {
    $failed = New-StockWriterTraceProtocolState $transaction
    $failed = Move-StockWriterTraceProtocolState $failed $failure $transaction 1
    Assert-True ($failed.TraceCompleteness -ceq 'incomplete') `
        "${failure}_not_incomplete"
}

$loss = New-StockWriterTraceProtocolState $transaction
$loss = Move-StockWriterTraceProtocolState $loss prelaunch_ready $transaction 0
$loss = Move-StockWriterTraceProtocolState $loss trace_ready $transaction 1
$loss = Move-StockWriterTraceProtocolState $loss launch_released $transaction 2
$loss = Move-StockWriterTraceProtocolState $loss stock_processes_stopped $transaction 3
$loss = Move-StockWriterTraceProtocolState $loss stop_requested $transaction 15003
$loss = Move-StockWriterTraceProtocolState $loss trace_finalized $transaction 15004 `
    -CleanupExact
Assert-True ($loss.TraceCompleteness -ceq 'incomplete') `
    'injected_loss_allowed_complete_result'
$duplicate = Move-StockWriterTraceProtocolState $state stop_requested $transaction 55201
Assert-True ($duplicate.State -ceq 'trace_finalized') 'duplicate_stop_changed_terminal_state'

$captureSource = Get-Content -LiteralPath (
    Join-Path $PSScriptRoot 'capture_stock_runtime_state.ps1') -Raw
Assert-True ($captureSource -cmatch
    'if \(\$EnableWriterTraceHandoff\)[\s\S]+New-StockWriterTraceRequest') `
    'writer_trace_enable_gate_missing'
Assert-True ($captureSource.Contains(
        "throw 'writer_trace_handoff_parameters_without_enable'")) `
    'default_writer_trace_parameter_rejection_missing'

$actualSmoke = 'not_requested'
$actualEventsLost = 'N/A'
$actualOverflow = 'N/A'
$actualSessionCleanup = 'N/A'
$actualNegative = 'not_requested'
$actualPositiveSelfTest = 'not_requested'
if (-not [string]::IsNullOrWhiteSpace($WriterTraceToolPath)) {
    $principal = [Security.Principal.WindowsPrincipal]::new(
        [Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        $actualSmoke = 'capability_skip_not_elevated'
        $actualNegative = 'capability_skip_not_elevated'
        $actualPositiveSelfTest = 'capability_skip_not_elevated'
    } else {
    $temp = Join-Path ([IO.Path]::GetTempPath()) (
        'hlclient-writer-handoff-' + [Guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($temp)
    try {
        $target = Join-Path $temp 'target.txt'
        $output = Join-Path $temp 'trace.json'
        [IO.File]::WriteAllText($target, 'before')
        $request = New-StockWriterTraceRequest $transaction $WriterTraceToolPath `
            (Get-FileHash -Algorithm SHA256 -LiteralPath $WriterTraceToolPath).Hash `
            $target $output -TraceReadyTimeoutMilliseconds 5000 `
            -TailMilliseconds 0 -DrainTimeoutMilliseconds 5000 `
            -HardTraceDeadlineMilliseconds 60000
        Add-StockWriterTraceTimelineEvent $request.Timeline prelaunch_ready `
            -IpcEvidence synthetic_prelaunch | Out-Null
        $owner = Start-StockWriterTraceCollector $request
        Assert-True ($null -ne $owner.ReadyReceipt) 'actual_trace_ready_missing'
        Add-StockWriterTraceTimelineEvent $owner.Timeline launch_released `
            -IpcEvidence synthetic_release | Out-Null
        Add-StockWriterTraceTimelineEvent $owner.Timeline stock_process_created `
            -Source orchestrator -IpcEvidence synthetic_producer_started | Out-Null
        [IO.File]::AppendAllText($target, '-direct')
        Add-StockWriterTraceTimelineEvent $owner.Timeline stock_processes_stopped `
            -Source orchestrator -IpcEvidence synthetic_producer_stopped | Out-Null
        $owner = Complete-StockWriterTraceCollector $owner 0
        Add-StockWriterTraceTimelineEvent $owner.Timeline restoration_started | Out-Null
        Add-StockWriterTraceTimelineEvent $owner.Timeline post_inventory_started | Out-Null
        Add-StockWriterTraceTimelineEvent $owner.Timeline post_inventory_finished | Out-Null
        $terminal = Write-StockWriterTraceTerminalReceipt $owner changed
        Save-SmokeArtifact $output 'direct-write-trace.json'
        Save-SmokeArtifact $terminal.Path 'direct-write-terminal.json'
        Assert-True $owner.ExitConfirmed 'actual_collector_exit_not_confirmed'
        Assert-True $owner.SessionCleanupExact 'actual_etw_session_cleanup_inexact'
        Assert-True ($owner.EventsLost -eq 0) 'actual_etw_event_loss'
        Assert-True (-not $owner.Overflow) 'actual_etw_overflow'
        Assert-True ($terminal.Receipt.writer_attribution -ceq 'attributed') `
            'actual_direct_write_not_attributed'
        $actualSmoke = 'synthetic_etw_success'
        $actualEventsLost = [string]$owner.EventsLost
        $actualOverflow = $owner.Overflow.ToString().ToLowerInvariant()
        $actualSessionCleanup =
            $owner.SessionCleanupExact.ToString().ToLowerInvariant()

        $negativeTarget = Join-Path $temp 'negative target.txt'
        $negativeOutput = Join-Path $temp 'negative trace.json'
        [IO.File]::WriteAllText($negativeTarget, 'unchanged')
        $negativeTransaction = '1123456789abcdef0123456789abcdef'
        $negativeRequest = New-StockWriterTraceRequest $negativeTransaction `
            $WriterTraceToolPath `
            (Get-FileHash -Algorithm SHA256 -LiteralPath $WriterTraceToolPath).Hash `
            $negativeTarget $negativeOutput -TraceReadyTimeoutMilliseconds 5000 `
            -TailMilliseconds 0 -DrainTimeoutMilliseconds 5000 `
            -HardTraceDeadlineMilliseconds 60000
        Add-StockWriterTraceTimelineEvent $negativeRequest.Timeline `
            prelaunch_ready -IpcEvidence synthetic_prelaunch | Out-Null
        $negativeOwner = Start-StockWriterTraceCollector $negativeRequest
        Assert-True ($null -ne $negativeOwner.ReadyReceipt) `
            'actual_negative_trace_ready_missing'
        Add-StockWriterTraceTimelineEvent $negativeOwner.Timeline launch_released `
            -IpcEvidence synthetic_release | Out-Null
        Add-StockWriterTraceTimelineEvent $negativeOwner.Timeline `
            stock_process_created -Source orchestrator `
            -IpcEvidence synthetic_read_only_phase_started | Out-Null
        [void][IO.File]::ReadAllText($negativeTarget)
        Add-StockWriterTraceTimelineEvent $negativeOwner.Timeline `
            stock_processes_stopped -Source orchestrator `
            -IpcEvidence synthetic_read_only_phase_stopped | Out-Null
        $negativeOwner = Complete-StockWriterTraceCollector $negativeOwner 0
        Add-StockWriterTraceTimelineEvent $negativeOwner.Timeline `
            restoration_started | Out-Null
        Add-StockWriterTraceTimelineEvent $negativeOwner.Timeline `
            post_inventory_started | Out-Null
        Add-StockWriterTraceTimelineEvent $negativeOwner.Timeline `
            post_inventory_finished | Out-Null
        $negativeTerminal = Write-StockWriterTraceTerminalReceipt `
            $negativeOwner unchanged
        Save-SmokeArtifact $negativeOutput 'read-only-trace.json'
        Save-SmokeArtifact $negativeTerminal.Path 'read-only-terminal.json'
        Assert-True ($negativeTerminal.Receipt.result -ceq
            'complete_no_target_mutation_observed') `
            'actual_negative_window_not_complete'
        Assert-True ($negativeTerminal.Receipt.writer_attribution -ceq
            'not_observed') 'actual_negative_writer_identity_invented'
        $actualNegative = 'complete_no_target_mutation_observed'

        $selfTestRoot = Join-Path $temp 'native self test'
        $selfTestOutput = Join-Path $temp 'native-self-test.json'
        $selfTestLines = @(& $WriterTraceToolPath --self-test `
                $selfTestRoot $selfTestOutput)
        $selfTestExit = $LASTEXITCODE
        Assert-True ($selfTestExit -eq 0) 'native_positive_self_test_failed'
        $selfTestResult = Get-Content -LiteralPath $selfTestOutput -Raw |
            ConvertFrom-Json
        Save-SmokeArtifact $selfTestOutput 'positive-self-test-trace.json'
        Assert-True ([Int64]$selfTestResult.mutating_operations -gt 0) `
            'positive_self_test_zero_mutations_accepted'
        Assert-True ([string]$selfTestResult.capture_coverage -ceq 'complete') `
            'positive_self_test_capture_incomplete'
        Assert-True (([Int64]$selfTestResult.events_lost +
                [Int64]$selfTestResult.log_buffers_lost +
                [Int64]$selfTestResult.realtime_buffers_lost) -eq 0) `
            'positive_self_test_event_loss'
        Assert-True ([string]$selfTestResult.mutation_correlation -ceq
            'complete') 'positive_self_test_correlation_incomplete'
        Assert-True (@($selfTestLines | Where-Object {
                    $_ -ceq 'result=synthetic_success' }).Count -eq 1) `
            'positive_self_test_terminal_result_missing'
        $actualPositiveSelfTest = 'direct_write_and_rename_attributed'
    } finally {
        if (Test-Path -LiteralPath $temp) {
            Remove-Item -LiteralPath $temp -Recurse -Force
        }
    }
    }
}

Write-Output 'typed_result=writer_trace_handoff_synthetic_success'
Write-Output 'fake_clock_lifecycle=success'
Write-Output 'missing_ready_fake_stock_launches=0'
Write-Output 'foreign_transaction_receipt=rejected'
Write-Output 'closed_pipe_duplicate_stop=typed'
Write-Output 'injected_loss_complete_result=forbidden'
Write-Output 'default_capture_etw_session=not_created'
Write-Output "actual_synthetic_etw=$actualSmoke"
Write-Output "actual_synthetic_etw_events_lost=$actualEventsLost"
Write-Output "actual_synthetic_etw_overflow=$actualOverflow"
Write-Output "actual_synthetic_etw_session_cleanup=$actualSessionCleanup"
Write-Output "actual_synthetic_etw_negative=$actualNegative"
Write-Output "actual_synthetic_etw_positive_self_test=$actualPositiveSelfTest"
