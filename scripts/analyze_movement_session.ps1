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
    [switch]$Json
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
    }
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
    'lifetime_boundary', 'new_boundary', 'decision_change', 'displacement_threshold')
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
        'selected_main', 'selected_gait', 'selected_weapon', 'raw_main', 'raw_gait', 'raw_weapon', 'raw_effects')) {
        $sample[$name] = Get-Number $Object $name -Integer
        if ($name -notlike '*source_ordinal' -and $null -ne $sample[$name] -and
            $sample[$name] -gt [UInt32]::MaxValue) { throw 'invalid_remote_actor_field' }
    }
    foreach ($name in @('previous_server_s', 'current_server_s', 'latest_server_s', 'sample_server_s')) {
        $sample[$name] = Get-Number $Object $name
        if ($null -ne $sample[$name] -and $sample[$name] -gt 1000000000) { throw 'remote_server_time_out_of_bounds' }
    }
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
if ((Get-Field $report 'schema') -cne 'hlclient.external-movement.v1') { throw 'unsupported_report_schema' }
$runId = Get-Field $report 'run_id'
if ($runId -isnot [string] -or $runId -cnotmatch '^[0-9a-f]{32}$') { throw 'invalid_run_identity' }
if ($null -ne $expectedRunId -and $expectedRunId -cne $runId) { throw 'run_identity_mismatch' }
$processId = Get-Number $report 'process_id' -Required -Integer
$processCreation = Get-Number $report 'process_creation' -Required -Integer
if ($processId -eq 0 -or $processId -gt [UInt32]::MaxValue -or $processCreation -eq 0) { throw 'invalid_process_identity' }
$result = [ordered]@{
    schema = 'hlclient.movement-session-analysis.v1'
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
$result.remote_players = Get-RemotePlayers (Get-Object $report 'remote_players')
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

if ($Json) { $result | ConvertTo-Json -Depth 32; return }
Write-Output ('Запис {0}; процес {1}.' -f $result.run_id, $result.process_id)
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
