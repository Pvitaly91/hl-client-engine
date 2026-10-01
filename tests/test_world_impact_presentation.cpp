#include "collision_brush_test_fixture.hpp"
#include "world_render_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include <hlclient/app/world_impact_presentation.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/renderer/render_scene.hpp>
#include <hlclient/world_render/world_render_package_builder.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
namespace app=hlclient::app;
namespace api=hlclient::game_api;
namespace assets=hlclient::assets;
namespace renderer=hlclient::renderer;
namespace fixture=hlclient::tests::world_render_fixture;
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
