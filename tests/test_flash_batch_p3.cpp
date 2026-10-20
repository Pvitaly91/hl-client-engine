#include "entity_render/entity_opengl_test_support.hpp"

#include <hlclient/renderer/opengl/opengl_renderer.hpp>
#include <hlclient/renderer/render_scene.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {
namespace render=hlclient::renderer;
namespace fixture=hlclient::tests::entity_opengl_fixture;

render::RenderScene base_scene() {
    render::RenderScene scene;
    scene.clear_color={.04F,.05F,.07F,1};
    scene.camera.position={12,0,8};scene.camera.target={0,0,8};scene.camera.up={0,0,1};
    scene.camera.near_plane=.1F;scene.camera.far_plane=128;
    return scene;
}

std::uint64_t rgba_signature(const std::vector<std::uint8_t>& rgba) {
    std::uint64_t hash=14695981039346656037ULL;
    for(const auto channel:rgba){hash^=channel;hash*=1099511628211ULL;}
    return hash;
}

template<class Statistics>
void check_batch_operations(const Statistics& before,const Statistics& after,
                            std::uint64_t draws,std::uint64_t quads) {
    // This fixture is copied into the preserved pre-optimization source tree
    // to provide an independent pixel oracle. Its older statistics type has
    // no P3 fields, so only the candidate checks the new operation contract.
    if constexpr(requires(const Statistics& s){
        s.transient_flash_upload_count;
        s.transient_flash_draw_count;
        s.transient_flash_quad_count;
        s.transient_flash_storage_orphan_count;
        s.transient_flash_storage_bytes_high_water;
    }) {
        CHECK(after.transient_flash_upload_count-before.transient_flash_upload_count==draws);
        CHECK(after.transient_flash_draw_count-before.transient_flash_draw_count==draws);
        CHECK(after.transient_flash_quad_count-before.transient_flash_quad_count==quads);
        CHECK(after.transient_flash_storage_orphan_count-before.transient_flash_storage_orphan_count==
              (draws?1U:0U));
        constexpr std::size_t bytes_per_quad=6U*11U*sizeof(float);
        CHECK(after.transient_flash_storage_bytes_high_water==std::max(
            before.transient_flash_storage_bytes_high_water,
            static_cast<std::size_t>(quads)*bytes_per_quad));
    }
}

template<class Statistics>
void check_reset_operations(const Statistics& stats) {
    if constexpr(requires(const Statistics& s){
        s.transient_flash_upload_count;
        s.transient_flash_draw_count;
        s.transient_flash_quad_count;
        s.transient_flash_storage_orphan_count;
        s.transient_flash_storage_bytes_high_water;
    }) {
        CHECK(stats.transient_flash_draw_count==0);
        CHECK(stats.transient_flash_upload_count==0);
        CHECK(stats.transient_flash_quad_count==0);
        CHECK(stats.transient_flash_storage_orphan_count==0);
        CHECK(stats.transient_flash_storage_bytes_high_water==0);
    }
}

void capture(render::opengl::OpenGlRenderer& gpu,const render::RenderScene& scene,
             std::string_view label,std::uint64_t draws,std::uint64_t quads,
             bool expect_visible=true) {
    constexpr render::RenderExtent extent{320,240};
    const auto before=gpu.statistics();
    gpu.render(scene,extent);
    const auto observation=gpu.observe_framebuffer(extent,scene.clear_color,true);
    REQUIRE(observation);
    REQUIRE(glGetError()==GL_NO_ERROR);
    REQUIRE(observation.rgba8.size()==static_cast<std::size_t>(extent.width*extent.height*4));
    const auto hash=rgba_signature(observation.rgba8);
    std::cout<<"P3_PIXEL_ORACLE "<<label<<' '<<hash<<' '
             <<observation.non_clear_pixel_count<<std::endl;
    if(expect_visible)CHECK(observation.non_clear_pixel_count>0);
    check_batch_operations(before,gpu.statistics(),draws,quads);
}

void benchmark_if_requested(render::opengl::OpenGlRenderer& gpu,
                            const render::RenderScene& scene,std::string_view label) {
#if defined(_WIN32)
    std::size_t selected_length{};
    char selected[2]{};
    if(::getenv_s(&selected_length,selected,sizeof(selected),"HLCLIENT_P3_BATCH_BENCH")!=0 ||
       selected_length!=2 || selected[0]!='1' || selected[1]!='\0')return;
#else
    const char* selected=std::getenv("HLCLIENT_P3_BATCH_BENCH");
    if(!selected || selected[0]!='1' || selected[1]!='\0')return;
#endif
    constexpr render::RenderExtent extent{320,240};
    constexpr std::size_t warmup=16,iterations=128;
    for(std::size_t i=0;i<warmup;++i)gpu.render(scene,extent);
    glFinish(); // not inside the CPU submission loop
    std::array<double,iterations> elapsed{};
    for(auto& value:elapsed) {
        const auto begin=std::chrono::steady_clock::now();
        gpu.render(scene,extent);
        const auto end=std::chrono::steady_clock::now();
        value=std::chrono::duration<double,std::milli>(end-begin).count();
    }
    glFinish();
    std::sort(elapsed.begin(),elapsed.end());
    const auto percentile=[&](double p) {
        return elapsed[static_cast<std::size_t>((iterations-1)*p)];
    };
    std::cout<<std::fixed<<std::setprecision(6)
             <<"P3_SUBMIT {\"case\":\""<<label<<"\",\"iterations\":"<<iterations
             <<",\"p50_ms\":"<<percentile(.50)<<",\"p95_ms\":"<<percentile(.95)
             <<",\"p99_ms\":"<<percentile(.99)<<",\"max_ms\":"<<elapsed.back()<<"}"
             <<std::endl;
    REQUIRE(glGetError()==GL_NO_ERROR);
}
} // namespace

TEST_CASE("World transient batch preserves ordered mixed blend pixels and saturating capacity",
          "[optimization-p3][opengl][no-stock]") {
    auto context=fixture::try_context(320,240);
    if(!context || !fixture::capable_context())SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();
    auto& gpu=context->renderer();
    auto scene=base_scene();

    capture(gpu,scene,"empty_off",0,0,false);
    scene.world_flash_count=1;
    scene.world_flashes[0]={{0,0,8},.8F,{.34F,.22F,.13F,.50F}};
    capture(gpu,scene,"single_off",0,0);
    benchmark_if_requested(gpu,scene,"single_off");
    scene.camera.position={12,2,8};scene.camera.target={0,1,8};
    capture(gpu,scene,"single_camera_change",0,0);
    scene=base_scene();

    scene.world_flash_count=3;
    for(std::size_t i=0;i<scene.world_flash_count;++i)
        scene.world_flashes[i]={{0,static_cast<float>(i)*1.6F-2.2F,8.5F},.85F,
                                {.42F,.27F,.12F,.38F}};
    scene.world_particle_count=4;
    for(std::size_t i=0;i<scene.world_particle_count;++i)
        scene.world_particles[i]={{-.2F,static_cast<float>(i)*1.3F-2.0F,7.7F},.64F,
                                  {.15F,.43F,.30F,.47F},i==2};
    scene.world_line_count=3;
    scene.world_lines[0]={{0,-5.1F,7},{0,-3.3F,7},.55F,{.20F,.38F,.56F,.75F},.30F};
    scene.world_lines[1]={{0,-.8F,8},{0,.8F,8},.65F,{.56F,.16F,.12F,.50F},.15F,
                          hlclient::game_api::WorldLineGeometry::solid_3d};
    scene.world_lines[2]={{0,3.3F,7},{0,5.1F,7},.55F,{.22F,.45F,.17F,.65F},.35F};
    capture(gpu,scene,"mixed_off",6,9);
    benchmark_if_requested(gpu,scene,"mixed_off");
    scene.requested_msaa_samples=4;
    capture(gpu,scene,"mixed_msaa4",6,9);

    scene=base_scene();
    scene.world_flash_count=scene.world_flashes.size();
    for(std::size_t i=0;i<scene.world_flash_count;++i)
        scene.world_flashes[i]={{0,static_cast<float>(i%8U)*1.25F-4.5F,
                                 static_cast<float>(i/8U)*1.25F+5.8F},.40F,
                                {.06F,.10F,.16F,.07F}};
    scene.world_particle_count=scene.world_particles.size();
    for(std::size_t i=0;i<scene.world_particle_count;++i)
        scene.world_particles[i]={{-.25F,static_cast<float>(i%16U)*.65F-4.8F,
                                    static_cast<float>(i/16U)*.36F+5.1F},.30F,
                                   {.05F,.08F,.04F,.025F},true};
    scene.world_line_count=scene.world_lines.size();
    for(std::size_t i=0;i<scene.world_line_count;++i) {
        const float y=static_cast<float>(i%16U)*.65F-4.8F;
        const float z=static_cast<float>(i/16U)*1.25F+5.8F;
        scene.world_lines[i]={{0,y,z},{0,y+.45F,z},.15F,
                              {.04F,.07F,.10F,.08F},.20F};
    }
    capture(gpu,scene,"capacity_off",1,352);
    benchmark_if_requested(gpu,scene,"capacity_off");

    scene=base_scene();
    scene.world_flash_count=3;
    scene.world_flashes[0]={{0,-1,8},.7F,{.3F,.2F,.1F,.5F}};
    scene.world_flashes[1]={{0,0,8},std::numeric_limits<float>::quiet_NaN(),{1,1,1,1}};
    scene.world_flashes[2]={{0,1,8},.7F,{.2F,.3F,.1F,.5F}};
    scene.world_particle_count=2;
    scene.world_particles[0]={{0,-1,7},.55F,{.3F,.4F,.2F,.5F},false};
    scene.world_particles[1]={{0,1,7},.55F,{.2F,.4F,.3F,.5F},false};
    capture(gpu,scene,"invalid_off",2,4);

    auto studio=fixture::studio_scene();
    auto first_person=fixture::render_scene(studio);
    first_person.first_person_entities=first_person.dynamic_entities;
    first_person.dynamic_entities.reset();
    // First-person coordinates look along +X and clip anything inside the
    // near plane. Keep this visible so the unchanged single-flash path has
    // a meaningful pixel oracle in both the baseline and candidate renderer.
    first_person.first_person_flash=render::RenderMuzzleFlash{{10,0,0},2.0F,{.7F,.4F,.2F,.7F}};
    capture(gpu,first_person,"first_person_unchanged",0,0);
    gpu.reset_session_resources();
    check_reset_operations(gpu.statistics());
    context->release_renderer();
}
