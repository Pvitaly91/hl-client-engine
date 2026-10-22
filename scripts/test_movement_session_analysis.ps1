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
function New-PredictionWork {
    return [ordered]@{
        command_ms = 1.25; replay_ms = 2.5; collision_query_ms = 0.75
        trace_queries = [UInt64]9007199254740993; position_queries = [UInt64]::MaxValue
        contents_queries = [UInt64]13; traversal_steps = [UInt64]14
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
function New-PriorityReport {
    $report = New-Report
    $report.schema = 'hlclient.external-movement.v2'
    $report.limits = @{ recent_frames=961;incident_frames=480;manual_pre_incident_us=4000000
        manual_sampling_interval_us=12500;automatic_sampling_interval_us=8333;maximum_capacity_bytes=8MB;maximum_encoded_bytes=16MB }
    $report.capture_policy = @{ schema='hlclient.priority-movement-capture.v1';maximum_decisions=128;decision_evictions=7
        coalescing='same_network_map_life_fixed_post_deadline'
        replacement='completed_automatic_lowest_priority_then_oldest_ordinal_never_manual'
        decisions=@(@{ordinal=5;monotonic_us=6000000;frame_number=601;network_generation=1;map_generation=2;life_epoch=1
            source_record_identity=100;source_sequence=12;correction_ordinal=3;incident_ordinal=7;replacement_ordinal=1
            trigger=4;priority=40;context=0;owner_state=2;decision='replaced';reason='active'})
        exact_frames=@() }
    $report.incidents = @(@{ordinal=7;trigger_us=6000000;initial_trigger_us=6000000;planned_end_us=7000000;triggers=1;network_generation=1;map_generation=2
        priority=100;life_epoch=1;requested_pre_us=4000000;requested_post_us=1000000;sampling_interval_us=12500
        retained_samples=3;actual_pre_us=4000000;actual_post_us=1000000;background_samples_omitted=200;exact_samples_omitted=0
        encoded_samples_omitted=0;encoded_actual_pre_us=4000000;encoded_actual_post_us=1000000;complete=$true
        coverage_reason='time_bucket_sampling';manual_marker_us=@(6000000);omitted_manual_markers=0;end_status='post_window_complete'
        frames=@(@{monotonic_us=2000000;frame_number=200;network_generation=1;map_generation=2},
            @{monotonic_us=6000000;frame_number=600;network_generation=1;map_generation=2},
            @{monotonic_us=7000000;frame_number=700;network_generation=1;map_generation=2})})
    $report.capture_policy.exact_frames=@(@{incident_ordinal=7;frame=$report.incidents[0].frames[1]})
    $report.capture_policy.exact_frames[0].frame.prediction_work=New-PredictionWork
    $identity=@{event_ordinal=3;network_generation=1;map_generation=2;life_epoch=1;life_deaths=0;source_record_identity=100}
    $witness=@{identity=$identity;raw_position_error=31.562;reason='active';presentation_reason='interpolated'
        camera_measurement='different_time_samples';same_time_camera_delta=$null;raw_error_comparison='not_measured';presentation_requested_ns=5000000
        presentation_before_ns=4000000;presentation_after_ns=5000000;authoritative_origin=@(0,1,2)
        matched_origin=@(-31.562,1,2);authoritative_velocity=$null;observed_us=3500000;source_sequence=12}
    foreach($name in @('prediction_owner_state','classification','camera_status','predecessor_decision','source_freshness',
        'authoritative_origin_freshness','authoritative_velocity_freshness','matched_freshness','scene_freshness','collision_freshness',
        'presentation_freshness','support_freshness','raw_error_basis','rebase_status','anchor_status','collision_stage','blocking_kind',
        'presentation_before_kind','presentation_after_kind')) { $witness[$name]=0 }
    foreach($name in @('accepted','control_active','source_matches_scene','source_reliable','reassembled','contemporaneous_fragment_suffix')) { $witness[$name]=$false }
    $report.critical_corrections=@{schema='hlclient.critical-corrections.v1';enabled=$true;maximum_top_witnesses=8;maximum_recent_witnesses=16
        accepted_events=80;events_with_raw=79;retained_witnesses=1;duplicates=2;invalid=1;evictions=60
        maximum_raw_position_error=31.562;maximum_witness_identity=$identity;maximum_witness_status='available';witnesses=@($witness)}
    $report.session_summary.totals.producer_correction_maximum=@{events=79;raw_error=31.562;witness_identity=$identity
        status='available';scope='exact_producer_events_not_legacy_frame_ordinal_samples'}
    return $report
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
function New-MultiplayerEvent([int]$Id,[int]$Type,$Payload,[UInt64]$Stamp=1000000000) {
    $Payload.type=$Type
    return @{identity=@{producer=0;producer_event_id=$Id;network_generation=1;map_generation=1;command_id=101;render_frame=$Id;local_life=1}
        local_steady_ns=$Stamp;server_seconds=$null;server_time_domain=0;provenance=4;knowledge=0;edge=1;payload=$Payload}
}
function New-MultiplayerSection($Events) {
    $stats=@{observed=$Events.Count;accepted=$Events.Count;invalid=0;duplicates=0;stale_generation=0;sampled_out=0;recent_evictions=0;exact_evictions=0
        manual_omissions=0;manual_marks=0;manual_marks_dropped=0;coverage_omissions=0;allocation_failures=0;capacity_bytes=2000000}
    return @{schema='hlclient.multiplayer-timeline.v1';requested=$true;enabled=$true;local_clock='steady_epoch_nanoseconds'
        statistics=$stats;manual_windows=@();exact_transitions=$Events;recent_events=@();action_coverage=@();encoded_events_omitted=0;encoding_deadline_reached=$false}
}
try {
    # Capability commands are engine/module identifiers, not lowercase capture
    # reason enums. Preserve the canonical MP5 name and neutral module case.
    $capabilityReport=New-Report
    $capabilityReport.game_diagnostic_capabilities=@(
        @{weapon_id=4;command='weapon_9mmAR';modes=@(1,3,10,11,12);held_primary=$true;persistent_objects=$false},
        @{weapon_id=16;command='custom_Weapon42';modes=@(1,0,0,11,12);held_primary=$false;persistent_objects=$false})
    $capabilityPath=Write-Fixture 'canonical-capability-commands.json' $capabilityReport
    $capabilityHash=(Get-FileHash -LiteralPath $capabilityPath).Hash
    $capabilityFacts=& $analyzer -ReportPath $capabilityPath -Json | ConvertFrom-Json -AsHashtable
    Assert ($capabilityFacts.multiplayer.capability_matrix.Count -eq 10) 'Capability modes lost.'
    Assert (@($capabilityFacts.multiplayer.capability_matrix|Where-Object{$_.weapon_id -eq 4 -and $_.command -ceq 'weapon_9mmAR'}).Count -eq 5) 'Canonical MP5 command was rejected or case-folded.'
    Assert (@($capabilityFacts.multiplayer.capability_matrix|Where-Object{$_.weapon_id -eq 16 -and $_.command -ceq 'custom_Weapon42'}).Count -eq 5) 'Neutral module command case was not preserved.'
    Assert ((Get-FileHash -LiteralPath $capabilityPath).Hash -ceq $capabilityHash) 'Read-only analysis changed source report.'
    foreach($badCommand in @('','weapon AR','weapon/ar','weapon\ar','weapon;quit','weapon"ar',"weapon`nAR",'weapon_Ж',('a'*49),$null,42)) {
        $capabilityReport.game_diagnostic_capabilities[0].command=$badCommand
        $badCapabilityPath=Write-Fixture 'invalid-capability-command.json' $capabilityReport
        Assert-Rejected { & $analyzer -ReportPath $badCapabilityPath -Json } 'Unsafe or malformed capability command accepted.'
    }
    $identityReport=New-Report
    $null=New-Item -ItemType Directory -Path (Join-Path $fixtureRoot 'identity')
    $identityPath=Write-Fixture 'identity/external-movement.json' $identityReport
    $identityFacts=& $analyzer -ReportPath $identityPath -Json | ConvertFrom-Json -AsHashtable
    Assert ($identityFacts.identity_evidence.availability -eq 'not_recorded_older_or_fixture' -and
        $null -eq $identityFacts.identity_evidence.recorded_identity_consistent) 'Missing manifests acquired a valid identity claim.'
    $launchIdentity=@{schema='hlclient.external-launch-identity.v1';run_id=$identityReport.run_id
        client_sha256=('a'*64);launcher_sha256=('b'*64);config_sha256=('c'*64);ignored_path='C:/PrivateManifestSecret'}
    $processIdentity=@{schema='hlclient.external-process-identity.v1';run_id=$identityReport.run_id
        process_id=$identityReport.process_id;process_creation=$identityReport.process_creation;client_sha256=('A'*64)}
    $launchIdentityPath=Write-Fixture 'identity/launch-manifest.json' $launchIdentity
    $partialIdentity=& $analyzer -ReportPath $identityPath -Json | ConvertFrom-Json -AsHashtable
    Assert ($partialIdentity.identity_evidence.availability -eq 'partial_manifest_identity_unknown' -and
        $null -eq $partialIdentity.identity_evidence.recorded_identity_consistent) 'A launch-only manifest became a verified process.'
    $processIdentityPath=Write-Fixture 'identity/process-manifest.json' $processIdentity
    $validIdentityText=& $analyzer -ReportPath $identityPath -Json
    $validIdentity=$validIdentityText|ConvertFrom-Json -AsHashtable
    Assert ($validIdentity.identity_evidence.availability -eq 'consistent_manifest_pair' -and
        $validIdentity.identity_evidence.recorded_identity_consistent -and -not $validIdentity.identity_evidence.current_binaries_rehashed) 'Matching stored identity manifests did not bind or claimed a fresh binary hash.'
    Assert (($validIdentityText -join '') -notmatch 'PrivateManifestSecret') 'Unknown manifest fields leaked into analysis.'
    $manifestCases=@(
        @{leaf='launch';field='schema';value='hlclient.external-launch-identity.v99'},
        @{leaf='process';field='schema';value='hlclient.external-process-identity.v99'},
        @{leaf='launch';field='run_id';value=('f'*32)},@{leaf='process';field='run_id';value=('f'*32)},
        @{leaf='process';field='process_id';value=43},
        @{leaf='process';field='process_creation';value=($identityReport.process_creation+1)},
        @{leaf='process';field='client_sha256';value=('d'*64)},
        @{leaf='launch';field='client_sha256';value=('z'*64)},
        @{leaf='launch';field='launcher_sha256';value=('b'*63)},
        @{leaf='launch';field='config_sha256';value=('../'+('c'*61))},
        @{leaf='launch';field='process_id';value=43})
    foreach($case in $manifestCases) {
        $badIdentity=$(if($case.leaf -eq 'launch'){$launchIdentity.Clone()}else{$processIdentity.Clone()})
        $badIdentity[$case.field]=$case.value
        $null=Write-Fixture ('identity/'+$case.leaf+'-manifest.json') $badIdentity
        Assert-Rejected { & $analyzer -ReportPath $identityPath -Json } ('Invalid manifest identity accepted: '+$case.leaf+'/'+$case.field)
        $null=Write-Fixture 'identity/launch-manifest.json' $launchIdentity
        $null=Write-Fixture 'identity/process-manifest.json' $processIdentity
    }
    [IO.File]::WriteAllBytes($launchIdentityPath,[byte[]]@(0xff,0xfe,0xfd))
    Assert-Rejected { & $analyzer -ReportPath $identityPath -Json } 'Invalid manifest UTF-8 accepted.'
    $largeIdentityStream=[IO.File]::Create($launchIdentityPath)
    try{$largeIdentityStream.SetLength(16KB+1)}finally{$largeIdentityStream.Dispose()}
    Assert-Rejected { & $analyzer -ReportPath $identityPath -Json } 'Unbounded manifest accepted.'
    $null=Write-Fixture 'identity/launch-manifest.json' $launchIdentity
    Remove-Item -LiteralPath $launchIdentityPath
    $processOnly=& $analyzer -ReportPath $identityPath -Json | ConvertFrom-Json -AsHashtable
    Assert ($processOnly.identity_evidence.availability -eq 'partial_manifest_identity_unknown') 'A process-only manifest hid missing launch/config identity.'
    $null=New-Item -ItemType Directory -Path $launchIdentityPath
    Assert-Rejected { & $analyzer -ReportPath $identityPath -Json } 'A directory masquerading as a manifest was accepted.'

    $command=New-MultiplayerEvent 1 0 @{phase=3;angle_turns=@(0,0,0);lerp_msec=100;effective_interpolation_seconds=.1}
    $carrier=New-MultiplayerEvent 2 1 @{phase=3;command_ids=@(101)} 1001000000;$carrier.knowledge=4;$carrier.identity.carrier_packet=77
    $accepted=New-MultiplayerEvent 3 2 @{phase=1;kind=1;weapon_id=4;mode=1;command_angles_degrees=@(0,0,0);reason='accepted'} 1002000000
    $accepted.knowledge=2;$accepted.identity.action_id=900
    $multi=New-Report;$multi.multiplayer_timeline=New-MultiplayerSection @($command,$carrier,$accepted)
    $multiPath=Write-Fixture 'multiplayer-matched.json' $multi
    $matched=& $analyzer -ReportPath $multiPath -Json | ConvertFrom-Json -AsHashtable
    Assert ($matched.multiplayer.verdict -eq 'not_established') 'Matched controls incorrectly claimed a client defect or global health.'
    Assert ($matched.multiplayer.action_chains.Count -eq 1) 'Accepted action chain missing.'
    Assert ($matched.multiplayer.action_chains[0].immutable_command.producer_event_id -eq 1 -and $matched.multiplayer.action_chains[0].sent_carrier.carrier_packet -eq 77) 'Explicit command/carrier identity not retained.'
    Assert ($matched.multiplayer.action_chains[0].server_damage -eq 'unknown') 'Transport was promoted to damage.'
    $beforeHash=(Get-FileHash -LiteralPath $multiPath -Algorithm SHA256).Hash
    $published=& $analyzer -ReportPath $multiPath -PublishSummary -Json | ConvertFrom-Json -AsHashtable
    Assert (Test-Path -LiteralPath $published.summary_path -PathType Leaf) 'Explicit local summary was not published.'
    Assert ((Get-FileHash -LiteralPath $multiPath -Algorithm SHA256).Hash -eq $beforeHash) 'Publishing changed original evidence.'
    Assert-Rejected { & $analyzer -ReportPath $multiPath -Open -Json } 'Open silently enabled writing.'

    $due=New-MultiplayerEvent 31 0 @{phase=1;lerp_msec=0;effective_interpolation_seconds=.075}
    $built=New-MultiplayerEvent 32 0 @{phase=2;lerp_msec=0;effective_interpolation_seconds=.075} 1001000000
    $wirePhases=New-Report;$wirePhases.multiplayer_timeline=New-MultiplayerSection @($due,$built)
    $wirePath=Write-Fixture 'multiplayer-wire-phase-control.json' $wirePhases
    $wireFacts=& $analyzer -ReportPath $wirePath -Json | ConvertFrom-Json -AsHashtable
    $wireFindings=@($wireFacts.multiplayer.classifications|Where-Object{$_.code -eq 'wire_lerp_differs_from_declared_effective_delay'})
    Assert ($wireFindings.Count -eq 1 -and $wireFindings[0].identity.producer_event_id -eq 32) 'Unbuilt scheduler defaults were treated as wire values, or real built mismatch was lost.'

    $read=New-MultiplayerEvent 40 1 @{phase=4;begin_ns=1000000000;end_ns=1001000000};$read.identity.source_sequence=77
    $commit=New-MultiplayerEvent 41 1 @{phase=7;commit_ns=1040000000;atomic_commit=$true} 1040000000;$commit.identity.source_sequence=77
    $otherGeneration=New-MultiplayerEvent 42 1 @{phase=7;commit_ns=1050000000;atomic_commit=$true} 1050000000
    $otherGeneration.identity.source_sequence=77;$otherGeneration.identity.network_generation=2
    $otherMap=New-MultiplayerEvent 43 1 @{phase=7;commit_ns=1050000000;atomic_commit=$true} 1050000000
    $otherMap.identity.source_sequence=77;$otherMap.identity.map_generation=2
    $noRead=New-MultiplayerEvent 44 1 @{phase=7;commit_ns=1050000000;atomic_commit=$true} 1050000000;$noRead.identity.source_sequence=78
    $receiveReport=New-Report;$receiveReport.multiplayer_timeline=New-MultiplayerSection @($read,$commit,$otherGeneration,$otherMap,$noRead)
    $receivePath=Write-Fixture 'multiplayer-receive-commit-join.json' $receiveReport
    $receiveFacts=& $analyzer -ReportPath $receivePath -Json | ConvertFrom-Json -AsHashtable
    $receiveFindings=@($receiveFacts.multiplayer.classifications|Where-Object{$_.code -eq 'measured_receive_to_commit_interval'})
    Assert ($receiveFindings.Count -eq 1 -and $receiveFindings[0].measurements.milliseconds -eq 40 -and
        $receiveFindings[0].measurements.after_socket_read_ms -eq 39) 'Separate socket/commit records were not joined by exact source identity.'
    Assert (@($receiveFacts.multiplayer.receive_commit_links|Where-Object{$_.timing_status -eq 'unknown_receive_not_retained' -and
        $null -eq $_.receive_begin_to_commit_ms}).Count -eq 3) 'Unlinked or cross-generation receive timing was invented.'

    $carrier.payload.command_count=1;$carrier.payload.backup_count=0
    $backup=New-MultiplayerEvent 50 1 @{phase=3;command_ids=@(101,102);command_count=1;backup_count=1} 1010000000
    $backup.knowledge=4;$backup.identity.carrier_packet=78
    $backupReport=New-Report;$backupReport.multiplayer_timeline=New-MultiplayerSection @($command,$carrier,$accepted,$backup)
    $backupPath=Write-Fixture 'multiplayer-backup-carrier.json' $backupReport
    $backupFacts=& $analyzer -ReportPath $backupPath -Json | ConvertFrom-Json -AsHashtable
    $backupChain=$backupFacts.multiplayer.action_chains[0]
    Assert ($backupChain.sent_carrier.carrier_packet -eq 77 -and $backupChain.sent_carrier_membership -eq 'new' -and
        $backupChain.retained_sent_carrier_count -eq 2) 'A later backup overwrote the earliest retained successful carrier.'
    $backupReport.multiplayer_timeline=New-MultiplayerSection @($command,$accepted,$backup)
    $backupOnlyPath=Write-Fixture 'multiplayer-backup-only-carrier.json' $backupReport
    $backupOnlyFacts=& $analyzer -ReportPath $backupOnlyPath -Json | ConvertFrom-Json -AsHashtable
    $backupOnlyChain=$backupOnlyFacts.multiplayer.action_chains[0]
    Assert ($backupOnlyChain.sent_carrier_membership -eq 'backup' -and $null -eq $backupOnlyChain.original_send_latency_ms -and
        $backupOnlyChain.first_actual_send -eq 'unknown_if_earlier_details_omitted') 'A retained backup was claimed to be the original send or its latency.'

    $command.payload.lerp_msec=200
    $ack=New-MultiplayerEvent 4 1 @{phase=9;command_ids=@()} 1003000000;$ack.knowledge=7
    $cosmetic=New-MultiplayerEvent 5 2 @{phase=8;reason='cosmetic'} 1004000000;$cosmetic.knowledge=8
    $pose=New-MultiplayerEvent 6 4 @{phase=3;sample_seconds=10;pose_seconds=9.8;configured_delay_seconds=.1;effective_delay_seconds=.2;present_ms=35;gpu_ms=25} 1005000000
    $proxy=New-MultiplayerEvent 7 6 @{world_blocked=$true;fraction=.4} 1006000000;$proxy.knowledge=8
    $accepted.payload.command_angles_degrees=@(10,0,0)
    $move1=New-MultiplayerEvent 8 8 @{predicted_origin=@(0,0,0);raw_server_origin=@(0,0,0);velocity=@(320,0,0)} 1000000000;$move1.identity.command_id=1
    $move2=New-MultiplayerEvent 9 8 @{predicted_origin=@(0,0,0);raw_server_origin=@(32,0,0);velocity=@(320,0,0)} 1200000000;$move2.identity.command_id=2;$move2.identity.render_frame=11
    $fast=New-MultiplayerEvent 10 4 @{phase=8;frame_ms=5} 1200000000;$fast.identity.render_frame=11
    $death=New-MultiplayerEvent 11 3 @{kind=4} 1210000000
    $multi.multiplayer_timeline=New-MultiplayerSection @($command,$carrier,$accepted,$ack,$cosmetic,$pose,$proxy,$move1,$move2,$fast,$death)
    $negativePath=Write-Fixture 'multiplayer-negative.json' $multi
    $negative=& $analyzer -ReportPath $negativePath -Json | ConvertFrom-Json -AsHashtable
    $codes=@($negative.multiplayer.classifications | ForEach-Object {$_.code})
    foreach($expected in @('wire_lerp_differs_from_declared_effective_delay','transport_promoted_to_gameplay_outcome',
        'cosmetic_feedback_promoted_to_server_outcome','pose_origin_sample_times_disagree','configured_effective_interpolation_disagreement',
        'measured_present_ms','measured_gpu_ms','proxy_query_promoted_to_server_hit_or_miss','read_only_world_blocker_observed',
        'action_and_immutable_wire_aim_disagree','prediction_hold_despite_source_motion_and_fast_render','lifecycle_boundary_observed')) {
        Assert ($expected -in $codes) ('Intentional negative diagnostic missed '+$expected)
    }
    Assert (@($negative.multiplayer.classifications|Where-Object{$_.server_hit_or_miss -ne 'not_inferred'}).Count -eq 0) 'Negative classifier invented a server outcome.'
    foreach($gpuFirst in @($false,$true)) {
        $timedFrame=New-MultiplayerEvent 60 4 @{phase=7;frame_ms=5;present_ms=1} 1200000000
        $timedFrame.identity.render_frame=11
        $asyncGpu=New-MultiplayerEvent 61 4 @{phase=8;gpu_ms=25} $(if($gpuFirst){1190000000}else{1250000000})
        $asyncGpu.identity.render_frame=11
        $metadataOnly=New-MultiplayerEvent 62 4 @{phase=3;pose_seconds=10;frame_ms=$null;gpu_ms=$null} 1260000000
        $metadataOnly.identity.render_frame=11
        $foreignGpu=New-MultiplayerEvent 63 4 @{phase=8;gpu_ms=30} 1270000000
        $foreignGpu.identity.render_frame=11;$foreignGpu.identity.map_generation=2
        $timingReport=New-Report
        $timingReport.multiplayer_timeline=New-MultiplayerSection @($move1,$move2,$timedFrame,$asyncGpu,$metadataOnly,$foreignGpu)
        $timingPath=Write-Fixture ('multiplayer-independent-frame-gpu-'+$gpuFirst+'.json') $timingReport
        $timingFacts=& $analyzer -ReportPath $timingPath -Json | ConvertFrom-Json -AsHashtable
        $hold=@($timingFacts.multiplayer.classifications|Where-Object{$_.code -eq 'prediction_hold_despite_source_motion_and_fast_render'})
        Assert ($hold.Count -eq 1 -and $hold[0].measurements.render_frame_ms -eq 5) 'Late GPU/pose metadata erased the measured frame interval.'
        $gpuFacts=@($timingFacts.multiplayer.classifications|Where-Object{$_.code -eq 'measured_gpu_ms' -and $_.identity.producer_event_id -eq 61})
        Assert ($gpuFacts.Count -eq 1 -and $gpuFacts[0].measurements.milliseconds -eq 25 -and $gpuFacts[0].measurements.frame_ms -eq 5) 'Independent frame/GPU measurements were not retained by exact frame identity.'
        Assert ($gpuFacts[0].measurements.frame_measurement_identity.producer_event_id -eq 60) 'Frame measurement lost its actual owner identity.'
        $foreignFacts=@($timingFacts.multiplayer.classifications|Where-Object{$_.code -eq 'measured_gpu_ms' -and $_.identity.producer_event_id -eq 63})
        Assert ($foreignFacts.Count -eq 1 -and $null -eq $foreignFacts[0].measurements.frame_ms) 'Frame/GPU timing was joined across map generations.'
    }
    $multi.multiplayer_timeline.schema='hlclient.multiplayer-timeline.v99'
    $futurePath=Write-Fixture 'multiplayer-future.json' $multi
    Assert-Rejected { & $analyzer -ReportPath $futurePath -Json } 'Unknown multiplayer schema silently reinterpreted.'
    if($env:HLCLIENT_MULTIPLAYER_TIMELINE_FIXTURE_INPUT) {
        $actual=& $analyzer -ReportPath $env:HLCLIENT_MULTIPLAYER_TIMELINE_FIXTURE_INPUT -Json | ConvertFrom-Json -AsHashtable
        Assert ($actual.multiplayer.availability -eq 'ready' -and $actual.multiplayer.retained_unique_events -gt 0) 'Actual recorder fixture failed analyzer integration.'
    }
    $priority=New-PriorityReport
    $priorityPath=Write-Fixture 'priority-v2.json' $priority
    $priorityHash=(Get-FileHash -LiteralPath $priorityPath).Hash
    $priorityText=& $analyzer -ReportPath $priorityPath -Json
    $priorityFacts=$priorityText | ConvertFrom-Json -AsHashtable
    Assert ($priorityFacts.source_schema -eq 'hlclient.external-movement.v2') 'V2 format not explicit.'
    Assert ($priorityFacts.static_world.availability -eq 'not_recorded_in_older_build') 'Older static-world receipt was fabricated.'
    Assert ($priorityFacts.capture.incidents[0].ordinal -eq 7 -and $priorityFacts.capture.incidents[0].actual_pre_us -eq 4000000) 'Priority identity or actual four-second coverage lost.'
    Assert ($priorityFacts.capture.incidents[0].background_samples_omitted -eq 200) 'Reduced sampling hidden.'
    Assert ($priorityFacts.capture.decisions[0].replacement_ordinal -eq 1 -and $priorityFacts.capture.decision_evictions -eq 7) 'Replacement or decision eviction hidden.'
    Assert ($priorityFacts.critical_corrections.maximum_witness_status -eq 'available') 'Exact maximum unavailable.'
    Assert ($priorityFacts.critical_corrections.witnesses[0].identity.source_record_identity -eq 100) 'Source identity lost.'
    Assert ($priorityFacts.critical_corrections.delivery_status -eq 'unknown_not_recorded' -and
        $null -eq $priorityFacts.critical_corrections.delivery_sink_failures) 'Missing delivery metadata guessed complete.'
    Assert ($null -eq $priorityFacts.critical_corrections.witnesses[0].authoritative_velocity) 'Missing server velocity invented.'
    Assert ($null -eq $priorityFacts.critical_corrections.witnesses[0].same_time_camera_delta) 'Different-time samples presented as measured delta.'
    Assert ($priorityFacts.totals.producer_correction_maximum.status -eq 'available') 'Total maximum source link lost.'
    Assert ($priorityFacts.capture.exact_frames.Count -eq 1 -and $priorityFacts.capture.exact_frames[0].frame.frame_number -eq 600) 'Exact marker prefix hidden.'
    $exactWork=$priorityFacts.capture.exact_frames[0].frame.prediction_work
    Assert ($exactWork.command_ms -eq 1.25 -and $exactWork.collision_query_ms -eq 0.75 -and
        [decimal]$exactWork.trace_queries -eq [decimal]9007199254740993 -and
        [decimal]$exactWork.position_queries -eq [decimal][UInt64]::MaxValue -and
        $exactWork.traversal_steps -eq 14) 'Exact-frame prediction work lost or integer precision changed.'
    Assert ($priorityFacts.capture.incidents[0].initial_trigger_us -eq 6000000 -and
        $priorityFacts.capture.incidents[0].end_status -eq 'post_window_complete' -and
        $priorityFacts.capture.incidents[0].omitted_manual_markers -eq 0 -and
        $priorityFacts.capture.incidents[0].marker_witnesses[0].status -eq 'available') 'Incident lifecycle or marker proof hidden.'
    Assert ((Get-FileHash -LiteralPath $priorityPath).Hash -eq $priorityHash) 'V2 input modified.'
    $priorityHuman=(& $analyzer -ReportPath $priorityPath) -join "`n"
    Assert ($priorityHuman -match 'фактично 4/1 с' -and $priorityHuman -match 'не повний покадровий') 'V2 coverage or thinning caveat missing.'
    Assert ($priorityHuman -match 'точний кадр=available' -and $priorityHuman -match 'Доставка producer witnesses: unknown_not_recorded') 'Exact frame or unknown delivery caveat absent.'
    $staticWorld=New-PriorityReport
    $staticWorld.static_world_receipt=@{schema='hlclient.static-world-frame.v1'
        scope='completed_renderer_frame_in_recent_window_or_incident';counter_semantics='per_frame_not_cumulative'
        considered_blocks_semantics='eligible_immutable_pool';tested_bounds_semantics='frustum_calls_including_retests'
        selected_blocks_semantics='submitted_pool_blocks'
        selection_preparation_scope='frustum_probe_range_decisions_excludes_gl_submission_and_map_load'
        submitted_triangles_semantics='geometry_not_visible_pixels';unknown='null'
        omission='bounded_recent_window_and_incident_sampling'}
    $staticWorld.recent_window.frames=@(
        @{frame_number=10;static_world=@{path='prepared_selected';fallback_reason='fragmented_batch'
            resource_id=81;resource_revision=7;considered_blocks=272;tested_bounds=63;selected_blocks=180
            considered_surfaces=$null;selected_surfaces=$null;submitted_draws=19;submitted_triangles=1600
            prepared_rebuilds=$null;selection_preparation_ms=0.125}},
        @{frame_number=11;static_world=$null})
    $staticWorldPath=Write-Fixture 'priority-static-world.json' $staticWorld
    $staticWorldFacts=(& $analyzer -ReportPath $staticWorldPath -Json) | ConvertFrom-Json -AsHashtable
    Assert ($staticWorldFacts.static_world.coverage -eq 'recent_window_only' -and
        $staticWorldFacts.static_world.frames_scanned -eq 2 -and
        $staticWorldFacts.static_world.frames_with_receipt -eq 1 -and
        $staticWorldFacts.static_world.frames_unavailable -eq 1) 'Static-world bounded coverage was expanded or lost.'
    Assert ($staticWorldFacts.static_world.paths.prepared_selected -eq 1 -and
        $staticWorldFacts.static_world.fallback_frames -eq 1 -and
        $staticWorldFacts.static_world.counters.tested_bounds.total -eq 63 -and
        $staticWorldFacts.static_world.counters.submitted_triangles.total -eq 1600 -and
        $staticWorldFacts.static_world.counters.considered_surfaces.observed -eq 0 -and
        $staticWorldFacts.static_world.selection_preparation_ms_max -eq 0.125 -and
        $staticWorldFacts.static_world.counters.prepared_rebuilds.observed -eq 0) 'Static-world counters invented missing data.'
    $staticWorld.static_world_receipt.schema='hlclient.static-world-frame.v99'
    $staticWorldFuturePath=Write-Fixture 'priority-static-world-future.json' $staticWorld
    Assert-Rejected { & $analyzer -ReportPath $staticWorldFuturePath -Json } 'Unknown static-world schema silently reinterpreted.'
    $staticWorld.static_world_receipt.schema='hlclient.static-world-frame.v1'
    $staticWorld.static_world_receipt.tested_bounds_semantics='unique_blocks_only'
    $staticWorldChangedSemanticsPath=Write-Fixture 'priority-static-world-changed-semantics.json' $staticWorld
    Assert-Rejected { & $analyzer -ReportPath $staticWorldChangedSemanticsPath -Json } 'Changed static-world v1 metadata silently reinterpreted.'
    $missingExactWork=New-PriorityReport
    $missingExactWork.capture_policy.exact_frames[0].frame.Remove('prediction_work')
    $missingExactWorkPath=Write-Fixture 'priority-prediction-work-older.json' $missingExactWork
    $missingExactWorkFacts=(& $analyzer -ReportPath $missingExactWorkPath -Json) | ConvertFrom-Json -AsHashtable
    Assert ($null -eq $missingExactWorkFacts.capture.exact_frames[0].frame.prediction_work) 'Older exact frame fabricated prediction work.'
    $olderPriority=New-PriorityReport
    $olderPriority.incidents[0].Remove('initial_trigger_us');$olderPriority.capture_policy.Remove('exact_frames')
    $olderPriorityPath=Write-Fixture 'priority-older-extensions.json' $olderPriority
    $olderPriorityFacts=(& $analyzer -ReportPath $olderPriorityPath -Json) | ConvertFrom-Json -AsHashtable
    Assert ($olderPriorityFacts.capture.exact_frames_availability -eq 'not_recorded_in_older_build' -and
        $olderPriorityFacts.capture.incidents[0].initial_trigger_status -eq 'not_recorded_in_older_build') 'Older V2 extension guessed available.'
    $encodedCap=New-PriorityReport
    $encodedCap.incidents[0].frames=@();$encodedCap.incidents[0].encoded_samples_omitted=3
    $encodedCap.incidents[0].encoded_actual_pre_us=$null;$encodedCap.incidents[0].encoded_actual_post_us=$null
    $encodedCap.incidents[0].omitted_manual_markers=4
    $encodedCapPath=Write-Fixture 'priority-protected-prefix-after-encode-cap.json' $encodedCap
    $encodedCapFacts=(& $analyzer -ReportPath $encodedCapPath -Json) | ConvertFrom-Json -AsHashtable
    Assert ($encodedCapFacts.capture.incidents[0].marker_witnesses[0].status -eq 'available' -and
        $null -eq $encodedCapFacts.capture.incidents[0].encoded_actual_pre_us -and
        $encodedCapFacts.capture.incidents[0].omitted_manual_markers -eq 4) 'Encoded frame loss hid surviving exact marker or overflow.'
    $promoted=New-PriorityReport;$promoted.incidents[0].initial_trigger_us=5900000;$promoted.incidents[0].triggers=3
    $originalTrigger=@{monotonic_us=5900000;frame_number=590;network_generation=1;map_generation=2}
    $promoted.incidents[0].frames=@($promoted.incidents[0].frames[0],$originalTrigger,$promoted.incidents[0].frames[1],$promoted.incidents[0].frames[2])
    $promoted.incidents[0].retained_samples=4;$promoted.capture_policy.exact_frames+=@{incident_ordinal=7;frame=$originalTrigger}
    $promotedPath=Write-Fixture 'priority-promoted-original-trigger.json' $promoted
    $promotedFacts=(& $analyzer -ReportPath $promotedPath -Json) | ConvertFrom-Json -AsHashtable
    Assert ($promotedFacts.capture.exact_frames.Count -eq 2 -and $promotedFacts.capture.exact_frames[1].roles -contains 'initial_trigger') 'Promoted automatic trigger identity lost.'
    foreach($status in @('not_observed','complete','partial')) {
        $delivery=New-PriorityReport;$delivery.critical_corrections.delivery_status=$status
        $delivery.critical_corrections.delivery_observations=$(if($status -eq 'not_observed'){0}else{5})
        $delivery.critical_corrections.delivery_sink_failures=$(if($status -eq 'partial'){1}else{0})
        $delivery.critical_corrections.delivery_published_events=80;$delivery.critical_corrections.delivery_counter_resets=0
        $delivery.critical_corrections.delivery_generation_boundaries=1;$delivery.critical_corrections.delivery_duplicate_observations=2
        $delivery.critical_corrections.delivery_stale_generation_observations=0
        $deliveryPath=Write-Fixture ('priority-delivery-'+$status+'.json') $delivery
        $deliveryFacts=(& $analyzer -ReportPath $deliveryPath -Json) | ConvertFrom-Json -AsHashtable
        Assert ($deliveryFacts.critical_corrections.delivery_status -eq $status) ('Producer delivery '+$status+' lost.')
    }
    foreach($variant in @('prefix_limit','prefix_duplicate','prefix_owner','prefix_generation','prefix_unlinked','prefix_missing',
        'initial_after_marker','end_status','end_complete','marker_overflow','delivery_bad_status','delivery_incomplete',
        'delivery_counter_without_status','delivery_partial_reset','delivery_partial_stale','delivery_policy',
        'work_missing','work_extra','work_duration','work_counter_fraction','work_counter_overflow','work_counter_string')) {
        $invalidExtension=New-PriorityReport
        switch($variant) {
            prefix_limit { $invalidExtension.capture_policy.exact_frames=@($invalidExtension.capture_policy.exact_frames[0])*73 }
            prefix_duplicate { $invalidExtension.capture_policy.exact_frames+=$invalidExtension.capture_policy.exact_frames[0] }
            prefix_owner { $invalidExtension.capture_policy.exact_frames[0].incident_ordinal=8 }
            prefix_generation { $invalidExtension.capture_policy.exact_frames[0].frame.map_generation=3 }
            prefix_unlinked { $invalidExtension.capture_policy.exact_frames[0].frame.monotonic_us=6500000 }
            prefix_missing { $invalidExtension.capture_policy.exact_frames=@() }
            initial_after_marker { $invalidExtension.incidents[0].initial_trigger_us=6000001 }
            end_status { $invalidExtension.incidents[0].end_status='unknown' }
            end_complete { $invalidExtension.incidents[0].end_status='life_boundary' }
            marker_overflow { $invalidExtension.incidents[0].omitted_manual_markers=-1 }
            delivery_bad_status { $invalidExtension.critical_corrections.delivery_status='invented_complete' }
            delivery_incomplete { $invalidExtension.critical_corrections.delivery_status='complete' }
            delivery_counter_without_status { $invalidExtension.critical_corrections.delivery_sink_failures=0 }
            delivery_partial_reset { $invalidExtension.critical_corrections.delivery_status='complete' }
            delivery_partial_stale { $invalidExtension.critical_corrections.delivery_status='complete' }
            delivery_policy { $invalidExtension.critical_corrections.delivery_counter_policy='C:/private/secret' }
            work_missing { $invalidExtension.capture_policy.exact_frames[0].frame.prediction_work.Remove('traversal_steps') }
            work_extra { $invalidExtension.capture_policy.exact_frames[0].frame.prediction_work.unapproved='fixture-sensitive-value' }
            work_duration { $invalidExtension.capture_policy.exact_frames[0].frame.prediction_work.command_ms=1000000001 }
            work_counter_fraction { $invalidExtension.capture_policy.exact_frames[0].frame.prediction_work.trace_queries=1.5 }
            work_counter_overflow { $invalidExtension.capture_policy.exact_frames[0].frame.prediction_work.trace_queries=[decimal]'18446744073709551616' }
            work_counter_string { $invalidExtension.capture_policy.exact_frames[0].frame.prediction_work.trace_queries='9007199254740993' }
        }
        if($variant -in @('delivery_bad_status','delivery_partial_reset','delivery_partial_stale')) {
            foreach($name in @('delivery_observations','delivery_sink_failures','delivery_published_events','delivery_counter_resets',
                'delivery_generation_boundaries','delivery_duplicate_observations','delivery_stale_generation_observations')) {$invalidExtension.critical_corrections[$name]=0}
            $invalidExtension.critical_corrections.delivery_observations=1
            if($variant -eq 'delivery_partial_reset'){$invalidExtension.critical_corrections.delivery_counter_resets=1}
            if($variant -eq 'delivery_partial_stale'){$invalidExtension.critical_corrections.delivery_stale_generation_observations=1}
        }
        $invalidExtensionPath=Write-Fixture ('priority-extension-invalid-'+$variant+'.json') $invalidExtension
        Assert-Rejected { & $analyzer -ReportPath $invalidExtensionPath -Json } ('Invalid priority extension '+$variant+' accepted.')
    }
    foreach($variant in @('schema','capacity','coverage','retained','reason','decision','witness_count','identity','duplicate','same_time','number','velocity')) {
        $badPriority=New-PriorityReport
        switch($variant) {
            schema { $badPriority.schema='hlclient.external-movement.v3' }
            capacity { $badPriority.limits.incident_frames=512 }
            coverage { $badPriority.incidents[0].actual_pre_us=5000000 }
            retained { $badPriority.incidents[0].retained_samples=481 }
            reason { $badPriority.capture_policy.decisions[0].reason='C:/private/secret' }
            decision { $badPriority.capture_policy.decisions[0].decision='unapproved' }
            witness_count { $badPriority.critical_corrections.retained_witnesses=2 }
            identity { $badPriority.critical_corrections.maximum_witness_identity=@{event_ordinal=9;network_generation=1;map_generation=2;life_epoch=1;life_deaths=0;source_record_identity=100} }
            duplicate { $badPriority.critical_corrections.witnesses += $badPriority.critical_corrections.witnesses[0] }
            same_time { $badPriority.critical_corrections.witnesses[0].same_time_camera_delta=1 }
            number { $badPriority.critical_corrections.witnesses[0].raw_position_error='private' }
            velocity { $badPriority.critical_corrections.witnesses[0].authoritative_velocity=@(1,'private',2) }
        }
        # In identity control don't mutate a shared fixture dictionary twice.
        if($variant -eq 'identity') { $badPriority.critical_corrections.maximum_witness_identity=@{event_ordinal=9;network_generation=1;map_generation=2;life_epoch=1;life_deaths=0;source_record_identity=100} }
        $badPriorityPath=Write-Fixture ('priority-invalid-'+$variant+'.json') $badPriority
        Assert-Rejected { & $analyzer -ReportPath $badPriorityPath -Json } ('Invalid V2 '+$variant+' accepted.')
    }
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
    $renderSupportFields = @('raw_render_mode', 'raw_render_amount', 'raw_render_effect',
        'rendered_opacity', 'support_entity', 'support_model', 'support_offset_z')
    foreach ($field in $renderSupportFields) {
        Assert ($null -eq $remoteFacts.remote_players.recent_samples[0][$field] -and
            $null -eq $remoteFacts.remote_players.events[1].before[$field] -and
            $null -eq $remoteFacts.remote_players.events[1].current[$field] -and
            $null -eq $remoteFacts.remote_players.manual_captures[0].samples[1][$field] -and
            $null -eq $remoteFacts.remote_players.manual_captures[0].events[0].current[$field]) (
            'Older remote records fabricated render/support observation: ' + $field)
    }
    Assert ($remoteText -notmatch 'PrivateAssetRoot|secret-resource|fixture-sensitive') 'Remote diagnostic leaked unapproved fields.'
    Assert ((Get-FileHash -LiteralPath $remotePath).Hash -eq $remoteHash) 'Remote analyzer wrote input.'
    Assert ($remoteFacts.cohorts.controlled_alive.counts.frames -eq 10 -and $remoteFacts.fps_observations.mean_fps_equivalent -eq 100) 'Remote diagnostics changed local prediction or FPS summary.'
    $remoteHuman = (& $analyzer -ReportPath $remotePath) -join "`n"
    Assert ($remoteHuman -match 'Актор entity=3' -and $remoteHuman -match 'Δt=10 мс' -and $remoteHuman -match 'sampled=\[3, 20, -32\]') 'Human per-actor evidence absent.'
    Assert ($remoteHuman -match 'не доводить плавності кожного' -and $remoteHuman -match 'не між прорідженими') 'Remote limitations absent.'
    foreach ($triggerMask in @(256, 512, 768, 992)) {
        $renderSupport = New-Report; $renderSupport.remote_players = New-RemotePlayers
        $renderBefore = $renderSupport.remote_players.recent_samples[0]
        $renderCurrent = $renderSupport.remote_players.recent_samples[1]
        $renderBefore.raw_render_mode = 0; $renderBefore.raw_render_amount = 255; $renderBefore.raw_render_effect = 0
        $renderBefore.rendered_opacity = 1
        # Explicit unknown values must remain distinct from measured zero.
        foreach ($field in @('support_entity', 'support_model', 'support_offset_z')) { $renderBefore[$field] = $null }
        $renderCurrent.raw_render_mode = 2; $renderCurrent.raw_render_amount = 64; $renderCurrent.raw_render_effect = 3
        $renderCurrent.rendered_opacity = 64.0 / 255.0
        $renderCurrent.support_entity = 71; $renderCurrent.support_model = 9; $renderCurrent.support_offset_z = -7.5
        $renderSupport.remote_players.events[1].trigger_mask = $triggerMask
        $renderSupport.remote_players.manual_captures[0].events[0].trigger_mask = $triggerMask
        $renderSupportPath = Write-Fixture ('remote-render-support-' + $triggerMask + '.json') $renderSupport
        $renderSupportHash = (Get-FileHash -LiteralPath $renderSupportPath).Hash
        $renderSupportText = & $analyzer -ReportPath $renderSupportPath -Json
        $renderSupportFacts = $renderSupportText | ConvertFrom-Json -AsHashtable
        foreach ($event in @($renderSupportFacts.remote_players.events[1],
            $renderSupportFacts.remote_players.manual_captures[0].events[0])) {
            Assert ($event.trigger_mask -eq $triggerMask -and
                ($event.triggers -contains 'render_state_change') -eq [bool]($triggerMask -band 256) -and
                ($event.triggers -contains 'support_change') -eq [bool]($triggerMask -band 512)) 'Render/support trigger bits decoded incorrectly.'
            Assert (($event.triggers -contains 'displacement_threshold') -eq [bool]($triggerMask -band 128)) 'Render/support trigger confused with displacement.'
            Assert ($event.before.raw_render_mode -eq 0 -and $event.before.raw_render_amount -eq 255 -and
                $event.before.raw_render_effect -eq 0 -and $event.before.rendered_opacity -eq 1 -and
                $null -eq $event.before.support_entity -and $null -eq $event.before.support_model -and
                $null -eq $event.before.support_offset_z) 'Before-event render state or unknown support lost.'
        }
        foreach ($sample in @($renderSupportFacts.remote_players.recent_samples[1],
            $renderSupportFacts.remote_players.events[1].current,
            $renderSupportFacts.remote_players.manual_captures[0].samples[1],
            $renderSupportFacts.remote_players.manual_captures[0].events[0].current)) {
            Assert ($sample.raw_render_mode -eq 2 -and $sample.raw_render_amount -eq 64 -and
                $sample.raw_render_effect -eq 3 -and [Math]::Abs($sample.rendered_opacity - 64.0 / 255.0) -lt 0.0000001) 'Raw render amount conflated with normalized rendered opacity.'
            Assert ($sample.support_entity -eq 71 -and $sample.support_model -eq 9 -and
                $sample.support_offset_z -eq -7.5) 'Support identity or signed vertical correction lost.'
        }
        Assert ($renderSupportText -notmatch 'PrivateAssetRoot|secret-resource|fixture-sensitive') 'Render/support diagnostics leaked unapproved fields.'
        Assert ((Get-FileHash -LiteralPath $renderSupportPath).Hash -eq $renderSupportHash) 'Render/support analyzer wrote its input.'
    }
    foreach ($supportOffset in @(-128, 0, 128)) {
        $renderBounds = New-Report; $renderBounds.remote_players = New-RemotePlayers
        $renderBounds.remote_players.recent_samples[1].raw_render_amount = 0
        $renderBounds.remote_players.recent_samples[1].rendered_opacity = 0
        $renderBounds.remote_players.recent_samples[1].support_offset_z = $supportOffset
        $renderBoundsPath = Write-Fixture ('remote-render-support-bound-' + $supportOffset + '.json') $renderBounds
        $renderBoundsFacts = & $analyzer -ReportPath $renderBoundsPath -Json | ConvertFrom-Json -AsHashtable
        $sample = $renderBoundsFacts.remote_players.manual_captures[0].events[0].current
        Assert ($null -ne $sample.raw_render_amount -and $sample.raw_render_amount -eq 0 -and
            $null -ne $sample.rendered_opacity -and $sample.rendered_opacity -eq 0 -and
            $null -ne $sample.support_offset_z -and $sample.support_offset_z -eq $supportOffset) 'Zero opacity or inclusive signed support bounds discarded.'
    }
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
        'zero_observed_distance', 'unknown_observed_distance', 'too_many_comparisons', 'manual_events', 'manual_event_time', 'manual_event_truncated',
        'render_mode_string', 'render_mode_negative', 'render_amount_fraction', 'render_effect_overflow',
        'opacity_negative', 'opacity_over_one', 'opacity_nonfinite', 'support_entity_fraction', 'support_model_overflow',
        'support_offset_low', 'support_offset_high', 'support_offset_string', 'support_offset_nonfinite', 'manual_event_trigger')) {
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
            event_trigger { $section.events[1].trigger_mask = 1024 }
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
            render_mode_string { $section.events[1].current.raw_render_mode = 'fixture-sensitive-value' }
            render_mode_negative { $section.events[1].current.raw_render_mode = -1 }
            render_amount_fraction { $section.events[1].current.raw_render_amount = 64.5 }
            render_effect_overflow { $section.events[1].current.raw_render_effect = 4294967296 }
            opacity_negative { $section.events[1].current.rendered_opacity = -0.01 }
            opacity_over_one { $section.events[1].current.rendered_opacity = 1.01 }
            opacity_nonfinite { $section.events[1].current.rendered_opacity = [double]::NaN }
            support_entity_fraction { $section.events[1].current.support_entity = 7.5 }
            support_model_overflow { $section.events[1].current.support_model = 4294967296 }
            support_offset_low { $section.events[1].current.support_offset_z = -128.01 }
            support_offset_high { $section.events[1].current.support_offset_z = 128.01 }
            support_offset_string { $section.events[1].current.support_offset_z = 'fixture-sensitive-value' }
            support_offset_nonfinite { $section.events[1].current.support_offset_z = [double]::PositiveInfinity }
            manual_event_trigger { $section.manual_captures[0].events[0].trigger_mask = 1024 }
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
            prediction_work = New-PredictionWork
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
    $fpsWork=$fpsFacts.frame_pacing.episodes[0].prediction_work
    Assert ($fpsWork.replay_ms -eq 2.5 -and [decimal]$fpsWork.trace_queries -eq [decimal]9007199254740993 -and
        [decimal]$fpsWork.position_queries -eq [decimal][UInt64]::MaxValue -and $fpsWork.contents_queries -eq 13) 'Worst-frame prediction work lost or integer precision changed.'
    Assert ($fpsText -notmatch 'PrivateAssetRoot|secret-resource|fixture-sensitive|99999') 'FPS report leaked an unapproved field.'
    Assert ((Get-FileHash -LiteralPath $fpsPath).Hash -eq $fpsHash) 'FPS analysis mutated its input.'
    $fpsHuman = (& $analyzer -ReportPath $fpsPath) -join "`n"
    Assert ($fpsHuman -match 'FPS-епізод' -and $fpsHuman -match 'еквівалент' -and $fpsHuman -match 'CPU 27') 'Human FPS evidence absent.'
    $fpsWithoutWork=$fps | ConvertTo-Json -Depth 32 | ConvertFrom-Json -AsHashtable
    $fpsWithoutWork.session_summary.frame_pacing.episodes[0].Remove('prediction_work')
    $fpsWithoutWorkPath=Write-Fixture 'fps-prediction-work-older.json' $fpsWithoutWork
    $fpsWithoutWorkFacts=(& $analyzer -ReportPath $fpsWithoutWorkPath -Json) | ConvertFrom-Json -AsHashtable
    Assert ($null -eq $fpsWithoutWorkFacts.frame_pacing.episodes[0].prediction_work) 'Older frame-pacing episode fabricated prediction work.'
    foreach ($variant in @('bounds','count','reversed','zero','duplicate','end','sensitive','work_shape','work_counter')) {
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
            work_shape { $badFps.session_summary.frame_pacing.episodes[0].prediction_work.unapproved = 'fixture-sensitive-value' }
            work_counter { $badFps.session_summary.frame_pacing.episodes[0].prediction_work.contents_queries = -1 }
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
