#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::assets {

inline constexpr std::size_t kWorldLightmapStyleSlotCount = 4U;

enum class WorldLightmapPixelFormat {
    rgba8,
};

enum class WorldLightmapCompatibilityProfile {
    goldsrc_rgb_lightmap_v1,
};

enum class WorldLightmapEvidenceProfile {
    valve_public_tools_and_synthetic_fixtures,
};

struct WorldLightmapRectangle {
    std::uint32_t x{0U};
    std::uint32_t y{0U};
    std::uint32_t width{0U};
    std::uint32_t height{0U};
};

struct WorldSurfaceLightStyles {
    std::uint8_t style_count{0U};
    std::array<std::uint8_t, kWorldLightmapStyleSlotCount> style_ids{
        0xFFU, 0xFFU, 0xFFU, 0xFFU};
};

struct WorldLightmapImage {
    std::uint32_t width{0U};
    std::uint32_t height{0U};
    WorldLightmapPixelFormat pixel_format{WorldLightmapPixelFormat::rgba8};
    std::vector<std::byte> rgba_pixels;
};

// All four images use the same dimensions and rectangle layout. Source style
// slots which are unused by a surface remain black with alpha 255.
struct WorldLightmapAtlasPage {
    std::uint32_t width{0U};
    std::uint32_t height{0U};
    std::array<WorldLightmapImage, kWorldLightmapStyleSlotCount>
        style_slot_images{};
};

enum class WorldSurfaceLightmapBindingStatus {
    resolved,
    unlit_no_lightmap,
    invalid_metadata,
    range_out_of_bounds,
    atlas_limit_exceeded,
    unsupported_style_profile,
};

struct WorldSurfaceLightmapBinding {
    std::size_t surface_index{0U};
    WorldSurfaceLightmapBindingStatus status{
        WorldSurfaceLightmapBindingStatus::invalid_metadata};
    std::optional<std::size_t> atlas_page_index;
    WorldLightmapRectangle inner_rectangle{};
    WorldLightmapRectangle padded_rectangle{};
    std::int32_t texture_min_s{0};
    std::int32_t texture_min_t{0};
    std::uint32_t sample_width{0U};
    std::uint32_t sample_height{0U};
    WorldSurfaceLightStyles source_styles{};
    // M4.3 retains every source layer but intentionally renders only the
    // first source slot. Dynamic light-style blending is a later milestone.
    std::uint8_t selected_static_source_style_slot{0U};
    WorldLightmapCompatibilityProfile compatibility_profile{
        WorldLightmapCompatibilityProfile::goldsrc_rgb_lightmap_v1};
    WorldLightmapEvidenceProfile evidence_profile{
        WorldLightmapEvidenceProfile::valve_public_tools_and_synthetic_fixtures};
};

struct WorldLightmapStatistics {
    std::size_t surface_binding_count{0U};
    std::size_t resolved_surface_count{0U};
    std::size_t unlit_surface_count{0U};
    std::size_t atlas_page_count{0U};
    std::size_t retained_source_style_count{0U};
    std::size_t total_source_sample_count{0U};
    std::size_t total_atlas_rgba_byte_count{0U};
};

struct WorldLightmapSetLimits {
    std::size_t maximum_surface_binding_count{65'535U};
    std::size_t maximum_atlas_page_count{16U};
    std::uint32_t maximum_atlas_dimension{2'048U};
    std::size_t maximum_total_atlas_rgba_bytes{256U * 1024U * 1024U};
};

enum class WorldLightmapSetErrorCode {
    invalid_configuration,
    binding_count_mismatch,
    atlas_page_count_limit_exceeded,
    invalid_atlas_page,
    invalid_surface_binding,
    total_atlas_rgba_bytes_limit_exceeded,
    unable_to_retain_lightmap_set,
};

[[nodiscard]] std::string_view to_string(WorldLightmapSetErrorCode code) noexcept;

struct WorldLightmapSetError {
    WorldLightmapSetErrorCode code{
        WorldLightmapSetErrorCode::invalid_configuration};
    std::optional<std::size_t> element_index;
    std::string context;
};

struct WorldLightmapSetCreateResult;

// Immutable, owning, renderer-neutral CPU lightmap publication. Creation
// validates every page and per-surface binding before publishing any state.
class WorldLightmapSet final {
public:
    [[nodiscard]] static WorldLightmapSetCreateResult create(
        std::vector<WorldLightmapAtlasPage> pages,
        std::vector<WorldSurfaceLightmapBinding> bindings,
        std::size_t expected_surface_count,
        const WorldLightmapSetLimits& limits = {});

    WorldLightmapSet(const WorldLightmapSet&) = default;
    WorldLightmapSet(WorldLightmapSet&&) noexcept = default;
    WorldLightmapSet& operator=(const WorldLightmapSet&) = delete;
    WorldLightmapSet& operator=(WorldLightmapSet&&) noexcept = delete;
    ~WorldLightmapSet() = default;

    [[nodiscard]] std::span<const WorldLightmapAtlasPage> pages() const noexcept;
    [[nodiscard]] std::span<const WorldSurfaceLightmapBinding> bindings()
        const noexcept;
    [[nodiscard]] std::size_t page_count() const noexcept;
    [[nodiscard]] std::size_t binding_count() const noexcept;
    [[nodiscard]] const WorldSurfaceLightmapBinding* binding_for_surface(
        std::size_t surface_index) const noexcept;
    [[nodiscard]] bool complete_for_world_surfaces() const noexcept;
    [[nodiscard]] const WorldLightmapStatistics& statistics() const noexcept;
    [[nodiscard]] WorldLightmapCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] WorldLightmapEvidenceProfile evidence_profile() const noexcept;

private:
    WorldLightmapSet(
        std::vector<WorldLightmapAtlasPage> pages,
        std::vector<WorldSurfaceLightmapBinding> bindings,
        WorldLightmapStatistics statistics) noexcept;

    std::vector<WorldLightmapAtlasPage> pages_;
    std::vector<WorldSurfaceLightmapBinding> bindings_;
    WorldLightmapStatistics statistics_{};
};

struct WorldLightmapSetCreateResult {
    std::optional<WorldLightmapSet> lightmap_set;
    std::optional<WorldLightmapSetError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return lightmap_set.has_value();
    }
};

[[nodiscard]] constexpr bool is_renderable(
    const WorldSurfaceLightmapBindingStatus status) noexcept
{
    return status == WorldSurfaceLightmapBindingStatus::resolved ||
        status == WorldSurfaceLightmapBindingStatus::unlit_no_lightmap;
}

} // namespace hlclient::assets
