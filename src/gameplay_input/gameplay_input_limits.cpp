#include <hlclient/gameplay_input/gameplay_input_limits.hpp>

#include <cmath>

namespace hlclient::gameplay_input {

bool valid_gameplay_input_limits(const GameplayInputLimits& limits) noexcept
{
    return limits.maximum_events_per_frame > 0U &&
        limits.maximum_events_per_frame <=
            kGameplayInputSafetyHardLimits.maximum_events_per_frame &&
        limits.maximum_actions > 0U &&
        limits.maximum_actions <=
            kGameplayInputSafetyHardLimits.maximum_actions &&
        limits.maximum_bindings > 0U &&
        limits.maximum_bindings <=
            kGameplayInputSafetyHardLimits.maximum_bindings &&
        limits.maximum_mouse_delta > 0 &&
        limits.maximum_mouse_delta <=
            kGameplayInputSafetyHardLimits.maximum_mouse_delta &&
        std::isfinite(limits.maximum_wheel_delta) &&
        limits.maximum_wheel_delta > 0.0 &&
        limits.maximum_wheel_delta <=
            kGameplayInputSafetyHardLimits.maximum_wheel_delta &&
        std::isfinite(limits.maximum_frame_duration_seconds) &&
        limits.maximum_frame_duration_seconds > 0.0 &&
        limits.maximum_frame_duration_seconds <=
            kGameplayInputSafetyHardLimits.maximum_frame_duration_seconds &&
        std::isfinite(limits.maximum_camera_position_magnitude) &&
        limits.maximum_camera_position_magnitude >= 1.0 &&
        limits.maximum_camera_position_magnitude <=
            kGameplayInputSafetyHardLimits.maximum_camera_position_magnitude &&
        std::isfinite(limits.maximum_camera_speed) &&
        limits.maximum_camera_speed >= 1.0 &&
        limits.maximum_camera_speed <=
            kGameplayInputSafetyHardLimits.maximum_camera_speed &&
        std::isfinite(limits.minimum_mouse_sensitivity) &&
        std::isfinite(limits.maximum_mouse_sensitivity) &&
        limits.minimum_mouse_sensitivity >=
            kGameplayInputSafetyHardLimits.minimum_mouse_sensitivity &&
        limits.maximum_mouse_sensitivity <=
            kGameplayInputSafetyHardLimits.maximum_mouse_sensitivity &&
        limits.minimum_mouse_sensitivity <=
            limits.maximum_mouse_sensitivity &&
        std::isfinite(limits.minimum_vertical_fov_radians) &&
        std::isfinite(limits.maximum_vertical_fov_radians) &&
        limits.minimum_vertical_fov_radians >=
            kGameplayInputSafetyHardLimits.minimum_vertical_fov_radians &&
        limits.maximum_vertical_fov_radians <=
            kGameplayInputSafetyHardLimits.maximum_vertical_fov_radians &&
        limits.minimum_vertical_fov_radians <
            limits.maximum_vertical_fov_radians &&
        limits.maximum_camera_revisions > 0U;
}

} // namespace hlclient::gameplay_input
