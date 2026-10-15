#include "snark_world_test_fixture.hpp"
#include "delta_test_fixture.hpp"
#include "player_origin_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include <hlclient/goldsrc/runtime_replay_fixture.hpp>
#include <hlclient/games/halflife/blood_profile.hpp>
#include <catch2/catch_approx.hpp>
#include <limits>
#include <memory>
namespace {
namespace t=hlclient::tests;namespace f=t::snark_world_fixture;namespace wf=t::hand_grenade_world_fixture;
namespace c=hlclient::client;namespace g=hlclient::goldsrc;namespace api=hlclient::game_api;namespace r=hlclient::renderer;
using Catch::Approx;
constexpr unsigned component_count(unsigned) {return 17U;} // Profiles below use size16/16/7: each capped16 drops.
}
TEST_CASE("E27 living Snark exact server model never falls back to pickup nest","[e27][snark-world][resources][no-stock]") {
    for(const unsigned slot:{74U,237U})for(const bool available:{false,true}) {
        f::Owned o(slot,available);o.present(10);
        CHECK(o.world.entity_frame()->studio_instances().size()==(available ? 1U : 0U));
        REQUIRE(o.assets->studio_model_data(312));CHECK(o.assets->studio_model_data(312)->sequences[1].label=="owned_pickup");
        if(available) {REQUIRE(o.assets->studio_model_data(slot));CHECK(o.assets->studio_model_data(slot)->sequences[3].label=="owned_run");}
        o.state.packet_entities[0].model_index=312;o.state.packet_entities[0].sequence=1;o.publish(2,1.1,10.1);
        CHECK(o.world.entity_frame()->studio_instances().size()==1);CHECK(o.assets->summary().model_names.at(312)=="models/w_sqknest.mdl");
        o.state.packet_entities[0].model_index=999;o.publish(3,1.2,10.2);CHECK(o.world.entity_frame()->studio_instances().empty());
    }
}
TEST_CASE("E27 multiple BOUNCE Snarks turn across yaw359 to0 and survive owner death without world inference",
    "[e27][snark-world][interpolation][lifecycle][no-stock]") {
    f::Owned o;auto second=f::monster(41);second.owner_entity_reference=2;second.origin={0.,100.,16.};
    o.state.packet_entities.push_back(second);o.state.packet_entities[0].angles.y=359;
    o.publish(2,1.1,10);o.state.packet_entities[0].origin={20.,0.,0.};o.state.packet_entities[0].angles.y=0;
    o.state.packet_entities[0].frame=128;o.state.packet_entities[0].controllers[0]=200;o.state.packet_entities[0].blending[1]=100;
    o.state.packet_entities[1].origin.x=40;o.publish(3,1.2,10.1);const auto canonical=o.state;
    o.present(10.15);CHECK(o.instance().transform.origin.x==Approx(10).margin(.002));
    CHECK(o.instance().transform.origin.z==Approx(8).margin(.002));
    CHECK(std::remainder(o.instance().transform.rotation_degrees.z,360.F)==Approx(-.5).margin(.002));
    CHECK(o.instance(41).transform.origin.x==Approx(20).margin(.002));CHECK(o.state==canonical);
    o.state.packet_entities[0].origin={10.,0.,10.};o.state.packet_entities[0].angles.y=30;
    o.publish(4,1.3,10.2);o.present(10.25);CHECK(o.instance().transform.origin.x==Approx(15).margin(.002));
    CHECK(o.instance().transform.origin.z==Approx(5).margin(.002));o.present(40);CHECK(o.instance().transform.origin.x==10);
    auto dead=o.state;dead.receiving_client->health=0;dead.lifecycle.state=c::LocalPlayerLifeState::dead;
    dead.lifecycle.life_epoch=2;o.host.observe(dead,30);o.host.cancel_uncommitted();o.present(40.1);
    CHECK(o.world.entity_frame()->studio_instances().size()==2);CHECK(o.state.packet_entities[0].owner_entity_reference==1);
    CHECK(o.host.drain_audio().count==0);CHECK(o.host.drain_remote_audio().count==0);
    CHECK(o.host.drain_remote_effects(40.1).count==0);CHECK(o.host.drain_temporary_effects(40.1).count==0);
    CHECK(o.host.drain_follow_events().count==0);
}
TEST_CASE("E27 Snark modelzero NODRAW removal same slot reuse and empty world stay canonical",
    "[e27][snark-world][lifecycle][no-stock]") {
    f::Owned o;o.state.packet_entities.push_back(f::monster(41));o.publish(2,1.1,10);
    o.state.packet_entities[0].model_index=0;o.publish(3,1.2,10.1);
    REQUIRE(o.world.entity_frame()->studio_instances().size()==1);CHECK(o.instance(41).entity_number==41);
    o.state.packet_entities[1].effects=128;o.publish(4,1.3,10.2);CHECK(o.world.entity_frame()->studio_instances().empty());
    o.state.packet_entities.clear();o.publish(5,1.4,10.3);CHECK(o.world.entity_frame()->studio_instances().empty());
    auto fresh=f::monster();fresh.origin={300.,20.,8.};o.state.packet_entities={fresh};o.publish(6,1.5,10.4);
    CHECK(o.instance().transform.origin.x==300);o.present(10.45);CHECK(o.instance().transform.origin.x==300);
    CHECK(o.host.drain_temporary_effects(10.45).count==0);CHECK(o.host.drain_remote_effects(10.45).count==0);
    o.state.packet_entities.clear();o.publish(7,1.6,10.5);o.present(100);CHECK(o.world.entity_frame()->studio_instances().empty());
    CHECK(o.host.drain_temporary_effects(100).count==0);
}
TEST_CASE("E27 remote p-squeak follows committed moving crouch death and respawn without fabricated throw",
    "[e27][snark-world][remote-pmodel][no-stock]") {
    f::Owned o;o.state.packet_entities.insert(o.state.packet_entities.begin(),f::player());o.publish(2,2,10);
    const auto child=[&]() {const auto all=o.world.entity_frame()->studio_instances();return std::ranges::find_if(all,
        [](const auto& e){return e.entity_number==2 && e.subinstance_ordinal==1;});};
    auto found=child();REQUIRE(found!=o.world.entity_frame()->studio_instances().end());const auto before=found->transform.origin;
    CHECK(o.assets->summary().model_names.at(71)=="models/p_squeak.mdl");
    auto& p=o.state.packet_entities.front();p.origin.x=8;p.sequence=p.gait_sequence=2;p.use_hull=1;o.publish(3,2.1,10.1);o.present(10.3);
    found=child();REQUIRE(found!=o.world.entity_frame()->studio_instances().end());CHECK(found->transform.origin.x>before.x);
    p.weapon_model_index=0;p.sequence=4;p.gait_sequence=0;o.publish(4,2.2,10.4);o.present(10.6);
    CHECK(child()==o.world.entity_frame()->studio_instances().end());CHECK(o.instance(40).entity_number==40);
    p.weapon_model_index=71;p.sequence=p.gait_sequence=0;p.origin.x=40;o.publish(5,2.3,10.7);o.present(10.9);
    CHECK(child()!=o.world.entity_frame()->studio_instances().end());
    CHECK(o.host.drain_audio().count==0);CHECK(o.host.drain_remote_audio().count==0);
    CHECK(o.host.drain_remote_effects(10.9).count==0);CHECK(o.host.drain_temporary_effects(10.9).count==0);
}
TEST_CASE("E27 living monster and player corpse classification require their independent committed evidence",
    "[e27][snark-world][corpse][no-stock]") {
    f::Owned o;const auto player=f::player();auto snark=f::monster();const std::array entities{player,snark};
    CHECK(o.host.world_entity({snark,nullptr,entities,8}).role==api::WorldEntityPresentationRole::ordinary);
    auto dead=o.state;dead.receiving_client->health=0;dead.lifecycle.state=c::LocalPlayerLifeState::dead;
    dead.lifecycle.life_epoch=2;o.host.observe(dead,.1);
    CHECK(o.host.world_entity({snark,nullptr,entities,8}).role==api::WorldEntityPresentationRole::ordinary);
    // The pre-existing E13 body-queue contract alone identifies a player
    // proxy. A monster filename, owner slot, age or local death does not.
    snark.render_effect=17;snark.render_amount=2;
    const auto proxy=o.host.world_entity({snark,nullptr,entities,8});CHECK(proxy.role==api::WorldEntityPresentationRole::player_proxy);
    CHECK(proxy.model_index==7);CHECK(proxy.source_player==2);snark.render_amount=0;
    CHECK(o.host.world_entity({snark,nullptr,entities,8}).role==api::WorldEntityPresentationRole::unavailable);
    CHECK(o.host.drain_temporary_effects(.1).count==0);CHECK(o.host.drain_remote_audio().count==0);
}
TEST_CASE("E27 committed Snark hunt die deploy blast and bodysplat retain spatial pitch and emitter lifetimes",
    "[e27][snark-world][audio][no-stock]") {
    using namespace std::chrono_literals;
    struct Samples:g::SoundAssets {
        wf::Samples data;g::SoundAssetResult request(std::uint16_t slot) noexcept override{return data.request(slot);}
        std::string_view virtual_name(std::uint16_t slot) const noexcept override {
            constexpr std::array names{"sound/squeek/sqk_hunt1.wav","sound/squeek/sqk_die1.wav",
                "sound/squeek/sqk_deploy1.wav","sound/squeek/sqk_blast1.wav","sound/common/bodysplat.wav"};
            return slot>=51 && slot<=55 ? names[slot-51] : std::string_view{};
        }
    };
    for(const unsigned slot:{51U,52U,53U,54U,55U})for(const float y:{-100.F,100.F}) {
        Samples samples;wf::Sink sink;g::ServerAudio playback(sink);const hlclient::audio::Listener listener{{},{0,-1,0},1,false};
        const unsigned entity=y<0 ? 40U : 41U;const std::array<g::SoundSource,1> alive{{{entity,{0,y,0},7}}};
        playback.present(alive,listener,{},1);auto e=wf::sound(1,entity,slot,y,slot==54 ? 3U : slot==53 ? 1U : 2U);
        e.sound.pitch=static_cast<std::uint8_t>(slot==55 ? 200U : slot==52 ? 160U : slot==54 ? 100U : 105U);
        playback.consume(e,{});playback.consume(e,{});playback.update(&samples,{});REQUIRE(sink.starts.size()==1);
        CHECK(sink.starts[0].origin.y==y);CHECK(sink.starts[0].pitch==Approx(*e.sound.pitch/100.F));CHECK_FALSE(sink.starts[0].local);
        std::array<float,64> pcm{};sink.output.mixer.render(pcm);CHECK(std::ranges::all_of(pcm,[](float v){return std::isfinite(v);}));
        CHECK(pcm[y<0 ? 0 : 1]==0);CHECK(pcm[y<0 ? 1 : 0]>.001F);
        playback.present({},listener,g::SoundTime{}+10ms,1);const std::array<g::SoundSource,1> reuse{{{entity,{0,-y,0},8}}};
        playback.present(reuse,listener,g::SoundTime{}+20ms,1);std::array<float,64> after{};sink.output.mixer.render(after);CHECK(after==pcm);
        e.generation=2;e.record=e.ordinal=2;e.cursor=128;playback.consume(e,g::SoundTime{}+30ms);playback.update(&samples,g::SoundTime{}+30ms);
        CHECK(sink.starts.size()==2);playback.consume(wf::sound(3,entity,slot,-y),g::SoundTime{}+40ms);CHECK(sink.starts.size()==2);
    }
    f::Owned o;std::vector<g::SoundSource> sources;o.assets->sound_sources(o.state,nullptr,sources);
    REQUIRE(sources.size()==1);const auto lifetime=sources[0].lifetime;REQUIRE(lifetime!=0);
    o.state.packet_entities[0].effects=128;o.publish(2,1.1,10);o.assets->sound_sources(o.state,nullptr,sources);
    REQUIRE(sources.size()==1);CHECK(sources[0].lifetime==lifetime); // visibility is not sound ownership
    o.state.packet_entities.clear();o.publish(3,1.2,10.1);o.assets->sound_sources(o.state,nullptr,sources);CHECK(sources.empty());
    o.state.packet_entities={f::monster()};o.publish(4,1.3,10.2);o.assets->sound_sources(o.state,nullptr,sources);
    REQUIRE(sources.size()==1);CHECK(sources[0].lifetime!=lifetime);
}
TEST_CASE("E27 yellow bloodsprite preserves both committed model slots in spray and independent drops without fireball light or sound",
    "[e27][snark-world][bloodsprite][no-stock]") {
    for(const unsigned palette:{195U,247U})for(const unsigned scale:{1U,3U,16U,255U}) {
        f::Owned o;auto e=f::blood(1,palette,scale);o.host.committed_temporary_effect(e);o.host.committed_temporary_effect(e);
        const auto batch=o.host.drain_temporary_effects(0);REQUIRE(batch.count==1);const auto& intent=batch.effects[0];
        const bool large_yellow=palette==195 && scale>=8;
        const auto profile=hlclient::games::halflife::blood_sprite_profile(static_cast<std::uint8_t>(scale));
        REQUIRE(intent.sprite);CHECK(intent.sprite->model_slot==321);
        {
            REQUIRE(intent.sprite_burst);CHECK_FALSE(intent.secondary_sprite);
            CHECK(intent.sprite_burst->sprite.model_slot==322);CHECK(intent.sprite_burst->count==profile.drop_count);
            CHECK(intent.sprite_burst->sprite.scale==profile.drop_scale);
            CHECK(intent.sprite_burst->sprite.animation==api::TemporarySpriteAnimation::fixed);
            CHECK(intent.sprite_burst->sprite.gravity_units==800);
            CHECK(intent.sprite_burst->minimum_lifetime_seconds==.6);
            CHECK(intent.sprite_burst->maximum_lifetime_seconds==.9);
            CHECK(intent.sprite->tint[0]==Approx((palette==195 ? 160.F : 80.F)/255.F));
            CHECK(intent.sprite->tint[1]==Approx((palette==195 ? 130.F : 0.F)/255.F));
            CHECK(intent.sprite->tint[2]==Approx((palette==195 ? 20.F : 0.F)/255.F));
            CHECK(intent.sprite_burst->sprite.tint==intent.sprite->tint);
        }
        CHECK(intent.diagnostic_tag==(palette==247 ? 1U : large_yellow ? 2U : 3U));
        CHECK(intent.sprite->scale==Approx(scale/30.F));
        CHECK(intent.sprite->vertical_speed_units==0);CHECK(intent.sprite->gravity_units==0);
        CHECK(intent.sprite->maximum_lifetime_seconds==.5);
        CHECK(intent.sprite->frames_per_second==20);
        CHECK_FALSE(intent.sprite->fade_with_animation);
        CHECK_FALSE(intent.light);CHECK_FALSE(intent.particles);CHECK_FALSE(intent.sound);
        auto effects_storage=std::make_unique<hlclient::app::TemporaryEffectPresentation>();auto& effects=*effects_storage;effects.reset(1);effects.consume(batch,0);effects.consume(batch,0);
        r::RenderScene scene;effects.present(scene,0);REQUIRE(effects.sprites().size()==1U+profile.drop_count);
        CHECK(scene.world_effect_light_count==0);CHECK(scene.world_particle_count==0);CHECK(scene.world_flash_count==0);
        auto materialized=o.assets->materialize_temporary_sprites(effects.sprites(),0);REQUIRE(materialized);
        REQUIRE(materialized->frame->sprite_instances().size()==1U+profile.drop_count);
        for(const auto& sprite:materialized->frame->sprite_instances()) {
            CHECK(sprite.tint==intent.sprite->tint);CHECK(sprite.blend_mode==hlclient::entity_render::SpriteBlendMode::alpha_blended);
            CHECK(sprite.texture_format_support==hlclient::entity_render::SpriteRenderTextureSupportStatus::supported_alpha_test_masked);
        }
        effects.present(scene,1.6);CHECK(effects.sprites().empty());CHECK_FALSE(o.assets->materialize_temporary_sprites(effects.sprites(),1.6));
        CHECK(o.host.drain_audio().count==0);CHECK(o.host.drain_remote_audio().count==0);CHECK(o.host.drain_temporary_effects(1).count==0);
    }
}
TEST_CASE("E27 bloodsprite bounds missing component capacity replay reset and unknown palette are explicit",
    "[e27][snark-world][bloodsprite][bounds][pop-diagnostics][no-stock]") {
    for(const bool spray:{false,true})for(const bool drop:{false,true}) {
        f::Owned o(237,true,spray,drop);o.host.committed_temporary_effect(f::blood());
        auto b=o.host.drain_temporary_effects(0);REQUIRE(b.count==1);auto effects_storage=std::make_unique<hlclient::app::TemporaryEffectPresentation>();auto& effects=*effects_storage;
        effects.reset(1);effects.consume(b,0);r::RenderScene scene;effects.present(scene,0);
        const auto made=o.assets->materialize_temporary_sprites(effects.sprites(),0);
        const unsigned components=unsigned(spray)+16U*unsigned(drop);
        CHECK(bool(made)==(spray || drop));if(made)CHECK(made->frame->sprite_instances().size()==components);
        CHECK(b.statistics.blood_received==1);CHECK(b.statistics.blood_sprites==1);
        CHECK(effects.statistics(2).accepted==1);REQUIRE(effects.sprites().size()==17);
        CHECK(std::ranges::all_of(effects.sprites(),[](const auto& frame){return frame.diagnostic_tag==2;}));
        const auto diagnostic=o.assets->temporary_sprite_statistics(2);
        CHECK(diagnostic.submitted==components);
        CHECK(diagnostic.absent==17U-components);
        CHECK(diagnostic.unsupported==0);CHECK(diagnostic.invalid==0);CHECK(diagnostic.expired==0);CHECK(diagnostic.frame_rejected==0);
        CHECK(o.assets->temporary_sprite_statistics(0).submitted==0);CHECK(o.assets->temporary_sprite_statistics(0).absent==0);
        CHECK(scene.world_effect_light_count==0);CHECK(scene.world_particle_count==0);CHECK(scene.world_flash_count==0);
    }
    f::Owned o;for(const int invalid:{-1,0,1024,32767}) {auto e=f::blood(static_cast<unsigned>(invalid+100));
        e.record=e.record_ordinal=static_cast<unsigned>(invalid+32770);e.blood_sprite_indices[0]=static_cast<std::int16_t>(invalid);
        o.host.committed_temporary_effect(e);CHECK(o.host.drain_temporary_effects(0).count==0);}
    auto zero=f::blood(100,195,0);o.host.committed_temporary_effect(zero);CHECK(o.host.drain_temporary_effects(0).count==0);
    auto unknown=f::blood(101,42);o.host.committed_temporary_effect(unknown);const auto unsupported=o.host.drain_temporary_effects(0);
    CHECK(unsupported.count==0);CHECK(unsupported.statistics.unsupported_palette==1);
    for(unsigned i=0;i<40;++i)o.host.committed_temporary_effect(f::blood(1000+i));
    const auto batch=o.host.drain_temporary_effects(0);CHECK(batch.count==32);CHECK(batch.statistics.capacity==8);
    auto effects_storage=std::make_unique<hlclient::app::TemporaryEffectPresentation>();auto& effects=*effects_storage;effects.reset(1);effects.consume(batch,0);r::RenderScene scene;effects.present(scene,0);
    CHECK(effects.sprites().size()==160);CHECK(effects.statistics().sprite_evictions==0);
    CHECK(effects.statistics().drop_evictions==384);effects.reset(2);effects.consume(batch,0);
    effects.present(scene,0);CHECK(effects.sprites().empty());o.host.reset({2,2,1});o.host.committed_temporary_effect(f::blood(2000));
    CHECK(o.host.drain_temporary_effects(0).count==0);
}
TEST_CASE("E30 field-driven blood retains anchored spray independent ballistic drops and unrelated smoke",
    "[e27][snark-world][bloodsprite][pop-visibility][no-stock]") {
    for(const unsigned palette:{195U,247U})for(const unsigned scale:{1U,7U,8U,16U}) {
        f::Owned o;auto event=f::blood(1,palette,scale);event.origin.z=0;
        o.host.committed_temporary_effect(event);const auto batch=o.host.drain_temporary_effects(0);REQUIRE(batch.count==1);
        auto effects_storage=std::make_unique<hlclient::app::TemporaryEffectPresentation>();auto& effects=*effects_storage;effects.reset(1);effects.consume(batch,0);
        r::RenderScene scene;effects.present(scene,.2);const auto made=o.assets->materialize_temporary_sprites(effects.sprites(),.2);
        const auto profile=hlclient::games::halflife::blood_sprite_profile(static_cast<std::uint8_t>(scale));
        REQUIRE(made);REQUIRE(made->frame->sprite_instances().size()==1U+profile.drop_count);
        const auto& spray=made->frame->sprite_instances()[0];const auto& mark=made->frame->sprite_instances()[1];
        CHECK(spray.transform.origin.z==0);
        CHECK(spray.opacity==1);CHECK(mark.opacity==1);
        CHECK(spray.selected_frame_index==4);
        {
            for(std::size_t i=1;i<effects.sprites().size();++i) {
                const auto& drop=effects.sprites()[i];const auto& rendered=made->frame->sprite_instances()[i];
                CHECK(rendered.transform.origin.x==Approx(drop.velocity.x*.2F));
                CHECK(rendered.transform.origin.y==Approx(drop.velocity.y*.2F));
                CHECK(rendered.transform.origin.z==Approx(drop.velocity.z*.2F-16.F));
                CHECK(rendered.selected_frame_index==drop.sprite.frame_choice%6);
                CHECK(rendered.transform.uniform_scale==profile.drop_scale);CHECK(rendered.opacity==1);
                CHECK(drop.sprite.maximum_lifetime_seconds>=.6);CHECK(drop.sprite.maximum_lifetime_seconds<=.9);
            }
            CHECK(effects.sprites()[1].velocity.x!=effects.sprites()[2].velocity.x);
        }
        effects.present(scene,.5);const auto peak=o.assets->materialize_temporary_sprites(effects.sprites(),.5);REQUIRE(peak);
        CHECK(peak->frame->sprite_instances().size()==profile.drop_count);
        CHECK(scene.world_effect_light_count==0);CHECK(scene.world_particle_count==0);CHECK(scene.world_flash_count==0);
        CHECK(o.host.drain_audio().count==0);
    }
    f::Owned o;auto smoke=f::blood();smoke.kind=c::TemporaryEffectKind::smoke;
    smoke.sprite_index=321;smoke.scale_tenths=16;smoke.framerate=10;smoke.origin.z=0;
    o.host.committed_temporary_effect(smoke);const auto batch=o.host.drain_temporary_effects(0);REQUIRE(batch.count==1);
    REQUIRE(batch.effects[0].sprite);CHECK(batch.effects[0].sprite->fade_with_animation);
    CHECK(batch.effects[0].sprite->gravity_units==0);
    auto effects_storage=std::make_unique<hlclient::app::TemporaryEffectPresentation>();auto& effects=*effects_storage;effects.reset(1);effects.consume(batch,0);
    r::RenderScene scene;effects.present(scene,.2);const auto made=o.assets->materialize_temporary_sprites(effects.sprites(),.2);REQUIRE(made);
    const auto& sprite=made->frame->sprite_instances()[0];CHECK(sprite.transform.origin.z==Approx(6));
    CHECK(sprite.opacity==Approx(.65F*2.F/3.F));
    auto control_frames=std::vector(effects.sprites().begin(),effects.sprites().end());
    for(const double age:{.13,.21}) {
        control_frames[0].age_seconds=age;
        const auto unchanged=o.assets->materialize_temporary_sprites(control_frames,age);REQUIRE(unchanged);
        CHECK(unchanged->frame->sprite_instances()[0].transform.origin.z==30.F*static_cast<float>(age));
    }
    for(const float invalid:{-1.F,1025.F,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        auto rejected=batch;rejected.effects[0].sprite->gravity_units=invalid;
        auto owner_storage=std::make_unique<hlclient::app::TemporaryEffectPresentation>();auto& owner=*owner_storage;owner.reset(1);owner.consume(rejected,0);owner.present(scene,0);
        CHECK(owner.sprites().empty());CHECK(owner.statistics().invalid==1);
        auto frames=std::vector(effects.sprites().begin(),effects.sprites().end());frames[0].sprite.gravity_units=invalid;
        CHECK_FALSE(o.assets->materialize_temporary_sprites(frames,.2));
    }
    auto frames=std::vector(effects.sprites().begin(),effects.sprites().end());frames[0].sprite.maximum_lifetime_seconds=.1;
    CHECK_FALSE(o.assets->materialize_temporary_sprites(frames,.2));
}
TEST_CASE("E27 pop diagnostics preserve blood provenance through host module pool and materializer",
    "[e27][snark-world][bloodsprite][pop-diagnostics][no-stock]") {
    f::Owned o;o.host.set_movement_audio_time_origin(1000);auto blood=f::blood();blood.received_at_seconds=1000.25;
    o.host.committed_temporary_effect(blood);o.host.committed_temporary_effect(blood);
    const auto delivery=o.host.temporary_delivery_statistics(c::TemporaryEffectKind::blood_sprite);
    CHECK(delivery.received==2);CHECK(delivery.forwarded==2);CHECK(delivery.clock_dropped==0);
    const auto batch=o.host.drain_temporary_effects(.25);REQUIRE(batch.count==1);
    CHECK(batch.statistics.blood_received==2);CHECK(batch.statistics.blood_sprites==1);CHECK(batch.statistics.blood_duplicate==1);
    CHECK(batch.statistics.blood_invalid==0);CHECK(batch.statistics.blood_stale==0);CHECK(batch.statistics.blood_palette==0);
    CHECK(batch.statistics.blood_capacity==0);CHECK(batch.statistics.blood_delivery_stale==0);
    CHECK(batch.effects[0].diagnostic_tag==2);CHECK(batch.effects[0].starts_at_seconds==.25);
    auto effects_storage=std::make_unique<hlclient::app::TemporaryEffectPresentation>();auto& effects=*effects_storage;effects.reset(1);effects.consume(batch,.25);effects.consume(batch,.25);
    r::RenderScene scene;effects.present(scene,.25);REQUIRE(effects.sprites().size()==17);
    CHECK(effects.statistics(2).accepted==1);CHECK(effects.statistics(2).duplicate==1);
    CHECK(effects.statistics(2).stale==0);CHECK(effects.statistics(2).invalid==0);CHECK(effects.statistics(2).sprite_evictions==0);
    for(const auto& frame:effects.sprites()) {CHECK(frame.diagnostic_tag==2);CHECK(frame.identity==batch.effects[0].identity);CHECK(frame.age_seconds==0);}
    const auto made=o.assets->materialize_temporary_sprites(effects.sprites(),.25);REQUIRE(made);
    REQUIRE(made->frame->sprite_instances().size()==17);const auto resource=o.assets->temporary_sprite_statistics(2);
    CHECK(resource.submitted==17);CHECK(resource.absent==0);CHECK(resource.unsupported==0);
    CHECK(resource.invalid==0);CHECK(resource.expired==0);CHECK(resource.frame_rejected==0);
    CHECK(scene.world_effect_light_count==0);CHECK(scene.world_particle_count==0);CHECK(scene.world_flash_count==0);
    CHECK(o.host.drain_audio().count==0);CHECK(o.host.drain_remote_audio().count==0);

    // Even a generic explosion using the same approved sprite is not blood
    // diagnostic evidence. The opaque tag, never the filename, partitions it.
    auto explosion=f::blood(2);explosion.received_at_seconds=1000.25;explosion.kind=c::TemporaryEffectKind::explosion;
    explosion.sprite_index=321;explosion.scale_tenths=16;explosion.framerate=10;explosion.flags=14;
    o.host.committed_temporary_effect(explosion);const auto generic=o.host.drain_temporary_effects(.25);REQUIRE(generic.count==1);
    CHECK(generic.effects[0].diagnostic_tag==0);CHECK(generic.statistics.blood_received==2);CHECK(generic.statistics.blood_sprites==1);
    CHECK(generic.statistics.blood_duplicate==1);CHECK(generic.statistics.blood_delivery_stale==0);
    auto generic_effects_storage=std::make_unique<hlclient::app::TemporaryEffectPresentation>();auto& generic_effects=*generic_effects_storage;generic_effects.reset(1);generic_effects.consume(generic,.25);
    r::RenderScene generic_scene;generic_effects.present(generic_scene,.25);REQUIRE(generic_effects.sprites().size()==1);
    CHECK(generic_effects.statistics(0).accepted==1);CHECK(generic_effects.statistics(2).accepted==0);
    REQUIRE(o.assets->materialize_temporary_sprites(generic_effects.sprites(),.25));
    CHECK(o.assets->temporary_sprite_statistics(0).submitted==1);CHECK(o.assets->temporary_sprite_statistics(2).submitted==17);
    CHECK(o.host.temporary_delivery_statistics(c::TemporaryEffectKind::blood_sprite).received==2);
}
TEST_CASE("E27 pop diagnostics identify rejected blood without synthesizing other effects",
    "[e27][snark-world][bloodsprite][pop-diagnostics][bounds][no-stock]") {
    f::Owned o;api::TemporaryEffectsBatch batch;
    SECTION("wrong generation is module stale rather than a host clock failure") {
        auto e=f::blood();e.generation=2;o.host.committed_temporary_effect(e);batch=o.host.drain_temporary_effects(0);
        CHECK(batch.statistics.blood_received==1);CHECK(batch.statistics.blood_stale==1);CHECK(batch.statistics.blood_delivery_stale==0);
        CHECK(o.host.temporary_delivery_statistics(c::TemporaryEffectKind::blood_sprite).forwarded==1);
    }
    SECTION("invalid signed model tokens scale and origin have the blood invalid reason") {
        for(const int slot:{-1,0,1024,32767}) {auto e=f::blood(static_cast<unsigned>(slot+32770));
            e.blood_sprite_indices[0]=static_cast<std::int16_t>(slot);o.host.committed_temporary_effect(e);}
        auto zero=f::blood(100,195,0);o.host.committed_temporary_effect(zero);
        auto nonfinite=f::blood(101);nonfinite.origin.x=std::numeric_limits<float>::quiet_NaN();o.host.committed_temporary_effect(nonfinite);
        batch=o.host.drain_temporary_effects(0);CHECK(batch.statistics.blood_received==6);CHECK(batch.statistics.blood_invalid==6);
        CHECK(batch.statistics.invalid==6);CHECK(batch.statistics.blood_palette==0);
    }
    SECTION("unknown palette is rejected before a guessed visual even with an invalid model token") {
        auto e=f::blood(1,42);e.blood_sprite_indices[0]=-1;o.host.committed_temporary_effect(e);batch=o.host.drain_temporary_effects(0);
        CHECK(batch.statistics.blood_received==1);CHECK(batch.statistics.blood_palette==1);CHECK(batch.statistics.unsupported_palette==1);
        CHECK(batch.statistics.blood_invalid==0);CHECK(batch.statistics.blood_sprites==0);
    }
    SECTION("late and future delivery differ from generation stale and do not borrow generic explosion counts") {
        o.host.committed_temporary_effect(f::blood());batch=o.host.drain_temporary_effects(.3);CHECK(batch.count==0);
        CHECK(batch.statistics.blood_delivery_stale==1);CHECK(batch.statistics.blood_stale==0);
        auto future=f::blood(2);future.received_at_seconds=1;o.host.committed_temporary_effect(future);
        batch=o.host.drain_temporary_effects(.3);CHECK(batch.count==0);CHECK(batch.statistics.blood_delivery_stale==2);
        auto generic=wf::effect(3,1);o.host.committed_temporary_effect(generic);batch=o.host.drain_temporary_effects(.3);
        CHECK(batch.statistics.stale==3);CHECK(batch.statistics.blood_delivery_stale==2);
        CHECK(batch.statistics.blood_received==2);CHECK(batch.statistics.blood_sprites==2);CHECK(batch.statistics.blood_stale==0);
    }
    CHECK(batch.count==0);auto effects_storage=std::make_unique<hlclient::app::TemporaryEffectPresentation>();auto& effects=*effects_storage;effects.reset(1);effects.consume(batch,.3);
    r::RenderScene scene;effects.present(scene,.3);CHECK(effects.sprites().empty());CHECK(effects.models().empty());
    CHECK(effects.statistics(2).accepted==0);CHECK_FALSE(o.assets->materialize_temporary_sprites(effects.sprites(),.3));
    CHECK(o.assets->temporary_sprite_statistics(2).submitted==0);CHECK(o.assets->temporary_sprite_statistics(2).absent==0);
    CHECK(scene.world_effect_light_count==0);CHECK(scene.world_particle_count==0);CHECK(scene.world_flash_count==0);
    CHECK(o.host.drain_audio().count==0);CHECK(o.host.drain_remote_audio().count==0);
}
TEST_CASE("E27 pop diagnostics retain bounded blood capacity and victim-tag eviction accounting",
    "[e27][snark-world][bloodsprite][pop-diagnostics][bounds][no-stock]") {
    f::Owned o;for(unsigned record=1;record<=40;++record)o.host.committed_temporary_effect(f::blood(record));
    const auto batch=o.host.drain_temporary_effects(0);REQUIRE(batch.count==32);
    CHECK(batch.statistics.blood_received==40);CHECK(batch.statistics.blood_sprites==32);CHECK(batch.statistics.blood_capacity==8);
    CHECK(batch.statistics.capacity==8);CHECK(batch.statistics.blood_duplicate==0);CHECK(batch.statistics.blood_delivery_stale==0);
    auto effects_storage=std::make_unique<hlclient::app::TemporaryEffectPresentation>();auto& effects=*effects_storage;effects.reset(1);effects.consume(batch,0);r::RenderScene scene;effects.present(scene,0);
    REQUIRE(effects.sprites().size()==160);CHECK(effects.statistics(2).accepted==32);CHECK(effects.statistics(2).sprite_evictions==0);
    CHECK(effects.statistics(2).drops_created==512);CHECK(effects.statistics(2).drop_evictions==384);
    CHECK(effects.statistics(0).accepted==0);CHECK(effects.statistics(0).sprite_evictions==0);
    auto generic=wf::effect(41,0);generic.sprite_index=321;generic.flags=14;o.host.committed_temporary_effect(generic);
    effects.consume(o.host.drain_temporary_effects(0),0);scene={};effects.present(scene,0);REQUIRE(effects.sprites().size()==160);
    CHECK(effects.statistics(2).sprite_evictions==1);CHECK(effects.statistics(0).sprite_evictions==0);
    const auto made=o.assets->materialize_temporary_sprites(effects.sprites(),0);REQUIRE(made);
    CHECK(made->frame->sprite_instances().size()==160);CHECK(o.assets->temporary_sprite_statistics(2).submitted==159);
    CHECK(o.assets->temporary_sprite_statistics(0).submitted==1);CHECK(o.assets->temporary_sprite_statistics(2).absent==0);
    CHECK(scene.world_effect_light_count==0);CHECK(scene.world_particle_count==0);CHECK(scene.world_flash_count==0);
}
TEST_CASE("E27 opaque out-of-range diagnostic tags fold to zero without changing sprites or resource reasons",
    "[e27][snark-world][bloodsprite][pop-diagnostics][no-stock]") {
    f::Owned o;o.host.committed_temporary_effect(f::blood());auto batch=o.host.drain_temporary_effects(0);REQUIRE(batch.count==1);
    batch.effects[0].diagnostic_tag=255;auto effects_storage=std::make_unique<hlclient::app::TemporaryEffectPresentation>();auto& effects=*effects_storage;effects.reset(1);effects.consume(batch,0);
    r::RenderScene scene;effects.present(scene,0);REQUIRE(effects.sprites().size()==17);
    CHECK(effects.statistics(0).accepted==1);CHECK(effects.statistics(1).accepted==0);CHECK(effects.statistics(255).accepted==0);
    CHECK(std::ranges::all_of(effects.sprites(),[](const auto& frame){return frame.diagnostic_tag==255;}));
    const auto folded=o.assets->materialize_temporary_sprites(effects.sprites(),0);REQUIRE(folded);CHECK(folded->frame->sprite_instances().size()==17);
    CHECK(o.assets->temporary_sprite_statistics(0).submitted==17);CHECK(o.assets->temporary_sprite_statistics(1).submitted==0);
    CHECK(o.assets->temporary_sprite_statistics(255).submitted==0);
    auto frames=std::vector(effects.sprites().begin(),effects.sprites().end());for(auto& frame:frames)frame.diagnostic_tag=0;
    const auto ordinary=o.assets->materialize_temporary_sprites(frames,0);REQUIRE(ordinary);REQUIRE(ordinary->frame->sprite_instances().size()==17);
    for(unsigned i=0;i<17;++i) {const auto& a=folded->frame->sprite_instances()[i];const auto& b=ordinary->frame->sprite_instances()[i];
        CHECK(a.sprite_asset_index==b.sprite_asset_index);CHECK(a.selected_frame_index==b.selected_frame_index);
        CHECK(a.transform.origin.x==b.transform.origin.x);CHECK(a.transform.origin.y==b.transform.origin.y);
        CHECK(a.transform.origin.z==b.transform.origin.z);CHECK(a.transform.uniform_scale==b.transform.uniform_scale);
        CHECK(a.tint==b.tint);CHECK(a.opacity==b.opacity);CHECK(a.blend_mode==b.blend_mode);}
    frames.resize(2);
    frames[0].diagnostic_tag=1;frames[1].diagnostic_tag=1;frames[0].age_seconds=.6;frames[1].sprite.model_slot=237;
    CHECK_FALSE(o.assets->materialize_temporary_sprites(frames,0));
    CHECK(o.assets->temporary_sprite_statistics(1).expired==1);CHECK(o.assets->temporary_sprite_statistics(1).unsupported==1);
    frames[0].age_seconds=-1;frames[1].sprite.model_slot=999;CHECK_FALSE(o.assets->materialize_temporary_sprites(frames,0));
    const auto rejected=o.assets->temporary_sprite_statistics(1);
    CHECK(rejected.invalid==1);CHECK(rejected.absent==1);CHECK(rejected.submitted==0);CHECK(rejected.frame_rejected==0);
    CHECK(o.assets->temporary_sprite_statistics(0).submitted==34);CHECK(scene.world_effect_light_count==0);CHECK(scene.world_particle_count==0);
}
TEST_CASE("E27 red bite blood cannot stand in for large or small yellow profile delivery evidence",
    "[e27][snark-world][bloodsprite][pop-diagnostics][lifecycle][no-stock]") {
    // These tags describe the committed palette/scale profile, not an inferred
    // monster, death, fuse, resource filename, or audible pop.
    constexpr std::array<std::array<unsigned,2>,3> profiles{{{247,16},{195,16},{195,7}}};
    constexpr std::array<std::uint8_t,3> tags{1,2,3};
    f::Owned o;
    for(unsigned i=0;i<profiles.size();++i) {
        const auto event=f::blood(i+1,profiles[i][0],profiles[i][1]);
        o.host.committed_temporary_effect(event);o.host.committed_temporary_effect(event);
    }
    const auto batch=o.host.drain_temporary_effects(0);REQUIRE(batch.count==3);
    CHECK(batch.statistics.blood_received==6);CHECK(batch.statistics.blood_sprites==3);
    CHECK(batch.statistics.blood_duplicate==3);CHECK(batch.statistics.blood_invalid==0);CHECK(batch.statistics.blood_stale==0);
    auto effects_storage=std::make_unique<hlclient::app::TemporaryEffectPresentation>();auto& effects=*effects_storage;effects.reset(1);effects.consume(batch,0);effects.consume(batch,0);
    r::RenderScene scene;effects.present(scene,0);REQUIRE(effects.sprites().size()==51);
    for(unsigned i=0;i<profiles.size();++i) {
        const auto tag=static_cast<std::uint8_t>(i+1);CHECK(batch.effects[i].diagnostic_tag==tag);
        CHECK(batch.effects[i].identity.record==i+1);
        unsigned components{};
        for(const auto& frame:effects.sprites())if(frame.diagnostic_tag==tag) {
            ++components;CHECK(frame.identity==batch.effects[i].identity);
        }
        CHECK(components==component_count(tag));
        CHECK(effects.statistics(tag).accepted==1);CHECK(effects.statistics(tag).duplicate==1);
        CHECK(effects.statistics(tag).invalid==0);CHECK(effects.statistics(tag).stale==0);
    }
    const auto made=o.assets->materialize_temporary_sprites(effects.sprites(),0);REQUIRE(made);
    REQUIRE(made->frame->sprite_instances().size()==51);
    for(const auto tag:tags) {
        CHECK(o.assets->temporary_sprite_statistics(tag).submitted==component_count(tag));
        CHECK(o.assets->temporary_sprite_statistics(tag).frame_rejected==0);
    }
    CHECK(o.assets->temporary_sprite_statistics(0).submitted==0);

    auto frames=std::vector(effects.sprites().begin(),effects.sprites().end());
    std::array<unsigned,4> tag_seen{};
    for(auto& frame:frames) {
        if(tag_seen[frame.diagnostic_tag]++==0)frame.age_seconds=-1;
        else frame.sprite.model_slot=999;
    }
    CHECK_FALSE(o.assets->materialize_temporary_sprites(frames,0));
    for(const auto tag:tags) {
        CHECK(o.assets->temporary_sprite_statistics(tag).invalid==1);
        CHECK(o.assets->temporary_sprite_statistics(tag).absent==component_count(tag)-1);
        CHECK(o.assets->temporary_sprite_statistics(tag).submitted==component_count(tag));
    }
    frames.assign(effects.sprites().begin(),effects.sprites().end());
    for(auto& frame:frames)frame.age_seconds=2;
    CHECK_FALSE(o.assets->materialize_temporary_sprites(frames,2));
    for(const auto tag:tags)CHECK(o.assets->temporary_sprite_statistics(tag).expired==component_count(tag));
    frames.assign(effects.sprites().begin(),effects.sprites().end());
    tag_seen.fill(0);
    for(auto& frame:frames)frame.sprite.model_slot=tag_seen[frame.diagnostic_tag]++==0 ? 237 : 999;
    CHECK_FALSE(o.assets->materialize_temporary_sprites(frames,0));
    for(const auto tag:tags) {
        CHECK(o.assets->temporary_sprite_statistics(tag).unsupported==1);
        CHECK(o.assets->temporary_sprite_statistics(tag).absent==2U*(component_count(tag)-1));
        CHECK(o.assets->temporary_sprite_statistics(tag).submitted==component_count(tag));
    }

    auto invalid=batch;auto stale=batch;
    for(unsigned i=0;i<profiles.size();++i) {
        invalid.effects[i].identity.record+=100;invalid.effects[i].origin.x=std::numeric_limits<float>::quiet_NaN();
        stale.effects[i].identity.generation=2;
        auto bad=f::blood(i+101,profiles[i][0],profiles[i][1]);bad.origin.x=std::numeric_limits<float>::quiet_NaN();
        o.host.committed_temporary_effect(bad);
        auto old=f::blood(i+201,profiles[i][0],profiles[i][1]);old.generation=2;o.host.committed_temporary_effect(old);
    }
    effects.consume(invalid,0);effects.consume(stale,0);
    for(const auto tag:tags) {
        CHECK(effects.statistics(tag).accepted==1);CHECK(effects.statistics(tag).duplicate==1);
        CHECK(effects.statistics(tag).invalid==1);CHECK(effects.statistics(tag).stale==1);
    }
    const auto rejected=o.host.drain_temporary_effects(0);CHECK(rejected.count==0);
    CHECK(rejected.statistics.blood_received==12);CHECK(rejected.statistics.blood_sprites==3);
    CHECK(rejected.statistics.blood_duplicate==3);CHECK(rejected.statistics.blood_invalid==3);CHECK(rejected.statistics.blood_stale==3);
    for(unsigned i=0;i<profiles.size();++i)o.host.committed_temporary_effect(f::blood(i+301,profiles[i][0],profiles[i][1]));
    const auto late=o.host.drain_temporary_effects(.3);CHECK(late.count==0);
    CHECK(late.statistics.blood_received==15);CHECK(late.statistics.blood_sprites==6);
    CHECK(late.statistics.blood_stale==3);CHECK(late.statistics.blood_delivery_stale==3);
    effects.present(scene,2);CHECK(effects.sprites().empty());
    CHECK_FALSE(o.assets->materialize_temporary_sprites(effects.sprites(),2));
    CHECK(scene.world_effect_light_count==0);CHECK(scene.world_particle_count==0);CHECK(scene.world_flash_count==0);

    o.host.reset({2,2,1});o.bind_effects();effects.reset(2);const auto cleared=o.host.drain_temporary_effects(0);
    CHECK(cleared.count==0);CHECK(cleared.statistics.blood_received==0);CHECK(cleared.statistics.blood_sprites==0);
    CHECK(cleared.statistics.blood_duplicate==0);CHECK(cleared.statistics.blood_invalid==0);CHECK(cleared.statistics.blood_stale==0);
    for(const auto tag:tags) {
        CHECK(effects.statistics(tag).accepted==0);CHECK(effects.statistics(tag).duplicate==0);
        CHECK(effects.statistics(tag).invalid==0);CHECK(effects.statistics(tag).stale==0);
    }
    effects.consume(batch,0);effects.present(scene,0);CHECK(effects.sprites().empty());
    for(unsigned i=0;i<profiles.size();++i) {
        auto fresh=f::blood(i+1,profiles[i][0],profiles[i][1]);fresh.generation=2;o.host.committed_temporary_effect(fresh);
    }
    const auto next=o.host.drain_temporary_effects(0);REQUIRE(next.count==3);CHECK(next.statistics.blood_sprites==3);
    CHECK(next.statistics.blood_duplicate==0);effects.consume(next,0);effects.present(scene,0);REQUIRE(effects.sprites().size()==51);
    f::Owned replacement;CHECK_FALSE(replacement.assets->materialize_temporary_sprites(effects.sprites(),0));
    for(const auto tag:tags) {
        CHECK(effects.statistics(tag).accepted==1);CHECK(effects.statistics(tag).stale==1);
        CHECK(o.assets->temporary_sprite_statistics(tag).submitted==component_count(tag)); // Original resource context is cumulative.
        CHECK(replacement.assets->temporary_sprite_statistics(tag).invalid==component_count(tag));
        CHECK(replacement.assets->temporary_sprite_statistics(tag).submitted==0);
    }
    CHECK(o.host.drain_audio().count==0);CHECK(o.host.drain_remote_audio().count==0);
}
TEST_CASE("E27 pop diagnostic resets discard old host module and pool evidence while resources are context owned",
    "[e27][snark-world][bloodsprite][pop-diagnostics][lifecycle][no-stock]") {
    f::Owned o;o.host.committed_temporary_effect(f::blood());const auto old=o.host.drain_temporary_effects(0);
    auto effects_storage=std::make_unique<hlclient::app::TemporaryEffectPresentation>();auto& effects=*effects_storage;effects.reset(1);effects.consume(old,0);r::RenderScene scene;effects.present(scene,0);
    REQUIRE(o.assets->materialize_temporary_sprites(effects.sprites(),0));CHECK(o.assets->temporary_sprite_statistics(2).submitted==17);
    o.host.reset({2,2,1});o.bind_effects();effects.reset(2);const auto cleared=o.host.drain_temporary_effects(0);
    CHECK(cleared.count==0);CHECK(cleared.statistics.blood_received==0);CHECK(cleared.statistics.blood_sprites==0);
    CHECK(cleared.statistics.blood_duplicate==0);CHECK(cleared.statistics.blood_invalid==0);CHECK(cleared.statistics.blood_stale==0);
    CHECK(cleared.statistics.blood_palette==0);CHECK(cleared.statistics.blood_capacity==0);CHECK(cleared.statistics.blood_delivery_stale==0);
    const auto delivery=o.host.temporary_delivery_statistics(c::TemporaryEffectKind::blood_sprite);
    CHECK(delivery.received==0);CHECK(delivery.forwarded==0);CHECK(delivery.clock_dropped==0);
    scene={};effects.present(scene,0);CHECK(effects.sprites().empty());CHECK(effects.statistics(2).accepted==0);
    effects.consume(old,0);effects.present(scene,0);CHECK(effects.sprites().empty());CHECK(effects.statistics(2).stale==1);
    auto next=f::blood();next.generation=2;o.host.committed_temporary_effect(next);const auto fresh=o.host.drain_temporary_effects(0);REQUIRE(fresh.count==1);
    CHECK(fresh.statistics.blood_received==1);CHECK(fresh.statistics.blood_duplicate==0);CHECK(fresh.statistics.blood_sprites==1);
    CHECK(o.host.temporary_delivery_statistics(c::TemporaryEffectKind::blood_sprite).clock_dropped==0); // Clock survives reset.
    effects.consume(fresh,0);effects.present(scene,0);REQUIRE(effects.sprites().size()==17);CHECK(effects.statistics(2).accepted==1);
    CHECK(o.assets->temporary_sprite_statistics(2).submitted==17); // Existing resource diagnostics are lifetime cumulative.
    f::Owned replacement;CHECK(replacement.assets->temporary_sprite_statistics(2).submitted==0);
    CHECK(replacement.assets->temporary_sprite_statistics(2).absent==0);CHECK(replacement.assets->temporary_sprite_statistics(0).submitted==0);
    CHECK_FALSE(replacement.assets->materialize_temporary_sprites(effects.sprites(),0));
    CHECK(replacement.assets->temporary_sprite_statistics(2).invalid==17);CHECK(replacement.assets->temporary_sprite_statistics(2).submitted==0);
    CHECK(scene.world_effect_light_count==0);CHECK(scene.world_particle_count==0);CHECK(scene.world_flash_count==0);
}
TEST_CASE("E27 bloodsprite exact wire publication is atomic with malformed suffix and preserves unsupported bytes",
    "[e27][snark-world][bloodsprite][runtime-replay][no-stock]") {
    for(const bool selected:{false,true})for(const bool malformed:{false,true}) {
        auto fixture=g::make_runtime_replay_fixture({});REQUIRE(fixture);std::shared_ptr<api::GameClientHost> host;
        if(selected) {host=std::make_shared<api::GameClientHost>(hlclient::games::halflife::make_half_life_client_module());
            fixture.fixture->initialization.game_client=host;}
        c::ClientWorldState world;auto replay=g::RuntimeReplaySession::initialize(fixture.fixture->initialization,world);REQUIRE(replay);
        if(host) {host->set_movement_audio_time_origin(0);host->bind_effect_palette(t::effect_palette_fixture::metadata());}
        auto record=fixture.fixture->records.front();record.received_at=g::SoundTime{};
        const auto wire=f::blood_wire();record.payload.bytes.insert(record.payload.bytes.end(),wire.begin(),wire.end());
        const auto strange=f::blood_wire(-1,1024,42,255);record.payload.bytes.insert(record.payload.bytes.end(),strange.begin(),strange.end());
        if(malformed)record.payload.bytes.push_back(std::byte{254});const auto before=world.runtime_observation();
        const auto applied=replay.session->apply_record(record);
        if(malformed) {CHECK_FALSE(applied);CHECK(world.runtime_observation()==before);if(host)CHECK(host->drain_temporary_effects(0).count==0);continue;}
        REQUIRE(applied);REQUIRE(applied.event->temporary_effects.size()==2);const auto& e=applied.event->temporary_effects[0];
        CHECK(e.kind==c::TemporaryEffectKind::blood_sprite);CHECK((e.blood_sprite_indices==std::array<std::int16_t,2>{321,322}));
        CHECK(e.palette_color==195);CHECK(e.blood_scale==16);CHECK(e.origin.z==16);
        CHECK((applied.event->temporary_effects[1].blood_sprite_indices==std::array<std::int16_t,2>{-1,1024}));
        CHECK(applied.event->temporary_effects[1].palette_color==42);CHECK(applied.event->temporary_effects[1].blood_scale==255);
        if(host) {const auto batch=host->drain_temporary_effects(0);CHECK(batch.count==1);CHECK(batch.statistics.unsupported_palette==1);}
        const auto committed=world.runtime_observation();CHECK_FALSE(replay.session->apply_record(record));CHECK(world.runtime_observation()==committed);
        if(host)CHECK(host->drain_temporary_effects(0).count==0);
    }
}
TEST_CASE("E27 mixed player multiple Snarks Hornet grenade Satchel mine beam and sprite wire remains atomic",
    "[e27][snark-world][packet-entities][transactional][no-stock]") {
    namespace d=hlclient::test::delta_fixture;namespace pf=hlclient::test::player_origin_fixture;
    const d::Field ordinary[]{{"origin[2]",0x80000004U,8,18,32000,4000},{"angles[0]",0x10U,12,16},
        {"modelindex",8,24,10},{"skin",0x80000002U,36,9},{"movetype",8,68,8},{"owner",8,72,5},{"sequence",8,28,8}};
    const d::Field custom[]{{"rendermode",8,32,8},{"origin[0]",0x80000004U,0,17,32000,4000},
        {"origin[1]",0x80000004U,4,17,32000,4000},{"origin[2]",0x80000004U,8,17,32000,4000},
        {"angles[0]",0x80000004U,12,17,32000,4000},{"angles[1]",0x80000004U,16,17,32000,4000},
        {"angles[2]",0x80000004U,20,17,32000,4000},{"sequence",8,28,16},{"skin",8,36,16},
        {"modelindex",8,24,16},{"scale",4,40,8,4000,400},{"body",8,44,8},
        {"rendercolor.r",1,48,8},{"rendercolor.g",1,49,8},{"rendercolor.b",1,50,8},
        {"renderfx",8,52,8},{"renderamt",8,56,8},{"frame",4,60,8},{"animtime",4,64,8,4000,400}};
    g::DeltaSchemaRegistryBuilder registry;
    for(const auto& bytes:std::array{d::schema("entity_state_player_t",ordinary),d::schema("entity_state_t",ordinary),d::schema("custom_entity_state_t",custom)}) {
        const auto parsed=g::DeltaDescriptionParser{}.parse(bytes,0);REQUIRE(parsed);REQUIRE(registry.insert(*parsed.schema));
    }
    const auto schemas=std::make_shared<const g::DeltaSchemaRegistryState>(std::move(registry).publish());const auto baselines=pf::baselines(*schemas);
    for(const unsigned count:{2U,16U})for(const unsigned slot:{74U,237U})for(const bool malformed:{false,true}) {
        d::BitWriter w;w.write(7,8);w.write(std::bit_cast<std::uint32_t>(30.F),32);w.write(40,8);w.write(count+9,16);
        const auto header=[&](bool beam,bool preceding){w.write(1,1);w.write(beam ? 1U : 0U,1);w.write(preceding ? 1U : 0U,1);if(preceding)w.write(1,6);};
        const auto fields=[&](double z,unsigned model,unsigned move,unsigned owner,unsigned sequence,bool inherited=false) {
            w.write(1,3);w.write(inherited ? 126U : 127U,8);if(!inherited)pf::coordinate(w,z,8,18);
            w.write(16384,16);w.write(model,10);w.write(0,9);w.write(move,8);w.write(owner,5);w.write(sequence,8);
        };
        header(false,false);fields(-12,7,3,0,0);header(false,false);fields(-16,8,3,0,0);
        header(true,true);w.write(3,3);w.write(0xf7,8);w.write(0xff,8);w.write(7,8);w.write(16,8);
        for(const double value:{100.,20.,-100.,-200.,-300.})pf::coordinate(w,value,8,17);
        w.write(0,16);w.write(4097,16);w.write(319,16);w.write(40,8);w.write(20,8);
        w.write(50,8);w.write(50,8);w.write(125,8);w.write(0,8);w.write(255,8);w.write(0,8);w.write(50,8);
        for(unsigned i=0;i<count;++i){header(false,true);fields(-16.0-i,slot,10,i%2+1,3,i==0);}
        for(const auto item:std::array<std::array<unsigned,3>,6>{{{82,5,0},{93,5,0},{107,10,0},{108,10,0},{109,5,7},{319,0,0}}}) {
            header(false,true);fields(-120.0-item[0],item[0],item[1],1,item[2]);
        }
        w.write(0,16);w.align_zero();const auto blood=f::blood_wire();w.bytes().insert(w.bytes().end(),blood.begin(),blood.end());
        // Two independently emitted packed svc_sound voices retain pitch and
        // spatial source tokens alongside the monster entity publication.
        for(const unsigned entity:{4U,5U}) {
            d::BitWriter s;s.write(6,8);s.write(11,9);s.write(255,8);s.write(51,8);s.write(2,3);s.write(entity,11);s.write(51,8);
            s.write(0,1);s.write(1,1);s.write(0,1);s.write(1,1);s.write(0,1);s.write(entity==4 ? 1U : 0U,1);s.write(100,12);
            s.write(entity==4 ? 105U : 160U,8);s.align_zero();w.bytes().insert(w.bytes().end(),s.bytes().begin(),s.bytes().end());
        }
        c::ClientWorldState world;g::RuntimeReplayInitialization init;init.generation=1;init.max_clients=2;init.schemas=schemas;init.baselines=baselines;
        init.game_client=std::make_shared<api::GameClientHost>(hlclient::games::halflife::make_half_life_client_module());
        const auto host=init.game_client;const auto sounds=std::make_shared<g::CommittedSoundQueue>();init.sound_events=sounds;
        const auto replay=g::RuntimeReplaySession::initialize(std::move(init),world);REQUIRE(replay);host->set_movement_audio_time_origin(0);
        host->bind_effect_palette(t::effect_palette_fixture::metadata());
        g::CommittedSound next;while(sounds->pop(next)) {}const auto before=world.runtime_observation();g::RuntimeReplayRecord record;
        record.generation=1;record.record_identity=record.record_ordinal=1;record.payload=d::owning_payload(w.bytes());record.payload.source_sequence=20;
        record.received_at=g::SoundTime{};if(malformed)record.payload.bytes.push_back(std::byte{254});
        record.initial_cursor=*g::StockRuntimeSourceCursor::create(0,0,record.payload.bytes.size());const auto applied=replay.session->apply_record(record);
        if(malformed) {CHECK_FALSE(applied);CHECK(world.runtime_observation()==before);CHECK_FALSE(sounds->pop(next));CHECK(host->drain_temporary_effects(0).count==0);continue;}
        REQUIRE(applied);const auto current=world.runtime_observation();REQUIRE(current->packet_entities.size()==count+9);
        CHECK(current->packet_entities[0].player_movement_schema);CHECK(current->packet_entities[1].player_movement_schema);
        REQUIRE(current->packet_entities[2].custom_beam);CHECK_FALSE(current->packet_entities[2].ordinary_visual_schema);
        for(unsigned i=0;i<count;++i) {const auto& snark=current->packet_entities[i+3];CHECK(snark.model_index==slot);CHECK(snark.sequence==3);
            CHECK(snark.ordinary_visual_schema);CHECK_FALSE(snark.custom_beam);CHECK_FALSE(snark.player_movement_schema);
            CHECK(snark.brush_move_type==10);CHECK(snark.owner_entity_reference==i%2+1);CHECK(snark.origin.z==-16.0-i);}
        for(unsigned i=0;i<6;++i)CHECK((current->packet_entities[count+3+i].model_index==std::array<unsigned,6>{82,93,107,108,109,319}[i]));
        REQUIRE(sounds->pop(next));CHECK(next.sound.entity_reference==4);CHECK(next.sound.pitch==105);CHECK(next.sound.origin[1]==-100);
        REQUIRE(sounds->pop(next));CHECK(next.sound.entity_reference==5);CHECK(next.sound.pitch==160);CHECK(next.sound.origin[1]==100);CHECK_FALSE(sounds->pop(next));
        const auto batch=host->drain_temporary_effects(0);REQUIRE(batch.count==1);CHECK_FALSE(batch.effects[0].sound);CHECK_FALSE(batch.effects[0].light);
        CHECK_FALSE(replay.session->apply_record(record));CHECK(world.runtime_observation()==current);CHECK_FALSE(sounds->pop(next));
    }
}
TEST_CASE("E27 owned living Studio and yellow server blood use normal OpenGL and clean transient state",
    "[e27][snark-world][bloodsprite][opengl][no-stock]") {
    auto gl=t::entity_opengl_fixture::try_context();if(!gl)SKIP("OpenGL 3.3 Core unavailable");
    REQUIRE(t::entity_opengl_fixture::capable_context());gl->initialize_renderer();f::Owned o;
    const auto draw=[&](r::RenderScene scene) {scene.clear_color={0,0,0,1};scene.camera={{0,0,40},{0,0,16},{0,1,0},1.047F,.1F,128};
        gl->renderer().render(scene,{96,96});CHECK(glGetError()==GL_NO_ERROR);return t::entity_opengl_fixture::framebuffer();};
    const auto baseline=draw({});r::RenderScene scene;scene.dynamic_entities=r::RenderDynamicEntities{o.world.entity_scene(),o.world.entity_frame(),{}};
    CHECK(draw(scene)!=baseline);const auto uploads=gl->renderer().entity_statistics().studio_asset_upload_count;
    o.state.packet_entities[0].angles.y=90;o.publish(2,1.1,10);scene.dynamic_entities=r::RenderDynamicEntities{o.world.entity_scene(),o.world.entity_frame(),{}};
    draw(scene);CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==uploads);
    o.state.packet_entities.clear();o.publish(3,1.2,10.1);scene={};CHECK(draw(scene)==baseline);
    o.host.committed_temporary_effect(f::blood());const auto batch=o.host.drain_temporary_effects(0);REQUIRE(batch.count==1);
    auto effects_storage=std::make_unique<hlclient::app::TemporaryEffectPresentation>();auto& effects=*effects_storage;effects.reset(1);effects.consume(batch,0);effects.present(scene,0);
    scene.transient_sprite_entities=o.assets->materialize_temporary_sprites(effects.sprites(),0);REQUIRE(scene.transient_sprite_entities);
    const auto blood=draw(scene);CHECK(blood!=baseline);std::size_t yellow{};
    for(std::size_t i=0;i<blood.size();i+=4)if(std::to_integer<unsigned>(blood[i])>40 && std::to_integer<unsigned>(blood[i+1])>30 &&
        std::to_integer<unsigned>(blood[i+2])<30)++yellow;
    CHECK(yellow>4);CHECK(scene.world_effect_light_count==0);CHECK(scene.world_particle_count==0);CHECK(scene.world_flash_count==0);
    auto control_frames=std::vector(effects.sprites().begin(),effects.sprites().end());
    for(auto& frame:control_frames)frame.sprite.tint={1,1,1};
    auto control_scene=scene;control_scene.transient_sprite_entities=o.assets->materialize_temporary_sprites(control_frames,0);
    REQUIRE(control_scene.transient_sprite_entities);const auto gray=draw(control_scene);std::size_t gray_inside{},plain_yellow{},coverage_changes{},palette_pixels{};
    for(std::size_t i=0;i<gray.size();i+=4) {
        if(std::to_integer<unsigned>(gray[i])>20 && gray[i]==gray[i+1] && gray[i+1]==gray[i+2])++gray_inside;
        if(std::to_integer<unsigned>(gray[i])>20 && std::to_integer<unsigned>(gray[i+1])>20 && std::to_integer<unsigned>(gray[i+2])<5)++plain_yellow;
        if((gray[i]!=std::byte{0} || gray[i+1]!=std::byte{0} || gray[i+2]!=std::byte{0})!=
            (blood[i]!=std::byte{0} || blood[i+1]!=std::byte{0} || blood[i+2]!=std::byte{0}))++coverage_changes;
        if(gray[i]==std::byte{255} && gray[i+1]==std::byte{255} && gray[i+2]==std::byte{255}) {
            ++palette_pixels;
            CHECK(std::to_integer<unsigned>(blood[i])==160);CHECK(std::to_integer<unsigned>(blood[i+1])==130);
            CHECK(std::to_integer<unsigned>(blood[i+2])==20);
        }
    }
    CHECK(gray_inside>4);CHECK(palette_pixels>4);CHECK(plain_yellow==0);CHECK(coverage_changes==0);CHECK(gray!=blood);
    for(auto& frame:control_frames)frame.sprite.tint={1,.05F,.05F};
    control_scene.transient_sprite_entities=o.assets->materialize_temporary_sprites(control_frames,0);REQUIRE(control_scene.transient_sprite_entities);
    const auto red=draw(control_scene);std::size_t red_only{};
    for(std::size_t i=0;i<red.size();i+=4)if(std::to_integer<unsigned>(red[i])>20 && std::to_integer<unsigned>(red[i+1])<20 &&
        std::to_integer<unsigned>(red[i+2])<20)++red_only;
    CHECK(red_only>4);CHECK(red!=blood);CHECK(draw(scene)==blood); // tint is per draw, never sticky
    const auto sprite_uploads=gl->renderer().entity_statistics().sprite_asset_upload_count;CHECK(draw(scene)==blood);
    CHECK(gl->renderer().entity_statistics().sprite_asset_upload_count==sprite_uploads);
    scene={};effects.present(scene,1.6);scene.transient_sprite_entities=o.assets->materialize_temporary_sprites(effects.sprites(),1.6);
    CHECK_FALSE(scene.transient_sprite_entities);CHECK(draw(scene)==baseline);CHECK_FALSE(scene.first_person_flash);
    GLboolean depth{};glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);CHECK(depth==GL_TRUE);CHECK(glIsEnabled(GL_BLEND)==GL_FALSE);
    gl->release_renderer();
}
