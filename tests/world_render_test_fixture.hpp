#pragma once

#include <hlclient/assets/world_lightmap_types.hpp>
#include <hlclient/assets/world_texture_types.hpp>
#include <hlclient/world_render/world_render_package_builder.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace hlclient::tests::world_render_fixture {

struct FixtureOptions {
    bool masked{false};
    bool unlit{false};
    std::size_t atlas_page_index{0U};
    std::size_t atlas_page_count{1U};
};

[[nodiscard]] inline assets::WorldTextureAsset make_texture(const bool masked)
{
    assets::WorldTextureAsset texture;
    texture.name = masked ? "{MASKED" : "STONE";
    texture.width = 16U;
    texture.height = 16U;
    texture.alpha_mode = masked
        ? assets::WorldTextureAlphaMode::masked_index_255
        : assets::WorldTextureAlphaMode::opaque;
    texture.source_bsp_texture_index = 0U;
    for (std::size_t level = 0U; level < texture.mip_levels.size(); ++level) {
        auto& mip = texture.mip_levels[level];
        mip.width = texture.width >> level;
        mip.height = texture.height >> level;
        mip.rgba_pixels.assign(
            static_cast<std::size_t>(mip.width) * mip.height * 4U,
            std::byte{0x40});
        for (std::size_t offset = 3U; offset < mip.rgba_pixels.size(); offset += 4U) {
            mip.rgba_pixels[offset] = std::byte{0xFF};
        }
    }
    return texture;
}

[[nodiscard]] inline assets::WorldAsset make_world(
    const std::size_t surface_count = 1U)
{
    assets::WorldAsset world;
    world.identity.source_name = "maps/synthetic.bsp";
    world.bounds = {{0.0F, 0.0F, 0.0F}, {16.0F, 16.0F, 0.0F}};
    world.vertices = {
        {{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F}},
        {{16.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {16.0F, 0.0F}},
        {{16.0F, 16.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {16.0F, 16.0F}},
        {{0.0F, 16.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 16.0F}},
    };
    world.indices = {0U, 1U, 2U, 0U, 2U, 3U};

    assets::WorldMaterialReference material;
    material.texture_name = "STONE";
    material.width = 16U;
    material.height = 16U;
    material.texture_storage = assets::WorldTextureStorage::embedded;
    material.source_texture_index = 0U;
    world.materials.push_back(std::move(material));

    assets::WorldSurface surface;
    surface.first_index = 0U;
    surface.index_count = 6U;
    surface.material_index = 0U;
    surface.bounds = world.bounds;
    surface.source_surface_ordinal = 0U;
    surface.lightmap_offset = 0U;
    surface.light_styles = {0U, 0xFFU, 0xFFU, 0xFFU};
    surface.first_vertex = 0U;
    surface.vertex_count = 4U;
    world.surfaces.push_back(surface);
    world.statistics.emitted_vertex_count = 4U;
    world.statistics.emitted_triangle_count = 2U;
    world.statistics.emitted_surface_count = 1U;
    world.statistics.material_count = 1U;
    const auto template_vertices = world.vertices;
    const auto template_surface = world.surfaces[0U];
    for (std::size_t surface_index = 1U; surface_index < surface_count;
         ++surface_index) {
        const auto first_vertex = world.vertices.size();
        const auto first_index = world.indices.size();
        const auto shift = static_cast<float>(surface_index) * 32.0F;
        for (auto vertex : template_vertices) {
            vertex.position.x += shift;
            world.vertices.push_back(vertex);
        }
        for (const auto index : std::array<std::uint32_t, 6U>{
                 0U, 1U, 2U, 0U, 2U, 3U}) {
            world.indices.push_back(
                static_cast<std::uint32_t>(first_vertex) + index);
        }
        auto additional_surface = template_surface;
        additional_surface.first_vertex = static_cast<std::uint32_t>(first_vertex);
        additional_surface.first_index = static_cast<std::uint32_t>(first_index);
        additional_surface.source_surface_ordinal =
            static_cast<std::uint32_t>(surface_index);
        additional_surface.bounds.minimum.x += shift;
        additional_surface.bounds.maximum.x += shift;
        world.surfaces.push_back(additional_surface);
        world.bounds.maximum.x = additional_surface.bounds.maximum.x;
    }
    world.statistics.emitted_vertex_count = world.vertices.size();
    world.statistics.emitted_triangle_count = world.indices.size() / 3U;
    world.statistics.emitted_surface_count = world.surfaces.size();
    return world;
}

[[nodiscard]] inline assets::WorldTextureSet make_texture_set(const bool masked)
{
    std::vector<assets::WorldTextureAsset> textures;
    textures.push_back(make_texture(masked));
    assets::WorldMaterialTextureBinding binding;
    binding.material_index = 0U;
    binding.status = assets::WorldMaterialTextureBindingStatus::resolved_embedded;
    binding.texture_asset_index = 0U;
    binding.source_bsp_texture_index = 0U;
    auto created = assets::WorldTextureSet::create(
        std::move(textures), {binding}, {}, 1U);
    if (!created) {
        throw std::runtime_error{"Unable to create synthetic texture set"};
    }
    return std::move(*created.texture_set);
}

[[nodiscard]] inline assets::WorldLightmapAtlasPage make_page(
    const std::uint8_t base_value)
{
    assets::WorldLightmapAtlasPage page;
    page.width = 4U;
    page.height = 4U;
    for (std::size_t slot = 0U; slot < page.style_slot_images.size(); ++slot) {
        auto& image = page.style_slot_images[slot];
        image.width = page.width;
        image.height = page.height;
        image.rgba_pixels.resize(4U * 4U * 4U);
        for (std::size_t pixel = 0U; pixel < 16U; ++pixel) {
            const auto offset = pixel * 4U;
            image.rgba_pixels[offset] = slot == 0U
                ? std::byte{base_value}
                : std::byte{0U};
            image.rgba_pixels[offset + 1U] =
                slot == 0U ? std::byte{0x20} : std::byte{0U};
            image.rgba_pixels[offset + 2U] =
                slot == 0U ? std::byte{0x30} : std::byte{0U};
            image.rgba_pixels[offset + 3U] = std::byte{0xFF};
        }
    }
    return page;
}

[[nodiscard]] inline assets::WorldLightmapSet make_lightmap_set(
    const FixtureOptions& options)
{
    std::vector<assets::WorldLightmapAtlasPage> pages;
    for (std::size_t page = 0U; page < options.atlas_page_count; ++page) {
        pages.push_back(make_page(static_cast<std::uint8_t>(0x10U + page)));
    }

    assets::WorldSurfaceLightmapBinding binding;
    binding.surface_index = 0U;
    if (options.unlit) {
        binding.status = assets::WorldSurfaceLightmapBindingStatus::unlit_no_lightmap;
        binding.sample_width = 2U;
        binding.sample_height = 2U;
    } else {
        binding.status = assets::WorldSurfaceLightmapBindingStatus::resolved;
        binding.atlas_page_index = options.atlas_page_index;
        binding.inner_rectangle = {1U, 1U, 2U, 2U};
        binding.padded_rectangle = {0U, 0U, 4U, 4U};
        binding.sample_width = 2U;
        binding.sample_height = 2U;
        binding.source_styles.style_count = 1U;
        binding.source_styles.style_ids = {0U, 0xFFU, 0xFFU, 0xFFU};
    }
    if (options.unlit) {
        pages.clear();
    }
    std::vector<assets::WorldSurfaceLightmapBinding> bindings{binding};
    if (!options.unlit) {
        for (std::size_t page_index = 0U;
             page_index < options.atlas_page_count; ++page_index) {
            if (page_index == options.atlas_page_index) {
                continue;
            }
            auto additional = binding;
            additional.surface_index = bindings.size();
            additional.atlas_page_index = page_index;
            bindings.push_back(additional);
        }
    }
    const auto surface_count = bindings.size();
    auto created = assets::WorldLightmapSet::create(
        std::move(pages), std::move(bindings), surface_count);
    if (!created) {
        throw std::runtime_error{"Unable to create synthetic lightmap set"};
    }
    return std::move(*created.lightmap_set);
}

[[nodiscard]] inline assets::TexturedWorldAsset make_textured_world(
    const bool masked = false,
    const std::size_t surface_count = 1U)
{
    return {make_world(surface_count), make_texture_set(masked)};
}

[[nodiscard]] inline world_render::WorldRenderPackageBuildResult make_package(
    const FixtureOptions& options = {},
    const world_render::WorldRenderPackageLimits& limits = {})
{
    world_render::WorldRenderPackageBuilder builder;
    const auto surface_count = options.unlit ? 1U : options.atlas_page_count;
    return builder.build(
        make_textured_world(options.masked, surface_count),
        make_lightmap_set(options),
        limits);
}

} // namespace hlclient::tests::world_render_fixture
