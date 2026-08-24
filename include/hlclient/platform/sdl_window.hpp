#pragma once

#include <memory>
#include <string>

namespace hlclient::platform {

struct SdlWindowConfig {
    std::string title{"HL Client Engine"};
    int width{1280};
    int height{720};
    bool hidden{false};
};

struct PixelExtent {
    int width{0};
    int height{0};
};

enum class WindowEventType {
    quit_requested,
    resized,
};

struct WindowEvent {
    WindowEventType type{WindowEventType::resized};
    PixelExtent extent{};
};

class SdlWindow final {
public:
    explicit SdlWindow(const SdlWindowConfig& config);
    ~SdlWindow() noexcept;

    SdlWindow(const SdlWindow&) = delete;
    SdlWindow& operator=(const SdlWindow&) = delete;
    SdlWindow(SdlWindow&&) = delete;
    SdlWindow& operator=(SdlWindow&&) = delete;

    [[nodiscard]] bool poll_event(WindowEvent& event) noexcept;
    [[nodiscard]] PixelExtent pixel_extent() const;
    [[nodiscard]] bool vsync_enabled() const noexcept;
    void swap_buffers();

private:
    struct Impl;
    std::unique_ptr<Impl> implementation_;
};

} // namespace hlclient::platform
