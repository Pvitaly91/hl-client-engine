#pragma once
#include <cstdint>
#include <span>
#include <string>

namespace hlclient::platform {

// Explicit application diagnostic output; input is bottom-up owning readback.
void save_rgba_framebuffer_png(const std::string& output, int width, int height,
    std::span<const std::uint8_t> rgba);

class SdlRuntime final {
public:
    SdlRuntime();
    ~SdlRuntime() noexcept;

    SdlRuntime(const SdlRuntime&) = delete;
    SdlRuntime& operator=(const SdlRuntime&) = delete;
    SdlRuntime(SdlRuntime&&) = delete;
    SdlRuntime& operator=(SdlRuntime&&) = delete;
};

} // namespace hlclient::platform
