#requires -Version 7.0
# Project-owned JSON fixtures only: no game, server, installed assets or device.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$analyzer = Join-Path $PSScriptRoot 'analyze_movement_session.ps1'
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('hlclient-movement-analysis-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $fixtureRoot
$fixtureRoot = (Get-Item -LiteralPath $fixtureRoot).FullName
$originalLocalAppData = $env:LOCALAPPDATA
$checks = 0
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}
function Assert-Rejected([scriptblock]$Action, [string]$Message) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    Assert $rejected $Message
}
function New-Metric([int]$Observed = 10, [double]$Maximum = 16) {
    return @{
        observed = $Observed; not_available = 0
        mean = $(if ($Observed) { 10.0 } else { $null })
        maximum = $(if ($Observed) { $Maximum } else { $null })
        p50_upper_bound = $(if ($Observed) { 12.0 } else { $null })
        p95_upper_bound = $(if ($Observed) { 20.0 } else { $null })
        p99_upper_bound = $(if ($Observed) { 20.0 } else { $null })
    }
}
function New-Summary([int]$Frames, [int]$Fallback = 0) {
    return @{
        counts = @{
            frames = $Frames; prediction_active_frames = $Frames; prediction_inactive_frames = 0
            prediction_unknown_frames = 0; fallback_entries = $Fallback; correction_samples = 2
            correction_ordinal_advances_unmeasured = 0; frame_over20_ms = 1; frame_over33_3_ms = 0
            frame_over50_ms = 0; startsolid_frames = 0; allsolid_frames = 0; committed_records = 2
            replayed_commands = 4; bytes = 128; fallback_observed_interval_us = 0
        }
        metrics = @{
            frame_interval_ms = New-Metric $Frames
            state_age_ms = New-Metric $Frames 45
            raw_correction_on_ordinal_sample_units = New-Metric $Frames 3
            gpu_ms = New-Metric 0
        }
    }
}
function New-Report {
    $active = New-Summary 10 1
    $total = New-Summary 110 8
    return @{
        schema = 'hlclient.external-movement.v1'; run_id = '0123456789abcdef0123456789abcdef'
        process_id = 42; process_creation = 12345678901234567; requested = $true; enabled = $true
        statistics = @{ accepted_frames = 110; retained_frames = 5; incidents = 1 }
        recent_window = @{ begin_us = 100000000; end_us = 104000000; frames = @() }
        session_summary = @{
            schema = 'hlclient.movement-session.v1'; requested = $true; enabled = $true; availability = 'ready'
            coverage = @{
                first_us = 1000000; last_us = 111000000; elapsed_us = 110000000; accepted_frames = 110
                generation_boundaries = 1; life_boundaries = 2; unattributed_interval_us = 400000
            }
            totals = $total
            cohorts = @{
                controlled_alive = $active; background = New-Summary 90 6
                inactive_or_dead = New-Summary 8 1; unknown = New-Summary 2
            }
            timeline = @{
                bucket_width_us = 1000000; compactions = 1; maximum_bins = 512
                bins = @(@{
                    begin_us = 1000000; end_us = 2000000; frames = 110
                    counts = $total.counts; controlled_alive_counts = $active.counts
                    metrics = @{ frame_interval_ms = @{ observed = 110; mean = 10; maximum = 75 } }
                    generation_boundaries = 0
                })
            }
        }
        # These unrelated strings must never leak through the whitelist.
        ignored_native_asset = 'C:/PrivateAssetRoot/secret-resource.wav'
        ignored_auth = 'fixture-sensitive-value'
    }
}
function New-RemoteSample([UInt64]$Stamp = 1000000) {
    return @{
        monotonic_us = $Stamp; frame_number = 101; network_generation = 1; map_generation = 2
        present = $true; entity_index = 3; model_index = 42; lifetime = 7
        previous_raw_origin = @(-10.5, 20, -32); current_raw_origin = @(-1.5, 20, -32)
        latest_raw_origin = @(0, 21, -32); sampled_origin = @(-6, 20, -32); rendered_origin = @(-6, 20, -32)
        previous_source_ordinal = 100; current_source_ordinal = 101; latest_source_ordinal = 102
        previous_server_s = 10.1; current_server_s = 10.2; latest_server_s = 10.3; sample_server_s = 10.15
        selected_main = 4; selected_gait = 3; selected_weapon = 21
        raw_main = 5; raw_gait = 2; raw_weapon = 22; raw_effects = 0
        pose_submitted = $true; visible = $null; decision = 'interpolated'; alpha = 0.5
        floor_reasons = 1; new_boundary_reasons = 0; hold_reasons = 0; clock_reasons = 0
        floor_record_ordinal = 90; resolved_floor_record_ordinal = 90; position_previous_record_ordinal = 100; position_current_record_ordinal = 101
        ignored_native_asset = 'C:/PrivateAssetRoot/secret-resource.mdl'
    }
}
function New-RemotePlayers {
    $before = New-RemoteSample
    $current = New-RemoteSample 1010000
    $current.frame_number = 102; $current.sampled_origin = @(3, 20, -32); $current.rendered_origin = @(3.1, 20, -32)
    $current.decision = 'held_floor'; $current.new_boundary_reasons = 1024; $current.floor_reasons = 1024
    $current.hold_reasons = 64; $current.clock_reasons = 1024; $current.selected_weapon = 22
    return @{
        schema = 'hlclient.remote-players.v1'; requested = $true; enabled = $true; availability = 'ready'
        causality = 'observations_not_root_cause_proof'
        limits = @{
            max_actors = 32; recent_samples = 640; event_records = 128; manual_captures = 2; manual_samples = 160; manual_events = 64
            retention_us = 4000000; sample_interval_us = 200000; event_cooldown_us = 250000; manual_pre_us = 1000000
            displacement_trigger_units = 8; maximum_capacity_bytes = 1MB; maximum_encoded_bytes = 2MB
        }
        statistics = @{
            accepted_frames = 2; duplicate_frames = 0; rejected_frames = 0; invalid_observations = 0
            invalid_values = 0; duplicate_actors = 0; absences = 0; reentries = 0; generation_boundaries = 0
            lifetime_boundaries = 0; event_capacity_drops = 4; recent_capacity_drops = 0; retention_evictions = 10
            event_cooldown_suppressions = 2; manual_capture_drops = 0
            retained_samples = 2; retained_events = 2; manual_captures = 1; allocated_capacity_bytes = 800000
        }
        actors = @(@{
            entity_index = 3; samples = 2; comparisons = 1; raw_comparisons = 1; sampled_comparisons = 1; rendered_comparisons = 1
            absences = 0; reentries = 0; lifetime_boundaries = 0
            generation_boundaries = 0; displacements = 1; new_boundaries = 1; decision_changes = 1
            event_cooldown_suppressions = 2; raw_distance_sum = 0; sampled_distance_sum = 9; rendered_distance_sum = 9.1
            max_raw_units = 0; max_sampled_units = 9; max_rendered_units = 9.1; max_sampled_speed = 900; max_rendered_speed = 910
            decision_counts = @(1, 0, 0, 0, 0, 0, 1, 0); boundary_counts = @(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0)
            hold_counts = @(0, 0, 0, 0, 0, 0, 1)
        })
        recent_samples = @($before, $current)
        events = @(
            @{ ordinal = 1; trigger_mask = 1; before = $null; current = $before
                delta = @{ elapsed_us = $null; raw_units = $null; sampled_units = $null; rendered_units = $null; sampled_speed = $null; rendered_speed = $null } }
            @{ ordinal = 2; trigger_mask = 224; before = $before; current = $current
                delta = @{ elapsed_us = 10000; raw_units = 0; sampled_units = 9; rendered_units = 9.1; sampled_speed = 900; rendered_speed = 910 } }
        )
        manual_captures = @(@{ marker_us = 1020000; truncated = $false; event_truncated = $false; samples = @($before, $current)
            events = @(@{ ordinal = 2; trigger_mask = 224; before = $before; current = $current
                delta = @{ elapsed_us = 10000; raw_units = 0; sampled_units = 9; rendered_units = 9.1; sampled_speed = 900; rendered_speed = 910 } }) })
    }
}
function Write-Fixture([string]$Name, $Value) {
    $path = Join-Path $fixtureRoot $Name
    [IO.File]::WriteAllText($path, ($Value | ConvertTo-Json -Depth 32), [Text.UTF8Encoding]::new($false))
    return $path
}
try {
    $report = New-Report
    $path = Write-Fixture 'whole.json' $report
    $beforeHash = (Get-FileHash -LiteralPath $path).Hash
    $jsonText = & $analyzer -ReportPath $path -Json
    $facts = $jsonText | ConvertFrom-Json -AsHashtable
    Assert ($facts.coverage -eq 'whole_session') 'Whole session coverage missing.'
    Assert ($facts.session.elapsed_us -eq 110000000 -and $facts.session.accepted_frames -eq 110) 'Coverage changed.'
    Assert ($facts.cohorts.controlled_alive.counts.frames -eq 10 -and $facts.cohorts.background.counts.frames -eq 90) 'Cohorts mixed.'
    Assert ($facts.cohorts.controlled_alive.counts.fallback_entries -eq 1 -and $facts.totals.counts.fallback_entries -eq 8) 'Fallback context mixed.'
    Assert ($facts.cohorts.controlled_alive.metrics.gpu_ms.observed -eq 0 -and $null -eq $facts.cohorts.controlled_alive.metrics.gpu_ms.maximum) 'Missing GPU fabricated as zero.'
    Assert ($facts.timeline.frame_windows[0].metrics.frame_interval_ms.maximum -eq 75) 'Timeline maximum absent.'
    Assert ($facts.timeline.prediction_windows.Count -eq 1) 'Prediction window absent.'
    Assert ($facts.timeline.metric_scope -eq 'all_cohorts_in_window_counts_separate_controlled_alive') 'Mixed timeline scope unlabelled.'
    Assert ($jsonText -notmatch 'PrivateAssetRoot|secret-resource|fixture-sensitive') 'Unapproved strings leaked.'
    Assert ((Get-FileHash -LiteralPath $path).Hash -eq $beforeHash) 'Analyzer wrote its input.'
    $human = (& $analyzer -ReportPath $path) -join "`n"
    Assert ($human -match 'верхні межі' -and $human -match 'не є ping' -and $human -match 'не автоматичний висновок') 'Limitations missing from human report.'
    Assert ($human -match 'max 3 од. світу') 'Correction magnitude missing or wrong units.'
    Assert ($facts.frame_pacing.availability -eq 'not_recorded_in_older_build') 'Old build FPS details fabricated.'
    Assert ($facts.fps_observations.mean_fps_equivalent -eq 100) 'Mean FPS reciprocal wrong.'
    Assert ($facts.remote_players.availability -eq 'not_recorded' -and -not $facts.remote_players.Contains('actors')) 'Old build remote observations fabricated as zero.'
    Assert ($null -eq $facts.encoded_local_frames_omitted) 'Old serialization coverage fabricated as measured zero.'
    Assert ($human -match 'не нуль подій' -and $human -match 'не записувалася') 'Missing remote diagnostic disclaimer absent.'
    $remote = New-Report
    $remote.remote_players = New-RemotePlayers
    $remotePath = Write-Fixture 'remote-players.json' $remote
    $remoteHash = (Get-FileHash -LiteralPath $remotePath).Hash
    $remoteText = & $analyzer -ReportPath $remotePath -Json
    $remoteFacts = $remoteText | ConvertFrom-Json -AsHashtable
    Assert ($remoteFacts.remote_players.availability -eq 'ready' -and $remoteFacts.remote_players.actors.Count -eq 1) 'Remote actor coverage absent.'
    Assert ($remoteFacts.remote_players.actors[0].held_observations -eq 1 -and $remoteFacts.remote_players.actors[0].displacements -eq 1) 'Held and displacement observations conflated.'
    Assert ($remoteFacts.remote_players.actors[0].boundary_counts.same_time_origin_correction -eq 1) 'Per-actor boundary categories lost.'
    Assert ($remoteFacts.remote_players.events[1].current.sampled_origin[0] -eq 3 -and $remoteFacts.remote_players.events[1].current.rendered_origin[0] -eq 3.1) 'Sampled and rendered positions conflated.'
    Assert ($remoteFacts.remote_players.events[1].current.previous_raw_origin[0] -eq -10.5) 'Negative world coordinates rejected or altered.'
    Assert ($remoteFacts.remote_players.events[1].delta.elapsed_us -eq 10000 -and $remoteFacts.remote_players.limits.sample_interval_us -eq 200000) 'Observed delta confused with retained cadence.'
    Assert ($null -eq $remoteFacts.remote_players.events[0].delta.rendered_units -and $null -eq $remoteFacts.remote_players.events[0].before) 'Initial event distance fabricated.'
    Assert ($null -eq $remoteFacts.remote_players.events[0].current.visible) 'Unknown visibility fabricated.'
    Assert ($remoteFacts.remote_players.events[1].current.raw_weapon -eq 22 -and $remoteFacts.remote_players.events[0].current.selected_weapon -eq 21) 'Raw and selected weapon identities lost.'
    Assert ($remoteFacts.remote_players.events[1].current.new_boundary_reasons_labels -contains 'same_time_origin_correction' -and $remoteFacts.remote_players.events[1].current.clock_reasons_labels -contains 'newest_underflow') 'Reason masks incorrectly decoded.'
    Assert ($remoteFacts.remote_players.statistics.event_capacity_drops -eq 4 -and $remoteFacts.remote_players.statistics.event_cooldown_suppressions -eq 2) 'Remote event omissions hidden.'
    Assert ($remoteFacts.remote_players.manual_captures[0].samples.Count -eq 2) 'F8 preceding actor samples missing.'
    Assert ($remoteFacts.remote_players.manual_captures[0].events[0].delta.elapsed_us -eq 10000) 'F8 exact event pair lost.'
    Assert ($remoteText -notmatch 'PrivateAssetRoot|secret-resource|fixture-sensitive') 'Remote diagnostic leaked unapproved fields.'
    Assert ((Get-FileHash -LiteralPath $remotePath).Hash -eq $remoteHash) 'Remote analyzer wrote input.'
    Assert ($remoteFacts.cohorts.controlled_alive.counts.frames -eq 10 -and $remoteFacts.fps_observations.mean_fps_equivalent -eq 100) 'Remote diagnostics changed local prediction or FPS summary.'
    $remoteHuman = (& $analyzer -ReportPath $remotePath) -join "`n"
    Assert ($remoteHuman -match 'Актор entity=3' -and $remoteHuman -match 'Δt=10 мс' -and $remoteHuman -match 'sampled=\[3, 20, -32\]') 'Human per-actor evidence absent.'
    Assert ($remoteHuman -match 'не доводить плавності кожного' -and $remoteHuman -match 'не між прорідженими') 'Remote limitations absent.'
    $bounded = New-Report; $bounded.remote_players = New-RemotePlayers; $bounded.encoded_local_frames_omitted = 100
    $boundedPath = Write-Fixture 'remote-local-encoding-limit.json' $bounded
    $boundedFacts = & $analyzer -ReportPath $boundedPath -Json | ConvertFrom-Json -AsHashtable
    Assert ($boundedFacts.encoded_local_frames_omitted -eq 100 -and $boundedFacts.session.accepted_frames -eq 110) 'Serialization omission confused with collection or session coverage.'
    $boundedHuman = (& $analyzer -ReportPath $boundedPath) -join "`n"
    Assert ($boundedHuman -match 'не серіалізовано 100' -and $boundedHuman -match 'не втрачені мережеві') 'Serialization omission not explicit.'
    foreach ($status in @('disabled', 'allocation_failed', 'budget_exceeded', 'encode_failed', 'awaiting_presentation')) {
        $unavailable = New-Report
        $unavailable.remote_players = @{
            schema = 'hlclient.remote-players.v1'; requested = ($status -ne 'disabled'); enabled = ($status -in @('budget_exceeded', 'encode_failed', 'awaiting_presentation'))
            availability = $status; causality = 'observations_not_root_cause_proof'
        }
        $unavailablePath = Write-Fixture ('remote-'+$status+'.json') $unavailable
        $unavailableFacts = & $analyzer -ReportPath $unavailablePath -Json | ConvertFrom-Json -AsHashtable
        Assert ($unavailableFacts.remote_players.availability -eq $status -and -not $unavailableFacts.remote_players.Contains('actors')) ('Unavailable remote '+$status+' treated as observed.')
        $unavailableHuman = (& $analyzer -ReportPath $unavailablePath) -join "`n"
        Assert ($unavailableHuman -match 'не нуль подій') ('Unavailable remote '+$status+' lacks caveat.')
    }
    $absentActor = New-Report; $absentActor.remote_players = New-RemotePlayers
    $absentActor.remote_players.events[1].current.present = $false
    $absentActor.remote_players.events[1].current.decision = 'held_only'
    foreach ($key in @($absentActor.remote_players.events[1].delta.Keys)) { $absentActor.remote_players.events[1].delta[$key] = $null }
    foreach ($key in @($absentActor.remote_players.manual_captures[0].events[0].delta.Keys)) { $absentActor.remote_players.manual_captures[0].events[0].delta[$key] = $null }
    $absentPath = Write-Fixture 'remote-absent-actor.json' $absentActor
    $absentFacts = & $analyzer -ReportPath $absentPath -Json | ConvertFrom-Json -AsHashtable
    Assert (-not $absentFacts.remote_players.events[1].current.decision_observed) 'Absent actor default pose decision presented as observed.'
    $noRender = New-Report; $noRender.remote_players = New-RemotePlayers
    $noRender.remote_players.actors[0].rendered_comparisons = 0
    foreach ($key in @('rendered_distance_sum', 'max_rendered_units', 'max_rendered_speed')) { $noRender.remote_players.actors[0][$key] = $null }
    $noRenderPath = Write-Fixture 'remote-no-render-observations.json' $noRender
    $noRenderFacts = & $analyzer -ReportPath $noRenderPath -Json | ConvertFrom-Json -AsHashtable
    Assert ($null -eq $noRenderFacts.remote_players.actors[0].max_rendered_units -and $noRenderFacts.remote_players.actors[0].max_raw_units -eq 0) 'Unknown rendered delta conflated with observed raw zero.'
    foreach ($variant in @('schema', 'availability', 'enabled', 'limit', 'actors', 'duplicate_actor', 'recent', 'events',
        'manual', 'manual_samples', 'manual_time', 'position_length', 'position_string', 'position_null_component',
        'position_nonfinite', 'counter_nonfinite', 'boolean', 'alpha', 'decision', 'mask', 'counter_vector',
        'duplicate_event', 'event_elapsed', 'event_identity', 'event_trigger', 'event_negative_distance',
        'position_bound', 'time_bound', 'negative_time', 'retained_count', 'allocation', 'decision_count', 'missing_elapsed', 'life_delta',
        'zero_observed_distance', 'unknown_observed_distance', 'too_many_comparisons', 'manual_events', 'manual_event_time', 'manual_event_truncated')) {
        $badRemote = New-Report; $badRemote.remote_players = New-RemotePlayers
        $section = $badRemote.remote_players
        switch ($variant) {
            schema { $section.schema = 'untrusted.v2' }
            availability { $section.availability = 'untrusted-status' }
            enabled { $section.enabled = $false }
            limit { $section.limits.recent_samples = 641 }
            actors { $section.actors = @($section.actors[0]) * 33 }
            duplicate_actor { $section.actors += $section.actors[0] }
            recent { $section.recent_samples = @($section.recent_samples[0]) * 641 }
            events { $section.events = @($section.events[0]) * 129 }
            manual { $section.manual_captures = @($section.manual_captures[0]) * 3 }
            manual_samples { $section.manual_captures[0].samples = @($section.recent_samples[0]) * 161 }
            manual_time { $section.manual_captures[0].marker_us = 1 }
            position_length { $section.events[1].current.sampled_origin = @(1, 2) }
            position_string { $section.events[1].current.sampled_origin[0] = 'fixture-sensitive-value' }
            position_null_component { $section.events[1].current.sampled_origin[0] = $null }
            position_nonfinite { $section.events[1].current.sampled_origin[0] = [double]::NaN }
            counter_nonfinite { $section.statistics.accepted_frames = [double]::PositiveInfinity }
            boolean { $section.events[1].current.visible = 'yes' }
            alpha { $section.events[1].current.alpha = 1.01 }
            decision { $section.events[1].current.decision = 'fixture-sensitive-value' }
            mask { $section.events[1].current.floor_reasons = 8192 }
            counter_vector { $section.actors[0].hold_counts = @(0, 0) }
            duplicate_event { $section.events[1].ordinal = 1 }
            event_elapsed { $section.events[1].delta.elapsed_us = 200000 }
            event_identity { $section.events[1].current.entity_index = 4 }
            event_trigger { $section.events[1].trigger_mask = 256 }
            event_negative_distance { $section.events[1].delta.rendered_units = -1 }
            position_bound { $section.events[1].current.sampled_origin[0] = 1000000001 }
            time_bound { $section.events[1].current.sample_server_s = 1000000001 }
            negative_time { $section.events[1].current.sample_server_s = -1 }
            retained_count { $section.statistics.retained_events = 3 }
            allocation { $section.statistics.allocated_capacity_bytes = 1MB + 1 }
            decision_count { $section.actors[0].decision_counts[0] = 2 }
            missing_elapsed { $section.events[1].delta.elapsed_us = $null }
            life_delta { $section.events[1].current.lifetime = 8 }
            zero_observed_distance { $section.actors[0].rendered_comparisons = 0 }
            unknown_observed_distance { $section.actors[0].max_raw_units = $null }
            too_many_comparisons { $section.actors[0].raw_comparisons = 2 }
            manual_events { $section.manual_captures[0].events = @($section.events[1]) * 65 }
            manual_event_time { $section.manual_captures[0].events[0].current.monotonic_us = 2000001 }
            manual_event_truncated { $section.manual_captures[0].event_truncated = 'false' }
        }
        $badRemotePath = Write-Fixture ('remote-invalid-'+$variant+'.json') $badRemote
        Assert-Rejected { & $analyzer -ReportPath $badRemotePath -Json } ('Invalid remote '+$variant+' accepted.')
    }
    $fps = New-Report
    $fps.session_summary.frame_pacing = @{
        schema = 'hlclient.frame-pacing.v1'; scope = 'controlled_alive_contiguous_endpoint_context'
        definition = 'absolute_long_frames_not_drop_from_target_fps'
        selection = 'worst_peak_frame_then_duration_then_first_ordinal'
        threshold_ms = 20; maximum_episodes = 64; total_episodes = 1; retained_episodes = 1; omitted_episodes = 0
        slow_frames = 2; slow_frame_interval_total_ms = 65
        episodes = @(@{
            ordinal = 1; begin_us = 3000000; end_us = 3065000; first_frame = 51; last_frame = 52; worst_frame = 52
            network_generation = 1; map_generation = 2; slow_frames = 2; maximum_frame_ms = 40; frame_interval_total_ms = 65
            recovery_observed = $true; end_status = 'recovered'; observed_players = 16; entities = 240; prediction_active = $true
            slowest_frame_fps_equivalent = 99999 # Analyzer derives this from measured duration, not this field.
            ignored_auth = 'fixture-sensitive-value'
            worst_frame_timings = @{ frame_interval_ms = 40; cpu_update_ms = 27; studio_ms = 12; render_ms = 4; gpu_ms = $null }
        })
    }
    $fpsPath = Write-Fixture 'fps.json' $fps
    $fpsHash = (Get-FileHash -LiteralPath $fpsPath).Hash
    $fpsText = & $analyzer -ReportPath $fpsPath -Json
    $fpsFacts = $fpsText | ConvertFrom-Json -AsHashtable
    Assert ($fpsFacts.frame_pacing.availability -eq 'ready' -and $fpsFacts.frame_pacing.total_episodes -eq 1) 'FPS episodes absent.'
    Assert ($fpsFacts.frame_pacing.episodes[0].slowest_frame_fps_equivalent -eq 25) 'Single-frame FPS wrong or fabricated field trusted.'
    Assert ([Math]::Abs($fpsFacts.frame_pacing.episodes[0].slow_frames_mean_fps_equivalent - 2000.0/65) -lt 0.00001) 'Slow frame cadence incorrectly averages reciprocal FPS.'
    Assert ($null -eq $fpsFacts.frame_pacing.episodes[0].worst_frame_timings.gpu_ms) 'Unavailable GPU guessed.'
    Assert ($fpsText -notmatch 'PrivateAssetRoot|secret-resource|fixture-sensitive|99999') 'FPS report leaked an unapproved field.'
    Assert ((Get-FileHash -LiteralPath $fpsPath).Hash -eq $fpsHash) 'FPS analysis mutated its input.'
    $fpsHuman = (& $analyzer -ReportPath $fpsPath) -join "`n"
    Assert ($fpsHuman -match 'FPS-епізод' -and $fpsHuman -match 'еквівалент' -and $fpsHuman -match 'CPU 27') 'Human FPS evidence absent.'
    foreach ($variant in @('bounds','count','reversed','zero','duplicate','end','sensitive')) {
        $badFps = $fps | ConvertTo-Json -Depth 32 | ConvertFrom-Json -AsHashtable
        switch ($variant) {
            bounds { $badFps.session_summary.frame_pacing.maximum_episodes = 65 }
            count { $badFps.session_summary.frame_pacing.retained_episodes = 2 }
            reversed { $badFps.session_summary.frame_pacing.episodes[0].end_us = 1 }
            zero { $badFps.session_summary.frame_pacing.episodes[0].maximum_frame_ms = 0 }
            duplicate {
                $badFps.session_summary.frame_pacing.episodes += $badFps.session_summary.frame_pacing.episodes[0]
                $badFps.session_summary.frame_pacing.retained_episodes = 2
                $badFps.session_summary.frame_pacing.total_episodes = 2
            }
            end { $badFps.session_summary.frame_pacing.episodes[0].recovery_observed = $false }
            sensitive { $badFps.session_summary.frame_pacing.episodes[0].worst_frame_timings.gpu_ms = 'fixture-sensitive-value' }
        }
        $badFpsPath = Write-Fixture ('fps-invalid-'+$variant+'.json') $badFps
        Assert-Rejected { & $analyzer -ReportPath $badFpsPath -Json } ('Invalid FPS '+$variant+' accepted.')
    }
    $mixed = New-Report
    $mixed.session_summary.timeline.bins += @{
        begin_us = 2000000; end_us = 3000000; frames = 90
        counts = (New-Summary 90).counts; controlled_alive_counts = (New-Summary 0).counts
        metrics = @{ frame_interval_ms = @{ observed = 90; mean = 300; maximum = 1000 } }
        generation_boundaries = 0
    }
    $mixedPath = Write-Fixture 'background-pause.json' $mixed
    $mixedFacts = & $analyzer -ReportPath $mixedPath -Json | ConvertFrom-Json -AsHashtable
    Assert ($mixedFacts.timeline.frame_windows.Count -eq 1 -and $mixedFacts.timeline.frame_windows[0].metrics.frame_interval_ms.maximum -eq 75) 'Background-only pause displaced controlled frame window.'
    $ranked = New-Report
    $ranked.session_summary.timeline.bins[0].controlled_alive_counts.fallback_entries = 0
    $ranked.session_summary.timeline.bins[0].metrics.raw_correction_on_ordinal_sample_units = @{ observed = 2; mean = 5; maximum = 9 }
    $ordinaryCounts = (New-Summary 100).counts
    $ordinaryCounts.correction_samples = 100
    $ranked.session_summary.timeline.bins += @{
        begin_us = 2000000; end_us = 3000000; frames = 100
        counts = $ordinaryCounts; controlled_alive_counts = $ordinaryCounts
        metrics = @{ raw_correction_on_ordinal_sample_units = @{ observed = 100; mean = 0.01; maximum = 0.1 } }
        generation_boundaries = 0
    }
    $rankedPath = Write-Fixture 'correction-ranking.json' $ranked
    $rankedFacts = & $analyzer -ReportPath $rankedPath -Json | ConvertFrom-Json -AsHashtable
    Assert ($rankedFacts.timeline.prediction_windows[0].begin_us -eq 1000000) 'Many tiny ordinary corrections outranked larger observed correction.'

    $old = New-Report
    $old.Remove('session_summary')
    $oldPath = Write-Fixture 'old.json' $old
    $legacy = & $analyzer -ReportPath $oldPath -Json | ConvertFrom-Json -AsHashtable
    Assert ($legacy.coverage -eq 'recent_window_only' -and $legacy.accepted_frames -eq 110 -and $legacy.retained_frames -eq 5) 'Legacy wrongly presented as full coverage.'
    $old.session_summary = $null
    $nullPath = Write-Fixture 'null-summary.json' $old
    $nullFacts = & $analyzer -ReportPath $nullPath -Json | ConvertFrom-Json -AsHashtable
    Assert ($nullFacts.coverage -eq 'recent_window_only') 'Unavailable summary fabricated.'
    $old.enabled = $false
    $disabledPath = Write-Fixture 'disabled.json' $old
    $disabled = & $analyzer -ReportPath $disabledPath -Json | ConvertFrom-Json -AsHashtable
    Assert ($disabled.coverage -eq 'unavailable') 'Disabled report treated as observed.'
    $failed = New-Report
    $failed.session_summary = @{ schema = 'hlclient.movement-session.v1'; requested = $true; enabled = $false; availability = 'allocation_failed' }
    $failedPath = Write-Fixture 'allocation-failed.json' $failed
    $failedFacts = & $analyzer -ReportPath $failedPath -Json | ConvertFrom-Json -AsHashtable
    Assert ($failedFacts.coverage -eq 'recent_window_only' -and $failedFacts.summary_availability -eq 'allocation_failed') 'Failed summary allocation treated as measured.'
    $empty = New-Report
    $empty.session_summary.coverage.first_us = $null; $empty.session_summary.coverage.last_us = $null
    $empty.session_summary.coverage.elapsed_us = $null; $empty.session_summary.coverage.accepted_frames = 0
    $empty.session_summary.totals = New-Summary 0
    foreach ($name in @('controlled_alive', 'background', 'inactive_or_dead', 'unknown')) { $empty.session_summary.cohorts[$name] = New-Summary 0 }
    $empty.session_summary.timeline.bins = @()
    $emptyPath = Write-Fixture 'empty.json' $empty
    $emptyFacts = & $analyzer -ReportPath $emptyPath -Json | ConvertFrom-Json -AsHashtable
    Assert ($null -eq $emptyFacts.session.elapsed_us -and $emptyFacts.timeline.bins -eq 0) 'Empty session duration fabricated.'
    $emptyText = (& $analyzer -ReportPath $emptyPath) -join "`n"
    Assert ($emptyText -match 'Уся сесія: н/д с') 'Missing duration printed as zero.'

    $bad = New-Report; $bad.schema = 'untrusted.v1'
    $badPath = Write-Fixture 'bad-schema.json' $bad
    Assert-Rejected { & $analyzer -ReportPath $badPath -Json } 'Unknown schema accepted.'
    $bad = New-Report; $bad.run_id = 'not-an-identity'
    $badPath = Write-Fixture 'bad-identity.json' $bad
    Assert-Rejected { & $analyzer -ReportPath $badPath -Json } 'Invalid identity accepted.'
    $bad = New-Report; $bad.process_id = 0
    $badPath = Write-Fixture 'bad-process.json' $bad
    Assert-Rejected { & $analyzer -ReportPath $badPath -Json } 'Missing process identity accepted.'
    $bad = New-Report; $bad.session_summary.schema = 'future.v2'
    $badPath = Write-Fixture 'bad-session-schema.json' $bad
    Assert-Rejected { & $analyzer -ReportPath $badPath -Json } 'Unknown session schema accepted.'
    $bad = New-Report; $bad.session_summary.totals.counts.frames = 'fixture-sensitive-value'
    $badPath = Write-Fixture 'bad-counter.json' $bad
    Assert-Rejected { & $analyzer -ReportPath $badPath -Json } 'String counter accepted.'
    $bad = New-Report; $bad.session_summary.cohorts.controlled_alive.metrics.gpu_ms.maximum = 1
    $badPath = Write-Fixture 'bad-zero-observations.json' $bad
    Assert-Rejected { & $analyzer -ReportPath $badPath -Json } 'No-observation value accepted.'
    $bad = New-Report; $bad.session_summary.timeline.bins = @($bad.session_summary.timeline.bins[0]) * 513
    $badPath = Write-Fixture 'too-many-bins.json' $bad
    Assert-Rejected { & $analyzer -ReportPath $badPath -Json } 'Unbounded timeline accepted.'
    $malformedPath = Join-Path $fixtureRoot 'malformed.json'
    [IO.File]::WriteAllText($malformedPath, '{"schema":', [Text.UTF8Encoding]::new($false))
    Assert-Rejected { & $analyzer -ReportPath $malformedPath -Json } 'Malformed JSON accepted.'
    $utf8Path = Join-Path $fixtureRoot 'invalid-utf8.json'
    [IO.File]::WriteAllBytes($utf8Path, [byte[]]@(0xff, 0xff))
    Assert-Rejected { & $analyzer -ReportPath $utf8Path -Json } 'Invalid UTF8 accepted.'
    $largePath = Join-Path $fixtureRoot 'oversize.json'
    $largeStream = [IO.File]::Create($largePath)
    try { $largeStream.SetLength(16MB + 1) } finally { $largeStream.Dispose() }
    Assert-Rejected { & $analyzer -ReportPath $largePath -Json } 'Oversize report accepted.'
    Assert-Rejected { & $analyzer -ReportPath $path -RunDirectory $fixtureRoot -Json } 'Mutually exclusive paths accepted.'

    $env:LOCALAPPDATA = Join-Path $fixtureRoot 'local'
    $runsRoot = Join-Path $env:LOCALAPPDATA 'HLClientEngine/runs'
    $runRoot = Join-Path $runsRoot $report.run_id
    $null = New-Item -ItemType Directory -Path $runRoot -Force
    $normalPath = Join-Path $runRoot 'external-movement.json'
    [IO.File]::WriteAllText($normalPath, ($report | ConvertTo-Json -Depth 32), [Text.UTF8Encoding]::new($false))
    $latest = & $analyzer -Json | ConvertFrom-Json -AsHashtable
    Assert ($latest.run_id -eq $report.run_id -and $latest.coverage -eq 'whole_session') 'Default normal-run discovery failed.'
    $explicitRun = & $analyzer -RunDirectory $runRoot -Json | ConvertFrom-Json -AsHashtable
    Assert ($explicitRun.run_id -eq $report.run_id) 'Explicit normal-run discovery failed.'
    $report.run_id = 'ffffffffffffffffffffffffffffffff'
    [IO.File]::WriteAllText($normalPath, ($report | ConvertTo-Json -Depth 32), [Text.UTF8Encoding]::new($false))
    Assert-Rejected { & $analyzer -RunDirectory $runRoot -Json } 'Mismatched run directory identity accepted.'
    Write-Output ('Movement session analysis fixture checks passed: {0}.' -f $checks)
} finally {
    $env:LOCALAPPDATA = $originalLocalAppData
    $resolvedCleanup = [IO.Path]::GetFullPath($fixtureRoot)
    $temporaryParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    if ([IO.Path]::GetDirectoryName($resolvedCleanup) -ne $temporaryParent -or
        [IO.Path]::GetFileName($resolvedCleanup) -notmatch '^hlclient-movement-analysis-[0-9a-f]{32}$') {
        throw 'Unsafe generated-fixture cleanup path.'
    }
    Remove-Item -LiteralPath $resolvedCleanup -Recurse -Force
}
