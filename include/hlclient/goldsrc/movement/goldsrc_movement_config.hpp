#pragma once

#include <hlclient/goldsrc/movement/local_movement_query_config.hpp>
#include <hlclient/movement/local_player_movement_state.hpp>

#include <cstddef>
#include <cstdint>

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

// Neutral parameters selected by a concrete game module. The reference host
// owns hull queries/integration; no Half-Life ladder speeds are its defaults.
struct ReferenceLadderMovementPolicy {
    float maximum_climb_speed{};
    float duck_speed_multiplier{};
    float jump_away_speed{};
};

struct GoldSrcLocalMovementConfig {
    // Simulation-local ground command cap, selected by the game policy.
    // Applied before duck scaling. Never mutates wire values or MoveVars.
    std::uint16_t ground_button_speed_limit_mask{};
    float ground_button_speed_limit_multiplier{1.0F};
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

} // namespace hlclient::goldsrc::movement
