#include <hlclient/prediction/prediction_types.hpp>

#include <hlclient/goldsrc/movement/local_movement_collision.hpp>

namespace hlclient::prediction {

bool PredictionSessionIdentity::valid() const noexcept
{
    if (session_generation == 0U || prediction_generation == 0U ||
        (collision_world_primary == 0U &&
            collision_world_secondary == 0U) ||
        collision_world_revision == 0U || collision_scene_signature == 0U ||
        movement_environment_signature == 0U ||
        movement_config_signature == 0U ||
        spawn_initial_state_signature == 0U) {
        return false;
    }
    if (collision_profile !=
            goldsrc::movement::LocalMovementCollisionProfile::world_only_v1 &&
        collision_profile != goldsrc::movement::LocalMovementCollisionProfile::
            explicit_synthetic_static_brush_v1) {
        return false;
    }
    if (prediction_profile == PredictionCompatibilityProfile::
            stock_protocol_48_authoritative_reconciliation_evidence_pending ||
        acknowledgement_profile == PredictionAcknowledgementProfile::
            stock_usercmd_acknowledgement_evidence_pending ||
        command_profile == movement::GoldSrcMovementCommandProfile::
            stock_usercmd_semantics_evidence_pending) {
        return false;
    }
    return prediction_profile == PredictionCompatibilityProfile::
               synthetic_authoritative_reconciliation_v1 &&
        acknowledgement_profile == PredictionAcknowledgementProfile::
            synthetic_uint32_non_wrapping_v1 &&
        command_profile == movement::GoldSrcMovementCommandProfile::
            synthetic_usercmd_semantics_v1;
}

bool valid_local_prediction_history_limits(
    const LocalPredictionHistoryLimits& limits) noexcept
{
    return limits.maximum_entries > 0U &&
        limits.maximum_entries <= kHardMaximumPredictionHistoryEntries &&
        limits.maximum_retained_state_bytes > 0U &&
        limits.maximum_retained_command_bytes > 0U &&
        limits.maximum_authority_delay_commands > 0U &&
        limits.maximum_authority_delay_commands <= limits.maximum_entries &&
        limits.maximum_replay_commands > 0U &&
        limits.maximum_replay_commands <= kHardMaximumPredictionReplayCommands &&
        limits.maximum_history_revision > 0U &&
        limits.maximum_touch_summary_bytes > 0U;
}

bool valid_prediction_reconciliation_limits(
    const PredictionReconciliationLimits& limits) noexcept
{
    return limits.maximum_replay_commands > 0U &&
        limits.maximum_replay_commands <= kHardMaximumPredictionReplayCommands &&
        limits.maximum_replay_substeps > 0U &&
        limits.maximum_replay_traces > 0U &&
        limits.maximum_replay_touches > 0U &&
        limits.maximum_authoritative_updates > 0U &&
        limits.maximum_pending_authority_updates > 0U &&
        limits.maximum_reconciliation_bytes > 0U &&
        limits.maximum_reconciliations_per_update > 0U &&
        limits.maximum_replay_time_nanoseconds > 0U &&
        limits.maximum_prediction_revision > 0U;
}

std::string_view to_string(const PredictionCompatibilityProfile profile) noexcept
{
    switch (profile) {
    case PredictionCompatibilityProfile::
            synthetic_authoritative_reconciliation_v1:
        return "synthetic_authoritative_reconciliation_v1";
    case PredictionCompatibilityProfile::
            stock_protocol_48_authoritative_reconciliation_evidence_pending:
        return "stock_protocol_48_authoritative_reconciliation_evidence_pending";
    }
    return "unknown";
}

std::string_view to_string(const PredictionEvidenceProfile profile) noexcept
{
    switch (profile) {
    case PredictionEvidenceProfile::
            project_typed_authoritative_states_and_independent_fixtures:
        return "project_typed_authoritative_states_and_independent_fixtures";
    case PredictionEvidenceProfile::stock_ack_and_player_state_semantics_pending:
        return "stock_ack_and_player_state_semantics_pending";
    }
    return "unknown";
}

std::string_view to_string(const PredictionAcknowledgementProfile profile) noexcept
{
    switch (profile) {
    case PredictionAcknowledgementProfile::synthetic_uint32_non_wrapping_v1:
        return "synthetic_uint32_non_wrapping_v1";
    case PredictionAcknowledgementProfile::
            stock_usercmd_acknowledgement_evidence_pending:
        return "stock_usercmd_acknowledgement_evidence_pending";
    }
    return "unknown";
}

std::string_view to_string(
    const AuthoritativePlayerDiscontinuity discontinuity) noexcept
{
    switch (discontinuity) {
    case AuthoritativePlayerDiscontinuity::normal: return "normal";
    case AuthoritativePlayerDiscontinuity::teleport: return "teleport";
    case AuthoritativePlayerDiscontinuity::respawn_or_hard_reset:
        return "respawn_or_hard_reset";
    }
    return "unknown";
}

std::string_view to_string(const PredictionCorrectionClass correction) noexcept
{
    switch (correction) {
    case PredictionCorrectionClass::exact: return "exact";
    case PredictionCorrectionClass::replay_without_visual_offset:
        return "replay_without_visual_offset";
    case PredictionCorrectionClass::small_visual_correction:
        return "small_visual_correction";
    case PredictionCorrectionClass::large_snap: return "large_snap";
    case PredictionCorrectionClass::teleport_snap: return "teleport_snap";
    case PredictionCorrectionClass::hard_reset: return "hard_reset";
    }
    return "unknown";
}

std::string_view to_string(
    const PredictionVisualCorrectionProfile profile) noexcept
{
    switch (profile) {
    case PredictionVisualCorrectionProfile::
            project_linear_decay_collision_constrained_v1:
        return "project_linear_decay_collision_constrained_v1";
    case PredictionVisualCorrectionProfile::no_smoothing_snap_v1:
        return "no_smoothing_snap_v1";
    case PredictionVisualCorrectionProfile::
            stock_visual_correction_evidence_pending:
        return "stock_visual_correction_evidence_pending";
    }
    return "unknown";
}

std::string_view to_string(const PredictionErrorCode code) noexcept
{
    switch (code) {
    case PredictionErrorCode::invalid_configuration: return "invalid_configuration";
    case PredictionErrorCode::invalid_session_identity:
        return "invalid_session_identity";
    case PredictionErrorCode::prediction_session_mismatch:
        return "prediction_session_mismatch";
    case PredictionErrorCode::stock_evidence_pending: return "stock_evidence_pending";
    case PredictionErrorCode::invalid_authoritative_state:
        return "invalid_authoritative_state";
    case PredictionErrorCode::authoritative_state_blocking:
        return "authoritative_state_blocking";
    case PredictionErrorCode::invalid_authority_update_ordinal:
        return "invalid_authority_update_ordinal";
    case PredictionErrorCode::authoritative_acknowledgement_missing:
        return "authoritative_acknowledgement_missing";
    case PredictionErrorCode::future_acknowledgement:
        return "future_acknowledgement";
    case PredictionErrorCode::acknowledgement_missing:
        return "acknowledgement_missing";
    case PredictionErrorCode::acknowledgement_evicted:
        return "acknowledgement_evicted";
    case PredictionErrorCode::stale_authoritative_update:
        return "stale_authoritative_update";
    case PredictionErrorCode::conflicting_authoritative_update:
        return "conflicting_authoritative_update";
    case PredictionErrorCode::prediction_command_gap: return "prediction_command_gap";
    case PredictionErrorCode::duplicate_predicted_command:
        return "duplicate_predicted_command";
    case PredictionErrorCode::out_of_order_predicted_command:
        return "out_of_order_predicted_command";
    case PredictionErrorCode::prediction_history_full:
        return "prediction_history_full";
    case PredictionErrorCode::prediction_history_backpressure:
        return "prediction_history_backpressure";
    case PredictionErrorCode::prediction_replay_limit_exceeded:
        return "prediction_replay_limit_exceeded";
    case PredictionErrorCode::prediction_replay_failed:
        return "prediction_replay_failed";
    case PredictionErrorCode::movement_environment_mismatch:
        return "movement_environment_mismatch";
    case PredictionErrorCode::collision_world_mismatch:
        return "collision_world_mismatch";
    case PredictionErrorCode::movement_config_mismatch:
        return "movement_config_mismatch";
    case PredictionErrorCode::hard_reset_generation_required:
        return "hard_reset_generation_required";
    case PredictionErrorCode::authoritative_update_backpressure:
        return "authoritative_update_backpressure";
    case PredictionErrorCode::visual_correction_failed:
        return "visual_correction_failed";
    case PredictionErrorCode::visual_correction_collision_failed:
        return "visual_correction_collision_failed";
    case PredictionErrorCode::revision_exhausted: return "revision_exhausted";
    case PredictionErrorCode::allocation_failed: return "allocation_failed";
    case PredictionErrorCode::non_finite_result: return "non_finite_result";
    }
    return "unknown";
}

} // namespace hlclient::prediction
