#pragma once

#include <hlclient/game_api/presentation.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::game_api {

inline constexpr std::size_t kMaximumHudResourceDescriptors = 15U;
inline constexpr std::size_t kMaximumHudResourceEntries = 512U;
inline constexpr std::size_t kMaximumHudResourceDependencies = 256U;

enum class HudResourceStatus : std::uint8_t {
    not_yet_resolved, ready, absent_by_asset_contract, missing_file,
    not_authorized, open_failed, decode_failed, invalid_manifest,
    invalid_rectangle, unsupported_material, resource_limit, hidden_by_policy,
    submitted
};
[[nodiscard]] inline std::string_view to_string(HudResourceStatus value) noexcept {
    switch (value) {
    case HudResourceStatus::not_yet_resolved: return "not_yet_resolved";
    case HudResourceStatus::ready: return "ready";
    case HudResourceStatus::absent_by_asset_contract: return "absent_by_asset_contract";
    case HudResourceStatus::missing_file: return "missing_file";
    case HudResourceStatus::not_authorized: return "not_authorized";
    case HudResourceStatus::open_failed: return "open_failed";
    case HudResourceStatus::decode_failed: return "decode_failed";
    case HudResourceStatus::invalid_manifest: return "invalid_manifest";
    case HudResourceStatus::invalid_rectangle: return "invalid_rectangle";
    case HudResourceStatus::unsupported_material: return "unsupported_material";
    case HudResourceStatus::resource_limit: return "resource_limit";
    case HudResourceStatus::hidden_by_policy: return "hidden_by_policy";
    case HudResourceStatus::submitted: return "submitted";
    }
    return "unavailable";
}

// A module may name one audited descriptor-count quirk. This is not a general
// permissive parser mode: the complete source SHA and both counts must match.
struct HudManifestCountCompatibility final {
    std::string exact_source_sha256;
    std::uint32_t declared_count{}, parsed_count{};
    friend bool operator==(const HudManifestCountCompatibility&,
                           const HudManifestCountCompatibility&) = default;
};
struct HudManifestRequest final {
    std::string name; // Safe virtual sprites/<descriptor>.txt, never native.
    std::optional<HudManifestCountCompatibility> count_compatibility;
    friend bool operator==(const HudManifestRequest&, const HudManifestRequest&) = default;
};
struct HudResourceEntry final {
    std::string role; // Inert descriptor metadata; interpretation is game-owned.
    std::uint32_t resolution{};
    std::string sprite_name; // Safe virtual name, provenance only.
    std::uint64_t resource_id{}, resource_revision{};
    std::uint32_t frame{};
    HudSourceRectangle source{};
    std::uint32_t frame_width{}, frame_height{};
    HudSpriteBlend blend{HudSpriteBlend::additive};
    HudResourceStatus status{HudResourceStatus::not_yet_resolved};
    std::string asset_sha256;
    friend bool operator==(const HudResourceEntry&, const HudResourceEntry&) = default;
};
struct HudResourceDescriptor final {
    std::string name;
    HudResourceStatus status{HudResourceStatus::not_yet_resolved};
    std::vector<HudResourceEntry> entries;
    std::optional<std::uint32_t> declared_count;
    std::uint32_t parsed_count{};
    bool exact_count_compatibility_applied{};
    std::string source_sha256;
    friend bool operator==(const HudResourceDescriptor&, const HudResourceDescriptor&) = default;
};
struct HudResourceSet final {
    std::vector<HudResourceDescriptor> descriptors;
    std::uint64_t revision{};
    friend bool operator==(const HudResourceSet&, const HudResourceSet&) = default;
};

} // namespace hlclient::game_api
