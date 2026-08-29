#include <hlclient/platform/sdl_runtime.hpp>
#include <hlclient/platform/sdl_window.hpp>

#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>

namespace hlclient::platform {
namespace {

bool runtime_active = false;

} // namespace

SdlRuntime::SdlRuntime()
{
    if (runtime_active) {
        throw std::logic_error{"Only one SDL runtime owner may be active"};
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        const std::string diagnostic{SDL_GetError()};
        const auto error =
            std::string{"SDL initialization failed: "} + diagnostic;
        SDL_Quit();
        if (proves_opengl_startup_capability_unavailable(
                OpenGlStartupCapabilityFailure::video_subsystem_unavailable,
                diagnostic)) {
            throw OpenGlStartupCapabilityError{
                OpenGlStartupCapabilityFailure::video_subsystem_unavailable,
                error};
        }
        throw std::runtime_error{error};
    }
    runtime_active = true;
}

SdlRuntime::~SdlRuntime() noexcept
{
    SDL_Quit();
    runtime_active = false;
}

} // namespace hlclient::platform
