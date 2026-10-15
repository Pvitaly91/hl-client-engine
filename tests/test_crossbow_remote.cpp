#include "crossbow_test_fixture.hpp"
#include <hlclient/app/temporary_effect_presentation.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/goldsrc/sound_assets.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
namespace f=hlclient::tests::crossbow_fixture;
namespace api=hlclient::game_api;
namespace g=hlclient::goldsrc;
namespace a=hlclient::audio;
namespace {
struct Samples final:g::SoundAssets {
    std::shared_ptr<const hlclient::assets::AudioAsset> pcm;
    Samples(){auto p=std::make_shared<hlclient::assets::AudioAsset>();p->sample_rate=48000;p->channel_count=1;
        p->interleaved_samples.assign(24000,.125F);pcm=std::move(p);}
    g::SoundAssetResult request(std::uint16_t) noexcept override {return {g::SoundAssetStatus::ready,pcm};}
    g::SoundAssetResult request_local(const api::LocalSoundReference&) noexcept override {return {g::SoundAssetStatus::ready,pcm};}
};
struct Sink:a::Output {
    a::OfflineOutput output;std::vector<a::Command> starts;
    bool submit(const a::Command& c) noexcept override {if(c.operation==a::Operation::start)starts.push_back(c);return output.submit(c);}
    void set_listener(a::Listener l) noexcept override {output.set_listener(l);}
};
}
TEST_CASE("E19 assigned remote ordinary event is spatial launch audio only and cannot create a local projectile", "[e19][remote-effects][weapon-audio]") {
    f::Module m;(void)m.host.drain_remote_audio();auto event=f::event(false,3);
    m.host.committed_scripted_event(event);const auto effects=m.host.drain_remote_effects(.03);
    CHECK(effects.count==0);CHECK(effects.statistics.fire==1);CHECK(effects.statistics.accepted==1);
    CHECK(m.host.drain_temporary_effects(.03).count==0);
    const auto audio=m.host.drain_remote_audio();REQUIRE(audio.count==2);
    CHECK(audio.cues[0].reference.name()=="weapons/xbow_fire1.wav");
    CHECK(audio.cues[1].reference.name()=="weapons/xbow_reload1.wav");
    for(std::size_t i=0;i<audio.count;++i) {
        REQUIRE(audio.cues[i].world_origin);CHECK(audio.cues[i].world_origin->x==event.origin.x);
        CHECK(audio.cues[i].channel==api::LocalSoundChannel::automatic);
        CHECK(audio.cues[i].pitch>=.93F);CHECK(audio.cues[i].pitch<=1.08F);
    }
    Samples samples;Sink sink;sink.set_listener({{90,80,52},{1,0,0},1,false});g::LocalAudio player(sink,true);
    player.update(audio,&samples,.03,true);REQUIRE(sink.starts.size()==2);
    std::array<float,512> pcm{};sink.output.mixer.render(pcm);
    CHECK(std::ranges::all_of(pcm,[](float v){return std::isfinite(v);}));
    CHECK(std::ranges::any_of(pcm,[](float v){return v!=0;}));
    m.host.committed_scripted_event(event);CHECK(m.host.drain_remote_effects(.04).count==0);
    CHECK(m.host.drain_remote_audio().count==0);CHECK(m.host.sample(.04).primary_fire_starts==0);
}
TEST_CASE("E19 remote sniper hit distinct spatial sound and neutral stuck model never flash or explosion", "[e19][remote-effects][temporary-effects][weapon-audio]") {
    f::Module m;(void)m.host.drain_remote_audio();m.host.committed_scripted_event(f::event(true,3));
    const auto batch=m.host.drain_remote_effects(.03);REQUIRE(batch.count==1);const auto& e=batch.effects[0];
    CHECK(e.action.weapon_id==6);REQUIRE(e.impact);CHECK(e.impact->suppress_decal);
    CHECK_FALSE(e.flash);CHECK_FALSE(e.light);CHECK_FALSE(e.shell);CHECK_FALSE(e.impact->tracer);
    CHECK(e.impact->maximum_distance_units==8192);CHECK(m.host.drain_remote_audio().count==2);
    m.host.remote_world_impact(e.action,{0,80,80},.04);
    const auto hit=m.host.drain_remote_audio();REQUIRE(hit.count==1);
    CHECK(hit.cues[0].reference.name()=="weapons/xbow_hit1.wav");REQUIRE(hit.cues[0].world_origin);
    CHECK(hit.cues[0].world_origin->x==0);CHECK(hit.cues[0].marker_ordinal==2);
    Samples samples;Sink sink;sink.set_listener({{10,80,80},{0,1,0},1,false});g::LocalAudio player(sink,true);
    player.update(hit,&samples,.04,true);REQUIRE(sink.starts.size()==1);
    std::array<float,512> pcm{};sink.output.mixer.render(pcm);
    CHECK(std::ranges::any_of(pcm,[](float v){return v!=0;})); // isolated hit, no launch voice
    const auto temporary=m.host.drain_temporary_effects(.04);REQUIRE(temporary.count==2);
    REQUIRE(temporary.effects[0].model);CHECK(temporary.effects[0].origin.x==Catch::Approx(10));
    REQUIRE(temporary.effects[1].particles);CHECK_FALSE(temporary.effects[1].light);CHECK_FALSE(temporary.effects[1].sound);
    m.host.remote_world_impact(e.action,{0,80,80},.05);CHECK(m.host.drain_remote_audio().count==0);
    CHECK(m.host.drain_temporary_effects(.05).count==0);CHECK(m.host.drain_temporary_effects(.2).count==0);
}
TEST_CASE("E19 remote life reset unknown event absent parameters stale duplicate and local echo fail closed", "[e19][remote-effects][lifecycle]") {
    for(unsigned reason=0;reason<6;++reason) {
        f::Module m;(void)m.host.drain_remote_audio();auto e=f::event(true,3);
        if(reason==0)e.event_index=903;if(reason==1)e.integer_parameter2.reset();
        if(reason==2)e.generation=2;if(reason==3)e.emitter_entity=1;
        m.host.committed_scripted_event(e);
        if(reason==4)m.host.remote_slot_boundary(3);
        const auto b=m.host.drain_remote_effects(reason==5 ? 1 : .03);
        CHECK(b.count==0);CHECK(m.host.drain_remote_audio().count==0);
    }
    f::Module m;const std::array remapped{f::binding(false,31),f::binding(true,32)};
    m.host.configure_scripted_events(remapped);auto e=f::event(true,3);e.event_index=32;
    m.host.committed_scripted_event(e);CHECK(m.host.drain_remote_effects(.03).count==1);
    m.host.reset({2,2,1});CHECK(m.host.drain_temporary_effects(.04).count==0);
    CHECK(m.host.drain_remote_audio().count==0);m.host.teardown();CHECK(m.host.drain_remote_effects(.04).count==0);
}
TEST_CASE("E19 server sparks use existing particle pool while bubble trail visuals are explicitly deferred", "[e19][temporary-effects]") {
    f::Module m;hlclient::client::CommittedTemporaryEffect e;e.generation=1;e.record=e.record_ordinal=5;
    e.received_at_seconds=.03;e.kind=hlclient::client::TemporaryEffectKind::sparks;e.origin={0,0,16};
    m.host.committed_temporary_effect(e);const auto b=m.host.drain_temporary_effects(.03);
    REQUIRE(b.count==1);REQUIRE(b.effects[0].particles);CHECK_FALSE(b.effects[0].sound);CHECK_FALSE(b.effects[0].model);
    hlclient::app::TemporaryEffectPresentation owner;owner.reset(1);owner.consume(b,.03);
    hlclient::renderer::RenderScene scene;owner.present(scene,.03);CHECK(scene.world_particle_count>0);
    m.host.committed_temporary_effect(e);CHECK(m.host.drain_temporary_effects(.04).count==0);
    e.kind=hlclient::client::TemporaryEffectKind::bubble_trail;e.record=e.record_ordinal=6;
    m.host.committed_temporary_effect(e);const auto deferred=m.host.drain_temporary_effects(.04);
    CHECK(deferred.count==0);CHECK(deferred.statistics.deferred_kind==1);
    scene={};owner.present(scene,1);CHECK(scene.world_particle_count==0);
}
