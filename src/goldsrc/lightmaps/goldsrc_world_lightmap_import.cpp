#include <hlclient/goldsrc/lightmaps/goldsrc_world_lightmap_import.hpp>

#include <hlclient/goldsrc/bsp/goldsrc_bsp_format.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace hlclient::goldsrc::lightmaps {
namespace {

inline constexpr std::size_t kMaximumDiagnosticContextBytes = 192U;

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool checked_multiply(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] std::optional<std::int32_t> read_i32_le(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept
{
    if (offset > bytes.size() || bytes.size() - offset < 4U) {
        return std::nullopt;
    }
    std::uint32_t value = 0U;
    for (std::size_t byte_index = 0U; byte_index < 4U; ++byte_index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(bytes[offset + byte_index]))
                 << static_cast<unsigned int>(byte_index * 8U);
    }
    return std::bit_cast<std::int32_t>(value);
}

struct LightingLumpResult {
    std::optional<std::span<const std::byte>> bytes;
    GoldSrcWorldLightmapImportErrorCode error_code{
        GoldSrcWorldLightmapImportErrorCode::invalid_bsp_source};
};

[[nodiscard]] LightingLumpResult lighting_lump(
    const std::span<const std::byte> source) noexcept
{
    if (source.size() < bsp::kGoldSrcBspHeaderWireSize) {
        return {};
    }
    const auto version = read_i32_le(source, 0U);
    if (!version || *version != bsp::kGoldSrcBspVersion) {
        return {};
    }
    const auto descriptor = 4U +
        bsp::goldsrc_bsp_lump_index(bsp::GoldSrcBspLumpId::lighting) *
            bsp::kGoldSrcBspLumpDescriptorWireSize;
    const auto signed_offset = read_i32_le(source, descriptor);
    const auto signed_length = read_i32_le(source, descriptor + 4U);
    if (!signed_offset || !signed_length || *signed_offset < 0 ||
        *signed_length < 0) {
        return LightingLumpResult{
            std::nullopt,
            GoldSrcWorldLightmapImportErrorCode::invalid_lighting_lump};
    }
    const auto offset = static_cast<std::size_t>(
        static_cast<std::uint32_t>(*signed_offset));
    const auto length = static_cast<std::size_t>(
        static_cast<std::uint32_t>(*signed_length));
    std::size_t end = 0U;
    if (!checked_add(offset, length, end) || end > source.size() ||
        (length != 0U && offset < bsp::kGoldSrcBspHeaderWireSize)) {
        return LightingLumpResult{
            std::nullopt,
            GoldSrcWorldLightmapImportErrorCode::invalid_lighting_lump};
    }
    return LightingLumpResult{source.subspan(offset, length),
        GoldSrcWorldLightmapImportErrorCode::invalid_lighting_lump};
}

[[nodiscard]] GoldSrcWorldLightmapImportResult import_failure(
    const GoldSrcWorldLightmapImportErrorCode code,
    const std::optional<std::size_t> surface_index,
    const std::optional<assets::WorldSurfaceLightmapBindingStatus> binding_status,
    const std::optional<GoldSrcLightmapExtentErrorCode> extent_code,
    const std::optional<assets::WorldLightmapSetErrorCode> set_code,
    const std::string_view context)
{
    GoldSrcWorldLightmapImportError error;
    error.code = code;
    error.surface_index = surface_index;
    error.binding_status = binding_status;
    error.extent_code = extent_code;
    error.lightmap_set_code = set_code;
    const auto bounded = context.substr(
        0U, (std::min)(context.size(), kMaximumDiagnosticContextBytes));
    error.context.assign(bounded.data(), bounded.size());
    return GoldSrcWorldLightmapImportResult{std::nullopt, std::move(error)};
}

struct PreparedSurface {
    std::size_t surface_index{0U};
    std::size_t source_byte_offset{0U};
    std::size_t samples_per_style{0U};
};

struct PageLayout {
    std::uint32_t used_height{0U};
};

[[nodiscard]] std::size_t rgba_offset(
    const std::uint32_t page_width,
    const std::uint32_t x,
    const std::uint32_t y) noexcept
{
    return (static_cast<std::size_t>(y) * page_width + x) * 4U;
}

[[nodiscard]] assets::WorldSurfaceLightStyles source_styles(
    const assets::WorldSurface& surface) noexcept
{
    assets::WorldSurfaceLightStyles result;
    for (const auto style : surface.light_styles) {
        if (style == 0xFFU) {
            break;
        }
        const auto slot = static_cast<std::size_t>(result.style_count);
        if (slot >= result.style_ids.size()) {
            break;
        }
        result.style_ids[slot] = style;
        ++result.style_count;
    }
    return result;
}

} // namespace

bool valid_goldsrc_world_lightmap_import_limits(
    const GoldSrcWorldLightmapImportLimits& limits) noexcept
{
    std::size_t padding_twice = 0U;
    return limits.maximum_surface_count > 0U &&
        limits.maximum_surface_count <= kGoldSrcLightmapHardMaximumSurfaceCount &&
        limits.maximum_samples_per_surface > 0U &&
        limits.maximum_samples_per_surface <=
            kGoldSrcLightmapHardMaximumSamplesPerSurface &&
        limits.maximum_total_source_samples > 0U &&
        limits.atlas_width > 0U &&
        limits.atlas_width <= limits.maximum_atlas_dimension &&
        limits.maximum_atlas_dimension > 0U &&
        limits.maximum_atlas_dimension <=
            kGoldSrcLightmapHardMaximumAtlasDimension &&
        limits.maximum_atlas_pages > 0U &&
        limits.maximum_atlas_pages <= kGoldSrcLightmapHardMaximumAtlasPages &&
        limits.maximum_total_atlas_rgba_bytes > 0U &&
        limits.maximum_total_atlas_rgba_bytes <=
            kGoldSrcLightmapHardMaximumTotalAtlasRgbaBytes &&
        limits.atlas_padding == 1U &&
        limits.maximum_style_count > 0U &&
        limits.maximum_style_count <= assets::kWorldLightmapStyleSlotCount &&
        checked_multiply(static_cast<std::size_t>(limits.atlas_padding),
            2U,
            padding_twice) &&
        padding_twice < limits.atlas_width &&
        padding_twice < limits.maximum_atlas_dimension;
}

std::string_view to_string(const GoldSrcLightmapExtentErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcLightmapExtentErrorCode::invalid_configuration:
        return "invalid_configuration";
    case GoldSrcLightmapExtentErrorCode::invalid_surface_vertex_range:
        return "invalid_surface_vertex_range";
    case GoldSrcLightmapExtentErrorCode::non_finite_texture_coordinate:
        return "non_finite_texture_coordinate";
    case GoldSrcLightmapExtentErrorCode::coordinate_range_overflow:
        return "coordinate_range_overflow";
    case GoldSrcLightmapExtentErrorCode::sample_limit_exceeded:
        return "sample_limit_exceeded";
    case GoldSrcLightmapExtentErrorCode::coordinate_outside_extent:
        return "coordinate_outside_extent";
    }
    return "unknown";
}

GoldSrcLightmapExtentResult calculate_goldsrc_lightmap_extents(
    const assets::WorldAsset& world,
    const assets::WorldSurface& surface,
    const std::size_t maximum_samples_per_surface) noexcept
{
    const auto fail = [](const GoldSrcLightmapExtentErrorCode code,
                          const std::optional<std::size_t> vertex_index,
                          const std::string_view context) noexcept {
        GoldSrcLightmapExtentError error;
        error.code = code;
        error.vertex_index = vertex_index;
        try {
            error.context.assign(context.data(), context.size());
        } catch (...) {
        }
        return GoldSrcLightmapExtentResult{std::nullopt, std::move(error)};
    };

    if (maximum_samples_per_surface == 0U ||
        maximum_samples_per_surface >
            kGoldSrcLightmapHardMaximumSamplesPerSurface) {
        return fail(GoldSrcLightmapExtentErrorCode::invalid_configuration,
            std::nullopt,
            "Maximum samples per surface is outside the supported range");
    }
    const auto first_vertex = static_cast<std::size_t>(surface.first_vertex);
    const auto vertex_count = static_cast<std::size_t>(surface.vertex_count);
    if (vertex_count < 3U || first_vertex > world.vertices.size() ||
        vertex_count > world.vertices.size() - first_vertex) {
        return fail(GoldSrcLightmapExtentErrorCode::invalid_surface_vertex_range,
            std::nullopt,
            "Face-local vertex range is not bounded by the owning world asset");
    }

    double minimum_s = std::numeric_limits<double>::infinity();
    double minimum_t = std::numeric_limits<double>::infinity();
    double maximum_s = -std::numeric_limits<double>::infinity();
    double maximum_t = -std::numeric_limits<double>::infinity();
    for (std::size_t local_index = 0U; local_index < vertex_count; ++local_index) {
        const auto& coordinate =
            world.vertices[first_vertex + local_index].texture_coordinate;
        const auto s = static_cast<double>(coordinate.x);
        const auto t = static_cast<double>(coordinate.y);
        if (!std::isfinite(s) || !std::isfinite(t)) {
            return fail(
                GoldSrcLightmapExtentErrorCode::non_finite_texture_coordinate,
                local_index,
                "Face-local raw S/T coordinate is not finite");
        }
        minimum_s = (std::min)(minimum_s, s);
        minimum_t = (std::min)(minimum_t, t);
        maximum_s = (std::max)(maximum_s, s);
        maximum_t = (std::max)(maximum_t, t);
    }

    constexpr auto block_size = static_cast<double>(kGoldSrcLightmapTextureBlockSize);
    const auto minimum_block_s_value = std::floor(minimum_s / block_size);
    const auto minimum_block_t_value = std::floor(minimum_t / block_size);
    const auto maximum_block_s_value = std::ceil(maximum_s / block_size);
    const auto maximum_block_t_value = std::ceil(maximum_t / block_size);
    constexpr auto minimum_texture_block =
        static_cast<double>(std::numeric_limits<std::int32_t>::min()) /
        block_size;
    constexpr auto maximum_texture_block =
        static_cast<double>(std::numeric_limits<std::int32_t>::max()) /
        block_size;
    if (minimum_block_s_value < minimum_texture_block ||
        minimum_block_t_value < minimum_texture_block ||
        maximum_block_s_value > maximum_texture_block ||
        maximum_block_t_value > maximum_texture_block) {
        return fail(GoldSrcLightmapExtentErrorCode::coordinate_range_overflow,
            std::nullopt,
            "Texture-block minima or maxima cannot be represented safely");
    }

    const auto minimum_block_s = static_cast<std::int64_t>(minimum_block_s_value);
    const auto minimum_block_t = static_cast<std::int64_t>(minimum_block_t_value);
    const auto maximum_block_s = static_cast<std::int64_t>(maximum_block_s_value);
    const auto maximum_block_t = static_cast<std::int64_t>(maximum_block_t_value);
    const auto block_count_s = maximum_block_s - minimum_block_s;
    const auto block_count_t = maximum_block_t - minimum_block_t;
    if (block_count_s < 0 || block_count_t < 0 ||
        static_cast<std::uint64_t>(block_count_s) >=
            std::numeric_limits<std::uint32_t>::max() ||
        static_cast<std::uint64_t>(block_count_t) >=
            std::numeric_limits<std::uint32_t>::max()) {
        return fail(GoldSrcLightmapExtentErrorCode::coordinate_range_overflow,
            std::nullopt,
            "Texture-block extent overflows the format-neutral dimensions");
    }

    const auto sample_width = static_cast<std::uint32_t>(block_count_s) + 1U;
    const auto sample_height = static_cast<std::uint32_t>(block_count_t) + 1U;
    std::size_t sample_count = 0U;
    if (!checked_multiply(static_cast<std::size_t>(sample_width),
            static_cast<std::size_t>(sample_height),
            sample_count) ||
        sample_count == 0U || sample_count > maximum_samples_per_surface) {
        return fail(GoldSrcLightmapExtentErrorCode::sample_limit_exceeded,
            std::nullopt,
            "Surface lightmap sample count exceeds the configured limit");
    }

    const auto texture_min_s_value =
        minimum_block_s * static_cast<std::int64_t>(kGoldSrcLightmapTextureBlockSize);
    const auto texture_min_t_value =
        minimum_block_t * static_cast<std::int64_t>(kGoldSrcLightmapTextureBlockSize);
    const auto maximum_local_s = static_cast<double>(sample_width - 1U);
    const auto maximum_local_t = static_cast<double>(sample_height - 1U);
    for (std::size_t local_index = 0U; local_index < vertex_count; ++local_index) {
        const auto& coordinate =
            world.vertices[first_vertex + local_index].texture_coordinate;
        const auto local_s =
            (static_cast<double>(coordinate.x) -
                static_cast<double>(texture_min_s_value)) /
            block_size;
        const auto local_t =
            (static_cast<double>(coordinate.y) -
                static_cast<double>(texture_min_t_value)) /
            block_size;
        if (!std::isfinite(local_s) || !std::isfinite(local_t) ||
            local_s < -kGoldSrcLightmapCoordinateTolerance ||
            local_t < -kGoldSrcLightmapCoordinateTolerance ||
            local_s > maximum_local_s + kGoldSrcLightmapCoordinateTolerance ||
            local_t > maximum_local_t + kGoldSrcLightmapCoordinateTolerance) {
            return fail(GoldSrcLightmapExtentErrorCode::coordinate_outside_extent,
                local_index,
                "Face-local S/T coordinate maps outside the calculated extent");
        }
    }

    const auto extent_s = static_cast<std::uint64_t>(block_count_s) *
        kGoldSrcLightmapTextureBlockSize;
    const auto extent_t = static_cast<std::uint64_t>(block_count_t) *
        kGoldSrcLightmapTextureBlockSize;
    if (extent_s > std::numeric_limits<std::uint32_t>::max() ||
        extent_t > std::numeric_limits<std::uint32_t>::max()) {
        return fail(GoldSrcLightmapExtentErrorCode::coordinate_range_overflow,
            std::nullopt,
            "Texel-space extent overflows the format-neutral dimensions");
    }
    return GoldSrcLightmapExtentResult{
        GoldSrcLightmapExtents{
            static_cast<std::int32_t>(texture_min_s_value),
            static_cast<std::int32_t>(texture_min_t_value),
            static_cast<std::uint32_t>(extent_s),
            static_cast<std::uint32_t>(extent_t),
            sample_width,
            sample_height,
        },
        std::nullopt,
    };
}

std::string_view to_string(
    const GoldSrcWorldLightmapImportErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcWorldLightmapImportErrorCode::invalid_configuration:
        return "invalid_configuration";
    case GoldSrcWorldLightmapImportErrorCode::invalid_world_asset:
        return "invalid_world_asset";
    case GoldSrcWorldLightmapImportErrorCode::invalid_bsp_source:
        return "invalid_bsp_source";
    case GoldSrcWorldLightmapImportErrorCode::invalid_lighting_lump:
        return "invalid_lighting_lump";
    case GoldSrcWorldLightmapImportErrorCode::surface_limit_exceeded:
        return "surface_limit_exceeded";
    case GoldSrcWorldLightmapImportErrorCode::invalid_surface_vertex_range:
        return "invalid_surface_vertex_range";
    case GoldSrcWorldLightmapImportErrorCode::invalid_lightmap_extent:
        return "invalid_lightmap_extent";
    case GoldSrcWorldLightmapImportErrorCode::invalid_lightmap_metadata:
        return "invalid_lightmap_metadata";
    case GoldSrcWorldLightmapImportErrorCode::lightmap_range_out_of_bounds:
        return "lightmap_range_out_of_bounds";
    case GoldSrcWorldLightmapImportErrorCode::source_sample_limit_exceeded:
        return "source_sample_limit_exceeded";
    case GoldSrcWorldLightmapImportErrorCode::atlas_rectangle_limit_exceeded:
        return "atlas_rectangle_limit_exceeded";
    case GoldSrcWorldLightmapImportErrorCode::atlas_page_limit_exceeded:
        return "atlas_page_limit_exceeded";
    case GoldSrcWorldLightmapImportErrorCode::atlas_memory_limit_exceeded:
        return "atlas_memory_limit_exceeded";
    case GoldSrcWorldLightmapImportErrorCode::lightmap_set_build_failed:
        return "lightmap_set_build_failed";
    case GoldSrcWorldLightmapImportErrorCode::unable_to_retain_lightmap_set:
        return "unable_to_retain_lightmap_set";
    }
    return "unknown";
}

GoldSrcWorldLightmapImportResult GoldSrcWorldLightmapImporter::import(
    const assets::WorldAsset& world,
    const std::span<const std::byte> retained_bsp_source,
    const GoldSrcWorldLightmapImportLimits& limits)
{
    if (!valid_goldsrc_world_lightmap_import_limits(limits)) {
        return import_failure(
            GoldSrcWorldLightmapImportErrorCode::invalid_configuration,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "GoldSrc world lightmap import limits are invalid");
    }
    if (world.source_profile !=
        assets::WorldGeometrySourceProfile::goldsrc_bsp_v30) {
        return import_failure(
            GoldSrcWorldLightmapImportErrorCode::invalid_world_asset,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "World asset is not a GoldSrc BSP v30 CPU geometry publication");
    }
    if (world.surfaces.size() > limits.maximum_surface_count) {
        return import_failure(
            GoldSrcWorldLightmapImportErrorCode::surface_limit_exceeded,
            world.surfaces.size(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "World surface count exceeds the configured lightmap limit");
    }
    const auto lighting = lighting_lump(retained_bsp_source);
    if (!lighting.bytes) {
        return import_failure(lighting.error_code,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Retained BSP source has no bounded BSP v30 lighting lump");
    }

    try {
        std::vector<assets::WorldSurfaceLightmapBinding> bindings;
        std::vector<PreparedSurface> prepared_surfaces;
        bindings.reserve(world.surfaces.size());
        prepared_surfaces.reserve(world.surfaces.size());
        std::size_t total_source_samples = 0U;

        for (std::size_t surface_index = 0U;
             surface_index < world.surfaces.size();
             ++surface_index) {
            const auto& surface = world.surfaces[surface_index];
            const auto extent_result = calculate_goldsrc_lightmap_extents(
                world, surface, limits.maximum_samples_per_surface);
            if (!extent_result) {
                const auto extent_code = extent_result.error
                    ? extent_result.error->code
                    : GoldSrcLightmapExtentErrorCode::invalid_surface_vertex_range;
                return import_failure(
                    extent_code ==
                            GoldSrcLightmapExtentErrorCode::invalid_surface_vertex_range
                        ? GoldSrcWorldLightmapImportErrorCode::
                              invalid_surface_vertex_range
                        : GoldSrcWorldLightmapImportErrorCode::
                              invalid_lightmap_extent,
                    surface_index,
                    assets::WorldSurfaceLightmapBindingStatus::invalid_metadata,
                    extent_code,
                    std::nullopt,
                    extent_result.error
                        ? std::string_view{extent_result.error->context}
                        : std::string_view{"Unable to calculate surface lightmap extents"});
            }
            const auto& extents = *extent_result.extents;
            const auto styles = source_styles(surface);
            assets::WorldSurfaceLightmapBinding binding;
            binding.surface_index = surface_index;
            binding.texture_min_s = extents.texture_min_s;
            binding.texture_min_t = extents.texture_min_t;
            binding.sample_width = extents.sample_width;
            binding.sample_height = extents.sample_height;
            binding.source_styles = styles;

            if (!surface.lightmap_offset) {
                if (styles.style_count != 0U) {
                    return import_failure(
                        GoldSrcWorldLightmapImportErrorCode::
                            invalid_lightmap_metadata,
                        surface_index,
                        assets::WorldSurfaceLightmapBindingStatus::invalid_metadata,
                        std::nullopt,
                        std::nullopt,
                        "Surface has active light styles but no lightmap offset");
                }
                binding.status =
                    assets::WorldSurfaceLightmapBindingStatus::unlit_no_lightmap;
                bindings.push_back(std::move(binding));
                continue;
            }

            if (styles.style_count == 0U) {
                return import_failure(
                    GoldSrcWorldLightmapImportErrorCode::invalid_lightmap_metadata,
                    surface_index,
                    assets::WorldSurfaceLightmapBindingStatus::invalid_metadata,
                    std::nullopt,
                    std::nullopt,
                    "Lightmapped surface has no active source light style");
            }
            if (styles.style_count > limits.maximum_style_count) {
                return import_failure(
                    GoldSrcWorldLightmapImportErrorCode::invalid_lightmap_metadata,
                    surface_index,
                    assets::WorldSurfaceLightmapBindingStatus::
                        unsupported_style_profile,
                    std::nullopt,
                    std::nullopt,
                    "Surface source light-style count exceeds the configured profile");
            }

            std::size_t samples_per_style = 0U;
            std::size_t surface_source_samples = 0U;
            std::size_t bytes_per_style = 0U;
            std::size_t total_source_bytes = 0U;
            if (!checked_multiply(static_cast<std::size_t>(extents.sample_width),
                    static_cast<std::size_t>(extents.sample_height),
                    samples_per_style) ||
                !checked_multiply(samples_per_style,
                    static_cast<std::size_t>(styles.style_count),
                    surface_source_samples) ||
                !checked_add(total_source_samples,
                    surface_source_samples,
                    total_source_samples) ||
                total_source_samples > limits.maximum_total_source_samples ||
                !checked_multiply(samples_per_style,
                    kGoldSrcLightmapBytesPerSample,
                    bytes_per_style) ||
                !checked_multiply(bytes_per_style,
                    static_cast<std::size_t>(styles.style_count),
                    total_source_bytes)) {
                return import_failure(
                    GoldSrcWorldLightmapImportErrorCode::
                        source_sample_limit_exceeded,
                    surface_index,
                    assets::WorldSurfaceLightmapBindingStatus::invalid_metadata,
                    std::nullopt,
                    std::nullopt,
                    "Aggregate source lightmap sample count exceeds its configured limit");
            }
            const auto source_byte_offset =
                static_cast<std::size_t>(*surface.lightmap_offset);
            if (source_byte_offset > lighting.bytes->size() ||
                total_source_bytes > lighting.bytes->size() - source_byte_offset) {
                return import_failure(
                    GoldSrcWorldLightmapImportErrorCode::
                        lightmap_range_out_of_bounds,
                    surface_index,
                    assets::WorldSurfaceLightmapBindingStatus::range_out_of_bounds,
                    std::nullopt,
                    std::nullopt,
                    "Surface RGB style blocks extend outside the BSP lighting lump");
            }
            binding.status = assets::WorldSurfaceLightmapBindingStatus::resolved;
            bindings.push_back(std::move(binding));
            prepared_surfaces.push_back(PreparedSurface{
                surface_index, source_byte_offset, samples_per_style});
        }

        std::vector<PageLayout> page_layouts;
        std::uint32_t cursor_x = 0U;
        std::uint32_t cursor_y = 0U;
        std::uint32_t shelf_height = 0U;
        for (const auto& prepared : prepared_surfaces) {
            auto& binding = bindings[prepared.surface_index];
            const auto padding_twice = limits.atlas_padding * 2U;
            if (binding.sample_width >
                    std::numeric_limits<std::uint32_t>::max() - padding_twice ||
                binding.sample_height >
                    std::numeric_limits<std::uint32_t>::max() - padding_twice) {
                return import_failure(
                    GoldSrcWorldLightmapImportErrorCode::
                        atlas_rectangle_limit_exceeded,
                    prepared.surface_index,
                    assets::WorldSurfaceLightmapBindingStatus::atlas_limit_exceeded,
                    std::nullopt,
                    std::nullopt,
                    "Padded surface lightmap rectangle overflows its dimensions");
            }
            const auto padded_width = binding.sample_width + padding_twice;
            const auto padded_height = binding.sample_height + padding_twice;
            if (padded_width > limits.atlas_width ||
                padded_height > limits.maximum_atlas_dimension) {
                return import_failure(
                    GoldSrcWorldLightmapImportErrorCode::
                        atlas_rectangle_limit_exceeded,
                    prepared.surface_index,
                    assets::WorldSurfaceLightmapBindingStatus::atlas_limit_exceeded,
                    std::nullopt,
                    std::nullopt,
                    "Padded surface lightmap rectangle is larger than an atlas page");
            }

            if (page_layouts.empty()) {
                page_layouts.push_back(PageLayout{});
            }
            if (cursor_x > limits.atlas_width - padded_width) {
                cursor_x = 0U;
                if (shelf_height > limits.maximum_atlas_dimension - cursor_y) {
                    cursor_y = limits.maximum_atlas_dimension;
                } else {
                    cursor_y += shelf_height;
                }
                shelf_height = 0U;
            }
            if (cursor_y > limits.maximum_atlas_dimension - padded_height) {
                if (page_layouts.size() >= limits.maximum_atlas_pages) {
                    return import_failure(
                        GoldSrcWorldLightmapImportErrorCode::
                            atlas_page_limit_exceeded,
                        prepared.surface_index,
                        assets::WorldSurfaceLightmapBindingStatus::
                            atlas_limit_exceeded,
                        std::nullopt,
                        std::nullopt,
                        "Deterministic atlas packing exceeds the configured page limit");
                }
                page_layouts.push_back(PageLayout{});
                cursor_x = 0U;
                cursor_y = 0U;
                shelf_height = 0U;
            }

            binding.atlas_page_index = page_layouts.size() - 1U;
            binding.padded_rectangle = assets::WorldLightmapRectangle{
                cursor_x, cursor_y, padded_width, padded_height};
            binding.inner_rectangle = assets::WorldLightmapRectangle{
                cursor_x + limits.atlas_padding,
                cursor_y + limits.atlas_padding,
                binding.sample_width,
                binding.sample_height};
            cursor_x += padded_width;
            shelf_height = (std::max)(shelf_height, padded_height);
            page_layouts.back().used_height = (std::max)(
                page_layouts.back().used_height, cursor_y + padded_height);
        }

        std::size_t total_atlas_rgba_bytes = 0U;
        std::vector<assets::WorldLightmapAtlasPage> pages;
        pages.reserve(page_layouts.size());
        for (const auto& layout : page_layouts) {
            std::size_t pixel_count = 0U;
            std::size_t image_byte_count = 0U;
            std::size_t page_byte_count = 0U;
            if (layout.used_height == 0U ||
                !checked_multiply(static_cast<std::size_t>(limits.atlas_width),
                    static_cast<std::size_t>(layout.used_height),
                    pixel_count) ||
                !checked_multiply(pixel_count, 4U, image_byte_count) ||
                !checked_multiply(image_byte_count,
                    assets::kWorldLightmapStyleSlotCount,
                    page_byte_count) ||
                !checked_add(total_atlas_rgba_bytes,
                    page_byte_count,
                    total_atlas_rgba_bytes) ||
                total_atlas_rgba_bytes >
                    limits.maximum_total_atlas_rgba_bytes) {
                return import_failure(
                    GoldSrcWorldLightmapImportErrorCode::
                        atlas_memory_limit_exceeded,
                    std::nullopt,
                    assets::WorldSurfaceLightmapBindingStatus::atlas_limit_exceeded,
                    std::nullopt,
                    std::nullopt,
                    "Owning four-layer atlas RGBA storage exceeds its configured limit");
            }

            assets::WorldLightmapAtlasPage page;
            page.width = limits.atlas_width;
            page.height = layout.used_height;
            for (auto& image : page.style_slot_images) {
                image.width = page.width;
                image.height = page.height;
                image.rgba_pixels.assign(image_byte_count, std::byte{0U});
                for (std::size_t alpha = 3U; alpha < image_byte_count;
                     alpha += 4U) {
                    image.rgba_pixels[alpha] = std::byte{0xFFU};
                }
            }
            pages.push_back(std::move(page));
        }

        for (const auto& prepared : prepared_surfaces) {
            const auto& binding = bindings[prepared.surface_index];
            auto& page = pages[*binding.atlas_page_index];
            for (std::size_t style_slot = 0U;
                 style_slot < binding.source_styles.style_count;
                 ++style_slot) {
                auto& image = page.style_slot_images[style_slot];
                const auto source_style_offset = prepared.source_byte_offset +
                    style_slot * prepared.samples_per_style *
                        kGoldSrcLightmapBytesPerSample;
                for (std::uint32_t padded_y = 0U;
                     padded_y < binding.padded_rectangle.height;
                     ++padded_y) {
                    const auto local_y = padded_y <= limits.atlas_padding
                        ? 0U
                        : (std::min)(
                              padded_y - limits.atlas_padding,
                              binding.sample_height - 1U);
                    for (std::uint32_t padded_x = 0U;
                         padded_x < binding.padded_rectangle.width;
                         ++padded_x) {
                        const auto local_x = padded_x <= limits.atlas_padding
                            ? 0U
                            : (std::min)(
                                  padded_x - limits.atlas_padding,
                                  binding.sample_width - 1U);
                        const auto sample_index =
                            static_cast<std::size_t>(local_y) *
                                binding.sample_width +
                            local_x;
                        const auto source_offset = source_style_offset +
                            sample_index * kGoldSrcLightmapBytesPerSample;
                        const auto destination_offset = rgba_offset(
                            page.width,
                            binding.padded_rectangle.x + padded_x,
                            binding.padded_rectangle.y + padded_y);
                        image.rgba_pixels[destination_offset] =
                            (*lighting.bytes)[source_offset];
                        image.rgba_pixels[destination_offset + 1U] =
                            (*lighting.bytes)[source_offset + 1U];
                        image.rgba_pixels[destination_offset + 2U] =
                            (*lighting.bytes)[source_offset + 2U];
                    }
                }
            }
        }

        assets::WorldLightmapSetLimits set_limits;
        set_limits.maximum_surface_binding_count = limits.maximum_surface_count;
        set_limits.maximum_atlas_page_count = limits.maximum_atlas_pages;
        set_limits.maximum_atlas_dimension = limits.maximum_atlas_dimension;
        set_limits.maximum_total_atlas_rgba_bytes =
            limits.maximum_total_atlas_rgba_bytes;
        auto created = assets::WorldLightmapSet::create(std::move(pages),
            std::move(bindings),
            world.surfaces.size(),
            set_limits);
        if (!created) {
            return import_failure(
                GoldSrcWorldLightmapImportErrorCode::lightmap_set_build_failed,
                created.error ? created.error->element_index : std::nullopt,
                std::nullopt,
                std::nullopt,
                created.error
                    ? std::optional{created.error->code}
                    : std::nullopt,
                created.error
                    ? std::string_view{created.error->context}
                    : std::string_view{
                          "Transactional world lightmap-set creation failed"});
        }
        return GoldSrcWorldLightmapImportResult{
            std::move(created.lightmap_set), std::nullopt};
    } catch (const std::bad_alloc&) {
        return import_failure(
            GoldSrcWorldLightmapImportErrorCode::unable_to_retain_lightmap_set,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Unable to allocate bounded owning lightmap import state");
    } catch (...) {
        return import_failure(
            GoldSrcWorldLightmapImportErrorCode::unable_to_retain_lightmap_set,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "Unexpected failure while building the owning lightmap set");
    }
}

} // namespace hlclient::goldsrc::lightmaps
