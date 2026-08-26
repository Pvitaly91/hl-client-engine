#pragma once

#include <hlclient/input/input_event.hpp>

#include <bitset>
#include <cstddef>
#include <cstdint>

namespace hlclient::input {

inline constexpr std::size_t physical_key_count =
    static_cast<std::size_t>(PhysicalKey::count);
inline constexpr std::size_t physical_mouse_button_count =
    static_cast<std::size_t>(PhysicalMouseButton::count);

enum class InputResetReason : std::uint8_t {
    none,
    focus_lost,
};

struct InputStateStatistics final {
    std::size_t processed_event_count{0U};
    std::size_t ignored_repeat_count{0U};
    std::size_t ignored_duplicate_release_count{0U};
    std::size_t ignored_unfocused_event_count{0U};
    std::size_t ignored_uncaptured_motion_count{0U};
    std::size_t released_by_focus_loss_count{0U};
    std::size_t discarded_pre_capture_button_count{0U};
    std::size_t relative_mouse_clamp_count{0U};
    std::size_t wheel_clamp_count{0U};

    [[nodiscard]] friend constexpr bool operator==(
        const InputStateStatistics&,
        const InputStateStatistics&) noexcept = default;
};

class InputSnapshot final {
public:
    InputSnapshot(const InputSnapshot&) = default;
    InputSnapshot(InputSnapshot&&) noexcept = default;
    InputSnapshot& operator=(const InputSnapshot&) = delete;
    InputSnapshot& operator=(InputSnapshot&&) noexcept = delete;
    ~InputSnapshot() = default;

    [[nodiscard]] constexpr std::uint64_t sequence() const noexcept { return sequence_; }
    [[nodiscard]] constexpr InputCompatibilityProfile compatibility_profile() const noexcept
    {
        return compatibility_profile_;
    }
    [[nodiscard]] constexpr InputFocusState focus_state() const noexcept
    {
        return focus_state_;
    }
    [[nodiscard]] constexpr InputCaptureState capture_state() const noexcept
    {
        return capture_state_;
    }
    [[nodiscard]] constexpr InputCaptureState
    capture_state_at_frame_start() const noexcept
    {
        return frame_start_capture_state_;
    }
    [[nodiscard]] constexpr bool focused() const noexcept
    {
        return focus_state_ == InputFocusState::focused;
    }
    [[nodiscard]] constexpr bool captured() const noexcept
    {
        return capture_state_ == InputCaptureState::captured;
    }

    [[nodiscard]] bool key_held(PhysicalKey key) const noexcept;
    [[nodiscard]] bool key_held_at_frame_start(PhysicalKey key) const noexcept;
    [[nodiscard]] bool key_pressed(PhysicalKey key) const noexcept;
    [[nodiscard]] bool key_released(PhysicalKey key) const noexcept;
    [[nodiscard]] bool mouse_button_held(PhysicalMouseButton button) const noexcept;
    [[nodiscard]] bool mouse_button_held_at_frame_start(
        PhysicalMouseButton button) const noexcept;
    [[nodiscard]] bool mouse_button_pressed(PhysicalMouseButton button) const noexcept;
    [[nodiscard]] bool mouse_button_released(PhysicalMouseButton button) const noexcept;
    [[nodiscard]] bool mouse_button_discarded_by_capture(
        PhysicalMouseButton button) const noexcept;

    [[nodiscard]] constexpr RelativeMouseDelta relative_mouse_delta() const noexcept
    {
        return relative_mouse_delta_;
    }
    [[nodiscard]] constexpr MouseWheelDelta wheel_delta() const noexcept
    {
        return wheel_delta_;
    }
    [[nodiscard]] constexpr InputResetReason reset_reason() const noexcept
    {
        return reset_reason_;
    }
    [[nodiscard]] constexpr const InputStateStatistics& statistics() const noexcept
    {
        return statistics_;
    }

private:
    friend class InputStateTracker;

    using KeyBits = std::bitset<physical_key_count>;
    using MouseButtonBits = std::bitset<physical_mouse_button_count>;

    InputSnapshot(
        std::uint64_t sequence,
        InputFocusState focus_state,
        InputCaptureState capture_state,
        InputCaptureState frame_start_capture_state,
        KeyBits frame_start_held_keys,
        KeyBits held_keys,
        KeyBits pressed_keys,
        KeyBits released_keys,
        MouseButtonBits frame_start_held_mouse_buttons,
        MouseButtonBits held_mouse_buttons,
        MouseButtonBits pressed_mouse_buttons,
        MouseButtonBits released_mouse_buttons,
        MouseButtonBits capture_discarded_mouse_buttons,
        RelativeMouseDelta relative_mouse_delta,
        MouseWheelDelta wheel_delta,
        InputResetReason reset_reason,
        InputStateStatistics statistics) noexcept;

    std::uint64_t sequence_{0U};
    InputCompatibilityProfile compatibility_profile_{
        InputCompatibilityProfile::local_keyboard_mouse_input_v1};
    InputFocusState focus_state_{InputFocusState::unfocused};
    InputCaptureState capture_state_{InputCaptureState::released};
    InputCaptureState frame_start_capture_state_{InputCaptureState::released};
    KeyBits frame_start_held_keys_{};
    KeyBits held_keys_{};
    KeyBits pressed_keys_{};
    KeyBits released_keys_{};
    MouseButtonBits frame_start_held_mouse_buttons_{};
    MouseButtonBits held_mouse_buttons_{};
    MouseButtonBits pressed_mouse_buttons_{};
    MouseButtonBits released_mouse_buttons_{};
    MouseButtonBits capture_discarded_mouse_buttons_{};
    RelativeMouseDelta relative_mouse_delta_{};
    MouseWheelDelta wheel_delta_{};
    InputResetReason reset_reason_{InputResetReason::none};
    InputStateStatistics statistics_{};
};

} // namespace hlclient::input
