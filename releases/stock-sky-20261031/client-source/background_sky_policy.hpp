#pragma once

#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>

// One explicit process-startup decision; renderer frames never read environment.
// Keep the legacy numeric values: the diagnostic modes never mean "depth".
enum class BackgroundSkyPolicy {
    original=0, depth=1, no_background=2, no_sky_color=3,
    // Historical rejected algorithms occupied 4/5; never reinterpret them.
    world_sky_non_occluding=6
};
[[nodiscard]] constexpr std::optional<BackgroundSkyPolicy>
parse_background_sky_policy(std::string_view value) noexcept {
    if(value == "original") return BackgroundSkyPolicy::original;
    if(value == "depth") return BackgroundSkyPolicy::depth;
    if(value == "no_background") return BackgroundSkyPolicy::no_background;
    if(value == "no_sky_color") return BackgroundSkyPolicy::no_sky_color;
    if(value == "world_sky_non_occluding") return BackgroundSkyPolicy::world_sky_non_occluding;
    return std::nullopt;
}
[[nodiscard]] constexpr std::string_view
background_sky_policy_name(BackgroundSkyPolicy policy) noexcept {
    switch(policy) {
    case BackgroundSkyPolicy::original: return "original";
    case BackgroundSkyPolicy::depth: return "depth";
    case BackgroundSkyPolicy::no_background: return "no_background";
    case BackgroundSkyPolicy::no_sky_color: return "no_sky_color";
    case BackgroundSkyPolicy::world_sky_non_occluding: return "world_sky_non_occluding";
    }
    return "invalid";
}
[[nodiscard]] inline BackgroundSkyPolicy read_background_sky_policy() {
#ifdef _WIN32
    char* value{};
    std::size_t bytes{};
    if(::_dupenv_s(&value, &bytes, "HLC_BACKGROUND_SKY_POLICY") != 0)
        throw std::runtime_error("Unable to read HLC_BACKGROUND_SKY_POLICY");
    const std::unique_ptr<char, decltype(&std::free)> owner{value, &std::free};
#else
    const char* value = std::getenv("HLC_BACKGROUND_SKY_POLICY");
#endif
    // Unset is the accepted original path. Empty/unknown values are errors.
    const auto parsed = parse_background_sky_policy(value ? value : "original");
    if(!parsed) throw std::invalid_argument(
        "HLC_BACKGROUND_SKY_POLICY must be original, depth, no_background, no_sky_color or world_sky_non_occluding (default: original)");
    return *parsed;
}
