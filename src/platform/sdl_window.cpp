#include <hlclient/platform/sdl_window.hpp>

#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>

namespace hlclient::platform {
namespace {

[[nodiscard]] std::string sdl_error(const char* operation)
{
    return std::string{operation} + ": " + SDL_GetError();
}

void set_gl_attribute(const SDL_GLAttr attribute, const int value, const char* description)
{
    if (!SDL_GL_SetAttribute(attribute, value)) {
        throw std::runtime_error{sdl_error(description)};
    }
}

} // namespace

struct SdlWindow::Impl final {
    ~Impl() noexcept
    {
        if (context != nullptr) {
            (void)SDL_GL_DestroyContext(context);
        }
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
    }

    SDL_Window* window{nullptr};
    SDL_GLContext context{nullptr};
    bool vsync{false};
};

SdlWindow::SdlWindow(const SdlWindowConfig& config) : implementation_{std::make_unique<Impl>()}
{
    if (config.width <= 0 || config.height <= 0) {
        throw std::invalid_argument{"SDL window dimensions must be positive"};
    }

    set_gl_attribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3, "Unable to request OpenGL major version");
    set_gl_attribute(SDL_GL_CONTEXT_MINOR_VERSION, 3, "Unable to request OpenGL minor version");
    set_gl_attribute(
        SDL_GL_CONTEXT_PROFILE_MASK,
        SDL_GL_CONTEXT_PROFILE_CORE,
        "Unable to request an OpenGL core profile");
    set_gl_attribute(SDL_GL_DOUBLEBUFFER, 1, "Unable to request OpenGL double buffering");
    set_gl_attribute(SDL_GL_DEPTH_SIZE, 24, "Unable to request a 24-bit OpenGL depth buffer");

#if !defined(NDEBUG)
    set_gl_attribute(
        SDL_GL_CONTEXT_FLAGS,
        SDL_GL_CONTEXT_DEBUG_FLAG,
        "Unable to request an OpenGL debug context");
#endif

    constexpr SDL_WindowFlags flags =
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    implementation_->window = SDL_CreateWindow(
        config.title.c_str(),
        config.width,
        config.height,
        flags);
    if (implementation_->window == nullptr) {
        throw std::runtime_error{sdl_error("SDL window creation failed")};
    }

    implementation_->context = SDL_GL_CreateContext(implementation_->window);
    if (implementation_->context == nullptr) {
        throw std::runtime_error{sdl_error("OpenGL context creation failed")};
    }

    if (!SDL_GL_MakeCurrent(implementation_->window, implementation_->context)) {
        throw std::runtime_error{sdl_error("Unable to make the OpenGL context current")};
    }

    implementation_->vsync = SDL_GL_SetSwapInterval(1);
    if (!implementation_->vsync) {
        (void)SDL_GL_SetSwapInterval(0);
    }
}

SdlWindow::~SdlWindow() noexcept = default;

bool SdlWindow::poll_event(WindowEvent& event) noexcept
{
    SDL_Event native_event{};
    while (SDL_PollEvent(&native_event)) {
        if (native_event.type == SDL_EVENT_QUIT ||
            native_event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
            (native_event.type == SDL_EVENT_KEY_DOWN && native_event.key.key == SDLK_ESCAPE)) {
            event = WindowEvent{WindowEventType::quit_requested, {}};
            return true;
        }

        if (native_event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            event = WindowEvent{
                WindowEventType::resized,
                PixelExtent{native_event.window.data1, native_event.window.data2},
            };
            return true;
        }
    }
    return false;
}

PixelExtent SdlWindow::pixel_extent() const
{
    PixelExtent result{};
    if (!SDL_GetWindowSizeInPixels(implementation_->window, &result.width, &result.height)) {
        throw std::runtime_error{sdl_error("Unable to query the OpenGL drawable size")};
    }
    return result;
}

bool SdlWindow::vsync_enabled() const noexcept
{
    return implementation_->vsync;
}

void SdlWindow::swap_buffers()
{
    if (!SDL_GL_SwapWindow(implementation_->window)) {
        throw std::runtime_error{sdl_error("OpenGL buffer swap failed")};
    }
}

} // namespace hlclient::platform
