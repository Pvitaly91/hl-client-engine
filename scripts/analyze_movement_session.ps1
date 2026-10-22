#requires -Version 7.0
<#
.SYNOPSIS
Read the bounded normal-client movement report without launching or changing anything.
.DESCRIPTION
With no path, inspect only the newest external-movement.json immediately beneath
LOCALAPPDATA/HLClientEngine/runs. Whole-session histogram quantiles are approximate
upper bounds. Timing and prediction observations do not establish a root cause.
.EXAMPLE
pwsh -NoProfile -File scripts/analyze_movement_session.ps1
.EXAMPLE
pwsh -NoProfile -File scripts/analyze_movement_session.ps1 -RunDirectory C:/Reports/0123456789abcdef0123456789abcdef -Json
#>
[CmdletBinding(DefaultParameterSetName = 'Latest')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Report')][string]$ReportPath,
    [Parameter(Mandatory, ParameterSetName = 'Run')][string]$RunDirectory,
    [switch]$Json,
    [switch]$PublishSummary,
    [switch]$Open
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$maximumReportBytes = 16MB
$cohortNames = @('controlled_alive', 'background', 'inactive_or_dead', 'unknown')
$countNames = @('frames', 'prediction_active_frames', 'prediction_inactive_frames',
    'prediction_unknown_frames', 'fallback_entries', 'correction_samples',
    'correction_ordinal_advances_unmeasured', 'frame_over20_ms', 'frame_over33_3_ms',
    'frame_over50_ms', 'startsolid_frames', 'allsolid_frames', 'committed_records',
    'replayed_commands', 'bytes', 'fallback_observed_interval_us')
$metricNames = @('frame_interval_ms', 'cpu_update_ms', 'network_ms', 'parse_commit_ms',
    'prediction_ms', 'collision_ms', 'studio_ms', 'asset_ms', 'render_ms', 'present_ms',
    'report_ms', 'gpu_ms', 'state_age_ms', 'raw_correction_on_ordinal_sample_units',
    'camera_jump_snapshot_on_ordinal_units', 'replay_duration_ms',
    'fallback_duration_snapshot_ms', 'raw_error_on_fallback_entry_units')

function Get-Field($Object, [string]$Name) {
    if ($Object -is [Collections.IDictionary] -and $Object.Contains($Name)) {
        return ,($Object[$Name])
    }
    return $null
}
function Get-Object($Object, [string]$Name, [switch]$Required) {
    $value = Get-Field $Object $Name
    if ($null -ne $value -and $value -isnot [Collections.IDictionary]) {
        throw 'invalid_report_object'
    }
    if ($Required -and $null -eq $value) { throw 'missing_report_object' }
    return $value
}
function Get-Number($Object, [string]$Name, [switch]$Required, [switch]$Integer) {
    $value = Get-Field $Object $Name
    if ($null -eq $value) {
        if ($Required) { throw 'missing_report_number' }
        return $null
    }
    if ($value -is [string] -or $value -is [bool] -or
        $value -is [Collections.IEnumerable] -or -not [double]::IsFinite([double]$value) -or
        [double]$value -lt 0) { throw 'invalid_report_number' }
    if ($Integer -and ([decimal]$value -ne [decimal]::Truncate([decimal]$value) -or
        [decimal]$value -gt [decimal][UInt64]::MaxValue)) { throw 'invalid_report_counter' }
    return $value
}
function Get-PredictionWork($Object) {
    if ($null -eq $Object) { return $null }
    if ($Object -isnot [Collections.IDictionary]) { throw 'invalid_prediction_work_object' }
    $durationNames = @('command_ms', 'replay_ms', 'collision_query_ms')
    $counterNames = @('trace_queries', 'position_queries', 'contents_queries', 'traversal_steps')
    $knownNames = @($durationNames) + @($counterNames)
    if ($Object.Count -ne $knownNames.Count) { throw 'invalid_prediction_work_shape' }
    foreach ($key in $Object.Keys) {
        if (($key -isnot [string]) -or ([string]$key) -cnotin $knownNames) {
            throw 'invalid_prediction_work_shape'
        }
    }
    $result = [ordered]@{}
    foreach ($name in $durationNames) {
        $value = Get-Field $Object $name
        if ($null -eq $value -or $value -is [string] -or $value -is [bool] -or
            $value -is [Collections.IEnumerable]) { throw 'invalid_prediction_work_duration' }
        try { $number = [double]$value } catch { throw 'invalid_prediction_work_duration' }
        if (-not [double]::IsFinite($number) -or $number -lt 0 -or $number -gt 1e9) {
            throw 'invalid_prediction_work_duration'
        }
        $result[$name] = $number
    }
    foreach ($name in $counterNames) {
        $value = Get-Field $Object $name
        if ($null -eq $value -or $value -is [string] -or $value -is [bool] -or
            $value -is [Collections.IEnumerable]) { throw 'invalid_prediction_work_counter' }
        try { $number = [decimal]$value } catch { throw 'invalid_prediction_work_counter' }
        if ($number -lt 0 -or $number -ne [decimal]::Truncate($number) -or
            $number -gt [decimal][UInt64]::MaxValue) { throw 'invalid_prediction_work_counter' }
        $result[$name] = [UInt64]$number
    }
    return $result
}
function Get-StaticWorldReceipt($Report) {
    $policy = Get-Object $Report 'static_world_receipt'
    if ($null -eq $policy) {
        return [ordered]@{ availability = 'not_recorded_in_older_build'; coverage = 'unavailable' }
    }
    if ((Get-Field $policy 'schema') -cne 'hlclient.static-world-frame.v1' -or
        (Get-Field $policy 'scope') -cne 'completed_renderer_frame_in_recent_window_or_incident' -or
        (Get-Field $policy 'counter_semantics') -cne 'per_frame_not_cumulative' -or
        (Get-Field $policy 'considered_blocks_semantics') -cne 'eligible_immutable_pool' -or
        (Get-Field $policy 'tested_bounds_semantics') -cne 'frustum_calls_including_retests' -or
        (Get-Field $policy 'selected_blocks_semantics') -cne 'submitted_pool_blocks' -or
        (Get-Field $policy 'selection_preparation_scope') -cne 'frustum_probe_range_decisions_excludes_gl_submission_and_map_load' -or
        (Get-Field $policy 'submitted_triangles_semantics') -cne 'geometry_not_visible_pixels' -or
        (Get-Field $policy 'unknown') -cne 'null' -or
        (Get-Field $policy 'omission') -cne 'bounded_recent_window_and_incident_sampling') {
        throw 'unsupported_static_world_receipt_schema'
    }
    $recent = Get-Object $Report 'recent_window' -Required
    $frames = Get-RemoteArray $recent 'frames' 961
    $paths = @('none','original_batches','prepared_bypass','prepared_selected','visible_commands')
    $reasons = @('none','prepared_unavailable','insufficient_probe_rejections',
        'invalid_prepared_batch','fragmented_batch')
    $counterNames = @('considered_blocks','tested_bounds','selected_blocks',
        'considered_surfaces','selected_surfaces','submitted_draws','submitted_triangles','prepared_rebuilds')
    $summary = [ordered]@{
        availability = 'ready'; schema = 'hlclient.static-world-frame.v1'
        coverage = 'recent_window_only'; source = 'retained_encoded_frames_not_whole_session'
        frames_scanned = $frames.Count; frames_with_receipt = 0; frames_unavailable = 0
        paths = [ordered]@{}; fallback_frames = 0; counters = [ordered]@{}
        selection_preparation_ms_observed = 0; selection_preparation_ms_max = $null
    }
    foreach ($name in $counterNames) {
        $summary.counters[$name] = [ordered]@{ observed = 0; total = 0 }
    }
    foreach ($frame in $frames) {
        if ($frame -isnot [Collections.IDictionary]) { throw 'invalid_static_world_frame' }
        $receipt = Get-Object $frame 'static_world'
        if ($null -eq $receipt) { $summary.frames_unavailable++; continue }
        $path = Get-Field $receipt 'path';$reason = Get-Field $receipt 'fallback_reason'
        if ($path -isnot [string] -or $path -cnotin $paths -or
            $reason -isnot [string] -or $reason -cnotin $reasons) { throw 'invalid_static_world_path' }
        $summary.frames_with_receipt++
        if (-not $summary.paths.Contains($path)) { $summary.paths[$path] = 0 }
        $summary.paths[$path]++
        if ($reason -cne 'none') { $summary.fallback_frames++ }
        foreach ($name in @('resource_id','resource_revision') + $counterNames) {
            $number = Get-Number $receipt $name -Integer
            if ($null -ne $number -and $name -in $counterNames) {
                $summary.counters[$name].observed++
                $summary.counters[$name].total += [decimal]$number
            }
        }
        foreach ($name in @('selection_preparation_ms')) {
            $duration = Get-Number $receipt $name
            if ($null -ne $duration) {
                if ($duration -gt 1e9) { throw 'invalid_static_world_duration' }
                $observedName = $name + '_observed';$maximumName = $name + '_max'
                $summary[$observedName]++
                if ($null -eq $summary[$maximumName] -or $duration -gt $summary[$maximumName]) {
                    $summary[$maximumName] = $duration
                }
            }
        }
    }
    return $summary
}
function Get-Boolean($Object, [string]$Name) {
    $value = Get-Field $Object $Name
    if ($value -isnot [bool]) { throw 'invalid_report_boolean' }
    return $value
}
function Get-Counts($Object) {
    if ($null -eq $Object) { return $null }
    $result = [ordered]@{}
    foreach ($name in $countNames) { $result[$name] = Get-Number $Object $name -Integer }
    return $result
}
function Get-Metrics($Object, [switch]$Timeline) {
    $result = [ordered]@{}
    foreach ($name in $metricNames) {
        $metric = Get-Object $Object $name
        if ($null -eq $metric) { continue }
        $item = [ordered]@{
            observed = Get-Number $metric 'observed' -Required -Integer
            mean = Get-Number $metric 'mean'
            maximum = Get-Number $metric 'maximum'
        }
        if (-not $Timeline) {
            $item.not_available = Get-Number $metric 'not_available' -Required -Integer
            foreach ($quantile in @('p50_upper_bound', 'p95_upper_bound', 'p99_upper_bound')) {
                $item[$quantile] = Get-Number $metric $quantile
            }
        }
        if ($item.observed -eq 0 -and ($null -ne $item.mean -or $null -ne $item.maximum)) {
            throw 'zero_observations_with_measured_value'
        }
        $result[$name] = $item
    }
    return $result
}
function Get-Summary($Object) {
    return [ordered]@{
        counts = Get-Counts (Get-Object $Object 'counts' -Required)
        metrics = Get-Metrics (Get-Object $Object 'metrics' -Required)
        producer_correction_maximum = Get-ProducerMaximum (Get-Object $Object 'producer_correction_maximum')
    }
}
function Get-CorrectionIdentity($Object) {
    if ($null -eq $Object) { return $null }
    $item = [ordered]@{}
    foreach ($name in @('event_ordinal','network_generation','map_generation','life_epoch','life_deaths','source_record_identity')) {
        $item[$name] = Get-Number $Object $name -Required -Integer
    }
    if ($item.event_ordinal -eq 0) { throw 'invalid_correction_event_identity' }
    return $item
}
function Get-ProducerMaximum($Object) {
    if ($null -eq $Object) { return [ordered]@{ status = 'not_recorded_in_older_build' } }
    if ((Get-Field $Object 'scope') -cne 'exact_producer_events_not_legacy_frame_ordinal_samples') { throw 'invalid_producer_maximum_scope' }
    $item = [ordered]@{
        events = Get-Number $Object 'events' -Required -Integer
        raw_error = Get-Number $Object 'raw_error'
        witness_identity = Get-CorrectionIdentity (Get-Object $Object 'witness_identity')
        status = Get-Field $Object 'status'
    }
    if ($item.status -cnotin @('available','witness_missing','not_measured') -or
        ($item.events -gt 0) -ne ($null -ne $item.raw_error) -or
        ($item.events -gt 0) -ne ($null -ne $item.witness_identity) -or
        ($item.events -eq 0) -ne ($item.status -eq 'not_measured')) { throw 'invalid_producer_maximum' }
    return $item
}
function Get-CaptureToken($Object, [string]$Name, [int]$Maximum = 96) {
    $value = Get-Field $Object $Name
    if ($value -isnot [string] -or $value.Length -gt $Maximum -or $value -cnotmatch '^[a-z0-9_-]*$') { throw 'invalid_capture_token' }
    return $value
}
function Get-WeaponCommandToken($Object) {
    $value = Get-Field $Object 'command'
    # Registry command case is significant (the standard MP5 is weapon_9mmAR).
    # Do not relax the lowercase-only contract for capture reason enums.
    if ($value -isnot [string] -or $value.Length -gt 48 -or
        $value -cnotmatch '^[A-Za-z0-9_-]+$') { throw 'invalid_weapon_capability_command' }
    return $value
}
function Get-CriticalCorrections($Object, [bool]$Enabled, [bool]$Legacy) {
    if ($null -eq $Object) {
        if ($Enabled -and -not $Legacy) { throw 'missing_critical_corrections' }
        return [ordered]@{ availability = $(if ($Legacy) { 'not_recorded_in_older_build' } else { 'disabled' }) }
    }
    if ((Get-Field $Object 'schema') -cne 'hlclient.critical-corrections.v1' -or
        -not (Get-Boolean $Object 'enabled') -or -not $Enabled -or
        (Get-Number $Object 'maximum_top_witnesses' -Required -Integer) -ne 8 -or
        (Get-Number $Object 'maximum_recent_witnesses' -Required -Integer) -ne 16) { throw 'invalid_critical_corrections_profile' }
    $facts = [ordered]@{ availability = 'ready' }
    foreach ($name in @('accepted_events','events_with_raw','retained_witnesses','duplicates','invalid','evictions')) {
        $facts[$name] = Get-Number $Object $name -Required -Integer
    }
    $facts.delivery_status = 'unknown_not_recorded'
    $facts.delivery_counter_policy = Get-Field $Object 'delivery_counter_policy'
    if ($null -eq $facts.delivery_counter_policy) { $facts.delivery_counter_policy = 'not_recorded_in_older_build' }
    elseif ($facts.delivery_counter_policy -cne 'generation_qualified_cumulative_deltas_reset_baselines_mark_partial') {
        throw 'invalid_correction_delivery_policy'
    }
    $deliveryFields = @('delivery_observations','delivery_sink_failures','delivery_published_events','delivery_counter_resets',
        'delivery_generation_boundaries','delivery_duplicate_observations','delivery_stale_generation_observations')
    $deliveryStatus = Get-Field $Object 'delivery_status'
    if ($null -ne $deliveryStatus) {
        foreach ($name in $deliveryFields) { $facts[$name] = Get-Number $Object $name -Required -Integer }
        $expectedDelivery = if ($facts.delivery_observations -eq 0) { 'not_observed' }
            elseif ($facts.delivery_sink_failures -gt 0 -or $facts.delivery_counter_resets -gt 0 -or
                $facts.delivery_stale_generation_observations -gt 0) { 'partial' } else { 'complete' }
        if ($deliveryStatus -cne $expectedDelivery) { throw 'invalid_correction_delivery_status' }
        $facts.delivery_status = $deliveryStatus
    } else {
        foreach ($name in $deliveryFields) {
            if ($null -ne (Get-Field $Object $name)) { throw 'correction_delivery_counters_without_status' }
            $facts[$name] = $null
        }
    }
    $facts.maximum_raw_position_error = Get-Number $Object 'maximum_raw_position_error'
    $facts.maximum_witness_identity = Get-CorrectionIdentity (Get-Object $Object 'maximum_witness_identity')
    $facts.maximum_witness_status = Get-Field $Object 'maximum_witness_status'
    $identities = [Collections.Generic.HashSet[string]]::new()
    $facts.witnesses = @(foreach ($witness in (Get-RemoteArray $Object 'witnesses' 24)) {
        $identity = Get-CorrectionIdentity (Get-Object $witness 'identity' -Required)
        if (-not $identities.Add(($identity.Values -join '/'))) { throw 'duplicate_correction_witness' }
        $item = [ordered]@{ identity = $identity }
        foreach ($name in @('accepted_ordinal','source_record_ordinal','source_sequence','scene_record_identity','scene_revision',
            'before_prediction_epoch','after_prediction_epoch','carrier_history_revision','collision_world_revision','collision_scene_signature',
            'observed_ns','source_commit_ns','observed_us','carrier_acknowledgement','carrier_outgoing_sequence','boundary_command',
            'history_anchor_command','history_latest_command','history_size','suffix_first_command','suffix_last_command','suffix_commands',
            'suffix_milliseconds','replayed_commands','command_msec','command_buttons')) {
            $item[$name] = Get-Number $witness $name -Integer
        }
        foreach ($name in @('owner_time_ns','command_sample_ns','presentation_requested_ns','presentation_before_ns','presentation_after_ns')) {
            $value = Get-RemoteSignedNumber $witness $name
            if ($null -ne $value -and ([decimal]$value -ne [decimal]::Truncate([decimal]$value) -or
                [decimal]$value -gt [decimal][Int64]::MaxValue -or [decimal]$value -lt [decimal][Int64]::MinValue)) { throw 'invalid_correction_time' }
            $item[$name] = $value
        }
        foreach ($name in @('authoritative_origin','authoritative_velocity','matched_origin','matched_velocity','rebase_origin','rebase_velocity',
            'position_delta','velocity_delta','rebase_position_delta','before_eye','after_eye','query_start','query_end','plane_normal')) {
            $item[$name] = Get-RemoteVector $witness $name
        }
        foreach ($name in @('raw_position_error','raw_velocity_error','same_time_camera_delta','source_server_time_s','scene_server_time_s',
            'trace_fraction','residual_units','support_entity','support_model','blocking_entity','blocking_model','collision_plane_index','blocking_instance')) {
            $item[$name] = Get-Number $witness $name
        }
        $item.support_velocity_z = Get-RemoteSignedNumber $witness 'support_velocity_z'
        foreach ($name in @('prediction_owner_state','classification','camera_status','predecessor_decision','source_freshness',
            'authoritative_origin_freshness','authoritative_velocity_freshness','matched_freshness','scene_freshness','collision_freshness',
            'presentation_freshness','support_freshness','raw_error_basis','rebase_status','anchor_status','collision_stage','blocking_kind',
            'presentation_before_kind','presentation_after_kind')) {
            $item[$name] = Get-Number $witness $name -Required -Integer
            if ($item[$name] -gt 255) { throw 'invalid_correction_classification' }
        }
        foreach ($name in @('hull','mode')) {
            $item[$name] = Get-Number $witness $name -Integer
        }
        if ($item.classification -gt 4 -or $item.camera_status -gt 3 -or $item.prediction_owner_state -gt 4 -or
            $item.predecessor_decision -gt 5 -or $item.raw_error_basis -gt 2) { throw 'invalid_correction_enum' }
        foreach ($name in @('source_freshness','authoritative_origin_freshness','authoritative_velocity_freshness','matched_freshness',
            'scene_freshness','collision_freshness','presentation_freshness','support_freshness')) {
            if ($item[$name] -gt 5) { throw 'invalid_correction_freshness' }
        }
        foreach ($name in @('accepted','control_active','source_matches_scene','source_reliable','reassembled','contemporaneous_fragment_suffix')) {
            $item[$name] = Get-Boolean $witness $name
        }
        foreach ($name in @('alive','startsolid','allsolid','grounded')) {
            $value = Get-Field $witness $name
            if ($null -ne $value -and $value -isnot [bool]) { throw 'invalid_optional_correction_boolean' }
            $item[$name] = $value
        }
        $item.reason = Get-CaptureToken $witness 'reason'
        $item.presentation_reason = Get-CaptureToken $witness 'presentation_reason' 40
        $item.raw_error_comparison = Get-Field $witness 'raw_error_comparison'
        if ($item.raw_error_comparison -cnotin @('matched_to_derived_rebase_seed','matched_to_authoritative_origin','not_measured')) { throw 'invalid_correction_error_basis' }
        $item.camera_measurement = Get-Field $witness 'camera_measurement'
        if ($item.camera_measurement -cnotin @('not_measured','same_time_samples','different_time_samples','incompatible_context') -or
            ($null -ne $item.same_time_camera_delta -and ($item.camera_measurement -ne 'same_time_samples' -or
            $null -eq $item.presentation_requested_ns -or $item.presentation_before_ns -ne $item.presentation_requested_ns -or
            $item.presentation_after_ns -ne $item.presentation_requested_ns -or $item.presentation_before_kind -ne 1 -or $item.presentation_after_kind -ne 1))) {
            throw 'invalid_same_time_correction_measurement'
        }
        [pscustomobject]$item
    })
    if ($facts.retained_witnesses -ne $facts.witnesses.Count -or $facts.events_with_raw -gt $facts.accepted_events -or
        $facts.retained_witnesses -gt $facts.accepted_events -or
        $facts.maximum_witness_status -cnotin @('available','not_measured') -or
        ($facts.events_with_raw -gt 0) -ne ($facts.maximum_witness_status -eq 'available') -or
        ($facts.events_with_raw -gt 0) -ne ($null -ne $facts.maximum_raw_position_error) -or
        ($facts.events_with_raw -gt 0) -ne ($null -ne $facts.maximum_witness_identity)) { throw 'critical_correction_counts_or_maximum_invalid' }
    if ($facts.maximum_witness_identity -and -not $identities.Contains(($facts.maximum_witness_identity.Values -join '/'))) { throw 'critical_maximum_witness_missing' }
    if ($facts.maximum_witness_identity) {
        $maximumKey=$facts.maximum_witness_identity.Values -join '/'
        $maximumWitness=@($facts.witnesses | Where-Object { ($_.identity.Values -join '/') -ceq $maximumKey })[0]
        if ($maximumWitness.raw_position_error -ne $facts.maximum_raw_position_error -or
            @($facts.witnesses | Where-Object { $null -ne $_.raw_position_error -and $_.raw_position_error -gt $facts.maximum_raw_position_error }).Count) {
            throw 'critical_maximum_value_identity_mismatch'
        }
    }
    return $facts
}
function Get-ExactCaptureFrame($Object) {
    if ($Object -isnot [Collections.IDictionary]) { throw 'invalid_exact_capture_frame' }
    $frame = [ordered]@{}
    foreach ($name in @('monotonic_us','frame_number','network_generation','map_generation')) {
        $frame[$name] = Get-Number $Object $name -Required -Integer
    }
    foreach ($name in @('source_sequence','correction_ordinal','authoritative_source_record_identity','authoritative_source_sequence',
        'anchor_command','input_sample_us','render_submit_us','limiter_wait_us','yield_wait_us')) {
        $frame[$name] = Get-Number $Object $name -Integer
    }
    foreach ($name in @('frame_interval','cpu_update','prediction','collision','studio','render_submission','present_wait','gpu',
        'raw_correction','camera_jump','state_age')) { $frame[$name] = Get-Number $Object $name }
    foreach ($name in @('raw_server_origin','predicted_origin','presented_eye','velocity','authoritative_velocity',
        'matched_origin_before_correction','matched_velocity_before_correction')) { $frame[$name] = Get-RemoteVector $Object $name }
    foreach ($name in @('prediction_active','startsolid','allsolid','grounded','previous_iteration_waits_known')) {
        $value = Get-Field $Object $name
        if ($null -ne $value -and $value -isnot [bool]) { throw 'invalid_exact_capture_boolean' }
        $frame[$name] = $value
    }
    foreach ($name in @('fallback_reason','presentation_reason')) {
        if ($null -ne (Get-Field $Object $name)) { $frame[$name] = Get-CaptureToken $Object $name }
    }
    $frame.prediction_work = Get-PredictionWork (Get-Field $Object 'prediction_work')
    $command = Get-Object $Object 'command'
    if ($null -ne $command) {
        $frame.command = [ordered]@{}
        foreach ($name in @('sequence','sample_us','msec','buttons')) { $frame.command[$name] = Get-Number $command $name -Required -Integer }
        if ($frame.command.sequence -gt [UInt32]::MaxValue -or $frame.command.msec -gt 255 -or
            $frame.command.buttons -gt [UInt16]::MaxValue -or $frame.command.sample_us -gt $frame.monotonic_us) { throw 'invalid_exact_capture_command' }
        foreach ($name in @('angle_turns','movement')) {
            $parts = Get-RemoteArray $command $name 3
            if ($parts.Count -ne 3) { throw 'invalid_exact_capture_command_vector' }
            $frame.command[$name] = @(foreach ($part in $parts) {
                $value = Get-RemoteSignedNumber @{ component=$part } 'component'
                if ($null -eq $value -or [decimal]$value -ne [decimal]::Truncate([decimal]$value) -or
                    ($name -eq 'angle_turns' -and ($value -lt 0 -or $value -gt 65535)) -or
                    ($name -eq 'movement' -and ($value -lt -32768 -or $value -gt 32767))) { throw 'invalid_exact_capture_command_component' }
                $value
            })
        }
    }
    return $frame
}
function Get-PriorityCapture($Report, [bool]$Enabled, [bool]$Legacy) {
    if ($Legacy -or -not $Enabled) { return [ordered]@{ availability = $(if ($Legacy) { 'legacy_one_second_policy_or_not_recorded' } else { 'disabled' }) } }
    $policy = Get-Object $Report 'capture_policy' -Required
    if ((Get-Field $policy 'schema') -cne 'hlclient.priority-movement-capture.v1' -or
        (Get-Number $policy 'maximum_decisions' -Required -Integer) -ne 128 -or
        (Get-Field $policy 'coalescing') -cne 'same_network_map_life_fixed_post_deadline' -or
        (Get-Field $policy 'replacement') -cne 'completed_automatic_lowest_priority_then_oldest_ordinal_never_manual') { throw 'invalid_capture_policy' }
    $limits = Get-Object $Report 'limits' -Required
    foreach ($pair in @(@('recent_frames',961),@('incident_frames',480),@('manual_pre_incident_us',4000000),
        @('manual_sampling_interval_us',12500),@('automatic_sampling_interval_us',8333),@('maximum_capacity_bytes',8MB),@('maximum_encoded_bytes',16MB))) {
        if ((Get-Number $limits $pair[0] -Required -Integer) -ne $pair[1]) { throw 'invalid_capture_limits' }
    }
    $facts = [ordered]@{ availability = 'ready'; requested_manual_pre_us = 4000000; manual_sampling_interval_us = 12500
        decision_evictions = Get-Number $policy 'decision_evictions' -Required -Integer }
    $facts.decisions = @(foreach ($decision in (Get-RemoteArray $policy 'decisions' 128)) {
        $item = [ordered]@{}
        foreach ($name in @('ordinal','monotonic_us','frame_number','network_generation','map_generation','life_epoch','source_record_identity',
            'source_sequence','correction_ordinal','incident_ordinal','replacement_ordinal','trigger','priority','context','owner_state')) {
            $item[$name] = Get-Number $decision $name -Integer
        }
        $item.decision = Get-Field $decision 'decision'; $item.reason = Get-CaptureToken $decision 'reason'
        if ($item.decision -cnotin @('accepted','coalesced','expected_lifecycle','cooldown','manual_reservation','capacity','lower_priority',
            'active_windows','allocation_failed','replaced','invalid_timestamp') -or $item.trigger -notin @(1,2,4,8) -or
            $item.context -gt 3 -or $item.owner_state -gt 4 -or $item.priority -gt 100) { throw 'invalid_capture_decision' }
        [pscustomobject]$item
    })
    $ordinals = [Collections.Generic.HashSet[UInt64]]::new()
    $facts.incidents = @(foreach ($incident in (Get-RemoteArray $Report 'incidents' 8)) {
        $item = [ordered]@{}
        foreach ($name in @('ordinal','trigger_us','planned_end_us','triggers','network_generation','map_generation','priority','life_epoch',
            'requested_pre_us','requested_post_us','sampling_interval_us','retained_samples','actual_pre_us','actual_post_us',
            'background_samples_omitted','exact_samples_omitted','omitted_manual_markers','encoded_samples_omitted','encoded_actual_pre_us','encoded_actual_post_us')) {
            $item[$name] = Get-Number $incident $name -Integer -Required:($name -notin @('life_epoch','encoded_actual_pre_us','encoded_actual_post_us'))
        }
        $item.complete = Get-Boolean $incident 'complete'
        $item.initial_trigger_us = Get-Number $incident 'initial_trigger_us' -Integer
        $item.initial_trigger_status = if ($null -eq $item.initial_trigger_us) { 'not_recorded_in_older_build' } else { 'recorded' }
        $item.end_status = Get-Field $incident 'end_status'
        $item.coverage_reason = Get-Field $incident 'coverage_reason'
        $item.manual_marker_us = @(foreach ($stamp in (Get-RemoteArray $incident 'manual_marker_us' 8)) {
            Get-Number @{ stamp=$stamp } 'stamp' -Required -Integer
        })
        if ($item.ordinal -eq 0 -or -not $ordinals.Add([UInt64]$item.ordinal) -or $item.triggers -lt 1 -or $item.triggers -gt 15 -or
            $item.retained_samples -gt 480 -or $item.encoded_samples_omitted -gt $item.retained_samples -or
            $item.actual_pre_us -gt $item.requested_pre_us -or $item.actual_post_us -gt $item.requested_post_us -or
            ($null -ne $item.initial_trigger_us -and $item.initial_trigger_us -gt $item.trigger_us) -or
            $item.end_status -cnotin @('generation_boundary','life_boundary','post_window_complete','session_close_before_post_window') -or
            $item.complete -ne ($item.end_status -eq 'post_window_complete') -or
            $item.coverage_reason -cnotin @('capacity_detail_omitted','generation_boundary','life_boundary',
                'start_or_generation_boundary_or_recent_capacity','session_close_before_post_window','time_bucket_sampling','full_available_window')) {
            throw 'invalid_capture_coverage'
        }
        $frames = Get-RemoteArray $incident 'frames' 480
        if ($frames.Count + $item.encoded_samples_omitted -ne $item.retained_samples) { throw 'invalid_encoded_capture_count' }
        if ($frames.Count) {
            $firstStamp=Get-Number $frames[0] 'monotonic_us' -Required -Integer
            $lastStamp=Get-Number $frames[-1] 'monotonic_us' -Required -Integer
            $expectedPre=if($firstStamp -le $item.trigger_us){$item.trigger_us-$firstStamp}else{$null}
            $expectedPost=if($lastStamp -ge $item.trigger_us){$lastStamp-$item.trigger_us}else{$null}
            if ($firstStamp -gt $lastStamp -or $expectedPre -ne $item.encoded_actual_pre_us -or
                $expectedPost -ne $item.encoded_actual_post_us) { throw 'invalid_encoded_capture_coverage' }
        } elseif ($null -ne $item.encoded_actual_pre_us -or $null -ne $item.encoded_actual_post_us) { throw 'encoded_coverage_without_frames' }
        [pscustomobject]$item
    })
    $facts.exact_frames_availability = if ($null -eq (Get-Field $policy 'exact_frames')) { 'not_recorded_in_older_build' } else { 'recorded' }
    $facts.exact_frames = @()
    $exactIdentities = [Collections.Generic.HashSet[string]]::new()
    if ($facts.exact_frames_availability -eq 'recorded') {
        $facts.exact_frames = @(foreach ($entry in (Get-RemoteArray $policy 'exact_frames' 72)) {
            $ordinal = Get-Number $entry 'incident_ordinal' -Required -Integer
            $owners = @($facts.incidents | Where-Object { $_.ordinal -eq $ordinal })
            if ($owners.Count -ne 1) { throw 'exact_capture_incident_missing' }
            $owner = $owners[0]
            $frame = Get-ExactCaptureFrame (Get-Object $entry 'frame' -Required)
            if (-not $exactIdentities.Add(('{0}/{1}' -f $ordinal,$frame.frame_number)) -or
                $frame.network_generation -ne $owner.network_generation -or $frame.map_generation -ne $owner.map_generation -or
                $frame.monotonic_us -lt ($owner.trigger_us-$owner.actual_pre_us) -or
                $frame.monotonic_us -gt ($owner.trigger_us+$owner.actual_post_us)) { throw 'invalid_exact_capture_identity_or_range' }
            $roles = @()
            if ($null -ne $owner.initial_trigger_us -and $frame.monotonic_us -eq $owner.initial_trigger_us) { $roles += 'initial_trigger' }
            if ($owner.manual_marker_us -contains $frame.monotonic_us) { $roles += 'manual_marker' }
            if (-not $roles.Count) {
                if ($null -ne $owner.initial_trigger_us) { throw 'exact_capture_not_trigger_or_marker' }
                $roles += 'original_trigger_unknown_in_older_build'
            }
            $original = @((Get-Field $Report 'incidents') | Where-Object { (Get-Field $_ 'ordinal') -eq $ordinal })[0]
            $encoded = Get-RemoteArray $original 'frames' 480
            $matching = @($encoded | Where-Object { (Get-Field $_ 'frame_number') -eq $frame.frame_number -and
                (Get-Field $_ 'monotonic_us') -eq $frame.monotonic_us })
            if ($owner.encoded_samples_omitted -eq 0 -and -not $matching.Count) { throw 'exact_capture_not_in_retained_frames' }
            [pscustomobject][ordered]@{ incident_ordinal=$ordinal; roles=$roles; frame=$frame }
        })
        foreach ($owner in $facts.incidents) {
            $original = @((Get-Field $Report 'incidents') | Where-Object { (Get-Field $_ 'ordinal') -eq $owner.ordinal })[0]
            foreach ($encoded in (Get-RemoteArray $original 'frames' 480)) {
                $stamp = Get-Number $encoded 'monotonic_us' -Required -Integer
                if (($null -ne $owner.initial_trigger_us -and $stamp -eq $owner.initial_trigger_us) -or $owner.manual_marker_us -contains $stamp) {
                    $frameNumber = Get-Number $encoded 'frame_number' -Required -Integer
                    if (-not $exactIdentities.Contains(('{0}/{1}' -f $owner.ordinal,$frameNumber))) { throw 'retained_exact_capture_prefix_missing' }
                }
            }
        }
    }
    foreach ($owner in $facts.incidents) {
        $owner | Add-Member -NotePropertyName marker_witnesses -NotePropertyValue @(foreach ($stamp in $owner.manual_marker_us) {
            $matching = @($facts.exact_frames | Where-Object { $_.incident_ordinal -eq $owner.ordinal -and $_.frame.monotonic_us -eq $stamp })
            [pscustomobject]@{ marker_us=$stamp; status=$(if ($matching.Count) { 'available' } elseif ($facts.exact_frames_availability -ne 'recorded') {
                'not_recorded_in_older_build' } else { 'not_retained_or_not_same_time_sample' }) }
        })
    }
    return $facts
}
function Get-FramePacing($Object) {
    if ($null -eq $Object) { return [ordered]@{ availability = 'not_recorded_in_older_build'; episodes = @() } }
    if ((Get-Field $Object 'schema') -cne 'hlclient.frame-pacing.v1' -or
        (Get-Field $Object 'scope') -cne 'controlled_alive_contiguous_endpoint_context' -or
        (Get-Field $Object 'definition') -cne 'absolute_long_frames_not_drop_from_target_fps' -or
        (Get-Field $Object 'selection') -cne 'worst_peak_frame_then_duration_then_first_ordinal') {
        throw 'invalid_frame_pacing_profile'
    }
    $facts = [ordered]@{ availability = 'ready'; scope = 'controlled_alive_contiguous_endpoint_context' }
    foreach ($name in @('maximum_episodes', 'total_episodes', 'retained_episodes', 'omitted_episodes', 'slow_frames')) {
        $facts[$name] = Get-Number $Object $name -Required -Integer
    }
    $facts.threshold_ms = Get-Number $Object 'threshold_ms' -Required
    $facts.slow_frame_interval_total_ms = Get-Number $Object 'slow_frame_interval_total_ms' -Required
    $entries = Get-Field $Object 'episodes'
    if ($facts.maximum_episodes -ne 64 -or $facts.retained_episodes -gt 64 -or
        $facts.threshold_ms -le 0 -or $facts.threshold_ms -gt 1000 -or
        $facts.total_episodes -ne ($facts.retained_episodes + $facts.omitted_episodes) -or
        $entries -isnot [Collections.IList] -or $entries.Count -ne $facts.retained_episodes) {
        throw 'frame_pacing_bounds_or_counts_invalid'
    }
    $ordinals = [Collections.Generic.HashSet[UInt64]]::new()
    $facts.episodes = @(foreach ($entry in $entries) {
        if ($entry -isnot [Collections.IDictionary]) { throw 'invalid_frame_pacing_episode' }
        $item = [ordered]@{}
        foreach ($name in @('ordinal', 'begin_us', 'end_us', 'first_frame', 'last_frame', 'worst_frame',
            'network_generation', 'map_generation', 'slow_frames')) {
            $item[$name] = Get-Number $entry $name -Required -Integer
        }
        $item.maximum_frame_ms = Get-Number $entry 'maximum_frame_ms' -Required
        $item.frame_interval_total_ms = Get-Number $entry 'frame_interval_total_ms' -Required
        if ($item.ordinal -eq 0 -or -not $ordinals.Add([UInt64]$item.ordinal) -or
            $item.ordinal -gt $facts.total_episodes -or $item.end_us -lt $item.begin_us -or
            $item.slow_frames -eq 0 -or $item.slow_frames -gt $facts.slow_frames -or
            $item.last_frame -lt $item.first_frame -or $item.slow_frames -ne ($item.last_frame-$item.first_frame+1) -or
            $item.worst_frame -lt $item.first_frame -or $item.worst_frame -gt $item.last_frame -or
            $item.maximum_frame_ms -le $facts.threshold_ms -or $item.frame_interval_total_ms -lt $item.maximum_frame_ms -or
            $item.frame_interval_total_ms -gt $item.slow_frames*$item.maximum_frame_ms -or
            $item.frame_interval_total_ms -le $item.slow_frames*$facts.threshold_ms) {
            throw 'inconsistent_frame_pacing_episode'
        }
        $item.span_ms = ($item.end_us - $item.begin_us) / 1000.0
        $item.slowest_frame_fps_equivalent = 1000.0 / $item.maximum_frame_ms
        $item.slow_frames_mean_fps_equivalent = 1000.0 * $item.slow_frames / $item.frame_interval_total_ms
        $item.recovery_observed = Get-Boolean $entry 'recovery_observed'
        $item.end_status = Get-Field $entry 'end_status'
        if ($item.end_status -notin @('recording_end_or_still_active', 'recovered', 'context_boundary',
            'generation_boundary', 'life_boundary', 'missing_interval', 'frame_number_gap') -or
            $item.recovery_observed -ne ($item.end_status -eq 'recovered')) { throw 'invalid_frame_pacing_end' }
        $item.worst_frame_timings = [ordered]@{}
        $timings = Get-Object $entry 'worst_frame_timings' -Required
        foreach ($name in @('frame_interval_ms', 'cpu_update_ms', 'network_ms', 'parse_commit_ms', 'prediction_ms',
            'collision_ms', 'studio_ms', 'asset_ms', 'render_ms', 'present_ms', 'report_ms', 'gpu_ms')) {
            $item.worst_frame_timings[$name] = Get-Number $timings $name
        }
        foreach ($name in @('observed_players', 'entities')) { $item[$name] = Get-Number $entry $name -Integer }
        $prediction = Get-Field $entry 'prediction_active'
        if ($null -ne $prediction -and $prediction -isnot [bool]) { throw 'invalid_frame_pacing_prediction_status' }
        $item.prediction_active = $prediction
        $item.prediction_work = Get-PredictionWork (Get-Field $entry 'prediction_work')
        [pscustomobject]$item
    })
    return $facts
}
function Get-RemoteSignedNumber($Object, [string]$Name) {
    $value = Get-Field $Object $Name
    if ($null -eq $value) { return $null }
    if ($value -is [string] -or $value -is [bool] -or $value -is [Collections.IEnumerable] -or
        -not [double]::IsFinite([double]$value)) { throw 'invalid_remote_number' }
    return $value
}
function Get-RemoteVector($Object, [string]$Name) {
    $value = Get-Field $Object $Name
    if ($null -eq $value) { return $null }
    if ($value -isnot [Collections.IList] -or $value.Count -ne 3) { throw 'invalid_remote_vector' }
    $components = @(foreach ($part in $value) {
        if ($null -eq $part) { throw 'invalid_remote_vector_component' }
        $number = Get-RemoteSignedNumber @{ component = $part } 'component'
        if ([Math]::Abs([double]$number) -gt 1000000000) { throw 'remote_position_out_of_bounds' }
        $number
    })
    return ,$components
}
function Get-RemoteMask($Object, [string]$Name, [string[]]$Labels) {
    $value = Get-Number $Object $Name -Required -Integer
    if ($value -gt ([Math]::Pow(2, $Labels.Count) - 1)) { throw 'invalid_remote_reason_mask' }
    return [ordered]@{
        mask = $value
        reasons = @(for ($index = 0; $index -lt $Labels.Count; $index++) {
            if ([UInt64]$value -band ([UInt64]1 -shl $index)) { $Labels[$index] }
        })
    }
}
$remoteDecisions = @('interpolated', 'exact_previous', 'exact_current', 'held_oldest',
    'held_newest', 'held_only', 'held_floor', 'held_current')
$remoteContinuityReasons = @('first_observation', 'history_stale', 'no_interpolation_option',
    'explicit_discontinuity', 'model_changed', 'schema_changed', 'nointerp_effect', 'teleport_distance',
    'removal', 'reentry', 'same_time_origin_correction', 'game_boundary', 'clock_reset')
$remoteHoldReasons = @('floor_actor_missing', 'bracket_actor_missing', 'target_before_floor',
    'bracket_before_floor', 'selected_model_mismatch', 'bracket_regression', 'per_actor_reset')
$remoteClockReasons = @('initial_anchor', 'explicit_reset', 'generation_changed', 'server_time_missing',
    'server_time_regressed', 'snapshot_gap', 'application_gap', 'mapped_time_invalid',
    'mapped_time_drift', 'oldest_underflow', 'newest_underflow')
$remoteTriggers = @('first_observation', 'disappearance', 'reentry', 'generation_boundary',
    'lifetime_boundary', 'new_boundary', 'decision_change', 'displacement_threshold',
    'render_state_change', 'support_change')
function Get-RemoteSample($Object) {
    if ($Object -isnot [Collections.IDictionary]) { throw 'invalid_remote_sample' }
    $sample = [ordered]@{}
    foreach ($name in @('monotonic_us', 'frame_number', 'network_generation', 'map_generation',
        'entity_index', 'model_index', 'lifetime', 'floor_record_ordinal', 'resolved_floor_record_ordinal',
        'position_previous_record_ordinal', 'position_current_record_ordinal')) {
        $sample[$name] = Get-Number $Object $name -Required -Integer
    }
    if ($sample.entity_index -lt 1 -or $sample.entity_index -gt 32 -or
        $sample.model_index -gt [UInt32]::MaxValue) { throw 'invalid_remote_actor_identity' }
    $sample.present = Get-Boolean $Object 'present'
    foreach ($name in @('previous_raw_origin', 'current_raw_origin', 'latest_raw_origin', 'sampled_origin', 'rendered_origin')) {
        $sample[$name] = Get-RemoteVector $Object $name
    }
    foreach ($name in @('previous_source_ordinal', 'current_source_ordinal', 'latest_source_ordinal',
        'selected_main', 'selected_gait', 'selected_weapon', 'raw_main', 'raw_gait', 'raw_weapon', 'raw_effects',
        'raw_render_mode', 'raw_render_amount', 'raw_render_effect', 'support_entity', 'support_model')) {
        $sample[$name] = Get-Number $Object $name -Integer
        if ($name -notlike '*source_ordinal' -and $null -ne $sample[$name] -and
            $sample[$name] -gt [UInt32]::MaxValue) { throw 'invalid_remote_actor_field' }
    }
    foreach ($name in @('previous_server_s', 'current_server_s', 'latest_server_s', 'sample_server_s')) {
        $sample[$name] = Get-Number $Object $name
        if ($null -ne $sample[$name] -and $sample[$name] -gt 1000000000) { throw 'remote_server_time_out_of_bounds' }
    }
    $sample.rendered_opacity = Get-Number $Object 'rendered_opacity'
    if ($null -ne $sample.rendered_opacity -and $sample.rendered_opacity -gt 1) { throw 'invalid_remote_rendered_opacity' }
    $sample.support_offset_z = Get-RemoteSignedNumber $Object 'support_offset_z'
    if ($null -ne $sample.support_offset_z -and [Math]::Abs($sample.support_offset_z) -gt 128) { throw 'invalid_remote_support_offset' }
    foreach ($name in @('pose_submitted', 'visible')) {
        $value = Get-Field $Object $name
        if ($null -ne $value -and $value -isnot [bool]) { throw 'invalid_remote_optional_boolean' }
        $sample[$name] = $value
    }
    $sample.alpha = Get-Number $Object 'alpha' -Required
    if ($sample.alpha -gt 1) { throw 'invalid_remote_interpolation_alpha' }
    $sample.decision = Get-Field $Object 'decision'
    if ($sample.decision -cnotin $remoteDecisions) { throw 'invalid_remote_presentation_decision' }
    foreach ($name in @('floor_reasons', 'new_boundary_reasons', 'hold_reasons', 'clock_reasons')) {
        $labels = if ($name -eq 'hold_reasons') { $remoteHoldReasons } elseif ($name -eq 'clock_reasons') {
            $remoteClockReasons
        } else { $remoteContinuityReasons }
        $mask = Get-RemoteMask $Object $name $labels
        $sample[$name] = $mask.mask
        $sample[($name + '_labels')] = $mask.reasons
    }
    # Absence records have no observed pose. Their neutral default decision is
    # not evidence that the renderer held an invisible player's position.
    $sample.decision_observed = $sample.present
    return $sample
}
function Get-RemoteCounterVector($Object, [string]$Name, [string[]]$Labels) {
    $entries = Get-Field $Object $Name
    if ($entries -isnot [Collections.IList] -or $entries.Count -ne $Labels.Count) {
        throw 'invalid_remote_counter_vector'
    }
    $result = [ordered]@{}
    for ($index = 0; $index -lt $Labels.Count; $index++) {
        $result[$Labels[$index]] = Get-Number @{ count = $entries[$index] } 'count' -Required -Integer
    }
    return $result
}
function Get-RemoteArray($Object, [string]$Name, [int]$Maximum) {
    $entries = Get-Field $Object $Name
    if ($entries -isnot [Collections.IList] -or $entries.Count -gt $Maximum) {
        throw 'remote_collection_out_of_bounds'
    }
    return ,$entries
}
function Get-RemoteEvents($Object, [string]$Name, [int]$Maximum) {
    $eventOrdinals = [Collections.Generic.HashSet[UInt64]]::new()
    foreach ($event in (Get-RemoteArray $Object $Name $Maximum)) {
        if ($event -isnot [Collections.IDictionary]) { throw 'invalid_remote_event' }
        $item = [ordered]@{ ordinal = Get-Number $event 'ordinal' -Required -Integer }
        if ($item.ordinal -eq 0 -or -not $eventOrdinals.Add([UInt64]$item.ordinal)) { throw 'invalid_remote_event_ordinal' }
        $trigger = Get-RemoteMask $event 'trigger_mask' $remoteTriggers
        if ($trigger.mask -eq 0) { throw 'invalid_remote_event_trigger' }
        $item.trigger_mask = $trigger.mask; $item.triggers = $trigger.reasons
        $previous = Get-Object $event 'before'
        $item.before = if ($null -ne $previous) { Get-RemoteSample $previous } else { $null }
        $item.current = Get-RemoteSample (Get-Object $event 'current' -Required)
        if ($null -ne $item.before -and ($item.before.monotonic_us -gt $item.current.monotonic_us -or
            $item.before.entity_index -ne $item.current.entity_index)) { throw 'invalid_remote_event_pair' }
        $delta = Get-Object $event 'delta' -Required
        $item.delta = [ordered]@{ elapsed_us = Get-Number $delta 'elapsed_us' -Integer }
        foreach ($name in @('raw_units', 'sampled_units', 'rendered_units', 'sampled_speed', 'rendered_speed')) {
            $item.delta[$name] = Get-Number $delta $name
        }
        if ($null -ne $item.delta.elapsed_us -and ($null -eq $item.before -or $item.delta.elapsed_us -eq 0 -or
            $item.delta.elapsed_us -ne ($item.current.monotonic_us - $item.before.monotonic_us) -or
            -not $item.before.present -or -not $item.current.present -or
            $item.before.network_generation -ne $item.current.network_generation -or
            $item.before.map_generation -ne $item.current.map_generation -or
            $item.before.lifetime -ne $item.current.lifetime)) {
            throw 'invalid_remote_event_elapsed'
        }
        if ($null -eq $item.delta.elapsed_us -and @($item.delta.Values | Where-Object { $null -ne $_ }).Count) {
            throw 'remote_distance_without_comparable_interval'
        }
        [pscustomobject]$item
    }
}
function Get-RemotePlayers($Object) {
    if ($null -eq $Object) { return [ordered]@{ availability = 'not_recorded' } }
    if ((Get-Field $Object 'schema') -cne 'hlclient.remote-players.v1' -or
        (Get-Field $Object 'causality') -cne 'observations_not_root_cause_proof') {
        throw 'invalid_remote_players_profile'
    }
    $facts = [ordered]@{
        requested = Get-Boolean $Object 'requested'
        enabled = Get-Boolean $Object 'enabled'
        availability = Get-Field $Object 'availability'
        causality = 'observations_not_root_cause_proof'
    }
    if ($facts.availability -cnotin @('ready', 'disabled', 'allocation_failed', 'budget_exceeded', 'encode_failed', 'awaiting_presentation') -or
        $facts.enabled -ne ($facts.availability -in @('ready', 'budget_exceeded', 'encode_failed', 'awaiting_presentation')) -or
        ($facts.enabled -and -not $facts.requested)) { throw 'invalid_remote_players_availability' }
    if ($facts.availability -ne 'ready') { return $facts }
    $limits = Get-Object $Object 'limits' -Required
    $expectedLimits = [ordered]@{
        max_actors = 32; recent_samples = 640; event_records = 128; manual_captures = 2; manual_samples = 160; manual_events = 64
        retention_us = 4000000; sample_interval_us = 200000; event_cooldown_us = 250000; manual_pre_us = 1000000
        displacement_trigger_units = 8; maximum_capacity_bytes = 1MB; maximum_encoded_bytes = 2MB
    }
    $facts.limits = [ordered]@{}
    foreach ($name in $expectedLimits.Keys) {
        $facts.limits[$name] = Get-Number $limits $name -Required -Integer
        if ($facts.limits[$name] -ne $expectedLimits[$name]) { throw 'invalid_remote_players_limits' }
    }
    $statistics = Get-Object $Object 'statistics' -Required
    $facts.statistics = [ordered]@{}
    foreach ($name in @('accepted_frames', 'duplicate_frames', 'rejected_frames', 'invalid_observations',
        'invalid_values', 'duplicate_actors', 'absences', 'reentries', 'generation_boundaries', 'lifetime_boundaries',
        'event_capacity_drops', 'recent_capacity_drops', 'retention_evictions', 'event_cooldown_suppressions', 'manual_capture_drops',
        'retained_samples', 'retained_events', 'manual_captures', 'allocated_capacity_bytes')) {
        $facts.statistics[$name] = Get-Number $statistics $name -Required -Integer
    }
    if ($facts.statistics.allocated_capacity_bytes -gt $facts.limits.maximum_capacity_bytes) {
        throw 'remote_allocation_out_of_bounds'
    }
    $actorIds = [Collections.Generic.HashSet[UInt64]]::new()
    $facts.actors = @(foreach ($actor in (Get-RemoteArray $Object 'actors' 32)) {
        if ($actor -isnot [Collections.IDictionary]) { throw 'invalid_remote_actor_summary' }
        $item = [ordered]@{}
        foreach ($name in @('entity_index', 'samples', 'comparisons', 'absences', 'reentries', 'lifetime_boundaries',
            'generation_boundaries', 'displacements', 'new_boundaries', 'decision_changes', 'event_cooldown_suppressions',
            'raw_comparisons', 'sampled_comparisons', 'rendered_comparisons')) {
            $item[$name] = Get-Number $actor $name -Required -Integer
        }
        if ($item.entity_index -lt 1 -or $item.entity_index -gt 32 -or
            -not $actorIds.Add([UInt64]$item.entity_index) -or $item.comparisons -gt $item.samples) {
            throw 'invalid_remote_actor_summary_identity_or_counts'
        }
        foreach ($name in @('raw_distance_sum', 'sampled_distance_sum', 'rendered_distance_sum',
            'max_raw_units', 'max_sampled_units', 'max_rendered_units', 'max_sampled_speed', 'max_rendered_speed')) {
            $item[$name] = Get-Number $actor $name
            $kind = if ($name -match 'raw') { 'raw' } elseif ($name -match 'sampled') { 'sampled' } else { 'rendered' }
            $observations = $item[($kind + '_comparisons')]
            if ($observations -gt $item.comparisons -or
                ($observations -gt 0) -ne ($null -ne $item[$name])) { throw 'invalid_remote_distance_observation_count' }
        }
        $item.decision_counts = Get-RemoteCounterVector $actor 'decision_counts' $remoteDecisions
        $item.boundary_counts = Get-RemoteCounterVector $actor 'boundary_counts' $remoteContinuityReasons
        $item.hold_counts = Get-RemoteCounterVector $actor 'hold_counts' $remoteHoldReasons
        $item.held_observations = [UInt64]0
        $decisionTotal = [decimal]0
        foreach ($decision in $remoteDecisions) {
            if ($decision.StartsWith('held_')) { $item.held_observations += $item.decision_counts[$decision] }
            $decisionTotal += $item.decision_counts[$decision]
        }
        if ($decisionTotal -ne $item.samples) { throw 'invalid_remote_decision_counts' }
        [pscustomobject]$item
    })
    $facts.recent_samples = @(foreach ($sample in (Get-RemoteArray $Object 'recent_samples' 640)) {
        Get-RemoteSample $sample
    })
    $facts.events = @(Get-RemoteEvents $Object 'events' 128)
    $facts.manual_captures = @(foreach ($capture in (Get-RemoteArray $Object 'manual_captures' 2)) {
        if ($capture -isnot [Collections.IDictionary]) { throw 'invalid_remote_manual_capture' }
        $item = [ordered]@{
            marker_us = Get-Number $capture 'marker_us' -Required -Integer
            truncated = Get-Boolean $capture 'truncated'
            event_truncated = Get-Boolean $capture 'event_truncated'
            samples = @(foreach ($sample in (Get-RemoteArray $capture 'samples' 160)) { Get-RemoteSample $sample })
            events = @(Get-RemoteEvents $capture 'events' 64)
        }
        foreach ($sample in $item.samples) {
            if ($sample.monotonic_us -gt $item.marker_us -or $item.marker_us - $sample.monotonic_us -gt 1000000) {
                throw 'remote_manual_capture_time_out_of_bounds'
            }
        }
        foreach ($event in $item.events) {
            if ($event.current.monotonic_us -gt $item.marker_us -or $item.marker_us - $event.current.monotonic_us -gt 1000000) {
                throw 'remote_manual_event_time_out_of_bounds'
            }
        }
        [pscustomobject]$item
    })
    if ($facts.statistics.retained_samples -ne $facts.recent_samples.Count -or
        $facts.statistics.retained_events -ne $facts.events.Count -or
        $facts.statistics.manual_captures -ne $facts.manual_captures.Count) { throw 'remote_retained_counts_mismatch' }
    $facts.limitations = @('per_actor_observations_not_automatic_jerk_or_cause_verdict',
        'displacement_threshold_is_not_speed_or_teleport_classification',
        'event_deltas_compare_observed_frames_not_retained_sample_interval',
        'raw_sampled_rendered_positions_are_distinct_observations',
        'held_pose_decisions_are_not_stationary_or_invisible_player_proof',
        'actor_summary_aggregates_reused_entity_slots_across_labelled_lifetimes',
        'global_presentation_clock_does_not_establish_per_actor_continuity',
        'retention_cooldown_and_capacity_can_omit_event_details')
    return $facts
}
function Format-Value($Value, [string]$Suffix = '') {
    if ($null -eq $Value) { return 'н/д' }
    return ([double]$Value).ToString('0.###', [Globalization.CultureInfo]::InvariantCulture) + $Suffix
}
function Format-Scaled($Value, [double]$Divisor) {
    if ($null -eq $Value) { return 'н/д' }
    return Format-Value ($Value / $Divisor)
}
function Format-RemotePosition($Value) {
    if ($null -eq $Value) { return 'н/д' }
    return '[' + (($Value | ForEach-Object { Format-Value $_ }) -join ', ') + ']'
}
function Write-RemotePlayers($Facts) {
    if ($Facts.availability -ne 'ready') {
        $status = switch ($Facts.availability) {
            not_recorded { 'не записувалася в цій збірці' }
            disabled { 'вимкнена' }
            allocation_failed { "не почалася: пам'ять для запису недоступна" }
            budget_exceeded { 'недоступна: перевищено обмеження розміру звіту' }
            encode_failed { 'недоступна: не вдалося сформувати розділ звіту' }
            awaiting_presentation { 'ще не отримала придатного кадру відображення' }
        }
        Write-Output ('Діагностика окремих інших гравців: {0}. Це не нуль подій і не підтвердження плавності.' -f $status)
        return
    }
    Write-Output ('Інші гравці: записано кадрів {0}; акторів {1}; збережено останніх зразків {2}, автоматичних подій {3}, позначок F8 {4}.' -f
        $Facts.statistics.accepted_frames, $Facts.actors.Count, $Facts.recent_samples.Count, $Facts.events.Count, $Facts.manual_captures.Count)
    Write-Output ('Пропущено деталей: ліміт подій {0}, cooldown {1}, ліміт останніх зразків {2}, витіснення за часом {3}; невалідних спостережень {4}, значень {5}.' -f
        $Facts.statistics.event_capacity_drops, $Facts.statistics.event_cooldown_suppressions,
        $Facts.statistics.recent_capacity_drops, $Facts.statistics.retention_evictions,
        $Facts.statistics.invalid_observations, $Facts.statistics.invalid_values)
    foreach ($actor in $Facts.actors) {
        $reasons = @($actor.boundary_counts.Keys | Where-Object { $actor.boundary_counts[$_] -gt 0 } |
            ForEach-Object { '{0}={1}' -f $_, $actor.boundary_counts[$_] })
        Write-Output ('Актор entity={0}: зразків {1}, зіставлень {2}, переміщень понад поріг {3}, held-рішень {4}, нових меж {5}; max raw/sample/render {6}/{7}/{8} од.; причини меж: {9}.' -f
            $actor.entity_index, $actor.samples, $actor.comparisons, $actor.displacements, $actor.held_observations,
            $actor.new_boundaries, (Format-Value $actor.max_raw_units), (Format-Value $actor.max_sampled_units),
            (Format-Value $actor.max_rendered_units), $(if ($reasons.Count) { $reasons -join ', ' } else { 'не спостережено' }))
    }
    foreach ($capture in $Facts.manual_captures) {
        Write-Output ('Позначка F8: локальний час {0} с; зразків {1}, точних пар подій {2}; пропущені за лімітом зразки={3}, події={4}.' -f
            (Format-Scaled $capture.marker_us 1000000), $capture.samples.Count, $capture.events.Count,
            $capture.truncated, $capture.event_truncated)
    }
    foreach ($event in @($Facts.events | Sort-Object { $_.current.monotonic_us } -Descending | Select-Object -First 10)) {
        $sample = $event.current
        Write-Output ('Подія актора {0}, life={1}, локальний час {2} с: {3}; present={4}, decision={5}; Δt={6} мс, raw/sample/render={7}/{8}/{9} од., sample/render speed={10}/{11} од./с.' -f
            $sample.entity_index, $sample.lifetime, (Format-Scaled $sample.monotonic_us 1000000),
            ($event.triggers -join ','), $sample.present, $(if ($sample.present) { $sample.decision } else { 'не спостерігалося' }),
            (Format-Scaled $event.delta.elapsed_us 1000), (Format-Value $event.delta.raw_units),
            (Format-Value $event.delta.sampled_units), (Format-Value $event.delta.rendered_units),
            (Format-Value $event.delta.sampled_speed), (Format-Value $event.delta.rendered_speed))
        Write-Output ('  Координати: raw previous/current/latest={0}/{1}/{2}; sampled={3}; rendered={4}; sequence/gait/weapon={5}/{6}/{7}.' -f
            (Format-RemotePosition $sample.previous_raw_origin), (Format-RemotePosition $sample.current_raw_origin),
            (Format-RemotePosition $sample.latest_raw_origin), (Format-RemotePosition $sample.sampled_origin),
            (Format-RemotePosition $sample.rendered_origin), (Format-Value $sample.selected_main),
            (Format-Value $sample.selected_gait), (Format-Value $sample.selected_weapon))
        Write-Output ('  Нові межі={0}; чинна floor={1}; hold={2}; clock={3}. Позначки source previous/current/latest={4}/{5}/{6}, серверний час previous/current/latest/sample={7}/{8}/{9}/{10} с.' -f
            ($sample.new_boundary_reasons_labels -join ','), ($sample.floor_reasons_labels -join ','),
            ($sample.hold_reasons_labels -join ','), ($sample.clock_reasons_labels -join ','),
            (Format-Value $sample.previous_source_ordinal), (Format-Value $sample.current_source_ordinal),
            (Format-Value $sample.latest_source_ordinal), (Format-Value $sample.previous_server_s),
            (Format-Value $sample.current_server_s), (Format-Value $sample.latest_server_s), (Format-Value $sample.sample_server_s))
    }
    Write-Output 'Δt події — між фактичними кадрами спостереження, не між прорідженими зразками. Поріг переміщення не доводить ривок; held не означає нерухомість гравця.'
    Write-Output 'Справний загальний годинник не доводить плавності кожного гравця. Координати та причини меж — спостереження, не автоматично встановлена причина.'
}
function Format-Metric($Metrics, [string]$Name, [string]$Unit = 'мс') {
    $metric = Get-Field $Metrics $Name
    if ($null -eq $metric -or $metric.observed -eq 0) { return 'немає вимірів' }
    return ('p50 ≤ {0}; p95 ≤ {1}; p99 ≤ {2}; max {3} {5}; вимірів {4}' -f
        (Format-Value $metric.p50_upper_bound), (Format-Value $metric.p95_upper_bound),
        (Format-Value $metric.p99_upper_bound), (Format-Value $metric.maximum), $metric.observed, $Unit)
}
function Get-RankingMaximum($Metrics, [string]$Name) {
    $metric = Get-Field $Metrics $Name
    if ($null -eq $metric -or $metric.observed -eq 0 -or $null -eq $metric.maximum) { return -1 }
    return $metric.maximum
}

function Read-AdjacentIdentityManifest([IO.DirectoryInfo]$Directory,[string]$Leaf) {
    # Only these two literal neighbouring files are eligible. JSON references
    # never supply a path, and no executable/configuration is read or hashed.
    if($Leaf -cnotin @('launch-manifest.json','process-manifest.json')){throw 'invalid_identity_manifest_leaf'}
    for($ancestor=$Directory;$null -ne $ancestor;$ancestor=$ancestor.Parent) {
        if($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'identity_manifest_reparse_directory'}
    }
    $path=Join-Path $Directory.FullName $Leaf
    if(-not (Test-Path -LiteralPath $path)){return $null}
    $item=Get-Item -LiteralPath $path -Force
    if($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'identity_manifest_not_regular_file'}
    if([IO.Path]::GetDirectoryName($item.FullName) -cne $Directory.FullName){throw 'identity_manifest_not_adjacent'}
    $manifestStream=[IO.File]::Open($item.FullName,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try {
        if($manifestStream.Length -eq 0 -or $manifestStream.Length -gt 16KB){throw 'identity_manifest_size_out_of_bounds'}
        $buffer=[byte[]]::new([int]$manifestStream.Length);$readTotal=0
        while($readTotal -lt $buffer.Length) {
            $read=$manifestStream.Read($buffer,$readTotal,$buffer.Length-$readTotal)
            if($read -eq 0){throw 'identity_manifest_changed_during_read'}
            $readTotal+=$read
        }
        if($manifestStream.ReadByte() -ne -1){throw 'identity_manifest_changed_during_read'}
        $after=Get-Item -LiteralPath $path -Force
        if($after.PSIsContainer -or ($after.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'identity_manifest_changed_during_read'}
    } finally {$manifestStream.Dispose()}
    try {$value=ConvertFrom-Json -InputObject ([Text.UTF8Encoding]::new($false,$true).GetString($buffer)) -AsHashtable -Depth 8}
    catch {throw 'identity_manifest_invalid_utf8_or_json'}
    if($value -isnot [Collections.IDictionary]){throw 'identity_manifest_not_object'}
    return $value
}
function Get-ManifestSha256($Manifest,[string]$Name) {
    $value=Get-Field $Manifest $Name
    if($value -isnot [string] -or $value -cnotmatch '^[0-9a-fA-F]{64}$'){throw 'identity_manifest_invalid_sha256'}
    return $value.ToLowerInvariant()
}
function Get-AdjacentRunIdentity([string]$Path,[string]$RunId,$ProcessId,$ProcessCreation) {
    $reportFile=Get-Item -LiteralPath $Path -Force
    if($reportFile.PSIsContainer -or ($reportFile.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'identity_report_not_regular_file'}
    $launch=Read-AdjacentIdentityManifest $reportFile.Directory 'launch-manifest.json'
    $process=Read-AdjacentIdentityManifest $reportFile.Directory 'process-manifest.json'
    $facts=[ordered]@{availability='not_recorded_older_or_fixture';scope='stored_manifest_consistency_only'
        launch_present=($null -ne $launch);process_present=($null -ne $process)
        recorded_identity_consistent=$null;current_binaries_rehashed=$false}
    if($null -eq $launch -and $null -eq $process){return $facts}
    $launchClient=$null;$processClient=$null
    if($null -ne $launch) {
        if((Get-Field $launch 'schema') -cne 'hlclient.external-launch-identity.v1'){throw 'unsupported_launch_identity_schema'}
        if((Get-Field $launch 'run_id') -cne $RunId){throw 'launch_run_identity_mismatch'}
        $launchClient=Get-ManifestSha256 $launch 'client_sha256'
        $facts.launcher_sha256=Get-ManifestSha256 $launch 'launcher_sha256'
        $facts.config_sha256=Get-ManifestSha256 $launch 'config_sha256'
        # The current launcher writes its launch manifest before spawn. If a
        # future producer includes process fields, both must agree exactly.
        $launchPid=Get-Field $launch 'process_id';$launchCreation=Get-Field $launch 'process_creation'
        if($null -ne $launchPid -or $null -ne $launchCreation) {
            if([decimal](Get-Number $launch 'process_id' -Required -Integer) -ne [decimal]$ProcessId -or
                [decimal](Get-Number $launch 'process_creation' -Required -Integer) -ne [decimal]$ProcessCreation){throw 'launch_process_identity_mismatch'}
        }
    }
    if($null -ne $process) {
        if((Get-Field $process 'schema') -cne 'hlclient.external-process-identity.v1'){throw 'unsupported_process_identity_schema'}
        if((Get-Field $process 'run_id') -cne $RunId){throw 'process_run_identity_mismatch'}
        if([decimal](Get-Number $process 'process_id' -Required -Integer) -ne [decimal]$ProcessId -or
            [decimal](Get-Number $process 'process_creation' -Required -Integer) -ne [decimal]$ProcessCreation){throw 'process_identity_mismatch'}
        $processClient=Get-ManifestSha256 $process 'client_sha256'
    }
    if($null -eq $launch -or $null -eq $process){$facts.availability='partial_manifest_identity_unknown';return $facts}
    if($launchClient -cne $processClient){throw 'client_binary_manifest_identity_mismatch'}
    $facts.availability='consistent_manifest_pair';$facts.recorded_identity_consistent=$true
    $facts.client_sha256=$launchClient
    return $facts
}

function Get-MultiplayerTimeline($Object) {
    if ($null -eq $Object) { return [ordered]@{ availability='not_recorded_in_older_build'; classifications=@(); action_chains=@() } }
    if ((Get-Field $Object 'schema') -cne 'hlclient.multiplayer-timeline.v1') { throw 'unsupported_multiplayer_timeline_schema' }
    if ((Get-Field $Object 'encoding_failed') -eq $true) { return [ordered]@{ availability='encoding_failed'; classifications=@(); action_chains=@() } }
    if (-not (Get-Boolean $Object 'enabled')) {
        return [ordered]@{ availability=$(if((Get-Field $Object 'availability') -eq 'allocation_failed'){'allocation_failed'}else{'disabled'})
            server_outcome_default='unknown'; classifications=@(); action_chains=@() }
    }
    if ((Get-Field $Object 'local_clock') -cne 'steady_epoch_nanoseconds') { throw 'unsupported_multiplayer_clock' }
    $enabled=Get-Boolean $Object 'enabled'
    $facts=[ordered]@{ availability=$(if($enabled){'ready'}else{'disabled'}); server_outcome_default='unknown'
        scope='retained_causal_details_not_unthinned_fps_statistics'; classifications=@(); action_chains=@()
        encoded_events_omitted=(Get-Number $Object 'encoded_events_omitted' -Required -Integer)
        encoding_deadline_reached=(Get-Boolean $Object 'encoding_deadline_reached')
        missing_evidence=@('actual_server_command_execution','server_rewind_pose','physical_input_and_scanout_time') }
    $statistics=Get-Object $Object 'statistics' -Required
    $facts.statistics=[ordered]@{}
    foreach($name in @('observed','accepted','invalid','duplicates','stale_generation','sampled_out','recent_evictions','exact_evictions',
        'manual_omissions','manual_marks','manual_marks_dropped','coverage_omissions','allocation_failures','capacity_bytes')) {
        $facts.statistics[$name]=Get-Number $statistics $name -Required -Integer
    }
    if($facts.statistics.capacity_bytes -gt 2MB){throw 'multiplayer_capacity_out_of_bounds'}
    $all=[Collections.Generic.List[object]]::new()
    foreach($window in (Get-RemoteArray $Object 'manual_windows' 8)) {
        $begin=Get-Number $window 'begin_ns' -Required -Integer;$end=Get-Number $window 'end_ns' -Required -Integer
        $trigger=Get-Number $window 'trigger_ns' -Required -Integer
        if($end -lt $trigger -or $begin -gt $trigger -or $trigger-$begin -gt 4000000000 -or $end-$trigger -ne 1000000000){throw 'invalid_multiplayer_manual_window'}
        foreach($event in (Get-RemoteArray $window 'events' 64)){$all.Add($event)}
    }
    foreach($event in (Get-RemoteArray $Object 'exact_transitions' 512)){$all.Add($event)}
    foreach($event in (Get-RemoteArray $Object 'recent_events' 1280)){$all.Add($event)}
    $events=[Collections.Generic.List[object]]::new();$seen=@{}
    foreach($event in $all) {
        $identity=Get-Object $event 'identity' -Required;$payload=Get-Object $event 'payload' -Required
        $id=[ordered]@{}
        foreach($name in @('producer','producer_event_id','network_generation','map_generation')){$id[$name]=Get-Number $identity $name -Required -Integer}
        if($id.producer -gt 11 -or $id.producer_event_id -eq 0){throw 'invalid_multiplayer_identity'}
        foreach($name in @('local_life','actor_entity','actor_occupant','actor_visual_lifetime','actor_model_revision','input_sequence','command_id','action_id',
            'carrier_packet','source_sequence','source_record','source_record_ordinal','source_message_bit_offset','source_entry_ordinal','source_event',
            'simulation_iteration','render_frame','scene_revision','pose_revision','related_action_id','related_actor_entity','source_query_id')){$id[$name]=Get-Number $identity $name -Integer}
        $stamp=Get-Number $event 'local_steady_ns' -Required -Integer
        $type=Get-Number $payload 'type' -Required -Integer;$knowledge=Get-Number $event 'knowledge' -Required -Integer
        $provenance=Get-Number $event 'provenance' -Required -Integer;$edge=Get-Number $event 'edge' -Required -Integer
        $domain=Get-Number $event 'server_time_domain' -Required -Integer;$serverSeconds=Get-RemoteSignedNumber $event 'server_seconds'
        if($stamp -eq 0 -or $type -gt 9 -or $knowledge -gt 10 -or $provenance -gt 5 -or $edge -gt 3 -or $domain -gt 3){throw 'invalid_multiplayer_event_enum'}
        $key='{0}/{1}/{2}/{3}' -f $id.producer,$id.network_generation,$id.map_generation,$id.producer_event_id
        $fingerprint=$event|ConvertTo-Json -Compress -Depth 12
        if($seen.ContainsKey($key)){if($seen[$key] -cne $fingerprint){throw 'conflicting_multiplayer_event_identity'};continue}
        $seen[$key]=$fingerprint
        $events.Add([pscustomobject]@{identity=$id;local_steady_ns=$stamp;type=$type;payload=$payload;knowledge=$knowledge;provenance=$provenance;edge=$edge;domain=$domain;server_seconds=$serverSeconds})
    }
    $ordered=@($events|Sort-Object local_steady_ns,@{Expression={$_.identity.producer}},@{Expression={$_.identity.producer_event_id}})
    $findings=[Collections.Generic.List[object]]::new();$chains=[Collections.Generic.List[object]]::new();$receiveLinks=[Collections.Generic.List[object]]::new()
    $commands=@{};$carriers=@{};$receives=@{};$frames=@{};$movement=@{}
    $findingsOmitted=0;$receiveLinksOmitted=0
    function Add-MultiplayerFinding($Event,[string]$Class,[string]$Code,[string]$Confidence,$Measurements,[string[]]$Missing=@()) {
        if($findings.Count -ge 128){$script:multiplayerFindingOmissions++;return}
        $findings.Add([ordered]@{classification=$Class;code=$Code;confidence=$Confidence;identity=$Event.identity;local_steady_ns=$Event.local_steady_ns
            evidence_kind=@('unknown','runtime_observed','derived_estimate','source_model','synthetic','user_report')[[int]$Event.provenance]
            measurements=$Measurements;missing_evidence=$Missing;server_hit_or_miss='not_inferred'})
    }
    $script:multiplayerFindingOmissions=0
    # First pass joins ONLY explicit generation-qualified command/frame IDs.
    # A nearby timestamp never invents an action or shooter association.
    foreach($event in $ordered) {
        $id=$event.identity;$p=$event.payload
        if($null -ne $id.command_id -and $event.type -eq 0 -and (Get-Number $p 'phase' -Required -Integer) -in @(2,3,5)) {
            $commands[('{0}/{1}/{2}' -f $id.network_generation,$id.map_generation,$id.command_id)]=$event
        }
        if($event.type -eq 1 -and (Get-Number $p 'phase' -Required -Integer) -eq 3) {
            $members=Get-RemoteArray $p 'command_ids' 64
            $backups=Get-Number $p 'backup_count' -Integer;$newCommands=Get-Number $p 'command_count' -Integer
            $membershipKnown=$null -ne $backups -and $null -ne $newCommands -and $backups+$newCommands -eq $members.Count
            for($member=0;$member -lt $members.Count;$member++) {
                $command=$members[$member]
                $number=Get-Number @{value=$command} 'value' -Required -Integer
                $key='{0}/{1}/{2}' -f $id.network_generation,$id.map_generation,$number
                if(-not $carriers.ContainsKey($key)) {
                    $carriers[$key]=@{event=$event;membership=$(if(-not $membershipKnown){'unknown'}elseif($member -lt $backups){'backup'}else{'new'})
                        retained_count=1}
                } else {$carriers[$key].retained_count++}
            }
        }
        if($event.type -eq 1 -and (Get-Number $p 'phase' -Required -Integer) -eq 4 -and $null -ne $id.source_sequence) {
            $key='{0}/{1}/{2}' -f $id.network_generation,$id.map_generation,$id.source_sequence
            $begin=Get-Number $p 'begin_ns' -Integer;$end=Get-Number $p 'end_ns' -Integer
            if(-not $receives.ContainsKey($key)){$receives[$key]=@{event=$event;begin=$begin;end=$end;ambiguous=$false}}
            elseif($receives[$key].begin -ne $begin -or $receives[$key].end -ne $end){$receives[$key].ambiguous=$true}
        }
        if($event.type -eq 4 -and $null -ne $id.render_frame) {
            $key='{0}/{1}/{2}' -f $id.network_generation,$id.map_generation,$id.render_frame
            # Swap timing, delayed GPU results and pose metadata are separate
            # owner facts for one exact frame. An absent measurement must never
            # erase an already observed value from a different owner event.
            foreach($scope in @('frame_ms','present_ms','gpu_ms')) {
                $measurement=Get-Number $p $scope
                if($null -ne $measurement) {
                    if(-not $frames.ContainsKey($key)){$frames[$key]=@{}}
                    $frames[$key][$scope]=@{value=$measurement;identity=$id}
                }
            }
        }
    }
    foreach($event in $ordered) {
        $id=$event.identity;$p=$event.payload
        if($null -ne $event.server_seconds -and $event.domain -eq 0) {
            Add-MultiplayerFinding $event 'clock_units_metadata_mismatch' 'server_seconds_without_clock_domain' 'exact_record_inconsistency' @{}
        }
        if($event.type -eq 1) {
            $phase=Get-Number $p 'phase' -Required -Integer
            if($event.knowledge -gt 5){Add-MultiplayerFinding $event 'server_contract_evidence_missing' 'transport_promoted_to_gameplay_outcome' 'exact_record_inconsistency' @{knowledge=$event.knowledge}}
            if($phase -eq 8 -and (Get-Field $p 'atomic_commit') -eq $true){Add-MultiplayerFinding $event 'client_receive_commit_backlog' 'rollback_marked_committed' 'exact_record_inconsistency' @{}}
            if($phase -eq 7) {
                $commit=Get-Number $p 'commit_ns' -Integer;$receive=$null
                if($null -ne $id.source_sequence) {
                    $receiveKey='{0}/{1}/{2}' -f $id.network_generation,$id.map_generation,$id.source_sequence
                    if($receives.ContainsKey($receiveKey)){$receive=$receives[$receiveKey]}
                }
                $timing='unknown_receive_not_retained';$elapsed=$null;$afterRead=$null
                if($receive -and $receive.ambiguous){$timing='unknown_ambiguous_receive_identity'}
                elseif($receive -and $null -ne $receive.begin -and $null -ne $receive.end -and $null -ne $commit) {
                    if($receive.begin -le $receive.end -and $receive.end -le $commit) {
                        $timing='measured_exact_source_sequence';$elapsed=[double]($commit-$receive.begin)/1e6;$afterRead=[double]($commit-$receive.end)/1e6
                        if($elapsed -ge 20.) {
                            Add-MultiplayerFinding $event 'client_receive_commit_backlog' 'measured_receive_to_commit_interval' 'observed_interval_not_cause' @{
                                milliseconds=$elapsed;after_socket_read_ms=$afterRead;receive_identity=$receive.event.identity}
                        }
                    } else {$timing='unknown_clock_order_inconsistent'}
                } elseif($receive){$timing='unknown_required_timestamp_missing'}
                if($receiveLinks.Count -lt 128) {
                    $receiveLinks.Add([ordered]@{commit_identity=$id;receive_identity=$(if($receive){$receive.event.identity}else{$null})
                        timing_status=$timing;receive_begin_to_commit_ms=$elapsed;receive_end_to_commit_ms=$afterRead})
                } else {$receiveLinksOmitted++}
            }
        } elseif($event.type -eq 0) {
            $phase=Get-Number $p 'phase' -Required -Integer
            $lerp=Get-Number $p 'lerp_msec' -Integer;$effective=Get-Number $p 'effective_interpolation_seconds'
            if($phase -in @(2,3,5) -and $null -ne $lerp -and $null -ne $effective -and [Math]::Abs([double]$lerp-1000.*$effective) -gt 1.) {
                Add-MultiplayerFinding $event 'clock_units_metadata_mismatch' 'wire_lerp_differs_from_declared_effective_delay' 'exact_record_inconsistency' @{lerp_msec=$lerp;effective_delay_ms=(1000.*$effective)} @('actual_server_rewind_contract')
            }
        } elseif($event.type -eq 2) {
            $phase=Get-Number $p 'phase' -Required -Integer
            if($phase -eq 8 -and $event.knowledge -in @(7,8)){Add-MultiplayerFinding $event 'cosmetic_feedback_mismatch' 'cosmetic_feedback_promoted_to_server_outcome' 'exact_record_inconsistency' @{}}
            if($phase -eq 6){Add-MultiplayerFinding $event 'expected_gameplay' 'local_action_rejected_reason_retained' 'local_observation' @{reason=(Get-CaptureToken $p 'reason' 48)}}
            if($phase -in @(1,2) -and $chains.Count -lt 128) {
                $key='{0}/{1}/{2}' -f $id.network_generation,$id.map_generation,$id.command_id
                $normalized=$(if($null -ne $id.command_id -and $commands.ContainsKey($key)){$commands[$key]}else{$null})
                $carrier=$(if($null -ne $id.command_id -and $carriers.ContainsKey($key)){$carriers[$key]}else{$null})
                $chains.Add([ordered]@{action_identity=$id;local_steady_ns=$event.local_steady_ns;local_acceptance_observed=($event.knowledge -eq 2)
                    immutable_command=$(if($normalized){$normalized.identity}else{$null});sent_carrier=$(if($carrier){$carrier.event.identity}else{$null})
                    sent_carrier_selection='earliest_retained_successful_carrier';first_actual_send='unknown_if_earlier_details_omitted'
                    sent_carrier_membership=$(if($carrier){$carrier.membership}else{'unknown'})
                    retained_sent_carrier_count=$(if($carrier){$carrier.retained_count}else{0});original_send_latency_ms=$null
                    server_execution='unknown';server_damage='unknown';correlation='generation_qualified_explicit_command_id_only'
                    missing_evidence=@($(if(-not $normalized){'immutable_command_not_retained'}),$(if(-not $carrier){'sent_carrier_not_retained'}),'server_action_execution_or_damage')|Where-Object{$_}})
                if($normalized) {
                    $angles=Get-RemoteVector $p 'command_angles_degrees';$turns=Get-RemoteArray $normalized.payload 'angle_turns' 3
                    if($angles -and $turns.Count -eq 3) {
                        $maximumDifference=0.
                        for($axis=0;$axis -lt 3;$axis++){$delta=(([double]$turns[$axis]*360./65536.-[double]$angles[$axis]+540.)%360.)-180.;$maximumDifference=[Math]::Max($maximumDifference,[Math]::Abs($delta))}
                        if($maximumDifference -gt 0.02){Add-MultiplayerFinding $event 'weapon_eligibility_mode_spread_ray' 'action_and_immutable_wire_aim_disagree' 'exact_command_identity' @{maximum_angle_difference_degrees=$maximumDifference} @('cause_units_boundary_or_double_punch_not_established')}
                    }
                }
            }
        } elseif($event.type -eq 4) {
            $sample=Get-RemoteSignedNumber $p 'sample_seconds';$pose=Get-RemoteSignedNumber $p 'pose_seconds'
            if($null -ne $sample -and $null -ne $pose -and [Math]::Abs($sample-$pose) -gt 0.001){Add-MultiplayerFinding $event 'remote_interpolation_pose_attachment' 'pose_origin_sample_times_disagree' 'same_record_observation' @{origin_sample_seconds=$sample;pose_sample_seconds=$pose}}
            $configured=Get-Number $p 'configured_delay_seconds';$effective=Get-Number $p 'effective_delay_seconds'
            if($null -ne $configured -and $null -ne $effective -and [Math]::Abs($configured-$effective) -gt 0.002){Add-MultiplayerFinding $event 'remote_interpolation_pose_attachment' 'configured_effective_interpolation_disagreement' 'same_record_observation' @{configured_seconds=$configured;effective_seconds=$effective} @('whether_policy_clamp_or_double_delay')}
            foreach($scope in @('present_ms','gpu_ms')) {
                $delay=Get-Number $p $scope
                if($null -ne $delay -and $delay -ge 20.) {
                    $measurements=@{milliseconds=$delay;frame_ms=$null;frame_measurement_identity=$null}
                    $frameKey='{0}/{1}/{2}' -f $id.network_generation,$id.map_generation,$id.render_frame
                    if($null -ne $id.render_frame -and $frames.ContainsKey($frameKey) -and $frames[$frameKey].ContainsKey('frame_ms')) {
                        $measurements.frame_ms=$frames[$frameKey].frame_ms.value
                        $measurements.frame_measurement_identity=$frames[$frameKey].frame_ms.identity
                    }
                    Add-MultiplayerFinding $event 'cpu_render_present_delay' ('measured_'+$scope) 'measured_scope_not_physics_cause' $measurements
                }
            }
        } elseif($event.type -eq 6) {
            if($event.knowledge -in @(7,8)){Add-MultiplayerFinding $event 'server_contract_evidence_missing' 'proxy_query_promoted_to_server_hit_or_miss' 'exact_record_inconsistency' @{}}
            if((Get-Field $p 'world_blocked') -eq $true){Add-MultiplayerFinding $event 'expected_gameplay' 'read_only_world_blocker_observed' 'local_proxy_not_server_result' @{fraction=(Get-Number $p 'fraction')}}
        } elseif($event.type -eq 8) {
            $key='{0}/{1}/{2}' -f $id.network_generation,$id.map_generation,$id.local_life
            $predicted=Get-RemoteVector $p 'predicted_origin';$raw=Get-RemoteVector $p 'raw_server_origin';$velocity=Get-RemoteVector $p 'velocity'
            if($movement.ContainsKey($key) -and $predicted -and $raw -and $velocity) {
                $previous=$movement[$key];$oldPredicted=Get-RemoteVector $previous.payload 'predicted_origin';$oldRaw=Get-RemoteVector $previous.payload 'raw_server_origin'
                $dt=([double]$event.local_steady_ns-[double]$previous.local_steady_ns)/1e9
                $frameKey='{0}/{1}/{2}' -f $id.network_generation,$id.map_generation,$id.render_frame
                if($oldPredicted -and $oldRaw -and $dt -ge 0.1 -and $dt -le 1. -and $id.command_id -gt $previous.identity.command_id -and $frames.ContainsKey($frameKey)) {
                    $frameMs=$(if($frames[$frameKey].ContainsKey('frame_ms')){$frames[$frameKey].frame_ms.value}else{$null});$pd=0.;$rd=0.;$speed=0.
                    for($axis=0;$axis -lt 3;$axis++){$pd+=[Math]::Pow($predicted[$axis]-$oldPredicted[$axis],2);$rd+=[Math]::Pow($raw[$axis]-$oldRaw[$axis],2);$speed+=$velocity[$axis]*$velocity[$axis]}
                    if($null -ne $frameMs -and $frameMs -le 10. -and $pd -lt 0.0001 -and $rd -gt 1. -and $speed -gt 1.){Add-MultiplayerFinding $event 'local_simulation_collision' 'prediction_hold_despite_source_motion_and_fast_render' 'candidate' @{interval_seconds=$dt;render_frame_ms=$frameMs} @('intermediate_simulation_collision_steps')}
                }
            }
            if(-not $movement.ContainsKey($key) -or ($event.local_steady_ns-$movement[$key].local_steady_ns) -gt 200000000){$movement[$key]=$event}
        } elseif($event.type -eq 3 -and (Get-Number $p 'kind' -Required -Integer) -in @(3,4,5,6)) {
            Add-MultiplayerFinding $event 'expected_gameplay' 'lifecycle_boundary_observed' 'observed_not_a_suppression_rule' @{kind=(Get-Number $p 'kind' -Required -Integer)}
        }
    }
    $facts.retained_unique_events=$events.Count;$facts.classifications=@($findings.ToArray());$facts.action_chains=@($chains.ToArray())
    $facts.receive_commit_links=@($receiveLinks.ToArray())
    $facts.receive_commit_links_omitted=$receiveLinksOmitted
    $facts.classifications_omitted=$script:multiplayerFindingOmissions
    if($findings.Count -eq 0){$facts.verdict='not_established'}else{$facts.verdict='classified_observations_not_global_health_pass'}
    $facts.action_coverage=@(foreach($row in (Get-RemoteArray $Object 'action_coverage' 256)) {
        [ordered]@{weapon_id=(Get-Number $row 'weapon_id' -Required -Integer);mode=(Get-Number $row 'mode' -Required -Integer)
            kind=(Get-Number $row 'kind' -Required -Integer);phase=(Get-Number $row 'phase' -Required -Integer);observations=(Get-Number $row 'observations' -Required -Integer)}
    })
    return $facts
}

$expectedRunId = $null
if ($PSCmdlet.ParameterSetName -eq 'Latest') {
    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) { throw 'local_app_data_unavailable' }
    $runsRoot = Join-Path $env:LOCALAPPDATA 'HLClientEngine/runs'
    if (-not (Test-Path -LiteralPath $runsRoot -PathType Container)) { throw 'no_movement_reports' }
    $candidates = @(Get-ChildItem -LiteralPath $runsRoot -Directory | Where-Object {
        $_.Name -cmatch '^[0-9a-f]{32}$' -and -not ($_.Attributes -band [IO.FileAttributes]::ReparsePoint)
    } | ForEach-Object {
        $candidate = Join-Path $_.FullName 'external-movement.json'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { Get-Item -LiteralPath $candidate }
    } | Sort-Object LastWriteTimeUtc -Descending)
    if ($candidates.Count -eq 0) { throw 'no_movement_reports' }
    $ReportPath = $candidates[0].FullName
    $expectedRunId = $candidates[0].Directory.Name
} elseif ($PSCmdlet.ParameterSetName -eq 'Run') {
    $run = Get-Item -LiteralPath $RunDirectory
    if (-not $run.PSIsContainer) { throw 'run_directory_required' }
    $expectedRunId = $run.Name
    $ReportPath = Join-Path $run.FullName 'external-movement.json'
}

# Bound the actual read, including a concurrently extended input. Do not display
# arbitrary JSON fields, resource names or native asset paths in either output.
$stream = [IO.File]::Open($ReportPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
try {
    if ($stream.Length -eq 0 -or $stream.Length -gt $maximumReportBytes) { throw 'report_size_out_of_bounds' }
    $data = [byte[]]::new([int]$stream.Length)
    $offset = 0
    while ($offset -lt $data.Length) {
        $read = $stream.Read($data, $offset, $data.Length - $offset)
        if ($read -eq 0) { throw 'report_changed_during_read' }
        $offset += $read
    }
    if ($stream.ReadByte() -ne -1) { throw 'report_changed_during_read' }
} finally { $stream.Dispose() }
try {
    $text = [Text.UTF8Encoding]::new($false, $true).GetString($data)
    $report = ConvertFrom-Json -InputObject $text -AsHashtable -Depth 64
} catch { throw 'report_invalid_utf8_or_json' }
$sourceSchema = Get-Field $report 'schema'
if ($sourceSchema -cnotin @('hlclient.external-movement.v1','hlclient.external-movement.v2')) { throw 'unsupported_report_schema_expected_external_movement_v1_or_v2' }
$legacyCapture = $sourceSchema -ceq 'hlclient.external-movement.v1'
$runId = Get-Field $report 'run_id'
if ($runId -isnot [string] -or $runId -cnotmatch '^[0-9a-f]{32}$') { throw 'invalid_run_identity' }
if ($null -ne $expectedRunId -and $expectedRunId -cne $runId) { throw 'run_identity_mismatch' }
$processId = Get-Number $report 'process_id' -Required -Integer
$processCreation = Get-Number $report 'process_creation' -Required -Integer
if ($processId -eq 0 -or $processId -gt [UInt32]::MaxValue -or $processCreation -eq 0) { throw 'invalid_process_identity' }
$result = [ordered]@{
    schema = 'hlclient.movement-session-analysis.v2'
    source_schema = $sourceSchema
    run_id = $runId
    process_id = $processId
    process_creation = $processCreation
    requested = Get-Boolean $report 'requested'
    enabled = Get-Boolean $report 'enabled'
    coverage = 'unavailable'
    causality = 'observations_not_root_cause_proof'
    limitations = @('histogram_quantiles_are_approximate_upper_bounds',
        'usable_state_age_is_not_ping_or_packet_loss',
        'background_dead_unknown_are_separate_from_controlled_alive',
        'cohort_frame_intervals_exclude_context_life_generation_transitions',
        'correction_ordinal_sample_can_cover_multiple_unobserved_corrections',
        'camera_jump_snapshot_is_not_exact_correction_event',
        'frame_stage_times_can_overlap_do_not_sum',
        'no_automated_prediction_health_or_cause_verdict')
}
$result.identity_evidence = Get-AdjacentRunIdentity $ReportPath $runId $processId $processCreation
$result.static_world = Get-StaticWorldReceipt $report
$result.capture = Get-PriorityCapture $report $result.enabled $legacyCapture
$result.critical_corrections = Get-CriticalCorrections (Get-Object $report 'critical_corrections') $result.enabled $legacyCapture
$result.remote_players = Get-RemotePlayers (Get-Object $report 'remote_players')
$result.multiplayer = Get-MultiplayerTimeline (Get-Object $report 'multiplayer_timeline')
$capabilities=Get-Field $report 'game_diagnostic_capabilities'
if($null -ne $capabilities) {
    $result.multiplayer.capability_matrix=@(foreach($capability in (Get-RemoteArray $report 'game_diagnostic_capabilities' 32)) {
        $weapon=Get-Number $capability 'weapon_id' -Required -Integer
        $command=Get-WeaponCommandToken $capability
        $modes=Get-RemoteArray $capability 'modes' 5
        if($modes.Count -ne 5){throw 'invalid_weapon_capability_modes'}
        for($mode=0;$mode -lt 5;$mode++) {
            $kind=Get-Number @{value=$modes[$mode]} 'value' -Required -Integer
            if($kind -gt 17){throw 'invalid_weapon_capability_kind'}
            $observations=0
            if($result.multiplayer.availability -eq 'ready') {
                foreach($row in $result.multiplayer.action_coverage){if($row.weapon_id -eq $weapon -and $row.mode -eq $mode){$observations+=$row.observations}}
            }
            [ordered]@{weapon_id=$weapon;command=$command;mode=$mode;kind=$kind;capability_declared=($kind -ne 0)
                held_primary=(Get-Boolean $capability 'held_primary');persistent_objects=(Get-Boolean $capability 'persistent_objects')
                runtime_observations=$observations;runtime_status=$(if($kind -eq 0){'unsupported'}elseif($observations){'runtime_observed'}else{'runtime_not_observed'})
                fixture_status='see_versioned_build_test_evidence_not_inferred_from_run'}
        }
    })
} else {$result.multiplayer.capability_matrix_availability='not_recorded_in_older_build'}
$result.encoded_local_frames_omitted = Get-Number $report 'encoded_local_frames_omitted' -Integer
$session = Get-Object $report 'session_summary'
$sessionEnabled = $false
$result.summary_availability = 'absent'
if ($null -ne $session) {
    if ((Get-Field $session 'schema') -cne 'hlclient.movement-session.v1') { throw 'unsupported_session_schema' }
    $sessionEnabled = Get-Boolean $session 'enabled'
    $null = Get-Boolean $session 'requested'
    $availability = Get-Field $session 'availability'
    if ($availability -notin @('ready', 'disabled', 'allocation_failed')) { throw 'invalid_session_availability' }
    $result.summary_availability = $availability
}
if (-not $sessionEnabled) {
    $recent = Get-Object $report 'recent_window'
    $statistics = Get-Object $report 'statistics'
    $result.coverage = if ($result.enabled) { 'recent_window_only' } else { 'unavailable' }
    $result.accepted_frames = Get-Number $statistics 'accepted_frames' -Integer
    $result.recent_begin_us = Get-Number $recent 'begin_us' -Integer
    $result.recent_end_us = Get-Number $recent 'end_us' -Integer
    $result.retained_frames = Get-Number $statistics 'retained_frames' -Integer
    $result.incidents = Get-Number $statistics 'incidents' -Integer
    $result.limitations += 'no_whole_session_summary_old_format_or_summary_allocation_failure'
} else {
    $coverage = Get-Object $session 'coverage' -Required
    $result.coverage = 'whole_session'
    $result.session = [ordered]@{}
    foreach ($name in @('first_us', 'last_us', 'elapsed_us', 'accepted_frames',
        'generation_boundaries', 'life_boundaries', 'unattributed_interval_us')) {
        $result.session[$name] = Get-Number $coverage $name -Integer
    }
    if ($null -ne $result.session.first_us -and $null -ne $result.session.last_us -and
        $result.session.last_us -lt $result.session.first_us) { throw 'reversed_session_interval' }
    $result.totals = Get-Summary (Get-Object $session 'totals' -Required)
    $cohorts = Get-Object $session 'cohorts' -Required
    $result.cohorts = [ordered]@{}
    foreach ($name in $cohortNames) {
        $result.cohorts[$name] = Get-Summary (Get-Object $cohorts $name -Required)
    }
    $result.frame_pacing = Get-FramePacing (Get-Object $session 'frame_pacing')
    $frameMetric = Get-Field $result.cohorts.controlled_alive.metrics 'frame_interval_ms'
    $result.fps_observations = [ordered]@{
        scope = 'controlled_alive_measured_intervals'
        mean_fps_equivalent = $(if ($null -ne $frameMetric -and $frameMetric.mean -gt 0) { 1000.0 / $frameMetric.mean } else { $null })
        slowest_frame_fps_equivalent = $(if ($null -ne $frameMetric -and $frameMetric.maximum -gt 0) { 1000.0 / $frameMetric.maximum } else { $null })
        policy = 'reciprocal_of_measured_interval_not_one_second_fps_or_target_drop_verdict'
    }
    $counterDeltas = Get-Object $session 'counter_deltas'
    $result.counter_deltas = [ordered]@{}
    foreach ($name in @('datagrams_received', 'rejected_sequences', 'choke_markers')) {
        $counter = Get-Object $counterDeltas $name
        if ($null -eq $counter) { continue }
        $values = [ordered]@{}
        foreach ($field in @('delta', 'observed_frames', 'missing_frames', 'baselines', 'resets')) {
            $values[$field] = Get-Number $counter $field -Required -Integer
        }
        $result.counter_deltas[$name] = $values
    }
    $observations = Get-Object $session 'observations'
    $result.observations = [ordered]@{}
    foreach ($name in @('initial_inactive_observations', 'initial_nonzero_correction_ordinals',
        'correction_ordinal_resets', 'correction_identity_missing_frames', 'committed_records_observed_frames',
        'replayed_commands_observed_frames', 'bytes_observed_frames', 'duplicate_or_unordered_frames',
        'manual_markers', 'manual_markers_rejected', 'frame_interval_context_unattributed')) {
        $result.observations[$name] = Get-Number $observations $name -Integer
    }
    $timeline = Get-Object $session 'timeline' -Required
    $bins = Get-Field $timeline 'bins'
    if ($null -eq $bins -or $bins -is [string] -or $bins -isnot [Collections.IList] -or $bins.Count -gt 512) {
        throw 'timeline_bins_out_of_bounds'
    }
    $windows = @(foreach ($bin in $bins) {
        $start = Get-Number $bin 'begin_us' -Required -Integer
        $end = Get-Number $bin 'end_us' -Required -Integer
        if ($end -lt $start) { throw 'reversed_timeline_interval' }
        [pscustomobject][ordered]@{
            begin_us = $start
            end_us = $end
            frames = Get-Number $bin 'frames' -Integer
            counts = Get-Counts (Get-Object $bin 'counts' -Required)
            controlled_alive_counts = Get-Counts (Get-Object $bin 'controlled_alive_counts' -Required)
            metrics = Get-Metrics (Get-Object $bin 'metrics' -Required) -Timeline
            producer_correction_maximum = Get-ProducerMaximum (Get-Object $bin 'producer_correction_maximum')
            generation_boundaries = Get-Number $bin 'generation_boundaries' -Integer
            manual_markers = Get-Number $bin 'manual_markers' -Integer
            datagrams_delta = Get-Number $bin 'datagrams_delta' -Integer
            rejected_sequences_delta = Get-Number $bin 'rejected_sequences_delta' -Integer
            choke_markers_delta = Get-Number $bin 'choke_markers_delta' -Integer
        }
    })
    $measuredFrameWindows = @($windows | Where-Object {
        $metric = Get-Field $_.metrics 'frame_interval_ms'
        $null -ne $metric -and $metric.observed -gt 0
    })
    $controlledFrameWindows = @($measuredFrameWindows | Where-Object { $_.controlled_alive_counts.frames -gt 0 })
    $frameWindows = if ($controlledFrameWindows.Count) { $controlledFrameWindows } else { $measuredFrameWindows }
    $result.timeline = [ordered]@{
        bucket_width_us = Get-Number $timeline 'bucket_width_us' -Required -Integer
        compactions = Get-Number $timeline 'compactions' -Required -Integer
        bins = $windows.Count
        metric_scope = 'all_cohorts_in_window_counts_separate_controlled_alive'
        frame_window_selection = $(if ($controlledFrameWindows.Count) { 'contains_controlled_alive_frames' } else { 'no_controlled_alive_frames_all_contexts' })
        frame_windows = @($frameWindows |
            Sort-Object { $_.metrics.frame_interval_ms.maximum } -Descending | Select-Object -First 3)
        prediction_windows = @($windows | Where-Object {
            $_.controlled_alive_counts.fallback_entries -gt 0 -or $_.controlled_alive_counts.correction_samples -gt 0
        } | Sort-Object @{ Expression = { $_.controlled_alive_counts.fallback_entries }; Descending = $true },
            @{ Expression = { Get-RankingMaximum $_.metrics 'raw_error_on_fallback_entry_units' }; Descending = $true },
            @{ Expression = { Get-RankingMaximum $_.metrics 'raw_correction_on_ordinal_sample_units' }; Descending = $true } |
            Select-Object -First 3)
    }
}

if($Open -and -not $PublishSummary){throw 'open_requires_explicit_publish_summary'}
if($PublishSummary) {
    $summaryRoot=(Get-Item -LiteralPath (Split-Path -Parent ([IO.Path]::GetFullPath($ReportPath)))).FullName
    if((Get-Item -LiteralPath $summaryRoot).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'summary_directory_reparse_rejected'}
    $summaryPath=[IO.Path]::GetFullPath((Join-Path $summaryRoot 'multiplayer-analysis.html'))
    if([IO.Path]::GetDirectoryName($summaryPath) -cne $summaryRoot){throw 'summary_path_escape_rejected'}
    if((Test-Path -LiteralPath $summaryPath) -and ((Get-Item -LiteralPath $summaryPath).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'summary_file_reparse_rejected'}
    $summaryJson=$result|ConvertTo-Json -Depth 32
    if([Text.Encoding]::UTF8.GetByteCount($summaryJson) -gt 4MB){throw 'analysis_summary_size_out_of_bounds'}
    $summaryHtml='<!doctype html><meta charset="utf-8"><title>HL Client multiplayer diagnostics</title><style>body{font:15px system-ui;background:#17191d;color:#eee;margin:2rem}pre{white-space:pre-wrap;overflow-wrap:anywhere}h1{color:#f5a936}</style><h1>HL Client multiplayer diagnostics</h1><p>Bounded client observations. Missing server execution, rewind and damage evidence stay unknown. No live parity verdict.</p><pre>'+[Net.WebUtility]::HtmlEncode($summaryJson)+'</pre>'
    $temporary=Join-Path $summaryRoot ('multiplayer-analysis-'+[Guid]::NewGuid().ToString('N')+'.tmp')
    try{[IO.File]::WriteAllText($temporary,$summaryHtml,[Text.UTF8Encoding]::new($false));[IO.File]::Move($temporary,$summaryPath,$true)}
    finally{if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary}}
    $result.summary_path=$summaryPath
    if($Open){Start-Process -FilePath $summaryPath}
}
if ($Json) { $result | ConvertTo-Json -Depth 32; return }
if($result.multiplayer.availability -eq 'ready') {
    Write-Output ('Комплексна діагностика: {0} унікальних збережених подій; пропущено деталей кодування {1}; висновок {2}.' -f $result.multiplayer.retained_unique_events,$result.multiplayer.encoded_events_omitted,$result.multiplayer.verdict)
    foreach($finding in $result.multiplayer.classifications){Write-Output ('  {0}: {1}; evidence={2}, confidence={3}.' -f $finding.classification,$finding.code,$finding.evidence_kind,$finding.confidence)}
}
Write-Output ('Запис {0}; процес {1}.' -f $result.run_id, $result.process_id)
Write-Output ('Ідентичність запису: {0}; поточні виконувані файли повторно не хешувалися.' -f $result.identity_evidence.availability)
Write-Output ('Профіль запису: {0}; формат {1}.' -f $(if ($result.enabled) { 'увімкнений' } else { 'вимкнений або недоступний' }), $result.source_schema)
if ($result.capture.availability -eq 'ready') {
    $manuals = @($result.capture.incidents | Where-Object { $_.manual_marker_us.Count -gt 0 })
    Write-Output ('Пріоритетний запис: вікон {0}, ручних вікон F8 {1}; витіснено старих записів рішень {2}.' -f
        $result.capture.incidents.Count, $manuals.Count, $result.capture.decision_evictions)
    foreach ($incident in $result.capture.incidents) {
        Write-Output ('Вікно #{0}: triggers={1}, priority={2}; до/після запит {3}/{4} с, фактично {5}/{6} с; зразків {7}, інтервал фону {8} мс; пропуск фону/точних/кодування {9}/{10}/{11}; coverage={12}.' -f
            $incident.ordinal, $incident.triggers, $incident.priority, (Format-Scaled $incident.requested_pre_us 1000000),
            (Format-Scaled $incident.requested_post_us 1000000), (Format-Scaled $incident.encoded_actual_pre_us 1000000),
            (Format-Scaled $incident.encoded_actual_post_us 1000000), $incident.retained_samples,
            (Format-Scaled $incident.sampling_interval_us 1000), $incident.background_samples_omitted,
            $incident.exact_samples_omitted, $incident.encoded_samples_omitted, $incident.coverage_reason)
        Write-Output ('  Завершення: {0}; початковий trigger {1} мкс; пропущено детальних F8-позначок: {2}.' -f
            $incident.end_status,(Format-Value $incident.initial_trigger_us),(Format-Value $incident.omitted_manual_markers))
        foreach ($marker in $incident.marker_witnesses) { Write-Output ('  F8 {0} мкс: точний кадр={1}.' -f $marker.marker_us,$marker.status) }
        if($incident.encoded_samples_omitted -gt 0) {
            Write-Output ('  До кодування збережено {0}/{1} с до/після; фактичне покриття вище — тільки доставлені кадри у файлі.' -f
                (Format-Scaled $incident.actual_pre_us 1000000),(Format-Scaled $incident.actual_post_us 1000000))
        }
    }
    foreach ($group in @($result.capture.decisions | Group-Object decision)) {
        Write-Output ('Рішення запису {0}: {1}.' -f $group.Name,$group.Count)
    }
    Write-Output 'Фонові зразки проріджені за часом; це не повний покадровий запис. Точні події корекцій зберігаються окремо.'
} elseif ($legacyCapture) {
    Write-Output 'Старий формат: розширення F8 до 4 секунд і точні producer witnesses ще не записувалися. Відсутні дані не відновлюються припущеннями.'
}
if ($result.critical_corrections.availability -eq 'ready') {
    $critical = $result.critical_corrections
    Write-Output ('Точні події корекцій: прийнято {0}, збережено {1}; max raw {2} од., доказ={3}; витіснення recent {4}, невалідних {5}.' -f
        $critical.accepted_events,$critical.retained_witnesses,(Format-Value $critical.maximum_raw_position_error),
        $critical.maximum_witness_status,$critical.evictions,$critical.invalid)
    Write-Output ('Доставка producer witnesses: {0}; спостережень {1}, помилок sink {2}, опубліковано {3}, скидань лічильника {4}, старих generations {5}.' -f
        $critical.delivery_status,(Format-Value $critical.delivery_observations),(Format-Value $critical.delivery_sink_failures),
        (Format-Value $critical.delivery_published_events),(Format-Value $critical.delivery_counter_resets),(Format-Value $critical.delivery_stale_generation_observations))
}
Write-RemotePlayers $result.remote_players
if ($null -ne $result.encoded_local_frames_omitted -and $result.encoded_local_frames_omitted -gt 0) {
    Write-Output ('Ліміт розміру звіту: не серіалізовано {0} детальних локальних кадрів; це не втрачені мережеві пакети. Агрегати сесії та FPS наведено окремо.' -f $result.encoded_local_frames_omitted)
}
if ($result.coverage -ne 'whole_session') {
    if (-not $result.enabled) { Write-Output 'Діагностика вимкнена або не змогла почати запис.' }
    else { Write-Output 'Повної статистики сесії немає: доступні лише останні кадри (до 4 секунд) і обмежені вікна подій.' }
    Write-Output ('Прийнято кадрів: {0}; збережено останніх: {1}; вікон подій: {2}.' -f
        (Format-Value $result.accepted_frames), (Format-Value $result.retained_frames), (Format-Value $result.incidents))
    Write-Output 'Цей запис не дозволяє оцінити всю гру. Потрібна нова сесія з увімкненою діагностикою.'
    return
}
Write-Output ('Уся сесія: {0} с; {1} кадрів. Інтервали поза обліком контексту: {2} мс.' -f
    (Format-Scaled $result.session.elapsed_us 1000000), $result.session.accepted_frames,
    (Format-Scaled $result.session.unattributed_interval_us 1000))
foreach ($name in $cohortNames) {
    Write-Output ('Контекст {0}: {1} кадрів.' -f $name, (Format-Value $result.cohorts[$name].counts.frames))
}
$playing = $result.cohorts.controlled_alive
Write-Output ('Кадри під керуванням живого гравця: ' + (Format-Metric $playing.metrics 'frame_interval_ms'))
Write-Output 'p50/p95/p99 — приблизні верхні межі гістограми; max — виміряний максимум.'
Write-Output ('Кадрів понад 20 / 33,3 / 50 мс: {0} / {1} / {2}.' -f
    (Format-Value $playing.counts.frame_over20_ms), (Format-Value $playing.counts.frame_over33_3_ms),
    (Format-Value $playing.counts.frame_over50_ms))
Write-Output ('Темп за виміряними інтервалами: приблизно {0} FPS; найповільніший окремий кадр: еквівалент {1} FPS.' -f
    (Format-Value $result.fps_observations.mean_fps_equivalent), (Format-Value $result.fps_observations.slowest_frame_fps_equivalent))
Write-Output 'Еквівалент FPS = 1000 / час кадру в мс. Одиничний довгий кадр не означає тривалої просадки або середнього FPS за секунду.'
if ($result.frame_pacing.availability -eq 'ready') {
    Write-Output ('FPS-епізоди з кадрами понад {0} мс: {1}; збережено найгірших {2}, пропущено деталей {3}; сума довгих інтервалів {4} мс.' -f
        (Format-Value $result.frame_pacing.threshold_ms), $result.frame_pacing.total_episodes,
        $result.frame_pacing.retained_episodes, $result.frame_pacing.omitted_episodes,
        (Format-Value $result.frame_pacing.slow_frame_interval_total_ms))
    foreach ($episode in @($result.frame_pacing.episodes | Sort-Object maximum_frame_ms -Descending | Select-Object -First 10)) {
        Write-Output ('FPS-епізод: локальний час {0}–{1} с; довгих кадрів {2}; span {3} мс; max {4} мс ({5} FPS-екв.); CPU {6}, Studio {7}, render {8}, present {9}, GPU {10} мс; end={11}.' -f
            (Format-Scaled $episode.begin_us 1000000), (Format-Scaled $episode.end_us 1000000), $episode.slow_frames,
            (Format-Value $episode.span_ms), (Format-Value $episode.maximum_frame_ms),
            (Format-Value $episode.slowest_frame_fps_equivalent), (Format-Value $episode.worst_frame_timings.cpu_update_ms),
            (Format-Value $episode.worst_frame_timings.studio_ms), (Format-Value $episode.worst_frame_timings.render_ms),
            (Format-Value $episode.worst_frame_timings.present_ms), (Format-Value $episode.worst_frame_timings.gpu_ms), $episode.end_status)
    }
} else { Write-Output 'Деталі FPS-епізодів у цій старішій збірці ще не записувалися; наявні загальні часи кадрів збережені.' }
Write-Output ('Prediction: активних кадрів {0}, неактивних {1}, невідомих {2}; входів у fallback {3}; зразків нової корекції {4}.' -f
    (Format-Value $playing.counts.prediction_active_frames), (Format-Value $playing.counts.prediction_inactive_frames),
    (Format-Value $playing.counts.prediction_unknown_frames), (Format-Value $playing.counts.fallback_entries),
    (Format-Value $playing.counts.correction_samples))
Write-Output ('Raw error при новому ordinal корекції: ' +
    (Format-Metric $playing.metrics 'raw_correction_on_ordinal_sample_units' 'од. світу'))
Write-Output ('Проміжних просувань ordinal без окремого зразка: {0}; спостережуваний час fallback: {1} мс.' -f
    (Format-Value $playing.counts.correction_ordinal_advances_unmeasured),
    (Format-Scaled $playing.counts.fallback_observed_interval_us 1000))
Write-Output ('Вік придатного локального стану: ' + (Format-Metric $playing.metrics 'state_age_ms'))
Write-Output 'Вік стану не є ping або відсотком втрат пакетів. Зразок ordinal може пропустити проміжні корекції.'
Write-Output ('Ручних позначок F8: {0}. Запис усієї сесії працює також без них.' -f
    (Format-Value $result.observations.manual_markers))
foreach ($name in @('datagrams_received', 'rejected_sequences', 'choke_markers')) {
    $counter = Get-Field $result.counter_deltas $name
    if ($null -ne $counter) {
        Write-Output ('Лічильник {0}: приріст {1}, початкових баз {2}, скидань {3}.' -f
            $name, $counter.delta, $counter.baselines, $counter.resets)
    }
}
Write-Output 'Відхилені sequence не вимірюють втрати пакетів; choke не доводить перевантаження сервера.'
foreach ($name in @('cpu_update_ms', 'prediction_ms', 'collision_ms', 'studio_ms', 'asset_ms', 'render_ms', 'present_ms', 'gpu_ms')) {
    Write-Output ('Етап {0}: {1}' -f $name, (Format-Metric $playing.metrics $name))
}
Write-Output ('Шкала часу: {0} вікон, ширина {1} с, укрупнень {2}. Вибрані вікна містять усі контексти.' -f
    $result.timeline.bins, (Format-Value ($result.timeline.bucket_width_us / 1000000)), $result.timeline.compactions)
foreach ($window in $result.timeline.frame_windows) {
    Write-Output ('Повільне вікно [{0}; {1}] с: max кадру {2} мс, керованих кадрів {3}.' -f
        (Format-Value (($window.begin_us - $result.session.first_us) / 1000000)),
        (Format-Value (($window.end_us - $result.session.first_us) / 1000000)),
        (Format-Value $window.metrics.frame_interval_ms.maximum),
        (Format-Value $window.controlled_alive_counts.frames))
}
foreach ($window in $result.timeline.prediction_windows) {
    Write-Output ('Вікно prediction [{0}; {1}] с: fallback {2}, зразків корекції {3} (лише керований живий гравець).' -f
        (Format-Value (($window.begin_us - $result.session.first_us) / 1000000)),
        (Format-Value (($window.end_us - $result.session.first_us) / 1000000)),
        (Format-Value $window.controlled_alive_counts.fallback_entries),
        (Format-Value $window.controlled_alive_counts.correction_samples))
}
Write-Output 'Це спостереження, а не автоматичний висновок про причину ривків. Часи вкладених етапів не слід додавати.'
