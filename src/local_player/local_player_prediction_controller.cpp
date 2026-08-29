#include <hlclient/local_player/local_player_prediction_controller.hpp>

#include <algorithm>
#include <new>
#include <utility>
#include <vector>

namespace hlclient::local_player {
namespace {

void increment(std::uint64_t& value) noexcept
{
    if (value != UINT64_MAX) {
        ++value;
    }
}

void append_event(LocalPlayerPredictionEventBatch& batch,
    const LocalPlayerPredictionEvent& event) noexcept
{
    // The fixed batch has room for the hard maximum replay depth plus every
    // lifecycle event emitted by one controller operation.
    const auto appended = batch.append(event);
    (void)appended;
}

[[nodiscard]] LocalPlayerPredictionEvent failure_event(
    const std::optional<goldsrc::GoldSrcUserCmdSequence> sequence =
        std::nullopt,
    const std::uint64_t authority_update_ordinal = 0U) noexcept
{
    return {LocalPlayerPredictionEventType::reconciliation_failed, sequence,
        authority_update_ordinal};
}

[[nodiscard]] assets::AssetVector3 eye_position(
    const movement::LocalPlayerMovementState& state) noexcept
{
    return {
        state.origin().x + state.view_offset().x,
        state.origin().y + state.view_offset().y,
        state.origin().z + state.view_offset().z,
    };
}

[[nodiscard]] bool collision_matches_session(
    const prediction::PredictionSessionIdentity& session,
    const goldsrc::movement::ILocalMovementCollision& collision) noexcept
{
    const auto identity = collision.session_identity();
    return collision.valid() && identity.has_value() && identity->valid() &&
        collision.profile() == identity->profile &&
        identity->profile == session.collision_profile &&
        identity->collision_world_primary == session.collision_world_primary &&
        identity->collision_world_secondary ==
            session.collision_world_secondary &&
        identity->collision_world_revision ==
            session.collision_world_revision &&
        identity->scene_signature == session.collision_scene_signature;
}

[[nodiscard]] bool camera_collision_query_matches_session(
    const prediction::PredictionSessionIdentity& session,
    const collision::CollisionWorldQuery* const query) noexcept
{
    if (query == nullptr || query->package() == nullptr) {
        return false;
    }
    const goldsrc::movement::WorldOnlyMovementCollision world{
        query->package()};
    const auto identity = world.session_identity();
    return world.valid() && identity.has_value() && identity->valid() &&
        world.profile() == identity->profile &&
        identity->profile == session.collision_profile &&
        identity->collision_world_primary == session.collision_world_primary &&
        identity->collision_world_secondary ==
            session.collision_world_secondary &&
        identity->collision_world_revision == session.collision_world_revision &&
        identity->scene_signature == session.collision_scene_signature;
}

[[nodiscard]] std::optional<prediction::PredictionError>
preflight_active_visual_publication(
    const prediction::PredictionVisualCorrectionState& correction,
    const gameplay_camera::GameplayCameraState& candidate_physical_camera,
    const std::uint64_t maximum_camera_revision) noexcept
{
    if (!correction.active()) {
        return std::nullopt;
    }
    // A nonzero residual has at least one content-changing publication left:
    // either the candidate physical camera moves beneath the residual, or the
    // residual itself advances toward zero on the next sample. Reserve that
    // revision before movement or history is committed.
    const auto publication_base = (std::max)(
        candidate_physical_camera.revision(),
        correction.camera_publication_revision());
    if (maximum_camera_revision == 0U ||
        publication_base >= maximum_camera_revision) {
        return prediction::PredictionError{
            prediction::PredictionErrorCode::revision_exhausted,
            std::nullopt,
            "active visual correction has no camera publication revision remaining"};
    }
    return std::nullopt;
}

} // namespace

std::string_view to_string(
    const LocalPlayerPredictionControllerStatus status) noexcept
{
    switch (status) {
    case LocalPlayerPredictionControllerStatus::idle: return "idle";
    case LocalPlayerPredictionControllerStatus::predicting: return "predicting";
    case LocalPlayerPredictionControllerStatus::awaiting_authority:
        return "awaiting_authority";
    case LocalPlayerPredictionControllerStatus::reconciling:
        return "reconciling";
    case LocalPlayerPredictionControllerStatus::replay_complete:
        return "replay_complete";
    case LocalPlayerPredictionControllerStatus::visual_correction_active:
        return "visual_correction_active";
    case LocalPlayerPredictionControllerStatus::history_backpressure:
        return "history_backpressure";
    case LocalPlayerPredictionControllerStatus::authority_pending:
        return "authority_pending";
    case LocalPlayerPredictionControllerStatus::authority_stale:
        return "authority_stale";
    case LocalPlayerPredictionControllerStatus::authority_conflict:
        return "authority_conflict";
    case LocalPlayerPredictionControllerStatus::replay_failed:
        return "replay_failed";
    case LocalPlayerPredictionControllerStatus::hard_reset: return "hard_reset";
    case LocalPlayerPredictionControllerStatus::stock_evidence_pending:
        return "stock_evidence_pending";
    case LocalPlayerPredictionControllerStatus::cancelled: return "cancelled";
    case LocalPlayerPredictionControllerStatus::failed: return "failed";
    }
    return "unknown";
}

std::string_view to_string(const LocalPlayerPredictionEventType type) noexcept
{
    switch (type) {
    case LocalPlayerPredictionEventType::none: return "none";
    case LocalPlayerPredictionEventType::predicted_command_appended:
        return "predicted_command_appended";
    case LocalPlayerPredictionEventType::prediction_history_advanced:
        return "prediction_history_advanced";
    case LocalPlayerPredictionEventType::authoritative_update_received:
        return "authoritative_update_received";
    case LocalPlayerPredictionEventType::authoritative_update_ignored_stale:
        return "authoritative_update_ignored_stale";
    case LocalPlayerPredictionEventType::acknowledgement_accepted:
        return "acknowledgement_accepted";
    case LocalPlayerPredictionEventType::misprediction_measured:
        return "misprediction_measured";
    case LocalPlayerPredictionEventType::command_replay_started:
        return "command_replay_started";
    case LocalPlayerPredictionEventType::command_replayed:
        return "command_replayed";
    case LocalPlayerPredictionEventType::command_replay_completed:
        return "command_replay_completed";
    case LocalPlayerPredictionEventType::history_trimmed:
        return "history_trimmed";
    case LocalPlayerPredictionEventType::visual_correction_started:
        return "visual_correction_started";
    case LocalPlayerPredictionEventType::visual_correction_active:
        return "visual_correction_active";
    case LocalPlayerPredictionEventType::visual_correction_constrained:
        return "visual_correction_constrained";
    case LocalPlayerPredictionEventType::visual_correction_completed:
        return "visual_correction_completed";
    case LocalPlayerPredictionEventType::hard_reset_applied:
        return "hard_reset_applied";
    case LocalPlayerPredictionEventType::prediction_backpressure:
        return "prediction_backpressure";
    case LocalPlayerPredictionEventType::reconciliation_failed:
        return "reconciliation_failed";
    }
    return "unknown";
}

LocalPlayerPredictionController::LocalPlayerPredictionController(
    LocalPlayerMovementController movement_controller,
    prediction::PredictionSessionIdentity session,
    std::shared_ptr<const prediction::LocalPredictionHistoryState> history,
    LocalPlayerPredictionControllerConfig config) noexcept
    : movement_controller_{std::move(movement_controller)},
      session_{session},
      history_{std::move(history)},
      config_{config}
{
}

LocalPlayerPredictionController::CreateResult
LocalPlayerPredictionController::create(
    LocalPlayerMovementController movement_controller,
    prediction::PredictionSessionIdentity session,
    LocalPlayerPredictionControllerConfig config)
{
    if (session.prediction_profile == prediction::PredictionCompatibilityProfile::
            stock_protocol_48_authoritative_reconciliation_evidence_pending ||
        session.acknowledgement_profile ==
            prediction::PredictionAcknowledgementProfile::
                stock_usercmd_acknowledgement_evidence_pending ||
        session.command_profile == movement::GoldSrcMovementCommandProfile::
            stock_usercmd_semantics_evidence_pending) {
        return {nullptr,
            prediction::PredictionError{
                prediction::PredictionErrorCode::stock_evidence_pending,
                std::nullopt,
                "stock authoritative state and acknowledgement semantics are evidence-pending"}};
    }
    if (!movement_controller.valid_configuration() || !session.valid() ||
        !prediction::valid_local_prediction_history_limits(config.history) ||
        !prediction::valid_prediction_reconciliation_limits(
            config.reconciliation.limits) ||
        !prediction::valid_prediction_state_comparison_config(
            config.reconciliation.comparison) ||
        !prediction::valid_prediction_visual_correction_config(config.visual)) {
        return {nullptr,
            prediction::PredictionError{
                prediction::PredictionErrorCode::invalid_configuration,
                std::nullopt,
                "local prediction controller configuration is invalid"}};
    }
    if (prediction::prediction_movement_environment_signature(
            movement_controller.environment()) !=
            session.movement_environment_signature) {
        return {nullptr,
            prediction::PredictionError{
                prediction::PredictionErrorCode::movement_environment_mismatch,
                std::nullopt,
                "movement environment does not match prediction session"}};
    }
    if (prediction::prediction_movement_config_signature(
            movement_controller.config().movement) !=
            session.movement_config_signature) {
        return {nullptr,
            prediction::PredictionError{
                prediction::PredictionErrorCode::movement_config_mismatch,
                std::nullopt,
                "movement configuration does not match prediction session"}};
    }
    if (movement::local_player_movement_state_signature(
            movement_controller.player_state()) !=
            session.spawn_initial_state_signature) {
        return {nullptr,
            prediction::PredictionError{
                prediction::PredictionErrorCode::invalid_session_identity,
                std::nullopt,
                "initial movement state does not match prediction session"}};
    }
    auto history = prediction::LocalPredictionHistoryState::create_initial(
        movement_controller.player_state(), session, config.history);
    if (!history || !history.history) {
        return {nullptr, history.error};
    }
    try {
        return {std::unique_ptr<LocalPlayerPredictionController>{
                    new LocalPlayerPredictionController{
                        std::move(movement_controller), session,
                        std::move(history.history), config}},
            std::nullopt};
    } catch (const std::bad_alloc&) {
        return {nullptr,
            prediction::PredictionError{
                prediction::PredictionErrorCode::allocation_failed,
                std::nullopt,
                "local prediction controller allocation failed"}};
    }
}

const prediction::PredictionSessionIdentity&
LocalPlayerPredictionController::session() const noexcept { return session_; }
const LocalPlayerMovementController&
LocalPlayerPredictionController::movement_controller() const noexcept
{ return movement_controller_; }
const std::shared_ptr<const prediction::LocalPredictionHistoryState>&
LocalPlayerPredictionController::history() const noexcept { return history_; }
LocalPlayerPredictionControllerStatus
LocalPlayerPredictionController::status() const noexcept { return status_; }
const prediction::PredictionVisualCorrectionState&
LocalPlayerPredictionController::visual_correction() const noexcept
{ return *visual_correction_; }
const prediction::LocalPredictionStatistics&
LocalPlayerPredictionController::statistics() const noexcept
{ return statistics_; }

LocalPlayerPredictionOperationResult
LocalPlayerPredictionController::current_result(
    std::optional<prediction::PredictionError> prediction_error,
    std::optional<LocalPlayerMovementControllerError> movement_error,
    std::optional<LocalPlayerPredictionEvent> event,
    LocalPlayerPredictionEventBatch events)
{
    if (events.empty() && event) {
        append_event(events, *event);
    }
    LocalPlayerPredictionOperationResult result;
    result.player_state = history_->current_predicted_state();
    if (visual_correction_->active()) {
        if (visual_correction_->last_published_camera()) {
            result.camera.emplace(
                *visual_correction_->last_published_camera());
        } else {
            result.camera.emplace(movement_controller_.camera());
        }
    } else {
        collision::CollisionQueryScratch publication_scratch;
        auto published = prediction::sample_prediction_visual_correction(
            *visual_correction_, movement_controller_.camera(),
            visual_correction_->last_sample_monotonic_time_seconds(), nullptr,
            publication_scratch, {}, movement_controller_.config().camera
                .maximum_camera_revisions());
        if (published && published.camera && published.correction) {
            result.camera.emplace(std::move(*published.camera));
            visual_correction_.emplace(std::move(*published.correction));
        } else {
            if (visual_correction_->last_published_camera()) {
                result.camera.emplace(
                    *visual_correction_->last_published_camera());
            } else {
                result.camera.emplace(movement_controller_.camera());
            }
            if (!prediction_error) {
                prediction_error = published.error;
            }
            status_ = LocalPlayerPredictionControllerStatus::failed;
            const auto failure = failure_event();
            append_event(events, failure);
            event = failure;
        }
    }
    result.history = history_;
    result.prediction_error = std::move(prediction_error);
    result.movement_error = std::move(movement_error);
    result.events = std::move(events);
    result.event = std::move(event);
    result.status = status_;
    return result;
}

LocalPlayerPredictionOperationResult
LocalPlayerPredictionController::update_local_input(
    const std::int64_t monotonic_time_nanoseconds,
    const gameplay_input::GameplayInputIntent& intent,
    const goldsrc::movement::ILocalMovementCollision& collision,
    goldsrc::movement::GoldSrcLocalMovementScratch& scratch)
{
    if (cancelled_) {
        status_ = LocalPlayerPredictionControllerStatus::cancelled;
        return current_result();
    }
    status_ = LocalPlayerPredictionControllerStatus::predicting;
    movement_controller_.capture_pending_input(intent);
    if (!collision_matches_session(session_, collision)) {
        status_ = LocalPlayerPredictionControllerStatus::failed;
        return current_result(prediction::PredictionError{
            prediction::PredictionErrorCode::collision_world_mismatch,
            std::nullopt,
            "active collision world does not match prediction session"},
            std::nullopt, failure_event());
    }
    auto prepared = movement_controller_.prepare_update(
        monotonic_time_nanoseconds, intent, collision, scratch);
    if (!prepared || !prepared.prepared_update) {
        status_ = LocalPlayerPredictionControllerStatus::failed;
        return current_result(std::nullopt, prepared.preview.error,
            LocalPlayerPredictionEvent{
                LocalPlayerPredictionEventType::reconciliation_failed});
    }
    auto& plan = *prepared.prepared_update;
    const auto maximum_camera_revision =
        movement_controller_.config().camera.maximum_camera_revisions();
    if (const auto error = preflight_active_visual_publication(
            *visual_correction_, plan.final_camera(),
            maximum_camera_revision)) {
        movement_controller_.abandon_prepared_update(plan);
        status_ = LocalPlayerPredictionControllerStatus::failed;
        return current_result(error, std::nullopt, failure_event());
    }
    if (!visual_correction_->active()) {
        collision::CollisionQueryScratch publication_scratch;
        const auto publication_preflight =
            prediction::sample_prediction_visual_correction(
                *visual_correction_, plan.final_camera(),
                visual_correction_->last_sample_monotonic_time_seconds(),
                nullptr, publication_scratch, {},
                maximum_camera_revision);
        if (!publication_preflight) {
            movement_controller_.abandon_prepared_update(plan);
            status_ = LocalPlayerPredictionControllerStatus::failed;
            return current_result(publication_preflight.error, std::nullopt,
                failure_event());
        }
    }
    if (plan.commands().empty()) {
        auto committed = movement_controller_.commit_prepared_update(
            std::move(plan));
        if (!committed) {
            status_ = LocalPlayerPredictionControllerStatus::failed;
            return current_result(std::nullopt, committed.error,
                failure_event());
        }
        status_ = LocalPlayerPredictionControllerStatus::awaiting_authority;
        return current_result();
    }
    try {
        std::vector<prediction::PredictedCommandAppend> appends;
        appends.reserve(plan.commands().size());
        for (const auto& command : plan.commands()) {
            prediction::PredictionTouchSummary touch;
            touch.touch_count = command.touch_summary.touch_count;
            touch.first_hit_kind = command.touch_summary.first_hit_kind;
            touch.last_hit_kind = command.touch_summary.last_hit_kind;
            touch.start_solid = command.statistics.start_solid_count != 0U;
            touch.all_solid = command.statistics.all_solid_count != 0U;
            touch.deterministic_signature =
                command.touch_summary.deterministic_signature;
            touch.accounted_bytes = sizeof(prediction::PredictionTouchSummary);
            appends.push_back(prediction::PredictedCommandAppend{
                command.command, command.pre_state, command.post_state,
                command.statistics, touch});
        }
        auto appended = prediction::append_local_prediction_commands(
            *history_, appends);
        if (!appended || !appended.history) {
            movement_controller_.abandon_prepared_update(plan);
            status_ = appended.error && appended.error->code ==
                    prediction::PredictionErrorCode::
                        prediction_history_backpressure
                ? LocalPlayerPredictionControllerStatus::history_backpressure
                : LocalPlayerPredictionControllerStatus::failed;
            if (status_ ==
                LocalPlayerPredictionControllerStatus::history_backpressure) {
                increment(statistics_.history_backpressure_count);
            }
            return current_result(appended.error, std::nullopt,
                LocalPlayerPredictionEvent{
                    status_ == LocalPlayerPredictionControllerStatus::
                            history_backpressure
                        ? LocalPlayerPredictionEventType::prediction_backpressure
                        : LocalPlayerPredictionEventType::reconciliation_failed});
        }
        if (const auto error =
                movement_controller_.preflight_prepared_update(plan)) {
            movement_controller_.abandon_prepared_update(plan);
            status_ = LocalPlayerPredictionControllerStatus::failed;
            return current_result(std::nullopt, error, failure_event());
        }
        auto committed = movement_controller_.commit_prepared_update(
            std::move(plan));
        if (!committed) {
            status_ = LocalPlayerPredictionControllerStatus::failed;
            return current_result(std::nullopt, committed.error,
                failure_event());
        }
        history_ = std::move(appended.history);
        statistics_.predicted_commands =
            statistics_.predicted_commands >
                    UINT64_MAX - appended.appended_command_count
                ? UINT64_MAX
                : statistics_.predicted_commands +
                    static_cast<std::uint64_t>(
                        appended.appended_command_count);
        statistics_.history_high_water_mark = (std::max)(
            statistics_.history_high_water_mark, history_->size());
        status_ = LocalPlayerPredictionControllerStatus::awaiting_authority;
        const LocalPlayerPredictionEvent principal{
            LocalPlayerPredictionEventType::predicted_command_appended,
            history_->newest_command_sequence(), 0U,
            appended.appended_command_count,
            prediction::PredictionCorrectionClass::exact,
            history_->revision()};
        LocalPlayerPredictionEventBatch events;
        append_event(events, principal);
        append_event(events, LocalPlayerPredictionEvent{
            LocalPlayerPredictionEventType::prediction_history_advanced,
            history_->newest_command_sequence(), 0U,
            appended.appended_command_count,
            prediction::PredictionCorrectionClass::exact,
            history_->revision()});
        auto result = current_result(std::nullopt, std::nullopt, principal,
            std::move(events));
        result.command_count = appended.appended_command_count;
        return result;
    } catch (const std::bad_alloc&) {
        movement_controller_.abandon_prepared_update(plan);
        status_ = LocalPlayerPredictionControllerStatus::failed;
        return current_result(prediction::PredictionError{
            prediction::PredictionErrorCode::allocation_failed, std::nullopt,
            "prediction controller append allocation failed"}, std::nullopt,
            failure_event());
    }
}

LocalPlayerPredictionOperationResult
LocalPlayerPredictionController::apply_authoritative_state(
    const prediction::AuthoritativePlayerState& authoritative,
    const double monotonic_time_seconds,
    const goldsrc::movement::ILocalMovementCollision& collision,
    goldsrc::movement::GoldSrcLocalMovementScratch& scratch)
{
    if (cancelled_) {
        status_ = LocalPlayerPredictionControllerStatus::cancelled;
        return current_result();
    }
    status_ = LocalPlayerPredictionControllerStatus::reconciling;
    increment(statistics_.authoritative_updates);
    const auto authority_sequence = authoritative.acknowledgement().sequence();
    const auto authority_ordinal =
        authoritative.update_identity().update_ordinal();
    LocalPlayerPredictionEventBatch events;
    append_event(events, LocalPlayerPredictionEvent{
        LocalPlayerPredictionEventType::authoritative_update_received,
        authority_sequence, authority_ordinal});
    auto reconciled = prediction::LocalPlayerPredictionReconciler::reconcile(
        *history_, authoritative, movement_controller_.environment(), collision,
        scratch, movement_controller_.config().movement,
        config_.reconciliation);
    if (!reconciled) {
        status_ = reconciled.error && reconciled.error->code ==
                prediction::PredictionErrorCode::
                    conflicting_authoritative_update
            ? LocalPlayerPredictionControllerStatus::authority_conflict
            : reconciled.error && reconciled.error->code ==
                    prediction::PredictionErrorCode::prediction_replay_failed
                ? LocalPlayerPredictionControllerStatus::replay_failed
                : LocalPlayerPredictionControllerStatus::failed;
        if (status_ == LocalPlayerPredictionControllerStatus::authority_conflict) {
            increment(statistics_.conflicts);
        }
        if (status_ == LocalPlayerPredictionControllerStatus::replay_failed) {
            increment(statistics_.replay_failures);
        }
        const auto principal = failure_event(
            authority_sequence, authority_ordinal);
        append_event(events, principal);
        return current_result(reconciled.error, std::nullopt, principal,
            std::move(events));
    }
    if (reconciled.stale_ignored) {
        status_ = LocalPlayerPredictionControllerStatus::authority_stale;
        increment(statistics_.stale_updates);
        const LocalPlayerPredictionEvent principal{
            LocalPlayerPredictionEventType::
                authoritative_update_ignored_stale,
            authority_sequence, authority_ordinal};
        append_event(events, principal);
        return current_result(std::nullopt, std::nullopt, principal,
            std::move(events));
    }
    if (reconciled.duplicate_ignored) {
        status_ = LocalPlayerPredictionControllerStatus::awaiting_authority;
        increment(statistics_.duplicate_updates);
        return current_result(std::nullopt, std::nullopt, std::nullopt,
            std::move(events));
    }
    const auto old_history_size = history_->size();
    const auto old_history_revision = history_->revision();
    const auto old_eye = movement_controller_.camera().position();
    const auto new_eye = eye_position(*reconciled.corrected_current_state);
    auto visual = prediction::begin_prediction_visual_correction(
        visual_correction_,
        old_eye, new_eye, reconciled.correction_class, monotonic_time_seconds,
        authoritative.update_identity().update_ordinal(), history_->revision(),
        reconciled.history->revision(), config_.visual);
    if (!visual || !visual.correction) {
        status_ = LocalPlayerPredictionControllerStatus::failed;
        const auto principal = failure_event(
            authority_sequence, authority_ordinal);
        append_event(events, principal);
        return current_result(visual.error, std::nullopt, principal,
            std::move(events));
    }
    const auto hard_reset = reconciled.correction_class ==
        prediction::PredictionCorrectionClass::hard_reset;
    auto replaced = movement_controller_.replace_simulation_state(
        movement_controller_.revision(), *reconciled.corrected_current_state,
        hard_reset);
    if (!replaced) {
        status_ = LocalPlayerPredictionControllerStatus::failed;
        const auto principal = failure_event(
            authority_sequence, authority_ordinal);
        append_event(events, principal);
        return current_result(std::nullopt, replaced.error, principal,
            std::move(events));
    }
    history_ = std::move(reconciled.history);
    visual_correction_.emplace(std::move(*visual.correction));
    if (hard_reset) {
        session_ = authoritative.update_identity().session();
    }
    if (authority_sequence) {
        increment(statistics_.accepted_acknowledgements);
    }
    increment(statistics_.reconciliation_count);
    if (reconciled.correction_class ==
        prediction::PredictionCorrectionClass::exact) {
        increment(statistics_.exact_match_count);
    } else {
        increment(statistics_.corrected_commands);
    }
    if (reconciled.replay_statistics.replayed_command_count != 0U) {
        increment(statistics_.replay_count);
        statistics_.replayed_command_count =
            statistics_.replayed_command_count > UINT64_MAX -
                    reconciled.replay_statistics.replayed_command_count
                ? UINT64_MAX
                : statistics_.replayed_command_count +
                    static_cast<std::uint64_t>(
                        reconciled.replay_statistics.replayed_command_count);
        statistics_.maximum_replay_depth = (std::max)(
            statistics_.maximum_replay_depth,
            reconciled.replay_statistics.replayed_command_count);
    }
    switch (reconciled.correction_class) {
    case prediction::PredictionCorrectionClass::small_visual_correction:
        increment(statistics_.small_corrections);
        break;
    case prediction::PredictionCorrectionClass::large_snap:
        increment(statistics_.large_snaps);
        break;
    case prediction::PredictionCorrectionClass::teleport_snap:
        increment(statistics_.teleports);
        break;
    case prediction::PredictionCorrectionClass::hard_reset:
        increment(statistics_.hard_resets);
        break;
    case prediction::PredictionCorrectionClass::exact:
    case prediction::PredictionCorrectionClass::replay_without_visual_offset:
        break;
    }
    status_ = hard_reset
        ? LocalPlayerPredictionControllerStatus::hard_reset
        : visual_correction_->active()
            ? LocalPlayerPredictionControllerStatus::visual_correction_active
            : LocalPlayerPredictionControllerStatus::replay_complete;
    if (hard_reset) {
        append_event(events, LocalPlayerPredictionEvent{
            LocalPlayerPredictionEventType::hard_reset_applied,
            std::nullopt, authority_ordinal, old_history_size,
            reconciled.correction_class, history_->revision()});
    } else {
        append_event(events, LocalPlayerPredictionEvent{
            LocalPlayerPredictionEventType::acknowledgement_accepted,
            authority_sequence, authority_ordinal, 0U,
            reconciled.correction_class, history_->revision()});
        if (reconciled.acknowledgement_metrics) {
            append_event(events, LocalPlayerPredictionEvent{
                LocalPlayerPredictionEventType::misprediction_measured,
                authority_sequence, authority_ordinal, 0U,
                reconciled.correction_class, history_->revision()});
        }
        const auto replay_count =
            reconciled.replay_statistics.replayed_command_count;
        if (replay_count != 0U) {
            append_event(events, LocalPlayerPredictionEvent{
                LocalPlayerPredictionEventType::command_replay_started,
                reconciled.replay_statistics.first_sequence,
                authority_ordinal, replay_count,
                reconciled.correction_class, history_->revision()});
            std::size_t emitted_replays = 0U;
            for (const auto& entry : history_->entries()) {
                if (emitted_replays >= replay_count) {
                    break;
                }
                append_event(events, LocalPlayerPredictionEvent{
                    LocalPlayerPredictionEventType::command_replayed,
                    entry.command_sequence(), authority_ordinal, 1U,
                    reconciled.correction_class, history_->revision()});
                ++emitted_replays;
            }
            append_event(events, LocalPlayerPredictionEvent{
                LocalPlayerPredictionEventType::command_replay_completed,
                reconciled.replay_statistics.last_sequence,
                authority_ordinal, replay_count,
                reconciled.correction_class, history_->revision()});
        }
    }
    if (old_history_size > history_->size()) {
        append_event(events, LocalPlayerPredictionEvent{
            LocalPlayerPredictionEventType::history_trimmed,
            authority_sequence, authority_ordinal,
            old_history_size - history_->size(), reconciled.correction_class,
            history_->revision()});
    }
    if (history_->revision() != old_history_revision) {
        append_event(events, LocalPlayerPredictionEvent{
            LocalPlayerPredictionEventType::prediction_history_advanced,
            history_->newest_command_sequence(), authority_ordinal,
            history_->size(), reconciled.correction_class,
            history_->revision()});
    }
    if (visual_correction_->active()) {
        append_event(events, LocalPlayerPredictionEvent{
            LocalPlayerPredictionEventType::visual_correction_started,
            authority_sequence, authority_ordinal, 0U,
            reconciled.correction_class, history_->revision()});
        append_event(events, LocalPlayerPredictionEvent{
            LocalPlayerPredictionEventType::visual_correction_active,
            authority_sequence, authority_ordinal, 0U,
            reconciled.correction_class, history_->revision()});
    }
    const LocalPlayerPredictionEvent principal{
        hard_reset
            ? LocalPlayerPredictionEventType::hard_reset_applied
            : reconciled.replay_statistics.replayed_command_count != 0U
                ? LocalPlayerPredictionEventType::command_replay_completed
                : LocalPlayerPredictionEventType::acknowledgement_accepted,
        authority_sequence, authority_ordinal,
        reconciled.replay_statistics.replayed_command_count,
        reconciled.correction_class, history_->revision()};
    auto result = current_result(std::nullopt, std::nullopt, principal,
        std::move(events));
    result.replay_depth =
        reconciled.replay_statistics.replayed_command_count;
    return result;
}

LocalPlayerPredictionOperationResult
LocalPlayerPredictionController::sample_camera(
    const double monotonic_time_seconds,
    const collision::CollisionWorldQuery* const collision_query,
    collision::CollisionQueryScratch& scratch,
    const collision::CollisionQueryLimits& query_limits)
{
    const auto correction_was_active = visual_correction_->active();
    const auto correction_class = visual_correction_->correction_class();
    const auto authority_ordinal =
        visual_correction_->source_authority_update_ordinal();
    if (visual_correction_->active() &&
        !camera_collision_query_matches_session(session_, collision_query)) {
        status_ = LocalPlayerPredictionControllerStatus::failed;
        return current_result(prediction::PredictionError{
            prediction::PredictionErrorCode::collision_world_mismatch,
            std::nullopt,
            "visual correction collision world does not match prediction session"},
            std::nullopt, failure_event(std::nullopt, authority_ordinal));
    }
    auto sampled = prediction::sample_prediction_visual_correction(
        *visual_correction_, movement_controller_.camera(),
        monotonic_time_seconds, collision_query, scratch, query_limits,
        movement_controller_.config().camera.maximum_camera_revisions());
    if (!sampled || !sampled.camera || !sampled.correction) {
        status_ = LocalPlayerPredictionControllerStatus::failed;
        return current_result(sampled.error, std::nullopt,
            failure_event(std::nullopt, authority_ordinal));
    }
    visual_correction_.emplace(std::move(*sampled.correction));
    if (sampled.constrained) {
        increment(statistics_.constrained_camera_corrections);
    }
    if (sampled.completed) {
        status_ = LocalPlayerPredictionControllerStatus::awaiting_authority;
    }
    LocalPlayerPredictionEventBatch events;
    std::optional<LocalPlayerPredictionEvent> event;
    if (correction_was_active && !sampled.completed) {
        append_event(events, LocalPlayerPredictionEvent{
            LocalPlayerPredictionEventType::visual_correction_active,
            std::nullopt, authority_ordinal, 0U, correction_class,
            history_->revision()});
    }
    if (sampled.constrained) {
        const LocalPlayerPredictionEvent constrained{
            LocalPlayerPredictionEventType::visual_correction_constrained,
            std::nullopt, authority_ordinal, 0U, correction_class,
            history_->revision()};
        append_event(events, constrained);
        event = constrained;
    }
    if (sampled.completed) {
        const LocalPlayerPredictionEvent completed{
            LocalPlayerPredictionEventType::visual_correction_completed,
            std::nullopt, authority_ordinal, 0U, correction_class,
            history_->revision()};
        append_event(events, completed);
        event = completed;
    }
    auto result = current_result(std::nullopt, std::nullopt, std::move(event),
        std::move(events));
    result.camera.reset();
    result.camera.emplace(std::move(*sampled.camera));
    return result;
}

void LocalPlayerPredictionController::cancel() noexcept
{
    cancelled_ = true;
    movement_controller_.discard_pending_input();
    visual_correction_.emplace(
        prediction::PredictionVisualCorrectionState::inactive());
    status_ = LocalPlayerPredictionControllerStatus::cancelled;
}

} // namespace hlclient::local_player
