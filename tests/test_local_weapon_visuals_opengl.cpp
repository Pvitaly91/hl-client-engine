#include "entity_render/entity_opengl_test_support.hpp"
#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/renderer/render_scene.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

namespace {
namespace fixture=hlclient::tests::entity_opengl_fixture;
namespace assets=hlclient::tests::entity_render_fixture;
namespace entity=hlclient::entity_render;
namespace renderer=hlclient::renderer;

std::shared_ptr<const entity::EntityRenderFrame> frame(
    const entity::EntitySceneRenderPackage& package,bool instance,
    float x,std::uint64_t revision) {
    entity::EntityRenderFrameBuildInput input;
    input.resource_id=0xE404U;input.resource_revision=revision;
    input.interpolation={0,0,0,0,1,1,entity::EntityRenderInterpolationProfile::synthetic_seconds_v1};
    if(instance) {
        entity::StudioRenderPose pose{package.studio_assets()[0]->source_identity(),{}};
        pose.bone_matrices.push_back({1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1});
        input.studio_poses.push_back(std::move(pose));
        entity::StudioEntityRenderInstance shell;
        shell.entity_number=1;shell.studio_asset_index=0;shell.pose_index=0;
        shell.transform.origin={x,0,0};
        shell.transform.rotation_degrees={0,90,0}; // face the +X test camera
        shell.interpolated_bounds={{x-2,-3,-4},{x+5,6,7}};
        input.studio_instances.push_back(shell);
    }
    auto built=entity::EntityRenderFrameBuilder{}.build(package,std::move(input));
    REQUIRE(built);
    return std::make_shared<const entity::EntityRenderFrame>(std::move(*built.frame));
}
}

TEST_CASE("E4 attached flash changes actual OpenGL pixels only in its bounded frame",
          "[weapon-visuals][opengl][actual-context]") {
    auto package_result=assets::scene_package(assets::render_assets(true,false));
    REQUIRE(package_result);
    auto package=std::make_shared<const entity::EntitySceneRenderPackage>(
        std::move(*package_result.package));
    auto context=fixture::try_context(320,240);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();
    renderer::RenderScene scene;
    scene.camera.position={0,0,0};scene.camera.target={1,0,0};
    scene.first_person_entities=renderer::RenderDynamicEntities{
        package,frame(*package,false,0,1),{}};
    context->renderer().render(scene,{320,240});
    auto baseline=context->renderer().observe_framebuffer({320,240},scene.clear_color);
    REQUIRE(baseline);
    scene.first_person_flash=renderer::RenderMuzzleFlash{{10,0,0},2.0F,{1,0.7F,0.25F,0.85F}};
    context->renderer().render(scene,{320,240});
    auto visible=context->renderer().observe_framebuffer({320,240},scene.clear_color);
    REQUIRE(visible);
    CHECK(visible.color_signature!=baseline.color_signature);
    CHECK(visible.non_clear_pixel_count>baseline.non_clear_pixel_count);
    CHECK(glGetError()==GL_NO_ERROR);
    for(float pitch:{80.0F,-80.0F}) {
        constexpr float radians=0.01745329252F;
        scene.camera.position={100,-75,30};
        scene.camera.target={100+std::cos(pitch*radians),-75,
            30+std::sin(pitch*radians)};
        context->renderer().render(scene,{320,240});
        auto turned=context->renderer().observe_framebuffer({320,240},scene.clear_color);
        REQUIRE(turned);CHECK(turned.color_signature==visible.color_signature);
    }
    scene.first_person_flash.reset();
    context->renderer().render(scene,{320,240});
    auto expired=context->renderer().observe_framebuffer({320,240},scene.clear_color);
    REQUIRE(expired);CHECK(expired.color_signature==baseline.color_signature);
    context->release_renderer();
}

TEST_CASE("E4 world Studio casing instance changes pixels outside first-person pass",
          "[weapon-visuals][opengl][actual-context]") {
    auto package_result=assets::scene_package(assets::render_assets(true,false));
    REQUIRE(package_result);
    auto package=std::make_shared<const entity::EntitySceneRenderPackage>(
        std::move(*package_result.package));
    auto context=fixture::try_context(320,240);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();
    renderer::RenderScene scene;
    scene.camera.position={0,0,0};scene.camera.target={1,0,0};
    context->renderer().render(scene,{320,240});
    auto baseline=context->renderer().observe_framebuffer({320,240},scene.clear_color);
    REQUIRE(baseline);
    scene.transient_world_entities=renderer::RenderDynamicEntities{
        package,frame(*package,true,20,2),{}};
    context->renderer().render(scene,{320,240});
    auto visible=context->renderer().observe_framebuffer({320,240},scene.clear_color);
    REQUIRE(visible);CHECK(visible.color_signature!=baseline.color_signature);
    CHECK(visible.non_clear_pixel_count>baseline.non_clear_pixel_count);
    CHECK(glGetError()==GL_NO_ERROR);
    scene.transient_world_entities.reset();
    context->renderer().render(scene,{320,240});
    auto expired=context->renderer().observe_framebuffer({320,240},scene.clear_color);
    REQUIRE(expired);CHECK(expired.color_signature==baseline.color_signature);
    context->release_renderer();
}

TEST_CASE("E4.1 point light affects first-person Studio pixels without a flash quad",
          "[weapon-visuals][opengl][actual-context]") {
    auto package_result=assets::scene_package(assets::render_assets(true,false));
    REQUIRE(package_result);
    auto package=std::make_shared<const entity::EntitySceneRenderPackage>(
        std::move(*package_result.package));
    auto context=fixture::try_context(320,240);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();
    renderer::RenderScene scene;
    scene.camera.position={0,0,0};scene.camera.target={1,0,0};
    scene.first_person_entities=renderer::RenderDynamicEntities{
        package,frame(*package,true,10,3),{}};
    context->renderer().render(scene,{320,240});
    const auto baseline=context->renderer().observe_framebuffer({320,240},scene.clear_color);
    REQUIRE(baseline);
    scene.transient_first_person_light=renderer::RenderPointLight{
        {10,0,0},30.0F,1.0F,{1.0F,0.62F,0.28F}};
    context->renderer().render(scene,{320,240});
    const auto lit=context->renderer().observe_framebuffer({320,240},scene.clear_color);
    REQUIRE(lit);
    CHECK(lit.color_signature!=baseline.color_signature);
    CHECK(glGetError()==GL_NO_ERROR);
    scene.transient_first_person_light.reset();
    context->renderer().render(scene,{320,240});
    const auto expired=context->renderer().observe_framebuffer({320,240},scene.clear_color);
    REQUIRE(expired);
    CHECK(expired.color_signature==baseline.color_signature);
    context->release_renderer();
}
