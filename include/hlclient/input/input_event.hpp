#pragma once

#include <cstdint>
#include <array>
#include <string_view>

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
    digit_1,
    digit_2,
    digit_3,
    digit_4,
    digit_5,
    f8,
    q,
    grave, enter, home, end, backspace, delete_key, page_up, page_down,
    b, c, f, g, h, i, j, k, l, m, n, o, p, t, u, v, x, y, z,
    digit_0, digit_6, digit_7, digit_8, digit_9,
    right_control, right_shift,
    left_alt, right_alt, f3, f4, f5, f6, f7, f9, f10, f11, f12,
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
    text_input,
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
    // Own SDL's transient UTF-8 bytes without changing the no-throw event pump.
    // Retain only complete codepoints; the editor separately validates Unicode.
    [[nodiscard]] static constexpr InputEvent text_input(std::string_view text) noexcept
    {
        auto event=InputEvent{InputEventType::text_input};
        std::size_t count=text.size()<event.text_.size()?text.size():event.text_.size();
        if(count<text.size())while(count>0&&(static_cast<unsigned char>(text[count])&0xc0U)==0x80U)--count;
        for(std::size_t i=0;i<count;++i)event.text_[i]=text[i];
        event.text_size_=static_cast<std::uint16_t>(count);
        return event;
    }
    [[nodiscard]] constexpr std::string_view text() const noexcept {return {text_.data(),text_size_};}
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
    // SDL timestamps and the dequeue clock are deliberately separate domains.
    // Synthetic/capture-recovery events have no native timestamp.
    constexpr void set_observation_clocks(std::uint64_t sdl_ns, std::uint64_t dequeue_ns) noexcept {
        sdl_timestamp_ns_=sdl_ns; dequeue_steady_ns_=dequeue_ns;
    }
    [[nodiscard]] constexpr std::uint64_t sdl_timestamp_ns() const noexcept { return sdl_timestamp_ns_; }
    [[nodiscard]] constexpr std::uint64_t dequeue_steady_ns() const noexcept { return dequeue_steady_ns_; }

private:
    explicit constexpr InputEvent(const InputEventType type) noexcept : type_{type} {}

    InputEventType type_{InputEventType::focus_lost};
    PhysicalKey key_{PhysicalKey::count};
    PhysicalMouseButton mouse_button_{PhysicalMouseButton::count};
    RelativeMouseDelta relative_mouse_delta_{};
    MouseWheelDelta wheel_delta_{};
    bool repeated_{false};
    std::array<char,128U> text_{};
    std::uint16_t text_size_{};
    std::uint64_t sdl_timestamp_ns_{},dequeue_steady_ns_{};
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
