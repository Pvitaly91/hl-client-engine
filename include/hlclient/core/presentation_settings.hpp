#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace hlclient::core {

// A shared, engine-neutral launch contract. No SDL, GL, desktop gamma or game
// semantics. Values are normalized controls, not stock gamma cvars.
enum class VideoWindowMode : std::uint8_t { windowed, borderless, exclusive };

[[nodiscard]] inline constexpr std::string_view to_string(VideoWindowMode mode) noexcept {
    switch (mode) {
    case VideoWindowMode::windowed: return "windowed";
    case VideoWindowMode::borderless: return "borderless";
    case VideoWindowMode::exclusive: return "exclusive";
    }
    return "invalid";
}

struct VideoSettings {
    std::size_t display_index{};
    VideoWindowMode mode{VideoWindowMode::windowed};
    int width{1280}, height{720};
    bool vsync{true};
    // Requested samples of the scene target, not of the native window. The
    // renderer reports the actually supported count independently.
    unsigned msaa_samples{4};
    friend bool operator==(const VideoSettings&, const VideoSettings&) = default;
};

[[nodiscard]] inline constexpr bool valid_msaa_samples(unsigned samples) noexcept {
    return samples == 0U || samples == 2U || samples == 4U || samples == 8U;
}

[[nodiscard]] inline bool valid_video_settings(const VideoSettings& settings) noexcept {
    return valid_msaa_samples(settings.msaa_samples) &&
        settings.display_index < 32U && settings.width > 0 && settings.height > 0 &&
        settings.width <= 8192 && settings.height <= 8192 &&
        static_cast<std::uint64_t>(settings.width) * static_cast<std::uint64_t>(settings.height) <= 32U*1024U*1024U &&
        (settings.mode == VideoWindowMode::windowed || settings.mode == VideoWindowMode::borderless ||
         settings.mode == VideoWindowMode::exclusive);
}

// Independent of native UI DPI, source sprite tier, world size and FOV.
// Auto selects the presentation layer's source-backed layout; an explicit
// multiplier is bounded to 50..200 percent of that reference layout.
struct HudScaleSettings {
    bool automatic{true};
    float scale{1.0F};
    friend bool operator==(const HudScaleSettings&, const HudScaleSettings&) = default;
};

[[nodiscard]] inline bool valid_hud_scale_settings(const HudScaleSettings& settings) noexcept {
    return std::isfinite(settings.scale) && settings.scale >= 0.5F && settings.scale <= 2.0F;
}

struct ImageAdjustments {
    float brightness{0.0F}, gamma{1.0F}, contrast{1.0F};
    friend bool operator==(const ImageAdjustments&, const ImageAdjustments&) = default;
};

[[nodiscard]] inline bool valid_image_adjustments(const ImageAdjustments& settings) noexcept {
    return std::isfinite(settings.brightness) && settings.brightness >= -0.5F && settings.brightness <= 0.5F &&
        std::isfinite(settings.gamma) && settings.gamma >= 0.25F && settings.gamma <= 4.0F &&
        std::isfinite(settings.contrast) && settings.contrast >= 0.0F && settings.contrast <= 2.0F;
}

// Reference for the single scene-color pass: contrast about 0.5, additive
// brightness, then midtone gamma. Inputs/settings must be validated first.
// Output alpha is preserved; HUD is composited afterwards. Neutral bypass is
// exact in production (no texture copy, quantization or shader execution).
[[nodiscard]] inline std::array<float,4> adjusted_image_color(
        std::array<float,4> color, const ImageAdjustments& settings) noexcept {
    if (!valid_image_adjustments(settings)) return color;
    for (std::size_t i=0; i<3; ++i)
        color[i] = std::pow(std::clamp((color[i]-0.5F)*settings.contrast+0.5F+
            settings.brightness, 0.0F, 1.0F), 1.0F/settings.gamma);
    return color;
}

} // namespace hlclient::core
