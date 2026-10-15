#include "snark_world_test_fixture.hpp"
#include "snark_sound_pop_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include <hlclient/games/halflife/blood_profile.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace {
namespace t=hlclient::tests;
namespace f=t::snark_world_fixture;
namespace api=hlclient::game_api;
namespace app=hlclient::app;
namespace r=hlclient::renderer;
using Catch::Approx;
constexpr unsigned width=320,height=240;

std::filesystem::path environment_path(const wchar_t* name) {
#ifdef _WIN32
    wchar_t* raw=nullptr;std::size_t count{};std::filesystem::path result;
    if(_wdupenv_s(&raw,&count,name)==0 && raw)result=raw;
    std::free(raw);return result;
#else
    (void)name;return {};
#endif
}
// Project-owned coverage only. Geometry deliberately differs between frames,
// catching a maximum-header-bounds or half-extents substitution in world draw.
std::vector<std::byte> sprite(bool spray,bool variable) {
    const int size=spray ? 64 : 16,frames=spray ? 10 : 9;
    auto bytes=t::sprite_fixture::literal_header_and_palette(frames,3,2,0,size,size);
    for(unsigned c=0;c<3;++c)bytes[t::sprite_fixture::kPaletteOffset+3+c]=std::byte{200};
    bytes[t::sprite_fixture::kPaletteOffset+255*3]=std::byte{0};
    bytes[t::sprite_fixture::kPaletteOffset+255*3+1]=std::byte{0};
    bytes[t::sprite_fixture::kPaletteOffset+255*3+2]=std::byte{255};
    for(int frame=0;frame<frames;++frame) {
        const int w=variable && frame%2==0 ? size/2 : size;
        const int h=variable && frame%2==0 ? size/4 : size;
        const int ox=variable && frame%2==0 ? -3 : -w/2;
        const int oy=variable && frame%2==0 ? 5 : h/2;
        t::sprite_fixture::FrameSpec image{ox,oy,w,h,
            std::vector<std::byte>(static_cast<std::size_t>(w*h),std::byte{255})};
        for(int y=1;y<h-1;++y)for(int x=1;x<w-1;++x)
            if(std::abs(x-w/2)*h+std::abs(y-h/2)*w<w*h/3)
                image.pixels[static_cast<std::size_t>(y*w+x)]=std::byte{1};
        t::sprite_fixture::append_single_entry(bytes,image);
    }
    return bytes;
}
struct Assets {
    t::ScopedLocalResourceTestRoot owned;
    hlclient::client::ClientWorldState world;
    std::unique_ptr<app::RuntimeReplayLocalAssets> resources;
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    explicit Assets(bool variable=false,const std::filesystem::path& actual={}) {
        std::filesystem::path root=actual;std::string map="maps/crossfire.bsp";
        if(actual.empty()) {
            (void)t::hand_grenade_world_fixture::owned(owned);
            owned.write("valve","sprites/bloodspray.spr",sprite(true,variable));
            owned.write("valve","sprites/blood.spr",sprite(false,variable));
            owned.write("valve","gfx/palette.lmp",t::effect_palette_fixture::bytes());
            root=owned.path();map="maps/test.bsp";
        }
        const auto table=t::readiness_fixture::parse_resource_list({{2,map,9,0,0},
            {2,"sprites/bloodspray.spr",321,0,0},{2,"sprites/blood.spr",322,0,0}});
        auto made=app::RuntimeReplayLocalAssets::create(table,
            t::readiness_fixture::parse_server_info(map),root,"valve",
            app::ReplayLocalCameraPolicy::external_live_receiving_client,"gfx/palette.lmp");
        REQUIRE(made.projection);resources=std::move(made.projection);
        auto state=t::hand_grenade_world_fixture::state();state.packet_entities.clear();
        t::hand_grenade_world_fixture::stamp(state,1,1);REQUIRE(resources->reset_generation(state,world));
        host.reset({1,1,1});host.set_movement_audio_time_origin(0);
        host.bind_effect_palette(resources->effect_palette());
        REQUIRE(resources->effect_palette().status==api::EffectPaletteStatus::ready);
        host.bind_effect_resource({api::EffectResourceBinding::Kind::model,321,"sprites/bloodspray.spr"});
        host.bind_effect_resource({api::EffectResourceBinding::Kind::model,322,"sprites/blood.spr"});
    }
    api::TemporaryEffectsBatch event(unsigned color,unsigned size,unsigned record=1) {
        auto blood=f::blood(record,color,size);blood.origin={0,0,64};host.committed_temporary_effect(blood);
        auto batch=host.drain_temporary_effects(0);REQUIRE(batch.count==1);return batch;
    }
};
std::unique_ptr<app::TemporaryEffectPresentation> owner(const api::TemporaryEffectsBatch& batch) {
    auto result=std::make_unique<app::TemporaryEffectPresentation>();result->reset(1);result->consume(batch,0);return result;
}
r::RenderScene scene(float distance=128,float fov=1.04719755F) {
    r::RenderScene result;result.clear_color={.1F,.1F,.1F,1};
    result.camera={{0,-distance,64},{0,0,64},{0,0,1},fov,.1F,4096};return result;
}
void present(Assets& assets,app::TemporaryEffectPresentation& effects,r::RenderScene& target,double age) {
    effects.present(target,age);target.transient_sprite_entities=
        assets.resources->materialize_temporary_sprites(effects.sprites(),age);
    CHECK(target.world_particle_count==0);CHECK(target.world_effect_light_count==0);
    CHECK(target.world_flash_count==0);CHECK(target.world_line_count==0);
}
struct Pixels {unsigned count{},left{width},right{},bottom{height},top{};};
Pixels changed(const std::vector<std::uint8_t>& value,const std::vector<std::uint8_t>& base) {
    REQUIRE(value.size()==base.size());Pixels out;
    for(unsigned i=0;i<width*height;++i) {
        if(value[i*4]==base[i*4] && value[i*4+1]==base[i*4+1] && value[i*4+2]==base[i*4+2])continue;
        ++out.count;out.left=std::min(out.left,i%width);out.right=std::max(out.right,i%width);
        out.bottom=std::min(out.bottom,i/width);out.top=std::max(out.top,i/width);
    }
    return out;
}
void export_frame(const std::string& name,const std::vector<std::uint8_t>& pixels) {
    const auto directory=environment_path(L"HLCLIENT_E30_ARTIFACT_DIR");if(directory.empty())return;
    REQUIRE(directory.is_absolute());REQUIRE(std::filesystem::is_directory(directory));
    std::ofstream out{directory/(name+".tga"),std::ios::binary};REQUIRE(out);
    const std::array<unsigned char,18> header{0,0,2,0,0,0,0,0,0,0,0,0,
        static_cast<unsigned char>(width&255),static_cast<unsigned char>(width>>8),
        static_cast<unsigned char>(height&255),static_cast<unsigned char>(height>>8),32,0x20};
    out.write(reinterpret_cast<const char*>(header.data()),header.size());
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        const auto at=(static_cast<std::size_t>(height-1-y)*width+x)*4;
        const std::array<std::uint8_t,4> bgra{pixels[at+2],pixels[at+1],pixels[at],pixels[at+3]};
        out.write(reinterpret_cast<const char*>(bgra.data()),bgra.size());
    }
    REQUIRE(out.good());
}
void gl_comparison(Assets& assets,bool actual) {
    auto gl=t::entity_opengl_fixture::try_context(width,height);
    if(!gl)SKIP("OpenGL 3.3 Core context unavailable");
    REQUIRE(t::entity_opengl_fixture::capable_context());gl->initialize_renderer();
    const auto capture=[&](const r::RenderScene& target) {
        gl->renderer().render(target,{width,height});
        auto result=gl->renderer().observe_framebuffer({width,height},target.clear_color,true);
        REQUIRE(result);return result.rgba8;
    };
    unsigned record=1;
    for(const unsigned color:{247U,195U})for(const unsigned size:{3U,6U,10U,16U}) {
        INFO("color="<<color<<" raw="<<size<<" original_assets="<<actual);
        const auto batch=assets.event(color,size,record++);
        auto spray=batch,drops=batch,old=batch;
        spray.effects[0].sprite_burst.reset();drops.effects[0].sprite.reset();
        // Exact former E27.1 and legacy player profiles, fixture controls only.
        old.effects[0].sprite->scale=static_cast<float>(size)*.1F;
        if(color==247 || size<8) {
            old.effects[0].sprite->tint=color==247 ? std::array<float,3>{1,.05F,.05F} : std::array<float,3>{1,.85F,0};
            old.effects[0].sprite->frames_per_second=10;old.effects[0].sprite->maximum_lifetime_seconds=.6;
            old.effects[0].sprite->fade_with_animation=true;old.effects[0].sprite_burst.reset();
            old.effects[0].secondary_sprite=api::TemporarySpriteIntent{322,size*.05F,-32,1,10,
                api::TemporarySpriteBlend::alpha_blended,.6,old.effects[0].sprite->tint};
        } else old.effects[0].sprite_burst->sprite.scale=.5F;
        auto all=owner(batch),spray_owner=owner(spray),drop_owner=owner(drops),before=owner(old);
        const auto baseline=capture(scene());
        for(const double age:{0.,.1,.25,.45,.7,1.0}) {
            auto target=scene(),spray_target=target,drop_target=target,before_target=target;
            present(assets,*all,target,age);present(assets,*spray_owner,spray_target,age);
            present(assets,*drop_owner,drop_target,age);present(assets,*before,before_target,age);
            const auto rendered=capture(target),isolated=capture(spray_target),drop_pixels=capture(drop_target),previous=capture(before_target);
            if(age<=.25) {
                const auto pixels=changed(isolated,baseline);CHECK(pixels.count>0);
                REQUIRE(spray_target.transient_sprite_entities);
                const auto& instance=spray_target.transient_sprite_entities->frame->sprite_instances()[0];
                const auto& frame=spray_target.transient_sprite_entities->package->sprite_assets()[instance.sprite_asset_index]->frames()[instance.selected_frame_index];
                CHECK(instance.transform.uniform_scale==Approx(size/30.F));
                const float full_width=frame.geometry.width*instance.transform.uniform_scale;
                const float projection=full_width*height/(256.F*std::tan(scene().camera.vertical_field_of_view_radians*.5F));
                CHECK(pixels.right-pixels.left+1<=static_cast<unsigned>(std::ceil(projection))+2U);
                const float projected_height=frame.geometry.height*instance.transform.uniform_scale*height/
                    (256.F*std::tan(scene().camera.vertical_field_of_view_radians*.5F));
                CHECK(pixels.count<static_cast<unsigned>(std::ceil(projection)*std::ceil(projected_height)));
                bool transparent{};
                for(std::size_t alpha=3;alpha<frame.rgba8.size();alpha+=4)
                    transparent=transparent || frame.rgba8[alpha]==std::byte{0};
                CHECK(transparent);
                std::cout<<"e30-blood-size raw="<<size<<" color="<<color<<" source="<<(actual ? "approved" : "owned")
                    <<" frame="<<instance.selected_frame_index<<" frame_width="<<frame.geometry.width
                    <<" frame_height="<<frame.geometry.height<<" origin="<<frame.geometry.source_origin.x<<','<<frame.geometry.source_origin.y
                    <<" effect_scale="<<instance.transform.uniform_scale<<" full_width="<<full_width
                    <<" distance=128 fov=60 viewport=320x240 projected_full_width="<<projection
                    <<" covered_pixel_span="<<pixels.right-pixels.left+1<<'\n';
            }
            if(age==.1 || age==.25) {CHECK(changed(drop_pixels,baseline).count>0);CHECK(rendered!=isolated);CHECK(rendered!=drop_pixels);}
            if(age==0)CHECK(rendered!=previous);
            if(age>=.5)CHECK(isolated==baseline);
            if(age==1) {CHECK(rendered==baseline);CHECK(all->sprites().empty());}
            const auto stamp=std::to_string(static_cast<unsigned>(std::lround(age*1000)));
            const auto label=std::string{actual ? "actual" : "owned"}+"-blood-"+std::to_string(color)+"-"+std::to_string(size)+"-"+stamp;
            export_frame(label+"-before",previous);export_frame(label+"-after",rendered);
            export_frame(label+"-spray",isolated);export_frame(label+"-drops",drop_pixels);
        }
        // World sprite dimensions stay invariant under FOV and viewport policy.
        for(const float fov:{.52359877F,1.04719755F})for(const float distance:{32.F,64.F,128.F}) {
            auto isolated_owner=owner(spray);auto target=scene(distance,fov);present(assets,*isolated_owner,target,.1);
            REQUIRE(target.transient_sprite_entities);CHECK(target.transient_sprite_entities->frame->sprite_instances()[0].transform.uniform_scale==Approx(size/30.F));
            CHECK(changed(capture(target),capture(scene(distance,fov))).count>0);
        }
    }
    GLboolean depth{};glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);CHECK(depth==GL_TRUE);
    CHECK(glIsEnabled(GL_BLEND)==GL_FALSE);CHECK(glGetError()==GL_NO_ERROR);gl->release_renderer();
}
}

TEST_CASE("E30 blood size is a bounded blood-specific profile distinct from count and literal byte multiplier",
    "[e30][blood-sprite][profile][no-stock]") {
    const auto zero=hlclient::games::halflife::blood_sprite_profile(0);
    CHECK(zero.spray_scale==0);CHECK(zero.drop_scale==0);CHECK(zero.drop_count==0);
    for(unsigned raw=1;raw<=255;++raw) {
        const auto profile=hlclient::games::halflife::blood_sprite_profile(static_cast<std::uint8_t>(raw));
        CHECK(profile.spray_scale==Approx(raw/30.F));CHECK(profile.drop_scale==Approx(raw/20.F));
        CHECK(profile.spray_scale>=raw/35.F);CHECK(profile.spray_scale<=raw/25.F);
        CHECK(profile.drop_scale>=raw/25.F);CHECK(profile.drop_scale<=raw/15.F);
        CHECK(profile.drop_count==std::min(16U,raw+9U));CHECK(profile.drop_count<=16);
        CHECK(profile.spray_scale<=25.5F);CHECK(profile.drop_scale<=25.5F);
    }
    CHECK(64.F*hlclient::games::halflife::blood_sprite_profile(16).spray_scale==Approx(34.133333F));
    CHECK(16.F*hlclient::games::halflife::blood_sprite_profile(16).drop_scale==Approx(12.8F));
}
TEST_CASE("E30 received red and yellow fields share verified palette geometry without Snark numeric classification",
    "[e30][blood-sprite][game-api][no-stock]") {
    Assets assets(true);unsigned record=1;
    for(const unsigned color:{195U,247U})for(const unsigned size:{1U,3U,6U,10U,16U,255U}) {
        auto batch=assets.event(color,size,record++);const auto& effect=batch.effects[0];
        REQUIRE(effect.sprite);REQUIRE(effect.sprite_burst);CHECK_FALSE(effect.secondary_sprite);
        CHECK(effect.identity.source==api::TemporaryEffectIdentity::Source::server_record);
        CHECK(effect.sprite->model_slot==321);CHECK(effect.sprite_burst->sprite.model_slot==322);
        CHECK(effect.sprite->scale==Approx(size/30.F));CHECK(effect.sprite_burst->sprite.scale==Approx(size/20.F));
        CHECK(effect.sprite_burst->count==std::min(16U,size+9U));
        const auto& palette=assets.resources->effect_palette().colors[color];
        for(unsigned channel=0;channel<3;++channel)CHECK(effect.sprite->tint[channel]==Approx(palette[channel]/255.F));
        CHECK(effect.sprite_burst->sprite.tint==effect.sprite->tint);
        CHECK_FALSE(effect.sound);CHECK_FALSE(effect.light);CHECK_FALSE(effect.particles);
        auto effects=owner(batch);auto target=scene();present(assets,*effects,target,0);
        REQUIRE(target.transient_sprite_entities);const auto& draw=*target.transient_sprite_entities;
        REQUIRE(draw.frame->sprite_instances().size()==1U+effect.sprite_burst->count);
        const auto& spray=draw.frame->sprite_instances()[0];const auto& frame=draw.package->sprite_assets()[spray.sprite_asset_index]->frames()[0];
        CHECK(frame.geometry.width==32);CHECK(frame.geometry.height==16);
        CHECK(frame.geometry.source_origin.x==-3);CHECK(frame.geometry.source_origin.y==5);
        CHECK(frame.geometry.local_corners[0].x==-3);CHECK(frame.geometry.local_corners[0].y==-11);
        CHECK(frame.geometry.local_corners[2].x==29);CHECK(frame.geometry.local_corners[2].y==5);
        CHECK(spray.transform.uniform_scale==Approx(size/30.F));
        CHECK(assets.host.drain_audio().count==0);CHECK(assets.host.drain_remote_audio().count==0);
    }
}
TEST_CASE("E30 missing palette rejects every blood color and never replays previously missed sprites",
    "[e30][blood-sprite][palette][no-stock]") {
    for(const unsigned color:{195U,247U}) {
        f::Owned assets;assets.host.bind_effect_palette({});assets.host.committed_temporary_effect(f::blood(1,color,3));
        auto batch=assets.host.drain_temporary_effects(0);CHECK(batch.count==0);CHECK(batch.statistics.blood_palette_unavailable==1);
        assets.bind_effects();assets.host.committed_temporary_effect(f::blood(1,color,3));
        CHECK(assets.host.drain_temporary_effects(0).count==0);
        assets.host.committed_temporary_effect(f::blood(2,color,3));CHECK(assets.host.drain_temporary_effects(0).count==1);
    }
}
TEST_CASE("E30 blood absolute animation drop trajectories focus and stalls do not depend on render cadence",
    "[e30][blood-sprite][clock][focus][no-stock]") {
    Assets assets;const auto batch=assets.event(247,3);
    for(const unsigned fps:{30U,60U,144U,240U}) {
        auto effects=owner(batch);auto target=scene();
        for(unsigned tick=0;tick<fps/5;++tick) {target=scene();present(assets,*effects,target,static_cast<double>(tick)/fps);}
        assets.host.cancel_uncommitted();target=scene();present(assets,*effects,target,.2);
        REQUIRE(target.transient_sprite_entities);const auto& instances=target.transient_sprite_entities->frame->sprite_instances();
        REQUIRE(instances.size()==13);CHECK(instances[0].selected_frame_index==4);
        for(std::size_t i=1;i<instances.size();++i) {
            const auto& drop=effects->sprites()[i];CHECK(instances[i].transform.origin.x==Approx(drop.velocity.x*.2F));
            CHECK(instances[i].transform.origin.y==Approx(drop.velocity.y*.2F));
            CHECK(instances[i].transform.origin.z==Approx(64+drop.velocity.z*.2F-16.F));
            CHECK(instances[i].transform.uniform_scale==Approx(.15F));
        }
        target=scene();present(assets,*effects,target,3);CHECK(effects->sprites().empty());CHECK_FALSE(target.transient_sprite_entities);
        target=scene();present(assets,*effects,target,.2);CHECK(effects->sprites().empty()); // No time regression/resurrection.
        effects->consume(batch,3);CHECK(effects->statistics().stale==1);
    }
}
TEST_CASE("E30 project owned blood original-sized frames use production OpenGL isolated spray drop and before controls",
    "[e30][blood-sprite][opengl][no-stock]") {Assets assets;gl_comparison(assets,false);}
TEST_CASE("E30 approved original blood SPR palette and scale reach normal OpenGL with isolated before after",
    "[e30][blood-sprite][opengl][actual-assets][optional-local]") {
    const auto root=environment_path(L"HLCLIENT_LOCAL_GAME_ROOT");
    if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())
        SKIP("Exact approved research root opt-in absent; project-owned GL remains mandatory");
    Assets assets(false,root);gl_comparison(assets,true);
}
