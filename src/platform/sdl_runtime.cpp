#include <hlclient/platform/sdl_runtime.hpp>

#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>

namespace hlclient::platform {
namespace {

bool runtime_active = false;

[[nodiscard]] std::string sdl_error(const char* operation)
{
    return std::string{operation} + ": " + SDL_GetError();
}

} // namespace

SdlRuntime::SdlRuntime()
{
    if (runtime_active) {
        throw std::logic_error{"Only one SDL runtime owner may be active"};
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        const auto error = sdl_error("SDL initialization failed");
        SDL_Quit();
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
