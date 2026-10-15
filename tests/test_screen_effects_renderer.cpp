#include "entity_render/entity_opengl_test_support.hpp"
#include "world_render_test_fixture.hpp"

#include <hlclient/renderer/render_camera_math.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
namespace r=hlclient::renderer;
namespace glf=hlclient::tests::entity_opengl_fixture;
namespace wf=hlclient::tests::world_render_fixture;
constexpr r::RenderExtent extent{96,96};

r::RenderScene scene_with_world(bool world) {
    r::RenderScene scene;scene.clear_color={.2F,.3F,.4F,1};
    scene.camera.position={8,8,16};scene.camera.target={8,8,0};scene.camera.up={0,1,0};
    if(world) {
        const wf::FixtureOptions options{.unlit=true};
        auto built=wf::make_package(options);REQUIRE(built);REQUIRE(built.package);
        scene.static_world.emplace();
        scene.static_world->package=std::make_shared<const hlclient::world_render::WorldRenderPackage>(
            std::move(*built.package));
    }
    return scene;
}
auto render(glf::HiddenContext& context,const r::RenderScene& scene) {
    context.renderer().render(scene,extent);
    auto pixels=context.renderer().observe_framebuffer(extent,scene.clear_color,true);
    REQUIRE(pixels);CHECK(glGetError()==GL_NO_ERROR);return pixels;
}
std::array<unsigned,3> pixel(const r::opengl::OpenGlFramebufferObservation& pixels,int x,int y) {
    const auto at=4U*static_cast<std::size_t>((extent.height-1-y)*extent.width+x);
    REQUIRE(at+2<pixels.rgba8.size());
    return {pixels.rgba8[at],pixels.rgba8[at+1],pixels.rgba8[at+2]};
}
}

TEST_CASE("Screen fade composes after scene color before HUD and expires without state residue",
    "[renderer][opengl][screen-effects][actual-context][no-stock]") {
    auto context=glf::try_context(extent.width,extent.height);
    if(!context || !glf::capable_context())SKIP("OpenGL3.3 Core context unavailable");
    context->initialize_renderer();
    for(const bool world:{false,true})for(const unsigned msaa:{0U,4U}) {
        INFO(world);INFO(msaa);
        auto scene=scene_with_world(world);scene.requested_msaa_samples=msaa;
        scene.image_adjustments={.1F,.8F,1.2F};
        scene.basic_hud.emplace();scene.basic_hud->rectangles.push_back({4,4,12,8,{1,0,0,1},10});
        const auto camera=scene.camera;
        const auto baseline=render(*context,scene);
        const auto center=pixel(baseline,48,48);
        scene.screen_effects.overlay_rgba={1,1,1,.5F};
        const auto half=render(*context,scene);
        const auto half_center=pixel(half,48,48);
        for(std::size_t channel=0;channel<3U;++channel)
            CHECK(std::abs(static_cast<int>(half_center[channel])-
                static_cast<int>(std::lround((center[channel]+255)*.5)))<=1);
        CHECK(pixel(half,8,8)==pixel(baseline,8,8)); // HUD is not whitened
        scene.screen_effects.overlay_rgba[3]=1;
        const auto white=render(*context,scene);
        for(const auto xy:std::array<std::array<int,2>,5>{{{0,0},{95,0},{0,95},{95,95},{48,48}}})
            CHECK(pixel(white,xy[0],xy[1])==std::array<unsigned,3>{255,255,255});
        CHECK(pixel(white,8,8)==std::array<unsigned,3>{255,0,0});
        scene.screen_effects.overlay_rgba={.5F,.25F,0,.5F};scene.screen_effects.modulate=true;
        const auto modulated=render(*context,scene);
        const auto mod_center=pixel(modulated,48,48);
        const std::array<double,3> factors{.75,.625,.5};
        for(std::size_t channel=0;channel<3U;++channel)
            CHECK(std::abs(static_cast<int>(mod_center[channel])-
                static_cast<int>(std::lround(center[channel]*factors[channel])))<=1);
        CHECK(pixel(modulated,8,8)==pixel(baseline,8,8));
        scene.screen_effects={};
        CHECK(render(*context,scene).rgba8==baseline.rgba8);
        CHECK(scene.camera==camera);
        for(const float bad:{std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),-1.0F,2.0F}) {
            scene.screen_effects.overlay_rgba={1,1,1,bad};
            CHECK_THROWS_AS(context->renderer().render(scene,extent),std::invalid_argument);
            CHECK(context->renderer().observe_framebuffer(extent,scene.clear_color,true).rgba8==baseline.rgba8);
        }
        scene.screen_effects={};CHECK(render(*context,scene).rgba8==baseline.rgba8);
    }
}

TEST_CASE("Screen shake changes production world pixels deterministically without moving the source camera",
    "[renderer][opengl][screen-effects][actual-context][no-stock]") {
    auto context=glf::try_context(extent.width,extent.height);
    if(!context || !glf::capable_context())SKIP("OpenGL3.3 Core context unavailable");
    context->initialize_renderer();auto scene=scene_with_world(true);
    const auto original=scene.camera;
    const auto baseline=render(*context,scene);
    scene.screen_effects.translation={.5F,.25F,0};scene.screen_effects.roll_degrees=7;
    const auto moved=render(*context,scene);
    CHECK(moved.rgba8!=baseline.rgba8);
    CHECK(render(*context,scene).rgba8==moved.rgba8);
    CHECK(scene.camera==original);
    scene.screen_effects={};CHECK(render(*context,scene).rgba8==baseline.rgba8);
    CHECK(scene.camera==original);
}
