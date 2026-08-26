#include <hlclient/platform/sdl_window.hpp>

#include <hlclient/input/input_state_tracker.hpp>

#include "sdl_platform_event_translator.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace hlclient::platform {
namespace {

std::atomic_bool sdl_event_pump_claimed{false};

[[nodiscard]] std::optional<SDL_WindowID> event_window_id(
    const SDL_Event& event) noexcept
{
    switch (event.type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return event.window.windowID;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        return event.key.windowID;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return event.button.windowID;
    case SDL_EVENT_MOUSE_MOTION:
        return event.motion.windowID;
    case SDL_EVENT_MOUSE_WHEEL:
        return event.wheel.windowID;
    default:
        return std::nullopt;
    }
}

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
    static constexpr std::size_t pending_event_capacity = 8U;

    ~Impl() noexcept
    {
        if (window != nullptr) {
            (void)SDL_SetWindowRelativeMouseMode(window, false);
            (void)SDL_ShowCursor();
        }
        if (context != nullptr) {
            (void)SDL_GL_DestroyContext(context);
        }
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
        if (owns_event_pump) {
            sdl_event_pump_claimed.store(false, std::memory_order_release);
        }
    }

    SDL_Window* window{nullptr};
    SDL_WindowID window_id{0U};
    SDL_GLContext context{nullptr};
    bool vsync{false};
    bool owns_event_pump{false};
    input::InputFocusState focus_state{input::InputFocusState::unfocused};
    input::InputCaptureState capture_state{input::InputCaptureState::released};
    std::array<PlatformEvent, pending_event_capacity> pending_events{};
    std::size_t pending_event_head{0U};
    std::size_t pending_event_count{0U};
    std::size_t native_events_in_current_drain{0U};
    bool native_event_pump_terminal{false};
    detail::SdlPlatformEventTranslator translator{};

    [[nodiscard]] bool pending_event_available() const noexcept
    {
        return pending_event_count < pending_events.size();
    }

    [[nodiscard]] bool capture_acquisition_event_available() const noexcept
    {
        // A successful acquisition must leave one slot for its mandatory
        // fail-safe release notification.
        return pending_event_count + 1U < pending_events.size();
    }

    [[nodiscard]] bool queue_event(PlatformEvent event) noexcept
    {
        if (!pending_event_available()) {
            return false;
        }
        const auto index =
            (pending_event_head + pending_event_count) % pending_events.size();
        pending_events[index] = std::move(event);
        ++pending_event_count;
        return true;
    }

    [[nodiscard]] bool pop_event(PlatformEvent& event) noexcept
    {
        if (pending_event_count == 0U) {
            return false;
        }
        event = std::move(pending_events[pending_event_head]);
        pending_event_head =
            (pending_event_head + 1U) % pending_events.size();
        --pending_event_count;
        return true;
    }

    [[nodiscard]] bool owns_keyboard_focus() const noexcept
    {
        return window != nullptr && SDL_GetKeyboardFocus() == window;
    }

    void refresh_capture_state() noexcept
    {
        capture_state = window != nullptr && owns_keyboard_focus() &&
                SDL_GetWindowRelativeMouseMode(window)
            ? input::InputCaptureState::captured
            : input::InputCaptureState::released;
    }

    [[nodiscard]] bool try_release_capture_and_show_cursor() noexcept
    {
        const bool relative_mode_disabled = window == nullptr ||
            SDL_SetWindowRelativeMouseMode(window, false);
        const bool cursor_shown = SDL_ShowCursor();
        const bool relative_mode_still_enabled =
            window != nullptr && SDL_GetWindowRelativeMouseMode(window);
        refresh_capture_state();
        return relative_mode_disabled && cursor_shown &&
            !relative_mode_still_enabled &&
            capture_state == input::InputCaptureState::released &&
            SDL_CursorVisible();
    }

    void queue_capture_recovery_failure() noexcept
    {
        (void)queue_event(PlatformEvent{WindowEvent{
            WindowEventType::input_capture_recovery_failed, {}}});
    }
};

SdlWindow::SdlWindow(const SdlWindowConfig& config) : implementation_{std::make_unique<Impl>()}
{
    bool expected = false;
    if (!sdl_event_pump_claimed.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        throw std::logic_error{
            "SdlWindow requires exclusive ownership of the SDL event pump"};
    }
    implementation_->owns_event_pump = true;

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

    SDL_WindowFlags flags =
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (config.hidden) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    implementation_->window = SDL_CreateWindow(
        config.title.c_str(),
        config.width,
        config.height,
        flags);
    if (implementation_->window == nullptr) {
        throw std::runtime_error{sdl_error("SDL window creation failed")};
    }
    implementation_->window_id = SDL_GetWindowID(implementation_->window);
    if (implementation_->window_id == 0U) {
        throw std::runtime_error{sdl_error("Unable to identify the SDL window")};
    }
    implementation_->focus_state =
        (SDL_GetWindowFlags(implementation_->window) & SDL_WINDOW_INPUT_FOCUS) != 0U
        ? input::InputFocusState::focused
        : input::InputFocusState::unfocused;

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

bool SdlWindow::poll_event(PlatformEvent& event) noexcept
{
    if (implementation_->native_event_pump_terminal) {
        return false;
    }
    if (implementation_->pop_event(event)) {
        return true;
    }

    SDL_Event native_event{};
    while (SDL_PollEvent(&native_event)) {
        if (implementation_->native_events_in_current_drain >=
            input::InputStateLimits::hard_maximum_events_per_frame) {
            implementation_->native_event_pump_terminal = true;
            implementation_->refresh_capture_state();
            bool recovered = true;
            if (implementation_->capture_state ==
                    input::InputCaptureState::captured ||
                SDL_GetWindowRelativeMouseMode(implementation_->window)) {
                recovered = implementation_->
                    try_release_capture_and_show_cursor();
            }
            event = PlatformEvent{WindowEvent{
                recovered
                    ? WindowEventType::native_event_limit_exceeded
                    : WindowEventType::input_capture_recovery_failed,
                {}}};
            return true;
        }
        ++implementation_->native_events_in_current_drain;
        const auto native_window_id = event_window_id(native_event);
        if (native_window_id &&
            *native_window_id != implementation_->window_id) {
            continue;
        }
        implementation_->refresh_capture_state();
        if (native_event.type == SDL_EVENT_KEY_DOWN &&
            native_event.key.scancode == SDL_SCANCODE_ESCAPE &&
            !native_event.key.repeat) {
            if (implementation_->capture_state == input::InputCaptureState::captured) {
                const auto translated = implementation_->translator.translate(native_event);
                const auto recovered =
                    implementation_->try_release_capture_and_show_cursor();
                if (recovered) {
                    (void)implementation_->queue_event(
                        PlatformEvent{input::InputEvent::capture_released()});
                } else {
                    implementation_->queue_capture_recovery_failure();
                }
                if (translated) {
                    event = *translated;
                    return true;
                }
                continue;
            }
            event = PlatformEvent{WindowEvent{WindowEventType::quit_requested, {}}};
            return true;
        }

        if (native_event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
            implementation_->focus_state = input::InputFocusState::focused;
            if (implementation_->capture_state ==
                    input::InputCaptureState::captured &&
                !implementation_->try_release_capture_and_show_cursor()) {
                implementation_->queue_capture_recovery_failure();
            }
        } else if (native_event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            implementation_->focus_state = input::InputFocusState::unfocused;
            if (!implementation_->try_release_capture_and_show_cursor()) {
                implementation_->queue_capture_recovery_failure();
            }
        }

        const auto translated = implementation_->translator.translate(native_event);
        if (translated) {
            event = *translated;
            return true;
        }
    }
    implementation_->native_events_in_current_drain = 0U;
    return false;
}

bool SdlWindow::poll_event(WindowEvent& event) noexcept
{
    PlatformEvent platform_event{WindowEvent{}};
    while (poll_event(platform_event)) {
        if (const auto* window_event = std::get_if<WindowEvent>(&platform_event)) {
            event = *window_event;
            return true;
        }
    }
    return false;
}

RelativeMouseCaptureResult SdlWindow::request_relative_mouse_capture(const bool enabled)
{
    if (implementation_->native_event_pump_terminal) {
        const bool was_relative =
            SDL_GetWindowRelativeMouseMode(implementation_->window);
        const auto recovered =
            implementation_->try_release_capture_and_show_cursor();
        if (!recovered) {
            return RelativeMouseCaptureResult{
                RelativeMouseCaptureStatus::failed,
                implementation_->capture_state,
                enabled
                    ? "Native event pump is terminal; capture acquisition was rejected and cleanup failed"
                    : "Native event pump is terminal and release cleanup failed",
            };
        }
        if (enabled) {
            return RelativeMouseCaptureResult{
                RelativeMouseCaptureStatus::failed,
                implementation_->capture_state,
                "Native event pump is terminal; capture acquisition is unavailable",
            };
        }
        return RelativeMouseCaptureResult{
            was_relative
                ? RelativeMouseCaptureStatus::released
                : RelativeMouseCaptureStatus::unchanged,
            implementation_->capture_state,
            {},
        };
    }
    if (SDL_GetWindowRelativeMouseMode(implementation_->window) &&
        !implementation_->owns_keyboard_focus() &&
        !implementation_->try_release_capture_and_show_cursor()) {
        implementation_->queue_capture_recovery_failure();
        return RelativeMouseCaptureResult{
            RelativeMouseCaptureStatus::failed,
            implementation_->capture_state,
            "Relative mouse capture was requested without operational focus and cleanup failed",
        };
    }
    implementation_->refresh_capture_state();
    const auto desired_state = enabled
        ? input::InputCaptureState::captured
        : input::InputCaptureState::released;
    if (desired_state == implementation_->capture_state) {
        if (!enabled && !SDL_ShowCursor()) {
            const auto diagnostic = sdl_error("Unable to restore the SDL cursor");
            if (implementation_->try_release_capture_and_show_cursor()) {
                return RelativeMouseCaptureResult{
                    RelativeMouseCaptureStatus::unchanged,
                    implementation_->capture_state,
                    {},
                };
            }
            implementation_->queue_capture_recovery_failure();
            return RelativeMouseCaptureResult{
                RelativeMouseCaptureStatus::failed,
                implementation_->capture_state,
                diagnostic + "; fail-closed cleanup failed",
            };
        }
        return RelativeMouseCaptureResult{
            RelativeMouseCaptureStatus::unchanged,
            implementation_->capture_state,
            {},
        };
    }


    if (enabled && !implementation_->capture_acquisition_event_available()) {
        return RelativeMouseCaptureResult{
            RelativeMouseCaptureStatus::failed,
            implementation_->capture_state,
            "Relative mouse capture transition queue has reserved its final release slot",
        };
    }

    if (!implementation_->pending_event_available()) {
        if (!enabled) {
            const auto recovered =
                implementation_->try_release_capture_and_show_cursor();
            return RelativeMouseCaptureResult{
                RelativeMouseCaptureStatus::failed,
                implementation_->capture_state,
                recovered
                    ? "Relative mouse capture was released but its transition queue is full"
                    : "Relative mouse capture transition queue is full and release cleanup failed",
            };
        }
        return RelativeMouseCaptureResult{
            RelativeMouseCaptureStatus::failed,
            implementation_->capture_state,
            "Relative mouse capture transition queue is full",
        };
    }

    if (enabled &&
        (implementation_->focus_state != input::InputFocusState::focused ||
            !implementation_->owns_keyboard_focus())) {
        const auto recovered =
            implementation_->try_release_capture_and_show_cursor();
        return RelativeMouseCaptureResult{
            RelativeMouseCaptureStatus::failed,
            implementation_->capture_state,
            recovered
                ? "Relative mouse capture requires operational keyboard focus"
                : "Relative mouse capture requires operational keyboard focus and cleanup failed",
        };
    }

    if (!SDL_SetWindowRelativeMouseMode(implementation_->window, enabled)) {
        auto diagnostic = sdl_error(enabled
            ? "Unable to enable SDL relative mouse mode"
            : "Unable to disable SDL relative mouse mode");
        const auto recovered =
            implementation_->try_release_capture_and_show_cursor();
        if (recovered) {
            (void)implementation_->queue_event(
                PlatformEvent{input::InputEvent::capture_released()});
        } else {
            diagnostic += "; fail-closed cleanup failed";
            implementation_->queue_capture_recovery_failure();
        }
        return RelativeMouseCaptureResult{
            RelativeMouseCaptureStatus::failed,
            implementation_->capture_state,
            diagnostic,
        };
    }

    implementation_->refresh_capture_state();
    if (implementation_->capture_state != desired_state) {
        auto diagnostic = sdl_error(
            "SDL relative mouse mode did not reach the requested state");
        if (implementation_->try_release_capture_and_show_cursor()) {
            (void)implementation_->queue_event(
                PlatformEvent{input::InputEvent::capture_released()});
        } else {
            diagnostic += "; fail-closed cleanup failed";
            implementation_->queue_capture_recovery_failure();
        }
        return RelativeMouseCaptureResult{
            RelativeMouseCaptureStatus::failed,
            implementation_->capture_state,
            diagnostic,
        };
    }
    auto status = enabled
        ? RelativeMouseCaptureStatus::acquired
        : RelativeMouseCaptureStatus::released;
    if (!enabled && !SDL_ShowCursor()) {
        auto diagnostic = sdl_error("Unable to restore the SDL cursor");
        if (!implementation_->try_release_capture_and_show_cursor()) {
            diagnostic += "; fail-closed cleanup failed";
            implementation_->queue_capture_recovery_failure();
            return RelativeMouseCaptureResult{
                RelativeMouseCaptureStatus::failed,
                implementation_->capture_state,
                diagnostic,
            };
        }
    }
    (void)implementation_->queue_event(PlatformEvent{enabled
            ? input::InputEvent::capture_acquired()
            : input::InputEvent::capture_released()});
    return RelativeMouseCaptureResult{status, implementation_->capture_state, {}};
}

input::InputFocusState SdlWindow::focus_state() const noexcept
{
    return implementation_->focus_state;
}

input::InputCaptureState SdlWindow::capture_state() const noexcept
{
    return implementation_->capture_state;
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
