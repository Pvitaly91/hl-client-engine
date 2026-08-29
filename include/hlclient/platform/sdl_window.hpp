#pragma once

#include <hlclient/input/input_event.hpp>
#include <hlclient/platform/platform_event.hpp>

#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

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

    [[nodiscard]] RelativeMouseCaptureResult request_relative_mouse_capture(bool enabled);
    [[nodiscard]] input::InputFocusState focus_state() const noexcept;
    [[nodiscard]] input::InputCaptureState capture_state() const noexcept;
    [[nodiscard]] PixelExtent pixel_extent() const;
    [[nodiscard]] bool vsync_enabled() const noexcept;
    void swap_buffers();

private:
    struct Impl;
    std::unique_ptr<Impl> implementation_;
};

} // namespace hlclient::platform
