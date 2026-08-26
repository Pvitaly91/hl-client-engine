#include <hlclient/input/input_snapshot.hpp>

#include <utility>

namespace hlclient::input {
namespace {

[[nodiscard]] constexpr std::size_t key_index(const PhysicalKey key) noexcept
{
    return static_cast<std::size_t>(key);
}

[[nodiscard]] constexpr std::size_t mouse_button_index(
    const PhysicalMouseButton button) noexcept
{
    return static_cast<std::size_t>(button);
}

} // namespace

InputSnapshot::InputSnapshot(
    const std::uint64_t sequence,
    const InputFocusState focus_state,
    const InputCaptureState capture_state,
    const InputCaptureState frame_start_capture_state,
    KeyBits frame_start_held_keys,
    KeyBits held_keys,
    KeyBits pressed_keys,
    KeyBits released_keys,
    MouseButtonBits frame_start_held_mouse_buttons,
    MouseButtonBits held_mouse_buttons,
    MouseButtonBits pressed_mouse_buttons,
    MouseButtonBits released_mouse_buttons,
    MouseButtonBits capture_discarded_mouse_buttons,
    const RelativeMouseDelta relative_mouse_delta,
    const MouseWheelDelta wheel_delta,
    const InputResetReason reset_reason,
    const InputStateStatistics statistics) noexcept
    : sequence_{sequence},
      focus_state_{focus_state},
      capture_state_{capture_state},
      frame_start_capture_state_{frame_start_capture_state},
      frame_start_held_keys_{std::move(frame_start_held_keys)},
      held_keys_{std::move(held_keys)},
      pressed_keys_{std::move(pressed_keys)},
      released_keys_{std::move(released_keys)},
      frame_start_held_mouse_buttons_{
          std::move(frame_start_held_mouse_buttons)},
      held_mouse_buttons_{std::move(held_mouse_buttons)},
      pressed_mouse_buttons_{std::move(pressed_mouse_buttons)},
      released_mouse_buttons_{std::move(released_mouse_buttons)},
      capture_discarded_mouse_buttons_{
          std::move(capture_discarded_mouse_buttons)},
      relative_mouse_delta_{relative_mouse_delta},
      wheel_delta_{wheel_delta},
      reset_reason_{reset_reason},
      statistics_{statistics}
{
}

bool InputSnapshot::key_held(const PhysicalKey key) const noexcept
{
    return is_valid(key) && held_keys_.test(key_index(key));
}

bool InputSnapshot::key_held_at_frame_start(
    const PhysicalKey key) const noexcept
{
    return is_valid(key) && frame_start_held_keys_.test(key_index(key));
}

bool InputSnapshot::key_pressed(const PhysicalKey key) const noexcept
{
    return is_valid(key) && pressed_keys_.test(key_index(key));
}

bool InputSnapshot::key_released(const PhysicalKey key) const noexcept
{
    return is_valid(key) && released_keys_.test(key_index(key));
}

bool InputSnapshot::mouse_button_held(const PhysicalMouseButton button) const noexcept
{
    return is_valid(button) && held_mouse_buttons_.test(mouse_button_index(button));
}

bool InputSnapshot::mouse_button_held_at_frame_start(
    const PhysicalMouseButton button) const noexcept
{
    return is_valid(button) &&
        frame_start_held_mouse_buttons_.test(mouse_button_index(button));
}

bool InputSnapshot::mouse_button_pressed(const PhysicalMouseButton button) const noexcept
{
    return is_valid(button) && pressed_mouse_buttons_.test(mouse_button_index(button));
}

bool InputSnapshot::mouse_button_released(const PhysicalMouseButton button) const noexcept
{
    return is_valid(button) && released_mouse_buttons_.test(mouse_button_index(button));
}

bool InputSnapshot::mouse_button_discarded_by_capture(
    const PhysicalMouseButton button) const noexcept
{
    return is_valid(button) &&
        capture_discarded_mouse_buttons_.test(mouse_button_index(button));
}

} // namespace hlclient::input
