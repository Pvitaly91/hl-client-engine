// Private standalone fixture only. Link history_projection_allocation.cpp in
// that executable; do not add its global operator-new override to aggregates.
#include "../src/renderer/opengl/prepared_world_visibility.hpp"
#include "history_projection_allocation.hpp"
#include "world_render_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>

namespace {

namespace assets = hlclient::assets;
namespace renderer = hlclient::renderer;
namespace spatial = hlclient::world_spatial;
namespace scene_render = hlclient::world_scene_render;
namespace visibility = hlclient::world_visibility;
namespace prepared = hlclient::renderer::opengl;

[[nodiscard]] std::shared_ptr<const scene_render::WorldSceneRenderPackage>
make_allocation_scene()
{
    hlclient::tests::world_render_fixture::FixtureOptions geometry;
    geometry.atlas_page_count = 2U;
    auto world = hlclient::tests::world_render_fixture::make_package(geometry);
    REQUIRE(world);
    auto package = std::make_shared<const hlclient::world_render::WorldRenderPackage>(
        std::move(*world.package));
    const assets::WorldBounds bounds{{-256.0F, -256.0F, -256.0F}, {256.0F, 256.0F, 256.0F}};
    spatial::WorldSpatialNode root;
    root.plane_index = 0U;
    root.bounds = bounds;
    root.children = {{{spatial::WorldSpatialNodeChildKind::leaf, 1U},
        {spatial::WorldSpatialNodeChildKind::leaf, 2U}}};
    std::vector<spatial::WorldSpatialLeaf> leaves(3U);
    for (std::uint32_t i = 0U; i < leaves.size(); ++i) {
        leaves[i].source_leaf_index = i;
        leaves[i].surface_membership.source_leaf_index = i;
        leaves[i].bounds = bounds;
        leaves[i].contents = i == 0U ? -2 : -1;
        leaves[i].solid_or_special = i == 0U;
        leaves[i].pvs_bit_addressable = i != 0U;
        if (i != 0U) {
            leaves[i].pvs_row_index = i - 1U;
            leaves[i].surface_membership.world_surface_indices = {i - 1U};
        }
    }
    spatial::WorldPvsTable pvs{1U, 2U,
        {{std::byte{0x01}}, {std::byte{0x02}}, {std::byte{0x03}}},
        {std::nullopt, 0U, 1U}, 2U};
    spatial::WorldSpatialPackage spatial_package{
        {{{-1.0F, 0.0F, 0.0F}, -24.0F, {}}}, {root}, std::move(leaves), std::move(pvs),
        {0U, 2U, bounds}, {},
        spatial::WorldSpatialCompatibilityProfile::goldsrc_bsp_v30_leaf_one_is_pvs_bit_zero,
        spatial::WorldSpatialEvidenceProfile::canonical_validated_bsp_records};
    auto scene = scene_render::WorldSceneRenderPackageBuilder{}.build(
        std::move(package), std::move(spatial_package));
    REQUIRE(scene);
    return std::make_shared<const scene_render::WorldSceneRenderPackage>(std::move(*scene.package));
}

TEST_CASE("Prepared BSP selector warmed view and PVS transitions allocate nothing",
    "[world][visibility][prepared-pvs-allocation][private-fixture]")
{
    const auto scene = make_allocation_scene();
    prepared::PreparedWorldVisibility selector;
    REQUIRE(selector.prepare(*scene));
    const auto* selected_storage = selector.selected_surfaces().data();
    const auto retained_bytes = selector.statistics().prepared_bytes;
    bool all_selected = true;
    bool storage_unchanged = true;
    std::uint64_t normal_frames{}, boundary_frames{}, disabled_frames{};
    const auto run = [&](std::uint32_t count) noexcept {
        for (std::uint32_t i = 0U; i < count; ++i) {
            // Several consecutive frames at each context exercise cache hits,
            // then cross a leaf, approach its boundary, disable and restore PVS.
            const auto phase = (i / 32U) % 6U;
            renderer::RenderCamera camera;
            camera.position = {phase == 2U ? 40.0F : phase == 3U ? 24.01F : 8.0F, 8.0F, 96.0F};
            const float angle = static_cast<float>(i % 32U) * 0.1F;
            camera.target = {camera.position.x + 12.0F * std::cos(angle),
                camera.position.y + 12.0F * std::sin(angle), 0.0F};
            camera.up = {0.0F, 1.0F, 0.0F};
            camera.vertical_field_of_view_radians = phase == 1U ? 0.4F : 1.57079632679F;
            camera.near_plane = 0.1F;
            camera.far_plane = 512.0F;
            const auto extent = i % 2U == 0U ? renderer::RenderExtent{1280, 720}
                                           : renderer::RenderExtent{640, 480};
            const auto frustum = visibility::WorldViewFrustum::from_camera(camera, extent);
            if (!frustum || !selector.select(camera, *frustum.frustum, i % 3U != 0U, phase != 4U)) {
                all_selected = false;
                continue;
            }
            storage_unchanged = storage_unchanged &&
                selector.selected_surfaces().data() == selected_storage &&
                selector.statistics().prepared_bytes == retained_bytes;
            switch (selector.statistics().fallback) {
            case prepared::PreparedWorldPvsFallback::none: ++normal_frames; break;
            case prepared::PreparedWorldPvsFallback::liquid_or_content_boundary: ++boundary_frames; break;
            case prepared::PreparedWorldPvsFallback::policy_disabled: ++disabled_frames; break;
            default: all_selected = false; break;
            }
        }
    };
    // Warm all code/context paths, including CRT trigonometric calls, before
    // observing the operator-new counter. Catch assertions/logging stay out.
    run(192U);
    REQUIRE(all_selected);
    normal_frames = boundary_frames = disabled_frames = 0U;
    const auto rebuilds_before = selector.statistics().pvs_rebuilds;
    const auto hits_before = selector.statistics().cache_hits;
    history_allocation::start();
    run(4096U);
    const auto observed = history_allocation::stop();
    const auto rebuilds = selector.statistics().pvs_rebuilds - rebuilds_before;
    const auto hits = selector.statistics().cache_hits - hits_before;
    std::printf("prepared_pvs_allocations frames=4096 calls=%llu bytes=%llu pvs_rebuilds=%llu cache_hits=%llu normal=%llu boundary=%llu policy_off=%llu retained_bytes=%zu\n",
        static_cast<unsigned long long>(observed.calls), static_cast<unsigned long long>(observed.bytes),
        static_cast<unsigned long long>(rebuilds), static_cast<unsigned long long>(hits),
        static_cast<unsigned long long>(normal_frames), static_cast<unsigned long long>(boundary_frames),
        static_cast<unsigned long long>(disabled_frames), retained_bytes);
    REQUIRE(all_selected);
    REQUIRE(storage_unchanged);
    REQUIRE(observed.calls == 0U);
    REQUIRE(observed.bytes == 0U);
    REQUIRE(rebuilds > 0U);
    REQUIRE(hits > 0U);
    REQUIRE(normal_frames > 0U);
    REQUIRE(boundary_frames > 0U);
    REQUIRE(disabled_frames > 0U);
    REQUIRE(normal_frames + boundary_frames + disabled_frames == 4096U);
    REQUIRE(selector.statistics().preparation_count == 1U);
}

} // namespace
