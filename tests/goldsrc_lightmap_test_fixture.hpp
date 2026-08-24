#pragma once

#include "synthetic_goldsrc_bsp_fixture.hpp"

#include <hlclient/assets/asset_types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <vector>

namespace hlclient::tests::lightmap_fixture {

struct SurfaceDescription {
    float minimum_s{0.0F};
    float minimum_t{0.0F};
    float maximum_s{16.0F};
    float maximum_t{16.0F};
    std::array<std::uint8_t, 4U> styles{0U, 0xFFU, 0xFFU, 0xFFU};
    std::optional<std::uint32_t> lightmap_offset{0U};
};

[[nodiscard]] inline assets::WorldAsset make_world(
    const std::span<const SurfaceDescription> descriptions)
{
    assets::WorldAsset world;
    world.source_profile = assets::WorldGeometrySourceProfile::goldsrc_bsp_v30;
    for (std::size_t surface_index = 0U;
         surface_index < descriptions.size();
         ++surface_index) {
        const auto& description = descriptions[surface_index];
        const auto first_vertex = world.vertices.size();
        const std::array coordinates{
            assets::AssetVector2{description.minimum_s, description.minimum_t},
            assets::AssetVector2{description.maximum_s, description.minimum_t},
            assets::AssetVector2{description.maximum_s, description.maximum_t},
            assets::AssetVector2{description.minimum_s, description.maximum_t},
        };
        for (std::size_t corner = 0U; corner < coordinates.size(); ++corner) {
            world.vertices.push_back(assets::WorldVertex{
                assets::AssetVector3{
                    static_cast<float>(surface_index * 32U) +
                        (corner == 1U || corner == 2U ? 16.0F : 0.0F),
                    corner >= 2U ? 16.0F : 0.0F,
                    0.0F},
                assets::AssetVector3{0.0F, 0.0F, 1.0F},
                coordinates[corner],
            });
        }
        const auto first_index = world.indices.size();
        world.indices.insert(world.indices.end(),
            {static_cast<std::uint32_t>(first_vertex),
                static_cast<std::uint32_t>(first_vertex + 1U),
                static_cast<std::uint32_t>(first_vertex + 2U),
                static_cast<std::uint32_t>(first_vertex),
                static_cast<std::uint32_t>(first_vertex + 2U),
                static_cast<std::uint32_t>(first_vertex + 3U)});
        assets::WorldSurface surface;
        surface.first_index = static_cast<std::uint32_t>(first_index);
        surface.index_count = 6U;
        surface.material_index = 0U;
        surface.source_surface_ordinal =
            static_cast<std::uint32_t>(surface_index);
        surface.lightmap_offset = description.lightmap_offset;
        surface.light_styles = description.styles;
        surface.first_vertex = static_cast<std::uint32_t>(first_vertex);
        surface.vertex_count = 4U;
        world.surfaces.push_back(surface);
    }
    return world;
}

[[nodiscard]] inline assets::WorldAsset make_world(
    const SurfaceDescription& description = {})
{
    return make_world(std::span{&description, 1U});
}

[[nodiscard]] inline std::vector<std::byte> make_bsp_with_lighting(
    const std::span<const std::byte> lighting)
{
    SyntheticBspBuilder builder;
    builder.lump(SyntheticBspLumpId::lighting).assign(
        lighting.begin(), lighting.end());
    return builder.build();
}

[[nodiscard]] inline std::vector<std::byte> sequential_rgb_samples(
    const std::size_t sample_count,
    const std::uint8_t first = 1U)
{
    std::vector<std::byte> bytes;
    bytes.reserve(sample_count * 3U);
    auto value = first;
    for (std::size_t sample = 0U; sample < sample_count; ++sample) {
        bytes.push_back(static_cast<std::byte>(value++));
        bytes.push_back(static_cast<std::byte>(value++));
        bytes.push_back(static_cast<std::byte>(value++));
    }
    return bytes;
}

[[nodiscard]] inline std::array<std::uint8_t, 4U> styles(
    const std::initializer_list<std::uint8_t> active)
{
    std::array<std::uint8_t, 4U> result{0xFFU, 0xFFU, 0xFFU, 0xFFU};
    std::size_t index = 0U;
    for (const auto style : active) {
        if (index == result.size()) {
            break;
        }
        result[index++] = style;
    }
    return result;
}

} // namespace hlclient::tests::lightmap_fixture
