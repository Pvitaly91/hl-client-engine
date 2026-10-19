#pragma once

#include <hlclient/input/input_event.hpp>
#include <hlclient/platform/platform_event.hpp>
#include <hlclient/core/presentation_settings.hpp>

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::platform {

// Startup capability failures are intentionally separate from ordinary SDL
// and renderer failures.  Callers may use this narrow classification to skip
// an OpenGL test on a genuinely headless/legacy host without treating shader,
// upload, draw, swap, allocation, or programming failures as unavailable
// hardware.
enum class OpenGlStartupCapabilityFailure : std::uint8_t {
    none,
    video_subsystem_unavailable,
    window_unavailable,
    context_attribute_unavailable,
    context_unavailable,
    context_activation_unavailable,
    function_loading_unavailable,
    legacy_context,
};

[[nodiscard]] std::string_view to_string(
    OpenGlStartupCapabilityFailure failure) noexcept;

class OpenGlStartupCapabilityError final : public std::runtime_error {
public:
    OpenGlStartupCapabilityError(
        OpenGlStartupCapabilityFailure failure,
        std::string context);

    [[nodiscard]] OpenGlStartupCapabilityFailure failure() const noexcept;

private:
    OpenGlStartupCapabilityFailure failure_;
};

[[nodiscard]] OpenGlStartupCapabilityFailure
classify_opengl_startup_capability_failure(
    const std::exception& error) noexcept;

// SDL reports startup failures as diagnostic strings rather than typed error
// codes.  Only diagnostics that explicitly establish missing video/OpenGL
// capability are eligible for a skip.  Generic failures (including possible
// allocation or driver faults) remain fatal.
[[nodiscard]] bool proves_opengl_startup_capability_unavailable(
    OpenGlStartupCapabilityFailure failure,
    std::string_view diagnostic) noexcept;

struct SdlWindowConfig {
    std::string title{"HL Client Engine"};
    int width{1280};
    int height{720};
    bool hidden{false};
    std::size_t display_index{};
    core::VideoWindowMode mode{core::VideoWindowMode::windowed};
    bool vsync{true};
    unsigned msaa_samples{4}; // scene request only; native framebuffer stays single-sample
};

struct VideoDisplay {
    std::size_t index{};
    std::uint32_t id{};
    std::string name;
    int desktop_width{}, desktop_height{};
    float content_scale{1.0F};
};

// SDL video must already be initialized; this is enumeration, not discovery
// of network hosts. At most32 displays are retained.
[[nodiscard]] std::vector<VideoDisplay> enumerate_video_displays();

struct VideoWindowState {
    core::VideoSettings requested;
    std::size_t display_index{};
    core::VideoWindowMode actual_mode{core::VideoWindowMode::windowed};
    PixelExtent window_extent{}, framebuffer_extent{}, desktop_extent{};
    float display_scale{1.0F};
    float display_content_scale{1.0F}, pixel_density{1.0F};
    int window_x{}, window_y{};
    int default_framebuffer_samples{}, default_framebuffer_sample_buffers{};
    bool default_framebuffer_samples_observed{};
    bool vsync{};
    // OS/driver reports, not measured scanout or VRR capability.
    std::optional<double> display_refresh_hz;
    std::optional<int> actual_swap_interval;
    bool safe_window_recovery{};
    std::string recovery_reason;
};

enum class RelativeMouseCaptureStatus : std::uint8_t {
    unchanged,
    acquired,
    released,
    failed,
};

struct RelativeMouseCaptureResult final {
    RelativeMouseCaptureStatus status{RelativeMouseCaptureStatus::unchanged};
    input::InputCaptureState state{input::InputCaptureState::released};
    std::string diagnostic{};

    [[nodiscard]] bool succeeded() const noexcept
    {
        return status != RelativeMouseCaptureStatus::failed;
    }
    [[nodiscard]] explicit operator bool() const noexcept { return succeeded(); }
};

class SdlWindow final {
public:
    explicit SdlWindow(const SdlWindowConfig& config);
    ~SdlWindow() noexcept;

    SdlWindow(const SdlWindow&) = delete;
    SdlWindow& operator=(const SdlWindow&) = delete;
    SdlWindow(SdlWindow&&) = delete;
    SdlWindow& operator=(SdlWindow&&) = delete;

    // This is the sole owner of SDL_PollEvent. Every native event is translated
    // once into a platform-neutral window or input event in original queue order.
    [[nodiscard]] bool poll_event(PlatformEvent& event) noexcept;

    // Compatibility adapter for non-interactive callers. It delegates to the
    // same sole pump and skips input events; it never polls SDL independently.
    [[nodiscard]] bool poll_event(WindowEvent& event) noexcept;

    // An application overlay can own Escape before the sole native event pump.
    // Default false preserves ordinary release-capture/close behavior. Focus
    // loss and hard event-limit recovery always keep their fail-safe release.
    void set_escape_input_owned(bool owned) noexcept;
    void set_input_diagnostic_clocks(bool enabled) noexcept;

    // The owning overlay activates SDL Unicode delivery on the window/context
    // thread. Clipboard strings are bounded; no system font or OS shell is used.
    [[nodiscard]] bool set_text_input_enabled(bool enabled) noexcept;
    [[nodiscard]] bool text_input_enabled() const noexcept;
    [[nodiscard]] std::string clipboard_text(std::size_t maximum_bytes = 8192U) const;
    [[nodiscard]] bool set_clipboard_text(std::string_view text) const;

    [[nodiscard]] RelativeMouseCaptureResult request_relative_mouse_capture(bool enabled);
    [[nodiscard]] input::InputFocusState focus_state() const noexcept;
    [[nodiscard]] input::InputCaptureState capture_state() const noexcept;
    [[nodiscard]] PixelExtent pixel_extent() const;
    [[nodiscard]] bool vsync_enabled() const noexcept;
    // Fresh owner-thread query for startup/final receipts and callers which
    // modify SDL directly. The render loop uses the event-invalidated cache.
    [[nodiscard]] VideoWindowState video_state() const;
    [[nodiscard]] const VideoWindowState& cached_video_state() const;
    [[nodiscard]] std::uint64_t video_state_revision() const noexcept;
    [[nodiscard]] std::uint64_t video_state_refresh_count() const noexcept;
    void swap_buffers();

private:
    struct Impl;
    std::unique_ptr<Impl> implementation_;
};

} // namespace hlclient::platform
