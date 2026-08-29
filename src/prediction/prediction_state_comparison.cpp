#include <hlclient/prediction/prediction_state_comparison.hpp>

#include <algorithm>
#include <cmath>

namespace hlclient::prediction {
namespace {

[[nodiscard]] bool finite_positive(const double value) noexcept
{
    return std::isfinite(value) && value >= 0.0;
}

[[nodiscard]] double magnitude(const assets::AssetVector3& value) noexcept
{
    return std::sqrt(static_cast<double>(value.x) * value.x +
        static_cast<double>(value.y) * value.y +
        static_cast<double>(value.z) * value.z);
}

[[nodiscard]] double shortest_angle_delta(
    const double predicted,
    const double authoritative) noexcept
{
    auto delta = authoritative - predicted;
    delta = std::fmod(delta + 180.0, 360.0);
    if (delta < 0.0) {
        delta += 360.0;
    }
    return delta - 180.0;
}

[[nodiscard]] bool exact_vector(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] bool exact_ground(
    const movement::PlayerGroundState& left,
    const movement::PlayerGroundState& right) noexcept
{
    return left.grounded() == right.grounded() &&
        left.walkable() == right.walkable() && left.hit() == right.hit() &&
        left.plane() == right.plane() &&
        exact_vector(left.contact_position(), right.contact_position()) &&
        left.probe_fraction() == right.probe_fraction() &&
        left.evidence_profile() == right.evidence_profile();
}

[[nodiscard]] bool exact_physical_state(
    const movement::LocalPlayerMovementState& left,
    const movement::LocalPlayerMovementState& right) noexcept
{
    // Revision is publication metadata and intentionally excluded. Sequence,
    // timing and old-buttons are simulation state and remain included.
    return exact_vector(left.origin(), right.origin()) &&
        exact_vector(left.velocity(), right.velocity()) &&
        exact_vector(left.view_angles(), right.view_angles()) &&
        left.hull() == right.hull() && left.mode() == right.mode() &&
        exact_ground(left.ground_state(), right.ground_state()) &&
        exact_vector(left.view_offset(), right.view_offset()) &&
        left.old_buttons() == right.old_buttons() &&
        left.source_command_sequence() == right.source_command_sequence() &&
        left.simulation_time_nanoseconds() ==
            right.simulation_time_nanoseconds() &&
        left.last_valid_contents() == right.last_valid_contents() &&
        left.gravity_multiplier() == right.gravity_multiplier() &&
        left.friction_multiplier() == right.friction_multiplier() &&
        left.compatibility_profile() == right.compatibility_profile() &&
        left.evidence_profile() == right.evidence_profile() &&
        left.command_profile() == right.command_profile();
}

} // namespace

bool valid_prediction_state_comparison_config(
    const PredictionStateComparisonConfig& config) noexcept
{
    return finite_positive(config.position_exact_epsilon) &&
        finite_positive(config.velocity_exact_epsilon) &&
        finite_positive(config.angle_exact_epsilon) &&
        finite_positive(config.visual_no_offset_epsilon) &&
        finite_positive(config.small_correction_maximum) &&
        finite_positive(config.large_correction_snap_threshold) &&
        finite_positive(
            config.maximum_acceptable_authoritative_position_error) &&
        finite_positive(
            config.maximum_acceptable_authoritative_velocity_error) &&
        config.visual_no_offset_epsilon <= config.small_correction_maximum &&
        config.small_correction_maximum <=
            config.large_correction_snap_threshold;
}

PredictionStateComparisonResult compare_prediction_states(
    const movement::LocalPlayerMovementState& predicted,
    const movement::LocalPlayerMovementState& authoritative,
    const PredictionStateComparisonConfig& config) noexcept
{
    if (!valid_prediction_state_comparison_config(config)) {
        return {std::nullopt,
            PredictionError{PredictionErrorCode::invalid_configuration,
                std::nullopt, "prediction comparison config is invalid"}};
    }
    PredictionErrorMetrics metrics;
    metrics.position_delta = {
        authoritative.origin().x - predicted.origin().x,
        authoritative.origin().y - predicted.origin().y,
        authoritative.origin().z - predicted.origin().z,
    };
    metrics.position_error_magnitude = magnitude(metrics.position_delta);
    metrics.horizontal_position_error = std::sqrt(
        static_cast<double>(metrics.position_delta.x) *
                metrics.position_delta.x +
        static_cast<double>(metrics.position_delta.y) *
                metrics.position_delta.y);
    metrics.vertical_position_error = std::abs(metrics.position_delta.z);
    metrics.velocity_delta = {
        authoritative.velocity().x - predicted.velocity().x,
        authoritative.velocity().y - predicted.velocity().y,
        authoritative.velocity().z - predicted.velocity().z,
    };
    metrics.velocity_error_magnitude = magnitude(metrics.velocity_delta);
    metrics.shortest_path_angle_deltas = {
        static_cast<float>(shortest_angle_delta(
            predicted.view_angles().x, authoritative.view_angles().x)),
        static_cast<float>(shortest_angle_delta(
            predicted.view_angles().y, authoritative.view_angles().y)),
        static_cast<float>(shortest_angle_delta(
            predicted.view_angles().z, authoritative.view_angles().z)),
    };
    metrics.hull_mismatch = predicted.hull() != authoritative.hull();
    metrics.mode_mismatch = predicted.mode() != authoritative.mode();
    metrics.grounded_mismatch = predicted.ground_state().grounded() !=
        authoritative.ground_state().grounded();
    metrics.contents_mismatch = predicted.last_valid_contents() !=
        authoritative.last_valid_contents();
    metrics.old_buttons_mismatch = predicted.old_buttons() !=
        authoritative.old_buttons();
    metrics.simulation_time_mismatch =
        predicted.simulation_time_nanoseconds() !=
        authoritative.simulation_time_nanoseconds();
    metrics.exact_physical_state_match =
        exact_physical_state(predicted, authoritative);
    metrics.exact_state_signature_match =
        movement::local_player_movement_state_signature(predicted) ==
        movement::local_player_movement_state_signature(authoritative);
    if (!std::isfinite(metrics.position_error_magnitude) ||
        !std::isfinite(metrics.velocity_error_magnitude) ||
        !std::isfinite(metrics.horizontal_position_error) ||
        !std::isfinite(metrics.vertical_position_error) ||
        !std::isfinite(metrics.shortest_path_angle_deltas.x) ||
        !std::isfinite(metrics.shortest_path_angle_deltas.y) ||
        !std::isfinite(metrics.shortest_path_angle_deltas.z)) {
        return {std::nullopt,
            PredictionError{PredictionErrorCode::non_finite_result,
                std::nullopt, "prediction comparison produced a non-finite metric"}};
    }
    return {metrics, std::nullopt};
}

} // namespace hlclient::prediction
