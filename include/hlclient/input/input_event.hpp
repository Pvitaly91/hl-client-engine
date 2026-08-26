#pragma once

#include <cstdint>

namespace hlclient::input {

enum class InputCompatibilityProfile : std::uint8_t {
    local_keyboard_mouse_input_v1,
    evidence_pending_m4_6_2,
};

enum class PhysicalKey : std::uint8_t {
    w,
    a,
    s,
    d,
    space,
    left_control,
    left_shift,
    e,
    r,
    escape,
    tab,
    up,
    down,
    left,
    right,
    f1,
    f2,
    count,
};

enum class PhysicalMouseButton : std::uint8_t {
    left,
    right,
    middle,
    x1,
    x2,
    count,
};

enum class InputEventType : std::uint8_t {
    focus_gained,
    focus_lost,
    key_pressed,
    key_released,
    mouse_button_pressed,
    mouse_button_released,
    mouse_motion,
    mouse_wheel,
    capture_acquired,
    capture_released,
};

enum class InputFocusState : std::uint8_t {
    unfocused,
    focused,
};

enum class InputCaptureState : std::uint8_t {
    released,
    captured,
};

struct RelativeMouseDelta final {
    std::int32_t x{0};
    std::int32_t y{0};

    [[nodiscard]] friend constexpr bool operator==(
        const RelativeMouseDelta&,
        const RelativeMouseDelta&) noexcept = default;
};

struct MouseWheelDelta final {
    double horizontal{0.0};
    double vertical{0.0};

    [[nodiscard]] friend constexpr bool operator==(
        const MouseWheelDelta&,
        const MouseWheelDelta&) noexcept = default;
};

class InputEvent final {
public:
    [[nodiscard]] static constexpr InputEvent focus_gained() noexcept
    {
        return InputEvent{InputEventType::focus_gained};
    }

    [[nodiscard]] static constexpr InputEvent focus_lost() noexcept
    {
        return InputEvent{InputEventType::focus_lost};
    }

    [[nodiscard]] static constexpr InputEvent key_pressed(
        const PhysicalKey key,
        const bool repeated = false) noexcept
    {
        auto event = InputEvent{InputEventType::key_pressed};
        event.key_ = key;
        event.repeated_ = repeated;
        return event;
    }

    [[nodiscard]] static constexpr InputEvent key_released(const PhysicalKey key) noexcept
    {
        auto event = InputEvent{InputEventType::key_released};
        event.key_ = key;
        return event;
    }

    [[nodiscard]] static constexpr InputEvent mouse_button_pressed(
        const PhysicalMouseButton button) noexcept
    {
        auto event = InputEvent{InputEventType::mouse_button_pressed};
        event.mouse_button_ = button;
        return event;
    }

    [[nodiscard]] static constexpr InputEvent mouse_button_released(
        const PhysicalMouseButton button) noexcept
    {
        auto event = InputEvent{InputEventType::mouse_button_released};
        event.mouse_button_ = button;
        return event;
    }

    [[nodiscard]] static constexpr InputEvent mouse_motion(
        const std::int32_t delta_x,
        const std::int32_t delta_y) noexcept
    {
        auto event = InputEvent{InputEventType::mouse_motion};
        event.relative_mouse_delta_ = {delta_x, delta_y};
        return event;
    }

    [[nodiscard]] static constexpr InputEvent mouse_wheel(
        const double horizontal_steps,
        const double vertical_steps) noexcept
    {
        auto event = InputEvent{InputEventType::mouse_wheel};
        event.wheel_delta_ = {horizontal_steps, vertical_steps};
        return event;
    }

    [[nodiscard]] static constexpr InputEvent capture_acquired() noexcept
    {
        return InputEvent{InputEventType::capture_acquired};
    }

    [[nodiscard]] static constexpr InputEvent capture_released() noexcept
    {
        return InputEvent{InputEventType::capture_released};
    }

    [[nodiscard]] constexpr InputEventType type() const noexcept { return type_; }
    [[nodiscard]] constexpr PhysicalKey key() const noexcept { return key_; }
    [[nodiscard]] constexpr PhysicalMouseButton mouse_button() const noexcept
    {
        return mouse_button_;
    }
    [[nodiscard]] constexpr RelativeMouseDelta relative_mouse_delta() const noexcept
    {
        return relative_mouse_delta_;
    }
    [[nodiscard]] constexpr MouseWheelDelta wheel_delta() const noexcept
    {
        return wheel_delta_;
    }
    [[nodiscard]] constexpr bool repeated() const noexcept { return repeated_; }

private:
    explicit constexpr InputEvent(const InputEventType type) noexcept : type_{type} {}

    InputEventType type_{InputEventType::focus_lost};
    PhysicalKey key_{PhysicalKey::count};
    PhysicalMouseButton mouse_button_{PhysicalMouseButton::count};
    RelativeMouseDelta relative_mouse_delta_{};
    MouseWheelDelta wheel_delta_{};
    bool repeated_{false};
};

[[nodiscard]] constexpr bool is_valid(const PhysicalKey key) noexcept
{
    return static_cast<std::uint8_t>(key) < static_cast<std::uint8_t>(PhysicalKey::count);
}

[[nodiscard]] constexpr bool is_valid(const PhysicalMouseButton button) noexcept
{
    return static_cast<std::uint8_t>(button) <
        static_cast<std::uint8_t>(PhysicalMouseButton::count);
}

} // namespace hlclient::input
