#pragma once

#include <hlclient/goldsrc/movement/goldsrc_local_movement.hpp>
#include <hlclient/goldsrc/stock_runtime_frame.hpp>
#include <hlclient/prediction/authoritative_player_state.hpp>
#include <hlclient/prediction/local_prediction.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace hlclient::prediction {

enum class StockAuthoritativeProjectionStatus : std::uint8_t {
    complete_authoritative_state,
    command_acknowledgement_pending,
    local_player_identity_pending,
    server_time_pending,
    required_field_missing,
    field_conflict,
    collision_validation_failed,
    unsupported_liquid_or_ladder,
    profile_mismatch,
    stock_evidence_pending,
};

enum class StockAuthoritativeCollisionValidationStatus : std::uint8_t {
    not_attempted,
    validated_free,
    blocking,
    liquid,
    query_failed,
    session_mismatch,
};

enum class StockAuthoritativeDerivedGroundProfile : std::uint8_t {
    collision_derived_ground_v1,
};

struct StockAuthoritativeCollisionValidation {
    StockAuthoritativeCollisionValidationStatus status{
        StockAuthoritativeCollisionValidationStatus::not_attempted};
    std::optional<movement::PlayerMovementContents> contents;
    std::optional<bool> grounded;
    std::optional<bool> walkable;
    std::optional<StockAuthoritativeDerivedGroundProfile> ground_profile;
    std::optional<goldsrc::movement::LocalMovementCollisionErrorCode>
        collision_error;
};

struct StockAuthoritativeAdapterContext {
    std::uint64_t runtime_generation{0U};
    goldsrc::movement::LocalMovementCollisionSessionIdentity collision_session;
    std::uint64_t movement_environment_signature{0U};
    std::uint64_t movement_config_signature{0U};
};

struct StockAuthoritativeProjectionResult {
    StockAuthoritativeProjectionStatus status{
        StockAuthoritativeProjectionStatus::stock_evidence_pending};
    std::shared_ptr<const goldsrc::StockAuthoritativeMovementObservation>
        observation;
    std::optional<StockAuthoritativeCollisionValidation> collision_validation;
    // This remains empty for the evidence-pending stock profile. It cannot be
    // populated by accepting a caller-supplied acknowledgement.
    std::optional<AuthoritativePlayerState> authoritative_state;
    std::string_view context;

    [[nodiscard]] bool prediction_ready() const noexcept
    {
        return status ==
                StockAuthoritativeProjectionStatus::
                    complete_authoritative_state &&
            authoritative_state.has_value();
    }
};

class StockAuthoritativePlayerStateAdapter final {
public:
    [[nodiscard]] static StockAuthoritativeProjectionResult project(
        const goldsrc::StockRuntimeFrameState& frame,
        const StockAuthoritativeAdapterContext& context,
        const goldsrc::movement::GoldSrcMovementEnvironment& environment,
        const goldsrc::movement::ILocalMovementCollision& collision,
        collision::CollisionQueryScratch& collision_scratch,
        const goldsrc::movement::GoldSrcLocalMovementConfig& movement_config =
            {}) noexcept;
};

[[nodiscard]] std::string_view to_string(
    StockAuthoritativeProjectionStatus status) noexcept;
[[nodiscard]] std::string_view to_string(
    StockAuthoritativeCollisionValidationStatus status) noexcept;

} // namespace hlclient::prediction
