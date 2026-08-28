#pragma once

#include <hlclient/assets/asset_types.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::goldsrc::movement {

// Named literals independently transcribed from the pinned public Valve
// pm_shared sources. The dry-walk profile is not a full PM_Move claim.
inline constexpr float kValveStandingHullMinimumZ = -36.0F;
inline constexpr float kValveStandingHullMaximumZ = 36.0F;
inline constexpr float kValveDuckHullMinimumZ = -18.0F;
inline constexpr float kValveDuckHullMaximumZ = 18.0F;
inline constexpr float kValveStandingViewOffsetZ = 28.0F;
inline constexpr float kValveDuckViewOffsetZ = 12.0F;
inline constexpr float kValveStopEpsilon = 0.1F;
inline constexpr float kValveFrictionSpeedEpsilon = 0.1F;
inline constexpr std::size_t kValveMaximumClipPlanes = 5U;
inline constexpr std::size_t kValveMaximumSlideBumps = 4U;
inline constexpr float kValveGroundProbeDistance = 2.0F;
inline constexpr float kValveMinimumWalkableNormalZ = 0.7F;
inline constexpr float kValveMaximumGroundSnapUpwardVelocity = 180.0F;
inline constexpr float kValveAirWishSpeedCap = 30.0F;
inline constexpr float kValveJumpReferenceGravity = 800.0F;
inline constexpr float kValveJumpReferenceHeight = 45.0F;
inline constexpr float kValveJumpImpulse = 268.32815729997475F;
inline constexpr float kGoldSrcDegreesToRadians =
    0.01745329251994329576923690768489F;

enum class GoldSrcMovementMathErrorCode : std::uint8_t {
    non_finite_input,
    invalid_duration,
    invalid_maximum_speed,
    invalid_stop_speed,
    invalid_friction,
    invalid_acceleration,
    invalid_wish_speed,
    invalid_wish_direction,
    invalid_plane_normal,
    invalid_overbounce,
    invalid_stop_epsilon,
    invalid_maximum_velocity,
    non_finite_result,
};

struct GoldSrcMovementMathError {
    GoldSrcMovementMathErrorCode code{
        GoldSrcMovementMathErrorCode::non_finite_input};
    std::string_view context;
};

struct GoldSrcMovementVectorResult {
    std::optional<assets::AssetVector3> value;
    std::optional<GoldSrcMovementMathError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value.has_value();
    }
};

enum class GoldSrcClipBlockedAxis : std::uint8_t {
    none = 0U,
    floor = 1U << 0U,
    wall_or_step = 1U << 1U,
};

struct GoldSrcClippedVelocity {
    assets::AssetVector3 velocity{};
    GoldSrcClipBlockedAxis blocked_axes{GoldSrcClipBlockedAxis::none};
};

struct GoldSrcClipVelocityResult {
    std::optional<GoldSrcClippedVelocity> value;
    std::optional<GoldSrcMovementMathError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value.has_value();
    }
};

struct GoldSrcWishDirection {
    assets::AssetVector3 direction{};
    float speed{0.0F};
    float uncapped_speed{0.0F};
};

struct GoldSrcWishDirectionResult {
    std::optional<GoldSrcWishDirection> wish;
    std::optional<GoldSrcMovementMathError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return wish.has_value();
    }
};

[[nodiscard]] bool finite_movement_vector(
    const assets::AssetVector3& value) noexcept;
[[nodiscard]] float movement_dot(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept;

[[nodiscard]] GoldSrcWishDirectionResult yaw_only_wish_direction(
    float yaw_degrees,
    float forward_move,
    float side_move,
    float maximum_speed) noexcept;

[[nodiscard]] GoldSrcMovementVectorResult apply_horizontal_ground_friction(
    const assets::AssetVector3& velocity,
    float stop_speed,
    float friction,
    float friction_multiplier,
    float duration_seconds) noexcept;

[[nodiscard]] GoldSrcMovementVectorResult accelerate_ground(
    const assets::AssetVector3& velocity,
    const assets::AssetVector3& wish_direction,
    float wish_speed,
    float acceleration,
    float friction_multiplier,
    float duration_seconds) noexcept;

[[nodiscard]] GoldSrcMovementVectorResult accelerate_air(
    const assets::AssetVector3& velocity,
    const assets::AssetVector3& wish_direction,
    float wish_speed,
    float air_acceleration,
    float friction_multiplier,
    float duration_seconds) noexcept;

[[nodiscard]] GoldSrcMovementVectorResult apply_component_stop_epsilon(
    const assets::AssetVector3& velocity,
    float stop_epsilon = kValveStopEpsilon) noexcept;

// Public Valve PM_ClipVelocity-compatible classification for the supported
// dry-walk subset: positive-Z planes block the floor axis, and exactly
// vertical planes block the wall/step axis.  It does not mutate position.
[[nodiscard]] GoldSrcClipVelocityResult clip_velocity_against_plane(
    const assets::AssetVector3& velocity,
    const assets::AssetVector3& plane_normal,
    float overbounce = 1.0F,
    float stop_epsilon = kValveStopEpsilon) noexcept;

[[nodiscard]] GoldSrcMovementVectorResult clip_velocity(
    const assets::AssetVector3& velocity,
    const assets::AssetVector3& plane_normal,
    float overbounce = 1.0F,
    float stop_epsilon = kValveStopEpsilon) noexcept;

[[nodiscard]] GoldSrcMovementVectorResult clamp_velocity_per_axis(
    const assets::AssetVector3& velocity,
    float maximum_velocity) noexcept;

[[nodiscard]] std::string_view to_string(
    GoldSrcMovementMathErrorCode code) noexcept;

} // namespace hlclient::goldsrc::movement
