#include "world_render_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"

#include "gauss_world_visibility_control.hpp"
#include <hlclient/renderer/render_camera_math.hpp>

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
namespace control=hlclient::tests;
namespace renderer=hlclient::renderer;
namespace spatial=hlclient::world_spatial;
namespace scene_render=hlclient::world_scene_render;
namespace visibility=hlclient::world_visibility;
namespace fixture=hlclient::tests::world_render_fixture;
namespace gpu_fixture=hlclient::tests::entity_opengl_fixture;

std::shared_ptr<const scene_render::WorldSceneRenderPackage> make_visibility_scene(
    const bool solid_camera=false,const bool with_brush=false)
{
    fixture::FixtureOptions options;options.atlas_page_count=2;
    auto built=fixture::make_package(options);
    if(!built || !built.package)throw std::runtime_error{"Synthetic world unavailable"};
    auto world=std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*built.package));
    const hlclient::assets::WorldBounds bounds{{0,0,-1},{48,16,24}};
    spatial::WorldSpatialPlane plane{{1,0,0},24,0};
    spatial::WorldSpatialNode node;node.plane_index=0;node.bounds=bounds;
    node.children={spatial::WorldSpatialNodeChild{spatial::WorldSpatialNodeChildKind::leaf,2},
        spatial::WorldSpatialNodeChild{spatial::WorldSpatialNodeChildKind::leaf,solid_camera?0U:1U}};
    spatial::WorldSpatialLeaf zero;zero.source_leaf_index=0;zero.bounds=bounds;
    zero.surface_membership.source_leaf_index=0;zero.solid_or_special=true;
    spatial::WorldSpatialLeaf one;one.source_leaf_index=1;one.bounds={{0,0,-1},{24,16,24}};
    one.pvs_row_index=0;one.pvs_bit_addressable=true;
    one.surface_membership.source_leaf_index=1;one.surface_membership.source_marksurface_count=1;
    one.surface_membership.world_surface_indices={0};
    auto two=one;two.source_leaf_index=2;two.bounds={{24,0,-1},{48,16,24}};two.pvs_row_index=1;
    two.surface_membership.source_leaf_index=2;two.surface_membership.world_surface_indices={1};
    spatial::WorldSpatialPackage space{{plane},{node},{zero,one,two},
        spatial::WorldPvsTable{1U,2U,{{std::byte{1}},{std::byte{2}}},{std::nullopt,0U,1U},0U},
        spatial::WorldSpatialModelMetadata{0U,2U,bounds},
        spatial::WorldSpatialStatistics{1U,1U,3U,2U,2U,2U,2U},
        spatial::WorldSpatialCompatibilityProfile::goldsrc_bsp_v30_leaf_one_is_pvs_bit_zero,
        spatial::WorldSpatialEvidenceProfile::canonical_validated_bsp_records};
    std::vector<scene_render::BrushSubmodelRenderModel> models;
    std::vector<scene_render::BrushSubmodelRenderInstance> brushes;
    if(with_brush) {
        models.emplace_back(1U,world->bounds(),std::vector<std::uint32_t>{0U,1U});
        scene_render::BrushSubmodelRenderInstance brush;
        brush.source_instance_index=7;brush.source_entity_ordinal=3;brush.source_model_index=1;
        brush.transformed_bounds=world->bounds();brush.touched_leaf_indices={1,2};
        brush.support_status=scene_render::BrushSubmodelRenderSupportStatus::supported_static_opaque;
        brushes.push_back(std::move(brush));
    }
    scene_render::BrushSubmodelRenderLibrary library{with_brush?world:decltype(world){},std::move(models)};
    auto package=scene_render::WorldSceneRenderPackageBuilder{}.build(
        world,std::move(space),std::move(library),std::move(brushes));
    if(!package || !package.package)throw std::runtime_error{
        "Synthetic scene unavailable: "+(package.error?package.error->context:std::string{"unknown failure"})};
    return std::make_shared<const scene_render::WorldSceneRenderPackage>(std::move(*package.package));
}

renderer::RenderScene make_render_scene(
    const std::shared_ptr<const scene_render::WorldSceneRenderPackage>& package)
{
    renderer::RenderScene scene;
    scene.static_world=renderer::RenderStaticWorld{package->world_package()};
    scene.static_world->scene_package=package;
    scene.camera.position={8,8,16};scene.camera.target={8,8,0};scene.camera.up={0,1,0};
    return scene;
}

TEST_CASE("Runtime world visibility uses final camera and invalidates exact immutable inputs",
    "[gauss-71b97][runtime-visibility]")
{
    const auto package=make_visibility_scene(false,true);
    auto scene=make_render_scene(package);
    scene.static_world->runtime_brushes=std::make_shared<const scene_render::RuntimeBrushRenderFrame>();
    const auto runtime=scene.static_world->runtime_brushes;
    control::RuntimeWorldVisibility selector;
    constexpr renderer::RenderExtent extent{192,108};
    REQUIRE(selector.apply(scene,extent));
    REQUIRE(scene.static_world->visible_draw_list);
    CHECK(selector.statistics().selection.visible_world_surface_count==1U);
    CHECK(selector.statistics().selection.visible_brush_instance_count==0U);
    CHECK(selector.statistics().camera_leaf==1U);
    CHECK(scene.static_world->runtime_brushes==runtime);
    for(const auto& command:scene.static_world->visible_draw_list->commands())
        CHECK(command.object_kind==visibility::WorldVisibleObjectKind::world_surface);
    const auto first=scene.static_world->visible_draw_list;
    const auto identity=package->visibility_scene_identity();
    CHECK(first->scene_identity()==identity);
    REQUIRE(selector.apply(scene,extent));
    CHECK(scene.static_world->visible_draw_list==first);
    CHECK(selector.statistics().resolves==1U);CHECK(selector.statistics().cache_hits==1U);

    // Changes to entities, runtime brushes, lighting or HUD do not invalidate
    // immutable world selection and are never removed by it.
    scene.image_adjustments.brightness=0.1F;
    REQUIRE(selector.apply(scene,extent));CHECK(scene.static_world->visible_draw_list==first);
    scene.camera.target.x+=1.0F;
    REQUIRE(selector.apply(scene,extent));CHECK(scene.static_world->visible_draw_list!=first);
    auto last=scene.static_world->visible_draw_list;
    REQUIRE(selector.apply(scene,{108,192}));CHECK(scene.static_world->visible_draw_list!=last);
    last=scene.static_world->visible_draw_list;
    scene.camera.vertical_field_of_view_radians*=0.9F;
    REQUIRE(selector.apply(scene,{108,192}));CHECK(scene.static_world->visible_draw_list!=last);
    last=scene.static_world->visible_draw_list;
    scene.camera.near_plane*=2.0F;
    REQUIRE(selector.apply(scene,{108,192}));CHECK(scene.static_world->visible_draw_list!=last);
    last=scene.static_world->visible_draw_list;
    scene.camera.far_plane*=0.9F;
    REQUIRE(selector.apply(scene,{108,192}));CHECK(scene.static_world->visible_draw_list!=last);

    scene=make_render_scene(package);
    // Valid screen shake is bounded to 16 units. Put the unshaken camera
    // beside the x=24 leaf boundary so the allowed shake crosses it.
    scene.camera.position.x=20;scene.camera.target.x=20;
    REQUIRE(selector.apply(scene,extent));CHECK(selector.statistics().camera_leaf==1U);
    scene.screen_effects.translation={16,0,0};
    REQUIRE(selector.apply(scene,extent));CHECK(selector.statistics().camera_leaf==2U);
    REQUIRE(scene.static_world->visible_draw_list->commands().size()==1U);
    CHECK(scene.static_world->visible_draw_list->commands()[0].source_surface_index==1U);
    last=scene.static_world->visible_draw_list;
    scene.screen_effects.roll_degrees=10;
    REQUIRE(selector.apply(scene,extent));CHECK(scene.static_world->visible_draw_list!=last);

    auto replacement=make_visibility_scene(false,true);
    REQUIRE(replacement!=package);
    scene=make_render_scene(replacement);
    REQUIRE(selector.apply(scene,extent));CHECK(selector.statistics().adapter_rebuilds==2U);
    CHECK(scene.static_world->visible_draw_list->scene_identity()==replacement->visibility_scene_identity());
    selector.reset();CHECK(selector.statistics().resolves==0U);
    REQUIRE(selector.apply(scene,extent));CHECK(selector.statistics().adapter_rebuilds==1U);
}

TEST_CASE("Runtime visibility fails conservatively without stale selection or missing-PVS holes",
    "[gauss-71b97][runtime-visibility]")
{
    control::RuntimeWorldVisibility selector;
    auto scene=make_render_scene(make_visibility_scene());
    constexpr renderer::RenderExtent extent{192,108};
    REQUIRE(selector.apply(scene,extent));
    const auto selected=scene.static_world->visible_draw_list;
    CHECK_FALSE(selector.apply(scene,{0,108}));
    CHECK_FALSE(scene.static_world->visible_draw_list);CHECK_FALSE(scene.static_world->visibility_summary);
    REQUIRE(selector.apply(scene,extent));CHECK(scene.static_world->visible_draw_list==selected);
    CHECK(selector.statistics().selection.visible_world_surface_count==1U);
    scene.camera.target=scene.camera.position;
    CHECK_FALSE(selector.apply(scene,extent));CHECK_FALSE(scene.static_world->visible_draw_list);
    scene=make_render_scene(make_visibility_scene(true));
    REQUIRE(selector.apply(scene,extent));
    CHECK(selector.statistics().pvs_fallback==visibility::WorldPvsFallbackReason::camera_in_leaf_zero);
    CHECK(selector.statistics().selection.visible_world_surface_count==1U);
    CHECK(selector.statistics().fallbacks==1U);
    visibility::WorldVisibilityLimits resolve_limits;resolve_limits.maximum_visible_world_surfaces=0;
    control::RuntimeWorldVisibility limited{resolve_limits};
    CHECK_FALSE(limited.apply(scene,extent));CHECK_FALSE(scene.static_world->visible_draw_list);
    CHECK_FALSE(scene.static_world->visibility_summary);
    CHECK(limited.statistics().status==control::RuntimeWorldVisibilityStatus::resolve_failed);
    control::RuntimeWorldVisibility draw_limited{{},{0}};
    CHECK_FALSE(draw_limited.apply(scene,extent));CHECK_FALSE(scene.static_world->visible_draw_list);
    CHECK(draw_limited.statistics().status==control::RuntimeWorldVisibilityStatus::draw_list_failed);
    scene.static_world.reset();CHECK_FALSE(selector.apply(scene,extent));
    CHECK(selector.statistics().status==control::RuntimeWorldVisibilityStatus::no_scene);
}

TEST_CASE("Runtime final-camera selection retains actual pixels with no static brush duplication",
    "[gauss-71b97][runtime-visibility][opengl][actual-context]")
{
    auto gl=gpu_fixture::try_context(192,108);
    if(!gl || !gpu_fixture::capable_context())SKIP("OpenGL 3.3 Core unavailable");
    gl->initialize_renderer();
    auto scene=make_render_scene(make_visibility_scene(false,true));
    constexpr renderer::RenderExtent extent{192,108};
    control::RuntimeWorldVisibility selector;
    for(const float x:{8.0F,40.0F}) {
        scene.camera.position.x=x;scene.camera.target.x=x;
        scene.static_world->visible_draw_list.reset();scene.static_world->visibility_summary.reset();
        gl->renderer().render(scene,extent);
        const auto all=gl->renderer().observe_framebuffer(extent,scene.clear_color,true);
        REQUIRE(all);
        REQUIRE(selector.apply(scene,extent));
        gl->renderer().render(scene,extent);
        const auto visible=gl->renderer().observe_framebuffer(extent,scene.clear_color,true);
        REQUIRE(visible);CHECK(visible.rgba8==all.rgba8);
        CHECK(gl->renderer().statistics().visible_brush_instance_count==0U);
    }
}
} // namespace
