#include <hlclient/prediction/prediction_reconciliation.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace hlclient::prediction {
namespace {

[[nodiscard]] PredictionReconciliationResult failed(
    const LocalPredictionHistoryState& history,
    const PredictionErrorCode code,
    const std::string_view context,
    const std::optional<goldsrc::GoldSrcUserCmdSequence> sequence =
        std::nullopt)
{
    PredictionReconciliationResult result;
    try {
        result.history = std::shared_ptr<const LocalPredictionHistoryState>{
            new LocalPredictionHistoryState{history}};
    } catch (const std::bad_alloc&) {
        // Preserve the more useful original error; callers already retain the
        // immutable input history when publication allocation fails.
    }
    result.corrected_current_state = history.current_predicted_state();
    result.error = PredictionError{code, sequence, context};
    return result;
}

[[nodiscard]] bool checked_add(
    std::uint64_t& destination,
    const std::uint64_t value) noexcept
{
    if (destination > UINT64_MAX - value) {
        return false;
    }
    destination += value;
    return true;
}

[[nodiscard]] bool same_collision_session(
    const PredictionSessionIdentity& session,
    const goldsrc::movement::ILocalMovementCollision& collision) noexcept
{
    const auto identity = collision.session_identity();
    return collision.valid() && identity && identity->valid() &&
        collision.profile() == identity->profile &&
        identity->profile == session.collision_profile &&
        identity->collision_world_primary == session.collision_world_primary &&
        identity->collision_world_secondary ==
            session.collision_world_secondary &&
        identity->collision_world_revision ==
            session.collision_world_revision &&
        identity->scene_signature == session.collision_scene_signature;
}

[[nodiscard]] bool hard_reset_session_compatible(
    const PredictionSessionIdentity& previous,
    const PredictionSessionIdentity& replacement) noexcept
{
    return replacement.valid() &&
        replacement.collision_world_primary == previous.collision_world_primary &&
        replacement.collision_world_secondary ==
            previous.collision_world_secondary &&
        replacement.collision_world_revision ==
            previous.collision_world_revision &&
        replacement.collision_scene_signature ==
            previous.collision_scene_signature &&
        replacement.collision_profile == previous.collision_profile &&
        replacement.movement_environment_signature ==
            previous.movement_environment_signature &&
        replacement.movement_config_signature ==
            previous.movement_config_signature &&
        replacement.command_profile == previous.command_profile &&
        replacement.prediction_profile == previous.prediction_profile &&
        replacement.acknowledgement_profile ==
            previous.acknowledgement_profile;
}

[[nodiscard]] bool hard_reset_session_strictly_newer(
    const PredictionSessionIdentity& previous,
    const PredictionSessionIdentity& replacement) noexcept
{
    return replacement.session_generation > previous.session_generation ||
        (replacement.session_generation == previous.session_generation &&
            replacement.prediction_generation >
                previous.prediction_generation);
}

[[nodiscard]] bool same_authority_identity(
    const AuthoritativePlayerUpdateIdentity& left,
    const AuthoritativePlayerUpdateIdentity& right) noexcept
{
    return left.session() == right.session() &&
        left.update_ordinal() == right.update_ordinal() &&
        left.acknowledgement() == right.acknowledgement() &&
        left.synthetic_authority_time_nanoseconds() ==
            right.synthetic_authority_time_nanoseconds() &&
        left.discontinuity() == right.discontinuity();
}

[[nodiscard]] std::optional<PredictionError> validate_runtime(
    const PredictionSessionIdentity& session,
    const goldsrc::movement::GoldSrcMovementEnvironment& environment,
    const goldsrc::movement::ILocalMovementCollision& collision,
    const goldsrc::movement::GoldSrcLocalMovementConfig& movement_config)
    noexcept
{
    if (!same_collision_session(session, collision)) {
        return PredictionError{PredictionErrorCode::collision_world_mismatch,
            std::nullopt,
            "active collision world does not match prediction session"};
    }
    if (prediction_movement_environment_signature(environment) !=
        session.movement_environment_signature) {
        return PredictionError{
            PredictionErrorCode::movement_environment_mismatch, std::nullopt,
            "movement environment does not match prediction session"};
    }
    if (prediction_movement_config_signature(movement_config) !=
        session.movement_config_signature) {
        return PredictionError{PredictionErrorCode::movement_config_mismatch,
            std::nullopt,
            "movement configuration does not match prediction session"};
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<PredictionError> validate_authority_collision(
    const AuthoritativePlayerState& authoritative,
    const goldsrc::movement::ILocalMovementCollision& collision,
    goldsrc::movement::GoldSrcLocalMovementScratch& scratch,
    const goldsrc::movement::GoldSrcLocalMovementConfig& movement_config)
{
    const auto profile = collision.profile();
    const auto tested = collision.test_position(
        authoritative.movement_state().origin(),
        authoritative.movement_state().hull(), scratch.collision,
        movement_config.collision_query);
    if (!tested || !tested.result) {
        return PredictionError{PredictionErrorCode::invalid_authoritative_state,
            std::nullopt,
            "authoritative collision validation query failed"};
    }
    const auto valid_profile = [&profile]() noexcept {
        switch (profile) {
        case goldsrc::movement::LocalMovementCollisionProfile::world_only_v1:
        case goldsrc::movement::LocalMovementCollisionProfile::
                explicit_synthetic_static_brush_v1:
            return true;
        }
        return false;
    };
    const auto valid_contents = [](const auto& contents) noexcept {
        using Contents = movement::PlayerMovementContents;
        switch (contents.category) {
        case Contents::empty: return contents.source_goldsrc_code == -1;
        case Contents::solid: return contents.source_goldsrc_code == -2;
        case Contents::water: return contents.source_goldsrc_code == -3;
        case Contents::slime: return contents.source_goldsrc_code == -4;
        case Contents::lava: return contents.source_goldsrc_code == -5;
        case Contents::current:
            return contents.source_goldsrc_code >= -14 &&
                contents.source_goldsrc_code <= -9;
        case Contents::sky: return contents.source_goldsrc_code == -6;
        case Contents::special:
            return contents.source_goldsrc_code == -7 ||
                contents.source_goldsrc_code == -8 ||
                contents.source_goldsrc_code == -15;
        }
        return false;
    };
    const auto valid_hit = [&profile](const auto& hit) noexcept {
        switch (hit.kind) {
        case movement::PlayerMovementHitKind::world:
            return hit.source_model_index == 0U &&
                !hit.stable_instance_ordinal && !hit.source_entity_index;
        case movement::PlayerMovementHitKind::explicit_synthetic_brush:
            return profile == goldsrc::movement::LocalMovementCollisionProfile::
                                  explicit_synthetic_static_brush_v1 &&
                hit.source_model_index != 0U &&
                hit.stable_instance_ordinal.has_value();
        }
        return false;
    };
    const auto& position = *tested.result;
    bool valid_status = false;
    switch (position.status) {
    case goldsrc::movement::LocalMovementPositionStatus::free:
    case goldsrc::movement::LocalMovementPositionStatus::blocking:
        valid_status = true;
        break;
    }
    if (!valid_profile() || !valid_status ||
        !valid_contents(position.contents) ||
        (position.hit && !valid_hit(*position.hit)) ||
        (position.status ==
                goldsrc::movement::LocalMovementPositionStatus::free &&
            position.hit) ||
        position.traversal_depth > movement_config.collision_query.query_limits.
                maximum_traversal_steps) {
        return PredictionError{PredictionErrorCode::invalid_authoritative_state,
            std::nullopt,
            "authoritative collision validation returned malformed data"};
    }
    if (position.status ==
        goldsrc::movement::LocalMovementPositionStatus::blocking) {
        return PredictionError{PredictionErrorCode::authoritative_state_blocking,
            std::nullopt,
            "authoritative state begins in blocking collision"};
    }
    const auto unsupported_liquid = [](const auto contents) noexcept {
        using Contents = movement::PlayerMovementContents;
        return contents == Contents::water || contents == Contents::slime ||
            contents == Contents::lava || contents == Contents::current;
    };
    if (unsupported_liquid(position.contents.category)) {
        return PredictionError{PredictionErrorCode::invalid_authoritative_state,
            std::nullopt,
            "authoritative state occupies unsupported liquid contents"};
    }
    if (position.contents.category !=
        authoritative.movement_state().last_valid_contents()) {
        return PredictionError{PredictionErrorCode::invalid_authoritative_state,
            std::nullopt,
            "authoritative contents do not match the active collision world"};
    }
    return std::nullopt;
}

[[nodiscard]] PredictionCorrectionClass classify_correction(
    const PredictionErrorMetrics& metrics,
    const AuthoritativePlayerDiscontinuity discontinuity,
    const PredictionStateComparisonConfig& config) noexcept
{
    if (discontinuity == AuthoritativePlayerDiscontinuity::teleport) {
        return PredictionCorrectionClass::teleport_snap;
    }
    if (discontinuity ==
        AuthoritativePlayerDiscontinuity::respawn_or_hard_reset) {
        return PredictionCorrectionClass::hard_reset;
    }
    if (metrics.exact_physical_state_match) {
        return PredictionCorrectionClass::exact;
    }
    if (metrics.hull_mismatch || metrics.mode_mismatch ||
        metrics.contents_mismatch || metrics.grounded_mismatch ||
        metrics.position_error_magnitude >=
            config.large_correction_snap_threshold) {
        return PredictionCorrectionClass::large_snap;
    }
    if (metrics.position_error_magnitude <=
        config.visual_no_offset_epsilon) {
        return PredictionCorrectionClass::replay_without_visual_offset;
    }
    if (metrics.position_error_magnitude <=
        config.small_correction_maximum) {
        return PredictionCorrectionClass::small_visual_correction;
    }
    return PredictionCorrectionClass::large_snap;
}

} // namespace

PredictionReconciliationResult LocalPlayerPredictionReconciler::reconcile(
    const LocalPredictionHistoryState& history,
    const AuthoritativePlayerState& authoritative,
    const goldsrc::movement::GoldSrcMovementEnvironment& environment,
    const goldsrc::movement::ILocalMovementCollision& collision,
    goldsrc::movement::GoldSrcLocalMovementScratch& scratch,
    const goldsrc::movement::GoldSrcLocalMovementConfig& movement_config,
    const PredictionReconciliationConfig& config)
{
    if (!valid_prediction_state_comparison_config(config.comparison) ||
        !valid_prediction_reconciliation_limits(config.limits) ||
        !goldsrc::movement::valid_goldsrc_local_movement_config(
            movement_config)) {
        return failed(history, PredictionErrorCode::invalid_configuration,
            "prediction reconciliation configuration is invalid");
    }
    const auto discontinuity =
        authoritative.update_identity().discontinuity();
    const auto hard_reset_update = discontinuity ==
        AuthoritativePlayerDiscontinuity::respawn_or_hard_reset;
    const auto& authority_identity = authoritative.update_identity();
    const auto& authority_session = authority_identity.session();
    const auto same_session = authority_session == history.session();
    const auto compatible_reset_session = hard_reset_update &&
        hard_reset_session_compatible(history.session(), authority_session);
    if ((!hard_reset_update && !same_session) ||
        (hard_reset_update && !same_session && !compatible_reset_session)) {
        return failed(history, PredictionErrorCode::prediction_session_mismatch,
            "authoritative update belongs to another prediction session");
    }
    if (authority_identity.update_ordinal() >
        config.limits.maximum_authoritative_updates) {
        return failed(history,
            PredictionErrorCode::prediction_replay_limit_exceeded,
            "authority update ordinal exceeds the configured bound");
    }
    if (const auto runtime = validate_runtime(
            authority_session, environment, collision, movement_config)) {
        return failed(history, runtime->code, runtime->context);
    }
    if (const auto authority_error = validate_authority_collision(
            authoritative, collision, scratch, movement_config)) {
        return failed(
            history, authority_error->code, authority_error->context);
    }

    const auto& accepted_identity =
        history.anchor().authority_update_identity();
    const auto same_accepted_state =
        history.anchor().authority_state_signature() &&
        *history.anchor().authority_state_signature() ==
            authoritative.state_signature();
    if (accepted_identity && same_accepted_state &&
        same_authority_identity(*accepted_identity, authority_identity)) {
        PredictionReconciliationResult duplicate;
        try {
            duplicate.history =
                std::shared_ptr<const LocalPredictionHistoryState>{
                    new LocalPredictionHistoryState{history}};
        } catch (const std::bad_alloc&) {
            return failed(history, PredictionErrorCode::allocation_failed,
                "duplicate authority publication allocation failed");
        }
        duplicate.corrected_current_state = history.current_predicted_state();
        duplicate.duplicate_ignored = true;
        return duplicate;
    }

    // Update ordinals are scoped to one session. A newer hard-reset session
    // may restart its ordinal sequence, but once that reset is accepted the
    // same session/ordinal cannot carry a different state.
    if (hard_reset_update && accepted_identity &&
        accepted_identity->session() == authority_session &&
        accepted_identity->update_ordinal() ==
            authority_identity.update_ordinal()) {
        return failed(history,
            PredictionErrorCode::conflicting_authoritative_update,
            "hard-reset authority conflicts with an accepted identity");
    }

    if (hard_reset_update) {
        if (!compatible_reset_session ||
            !hard_reset_session_strictly_newer(
                history.session(), authority_session)) {
            return failed(history,
                PredictionErrorCode::hard_reset_generation_required,
                "hard reset requires a distinct compatible prediction generation");
        }
        if (authority_session.spawn_initial_state_signature !=
            authoritative.state_signature()) {
            return failed(history,
                PredictionErrorCode::hard_reset_generation_required,
                "hard-reset state does not match the replacement session spawn identity");
        }
        try {
            auto state =
                std::make_shared<const movement::LocalPlayerMovementState>(
                    authoritative.movement_state());
            PredictionHistoryAnchor anchor{state, authority_identity,
                authoritative.state_signature(), authority_session};
            auto statistics = history.statistics();
            if (statistics.publication_count == UINT64_MAX ||
                statistics.total_acknowledged_commands >
                    UINT64_MAX - static_cast<std::uint64_t>(history.size()) ||
                history.revision() >=
                    history.limits().maximum_history_revision ||
                history.revision() >=
                    config.limits.maximum_prediction_revision) {
                return failed(history, PredictionErrorCode::revision_exhausted,
                    "hard-reset prediction counters are exhausted");
            }
            constexpr auto state_bytes =
                sizeof(movement::LocalPlayerMovementState);
            if (state_bytes >
                    history.limits().maximum_retained_state_bytes ||
                state_bytes > config.limits.maximum_reconciliation_bytes) {
                return failed(history,
                    PredictionErrorCode::prediction_history_full,
                    "hard-reset prediction exceeds retained byte limits");
            }
            ++statistics.publication_count;
            statistics.total_acknowledged_commands +=
                static_cast<std::uint64_t>(history.size());
            auto replacement =
                std::shared_ptr<const LocalPredictionHistoryState>{
                    new LocalPredictionHistoryState{std::move(anchor), {},
                        state, history.revision() + 1U,
                        state_bytes, 0U, 0U,
                        history.limits(), statistics}};
            PredictionReconciliationResult result;
            result.history = std::move(replacement);
            result.corrected_current_state = std::move(state);
            result.correction_class = PredictionCorrectionClass::hard_reset;
            result.history_changed = true;
            return result;
        } catch (const std::bad_alloc&) {
            return failed(history, PredictionErrorCode::allocation_failed,
                "hard-reset prediction publication allocation failed");
        }
    }

    const auto authority_ordinal = authority_identity.update_ordinal();
    if (const auto accepted = history.anchor().authority_update_ordinal()) {
        if (authority_ordinal < *accepted) {
            if (!config.ignore_stale_updates) {
                return failed(history,
                    PredictionErrorCode::stale_authoritative_update,
                    "authority update ordinal moved backwards");
            }
            PredictionReconciliationResult stale;
            try {
                stale.history = std::shared_ptr<
                    const LocalPredictionHistoryState>{
                    new LocalPredictionHistoryState{history}};
            } catch (const std::bad_alloc&) {
                return failed(history, PredictionErrorCode::allocation_failed,
                    "stale authority publication allocation failed");
            }
            stale.corrected_current_state = history.current_predicted_state();
            stale.stale_ignored = true;
            return stale;
        }
        if (authority_ordinal == *accepted) {
            return failed(history,
                PredictionErrorCode::conflicting_authoritative_update,
                "authority update conflicts with an accepted identity");
        }
    }

    const auto accepted_acknowledgement =
        history.anchor().acknowledgement().sequence();
    const auto incoming_acknowledgement =
        authoritative.acknowledgement().sequence();
    if (accepted_acknowledgement && incoming_acknowledgement &&
        incoming_acknowledgement->value() <
            accepted_acknowledgement->value()) {
        if (!config.ignore_stale_updates) {
            return failed(history,
                PredictionErrorCode::stale_authoritative_update,
                "authority acknowledgement moved backwards",
                *incoming_acknowledgement);
        }
        PredictionReconciliationResult stale;
        try {
            stale.history =
                std::shared_ptr<const LocalPredictionHistoryState>{
                    new LocalPredictionHistoryState{history}};
        } catch (const std::bad_alloc&) {
            return failed(history, PredictionErrorCode::allocation_failed,
                "stale acknowledgement publication allocation failed",
                *incoming_acknowledgement);
        }
        stale.corrected_current_state = history.current_predicted_state();
        stale.stale_ignored = true;
        return stale;
    }
    if (accepted_acknowledgement && incoming_acknowledgement &&
        *accepted_acknowledgement == *incoming_acknowledgement &&
        !same_accepted_state) {
        return failed(history,
            PredictionErrorCode::conflicting_authoritative_update,
            "authority state conflicts with an accepted acknowledgement");
    }

    if (!authoritative.acknowledgement().sequence()) {
        return failed(history,
            PredictionErrorCode::authoritative_acknowledgement_missing,
            "normal authority update has no exact acknowledgement");
    }
    const auto acknowledged = *authoritative.acknowledgement().sequence();
    const movement::LocalPlayerMovementState* acknowledged_state = nullptr;
    const auto anchor_sequence =
        history.anchor().acknowledgement().sequence();
    if (anchor_sequence && acknowledged.value() < anchor_sequence->value()) {
        return failed(history, PredictionErrorCode::acknowledgement_evicted,
            "authority acknowledgement is older than the retained anchor",
            acknowledged);
    }
    if (anchor_sequence && acknowledged == *anchor_sequence) {
        acknowledged_state = history.anchor().movement_state().get();
    } else {
        const auto newest = history.newest_command_sequence();
        if (!newest || acknowledged.value() > newest->value()) {
            return failed(history, PredictionErrorCode::future_acknowledgement,
                "authority acknowledgement is newer than predicted history",
                acknowledged);
        }
        const auto* acknowledged_entry = history.find_exact(acknowledged);
        if (acknowledged_entry == nullptr) {
            return failed(history, PredictionErrorCode::acknowledgement_missing,
                "exact acknowledged command is absent from prediction history",
                acknowledged);
        }
        acknowledged_state = acknowledged_entry->post_command_state().get();
    }

    auto acknowledgement_comparison = compare_prediction_states(
        *acknowledged_state, authoritative.movement_state(), config.comparison);
    if (!acknowledgement_comparison ||
        !acknowledgement_comparison.metrics) {
        return failed(history, PredictionErrorCode::invalid_authoritative_state,
            "authoritative state comparison failed", acknowledged);
    }
    if (discontinuity == AuthoritativePlayerDiscontinuity::normal &&
        (acknowledgement_comparison.metrics->position_error_magnitude >
                config.comparison.
                    maximum_acceptable_authoritative_position_error ||
            acknowledgement_comparison.metrics->velocity_error_magnitude >
                config.comparison.
                    maximum_acceptable_authoritative_velocity_error)) {
        return failed(history, PredictionErrorCode::invalid_authoritative_state,
            "normal authority correction exceeds configured safety bounds",
            acknowledged);
    }

    const auto first_unacknowledged = std::find_if(
        history.entries().begin(), history.entries().end(),
        [acknowledged](const PredictedCommandEntry& entry) {
            return entry.command_sequence().value() > acknowledged.value();
        });
    const auto replay_depth = static_cast<std::size_t>(
        std::distance(first_unacknowledged, history.entries().end()));
    const auto exact_fast_path =
        acknowledgement_comparison.metrics->exact_physical_state_match &&
        discontinuity == AuthoritativePlayerDiscontinuity::normal;
    if (!exact_fast_path &&
        (replay_depth > config.limits.maximum_replay_commands ||
            replay_depth > history.limits().maximum_replay_commands)) {
        return failed(history,
            PredictionErrorCode::prediction_replay_limit_exceeded,
            "unacknowledged command replay exceeds configured limit",
            acknowledged);
    }
    std::uint64_t replay_time_nanoseconds = 0U;
    if (!exact_fast_path) {
        for (auto iterator = first_unacknowledged;
             iterator != history.entries().end(); ++iterator) {
            const auto duration =
                iterator->command()->sample_duration_nanoseconds();
            if (replay_time_nanoseconds > UINT64_MAX - duration ||
                replay_time_nanoseconds + duration >
                    config.limits.maximum_replay_time_nanoseconds) {
                return failed(history,
                    PredictionErrorCode::prediction_replay_limit_exceeded,
                    "retained commands exceed the replay-time bound",
                    iterator->command_sequence());
            }
            replay_time_nanoseconds += duration;
        }
    }

    try {
        auto authoritative_state =
            std::make_shared<const movement::LocalPlayerMovementState>(
                authoritative.movement_state());
        PredictionHistoryAnchor anchor{authoritative_state, authority_identity,
            authoritative.state_signature(), history.session()};
        std::vector<PredictedCommandEntry> replacement_entries;
        replacement_entries.reserve(replay_depth);
        auto current_state = authoritative_state;
        PredictionReplayStatistics replay_statistics;
        std::uint32_t expected = acknowledged.value() == UINT32_MAX
            ? 0U
            : acknowledged.value() + 1U;
        for (auto iterator = first_unacknowledged;
             iterator != history.entries().end(); ++iterator) {
            const auto sequence = iterator->command_sequence();
            if (expected == 0U || sequence.value() != expected) {
                return failed(history, PredictionErrorCode::prediction_command_gap,
                    "retained replay commands are not contiguous", sequence);
            }
            if (exact_fast_path) {
                const auto continuity = compare_prediction_states(
                    *iterator->pre_command_state(), *current_state,
                    config.comparison);
                if (!continuity || !continuity.metrics ||
                    !continuity.metrics->exact_physical_state_match) {
                    return failed(history,
                        PredictionErrorCode::prediction_command_gap,
                        "fast-path retained state continuity is invalid",
                        sequence);
                }
                auto post_state = iterator->post_command_state();
                if (iterator->pre_state_signature() !=
                    movement::local_player_movement_state_signature(
                        *current_state)) {
                    if (current_state->state_revision() == UINT64_MAX) {
                        return failed(history,
                            PredictionErrorCode::revision_exhausted,
                            "fast-path state revision is exhausted", sequence);
                    }
                    auto post_info =
                        movement::local_player_movement_state_create_info(
                            *post_state);
                    post_info.state_revision =
                        current_state->state_revision() + 1U;
                    auto normalized =
                        movement::LocalPlayerMovementState::create(
                            post_info, movement_config.state_limits);
                    if (!normalized || !normalized.state) {
                        return failed(history,
                            PredictionErrorCode::non_finite_result,
                            "fast-path state metadata normalization failed",
                            sequence);
                    }
                    post_state = std::make_shared<
                        const movement::LocalPlayerMovementState>(
                            std::move(*normalized.state));
                }
                replacement_entries.push_back(PredictedCommandEntry{
                    iterator->command(), current_state, post_state,
                    iterator->simulation_statistics(),
                    iterator->touch_summary(),
                    history.session().prediction_generation,
                    iterator->entry_ordinal()});
                current_state = std::move(post_state);
            } else {
                auto simulated =
                    goldsrc::movement::GoldSrcLocalMovementKernel::simulate(
                        *current_state, *iterator->command(), environment,
                        collision, scratch, movement_config);
                if (!simulated || !simulated.state) {
                    return failed(history,
                        PredictionErrorCode::prediction_replay_failed,
                        "retained command movement replay failed", sequence);
                }
                if (simulated.statistics.substep_count >
                        config.limits.maximum_replay_substeps -
                            replay_statistics.substep_count ||
                    simulated.statistics.trace_count >
                        config.limits.maximum_replay_traces -
                            replay_statistics.trace_count ||
                    simulated.touches.size() >
                        config.limits.maximum_replay_touches -
                            replay_statistics.touch_count) {
                    return failed(history,
                        PredictionErrorCode::prediction_replay_limit_exceeded,
                        "movement replay work limit exceeded", sequence);
                }
                replay_statistics.substep_count +=
                    simulated.statistics.substep_count;
                replay_statistics.trace_count += simulated.statistics.trace_count;
                replay_statistics.touch_count += simulated.touches.size();
                auto post_state =
                    std::make_shared<const movement::LocalPlayerMovementState>(
                        std::move(*simulated.state));
                replacement_entries.push_back(PredictedCommandEntry{
                    iterator->command(),
                    current_state, post_state, simulated.statistics,
                    summarize_prediction_touches(simulated.touches,
                        simulated.statistics.start_solid_count != 0U,
                        simulated.statistics.all_solid_count != 0U),
                    history.session().prediction_generation,
                    iterator->entry_ordinal()});
                current_state = std::move(post_state);
                if (!replay_statistics.first_sequence) {
                    replay_statistics.first_sequence = sequence;
                }
                replay_statistics.last_sequence = sequence;
                ++replay_statistics.replayed_command_count;
            }
            expected = sequence.value() == UINT32_MAX
                ? 0U
                : sequence.value() + 1U;
        }

        const auto current_comparison = compare_prediction_states(
            *history.current_predicted_state(), *current_state,
            config.comparison);
        if (!current_comparison || !current_comparison.metrics) {
            return failed(history, PredictionErrorCode::non_finite_result,
                "current prediction correction comparison failed");
        }
        auto correction = exact_fast_path
            ? PredictionCorrectionClass::exact
            : classify_correction(
                  *current_comparison.metrics, discontinuity, config.comparison);
        if (!exact_fast_path &&
            correction == PredictionCorrectionClass::exact) {
            correction =
                PredictionCorrectionClass::replay_without_visual_offset;
        }
        auto statistics = history.statistics();
        const auto acknowledged_count = static_cast<std::uint64_t>(
            history.size() - replay_depth);
        if (!checked_add(
                statistics.total_acknowledged_commands, acknowledged_count) ||
            !checked_add(statistics.total_replayed_commands,
                static_cast<std::uint64_t>(
                    replay_statistics.replayed_command_count)) ||
            statistics.publication_count == UINT64_MAX ||
            history.revision() >= history.limits().maximum_history_revision ||
            history.revision() >= config.limits.maximum_prediction_revision) {
            return failed(history, PredictionErrorCode::revision_exhausted,
                "reconciled prediction history revision is exhausted");
        }
        ++statistics.publication_count;
        const auto state_bytes = sizeof(movement::LocalPlayerMovementState) +
            replacement_entries.size() *
                sizeof(movement::LocalPlayerMovementState) * 2U;
        const auto command_bytes = replacement_entries.size() *
            sizeof(goldsrc::GoldSrcUserCmdState);
        std::size_t touch_bytes = 0U;
        for (const auto& entry : replacement_entries) {
            if (touch_bytes > (std::numeric_limits<std::size_t>::max)() -
                    entry.touch_summary().accounted_bytes) {
                return failed(history, PredictionErrorCode::allocation_failed,
                    "reconciled touch accounting overflowed");
            }
            touch_bytes += entry.touch_summary().accounted_bytes;
        }
        if (state_bytes > history.limits().maximum_retained_state_bytes ||
            command_bytes > history.limits().maximum_retained_command_bytes ||
            touch_bytes > history.limits().maximum_touch_summary_bytes ||
            state_bytes + command_bytes + touch_bytes >
                config.limits.maximum_reconciliation_bytes) {
            return failed(history,
                PredictionErrorCode::prediction_history_full,
                "reconciled prediction history exceeds retained byte limits");
        }
        auto replacement = std::shared_ptr<const LocalPredictionHistoryState>{
            new LocalPredictionHistoryState{std::move(anchor),
                std::move(replacement_entries), current_state,
                history.revision() + 1U, state_bytes, command_bytes,
                touch_bytes, history.limits(), statistics}};
        PredictionReconciliationResult result;
        result.history = std::move(replacement);
        result.corrected_current_state = std::move(current_state);
        result.acknowledgement_metrics = *acknowledgement_comparison.metrics;
        result.current_correction_metrics = *current_comparison.metrics;
        result.correction_class = correction;
        result.replay_statistics = replay_statistics;
        result.history_changed = true;
        return result;
    } catch (const std::bad_alloc&) {
        return failed(history, PredictionErrorCode::allocation_failed,
            "prediction reconciliation allocation failed", acknowledged);
    }
}

} // namespace hlclient::prediction
