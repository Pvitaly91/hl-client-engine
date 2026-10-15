#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/games/halflife/hornet_profile.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/goldsrc/sound_assets.hpp>
#include <hlclient/goldsrc/runtime_replay_fixture.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <limits>
#include <string>
#include <vector>

namespace {
namespace api=hlclient::game_api;
namespace hl=hlclient::games::halflife;
namespace client=hlclient::client;
namespace assets=hlclient::assets;
namespace audio=hlclient::audio;
namespace goldsrc=hlclient::goldsrc;
using Catch::Approx;

api::ScriptedEventBinding hornet_binding(std::uint16_t slot=913,
    std::string_view token="events/firehornet.sc") {
    api::ScriptedEventBinding b;b.index=slot;
    std::copy(token.begin(),token.end(),b.name.begin());return b;
}
api::CommittedScriptedEvent hornet_event(int mode=0,std::uint64_t record=2,
    std::uint32_t emitter=3,double time=.03,std::uint16_t slot=913) {
    api::CommittedScriptedEvent e;e.generation=1;e.record=e.record_ordinal=record;
    e.message_bit_offset=40;e.event_index=slot;e.emitter_entity=emitter;
    e.origin={40,20,12};e.angles={-60,160,0};e.received_at_seconds=time;
    e.integer_parameter1=mode;return e;
}
struct Module {
    api::GameClientHost host{hl::make_half_life_client_module()};
    explicit Module(std::uint16_t slot=913) {
        host.reset({1,1,1});host.set_movement_audio_time_origin(0);
        const std::array bindings{hornet_binding(slot)};host.configure_scripted_events(bindings);
    }
};
bool launch_name(std::string_view token) {
    return token=="agrunt/ag_fire1.wav" || token=="agrunt/ag_fire2.wav" || token=="agrunt/ag_fire3.wav";
}
struct Samples final:goldsrc::SoundAssets {
    std::shared_ptr<const assets::AudioAsset> sample;
    std::vector<std::string> requested;
    Samples() {
        auto pcm=std::make_shared<assets::AudioAsset>();pcm->sample_rate=48000;pcm->channel_count=1;
        pcm->interleaved_samples.assign(24000,.125F);sample=std::move(pcm);
    }
    goldsrc::SoundAssetResult request(std::uint16_t) noexcept override {return {};}
    goldsrc::SoundAssetResult request_local(const api::LocalSoundReference& ref) noexcept override {
        requested.emplace_back(ref.name());
        return {goldsrc::SoundAssetStatus::ready,sample};
    }
};
struct Sink final:audio::Output {
    audio::OfflineOutput output;std::vector<audio::Command> starts;
    bool submit(const audio::Command& c) noexcept override {
        if(c.operation==audio::Operation::start)starts.push_back(c);
        return output.submit(c);
    }
    void set_listener(audio::Listener value) noexcept override {output.set_listener(value);}
};
struct ServerSamples final:goldsrc::SoundAssets {
    Samples pcm;
    std::vector<std::uint16_t> requests;
    goldsrc::SoundAssetResult request(std::uint16_t index) noexcept override {
        requests.push_back(index);return {goldsrc::SoundAssetStatus::ready,pcm.sample};
    }
    std::string_view virtual_name(std::uint16_t index) const noexcept override {
        return index==5 ? "hornet/ag_buzz1.wav" : index==6 ? "hornet/ag_hornethit1.wav" : "";
    }
};
std::vector<std::byte> hornet_server_sound(unsigned entity,unsigned slot,int y) {
    // Project-owned service body, assigned sound slot and projectile entity.
    // CHAN_VOICE, explicit .8-ish attenuation, integer source coordinates.
    std::vector<std::byte> bytes{std::byte{6}};std::size_t bit=8;
    const auto put=[&](unsigned value,unsigned count) {
        for(unsigned i=0;i<count;++i,++bit) {
            if(bit/8>=bytes.size())bytes.push_back(std::byte{});
            if(value&(1U<<i))bytes[bit/8]|=static_cast<std::byte>(1U<<(bit%8));
        }
    };
    put(2,9);put(51,8);put(2,3);put(entity,11);put(slot,8);
    put(0,1);put(1,1);put(0,1);
    put(1,1);put(0,1);put(y<0,1);put(static_cast<unsigned>(std::abs(y)),12);
    return bytes;
}

// Independent minimal public Studio skeleton: all four torso controls and a
// separate lower branch, with no stock player data. Its sequence labels carry
// no weapon semantics; the server-selected sequence is always the authority.
assets::SkeletalModelAssetData owned_player_model() {
    assets::SkeletalModelAssetData m;
    constexpr std::array names{"Bip01","Bip01 Pelvis","Bip01 Spine","Spine1","Spine2","Spine3","Leg"};
    constexpr std::array parents{-1,0,1,2,3,4,1};
    for(std::size_t i=0;i<names.size();++i) {
        assets::ModelBone b;b.name=names[i];b.parent_index=parents[i];
        if(i>=2 && i<6)b.controller_indices[5]=static_cast<std::int32_t>(i-2);
        m.bones.push_back(b);
    }
    for(std::int32_t i=0;i<4;++i)m.bone_controllers.push_back({i+2,0x20,-30,30,127,i,false});
    for(unsigned i=0;i<5;++i) {
        assets::ModelSequence s;s.label="owned_server_state";s.frame_count=11;s.frames_per_second=10;
        s.source_flags=i==4 ? 0 : 1;s.blend_count=1;s.blend_start={-45,0};s.blend_end={45,0};
        if(i==1 || i==2)s.linear_movement.x=120;
        m.sequences.push_back(s);
    }
    m.skin_families.push_back({{0}});return m;
}
client::RuntimePacketEntityObservation observed_player() {
    client::RuntimePacketEntityObservation p;p.entity_number=3;
    p.player_movement_schema=p.ordinary_visual_schema=true;p.model_index=51;
    p.weapon_model_index=87; // The session resource map selects p_hgun, never a fixed weapon slot.
    p.sequence=0;p.gait_sequence=1;p.frame=0;p.frame_rate=1;p.animation_time_seconds=1;
    p.origin={40.0,20.0,12.0};p.angles={0.0,160.0,0.0};p.body=0;p.skin=0;
    for(auto& c:p.controllers)c=127;for(auto& b:p.blending)b=127;return p;
}
api::RemotePlayerPresentationContext player_context(const assets::SkeletalModelAssetData& m,
    const client::RuntimePacketEntityObservation& p,std::uint64_t ordinal,double time) {
    return {1,1,1,44,{1,2},m,p,p,nullptr,8,1,ordinal,0,time,time,time,false,ordinal,0};
}
}

TEST_CASE("E23 assigned TRACK and FAST events use shared spatial launch audio and no client world effects",
    "[e23][remote-effects][weapon-audio]") {
    for(const auto mode:{0,1})for(const auto slot:{17U,913U}) {
        Module m(static_cast<std::uint16_t>(slot));
        const auto prepared=m.host.drain_remote_audio();std::size_t launch_prepared=0;
        for(std::size_t i=0;i<prepared.prepare_count;++i)if(launch_name(prepared.prepare[i].name())) {
            ++launch_prepared;CHECK(prepared.prepare[i].source==api::SoundReferenceSource::module_profile_dependency);
        }
        CHECK(launch_prepared==3);
        const auto event=hornet_event(mode,2,3,.03,static_cast<std::uint16_t>(slot));
        m.host.committed_scripted_event(event);const auto fx=m.host.drain_remote_effects(.03);
        CHECK(fx.count==0);CHECK(fx.statistics.accepted==1);CHECK(fx.statistics.fire==1);
        CHECK(fx.statistics.impacts==0);CHECK(fx.statistics.shell_contacts==0);
        const auto cues=m.host.drain_remote_audio();REQUIRE(cues.count==1);
        const auto& c=cues.cues[0];CHECK(launch_name(c.reference.name()));
        CHECK(c.kind==api::LocalSoundKind::fire);CHECK(c.channel==api::LocalSoundChannel::automatic);
        REQUIRE(c.world_origin);CHECK(c.world_origin->x==event.origin.x);CHECK(c.world_origin->y==event.origin.y);
        CHECK(c.world_origin->z==event.origin.z);CHECK(c.volume==1);CHECK(c.pitch==1);CHECK(c.attenuation==Approx(.8));
        CHECK(m.host.drain_temporary_effects(.03).count==0);CHECK(m.host.drain_follow_events().count==0);
        CHECK(m.host.sample(.03).primary_fire_starts==0);CHECK(m.host.sample(.03).recoil_starts==0);
        // Production execution/mixer, isolated from any gunshot/step that could mask silence.
        Samples samples;Sink sink;sink.set_listener({{30,20,12},{1,0,0},1,false});
        goldsrc::LocalAudio executor(sink,true);executor.update(cues,&samples,.03,true);
        REQUIRE(sink.starts.size()==1);REQUIRE(samples.requested.size()==1);CHECK(launch_name(samples.requested[0]));
        std::array<float,1024> pcm{};sink.output.mixer.render(pcm);
        CHECK(std::ranges::all_of(pcm,[](float value){return std::isfinite(value);}));
        CHECK(std::ranges::any_of(pcm,[](float value){return std::abs(value)>.001F;}));
        executor.update(cues,&samples,.04,true);CHECK(sink.starts.size()==1);
        m.host.committed_scripted_event(event);CHECK(m.host.drain_remote_effects(.04).count==0);
        CHECK(m.host.drain_remote_audio().count==0);
    }
}

TEST_CASE("E23 rapid remote shots retain distinct emitters and event ordinals with deterministic sample replay",
    "[e23][remote-effects][dedup]") {
    std::vector<std::string> baseline;
    for(unsigned replay=0;replay<2;++replay) {
        Module m;(void)m.host.drain_remote_audio();
        auto a=hornet_event(1),b=a,c=a;b.entry_ordinal=1;b.received_at_seconds=.13;
        c.entry_ordinal=2;c.emitter_entity=4;c.origin={-40,-20,12};
        for(const auto e:{a,b,c,a,b,c})m.host.committed_scripted_event(e);
        const auto fx=m.host.drain_remote_effects(.13);CHECK(fx.count==0);
        CHECK(fx.statistics.fire==3);CHECK(fx.statistics.duplicates==3);
        const auto out=m.host.drain_remote_audio();REQUIRE(out.count==3);
        CHECK(out.cues[0].serial!=out.cues[1].serial);CHECK(out.cues[1].serial!=out.cues[2].serial);
        CHECK(out.cues[0].command_sequence!=out.cues[1].command_sequence);
        REQUIRE(out.cues[2].world_origin);CHECK(out.cues[2].world_origin->x==-40);
        for(std::size_t i=0;i<out.count;++i) {
            CHECK(launch_name(out.cues[i].reference.name()));
            if(!replay)baseline.emplace_back(out.cues[i].reference.name());
            else CHECK(out.cues[i].reference.name()==baseline[i]);
        }
        CHECK(m.host.drain_remote_effects(.14).count==0);CHECK(m.host.drain_remote_audio().count==0);
    }
}

TEST_CASE("E23 firehornet requires exact mapping and known owned fire mode without silent TRACK fallback",
    "[e23][remote-effects][validation]") {
    CHECK(hl::hornet_fire_mode(0)==hl::HornetFireMode::track);
    CHECK(hl::hornet_fire_mode(1)==hl::HornetFireMode::fast);
    for(const auto value:{-1,2,1000000})CHECK_FALSE(hl::hornet_fire_mode(value));
    for(unsigned reason=0;reason<10;++reason) {
        Module m;(void)m.host.drain_remote_audio();auto e=hornet_event();
        if(reason==0)e.integer_parameter1.reset();if(reason==1)e.integer_parameter1=2;
        if(reason==2)e.event_index=914;if(reason==3)e.emitter_entity=1;
        if(reason==4)e.generation=2;if(reason==5)e.resolution=api::ScriptedEventResolution::missing_arguments;
        if(reason==6)e.fire_delay_ticks=1;if(reason==7)e.origin.x=std::numeric_limits<float>::quiet_NaN();
        m.host.committed_scripted_event(e);
        if(reason==8)m.host.remote_slot_boundary(3);
        const auto fx=m.host.drain_remote_effects(reason==9 ? 1 : .03);
        CHECK(fx.count==0);CHECK(fx.statistics.accepted==0);CHECK(m.host.drain_remote_audio().count==0);
        if(reason==0)CHECK(fx.statistics.invalid==1);
        if(reason==1)CHECK(fx.statistics.unsupported==1);
    }
    for(const auto token:{"events/FireHornet.sc","firehornet.sc","events/firehornet.sc.bak"}) {
        Module m;const std::array b{hornet_binding(913,token)};m.host.configure_scripted_events(b);
        (void)m.host.drain_remote_audio();m.host.committed_scripted_event(hornet_event());
        CHECK(m.host.drain_remote_effects(.03).statistics.unsupported==1);CHECK(m.host.drain_remote_audio().count==0);
    }
    Module m;const std::array remap{hornet_binding(71)};m.host.configure_scripted_events(remap);
    (void)m.host.drain_remote_audio();m.host.committed_scripted_event(hornet_event());
    CHECK(m.host.drain_remote_effects(.03).statistics.unsupported==1);CHECK(m.host.drain_remote_audio().count==0);
    auto e=hornet_event(1,3,3,.04,71);m.host.committed_scripted_event(e);
    CHECK(m.host.drain_remote_effects(.04).statistics.accepted==1);CHECK(m.host.drain_remote_audio().count==1);
}

TEST_CASE("E23 remote outboxes remain bounded and reset or slot boundaries cancel only pending events",
    "[e23][remote-effects][lifecycle]") {
    Module m;(void)m.host.drain_remote_audio();
    for(unsigned i=0;i<40;++i)m.host.committed_scripted_event(hornet_event(1,i+2));
    const auto effects=m.host.drain_remote_effects(.03);CHECK(effects.count==0);
    CHECK(effects.statistics.accepted==32);CHECK(effects.statistics.capacity==8);
    CHECK(m.host.drain_remote_audio().count==32);
    m.host.committed_scripted_event(hornet_event(0,50));m.host.remote_slot_boundary(3);
    CHECK(m.host.drain_remote_effects(.04).count==0);CHECK(m.host.drain_remote_audio().count==0);
    m.host.reset({2,2,1});CHECK(m.host.drain_remote_effects(.05).statistics.accepted==0);
    CHECK(m.host.drain_remote_audio().count==0);m.host.teardown();
    CHECK(m.host.drain_remote_effects(.06).count==0);CHECK(m.host.drain_remote_audio().count==0);
}

TEST_CASE("E23 p-hgun uses the existing numeric p-model policy without fire event body overrides",
    "[e23][remote-player][pmodel][lifecycle]") {
    Module m;(void)m.host.drain_remote_audio();const auto model=owned_player_model();
    auto p=observed_player();std::uint64_t ordinal=1;
    // Owned idle/walk/run/crouch/fire observations, never client-chosen sequences.
    for(unsigned state=0;state<5;++state) {
        p.sequence=state==4 ? 3U : state%3U;p.gait_sequence=state==0 ? 0U : state==3 ? 2U : 1U;
        p.origin.x=40.0+state*10;p.animation_time_seconds=1+.1*state;
        const std::array entities{p};m.host.remote_entities(entities,8,ordinal);
        const auto context=player_context(model,p,ordinal++,1+.1*state);
        const auto before=m.host.remote_player(context);REQUIRE(before.status==api::RemotePlayerPresentationStatus::ready);
        CHECK(before.attachment_model_index==87);CHECK(before.sample.sequence==p.sequence);
        auto fire=hornet_event(state%2,20+state,3,1+.1*state);m.host.committed_scripted_event(fire);
        CHECK(m.host.drain_remote_effects(fire.received_at_seconds).count==0);
        CHECK(m.host.drain_remote_audio().count==1);
        const auto after=m.host.remote_player(context);CHECK(after.sample==before.sample);
        CHECK(after.gait_sample==before.gait_sample);CHECK(after.transform_angles==before.transform_angles);
        CHECK(after.attachment_model_index==87);CHECK(m.host.sample(fire.received_at_seconds).recoil_starts==0);
    }
    // The current zero hides an old sampled weapon immediately at death.
    const auto alive=p;p.weapon_model_index=0;p.gait_sequence=0;p.sequence=4;p.animation_time_seconds=2;
    auto death=player_context(model,p,ordinal++,2);const auto dead=m.host.remote_player(death);
    REQUIRE(dead.status==api::RemotePlayerPresentationStatus::ready);CHECK_FALSE(dead.attachment_model_index);
    auto respawn=alive;respawn.sequence=0;respawn.frame=0;respawn.animation_time_seconds=2.1;
    const std::array entities{respawn};m.host.remote_entities(entities,8,ordinal);
    auto renewed=player_context(model,respawn,ordinal++,2.1);renewed.discontinuity=true;
    CHECK(m.host.remote_player(renewed).attachment_model_index==87);
    respawn.weapon_model_index=99;respawn.animation_time_seconds=2.2;
    CHECK(m.host.remote_player(player_context(model,respawn,ordinal++,2.2)).attachment_model_index==99);
    respawn.weapon_model_index.reset();respawn.animation_time_seconds=2.3;
    CHECK_FALSE(m.host.remote_player(player_context(model,respawn,ordinal++,2.3)).attachment_model_index);
}

TEST_CASE("E23 committed projectile buzz and hit audio reach isolated PCM and survive visual disappearance without reattachment",
    "[e23][audio][runtime-replay][lifecycle]") {
    using namespace std::chrono_literals;
    for(const auto slot:{5U,6U})for(const auto y:{-100,100}) {
        auto fixture=goldsrc::make_runtime_replay_fixture({});REQUIRE(fixture);
        auto queue=std::make_shared<goldsrc::CommittedSoundQueue>();
        auto module=std::make_shared<api::GameClientHost>(hl::make_half_life_client_module());
        fixture.fixture->initialization.game_client=module;fixture.fixture->initialization.sound_events=queue;
        client::ClientWorldState world;
        auto replay=goldsrc::RuntimeReplaySession::initialize(fixture.fixture->initialization,world);REQUIRE(replay);
        auto record=fixture.fixture->records.front();record.received_at=goldsrc::SoundTime{};
        const auto encoded=hornet_server_sound(40,slot,y);
        record.payload.bytes.insert(record.payload.bytes.end(),encoded.begin(),encoded.end());
        auto malformed=record;malformed.payload.bytes.push_back(std::byte{255});
        CHECK_FALSE(replay.session->apply_record(malformed));
        goldsrc::CommittedSound committed;CHECK_FALSE(queue->pop(committed));
        REQUIRE(replay.session->apply_record(record));REQUIRE(queue->pop(committed));
        CHECK_FALSE(queue->pop(committed));CHECK(committed.sound.entity_reference==40);
        CHECK(committed.sound.sound_reference==slot);CHECK(committed.sound.channel==2);
        CHECK(committed.sound.origin[1]==y);CHECK_FALSE(replay.session->apply_record(record));
        CHECK_FALSE(queue->pop(committed));

        ServerSamples assets;Sink sink;goldsrc::ServerAudio playback(sink);
        const audio::Listener listener{{},{0,-1,0},1,false};
        const std::array<goldsrc::SoundSource,1> alive{{{40,{0,static_cast<float>(y),0},7}}};
        playback.present(alive,listener,{},1);
        CHECK(sink.starts.empty()); // Snapshots are not a local buzz/hit generator.
        playback.consume(committed,{});playback.update(&assets,{});REQUIRE(sink.starts.size()==1);
        CHECK(assets.requests==std::vector<std::uint16_t>{static_cast<std::uint16_t>(slot)});
        CHECK(sink.starts.front().origin.y==y);CHECK_FALSE(sink.starts.front().local);
        std::array<float,32> before{};sink.output.mixer.render(before);
        CHECK(std::ranges::all_of(before,[](float value){return std::isfinite(value);}));
        CHECK(before[y<0 ? 0 : 1]==0);CHECK(before[y<0 ? 1 : 0]>.001F);

        // modelindex=0 can remove the visual source before entity retirement.
        // The short server one-shot owns its PCM and original world position,
        // so neither hidden/removal nor reuse may cut or drag it to the owner.
        playback.present(alive,listener,goldsrc::SoundTime{}+10ms,1);
        playback.present({},listener,goldsrc::SoundTime{}+20ms,1);
        const std::array<goldsrc::SoundSource,1> reused{{{40,{0,static_cast<float>(-y),0},8}}};
        playback.present(reused,listener,goldsrc::SoundTime{}+30ms,1);
        playback.update(&assets,goldsrc::SoundTime{}+30ms);
        std::array<float,32> after{};sink.output.mixer.render(after);
        CHECK(after==before);CHECK(sink.starts.size()==1);CHECK(sink.output.mixer.active()==1);
        playback.consume(committed,goldsrc::SoundTime{}+30ms);playback.update(&assets,goldsrc::SoundTime{}+30ms);
        CHECK(sink.starts.size()==1);
        CHECK(module->drain_temporary_effects(.03).count==0);
        CHECK(module->drain_remote_audio().count==0);
        playback.reset(committed.generation+1);sink.output.mixer.render(after);
        CHECK(std::ranges::all_of(after,[](float value){return value==0;}));
        playback.consume(committed,goldsrc::SoundTime{}+40ms);playback.update(&assets,goldsrc::SoundTime{}+40ms);
        CHECK(sink.starts.size()==1);CHECK(sink.output.mixer.active()==0);
    }
}
