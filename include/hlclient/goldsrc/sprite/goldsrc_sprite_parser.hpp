#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/assets/sprite_asset_types.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc::sprite {

inline constexpr std::size_t kGoldSrcSpriteHeaderWireSize = 40U;
inline constexpr std::size_t kGoldSrcSpritePaletteCountWireSize = 2U;
inline constexpr std::uint16_t kGoldSrcSpritePaletteColorCount = 256U;
inline constexpr std::size_t kGoldSrcSpritePaletteRgbByteCount =
    static_cast<std::size_t>(kGoldSrcSpritePaletteColorCount) * 3U;
inline constexpr std::size_t kGoldSrcSpriteFrameTypeWireSize = 4U;
inline constexpr std::size_t kGoldSrcSpriteFrameHeaderWireSize = 16U;
inline constexpr std::size_t kGoldSrcSpriteGroupHeaderWireSize = 4U;
inline constexpr std::size_t kGoldSrcSpriteGroupIntervalWireSize = 4U;
inline constexpr std::uint32_t kGoldSrcSpriteIdentifier = 0x50534449U;
inline constexpr std::int32_t kGoldSrcSpriteVersion = 2;

inline constexpr std::size_t kGoldSrcSpriteHardMaximumSourceBytes =
    256U * 1024U * 1024U;
inline constexpr std::size_t kGoldSrcSpriteHardMaximumTopLevelEntries = 65'536U;
inline constexpr std::size_t kGoldSrcSpriteHardMaximumFlattenedFrames = 65'536U;
inline constexpr std::size_t kGoldSrcSpriteHardMaximumGroupFrames = 65'536U;
inline constexpr std::uint32_t kGoldSrcSpriteHardMaximumDimension = 16'384U;
inline constexpr std::uint64_t kGoldSrcSpriteHardMaximumPixelsPerFrame =
    268'435'456ULL;
inline constexpr std::size_t kGoldSrcSpriteHardMaximumTotalIndexedBytes =
    256U * 1024U * 1024U;
inline constexpr std::size_t kGoldSrcSpriteHardMaximumTotalRgbaBytes =
    768U * 1024U * 1024U;
inline constexpr float kGoldSrcSpriteHardMaximumDurationSeconds = 86'400.0F;
inline constexpr std::size_t kGoldSrcSpriteHardMaximumMetadataBytes =
    16U * 1024U * 1024U;
inline constexpr std::int32_t kGoldSrcSpriteHardMaximumOriginMagnitude =
    16'777'216;

struct GoldSrcSpriteImportLimits {
    std::size_t maximum_source_bytes{64U * 1024U * 1024U};
    std::size_t maximum_top_level_entries{4'096U};
    std::size_t maximum_flattened_frames{16'384U};
    std::size_t maximum_group_frames{4'096U};
    std::uint32_t maximum_width{4'096U};
    std::uint32_t maximum_height{4'096U};
    std::uint64_t maximum_pixels_per_frame{16'777'216ULL};
    std::size_t maximum_total_indexed_bytes{64U * 1024U * 1024U};
    std::size_t maximum_total_rgba_bytes{256U * 1024U * 1024U};
    float maximum_duration_seconds{3'600.0F};
    std::size_t maximum_metadata_bytes{4U * 1024U * 1024U};
    std::int32_t maximum_origin_magnitude{65'536};
};

[[nodiscard]] bool valid_goldsrc_sprite_import_limits(
    const GoldSrcSpriteImportLimits& limits) noexcept;

enum class GoldSrcSpriteErrorCode {
    invalid_configuration,
    source_limit_exceeded,
    source_too_small,
    invalid_identifier,
    unsupported_version,
    unsupported_orientation,
    unsupported_texture_format,
    invalid_bounding_radius,
    invalid_header_dimensions,
    invalid_top_level_count,
    invalid_beam_length,
    unsupported_sync_type,
    metadata_limit_exceeded,
    missing_palette_count,
    unsupported_palette_count,
    truncated_palette,
    invalid_frame_type,
    invalid_group_frame_count,
    flattened_frame_limit_exceeded,
    truncated_group_intervals,
    invalid_group_interval,
    duration_limit_exceeded,
    truncated_frame_header,
    invalid_frame_origin,
    invalid_frame_dimensions,
    frame_pixel_limit_exceeded,
    frame_pixel_range_overflow,
    truncated_frame_pixels,
    total_indexed_limit_exceeded,
    total_rgba_limit_exceeded,
    header_frame_dimensions_too_small,
    unexpected_trailing_data,
    unable_to_retain_sprite,
};

[[nodiscard]] std::string_view to_string(GoldSrcSpriteErrorCode code) noexcept;

struct GoldSrcSpriteError {
    GoldSrcSpriteErrorCode code{GoldSrcSpriteErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::optional<std::size_t> top_level_entry;
    std::optional<std::size_t> group_frame;
    std::string context;
};

struct GoldSrcSpriteParsedDocument {
    assets::SpriteSourceAssetData source_data;
    // Additive compatibility projection for the pre-M4.5.2 SpriteAsset API.
    // Index-alpha frames intentionally carry no guessed RGBA pixels.
    std::vector<assets::SpriteFrame> compatibility_frames;
};

struct GoldSrcSpriteParseResult {
    std::optional<GoldSrcSpriteParsedDocument> document;
    std::optional<GoldSrcSpriteError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return document.has_value();
    }
};

class GoldSrcSpriteParser final {
public:
    [[nodiscard]] static GoldSrcSpriteParseResult parse(
        std::span<const std::byte> source,
        const GoldSrcSpriteImportLimits& limits = {});
};

} // namespace hlclient::goldsrc::sprite
