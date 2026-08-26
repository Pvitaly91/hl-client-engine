#pragma once

#include <hlclient/input/input_event.hpp>
#include <hlclient/platform/platform_event.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace hlclient::platform {

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
