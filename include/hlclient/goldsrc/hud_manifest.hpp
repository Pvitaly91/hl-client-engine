#pragma once
#include <hlclient/game_api/hud_resources.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {
inline constexpr std::size_t kMaximumHudManifestBytes = 64U * 1024U;
inline constexpr std::size_t kMaximumHudManifestTokenBytes = 63U;
inline constexpr std::uint32_t kMaximumHudManifestDimension = 16'384U;
struct HudManifestRecord final {
    std::string role, sprite_basename;
    std::uint32_t resolution{};
    game_api::HudSourceRectangle source{};
};
enum class HudManifestError : std::uint8_t {
    none, size_limit, invalid_text, invalid_count, count_mismatch, entry_limit,
    invalid_record, invalid_name, invalid_number, duplicate_role_tier,
    dependency_limit
};
struct HudManifestParseResult final {
    std::vector<HudManifestRecord> records;
    std::optional<std::uint32_t> declared_count;
    bool exact_count_compatibility_applied{};
    std::string source_sha256;
    HudManifestError error{HudManifestError::none};
    std::size_t line{};
    explicit operator bool() const noexcept { return error == HudManifestError::none; }
};
[[nodiscard]] bool valid_hud_descriptor_name(std::string_view) noexcept;
[[nodiscard]] bool valid_hud_sprite_basename(std::string_view) noexcept;
[[nodiscard]] HudManifestParseResult parse_hud_manifest(
    std::span<const std::byte>,
    const std::optional<game_api::HudManifestCountCompatibility>& = {});
[[nodiscard]] bool valid_hud_source_rectangle(
    game_api::HudSourceRectangle, std::uint32_t width, std::uint32_t height) noexcept;
} // namespace hlclient::goldsrc
