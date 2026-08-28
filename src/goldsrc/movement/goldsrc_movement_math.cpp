#include <hlclient/goldsrc/movement/goldsrc_movement_math.hpp>

#include <algorithm>
#include <cmath>

namespace hlclient::goldsrc::movement {
namespace {

inline constexpr double kDirectionLengthTolerance = 1.0e-4;
inline constexpr float kDirectionVerticalTolerance = 1.0e-5F;

[[nodiscard]] GoldSrcMovementVectorResult vector_failure(
    const GoldSrcMovementMathErrorCode code,
    const std::string_view context) noexcept
{
    return {std::nullopt, GoldSrcMovementMathError{code, context}};
}

[[nodiscard]] GoldSrcWishDirectionResult wish_failure(
    const GoldSrcMovementMathErrorCode code,
    const std::string_view context) noexcept
{
    return {std::nullopt, GoldSrcMovementMathError{code, context}};
}

[[nodiscard]] bool valid_duration(const float value) noexcept
{
    return std::isfinite(value) && value >= 0.0F;
}

[[nodiscard]] bool valid_non_negative(const float value) noexcept
{
    return std::isfinite(value) && value >= 0.0F;
}

[[nodiscard]] bool valid_wish_direction(
    const assets::AssetVector3& direction,
    const float wish_speed,
    const bool require_horizontal) noexcept
{
    if (!finite_movement_vector(direction) ||
        (require_horizontal &&
            std::abs(direction.z) > kDirectionVerticalTolerance)) {
        return false;
    }
    if (wish_speed == 0.0F) {
        return direction.x == 0.0F && direction.y == 0.0F;
    }
    const auto length_squared =
        static_cast<double>(direction.x) * direction.x +
        static_cast<double>(direction.y) * direction.y +
        static_cast<double>(direction.z) * direction.z;
    return std::isfinite(length_squared) &&
        std::abs(length_squared - 1.0) <= kDirectionLengthTolerance;
}

[[nodiscard]] GoldSrcMovementVectorResult accelerate(
    const assets::AssetVector3& velocity,
    const assets::AssetVector3& wish_direction,
    const float wish_speed,
    const float acceleration,
    const float friction_multiplier,
    const float duration_seconds,
    const bool air) noexcept
{
    if (!finite_movement_vector(velocity) ||
        !finite_movement_vector(wish_direction)) {
        return vector_failure(GoldSrcMovementMathErrorCode::non_finite_input,
            "movement acceleration input is non-finite");
    }
    if (!valid_duration(duration_seconds)) {
        return vector_failure(GoldSrcMovementMathErrorCode::invalid_duration,
            "movement acceleration duration is invalid");
    }
    if (!valid_non_negative(wish_speed)) {
        return vector_failure(GoldSrcMovementMathErrorCode::invalid_wish_speed,
            "movement wish speed is invalid");
    }
    if (!valid_non_negative(acceleration)) {
        return vector_failure(GoldSrcMovementMathErrorCode::invalid_acceleration,
            "movement acceleration is invalid");
    }
    if (!valid_non_negative(friction_multiplier)) {
        return vector_failure(GoldSrcMovementMathErrorCode::invalid_friction,
            "movement friction multiplier is invalid");
    }
    if (!valid_wish_direction(wish_direction, wish_speed, air)) {
        return vector_failure(
            GoldSrcMovementMathErrorCode::invalid_wish_direction,
            air ? "air wish direction must be normalized and horizontal"
                : "ground wish direction must be normalized");
    }
    if (wish_speed == 0.0F || acceleration == 0.0F ||
        friction_multiplier == 0.0F || duration_seconds == 0.0F) {
        return {velocity, std::nullopt};
    }

    const auto capped_wish_speed = air
        ? std::min(wish_speed, kValveAirWishSpeedCap)
        : wish_speed;
    const auto current_speed = movement_dot(velocity, wish_direction);
    const auto add_speed = capped_wish_speed - current_speed;
    if (add_speed <= 0.0F) {
        return {velocity, std::nullopt};
    }

    // Public Valve PM_AirAccelerate intentionally uses the uncapped wish_speed
    // in this product; only add_speed observes the 30-unit cap.
    const auto acceleration_speed = static_cast<float>(std::min(
        static_cast<double>(add_speed),
        static_cast<double>(acceleration) * wish_speed * duration_seconds *
            friction_multiplier));
    const assets::AssetVector3 result{
        velocity.x + acceleration_speed * wish_direction.x,
        velocity.y + acceleration_speed * wish_direction.y,
        velocity.z + acceleration_speed * wish_direction.z,
    };
    return finite_movement_vector(result)
        ? GoldSrcMovementVectorResult{result, std::nullopt}
        : vector_failure(GoldSrcMovementMathErrorCode::non_finite_result,
              "movement acceleration produced a non-finite result");
}

} // namespace

bool finite_movement_vector(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

float movement_dot(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

GoldSrcWishDirectionResult yaw_only_wish_direction(
    const float yaw_degrees,
    const float forward_move,
    const float side_move,
    const float maximum_speed) noexcept
{
    if (!std::isfinite(yaw_degrees) || !std::isfinite(forward_move) ||
        !std::isfinite(side_move)) {
        return wish_failure(GoldSrcMovementMathErrorCode::non_finite_input,
            "yaw-only wish input is non-finite");
    }
    if (!std::isfinite(maximum_speed) || maximum_speed <= 0.0F) {
        return wish_failure(GoldSrcMovementMathErrorCode::invalid_maximum_speed,
            "yaw-only wish maximum speed must be positive");
    }

    const auto radians = static_cast<double>(yaw_degrees) *
        static_cast<double>(kGoldSrcDegreesToRadians);
    const auto sine = std::sin(radians);
    const auto cosine = std::cos(radians);
    // GoldSrc yaw zero faces +X; its horizontal right vector faces -Y.
    const auto wish_x = cosine * forward_move + sine * side_move;
    const auto wish_y = sine * forward_move - cosine * side_move;
    const auto uncapped_speed_double = std::hypot(wish_x, wish_y);
    if (!std::isfinite(uncapped_speed_double)) {
        return wish_failure(GoldSrcMovementMathErrorCode::non_finite_result,
            "yaw-only wish produced a non-finite speed");
    }
    const auto uncapped_speed = static_cast<float>(uncapped_speed_double);
    if (uncapped_speed == 0.0F) {
        return {GoldSrcWishDirection{{}, 0.0F, 0.0F}, std::nullopt};
    }
    const assets::AssetVector3 direction{
        static_cast<float>(wish_x / uncapped_speed_double),
        static_cast<float>(wish_y / uncapped_speed_double),
        0.0F,
    };
    const auto speed = std::min(uncapped_speed, maximum_speed);
    if (!finite_movement_vector(direction) || !std::isfinite(speed)) {
        return wish_failure(GoldSrcMovementMathErrorCode::non_finite_result,
            "yaw-only wish produced a non-finite result");
    }
    return {GoldSrcWishDirection{direction, speed, uncapped_speed}, std::nullopt};
}

GoldSrcMovementVectorResult apply_horizontal_ground_friction(
    const assets::AssetVector3& velocity,
    const float stop_speed,
    const float friction,
    const float friction_multiplier,
    const float duration_seconds) noexcept
{
    if (!finite_movement_vector(velocity)) {
        return vector_failure(GoldSrcMovementMathErrorCode::non_finite_input,
            "ground-friction velocity is non-finite");
    }
    if (!valid_duration(duration_seconds)) {
        return vector_failure(GoldSrcMovementMathErrorCode::invalid_duration,
            "ground-friction duration is invalid");
    }
    if (!valid_non_negative(stop_speed)) {
        return vector_failure(GoldSrcMovementMathErrorCode::invalid_stop_speed,
            "ground-friction stop speed is invalid");
    }
    if (!valid_non_negative(friction) ||
        !valid_non_negative(friction_multiplier)) {
        return vector_failure(GoldSrcMovementMathErrorCode::invalid_friction,
            "ground-friction coefficient is invalid");
    }

    const auto horizontal_speed = std::hypot(
        static_cast<double>(velocity.x), static_cast<double>(velocity.y));
    if (!std::isfinite(horizontal_speed)) {
        return vector_failure(GoldSrcMovementMathErrorCode::non_finite_result,
            "ground-friction speed is non-finite");
    }
    if (horizontal_speed < kValveFrictionSpeedEpsilon) {
        return {assets::AssetVector3{0.0F, 0.0F, velocity.z}, std::nullopt};
    }
    if (duration_seconds == 0.0F || friction == 0.0F ||
        friction_multiplier == 0.0F) {
        return {velocity, std::nullopt};
    }

    const auto control = std::max(horizontal_speed,
        static_cast<double>(stop_speed));
    const auto drop = control * friction * friction_multiplier * duration_seconds;
    const auto new_speed = std::max(horizontal_speed - drop, 0.0);
    const auto scale = static_cast<float>(new_speed / horizontal_speed);
    const assets::AssetVector3 result{
        velocity.x * scale,
        velocity.y * scale,
        velocity.z,
    };
    return finite_movement_vector(result)
        ? GoldSrcMovementVectorResult{result, std::nullopt}
        : vector_failure(GoldSrcMovementMathErrorCode::non_finite_result,
              "ground friction produced a non-finite result");
}

GoldSrcMovementVectorResult accelerate_ground(
    const assets::AssetVector3& velocity,
    const assets::AssetVector3& wish_direction,
    const float wish_speed,
    const float acceleration,
    const float friction_multiplier,
    const float duration_seconds) noexcept
{
    return accelerate(velocity, wish_direction, wish_speed, acceleration,
        friction_multiplier, duration_seconds, false);
}

GoldSrcMovementVectorResult accelerate_air(
    const assets::AssetVector3& velocity,
    const assets::AssetVector3& wish_direction,
    const float wish_speed,
    const float air_acceleration,
    const float friction_multiplier,
    const float duration_seconds) noexcept
{
    return accelerate(velocity, wish_direction, wish_speed, air_acceleration,
        friction_multiplier, duration_seconds, true);
}

GoldSrcMovementVectorResult apply_component_stop_epsilon(
    const assets::AssetVector3& velocity,
    const float stop_epsilon) noexcept
{
    if (!finite_movement_vector(velocity)) {
        return vector_failure(GoldSrcMovementMathErrorCode::non_finite_input,
            "stop-epsilon velocity is non-finite");
    }
    if (!std::isfinite(stop_epsilon) || stop_epsilon < 0.0F) {
        return vector_failure(
            GoldSrcMovementMathErrorCode::invalid_stop_epsilon,
            "component stop epsilon is invalid");
    }
    auto stop = [stop_epsilon](const float value) noexcept {
        return value > -stop_epsilon && value < stop_epsilon ? 0.0F : value;
    };
    return {assets::AssetVector3{
                stop(velocity.x), stop(velocity.y), stop(velocity.z)},
        std::nullopt};
}

GoldSrcClipVelocityResult clip_velocity_against_plane(
    const assets::AssetVector3& velocity,
    const assets::AssetVector3& plane_normal,
    const float overbounce,
    const float stop_epsilon) noexcept
{
    if (!finite_movement_vector(velocity) ||
        !finite_movement_vector(plane_normal)) {
        return {std::nullopt,
            GoldSrcMovementMathError{
                GoldSrcMovementMathErrorCode::non_finite_input,
                "clip-velocity input is non-finite"}};
    }
    const auto normal_length_squared =
        static_cast<double>(plane_normal.x) * plane_normal.x +
        static_cast<double>(plane_normal.y) * plane_normal.y +
        static_cast<double>(plane_normal.z) * plane_normal.z;
    if (!std::isfinite(normal_length_squared) ||
        std::abs(normal_length_squared - 1.0) > kDirectionLengthTolerance) {
        return {std::nullopt,
            GoldSrcMovementMathError{
                GoldSrcMovementMathErrorCode::invalid_plane_normal,
                "clip plane normal must be normalized"}};
    }
    if (!std::isfinite(overbounce) || overbounce < 0.0F) {
        return {std::nullopt,
            GoldSrcMovementMathError{
                GoldSrcMovementMathErrorCode::invalid_overbounce,
                "clip overbounce is invalid"}};
    }
    if (!std::isfinite(stop_epsilon) || stop_epsilon < 0.0F) {
        return {std::nullopt,
            GoldSrcMovementMathError{
                GoldSrcMovementMathErrorCode::invalid_stop_epsilon,
                "clip stop epsilon is invalid"}};
    }

    const auto backoff = movement_dot(velocity, plane_normal) * overbounce;
    const assets::AssetVector3 clipped{
        velocity.x - plane_normal.x * backoff,
        velocity.y - plane_normal.y * backoff,
        velocity.z - plane_normal.z * backoff,
    };
    if (!finite_movement_vector(clipped)) {
        return {std::nullopt,
            GoldSrcMovementMathError{
                GoldSrcMovementMathErrorCode::non_finite_result,
                "velocity clipping produced a non-finite result"}};
    }
    const auto stopped = apply_component_stop_epsilon(clipped, stop_epsilon);
    if (!stopped) {
        return {std::nullopt, stopped.error};
    }

    auto blocked_axes = GoldSrcClipBlockedAxis::none;
    if (plane_normal.z > 0.0F) {
        blocked_axes = static_cast<GoldSrcClipBlockedAxis>(
            static_cast<std::uint8_t>(blocked_axes) |
            static_cast<std::uint8_t>(GoldSrcClipBlockedAxis::floor));
    }
    if (plane_normal.z == 0.0F) {
        blocked_axes = static_cast<GoldSrcClipBlockedAxis>(
            static_cast<std::uint8_t>(blocked_axes) |
            static_cast<std::uint8_t>(GoldSrcClipBlockedAxis::wall_or_step));
    }
    return {GoldSrcClippedVelocity{*stopped.value, blocked_axes}, std::nullopt};
}

GoldSrcMovementVectorResult clip_velocity(
    const assets::AssetVector3& velocity,
    const assets::AssetVector3& plane_normal,
    const float overbounce,
    const float stop_epsilon) noexcept
{
    const auto clipped = clip_velocity_against_plane(
        velocity, plane_normal, overbounce, stop_epsilon);
    if (!clipped) {
        return {std::nullopt, clipped.error};
    }
    return {clipped.value->velocity, std::nullopt};
}

GoldSrcMovementVectorResult clamp_velocity_per_axis(
    const assets::AssetVector3& velocity,
    const float maximum_velocity) noexcept
{
    if (!finite_movement_vector(velocity)) {
        return vector_failure(GoldSrcMovementMathErrorCode::non_finite_input,
            "velocity clamp input is non-finite");
    }
    if (!std::isfinite(maximum_velocity) || maximum_velocity <= 0.0F) {
        return vector_failure(
            GoldSrcMovementMathErrorCode::invalid_maximum_velocity,
            "maximum velocity must be positive");
    }
    return {assets::AssetVector3{
                std::clamp(velocity.x, -maximum_velocity, maximum_velocity),
                std::clamp(velocity.y, -maximum_velocity, maximum_velocity),
                std::clamp(velocity.z, -maximum_velocity, maximum_velocity)},
        std::nullopt};
}

std::string_view to_string(const GoldSrcMovementMathErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcMovementMathErrorCode::non_finite_input:
        return "non_finite_input";
    case GoldSrcMovementMathErrorCode::invalid_duration:
        return "invalid_duration";
    case GoldSrcMovementMathErrorCode::invalid_maximum_speed:
        return "invalid_maximum_speed";
    case GoldSrcMovementMathErrorCode::invalid_stop_speed:
        return "invalid_stop_speed";
    case GoldSrcMovementMathErrorCode::invalid_friction:
        return "invalid_friction";
    case GoldSrcMovementMathErrorCode::invalid_acceleration:
        return "invalid_acceleration";
    case GoldSrcMovementMathErrorCode::invalid_wish_speed:
        return "invalid_wish_speed";
    case GoldSrcMovementMathErrorCode::invalid_wish_direction:
        return "invalid_wish_direction";
    case GoldSrcMovementMathErrorCode::invalid_plane_normal:
        return "invalid_plane_normal";
    case GoldSrcMovementMathErrorCode::invalid_overbounce:
        return "invalid_overbounce";
    case GoldSrcMovementMathErrorCode::invalid_stop_epsilon:
        return "invalid_stop_epsilon";
    case GoldSrcMovementMathErrorCode::invalid_maximum_velocity:
        return "invalid_maximum_velocity";
    case GoldSrcMovementMathErrorCode::non_finite_result:
        return "non_finite_result";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc::movement
