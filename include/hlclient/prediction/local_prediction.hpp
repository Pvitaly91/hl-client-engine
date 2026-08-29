#pragma once

#include <hlclient/goldsrc/movement/goldsrc_local_movement.hpp>
#include <hlclient/prediction/prediction_history.hpp>
#include <hlclient/prediction/prediction_state_comparison.hpp>

#include <optional>

namespace hlclient::prediction {

struct PredictionSessionCreateResult {
    std::optional<PredictionSessionIdentity> session;
    std::optional<PredictionError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return session.has_value() && !error.has_value();
    }
};

[[nodiscard]] std::uint64_t prediction_movement_environment_signature(
    const goldsrc::movement::GoldSrcMovementEnvironment& environment) noexcept;

[[nodiscard]] std::uint64_t prediction_movement_config_signature(
    const goldsrc::movement::GoldSrcLocalMovementConfig& config) noexcept;

[[nodiscard]] PredictionSessionCreateResult create_prediction_session_identity(
    std::uint64_t session_generation,
    std::uint64_t prediction_generation,
    const goldsrc::movement::ILocalMovementCollision& collision,
    const goldsrc::movement::GoldSrcMovementEnvironment& environment,
    const goldsrc::movement::GoldSrcLocalMovementConfig& movement_config,
    const movement::LocalPlayerMovementState& initial_state,
    PredictionCompatibilityProfile profile = PredictionCompatibilityProfile::
        synthetic_authoritative_reconciliation_v1,
    PredictionAcknowledgementProfile acknowledgement_profile =
        PredictionAcknowledgementProfile::synthetic_uint32_non_wrapping_v1)
    noexcept;

} // namespace hlclient::prediction
