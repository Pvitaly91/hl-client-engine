#include "collision_brush_test_fixture.hpp"
#include "crossbow_test_fixture.hpp"
#include "world_render_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include <hlclient/app/world_impact_presentation.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_brush_transform.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/games/halflife/weapon_visuals.hpp>
#include <hlclient/renderer/render_scene.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/world_render/world_render_package_builder.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
using Catch::Approx;
namespace app=hlclient::app;
namespace api=hlclient::game_api;
namespace assets=hlclient::assets;
namespace renderer=hlclient::renderer;
namespace fixture=hlclient::tests::world_render_fixture;
bool same_position(const assets::AssetVector3& a,const assets::AssetVector3& b) {
    return a.x==b.x && a.y==b.y && a.z==b.z;
}
std::shared_ptr<const hlclient::world_render::WorldRenderPackage> wall(
    bool masked=false,std::uint8_t shade=0x40) {
    auto world=fixture::make_world();
    for(auto& vertex:world.vertices) {
        const auto p=vertex.position;
        vertex.position={0,p.x,p.y};
        vertex.normal={1,0,0};
    }
    world.bounds={{0,0,0},{0,16,16}};
    world.surfaces[0].bounds=world.bounds;
    auto texture=fixture::make_texture(masked);
    for(auto& mip:texture.mip_levels)
        for(std::size_t i=0;i<mip.rgba_pixels.size();i+=4)
            mip.rgba_pixels[i]=mip.rgba_pixels[i+1]=mip.rgba_pixels[i+2]=std::byte{shade};
    assets::WorldMaterialTextureBinding binding;
    binding.material_index=0U;
    binding.status=assets::WorldMaterialTextureBindingStatus::resolved_embedded;
    binding.texture_asset_index=0U;
    binding.source_bsp_texture_index=0U;
    std::vector<assets::WorldTextureAsset> textures;
    textures.push_back(std::move(texture));
    auto set=assets::WorldTextureSet::create(std::move(textures),{binding},{},1U);
    REQUIRE(set);
    auto built=hlclient::world_render::WorldRenderPackageBuilder{}.build(
        {std::move(world),std::move(*set.texture_set)},
        fixture::make_lightmap_set({}));
    REQUIRE(built);
    return std::make_shared<const hlclient::world_render::WorldRenderPackage>(
        std::move(*built.package));
}
std::shared_ptr<const hlclient::world_render::WorldRenderPackage> split_wall() {
    auto world=fixture::make_world();
    const auto source_surface=world.surfaces[0];
    world.vertices.clear();world.indices.clear();world.surfaces.clear();
    world.bounds={{0,0,0},{0,16,16}};
    world.materials[0].texture_name="CONCRETE";
    auto metal=world.materials[0];metal.texture_name="METAL";
    metal.source_texture_index=1U;world.materials.push_back(metal);
    for(unsigned half=0;half<2;++half) {
        const float lo=half?8.0F:0.0F, hi=half?16.0F:8.0F;
        const auto base=static_cast<std::uint32_t>(world.vertices.size());
        for(const auto yz:std::array<std::array<float,2>,4>{{{lo,0},{hi,0},{hi,16},{lo,16}}})
            world.vertices.push_back({{0,yz[0],yz[1]},{1,0,0},{yz[0],yz[1]}});
        const auto first=static_cast<std::uint32_t>(world.indices.size());
        for(auto index:std::array<std::uint32_t,6>{0,1,2,0,2,3}) world.indices.push_back(base+index);
        auto surface=source_surface;
        surface.first_vertex=base;surface.vertex_count=4;
        surface.first_index=first;surface.index_count=6;
        surface.material_index=half;surface.source_surface_ordinal=half;
        surface.bounds={{0,lo,0},{0,hi,16}};
        world.surfaces.push_back(surface);
    }
    world.statistics.emitted_vertex_count=8;
    world.statistics.emitted_triangle_count=4;
    world.statistics.emitted_surface_count=2;
    world.statistics.material_count=2;
    auto a=fixture::make_texture(false),b=fixture::make_texture(false);
    a.name="CONCRETE";b.name="METAL";
    std::vector<assets::WorldTextureAsset> textures;
    textures.push_back(std::move(a));textures.push_back(std::move(b));
    std::vector<assets::WorldMaterialTextureBinding> bindings;
    for(unsigned i=0;i<2;++i) {
        assets::WorldMaterialTextureBinding binding;
        binding.material_index=i;
        binding.status=assets::WorldMaterialTextureBindingStatus::resolved_embedded;
        binding.texture_asset_index=i;binding.source_bsp_texture_index=i;
        bindings.push_back(binding);
    }
    auto set=assets::WorldTextureSet::create(std::move(textures),std::move(bindings),{},2U);
    REQUIRE(set);
    auto built=hlclient::world_render::WorldRenderPackageBuilder{}.build(
        {std::move(world),std::move(*set.texture_set)},
        fixture::make_lightmap_set({.atlas_page_count=2U}));
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    return std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*built.package));
}
api::LocalWorldImpactRequest shot(unsigned sequence=1,float y=8,float z=8) {
    api::LocalWorldImpactRequest r;
    r.action={1,2,59,3,sequence,api::LocalWeaponAction::primary_fire,0.1,1};
    r.origin={5,y,z};r.direction={-1,0,0};
    r.maximum_distance_units=20;r.decal_half_size_units=4;r.expires_at_seconds=0.35;
    return r;
}
api::LocalWorldImpactRequest crowbar(float origin_x=5.0F) {
    auto request=shot();
    request.action.weapon_id=1U;
    request.action.model_index=60U;
    request.action.kind=api::LocalWeaponAction::melee_swing;
    request.origin.x=origin_x;
    request.maximum_distance_units=32.0F;
    request.decal_delay_seconds=0.2;
    return request;
}
std::shared_ptr<const assets::WorldTextureAsset> decal() {
    auto texture=fixture::make_texture(true);
    texture.name="{SHOT1";
    auto& pixels=texture.mip_levels[0].rgba_pixels;
    for(std::size_t i=0;i<pixels.size();i+=4) {
        pixels[i]=std::byte{0x08};pixels[i+1]=std::byte{0x08};
        pixels[i+2]=std::byte{0x08};
        const auto p=i/4,x=p%16,y=p/16;
        pixels[i+3]=(x>=2 && x<14 && y>=2 && y<14)
            ? std::byte{0xff}:std::byte{0};
    }
    return std::make_shared<const assets::WorldTextureAsset>(std::move(texture));
}
std::shared_ptr<const assets::WorldTextureAsset> white_neutral_decal() {
    auto texture=fixture::make_texture(true);
    texture.name="{SHOT1";
    auto& pixels=texture.mip_levels[0].rgba_pixels;
    for(std::size_t i=0;i<pixels.size();i+=4) {
        const auto p=i/4,x=p%16,y=p/16;
        const auto value=(x>=6 && x<10 && y>=6 && y<10) ? std::byte{0x18} : std::byte{0xff};
        pixels[i]=pixels[i+1]=pixels[i+2]=value;
        pixels[i+3]=std::byte{0xff}; // mirrors the actual WAD: no index-255 background
    }
    return std::make_shared<const assets::WorldTextureAsset>(std::move(texture));
}
auto collision() {
    return hlclient::tests::collision_brush_fixture::package(true,0.0,0U);
}
std::shared_ptr<const hlclient::collision::CollisionWorldPackage> floor_collision() {
    const auto base=collision();
    std::vector<hlclient::collision::CollisionPlane> planes(base->planes().begin(),base->planes().end());
    for(auto& plane:planes) plane.normal={0,0,1};
    return std::make_shared<const hlclient::collision::CollisionWorldPackage>(
        std::move(planes),
        std::vector<hlclient::collision::CollisionNode>(base->nodes().begin(),base->nodes().end()),
        std::vector<hlclient::collision::CollisionLeaf>(base->leaves().begin(),base->leaves().end()),
        std::vector<hlclient::collision::CollisionClipnode>(base->clipnodes().begin(),base->clipnodes().end()),
        std::vector<hlclient::collision::CollisionModel>(base->models().begin(),base->models().end()),
        base->identity());
}
std::shared_ptr<const hlclient::world_render::WorldRenderPackage> floor_world() {
    auto world=fixture::make_world();
    for(auto& vertex:world.vertices) {
        const auto p=vertex.position;
        vertex.position={p.x,p.y,0};
        vertex.normal={0,0,1};
    }
    world.bounds={{0,0,0},{16,16,0}};
    world.surfaces[0].bounds=world.bounds;
    assets::WorldMaterialTextureBinding binding;
    binding.material_index=0U;
    binding.status=assets::WorldMaterialTextureBindingStatus::resolved_embedded;
    binding.texture_asset_index=0U;
    binding.source_bsp_texture_index=0U;
    std::vector<assets::WorldTextureAsset> textures{fixture::make_texture(false)};
    auto set=assets::WorldTextureSet::create(std::move(textures),{binding},{},1U);
    REQUIRE(set);
    auto built=hlclient::world_render::WorldRenderPackageBuilder{}.build(
        {std::move(world),std::move(*set.texture_set)},fixture::make_lightmap_set({}));
    REQUIRE(built);
    return std::make_shared<const hlclient::world_render::WorldRenderPackage>(
        std::move(*built.package));
}
}

TEST_CASE("E15 moving tracer head glow and light share launch endpoint fade bounds and reset",
          "[e15][world-impacts][glow]") {
    auto request=shot();request.origin={1000,8,8};request.maximum_distance_units=2000;
    request.tracer=api::LocalWorldImpactRequest::Tracer{};
    auto& tracer=*request.tracer;tracer.follow_local_muzzle=true;
    tracer.lifetime_seconds=.078;tracer.travel_speed_units_per_second=12000;
    tracer.segment_length_units=24;tracer.head_glow={1.25F,48,.35F};
    request.suppress_surface_effects=true;
    const auto world=wall();app::WorldImpactPresentation owner;
    REQUIRE(owner.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::hit);
    renderer::RenderScene scene;owner.append_lines(scene);
    CHECK(scene.world_line_count==0);CHECK(scene.world_effect_light_count==0);CHECK(scene.world_flash_count==0);
    const app::WorldImpactPresentation::LocalMuzzleAnchor anchor{1,59,{1000,8,8}};
    owner.append_lines(scene,anchor);
    REQUIRE(scene.world_effect_light_count==1);REQUIRE(scene.world_flash_count==1);
    CHECK(scene.world_effect_lights[0].center.x==Approx(976));
    CHECK(scene.world_flashes[0].center.x==scene.world_lines[0].end.x);
    const auto first_intensity=scene.world_effect_lights[0].intensity;
    (void)owner.submit(request,collision(),world.get(),{},false,.1);
    owner.update(.11);scene={};owner.append_lines(scene,anchor);
    REQUIRE(scene.world_effect_light_count==1);CHECK(scene.world_effect_lights[0].center.x==Approx(856));
    CHECK(scene.world_effect_lights[0].intensity<first_intensity);
    CHECK(scene.world_flashes[0].center.x==scene.world_lines[0].end.x);
    owner.update(.178);scene={};owner.append_lines(scene,anchor);
    CHECK(scene.world_flash_count==0);CHECK(scene.world_effect_light_count==0);CHECK(scene.world_line_count==0);
    owner.reset();scene={};owner.append_lines(scene,anchor);CHECK(scene.effect_lights().empty());
}

TEST_CASE("E15 supported impact glow is once per burst with separate muzzle light and short fade",
          "[e15][world-impacts][glow]") {
    const auto world=wall();app::WorldImpactPresentation owner;
    auto request=shot();request.particles=hlclient::games::halflife::bullet_impact_particles();
    REQUIRE(owner.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::hit);
    (void)owner.submit(request,collision(),world.get(),{},false,.1);
    owner.update(.1);renderer::RenderScene scene;
    scene.transient_world_light={{5,4,8},96,.8F,{1,.62F,.28F}};
    owner.append_impact_glows(scene);
    REQUIRE(scene.world_effect_light_count==1);REQUIRE(scene.world_flash_count==1);
    CHECK(scene.transient_world_light->center.y==4);CHECK(scene.world_effect_lights[0].center.y==8);
    CHECK(scene.world_effect_lights[0].radius_units==64);CHECK(scene.world_effect_lights[0].intensity==Approx(.55));
    CHECK(owner.particles().size()==16);CHECK(owner.particles()[0].emissive);
    owner.update(.13);scene={};owner.append_impact_glows(scene);
    REQUIRE(scene.world_effect_light_count==1);CHECK(scene.world_effect_lights[0].intensity==Approx(.275).margin(.001));
    owner.update(.161);scene={};owner.append_impact_glows(scene);
    CHECK(scene.effect_lights().empty());CHECK(scene.world_flash_count==0);CHECK(owner.particles().size()==16);
    owner.update(.12);scene={};owner.append_impact_glows(scene);CHECK(scene.effect_lights().empty());
    owner.reset();auto miss=request;miss.direction={1,0,0};
    CHECK(owner.submit(miss,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::miss);
    owner.update(.1);scene={};owner.append_impact_glows(scene);CHECK(scene.effect_lights().empty());
    request.suppress_surface_effects=true;
    REQUIRE(owner.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::hit);
    owner.update(.1);scene={};owner.append_impact_glows(scene);CHECK(scene.effect_lights().empty());
}

TEST_CASE("E15 production OpenGL lights wall beyond sprites without displacing muzzle and resets uniforms",
          "[e15][glow][opengl][actual-context]") {
    auto context=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");context->initialize_renderer();
    const auto world=wall();renderer::RenderScene scene;
    scene.camera.position={25,8,8};scene.camera.target={0,8,8};scene.static_world=renderer::RenderStaticWorld{world};
    const auto capture=[&] { context->renderer().render(scene,{320,240});
        auto image=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(image);return image; };
    const auto baseline=capture();scene.transient_world_light={{1,3,8},4,.6F,{1,.72F,.26F}};
    const auto primary=capture();CHECK(primary.color_signature!=baseline.color_signature);
    app::WorldImpactPresentation owner;auto request=shot(1,13);
    request.particles=hlclient::games::halflife::bullet_impact_particles();
    // Scale only light radius to this small fixture, to isolate two lit regions.
    request.particles->contact_glow.light_radius_units=4;
    REQUIRE(owner.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::hit);
    owner.update(.1);owner.append_impact_glows(scene);scene.world_flash_count=0;
    REQUIRE(scene.world_particle_count==0);REQUIRE(scene.world_line_count==0);
    const auto combined=capture();CHECK(combined.color_signature!=primary.color_signature);
    std::size_t retained_primary=0,added_secondary=0;
    for(std::size_t i=0;i<baseline.rgba8.size();i+=4) {
        if(primary.rgba8[i]>baseline.rgba8[i]) {
            CHECK(combined.rgba8[i]>=primary.rgba8[i]);++retained_primary;
        }
        if(combined.rgba8[i]>primary.rgba8[i]) ++added_secondary;
    }
    CHECK(retained_primary>20);CHECK(added_secondary>20); // real surface pixels, no effect sprite drawn
    scene.world_effect_lights[1]=scene.world_effect_lights[0];
    scene.world_effect_lights[1].center.x=std::numeric_limits<float>::quiet_NaN();scene.world_effect_light_count=2;
    CHECK(capture().color_signature==combined.color_signature);
    owner.update(.161);scene.world_effect_light_count=0;owner.append_impact_glows(scene);
    CHECK(capture().color_signature==primary.color_signature);
    scene.transient_world_light.reset();CHECK(capture().color_signature==baseline.color_signature);
    CHECK(glGetError()==GL_NO_ERROR);context->release_renderer();
}

TEST_CASE("E15 production OpenGL emissive sparks and tracer glows remain soft depth-tested and clean",
          "[e15][glow][opengl][actual-context]") {
    auto context=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");context->initialize_renderer();
    renderer::RenderScene scene;scene.camera.position={25,8,8};scene.camera.target={0,8,8};
    scene.static_world=renderer::RenderStaticWorld{wall()};
    const auto capture=[&] { context->renderer().render(scene,{320,240});
        auto image=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(image);return image; };
    const auto baseline=capture();scene.world_particle_count=1;
    scene.world_particles[0]={{2,8,8},1,{1,.72F,.26F,.9F},false};const auto dust=capture();
    scene.world_particles[0].emissive=true;const auto spark=capture();
    std::uint64_t dust_signal=0,spark_signal=0;
    for(std::size_t i=0;i<baseline.rgba8.size();i+=4) {
        if(dust.rgba8[i]>baseline.rgba8[i]) dust_signal+=dust.rgba8[i]-baseline.rgba8[i];
        if(spark.rgba8[i]>baseline.rgba8[i]) spark_signal+=spark.rgba8[i]-baseline.rgba8[i];
    }
    CHECK(spark_signal>dust_signal);scene.world_particles[0].center.x=-1;
    CHECK(capture().color_signature==baseline.color_signature);
    scene.world_particle_count=0;scene.world_line_count=1;
    scene.world_lines[0]={{5,4,8},{5,12,8},.65F,{1,.82F,.45F,.85F},.35F};
    const auto line=capture();scene.world_flash_count=1;
    scene.world_flashes[0]={scene.world_lines[0].end,1.25F,{1,.82F,.45F,.38F}};
    const auto glow=capture();CHECK(glow.color_signature!=line.color_signature);
    scene.world_line_count=0;scene.world_flashes[0].center.x=-1;
    CHECK(capture().color_signature==baseline.color_signature);
    scene.world_flash_count=0;CHECK(capture().color_signature==baseline.color_signature);
    CHECK(glGetError()==GL_NO_ERROR);context->release_renderer();
}

TEST_CASE("E11 particles require a supported static bullet hit but not decal or audio readiness",
          "[e11][world-impacts][particles]") {
    const auto world=wall();app::WorldImpactPresentation presenter;
    auto request=shot();request.particles=hlclient::games::halflife::bullet_impact_particles();
    REQUIRE(presenter.submit(request,collision(),world.get(),{},false,0.1).status==app::WorldImpactStatus::hit);
    presenter.update(0.1);REQUIRE(presenter.particles().size()==16U);
    for(const auto& p:presenter.particles()) {CHECK(p.center.x>0.0F);CHECK(p.center.x<0.3F);}
    CHECK(presenter.active()==0U); // Missing decals do not veto a burst.
    REQUIRE(presenter.submit(request,collision(),world.get(),{},false,0.1).status==app::WorldImpactStatus::hit);
    CHECK(presenter.particle_statistics().spawned==1U);CHECK(presenter.particle_statistics().duplicate==1U);
    presenter.update(0.5);CHECK(presenter.particles().empty());
    presenter.reset();
    auto miss=request;miss.direction={1,0,0};
    CHECK(presenter.submit(miss,collision(),world.get(),{},true,0.1).status==app::WorldImpactStatus::miss);
    CHECK(presenter.particle_statistics().spawned==0U);
    REQUIRE(presenter.submit(crowbar(),collision(),world.get(),{},true,0.1).status==app::WorldImpactStatus::hit);
    presenter.update(0.1);CHECK(presenter.particles().empty());
    const auto masked=wall(true);
    CHECK(presenter.submit(request,collision(),masked.get(),{},false,0.1).status==app::WorldImpactStatus::surface_unsupported);
    CHECK(presenter.particle_statistics().spawned==0U);
}
TEST_CASE("E11 deterministic particle pool is bounded replay-safe and independent of frame slices",
          "[e11][particles][bounds]") {
    renderer::ImpactParticlePool one,two;
    const auto profile=hlclient::games::halflife::bullet_impact_particles();const auto action=shot().action;
    REQUIRE(one.spawn(action,{0,8,8},{1,0,0},profile,0.1)==renderer::ParticleBurstStatus::spawned);
    REQUIRE(two.spawn(action,{0,8,8},{1,0,0},profile,0.1)==renderer::ParticleBurstStatus::spawned);
    one.update(0.2);two.update(0.12);two.update(0.17);two.update(0.2);
    CHECK(std::ranges::equal(one.particles(),two.particles()));
    const std::vector<renderer::RenderWorldParticle> before(one.particles().begin(),one.particles().end());
    one.update(0.2);CHECK(std::ranges::equal(one.particles(),before));
    for(const auto& p:one.particles()) {CHECK(std::isfinite(p.center.x));CHECK(std::isfinite(p.center.z));CHECK(p.center.x>0.125F);}
    auto remote=action;remote.emitter_entity=2U;
    REQUIRE(one.spawn(remote,{0,8,8},{1,0,0},profile,0.1)==renderer::ParticleBurstStatus::spawned);
    one.update(0.2);REQUIRE(one.particles().size()==32U);
    CHECK(one.particles()[0]!=one.particles()[16]);
    auto stale=action;stale.generation=2U;
    CHECK(one.spawn(stale,{0,8,8},{1,0,0},profile,0.1)==renderer::ParticleBurstStatus::stale_generation);
    for(unsigned shot_index=2U;shot_index<=40U;++shot_index) {
        auto next=action;next.command_sequence=shot_index;next.started_at_seconds=.2;
        REQUIRE(one.spawn(next,{0,float(shot_index),8},{1,0,0},profile,0.2)==renderer::ParticleBurstStatus::spawned);
    }
    one.update(0.2);CHECK(one.particles().size()==256U);CHECK(one.statistics().evicted==9U);
    for(const auto& p:one.particles()) CHECK(p.center.y>=9.0F); // Oldest nine bursts were evicted.
    one.update(0.5);CHECK(one.particles().empty());
    CHECK(one.spawn(action,{0,8,8},{1,0,0},profile,0.5)==renderer::ParticleBurstStatus::expired);
    one.reset();CHECK(one.particles().empty());
    CHECK(one.spawn(stale,{0,8,8},{1,0,0},profile,0.1)==renderer::ParticleBurstStatus::spawned);
    auto invalid=profile;invalid.count=17U;
    CHECK(two.spawn(action,{0,8,8},{1,0,0},invalid,0.1)==renderer::ParticleBurstStatus::invalid);
    CHECK(two.spawn(action,{0,8,8},{0,0,0},profile,0.1)==renderer::ParticleBurstStatus::invalid);
}
TEST_CASE("E11 production OpenGL draws short world particles without decals and preserves depth and cleanup",
          "[e11][particles][opengl][actual-context]") {
    auto context=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");context->initialize_renderer();
    const auto world=wall();app::WorldImpactPresentation presenter;
    renderer::RenderScene scene;scene.camera.position={25,8,8};scene.camera.target={0,8,8};
    scene.static_world=renderer::RenderStaticWorld{world};
    context->renderer().render(scene,{320,240});
    const auto baseline=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(baseline);
    auto request=shot();request.particles=hlclient::games::halflife::bullet_impact_particles();
    REQUIRE(presenter.submit(request,collision(),world.get(),{},false,0.1).status==app::WorldImpactStatus::hit);
    presenter.update(0.14);scene.world_particle_count=presenter.particles().size();
    std::ranges::copy(presenter.particles(),scene.world_particles.begin());
    context->renderer().render(scene,{320,240});
    const auto burst=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(burst);
    std::size_t changed=0U;
    for(std::size_t p=0;p<burst.rgba8.size()/4U;++p)
        if(!std::equal(burst.rgba8.begin()+p*4U,burst.rgba8.begin()+p*4U+3U,baseline.rgba8.begin()+p*4U)) ++changed;
    CHECK(changed>0U);CHECK(changed<320U*240U/16U); // particles, not a wall-sized quad
    CHECK_FALSE(scene.world_decals);
    // The same particles behind an opaque plane must not draw through it.
    for(std::size_t i=0;i<scene.world_particle_count;++i) scene.world_particles[i].center.x=-1.0F;
    context->renderer().render(scene,{320,240});
    const auto occluded=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(occluded);
    CHECK(occluded.color_signature==baseline.color_signature);
    presenter.update(0.5);scene.world_particle_count=presenter.particles().size();
    context->renderer().render(scene,{320,240});
    const auto cleanup=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(cleanup);
    CHECK(cleanup.color_signature==baseline.color_signature);CHECK(glGetError()==GL_NO_ERROR);
    context->release_renderer();
}
TEST_CASE("E15 production OpenGL tracer has bounded depth tested pixels and expiry",
          "[e15][tracer][opengl][actual-context]") {
    auto context=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();
    const auto world=wall();app::WorldImpactPresentation presenter;
    renderer::RenderScene scene;scene.camera.position={25,8,8};scene.camera.target={0,8,8};
    scene.static_world=renderer::RenderStaticWorld{world};
    context->renderer().render(scene,{320,240});
    const auto baseline=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(baseline);
    auto request=shot();request.tracer=api::LocalWorldImpactRequest::Tracer{};
    request.tracer->origin={5,4,8};request.suppress_surface_effects=true;
    request.particles=hlclient::games::halflife::bullet_impact_particles();
    REQUIRE(presenter.submit(request,collision(),world.get(),{},true,.1).status==app::WorldImpactStatus::hit);
    CHECK(presenter.active()==0);CHECK(presenter.particles().empty());
    (void)presenter.submit(request,collision(),world.get(),{},true,.1);
    presenter.append_lines(scene);REQUIRE(scene.world_line_count==1);
    context->renderer().render(scene,{320,240});
    const auto drawn=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(drawn);
    std::size_t changed=0;
    for(std::size_t p=0;p<drawn.rgba8.size()/4;++p)
      if(!std::equal(drawn.rgba8.begin()+p*4,drawn.rgba8.begin()+p*4+3,baseline.rgba8.begin()+p*4)) ++changed;
    CHECK(changed>0);CHECK(changed<320U*240U/16U);
    scene.world_lines[0].start.x=-1;scene.world_lines[0].end.x=-1;
    context->renderer().render(scene,{320,240});
    const auto hidden=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(hidden);
    CHECK(hidden.color_signature==baseline.color_signature);
    presenter.update(.2);scene.world_line_count=0;presenter.append_lines(scene);CHECK(scene.world_line_count==0);
    context->renderer().render(scene,{320,240});
    const auto expired=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(expired);
    CHECK(expired.color_signature==baseline.color_signature);CHECK(glGetError()==GL_NO_ERROR);
    presenter.reset();context->release_renderer();
}
TEST_CASE("E15 local tracer follows current muzzle without moving frozen hit", "[e15][tracer]") {
    app::WorldImpactPresentation presenter;const auto world=wall();auto request=shot();
    request.tracer=api::LocalWorldImpactRequest::Tracer{};
    request.tracer->follow_local_muzzle=true;request.tracer->origin={5,8,-100};
    request.tracer->lifetime_seconds=.03;
    request.suppress_surface_effects=true;
    REQUIRE(presenter.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::hit);
    renderer::RenderScene scene;presenter.append_lines(scene);CHECK(scene.world_line_count==0);
    presenter.append_lines(scene,app::WorldImpactPresentation::LocalMuzzleAnchor{1,59,{4,5,8}});
    REQUIRE(scene.world_line_count==1);CHECK(scene.world_lines[0].start.y==5);
    const auto end=scene.world_lines[0].end;scene.world_line_count=0;
    presenter.append_lines(scene,app::WorldImpactPresentation::LocalMuzzleAnchor{1,59,{3,6,12}});
    REQUIRE(scene.world_line_count==1);CHECK(scene.world_lines[0].start.z==12);
    CHECK(scene.world_lines[0].end.x==end.x);CHECK(scene.world_lines[0].end.y==end.y);
    scene.world_line_count=0;
    presenter.append_lines(scene,app::WorldImpactPresentation::LocalMuzzleAnchor{1,60,{3,6,12}});
    CHECK(scene.world_line_count==0);
    presenter.update(.129);presenter.append_lines(scene,app::WorldImpactPresentation::LocalMuzzleAnchor{1,59,{3,6,12}});
    CHECK(scene.world_line_count==1);scene.world_line_count=0;
    presenter.update(.131);presenter.append_lines(scene,app::WorldImpactPresentation::LocalMuzzleAnchor{1,59,{3,6,12}});
    CHECK(scene.world_line_count==0);
}
TEST_CASE("E15 debug frozen solid tracer owns one original world-space sample until reset",
          "[e15][tracer][tracer-freeze]") {
    const auto world=wall();auto request=shot();request.origin={2000,8,8};request.maximum_distance_units=4000;
    request.suppress_surface_effects=true;request.tracer=api::LocalWorldImpactRequest::Tracer{};
    auto& tracer=*request.tracer;tracer.origin=request.origin;tracer.width_units=.65F;
    tracer.travel_speed_units_per_second=12000;tracer.segment_length_units=48;tracer.lifetime_seconds=.078;
    tracer.head_taper_fraction=.90F;tracer.head_glow={1.25F,48,.35F};tracer.start_offset_units=8;
    tracer.taper_end_diameter_fraction=.20F;
    tracer.geometry=api::WorldLineGeometry::solid_3d;
    SECTION("local actual muzzle captured once") { tracer.follow_local_muzzle=true; }
    SECTION("remote committed world muzzle") { tracer.follow_local_muzzle=false;request.action.emitter_entity=2; }
    app::WorldImpactPresentation owner{true};
    REQUIRE(owner.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::hit);
    renderer::RenderScene scene;
    if(tracer.follow_local_muzzle) {
        owner.append_lines(scene);CHECK(scene.world_line_count==0);
        owner.append_lines(scene,app::WorldImpactPresentation::LocalMuzzleAnchor{1,60,{2000,8,8}});
        CHECK(scene.world_line_count==0);
    }
    owner.append_lines(scene,app::WorldImpactPresentation::LocalMuzzleAnchor{1,59,{2000,8,8}});
    REQUIRE(scene.world_line_count==1);const auto frozen=scene.world_lines[0];
    CHECK(frozen.start.x==Approx(1992));CHECK(frozen.end.x==Approx(1944));
    CHECK(frozen.width_units==tracer.width_units);CHECK(frozen.color==tracer.color);
    CHECK(frozen.head_taper_fraction==tracer.head_taper_fraction);CHECK(frozen.geometry==tracer.geometry);
    CHECK(frozen.taper_end_diameter_fraction==Approx(.20));
    CHECK(scene.world_flash_count==0);CHECK(scene.world_effect_light_count==0);
    for(const double time:{.11,.2,10.0,86400.0,12.0}) {
        owner.update(time);scene={};
        owner.append_lines(scene,app::WorldImpactPresentation::LocalMuzzleAnchor{1,60,{3000,50,100}});
        REQUIRE(scene.world_line_count==1);CHECK(same_position(scene.world_lines[0].start,frozen.start));
        CHECK(same_position(scene.world_lines[0].end,frozen.end));CHECK(scene.world_lines[0].color==frozen.color);
        CHECK(scene.world_flash_count==0);CHECK(scene.world_effect_light_count==0);
    }
    owner.reset();scene={};owner.append_lines(scene);CHECK(scene.world_line_count==0);
    REQUIRE(owner.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::hit);
    owner.append_lines(scene,app::WorldImpactPresentation::LocalMuzzleAnchor{1,59,{2000,8,8}});
    REQUIRE(scene.world_line_count==1);owner.update(10);scene={};owner.append_lines(scene);
    CHECK(scene.world_line_count==1); // reset clears sample/identity but retains configured debug mode
}

TEST_CASE("E15 debug frozen tracer preserves dedup bounds pool wrap clipping and normal defaults",
          "[e15][tracer][tracer-freeze]") {
    const auto world=wall();auto request=shot();request.suppress_surface_effects=true;
    request.tracer=api::LocalWorldImpactRequest::Tracer{};auto& tracer=*request.tracer;
    tracer.origin=request.origin;tracer.travel_speed_units_per_second=12000;tracer.segment_length_units=32;
    tracer.lifetime_seconds=.078;tracer.geometry=api::WorldLineGeometry::solid_3d;
    app::WorldImpactPresentation owner{true};renderer::RenderScene scene;
    const auto submit=[&] {return owner.submit(request,collision(),world.get(),{},false,.1);};
    SECTION("bounded latest sample even after submission ring wrap and repeats") {
        for(unsigned i=1;i<=70;++i) {
            request.action.command_sequence=i;tracer.origin={5,float(i),8};REQUIRE(submit().point);
        }
        owner.append_lines(scene);REQUIRE(scene.world_line_count==1);CHECK(scene.world_lines[0].start.y==70);
        const auto frozen=scene.world_lines[0];
        REQUIRE(submit().point);scene={};owner.append_lines(scene);REQUIRE(scene.world_line_count==1);
        CHECK(same_position(scene.world_lines[0].start,frozen.start));CHECK(same_position(scene.world_lines[0].end,frozen.end));
        request.tracer.reset();REQUIRE(submit().point);scene={};owner.append_lines(scene);
        REQUIRE(scene.world_line_count==1);CHECK(same_position(scene.world_lines[0].start,frozen.start));
        owner.update(10);scene.world_line_count=scene.world_lines.size();owner.append_lines(scene);
        CHECK(scene.world_line_count==scene.world_lines.size()); // no overflow at output capacity
    }
    SECTION("near hit clips, offset beyond hit cannot fabricate a persistent cone") {
        const auto result=submit();REQUIRE(result.point);owner.append_lines(scene);REQUIRE(scene.world_line_count==1);
        CHECK(scene.world_lines[0].end.x==Approx(result.point->x));
        owner.reset();tracer.start_offset_units=16;REQUIRE(submit().point);scene={};owner.append_lines(scene);
        owner.update(10);owner.append_lines(scene);CHECK(scene.world_line_count==0);
    }
    SECTION("no actual local anchor before deadline leaves no inspection sample") {
        tracer.follow_local_muzzle=true;REQUIRE(submit().point);owner.append_lines(scene);
        owner.update(.2);owner.append_lines(scene,app::WorldImpactPresentation::LocalMuzzleAnchor{1,59,{5,8,8}});
        CHECK(scene.world_line_count==0);
    }
    SECTION("invalid trace and missing collision do not create debug samples") {
        CHECK(owner.submit(request,{},world.get(),{},false,.1).status==app::WorldImpactStatus::collision_unavailable);
        request.origin.x=-5;CHECK(submit().status==app::WorldImpactStatus::start_solid);
        owner.append_lines(scene);CHECK(scene.world_line_count==0);
    }
    SECTION("default still flies and expires") {
        app::WorldImpactPresentation ordinary;request.origin={2000,8,8};request.maximum_distance_units=4000;
        tracer.origin=request.origin;REQUIRE(ordinary.submit(request,collision(),world.get(),{},false,.1).point);
        ordinary.append_lines(scene);REQUIRE(scene.world_line_count==1);CHECK(scene.world_lines[0].start.x==2000);
        ordinary.update(.11);scene={};ordinary.append_lines(scene);REQUIRE(scene.world_line_count==1);
        CHECK(scene.world_lines[0].start.x==Approx(1880));ordinary.update(.2);scene={};ordinary.append_lines(scene);
        CHECK(scene.world_line_count==0);
    }
    SECTION("flat and zero-motion callers keep their own expiry in debug mode") {
        tracer.geometry=api::WorldLineGeometry::flat_billboard;REQUIRE(submit().point);
        owner.append_lines(scene);REQUIRE(scene.world_line_count==1);owner.update(.2);scene={};owner.append_lines(scene);
        CHECK(scene.world_line_count==0);owner.reset();
        tracer.geometry=api::WorldLineGeometry::solid_3d;tracer.travel_speed_units_per_second=tracer.segment_length_units=0;
        REQUIRE(submit().point);scene={};owner.append_lines(scene);REQUIRE(scene.world_line_count==1);
        owner.update(.2);scene={};owner.append_lines(scene);CHECK(scene.world_line_count==0);
    }
}

TEST_CASE("E15 frozen production owner remains visible in GL from all sides after the flight deadline",
          "[e15][tracer][tracer-freeze][opengl][actual-context]") {
    auto context=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");context->initialize_renderer();
    const auto world=wall();auto request=shot();request.suppress_surface_effects=true;
    request.direction={0,1,0};request.maximum_distance_units=100;
    request.tracer=api::LocalWorldImpactRequest::Tracer{};auto& tracer=*request.tracer;
    tracer.origin=request.origin;tracer.geometry=api::WorldLineGeometry::solid_3d;
    tracer.width_units=.65F;tracer.head_taper_fraction=.35F;tracer.travel_speed_units_per_second=12000;
    tracer.segment_length_units=32;tracer.lifetime_seconds=.078;tracer.head_glow={1.25F,48,.35F};
    app::WorldImpactPresentation owner{true};
    REQUIRE(owner.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::miss);
    renderer::RenderScene scene;owner.append_lines(scene);REQUIRE(scene.world_line_count==1);
    const auto pinned=scene.world_lines[0];owner.update(1000);
    const auto capture=[&] {
        context->renderer().render(scene,{320,240});
        auto frame=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(frame);return frame;
    };
    const std::array<assets::AssetVector3,6> eyes{{{5,-24,8},{5,72,8},{45,24,8},{-35,24,8},{5,24,48},{35,54,38}}};
    for(std::size_t i=0;i<eyes.size();++i) {
        CAPTURE(i);scene={};scene.camera.position=eyes[i];scene.camera.target={5,24,8};
        scene.camera.up=i==4 ? assets::AssetVector3{0,1,0}:assets::AssetVector3{0,0,1};
        const auto baseline=capture();owner.append_lines(scene);REQUIRE(scene.world_line_count==1);
        CHECK(same_position(scene.world_lines[0].start,pinned.start));CHECK(same_position(scene.world_lines[0].end,pinned.end));
        const auto visible=capture();CHECK(visible.color_signature!=baseline.color_signature);
        CHECK(scene.world_flash_count==0);CHECK(scene.world_effect_light_count==0);
        scene.world_line_count=0;owner.append_lines(scene);CHECK(capture().rgba8==visible.rgba8);
        scene.world_line_count=0;CHECK(capture().rgba8==baseline.rgba8);
    }
    scene={};scene.camera.position={25,24,8};scene.camera.target={0,24,8};scene.static_world=renderer::RenderStaticWorld{world};
    // Synthetic wall spans y=0..16: inspect occlusion at y=12 instead of the long segment midpoint.
    scene.camera.position={25,12,8};scene.camera.target={0,12,8};const auto baseline=capture();
    owner.append_lines(scene);scene.world_lines[0].start={-1,8,8};scene.world_lines[0].end={-1,16,8};
    CHECK(capture().rgba8==baseline.rgba8);
    owner.reset();scene.world_line_count=0;owner.append_lines(scene);CHECK(scene.world_line_count==0);
    CHECK(capture().rgba8==baseline.rgba8);CHECK(glGetError()==GL_NO_ERROR);context->release_renderer();
}

TEST_CASE("E15 moving local streak launches once and travels independently of the gun", "[e15][tracer]") {
    app::WorldImpactPresentation presenter;const auto world=wall();auto request=shot();
    request.origin={1000,8,8};request.maximum_distance_units=2000;
    request.tracer=api::LocalWorldImpactRequest::Tracer{};
    request.tracer->follow_local_muzzle=true;request.tracer->origin={5,8,-100};
    request.tracer->lifetime_seconds=.03;
    request.tracer->travel_speed_units_per_second=12000;request.tracer->segment_length_units=24;
    request.suppress_surface_effects=true;
    const auto hit=presenter.submit(request,collision(),world.get(),{},false,.1);
    REQUIRE(hit.status==app::WorldImpactStatus::hit);REQUIRE(hit.point);
    renderer::RenderScene scene;presenter.append_lines(scene);CHECK(scene.world_line_count==0);
    presenter.append_lines(scene,app::WorldImpactPresentation::LocalMuzzleAnchor{1,60,{1000,8,8}});
    CHECK(scene.world_line_count==0); // an old/fallback model must not launch it
    presenter.append_lines(scene,app::WorldImpactPresentation::LocalMuzzleAnchor{1,59,{1000,8,8}});
    REQUIRE(scene.world_line_count==1);
    CHECK(scene.world_lines[0].start.x==Approx(1000));CHECK(scene.world_lines[0].end.x==Approx(976));
    scene.world_line_count=0;presenter.update(.11);
    // Switching/moving the gun after launch cannot drag a flying streak around.
    presenter.append_lines(scene,app::WorldImpactPresentation::LocalMuzzleAnchor{1,60,{2000,500,100}});
    REQUIRE(scene.world_line_count==1);
    CHECK(scene.world_lines[0].start.x==Approx(880));CHECK(scene.world_lines[0].end.x==Approx(856));
    CHECK(scene.world_lines[0].start.y==8);CHECK(scene.world_lines[0].start.z==8);
    const auto alpha=scene.world_lines[0].color[3];CHECK(alpha>0);CHECK(alpha<request.tracer->color[3]);
    scene.world_line_count=0;
    (void)presenter.submit(request,collision(),world.get(),{},false,.11); // replay does not restart it
    presenter.update(.105); // backwards render timestamps cannot rewind flight
    presenter.append_lines(scene);REQUIRE(scene.world_line_count==1);
    CHECK(scene.world_lines[0].start.x==Approx(880));CHECK(scene.world_lines[0].color[3]==alpha);
    scene.world_line_count=0;presenter.append_lines(scene);REQUIRE(scene.world_line_count==1);
    CHECK(scene.world_lines[0].end.x==Approx(856));
    scene.world_line_count=0;presenter.update(.129);presenter.append_lines(scene);
    REQUIRE(scene.world_line_count==1);CHECK(scene.world_lines[0].start.x==Approx(652));
    CHECK(scene.world_lines[0].end.x==Approx(628));CHECK(scene.world_lines[0].color[3]>0);
    scene.world_line_count=0;presenter.update(.131);presenter.append_lines(scene);CHECK(scene.world_line_count==0);
    presenter.reset();presenter.update(.1);presenter.append_lines(scene);CHECK(scene.world_line_count==0);
    REQUIRE(presenter.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::hit);
    presenter.append_lines(scene,app::WorldImpactPresentation::LocalMuzzleAnchor{1,59,{1000,8,8}});
    REQUIRE(scene.world_line_count==1);CHECK(scene.world_lines[0].start.x==Approx(1000));
    CHECK(scene.world_lines[0].end.x==Approx(976));
    CHECK(scene.world_lines[0].color[3]==Approx(request.tracer->color[3])); // map reset also resets clock
}
TEST_CASE("E15 moving streak clips at the frozen hit and independent emitters do not share a clock", "[e15][tracer]") {
    const auto world=wall();auto request=shot();request.origin={50,8,8};request.maximum_distance_units=100;
    request.tracer=api::LocalWorldImpactRequest::Tracer{};request.tracer->origin=request.origin;
    request.tracer->lifetime_seconds=.03;request.tracer->travel_speed_units_per_second=12000;
    request.tracer->segment_length_units=24;request.suppress_surface_effects=true;
    SECTION("near wall ends at collision, never passes through it") {
        app::WorldImpactPresentation presenter;
        const auto hit=presenter.submit(request,collision(),world.get(),{},false,.1);REQUIRE(hit.point);
        presenter.update(.103);renderer::RenderScene scene;presenter.append_lines(scene);
        REQUIRE(scene.world_line_count==1);CHECK(scene.world_lines[0].start.x==Approx(14));
        CHECK(scene.world_lines[0].end.x==Approx(hit.point->x).margin(.001));
        scene.world_line_count=0;presenter.update(.1042);presenter.append_lines(scene);
        CHECK(scene.world_line_count==0); // tail reached the surface before the 30ms cap
    }
    SECTION("remote emitters retain independent launch times") {
        app::WorldImpactPresentation presenter;request.origin={1000,8,8};request.maximum_distance_units=2000;
        request.tracer->origin=request.origin;request.action.emitter_entity=2;
        REQUIRE(presenter.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::hit);
        request.action.emitter_entity=3;request.action.started_at_seconds=.11;
        REQUIRE(presenter.submit(request,collision(),world.get(),{},false,.11).status==app::WorldImpactStatus::hit);
        presenter.update(.12);renderer::RenderScene scene;presenter.append_lines(scene);
        REQUIRE(scene.world_line_count==2);CHECK(scene.world_lines[0].start.x==Approx(760));
        CHECK(scene.world_lines[1].start.x==Approx(880));
    }
    SECTION("invalid motion profile is suppressed without blocking the hit") {
        for(const auto pair:std::array<std::array<float,2>,6>{{{0,24},{12000,0},{-1,24},{NAN,24},{12000,NAN},{100001,24}}}) {
            app::WorldImpactPresentation presenter;
            request.tracer->travel_speed_units_per_second=pair[0];request.tracer->segment_length_units=pair[1];
            CHECK(presenter.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::hit);
            renderer::RenderScene scene;presenter.append_lines(scene);CHECK(scene.world_line_count==0);
        }
    }
    SECTION("invalid tip profile is suppressed without blocking the hit") {
        for(const auto tip:std::array<float,3>{-.1F,1.1F,NAN}) {
            app::WorldImpactPresentation presenter;request.tracer->head_taper_fraction=tip;
            CHECK(presenter.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::hit);
            renderer::RenderScene scene;presenter.append_lines(scene);CHECK(scene.world_line_count==0);
        }
    }
    SECTION("invalid terminal diameter is suppressed without blocking the hit") {
        for(const auto diameter:std::array<float,4>{-.1F,1.1F,NAN,INFINITY}) {
            app::WorldImpactPresentation presenter;request.tracer->taper_end_diameter_fraction=diameter;
            CHECK(presenter.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::hit);
            renderer::RenderScene scene;presenter.append_lines(scene);CHECK(scene.world_line_count==0);
        }
    }
}
TEST_CASE("E15 60ms moving streak survives the old deadline and expires at the doubled cap", "[e15][tracer]") {
    app::WorldImpactPresentation presenter;const auto world=wall();auto request=shot();
    request.origin={2000,8,8};request.maximum_distance_units=4000;
    request.tracer=api::LocalWorldImpactRequest::Tracer{};request.tracer->origin=request.origin;
    request.tracer->lifetime_seconds=.06;request.tracer->travel_speed_units_per_second=12000;
    request.tracer->segment_length_units=24;request.suppress_surface_effects=true;
    SECTION("local current-muzzle launch") { request.tracer->follow_local_muzzle=true; }
    SECTION("remote world-space launch") { request.tracer->follow_local_muzzle=false; }
    REQUIRE(presenter.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::hit);
    renderer::RenderScene scene;
    presenter.append_lines(scene,app::WorldImpactPresentation::LocalMuzzleAnchor{1,59,request.origin});
    REQUIRE(scene.world_line_count==1);CHECK(scene.world_lines[0].end.x==Approx(1976));
    scene.world_line_count=0;presenter.update(.131);presenter.append_lines(scene);
    REQUIRE(scene.world_line_count==1);CHECK(scene.world_lines[0].start.x==Approx(1628));
    CHECK(scene.world_lines[0].color[3]==Approx(request.tracer->color[3]*29.0/60.0));
    scene.world_line_count=0;presenter.update(.159);presenter.append_lines(scene);
    REQUIRE(scene.world_line_count==1);CHECK(scene.world_lines[0].start.x==Approx(1292));
    CHECK(scene.world_lines[0].end.x==Approx(1268));CHECK(scene.world_lines[0].color[3]>0);
    scene.world_line_count=0;presenter.update(.161);presenter.append_lines(scene);CHECK(scene.world_line_count==0);
}
TEST_CASE("E15 production OpenGL shows a short moving streak rather than the full laser path",
          "[e15][tracer][opengl][actual-context]") {
    auto context=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();const auto world=wall();app::WorldImpactPresentation presenter;
    renderer::RenderScene scene;scene.camera.position={25,8,8};scene.camera.target={0,8,8};
    scene.static_world=renderer::RenderStaticWorld{world};
    context->renderer().render(scene,{320,240});
    const auto baseline=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(baseline);
    auto request=shot();request.tracer=api::LocalWorldImpactRequest::Tracer{};
    request.tracer->origin={5,4,8};request.tracer->lifetime_seconds=.03;
    // Smaller fixture units keep two positions visible in this 16-unit test wall.
    request.tracer->travel_speed_units_per_second=200;request.tracer->segment_length_units=1;
    request.suppress_surface_effects=true;
    REQUIRE(presenter.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::hit);
    presenter.update(.102);presenter.append_lines(scene);REQUIRE(scene.world_line_count==1);
    const auto first_start=scene.world_lines[0].start;
    context->renderer().render(scene,{320,240});
    const auto first=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(first);
    scene.world_line_count=0;presenter.update(.114);presenter.append_lines(scene);REQUIRE(scene.world_line_count==1);
    CHECK(scene.world_lines[0].start.x<first_start.x);
    context->renderer().render(scene,{320,240});
    const auto second=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(second);
    CHECK(first.color_signature!=second.color_signature);
    std::array<double,2> centroids{};std::size_t frame_index=0;
    for(const auto* frame:{&first,&second}) {
        std::size_t changed=0;double sum_x=0;
        for(std::size_t p=0;p<frame->rgba8.size()/4;++p)
            if(!std::equal(frame->rgba8.begin()+p*4,frame->rgba8.begin()+p*4+3,baseline.rgba8.begin()+p*4)) {
                ++changed;sum_x+=static_cast<double>(p%320U);
            }
        REQUIRE(changed>0);CHECK(changed<320U*240U/100U);
        centroids[frame_index++]=sum_x/static_cast<double>(changed);
    }
    CHECK(std::abs(centroids[1]-centroids[0])>5); // changing fade alone must not pass the movement proof
    scene.world_lines[0].start.x=-1;scene.world_lines[0].end.x=-1;
    context->renderer().render(scene,{320,240});
    const auto hidden=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(hidden);
    CHECK(hidden.color_signature==baseline.color_signature);
    scene.world_line_count=0;presenter.update(.131);presenter.append_lines(scene);CHECK(scene.world_line_count==0);
    context->renderer().render(scene,{320,240});
    const auto expired=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(expired);
    CHECK(expired.color_signature==baseline.color_signature);CHECK(glGetError()==GL_NO_ERROR);
    presenter.reset();context->release_renderer();
}
TEST_CASE("E15 pointed moving streak survives 77ms and carries the neutral tip profile", "[e15][tracer]") {
    app::WorldImpactPresentation presenter;const auto world=wall();auto request=shot();
    request.origin={2000,8,8};request.maximum_distance_units=4000;
    request.tracer=api::LocalWorldImpactRequest::Tracer{};request.tracer->origin=request.origin;
    request.tracer->lifetime_seconds=.078;request.tracer->travel_speed_units_per_second=12000;
    request.tracer->segment_length_units=24;request.tracer->head_taper_fraction=.35F;
    request.suppress_surface_effects=true;
    SECTION("local launch") { request.tracer->follow_local_muzzle=true; }
    SECTION("remote launch") { request.action.emitter_entity=2; }
    REQUIRE(presenter.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::hit);
    renderer::RenderScene scene;
    presenter.append_lines(scene,app::WorldImpactPresentation::LocalMuzzleAnchor{1,59,request.origin});
    REQUIRE(scene.world_line_count==1);CHECK(scene.world_lines[0].head_taper_fraction==Approx(.35));
    scene.world_line_count=0;presenter.update(.161);presenter.append_lines(scene);
    REQUIRE(scene.world_line_count==1);CHECK(scene.world_lines[0].start.x==Approx(1268));
    CHECK(scene.world_lines[0].color[3]==Approx(request.tracer->color[3]*17.0/78.0));
    scene.world_line_count=0;presenter.update(.177);presenter.append_lines(scene);
    REQUIRE(scene.world_line_count==1);CHECK(scene.world_lines[0].start.x==Approx(1076));
    CHECK(scene.world_lines[0].end.x==Approx(1052));CHECK(scene.world_lines[0].color[3]>0);
    CHECK(scene.world_lines[0].head_taper_fraction==Approx(.35));
    scene.world_line_count=0;
    presenter.update(request.action.started_at_seconds+request.tracer->lifetime_seconds);
    presenter.append_lines(scene);CHECK(scene.world_line_count==0);
}
TEST_CASE("E15 production OpenGL tapers only the leading end and restores rectangular draws",
          "[e15][tracer][opengl][actual-context]") {
    auto context=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();const auto world=wall();
    renderer::RenderScene scene;scene.camera.position={25,8,8};scene.camera.target={0,8,8};
    scene.static_world=renderer::RenderStaticWorld{world};
    const auto capture=[&] {
        context->renderer().render(scene,{320,240});
        auto frame=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);
        REQUIRE(frame);return frame;
    };
    const auto baseline=capture();
    scene.world_line_count=1;scene.world_lines[0]={{5,4,8},{5,12,8},2,{1,.82F,.45F,.85F}};
    const auto flat=capture();
    const auto column=[&](const auto& frame,std::size_t x) {
        std::size_t changed=0;
        for(std::size_t y=0;y<240;++y) {
            const auto p=(y*320+x)*4;
            if(!std::equal(frame.rgba8.begin()+p,frame.rgba8.begin()+p+3,baseline.rgba8.begin()+p)) ++changed;
        }
        return changed;
    };
    std::size_t left=320,right=0;
    for(std::size_t x=0;x<320;++x) if(column(flat,x)>0) {left=std::min(left,x);right=std::max(right,x);}
    REQUIRE(left<right);REQUIRE(right-left>60);
    const auto body=left+(right-left)/4,head=left+(right-left)*9/10,reverse_head=left+(right-left)/10;
    scene.world_lines[0].head_taper_fraction=.35F;const auto pointed=capture();
    CHECK(column(pointed,body)==column(flat,body));
    CHECK(column(pointed,head)>0);CHECK(column(pointed,head)*2<column(flat,head));
    // In this camera +Y is screen-right: reversing direction must move the point left.
    std::swap(scene.world_lines[0].start,scene.world_lines[0].end);const auto reversed=capture();
    CHECK(column(reversed,head)==column(flat,head));
    CHECK(column(reversed,reverse_head)>0);CHECK(column(reversed,reverse_head)*2<column(flat,reverse_head));
    scene.world_lines[0].head_taper_fraction=0;const auto flat_again=capture();
    CHECK(flat_again.color_signature==flat.color_signature); // no stale uniform
    for(const auto invalid:std::array<float,3>{-.1F,1.1F,NAN}) {
        scene.world_lines[0].head_taper_fraction=invalid;const auto rejected=capture();
        CHECK(rejected.color_signature==baseline.color_signature);
    }
    scene.world_lines[0].head_taper_fraction=.35F;
    scene.world_lines[0].start.x=scene.world_lines[0].end.x=-1;const auto hidden=capture();
    CHECK(hidden.color_signature==baseline.color_signature);
    scene.world_line_count=0;const auto cleanup=capture();CHECK(cleanup.color_signature==baseline.color_signature);
    CHECK(glGetError()==GL_NO_ERROR);context->release_renderer();
}
TEST_CASE("E15 solid geometry retains the exact flat owner's flight fade glow and expiry",
          "[e15][tracer][tracer-3d]") {
    const auto world=wall();auto request=shot();
    request.origin={2000,8,8};request.maximum_distance_units=4000;
    request.tracer=api::LocalWorldImpactRequest::Tracer{};request.tracer->origin=request.origin;
    request.tracer->width_units=1.30F;request.tracer->lifetime_seconds=.078;
    request.tracer->travel_speed_units_per_second=12000;request.tracer->segment_length_units=32;
    request.tracer->head_taper_fraction=.35F;request.tracer->head_glow={1.25F,48,.35F};
    request.suppress_surface_effects=true;
    SECTION("local current muzzle") {request.tracer->follow_local_muzzle=true;}
    SECTION("remote committed muzzle") {request.action.emitter_entity=2;}
    app::WorldImpactPresentation flat,solid;
    REQUIRE(flat.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::hit);
    request.tracer->geometry=api::WorldLineGeometry::solid_3d;
    REQUIRE(solid.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::hit);
    // Exact duplicate cannot restart either geometry or add another light.
    REQUIRE(solid.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::hit);
    for(const double now:{.1,.11,.161,.177,.178,.2}) {
        flat.update(now);solid.update(now);renderer::RenderScene a,b;
        const app::WorldImpactPresentation::LocalMuzzleAnchor anchor{1,59,request.origin};
        flat.append_lines(a,anchor);solid.append_lines(b,anchor);
        REQUIRE(a.world_line_count==b.world_line_count);
        CHECK(a.world_flash_count==b.world_flash_count);
        CHECK(a.world_effect_light_count==b.world_effect_light_count);
        if(a.world_line_count) {
            const auto& old=a.world_lines[0];const auto& next=b.world_lines[0];
            CHECK(next.geometry==api::WorldLineGeometry::solid_3d);
            CHECK(old.geometry==api::WorldLineGeometry::flat_billboard);
            CHECK(old.start.x==next.start.x);CHECK(old.start.y==next.start.y);CHECK(old.start.z==next.start.z);
            CHECK(old.end.x==next.end.x);CHECK(old.end.y==next.end.y);CHECK(old.end.z==next.end.z);
            CHECK(old.width_units==next.width_units);CHECK(old.color==next.color);
            CHECK(old.head_taper_fraction==next.head_taper_fraction);
            CHECK(a.world_flashes[0].center.x==b.world_flashes[0].center.x);
            CHECK(a.world_flashes[0].color==b.world_flashes[0].color);
            CHECK(a.world_effect_lights[0].intensity==b.world_effect_lights[0].intensity);
        }
    }
    flat.reset();solid.reset();renderer::RenderScene cleared;
    solid.append_lines(cleared);CHECK(cleared.world_line_count==0);CHECK(cleared.world_flash_count==0);
    request.action.command_sequence++;
    request.tracer->geometry=static_cast<api::WorldLineGeometry>(255);
    REQUIRE(solid.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::hit);
    solid.append_lines(cleared);CHECK(cleared.world_line_count==0); // invalid geometry is not a flat fallback
}
TEST_CASE("E15 per-action forward start is measured from the actual muzzle once without changing flight",
          "[e15][tracer][tracer-start]") {
    const auto world=wall();
    for(const auto geometry:{api::WorldLineGeometry::flat_billboard,api::WorldLineGeometry::solid_3d})
    for(const bool local:{false,true}) for(const float offset:{0.0F,8.0F,16.0F}) {
        CAPTURE(local,offset,static_cast<unsigned>(geometry));
        app::WorldImpactPresentation presenter;auto request=shot();
        request.origin={2000,8,8};request.maximum_distance_units=4000;
        request.tracer=api::LocalWorldImpactRequest::Tracer{};auto& tracer=*request.tracer;
        tracer.origin=request.origin;tracer.follow_local_muzzle=local;
        tracer.width_units=local ? .65F:1.30F;tracer.start_offset_units=offset;
        tracer.lifetime_seconds=.078;tracer.travel_speed_units_per_second=12000;
        tracer.segment_length_units=32;tracer.head_taper_fraction=.35F;
        tracer.head_glow={1.25F,48,.35F};tracer.geometry=geometry;
        request.suppress_surface_effects=true;
        const auto hit=presenter.submit(request,collision(),world.get(),{},false,.1);
        REQUIRE(hit.status==app::WorldImpactStatus::hit);REQUIRE(hit.point);
        const app::WorldImpactPresentation::LocalMuzzleAnchor anchor{1,59,{1000,8,8}};
        if(local) {
            renderer::RenderScene unavailable;
            presenter.append_lines(unavailable);CHECK(unavailable.world_line_count==0);
            presenter.append_lines(unavailable,app::WorldImpactPresentation::LocalMuzzleAnchor{2,59,anchor.point});
            presenter.append_lines(unavailable,app::WorldImpactPresentation::LocalMuzzleAnchor{1,60,anchor.point});
            CHECK(unavailable.world_line_count==0);CHECK(unavailable.world_flash_count==0);
        }
        const auto launch=local ? 1000.0F:2000.0F;
        renderer::RenderScene initial;presenter.append_lines(initial,anchor);
        REQUIRE(initial.world_line_count==1);
        CHECK(initial.world_lines[0].start.x==Approx(launch-offset));
        CHECK(initial.world_lines[0].end.x==Approx(launch-offset-32));
        CHECK(initial.world_lines[0].width_units==Approx(local ? .65:1.30));
        CHECK(initial.world_lines[0].geometry==geometry);
        CHECK(initial.world_lines[0].color==tracer.color);
        REQUIRE(initial.world_flash_count==1);REQUIRE(initial.world_effect_light_count==1);
        CHECK(initial.world_flashes[0].center.x==initial.world_lines[0].end.x);
        CHECK(initial.world_flashes[0].radius_units==Approx(1.25));
        CHECK(initial.world_effect_lights[0].radius_units==48);
        // A repeated action with a different offset must not re-roll/restart it.
        auto duplicate=request;duplicate.tracer->start_offset_units=1024;
        REQUIRE(presenter.submit(duplicate,collision(),world.get(),{},false,.105).point);
        presenter.update(.11);
        renderer::RenderScene moved;
        presenter.append_lines(moved,app::WorldImpactPresentation::LocalMuzzleAnchor{1,59,{1500,12,12}});
        REQUIRE(moved.world_line_count==1);
        CHECK(moved.world_lines[0].start.x==Approx(launch-offset-120));
        CHECK(moved.world_lines[0].end.x==Approx(launch-offset-152));
        CHECK(moved.world_lines[0].start.y==8);CHECK(moved.world_lines[0].start.z==8);
        CHECK(moved.world_lines[0].color[3]==Approx(tracer.color[3]*68.0/78.0));
        presenter.update(.102); // no rewind and no second application of the offset
        renderer::RenderScene repeated;presenter.append_lines(repeated);
        REQUIRE(repeated.world_line_count==1);
        CHECK(repeated.world_lines[0].start.x==moved.world_lines[0].start.x);
        CHECK(repeated.world_lines[0].end.x==moved.world_lines[0].end.x);
        presenter.update(.178);renderer::RenderScene expired;presenter.append_lines(expired);
        CHECK(expired.world_line_count==0);CHECK(expired.world_flash_count==0);
        presenter.reset();presenter.append_lines(expired);CHECK(expired.world_line_count==0);
        REQUIRE(presenter.submit(request,collision(),world.get(),{},false,.1).point);
        presenter.append_lines(expired,anchor);REQUIRE(expired.world_line_count==1);
        CHECK(expired.world_lines[0].start.x==Approx(launch-offset));
    }
}
TEST_CASE("E15 forward start keeps the immutable hit bounds and supports stationary neutral lines",
          "[e15][tracer][tracer-start]") {
    const auto world=wall();auto request=shot();request.origin={50,8,8};request.maximum_distance_units=100;
    request.tracer=api::LocalWorldImpactRequest::Tracer{};auto& tracer=*request.tracer;
    tracer.origin=request.origin;tracer.start_offset_units=16;tracer.lifetime_seconds=.078;
    tracer.travel_speed_units_per_second=12000;tracer.segment_length_units=32;
    tracer.head_glow={1.25F,48,.35F};
    SECTION("offset plus flight clips to the hit, without changing decal or trace") {
        app::WorldImpactPresentation presenter;
        const auto hit=presenter.submit(request,collision(),world.get(),{},true,.1);REQUIRE(hit.point);
        REQUIRE(presenter.active()==1);CHECK(hit.point->x==Approx(0).margin(.2));
        renderer::RenderScene initial;presenter.append_lines(initial);REQUIRE(initial.world_line_count==1);
        CHECK(initial.world_lines[0].start.x==Approx(34));CHECK(initial.world_lines[0].end.x==Approx(2));
        presenter.update(.101);renderer::RenderScene clipped;presenter.append_lines(clipped);
        REQUIRE(clipped.world_line_count==1);CHECK(clipped.world_lines[0].start.x==Approx(22));
        CHECK(clipped.world_lines[0].end.x==Approx(hit.point->x));
        presenter.update(.103);renderer::RenderScene arrived;presenter.append_lines(arrived);
        CHECK(arrived.world_line_count==0);CHECK(arrived.world_flash_count==0);
        CHECK(arrived.world_effect_light_count==0);CHECK(presenter.active()==1);
    }
    SECTION("hit at or before chosen start has no line, glow or artificial overshoot") {
        for(const auto origin:std::array<assets::AssetVector3,2>{{{5,8,8},{16,8,8}}}) {
            app::WorldImpactPresentation presenter;request.origin=origin;tracer.origin=origin;
            const auto hit=presenter.submit(request,collision(),world.get(),{},true,.1);REQUIRE(hit.point);
            renderer::RenderScene scene;presenter.append_lines(scene);
            CHECK(scene.world_line_count==0);CHECK(scene.world_flash_count==0);CHECK(scene.world_effect_light_count==0);
            CHECK(presenter.active()==1); // presentation offset never changes surface publication
        }
    }
    SECTION("invalid start distance rejects only the line, not the independent impact") {
        for(const float bad:{-1.0F,1025.0F,NAN,INFINITY}) {
            app::WorldImpactPresentation presenter;tracer.start_offset_units=bad;
            CHECK(presenter.submit(request,collision(),world.get(),{},true,.1).status==app::WorldImpactStatus::hit);
            renderer::RenderScene scene;presenter.append_lines(scene);CHECK(scene.world_line_count==0);
            CHECK(scene.world_flash_count==0);CHECK(presenter.active()==1);
        }
    }
    SECTION("stationary profile shifts its start toward the hit without changing its endpoint or fade") {
        tracer.travel_speed_units_per_second=tracer.segment_length_units=0;
        app::WorldImpactPresentation presenter;
        const auto hit=presenter.submit(request,collision(),world.get(),{},false,.1);REQUIRE(hit.point);
        renderer::RenderScene scene;presenter.append_lines(scene);REQUIRE(scene.world_line_count==1);
        CHECK(scene.world_lines[0].start.x==Approx(34));CHECK(scene.world_lines[0].end.x==Approx(hit.point->x));
        CHECK(scene.world_lines[0].color==tracer.color);
        tracer.follow_local_muzzle=true;presenter.reset();
        REQUIRE(presenter.submit(request,collision(),world.get(),{},false,.1).point);
        renderer::RenderScene anchored;
        presenter.append_lines(anchored,app::WorldImpactPresentation::LocalMuzzleAnchor{1,59,{40,8,8}});
        REQUIRE(anchored.world_line_count==1);CHECK(anchored.world_lines[0].start.x==Approx(24));
        CHECK(anchored.world_lines[0].end.x==Approx(hit.point->x));
    }
}
TEST_CASE("E15 production GL first-person solid line has half the remote body diameter",
          "[e15][tracer][tracer-start][opengl][actual-context]") {
    auto context=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();renderer::RenderScene scene;
    scene.camera.position={25,8,8};scene.camera.target={0,8,8};
    scene.static_world=renderer::RenderStaticWorld{wall()};
    const auto capture=[&] {
        context->renderer().render(scene,{320,240});
        auto frame=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);
        REQUIRE(frame);return frame;
    };
    const auto baseline=capture();scene.world_line_count=1;
    // Isolate geometry, not the retained equal-size head glow/light envelope.
    scene.world_lines[0]={{5,4,8},{5,12,8},1.30F,{1,.82F,.45F,.85F},.35F,api::WorldLineGeometry::solid_3d};
    const auto remote=capture();scene.world_lines[0].width_units=.65F;const auto local=capture();
    const auto column=[&](const auto& frame) {
        std::size_t changed=0;
        for(std::size_t y=0;y<240;++y) {
            const auto p=(y*320+140)*4;
            if(!std::equal(frame.rgba8.begin()+p,frame.rgba8.begin()+p+3,baseline.rgba8.begin()+p)) ++changed;
        }
        return changed;
    };
    const auto broad=column(remote),narrow=column(local);
    REQUIRE(broad>=8);REQUIRE(narrow>=3);
    CHECK(narrow<broad);CHECK(narrow>=broad*.35);CHECK(narrow<=broad*.65);
    scene.world_lines[0].width_units=1.30F;CHECK(capture().color_signature==remote.color_signature);
    scene.world_line_count=0;CHECK(capture().color_signature==baseline.color_signature);
    CHECK(glGetError()==GL_NO_ERROR);context->release_renderer();
}
TEST_CASE("E15 production GL solid tracer has a closed end-on silhouette from both ends and every side",
          "[e15][tracer][tracer-3d][opengl][actual-context]") {
    auto context=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();renderer::RenderScene scene;
    scene.world_lines[0]={{5,4,8},{5,12,8},2,{1,.82F,.45F,.85F},.35F};
    const auto capture=[&] {
        context->renderer().render(scene,{320,240});
        auto frame=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);
        REQUIRE(frame);return frame;
    };
    const auto changed=[](const auto& a,const auto& b) {
        std::size_t count=0;
        for(std::size_t i=0;i<a.rgba8.size();i+=4)
            if(!std::equal(a.rgba8.begin()+i,a.rgba8.begin()+i+3,b.rgba8.begin()+i)) ++count;
        return count;
    };
    const std::array<assets::AssetVector3,6> eyes{{{5,-12,8},{5,28,8},
        {25,8,8},{-15,8,8},{5,8,28},{22,22,20}}};
    for(std::size_t i=0;i<eyes.size();++i) {
        CAPTURE(i);scene.camera.position=eyes[i];scene.camera.target={5,8,8};
        scene.camera.up=i==4 ? assets::AssetVector3{0,1,0}:assets::AssetVector3{0,0,1};
        scene.world_line_count=0;const auto baseline=capture();
        scene.world_line_count=1;scene.world_lines[0].geometry=api::WorldLineGeometry::flat_billboard;
        const auto flat=capture();
        scene.world_lines[0].geometry=api::WorldLineGeometry::solid_3d;const auto solid=capture();
        CHECK(changed(solid,baseline)>30); // independent of head glow, particles or lights
        if(i<2) CHECK(changed(flat,baseline)==0); // former billboard collapses end-on
        scene.world_lines[0].geometry=api::WorldLineGeometry::flat_billboard;
        CHECK(capture().color_signature==flat.color_signature); // flat branch unchanged after 3D
        scene.world_line_count=0;CHECK(capture().color_signature==baseline.color_signature);
    }
    CHECK(glGetError()==GL_NO_ERROR);context->release_renderer();
}
TEST_CASE("E15 frozen production GL tracer leading silhouette grows smoothly rather than a full-width flat cut",
          "[e15][tracer][tracer-round-tail][opengl][actual-context]") {
    auto context=hlclient::tests::entity_opengl_fixture::try_context(640,480);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");context->initialize_renderer();
    const auto world=wall();
    for(const float width:{.65F,1.30F}) {
        CAPTURE(width);auto request=shot();request.origin={5,4,8};request.direction={0,1,0};request.maximum_distance_units=8;
        request.suppress_surface_effects=true;request.tracer=api::LocalWorldImpactRequest::Tracer{};
        auto& tracer=*request.tracer;tracer.origin=request.origin;tracer.width_units=width;
        tracer.geometry=api::WorldLineGeometry::solid_3d;tracer.head_taper_fraction=.35F;
        tracer.travel_speed_units_per_second=12000;tracer.segment_length_units=32;tracer.lifetime_seconds=.078;
        app::WorldImpactPresentation owner{true};
        REQUIRE(owner.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::miss);
        renderer::RenderScene scene;scene.camera.position={15,12,8};scene.camera.target={5,12,8};
        const auto capture=[&] {context->renderer().render(scene,{640,480});
            auto frame=context->renderer().observe_framebuffer({640,480},scene.clear_color,true);REQUIRE(frame);return frame;};
        const auto baseline=capture();owner.append_lines(scene);REQUIRE(scene.world_line_count==1);
        owner.update(1000);scene.world_line_count=0;owner.append_lines(scene);
        const auto visible=capture();const auto matrix=renderer::camera_view_projection(scene.camera,{640,480});REQUIRE(matrix);
        const auto column_at=[&](float along) {
            const auto clip=renderer::transform(*matrix.matrix,{5,12-along,8,1});REQUIRE(clip.w>0);
            return static_cast<std::size_t>(std::clamp((clip.x/clip.w*.5F+.5F)*640.0F,0.0F,639.0F));
        };
        const auto count=[&](std::size_t x) {std::size_t changed=0;
            for(std::size_t y=0;y<480U;++y) {const auto p=(y*640U+x)*4U;
                if(!std::equal(visible.rgba8.begin()+p,visible.rgba8.begin()+p+3,baseline.rgba8.begin()+p)) ++changed;}
            return changed;};
        const float radius=width*.5F;const auto rear_pixels=count(column_at(radius*.1F)),middle_pixels=count(column_at(radius*.6F)),body_pixels=count(column_at(radius*1.8F));
        INFO("rounded-front=" << rear_pixels << " middle=" << middle_pixels << " body=" << body_pixels);
        REQUIRE(rear_pixels>0U);REQUIRE(body_pixels>8U);CHECK(rear_pixels<body_pixels*.8);CHECK(middle_pixels>rear_pixels);CHECK(middle_pixels<=body_pixels+2U);
        CHECK(scene.world_flash_count==0);CHECK(scene.world_effect_light_count==0);
        owner.reset();scene.world_line_count=0;owner.append_lines(scene);CHECK(capture().rgba8==baseline.rgba8);
    }
    CHECK(glGetError()==GL_NO_ERROR);context->release_renderer();
}

TEST_CASE("E15 production GL solid tracer keeps the tip depth rejection and mixed flat state",
          "[e15][tracer][tracer-3d][opengl][actual-context]") {
    auto context=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();renderer::RenderScene scene;
    scene.camera.position={25,8,8};scene.camera.target={0,8,8};
    scene.static_world=renderer::RenderStaticWorld{wall()};
    const auto capture=[&] {
        context->renderer().render(scene,{320,240});
        auto frame=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);
        REQUIRE(frame);return frame;
    };
    const auto baseline=capture();
    scene.world_line_count=1;
    scene.world_lines[0]={{5,4,8},{5,12,8},2,{1,.82F,.45F,.85F},0,api::WorldLineGeometry::solid_3d};
    const auto round=capture();
    const auto column=[&](const auto& frame,std::size_t x) {
        std::size_t count=0;for(std::size_t y=0;y<240;++y) {
            const auto p=(y*320+x)*4;
            if(!std::equal(frame.rgba8.begin()+p,frame.rgba8.begin()+p+3,baseline.rgba8.begin()+p)) ++count;
        } return count;
    };
    std::size_t left=320,right=0;
    for(std::size_t x=0;x<320;++x) if(column(round,x)) {left=std::min(left,x);right=std::max(right,x);}
    REQUIRE(left<right);const auto head=left+(right-left)/10,body=left+(right-left)/2;
    scene.world_lines[0].head_taper_fraction=.35F;const auto tapered=capture();
    CHECK(column(tapered,body)==column(round,body));
    CHECK(column(tapered,head)>0);CHECK(column(tapered,head)*2<column(round,head));
    std::swap(scene.world_lines[0].start,scene.world_lines[0].end);
    const auto reversed=capture();CHECK(column(reversed,body)==column(round,body));
    CHECK(column(reversed,head)>column(tapered,head)); // rounded front moved here, pointed rear moved to the other end
    scene.world_lines[0].start.x=scene.world_lines[0].end.x=-3;
    CHECK(capture().color_signature==baseline.color_signature); // entire volume behind wall
    scene.world_lines[0]={{5,4,8},{5,12,8},2,{1,.82F,.45F,.85F},.35F};
    const auto original_flat=capture();
    scene.world_line_count=2;scene.world_lines[1]=scene.world_lines[0];
    scene.world_lines[1].geometry=api::WorldLineGeometry::solid_3d;
    scene.world_lines[1].start.z=scene.world_lines[1].end.z=11;
    CHECK(capture().color_signature!=original_flat.color_signature);
    scene.world_line_count=1;CHECK(capture().color_signature==original_flat.color_signature);
    scene.world_lines[0].geometry=api::WorldLineGeometry::solid_3d;
    for(const auto bad:std::array<float,3>{-.1F,1.1F,NAN}) {
        scene.world_lines[0].head_taper_fraction=bad;
        CHECK(capture().color_signature==baseline.color_signature);
    }
    scene.world_lines[0].head_taper_fraction=.35F;
    scene.world_lines[0].geometry=static_cast<api::WorldLineGeometry>(255);
    CHECK(capture().color_signature==baseline.color_signature);
    scene.world_line_count=0;CHECK(capture().color_signature==baseline.color_signature);
    CHECK(glGetError()==GL_NO_ERROR);context->release_renderer();
}

TEST_CASE("E15 frozen production GL rear cap has a twenty percent equator instead of a sharp cone",
          "[e15][tracer][tracer-round-rear][tracer-freeze][opengl][actual-context]") {
    auto context=hlclient::tests::entity_opengl_fixture::try_context(640,480);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");context->initialize_renderer();
    const auto world=wall();
    for(const float width:{.65F,1.30F}) {
        CAPTURE(width);auto request=shot();request.origin={5,4,8};request.direction={0,1,0};request.maximum_distance_units=8;
        request.suppress_surface_effects=true;request.tracer=api::LocalWorldImpactRequest::Tracer{};
        auto& tracer=*request.tracer;tracer.origin=request.origin;tracer.width_units=width;
        tracer.geometry=api::WorldLineGeometry::solid_3d;tracer.head_taper_fraction=.90F;
        tracer.taper_end_diameter_fraction=.20F;tracer.travel_speed_units_per_second=12000;
        tracer.segment_length_units=48;tracer.lifetime_seconds=.078;
        app::WorldImpactPresentation owner{true};
        REQUIRE(owner.submit(request,collision(),world.get(),{},false,.1).status==app::WorldImpactStatus::miss);
        renderer::RenderScene scene;scene.camera.position={10,4,8};scene.camera.target={5,4,8};
        const auto capture=[&] {context->renderer().render(scene,{640,480});
            auto frame=context->renderer().observe_framebuffer({640,480},scene.clear_color,true);REQUIRE(frame);return frame;};
        const auto baseline=capture();owner.append_lines(scene);REQUIRE(scene.world_line_count==1);
        REQUIRE(scene.world_lines[0].taper_end_diameter_fraction==Approx(.20));
        owner.update(1000);scene.world_line_count=0;owner.append_lines(scene);const auto rounded=capture();
        const auto matrix=renderer::camera_view_projection(scene.camera,{640,480});REQUIRE(matrix);
        const auto column_at=[&](float along) {const auto clip=renderer::transform(*matrix.matrix,{5,4+along,8,1});REQUIRE(clip.w>0);
            return static_cast<std::size_t>(std::clamp((clip.x/clip.w*.5F+.5F)*640.0F,0.0F,639.0F));};
        const auto count=[&](const auto& frame,std::size_t x) {std::size_t changed=0;
            for(std::size_t y=0;y<480U;++y) {const auto p=(y*640U+x)*4U;
                if(!std::equal(frame.rgba8.begin()+p,frame.rgba8.begin()+p+3,baseline.rgba8.begin()+p)) ++changed;}
            return changed;};
        const float cap_radius=width*.1F;
        const auto rear_pixels=count(rounded,column_at(cap_radius*.15F));
        const auto middle=count(rounded,column_at(cap_radius*.6F));
        const auto equator=count(rounded,column_at(cap_radius));
        INFO("rear-rounded=" << rear_pixels << " middle=" << middle << " equator=" << equator);
        REQUIRE(rear_pixels>0);CHECK(middle>rear_pixels);CHECK(equator>=middle);REQUIRE(equator>5U);
        scene.world_lines[0].taper_end_diameter_fraction=0;
        const auto sharp=capture();CHECK(middle>count(sharp,column_at(cap_radius*.6F))+2U);
        scene.world_lines[0].taper_end_diameter_fraction=.20F;CHECK(capture().rgba8==rounded.rgba8);
        scene.world_lines[0].start.x=scene.world_lines[0].end.x=-3;
        scene.static_world=renderer::RenderStaticWorld{world};const auto occluded=capture();
        scene.world_line_count=0;CHECK(capture().rgba8==occluded.rgba8);
        scene.static_world.reset();owner.reset();owner.append_lines(scene);CHECK(capture().rgba8==baseline.rgba8);
    }
    CHECK(glGetError()==GL_NO_ERROR);context->release_renderer();
}
TEST_CASE("E5 static BSP shot resolves one exact surface and clipped world polygon",
          "[world-impacts][collision]") {
    const auto package=wall();
    app::WorldImpactPresentation presenter;
    auto result=presenter.submit(shot(),collision(),package.get(),{},true,0.1);
    REQUIRE(result.status==app::WorldImpactStatus::hit);
    REQUIRE(result.point);
    CHECK(result.point->x==Catch::Approx(0).margin(0.2));
    REQUIRE(result.surface);
    CHECK(result.surface->source_surface_index==0U);
    CHECK(std::string_view{result.surface->texture_name.data()}=="STONE");
    auto frame=presenter.frame(decal());
    REQUIRE(frame);REQUIRE(frame->vertices);
    REQUIRE_FALSE(frame->vertices->empty());
    CHECK(frame->vertices->size()%3==0);
    for(const auto& v:*frame->vertices) {
        CHECK(v.position.x==Catch::Approx(0).margin(0.2));
        CHECK(v.position.y>=3.99F);CHECK(v.position.y<=12.01F);
        CHECK(v.position.z>=3.99F);CHECK(v.position.z<=12.01F);
        CHECK(v.uv.x>=0);CHECK(v.uv.x<=1);
        CHECK(v.uv.y>=0);CHECK(v.uv.y<=1);
    }
    presenter.reset();
    result=presenter.submit(shot(2,15.5F,8),collision(),package.get(),{},true,0.1);
    REQUIRE(result.status==app::WorldImpactStatus::hit);
    frame=presenter.frame(decal());
    REQUIRE(frame);
    for(const auto& v:*frame->vertices) CHECK(v.position.y<=16.001F);
    CHECK(presenter.active()==1U);
    presenter.update(20.2);
    CHECK(presenter.active()==0U);
}

TEST_CASE("E5 trace rejects miss, stale, invalid, startsolid and absent geometry",
          "[world-impacts][collision]") {
    const auto package=wall();
    app::WorldImpactPresentation presenter;
    auto miss=shot();miss.direction={1,0,0};
    CHECK(presenter.submit(miss,collision(),package.get(),{},true,0.1).status==
        app::WorldImpactStatus::miss);
    auto invalid=shot();invalid.direction={NAN,0,0};
    CHECK(presenter.submit(invalid,collision(),package.get(),{},true,0.1).status==
        app::WorldImpactStatus::invalid_request);
    CHECK(presenter.submit(shot(),collision(),package.get(),{},true,0.36).status==
        app::WorldImpactStatus::expired);
    CHECK(presenter.submit(shot(),collision(),nullptr,{},true,0.1).status==
        app::WorldImpactStatus::collision_unavailable);
    auto solid=shot();solid.origin={-5,8,8};solid.direction={1,0,0};
    CHECK(presenter.submit(solid,collision(),package.get(),{},true,0.1).status==
        app::WorldImpactStatus::start_solid);
    CHECK(presenter.active()==0U);
    CHECK(presenter.submit(shot(),collision(),package.get(),{},false,0.1).status==
        app::WorldImpactStatus::hit); // sound still eligible without decal image
    CHECK(presenter.active()==0U);
    const auto masked=wall(true);
    CHECK(presenter.submit(shot(2),collision(),masked.get(),{},true,0.1).status==
        app::WorldImpactStatus::surface_unsupported);
}

TEST_CASE("E7 adjacent coplanar BSP faces retain exact hit texture identity", "[e7][world-impacts]") {
    const auto package=split_wall();
    app::WorldImpactPresentation presenter;
    const auto concrete=presenter.submit(shot(1,4,8),collision(),package.get(),{},false,0.1);
    const auto metal=presenter.submit(shot(2,12,8),collision(),package.get(),{},false,0.1);
    REQUIRE(concrete.status==app::WorldImpactStatus::hit);
    REQUIRE(metal.status==app::WorldImpactStatus::hit);
    REQUIRE(concrete.surface);REQUIRE(metal.surface);
    CHECK(concrete.surface->source_surface_index==0U);
    CHECK(metal.surface->source_surface_index==1U);
    CHECK(std::string_view{concrete.surface->texture_name.data()}=="CONCRETE");
    CHECK(std::string_view{metal.surface->texture_name.data()}=="METAL");
    CHECK(concrete.surface->source_material_index!=metal.surface->source_material_index);
}

TEST_CASE("E6 crowbar line reach and immutable delayed decal use the existing BSP presenter",
          "[crowbar-impacts][collision]") {
    const auto package=wall();
    app::WorldImpactPresentation presenter;
    const auto hit=presenter.submit(crowbar(),collision(),package.get(),{},true,0.1);
    REQUIRE(hit.status==app::WorldImpactStatus::hit);
    REQUIRE(hit.point);
    CHECK(hit.point->x==Catch::Approx(0).margin(0.2));
    CHECK(presenter.pending()==1U);
    CHECK_FALSE(presenter.frame(white_neutral_decal(),api::LocalDecalMaterialMode::white_neutral_modulate));
    presenter.update(0.299);
    CHECK(presenter.pending()==1U);
    presenter.update(0.301);
    CHECK(presenter.pending()==0U);
    const auto frame=presenter.frame(white_neutral_decal(),api::LocalDecalMaterialMode::white_neutral_modulate);
    REQUIRE(frame); REQUIRE(frame->vertices);
    for(const auto& vertex:*frame->vertices)
        CHECK(vertex.position.x==Catch::Approx(0).margin(0.2));
    CHECK(presenter.submit(crowbar(33.0F),collision(),package.get(),{},true,0.1).status==
        app::WorldImpactStatus::miss);
    auto invalid=crowbar();invalid.direction={};
    CHECK(presenter.submit(invalid,collision(),package.get(),{},true,0.1).status==
        app::WorldImpactStatus::invalid_request);
    auto solid=crowbar();solid.origin.x=-5.0F;solid.direction={1,0,0};
    CHECK(presenter.submit(solid,collision(),package.get(),{},true,0.1).status==
        app::WorldImpactStatus::start_solid);
    CHECK(presenter.submit(crowbar(),{},package.get(),{},true,0.1).status==
        app::WorldImpactStatus::collision_unavailable);
    namespace brush_scene=hlclient::goldsrc::collision;
    namespace brush=hlclient::goldsrc::brush_models;
    const auto brush_world=hlclient::tests::collision_brush_fixture::package(true,0.0,1U,false);
    const auto library=brush_scene::build_brush_collision_model_library(brush_world);
    REQUIRE(library);
    const auto transform=brush::make_brush_rigid_transform({2,0,0},{});
    REQUIRE(transform);
    const brush_scene::BrushCollisionInstanceIdentity identity{1U,1U,42U};
    const std::vector definitions{brush_scene::BrushCollisionInstanceDefinition{identity,*transform.transform}};
    const brush_scene::ExplicitSyntheticBrushCollisionRoleProvider provider{{
        brush_scene::SyntheticBrushCollisionRoleBinding{identity,brush_scene::BrushCollisionRole::solid}}};
    const auto blocked=brush_scene::build_brush_collision_scene(library.library,definitions,provider);
    REQUIRE(blocked);
    CHECK(presenter.submit(crowbar(),brush_world,package.get(),blocked.scene,true,0.1).status==
        app::WorldImpactStatus::unsupported_blocker);
    presenter.update(20.2);
    CHECK(presenter.active()==0U);
    presenter.reset();
    REQUIRE(presenter.submit(crowbar(),collision(),package.get(),{},true,0.1).status==
        app::WorldImpactStatus::hit);
    CHECK(presenter.pending()==1U);
    presenter.reset(); // map/session change cancels the frozen, not-yet-published hit
    presenter.update(0.5);
    CHECK_FALSE(presenter.frame(white_neutral_decal(),api::LocalDecalMaterialMode::white_neutral_modulate));
}

TEST_CASE("E6 static BSP floor uses the same normal-aware impact clipping",
          "[crowbar-impacts][collision]") {
    const auto package=floor_world();
    auto request=crowbar();
    request.origin={8,8,5};
    request.direction={0,0,-1};
    app::WorldImpactPresentation presenter;
    const auto result=presenter.submit(request,floor_collision(),package.get(),{},true,0.1);
    REQUIRE(result.status==app::WorldImpactStatus::hit);
    REQUIRE(result.point);
    CHECK(result.point->z==Catch::Approx(0.0F).margin(0.2));
    presenter.update(0.301);
    const auto frame=presenter.frame(white_neutral_decal(),
        api::LocalDecalMaterialMode::white_neutral_modulate);
    REQUIRE(frame); REQUIRE(frame->vertices);
    for(const auto& vertex:*frame->vertices)
        CHECK(vertex.position.z==Catch::Approx(0.0F).margin(0.2));
}

TEST_CASE("E5 nearer solid brush masks the farther static world",
          "[world-impacts][collision][scene]") {
    namespace scene=hlclient::goldsrc::collision;
    namespace brush=hlclient::goldsrc::brush_models;
    const auto world=hlclient::tests::collision_brush_fixture::package(true,0.0,1U,false);
    const auto library=scene::build_brush_collision_model_library(world);
    REQUIRE(library);
    const auto transform=brush::make_brush_rigid_transform({2,0,0},{});
    REQUIRE(transform);
    const scene::BrushCollisionInstanceIdentity identity{1U,1U,42U};
    const std::vector definitions{scene::BrushCollisionInstanceDefinition{identity,*transform.transform}};
    const scene::ExplicitSyntheticBrushCollisionRoleProvider provider{{
        scene::SyntheticBrushCollisionRoleBinding{identity,scene::BrushCollisionRole::solid}}};
    const auto built=scene::build_brush_collision_scene(library.library,definitions,provider);
    REQUIRE(built);
    app::WorldImpactPresentation presenter;
    const auto render_world=wall();
    const auto result=presenter.submit(shot(),world,render_world.get(),built.scene,true,0.1);
    CHECK(result.status==app::WorldImpactStatus::unsupported_blocker);
    CHECK(presenter.active()==0U);
}

TEST_CASE("Weapon impacts resolve nearest brush material and keep decals attached to its lifetime",
          "[world-impacts][brush-weapon-impacts][collision][attachment]") {
    namespace bc=hlclient::goldsrc::collision;
    namespace bm=hlclient::goldsrc::brush_models;
    namespace ws=hlclient::world_scene_render;
    const auto collision_world=hlclient::tests::collision_brush_fixture::package(true,0.0,1U,false);
    const auto collision_models=bc::build_brush_collision_model_library(collision_world);REQUIRE(collision_models);
    const auto rigid=bm::make_brush_rigid_transform({2,0,0},{});REQUIRE(rigid);
    const bc::BrushCollisionInstanceIdentity identity{42,1,42};
    const std::array definitions{bc::BrushCollisionInstanceDefinition{identity,*rigid.transform}};
    const bc::ExplicitSyntheticBrushCollisionRoleProvider roles{{{identity,bc::BrushCollisionRole::solid}}};
    const auto collision_scene=bc::build_brush_collision_scene(collision_models.library,definitions,roles);REQUIRE(collision_scene);
    const auto render_world=wall();
    std::vector<ws::BrushSubmodelRenderModel> models;
    models.emplace_back(1,assets::WorldBounds{{0,0,0},{0,16,16}},std::vector<std::uint32_t>{0});
    ws::BrushSubmodelRenderLibrary library{render_world,std::move(models)};
    std::array<app::WorldImpactPresentation::ServerDecalTargetLifetime,1> lives{{{42,7,77,1}}};
    app::WorldImpactPresentation::BrushImpactContext context{&library,lives,white_neutral_decal(),
        api::LocalDecalMaterialMode::white_neutral_modulate};
    auto request=shot();request.particles=hlclient::games::halflife::bullet_impact_particles();
    request.tracer=api::LocalWorldImpactRequest::Tracer{};request.tracer->origin=request.origin;
    app::WorldImpactPresentation owner;
    const auto submit=[&] {
        const auto result=owner.submit(request,collision_world,render_world.get(),collision_scene.scene,true,.1,&context);
        // Normal application publishes particle samples in the subsequent
        // update, not in spawn; preserve that production composition order.
        owner.update(.1);return result;
    };
    ws::RuntimeBrushRenderFrame frame;frame.generation=1;
    const auto pose=bm::make_brush_submodel_transform({2,0,0},{});REQUIRE(pose);
    frame.instances.push_back({42,77,1,pose.transform->model_matrix,{{2,0,0},{2,16,16}}});
    SECTION("nearest brush produces world point particles material and local clipped geometry") {
        const auto hit=submit();REQUIRE(hit.status==app::WorldImpactStatus::hit);REQUIRE(hit.point);REQUIRE(hit.surface);
        CHECK(hit.point->x==Approx(2).margin(.04));CHECK(hit.point->y==8);CHECK(hit.point->z==8);
        CHECK(std::string_view(hit.surface->texture_name.data())=="STONE");
        CHECK(owner.particles().size()==16);CHECK(owner.active()==1);CHECK(owner.server_active()==0);
        CHECK_FALSE(owner.frame(context.texture,context.material_mode)); // not left on the static wall behind it
        auto batches=owner.frame_batches(&frame);REQUIRE(batches.size()==1);
        for(const auto& vertex:*batches[0].vertices) {
            CHECK(vertex.position.x==Approx(0));CHECK(vertex.position.y>=0);CHECK(vertex.position.y<=16);
            CHECK(vertex.position.z>=0);CHECK(vertex.position.z<=16);
        }
        CHECK(batches[0].model_transform==pose.transform->model_matrix);
        const auto geometry=batches[0].vertices;const auto revision=batches[0].revision;
        REQUIRE(submit().status==app::WorldImpactStatus::hit);CHECK(owner.active()==1);CHECK(owner.particles().size()==16);
        const auto moved=bm::make_brush_submodel_transform({18,4,3},{0,90,0});REQUIRE(moved);
        frame.instances[0].model_transform=moved.transform->model_matrix;
        batches=owner.frame_batches(&frame);REQUIRE(batches.size()==1);
        CHECK(batches[0].vertices==geometry);CHECK(batches[0].revision==revision);
        CHECK(batches[0].model_transform==moved.transform->model_matrix);
        frame.instances.clear();CHECK(owner.frame_batches(&frame).empty());CHECK(owner.active()==1);
        owner.retire_server_targets(lives);CHECK(owner.active()==1); // render culling is not a life boundary
        lives[0].lifetime=8;owner.retire_server_targets(lives);CHECK(owner.active()==0);
        REQUIRE(submit().status==app::WorldImpactStatus::hit);CHECK(owner.active()==0); // replay cannot reattach to new occupant
        request.action.command_sequence++;REQUIRE(submit().status==app::WorldImpactStatus::hit);CHECK(owner.active()==1);
        owner.update(20.101);CHECK(owner.active()==0);owner.reset();CHECK(owner.frame_batches(&frame).empty());
    }
    SECTION("missing texture cannot suppress eligible material hit or particles") {
        context.texture.reset();const auto hit=submit();REQUIRE(hit.status==app::WorldImpactStatus::hit);
        REQUIRE(hit.point);REQUIRE(hit.surface);CHECK(hit.point->x>1.9F);CHECK(owner.particles().size()==16);CHECK(owner.active()==0);
    }
    SECTION("missing lifetime or mismatched model remains a blocker rather than a false far-wall hit") {
        lives[0].source_model_index=2;
        CHECK(submit().status==app::WorldImpactStatus::unsupported_blocker);CHECK(owner.active()==0);CHECK(owner.particles().empty());
        lives[0].source_model_index=1;lives[0].lifetime=0;
        CHECK(submit().status==app::WorldImpactStatus::unsupported_blocker);CHECK(owner.active()==0);
    }
    SECTION("unavailable brush reconstruction cannot fall through to world-only effects") {
        const auto result=owner.submit(request,collision_world,render_world.get(),{},true,.1,&context);
        CHECK(result.status==app::WorldImpactStatus::collision_unavailable);CHECK_FALSE(result.point);
        owner.update(.1);CHECK(owner.active()==0);CHECK(owner.particles().empty());
        renderer::RenderScene scene;owner.append_lines(scene);CHECK(scene.world_line_count==0);
        owner.bind_server_resources(2,1);
        CHECK(submit().status==app::WorldImpactStatus::unsupported_blocker);
        owner.append_lines(scene);CHECK(scene.world_line_count==0);CHECK(owner.particles().empty());
    }
    SECTION("delayed melee decal follows the door and map reset cancels pending placement") {
        request=crowbar();REQUIRE(submit().status==app::WorldImpactStatus::hit);CHECK(owner.pending()==1);
        CHECK(owner.frame_batches(&frame).empty());owner.update(.299);CHECK(owner.frame_batches(&frame).empty());
        owner.update(.301);CHECK(owner.frame_batches(&frame).size()==1);owner.reset();CHECK(owner.active()==0);
    }
    SECTION("wrong frame generation model slot or submodel cannot attach an old mark") {
        REQUIRE(submit().status==app::WorldImpactStatus::hit);
        frame.generation=2;CHECK(owner.frame_batches(&frame).empty());frame.generation=1;
        frame.instances[0].model_slot=78;CHECK(owner.frame_batches(&frame).empty());frame.instances[0].model_slot=77;
        frame.instances[0].source_model_index=2;CHECK(owner.frame_batches(&frame).empty());
    }
    SECTION("resource and map boundaries clear attached local geometry and action receipts") {
        owner.bind_server_resources(1,1);REQUIRE(submit().status==app::WorldImpactStatus::hit);CHECK(owner.active()==1);
        owner.bind_server_resources(1,2);CHECK(owner.active()==0);CHECK(owner.frame_batches(&frame).empty());
        REQUIRE(submit().status==app::WorldImpactStatus::hit);CHECK(owner.active()==1);
        owner.bind_server_resources(2,3);CHECK(owner.active()==0);
        CHECK(submit().status==app::WorldImpactStatus::unsupported_blocker);CHECK(owner.active()==0);
        request.action.generation=2;frame.generation=2;REQUIRE(submit().status==app::WorldImpactStatus::hit);
        CHECK(owner.frame_batches(&frame).size()==1);owner.reset();CHECK(owner.active()==0);
    }
}

TEST_CASE("Brush weapon impact OpenGL uses attached transform depth and neutral background",
          "[world-impacts][brush-weapon-impacts][opengl][actual-context]") {
    namespace bc=hlclient::goldsrc::collision;
    namespace ws=hlclient::world_scene_render;
    auto gl=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!gl)SKIP("OpenGL 3.3 context unavailable");gl->initialize_renderer();
    const auto world=hlclient::tests::collision_brush_fixture::package(false,0,1);
    const auto collision_models=bc::build_brush_collision_model_library(world);REQUIRE(collision_models);
    const bc::BrushCollisionInstanceIdentity identity{42,1,42};
    const std::array definitions{bc::BrushCollisionInstanceDefinition{identity,{}}};
    const bc::ExplicitSyntheticBrushCollisionRoleProvider roles{{{identity,bc::BrushCollisionRole::solid}}};
    const auto collision_scene=bc::build_brush_collision_scene(collision_models.library,definitions,roles);REQUIRE(collision_scene);
    const auto package=wall(false,0x28);
    std::vector<ws::BrushSubmodelRenderModel> models;
    models.emplace_back(1,assets::WorldBounds{{0,0,0},{0,16,16}},std::vector<std::uint32_t>{0});
    ws::BrushSubmodelRenderLibrary library{package,std::move(models)};
    const std::array<app::WorldImpactPresentation::ServerDecalTargetLifetime,1> lives{{{42,7,77,1}}};
    const app::WorldImpactPresentation::BrushImpactContext input{&library,lives,white_neutral_decal(),
        api::LocalDecalMaterialMode::white_neutral_modulate};
    app::WorldImpactPresentation owner;
    REQUIRE(owner.submit(shot(),world,package.get(),collision_scene.scene,true,.1,&input).status==app::WorldImpactStatus::hit);
    ws::RuntimeBrushRenderFrame frame;frame.generation=1;frame.instances.push_back({42,77,1,{},{}});
    renderer::RenderScene scene;scene.camera.position={25,8,8};scene.camera.target={0,8,8};
    scene.static_world=renderer::RenderStaticWorld{package};
    const auto capture=[&] {gl->renderer().render(scene,{320,240});
        const auto pixels=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(pixels);return pixels;};
    const auto baseline=capture();scene.additional_world_decals=owner.frame_batches(&frame);
    REQUIRE(scene.additional_world_decals.size()==1);const auto first=capture();
    CHECK(first.color_signature!=baseline.color_signature);
    const auto texel=[&](const auto& pixels,std::size_t x,std::size_t y) {
        const auto at=(y*320+x)*4;return std::array{pixels.rgba8[at],pixels.rgba8[at+1],pixels.rgba8[at+2]};};
    CHECK(texel(first,160,120)!=texel(baseline,160,120));
    // Inside the 8-unit square, outside its tiny dark centre: no opaque white backing.
    CHECK(texel(first,182,120)==texel(baseline,182,120));
    const auto uploaded=gl->renderer().decal_statistics().geometry_upload_count;
    frame.instances[0].model_transform.values[13]=3;scene.additional_world_decals=owner.frame_batches(&frame);
    const auto moved=capture();CHECK(moved.color_signature!=first.color_signature);
    CHECK(gl->renderer().decal_statistics().geometry_upload_count==uploaded);
    frame.instances[0].model_transform.values[12]=-2;scene.additional_world_decals=owner.frame_batches(&frame);
    CHECK(capture().rgba8==baseline.rgba8); // behind the wall is occluded
    owner.update(20.2);scene.additional_world_decals=owner.frame_batches(&frame);CHECK(capture().rgba8==baseline.rgba8);
    CHECK(glGetError()==GL_NO_ERROR);gl->release_renderer();
}

TEST_CASE("E5 world decal pool evicts oldest and bounds total geometry",
          "[world-impacts][collision][bounds]") {
    const auto package=wall();
    app::WorldImpactPresentation presenter;
    for(unsigned i=0;i<65;++i) {
        auto request=shot(i+1,1.0F+0.2F*i,8);
        request.decal_half_size_units=0.05F;
        CHECK(presenter.submit(request,collision(),package.get(),{},true,0.1).status==
            app::WorldImpactStatus::hit);
    }
    CHECK(presenter.active()==64U);
    const auto frame=presenter.frame(decal());
    REQUIRE(frame); REQUIRE(frame->vertices);
    CHECK(frame->vertices->size()<=64U*96U);
    const auto first_y=std::min_element(frame->vertices->begin(),frame->vertices->end(),
        [](const auto& a,const auto& b){return a.position.y<b.position.y;});
    REQUIRE(first_y!=frame->vertices->end());
    CHECK(first_y->position.y>1.1F); // first 1.0-unit hit was deterministically evicted
    presenter.update(20.2);
    CHECK(presenter.active()==0U);
    CHECK_FALSE(presenter.frame(decal()));
}

TEST_CASE("E5 production OpenGL world decal changes only the retained world pass",
          "[world-impacts][opengl][actual-context]") {
    auto context=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();
    const auto package=wall();
    app::WorldImpactPresentation presenter;
    REQUIRE(presenter.submit(shot(),collision(),package.get(),{},true,0.1).status==
        app::WorldImpactStatus::hit);
    renderer::RenderScene scene;
    scene.camera.position={25,8,8};scene.camera.target={0,8,8};
    scene.static_world=renderer::RenderStaticWorld{package};
    context->renderer().render(scene,{320,240});
    const auto baseline=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);
    REQUIRE(baseline);
    scene.world_decals=presenter.frame(decal());
    context->renderer().render(scene,{320,240});
    const auto visible=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);
    REQUIRE(visible);
    CHECK(visible.color_signature!=baseline.color_signature);
    REQUIRE(visible.rgba8.size()==baseline.rgba8.size());
    std::size_t changed_pixels=0;
    for(std::size_t p=0;p<visible.rgba8.size()/4;++p) {
        const auto offset=p*4;
        if(!std::equal(visible.rgba8.begin()+offset,visible.rgba8.begin()+offset+4,
            baseline.rgba8.begin()+offset)) ++changed_pixels;
    }
    CHECK(changed_pixels>0U);
    CHECK(changed_pixels<320U*240U/2U); // clipped surface patch, not a full-screen overlay
    for(const auto p:{0U,319U,320U*239U,320U*240U-1U}) {
        const auto offset=std::size_t(p)*4U;
        CHECK(std::equal(visible.rgba8.begin()+offset,visible.rgba8.begin()+offset+4,
            baseline.rgba8.begin()+offset));
    }
    CHECK(glGetError()==GL_NO_ERROR);
    scene.camera.position={25,10,8};scene.camera.target={0,8,8};
    context->renderer().render(scene,{320,240});
    const auto moved=context->renderer().observe_framebuffer({320,240},scene.clear_color);
    REQUIRE(moved);
    CHECK(moved.color_signature!=baseline.color_signature);
    scene.camera.position={-25,8,8};scene.camera.target={0,8,8};
    scene.world_decals.reset();
    context->renderer().render(scene,{320,240});
    const auto back_baseline=context->renderer().observe_framebuffer({320,240},scene.clear_color);
    REQUIRE(back_baseline);
    scene.world_decals=presenter.frame(decal());
    context->renderer().render(scene,{320,240});
    const auto back_decal=context->renderer().observe_framebuffer({320,240},scene.clear_color);
    REQUIRE(back_decal);
    CHECK(back_decal.color_signature==back_baseline.color_signature);
    presenter.update(20.2);
    scene.world_decals=presenter.frame(decal());
    scene.camera.position={25,8,8};scene.camera.target={0,8,8};
    context->renderer().render(scene,{320,240});
    const auto expired=context->renderer().observe_framebuffer({320,240},scene.clear_color);
    REQUIRE(expired);
    CHECK(expired.color_signature==baseline.color_signature);
    context->release_renderer();
}

TEST_CASE("E5.1 white-neutral WAD decal preserves wall pixels inside its rectangle",
          "[world-impacts][opengl][actual-context][decal-modulate]") {
    auto context=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();
    for(const auto shade:{std::uint8_t{0x40},std::uint8_t{0xc0}})
        for(const bool oblique:{false,true}) {
            INFO("wall shade=" << static_cast<unsigned>(shade) << " oblique=" << oblique);
            const auto package=wall(false,shade);
            app::WorldImpactPresentation presenter;
            REQUIRE(presenter.submit(shot(),collision(),package.get(),{},true,0.1).status==
                app::WorldImpactStatus::hit);
            renderer::RenderScene scene;
            scene.camera.position=oblique ? assets::AssetVector3{25,12,10}
                : assets::AssetVector3{25,8,8};
            scene.camera.target={0,8,8};
            scene.static_world=renderer::RenderStaticWorld{package};
            context->renderer().render(scene,{320,240});
            const auto baseline=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);
            REQUIRE(baseline);
            scene.world_decals=presenter.frame(white_neutral_decal(),
                api::LocalDecalMaterialMode::white_neutral_modulate);
            context->renderer().render(scene,{320,240});
            const auto visible=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);
            REQUIRE(visible);
            const auto same_pixel=[&](unsigned x,unsigned y) {
                const auto p=(y*320U+x)*4U;
                return std::equal(visible.rgba8.begin()+p,visible.rgba8.begin()+p+3,
                    baseline.rgba8.begin()+p);
            };
            CHECK_FALSE(same_pixel(160,120)); // dark bullet center
            CHECK(same_pixel(144,120)); // opaque white texel *inside* decal rectangle
            CHECK(same_pixel(176,120));
            CHECK(same_pixel(160,104));
            CHECK(same_pixel(160,136));
            CHECK(glGetError()==GL_NO_ERROR);
            presenter.update(20.2);
            scene.world_decals=presenter.frame(white_neutral_decal(),
                api::LocalDecalMaterialMode::white_neutral_modulate);
            context->renderer().render(scene,{320,240});
            const auto expired=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);
            REQUIRE(expired);
            CHECK(expired.rgba8==baseline.rgba8);
        }
    context->release_renderer();
}

TEST_CASE("E6 accepted Half-Life crowbar swing reaches the production OpenGL world decal pass",
          "[crowbar-impacts][opengl][actual-context]") {
    auto context=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!context) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    host.reset({1,1,{1}});
    api::LocalWeaponModelMetadata model;
    model.generation=1; model.model_index=60; model.resource_revision=3;
    model.resource_name="models/v_crowbar.mdl";
    model.supported_bodies.fill(true); model.selectable_bodies.fill(true);
    model.sequences.resize(9,{30,46,false,{}});
    host.bind_model(model);
    hlclient::client::RuntimeClientObservationState observed;
    observed.generation=1; observed.publication_revision=1;
    observed.lifecycle.life_epoch=1;
    observed.lifecycle.state=hlclient::client::LocalPlayerLifeState::alive;
    observed.client_metadata.generation=1;
    observed.client_metadata.freshness=
        hlclient::client::RuntimeObservationFreshness::observed_in_record;
    observed.client_metadata.source=hlclient::client::RuntimeObservationSource{
        .record_identity=1,.record_ordinal=1};
    observed.receiving_client.emplace();
    observed.receiving_client->viewmodel_index=60;
    observed.receiving_client->health=100;
    observed.weapon_hud.active_weapon_id=1;
    observed.weapon_hud.catalogue.push_back({.id=1,.command_name="weapon_crowbar"});
    observed.weapon_slots.push_back({.wire_slot=1,.clip=0,.in_reload=false,
        .next_primary_attack=0.0,.weapon_id=1});
    host.observe(observed,0.0);
    api::LocalWeaponSubmittedCommand command{1,1,1,0.1};
    command.shot_context=api::LocalWeaponSubmittedCommand::ShotContext{{5,8,8},{-1,0,0}};
    host.submit(command,0.1);
    REQUIRE(host.sample(0.1).visual->sequence==4U);
    const auto frame=host.local_visuals({{5,8,8},{-1,0,0},{0,-1,0},{0,0,1},{},0.1});
    REQUIRE(frame.world_impact);
    const auto profile=host.local_crowbar_impact_assets();
    REQUIRE(profile);
    CHECK(std::string_view{profile->decal.texture_name.data()}=="{SHOT2");
    const auto package=wall();
    app::WorldImpactPresentation presenter;
    const auto result=presenter.submit(*frame.world_impact,collision(),package.get(),{},true,0.1);
    REQUIRE(result.status==app::WorldImpactStatus::hit);
    REQUIRE(host.resolved_world_impact(frame.world_impact->action,
        api::LocalWorldImpactOutcome::static_world_hit,result.point,0.1));
    REQUIRE(host.sample(0.1).visual->sequence==3U);
    renderer::RenderScene scene;
    scene.camera.position={25,8,8}; scene.camera.target={0,8,8};
    scene.static_world=renderer::RenderStaticWorld{package};
    context->renderer().render(scene,{320,240});
    const auto baseline=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);
    REQUIRE(baseline);
    presenter.update(0.301);
    scene.secondary_world_decals=presenter.frame(white_neutral_decal(),profile->decal.material_mode);
    REQUIRE(scene.secondary_world_decals);
    context->renderer().render(scene,{320,240});
    const auto visible=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);
    REQUIRE(visible);
    CHECK(visible.color_signature!=baseline.color_signature);
    const auto pixel_same=[&](unsigned x,unsigned y) {
        const auto offset=(y*320U+x)*4U;
        return std::equal(visible.rgba8.begin()+offset,visible.rgba8.begin()+offset+3,
            baseline.rgba8.begin()+offset);
    };
    CHECK_FALSE(pixel_same(160,120));
    CHECK(pixel_same(144,120));
    CHECK(pixel_same(176,120));
    CHECK(glGetError()==GL_NO_ERROR);
    scene.camera.position={25,10,8}; scene.camera.target={0,8,8};
    context->renderer().render(scene,{320,240});
    const auto moved=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);
    REQUIRE(moved);
    CHECK(moved.color_signature!=baseline.color_signature);
    presenter.reset(); scene.secondary_world_decals.reset();
    context->renderer().render(scene,{320,240});
    context->release_renderer();
}

TEST_CASE("E19 normal module sniper uses production static trace without decal and isolates unsupported surface",
          "[e19][composition][world-impact][no-stock]") {
    namespace bow=hlclient::tests::crossbow_fixture;
    bow::Module module;module.zoom();(void)module.host.drain_audio();
    auto command=bow::command();command.shot_context->eye={16,8,8};
    module.host.submit(command,.02);(void)module.host.sample(.02);
    const auto frame=module.host.local_visuals({{}, {}, {}, {}, {}, .02});REQUIRE(frame.world_impact);
    const auto world=wall();app::WorldImpactPresentation owner;
    const auto result=owner.submit(*frame.world_impact,collision(),world.get(),{},true,.03);
    REQUIRE(result.status==app::WorldImpactStatus::hit);REQUIRE(result.point);REQUIRE(result.surface);
    CHECK(result.point->x==Approx(0).margin(.04));CHECK(owner.active()==0);CHECK(owner.pending()==0);
    module.host.accepted_world_impact(frame.world_impact->action,*result.point,.03);
    REQUIRE(module.host.drain_temporary_effects(.03).count==2);
    const auto audio=module.host.drain_audio();REQUIRE(audio.count==3); // two launch cues, isolated hit
    CHECK(audio.cues[2].reference.name()=="weapons/xbow_hit1.wav");
    CHECK(audio.cues[2].world_origin->x==result.point->x);
    const auto masked=wall(true);app::WorldImpactPresentation unsupported;
    CHECK(unsupported.submit(*frame.world_impact,collision(),masked.get(),{},true,.03).status==
        app::WorldImpactStatus::surface_unsupported);
}
