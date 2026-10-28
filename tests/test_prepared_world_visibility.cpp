#include "../src/renderer/opengl/prepared_world_visibility.hpp"
#include "world_render_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>

namespace {

namespace assets = hlclient::assets;
namespace renderer = hlclient::renderer;
namespace prepared = hlclient::renderer::opengl;
namespace spatial = hlclient::world_spatial;
namespace scene_render = hlclient::world_scene_render;
namespace visibility = hlclient::world_visibility;
namespace fixture = hlclient::tests::world_render_fixture;

struct Options {
    std::size_t surface_count{2U};
    bool all_visible{};
    bool absent_pvs{};
    bool malformed_pvs{};
    bool unavailable_row{};
    bool solid_camera{};
    bool liquid_camera{};
    bool liquid_boundary{};
    bool misleading_bounds{};
    bool shared_surface{};
    bool cycle{};
    bool invalid_child{};
    bool masked{};
    bool sky{};
    bool almost_planar{};
    bool camera_outside_pvs_domain{};
};

[[nodiscard]] spatial::WorldSpatialPackage make_spatial(const Options options)
{
    const std::uint32_t visible_leaf_count = options.camera_outside_pvs_domain ? 1U : 4U;
    const assets::WorldBounds bounds{{-256.0F, -256.0F, -256.0F}, {256.0F, 256.0F, 256.0F}};
    std::vector<spatial::WorldSpatialPlane> planes{
        {{-1.0F, 0.0F, 0.0F}, -24.0F, {}},
        {{0.0F, 1.0F, 0.0F}, 0.0F, {}},
    };
    std::vector<spatial::WorldSpatialNode> nodes(3U);
    nodes[0].plane_index = 0U;
    nodes[0].children = {{{spatial::WorldSpatialNodeChildKind::node, 1U},
        {spatial::WorldSpatialNodeChildKind::node, 2U}}};
    nodes[1].plane_index = 1U;
    nodes[1].children = {{{spatial::WorldSpatialNodeChildKind::leaf, 1U},
        {spatial::WorldSpatialNodeChildKind::leaf, 2U}}};
    nodes[2].plane_index = 1U;
    nodes[2].children = {{{spatial::WorldSpatialNodeChildKind::leaf, 3U},
        {spatial::WorldSpatialNodeChildKind::leaf, 4U}}};
    for (auto& node : nodes) node.bounds = bounds;
    if (options.cycle) nodes[1].children[0] = {spatial::WorldSpatialNodeChildKind::node, 0U};
    if (options.invalid_child) nodes[1].children[0].index = 99U;
    std::vector<spatial::WorldSpatialLeaf> leaves(5U);
    for (std::uint32_t i = 0U; i < leaves.size(); ++i) {
        leaves[i].source_leaf_index = i;
        leaves[i].surface_membership.source_leaf_index = i;
        leaves[i].bounds = bounds;
        leaves[i].contents = i == 0U ? -2 : -1;
        leaves[i].solid_or_special = i == 0U;
        leaves[i].pvs_bit_addressable = i != 0U;
        if (i != 0U) leaves[i].pvs_row_index = i - 1U;
    }
    leaves[1].surface_membership.world_surface_indices = {0U};
    leaves[3].surface_membership.world_surface_indices = {1U};
    if (options.shared_surface) leaves[3].surface_membership.world_surface_indices.push_back(0U);
    if (options.solid_camera) { leaves[1].solid_or_special = true; leaves[1].contents = -2; }
    if (options.liquid_camera) leaves[1].contents = -3;
    if (options.liquid_boundary) leaves[2].contents = -3;
    if (options.misleading_bounds) {
        leaves[1].bounds = {{-1000.0F, -1000.0F, -1000.0F}, {-900.0F, -900.0F, -900.0F}};
        nodes[1].bounds = leaves[1].bounds;
    }
    if (options.unavailable_row) leaves[1].pvs_row_index.reset();
    spatial::WorldPvsTable pvs;
    if (!options.absent_pvs) {
        std::vector<std::vector<std::byte>> rows{
            {options.all_visible ? std::byte{0x0f} : std::byte{0x01}},
            {std::byte{0x02}}, {std::byte{0x04}}, {std::byte{0x08}}, {std::byte{0x0f}},
        };
        if (options.malformed_pvs) rows[0].clear();
        if (options.camera_outside_pvs_domain) rows[2] = {std::byte{0x00}};
        std::vector<std::optional<std::uint32_t>> leaf_rows{{}, 0U, 1U, 2U, 3U};
        if (options.unavailable_row) leaf_rows[1].reset();
        pvs = spatial::WorldPvsTable{1U, visible_leaf_count, std::move(rows), std::move(leaf_rows), 4U};
    }
    return {std::move(planes), std::move(nodes), std::move(leaves), std::move(pvs),
        {0U, visible_leaf_count, bounds}, {},
        spatial::WorldSpatialCompatibilityProfile::goldsrc_bsp_v30_leaf_one_is_pvs_bit_zero,
        spatial::WorldSpatialEvidenceProfile::canonical_validated_bsp_records};
}

[[nodiscard]] std::shared_ptr<const scene_render::WorldSceneRenderPackage>
make_scene(const Options options = {})
{
    fixture::FixtureOptions geometry;
    geometry.atlas_page_count = options.surface_count;
    geometry.masked = options.masked;
    auto built_world = [&] {
        if (!options.sky && !options.almost_planar) return fixture::make_package(geometry);
        auto world = fixture::make_world(options.surface_count);
        if (options.almost_planar) {
            for (auto& vertex : world.vertices) vertex.position.z = 10000.0F;
            world.vertices[3].position.z += 0.01F;
            for (auto& surface : world.surfaces) surface.bounds.minimum.z = surface.bounds.maximum.z = 10000.0F;
            world.surfaces[0].bounds.maximum.z = world.vertices[3].position.z;
            world.bounds.minimum.z = 10000.0F;
            world.bounds.maximum.z = world.vertices[3].position.z;
        }
        if (options.sky) {
            world.materials[0].texture_name = "sky";
            for (auto& surface : world.surfaces) surface.special_surface = true;
        }
        auto texture = fixture::make_texture(false);
        if (options.sky) texture.name = "sky";
        assets::WorldMaterialTextureBinding binding;
        binding.material_index = 0U;
        binding.status = assets::WorldMaterialTextureBindingStatus::resolved_embedded;
        binding.texture_asset_index = 0U;
        binding.source_bsp_texture_index = 0U;
        auto textures = assets::WorldTextureSet::create({std::move(texture)}, {binding}, {}, 1U);
        REQUIRE(textures);
        return hlclient::world_render::WorldRenderPackageBuilder{}.build(
            {std::move(world), std::move(*textures.texture_set)}, fixture::make_lightmap_set(geometry));
    }();
    REQUIRE(built_world);
    auto world = std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*built_world.package));
    auto built_scene = scene_render::WorldSceneRenderPackageBuilder{}.build(std::move(world), make_spatial(options));
    REQUIRE(built_scene);
    return std::make_shared<const scene_render::WorldSceneRenderPackage>(std::move(*built_scene.package));
}

[[nodiscard]] renderer::RenderCamera camera()
{
    renderer::RenderCamera result;
    result.position = {8.0F, 8.0F, 96.0F};
    result.target = {8.0F, 8.0F, 0.0F};
    result.up = {0.0F, 1.0F, 0.0F};
    result.vertical_field_of_view_radians = 1.57079632679F;
    result.near_plane = 0.1F;
    result.far_plane = 512.0F;
    return result;
}

void select(prepared::PreparedWorldVisibility& selector,
    const renderer::RenderCamera& view, bool cull_back = true,
    bool pvs = true, renderer::RenderExtent extent = {256, 256},
    bool cull_retained_sky = false)
{
    const auto frustum = visibility::WorldViewFrustum::from_camera(view, extent);
    REQUIRE(frustum);
    REQUIRE(selector.select(view, *frustum.frustum, cull_back, pvs,cull_retained_sky));
}

TEST_CASE("Prepared BSP visibility caches PVS but recomputes camera selection", "[world][visibility][prepared-pvs]")
{
    const auto scene = make_scene();
    prepared::PreparedWorldVisibility selector;
    REQUIRE(selector.prepare(*scene));
    auto view = camera();
    select(selector, view);
    REQUIRE(selector.selected_surfaces()[0] == 1U);
    REQUIRE(selector.selected_surfaces()[1] == 0U);
    REQUIRE(selector.pvs_surfaces()[0] == 1U);
    REQUIRE(selector.pvs_surfaces()[1] == 0U);
    REQUIRE(selector.statistics().pvs_rebuilds == 1U);
    REQUIRE(selector.statistics().pvs_rejected_nodes == 1U);
    REQUIRE(selector.statistics().camera_leaf == 1U);
    const auto* storage = selector.selected_surfaces().data();
    const auto* pvs_storage = selector.pvs_surfaces().data();
    const auto bytes = selector.statistics().prepared_bytes;
    for (int i = 0; i < 64; ++i) {
        view.vertical_field_of_view_radians = i % 2 == 0 ? 0.2F : 1.57079632679F;
        select(selector, view, true, true, i % 2 == 0 ? renderer::RenderExtent{512, 256} : renderer::RenderExtent{256, 256});
        REQUIRE(selector.selected_surfaces().data() == storage);
        REQUIRE(selector.pvs_surfaces().data() == pvs_storage);
        REQUIRE(selector.pvs_surfaces()[0] == 1U);
        REQUIRE(selector.pvs_surfaces()[1] == 0U);
        REQUIRE(selector.statistics().prepared_bytes == bytes);
    }
    REQUIRE(selector.statistics().pvs_rebuilds == 1U);
    REQUIRE(selector.statistics().cache_hits == 64U);
    view.position.x = 40.0F; view.target.x = 40.0F;
    select(selector, view);
    REQUIRE(selector.statistics().camera_leaf == 3U);
    REQUIRE(selector.statistics().pvs_rebuilds == 2U);
    REQUIRE(selector.selected_surfaces()[0] == 0U);
    REQUIRE(selector.selected_surfaces()[1] == 1U);
    REQUIRE(selector.pvs_surfaces()[0] == 0U);
    REQUIRE(selector.pvs_surfaces()[1] == 1U);
}

TEST_CASE("Prepared BSP visibility retains leaf-crossing geometry and unmapped surfaces", "[world][visibility][prepared-pvs]")
{
    Options options;
    options.surface_count = 3U;
    options.misleading_bounds = true;
    options.shared_surface = true;
    options.masked = true;
    const auto scene = make_scene(options);
    prepared::PreparedWorldVisibility selector;
    REQUIRE(selector.prepare(*scene));
    select(selector, camera());
    REQUIRE(selector.selected_surfaces()[0] == 1U);
    REQUIRE(selector.selected_surfaces()[1] == 0U);
    REQUIRE(selector.selected_surfaces()[2] == 1U);
    REQUIRE(selector.statistics().selected_surfaces == 2U);
}

TEST_CASE("Prepared BSP visibility updates frustum and backfaces within unchanged leaf", "[world][visibility][prepared-pvs]")
{
    Options options; options.all_visible = true;
    const auto scene = make_scene(options);
    prepared::PreparedWorldVisibility selector;
    REQUIRE(selector.prepare(*scene));
    auto view = camera();
    select(selector, view);
    REQUIRE(selector.statistics().selected_surfaces == 2U);
    view.vertical_field_of_view_radians = 0.2F;
    select(selector, view);
    REQUIRE(selector.selected_surfaces()[0] == 1U);
    REQUIRE(selector.selected_surfaces()[1] == 0U);
    REQUIRE(selector.pvs_surfaces()[0] == 1U);
    REQUIRE(selector.pvs_surfaces()[1] == 1U);
    REQUIRE(selector.statistics().pvs_rebuilds == 1U);
    view = camera(); view.position.z = -96.0F;
    select(selector, view);
    REQUIRE(selector.statistics().selected_surfaces == 0U);
    REQUIRE(selector.statistics().backface_rejected_surfaces == 2U);
    select(selector, view, false);
    REQUIRE(selector.statistics().selected_surfaces == 2U);
    REQUIRE(selector.statistics().pvs_rebuilds == 1U);
}

TEST_CASE("Prepared BSP visibility falls back conservatively for visibility uncertainty", "[world][visibility][prepared-pvs]")
{
    for (int state = 0; state < 7; ++state) {
        Options options;
        options.absent_pvs = state == 0;
        options.malformed_pvs = state == 1;
        options.unavailable_row = state == 2;
        options.solid_camera = state == 3;
        options.liquid_camera = state == 4;
        options.liquid_boundary = state == 5;
        const auto scene = make_scene(options);
        prepared::PreparedWorldVisibility selector;
        REQUIRE(selector.prepare(*scene));
        auto view = camera();
        if (state >= 5) { view.position.y = 0.01F; view.target.y = 0.01F; }
        select(selector, view);
        REQUIRE(selector.statistics().fallback != prepared::PreparedWorldPvsFallback::none);
        REQUIRE(selector.statistics().selected_surfaces == 2U);
        REQUIRE(selector.pvs_surfaces()[0] == 1U);
        REQUIRE(selector.pvs_surfaces()[1] == 1U);
    }
}

TEST_CASE("Prepared BSP visibility preserves sky and near-plane cross-leaf depth contributors", "[world][visibility][prepared-pvs]")
{
    SECTION("Finite sky outside the eye PVS remains submitted") {
        Options options; options.sky = true;
        const auto scene = make_scene(options);
        prepared::PreparedWorldVisibility selector;
        REQUIRE(selector.prepare(*scene));
        auto view = camera();
        view.vertical_field_of_view_radians = 0.2F;
        select(selector, view);
        REQUIRE(selector.statistics().fallback == prepared::PreparedWorldPvsFallback::none);
        REQUIRE(selector.selected_surfaces()[0] == 1U);
        REQUIRE(selector.selected_surfaces()[1] == 1U);
        REQUIRE(selector.pvs_surfaces()[0] == 1U);
        REQUIRE(selector.pvs_surfaces()[1] == 1U);
    }
    SECTION("Crossing another EMPTY leaf with the near volume invalidates the eye-only PVS") {
        const auto scene = make_scene();
        prepared::PreparedWorldVisibility selector;
        REQUIRE(selector.prepare(*scene));
        auto view = camera();
        select(selector, view);
        REQUIRE(selector.statistics().selected_surfaces == 1U);
        view.position.y = 0.01F; view.target.y = 0.01F;
        select(selector, view);
        REQUIRE(selector.statistics().camera_leaf == 1U);
        REQUIRE(selector.statistics().fallback == prepared::PreparedWorldPvsFallback::liquid_or_content_boundary);
        REQUIRE(selector.statistics().selected_surfaces == 2U);
        REQUIRE(selector.statistics().pvs_rebuilds == 2U);
        select(selector, camera());
        REQUIRE(selector.statistics().selected_surfaces == 1U);
        REQUIRE(selector.statistics().pvs_rebuilds == 3U);
    }
}

TEST_CASE("Retained sky frustum control preserves PVS independence and camera reevaluation",
    "[world][visibility][prepared-pvs][sky-frustum-selection]")
{
    Options options;options.sky=true;
    const auto scene=make_scene(options);
    prepared::PreparedWorldVisibility selector;
    REQUIRE(selector.prepare(*scene));
    auto view=camera();
    SECTION("Only off-frustum sky changes when C and E share a PVS context") {
        // Surface 1 belongs to leaf 3, absent from the eye's leaf-1 PVS.
        // Both finite sky surfaces are in the wide camera frustum.
        select(selector,view,true,true,{256,256},true);
        REQUIRE(selector.statistics().fallback==prepared::PreparedWorldPvsFallback::none);
        REQUIRE(selector.selected_surfaces()[0]==1U);
        REQUIRE(selector.selected_surfaces()[1]==1U);
        REQUIRE(selector.pvs_surfaces()[0]==1U);
        REQUIRE(selector.pvs_surfaces()[1]==1U);
        REQUIRE(selector.statistics().tested_bounds==2U);
        REQUIRE(selector.statistics().considered_surfaces==2U);
        const auto* storage=selector.selected_surfaces().data();
        const auto bytes=selector.statistics().prepared_bytes;
        view.vertical_field_of_view_radians=0.2F;
        select(selector,view,true,true,{256,256},true);
        REQUIRE(selector.selected_surfaces()[0]==1U);
        REQUIRE(selector.selected_surfaces()[1]==0U);
        REQUIRE(selector.statistics().selected_surfaces==1U);
        REQUIRE(selector.statistics().considered_surfaces==2U);
        REQUIRE(selector.statistics().tested_bounds==2U);
        // Switching back to unchanged C must restore the same off-screen sky.
        select(selector,view);
        REQUIRE(selector.selected_surfaces()[0]==1U);
        REQUIRE(selector.selected_surfaces()[1]==1U);
        REQUIRE(selector.statistics().tested_bounds==0U);
        select(selector,view,true,true,{256,256},true);
        REQUIRE(selector.selected_surfaces()[1]==0U);
        // Rotate in the SAME leaf toward the PVS-independent second sky.
        view.target.x=40.0F;
        select(selector,view,true,true,{256,256},true);
        REQUIRE(selector.selected_surfaces()[0]==0U);
        REQUIRE(selector.selected_surfaces()[1]==1U);
        REQUIRE(selector.pvs_surfaces()[0]==1U);
        REQUIRE(selector.pvs_surfaces()[1]==1U);
        REQUIRE(selector.statistics().camera_leaf==1U);
        REQUIRE(selector.statistics().pvs_rebuilds==1U);
        REQUIRE(selector.statistics().cache_hits==4U);
        REQUIRE(selector.selected_surfaces().data()==storage);
        REQUIRE(selector.statistics().prepared_bytes==bytes);
    }
    SECTION("Conservative near-plane uncertainty is retained") {
        // Geometry is z=0, 96 units from this eye. Preserve the existing
        // one-unit-plus-roundoff tolerance even just behind the near plane.
        view.near_plane=96.5F;
        select(selector,view,true,true,{256,256},true);
        REQUIRE(selector.selected_surfaces()[0]==1U);
        REQUIRE(selector.selected_surfaces()[1]==1U);
        view.near_plane=100.0F;
        select(selector,view,true,true,{256,256},true);
        REQUIRE(selector.selected_surfaces()[0]==0U);
        REQUIRE(selector.selected_surfaces()[1]==0U);
        REQUIRE(selector.pvs_surfaces()[0]==1U);
        REQUIRE(selector.pvs_surfaces()[1]==1U);
        select(selector,view);
        REQUIRE(selector.selected_surfaces()[0]==1U);
        REQUIRE(selector.selected_surfaces()[1]==1U);
    }
    SECTION("Near-wall fallback and ordinary selection remain unchanged") {
        const auto ordinary=make_scene();
        REQUIRE(selector.prepare(*ordinary));
        view.position.y=0.01F;view.target.y=0.01F;
        select(selector,view);
        REQUIRE(selector.statistics().fallback==prepared::PreparedWorldPvsFallback::liquid_or_content_boundary);
        REQUIRE(selector.selected_surfaces()[0]==1U);
        REQUIRE(selector.selected_surfaces()[1]==1U);
        const auto tested=selector.statistics().tested_bounds;
        select(selector,view,true,true,{256,256},true);
        REQUIRE(selector.statistics().fallback==prepared::PreparedWorldPvsFallback::liquid_or_content_boundary);
        REQUIRE(selector.selected_surfaces()[0]==1U);
        REQUIRE(selector.selected_surfaces()[1]==1U);
        REQUIRE(selector.statistics().tested_bounds==tested);
        REQUIRE(selector.statistics().pvs_rebuilds==1U);
    }
}

TEST_CASE("Prepared BSP visibility invalidates policy and immutable resource replacement", "[world][visibility][prepared-pvs]")
{
    const auto first = make_scene();
    prepared::PreparedWorldVisibility selector;
    REQUIRE(selector.prepare(*first));
    select(selector, camera());
    REQUIRE(selector.statistics().selected_surfaces == 1U);
    select(selector, camera(), true, false);
    REQUIRE(selector.statistics().selected_surfaces == 2U);
    REQUIRE(selector.statistics().fallback == prepared::PreparedWorldPvsFallback::policy_disabled);
    select(selector, camera());
    REQUIRE(selector.statistics().selected_surfaces == 1U);
    REQUIRE(selector.statistics().pvs_rebuilds == 3U);
    Options options; options.surface_count = 3U;
    const auto replacement = make_scene(options);
    REQUIRE(selector.prepare(*replacement));
    REQUIRE(selector.statistics().preparation_count == 2U);
    REQUIRE(selector.statistics().pvs_rebuilds == 0U);
    select(selector, camera());
    REQUIRE(selector.selected_surfaces().size() == 3U);
    REQUIRE(selector.statistics().selected_surfaces == 2U);
    selector.reset();
    REQUIRE_FALSE(selector.ready());
    REQUIRE(selector.selected_surfaces().empty());
    REQUIRE(selector.pvs_surfaces().empty());
}

TEST_CASE("Prepared BSP visibility rejects cyclic and invalid spatial graphs before traversal", "[world][visibility][prepared-pvs]")
{
    for (int state = 0; state < 2; ++state) {
        Options options; options.cycle = state == 0; options.invalid_child = state == 1;
        const auto scene = make_scene(options);
        prepared::PreparedWorldVisibility selector;
        REQUIRE_FALSE(selector.prepare(*scene));
        REQUIRE(selector.status() == prepared::PreparedWorldVisibilityStatus::invalid_spatial);
        REQUIRE(selector.selected_surfaces().empty());
    }
}

TEST_CASE("Prepared BSP visibility leaves almost-planar mixed-facing triangles to GL", "[world][visibility][prepared-pvs]")
{
    Options options; options.almost_planar = true;
    const auto scene = make_scene(options);
    prepared::PreparedWorldVisibility selector;
    REQUIRE(selector.prepare(*scene));
    auto view = camera();
    view.position = {10000.0F, 8.0F, 9998.0F};
    view.target = {8.0F, 8.0F, 10000.0F};
    view.up = {0.0F, 0.0F, 1.0F};
    view.near_plane = 1.0F;
    view.far_plane = 50000.0F;
    select(selector, view, true, false);
    // Triangle0 is back-facing, triangle1 is front-facing from this eye.
    // A near-coplanar tolerance must not classify the entire face by triangle0.
    REQUIRE(selector.selected_surfaces()[0] == 1U);
}

TEST_CASE("Prepared BSP visibility distrusts a camera row outside the PVS bit domain", "[world][visibility][prepared-pvs]")
{
    Options options; options.camera_outside_pvs_domain = true;
    const auto scene = make_scene(options);
    prepared::PreparedWorldVisibility selector;
    REQUIRE(selector.prepare(*scene));
    auto view = camera();
    view.position.x = view.target.x = 40.0F;
    select(selector, view);
    REQUIRE(selector.statistics().camera_leaf == 3U);
    REQUIRE(selector.statistics().fallback == prepared::PreparedWorldPvsFallback::row_unavailable);
    // The malformed DTO provides an empty row and claims leaf3 addressable,
    // but only leaf1 is in the actual bit domain. Do not trust that eye row.
    REQUIRE(selector.selected_surfaces()[0] == 1U);
    REQUIRE(selector.selected_surfaces()[1] == 1U);
}

} // namespace
