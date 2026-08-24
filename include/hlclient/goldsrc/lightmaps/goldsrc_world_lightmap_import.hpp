#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/assets/world_lightmap_types.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hlclient::goldsrc::lightmaps {

inline constexpr std::size_t kGoldSrcLightmapBytesPerSample = 3U;
inline constexpr std::uint32_t kGoldSrcLightmapTextureBlockSize = 16U;
inline constexpr double kGoldSrcLightmapCoordinateTolerance = 1.0e-5;
inline constexpr std::size_t kGoldSrcLightmapHardMaximumSurfaceCount = 65'535U;
inline constexpr std::size_t kGoldSrcLightmapHardMaximumSamplesPerSurface =
    1'048'576U;
inline constexpr std::uint32_t kGoldSrcLightmapHardMaximumAtlasDimension = 4'096U;
inline constexpr std::size_t kGoldSrcLightmapHardMaximumAtlasPages = 64U;
inline constexpr std::size_t kGoldSrcLightmapHardMaximumTotalAtlasRgbaBytes =
    512U * 1024U * 1024U;

struct GoldSrcWorldLightmapImportLimits {
    std::size_t maximum_surface_count{65'535U};
    std::size_t maximum_samples_per_surface{16'384U};
    std::size_t maximum_total_source_samples{16'777'216U};
    std::uint32_t atlas_width{1'024U};
    std::uint32_t maximum_atlas_dimension{2'048U};
    std::size_t maximum_atlas_pages{16U};
    std::size_t maximum_total_atlas_rgba_bytes{256U * 1024U * 1024U};
    std::uint32_t atlas_padding{1U};
    std::size_t maximum_style_count{assets::kWorldLightmapStyleSlotCount};
};

[[nodiscard]] bool valid_goldsrc_world_lightmap_import_limits(
    const GoldSrcWorldLightmapImportLimits& limits) noexcept;

struct GoldSrcLightmapExtents {
    std::int32_t texture_min_s{0};
    std::int32_t texture_min_t{0};
    std::uint32_t extent_s{0U};
    std::uint32_t extent_t{0U};
    std::uint32_t sample_width{0U};
    std::uint32_t sample_height{0U};
};

enum class GoldSrcLightmapExtentErrorCode {
    invalid_configuration,
    invalid_surface_vertex_range,
    non_finite_texture_coordinate,
    coordinate_range_overflow,
    sample_limit_exceeded,
    coordinate_outside_extent,
};

[[nodiscard]] std::string_view to_string(
    GoldSrcLightmapExtentErrorCode code) noexcept;

struct GoldSrcLightmapExtentError {
    GoldSrcLightmapExtentErrorCode code{
        GoldSrcLightmapExtentErrorCode::invalid_configuration};
    std::optional<std::size_t> vertex_index;
    std::string context;
};

struct GoldSrcLightmapExtentResult {
    std::optional<GoldSrcLightmapExtents> extents;
    std::optional<GoldSrcLightmapExtentError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return extents.has_value();
    }
};

// Calculates extents from the exact contiguous face-local WorldVertex range.
// Raw S/T remain in texel units; no rounding, normalization or clamping is
// performed before the GoldSrc floor/ceil block formula.
[[nodiscard]] GoldSrcLightmapExtentResult calculate_goldsrc_lightmap_extents(
    const assets::WorldAsset& world,
    const assets::WorldSurface& surface,
    std::size_t maximum_samples_per_surface = 16'384U) noexcept;

enum class GoldSrcWorldLightmapImportErrorCode {
    invalid_configuration,
    invalid_world_asset,
    invalid_bsp_source,
    invalid_lighting_lump,
    surface_limit_exceeded,
    invalid_surface_vertex_range,
    invalid_lightmap_extent,
    invalid_lightmap_metadata,
    lightmap_range_out_of_bounds,
    source_sample_limit_exceeded,
    atlas_rectangle_limit_exceeded,
    atlas_page_limit_exceeded,
    atlas_memory_limit_exceeded,
    lightmap_set_build_failed,
    unable_to_retain_lightmap_set,
};

[[nodiscard]] std::string_view to_string(
    GoldSrcWorldLightmapImportErrorCode code) noexcept;

struct GoldSrcWorldLightmapImportError {
    GoldSrcWorldLightmapImportErrorCode code{
        GoldSrcWorldLightmapImportErrorCode::invalid_configuration};
    std::optional<std::size_t> surface_index;
    std::optional<assets::WorldSurfaceLightmapBindingStatus> binding_status;
    std::optional<GoldSrcLightmapExtentErrorCode> extent_code;
    std::optional<assets::WorldLightmapSetErrorCode> lightmap_set_code;
    std::string context;
};

struct GoldSrcWorldLightmapImportResult {
    std::optional<assets::WorldLightmapSet> lightmap_set;
    std::optional<GoldSrcWorldLightmapImportError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return lightmap_set.has_value();
    }
};

class GoldSrcWorldLightmapImporter final {
public:
    [[nodiscard]] static GoldSrcWorldLightmapImportResult import(
        const assets::WorldAsset& world,
        std::span<const std::byte> retained_bsp_source,
        const GoldSrcWorldLightmapImportLimits& limits = {});
};

} // namespace hlclient::goldsrc::lightmaps
