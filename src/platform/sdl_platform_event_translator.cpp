#include "sdl_platform_event_translator.hpp"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace hlclient::platform::detail {
namespace {

[[nodiscard]] std::optional<input::PhysicalKey> translate_scancode(
    const SDL_Scancode scancode) noexcept
{
    switch (scancode) {
    case SDL_SCANCODE_W:
        return input::PhysicalKey::w;
    case SDL_SCANCODE_A:
        return input::PhysicalKey::a;
    case SDL_SCANCODE_S:
        return input::PhysicalKey::s;
    case SDL_SCANCODE_D:
        return input::PhysicalKey::d;
    case SDL_SCANCODE_SPACE:
        return input::PhysicalKey::space;
    case SDL_SCANCODE_LCTRL:
        return input::PhysicalKey::left_control;
    case SDL_SCANCODE_LSHIFT:
        return input::PhysicalKey::left_shift;
    case SDL_SCANCODE_E:
        return input::PhysicalKey::e;
    case SDL_SCANCODE_R:
        return input::PhysicalKey::r;
    case SDL_SCANCODE_ESCAPE:
        return input::PhysicalKey::escape;
    case SDL_SCANCODE_TAB:
        return input::PhysicalKey::tab;
    case SDL_SCANCODE_UP:
        return input::PhysicalKey::up;
    case SDL_SCANCODE_DOWN:
        return input::PhysicalKey::down;
    case SDL_SCANCODE_LEFT:
        return input::PhysicalKey::left;
    case SDL_SCANCODE_RIGHT:
        return input::PhysicalKey::right;
    case SDL_SCANCODE_F1:
        return input::PhysicalKey::f1;
    case SDL_SCANCODE_F2:
        return input::PhysicalKey::f2;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<input::PhysicalMouseButton> translate_mouse_button(
    const std::uint8_t button) noexcept
{
    switch (button) {
    case SDL_BUTTON_LEFT:
        return input::PhysicalMouseButton::left;
    case SDL_BUTTON_RIGHT:
        return input::PhysicalMouseButton::right;
    case SDL_BUTTON_MIDDLE:
        return input::PhysicalMouseButton::middle;
    case SDL_BUTTON_X1:
        return input::PhysicalMouseButton::x1;
    case SDL_BUTTON_X2:
        return input::PhysicalMouseButton::x2;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::int32_t> relative_axis(const float value) noexcept
{
    if (!std::isfinite(value)) {
        return std::nullopt;
    }
    const auto as_double = static_cast<double>(value);
    constexpr auto minimum = static_cast<double>(std::numeric_limits<std::int32_t>::min());
    constexpr auto maximum = static_cast<double>(std::numeric_limits<std::int32_t>::max());
    if (as_double < minimum || as_double > maximum) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(std::lround(as_double));
}

} // namespace

std::optional<PlatformEvent> SdlPlatformEventTranslator::translate(
    const SDL_Event& event) const noexcept
{
    switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return PlatformEvent{WindowEvent{WindowEventType::quit_requested, {}}};
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        return PlatformEvent{WindowEvent{
            WindowEventType::resized,
            PixelExtent{event.window.data1, event.window.data2},
        }};
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        return PlatformEvent{input::InputEvent::focus_gained()};
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return PlatformEvent{input::InputEvent::focus_lost()};
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const auto key = translate_scancode(event.key.scancode);
        if (!key) {
            return std::nullopt;
        }
        return PlatformEvent{event.type == SDL_EVENT_KEY_DOWN
            ? input::InputEvent::key_pressed(*key, event.key.repeat)
            : input::InputEvent::key_released(*key)};
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const auto button = translate_mouse_button(event.button.button);
        if (!button) {
            return std::nullopt;
        }
        return PlatformEvent{event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
            ? input::InputEvent::mouse_button_pressed(*button)
            : input::InputEvent::mouse_button_released(*button)};
    }
    case SDL_EVENT_MOUSE_MOTION: {
        const auto delta_x = relative_axis(event.motion.xrel);
        const auto delta_y = relative_axis(event.motion.yrel);
        if (!delta_x || !delta_y) {
            return std::nullopt;
        }
        return PlatformEvent{input::InputEvent::mouse_motion(*delta_x, *delta_y)};
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        auto horizontal = static_cast<double>(event.wheel.x);
        auto vertical = static_cast<double>(event.wheel.y);
        if (!std::isfinite(horizontal) || !std::isfinite(vertical)) {
            return std::nullopt;
        }
        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
            horizontal = -horizontal;
            vertical = -vertical;
        }
        return PlatformEvent{input::InputEvent::mouse_wheel(horizontal, vertical)};
    }
    default:
        return std::nullopt;
    }
}

} // namespace hlclient::platform::detail
