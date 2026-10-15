#include "hud_resources_test_fixture_e29.hpp"
#include "world_render_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include <hlclient/app/world_impact_presentation.hpp>
#include <hlclient/goldsrc/decal_assets.hpp>
#include <hlclient/games/halflife/blood_decal_profile.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>

namespace {
namespace a=hlclient::assets;
namespace app=hlclient::app;
namespace api=hlclient::game_api;
namespace g=hlclient::goldsrc;
namespace r=hlclient::renderer;
namespace wf=hlclient::tests::world_render_fixture;
namespace hf=hlclient::tests::hud_resources_e29;
struct Point {std::size_t x{},y{};};
std::shared_ptr<const hlclient::world_render::WorldRenderPackage> wall(
    std::array<std::uint8_t,3> shade,std::uint8_t light,bool occluded=false) {
    auto world=wf::make_world(occluded?2U:1U);
    for(std::size_t i=0;i<world.vertices.size();++i) {
        auto& vertex=world.vertices[i];const auto ordinal=i/4U;
        const auto original=vertex.position;
        vertex.position={ordinal?8.0F:0.0F,(original.x-float(ordinal)*32.0F)*16.0F,original.y*16.0F};
        vertex.normal={1,0,0};
    }
    world.bounds={{0,0,0},{occluded?8.0F:0.0F,256,256}};
    for(std::size_t i=0;i<world.surfaces.size();++i)world.surfaces[i].bounds={{i?8.0F:0.0F,0,0},{i?8.0F:0.0F,256,256}};
    auto texture=wf::make_texture(false);
    for(auto& mip:texture.mip_levels)for(std::size_t p=0;p<mip.rgba_pixels.size()/4U;++p) {
        const auto x=p%mip.width,y=p/mip.width;
        const int difference=((x/2U+y/2U)%2U)?12:-12;
        for(std::size_t channel=0;channel<3;++channel)
            mip.rgba_pixels[p*4U+channel]=static_cast<std::byte>(std::clamp(int(shade[channel])+difference,0,255));
    }
    a::WorldMaterialTextureBinding binding;binding.material_index=0;binding.texture_asset_index=0;binding.source_bsp_texture_index=0;
    binding.status=a::WorldMaterialTextureBindingStatus::resolved_embedded;
    std::vector<a::WorldTextureAsset> textures;textures.push_back(std::move(texture));
    auto texture_set=a::WorldTextureSet::create(std::move(textures),{binding},{},1);REQUIRE(texture_set);
    auto lightmap=wf::make_lightmap_set({.atlas_page_count=occluded?2U:1U});
    std::vector<a::WorldLightmapAtlasPage> pages(lightmap.pages().begin(),lightmap.pages().end());
    std::vector<a::WorldSurfaceLightmapBinding> bindings(lightmap.bindings().begin(),lightmap.bindings().end());
    for(auto& page:pages)for(std::size_t p=0;p<page.style_slot_images[0].rgba_pixels.size();p+=4U)
        for(std::size_t channel=0;channel<3;++channel)page.style_slot_images[0].rgba_pixels[p+channel]=static_cast<std::byte>(light);
    auto lights=a::WorldLightmapSet::create(std::move(pages),std::move(bindings),world.surfaces.size());REQUIRE(lights);
    auto built=hlclient::world_render::WorldRenderPackageBuilder{}.build(
        {std::move(world),std::move(*texture_set.texture_set)},std::move(*lights.lightmap_set));REQUIRE(built);
    return std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*built.package));
}
std::uint8_t coverage(const a::WorldTextureAsset& texture,std::size_t x,std::size_t y) {
    const auto& image=texture.mip_levels[0];return std::to_integer<std::uint8_t>(image.rgba_pixels[(y*image.width+x)*4U+3U]);
}
std::vector<Point> neutral_controls(const a::WorldTextureAsset& texture) {
    std::vector<Point> controls;
    const auto neutral=[&](std::size_t x,std::size_t y) {
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
            if(coverage(texture,std::size_t(int(x)+dx),std::size_t(int(y)+dy))!=0)return false;
        return true;
    };
    for(std::size_t quadrant=0;quadrant<4;++quadrant) {
        bool found=false;
        for(std::size_t y=2;y+2<texture.height && !found;++y)for(std::size_t x=2;x+2<texture.width && !found;++x) {
            if((x>=texture.width/2U)!=(quadrant%2U!=0U) || (y>=texture.height/2U)!=(quadrant/2U!=0U))continue;
            if(neutral(x,y)){controls.push_back({x,y});found=true;}
        }
    }
    // The original16x16 yellow variant has ink across one quadrant. Preserve
    // the SAME alpha0/filter-margin requirement, but do not invent a neutral
    // patch there: fill from actual valid texels, maximizing their separation.
    while(controls.size()<4U) {
        std::optional<Point> candidate;std::size_t best_separation{};
        for(std::size_t y=2;y+2<texture.height;++y)for(std::size_t x=2;x+2<texture.width;++x) {
            if(!neutral(x,y))continue;
            std::size_t separation=std::numeric_limits<std::size_t>::max();
            for(const auto previous:controls) {
                const auto dx=int(x)-int(previous.x),dy=int(y)-int(previous.y);
                separation=std::min(separation,static_cast<std::size_t>(dx*dx+dy*dy));
            }
            if(!separation)continue; // all four controls must be distinct
            if(!candidate || separation>best_separation){candidate=Point{x,y};best_separation=separation;}
        }
        if(!candidate)break;
        controls.push_back(*candidate);
    }
    return controls;
}
Point strongest_ink(const a::WorldTextureAsset& texture) {
    Point result{2,2};std::uint8_t best{};
    for(std::size_t y=2;y+2<texture.height;++y)for(std::size_t x=2;x+2<texture.width;++x)
        if(coverage(texture,x,y)>best){best=coverage(texture,x,y);result={x,y};}
    REQUIRE(best>=128U);return result;
}
Point projected(const r::RenderScene& scene,a::AssetVector3 point) {
    const auto matrix=r::camera_view_projection(scene.camera,{512,512});REQUIRE(matrix);
    const auto p=r::transform(*matrix.matrix,{point.x,point.y,point.z,1});REQUIRE(p.w>0);
    const auto x=std::floor((p.x/p.w*.5F+.5F)*512.0F),y=std::floor((p.y/p.w*.5F+.5F)*512.0F);
    REQUIRE(x>=0);REQUIRE(x<512);REQUIRE(y>=0);REQUIRE(y<512);
    return {static_cast<std::size_t>(x),static_cast<std::size_t>(y)};
}
Point source_pixel(const r::RenderScene& scene,const a::WorldTextureAsset& texture,Point texel) {
    return projected(scene,{0,128+float(texel.x)+.5F-float(texture.width)*.5F,
        128+float(texel.y)+.5F-float(texture.height)*.5F});
}
double sampled_alpha(const r::RenderScene& scene,const a::WorldTextureAsset& texture,Point pixel) {
    const auto distance=scene.camera.position.x;
    const auto pixels_per_unit=512.0/(2.0*std::tan(scene.camera.vertical_field_of_view_radians*.5)*distance);
    const auto x=std::clamp((double(pixel.x)+.5-256.0)/pixels_per_unit+texture.width*.5-.5,0.0,double(texture.width-1U));
    const auto y=std::clamp((double(pixel.y)+.5-256.0)/pixels_per_unit+texture.height*.5-.5,0.0,double(texture.height-1U));
    const auto x0=static_cast<std::size_t>(std::floor(x)),y0=static_cast<std::size_t>(std::floor(y));
    const auto x1=std::min(x0+1U,std::size_t(texture.width-1U)),y1=std::min(y0+1U,std::size_t(texture.height-1U));
    const auto fx=x-x0,fy=y-y0;
    return ((coverage(texture,x0,y0)*(1-fx)+coverage(texture,x1,y0)*fx)*(1-fy)+
        (coverage(texture,x0,y1)*(1-fx)+coverage(texture,x1,y1)*fx)*fy)/255.0;
}
void save(std::string_view label,const r::opengl::OpenGlFramebufferObservation& image) {
    const auto output=hf::environment_value("HLCLIENT_E30_DECAL_GL_OUTPUT");if(output.empty())return;
    const auto repository=std::filesystem::weakly_canonical(std::filesystem::path{__FILE__}.parent_path().parent_path());
    const auto allowed=repository/"manual-artifacts"/"task-records"/"e30-original-blood-20261010";
    const auto target=std::filesystem::weakly_canonical(output);const auto relative=target.lexically_relative(allowed);
    REQUIRE(!relative.empty());REQUIRE(!relative.is_absolute());
    REQUIRE(std::none_of(relative.begin(),relative.end(),[](const auto& component){return component=="..";}));
    REQUIRE(std::all_of(label.begin(),label.end(),[](char c){return (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='-';}));
    std::filesystem::create_directories(target);std::ofstream file{target/(std::string{label}+".ppm"),std::ios::binary};REQUIRE(file);
    file<<"P6\n512 512\n255\n";REQUIRE(image.rgba8.size()==512U*512U*4U);
    std::array<char,512U*3U> row{};
    for(std::size_t y=0;y<512;++y) {
        for(std::size_t x=0;x<512;++x)for(std::size_t channel=0;channel<3;++channel)
            row[x*3U+channel]=static_cast<char>(image.rgba8[((511U-y)*512U+x)*4U+channel]);
        file.write(row.data(),row.size());
    }
    REQUIRE(file.good());
}
}

TEST_CASE("E30 original approved WAD blood textures reach production GPU with lit color and neutral footprint",
    "[e30][decal-assets][server-decals][opengl][actual-context][actual-assets]") {
    const auto basedir=hf::environment_value("HLCLIENT_E30_ASSET_ROOT");
    if(basedir.empty())SKIP("Set HLCLIENT_E30_ASSET_ROOT for approved read-only original WAD GPU verification");
    auto roots=hlclient::local_resources::LocalResourceSearchRoots::create(basedir,"valve");REQUIRE(roots);
    auto made=hlclient::local_resources::LocalResourceEnvironment::create(std::move(*roots.roots));REQUIRE(made);
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment> environment{std::move(made.environment)};
    const auto prepared=g::prepare_decal_assets(hf::resources_for(environment,"maps/crossfire.bsp"),
        hlclient::games::halflife::blood_decal_assets);
    REQUIRE(prepared.archive_status==g::DecalAssetStatus::ready);REQUIRE(prepared.count==12U);
    auto context=hlclient::tests::entity_opengl_fixture::try_context(512,512);
    if(!context)SKIP("OpenGL3.3 context unavailable");context->initialize_renderer();
    const std::array<std::array<std::uint8_t,3>,3> shades{{{40,40,40},{210,210,210},{50,100,180}}};
    std::size_t verified{},neutral_checked{};
    for(std::size_t slot=0;slot<prepared.count;++slot) {
        const auto& asset=prepared.entries[slot];CAPTURE(slot);REQUIRE(asset.status==g::DecalAssetStatus::ready);REQUIRE(asset.texture);
        const auto& texture=*asset.texture;const auto controls=neutral_controls(texture);REQUIRE(controls.size()==4U);
        const auto ink=strongest_ink(texture);
        for(std::size_t shade_index=0;shade_index<shades.size();++shade_index)for(const auto light:{std::uint8_t{48},std::uint8_t{224}}) {
            CAPTURE(shade_index,light);const auto package=wall(shades[shade_index],light);app::WorldImpactPresentation owner;
            app::WorldImpactPresentation::ServerDecalRequest event;
            event.identity={1,slot+1U,0,0};event.record_ordinal=slot+1U;event.resource_generation=1;
            event.position={0,128,128};event.texture=asset.texture;event.material_mode=api::LocalDecalMaterialMode::lit_straight_alpha;
            REQUIRE(owner.submit_server(event,package.get()).status==app::ServerDecalStatus::submitted);
            r::RenderScene scene;scene.static_world=r::RenderStaticWorld{package};
            scene.camera.position={float(std::max(texture.width,texture.height))*1.5F,128,128};
            scene.camera.target={0,128,128};scene.camera.far_plane=2048;
            context->renderer().render(scene,{512,512});const auto before=context->renderer().observe_framebuffer({512,512},scene.clear_color,true);REQUIRE(before);
            scene.additional_world_decals=owner.frame_batches();context->renderer().render(scene,{512,512});
            const auto after=context->renderer().observe_framebuffer({512,512},scene.clear_color,true);REQUIRE(after);
            CHECK(after.color_signature!=before.color_signature);CHECK(context->renderer().decal_statistics().last_rendered==1U);
            for(const auto source:controls) {
                const auto pixel=source_pixel(scene,texture,source);const auto at=(pixel.y*512U+pixel.x)*4U;
                CHECK(std::equal(after.rgba8.begin()+at,after.rgba8.begin()+at+4U,before.rgba8.begin()+at));++neutral_checked;
            }
            const auto pixel=source_pixel(scene,texture,ink);const auto at=(pixel.y*512U+pixel.x)*4U;
            const auto alpha=sampled_alpha(scene,texture,pixel);CHECK(alpha>.25);
            bool changed=false;
            for(std::size_t channel=0;channel<3;++channel) {
                const auto tint=double(asset.audit.palette[255][channel])*light/255.0;
                const auto expected=tint*alpha+double(before.rgba8[at+channel])*(1-alpha);
                CHECK(std::abs(double(after.rgba8[at+channel])-expected)<=3.0);
                changed=changed || after.rgba8[at+channel]!=before.rgba8[at+channel];
            }
            CHECK(changed);CHECK(after.rgba8[at+3U]==before.rgba8[at+3U]);
            for(const auto uv:std::array<std::array<float,2>,4>{{{-.03F,.5F},{1.03F,.5F},{.5F,-.03F},{.5F,1.03F}}}) {
                const auto outside=projected(scene,{0,128+(uv[0]-.5F)*texture.width,128+(uv[1]-.5F)*texture.height});
                const auto p=(outside.y*512U+outside.x)*4U;
                CHECK(std::equal(after.rgba8.begin()+p,after.rgba8.begin()+p+4U,before.rgba8.begin()+p));
            }
            const auto upload=context->renderer().decal_statistics();owner.update(600);scene.additional_world_decals=owner.frame_batches();
            context->renderer().render(scene,{512,512});const auto steady=context->renderer().decal_statistics();
            CHECK(steady.texture_upload_count==upload.texture_upload_count);CHECK(steady.geometry_upload_count==upload.geometry_upload_count);
            const auto persistent=context->renderer().observe_framebuffer({512,512},scene.clear_color);REQUIRE(persistent);CHECK(persistent.color_signature==after.color_signature);
            if((slot==0U || slot==6U) && light==224U) {
                const auto label=std::string{slot?"yellow":"red"}+"-"+std::to_string(shade_index);
                save(label+"-before",before);save(label+"-after",after);
            }
            owner.reset();scene.additional_world_decals.clear();context->renderer().render(scene,{512,512});
            const auto cleared=context->renderer().observe_framebuffer({512,512},scene.clear_color);REQUIRE(cleared);CHECK(cleared.color_signature==before.color_signature);
            ++verified;
        }
        const auto occluder=wall({100,120,140},180,true);app::WorldImpactPresentation hidden;
        app::WorldImpactPresentation::ServerDecalRequest event;
        event.identity={1,slot+1U,0,0};event.record_ordinal=slot+1U;event.resource_generation=1;event.position={0,128,128};
        event.texture=asset.texture;event.material_mode=api::LocalDecalMaterialMode::lit_straight_alpha;
        REQUIRE(hidden.submit_server(event,occluder.get()).status==app::ServerDecalStatus::submitted);
        r::RenderScene scene;scene.static_world=r::RenderStaticWorld{occluder};scene.camera.position={400,128,128};scene.camera.target={0,128,128};
        context->renderer().render(scene,{512,512});const auto before=context->renderer().observe_framebuffer({512,512},scene.clear_color);REQUIRE(before);
        scene.additional_world_decals=hidden.frame_batches();context->renderer().render(scene,{512,512});
        const auto behind=context->renderer().observe_framebuffer({512,512},scene.clear_color);REQUIRE(behind);CHECK(behind.color_signature==before.color_signature);
        CHECK(glGetError()==GL_NO_ERROR);
    }
    CHECK(verified==72U);CHECK(neutral_checked==288U);
    std::cout<<"[e30-original-decal-gpu] variants=12 surface_light_cases="<<verified
        <<" inside_neutral_controls="<<neutral_checked<<" proof=approved_asset_production_gpu stock_capture_parity=not_verified\n";
    context->release_renderer();
}
