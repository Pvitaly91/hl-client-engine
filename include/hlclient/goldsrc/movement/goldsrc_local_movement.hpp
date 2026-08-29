#pragma once

#include <hlclient/goldsrc/movement/goldsrc_movement_environment.hpp>
#include <hlclient/goldsrc/movement/local_movement_collision.hpp>
#include <hlclient/goldsrc/movement/player_wall_contact_diagnostics.hpp>
#include <hlclient/goldsrc/usercmd_state.hpp>
#include <hlclient/movement/local_player_movement_state.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc::movement {

inline constexpr double kGoldSrcMovementStandingViewOffset = 28.0;
inline constexpr double kGoldSrcMovementDuckViewOffset = 12.0;
inline constexpr double kGoldSrcMovementStopEpsilon = 0.1;
inline constexpr std::size_t kGoldSrcMovementMaximumClipPlanes = 5U;
inline constexpr double kGoldSrcMovementGroundProbeDistance = 2.0;
inline constexpr double kGoldSrcMovementMinimumWalkableNormalZ = 0.7;
inline constexpr std::size_t kGoldSrcMovementMaximumSlideBumps = 4U;
inline constexpr std::size_t kGoldSrcMovementMaximumTouchesPerCommand = 256U;
inline constexpr std::size_t kGoldSrcMovementHardMaximumTouchesPerCommand =
    4'096U;
inline constexpr double kGoldSrcMovementMaximumGroundSnapUpwardVelocity = 180.0;
inline constexpr double kGoldSrcMovementAirWishSpeedCap = 30.0;
inline constexpr double kGoldSrcMovementJumpImpulse = 268.32815729997475;

enum class GoldSrcMovementGravityProfile : std::uint8_t {
    public_valve_split_half_step_v1,
};

enum class GoldSrcMovementJumpProfile : std::uint8_t {
    public_valve_reference_800x45_v1,
};

enum class GoldSrcMovementDuckProfile : std::uint8_t {
    project_immediate_bounded_v1,
};

enum class GoldSrcMovementAirProfile : std::uint8_t {
    public_valve_wish_cap_30_uncapped_accel_v1,
};

enum class GoldSrcMovementAirborneDuckPolicy : std::uint8_t {
    preserve_hull_center_v1,
};

struct GoldSrcLocalMovementConfig {
    double maximum_command_duration_seconds{0.255};
    double maximum_substep_duration_seconds{0.010};
    std::size_t maximum_substeps_per_command{32U};
    double ground_probe_distance{kGoldSrcMovementGroundProbeDistance};
    double minimum_walkable_normal_z{kGoldSrcMovementMinimumWalkableNormalZ};
    double maximum_ground_snap_upward_velocity{
        kGoldSrcMovementMaximumGroundSnapUpwardVelocity};
    std::size_t maximum_slide_bumps{kGoldSrcMovementMaximumSlideBumps};
    std::size_t maximum_clip_planes{kGoldSrcMovementMaximumClipPlanes};
    std::size_t maximum_touches_per_command{
        kGoldSrcMovementMaximumTouchesPerCommand};
    double stop_epsilon{kGoldSrcMovementStopEpsilon};
    double air_wish_speed_cap{kGoldSrcMovementAirWishSpeedCap};
    double jump_impulse{kGoldSrcMovementJumpImpulse};
    assets::AssetVector3 standing_view_offset{
        0.0F, 0.0F, static_cast<float>(kGoldSrcMovementStandingViewOffset)};
    assets::AssetVector3 duck_view_offset{
        0.0F, 0.0F, static_cast<float>(kGoldSrcMovementDuckViewOffset)};
    LocalMovementCollisionQueryConfig collision_query{};
    hlclient::movement::LocalPlayerMovementStateLimits state_limits{};
    GoldSrcMovementGravityProfile gravity_profile{
        GoldSrcMovementGravityProfile::public_valve_split_half_step_v1};
    GoldSrcMovementJumpProfile jump_profile{
        GoldSrcMovementJumpProfile::public_valve_reference_800x45_v1};
    GoldSrcMovementDuckProfile duck_profile{
        GoldSrcMovementDuckProfile::project_immediate_bounded_v1};
    GoldSrcMovementAirProfile air_profile{
        GoldSrcMovementAirProfile::
            public_valve_wish_cap_30_uncapped_accel_v1};
    GoldSrcMovementAirborneDuckPolicy airborne_duck_policy{
        GoldSrcMovementAirborneDuckPolicy::preserve_hull_center_v1};
};

[[nodiscard]] bool valid_goldsrc_local_movement_config(
    const GoldSrcLocalMovementConfig& config) noexcept;

enum class LocalMovementSimulationErrorCode : std::uint8_t {
    invalid_configuration,
    invalid_state,
    invalid_environment,
    unsupported_command_profile,
    stock_semantics_pending,
    invalid_command_sequence,
    invalid_command_duration,
    collision_query_failed,
    player_startsolid,
    player_allsolid,
    movement_trace_failed,
    velocity_limit_exceeded,
    substep_limit_exceeded,
    clip_plane_limit_exceeded,
    touch_limit_exceeded,
    allocation_failed,
    statistics_overflow,
    movement_stalled,
    liquid_movement_unsupported,
    ladder_movement_unsupported,
    duck_transition_failed,
    stand_clearance_blocked,
    state_revision_exhausted,
    simulation_time_overflow,
    non_finite_result,
};

[[nodiscard]] std::string_view to_string(
    LocalMovementSimulationErrorCode code) noexcept;

struct LocalMovementSimulationError {
    LocalMovementSimulationErrorCode code{
        LocalMovementSimulationErrorCode::invalid_configuration};
    std::optional<LocalMovementCollisionError> collision_error;
    std::string_view context;
};

struct LocalMovementSimulationResult {
    std::optional<hlclient::movement::LocalPlayerMovementState> state;
    std::vector<hlclient::movement::PlayerMovementTouch> touches;
    hlclient::movement::PlayerMovementStatistics statistics{};
    std::optional<LocalMovementSimulationError> error;
    std::uint32_t command_sequence{0U};
    std::uint64_t deterministic_state_signature{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value() && !error.has_value();
    }
};

struct GoldSrcLocalMovementScratch {
    // General queries and the two speculative walking routes use distinct
    // bounded scratch arenas. A rejected route cannot leave active traversal
    // marks in the selected route, and each arena retains its capacity for
    // reuse by later commands.
    hlclient::collision::CollisionQueryScratch collision;
    hlclient::collision::CollisionQueryScratch direct_candidate_collision;
    hlclient::collision::CollisionQueryScratch step_candidate_collision;
    PlayerMovementDiagnosticRing diagnostics;
    std::optional<PlayerWallContactDiagnosticFrame> last_diagnostic;
    std::uint16_t diagnostic_substep_ordinal{0U};
};

class GoldSrcLocalMovementKernel final {
public:
    [[nodiscard]] static LocalMovementSimulationResult simulate(
        const hlclient::movement::LocalPlayerMovementState& previous_state,
        const GoldSrcUserCmdState& command,
        const GoldSrcMovementEnvironment& environment,
        const ILocalMovementCollision& collision,
        GoldSrcLocalMovementScratch& scratch,
        const GoldSrcLocalMovementConfig& config = {});
};

} // namespace hlclient::goldsrc::movement
