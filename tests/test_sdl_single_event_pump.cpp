#include <hlclient/input/input_event.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <hlclient/platform/platform_event.hpp>
#include <hlclient/platform/sdl_runtime.hpp>
#include <hlclient/platform/sdl_window.hpp>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <variant>

namespace {

namespace input = hlclient::input;
namespace platform = hlclient::platform;

TEST_CASE("SdlWindow sole event pump preserves mixed native queue order", "[platform][sdl][pump]")
{
    std::unique_ptr<platform::SdlRuntime> runtime;
    std::unique_ptr<platform::SdlWindow> window;
    try {
        runtime = std::make_unique<platform::SdlRuntime>();
        window = std::make_unique<platform::SdlWindow>(platform::SdlWindowConfig{
            "HL Client SDL event-pump test",
            64,
            64,
            true,
        });
    } catch (const std::exception& error) {
        SKIP("SDL hidden OpenGL window unavailable: " << error.what());
    }

    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    int native_window_count = 0;
    SDL_Window** native_windows = SDL_GetWindows(&native_window_count);
    REQUIRE(native_windows != nullptr);
    REQUIRE(native_window_count == 1);
    const auto native_window_id = SDL_GetWindowID(native_windows[0U]);
    SDL_free(native_windows);
    REQUIRE(native_window_id != 0U);

    SDL_Event resize{};
    resize.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
    resize.window.windowID = native_window_id;
    resize.window.data1 = 320;
    resize.window.data2 = 200;
    REQUIRE(SDL_PushEvent(&resize));

    SDL_Event focus_gained{};
    focus_gained.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    focus_gained.window.windowID = native_window_id;
    REQUIRE(SDL_PushEvent(&focus_gained));

    const auto foreign_window_id = native_window_id == 1U
        ? SDL_WindowID{2U}
        : native_window_id - 1U;
    SDL_Event foreign_key{};
    foreign_key.type = SDL_EVENT_KEY_DOWN;
    foreign_key.key.windowID = foreign_window_id;
    foreign_key.key.scancode = SDL_SCANCODE_A;
    REQUIRE(SDL_PushEvent(&foreign_key));

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.windowID = native_window_id;
    key.key.scancode = SDL_SCANCODE_W;
    key.key.repeat = false;
    REQUIRE(SDL_PushEvent(&key));

    SDL_Event focus_lost{};
    focus_lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    focus_lost.window.windowID = native_window_id;
    REQUIRE(SDL_PushEvent(&focus_lost));

    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    REQUIRE(SDL_PushEvent(&quit));

    platform::PlatformEvent event{platform::WindowEvent{}};
    REQUIRE(window->poll_event(event));
    auto* window_event = std::get_if<platform::WindowEvent>(&event);
    REQUIRE(window_event != nullptr);
    CHECK(window_event->type == platform::WindowEventType::resized);
    CHECK(window_event->extent == platform::PixelExtent{320, 200});

    REQUIRE(window->poll_event(event));
    auto* input_event = std::get_if<input::InputEvent>(&event);
    REQUIRE(input_event != nullptr);
    CHECK(input_event->type() == input::InputEventType::focus_gained);
    CHECK(window->focus_state() == input::InputFocusState::focused);

    REQUIRE(window->poll_event(event));
    input_event = std::get_if<input::InputEvent>(&event);
    REQUIRE(input_event != nullptr);
    CHECK(input_event->type() == input::InputEventType::key_pressed);
    CHECK(input_event->key() == input::PhysicalKey::w);

    REQUIRE(window->poll_event(event));
    input_event = std::get_if<input::InputEvent>(&event);
    REQUIRE(input_event != nullptr);
    CHECK(input_event->type() == input::InputEventType::focus_lost);
    CHECK(window->focus_state() == input::InputFocusState::unfocused);
    CHECK(window->capture_state() == input::InputCaptureState::released);

    REQUIRE(window->poll_event(event));
    window_event = std::get_if<platform::WindowEvent>(&event);
    REQUIRE(window_event != nullptr);
    CHECK(window_event->type == platform::WindowEventType::quit_requested);
    CHECK_FALSE(window->poll_event(event));
}

TEST_CASE("SdlWindow rejects a second process-global SDL event-pump owner",
    "[platform][sdl][pump][ownership]")
{
    std::unique_ptr<platform::SdlRuntime> runtime;
    std::unique_ptr<platform::SdlWindow> window;
    try {
        runtime = std::make_unique<platform::SdlRuntime>();
        window = std::make_unique<platform::SdlWindow>(
            platform::SdlWindowConfig{
                "HL Client SDL event-pump owner test", 64, 64, true});
    } catch (const std::exception& error) {
        SKIP("SDL hidden OpenGL window unavailable: " << error.what());
    }

    REQUIRE_THROWS_AS(
        platform::SdlWindow(platform::SdlWindowConfig{
            "HL Client forbidden second event pump", 64, 64, true}),
        std::logic_error);
}

TEST_CASE("SdlWindow accepts the exact native poll-cycle hard limit",
    "[platform][sdl][pump][limits]")
{
    std::unique_ptr<platform::SdlRuntime> runtime;
    std::unique_ptr<platform::SdlWindow> window;
    try {
        runtime = std::make_unique<platform::SdlRuntime>();
        window = std::make_unique<platform::SdlWindow>(
            platform::SdlWindowConfig{
                "HL Client SDL native-limit exact test", 64, 64, true});
    } catch (const std::exception& error) {
        SKIP("SDL hidden OpenGL window unavailable: " << error.what());
    }

    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    int native_window_count = 0;
    SDL_Window** native_windows = SDL_GetWindows(&native_window_count);
    REQUIRE(native_windows != nullptr);
    REQUIRE(native_window_count == 1);
    const auto native_window_id = SDL_GetWindowID(native_windows[0U]);
    SDL_free(native_windows);
    REQUIRE(native_window_id != 0U);

    bool all_pushed = true;
    for (std::size_t index = 0U;
         index + 1U < input::InputStateLimits::hard_maximum_events_per_frame;
         ++index) {
        SDL_Event ignored{};
        ignored.type = SDL_EVENT_USER;
        if (!SDL_PushEvent(&ignored)) {
            all_pushed = false;
            break;
        }
    }
    REQUIRE(all_pushed);
    SDL_Event resize{};
    resize.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
    resize.window.windowID = native_window_id;
    resize.window.data1 = 320;
    resize.window.data2 = 200;
    REQUIRE(SDL_PushEvent(&resize));

    platform::PlatformEvent event{platform::WindowEvent{}};
    REQUIRE(window->poll_event(event));
    const auto* window_event = std::get_if<platform::WindowEvent>(&event);
    REQUIRE(window_event != nullptr);
    CHECK(window_event->type == platform::WindowEventType::resized);
    CHECK(window_event->extent == platform::PixelExtent{320, 200});
    CHECK_FALSE(window->poll_event(event));
}

TEST_CASE("SdlWindow rejects native hard limit plus one across mapped polls",
    "[platform][sdl][pump][limits][foreign-window][ordering]")
{
    std::unique_ptr<platform::SdlRuntime> runtime;
    std::unique_ptr<platform::SdlWindow> window;
    try {
        runtime = std::make_unique<platform::SdlRuntime>();
        window = std::make_unique<platform::SdlWindow>(
            platform::SdlWindowConfig{
                "HL Client SDL native-limit overflow test", 64, 64, true});
    } catch (const std::exception& error) {
        SKIP("SDL hidden OpenGL window unavailable: " << error.what());
    }

    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    int native_window_count = 0;
    SDL_Window** native_windows = SDL_GetWindows(&native_window_count);
    REQUIRE(native_windows != nullptr);
    REQUIRE(native_window_count == 1);
    const auto native_window_id = SDL_GetWindowID(native_windows[0U]);
    SDL_free(native_windows);
    REQUIRE(native_window_id != 0U);
    const auto foreign_window_id = native_window_id == 1U
        ? SDL_WindowID{2U}
        : native_window_id - 1U;

    SDL_Event first{};
    first.type = SDL_EVENT_KEY_DOWN;
    first.key.windowID = native_window_id;
    first.key.scancode = SDL_SCANCODE_W;
    REQUIRE(SDL_PushEvent(&first));

    bool all_pushed = true;
    for (std::size_t index = 1U;
         index < input::InputStateLimits::hard_maximum_events_per_frame;
         ++index) {
        SDL_Event foreign{};
        foreign.type = SDL_EVENT_KEY_DOWN;
        foreign.key.windowID = foreign_window_id;
        foreign.key.scancode = SDL_SCANCODE_A;
        if (!SDL_PushEvent(&foreign)) {
            all_pushed = false;
            break;
        }
    }
    REQUIRE(all_pushed);
    SDL_Event forbidden_sentinel{};
    forbidden_sentinel.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
    forbidden_sentinel.window.windowID = native_window_id;
    forbidden_sentinel.window.data1 = 640;
    forbidden_sentinel.window.data2 = 480;
    REQUIRE(SDL_PushEvent(&forbidden_sentinel));

    platform::PlatformEvent event{platform::WindowEvent{}};
    REQUIRE(window->poll_event(event));
    const auto* input_event = std::get_if<input::InputEvent>(&event);
    REQUIRE(input_event != nullptr);
    CHECK(input_event->type() == input::InputEventType::key_pressed);
    CHECK(input_event->key() == input::PhysicalKey::w);

    REQUIRE(window->poll_event(event));
    const auto* terminal = std::get_if<platform::WindowEvent>(&event);
    REQUIRE(terminal != nullptr);
    CHECK(terminal->type ==
        platform::WindowEventType::native_event_limit_exceeded);
    CHECK_FALSE(window->poll_event(event));

    const auto forbidden_capture =
        window->request_relative_mouse_capture(true);
    CHECK_FALSE(forbidden_capture);
    CHECK(forbidden_capture.status ==
        platform::RelativeMouseCaptureStatus::failed);
    CHECK(forbidden_capture.state == input::InputCaptureState::released);
    CHECK_FALSE(forbidden_capture.diagnostic.empty());
    CHECK(SDL_CursorVisible());
    const auto terminal_release =
        window->request_relative_mouse_capture(false);
    CHECK(terminal_release);
    CHECK(terminal_release.state == input::InputCaptureState::released);
    CHECK_FALSE(window->poll_event(event));
}

TEST_CASE("SdlWindow native overflow releases operational capture",
    "[platform][sdl][pump][limits][capture]")
{
    std::unique_ptr<platform::SdlRuntime> runtime;
    std::unique_ptr<platform::SdlWindow> window;
    try {
        runtime = std::make_unique<platform::SdlRuntime>();
        window = std::make_unique<platform::SdlWindow>(
            platform::SdlWindowConfig{
                "HL Client SDL captured native-limit test", 64, 64, true});
    } catch (const std::exception& error) {
        SKIP("SDL hidden OpenGL window unavailable: " << error.what());
    }

    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    int native_window_count = 0;
    SDL_Window** native_windows = SDL_GetWindows(&native_window_count);
    REQUIRE(native_windows != nullptr);
    REQUIRE(native_window_count == 1);
    const auto native_window_id = SDL_GetWindowID(native_windows[0U]);
    SDL_free(native_windows);
    REQUIRE(native_window_id != 0U);

    SDL_Event focus_gained{};
    focus_gained.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    focus_gained.window.windowID = native_window_id;
    REQUIRE(SDL_PushEvent(&focus_gained));
    platform::PlatformEvent event{platform::WindowEvent{}};
    REQUIRE(window->poll_event(event));
    REQUIRE_FALSE(window->poll_event(event));

    const auto capture = window->request_relative_mouse_capture(true);
    if (!capture) {
        const auto recovery = window->request_relative_mouse_capture(false);
        CHECK(recovery.state == input::InputCaptureState::released);
        CHECK(SDL_CursorVisible());
        SKIP("SDL hidden backend cannot acquire relative mouse mode");
    }
    REQUIRE(window->poll_event(event));
    REQUIRE(std::get_if<input::InputEvent>(&event) != nullptr);

    bool all_pushed = true;
    for (std::size_t index = 0U;
         index < input::InputStateLimits::hard_maximum_events_per_frame;
         ++index) {
        SDL_Event ignored{};
        ignored.type = SDL_EVENT_USER;
        if (!SDL_PushEvent(&ignored)) {
            all_pushed = false;
            break;
        }
    }
    REQUIRE(all_pushed);
    SDL_Event forbidden_sentinel{};
    forbidden_sentinel.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
    forbidden_sentinel.window.windowID = native_window_id;
    REQUIRE(SDL_PushEvent(&forbidden_sentinel));

    REQUIRE(window->poll_event(event));
    const auto* terminal = std::get_if<platform::WindowEvent>(&event);
    REQUIRE(terminal != nullptr);
    CHECK(terminal->type ==
        platform::WindowEventType::native_event_limit_exceeded);
    CHECK(window->capture_state() == input::InputCaptureState::released);
    CHECK(SDL_CursorVisible());
    CHECK_FALSE(window->poll_event(event));
}

TEST_CASE("SdlWindow retains ordered capture transitions until they are polled",
    "[platform][sdl][pump][capture][ordering]")
{
    std::unique_ptr<platform::SdlRuntime> runtime;
    std::unique_ptr<platform::SdlWindow> window;
    try {
        runtime = std::make_unique<platform::SdlRuntime>();
        window = std::make_unique<platform::SdlWindow>(
            platform::SdlWindowConfig{
                "HL Client SDL capture queue test", 64, 64, true});
    } catch (const std::exception& error) {
        SKIP("SDL hidden OpenGL window unavailable: " << error.what());
    }

    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    int native_window_count = 0;
    SDL_Window** native_windows = SDL_GetWindows(&native_window_count);
    REQUIRE(native_windows != nullptr);
    REQUIRE(native_window_count == 1);
    const auto native_window_id = SDL_GetWindowID(native_windows[0U]);
    SDL_free(native_windows);
    REQUIRE(native_window_id != 0U);

    SDL_Event focus_gained{};
    focus_gained.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    focus_gained.window.windowID = native_window_id;
    REQUIRE(SDL_PushEvent(&focus_gained));
    platform::PlatformEvent event{platform::WindowEvent{}};
    REQUIRE(window->poll_event(event));
    REQUIRE(std::get_if<input::InputEvent>(&event) != nullptr);

    const auto acquired = window->request_relative_mouse_capture(true);
    if (!acquired) {
        const auto recovery = window->request_relative_mouse_capture(false);
        CHECK(recovery.state == input::InputCaptureState::released);
        SKIP("SDL hidden backend cannot acquire relative mouse mode");
    }
    const auto released = window->request_relative_mouse_capture(false);
    if (!released) {
        CHECK(window->capture_state() == input::InputCaptureState::released);
        CHECK(SDL_CursorVisible());
        SKIP("SDL hidden backend could not complete capture release");
    }
    REQUIRE(window->capture_state() == input::InputCaptureState::released);

    REQUIRE(window->poll_event(event));
    const auto* transition = std::get_if<input::InputEvent>(&event);
    REQUIRE(transition != nullptr);
    CHECK(transition->type() == input::InputEventType::capture_acquired);
    REQUIRE(window->poll_event(event));
    transition = std::get_if<input::InputEvent>(&event);
    REQUIRE(transition != nullptr);
    CHECK(transition->type() == input::InputEventType::capture_released);

    for (std::size_t index = 0U; index < 8U; ++index) {
        const bool enable = index % 2U == 0U;
        const auto queued = window->request_relative_mouse_capture(enable);
        REQUIRE(queued);
        CHECK(queued.status == (enable
                ? platform::RelativeMouseCaptureStatus::acquired
                : platform::RelativeMouseCaptureStatus::released));
    }
    REQUIRE(window->capture_state() == input::InputCaptureState::released);
    const auto over_limit = window->request_relative_mouse_capture(true);
    REQUIRE_FALSE(over_limit);
    CHECK(over_limit.status == platform::RelativeMouseCaptureStatus::failed);
    CHECK(window->capture_state() == input::InputCaptureState::released);
    CHECK(SDL_CursorVisible());

    for (std::size_t index = 0U; index < 8U; ++index) {
        REQUIRE(window->poll_event(event));
        transition = std::get_if<input::InputEvent>(&event);
        REQUIRE(transition != nullptr);
        CHECK(transition->type() == (index % 2U == 0U
                ? input::InputEventType::capture_acquired
                : input::InputEventType::capture_released));
    }
}

TEST_CASE("SdlWindow capture lifecycle is typed and Escape restores the cursor",
    "[platform][sdl][pump][capture]")
{
    std::unique_ptr<platform::SdlRuntime> runtime;
    std::unique_ptr<platform::SdlWindow> window;
    try {
        runtime = std::make_unique<platform::SdlRuntime>();
        window = std::make_unique<platform::SdlWindow>(platform::SdlWindowConfig{
            "HL Client SDL capture test",
            64,
            64,
            true,
        });
    } catch (const std::exception& error) {
        SKIP("SDL hidden OpenGL window unavailable: " << error.what());
    }

    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    int native_window_count = 0;
    SDL_Window** native_windows = SDL_GetWindows(&native_window_count);
    REQUIRE(native_windows != nullptr);
    REQUIRE(native_window_count == 1);
    const auto native_window_id = SDL_GetWindowID(native_windows[0U]);
    SDL_free(native_windows);
    REQUIRE(native_window_id != 0U);
    SDL_Event focus_gained{};
    focus_gained.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    focus_gained.window.windowID = native_window_id;
    REQUIRE(SDL_PushEvent(&focus_gained));

    platform::PlatformEvent event{platform::WindowEvent{}};
    REQUIRE(window->poll_event(event));
    const auto* focus_event = std::get_if<input::InputEvent>(&event);
    REQUIRE(focus_event != nullptr);
    REQUIRE(focus_event->type() == input::InputEventType::focus_gained);

    const auto capture = window->request_relative_mouse_capture(true);
    CHECK(capture.state == window->capture_state());
    if (!capture) {
        CHECK(capture.status == platform::RelativeMouseCaptureStatus::failed);
        CHECK_FALSE(capture.diagnostic.empty());
        const auto recovery = window->request_relative_mouse_capture(false);
        CHECK(recovery.state == window->capture_state());
        CHECK(SDL_CursorVisible());
        SKIP("SDL hidden backend lacks operational keyboard focus/capture");
    }

    CHECK(capture.status == platform::RelativeMouseCaptureStatus::acquired);
    CHECK(capture.state == input::InputCaptureState::captured);
    REQUIRE(window->poll_event(event));
    const auto* capture_event = std::get_if<input::InputEvent>(&event);
    REQUIRE(capture_event != nullptr);
    CHECK(capture_event->type() == input::InputEventType::capture_acquired);

    SDL_Event escape{};
    escape.type = SDL_EVENT_KEY_DOWN;
    escape.key.windowID = native_window_id;
    escape.key.scancode = SDL_SCANCODE_ESCAPE;
    escape.key.repeat = false;
    REQUIRE(SDL_PushEvent(&escape));

    SDL_Event escape_repeat = escape;
    escape_repeat.key.repeat = true;
    REQUIRE(SDL_PushEvent(&escape_repeat));

    if (!window->poll_event(event)) {
        const auto recovery = window->request_relative_mouse_capture(false);
        REQUIRE(recovery);
        CHECK(window->capture_state() ==
            input::InputCaptureState::released);
        CHECK(SDL_CursorVisible());
        SKIP("Hidden SDL backend filtered synthetic captured key events");
    }
    const auto* escape_event = std::get_if<input::InputEvent>(&event);
    REQUIRE(escape_event != nullptr);
    CHECK(escape_event->type() == input::InputEventType::key_pressed);
    CHECK(escape_event->key() == input::PhysicalKey::escape);
    CHECK(window->capture_state() == input::InputCaptureState::released);
    CHECK(SDL_CursorVisible());

    REQUIRE(window->poll_event(event));
    const auto* release_event = std::get_if<input::InputEvent>(&event);
    REQUIRE(release_event != nullptr);
    CHECK(release_event->type() == input::InputEventType::capture_released);

    REQUIRE(window->poll_event(event));
    const auto* repeat_event = std::get_if<input::InputEvent>(&event);
    REQUIRE(repeat_event != nullptr);
    CHECK(repeat_event->type() == input::InputEventType::key_pressed);
    CHECK(repeat_event->key() == input::PhysicalKey::escape);
    CHECK(repeat_event->repeated());
    CHECK_FALSE(window->poll_event(event));
}

TEST_CASE("Captured SDL focus loss clears tracker state and restores the cursor",
    "[platform][sdl][pump][capture][focus-loss][integration]")
{
    std::unique_ptr<platform::SdlRuntime> runtime;
    std::unique_ptr<platform::SdlWindow> window;
    try {
        runtime = std::make_unique<platform::SdlRuntime>();
        window = std::make_unique<platform::SdlWindow>(platform::SdlWindowConfig{
            "HL Client SDL focus-loss test",
            64,
            64,
            true,
        });
    } catch (const std::exception& error) {
        SKIP("SDL hidden OpenGL window unavailable: " << error.what());
    }

    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    int native_window_count = 0;
    SDL_Window** native_windows = SDL_GetWindows(&native_window_count);
    REQUIRE(native_windows != nullptr);
    REQUIRE(native_window_count == 1);
    const auto native_window_id = SDL_GetWindowID(native_windows[0U]);
    SDL_free(native_windows);
    REQUIRE(native_window_id != 0U);

    SDL_Event focus_gained{};
    focus_gained.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    focus_gained.window.windowID = native_window_id;
    REQUIRE(SDL_PushEvent(&focus_gained));
    platform::PlatformEvent event{platform::WindowEvent{}};
    REQUIRE(window->poll_event(event));
    REQUIRE(std::get_if<input::InputEvent>(&event) != nullptr);

    const auto capture = window->request_relative_mouse_capture(true);
    if (!capture) {
        const auto recovery = window->request_relative_mouse_capture(false);
        CHECK(recovery.state == input::InputCaptureState::released);
        CHECK(SDL_CursorVisible());
        SKIP("SDL hidden backend cannot acquire relative mouse mode");
    }
    REQUIRE(window->poll_event(event));
    const auto* acquired = std::get_if<input::InputEvent>(&event);
    REQUIRE(acquired != nullptr);
    REQUIRE(acquired->type() == input::InputEventType::capture_acquired);

    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(*acquired);
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::w));
    tracker.apply_event(input::InputEvent::mouse_motion(25, -10));

    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    SDL_Event focus_lost{};
    focus_lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    focus_lost.window.windowID = native_window_id;
    REQUIRE(SDL_PushEvent(&focus_lost));
    if (!window->poll_event(event)) {
        const auto recovery = window->request_relative_mouse_capture(false);
        REQUIRE(recovery);
        SKIP("Hidden SDL backend filtered synthetic focus loss");
    }
    const auto* lost = std::get_if<input::InputEvent>(&event);
    REQUIRE(lost != nullptr);
    REQUIRE(lost->type() == input::InputEventType::focus_lost);
    tracker.apply_event(*lost);
    const auto reset = tracker.publish_snapshot();
    tracker.end_frame();

    CHECK(window->focus_state() == input::InputFocusState::unfocused);
    CHECK(window->capture_state() == input::InputCaptureState::released);
    CHECK(SDL_CursorVisible());
    CHECK_FALSE(reset.focused());
    CHECK_FALSE(reset.captured());
    CHECK_FALSE(reset.key_held(input::PhysicalKey::w));
    CHECK(reset.relative_mouse_delta() == input::RelativeMouseDelta{});
    CHECK(reset.reset_reason() == input::InputResetReason::focus_lost);

    tracker.begin_frame();
    const auto stable = tracker.publish_snapshot();
    tracker.end_frame();
    CHECK(stable.sequence() == reset.sequence() + 1U);
    CHECK_FALSE(stable.key_held(input::PhysicalKey::w));
    CHECK(stable.relative_mouse_delta() == input::RelativeMouseDelta{});
    CHECK(stable.reset_reason() == input::InputResetReason::none);
}

} // namespace
