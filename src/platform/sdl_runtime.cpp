#include <hlclient/platform/sdl_runtime.hpp>
#include <hlclient/platform/sdl_window.hpp>

#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>
#include <algorithm>
#include <memory>
#include <vector>

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

void save_rgba_framebuffer_png(const std::string& output, const int width,
    const int height, const std::span<const std::uint8_t> rgba)
{
    constexpr std::uint64_t maximum_pixels=16U*1024U*1024U;
    if (width<=0 || height<=0 || output.empty() ||
        static_cast<std::uint64_t>(width)*static_cast<std::uint64_t>(height)>maximum_pixels ||
        rgba.size()!=static_cast<std::uint64_t>(width)*static_cast<std::uint64_t>(height)*4U) {
        throw std::invalid_argument{"Invalid bounded framebuffer PNG output"};
    }
    std::vector<std::uint8_t> top_down(rgba.size());
    const auto stride=static_cast<std::size_t>(width)*4U;
    for (int y=0;y<height;++y) {
        std::copy_n(rgba.data()+static_cast<std::size_t>(height-1-y)*stride,stride,
            top_down.data()+static_cast<std::size_t>(y)*stride);
    }
    std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> surface{
        SDL_CreateSurfaceFrom(width,height,SDL_PIXELFORMAT_RGBA32,top_down.data(),width*4),SDL_DestroySurface};
    if (!surface || !SDL_SavePNG(surface.get(),output.c_str())) {
        throw std::runtime_error{"Unable to save requested replay framebuffer PNG"};
    }
}

} // namespace hlclient::platform
