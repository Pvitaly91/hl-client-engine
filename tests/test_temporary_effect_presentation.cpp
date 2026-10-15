#include <hlclient/app/temporary_effect_presentation.hpp>
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/games/halflife/temporary_effects.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/goldsrc/sound_assets.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "local_resource_readiness_test_fixture.hpp"
#include "hand_grenade_world_test_fixture.hpp"
#include <limits>
#include <memory>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <thread>
#include <vector>

namespace {
namespace a=hlclient::game_api;namespace c=hlclient::client;namespace app=hlclient::app;
c::CommittedTemporaryEffect event(std::uint64_t record=1,c::TemporaryEffectKind kind=c::TemporaryEffectKind::explosion) {
    c::CommittedTemporaryEffect e;e.generation=1;e.record=record;e.record_ordinal=static_cast<std::size_t>(record);
    e.message_bit_offset=24;e.received_at_seconds=10;e.kind=kind;
    e.origin={24,12,8};e.sprite_index=19;e.scale_tenths=20;e.framerate=15;return e;
}
void setup(a::GameClientHost& host) {host.reset({1,1,1});host.set_movement_audio_time_origin(10);}
struct ExplosionSamples final : hlclient::goldsrc::SoundAssets {
    std::shared_ptr<const hlclient::assets::AudioAsset> pcm;
    hlclient::goldsrc::SoundAssetStatus status{hlclient::goldsrc::SoundAssetStatus::ready};
    std::vector<a::LocalSoundReference> requested;
    ExplosionSamples() {
        auto sample=std::make_shared<hlclient::assets::AudioAsset>();
        sample->sample_rate=48000;sample->channel_count=1;
        sample->interleaved_samples.assign(48000,.125F);pcm=std::move(sample);
    }
    hlclient::goldsrc::SoundAssetResult request(std::uint16_t) noexcept override {return {status,pcm};}
    std::string_view virtual_name(std::uint16_t index) const noexcept override {
        return index==7 ? "sound/weapons/debris1.wav" : std::string_view{};
    }
    hlclient::goldsrc::SoundAssetResult request_local(const a::LocalSoundReference& reference) noexcept override {
        requested.push_back(reference);return {status,pcm};
    }
};
struct ExplosionSink final : hlclient::audio::Output {
    hlclient::audio::OfflineOutput output;
    std::vector<hlclient::audio::Command> starts;
    bool submit(const hlclient::audio::Command& command) noexcept override {
        if(command.operation==hlclient::audio::Operation::start) starts.push_back(command);
        return output.submit(command);
    }
    void set_listener(hlclient::audio::Listener listener) noexcept override {output.set_listener(listener);}
};
double explosion_energy(hlclient::audio::OfflineOutput& output) {
    std::array<float,512> pcm{};output.mixer.render(pcm);double energy{};
    for(const auto sample:pcm) {CHECK(std::isfinite(sample));energy+=std::abs(sample);}
    return energy;
}
a::TemporaryEffectsBatch sprite_drop_request(std::uint64_t record=1) {
    a::TemporaryEffectsBatch batch;batch.count=1;
    auto& effect=batch.effects[0];effect.identity={1,record,32,0};effect.origin={24,12,8};
    effect.starts_at_seconds=0;effect.diagnostic_tag=2;
    a::TemporarySpriteBurstIntent burst;
    burst.sprite.model_slot=19;burst.sprite.scale=.5F;
    burst.sprite.blend=a::TemporarySpriteBlend::alpha_blended;
    burst.sprite.animation=a::TemporarySpriteAnimation::fixed;
    burst.sprite.gravity_units=300;burst.sprite.opacity=.8F;
    burst.count=8;burst.minimum_velocity={-64,-32,48};burst.maximum_velocity={64,32,96};
    burst.minimum_lifetime_seconds=.75;burst.maximum_lifetime_seconds=1.25;burst.seed=0x51a7U;
    effect.sprite_burst=burst;
    return batch;
}
void same_drop_frames(std::span<const app::TemporarySpriteFrame> first,
    std::span<const app::TemporarySpriteFrame> second) {
    REQUIRE(first.size()==second.size());
    for(std::size_t i=0;i<first.size();++i) {
        CHECK(first[i].identity==second[i].identity);
        CHECK(first[i].origin.x==second[i].origin.x);CHECK(first[i].origin.y==second[i].origin.y);
        CHECK(first[i].origin.z==second[i].origin.z);
        CHECK(first[i].velocity.x==second[i].velocity.x);CHECK(first[i].velocity.y==second[i].velocity.y);
        CHECK(first[i].velocity.z==second[i].velocity.z);
        CHECK(first[i].sprite.frame_choice==second[i].sprite.frame_choice);
        CHECK(first[i].sprite.maximum_lifetime_seconds==second[i].sprite.maximum_lifetime_seconds);
        CHECK(first[i].age_seconds==second[i].age_seconds);
    }
}
}
TEST_CASE("Generic sprite drops have independent deterministic seeded state and presentation-clock phases",
    "[temporary-effects][sprite-burst][no-stock][clock]") {
    const auto batch=std::make_unique<a::TemporaryEffectsBatch>(sprite_drop_request());
    auto owner=std::make_unique<app::TemporaryEffectPresentation>();owner->reset(1);owner->consume(*batch,0);
    owner->consume(*batch,0);
    hlclient::renderer::RenderScene scene;owner->present(scene,.375);
    REQUIRE(owner->sprites().size()==8);const auto reference=std::vector(owner->sprites().begin(),owner->sprites().end());
    for(const auto& drop:reference) {
        CHECK(drop.velocity.x>=-64);CHECK(drop.velocity.x<=64);
        CHECK(drop.velocity.y>=-32);CHECK(drop.velocity.y<=32);
        CHECK(drop.velocity.z>=48);CHECK(drop.velocity.z<=96);
        CHECK(drop.sprite.maximum_lifetime_seconds>=.75);CHECK(drop.sprite.maximum_lifetime_seconds<=1.25);
        CHECK(drop.sprite.animation==a::TemporarySpriteAnimation::fixed);
        CHECK(drop.origin.z==8);CHECK(drop.age_seconds==.375);
    }
    CHECK(reference[0].sprite.frame_choice!=reference[1].sprite.frame_choice);
    CHECK(reference[0].velocity.x!=reference[1].velocity.x);
    scene={};owner->present(scene,.375);
    CHECK(owner->statistics().duplicate==1);CHECK(owner->statistics().drops_created==8);
    same_drop_frames(reference,owner->sprites());
    for(const unsigned fps:{30U,60U,144U,240U}) {
        auto sliced=std::make_unique<app::TemporaryEffectPresentation>();sliced->reset(1);sliced->consume(*batch,0);
        for(unsigned tick=0;static_cast<double>(tick)/fps<.375;++tick) {
            scene={};sliced->present(scene,static_cast<double>(tick)/fps);
        }
        scene={};sliced->present(scene,.375);same_drop_frames(reference,sliced->sprites());
        CHECK(sliced->statistics().drops_created==8);
    }
    scene={};owner->present(scene,1.25);CHECK(owner->sprites().empty());
    scene={};owner->present(scene,2);CHECK(owner->sprites().empty());
}
TEST_CASE("Generic sprite drop pool is separate bounded and cleared by generation reset",
    "[temporary-effects][sprite-burst][bounds][no-stock]") {
    auto batch=std::make_unique<a::TemporaryEffectsBatch>(sprite_drop_request());batch->count=32;
    for(std::size_t i=0;i<batch->count;++i) {
        batch->effects[i]=batch->effects[0];batch->effects[i].identity.record=i+1;
        auto sprite=batch->effects[i].sprite_burst->sprite;
        sprite.animation=a::TemporarySpriteAnimation::elapsed;sprite.frames_per_second=10;
        batch->effects[i].sprite=sprite;
    }
    auto owner=std::make_unique<app::TemporaryEffectPresentation>();owner->reset(1);owner->consume(*batch,0);
    hlclient::renderer::RenderScene scene;owner->present(scene,.1);
    CHECK(owner->sprites().size()==160);CHECK(owner->statistics().drops_created==256);
    CHECK(owner->statistics().drop_evictions==128);CHECK(owner->statistics().sprite_evictions==0);
    owner->consume(*batch,.1);scene={};owner->present(scene,.1);
    CHECK(owner->sprites().size()==160);CHECK(owner->statistics().duplicate==32);
    CHECK(owner->statistics().drops_created==256);
    scene={};owner->present(scene,1.25);CHECK(owner->sprites().size()==32);
    owner->reset(2);scene={};owner->present(scene,1.25);CHECK(owner->sprites().empty());
    owner->consume(*batch,0);scene={};owner->present(scene,0);CHECK(owner->sprites().empty());
    CHECK(owner->statistics().stale==32);CHECK(owner->statistics().drops_created==0);
}
TEST_CASE("Generic sprite burst rejects invalid bounds while fixed-frame FPS zero remains valid",
    "[temporary-effects][sprite-burst][bounds][no-stock]") {
    for(unsigned invalid=0;invalid<10;++invalid) {
        auto batch=std::make_unique<a::TemporaryEffectsBatch>(sprite_drop_request());
        auto& burst=*batch->effects[0].sprite_burst;
        switch(invalid) {
        case 0:burst.count=0;break;
        case 1:burst.count=17;break;
        case 2:burst.minimum_velocity.x=std::numeric_limits<float>::quiet_NaN();break;
        case 3:burst.maximum_velocity.z=513;break;
        case 4:burst.minimum_velocity.y=33;break;
        case 5:burst.minimum_lifetime_seconds=0;break;
        case 6:burst.maximum_lifetime_seconds=.5;break;
        case 7:burst.maximum_lifetime_seconds=4.001;break;
        case 8:burst.sprite.animation=static_cast<a::TemporarySpriteAnimation>(2);break;
        case 9:burst.sprite.animation=a::TemporarySpriteAnimation::elapsed;break;
        }
        auto owner=std::make_unique<app::TemporaryEffectPresentation>();owner->reset(1);owner->consume(*batch,0);
        hlclient::renderer::RenderScene scene;owner->present(scene,0);
        CHECK(owner->sprites().empty());CHECK(owner->statistics().invalid==1);
        CHECK(owner->statistics().drops_created==0);
    }
    const auto batch=std::make_unique<a::TemporaryEffectsBatch>(sprite_drop_request());
    auto owner=std::make_unique<app::TemporaryEffectPresentation>();owner->reset(1);owner->consume(*batch,0);
    hlclient::renderer::RenderScene scene;owner->present(scene,0);CHECK(owner->sprites().size()==8);
    CHECK(owner->statistics().invalid==0);
}
TEST_CASE("Generic fixed sprite drops use approved imported frames ballistic positions and stable opacity",
    "[temporary-effects][sprite-burst][sprite-materialization][no-stock]") {
    namespace t=hlclient::tests;namespace wf=t::hand_grenade_world_fixture;
    t::ScopedLocalResourceTestRoot root;(void)wf::owned(root);
    auto bytes=t::sprite_fixture::literal_header_and_palette(3,3,2,0,4,4);
    for(unsigned index=0;index<3;++index)
        t::sprite_fixture::append_single_entry(bytes,{-2,2,4,4,std::vector<std::byte>(16,std::byte{1})});
    root.write("valve","sprites/fixture_drops.spr",bytes);
    const auto resources=t::readiness_fixture::parse_resource_list({
        {2,"maps/test.bsp",9,0,0},{2,"sprites/fixture_drops.spr",19,0,0}});
    auto made=app::RuntimeReplayLocalAssets::create(resources,t::readiness_fixture::parse_server_info("maps/test.bsp"),
        root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
    REQUIRE(made.projection);auto state=wf::state();state.packet_entities.clear();wf::stamp(state,1,1);
    c::ClientWorldState world;REQUIRE(made.projection->reset_generation(state,world));
    auto batch=std::make_unique<a::TemporaryEffectsBatch>(sprite_drop_request());
    auto owner=std::make_unique<app::TemporaryEffectPresentation>();owner->reset(1);owner->consume(*batch,0);
    hlclient::renderer::RenderScene scene;owner->present(scene,.5);
    REQUIRE(owner->sprites().size()==8);
    const auto first=made.projection->materialize_temporary_sprites(owner->sprites(),.5);REQUIRE(first);
    REQUIRE(first->frame->sprite_instances().size()==8);
    for(std::size_t i=0;i<owner->sprites().size();++i) {
        const auto& drop=owner->sprites()[i];const auto& rendered=first->frame->sprite_instances()[i];
        CHECK(rendered.selected_frame_index==drop.sprite.frame_choice%3);
        CHECK(rendered.opacity==Catch::Approx(.8F));
        CHECK(rendered.transform.origin.x==Catch::Approx(24+drop.velocity.x*.5F));
        CHECK(rendered.transform.origin.y==Catch::Approx(12+drop.velocity.y*.5F));
        CHECK(rendered.transform.origin.z==Catch::Approx(8+drop.velocity.z*.5F-37.5F));
        CHECK(rendered.transform.uniform_scale==.5F);
    }
    scene={};owner->present(scene,.7);
    const auto next=made.projection->materialize_temporary_sprites(owner->sprites(),.7);REQUIRE(next);
    REQUIRE(next->frame->sprite_instances().size()==8);
    for(std::size_t i=0;i<next->frame->sprite_instances().size();++i) {
        CHECK(next->frame->sprite_instances()[i].selected_frame_index==first->frame->sprite_instances()[i].selected_frame_index);
        CHECK(next->frame->sprite_instances()[i].opacity==first->frame->sprite_instances()[i].opacity);
    }
    scene={};owner->present(scene,1.25);CHECK(owner->sprites().empty());
    CHECK_FALSE(made.projection->materialize_temporary_sprites(owner->sprites(),1.25));
    auto frames=std::vector(first->frame->sprite_instances().size(),app::TemporarySpriteFrame{});
    // Invalid supplied ballistic velocity cannot materialize a forged frame.
    frames[0].identity.generation=1;frames[0].sprite=batch->effects[0].sprite_burst->sprite;
    frames[0].velocity.x=std::numeric_limits<float>::infinity();
    CHECK_FALSE(made.projection->materialize_temporary_sprites(std::span{frames}.first(1),0));
}
TEST_CASE("E16 committed explosion uses production game host and bounded neutral owner", "[e16][temporary-effects][game-api]") {
    a::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};setup(host);
    auto e=event();host.committed_temporary_effect(e);
    const auto batch=host.drain_temporary_effects(0);REQUIRE(batch.count==1);
    const auto& intent=batch.effects[0];CHECK(intent.starts_at_seconds==0);
    CHECK(intent.record_ordinal==e.record_ordinal);
    REQUIRE(intent.sprite);CHECK(intent.sprite->model_slot==19);CHECK(intent.sprite->scale==2);
    CHECK(intent.sprite->frames_per_second==15);CHECK(intent.sprite->blend==a::TemporarySpriteBlend::additive);
    CHECK(intent.sprite->vertical_speed_units==8);REQUIRE(intent.light);REQUIRE(intent.particles);
    CHECK(intent.particles->radial_distribution);CHECK(intent.particles->flicker);
    REQUIRE(intent.sound);
    CHECK(intent.sound->reference.source==a::SoundReferenceSource::pinned_halflife_client_sound_profile);
    CHECK((intent.sound->reference.name()=="weapons/explode3.wav" ||
        intent.sound->reference.name()=="weapons/explode4.wav" ||
        intent.sound->reference.name()=="weapons/explode5.wav"));
    CHECK(intent.sound->volume==1);CHECK(intent.sound->pitch==1);
    CHECK(intent.sound->attenuation==Catch::Approx(.8));
    CHECK(batch.statistics.main_audio_evidence_pending==0);
    CHECK(batch.statistics.main_audio_requested==1);
    CHECK(host.drain_audio().count==0);CHECK(host.drain_remote_audio().count==0);
    CHECK(host.drain_temporary_effects(0).count==0);
    app::TemporaryEffectPresentation owner;owner.reset(1);owner.consume(batch,0);
    hlclient::renderer::RenderScene scene;owner.present(scene,0);
    REQUIRE(owner.sprites().size()==1);CHECK(scene.world_effect_light_count==1);CHECK(scene.world_particle_count==16);
    const auto first=scene.world_particles;scene={};owner.present(scene,0);
    CHECK(scene.world_particles==first);CHECK(scene.world_particle_count==16);
    scene={};owner.present(scene,.2);CHECK(owner.sprites()[0].age_seconds==Catch::Approx(.2));
    CHECK(scene.world_effect_lights[0].intensity<intent.light->intensity);
    CHECK(scene.world_particle_count==16);
    scene={};owner.present(scene,.7);CHECK(scene.world_effect_light_count==0);CHECK(scene.world_particle_count==0);
    scene={};owner.present(scene,4);CHECK(owner.sprites().empty());
}
TEST_CASE("E16 every verified explosion flag controls its component independently", "[e16][temporary-effects]") {
    a::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};setup(host);
    for(std::uint8_t flags=0;flags<16;++flags) {
        auto e=event(flags+1);e.flags=flags;host.committed_temporary_effect(e);
        auto b=host.drain_temporary_effects(0);REQUIRE(b.count==1);const auto& p=b.effects[0];
        REQUIRE(p.sprite);CHECK(p.sprite->blend==((flags&1) ? a::TemporarySpriteBlend::opaque : a::TemporarySpriteBlend::additive));
        CHECK(p.light.has_value()==!(flags&2));CHECK(p.particles.has_value()==!(flags&8));
        CHECK(p.sound.has_value()==!(flags&4));
    }
    const auto statistics=host.drain_temporary_effects(0).statistics;
    CHECK(statistics.main_audio_evidence_pending==0);
    CHECK(statistics.main_audio_requested==8);CHECK(statistics.main_audio_suppressed==8);
    auto unknown=event(100);unknown.flags=16;host.committed_temporary_effect(unknown);
    auto b=host.drain_temporary_effects(0);CHECK(b.count==0);CHECK(b.statistics.unsupported_flags==1);
}
TEST_CASE("E16 only server smoke starts smoke and same coordinates are independent identities", "[e16][temporary-effects]") {
    a::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};setup(host);
    app::TemporaryEffectPresentation owner;owner.reset(1);
    host.committed_temporary_effect(event());auto b=host.drain_temporary_effects(0);owner.consume(b,0);
    hlclient::renderer::RenderScene scene;owner.present(scene,.3);REQUIRE(owner.sprites().size()==1);
    CHECK(owner.sprites()[0].sprite.blend==a::TemporarySpriteBlend::additive);
    auto smoke=event(2,c::TemporaryEffectKind::smoke);smoke.received_at_seconds=10.3;smoke.sprite_index=33;
    host.committed_temporary_effect(smoke);b=host.drain_temporary_effects(.3);REQUIRE(b.count==1);
    CHECK_FALSE(b.effects[0].light);CHECK_FALSE(b.effects[0].particles);
    CHECK_FALSE(b.effects[0].sound);
    CHECK(b.effects[0].sprite->model_slot==33);CHECK(b.effects[0].sprite->vertical_speed_units==30);
    CHECK(b.effects[0].sprite->blend==a::TemporarySpriteBlend::alpha_blended);
    owner.consume(b,.3);scene={};owner.present(scene,.3);CHECK(owner.sprites().size()==2);
    host.committed_temporary_effect(smoke);CHECK(host.drain_temporary_effects(.3).count==0);
    owner.consume(b,.3);CHECK(owner.statistics().duplicate==1);
    smoke.record=3;host.committed_temporary_effect(smoke);b=host.drain_temporary_effects(.3);CHECK(b.count==1);
    owner.consume(b,.3);scene={};owner.present(scene,.3);CHECK(owner.sprites().size()==3);
}
TEST_CASE("E16 temporary effects are map session owned not selected weapon or player life owned", "[e16][temporary-effects][life]") {
    a::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};setup(host);
    host.committed_temporary_effect(event());app::TemporaryEffectPresentation owner;owner.reset(1);
    owner.consume(host.drain_temporary_effects(0),0);
    c::RuntimeClientObservationState dead;dead.generation=1;dead.lifecycle.state=c::LocalPlayerLifeState::dead;
    dead.lifecycle.life_epoch=2;host.observe(dead,.1);host.bind_model({});host.cancel_uncommitted();
    hlclient::renderer::RenderScene scene;owner.present(scene,.1);CHECK(owner.sprites().size()==1);
    CHECK(scene.world_effect_light_count==1);CHECK(scene.world_particle_count==16);
    auto other=event(2);other.received_at_seconds=10.1;host.committed_temporary_effect(other);
    CHECK(host.drain_temporary_effects(.1).count==1);
    owner.reset(2);scene={};owner.present(scene,.1);CHECK(owner.sprites().empty());CHECK(scene.world_particle_count==0);
    host.reset({2,2,1});host.committed_temporary_effect(other);CHECK(host.drain_temporary_effects(.1).count==0);
    host.teardown();other.generation=2;host.committed_temporary_effect(other);CHECK(host.drain_temporary_effects(.1).count==0);
}
TEST_CASE("E16 resource absence does not block bounded light particles or other gameplay", "[e16][temporary-effects]") {
    a::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};setup(host);
    auto e=event();e.sprite_index=0;host.committed_temporary_effect(e);auto b=host.drain_temporary_effects(0);
    REQUIRE(b.count==1);CHECK_FALSE(b.effects[0].sprite);REQUIRE(b.effects[0].light);REQUIRE(b.effects[0].particles);
    REQUIRE(b.effects[0].sound);CHECK(b.statistics.main_audio_requested==1);
    app::TemporaryEffectPresentation owner;owner.reset(1);owner.consume(b,0);hlclient::renderer::RenderScene scene;
    scene.world_effect_lights[scene.world_effect_light_count++]={{0,0,0},8,1,{1,1,1}};
    owner.present(scene,0);CHECK(scene.world_effect_light_count==2);CHECK(scene.world_particle_count==16);
    CHECK(scene.world_effect_lights[0].radius_units==8);
}
TEST_CASE("E16 capacity flood evicts presentation components never network state", "[e16][temporary-effects][bounds]") {
    a::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};setup(host);
    for(unsigned i=1;i<=40;++i)host.committed_temporary_effect(event(i));
    auto b=host.drain_temporary_effects(0);CHECK(b.count==32);CHECK(b.statistics.capacity==8);
    CHECK(b.statistics.main_audio_requested==32);
    app::TemporaryEffectPresentation owner;owner.reset(1);owner.consume(b,0);
    for(unsigned i=41;i<=72;++i)host.committed_temporary_effect(event(i));
    b=host.drain_temporary_effects(0);owner.consume(b,0);
    hlclient::renderer::RenderScene scene;owner.present(scene,0);
    CHECK(owner.sprites().size()==32);CHECK(scene.world_effect_light_count==8);CHECK(scene.world_particle_count==256);
    CHECK(owner.statistics().sprite_evictions==32);CHECK(owner.statistics().light_evictions==48);
    CHECK(host.active());CHECK(host.drain_audio().count==0);
    scene={};owner.present(scene,4);CHECK(owner.sprites().empty());CHECK(scene.world_particle_count==0);
}
TEST_CASE("E16 invalid late water and reset events have typed bounded outcomes", "[e16][temporary-effects]") {
    a::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};setup(host);
    auto e=event();e.origin.x=std::numeric_limits<float>::quiet_NaN();host.committed_temporary_effect(e);
    auto b=host.drain_temporary_effects(0);CHECK(b.count==0);CHECK(b.statistics.invalid==1);
    host.committed_temporary_effect(event(2,c::TemporaryEffectKind::bubbles));
    host.committed_temporary_effect(event(3,c::TemporaryEffectKind::sparks));
    b=host.drain_temporary_effects(0);REQUIRE(b.count==1);REQUIRE(b.effects[0].particles);
    CHECK(b.statistics.sparks==1);CHECK(b.statistics.deferred_kind==1); // E19 implements the already framed spark component.
    host.committed_temporary_effect(event(4));b=host.drain_temporary_effects(1);CHECK(b.count==0);CHECK(b.statistics.stale==1);
    app::TemporaryEffectPresentation owner;owner.reset(2);a::TemporaryEffectsBatch stale;
    stale.count=1;stale.effects[0].identity={1,1,0,0};owner.consume(stale,0);CHECK(owner.statistics().stale==1);
}
TEST_CASE("E16 receipt origin subtraction tolerates only floating point clock noise", "[e16][temporary-effects][clock]") {
    a::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};setup(host);
    auto same=event();same.received_at_seconds=10.3;host.committed_temporary_effect(same);
    auto batch=host.drain_temporary_effects(.3);REQUIRE(batch.count==1);
    CHECK(batch.effects[0].starts_at_seconds<=.3);
    app::TemporaryEffectPresentation owner;owner.reset(1);owner.consume(batch,.3);
    hlclient::renderer::RenderScene scene;owner.present(scene,.3);CHECK(owner.sprites().size()==1);
    auto future=event(2);future.received_at_seconds=10.30001;host.committed_temporary_effect(future);
    batch=host.drain_temporary_effects(.3);CHECK(batch.count==0);CHECK(batch.statistics.stale==1);
}
TEST_CASE("E16 explosion sample preparation is immutable bounded and survives readiness polling",
          "[e16][temporary-effects][weapon-audio][bounds]") {
    hlclient::games::halflife::TemporaryEffects effects;
    const auto preparation=effects.sound_preparation();REQUIRE(preparation.size()==3);
    CHECK(preparation[0].name()=="weapons/explode3.wav");
    CHECK(preparation[1].name()=="weapons/explode4.wav");
    CHECK(preparation[2].name()=="weapons/explode5.wav");
    for(const auto& reference:preparation)
        CHECK(reference.source==a::SoundReferenceSource::pinned_halflife_client_sound_profile);
    effects.reset(1);CHECK(effects.drain(0).count==0);
    CHECK(effects.sound_preparation().data()==preparation.data());
    auto e=event();effects.commit(e);const auto first=effects.drain(10);
    REQUIRE(first.count==1);REQUIRE(first.effects[0].sound);
    const auto selected=first.effects[0].sound->reference;
    CHECK_FALSE(selected.name().starts_with("weapons/debris"));
    effects.commit(e);CHECK(effects.drain(10).count==0);
    effects.reset(2);CHECK(effects.sound_preparation().data()==preparation.data());
    effects.reset(1);effects.commit(e);const auto repeat=effects.drain(10);
    REQUIRE(repeat.count==1);REQUIRE(repeat.effects[0].sound);
    CHECK(repeat.effects[0].sound->reference==selected);
    effects.reset(0);effects.commit(e);CHECK(effects.drain(10).count==0);
    CHECK(effects.sound_preparation().size()==3);
}
TEST_CASE("E16 explosion audio uses committed world provenance without projectile or weapon assumptions",
          "[e16][temporary-effects][weapon-audio][life]") {
    a::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};setup(host);
    auto first=event();first.sprite_index=0;first.scale_tenths=0;first.framerate=0;
    host.committed_temporary_effect(first);
    auto second=first;second.entry_ordinal=1;host.committed_temporary_effect(second);
    auto batch=host.drain_temporary_effects(0);REQUIRE(batch.count==2);
    CHECK(batch.effects[0].identity!=batch.effects[1].identity);
    for(std::size_t i=0;i<batch.count;++i) {
        const auto& intent=batch.effects[i];
        REQUIRE(intent.sound);CHECK_FALSE(intent.sprite);
        CHECK(intent.origin.x==first.origin.x);CHECK(intent.origin.y==first.origin.y);
        CHECK(intent.origin.z==first.origin.z);CHECK(intent.starts_at_seconds==0);
    }
    host.committed_temporary_effect(first);host.committed_temporary_effect(second);
    CHECK(host.drain_temporary_effects(0).count==0);
    c::RuntimeClientObservationState dead;dead.generation=1;
    dead.lifecycle.state=c::LocalPlayerLifeState::dead;dead.lifecycle.life_epoch=2;
    host.observe(dead,.1);host.bind_model({});host.cancel_uncommitted();
    auto later=event(3);later.received_at_seconds=10.1;host.committed_temporary_effect(later);
    batch=host.drain_temporary_effects(.1);REQUIRE(batch.count==1);REQUIRE(batch.effects[0].sound);
    CHECK(host.drain_audio().count==0);CHECK(host.drain_remote_audio().count==0);
    host.reset({2,2,1});host.committed_temporary_effect(later);
    CHECK(host.drain_temporary_effects(.1).count==0);
    host.teardown();later.generation=2;host.committed_temporary_effect(later);
    CHECK(host.drain_temporary_effects(.1).count==0);
}
TEST_CASE("E16 committed explosion reaches production temporary audio and isolated spatial PCM once",
          "[e16][temporary-effects][weapon-audio][audio]") {
    a::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};setup(host);
    auto e=event();e.origin={0,64,0};host.committed_temporary_effect(e);
    const auto batch=host.drain_temporary_effects(0);REQUIRE(batch.count==1);REQUIRE(batch.effects[0].sound);
    ExplosionSamples samples;ExplosionSink sink;sink.set_listener({{0,0,0},{0,1,0},.25F,false});
    hlclient::goldsrc::TemporaryAudio playback{sink};playback.reset(1);
    playback.update(batch,&samples,0,true);
    REQUIRE(sink.starts.size()==1);const auto& start=sink.starts[0];
    CHECK(start.origin.x==0);CHECK(start.origin.y==64);CHECK(start.origin.z==0);
    CHECK_FALSE(start.local);CHECK_FALSE(start.loop_enabled);CHECK(start.volume==1);
    CHECK(start.pitch==1);CHECK(start.attenuation==Catch::Approx(.8));
    REQUIRE(samples.requested.size()>=1);
    CHECK(samples.requested.back()==batch.effects[0].sound->reference);
    CHECK_FALSE(samples.requested.back().name().starts_with("weapons/debris"));
    CHECK(playback.statistics().submitted==1);CHECK(sink.output.mixer.statistics().started==1);
    std::array<float,512> pcm{};sink.output.mixer.render(pcm);
    double left{},right{};
    for(std::size_t i=0;i<pcm.size();i+=2) {
        CHECK(std::isfinite(pcm[i]));CHECK(std::isfinite(pcm[i+1]));
        left+=std::abs(pcm[i]);right+=std::abs(pcm[i+1]);
    }
    CHECK(right>1);CHECK(right>left);
    playback.update(batch,&samples,0,true);playback.update({},&samples,.1,true);
    CHECK(sink.starts.size()==1);CHECK(playback.event_statistics().duplicate==1);
    host.committed_temporary_effect(e);CHECK(host.drain_temporary_effects(.1).count==0);
    CHECK(host.drain_audio().count==0);CHECK(host.drain_remote_audio().count==0);
    ExplosionSink distant_sink;distant_sink.set_listener({{0,3000,0},{0,1,0},.25F,false});
    hlclient::goldsrc::TemporaryAudio distant{distant_sink};distant.reset(1);distant.update(batch,&samples,0,true);
    CHECK(explosion_energy(distant_sink.output)==0); // Real allocation can be distance-attenuated, not falsely missing.
}
TEST_CASE("E16 temporary audio readiness flags failure and reset are bounded without old sound replay",
          "[e16][temporary-effects][weapon-audio][audio][bounds]") {
    using Status=hlclient::goldsrc::SoundAssetStatus;
    a::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};setup(host);
    host.committed_temporary_effect(event());const auto batch=host.drain_temporary_effects(0);
    REQUIRE(batch.count==1);REQUIRE(batch.effects[0].sound);
    SECTION("pending can become ready only inside current short deadline") {
        ExplosionSamples samples;samples.status=Status::pending;ExplosionSink sink;
        hlclient::goldsrc::TemporaryAudio playback{sink};playback.reset(1);
        playback.update(batch,&samples,0,true);CHECK(sink.starts.empty());
        samples.status=Status::ready;playback.update({},&samples,.1,true);
        REQUIRE(sink.starts.size()==1);CHECK(explosion_energy(sink.output)>0);
        playback.update(batch,&samples,.1,true);CHECK(sink.starts.size()==1);
    }
    SECTION("late pending never starts an old explosion") {
        ExplosionSamples samples;samples.status=Status::pending;ExplosionSink sink;
        hlclient::goldsrc::TemporaryAudio playback{sink};playback.reset(1);
        playback.update(batch,&samples,0,true);samples.status=Status::ready;
        playback.update({},&samples,.3,true);CHECK(sink.starts.empty());CHECK(playback.statistics().late==1);
    }
    SECTION("missing and rejected audio leaves already accepted visual intent alone") {
        for(const auto status:{Status::missing,Status::not_authorized,Status::open_failed,Status::decode_failed,Status::limit}) {
            CAPTURE(status);ExplosionSamples samples;samples.status=status;ExplosionSink sink;
            hlclient::goldsrc::TemporaryAudio playback{sink};playback.reset(1);
            playback.update(batch,&samples,0,true);CHECK(sink.starts.empty());
            CHECK(playback.statistics().missing+playback.statistics().limits==1);
            REQUIRE(batch.effects[0].sprite);REQUIRE(batch.effects[0].light);REQUIRE(batch.effects[0].particles);
        }
    }
    SECTION("NOSOUND still presents other components but creates no request or voice") {
        auto e=event(2);e.flags=4;host.committed_temporary_effect(e);
        const auto silent=host.drain_temporary_effects(0);REQUIRE(silent.count==1);CHECK_FALSE(silent.effects[0].sound);
        ExplosionSamples samples;ExplosionSink sink;hlclient::goldsrc::TemporaryAudio playback{sink};playback.reset(1);
        playback.update(silent,&samples,0,true);CHECK(samples.requested.empty());CHECK(sink.starts.empty());
        REQUIRE(silent.effects[0].sprite);REQUIRE(silent.effects[0].light);REQUIRE(silent.effects[0].particles);
    }
    SECTION("map reset cancels pending and rejects stale provenance") {
        ExplosionSamples samples;samples.status=Status::pending;ExplosionSink sink;
        hlclient::goldsrc::TemporaryAudio playback{sink};playback.reset(1);
        playback.update(batch,&samples,0,true);playback.reset(2);samples.status=Status::ready;
        playback.update({},&samples,.1,true);playback.update(batch,&samples,.1,true);
        CHECK(sink.starts.empty());CHECK(playback.event_statistics().stale==1);
    }
    SECTION("unknown committed order is rejected rather than guessed from opaque ID") {
        auto unknown_order=batch;unknown_order.effects[0].record_ordinal=0;
        ExplosionSamples samples;ExplosionSink sink;hlclient::goldsrc::TemporaryAudio playback{sink};playback.reset(1);
        playback.update(unknown_order,&samples,0,true);
        CHECK(sink.starts.empty());CHECK(samples.requested.empty());CHECK(playback.event_statistics().invalid==1);
    }
}
TEST_CASE("E16 explosion automatic world voice coexists with gunshot and server debris across life reset",
          "[e16][temporary-effects][weapon-audio][audio][life]") {
    ExplosionSamples samples;ExplosionSink sink;sink.set_listener({{}, {0,1,0},.2F,false});
    a::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};setup(host);
    host.committed_temporary_effect(event());const auto batch=host.drain_temporary_effects(0);
    hlclient::goldsrc::ServerAudio server{sink};server.reset(1);
    hlclient::goldsrc::TemporaryAudio temporary{sink};temporary.reset(1);
    temporary.update(batch,&samples,0,true);
    hlclient::goldsrc::LocalAudio weapon{sink};a::LocalAudioBatch fire;fire.session=1;fire.scope=1;fire.count=1;
    auto& shot=fire.cues[0];shot.reference.source=a::SoundReferenceSource::pinned_halflife_client_sound_profile;
    constexpr std::string_view gunshot="weapons/pl_gun3.wav";
    std::copy(gunshot.begin(),gunshot.end(),shot.reference.sample.begin());
    shot.scope=1;shot.serial=1;shot.channel=a::LocalSoundChannel::weapon;shot.kind=a::LocalSoundKind::fire;
    weapon.update(fire,&samples,0,true);
    hlclient::goldsrc::CommittedSound debris;debris.generation=1;debris.record=1;debris.ordinal=1;
    debris.opcode=hlclient::goldsrc::RuntimeControlOpcode::svc_sound;
    debris.sound.channel=3;debris.sound.entity_reference=17;debris.sound.sound_reference=7;
    debris.sound.origin={24,12,8};debris.sound.volume=140;
    server.consume(debris,{});server.update(&samples,{});
    REQUIRE(sink.starts.size()==3);CHECK(sink.output.mixer.active()==3);
    CHECK(sink.starts[0].voice!=sink.starts[1].voice);CHECK(sink.starts[0].voice!=sink.starts[2].voice);
    CHECK(sink.starts[1].voice!=sink.starts[2].voice);
    CHECK(explosion_energy(sink.output)>0);
    c::RuntimeClientObservationState dead;dead.generation=1;dead.lifecycle.life_epoch=2;
    dead.lifecycle.state=c::LocalPlayerLifeState::dead;host.observe(dead,.1);host.bind_model({});host.cancel_uncommitted();
    temporary.update(host.drain_temporary_effects(.1),&samples,.1,true);
    CHECK(sink.output.mixer.active()==3);CHECK(sink.starts.size()==3);
    temporary.reset(2);CHECK(sink.output.mixer.active()==2); // Only the neutral world group stops.
    auto newer=event(2);newer.generation=2;newer.received_at_seconds=10.1;
    host.reset({2,2,1});host.set_movement_audio_time_origin(10);host.committed_temporary_effect(newer);
    temporary.update(host.drain_temporary_effects(.1),&samples,.1,true);
    CHECK(sink.starts.size()==4);CHECK(sink.output.mixer.active()==3);
}
TEST_CASE("E16 temporary audio bounded dedup accepts ordered arbitrary IDs beyond256 and rejects stale replay",
          "[e16][temporary-effects][weapon-audio][audio][bounds]") {
    a::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};setup(host);
    ExplosionSamples samples;auto short_pcm=std::make_shared<hlclient::assets::AudioAsset>();
    short_pcm->sample_rate=48000;short_pcm->channel_count=1;short_pcm->interleaved_samples.assign(8,.125F);
    samples.pcm=std::move(short_pcm);
    ExplosionSink sink;hlclient::goldsrc::TemporaryAudio playback{sink};playback.reset(1);
    a::TemporaryEffectsBatch earliest;
    for(std::uint64_t i=1;i<=320;++i) {
        const auto now=static_cast<double>(i-1)*.02;
        auto e=event(10000-i);e.record_ordinal=static_cast<std::size_t>(i);
        e.received_at_seconds=10+now;host.committed_temporary_effect(e);
        const auto batch=host.drain_temporary_effects(now);REQUIRE(batch.count==1);
        CHECK(batch.effects[0].record_ordinal==i);
        if(i==1) earliest=batch;
        playback.update(batch,&samples,now,true);
        std::array<float,32> pcm{};sink.output.mixer.render(pcm);
    }
    CHECK(sink.starts.size()==320);CHECK(playback.statistics().submitted==320);
    CHECK(playback.event_statistics().accepted==320);CHECK(playback.event_statistics().capacity==0);
    CHECK(sink.output.mixer.statistics().rejected==0);CHECK(sink.output.mixer.active()==0);
    // Re-injection after ring eviction is still old committed provenance,
    // even if a malicious caller moves its timestamp into today's window.
    earliest.effects[0].starts_at_seconds=6.4;
    playback.update(earliest,&samples,6.4,true);CHECK(sink.starts.size()==320);
    CHECK(playback.event_statistics().stale>=1);
    CHECK(host.temporary_sound_preparation().size()==3);
    host.teardown();CHECK(host.temporary_sound_preparation().empty());
}
TEST_CASE("E16 opt-in approved installed explosion family reaches isolated normal temporary audio PCM",
          "[e16][temporary-effects][installed-explosion][local-capture-assets][audio]") {
    std::filesystem::path root;
#ifdef _WIN32
    wchar_t* value=nullptr;std::size_t count=0;
    if(_wdupenv_s(&value,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && value) root=value;
    std::free(value);
#else
    if(const char* value=std::getenv("HLCLIENT_LOCAL_GAME_ROOT")) root=value;
#endif
    if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())
        SKIP("Installed explosion control needs the exact opt-in approved research root; project-owned PCM tests remain mandatory");
    constexpr std::array tokens{"maps/crossfire.bsp","sound/weapons/explode3.wav",
        "sound/weapons/explode4.wav","sound/weapons/explode5.wav"};
    std::array<std::filesystem::file_time_type,tokens.size()> times;
    std::array<std::uintmax_t,tokens.size()> sizes;
    for(std::size_t i=0;i<tokens.size();++i) {
        if(!std::filesystem::is_regular_file(root/"valve"/tokens[i])) {
            INFO(tokens[i]);SKIP("Optional installed explosion dependency absent; no fallback installation or generated audio");
        }
        times[i]=std::filesystem::last_write_time(root/"valve"/tokens[i]);
        sizes[i]=std::filesystem::file_size(root/"valve"/tokens[i]);
    }
    namespace readiness=hlclient::tests::readiness_fixture;
    // The world establishes the same verified exact root used by application
    // client-effect preparation. The three local sounds need no server slots.
    const auto resources=readiness::parse_resource_list({{2U,tokens[0],9U,0U,0U}});
    auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info(tokens[0]),root,"valve",
        app::ReplayLocalCameraPolicy::external_live_receiving_client);
    REQUIRE(created.projection);
    hlclient::goldsrc::ApprovedSoundAssets sounds{created.projection->sound_resources()};
    a::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};setup(host);
    const auto preparation=host.temporary_sound_preparation();REQUIRE(preparation.size()==3);
    std::array<hlclient::goldsrc::SoundAssetResult,3> decoded;
    for(std::size_t i=0;i<preparation.size();++i) {
        CAPTURE(preparation[i].name());
        CHECK(std::string{"sound/"}+std::string{preparation[i].name()}==tokens[i+1]);
        decoded[i]=sounds.request_local(preparation[i]);
    }
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{2};
    for(;;) {
        bool pending=false;
        for(std::size_t i=0;i<preparation.size();++i) {
            decoded[i]=sounds.request_local(preparation[i]);
            pending=pending || decoded[i].status==hlclient::goldsrc::SoundAssetStatus::pending;
        }
        if(!pending || std::chrono::steady_clock::now()>=deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    for(std::size_t i=0;i<decoded.size();++i) {
        CAPTURE(preparation[i].name());
        REQUIRE(decoded[i].status==hlclient::goldsrc::SoundAssetStatus::ready);REQUIRE(decoded[i].asset);
        const auto& asset=*decoded[i].asset;
        CHECK(asset.sample_rate==22050);CHECK(asset.channel_count==1);REQUIRE_FALSE(asset.interleaved_samples.empty());
        REQUIRE(std::ranges::all_of(asset.interleaved_samples,[](float x){return std::isfinite(x);}));
        double square_sum{},peak{};
        for(const auto sample:asset.interleaved_samples) {
            square_sum+=static_cast<double>(sample)*sample;
            peak=std::max(peak,static_cast<double>(std::abs(sample)));
        }
        const auto duration=static_cast<double>(asset.interleaved_samples.size())/asset.sample_rate;
        const auto rms=std::sqrt(square_sum/asset.interleaved_samples.size());
        INFO("Approved installed explosion duration="<<duration<<" seconds; RMS="<<rms<<"; peak="<<peak);
        CHECK(duration>1.25);CHECK(duration<1.35);CHECK(peak>.85);CHECK(peak<=1);CHECK(rms>.35);CHECK(rms<.55);
    }
    std::array<bool,3> isolated_mix_checked{};
    // Independent committed events naturally select all three profile members;
    // never replace an intent by a hand-made audio command or replay gunshot PCM.
    for(std::uint64_t record=1;record<=64 && !std::ranges::all_of(isolated_mix_checked,[](bool x){return x;});++record) {
        const auto now=static_cast<double>(record-1)*.02;
        auto e=event(record);e.origin={0,64,0};e.received_at_seconds=10+now;
        host.committed_temporary_effect(e);const auto batch=host.drain_temporary_effects(now);
        REQUIRE(batch.count==1);REQUIRE(batch.effects[0].sound);
        const auto selected=batch.effects[0].sound->reference;
        const auto found=std::ranges::find_if(preparation,[&](const auto& reference){return reference==selected;});
        REQUIRE(found!=preparation.end());const auto index=static_cast<std::size_t>(found-preparation.begin());
        if(isolated_mix_checked[index]) continue;
        ExplosionSink sink;sink.set_listener({{0,0,0},{0,1,0},.25F,false});
        hlclient::goldsrc::TemporaryAudio playback{sink};playback.reset(1);playback.update(batch,&sounds,now,true);
        REQUIRE(sink.starts.size()==1);CHECK(sink.starts[0].asset==decoded[index].asset);
        CHECK_FALSE(sink.starts[0].local);CHECK_FALSE(sink.starts[0].loop_enabled);
        CHECK(sink.starts[0].origin.x==0);CHECK(sink.starts[0].origin.y==64);CHECK(sink.starts[0].origin.z==0);
        CHECK(playback.statistics().submitted==1);CHECK(sink.output.mixer.statistics().started==1);
        std::vector<float> pcm(48000U*4U);sink.output.mixer.render(pcm);
        CHECK(std::ranges::all_of(pcm,[](float x){return std::isfinite(x);}));
        CHECK(std::ranges::any_of(pcm,[](float x){return x!=0;}));CHECK(sink.output.mixer.active()==0);
        playback.update(batch,&sounds,now,true);CHECK(sink.starts.size()==1);
        CHECK(host.drain_audio().count==0);CHECK(host.drain_remote_audio().count==0);
        isolated_mix_checked[index]=true;
    }
    CHECK(std::ranges::all_of(isolated_mix_checked,[](bool x){return x;}));
    host.teardown();
    for(std::size_t i=0;i<tokens.size();++i) {
        CHECK(times[i]==std::filesystem::last_write_time(root/"valve"/tokens[i]));
        CHECK(sizes[i]==std::filesystem::file_size(root/"valve"/tokens[i]));
    }
}
