#pragma once

#include <hlclient/input/input_event.hpp>

#include <variant>

namespace hlclient::platform {

struct PixelExtent final {
    int width{0};
    int height{0};

    [[nodiscard]] friend constexpr bool operator==(
        const PixelExtent&,
        const PixelExtent&) noexcept = default;
};

enum class WindowEventType {
    quit_requested,
    resized,
    input_capture_recovery_failed,
    native_event_limit_exceeded,
};

struct WindowEvent final {
    WindowEventType type{WindowEventType::resized};
    PixelExtent extent{};

    [[nodiscard]] friend constexpr bool operator==(
        const WindowEvent&,
        const WindowEvent&) noexcept = default;
};

using PlatformEvent = std::variant<WindowEvent, input::InputEvent>;

[[nodiscard]] inline bool is_window_event(const PlatformEvent& event) noexcept
{
    return std::holds_alternative<WindowEvent>(event);
}

[[nodiscard]] inline bool is_input_event(const PlatformEvent& event) noexcept
{
    return std::holds_alternative<input::InputEvent>(event);
}

} // namespace hlclient::platform
