#include <hlclient/platform/sdl_window.hpp>

#include <hlclient/input/input_state_tracker.hpp>
#include <hlclient/core/multiplayer_diagnostics.hpp>

#include "sdl_platform_event_translator.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace hlclient::platform {
namespace {

std::atomic_bool sdl_event_pump_claimed{false};
std::atomic<std::uint64_t> next_video_revision{1U};

[[nodiscard]] std::optional<SDL_WindowID> event_window_id(
    const SDL_Event& event) noexcept
{
    if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST)
        return event.window.windowID;
    switch (event.type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return event.window.windowID;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        return event.key.windowID;
    case SDL_EVENT_TEXT_INPUT:
        return event.text.windowID;
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

[[nodiscard]] bool contains_ascii_case_insensitive(
    const std::string_view text,
    const std::string_view pattern) noexcept
{
    if (pattern.empty() || pattern.size() > text.size()) {
        return false;
    }
    const auto lower = [](const char value) noexcept {
        return value >= 'A' && value <= 'Z'
            ? static_cast<char>(value - 'A' + 'a')
            : value;
    };
    for (std::size_t offset = 0U; offset + pattern.size() <= text.size();
         ++offset) {
        bool equal = true;
        for (std::size_t index = 0U; index < pattern.size(); ++index) {
            if (lower(text[offset + index]) != lower(pattern[index])) {
                equal = false;
                break;
            }
        }
        if (equal) {
            return true;
        }
    }
    return false;
}

[[noreturn]] void throw_sdl_opengl_startup_failure(
    const OpenGlStartupCapabilityFailure capability,
    const char* operation)
{
    const std::string diagnostic{SDL_GetError()};
    auto context = std::string{operation} + ": " + diagnostic;
    if (proves_opengl_startup_capability_unavailable(
            capability, diagnostic)) {
        throw OpenGlStartupCapabilityError{capability, std::move(context)};
    }
    throw std::runtime_error{std::move(context)};
}

void set_gl_attribute(const SDL_GLAttr attribute, const int value, const char* description)
{
    if (!SDL_GL_SetAttribute(attribute, value)) {
        throw_sdl_opengl_startup_failure(
            OpenGlStartupCapabilityFailure::context_attribute_unavailable,
            description);
    }
}

} // namespace

std::string_view to_string(
    const OpenGlStartupCapabilityFailure failure) noexcept
{
    switch (failure) {
    case OpenGlStartupCapabilityFailure::none:
        return "none";
    case OpenGlStartupCapabilityFailure::video_subsystem_unavailable:
        return "video_subsystem_unavailable";
    case OpenGlStartupCapabilityFailure::window_unavailable:
        return "window_unavailable";
    case OpenGlStartupCapabilityFailure::context_attribute_unavailable:
        return "context_attribute_unavailable";
    case OpenGlStartupCapabilityFailure::context_unavailable:
        return "context_unavailable";
    case OpenGlStartupCapabilityFailure::context_activation_unavailable:
        return "context_activation_unavailable";
    case OpenGlStartupCapabilityFailure::function_loading_unavailable:
        return "function_loading_unavailable";
    case OpenGlStartupCapabilityFailure::legacy_context:
        return "legacy_context";
    }
    return "unknown";
}

OpenGlStartupCapabilityError::OpenGlStartupCapabilityError(
    const OpenGlStartupCapabilityFailure failure,
    std::string context)
    : std::runtime_error{std::move(context)}, failure_{failure}
{
}

OpenGlStartupCapabilityFailure OpenGlStartupCapabilityError::failure()
    const noexcept
{
    return failure_;
}

OpenGlStartupCapabilityFailure
classify_opengl_startup_capability_failure(
    const std::exception& error) noexcept
{
    if (const auto* capability =
            dynamic_cast<const OpenGlStartupCapabilityError*>(&error)) {
        return capability->failure();
    }
    // Do not classify logic_error or bad_alloc by their text.  SDL failures
    // must be converted to the typed form at their startup boundary only after
    // their diagnostic proves capability unavailability.
    if (dynamic_cast<const std::logic_error*>(&error) != nullptr ||
        dynamic_cast<const std::bad_alloc*>(&error) != nullptr) {
        return OpenGlStartupCapabilityFailure::none;
    }
    const auto* runtime = dynamic_cast<const std::runtime_error*>(&error);
    if (runtime == nullptr) {
        return OpenGlStartupCapabilityFailure::none;
    }
    const std::string_view message{runtime->what()};
    if (message == "glad2 failed to load OpenGL functions") {
        return OpenGlStartupCapabilityFailure::function_loading_unavailable;
    }
    if (message.starts_with("OpenGL 3.3 Core is required,")) {
        return OpenGlStartupCapabilityFailure::legacy_context;
    }
    return OpenGlStartupCapabilityFailure::none;
}

bool proves_opengl_startup_capability_unavailable(
    const OpenGlStartupCapabilityFailure failure,
    const std::string_view diagnostic) noexcept
{
    // Never turn an allocation/resource exhaustion report into a successful
    // capability skip, even when the same text also mentions OpenGL.
    if (contains_ascii_case_insensitive(diagnostic, "memory") ||
        contains_ascii_case_insensitive(diagnostic, "allocation") ||
        contains_ascii_case_insensitive(diagnostic, "resource exhausted") ||
        contains_ascii_case_insensitive(diagnostic, "resource limit")) {
        return false;
    }

    const bool names_video =
        contains_ascii_case_insensitive(diagnostic, "video device") ||
        contains_ascii_case_insensitive(diagnostic, "video driver");
    const bool names_opengl =
        contains_ascii_case_insensitive(diagnostic, "opengl") ||
        contains_ascii_case_insensitive(diagnostic, "gl context");
    const bool explicitly_limited =
        contains_ascii_case_insensitive(diagnostic, "no available") ||
        contains_ascii_case_insensitive(diagnostic, "not available") ||
        contains_ascii_case_insensitive(diagnostic, "unavailable") ||
        contains_ascii_case_insensitive(diagnostic, "not supported") ||
        contains_ascii_case_insensitive(diagnostic, "unsupported") ||
        contains_ascii_case_insensitive(diagnostic, "not configured");
    if (!explicitly_limited) {
        return false;
    }
    switch (failure) {
    case OpenGlStartupCapabilityFailure::video_subsystem_unavailable:
        return names_video;
    case OpenGlStartupCapabilityFailure::window_unavailable:
        return names_video || names_opengl;
    case OpenGlStartupCapabilityFailure::context_attribute_unavailable:
    case OpenGlStartupCapabilityFailure::context_unavailable:
    case OpenGlStartupCapabilityFailure::context_activation_unavailable:
        return true;
    case OpenGlStartupCapabilityFailure::function_loading_unavailable:
    case OpenGlStartupCapabilityFailure::legacy_context:
    case OpenGlStartupCapabilityFailure::none:
        return false;
    }
    return false;
}

struct SdlWindow::Impl final {
    static constexpr std::size_t pending_event_capacity = 8U;

    ~Impl() noexcept
    {
        if (window != nullptr) {
            (void)SDL_StopTextInput(window);
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
    bool escape_input_owned{false};
    bool input_diagnostic_clocks{false};
    bool text_input_requested{false};
    bool suppress_toggle_text{false};
    core::VideoSettings requested_video;
    bool safe_window_recovery{};
    std::string video_recovery_reason;
    mutable std::optional<VideoWindowState> cached_video;
    // A replacement window/context never aliases the previous cache identity,
    // including console reconnect in the same application process.
    std::uint64_t video_revision{next_video_revision.fetch_add(1U,std::memory_order_relaxed)};
    mutable std::uint64_t video_refresh_count{};
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

    implementation_->requested_video = {config.display_index, config.mode,
        config.width, config.height, config.vsync, config.msaa_samples};
    if (!core::valid_video_settings(implementation_->requested_video)) {
        throw std::invalid_argument{"SDL video settings are outside the bounded contract"};
    }

    const auto displays = enumerate_video_displays();
    if (displays.empty()) throw std::runtime_error{"SDL has no usable display"};
    const auto selected_index = config.display_index < displays.size() ? config.display_index : 0U;
    const auto& display = displays[selected_index];
    SDL_Rect usable{0,0,display.desktop_width,display.desktop_height};
    if (!SDL_GetDisplayUsableBounds(display.id,&usable) || usable.w<=0 || usable.h<=0)
        usable={0,0,display.desktop_width,display.desktop_height};
    const int safe_width=std::max(1,std::min(1280,usable.w-32));
    const int safe_height=std::max(1,std::min(720,usable.h-64));
    auto requested_mode = config.mode;
    if (selected_index != config.display_index) {
        requested_mode = core::VideoWindowMode::windowed;
        implementation_->safe_window_recovery = true;
        implementation_->video_recovery_reason = "selected_display_unavailable";
    }
    std::optional<SDL_DisplayMode> exclusive_display_mode;
    if (requested_mode == core::VideoWindowMode::exclusive) {
        SDL_DisplayMode closest{};
        // Do not silently run a different fullscreen resolution. SDL's
        // closest-mode search may otherwise select a larger display mode.
        if (SDL_GetClosestFullscreenDisplayMode(display.id,config.width,config.height,
                0.0F,false,&closest) && closest.w==config.width && closest.h==config.height)
            exclusive_display_mode=closest;
        else {
            requested_mode=core::VideoWindowMode::windowed;
            implementation_->safe_window_recovery=true;
            implementation_->video_recovery_reason="exclusive_resolution_unavailable";
        }
    }
    // Hidden offscreen windows intentionally retain the exact caller extent,
    // including larger-than-desktop GPU fixtures. Only interactive windows
    // need this desktop-fit recovery; unavailable displays/modes still recover.
    if (!config.hidden && !implementation_->safe_window_recovery && requested_mode == core::VideoWindowMode::windowed &&
        (config.width>usable.w || config.height>usable.h)) {
        implementation_->safe_window_recovery = true;
        implementation_->video_recovery_reason = "window_resolution_exceeds_display";
    }
    int initial_width = config.width, initial_height = config.height;
    if (implementation_->safe_window_recovery) {
        initial_width = safe_width;
        initial_height = safe_height;
    }

    set_gl_attribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3, "Unable to request OpenGL major version");
    set_gl_attribute(SDL_GL_CONTEXT_MINOR_VERSION, 3, "Unable to request OpenGL minor version");
    set_gl_attribute(
        SDL_GL_CONTEXT_PROFILE_MASK,
        SDL_GL_CONTEXT_PROFILE_CORE,
        "Unable to request an OpenGL core profile");
    set_gl_attribute(SDL_GL_DOUBLEBUFFER, 1, "Unable to request OpenGL double buffering");
    set_gl_attribute(SDL_GL_DEPTH_SIZE, 24, "Unable to request a 24-bit OpenGL depth buffer");
    // Antialiasing belongs to the production scene FBO. Reset sticky SDL
    // attributes so earlier contexts cannot silently add native MSAA too.
    set_gl_attribute(SDL_GL_MULTISAMPLEBUFFERS, 0, "Unable to disable native MSAA buffers");
    set_gl_attribute(SDL_GL_MULTISAMPLESAMPLES, 0, "Unable to disable native MSAA samples");

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
        initial_width,
        initial_height,
        flags);
    if (implementation_->window == nullptr) {
        throw_sdl_opengl_startup_failure(
            OpenGlStartupCapabilityFailure::window_unavailable,
            "SDL window creation failed");
    }
    implementation_->window_id = SDL_GetWindowID(implementation_->window);
    if (implementation_->window_id == 0U) {
        throw std::runtime_error{sdl_error("Unable to identify the SDL window")};
    }
    const auto centered = static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(display.id));
    if ((!config.hidden || selected_index != 0U || requested_mode != core::VideoWindowMode::windowed) &&
        (!SDL_SetWindowPosition(implementation_->window, centered, centered) ||
         !SDL_SyncWindow(implementation_->window))) {
        implementation_->safe_window_recovery = true;
        implementation_->video_recovery_reason = "window_position_not_confirmed";
        requested_mode = core::VideoWindowMode::windowed;
    }
    if (requested_mode == core::VideoWindowMode::borderless ||
        requested_mode == core::VideoWindowMode::exclusive) {
        const bool exclusive=requested_mode==core::VideoWindowMode::exclusive;
        const bool completed = SDL_SetWindowFullscreenMode(implementation_->window,
                exclusive ? &*exclusive_display_mode : nullptr) &&
            SDL_SetWindowFullscreen(implementation_->window, true) &&
            SDL_SyncWindow(implementation_->window);
        int pixel_width{}, pixel_height{};
        const bool dimensions = SDL_GetWindowSizeInPixels(implementation_->window, &pixel_width, &pixel_height);
        const auto* actual_fullscreen_mode=SDL_GetWindowFullscreenMode(implementation_->window);
        const auto* actual_display_mode=SDL_GetCurrentDisplayMode(display.id);
        const bool mode_confirmed=exclusive
            ? actual_fullscreen_mode && actual_display_mode &&
              actual_fullscreen_mode->w==config.width && actual_fullscreen_mode->h==config.height &&
              actual_display_mode->w==config.width && actual_display_mode->h==config.height
            : actual_fullscreen_mode==nullptr;
        const bool confirmed = completed && dimensions && mode_confirmed &&
            (SDL_GetWindowFlags(implementation_->window) & SDL_WINDOW_FULLSCREEN) != 0U &&
            SDL_GetDisplayForWindow(implementation_->window) == display.id &&
            pixel_width == (exclusive ? config.width : display.desktop_width) &&
            pixel_height == (exclusive ? config.height : display.desktop_height);
        if (!confirmed) {
            implementation_->safe_window_recovery = true;
            implementation_->video_recovery_reason = exclusive
                ? "exclusive_mode_not_confirmed" : "borderless_mode_not_confirmed";
            // Always leave fullscreen first and clear the exclusive mode
            // request before restoring a bounded desktop window.
            const bool exited_fullscreen=SDL_SetWindowFullscreen(implementation_->window,false);
            const bool reset_mode=SDL_SetWindowFullscreenMode(implementation_->window,nullptr);
            const bool resized=SDL_SetWindowSize(implementation_->window,safe_width,safe_height);
            const bool synchronized=SDL_SyncWindow(implementation_->window);
            if (!exited_fullscreen || !reset_mode || !resized || !synchronized ||
                (SDL_GetWindowFlags(implementation_->window) & SDL_WINDOW_FULLSCREEN) != 0U)
                throw std::runtime_error{"Unable to recover the safe windowed video mode"};
        }
    }
    implementation_->focus_state =
        (SDL_GetWindowFlags(implementation_->window) & SDL_WINDOW_INPUT_FOCUS) != 0U
        ? input::InputFocusState::focused
        : input::InputFocusState::unfocused;

    implementation_->context = SDL_GL_CreateContext(implementation_->window);
    if (implementation_->context == nullptr) {
        throw_sdl_opengl_startup_failure(
            OpenGlStartupCapabilityFailure::context_unavailable,
            "OpenGL context creation failed");
    }

    if (!SDL_GL_MakeCurrent(implementation_->window, implementation_->context)) {
        throw_sdl_opengl_startup_failure(
            OpenGlStartupCapabilityFailure::context_activation_unavailable,
            "Unable to make the OpenGL context current");
    }

    const bool swap_request = SDL_GL_SetSwapInterval(config.vsync ? 1 : 0);
    if (!swap_request) {
        (void)SDL_GL_SetSwapInterval(0);
    }
    int actual_interval{};
    implementation_->vsync = SDL_GL_GetSwapInterval(&actual_interval) && actual_interval != 0;
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
        const auto diagnostic_dequeue_ns=implementation_->input_diagnostic_clocks
            ? core::multiplayer_diagnostic_now_ns() : 0U;
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
        const auto stamp_input=[&](PlatformEvent& value) noexcept {
            if(implementation_->input_diagnostic_clocks)
                if(auto* input=std::get_if<input::InputEvent>(&value))
                    input->set_observation_clocks(native_event.common.timestamp,
                        diagnostic_dequeue_ns);
        };
        const auto native_window_id = event_window_id(native_event);
        if (native_window_id &&
            *native_window_id != implementation_->window_id) {
            continue;
        }
        // Invalidation is owned by the sole SDL event pump. Input/motion does
        // not trigger display enumeration or GL metadata queries. Context is
        // immutable for this window's lifetime; reset events invalidate too.
        if ((native_event.type >= SDL_EVENT_DISPLAY_FIRST &&
             native_event.type <= SDL_EVENT_DISPLAY_LAST) ||
            native_event.type == SDL_EVENT_WINDOW_MOVED ||
            native_event.type == SDL_EVENT_WINDOW_RESIZED ||
            native_event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
            native_event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
            native_event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
            native_event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN ||
            native_event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN ||
            native_event.type == SDL_EVENT_WINDOW_MINIMIZED ||
            native_event.type == SDL_EVENT_WINDOW_MAXIMIZED ||
            native_event.type == SDL_EVENT_WINDOW_RESTORED ||
            native_event.type == SDL_EVENT_RENDER_TARGETS_RESET ||
            native_event.type == SDL_EVENT_RENDER_DEVICE_RESET) {
            implementation_->video_revision=next_video_revision.fetch_add(1U,std::memory_order_relaxed);
            implementation_->cached_video.reset();
        }
        implementation_->refresh_capture_state();
        if(native_event.type==SDL_EVENT_KEY_DOWN) {
            implementation_->suppress_toggle_text =
                native_event.key.scancode==SDL_SCANCODE_GRAVE;
        }
        if(native_event.type==SDL_EVENT_TEXT_INPUT&&
           (!implementation_->text_input_requested||implementation_->suppress_toggle_text||
            implementation_->focus_state!=input::InputFocusState::focused))continue;
        if (native_event.type == SDL_EVENT_KEY_DOWN &&
            native_event.key.scancode == SDL_SCANCODE_ESCAPE &&
            !native_event.key.repeat) {
            if (implementation_->escape_input_owned) {
                if (const auto translated = implementation_->translator.translate(native_event)) {
                    event = *translated;
                    stamp_input(event);
                    return true;
                }
                continue;
            }
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
                    stamp_input(event);
                    return true;
                }
                continue;
            }
            event = PlatformEvent{WindowEvent{WindowEventType::quit_requested, {}}};
            return true;
        }

        if (native_event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
            implementation_->focus_state = input::InputFocusState::focused;
            if(implementation_->text_input_requested)(void)SDL_StartTextInput(implementation_->window);
            if (implementation_->capture_state ==
                    input::InputCaptureState::captured &&
                !implementation_->try_release_capture_and_show_cursor()) {
                implementation_->queue_capture_recovery_failure();
            }
        } else if (native_event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            implementation_->focus_state = input::InputFocusState::unfocused;
            (void)SDL_StopTextInput(implementation_->window);
            if (!implementation_->try_release_capture_and_show_cursor()) {
                implementation_->queue_capture_recovery_failure();
            }
        }

        const auto translated = implementation_->translator.translate(native_event);
        if (translated) {
            event = *translated;
            stamp_input(event);
            return true;
        }
    }
    implementation_->native_events_in_current_drain = 0U;
    return false;
}

void SdlWindow::set_escape_input_owned(const bool owned) noexcept {
    implementation_->escape_input_owned = owned;
}
void SdlWindow::set_input_diagnostic_clocks(const bool enabled) noexcept {
    implementation_->input_diagnostic_clocks=enabled;
}

bool SdlWindow::set_text_input_enabled(const bool enabled) noexcept {
    if(enabled==implementation_->text_input_requested)return true;
    const bool success=enabled
        ? SDL_StartTextInput(implementation_->window):SDL_StopTextInput(implementation_->window);
    if(success)implementation_->text_input_requested=enabled;
    return success;
}
bool SdlWindow::text_input_enabled() const noexcept {return implementation_->text_input_requested;}
std::string SdlWindow::clipboard_text(const std::size_t maximum_bytes) const {
    char* value=SDL_GetClipboardText();
    if(!value)return {};
    struct Release {char* value;~Release(){SDL_free(value);}} release{value};
    const auto limit=std::min<std::size_t>(maximum_bytes,32768U);
    std::size_t count{};while(count<limit&&value[count])++count;
    if(value[count])while(count>0&&(static_cast<unsigned char>(value[count])&0xc0U)==0x80U)--count;
    return {value,count};
}
bool SdlWindow::set_clipboard_text(const std::string_view text) const {
    if(text.size()>32768U||text.find('\0')!=std::string_view::npos)return false;
    const std::string owned{text};return SDL_SetClipboardText(owned.c_str());
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

std::vector<VideoDisplay> enumerate_video_displays()
{
    int count{};
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    if (ids == nullptr) throw std::runtime_error{sdl_error("Unable to enumerate displays")};
    struct Release { SDL_DisplayID* ids; ~Release(){SDL_free(ids);} } release{ids};
    std::vector<VideoDisplay> result;
    result.reserve(static_cast<std::size_t>(std::clamp(count, 0, 32)));
    for (int index=0; index<std::min(count,32); ++index) {
        const auto* mode = SDL_GetDesktopDisplayMode(ids[index]);
        if (!mode || mode->w <= 0 || mode->h <= 0)
            throw std::runtime_error{sdl_error("Unable to query a display desktop mode")};
        const auto* name = SDL_GetDisplayName(ids[index]);
        const float scale = SDL_GetDisplayContentScale(ids[index]);
        result.push_back({static_cast<std::size_t>(index), ids[index], name ? name : "Display",
            mode->w, mode->h, scale > 0.0F ? scale : 1.0F});
    }
    return result;
}

VideoWindowState SdlWindow::video_state() const
{
    ++implementation_->video_refresh_count;
    VideoWindowState state;
    state.requested = implementation_->requested_video;
    state.safe_window_recovery = implementation_->safe_window_recovery;
    state.recovery_reason = implementation_->video_recovery_reason;
    const auto display = SDL_GetDisplayForWindow(implementation_->window);
    const auto displays = enumerate_video_displays();
    const auto found = std::find_if(displays.begin(),displays.end(),[&](const auto& candidate){return candidate.id == display;});
    if (found != displays.end()) {
        state.display_index = found->index;
        state.desktop_extent = {found->desktop_width,found->desktop_height};
        state.display_content_scale = found->content_scale;
    }
    if (!SDL_GetWindowSize(implementation_->window,&state.window_extent.width,&state.window_extent.height))
        throw std::runtime_error{sdl_error("Unable to query the window client size")};
    state.framebuffer_extent = pixel_extent();
    if (!SDL_GetWindowPosition(implementation_->window,&state.window_x,&state.window_y))
        throw std::runtime_error{sdl_error("Unable to query the window position")};
    state.display_scale = SDL_GetWindowDisplayScale(implementation_->window);
    state.pixel_density = SDL_GetWindowPixelDensity(implementation_->window);
    state.default_framebuffer_samples_observed =
        SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS,&state.default_framebuffer_sample_buffers) &&
        SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES,&state.default_framebuffer_samples);
    if ((SDL_GetWindowFlags(implementation_->window) & SDL_WINDOW_FULLSCREEN) != 0U)
        state.actual_mode = SDL_GetWindowFullscreenMode(implementation_->window) != nullptr
            ? core::VideoWindowMode::exclusive : core::VideoWindowMode::borderless;
    state.vsync = implementation_->vsync;
    if (const auto* mode = SDL_GetCurrentDisplayMode(display);
        mode && std::isfinite(mode->refresh_rate) && mode->refresh_rate > 0.0F)
        state.display_refresh_hz = mode->refresh_rate;
    int interval{};
    if (SDL_GL_GetSwapInterval(&interval)) state.actual_swap_interval = interval;
    return state;
}

const VideoWindowState& SdlWindow::cached_video_state() const
{
    if (!implementation_->cached_video)
        implementation_->cached_video = video_state();
    return *implementation_->cached_video;
}

std::uint64_t SdlWindow::video_state_revision() const noexcept
{
    return implementation_->video_revision;
}

std::uint64_t SdlWindow::video_state_refresh_count() const noexcept
{
    return implementation_->video_refresh_count;
}

void SdlWindow::swap_buffers()
{
    if (!SDL_GL_SwapWindow(implementation_->window)) {
        throw std::runtime_error{sdl_error("OpenGL buffer swap failed")};
    }
}

} // namespace hlclient::platform
