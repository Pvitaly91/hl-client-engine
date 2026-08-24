#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/assets/world_lightmap_types.hpp>
#include <hlclient/assets/world_texture_types.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace hlclient::world_render {

struct WorldRenderVertex {
    assets::AssetVector3 position{};
    assets::AssetVector3 normal{};
    assets::AssetVector2 base_texture_coordinate{};
    assets::AssetVector2 lightmap_atlas_coordinate{};
};

enum class WorldRenderLightmapMode {
    atlas,
    unlit_white,
};

enum class WorldRenderCompatibilityProfile {
    goldsrc_static_world_v1,
};

enum class WorldRenderEvidenceProfile {
    validated_geometry_texture_and_lightmap_bindings,
};

struct WorldRenderMaterial {
    std::size_t material_index{0U};
    std::size_t base_texture_asset_index{0U};
    assets::WorldTextureAlphaMode base_texture_alpha_mode{
        assets::WorldTextureAlphaMode::opaque};
    WorldRenderLightmapMode lightmap_mode{WorldRenderLightmapMode::unlit_white};
    std::optional<std::size_t> lightmap_atlas_page_index;
    bool special_surface{false};
    WorldRenderCompatibilityProfile compatibility_profile{
        WorldRenderCompatibilityProfile::goldsrc_static_world_v1};
    WorldRenderEvidenceProfile evidence_profile{
        WorldRenderEvidenceProfile::validated_geometry_texture_and_lightmap_bindings};
};

struct WorldDrawBatch {
    std::uint32_t first_index{0U};
    std::uint32_t index_count{0U};
    std::size_t render_material_index{0U};
    assets::WorldTextureAlphaMode alpha_mode{assets::WorldTextureAlphaMode::opaque};
    WorldRenderLightmapMode lightmap_mode{WorldRenderLightmapMode::unlit_white};
    std::optional<std::size_t> lightmap_atlas_page_index;
    std::size_t source_surface_count{0U};
};

enum class WorldRenderBaseTextureCoordinateSpace {
    normalized_source_rows_no_cpu_flip,
};

enum class WorldRenderLightmapCoordinateSpace {
    normalized_atlas_texel_centers_no_cpu_flip,
};

struct WorldRenderCoordinateMetadata {
    assets::WorldCoordinateSpace world_coordinate_space{
        assets::WorldCoordinateSpace::source_native_goldsrc_z_up};
    assets::WorldTextureCoordinateSpace source_texture_coordinate_space{
        assets::WorldTextureCoordinateSpace::texel_units};
    WorldRenderBaseTextureCoordinateSpace base_texture_coordinate_space{
        WorldRenderBaseTextureCoordinateSpace::normalized_source_rows_no_cpu_flip};
    WorldRenderLightmapCoordinateSpace lightmap_coordinate_space{
        WorldRenderLightmapCoordinateSpace::normalized_atlas_texel_centers_no_cpu_flip};
};

struct WorldRenderStatistics {
    std::size_t vertex_count{0U};
    std::size_t index_count{0U};
    std::size_t triangle_count{0U};
    std::size_t material_count{0U};
    std::size_t batch_count{0U};
    std::size_t source_surface_count{0U};
    std::size_t opaque_batch_count{0U};
    std::size_t masked_batch_count{0U};
    std::size_t atlas_batch_count{0U};
    std::size_t unlit_batch_count{0U};
    std::size_t base_texture_rgba_byte_count{0U};
    std::size_t lightmap_rgba_byte_count{0U};
    std::size_t output_geometry_byte_count{0U};
    std::size_t total_cpu_render_byte_count{0U};
};

struct WorldRendererResourceIdentity {
    std::uint64_t resource_id{0U};
    std::uint64_t revision{0U};

    [[nodiscard]] friend bool operator==(
        const WorldRendererResourceIdentity&,
        const WorldRendererResourceIdentity&) = default;
};

class WorldRenderPackageBuilder;

// Immutable owning renderer input. The nested world/texture/lightmap snapshots
// are retained so renderer backends never need filesystem or parser access.
class WorldRenderPackage final {
public:
    WorldRenderPackage(const WorldRenderPackage&) = delete;
    WorldRenderPackage& operator=(const WorldRenderPackage&) = delete;
    WorldRenderPackage(WorldRenderPackage&&) noexcept = default;
    WorldRenderPackage& operator=(WorldRenderPackage&&) noexcept = delete;
    ~WorldRenderPackage() = default;

    [[nodiscard]] const assets::TexturedWorldAsset& textured_world() const noexcept;
    [[nodiscard]] const assets::WorldLightmapSet& lightmaps() const noexcept;
    [[nodiscard]] std::span<const WorldRenderVertex> vertices() const noexcept;
    [[nodiscard]] std::span<const std::uint32_t> indices() const noexcept;
    [[nodiscard]] std::span<const WorldRenderMaterial> materials() const noexcept;
    [[nodiscard]] std::span<const WorldDrawBatch> draw_batches() const noexcept;
    [[nodiscard]] const assets::WorldBounds& bounds() const noexcept;
    [[nodiscard]] const WorldRenderCoordinateMetadata& coordinate_metadata() const noexcept;
    [[nodiscard]] const WorldRenderStatistics& statistics() const noexcept;
    [[nodiscard]] WorldRendererResourceIdentity resource_identity() const noexcept;
    [[nodiscard]] std::uint64_t resource_id() const noexcept;
    [[nodiscard]] std::uint64_t resource_revision() const noexcept;

private:
    friend class WorldRenderPackageBuilder;

    WorldRenderPackage(
        assets::TexturedWorldAsset textured_world,
        assets::WorldLightmapSet lightmaps,
        std::vector<WorldRenderVertex> vertices,
        std::vector<std::uint32_t> indices,
        std::vector<WorldRenderMaterial> materials,
        std::vector<WorldDrawBatch> draw_batches,
        assets::WorldBounds bounds,
        WorldRenderCoordinateMetadata coordinate_metadata,
        WorldRenderStatistics statistics,
        WorldRendererResourceIdentity resource_identity) noexcept;

    assets::TexturedWorldAsset textured_world_;
    assets::WorldLightmapSet lightmaps_;
    std::vector<WorldRenderVertex> vertices_;
    std::vector<std::uint32_t> indices_;
    std::vector<WorldRenderMaterial> materials_;
    std::vector<WorldDrawBatch> draw_batches_;
    assets::WorldBounds bounds_{};
    WorldRenderCoordinateMetadata coordinate_metadata_{};
    WorldRenderStatistics statistics_{};
    WorldRendererResourceIdentity resource_identity_{};
};

} // namespace hlclient::world_render
