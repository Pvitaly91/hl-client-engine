#include "world_render_test_fixture.hpp"
#include "collision_brush_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include <hlclient/app/world_impact_presentation.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_brush_transform.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
namespace a=hlclient::assets;
namespace app=hlclient::app;
namespace api=hlclient::game_api;
namespace r=hlclient::renderer;
namespace wr=hlclient::world_render;
namespace ws=hlclient::world_scene_render;
namespace fixture=hlclient::tests::world_render_fixture;
using V=a::AssetVector3;
using Catch::Approx;
V plus(V x,V y) {return {x.x+y.x,x.y+y.y,x.z+y.z};}
V times(V v,float scale) {return {v.x*scale,v.y*scale,v.z*scale};}
struct Surface {V origin{},u{0,1,0},v{0,0,1};};
std::shared_ptr<const wr::WorldRenderPackage> package(std::vector<Surface> surfaces={{}},
    std::array<std::uint8_t,3> shade={160,140,100},
    std::array<std::uint8_t,3> light={128,128,128},bool unlit=false) {
    auto world=fixture::make_world();const auto prototype=world.surfaces[0];
    world.vertices.clear();world.indices.clear();world.surfaces.clear();
    world.bounds={{std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()},
        {-std::numeric_limits<float>::max(),-std::numeric_limits<float>::max(),-std::numeric_limits<float>::max()}};
    for(const auto& surface:surfaces) {
        const auto first_vertex=static_cast<std::uint32_t>(world.vertices.size());
        const auto first_index=static_cast<std::uint32_t>(world.indices.size());
        const auto normal=r::normalize(r::cross(surface.u,surface.v));REQUIRE(normal);
        a::WorldBounds bounds{world.bounds.minimum,world.bounds.maximum};
        bounds.minimum={std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
        bounds.maximum={-std::numeric_limits<float>::max(),-std::numeric_limits<float>::max(),-std::numeric_limits<float>::max()};
        for(const auto uv:std::array<std::array<float,2>,4>{{{0,0},{16,0},{16,16},{0,16}}}) {
            const auto point=plus(surface.origin,plus(times(surface.u,uv[0]),times(surface.v,uv[1])));
            world.vertices.push_back({point,*normal,{uv[0],uv[1]}});
            bounds.minimum.x=std::min(bounds.minimum.x,point.x);bounds.minimum.y=std::min(bounds.minimum.y,point.y);bounds.minimum.z=std::min(bounds.minimum.z,point.z);
            bounds.maximum.x=std::max(bounds.maximum.x,point.x);bounds.maximum.y=std::max(bounds.maximum.y,point.y);bounds.maximum.z=std::max(bounds.maximum.z,point.z);
        }
        for(const auto index:std::array<std::uint32_t,6>{0,1,2,0,2,3})world.indices.push_back(first_vertex+index);
        auto emitted=prototype;emitted.first_vertex=first_vertex;emitted.first_index=first_index;
        emitted.source_surface_ordinal=static_cast<std::uint32_t>(world.surfaces.size());emitted.bounds=bounds;
        world.surfaces.push_back(emitted);
        world.bounds.minimum.x=std::min(world.bounds.minimum.x,bounds.minimum.x);world.bounds.minimum.y=std::min(world.bounds.minimum.y,bounds.minimum.y);world.bounds.minimum.z=std::min(world.bounds.minimum.z,bounds.minimum.z);
        world.bounds.maximum.x=std::max(world.bounds.maximum.x,bounds.maximum.x);world.bounds.maximum.y=std::max(world.bounds.maximum.y,bounds.maximum.y);world.bounds.maximum.z=std::max(world.bounds.maximum.z,bounds.maximum.z);
    }
    world.statistics.emitted_vertex_count=world.vertices.size();world.statistics.emitted_triangle_count=world.indices.size()/3U;
    world.statistics.emitted_surface_count=world.surfaces.size();
    auto texture=fixture::make_texture(false);
    for(auto& mip:texture.mip_levels)for(std::size_t p=0;p<mip.rgba_pixels.size();p+=4U)
        for(std::size_t channel=0;channel<3;++channel)mip.rgba_pixels[p+channel]=std::byte{shade[channel]};
    a::WorldMaterialTextureBinding texture_binding;texture_binding.material_index=0;
    texture_binding.status=a::WorldMaterialTextureBindingStatus::resolved_embedded;texture_binding.texture_asset_index=0;texture_binding.source_bsp_texture_index=0;
    std::vector<a::WorldTextureAsset> textures;textures.push_back(std::move(texture));
    auto texture_set=a::WorldTextureSet::create(std::move(textures),{texture_binding},{},1);REQUIRE(texture_set);
    auto lightmap=fixture::make_lightmap_set({.unlit=unlit,.atlas_page_count=surfaces.size()});
    if(!unlit) {
        std::vector<a::WorldLightmapAtlasPage> pages(lightmap.pages().begin(),lightmap.pages().end());
        std::vector<a::WorldSurfaceLightmapBinding> bindings(lightmap.bindings().begin(),lightmap.bindings().end());
        for(auto& page:pages)for(std::size_t p=0;p<page.style_slot_images[0].rgba_pixels.size();p+=4U)
            for(std::size_t channel=0;channel<3;++channel)page.style_slot_images[0].rgba_pixels[p+channel]=std::byte{light[channel]};
        auto corrected=a::WorldLightmapSet::create(std::move(pages),std::move(bindings),surfaces.size());REQUIRE(corrected);
        auto built=wr::WorldRenderPackageBuilder{}.build({std::move(world),std::move(*texture_set.texture_set)},std::move(*corrected.lightmap_set));REQUIRE(built);
        return std::make_shared<const wr::WorldRenderPackage>(std::move(*built.package));
    }
    REQUIRE(surfaces.size()==1U);
    auto built=wr::WorldRenderPackageBuilder{}.build({std::move(world),std::move(*texture_set.texture_set)},std::move(lightmap));REQUIRE(built);
    return std::make_shared<const wr::WorldRenderPackage>(std::move(*built.package));
}
std::shared_ptr<const a::WorldTextureAsset> decal(bool yellow=false) {
    auto texture=fixture::make_texture(true);texture.name=yellow?"{YBLOOD1":"{BLOOD1";
    for(auto& mip:texture.mip_levels)for(std::size_t p=0;p<mip.rgba_pixels.size();p+=4U) {
        const auto pixel=p/4U,x=pixel%mip.width,y=pixel/mip.width;
        mip.rgba_pixels[p]=std::byte{190};mip.rgba_pixels[p+1]=yellow?std::byte{155}:std::byte{15};mip.rgba_pixels[p+2]=std::byte{5};
        mip.rgba_pixels[p+3]=(x>=mip.width/4U && x<mip.width*3U/4U && y>=mip.height/4U && y<mip.height*3U/4U)?std::byte{200}:std::byte{0};
    }
    return std::make_shared<const a::WorldTextureAsset>(std::move(texture));
}
app::WorldImpactPresentation::ServerDecalRequest request(std::uint32_t ordinal=1,V position={0,8,8}) {
    app::WorldImpactPresentation::ServerDecalRequest result;
    result.identity={1,100U+ordinal,ordinal*8U,0};result.record_ordinal=ordinal;result.resource_generation=9;
    result.position=position;result.texture=decal();result.material_mode=api::LocalDecalMaterialMode::lit_straight_alpha;
    return result;
}
app::WorldImpactPresentation::ServerDecalTarget target(std::uint64_t life=2,V translation={},V rotation={}) {
    const auto transform=hlclient::goldsrc::brush_models::make_brush_submodel_transform(translation,rotation);REQUIRE(transform);
    return {45,70,3,life,transform.transform->model_matrix};
}
ws::RuntimeBrushRenderFrame frame(const app::WorldImpactPresentation::ServerDecalTarget& target) {
    ws::RuntimeBrushRenderFrame result;result.generation=1;result.revision=1;
    result.instances.push_back({target.entity_number,target.model_slot,target.source_model_index,target.model_transform});return result;
}
V transformed(const r::RenderMatrix4& m,V p) {const auto value=r::transform(m,{p.x,p.y,p.z,1});return {value.x,value.y,value.z};}
std::array<std::size_t,2> neutral_fixture_pixel(const r::RenderScene& scene,
    const a::WorldTextureAsset& texture,std::array<std::size_t,2> texel) {
    const auto& image=texture.mip_levels[0];
    REQUIRE(texel[0]>0U);REQUIRE(texel[1]>0U);
    REQUIRE(texel[0]+1U<image.width);REQUIRE(texel[1]+1U<image.height);
    // These are source texel centers inside the footprint, not guessed screen
    // coordinates. A zero3x3 neighborhood leaves a safe linear-filter margin.
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
        const auto x=std::size_t(int(texel[0])+dx),y=std::size_t(int(texel[1])+dy);
        REQUIRE(image.rgba_pixels[(y*image.width+x)*4U+3U]==std::byte{0});
    }
    const V point{0,8+float(texel[0])+.5F-float(texture.width)*.5F,
        8+float(texel[1])+.5F-float(texture.height)*.5F};
    const auto matrix=r::camera_view_projection(scene.camera,{320,240});REQUIRE(matrix);
    const auto clip=r::transform(*matrix.matrix,{point.x,point.y,point.z,1});REQUIRE(clip.w>0);
    const auto x=std::floor((clip.x/clip.w*.5F+.5F)*320.0F);
    const auto y=std::floor((clip.y/clip.w*.5F+.5F)*240.0F);
    REQUIRE(x>=0);REQUIRE(x<320);REQUIRE(y>=0);REQUIRE(y<240);
    return {static_cast<std::size_t>(x),static_cast<std::size_t>(y)};
}
}

TEST_CASE("E30 server positions materialize walls floors and angled planes without a shot or camera", "[e30][server-decals][geometry]") {
    const float k=std::sqrt(.5F);
    for(const auto surface:std::array<Surface,3>{{{},{{},{1,0,0},{0,1,0}},{{},{k,k,0},{0,0,1}}}}) {
        const auto world=package({surface});const auto point=plus(surface.origin,plus(times(surface.u,8),times(surface.v,8)));
        app::WorldImpactPresentation owner;const auto result=owner.submit_server(request(1,point),world.get());
        REQUIRE(result.status==app::ServerDecalStatus::submitted);CHECK(result.vertex_count>=6U);CHECK(result.vertex_count<=96U);
        const auto batches=owner.frame_batches();REQUIRE(batches.size()==1U);
        const auto normal=r::normalize(r::cross(surface.u,surface.v));REQUIRE(normal);
        for(const auto& vertex:*batches[0].vertices) {
            CHECK(r::dot({vertex.position.x-point.x,vertex.position.y-point.y,vertex.position.z-point.z},*normal)==Approx(0).margin(1e-4));
            CHECK(vertex.uv.x>=0);CHECK(vertex.uv.x<=1);CHECK(vertex.uv.y>=0);CHECK(vertex.uv.y<=1);
            for(const auto component:vertex.surface_light_rgb)CHECK(component==Approx(128.0F/255.0F));
        }
        CHECK_FALSE(owner.frame(decal())); // old one-texture local path cannot misdraw blood
    }
}
TEST_CASE("E30 coplanar boundaries clip with continuous UV and unsupported corner and thin-wall ties reject", "[e30][server-decals][geometry]") {
    SECTION("adjacent coplanar polygons") {
        const auto world=package({{},{{0,16,0}}});app::WorldImpactPresentation owner;
        REQUIRE(owner.submit_server(request(1,{0,16,8}),world.get()).status==app::ServerDecalStatus::submitted);
        const auto batches=owner.frame_batches();REQUIRE(batches.size()==1U);
        float minimum=100,maximum=-100;
        for(const auto& v:*batches[0].vertices){minimum=std::min(minimum,v.position.y);maximum=std::max(maximum,v.position.y);}
        CHECK(minimum==Approx(8));CHECK(maximum==Approx(24));
    }
    SECTION("corner is not a guessed normal") {
        const auto world=package({{},{{},{1,0,0},{0,1,0}}});app::WorldImpactPresentation owner;
        CHECK(owner.submit_server(request(1,{0,8,0}),world.get()).status==app::ServerDecalStatus::surface_ambiguous);
        CHECK(owner.server_active()==0U);
    }
    SECTION("thin wall never materializes on the opposite plane") {
        const auto world=package({{},{{-.125F,16,0},{0,-1,0},{0,0,1}}});app::WorldImpactPresentation owner;
        CHECK(owner.submit_server(request(1,{-.0625F,8,8}),world.get()).status==app::ServerDecalStatus::surface_ambiguous);
        REQUIRE(owner.submit_server(request(2),world.get()).status==app::ServerDecalStatus::submitted);
        for(const auto& v:*owner.frame_batches()[0].vertices)CHECK(v.position.x==Approx(0));
    }
}
TEST_CASE("E30 persistent server decals use exact provenance and bounded shared capacity", "[e30][server-decals][ownership]") {
    const auto world=package();app::WorldImpactPresentation owner;
    const auto first=request();REQUIRE(owner.submit_server(first,world.get()).status==app::ServerDecalStatus::submitted);
    CHECK(owner.submit_server(first,world.get()).status==app::ServerDecalStatus::duplicate);
    REQUIRE(owner.submit_server(request(2,{0,8.01F,8}),world.get()).status==app::ServerDecalStatus::submitted);
    CHECK(owner.server_active()==2U); // proximity is never a dedup key
    owner.update(1000000);CHECK(owner.server_active()==2U);
    for(std::uint32_t n=3;n<=70;++n) {
        const auto result=owner.submit_server(request(n),world.get());REQUIRE(result.status==app::ServerDecalStatus::submitted);
        CHECK(result.evicted==(n>64U));
    }
    CHECK(owner.active()==64U);CHECK(owner.server_active()==64U);CHECK(owner.frame_batches().size()==64U);
    for(std::uint32_t n=71;n<=200;++n)
        REQUIRE(owner.submit_server(request(n),world.get()).status==app::ServerDecalStatus::submitted);
    CHECK(owner.submit_server(first,world.get()).status==app::ServerDecalStatus::duplicate);
    CHECK(owner.server_active()==64U); // evicted receipts have a bounded stale floor
    owner.retire_server_targets({});CHECK(owner.server_active()==64U); // world is independent of player life
    owner.bind_server_resources(1,10);CHECK(owner.server_active()==0U);
    CHECK(owner.submit_server(first,world.get()).status==app::ServerDecalStatus::stale);
    auto next=request();next.resource_generation=10;REQUIRE(owner.submit_server(next,world.get()).status==app::ServerDecalStatus::submitted);
    owner.reset();CHECK(owner.frame_batches().empty());
}
TEST_CASE("E30 shared FIFO retains legacy local expiry without expiring persistent server marks", "[e30][server-decals][world-impacts]") {
    const auto world=package();app::WorldImpactPresentation owner;
    const auto collision=hlclient::tests::collision_brush_fixture::package(true,0.0,0U);
    api::LocalWorldImpactRequest shot;shot.action={1,2,59,3,1,api::LocalWeaponAction::primary_fire,.1,1};
    shot.origin={5,8,8};shot.direction={-1,0,0};shot.maximum_distance_units=20;shot.decal_half_size_units=4;shot.expires_at_seconds=.35;
    REQUIRE(owner.submit(shot,collision,world.get(),{},true,.1).status==app::WorldImpactStatus::hit);
    REQUIRE(owner.submit_server(request(),world.get()).status==app::ServerDecalStatus::submitted);
    CHECK(owner.active()==2U);REQUIRE(owner.frame(decal()));CHECK(owner.frame_batches().size()==1U);
    owner.update(20.2);CHECK(owner.active()==1U);CHECK_FALSE(owner.frame(decal()));CHECK(owner.frame_batches().size()==1U);
}
TEST_CASE("E30 brush placement retains model-local mesh and follows translation and rotation", "[e30][server-decals][brush]") {
    const auto local=package();app::WorldImpactPresentation owner;
    auto source_target=target(2,{100,20,0},{0,90,0});auto event=request();event.target_entity=45;
    event.position=transformed(source_target.model_transform,{0,8,8});
    const std::array<std::uint32_t,1> surfaces{0};
    REQUIRE(owner.submit_server(event,local.get(),surfaces,source_target).status==app::ServerDecalStatus::submitted);
    CHECK(owner.frame_batches().empty());CHECK(owner.server_active()==1U);
    auto current=frame(source_target);auto batches=owner.frame_batches(&current);REQUIRE(batches.size()==1U);
    CHECK(batches[0].model_transform==source_target.model_transform);
    const auto retained=batches[0].vertices;
    for(const auto& v:*retained)CHECK(v.position.x==Approx(0).margin(1e-4));
    auto moved=target(2,{115,40,9},{20,35,15});current=frame(moved);batches=owner.frame_batches(&current);
    REQUIRE(batches.size()==1U);CHECK(batches[0].vertices==retained);CHECK(batches[0].model_transform==moved.model_transform);
    current.instances.clear();CHECK(owner.frame_batches(&current).empty());CHECK(owner.server_active()==1U);
    const std::array<app::WorldImpactPresentation::ServerDecalTargetLifetime,1> live{{{45,2}}};
    owner.retire_server_targets(live);CHECK(owner.server_active()==1U);
    const std::array<app::WorldImpactPresentation::ServerDecalTargetLifetime,1> reused{{{45,3}}};
    owner.retire_server_targets(reused);CHECK(owner.server_active()==0U);
    auto replacement=request(2);replacement.target_entity=45;auto new_target=target(3);
    REQUIRE(owner.submit_server(replacement,local.get(),surfaces,new_target).status==app::ServerDecalStatus::submitted);
    CHECK(owner.server_active()==1U);owner.retire_server_targets({});CHECK(owner.server_active()==0U);
}
TEST_CASE("E30 brush materialization cannot use another model's surfaces or world fallback", "[e30][server-decals][brush]") {
    const auto local=package({{},{{0,32,0}}});app::WorldImpactPresentation owner;auto event=request();event.target_entity=45;
    const auto t=target();const std::array<std::uint32_t,1> wrong{1},right{0},bad{99};
    CHECK(owner.submit_server(event,local.get(),wrong,t).status==app::ServerDecalStatus::surface_unmapped);
    CHECK(owner.submit_server(event,local.get(),bad,t).status==app::ServerDecalStatus::target_unavailable);
    CHECK(owner.submit_server(event,local.get(),{},t).status==app::ServerDecalStatus::target_unavailable);
    CHECK(owner.submit_server(event,local.get(),right,{}).status==app::ServerDecalStatus::target_unavailable);
    REQUIRE(owner.submit_server(event,local.get(),right,t).status==app::ServerDecalStatus::submitted);
    auto current=frame(t);current.instances[0].model_slot=71;CHECK(owner.frame_batches(&current).empty());
    current=frame(t);current.instances[0].source_model_index=4;CHECK(owner.frame_batches(&current).empty());
    current=frame(t);current.generation=2;CHECK(owner.frame_batches(&current).empty());
}
TEST_CASE("E30 invalid target geometry and texture requests fail without partial publication", "[e30][server-decals][bounds]") {
    const auto world=package();app::WorldImpactPresentation owner;
    auto invalid=request();invalid.position.x=std::numeric_limits<float>::quiet_NaN();
    CHECK(owner.submit_server(invalid,world.get()).status==app::ServerDecalStatus::invalid_request);
    invalid=request();invalid.identity.source=api::TemporaryEffectIdentity::Source::server_sound;
    CHECK(owner.submit_server(invalid,world.get()).status==app::ServerDecalStatus::invalid_request);
    invalid=request();invalid.texture.reset();CHECK(owner.submit_server(invalid,world.get()).status==app::ServerDecalStatus::material_rejected);
    invalid=request();invalid.target_entity=8192;CHECK(owner.submit_server(invalid,world.get()).status==app::ServerDecalStatus::invalid_request);
    invalid=request();CHECK(owner.submit_server(invalid,nullptr).status==app::ServerDecalStatus::target_unavailable);
    invalid=request();invalid.position.x=1;CHECK(owner.submit_server(invalid,world.get()).status==app::ServerDecalStatus::surface_unmapped);
    invalid=request();invalid.target_entity=45;auto corrupt=target();corrupt.model_transform.values[0]=2;
    const std::array<std::uint32_t,1> surface{0};
    CHECK(owner.submit_server(invalid,world.get(),surface,corrupt).status==app::ServerDecalStatus::target_unavailable);
    CHECK(owner.active()==0U);CHECK(owner.frame_batches().empty());
    auto boundary=request();boundary.target_entity=8191;auto proven_target=target();proven_target.entity_number=8191;
    REQUIRE(owner.submit_server(boundary,world.get(),surface,proven_target).status==app::ServerDecalStatus::submitted);
    const auto current=frame(proven_target);CHECK(owner.frame_batches(&current).size()==1U);
}
TEST_CASE("E30 blood overlay uses exact static surface lighting rather than fullbright color", "[e30][server-decals][lighting]") {
    for(const auto light:std::array<std::array<std::uint8_t,3>,3>{{{16,8,4},{64,128,192},{240,230,220}}}) {
        const auto world=package({{}},{160,140,100},light);app::WorldImpactPresentation owner;
        REQUIRE(owner.submit_server(request(),world.get()).status==app::ServerDecalStatus::submitted);
        for(const auto& v:*owner.frame_batches()[0].vertices)for(std::size_t channel=0;channel<3;++channel)
            CHECK(v.surface_light_rgb[channel]==Approx(float(light[channel])/255.0F));
    }
    const auto unlit=package({{}},{160,140,100},{0,0,0},true);app::WorldImpactPresentation owner;
    REQUIRE(owner.submit_server(request(),unlit.get()).status==app::ServerDecalStatus::submitted);
    for(const auto& v:*owner.frame_batches()[0].vertices)CHECK((v.surface_light_rgb==std::array<float,3>{1,1,1}));
}
TEST_CASE("E30 persistent decals are independent of frame time focus cadence and stalls", "[e30][server-decals][clock]") {
    const auto world=package();
    for(const auto fps:{30,60,144,240}) {
        app::WorldImpactPresentation owner;REQUIRE(owner.submit_server(request(),world.get()).status==app::ServerDecalStatus::submitted);
        const auto before=owner.frame_batches();REQUIRE(before.size()==1U);
        for(int n=0;n<fps*2;++n)owner.update(double(n)/fps);
        owner.update(600);owner.update(.5);owner.retire_server_targets({});
        const auto after=owner.frame_batches();REQUIRE(after.size()==1U);CHECK(after[0].vertices==before[0].vertices);
        CHECK(after[0].model_transform==before[0].model_transform);CHECK(owner.server_active()==1U);
    }
}
TEST_CASE("E30 production OpenGL colored blood decal has neutral background and lit red yellow pixels", "[e30][server-decals][opengl][actual-context]") {
    auto context=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!context)SKIP("OpenGL3.3 context unavailable");context->initialize_renderer();
    for(const bool yellow:{false,true})for(const auto light:{std::uint8_t{40},std::uint8_t{220}})
        for(const auto shade:std::array<std::array<std::uint8_t,3>,3>{{{30,30,30},{200,200,200},{40,110,180}}}) {
            const auto world=package({{}},shade,{light,light,light});app::WorldImpactPresentation owner;
            auto event=request();event.texture=decal(yellow);REQUIRE(owner.submit_server(event,world.get()).status==app::ServerDecalStatus::submitted);
            r::RenderScene scene;scene.camera.position={25,8,8};scene.camera.target={0,8,8};scene.static_world=r::RenderStaticWorld{world};
            context->renderer().render(scene,{320,240});const auto baseline=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(baseline);
            scene.additional_world_decals=owner.frame_batches();context->renderer().render(scene,{320,240});
            const auto visible=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(visible);
            CHECK(visible.color_signature!=baseline.color_signature);CHECK(glGetError()==GL_NO_ERROR);
            const auto pixel=[&](const auto& image,std::size_t x,std::size_t y,std::size_t c) {return static_cast<unsigned>(image.rgba8[(y*320U+x)*4U+c]);};
            for(const auto texel:std::array<std::array<std::size_t,2>,4>{{{2,2},{13,2},{2,13},{13,13}}}) {
                const auto xy=neutral_fixture_pixel(scene,*event.texture,texel);
                for(std::size_t c=0;c<4;++c)CHECK(pixel(visible,xy[0],xy[1],c)==pixel(baseline,xy[0],xy[1],c));
            }
            CHECK(pixel(visible,160,120,0)>pixel(visible,160,120,2));
            if(yellow)CHECK(pixel(visible,160,120,1)>pixel(visible,160,120,2));
            else CHECK(pixel(visible,160,120,0)>pixel(visible,160,120,1));
            owner.update(600);scene.additional_world_decals=owner.frame_batches();context->renderer().render(scene,{320,240});
            const auto persistent=context->renderer().observe_framebuffer({320,240},scene.clear_color);REQUIRE(persistent);
            CHECK(persistent.color_signature==visible.color_signature);
            owner.reset();scene.additional_world_decals.clear();context->renderer().render(scene,{320,240});
            const auto reset=context->renderer().observe_framebuffer({320,240},scene.clear_color);REQUIRE(reset);CHECK(reset.color_signature==baseline.color_signature);
        }
    context->release_renderer();
}
TEST_CASE("E30 Null validates bounded neutral decal batches without rendering or throwing on unsupported effects", "[e30][server-decals][null]") {
    const auto world=package();app::WorldImpactPresentation owner;
    REQUIRE(owner.submit_server(request(),world.get()).status==app::ServerDecalStatus::submitted);
    r::null::NullRenderer renderer;renderer.initialize();r::RenderScene scene;
    scene.additional_world_decals=owner.frame_batches();renderer.render(scene,{320,240});
    auto stats=renderer.statistics();CHECK(stats.last_decal_valid==1U);CHECK(stats.last_decal_rejected==0U);
    auto valid=scene.additional_world_decals[0];
    auto coverage=std::make_shared<a::WorldTextureAsset>(*valid.texture);coverage->alpha_mode=a::WorldTextureAlphaMode::straight_alpha;
    valid.texture=coverage;scene.additional_world_decals={valid};renderer.render(scene,{320,240});
    CHECK(renderer.statistics().last_decal_valid==1U);
    auto missing=valid;missing.texture.reset();auto material=valid;material.material_mode=static_cast<api::LocalDecalMaterialMode>(255);
    auto transform=valid;transform.model_transform.values[0]=std::numeric_limits<float>::quiet_NaN();
    auto invalid_vertices=std::make_shared<std::vector<r::RenderDecalVertex>>(*valid.vertices);
    invalid_vertices->front().surface_light_rgb[1]=2;auto geometry=valid;geometry.vertices=invalid_vertices;
    scene.additional_world_decals={missing,material,transform,geometry};REQUIRE_NOTHROW(renderer.render(scene,{320,240}));
    stats=renderer.statistics();CHECK(stats.last_decal_rejected==4U);CHECK(stats.decal_missing_resource==1U);
    CHECK(stats.decal_unsupported_material==1U);CHECK(stats.decal_invalid_transform==1U);CHECK(stats.decal_invalid_geometry==1U);
    scene.additional_world_decals.assign(70U,valid);renderer.render(scene,{320,240});stats=renderer.statistics();
    CHECK(stats.last_decal_valid==64U);CHECK(stats.last_decal_rejected==6U);CHECK(stats.decal_batch_limit==6U);
    renderer.shutdown();
}
TEST_CASE("E30 OpenGL caches64 server and2 legacy decal batches across ordinary frames", "[e30][server-decals][opengl][actual-context][cache]") {
    auto context=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!context)SKIP("OpenGL3.3 context unavailable");context->initialize_renderer();
    const auto world=package();app::WorldImpactPresentation owner;
    std::array<std::shared_ptr<const a::WorldTextureAsset>,12> textures;
    for(std::size_t i=0;i<textures.size();++i)textures[i]=decal(i>=6U);
    for(std::uint32_t n=1;n<=64U;++n) {
        auto event=request(n);event.texture=textures[(n-1U)%textures.size()];
        REQUIRE(owner.submit_server(event,world.get()).status==app::ServerDecalStatus::submitted);
    }
    r::RenderScene scene;scene.camera.position={25,8,8};scene.camera.target={0,8,8};scene.static_world=r::RenderStaticWorld{world};
    scene.additional_world_decals=owner.frame_batches();
    auto legacy=scene.additional_world_decals[0];legacy.material_mode=api::LocalDecalMaterialMode::white_neutral_modulate;
    legacy.vertices=std::make_shared<const std::vector<r::RenderDecalVertex>>(*legacy.vertices);legacy.texture=decal();
    scene.world_decals=legacy;
    legacy.vertices=std::make_shared<const std::vector<r::RenderDecalVertex>>(*legacy.vertices);legacy.texture=decal();
    scene.secondary_world_decals=legacy;
    context->renderer().render(scene,{320,240});const auto first=context->renderer().decal_statistics();
    CHECK(first.last_submitted==66U);CHECK(first.last_rendered==66U);CHECK(first.last_rejected==0U);
    CHECK(first.texture_upload_count==14U);CHECK(first.geometry_upload_count==66U);
    CHECK(first.cached_texture_count==14U);CHECK(first.cached_geometry_count==66U);
    for(int frame_index=0;frame_index<4;++frame_index) {
        scene.additional_world_decals[0].model_transform.values[13]=float(frame_index)*.1F;
        context->renderer().render(scene,{320,240});const auto current=context->renderer().decal_statistics();
        CHECK(current.texture_upload_count==first.texture_upload_count);CHECK(current.geometry_upload_count==first.geometry_upload_count);
        CHECK(current.last_rendered==66U);CHECK(current.resource_budget==0U);CHECK(current.cached_geometry_count<=66U);
    }
    CHECK(glGetError()==GL_NO_ERROR);
    context->release_renderer();
}
TEST_CASE("E30 OpenGL rejects malformed batches and resource excess without leaking pass state", "[e30][server-decals][opengl][actual-context][bounds]") {
    auto context=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!context)SKIP("OpenGL3.3 context unavailable");context->initialize_renderer();
    const auto world=package();app::WorldImpactPresentation owner;
    REQUIRE(owner.submit_server(request(),world.get()).status==app::ServerDecalStatus::submitted);
    const auto valid=owner.frame_batches()[0];r::RenderScene scene;
    scene.camera.position={25,8,8};scene.camera.target={0,8,8};scene.static_world=r::RenderStaticWorld{world};
    context->renderer().render(scene,{320,240});const auto baseline=context->renderer().observe_framebuffer({320,240},scene.clear_color);REQUIRE(baseline);
    const auto state=[] {
        std::array<GLint,12> values{};
        const std::array<GLenum,8> keys{GL_BLEND_SRC_RGB,GL_BLEND_DST_RGB,GL_BLEND_SRC_ALPHA,GL_BLEND_DST_ALPHA,
            GL_BLEND_EQUATION_RGB,GL_BLEND_EQUATION_ALPHA,GL_CULL_FACE_MODE,GL_FRONT_FACE};
        for(std::size_t i=0;i<keys.size();++i)glGetIntegerv(keys[i],&values[i]);
        values[8]=glIsEnabled(GL_BLEND);values[9]=glIsEnabled(GL_CULL_FACE);values[10]=glIsEnabled(GL_DEPTH_TEST);values[11]=glIsEnabled(GL_POLYGON_OFFSET_FILL);
        return values;
    };
    const auto baseline_state=state();
    scene.additional_world_decals={valid};context->renderer().render(scene,{320,240});CHECK(state()==baseline_state);
    auto invalid=valid;invalid.model_transform.values[0]=std::numeric_limits<float>::quiet_NaN();
    auto missing=valid;missing.vertices.reset();auto material=valid;material.material_mode=static_cast<api::LocalDecalMaterialMode>(255);
    scene.additional_world_decals={invalid,missing,material};REQUIRE_NOTHROW(context->renderer().render(scene,{320,240}));
    CHECK(context->renderer().decal_statistics().last_rejected==3U);CHECK(state()==baseline_state);
    const auto invalid_only=context->renderer().observe_framebuffer({320,240},scene.clear_color);REQUIRE(invalid_only);
    CHECK(invalid_only.color_signature==baseline.color_signature);
    scene.additional_world_decals.assign(70U,valid);context->renderer().render(scene,{320,240});
    CHECK(context->renderer().decal_statistics().last_rendered==64U);CHECK(context->renderer().decal_statistics().last_rejected==6U);
    scene.additional_world_decals.clear();
    for(int n=0;n<17;++n){auto batch=valid;batch.texture=decal(n%2);scene.additional_world_decals.push_back(std::move(batch));}
    context->renderer().render(scene,{320,240});const auto limited=context->renderer().decal_statistics();
    CHECK(limited.cached_texture_count==16U);CHECK(limited.last_rendered==16U);CHECK(limited.last_rejected==1U);
    CHECK(limited.resource_budget==1U);CHECK(state()==baseline_state);CHECK(glGetError()==GL_NO_ERROR);
    scene.additional_world_decals.clear();context->renderer().render(scene,{320,240});
    const auto cleared=context->renderer().observe_framebuffer({320,240},scene.clear_color);REQUIRE(cleared);CHECK(cleared.color_signature==baseline.color_signature);
    context->release_renderer();
}
TEST_CASE("E30 adding another placement or local shot never reuploads an unchanged server decal mesh", "[e30][server-decals][opengl][actual-context][cache][revision]") {
    auto context=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!context)SKIP("OpenGL3.3 context unavailable");context->initialize_renderer();
    const auto world=package();app::WorldImpactPresentation owner;
    auto first=request();REQUIRE(owner.submit_server(first,world.get()).status==app::ServerDecalStatus::submitted);
    r::RenderScene scene;scene.camera.position={25,8,8};scene.camera.target={0,8,8};scene.static_world=r::RenderStaticWorld{world};
    scene.additional_world_decals=owner.frame_batches();REQUIRE(scene.additional_world_decals.size()==1U);
    const auto first_mesh=scene.additional_world_decals[0].vertices;
    const auto first_revision=scene.additional_world_decals[0].revision;
    context->renderer().render(scene,{320,240});const auto initial=context->renderer().decal_statistics();
    REQUIRE(initial.geometry_upload_count==1U);REQUIRE(initial.texture_upload_count==1U);
    auto second=request(2,{0,9,8});second.texture=first.texture;
    REQUIRE(owner.submit_server(second,world.get()).status==app::ServerDecalStatus::submitted);
    scene.additional_world_decals=owner.frame_batches();REQUIRE(scene.additional_world_decals.size()==2U);
    CHECK(scene.additional_world_decals[0].vertices==first_mesh);CHECK(scene.additional_world_decals[0].revision==first_revision);
    context->renderer().render(scene,{320,240});const auto added=context->renderer().decal_statistics();
    CHECK(added.geometry_upload_count==initial.geometry_upload_count+1U);CHECK(added.texture_upload_count==initial.texture_upload_count);
    CHECK(added.last_rendered==2U);CHECK(added.last_rejected==0U);
    const auto collision=hlclient::tests::collision_brush_fixture::package(true,0.0,0U);
    api::LocalWorldImpactRequest local;local.action={1,2,59,3,1,api::LocalWeaponAction::primary_fire,.1,1};
    local.origin={5,8,8};local.direction={-1,0,0};local.maximum_distance_units=20;local.decal_half_size_units=4;local.expires_at_seconds=.35;
    REQUIRE(owner.submit(local,collision,world.get(),{},true,.1).status==app::WorldImpactStatus::hit);
    scene.world_decals=owner.frame(decal());REQUIRE(scene.world_decals);
    scene.additional_world_decals=owner.frame_batches();
    CHECK(scene.additional_world_decals[0].vertices==first_mesh);CHECK(scene.additional_world_decals[0].revision==first_revision);
    context->renderer().render(scene,{320,240});const auto shot=context->renderer().decal_statistics();
    CHECK(shot.geometry_upload_count==added.geometry_upload_count+1U);CHECK(shot.last_rendered==3U);
    context->renderer().render(scene,{320,240});CHECK(context->renderer().decal_statistics().geometry_upload_count==shot.geometry_upload_count);
    CHECK(glGetError()==GL_NO_ERROR);context->release_renderer();
}
