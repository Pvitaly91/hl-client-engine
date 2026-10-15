#include "rpg_test_fixture.hpp"
#include "local_resource_readiness_test_fixture.hpp"
#include "goldsrc_sprite_test_fixture.hpp"
#include "goldsrc_studio_test_fixture.hpp"
#include "synthetic_goldsrc_bsp_fixture.hpp"
#include "synthetic_goldsrc_wad3_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include "world_render_test_fixture.hpp"
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>
#include <hlclient/games/halflife/rpg_profile.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/goldsrc/sound_assets.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace f=hlclient::tests::rpg_fixture;
namespace t=hlclient::tests;
namespace c=hlclient::client;
namespace g=hlclient::goldsrc;
namespace app=hlclient::app;
namespace r=hlclient::renderer;
namespace api=hlclient::game_api;
namespace readiness=t::readiness_fixture;
using Catch::Approx;
using namespace std::chrono_literals;
namespace {
c::RuntimeClientObservationState observation(unsigned ordinal=1,float x=0,float y=0,unsigned effects=0) {
    auto s=f::state();s.publication_revision=ordinal;s.server_time_seconds=ordinal*.1;
    s.entity_metadata=s.server_time_metadata=s.client_metadata={1,c::RuntimeObservationFreshness::observed_in_record,
        c::RuntimeObservationCompleteness::complete_reconstruction,c::RuntimeObservationSource{ordinal,ordinal,1,0,8}};
    c::RuntimePacketEntityObservation e;e.entity_number=40;e.model_index=74;e.origin={x,y,0.0};e.angles={0.0,359.0,0.0};
    e.ordinary_visual_schema=true;e.brush_move_type=10;e.sequence=0;e.frame=0;e.effects=effects;
    s.packet_entities.push_back(e);s.canonical_state_hash=c::runtime_observation_canonical_hash(s);return s;
}
c::CommittedTemporaryEffect follow(unsigned record=1) {
    c::CommittedTemporaryEffect e;e.generation=1;e.record=e.record_ordinal=record;e.received_at_seconds=0;
    e.kind=c::TemporaryEffectKind::beam_follow;e.entity_attachment=40;e.sprite_index=19;
    e.life_tenths=40;e.width_tenths=5;e.color={224,224,255};e.brightness=255;return e;
}
struct Samples:g::SoundAssets {
    std::shared_ptr<const hlclient::assets::AudioAsset> pcm;
    Samples(bool loop=false) {
        auto a=std::make_shared<hlclient::assets::AudioAsset>();a->sample_rate=48000;a->channel_count=1;
        a->interleaved_samples.assign(4800,.125F);if(loop)a->loop=hlclient::assets::AudioAsset::Loop{0,4800};pcm=a;
    }
    g::SoundAssetResult request(std::uint16_t) noexcept override {return {g::SoundAssetStatus::ready,pcm};}
    g::SoundAssetResult request_local(const api::LocalSoundReference&) noexcept override {return {g::SoundAssetStatus::ready,pcm};}
};
std::unique_ptr<app::RuntimeReplayLocalAssets> owned(t::ScopedLocalResourceTestRoot& root,bool glow=true) {
    t::SyntheticBspBuilder bsp;const std::string text="{\n\"classname\" \"worldspawn\"\n\"wad\" \"test.wad\"\n}\n";
    const auto eb=std::as_bytes(std::span{text});bsp.lump(t::SyntheticBspLumpId::entities).assign(eb.begin(),eb.end());
    t::SyntheticWad3Entry texture;texture.name="TEST_QUAD";texture.payload=t::synthetic_goldsrc_miptex("TEST_QUAD",64,64);
    root.write("valve","test.wad",t::synthetic_wad3({texture}).bytes);root.write("valve","maps/test.bsp",bsp.build());
    root.write("valve","models/rpgrocket.mdl",t::literal_minimal_goldsrc_studio_v10());
    root.write("valve","sprites/laserdot.spr",t::sprite_fixture::literal_single_sprite(1));
    root.write("valve","sprites/smoke.spr",t::sprite_fixture::literal_single_sprite(1));
    if(glow)root.write("valve","sprites/hotglow.spr",t::sprite_fixture::literal_single_sprite(1));
    auto resources=readiness::parse_resource_list({{2,"maps/test.bsp",9,0,0},{2,"models/rpgrocket.mdl",74,0,0},
        {2,"sprites/laserdot.spr",18,0,0},{2,"sprites/smoke.spr",19,0,0},{2,"sprites/hotglow.spr",20,0,0}});
    auto result=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/test.bsp"),root.path(),"valve",
        app::ReplayLocalCameraPolicy::external_live_receiving_client);REQUIRE(result.projection);return std::move(result.projection);
}
std::size_t changed(const std::vector<std::byte>& a,const std::vector<std::byte>& b) {
    REQUIRE(a.size()==b.size());std::size_t count=0;
    for(std::size_t i=0;i<a.size();i+=4) if(a[i]!=b[i] || a[i+1]!=b[i+1] || a[i+2]!=b[i+2]) ++count;
    return count;
}
void project(app::RuntimeReplayLocalAssets& assets,c::RuntimeClientObservationState& s,c::ClientWorldState& world) {
    s.canonical_state_hash=c::runtime_observation_canonical_hash(s);
    const auto result=assets.project_entities(s,world);INFO((result.error ? result.error->context : "ready"));REQUIRE(result);
}
#if defined(_WIN32)
struct StackProjectionCheck {
    std::unique_ptr<app::RuntimeReplayLocalAssets> assets;
    c::RuntimeClientObservationState state;
    c::ClientWorldState world;
    unsigned completed{};
};
DWORD WINAPI project_with_bounded_stack(void* parameter) noexcept {
    auto& check=*static_cast<StackProjectionCheck*>(parameter);
    try {
        for(unsigned iteration=0;iteration<20;++iteration) {
            if(!check.assets->reset_generation(check.state,check.world)) return 1;
            if(!check.assets->project_entities(check.state,check.world)) return 2;
            ++check.completed;
        }
        return 0;
    } catch(...) { return 3; }
}
#endif
}
TEST_CASE("E20 ready application projection resets and publishes within the Win32 remaining stack budget",
          "[e20][stack-budget][runtime-replay][no-stock]") {
#if defined(_WIN32)
    t::ScopedLocalResourceTestRoot root;
    auto check=std::make_unique<StackProjectionCheck>();
    check->assets=owned(root);check->state=observation();
    // The normal Win32 live loop retains ~699 KiB of its 1 MiB stack. Exercise
    // the same production projection/reset on a smaller 256 KiB reservation;
    // the test executable's shallow caller must not mask pool-copy growth.
    const auto thread=CreateThread(nullptr,256U*1024U,project_with_bounded_stack,
        check.get(),STACK_SIZE_PARAM_IS_A_RESERVATION,nullptr);
    REQUIRE(thread!=nullptr);
    const auto wait=WaitForSingleObject(thread,INFINITE);
    DWORD result=~DWORD{};
    const auto read=GetExitCodeThread(thread,&result);
    const auto closed=CloseHandle(thread);
    REQUIRE(wait==WAIT_OBJECT_0);REQUIRE(read);CHECK(closed);
    CHECK(result==0);CHECK(check->completed==20);
    CHECK(check->assets->entity_effect_statistics().received==0);
    CHECK(check->assets->summary().projected_frames>=40);
#else
    SKIP("Explicit Win32 thread stack reservation requires Windows");
#endif
}
TEST_CASE("E20 server follow trail samples curved presented origins with payload width and bounded history", "[e20][temporary-effects][lifecycle]") {
    app::EntityFollowPresentation p;auto s=observation();p.observe(s);p.consume(follow(),0);p.consume(follow(),0);
    CHECK(p.statistics().accepted==1);CHECK(p.statistics().duplicate==1);
    r::RenderScene scene;p.present(s.packet_entities,scene,0);CHECK(p.segments().empty());
    s=observation(2,10);p.observe(s);p.present(s.packet_entities,scene,.1);
    s=observation(3,10,10);p.observe(s);p.present(s.packet_entities,scene,.2);
    REQUIRE(p.segments().size()==2);CHECK(p.segments()[0].end.x==10);CHECK(p.segments()[1].end.y==10);
    CHECK(p.segments()[0].width==Approx(.5));CHECK(p.segments()[0].sprite_slot==19);
    CHECK(p.segments()[0].color[0]==Approx(224.0/255));
    p.present(s.packet_entities,scene,.2);CHECK(p.segments().size()==2);
    const auto id=p.lifetime(40);s=observation(4);s.packet_entities.clear();p.observe(s);
    p.present({},scene,.3);CHECK(p.segments().size()==2);CHECK(p.statistics().detached==1);
    s=observation(5,100);p.observe(s);CHECK(p.lifetime(40)!=id);p.present(s.packet_entities,scene,.4);
    CHECK(p.segments().size()==2);CHECK(p.segments().back().end.y==10); // no old trail reattaches
    p.present(s.packet_entities,scene,4.3);CHECK(p.segments().empty());
    auto fresh=follow(2);fresh.received_at_seconds=4.3;p.consume(fresh,4.3);p.present(s.packet_entities,scene,4.3);
    s=observation(6,110);p.observe(s);p.present(s.packet_entities,scene,4.4);CHECK(p.segments().size()==1);
    p.reset(2);CHECK(p.segments().empty());CHECK(p.lifetime(40)==0);
}
TEST_CASE("E20 follow discontinuity NOINTERP attachment rejection and capacity remain explicit", "[e20][temporary-effects][bounds]") {
    app::EntityFollowPresentation p;auto s=observation();p.observe(s);
    auto e=follow();e.entity_attachment|=0x1000;p.consume(e,0);CHECK(p.statistics().invalid==1);
    p.consume(follow(2),0);r::RenderScene scene;p.present(s.packet_entities,scene,0);
    s=observation(2,600);p.observe(s);p.present(s.packet_entities,scene,.1);CHECK(p.segments().empty());
    s=observation(3,601,0,32);p.observe(s);p.present(s.packet_entities,scene,.2);CHECK(p.segments().empty());
    CHECK(p.statistics().discontinuity==2);
    s.packet_entities.clear();for(unsigned i=1;i<=33;++i) {auto entity=observation().packet_entities[0];entity.entity_number=i;s.packet_entities.push_back(entity);}
    s.entity_metadata.source->record_ordinal=4;p.observe(s);
    for(unsigned i=1;i<=33;++i) {e=follow(i+2);e.entity_attachment=static_cast<std::uint16_t>(i);e.received_at_seconds=.2;p.consume(e,.2);}
    CHECK(p.statistics().capacity>=1); // the detached original keeps history until decay
}
TEST_CASE("E20 EF_LIGHT follows server bit origin removal independently of local life", "[e20][lifecycle][renderer]") {
    app::EntityFollowPresentation p;auto s=observation(1,4,8,64);p.observe(s);r::RenderScene scene;
    p.present(s.packet_entities,scene,0);REQUIRE(scene.world_effect_light_count==1);
    CHECK(scene.world_effect_lights[0].center.x==4);CHECK(scene.world_effect_lights[0].center.y==8);
    s=observation(2,7,12,64);s.lifecycle.state=c::LocalPlayerLifeState::dead;p.observe(s);
    scene.world_effect_light_count=0;p.present(s.packet_entities,scene,.1);CHECK(scene.world_effect_light_count==1);
    CHECK(scene.world_effect_lights[0].center.y==12);
    s=observation(3,7,12,0);p.observe(s);scene.world_effect_light_count=0;p.present(s.packet_entities,scene,.2);
    CHECK(scene.world_effect_light_count==0);
    s.packet_entities.clear();s.entity_metadata.source->record_ordinal=4;p.observe(s);p.present({},scene,.3);
    CHECK(scene.world_effect_light_count==0);
}
TEST_CASE("E20 RPG smoke profile is selected through the active game host for the exact approved resources",
          "[e20][rpg-flight-smoke][game-api][no-stock]") {
    f::Module module;
    const auto profile=module.host.entity_trail_profile("models/rpgrocket.mdl","sprites/smoke.spr");
    REQUIRE(profile);
    CHECK(profile->spacing_units==4);CHECK(profile->initial_diameter==6);CHECK(profile->final_diameter==22);
    CHECK(profile->lifetime_seconds==Approx(1.2));CHECK(profile->rise_units_per_second==6);CHECK(profile->head_glow_radius==5);
    CHECK(std::string_view{profile->head_sprite.data()}=="sprites/hotglow.spr");
    CHECK(profile->head_axis_offset==-1);CHECK(profile->smoke_opacity==Approx(.85));
    CHECK_FALSE(module.host.entity_trail_profile("models/grenade.mdl","sprites/smoke.spr"));
    CHECK_FALSE(module.host.entity_trail_profile("models/rpgrocket.mdl","sprites/laserdot.spr"));
    CHECK_FALSE(module.host.entity_trail_profile("","sprites/smoke.spr"));
    module.host.teardown();
    CHECK_FALSE(module.host.entity_trail_profile("models/rpgrocket.mdl","sprites/smoke.spr"));
}
TEST_CASE("E20 RPG smoke samples distance grows and fades without repeated stationary emission",
          "[e20][rpg-flight-smoke][temporary-effects][lifecycle][no-stock]") {
    f::Module module;const auto profile=module.host.entity_trail_profile("models/rpgrocket.mdl","sprites/smoke.spr");
    REQUIRE(profile);app::EntityFollowPresentation p;auto s=observation(1,0,0,64);p.observe(s);
    p.consume(follow(),0,profile);p.consume(follow(),0,profile);
    CHECK(p.statistics().accepted==1);CHECK(p.statistics().duplicate==1);
    r::RenderScene scene;p.present(s.packet_entities,scene,0);
    REQUIRE(p.segments().size()==1);CHECK(p.segments()[0].puff);CHECK(p.segments()[0].width==6);
    CHECK(p.segments()[0].age_fraction==0);CHECK(scene.world_flash_count==0);CHECK(p.heads().empty());
    s=observation(2,16,0,64);p.observe(s);scene={};p.present(s.packet_entities,scene,.1);
    REQUIRE(p.segments().size()==5);
    for(std::size_t i=0;i<5;++i) {
        const auto& puff=p.segments()[i];CHECK(puff.puff);CHECK(puff.sprite_slot==19);
        CHECK(puff.start.x==Approx(float(i)*4).margin(.601));CHECK(std::abs(puff.start.y)<=.601F);
        CHECK(puff.end.x==puff.start.x);CHECK(puff.end.y==puff.start.y);CHECK(puff.end.z==puff.start.z);
        CHECK(puff.width>=6);CHECK(puff.width<=22);CHECK(puff.opacity>0);CHECK(puff.opacity<=.85F);
    }
    const auto first=p.segments()[0];const auto sampled=p.statistics().sampled;
    scene={};p.present(s.packet_entities,scene,.1);CHECK(p.statistics().sampled==sampled);CHECK(p.segments().size()==5);
    scene={};p.present(s.packet_entities,scene,.2);CHECK(p.statistics().sampled==sampled);REQUIRE(p.segments().size()==5);
    CHECK(p.segments()[0].width>first.width);CHECK(p.segments()[0].opacity<first.opacity);
    CHECK(p.segments()[0].start.z>first.start.z);
    const auto old_life=p.lifetime(40);s=observation(3);s.packet_entities.clear();p.observe(s);
    scene={};p.present({},scene,.3);CHECK(p.segments().size()==5);CHECK(scene.world_flash_count==0);
    s=observation(4,100,0,64);p.observe(s);CHECK(p.lifetime(40)!=old_life);
    scene={};p.present(s.packet_entities,scene,.4);CHECK(p.statistics().sampled==sampled);CHECK(scene.world_flash_count==0);
    for(const auto& puff:p.segments()) CHECK(puff.start.x<=16.601F); // detached smoke cannot follow a reused entity slot
    scene={};p.present(s.packet_entities,scene,1.4);CHECK(p.segments().empty());
    auto next=follow(2);next.received_at_seconds=1.4;p.consume(next,1.4,profile);
    scene={};p.present(s.packet_entities,scene,1.4);REQUIRE(p.segments().size()==1);CHECK(p.segments()[0].start.x==Approx(100).margin(.601));
    p.reset(2);CHECK(p.segments().empty());CHECK(p.lifetime(40)==0);
}
TEST_CASE("E20 RPG smoke discontinuities and server visibility retire the emitter head",
          "[e20][rpg-flight-smoke][temporary-effects][lifecycle][no-stock]") {
    f::Module module;const auto profile=module.host.entity_trail_profile("models/rpgrocket.mdl","sprites/smoke.spr");
    REQUIRE(profile);
    for(const unsigned boundary:{0U,1U,2U}) {
        CAPTURE(boundary);app::EntityFollowPresentation p;auto s=observation(1,0,0,64);p.observe(s);p.consume(follow(),0,profile);
        r::RenderScene scene;p.present(s.packet_entities,scene,0);
        s=observation(2,16,0,64);p.observe(s);scene={};p.present(s.packet_entities,scene,.1);REQUIRE(p.segments().size()==5);
        const float x=boundary==0 ? 600.0F : 24.0F;const double now=boundary==1 ? .5 : .2;
        s=observation(3,x,0,boundary==2 ? 96U : 64U);p.observe(s);scene={};p.present(s.packet_entities,scene,now);
        CHECK(p.segments().empty());CHECK(p.statistics().discontinuity==1);
        s=observation(4,x+8,0,64);p.observe(s);scene={};p.present(s.packet_entities,scene,now+.05);
        REQUIRE_FALSE(p.segments().empty());for(const auto& puff:p.segments()) CHECK(puff.start.x>=x);
        s=observation(5,x+8,0,0);p.observe(s);scene={};p.present(s.packet_entities,scene,now+.1);
        CHECK(scene.world_flash_count==0);CHECK(scene.world_effect_light_count==0);
        s=observation(6,x+8,0,192);p.observe(s);scene={};p.present(s.packet_entities,scene,now+.15);
        CHECK(scene.world_flash_count==0);CHECK(scene.world_effect_light_count==0);
    }
}
TEST_CASE("E20 nozzle sprite follows presented model axis and server lifetime independently of smoke",
          "[e20][rpg-flight-smoke][rpg-nozzle][no-stock]") {
    f::Module module;const auto profile=module.host.entity_trail_profile("models/rpgrocket.mdl","sprites/smoke.spr");
    app::EntityFollowPresentation p;auto s=observation(1,10,20,64);s.packet_entities[0].angles={30.0,90.0,15.0};
    p.observe(s);p.consume(follow(),0,profile,20);r::RenderScene scene;p.present(s.packet_entities,scene,0);
    REQUIRE(p.heads().size()==1);const auto head=p.heads()[0];CHECK(head.sprite_slot==20);CHECK(head.puff);
    CHECK(head.width==10);CHECK(head.opacity==1);CHECK(head.color==std::array<float,3>{1,1,1});
    CHECK(head.start.x==Approx(10));CHECK(head.start.y==Approx(20-std::sqrt(.75)));CHECK(head.start.z==Approx(-.5));
    CHECK(scene.world_flash_count==0); // Real textured sprite, not a procedural substitute.
    scene={};p.present(s.packet_entities,scene,1.3);CHECK(p.segments().empty());CHECK(p.heads().size()==1);
    p.present(s.packet_entities,scene,1.3);CHECK(p.heads().size()==1); // no duplicate retained heads
    s=observation(2,40,10,64);s.packet_entities[0].angles={-90.0,0.0,0.0};p.observe(s);
    scene={};p.present(s.packet_entities,scene,1.4);REQUIRE(p.heads().size()==1);
    CHECK(p.heads()[0].start.x==Approx(40));CHECK(p.heads()[0].start.z==Approx(1));
    for(const auto flag:{0U,128U,192U}) {
        s.packet_entities[0].effects=flag;scene={};p.present(s.packet_entities,scene,1.5);CHECK(p.heads().empty());
    }
    s.packet_entities[0].effects=64;s.packet_entities[0].angles.x.reset();
    scene={};p.present(s.packet_entities,scene,1.6);CHECK(p.heads().empty());
    s=observation(3);s.packet_entities.clear();p.observe(s);p.present({},scene,1.7);CHECK(p.heads().empty());
    s=observation(4,100,0,64);p.observe(s);p.present(s.packet_entities,scene,1.8);CHECK(p.heads().empty());
    auto fresh=follow(2);fresh.received_at_seconds=1.8;p.consume(fresh,1.8,profile,20);
    p.present(s.packet_entities,scene,1.8);CHECK(p.heads().size()==1);p.reset(2);CHECK(p.heads().empty());
}
TEST_CASE("E20 nozzle real approved sprite produces isolated GPU pixels and missing resource keeps smoke",
          "[e20][rpg-flight-smoke][rpg-nozzle][opengl][no-stock]") {
    auto context=t::entity_opengl_fixture::try_context();if(!context) SKIP("OpenGL 3.3 Core unavailable");
    REQUIRE(t::entity_opengl_fixture::capable_context());context->initialize_renderer();
    for(const bool ready:{true,false}) {
        CAPTURE(ready);t::ScopedLocalResourceTestRoot root;auto assets=owned(root,ready);f::Module module;
        auto s=observation(1,0,0,64);s.packet_entities[0].angles={0.0,0.0,0.0};c::ClientWorldState world;
        s.canonical_state_hash=c::runtime_observation_canonical_hash(s);
        REQUIRE(assets->reset_generation(s,world));assets->consume_follow(follow(),0,&module.host);
        r::RenderScene scene;assets->present_entity_effects(scene,0);REQUIRE(scene.follow_trail_entities);
        CHECK(scene.follow_trail_entities->frame->sprite_instances().size()==(ready ? 2U : 1U));
        // Stationary smoke expires; head stays attached and alone, so pixels
        // cannot pass solely because smoke or the generic point light exists.
        assets->present_entity_effects(scene,1.3);scene.world_effect_light_count=0;
        CHECK(scene.world_flash_count==0);CHECK(bool(scene.follow_trail_entities)==ready);
        if(ready) {
            REQUIRE(scene.follow_trail_entities->frame->sprite_instances().size()==1);
            const auto& sprite=scene.follow_trail_entities->frame->sprite_instances()[0];
            CHECK_FALSE(sprite.ribbon_end);CHECK(sprite.transform.origin.x==Approx(-1));
            for(const auto eye:std::array{hlclient::assets::AssetVector3{-30,0,0},hlclient::assets::AssetVector3{-1,-30,0}}) {
                scene.camera={eye,{-1,0,0},{0,0,1},1.04719755F,.1F,4096};
                auto blank=scene;blank.follow_trail_entities.reset();context->renderer().render(blank,{96,96});
                const auto base=t::entity_opengl_fixture::framebuffer();context->renderer().render(scene,{96,96});
                CHECK(changed(t::entity_opengl_fixture::framebuffer(),base)>4);CHECK(glGetError()==GL_NO_ERROR);
            }
        } else CHECK(assets->follow_resource_statistics().absent>0);
        s.packet_entities.clear();s.entity_metadata.source->record_ordinal=2;project(*assets,s,world);
        assets->present_entity_effects(scene,1.4);CHECK_FALSE(scene.follow_trail_entities);
    }
}
TEST_CASE("E20 RPG smoke rejects invalid profiles and bounds retained and presented particles",
          "[e20][rpg-flight-smoke][temporary-effects][bounds][no-stock]") {
    f::Module module;const auto profile=module.host.entity_trail_profile("models/rpgrocket.mdl","sprites/smoke.spr");REQUIRE(profile);
    std::vector<api::EntityTrailProfile> invalid;
    auto broken=*profile;broken.spacing_units=0;invalid.push_back(broken);
    broken=*profile;broken.initial_diameter=-1;invalid.push_back(broken);
    broken=*profile;broken.final_diameter=std::numeric_limits<float>::infinity();invalid.push_back(broken);
    broken=*profile;broken.lifetime_seconds=0;invalid.push_back(broken);
    broken=*profile;broken.rise_units_per_second=std::numeric_limits<float>::quiet_NaN();invalid.push_back(broken);
    broken=*profile;broken.head_glow_radius=-1;invalid.push_back(broken);
    broken=*profile;broken.head_axis_offset=65;invalid.push_back(broken);
    broken=*profile;broken.smoke_opacity=1.1F;invalid.push_back(broken);
    broken=*profile;broken.head_sprite.fill('x');invalid.push_back(broken);
    for(const auto& bad:invalid) {
        app::EntityFollowPresentation p;auto s=observation();p.observe(s);p.consume(follow(),0,bad);
        r::RenderScene scene;p.present(s.packet_entities,scene,0);
        CHECK(p.statistics().accepted==0);CHECK(p.statistics().invalid==1);CHECK(p.segments().empty());
    }
    app::EntityFollowPresentation p;auto s=observation();s.packet_entities.clear();
    for(unsigned i=1;i<=33;++i) {auto entity=observation().packet_entities[0];entity.entity_number=i;s.packet_entities.push_back(entity);}
    p.observe(s);
    for(unsigned i=1;i<=33;++i) {auto event=follow(i);event.entity_attachment=static_cast<std::uint16_t>(i);p.consume(event,0,profile);}
    CHECK(p.statistics().accepted==32);CHECK(p.statistics().capacity==1);
    r::RenderScene scene;p.present(s.packet_entities,scene,0);
    for(unsigned tick=1;tick<=6;++tick) {
        for(auto& entity:s.packet_entities) entity.origin.x=256.0*tick;
        s.entity_metadata.source->record_ordinal=tick+1;p.observe(s);scene={};p.present(s.packet_entities,scene,tick*.02);
        CHECK(p.segments().size()<=512);
    }
    CHECK(p.segments().size()==512);const auto sampled=p.statistics().sampled;
    scene={};p.present(s.packet_entities,scene,.12);CHECK(p.statistics().sampled==sampled);
    s.packet_entities.clear();s.entity_metadata.source->record_ordinal=8;p.observe(s);scene={};p.present({},scene,1.5);
    CHECK(p.segments().empty());CHECK(scene.world_flash_count==0);
}
TEST_CASE("E20 normal application RPG smoke remains textured and visible head on then expires",
          "[e20][rpg-flight-smoke][opengl][runtime-replay][no-stock]") {
    auto context=t::entity_opengl_fixture::try_context();if(!context) SKIP("OpenGL 3.3 Core unavailable");
    REQUIRE(t::entity_opengl_fixture::capable_context());context->initialize_renderer();
    t::ScopedLocalResourceTestRoot root;auto assets=owned(root);f::Module module;auto s=observation();c::ClientWorldState world;
    REQUIRE(assets->reset_generation(s,world));assets->consume_follow(follow(),0,&module.host);
    r::RenderScene scene;assets->present_entity_effects(scene,0);REQUIRE(scene.follow_trail_entities);
    REQUIRE(scene.follow_trail_entities->frame->sprite_instances().size()==1);
    const auto scale=scene.follow_trail_entities->frame->sprite_instances().front().transform.uniform_scale;
    s=observation(2,16);project(*assets,s,world);assets->present_entity_effects(scene,.1);
    REQUIRE(scene.follow_trail_entities);REQUIRE(scene.follow_trail_entities->frame->sprite_instances().size()==5);
    for(const auto& sprite:scene.follow_trail_entities->frame->sprite_instances()) CHECK_FALSE(sprite.ribbon_end);
    const auto sample_count=assets->entity_effect_statistics().sampled;
    assets->present_entity_effects(scene,.2);CHECK(assets->entity_effect_statistics().sampled==sample_count);
    REQUIRE(scene.follow_trail_entities);
    CHECK(scene.follow_trail_entities->frame->sprite_instances().front().transform.uniform_scale>scale);
    auto& renderer=context->renderer();
    for(const auto camera:std::array{r::RenderCamera{{40,0,0},{0,0,0},{0,0,1},1.04719755F,.1F,4096},
            r::RenderCamera{{8,-35,8},{8,0,1},{0,0,1},1.04719755F,.1F,4096}}) {
        scene.camera=camera;scene.clear_color={.02F,.03F,.04F,1};
        auto blank=scene;blank.follow_trail_entities.reset();renderer.render(blank,{96,96});
        const auto baseline=t::entity_opengl_fixture::framebuffer();renderer.render(scene,{96,96});
        CHECK(changed(t::entity_opengl_fixture::framebuffer(),baseline)>4);CHECK(glGetError()==GL_NO_ERROR);
    }
    s.packet_entities.clear();s.entity_metadata.source->record_ordinal=3;project(*assets,s,world);
    assets->present_entity_effects(scene,1.4);CHECK_FALSE(scene.follow_trail_entities);
    auto blank=scene;blank.follow_trail_entities.reset();renderer.render(blank,{96,96});
    const auto baseline=t::entity_opengl_fixture::framebuffer();renderer.render(scene,{96,96});
    CHECK(t::entity_opengl_fixture::framebuffer()==baseline);
    GLboolean depth{},writes{};glGetBooleanv(GL_DEPTH_TEST,&depth);glGetBooleanv(GL_DEPTH_WRITEMASK,&writes);
    CHECK(depth==GL_TRUE);CHECK(writes==GL_TRUE);CHECK(glIsEnabled(GL_BLEND)==GL_FALSE);
}
TEST_CASE("E20 normal approved owned assets render glow and textured follow with NODRAW clear removal", "[e20][opengl][runtime-replay]") {
    auto context=t::entity_opengl_fixture::try_context();if(!context) SKIP("OpenGL 3.3 Core unavailable");
    REQUIRE(t::entity_opengl_fixture::capable_context());context->initialize_renderer();
    auto& renderer=context->renderer();
    const auto pixels=[&](r::RenderScene scene) {
        scene.clear_color={.02F,.03F,.04F,1};
        scene.camera={{0,-20,5},{0,0,0},{0,0,1},1.04719755F,.1F,4096};
        renderer.render(scene,{96,96});CHECK(glGetError()==GL_NO_ERROR);
        return t::entity_opengl_fixture::framebuffer();
    };
    t::ScopedLocalResourceTestRoot root;auto a=owned(root);auto s=observation();c::ClientWorldState world;
    REQUIRE(a->reset_generation(s,world));auto spot=s.packet_entities[0];spot.entity_number=41;spot.model_index=18;
    spot.render_mode=3;spot.render_effect=14;spot.render_amount=255;spot.brush_move_type=0;
    s.packet_entities.push_back(spot);s.entity_metadata.source->record_ordinal=2;s.publication_revision=2;
    project(*a,s,world);REQUIRE(world.entity_frame()->sprite_instances().size()==1);
    CHECK(world.entity_frame()->sprite_instances()[0].blend_mode==hlclient::entity_render::SpriteBlendMode::glow);
    a->consume_follow(follow(),0);r::RenderScene scene;a->present_entity_effects(scene,0);
    s.packet_entities[0].origin.x=8.0;s.entity_metadata.source->record_ordinal=3;project(*a,s,world);
    a->present_entity_effects(scene,.1);REQUIRE(scene.follow_trail_entities);
    REQUIRE(scene.follow_trail_entities->frame->sprite_instances().size()==1);
    CHECK(scene.follow_trail_entities->frame->sprite_instances()[0].ribbon_width==Approx(.5));
    const auto trail=pixels(scene);auto without_trail=scene;without_trail.follow_trail_entities.reset();
    const auto baseline=pixels(without_trail);CHECK(changed(trail,baseline)>4);
    a->present_entity_effects(scene,3.0);CHECK(changed(pixels(scene),baseline)>0);
    a->present_entity_effects(scene,4.2);CHECK(pixels(scene)==baseline);
    scene.follow_trail_entities.reset();scene.dynamic_entities=r::RenderDynamicEntities{world.entity_scene(),world.entity_frame(),{}};
    const auto visible=pixels(scene);
    s.packet_entities[1].effects=128;s.entity_metadata.source->record_ordinal=4;project(*a,s,world);
    CHECK(world.entity_frame()->sprite_instances().empty());
    scene.dynamic_entities=r::RenderDynamicEntities{world.entity_scene(),world.entity_frame(),{}};
    const auto hidden=pixels(scene);CHECK(changed(visible,hidden)>4);
    s.packet_entities[1].effects=0;s.entity_metadata.source->record_ordinal=5;project(*a,s,world);
    CHECK(world.entity_frame()->sprite_instances().size()==1);
    scene.dynamic_entities=r::RenderDynamicEntities{world.entity_scene(),world.entity_frame(),{}};
    CHECK(pixels(scene)==visible);
    for(const float distance:{2.0F,16.0F,64.0F}) {
        hlclient::entity_render::EntityRenderFrameBuildInput input;
        input.resource_id=0xe20;input.resource_revision=1;
        input.interpolation={0,0,0,0,1,1,hlclient::entity_render::EntityRenderInterpolationProfile::decoded_discrete_runtime_replay_v1};
        auto sprite=world.entity_frame()->sprite_instances()[0];
        sprite.transform.origin={0,-20+.9701425F*distance,5-.2425356F*distance};
        const auto o=sprite.transform.origin;sprite.bounds={{o.x-16,o.y-16,o.z-16},{o.x+16,o.y+16,o.z+16}};
        input.sprite_instances.push_back(sprite);
        auto frame=hlclient::entity_render::EntityRenderFrameBuilder{}.build(*world.entity_scene(),std::move(input));REQUIRE(frame);
        r::RenderScene near_scene;near_scene.dynamic_entities=r::RenderDynamicEntities{world.entity_scene(),
            std::make_shared<const hlclient::entity_render::EntityRenderFrame>(std::move(*frame.frame)),{}};
        const auto changed_pixels=changed(pixels(near_scene),baseline);CAPTURE(distance,changed_pixels);
        CHECK(changed_pixels>0);CHECK(changed_pixels<96U*96U/4U); // no fullscreen near-camera glow
    }
    GLboolean depth{},writes{};glGetBooleanv(GL_DEPTH_TEST,&depth);glGetBooleanv(GL_DEPTH_WRITEMASK,&writes);
    CHECK(depth==GL_TRUE);CHECK(writes==GL_TRUE);CHECK(glIsEnabled(GL_BLEND)==GL_FALSE);
    s.packet_entities.clear();s.entity_metadata.source->record_ordinal=6;project(*a,s,world);
    a->present_entity_effects(scene,4.2);CHECK_FALSE(scene.follow_trail_entities);
}
TEST_CASE("E20 server laser dot is occluded by world depth through normal materialization",
          "[e20][opengl][runtime-replay][world-glow-occlusion][no-stock]") {
    auto context=t::entity_opengl_fixture::try_context();
    if(!context || !t::entity_opengl_fixture::capable_context())SKIP("OpenGL 3.3 Core unavailable");
    context->initialize_renderer();
    t::ScopedLocalResourceTestRoot root;auto assets=owned(root);c::ClientWorldState world;
    auto state=observation();REQUIRE(assets->reset_generation(state,world));
    auto spot=state.packet_entities.front();spot.entity_number=41;spot.model_index=18;
    spot.render_mode=3;spot.render_effect=14;spot.render_amount=255;spot.brush_move_type=0;
    spot.origin={8.,12.,-4.};spot.visual_scale=.5;state.packet_entities={spot};
    state.publication_revision=2;state.entity_metadata.source->record_ordinal=2;
    project(*assets,state,world);
    REQUIRE(world.entity_frame()->sprite_instances().size()==1);
    CHECK(world.entity_frame()->sprite_instances()[0].blend_mode==hlclient::entity_render::SpriteBlendMode::glow);
    auto world_result=t::world_render_fixture::make_package();REQUIRE(world_result);
    r::RenderScene scene;scene.camera={{8,-12,6},{8,12,-4},{0,0,1},1.04719755F,.1F,4096};
    scene.static_world=r::RenderStaticWorld{
        std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*world_result.package)),
        r::RenderCullMode::none,r::RenderBaselineLightStylePolicy::source_slot_zero};
    const auto pixels=[&](const r::RenderScene& input) {
        context->renderer().render(input,{96,96});CHECK(glGetError()==GL_NO_ERROR);
        return t::entity_opengl_fixture::framebuffer();
    };
    const auto baseline=pixels(scene);
    scene.dynamic_entities=r::RenderDynamicEntities{world.entity_scene(),world.entity_frame(),{}};
    CHECK(pixels(scene)==baseline);
    auto unobstructed=scene;unobstructed.static_world.reset();
    const auto visible=pixels(unobstructed);unobstructed.dynamic_entities.reset();
    CHECK(changed(visible,pixels(unobstructed))>0);
    state.packet_entities.front().origin={8.,0.,1.};
    state.publication_revision=3;state.entity_metadata.source->record_ordinal=3;
    project(*assets,state,world);
    scene.dynamic_entities=r::RenderDynamicEntities{world.entity_scene(),world.entity_frame(),{}};
    CHECK(changed(pixels(scene),baseline)>0);
    scene.dynamic_entities.reset();CHECK(pixels(scene)==baseline);
    CHECK(glIsEnabled(GL_DEPTH_TEST)==GL_TRUE);CHECK(glIsEnabled(GL_BLEND)==GL_FALSE);
    context->release_renderer();
}
TEST_CASE("E20 normal host presents server rocket flight independently of local death and entity removal",
          "[e20][composition][runtime-replay][projectile][no-stock]") {
    t::ScopedLocalResourceTestRoot root;auto assets=owned(root);f::Module module;
    c::ClientWorldState world;world.set_camera({{96,-64,48},{0,0,16},{0,0,1}});
    auto first=observation();first.server_time_seconds=1;
    first.canonical_state_hash=c::runtime_observation_canonical_hash(first);REQUIRE(assets->reset_generation(first,world));
    REQUIRE(assets->present_entities(module.host,99,1,world,{320,240}));
    REQUIRE(world.entity_frame()->studio_instances().size()==1);
    auto second=observation(2,200,50,64);second.server_time_seconds=1.1;
    project(*assets,second,world);REQUIRE(assets->present_entities(module.host,99.1,1,world,{320,240}));
    REQUIRE(assets->present_entities(module.host,99.15,1,world,{320,240}));
    CHECK(world.entity_frame()->studio_instances()[0].transform.origin.x==Approx(100).margin(.002));
    CHECK(world.entity_frame()->studio_instances()[0].transform.origin.y==Approx(25).margin(.002));
    module.observed.receiving_client->health=0;module.observed.publication_revision=2;module.host.observe(module.observed,.2);
    REQUIRE(assets->present_entities(module.host,99.17,1,world,{320,240}));
    CHECK(world.entity_frame()->studio_instances().size()==1);
    auto removed=observation(3);removed.server_time_seconds=1.2;removed.packet_entities.clear();project(*assets,removed,world);
    REQUIRE(assets->present_entities(module.host,100,1,world,{320,240}));CHECK(world.entity_frame()->studio_instances().empty());
    CHECK(module.host.drain_temporary_effects(.2).count==0);CHECK(module.host.drain_audio().count==0);
}

TEST_CASE("E20 server MOVETYPE_NONE glow dot moves between updates through normal presentation",
          "[e20][laser-jitter][composition][runtime-replay][no-stock]") {
    t::ScopedLocalResourceTestRoot root;
    auto assets=owned(root);
    f::Module module;
    c::ClientWorldState world;
    const auto dot=[](unsigned ordinal,double x,double y=0.0,unsigned effects=0) {
        auto state=observation(ordinal);
        state.server_time_seconds=1.0+(ordinal-1U)*.02;
        auto& entity=state.packet_entities.front();
        entity.entity_number=41;entity.model_index=18;entity.brush_move_type=0;
        entity.origin={x,y,0.0};entity.render_mode=3;entity.render_effect=14;
        entity.render_amount=255;entity.effects=effects;
        state.canonical_state_hash=c::runtime_observation_canonical_hash(state);
        return state;
    };
    auto first=dot(1,0.0);
    SECTION("server glow sprite") {}
    SECTION("ordinary non-glow static sprite is not opted in") {first.packet_entities.front().render_mode=0;}
    first.canonical_state_hash=c::runtime_observation_canonical_hash(first);
    REQUIRE(assets->reset_generation(first,world));
    REQUIRE(assets->present_entities(module.host,100.0,1,world,{320,240}));
    auto second=dot(2,8.0);
    second.packet_entities.front().render_mode=first.packet_entities.front().render_mode;
    project(*assets,second,world);
    const auto immutable=second;
    REQUIRE(assets->present_entities(module.host,100.02,1,world,{320,240}));
    for(unsigned tick=1;tick<=4;++tick) {
        REQUIRE(assets->present_entities(module.host,100.02+tick*.005,1,world,{320,240}));
        REQUIRE(world.entity_frame()->sprite_instances().size()==1);
        const auto& sprite=world.entity_frame()->sprite_instances().front();
        CHECK(sprite.entity_number==41);
        const double expected=second.packet_entities.front().render_mode==3 ? tick*2.0 : 8.0;
        CHECK(sprite.transform.origin.x==Approx(expected).margin(.002));
    }
    CHECK(second==immutable); // Render samples must not change server state/hash.
    CHECK(module.host.drain_audio().count==0);
    CHECK(module.host.drain_temporary_effects(.04).count==0);
    if(second.packet_entities.front().render_mode==0) return;
    auto third=dot(3,8.0,8.0);
    project(*assets,third,world);
    REQUIRE(assets->present_entities(module.host,100.04,1,world,{320,240}));
    REQUIRE(assets->present_entities(module.host,100.05,1,world,{320,240}));
    REQUIRE(world.entity_frame()->sprite_instances().size()==1);
    CHECK(world.entity_frame()->sprite_instances().front().transform.origin.x==Approx(8).margin(.002));
    CHECK(world.entity_frame()->sprite_instances().front().transform.origin.y==Approx(4).margin(.002));
    const auto retained=world.entity_frame();
    REQUIRE(assets->present_entities(module.host,100.05,1,world,{320,240}));
    CHECK(world.entity_frame()->sprite_instances().front().transform.origin.x==retained->sprite_instances().front().transform.origin.x);
    CHECK(world.entity_frame()->sprite_instances().front().transform.origin.y==retained->sprite_instances().front().transform.origin.y);
    REQUIRE(assets->present_entities(module.host,100.049,1,world,{320,240}));
    CHECK(world.entity_frame()->sprite_instances().front().transform.origin.x==retained->sprite_instances().front().transform.origin.x);
    CHECK(world.entity_frame()->sprite_instances().front().transform.origin.y==retained->sprite_instances().front().transform.origin.y);
    auto hidden=dot(4,16.0,8.0,128);
    project(*assets,hidden,world);
    REQUIRE(assets->present_entities(module.host,100.06,1,world,{320,240}));
    CHECK(world.entity_frame()->sprite_instances().empty()); // Immediate server hide, never delayed by history.
    auto visible=dot(5,24.0,8.0);
    project(*assets,visible,world);
    REQUIRE(assets->present_entities(module.host,100.08,1,world,{320,240}));
    CHECK(world.entity_frame()->sprite_instances().size()==1);
    auto removed=dot(6,24.0);removed.packet_entities.clear();
    project(*assets,removed,world);
    REQUIRE(assets->present_entities(module.host,100.10,1,world,{320,240}));
    CHECK(world.entity_frame()->sprite_instances().empty());
    auto reused=dot(7,64.0);
    project(*assets,reused,world);
    REQUIRE(assets->present_entities(module.host,100.12,1,world,{320,240}));
    REQUIRE(world.entity_frame()->sprite_instances().size()==1);
    CHECK(world.entity_frame()->sprite_instances().front().transform.origin.x==64.0F);
    auto nointerp=dot(8,80.0,0.0,32);
    project(*assets,nointerp,world);
    REQUIRE(assets->present_entities(module.host,100.14,1,world,{320,240}));
    CHECK(world.entity_frame()->sprite_instances().front().transform.origin.x==80.0F);
    auto teleport=dot(9,1024.0);
    project(*assets,teleport,world);
    REQUIRE(assets->present_entities(module.host,100.16,1,world,{320,240}));
    CHECK(world.entity_frame()->sprite_instances().front().transform.origin.x==1024.0F);
}

TEST_CASE("E20 local and remote rocket launch reach isolated mixer independently of server motor", "[e20][audio][weapon-audio]") {
    for(const bool remote:{false,true}) {
        CAPTURE(remote);f::Module m;
        if(remote) {m.host.committed_scripted_event(t::crossbow_fixture::event(false,2));
            CHECK(m.host.drain_remote_effects(.03).count==0);}
        else {m.host.submit(t::crossbow_fixture::command(),.02);(void)m.host.sample(.02);}
        const auto launch=remote ? m.host.drain_remote_audio() : m.host.drain_audio();
        REQUIRE(launch.count==1);CHECK(launch.cues[0].reference.name()=="weapons/rocketfire1.wav");
        CHECK(launch.cues[0].world_origin.has_value()==remote);
        hlclient::audio::OfflineOutput sink;g::ServerAudio server{sink};server.reset(1);
        Samples one_shots;g::LocalAudio local{sink,remote};local.update(launch,&one_shots,.03,true);
        CHECK(local.statistics().submitted==1);CHECK(sink.mixer.active()==1);
        local.update(launch,&one_shots,.03,true);CHECK(local.statistics().submitted==1);CHECK(sink.mixer.active()==1);
        std::array<float,128> pcm{};sink.mixer.render(pcm);
        // This PCM window contains only the RPG launch, before any motor or explosion.
        CHECK(std::ranges::all_of(pcm,[](float v){return std::isfinite(v);}));
        CHECK(std::ranges::any_of(pcm,[](float v){return v!=0;}));
        Samples loops(true);g::CommittedSound e;e.generation=1;e.record=e.ordinal=1;e.cursor=16;
        e.opcode=g::RuntimeControlOpcode::svc_sound;e.sound.entity_reference=40;e.sound.channel=3;e.sound.sound_reference=9;
        server.consume(e,{});const std::array sources{g::SoundSource{40,{20,0,0},1}};server.present(sources,{}, {},1);server.update(&loops,{});
        CHECK(sink.mixer.active()==2);CHECK(server.statistics().started==1);
        server.present({}, {},g::SoundTime{}+100ms,1);CHECK(sink.mixer.active()==1);CHECK(server.statistics().attachment_invalidated==1);
        e.ordinal=2;e.cursor=24;server.consume(e,{});const std::array newer{g::SoundSource{40,{40,0,0},2}};
        server.present(newer,{}, {},1);server.update(&loops,{});CHECK(sink.mixer.active()==2);
        e.ordinal=3;e.cursor=32;e.sound.field_mask=32;server.consume(e,{});CHECK(sink.mixer.active()==1);
    }
}
TEST_CASE("E20 optional actual approved RPG model roles sprite and audio dependencies", "[e20][installed-rpg][local-capture-assets]") {
    std::filesystem::path root;
#ifdef _WIN32
    wchar_t* value=nullptr;std::size_t count{};if(_wdupenv_s(&value,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && value)root=value;std::free(value);
#endif
    if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal()) SKIP("Exact approved read-only root opt-in absent");
    auto resources=readiness::parse_resource_list({{2,"maps/crossfire.bsp",9,0,0},{2,"models/v_rpg.mdl",73,0,0},
        {2,"models/p_rpg.mdl",71,0,0},{2,"models/rpgrocket.mdl",74,0,0},{2,"models/w_rpg.mdl",75,0,0},
        {2,"models/w_rpgammo.mdl",76,0,0},{2,"sprites/laserdot.spr",18,0,0},{2,"sprites/smoke.spr",19,0,0},
        {0,"weapons/rocket1.wav",9,0,0},{2,"sprites/hotglow.spr",20,0,0}});
    auto a=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/crossfire.bsp"),root,"valve",
        app::ReplayLocalCameraPolicy::external_live_receiving_client);REQUIRE(a.projection);
    const auto world_package=a.projection->surface_scene()->world_package();
    REQUIRE(world_package->flat_sky_background());
    CHECK(world_package->flat_sky_background()->rgb==std::array<std::uint8_t,3>{181,222,255});
    c::ClientWorldState world;REQUIRE(a.projection->reset_generation(observation(),world));
    const auto meta=a.projection->presentation_model(1,73);REQUIRE(meta);
    for(std::size_t i=0;i<meta->sequences.size();++i) std::cout<<"rpg_actual_sequence="<<i<<","<<meta->sequences[i].label<<","<<meta->sequences[i].fps<<","<<meta->sequences[i].frame_count<<'\n';
    CHECK(hlclient::games::halflife::rpg_sequences(*meta));
    for(unsigned slot:{71U,74U,75U,76U}) {const auto model=a.projection->studio_model_data(slot);REQUIRE(model);
        std::cout<<"rpg_actual_model="<<slot<<" bones="<<model->bones.size()<<" sequences="<<model->sequences.size()<<" attachments="<<model->attachments.size()<<'\n';}
    g::ApprovedSoundAssets loader(a.projection->sound_resources());
    for(const auto name:{"weapons/rocketfire1.wav","weapons/rocket1.wav"}) {
        api::LocalSoundReference reference;reference.source=api::SoundReferenceSource::pinned_halflife_client_sound_profile;
        std::copy(name,name+std::char_traits<char>::length(name),reference.sample.begin());
        auto sample=std::string_view{name}=="weapons/rocket1.wav" ? loader.request(9) : loader.request_local(reference);
        const auto deadline=std::chrono::steady_clock::now()+2s;
        while(sample.status==g::SoundAssetStatus::pending && std::chrono::steady_clock::now()<deadline) {
            std::this_thread::sleep_for(1ms);sample=std::string_view{name}=="weapons/rocket1.wav" ? loader.request(9) : loader.request_local(reference);
        }
        REQUIRE(sample.status==g::SoundAssetStatus::ready);REQUIRE(sample.asset);
        CHECK(std::ranges::any_of(sample.asset->interleaved_samples,[](float v){return std::isfinite(v)&&v!=0;}));
        std::cout<<"rpg_actual_audio="<<name<<" rate="<<sample.asset->sample_rate<<" channels="<<sample.asset->channel_count
            <<" samples="<<sample.asset->interleaved_samples.size()<<" loop="<<sample.asset->loop.has_value()<<'\n';
        if(std::string_view{name}=="weapons/rocket1.wav") CHECK(sample.asset->loop);
        hlclient::audio::OfflineOutput sink;hlclient::audio::Command command;
        command.operation=hlclient::audio::Operation::start;command.voice=1;command.asset=sample.asset;
        command.volume=1;command.pitch=1;command.local=true;REQUIRE(sink.submit(command));
        std::vector<float> mixed(sample.asset->interleaved_samples.size()*2);sink.mixer.render(mixed);
        CHECK(std::ranges::any_of(mixed,[](float v){return std::isfinite(v)&&v!=0;}));
    }
    auto context=t::entity_opengl_fixture::try_context();if(!context) SKIP("Actual asset GPU control: OpenGL 3.3 unavailable");
    REQUIRE(t::entity_opengl_fixture::capable_context());context->initialize_renderer();
    // Actual approved v_rpg, same module -> neutral intent -> normal viewmodel
    // materializer/renderer, including a repeated unchanged clientdata role.
    f::Module reload_module(0);reload_module.host.bind_model(*meta);
    auto old_reload=f::state(0,2);old_reload.receiving_client->weapon_animation=2;
    old_reload.weapon_slots[0].in_reload=false;reload_module.host.observe(old_reload,0);
    (void)reload_module.host.sample(2.1);
    auto reloading=f::state(0,3);reloading.receiving_client->weapon_animation=2;
    reloading.weapon_slots[0].in_reload=true;reloading.receiving_client->next_weapon_attack=2;
    reload_module.host.observe(reloading,2.2);
    r::RenderScene reload_scene;reload_scene.camera={{0,0,0},{1,0,0},{0,0,1},1.04719755F,.1F,4096};
    context->renderer().render(reload_scene,{96,96});const auto empty_view=t::entity_opengl_fixture::framebuffer();
    const auto draw_reload=[&](double now){
        const auto sample=reload_module.host.sample(now);REQUIRE(sample.visual);
        const auto pose=reload_module.host.viewmodel(reloading,meta,now,sample.visual);
        REQUIRE(pose.status==api::ViewmodelStatus::ready);CHECK(pose.sequence==2);
        reload_scene.first_person_entities=a.projection->materialize_viewmodel(pose);REQUIRE(reload_scene.first_person_entities);
        context->renderer().render(reload_scene,{96,96});CHECK(glGetError()==GL_NO_ERROR);
        return t::entity_opengl_fixture::framebuffer();
    };
    const auto reload_start=draw_reload(2.2);CHECK(changed(reload_start,empty_view)>8);
    const auto reload_middle=draw_reload(3.2);CHECK(changed(reload_middle,reload_start)>8);
    auto s=observation();auto spot=s.packet_entities.front();spot.entity_number=41;spot.model_index=18;
    spot.render_mode=3;spot.render_effect=14;spot.render_amount=255;spot.brush_move_type=0;
    s.packet_entities.push_back(spot);s.entity_metadata.source->record_ordinal=2;project(*a.projection,s,world);
    REQUIRE(world.entity_frame()->sprite_instances().size()==1);
    r::RenderScene scene;scene.camera={{0,-20,5},{0,0,0},{0,0,1},1.04719755F,.1F,4096};
    auto& renderer=context->renderer();renderer.render(scene,{96,96});const auto baseline=t::entity_opengl_fixture::framebuffer();
    r::RenderScene sky;sky.static_world=r::RenderStaticWorld{world_package};sky.clear_color={.035F,.055F,.14F,1};
    sky.camera={{50000,50000,50000},{50000,50000,50001},{0,1,0},1.04719755F,.1F,2};
    renderer.render(sky,{96,96});const auto sky_pixels=t::entity_opengl_fixture::framebuffer();
    for(std::size_t i=0;i<sky_pixels.size();i+=4) {
        CHECK(sky_pixels[i]==std::byte{181});CHECK(sky_pixels[i+1]==std::byte{222});CHECK(sky_pixels[i+2]==std::byte{255});
    }
    scene.dynamic_entities=r::RenderDynamicEntities{world.entity_scene(),world.entity_frame(),{}};
    renderer.render(scene,{96,96});CHECK(changed(t::entity_opengl_fixture::framebuffer(),baseline)>0);
    f::Module module;a.projection->consume_follow(follow(),0,&module.host);r::RenderScene trail;trail.camera=scene.camera;
    a.projection->present_entity_effects(trail,0);s.packet_entities[0].origin.x=40.0;
    s.entity_metadata.source->record_ordinal=3;project(*a.projection,s,world);
    a.projection->present_entity_effects(trail,.1);REQUIRE(trail.follow_trail_entities);
    REQUIRE(trail.follow_trail_entities->frame->sprite_instances().size()>1);
    for(const auto& sprite:trail.follow_trail_entities->frame->sprite_instances()) CHECK_FALSE(sprite.ribbon_end);
    renderer.render(trail,{96,96});CHECK(changed(t::entity_opengl_fixture::framebuffer(),baseline)>0);
    trail.camera={{80,0,0},{0,0,0},{0,0,1},1.04719755F,.1F,4096};
    auto no_smoke=trail;no_smoke.follow_trail_entities.reset();renderer.render(no_smoke,{96,96});
    const auto axial_baseline=t::entity_opengl_fixture::framebuffer();renderer.render(trail,{96,96});
    CHECK(changed(t::entity_opengl_fixture::framebuffer(),axial_baseline)>0);
    a.projection->present_entity_effects(trail,.99);REQUIRE(trail.follow_trail_entities);
    CHECK(std::ranges::any_of(trail.follow_trail_entities->frame->sprite_instances(),
        [](const auto& sprite){return sprite.selected_frame_index==4;})); // actual smoke.spr: all five frames, including final fade
    s.packet_entities.clear();s.entity_metadata.source->record_ordinal=4;project(*a.projection,s,world);
    a.projection->present_entity_effects(trail,4.2);CHECK_FALSE(trail.follow_trail_entities);
    renderer.render(trail,{96,96});CHECK(t::entity_opengl_fixture::framebuffer()==axial_baseline);CHECK(glGetError()==GL_NO_ERROR);
    // Original approved hotglow alone, after all smoke expired: prove actual
    // sprite upload/draw, not just the synthetic fixture or smoke contribution.
    s=observation(5,0,0,64);s.packet_entities[0].angles={0.0,0.0,0.0};project(*a.projection,s,world);
    auto second=follow(2);second.received_at_seconds=4.3;a.projection->consume_follow(second,4.3,&module.host);
    trail={};a.projection->present_entity_effects(trail,4.3);a.projection->present_entity_effects(trail,5.6);
    REQUIRE(trail.follow_trail_entities);REQUIRE(trail.follow_trail_entities->frame->sprite_instances().size()==1);
    const auto& nozzle=trail.follow_trail_entities->frame->sprite_instances()[0];
    CHECK(nozzle.transform.origin.x==Approx(-1));CHECK(nozzle.transform.uniform_scale==Approx(10.0/64));
    trail.world_effect_light_count=0;trail.camera={{-30,0,0},{-1,0,0},{0,0,1},1.04719755F,.1F,4096};
    auto no_glow=trail;no_glow.follow_trail_entities.reset();renderer.render(no_glow,{96,96});
    const auto glow_baseline=t::entity_opengl_fixture::framebuffer();renderer.render(trail,{96,96});
    CHECK(changed(t::entity_opengl_fixture::framebuffer(),glow_baseline)>8);CHECK(glGetError()==GL_NO_ERROR);
}
