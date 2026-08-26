#include "../src/platform/sdl_platform_event_translator.hpp"

#include <hlclient/input/input_event.hpp>
#include <hlclient/platform/platform_event.hpp>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <variant>

namespace {

namespace input = hlclient::input;
namespace platform = hlclient::platform;

[[nodiscard]] input::InputEvent require_input(
    const std::optional<platform::PlatformEvent>& translated)
{
    REQUIRE(translated);
    const auto* event = std::get_if<input::InputEvent>(&*translated);
    REQUIRE(event != nullptr);
    return *event;
}

TEST_CASE("SDL keyboard translation uses physical scancodes only", "[platform][sdl][input]")
{
    const platform::detail::SdlPlatformEventTranslator translator;
    constexpr std::pair<SDL_Scancode, input::PhysicalKey> mappings[]{
        {SDL_SCANCODE_W, input::PhysicalKey::w},
        {SDL_SCANCODE_A, input::PhysicalKey::a},
        {SDL_SCANCODE_S, input::PhysicalKey::s},
        {SDL_SCANCODE_D, input::PhysicalKey::d},
        {SDL_SCANCODE_SPACE, input::PhysicalKey::space},
        {SDL_SCANCODE_LCTRL, input::PhysicalKey::left_control},
        {SDL_SCANCODE_LSHIFT, input::PhysicalKey::left_shift},
        {SDL_SCANCODE_E, input::PhysicalKey::e},
        {SDL_SCANCODE_R, input::PhysicalKey::r},
        {SDL_SCANCODE_ESCAPE, input::PhysicalKey::escape},
        {SDL_SCANCODE_TAB, input::PhysicalKey::tab},
        {SDL_SCANCODE_UP, input::PhysicalKey::up},
        {SDL_SCANCODE_DOWN, input::PhysicalKey::down},
        {SDL_SCANCODE_LEFT, input::PhysicalKey::left},
        {SDL_SCANCODE_RIGHT, input::PhysicalKey::right},
        {SDL_SCANCODE_F1, input::PhysicalKey::f1},
        {SDL_SCANCODE_F2, input::PhysicalKey::f2},
    };

    for (const auto& [scancode, expected] : mappings) {
        SDL_Event native{};
        native.type = SDL_EVENT_KEY_DOWN;
        native.key.scancode = scancode;
        native.key.key = SDLK_Z;
        native.key.repeat = true;
        const auto translated = require_input(translator.translate(native));
        CHECK(translated.type() == input::InputEventType::key_pressed);
        CHECK(translated.key() == expected);
        CHECK(translated.repeated());
    }

    SDL_Event release{};
    release.type = SDL_EVENT_KEY_UP;
    release.key.scancode = SDL_SCANCODE_W;
    const auto released = require_input(translator.translate(release));
    CHECK(released.type() == input::InputEventType::key_released);
    CHECK(released.key() == input::PhysicalKey::w);

    SDL_Event unknown{};
    unknown.type = SDL_EVENT_KEY_DOWN;
    unknown.key.scancode = SDL_SCANCODE_Z;
    unknown.key.key = SDLK_W;
    CHECK_FALSE(translator.translate(unknown));
}

TEST_CASE("SDL mouse translation is bounded and ignores unknown buttons", "[platform][sdl][input]")
{
    const platform::detail::SdlPlatformEventTranslator translator;
    constexpr std::pair<std::uint8_t, input::PhysicalMouseButton> mappings[]{
        {static_cast<std::uint8_t>(SDL_BUTTON_LEFT), input::PhysicalMouseButton::left},
        {static_cast<std::uint8_t>(SDL_BUTTON_RIGHT), input::PhysicalMouseButton::right},
        {static_cast<std::uint8_t>(SDL_BUTTON_MIDDLE), input::PhysicalMouseButton::middle},
        {static_cast<std::uint8_t>(SDL_BUTTON_X1), input::PhysicalMouseButton::x1},
        {static_cast<std::uint8_t>(SDL_BUTTON_X2), input::PhysicalMouseButton::x2},
    };
    for (const auto& [native_button, expected] : mappings) {
        SDL_Event native{};
        native.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        native.button.button = native_button;
        const auto translated = require_input(translator.translate(native));
        CHECK(translated.type() == input::InputEventType::mouse_button_pressed);
        CHECK(translated.mouse_button() == expected);

        native.type = SDL_EVENT_MOUSE_BUTTON_UP;
        const auto released = require_input(translator.translate(native));
        CHECK(released.type() == input::InputEventType::mouse_button_released);
        CHECK(released.mouse_button() == expected);
    }

    SDL_Event unknown{};
    unknown.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    unknown.button.button = 255U;
    CHECK_FALSE(translator.translate(unknown));

    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 900.0F;
    motion.motion.y = 700.0F;
    motion.motion.xrel = 2.6F;
    motion.motion.yrel = -3.4F;
    const auto relative = require_input(translator.translate(motion));
    CHECK(relative.relative_mouse_delta() == input::RelativeMouseDelta{3, -3});

    motion.motion.xrel = std::numeric_limits<float>::infinity();
    CHECK_FALSE(translator.translate(motion));
}

TEST_CASE("SDL wheel focus quit and resize retain platform semantics", "[platform][sdl][input]")
{
    const platform::detail::SdlPlatformEventTranslator translator;

    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.x = 1.5F;
    wheel.wheel.y = -2.0F;
    wheel.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    const auto wheel_event = require_input(translator.translate(wheel));
    CHECK(wheel_event.wheel_delta() == input::MouseWheelDelta{-1.5, 2.0});

    SDL_Event focus{};
    focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    CHECK(require_input(translator.translate(focus)).type() == input::InputEventType::focus_lost);
    focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    CHECK(require_input(translator.translate(focus)).type() == input::InputEventType::focus_gained);

    SDL_Event resize{};
    resize.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
    resize.window.data1 = 640;
    resize.window.data2 = 480;
    const auto resized = translator.translate(resize);
    REQUIRE(resized);
    const auto* window = std::get_if<platform::WindowEvent>(&*resized);
    REQUIRE(window != nullptr);
    CHECK(window->type == platform::WindowEventType::resized);
    CHECK(window->extent == platform::PixelExtent{640, 480});

    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    const auto quitting = translator.translate(quit);
    REQUIRE(quitting);
    window = std::get_if<platform::WindowEvent>(&*quitting);
    REQUIRE(window != nullptr);
    CHECK(window->type == platform::WindowEventType::quit_requested);
}

} // namespace
