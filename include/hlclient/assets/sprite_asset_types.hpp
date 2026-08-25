#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace hlclient::assets {

// Format-neutral source metadata for an owning sprite asset. Rendering policy
// intentionally lives outside these types: orientation, synchronization and
// texture-format values are descriptive source facts only.
enum class SpriteSourceCompatibilityProfile {
    goldsrc_palette_sprite_v2,
};

enum class SpriteOrientation {
    view_parallel_upright,
    facing_upright,
    view_parallel,
    oriented,
    view_parallel_oriented,
};

enum class SpriteTextureFormat {
    normal,
    additive,
    index_alpha,
    alpha_test,
};

enum class SpriteSyncType {
    synchronized,
    random,
};

struct SpritePaletteColor {
    std::uint8_t red{0U};
    std::uint8_t green{0U};
    std::uint8_t blue{0U};

    [[nodiscard]] friend bool operator==(
        const SpritePaletteColor&,
        const SpritePaletteColor&) = default;
};

struct SpritePalette {
    std::array<SpritePaletteColor, 256U> colors{};
};

struct SpriteFrameOrigin {
    std::int32_t x{0};
    std::int32_t y{0};
};

// RGBA data is a convenience derivation. Indexed pixels plus the palette and
// source texture format remain authoritative in every case.
enum class SpriteRgbaEvidenceProfile {
    normal_opaque,
    alpha_test_index_255,
    additive_opaque_preview_not_blend_semantics,
    index_alpha_conversion_evidence_pending,
};

struct SpriteIndexedFrame {
    SpriteFrameOrigin origin{};
    std::uint32_t width{0U};
    std::uint32_t height{0U};
    std::uint32_t source_top_level_entry{0U};
    std::optional<std::uint32_t> source_group_ordinal;
    std::optional<std::uint32_t> source_group_frame_ordinal;
    std::vector<std::byte> indexed_pixels;
    // Empty only when rgba_evidence says conversion is evidence-pending.
    std::vector<std::byte> derived_rgba8;
    SpriteRgbaEvidenceProfile rgba_evidence{
        SpriteRgbaEvidenceProfile::normal_opaque};
};

struct SpriteFrameGroup {
    std::uint32_t source_top_level_entry{0U};
    std::uint32_t source_group_ordinal{0U};
    std::vector<float> cumulative_intervals_seconds;
    std::vector<float> frame_durations_seconds;
    std::vector<std::uint32_t> flattened_frame_indices;
};

enum class SpriteTopLevelEntryKind {
    single,
    group,
};

struct SpriteTopLevelEntry {
    SpriteTopLevelEntryKind kind{SpriteTopLevelEntryKind::single};
    std::uint32_t source_top_level_entry{0U};
    std::uint32_t first_flattened_frame{0U};
    std::uint32_t flattened_frame_count{0U};
    std::optional<std::uint32_t> group_ordinal;
};

struct SpriteStatistics {
    std::uint64_t top_level_entry_count{0U};
    std::uint64_t flattened_frame_count{0U};
    std::uint64_t group_count{0U};
    std::uint64_t indexed_pixel_byte_count{0U};
    std::uint64_t derived_rgba_byte_count{0U};
};

struct SpriteSourceAssetData {
    SpriteSourceCompatibilityProfile compatibility_profile{
        SpriteSourceCompatibilityProfile::goldsrc_palette_sprite_v2};
    std::int32_t source_version{0};
    SpriteOrientation orientation{SpriteOrientation::view_parallel};
    SpriteTextureFormat texture_format{SpriteTextureFormat::normal};
    SpriteSyncType sync_type{SpriteSyncType::synchronized};
    float bounding_radius{0.0F};
    std::uint32_t maximum_width{0U};
    std::uint32_t maximum_height{0U};
    float beam_length{0.0F};
    SpritePalette palette{};
    std::vector<SpriteIndexedFrame> indexed_frames;
    std::vector<SpriteFrameGroup> groups;
    std::vector<SpriteTopLevelEntry> top_level_entries;
    SpriteStatistics statistics{};
};

} // namespace hlclient::assets
