#include <hlclient/platform/sdl_runtime.hpp>
#include <hlclient/platform/sdl_window.hpp>
#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>
#include <memory>

TEST_CASE("E29 application overlay owns Escape before native capture release or close",
    "[e29][hud-input][sdl][no-stock]") {
    namespace platform=hlclient::platform;
    std::unique_ptr<platform::SdlRuntime> runtime;
    std::unique_ptr<platform::SdlWindow> window;
    try {
        runtime=std::make_unique<platform::SdlRuntime>();
        window=std::make_unique<platform::SdlWindow>(platform::SdlWindowConfig{"E29 Escape owner",64,64,true});
    } catch(const std::exception& error) {
        const auto capability=platform::classify_opengl_startup_capability_failure(error);
        if(capability!=platform::OpenGlStartupCapabilityFailure::none)SKIP("Typed hidden window capability unavailable");
        throw;
    }
    SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
    int count{};auto* windows=SDL_GetWindows(&count);
    REQUIRE(windows);REQUIRE(count==1);
    const auto id=SDL_GetWindowID(windows[0]);SDL_free(windows);REQUIRE(id);
    SDL_Event escape{};escape.type=SDL_EVENT_KEY_DOWN;escape.key.windowID=id;
    escape.key.scancode=SDL_SCANCODE_ESCAPE;
    platform::PlatformEvent event{platform::WindowEvent{}};
    window->set_escape_input_owned(true);
    REQUIRE(SDL_PushEvent(&escape));REQUIRE(window->poll_event(event));
    const auto* key=std::get_if<hlclient::input::InputEvent>(&event);
    REQUIRE(key);CHECK(key->type()==hlclient::input::InputEventType::key_pressed);
    CHECK(key->key()==hlclient::input::PhysicalKey::escape);
    CHECK(window->capture_state()==hlclient::input::InputCaptureState::released);
    CHECK_FALSE(window->poll_event(event));
    // With the overlay closed, the original uncaptured Escape close path is
    // restored. Focus loss/recovery remain independent native fail-safes.
    window->set_escape_input_owned(false);
    REQUIRE(SDL_PushEvent(&escape));REQUIRE(window->poll_event(event));
    const auto* close=std::get_if<platform::WindowEvent>(&event);
    REQUIRE(close);CHECK(close->type==platform::WindowEventType::quit_requested);
}
