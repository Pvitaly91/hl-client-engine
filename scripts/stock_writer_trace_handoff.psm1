#requires -Version 5.1

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-StockWriterTraceProperty {
    param([Parameter(Mandatory = $true)][AllowNull()][object]$Object,
        [Parameter(Mandatory = $true)][string]$Name)
    return $null -ne $Object -and
        $null -ne $Object.PSObject.Properties[$Name]
}

function Join-StockWriterTraceReportPath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][ValidateNotNullOrEmpty()]
        [string]$Directory,
        [Parameter(Mandatory = $true)][ValidateNotNullOrEmpty()]
        [string]$ChildPath
    )
    if ([IO.Path]::IsPathRooted($ChildPath)) {
        throw 'writer_trace_report_child_path_rooted'
    }
    return Join-Path -Path $Directory -ChildPath $ChildPath
}

function New-StockWriterTraceTimeline {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [ValidatePattern('^[0-9a-f]{32}$')]
        [string]$TransactionId,
        [ValidateRange(1, [Int64]::MaxValue)]
        [Int64]$ClockFrequency = [Diagnostics.Stopwatch]::Frequency
    )
    return [pscustomobject]@{
        Schema = 'hlclient.writer-trace-timeline-state.v1'
        TransactionId = $TransactionId
        ClockKind = 'query_performance_counter'
        ClockDomain = 'windows_current_boot_qpc'
        ClockFrequency = $ClockFrequency
        Events = [Collections.Generic.List[object]]::new()
        IpcOrdering = [Collections.Generic.List[object]]::new()
    }
}

function Add-StockWriterTraceTimelineEvent {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Timeline,
        [Parameter(Mandatory = $true)]
        [ValidateSet('prelaunch_ready','provider_enabled','consumer_ready',
            'trace_ready_validated','launch_released','stock_process_created',
            'stock_processes_stopped','tail_complete',
            'trace_collection_stopped','trace_finalized',
            'restoration_started','post_inventory_started',
            'post_inventory_finished')]
        [string]$Name,
        [Int64]$NowTicks = [Diagnostics.Stopwatch]::GetTimestamp(),
        [ValidateSet('wrapper','native_helper','orchestrator')]
        [string]$Source = 'wrapper',
        [string]$IpcEvidence = ''
    )
    if ([string]$Timeline.Schema -cne
        'hlclient.writer-trace-timeline-state.v1' -or
        [string]$Timeline.ClockKind -cne 'query_performance_counter' -or
        [string]$Timeline.ClockDomain -cne 'windows_current_boot_qpc' -or
        [Int64]$Timeline.ClockFrequency -le 0 -or $NowTicks -le 0) {
        throw 'writer_trace_timeline_contract_invalid'
    }
    if (@($Timeline.Events | Where-Object { [string]$_.name -ceq $Name }).Count -ne 0) {
        throw "writer_trace_timeline_duplicate_event:$Name"
    }
    [void]$Timeline.Events.Add([pscustomobject]@{
            name = $Name
            monotonic_ticks = [Int64]$NowTicks
            source = $Source
        })
    if (-not [string]::IsNullOrEmpty($IpcEvidence)) {
        [void]$Timeline.IpcOrdering.Add([pscustomobject]@{
                ipc_sequence = $Timeline.IpcOrdering.Count + 1
                name = $Name
                evidence = $IpcEvidence
            })
    }
    return $Timeline
}

function Import-StockWriterTraceNativeTimeline {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Timeline,
        [Parameter(Mandatory = $true)][object]$Trace
    )
    if (-not (Test-StockWriterTraceProperty $Trace 'timeline')) {
        throw 'writer_trace_native_timeline_missing'
    }
    $native = $Trace.timeline
    if ([string]$native.schema -cne
            'hlclient.writer-trace-native-timeline.v1' -or
        [string]$native.transaction_id -cne [string]$Timeline.TransactionId -or
        [string]$native.clock_kind -cne [string]$Timeline.ClockKind -or
        [string]$native.clock_domain -cne [string]$Timeline.ClockDomain -or
        [Int64]$native.clock_frequency -ne [Int64]$Timeline.ClockFrequency) {
        throw 'writer_trace_native_timeline_domain_mismatch'
    }
    $expected = @('provider_enabled','consumer_ready',
        'trace_collection_stopped','trace_finalized')
    $events = @($native.events)
    if ($events.Count -ne $expected.Count) {
        throw 'writer_trace_native_timeline_event_set_invalid'
    }
    for ($index = 0; $index -lt $events.Count; ++$index) {
        $event = $events[$index]
        if ([int]$event.event_sequence -ne ($index + 1) -or
            [string]$event.name -cne $expected[$index]) {
            throw 'writer_trace_native_timeline_sequence_invalid'
        }
        Add-StockWriterTraceTimelineEvent $Timeline $expected[$index] `
            -NowTicks ([Int64]$event.monotonic_ticks) `
            -Source native_helper | Out-Null
    }
    return $Timeline
}

function Test-StockWriterTraceTimeline {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][object]$Timeline)
    $expected = @('prelaunch_ready','provider_enabled','consumer_ready',
        'trace_ready_validated','launch_released','stock_process_created',
        'stock_processes_stopped','tail_complete','trace_collection_stopped',
        'trace_finalized','restoration_started','post_inventory_started',
        'post_inventory_finished')
    $reasons = [Collections.Generic.List[string]]::new()
    $ordered = [Collections.Generic.List[object]]::new()
    [Int64]$previous = 0
    [Int64]$first = 0
    for ($index = 0; $index -lt $expected.Count; ++$index) {
        $matches = @($Timeline.Events | Where-Object {
                [string]$_.name -ceq $expected[$index] })
        if ($matches.Count -ne 1) {
            [void]$reasons.Add("timeline_event_missing_or_duplicate:$($expected[$index])")
            continue
        }
        [Int64]$ticks = $matches[0].monotonic_ticks
        if ($ticks -le 0) {
            [void]$reasons.Add("timeline_tick_invalid:$($expected[$index])")
        } elseif ($previous -gt 0 -and $ticks -lt $previous) {
            [void]$reasons.Add("timeline_order_invalid:$($expected[$index])")
        }
        if ($first -eq 0) { $first = $ticks }
        [void]$ordered.Add([pscustomobject]@{
                event_sequence = $index + 1
                name = $expected[$index]
                monotonic_ticks = $ticks
                relative_ticks = $ticks - $first
                source = [string]$matches[0].source
            })
        $previous = $ticks
    }
    return [pscustomobject]@{
        Complete = $reasons.Count -eq 0
        Reasons = @($reasons)
        Events = @($ordered)
    }
}

function ConvertTo-StockWriterTraceArgument {
    param([Parameter(Mandatory = $true)][string]$Value)
    if ($Value.Length -eq 0) { return '""' }
    if ($Value -cnotmatch '[\s"]') { return $Value }
    return '"' + ($Value -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
}

function Assert-StockWriterTraceOrdinaryFile {
    param([string]$Path, [string]$Label)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0 -or
        ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "${Label}_not_ordinary_file"
    }
    $parent = Get-Item -LiteralPath $item.DirectoryName -Force -ErrorAction Stop
    if (($parent.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "${Label}_parent_reparse_rejected"
    }
    $streams = @(Get-Item -LiteralPath $item.FullName -Stream * -ErrorAction Stop)
    if ($streams.Count -ne 1 -or $streams[0].Stream -cne ':$DATA') {
        throw "${Label}_ads_rejected"
    }
    return $item.FullName
}

function New-StockWriterTraceRequest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [ValidatePattern('^[0-9a-f]{32}$')]
        [string]$TransactionId,
        [Parameter(Mandatory = $true)][string]$ToolPath,
        [Parameter(Mandatory = $true)]
        [ValidatePattern('^[0-9A-Fa-f]{64}$')]
        [string]$ExpectedToolSha256,
        [Parameter(Mandatory = $true)][string]$TargetPath,
        [Parameter(Mandatory = $true)][string]$OutputPath,
        [ValidateRange(1, 5000)][int]$TraceReadyTimeoutMilliseconds = 5000,
        [ValidateRange(0, 15000)][int]$TailMilliseconds = 15000,
        [ValidateRange(1, 5000)][int]$DrainTimeoutMilliseconds = 5000,
        [ValidateRange(1, 60000)][int]$HardTraceDeadlineMilliseconds = 60000
    )
    $tool = Assert-StockWriterTraceOrdinaryFile $ToolPath 'writer_trace_tool'
    $target = Assert-StockWriterTraceOrdinaryFile $TargetPath 'writer_trace_target'
    $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $tool).Hash
    if ($actualHash -cne $ExpectedToolSha256.ToUpperInvariant()) {
        throw 'writer_trace_tool_fingerprint_mismatch'
    }
    $output = [IO.Path]::GetFullPath($OutputPath)
    $outputParent = Split-Path -Parent $output
    if (-not (Test-Path -LiteralPath $outputParent -PathType Container)) {
        throw 'writer_trace_output_parent_missing'
    }
    if (Test-Path -LiteralPath $output) {
        throw 'writer_trace_output_already_exists'
    }
    if (Test-Path -LiteralPath ($output + '.terminal.json')) {
        throw 'writer_trace_terminal_receipt_already_exists'
    }
    return [pscustomobject]@{
        Schema = 'hlclient.writer-trace-handoff-request.v2'
        TransactionId = [string]$TransactionId
        ToolPath = [string]$tool
        ToolSha256 = [string]$actualHash
        TargetPath = [string]$target
        OutputPath = [string]$output
        TraceReadyTimeoutMilliseconds = [int]$TraceReadyTimeoutMilliseconds
        TailMilliseconds = [int]$TailMilliseconds
        DrainTimeoutMilliseconds = [int]$DrainTimeoutMilliseconds
        HardTraceDeadlineMilliseconds = [int]$HardTraceDeadlineMilliseconds
        TerminalReceiptPath = [string]($output + '.terminal.json')
        Timeline = New-StockWriterTraceTimeline $TransactionId
    }
}

function Get-StockWriterTraceClassification {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Trace,
        [bool]$TerminalReceiptValid = $true,
        [int]$CollectorExitCode = 0,
        [string]$CollectorFailure = '',
        [bool]$TimelineComplete = $true
    )
    $schema = if (Test-StockWriterTraceProperty $Trace 'schema') {
        [string]$Trace.schema
    } else { '' }
    if ($schema -ceq 'hlclient.writer-trace.redacted.v1') {
        $legacyRequired = @('correlation_complete','events_lost',
            'log_buffers_lost','realtime_buffers_lost','overflow',
            'schema_failure','explicit_stop_success',
            'session_absent_after_stop','mutating_operations',
            'target_mutating_operations')
        $missing = @($legacyRequired | Where-Object {
                -not (Test-StockWriterTraceProperty $Trace $_) })
        if ($missing.Count -ne 0) {
            throw 'writer_trace_legacy_receipt_invalid'
        }
        $legacyComplete = [bool]$Trace.correlation_complete -and
            [Int64]$Trace.events_lost -eq 0 -and
            ([Int64]$Trace.log_buffers_lost +
                [Int64]$Trace.realtime_buffers_lost) -eq 0 -and
            -not [bool]$Trace.overflow -and
            -not [bool]$Trace.schema_failure -and
            [bool]$Trace.explicit_stop_success -and
            [bool]$Trace.session_absent_after_stop
        return [pscustomobject]@{
            ContractVersion = 1
            CaptureCoverage = 'unknown'
            TargetScopeObservation = 'unknown'
            MutationCorrelation = if ([Int64]$Trace.mutating_operations -gt 0) {
                $(if ([bool]$Trace.correlation_complete) { 'complete' } else { 'incomplete' })
            } else { 'not_evaluated' }
            WriterAttribution = 'unresolved'
            TargetMutationObserved = [Int64]$Trace.target_mutating_operations -gt 0
            Result = 'historical_contract_new_proofs_unavailable'
            LegacyComplete = $legacyComplete
            Reasons = @('v2_capture_and_target_scope_proofs_unavailable',
                'persisted_timeline_unavailable')
        }
    }
    if ($schema -cne 'hlclient.writer-trace.redacted.v2') {
        throw "writer_trace_receipt_schema_unsupported:$schema"
    }

    $counterNames = @('total_provider_events','relevant_operations',
        'mutating_operations','target_mutating_operations',
        'parent_mutating_operations','process_mapped_mutations',
        'completion_required_mutations','mutation_completions_observed',
        'successful_completions','failed_completions','events_lost',
        'log_buffers_lost','realtime_buffers_lost','schema_count',
        'queue_drops','correlation_map_evictions',
        'unresolved_mutation_candidates',
        'unresolved_target_mutation_candidates')
    $proofNames = @('owned_session','provider_identity_validated',
        'required_event_categories_enabled','provider_enabled',
        'consumer_ready_before_window','exact_target_binding',
        'target_filter_configuration_validated',
        'preexisting_target_handles_absent',
        'name_file_object_correlation_suitable',
        'observation_window_covered','normal_explicit_stop','drain_complete',
        'quality_counters_present','consumer_exit_accounted',
        'native_timeline_complete')
    $coverageProofNames = @('owned_session','provider_identity_validated',
        'required_event_categories_enabled','provider_enabled',
        'consumer_ready_before_window','observation_window_covered',
        'normal_explicit_stop','drain_complete','quality_counters_present',
        'consumer_exit_accounted','native_timeline_complete')
    $reasons = [Collections.Generic.List[string]]::new()
    $counter = @{}
    foreach ($name in $counterNames) {
        if (-not (Test-StockWriterTraceProperty $Trace $name) -or
            $null -eq $Trace.$name) {
            [void]$reasons.Add("quality_counter_missing:$name")
            $counter[$name] = $null
            continue
        }
        try { [Int64]$value = $Trace.$name }
        catch {
            [void]$reasons.Add("quality_counter_invalid:$name")
            $counter[$name] = $null
            continue
        }
        if ($value -lt 0) {
            [void]$reasons.Add("quality_counter_invalid:$name")
            $counter[$name] = $null
        } else { $counter[$name] = $value }
    }
    $proof = @{}
    $proofObject = if (Test-StockWriterTraceProperty $Trace 'proofs') {
        $Trace.proofs
    } else { $null }
    foreach ($name in $proofNames) {
        if (-not (Test-StockWriterTraceProperty $proofObject $name) -or
            $null -eq $proofObject.$name) {
            [void]$reasons.Add("proof_missing:$name")
            $proof[$name] = $false
        } else { $proof[$name] = [bool]$proofObject.$name }
    }
    foreach ($name in @('overflow','schema_failure',
            'explicit_stop_success','session_absent_after_stop')) {
        if (-not (Test-StockWriterTraceProperty $Trace $name) -or
            $null -eq $Trace.$name) {
            [void]$reasons.Add("quality_field_missing:$name")
        }
    }

    $allCountersPresent = @($counterNames | Where-Object {
            $null -eq $counter[$_] }).Count -eq 0
    $lossFree = $allCountersPresent -and
        $counter.events_lost -eq 0 -and
        $counter.log_buffers_lost -eq 0 -and
        $counter.realtime_buffers_lost -eq 0
    $captureComplete = $allCountersPresent -and
        @($coverageProofNames | Where-Object { -not $proof[$_] }).Count -eq 0 -and
        $lossFree -and $counter.queue_drops -eq 0 -and
        (Test-StockWriterTraceProperty $Trace 'overflow') -and
        -not [bool]$Trace.overflow -and
        (Test-StockWriterTraceProperty $Trace 'schema_failure') -and
        -not [bool]$Trace.schema_failure -and
        (Test-StockWriterTraceProperty $Trace 'explicit_stop_success') -and
        [bool]$Trace.explicit_stop_success -and
        (Test-StockWriterTraceProperty $Trace 'session_absent_after_stop') -and
        [bool]$Trace.session_absent_after_stop -and
        $TerminalReceiptValid -and $CollectorExitCode -eq 0 -and
        [string]::IsNullOrEmpty($CollectorFailure) -and $TimelineComplete
    if (-not $TerminalReceiptValid) { [void]$reasons.Add('terminal_receipt_invalid') }
    if ($CollectorExitCode -ne 0) { [void]$reasons.Add('collector_exit_nonzero') }
    if (-not [string]::IsNullOrEmpty($CollectorFailure)) {
        [void]$reasons.Add('collector_failure_present')
    }
    if (-not $TimelineComplete) { [void]$reasons.Add('persisted_timeline_incomplete') }
    if ($allCountersPresent) {
        if (-not $lossFree) { [void]$reasons.Add('event_or_buffer_loss') }
        if ($counter.queue_drops -ne 0) { [void]$reasons.Add('queue_drops') }
    }
    if ((Test-StockWriterTraceProperty $Trace 'overflow') -and
        [bool]$Trace.overflow) { [void]$reasons.Add('collector_overflow') }
    if ((Test-StockWriterTraceProperty $Trace 'schema_failure') -and
        [bool]$Trace.schema_failure) { [void]$reasons.Add('schema_decode_failure') }

    $targetReady = $allCountersPresent -and
        $proof.exact_target_binding -and
        $proof.target_filter_configuration_validated -and
        $proof.preexisting_target_handles_absent -and
        $proof.name_file_object_correlation_suitable -and
        $counter.unresolved_target_mutation_candidates -eq 0 -and
        $counter.correlation_map_evictions -eq 0
    if ($allCountersPresent -and
        $counter.unresolved_target_mutation_candidates -ne 0) {
        [void]$reasons.Add('unresolved_target_mutation_candidates')
    }
    if ($allCountersPresent -and $counter.correlation_map_evictions -ne 0) {
        [void]$reasons.Add('correlation_map_evictions')
    }

    $applicable = $allCountersPresent -and $counter.mutating_operations -gt 0
    $completionStatusesAccounted = $allCountersPresent -and
        ($counter.successful_completions + $counter.failed_completions) -eq
            $counter.mutation_completions_observed
    $correlationComplete = $applicable -and
        $counter.process_mapped_mutations -eq $counter.mutating_operations -and
        $counter.mutation_completions_observed -eq
            $counter.completion_required_mutations -and
        $completionStatusesAccounted
    $mutationCorrelation = if (-not $allCountersPresent) {
        'not_evaluated'
    } elseif (-not $applicable) {
        'not_applicable'
    } elseif ($correlationComplete) {
        'complete'
    } else { 'incomplete' }
    if ($applicable -and -not $correlationComplete) {
        [void]$reasons.Add('target_mutation_correlation_incomplete')
    }
    $writerAttribution = if (-not $applicable) {
        $(if ($captureComplete -and $targetReady) { 'not_observed' } else { 'unresolved' })
    } elseif ($correlationComplete) {
        'attributed'
    } else { 'unresolved' }
    $result = if ($captureComplete -and $targetReady -and -not $applicable) {
        'complete_no_target_mutation_observed'
    } elseif ($captureComplete -and $targetReady -and $correlationComplete) {
        'complete_target_mutation_attributed'
    } else { 'incomplete' }

    $declaredPairs = [ordered]@{
        capture_coverage = $(if ($captureComplete) { 'complete' } else { 'incomplete' })
        target_scope_observation = $(if ($targetReady) { 'ready' } else { 'incomplete' })
        mutation_correlation = $mutationCorrelation
        writer_attribution = $writerAttribution
        snapshot_result = 'not_evaluated'
        result = $result
    }
    foreach ($pair in $declaredPairs.GetEnumerator()) {
        if (-not (Test-StockWriterTraceProperty $Trace $pair.Key) -or
            [string]$Trace.($pair.Key) -cne [string]$pair.Value) {
            [void]$reasons.Add("declared_state_mismatch:$($pair.Key)")
            $captureComplete = $false
            $result = 'incomplete'
        }
    }
    if (-not (Test-StockWriterTraceProperty $Trace 'correlation_applicable') -or
        [bool]$Trace.correlation_applicable -ne $applicable) {
        [void]$reasons.Add('declared_state_mismatch:correlation_applicable')
        $captureComplete = $false
        $result = 'incomplete'
    }
    $targetObserved = $applicable
    if (-not (Test-StockWriterTraceProperty $Trace 'target_mutation_observed') -or
        [bool]$Trace.target_mutation_observed -ne $targetObserved) {
        [void]$reasons.Add('declared_state_mismatch:target_mutation_observed')
        $captureComplete = $false
        $result = 'incomplete'
    }
    return [pscustomobject]@{
        ContractVersion = 2
        CaptureCoverage = $(if ($captureComplete) { 'complete' } else { 'incomplete' })
        TargetScopeObservation = $(if ($targetReady) { 'ready' } else { 'incomplete' })
        MutationCorrelation = $mutationCorrelation
        WriterAttribution = $writerAttribution
        TargetMutationObserved = $targetObserved
        Result = $result
        LegacyComplete = $null
        Reasons = @($reasons | Select-Object -Unique)
    }
}

function New-StockWriterTraceProtocolState {
    param(
        [Parameter(Mandatory = $true)]
        [ValidatePattern('^[0-9a-f]{32}$')]
        [string]$TransactionId,
        [ValidateRange(1, 5000)][int]$TraceReadyTimeoutMilliseconds = 5000,
        [ValidateRange(0, 15000)][int]$TailMilliseconds = 15000,
        [ValidateRange(1, 5000)][int]$DrainTimeoutMilliseconds = 5000,
        [ValidateRange(1, 60000)][int]$HardTraceDeadlineMilliseconds = 60000
    )
    return [pscustomobject]@{
        Schema = 'hlclient.writer-trace-handoff-state.v1'
        TransactionId = $TransactionId
        State = 'preparing'
        Failure = $null
        TraceReadyTimeoutMilliseconds = $TraceReadyTimeoutMilliseconds
        TailMilliseconds = $TailMilliseconds
        DrainTimeoutMilliseconds = $DrainTimeoutMilliseconds
        HardTraceDeadlineMilliseconds = $HardTraceDeadlineMilliseconds
        PrelaunchReadyAt = $null
        TraceReadyAt = $null
        LaunchReleasedAt = $null
        StockProcessesStoppedAt = $null
        StopRequestedAt = $null
        TraceFinalizedAt = $null
        TraceCompleteness = 'not_started'
        FakeStockLaunches = 0
    }
}

function Move-StockWriterTraceProtocolState {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$State,
        [Parameter(Mandatory = $true)]
        [ValidateSet('prelaunch_ready','trace_ready','launch_released',
            'stock_processes_stopped','stop_requested','trace_finalized',
            'collector_crash','collector_deadline','closed_pipe')]
        [string]$Notification,
        [Parameter(Mandatory = $true)][string]$TransactionId,
        [Parameter(Mandatory = $true)][Int64]$NowMilliseconds,
        [switch]$Complete,
        [switch]$CleanupExact
    )
    if ($State.State -in @('trace_finalized','failed')) {
        if ($Notification -eq 'stop_requested' -and
            $State.State -eq 'trace_finalized') { return $State }
        throw 'writer_trace_notification_after_terminal_state'
    }
    if ($TransactionId -cne $State.TransactionId) {
        $State.State = 'failed'
        $State.Failure = 'writer_trace_transaction_mismatch'
        $State.TraceCompleteness = 'incomplete'
        return $State
    }
    $expected = switch ($Notification) {
        'prelaunch_ready' { 'preparing' }
        'trace_ready' { 'prelaunch_ready' }
        'launch_released' { 'trace_ready' }
        'stock_processes_stopped' { 'launch_released' }
        'stop_requested' { 'stock_processes_stopped' }
        'trace_finalized' { 'stop_requested' }
        default { $State.State }
    }
    if ($Notification -in @('collector_crash','collector_deadline','closed_pipe')) {
        $State.State = 'failed'
        $State.Failure = "writer_trace_$Notification"
        $State.TraceCompleteness = 'incomplete'
        return $State
    }
    if ($State.State -cne $expected) {
        $State.State = 'failed'
        $State.Failure = 'writer_trace_notification_out_of_order'
        $State.TraceCompleteness = 'incomplete'
        return $State
    }
    switch ($Notification) {
        'prelaunch_ready' {
            $State.State = 'prelaunch_ready'; $State.PrelaunchReadyAt = $NowMilliseconds
        }
        'trace_ready' {
            if ($NowMilliseconds - $State.PrelaunchReadyAt -gt
                $State.TraceReadyTimeoutMilliseconds) {
                $State.State = 'failed'; $State.Failure = 'writer_trace_ready_timeout'
                $State.TraceCompleteness = 'incomplete'; return $State
            }
            $State.State = 'trace_ready'; $State.TraceReadyAt = $NowMilliseconds
        }
        'launch_released' {
            $State.State = 'launch_released'; $State.LaunchReleasedAt = $NowMilliseconds
            $State.FakeStockLaunches = 1
        }
        'stock_processes_stopped' {
            $State.State = 'stock_processes_stopped'
            $State.StockProcessesStoppedAt = $NowMilliseconds
        }
        'stop_requested' {
            if ($NowMilliseconds - $State.StockProcessesStoppedAt -lt
                $State.TailMilliseconds) {
                $State.State = 'failed'; $State.Failure = 'writer_trace_tail_too_short'
                $State.TraceCompleteness = 'incomplete'; return $State
            }
            $State.State = 'stop_requested'; $State.StopRequestedAt = $NowMilliseconds
        }
        'trace_finalized' {
            if ($NowMilliseconds - $State.StopRequestedAt -gt
                $State.DrainTimeoutMilliseconds) {
                $State.State = 'failed'; $State.Failure = 'writer_trace_drain_timeout'
                $State.TraceCompleteness = 'incomplete'; return $State
            }
            $State.State = 'trace_finalized'; $State.TraceFinalizedAt = $NowMilliseconds
            $State.TraceCompleteness = if ($Complete -and $CleanupExact) {
                'complete'
            } else { 'incomplete' }
        }
    }
    if ($null -ne $State.TraceReadyAt -and
        $NowMilliseconds - $State.TraceReadyAt -gt
            $State.HardTraceDeadlineMilliseconds -and
        $State.State -ne 'trace_finalized') {
        $State.State = 'failed'
        $State.Failure = 'writer_trace_hard_deadline'
        $State.TraceCompleteness = 'incomplete'
    }
    return $State
}

function Start-StockWriterTraceCollector {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][object]$Request)
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Request.ToolPath
    if ([string]$Request.Schema -cne
        'hlclient.writer-trace-handoff-request.v2') {
        throw 'writer_trace_request_schema_unsupported'
    }
    $arguments = @('--trace-stdin-controlled-v3',
        [string]$Request.HardTraceDeadlineMilliseconds,
        [string]$Request.OutputPath,
        [string]$Request.TransactionId)
    $start.Arguments = (@($arguments | ForEach-Object {
                ConvertTo-StockWriterTraceArgument ([string]$_)
            }) -join ' ')
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $owner = [pscustomobject]@{
        Schema = 'hlclient.writer-trace-collector-owner.v1'
        Request = $Request
        Process = $process
        Started = $false
        CollectorStartedUtc = $null
        ProcessId = $null
        ReadyReceipt = $null
        TraceReadyUtc = $null
        StockProcessesStoppedUtc = $null
        StopRequestedUtc = $null
        TraceFinalizedUtc = $null
        FinalReceipt = $null
        StopSent = $false
        ExitConfirmed = $false
        ExitCode = $null
        Failure = $null
        TraceCompleteness = 'not_started'
        TargetScopeObservation = 'unknown'
        MutationCorrelation = 'not_evaluated'
        WriterAttribution = 'unresolved'
        TargetMutationObserved = $false
        TypedResult = 'not_started'
        ClassificationReasons = @()
        Trace = $null
        Timeline = $Request.Timeline
        EventsLost = $null
        Overflow = $null
        SessionCleanupExact = $false
    }
    try {
        if (-not $process.Start()) { throw 'writer_trace_collector_start_failed' }
        $owner.Started = $true
        $owner.CollectorStartedUtc = [DateTime]::UtcNow.ToString('o')
        $owner.ProcessId = $process.Id
        $process.StandardInput.WriteLine($Request.TargetPath)
        $readyTask = $process.StandardOutput.ReadLineAsync()
        if (-not $readyTask.Wait($Request.TraceReadyTimeoutMilliseconds)) {
            throw 'writer_trace_ready_timeout'
        }
        $receipt = $readyTask.GetAwaiter().GetResult()
        $expected = 'trace_ready=true;transaction=' + $Request.TransactionId +
            ';provider=ready;consumer=ready;receipt=' +
            'hlclient.writer-trace-ready.v2'
        if ($receipt -cne $expected) {
            throw 'writer_trace_ready_receipt_invalid'
        }
        $owner.ReadyReceipt = $receipt
        $owner.TraceReadyUtc = [DateTime]::UtcNow.ToString('o')
        Add-StockWriterTraceTimelineEvent $owner.Timeline `
            trace_ready_validated -IpcEvidence validated_ready_receipt | Out-Null
        $owner.TraceCompleteness = 'pending'
        return $owner
    } catch {
        $owner.Failure = $_.Exception.Message
        $owner.TraceCompleteness = 'incomplete'
        try { if (-not $process.HasExited) { $process.Kill() } } catch { }
        try { [void]$process.WaitForExit(5000) } catch { }
        $owner.ExitConfirmed = $process.HasExited
        if ($owner.ExitConfirmed) { $owner.ExitCode = $process.ExitCode }
        return $owner
    }
}

function Complete-StockWriterTraceCollector {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Owner,
        [ValidateRange(0, 15000)][int]$TailMilliseconds,
        [string]$PrimaryFailure = ''
    )
    if ($Owner.FinalReceipt) { return $Owner }
    if ($TailMilliseconds -gt 0) { Start-Sleep -Milliseconds $TailMilliseconds }
    if (@($Owner.Timeline.Events | Where-Object {
                [string]$_.name -ceq 'tail_complete' }).Count -eq 0) {
        Add-StockWriterTraceTimelineEvent $Owner.Timeline tail_complete | Out-Null
    }
    $process = $Owner.Process
    try {
        if (-not $Owner.Started) { throw 'writer_trace_collector_not_started' }
        if ($process.HasExited) {
            if ([string]::IsNullOrEmpty($PrimaryFailure)) {
                $Owner.Failure = 'writer_trace_collector_early_exit'
            } else { $Owner.Failure = $PrimaryFailure }
        } elseif (-not $Owner.StopSent) {
            try {
                $Owner.StopRequestedUtc = [DateTime]::UtcNow.ToString('o')
                $process.StandardInput.WriteLine('stop')
                $process.StandardInput.Close()
                $Owner.StopSent = $true
            } catch {
                if ([string]::IsNullOrEmpty($PrimaryFailure)) {
                    $Owner.Failure = 'writer_trace_control_pipe_closed'
                } else { $Owner.Failure = $PrimaryFailure }
            }
        }
        if (-not $process.WaitForExit($Owner.Request.DrainTimeoutMilliseconds)) {
            if (-not $process.HasExited) { $process.Kill() }
            [void]$process.WaitForExit(5000)
            if ([string]::IsNullOrEmpty($Owner.Failure)) {
                $Owner.Failure = 'writer_trace_drain_timeout'
            }
        }
        $Owner.ExitConfirmed = $process.HasExited
        if ($Owner.ExitConfirmed) { $Owner.ExitCode = $process.ExitCode }
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        if ([Text.Encoding]::UTF8.GetByteCount($stdout) -gt 4096 -or
            [Text.Encoding]::UTF8.GetByteCount($stderr) -gt 4096) {
            if ([string]::IsNullOrEmpty($Owner.Failure)) {
                $Owner.Failure = 'writer_trace_terminal_output_overflow'
            }
        }
        $terminal = 'trace_finalized=true;transaction=' +
            $Owner.Request.TransactionId + ';session_cleanup=exact;receipt=' +
            'hlclient.writer-trace-terminal.v2'
        if (@($stdout -split '\r?\n') -ccontains $terminal) {
            $Owner.FinalReceipt = $terminal
            $Owner.TraceFinalizedUtc = [DateTime]::UtcNow.ToString('o')
            [void]$Owner.Timeline.IpcOrdering.Add([pscustomobject]@{
                    ipc_sequence = $Owner.Timeline.IpcOrdering.Count + 1
                    name = 'trace_finalized'
                    evidence = 'validated_terminal_receipt'
                })
        } elseif ([string]::IsNullOrEmpty($Owner.Failure)) {
            $Owner.Failure = 'writer_trace_terminal_receipt_invalid'
        }
        if (Test-Path -LiteralPath $Owner.Request.OutputPath -PathType Leaf) {
            $trace = Get-Content -LiteralPath $Owner.Request.OutputPath -Raw |
                ConvertFrom-Json
            if ([string]$trace.schema -cne
                    'hlclient.writer-trace.redacted.v2' -or
                [string]$trace.transaction_id -cne
                    [string]$Owner.Request.TransactionId) {
                throw 'writer_trace_result_contract_invalid'
            }
            $Owner.Trace = $trace
            Import-StockWriterTraceNativeTimeline $Owner.Timeline $trace |
                Out-Null
            $classification = Get-StockWriterTraceClassification $trace `
                -TerminalReceiptValid ($null -ne $Owner.FinalReceipt) `
                -CollectorExitCode $(if ($null -ne $Owner.ExitCode) {
                        [int]$Owner.ExitCode
                    } else { -1 }) `
                -CollectorFailure ([string]$Owner.Failure)
            $Owner.EventsLost = [Int64]$trace.events_lost +
                [Int64]$trace.log_buffers_lost +
                [Int64]$trace.realtime_buffers_lost
            $Owner.Overflow = [bool]$trace.overflow
            $Owner.SessionCleanupExact =
                [bool]$trace.explicit_stop_success -and
                [bool]$trace.session_absent_after_stop
            $Owner.TraceCompleteness = $classification.CaptureCoverage
            $Owner.TargetScopeObservation =
                $classification.TargetScopeObservation
            $Owner.MutationCorrelation = $classification.MutationCorrelation
            $Owner.WriterAttribution = $classification.WriterAttribution
            $Owner.TargetMutationObserved =
                $classification.TargetMutationObserved
            $Owner.TypedResult = $classification.Result
            $Owner.ClassificationReasons = @($classification.Reasons)
        } else {
            $Owner.TraceCompleteness = 'incomplete'
            if ([string]::IsNullOrEmpty($Owner.Failure)) {
                $Owner.Failure = 'writer_trace_result_missing'
            }
        }
    } catch {
        if ([string]::IsNullOrEmpty($Owner.Failure)) {
            $Owner.Failure = $_.Exception.Message
        }
        $Owner.TraceCompleteness = 'incomplete'
        try { if (-not $process.HasExited) { $process.Kill() } } catch { }
        try { [void]$process.WaitForExit(5000) } catch { }
        $Owner.ExitConfirmed = $process.HasExited
        if ($Owner.ExitConfirmed) { $Owner.ExitCode = $process.ExitCode }
    }
    return $Owner
}

function Write-StockWriterTraceTerminalReceipt {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Owner,
        [Parameter(Mandatory = $true)]
        [ValidateSet('unchanged','changed','incomplete','not_evaluated')]
        [string]$SnapshotResult,
        [string]$ReportingFailure = ''
    )
    if ($null -eq $Owner.Trace) {
        throw 'writer_trace_terminal_receipt_trace_missing'
    }
    $timeline = Test-StockWriterTraceTimeline $Owner.Timeline
    $classification = Get-StockWriterTraceClassification $Owner.Trace `
        -TerminalReceiptValid ($null -ne $Owner.FinalReceipt) `
        -CollectorExitCode $(if ($null -ne $Owner.ExitCode) {
                [int]$Owner.ExitCode
            } else { -1 }) `
        -CollectorFailure ([string]$Owner.Failure) `
        -TimelineComplete ([bool]$timeline.Complete)
    $reasons = @($classification.Reasons + @($timeline.Reasons) |
        Select-Object -Unique)
    $receipt = [ordered]@{
        schema = 'hlclient.writer-trace-terminal-receipt.v2'
        contract_version = 2
        transaction_id = [string]$Owner.Request.TransactionId
        process_exit_code = $Owner.ExitCode
        collector_failure = $(if ([string]::IsNullOrEmpty($Owner.Failure)) {
                $null
            } else { [string]$Owner.Failure })
        reporting_failure = $(if ([string]::IsNullOrEmpty($ReportingFailure)) {
                $null
            } else { $ReportingFailure })
        capture_coverage = [string]$classification.CaptureCoverage
        target_scope_observation =
            [string]$classification.TargetScopeObservation
        mutation_correlation = [string]$classification.MutationCorrelation
        writer_attribution = [string]$classification.WriterAttribution
        snapshot_result = $SnapshotResult
        target_mutation_observed =
            [bool]$classification.TargetMutationObserved
        result = [string]$classification.Result
        reasons = $reasons
        quality = [ordered]@{
            provider_events = [Int64]$Owner.Trace.total_provider_events
            relevant_operations = [Int64]$Owner.Trace.relevant_operations
            mutating_operations = [Int64]$Owner.Trace.mutating_operations
            target_mutating_operations =
                [Int64]$Owner.Trace.target_mutating_operations
            process_mapped_mutations =
                [Int64]$Owner.Trace.process_mapped_mutations
            completion_required_mutations =
                [Int64]$Owner.Trace.completion_required_mutations
            mutation_completions_observed =
                [Int64]$Owner.Trace.mutation_completions_observed
            successful_completions =
                [Int64]$Owner.Trace.successful_completions
            failed_completions = [Int64]$Owner.Trace.failed_completions
            events_lost = [Int64]$Owner.Trace.events_lost
            log_buffers_lost = [Int64]$Owner.Trace.log_buffers_lost
            realtime_buffers_lost =
                [Int64]$Owner.Trace.realtime_buffers_lost
            queue_drops = [Int64]$Owner.Trace.queue_drops
            overflow = [bool]$Owner.Trace.overflow
            schema_failure = [bool]$Owner.Trace.schema_failure
            correlation_map_evictions =
                [Int64]$Owner.Trace.correlation_map_evictions
            unresolved_mutation_candidates =
                [Int64]$Owner.Trace.unresolved_mutation_candidates
            unresolved_target_mutation_candidates =
                [Int64]$Owner.Trace.unresolved_target_mutation_candidates
        }
        timeline = [ordered]@{
            schema = 'hlclient.writer-trace-persisted-timeline.v1'
            transaction_id = [string]$Owner.Timeline.TransactionId
            clock_kind = [string]$Owner.Timeline.ClockKind
            clock_domain = [string]$Owner.Timeline.ClockDomain
            clock_frequency = [Int64]$Owner.Timeline.ClockFrequency
            complete = [bool]$timeline.Complete
            events = @($timeline.Events)
            ipc_ordering = @($Owner.Timeline.IpcOrdering)
        }
    }
    $json = $receipt | ConvertTo-Json -Depth 12
    $encoding = [Text.UTF8Encoding]::new($false)
    $stream = [IO.File]::Open($Owner.Request.TerminalReceiptPath,
        [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try {
        $bytes = $encoding.GetBytes($json + [Environment]::NewLine)
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush($true)
    } finally { $stream.Dispose() }
    return [pscustomobject]@{
        Path = [string]$Owner.Request.TerminalReceiptPath
        Receipt = $receipt
    }
}

Export-ModuleMember -Function New-StockWriterTraceRequest,
    New-StockWriterTraceProtocolState, Move-StockWriterTraceProtocolState,
    Start-StockWriterTraceCollector, Complete-StockWriterTraceCollector,
    Get-StockWriterTraceClassification, New-StockWriterTraceTimeline,
    Add-StockWriterTraceTimelineEvent, Import-StockWriterTraceNativeTimeline,
    Test-StockWriterTraceTimeline, Write-StockWriterTraceTerminalReceipt,
    Join-StockWriterTraceReportPath
