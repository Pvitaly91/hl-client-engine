#pragma once

#include <hlclient/prediction/prediction_types.hpp>

namespace hlclient::prediction {

struct PredictionStateComparisonConfig {
    double position_exact_epsilon{0.00001};
    double velocity_exact_epsilon{0.00001};
    double angle_exact_epsilon{0.0001};
    double visual_no_offset_epsilon{0.001};
    double small_correction_maximum{4.0};
    double large_correction_snap_threshold{16.0};
    double maximum_acceptable_authoritative_position_error{4'096.0};
    double maximum_acceptable_authoritative_velocity_error{4'096.0};
};

struct PredictionErrorMetrics {
    assets::AssetVector3 position_delta{};
    double position_error_magnitude{0.0};
    double horizontal_position_error{0.0};
    double vertical_position_error{0.0};
    assets::AssetVector3 velocity_delta{};
    double velocity_error_magnitude{0.0};
    assets::AssetVector3 shortest_path_angle_deltas{};
    bool hull_mismatch{false};
    bool mode_mismatch{false};
    bool grounded_mismatch{false};
    bool contents_mismatch{false};
    bool old_buttons_mismatch{false};
    bool simulation_time_mismatch{false};
    bool exact_physical_state_match{false};
    bool exact_state_signature_match{false};
};

struct PredictionStateComparisonResult {
    std::optional<PredictionErrorMetrics> metrics;
    std::optional<PredictionError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return metrics.has_value() && !error.has_value();
    }
};

[[nodiscard]] bool valid_prediction_state_comparison_config(
    const PredictionStateComparisonConfig& config) noexcept;

[[nodiscard]] PredictionStateComparisonResult compare_prediction_states(
    const movement::LocalPlayerMovementState& predicted,
    const movement::LocalPlayerMovementState& authoritative,
    const PredictionStateComparisonConfig& config = {}) noexcept;

} // namespace hlclient::prediction
