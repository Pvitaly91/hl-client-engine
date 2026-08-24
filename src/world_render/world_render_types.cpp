#include <hlclient/world_render/world_render_types.hpp>

namespace hlclient::world_render {

WorldRenderPackage::WorldRenderPackage(
    assets::TexturedWorldAsset textured_world,
    assets::WorldLightmapSet lightmaps,
    std::vector<WorldRenderVertex> vertices,
    std::vector<std::uint32_t> indices,
    std::vector<WorldRenderMaterial> materials,
    std::vector<WorldDrawBatch> draw_batches,
    std::vector<WorldRenderSurfaceRange> surface_ranges,
    const assets::WorldBounds bounds,
    const WorldRenderCoordinateMetadata coordinate_metadata,
    const WorldRenderStatistics statistics,
    const WorldRendererResourceIdentity resource_identity) noexcept
    : textured_world_{std::move(textured_world)},
      lightmaps_{std::move(lightmaps)},
      vertices_{std::move(vertices)},
      indices_{std::move(indices)},
      materials_{std::move(materials)},
      draw_batches_{std::move(draw_batches)},
      surface_ranges_{std::move(surface_ranges)},
      bounds_{bounds},
      coordinate_metadata_{coordinate_metadata},
      statistics_{statistics},
      resource_identity_{resource_identity}
{
}

const assets::TexturedWorldAsset& WorldRenderPackage::textured_world() const noexcept
{
    return textured_world_;
}

const assets::WorldLightmapSet& WorldRenderPackage::lightmaps() const noexcept
{
    return lightmaps_;
}

std::span<const WorldRenderVertex> WorldRenderPackage::vertices() const noexcept
{
    return vertices_;
}

std::span<const std::uint32_t> WorldRenderPackage::indices() const noexcept
{
    return indices_;
}

std::span<const WorldRenderMaterial> WorldRenderPackage::materials() const noexcept
{
    return materials_;
}

std::span<const WorldDrawBatch> WorldRenderPackage::draw_batches() const noexcept
{
    return draw_batches_;
}

std::span<const WorldRenderSurfaceRange> WorldRenderPackage::surface_ranges()
    const noexcept
{
    return surface_ranges_;
}

const assets::WorldBounds& WorldRenderPackage::bounds() const noexcept
{
    return bounds_;
}

const WorldRenderCoordinateMetadata& WorldRenderPackage::coordinate_metadata() const noexcept
{
    return coordinate_metadata_;
}

const WorldRenderStatistics& WorldRenderPackage::statistics() const noexcept
{
    return statistics_;
}

WorldRendererResourceIdentity WorldRenderPackage::resource_identity() const noexcept
{
    return resource_identity_;
}

std::uint64_t WorldRenderPackage::resource_id() const noexcept
{
    return resource_identity_.resource_id;
}

std::uint64_t WorldRenderPackage::resource_revision() const noexcept
{
    return resource_identity_.revision;
}

} // namespace hlclient::world_render
