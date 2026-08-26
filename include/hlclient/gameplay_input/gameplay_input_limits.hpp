#pragma once

#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/input/input_state_tracker.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace hlclient::gameplay_input {

// Public safety envelope for the complete M4.6.1 input-to-camera path. The
// owning layers retain narrower builder/configuration limits; this aggregate
// makes the whole project profile inspectable without claiming stock input,
// movement, FOV, or timing semantics.
struct GameplayInputLimits final {
    std::size_t maximum_events_per_frame{1'024U};
    std::size_t maximum_actions{
        static_cast<std::size_t>(GameplayInputAction::count)};
    std::size_t maximum_bindings{64U};
    std::int32_t maximum_mouse_delta{1'000'000};
    double maximum_wheel_delta{10'000.0};
    double maximum_frame_duration_seconds{0.1};
    double maximum_camera_position_magnitude{10'000'000.0};
    double maximum_camera_speed{10'000.0};
    double minimum_mouse_sensitivity{0.001};
    double maximum_mouse_sensitivity{10.0};
    double minimum_vertical_fov_radians{0.3490658503988659};
    double maximum_vertical_fov_radians{2.443460952792061};
    std::uint64_t maximum_camera_revisions{
        std::numeric_limits<std::uint64_t>::max()};
};

inline constexpr GameplayInputLimits kGameplayInputSafetyHardLimits{
    input::InputStateLimits::hard_maximum_events_per_frame,
    static_cast<std::size_t>(GameplayInputAction::count),
    kGameplayInputBindingHardLimits.maximum_total_bindings,
    input::InputStateLimits::hard_maximum_relative_mouse_delta_per_axis,
    input::InputStateLimits::hard_maximum_wheel_delta_per_axis,
    1.0,
    10'000'000.0,
    10'000.0,
    0.001,
    10.0,
    0.3490658503988659,
    2.443460952792061,
    std::numeric_limits<std::uint64_t>::max(),
};

[[nodiscard]] bool valid_gameplay_input_limits(
    const GameplayInputLimits& limits) noexcept;

} // namespace hlclient::gameplay_input
