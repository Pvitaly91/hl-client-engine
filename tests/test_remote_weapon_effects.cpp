#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/goldsrc/runtime_replay_session.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include "player_origin_test_fixture.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
namespace api=hlclient::game_api;
namespace hl=hlclient::games::halflife;
namespace g=hlclient::goldsrc;
namespace f=hlclient::test::delta_fixture;
namespace p=hlclient::test::player_origin_fixture;
namespace a=hlclient::audio;
using Catch::Approx;
api::ScriptedEventBinding binding(std::uint16_t index,std::string_view token) {
    api::ScriptedEventBinding result; result.index=index;
    REQUIRE(token.size()<result.name.size());
    std::copy(token.begin(),token.end(),result.name.begin()); return result;
}
void bind(api::GameClientHost& host) {
    const std::array bindings{binding(71,"events/glock1.sc"),binding(9,"events/crowbar.sc"),
        binding(900,"events/glock2.sc")};
    host.configure_scripted_events(bindings);
}
struct Module {
    api::GameClientHost host{hl::make_half_life_client_module()};
    Module() {host.set_movement_audio_time_origin(1000);host.reset({1,1,1});bind(host);}
    api::CommittedScriptedEvent event(std::uint64_t record=1,std::uint32_t entity=3) const {
        api::CommittedScriptedEvent result;
        result.generation=1;result.record=record;result.message_bit_offset=8;result.emitter_entity=entity;
        result.event_index=71;result.origin={100,10,0};result.received_at_seconds=1001;
        return result;
    }
};
struct Loader final:g::SoundAssets {
    g::SoundAssetResult result;
    Loader() {auto pcm=std::make_shared<hlclient::assets::AudioAsset>();
        pcm->sample_rate=48000;pcm->channel_count=1;pcm->interleaved_samples.assign(4800,0.125F);
        result={g::SoundAssetStatus::ready,pcm};}
    g::SoundAssetResult request(std::uint16_t) noexcept override{return result;}
    g::SoundAssetResult request_local(const api::LocalSoundReference&) noexcept override{return result;}
};
struct Sink final:a::Output {
    a::OfflineOutput output;
    std::vector<a::Command> starts;
    bool submit(const a::Command& command) noexcept override {
        if(command.operation==a::Operation::start) starts.push_back(command);
        return output.submit(command);
    }
    void set_listener(a::Listener listener) noexcept override {output.set_listener(listener);}
};
std::shared_ptr<const g::DeltaSchemaRegistryState> schemas(bool integer_origin=false) {
    g::DeltaSchemaRegistryBuilder builder;
    const auto base=p::schemas();
    for(const auto& schema:base->schemas()) REQUIRE(builder.insert(schema));
    f::Field fields[]{
        {"entindex",8,0,10},{"bparam1",8,4,1},{"bparam2",8,8,1},
        {"origin[0]",0x80000004U,12,18,32000,4000},
        {"origin[1]",0x80000004U,16,18,32000,4000},
        {"origin[2]",0x80000004U,20,18,32000,4000},
        {"fparam1",0x80000004U,24,20,400000,4000},
        {"fparam2",0x80000004U,28,20,400000,4000},
        {"iparam1",0x80000008U,32,16},{"iparam2",0x80000008U,36,16},
        {"angles[0]",0x80000004U,40,18,32000,4000},
        {"angles[1]",0x80000004U,44,18,32000,4000},
        {"angles[2]",0x80000004U,48,18,32000,4000},{"ducking",8,52,1}};
    if(integer_origin) fields[3].type=0x00000008U;
    const auto parsed=g::DeltaDescriptionParser{}.parse(f::schema("event_t",fields),0);
    REQUIRE(parsed);REQUIRE(builder.insert(*parsed.schema));
    return std::make_shared<const g::DeltaSchemaRegistryState>(std::move(builder).publish());
}
void packet(f::BitWriter& w) {
    w.write(7,8);w.write(std::bit_cast<std::uint32_t>(30.0F),32);
    w.write(40,8);w.write(2,16);
    for(const std::uint32_t number:{1U,3U}) {
        w.write(0,1);w.write(1,1);w.write(number,11); // explicit sparse entity numbers
        w.write(0,1);w.write(0,1); // ordinary; no intra-message base
        w.write(1,3);w.write(0x47,8);
        p::coordinate(w,number==1 ? 0 : 128,8,18);
        p::coordinate(w,24,8,18);p::coordinate(w,-1660,8,18);
        w.write(7,10);
    }
    w.write(0,16);w.align_zero();
}
void event(f::BitWriter& w,std::uint32_t index=1,bool reliable=false,bool explicit_zero=false,
    std::uint32_t delay=0,std::uint32_t explicit_emitter=0) {
    w.write(reliable ? 21U : 3U,8);
    if(!reliable) w.write(1,5);
    w.write(71,10);
    if(!reliable) {w.write(1,1);w.write(index,11);w.write(explicit_zero || explicit_emitter ? 1U : 0U,1);}
    if(reliable) {w.write(1,3);w.write(1,8);w.write(3,10);} // entindex3, vectors valid0
    else if(explicit_zero || explicit_emitter) {
        w.write(1,3);w.write((explicit_zero ? 8U : 0U)|(explicit_emitter ? 1U : 0U),8);
        if(explicit_emitter) w.write(explicit_emitter,10);
        if(explicit_zero) w.write(0,18); // explicit originX0
    }
    w.write(delay ? 1U : 0U,1);if(delay) w.write(delay,16);w.align_zero();
}
struct Replay {
    std::shared_ptr<api::GameClientHost> host=std::make_shared<api::GameClientHost>(hl::make_half_life_client_module());
    hlclient::client::ClientWorldState world;
    std::unique_ptr<g::RuntimeReplaySession> session;
    explicit Replay(bool integer_origin=false) {
        g::RuntimeReplayInitialization init;
        init.generation=1;init.max_clients=4;init.schemas=schemas(integer_origin);
        init.baselines=p::baselines(*init.schemas);init.game_client=host;init.receiving_player_entity=1;
        host->set_movement_audio_time_origin(1000);
        auto created=g::RuntimeReplaySession::initialize(init,world);
        REQUIRE(created);session=std::move(created.session);bind(*host);
    }
    g::RuntimeReplayRecord record(std::vector<std::byte> bytes,std::uint32_t sequence=1) {
        auto body=p::payload(std::move(bytes),sequence);
        const auto cursor=g::StockRuntimeSourceCursor::create(0,0,body.bytes.size());REQUIRE(cursor);
        g::RuntimeReplayRecord result{1,sequence,sequence,std::move(body),*cursor};
        result.received_at=g::SoundTime{std::chrono::seconds{1001}};
        return result;
    }
};
}

TEST_CASE("Remote committed Glock effect uses exact binding and emitter without local viewmodel",
    "[remote-effects][game-module]") {
    Module f;
    const auto preparation=f.host.drain_remote_audio();REQUIRE(preparation.prepare_count==3);
    CHECK(preparation.prepare[0].name()=="weapons/pl_gun3.wav");
    auto event=f.event();f.host.committed_scripted_event(event);
    f.host.cancel_uncommitted(); // listener switches focus/model; remote event survives
    const auto result=f.host.drain_remote_effects(1.01);
    REQUIRE(result.count==1);const auto& effect=result.effects[0];
    REQUIRE(effect.flash);REQUIRE(effect.light);REQUIRE(effect.shell);REQUIRE(effect.impact);
    CHECK(effect.action.emitter_entity==3);CHECK(effect.action.generation==1);
    CHECK(effect.impact->origin.x==100);CHECK(effect.impact->origin.z==28);
    CHECK(effect.impact->direction.x==Approx(1));CHECK(effect.impact->direction.y==Approx(0));
    CHECK(effect.shell->expires_at_seconds==Approx(3.5));
    const auto audio=f.host.drain_remote_audio();REQUIRE(audio.count==1);
    CHECK(audio.cues[0].reference.name()=="weapons/pl_gun3.wav");
    REQUIRE(audio.cues[0].world_origin);CHECK(audio.cues[0].world_origin->x==100);
    CHECK(audio.cues[0].scheduled_seconds==Approx(1));
    CHECK(f.host.drain_audio().count==0);CHECK(f.host.drain_remote_effects(1.02).count==0);
    f.host.committed_scripted_event(event);CHECK(f.host.drain_remote_effects(1.03).statistics.duplicates==1);
    event=f.event(2,1);f.host.committed_scripted_event(event);
    CHECK(f.host.drain_remote_effects(1.03).statistics.local_echo==1);
    event=f.event(3);event.event_index=2;f.host.committed_scripted_event(event);
    CHECK(f.host.drain_remote_effects(1.03).statistics.unsupported==1);
    event=f.event(4);event.event_index=9;f.host.committed_scripted_event(event);
    CHECK(f.host.drain_remote_effects(1.03).count==0);
    auto swing=f.host.drain_remote_audio();REQUIRE(swing.count==1);
    CHECK(swing.cues[0].reference.name()=="weapons/cbar_miss1.wav");
}

TEST_CASE("Remote effect queue distinguishes generations delay expiry capacity and aim provenance",
    "[remote-effects][game-module][limits]") {
    Module f;
    auto event=f.event();event.resolution=api::ScriptedEventResolution::missing_packet;
    f.host.committed_scripted_event(event);CHECK(f.host.drain_remote_effects(1).statistics.unresolved==1);
    event=f.event(2);event.received_at_seconds=1000;
    f.host.committed_scripted_event(event);CHECK(f.host.drain_remote_effects(1).statistics.late==1);
    event=f.event(3);event.angles={350,90,0};event.angles_from_entity=true;event.ducking=true;
    f.host.committed_scripted_event(event);
    const auto pitch=f.host.drain_remote_effects(1);REQUIRE(pitch.count==1);REQUIRE(pitch.effects[0].impact);
    CHECK(pitch.effects[0].impact->origin.z==12);
    CHECK(pitch.effects[0].impact->direction.z==Approx(-0.5).margin(0.001));
    event=f.event(4);event.angles={0,90,0};event.spread_x=0.1F;
    f.host.committed_scripted_event(event);
    const auto spread=f.host.drain_remote_effects(1);REQUIRE(spread.count==1);
    CHECK(spread.effects[0].impact->direction.x>0.09);
    for(unsigned i=5;i<45;++i) {event=f.event(i);f.host.committed_scripted_event(event);}
    const auto bound=f.host.drain_remote_effects(1);CHECK(bound.count==32);CHECK(bound.statistics.capacity>=8);
    f.host.reset({2,2,1});f.host.committed_scripted_event(f.event(45));
    CHECK(f.host.drain_remote_effects(1).count==0);
    f.host.teardown();CHECK(f.host.drain_remote_effects(1).count==0);CHECK(f.host.drain_remote_audio().count==0);
}

TEST_CASE("Remote impacts and shell contacts produce isolated PCM without replacing local fire",
    "[remote-effects][audio][shell-audio]") {
    Module f;Loader loader;Sink sink;
    sink.set_listener({{100,0,0},{0,1,0},1,false});
    g::LocalAudio remote(sink,true),local(sink);
    f.host.committed_scripted_event(f.event());
    const auto effects=f.host.drain_remote_effects(1);REQUIRE(effects.count==1);
    const auto action=effects.effects[0].action;
    const auto fire=f.host.drain_remote_audio();remote.update(fire,&loader,1,true);
    auto localfire=fire;localfire.cues[0].world_origin.reset();
    localfire.cues[0].channel=api::LocalSoundChannel::weapon;
    local.update(localfire,&loader,1,true);
    REQUIRE(sink.starts.size()==2);CHECK(sink.starts[0].voice!=sink.starts[1].voice);
    f.host.remote_world_impact(action,{100,10,0},1.02);
    const auto impact=f.host.drain_remote_audio();REQUIRE(impact.count>=1);
    CHECK(impact.cues[0].kind==api::LocalSoundKind::impact);
    Sink isolated;isolated.set_listener({{100,0,0},{0,1,0},1,false});g::LocalAudio impact_only(isolated,true);
    impact_only.update(impact,&loader,1.02,true);
    std::array<float,32> pcm{};isolated.output.mixer.render(pcm);
    CHECK(std::all_of(pcm.begin(),pcm.end(),[](float x){return std::isfinite(x);}));
    CHECK(pcm[1]>pcm[0]);CHECK(pcm[1]>0);
    remote.update(impact,&loader,1.02,true);
    f.host.remote_world_impact(action,{100,10,0},1.02);CHECK(f.host.drain_remote_audio().count==0);
    api::LocalShellContact contact{action,1,{100,-10,0},80,1.2};
    f.host.cancel_uncommitted();f.host.remote_shell_contact(contact);
    const auto shell=f.host.drain_remote_audio();REQUIRE(shell.count==1);
    CHECK(shell.cues[0].reference.name()=="player/pl_shell1.wav");
    Sink shell_sink;shell_sink.set_listener({{100,0,0},{0,1,0},1,false});g::LocalAudio shell_only(shell_sink,true);
    shell_only.update(shell,&loader,1.2,true);shell_sink.output.mixer.render(pcm);
    CHECK(pcm[0]>pcm[1]);CHECK(pcm[0]>0);
    remote.update(shell,&loader,1.2,true);
    CHECK(sink.output.mixer.active()>=4);
    f.host.remote_shell_contact(contact);CHECK(f.host.drain_remote_audio().count==0);
    contact.ordinal=2;contact.inward_normal_speed=10;f.host.remote_shell_contact(contact);
    CHECK(f.host.drain_remote_audio().count==0);
    contact.inward_normal_speed=50;f.host.remote_shell_contact(contact);CHECK(f.host.drain_remote_audio().count==1);
    f.host.reset({2,2,1});f.host.remote_shell_contact(contact);CHECK(f.host.drain_remote_audio().count==0);
}

TEST_CASE("Committed event packet ordinal resolves sparse player before normal module effects",
    "[remote-effects][runtime-replay][scripted-events]") {
    Replay f;f::BitWriter writer;packet(writer);event(writer);
    const auto record=f.record(writer.bytes());const auto applied=f.session->apply_record(record);
    REQUIRE(applied);const auto output=f.host->drain_remote_effects(1.01);REQUIRE(output.count==1);
    const auto& effect=output.effects[0];CHECK(effect.action.emitter_entity==3);
    REQUIRE(effect.impact);CHECK(effect.impact->origin.x==128);CHECK(effect.impact->origin.y==24);
    CHECK(effect.impact->origin.z==-1632); // packet origin -1660 plus HL standing gun height28
    REQUIRE_FALSE(f.session->apply_record(record));CHECK(f.host->drain_remote_effects(1.02).count==0);
    f::BitWriter bad;packet(bad);event(bad);bad.write(0,8);
    const auto before=f.world.runtime_observation();REQUIRE_FALSE(f.session->apply_record(f.record(bad.bytes(),2)));
    CHECK(f.world.runtime_observation()==before);CHECK(f.host->drain_remote_effects(1.02).count==0);
}

TEST_CASE("Scripted event explicit zero vectors survive while bad packet and delayed events skip",
    "[remote-effects][runtime-replay][scripted-events]") {
    SECTION("Unreliable explicit zero origin is not replaced by entity position") {
        Replay f;f::BitWriter writer;packet(writer);event(writer,1,false,true);
        const auto applied=f.session->apply_record(f.record(writer.bytes()));REQUIRE(applied);
        const auto output=f.host->drain_remote_effects(1);REQUIRE(output.count==1);
        CHECK(output.effects[0].impact->origin.x==0);CHECK(output.effects[0].impact->origin.y==0);
        CHECK(output.effects[0].impact->origin.z==28);
        const auto& body=std::get<g::RuntimeControlScriptedEvents>(
            std::get<g::RuntimeControlEvent>(applied.event->decoded_batch.events.back()).body);
        CHECK(body.entries[0].argument_field_mask==8);
    }
    SECTION("Reliable entindex and null-base zero vectors need no packet snapshot") {
        Replay f;f::BitWriter writer;event(writer,0,true);
        REQUIRE(f.session->apply_record(f.record(writer.bytes())));
        const auto output=f.host->drain_remote_effects(1);REQUIRE(output.count==1);
        CHECK(output.effects[0].action.emitter_entity==3);CHECK(output.effects[0].impact->origin.x==0);
    }
    for(const auto index:{2U,2047U}) {
        Replay f;f::BitWriter writer;packet(writer);event(writer,index);
        REQUIRE(f.session->apply_record(f.record(writer.bytes())));
        const auto output=f.host->drain_remote_effects(1);CHECK(output.count==0);CHECK(output.statistics.unresolved==1);
    }
    SECTION("No stale previous packet fallback") {
        Replay f;f::BitWriter first;packet(first);REQUIRE(f.session->apply_record(f.record(first.bytes())));
        f::BitWriter second;event(second);REQUIRE(f.session->apply_record(f.record(second.bytes(),2)));
        CHECK(f.host->drain_remote_effects(1).statistics.unresolved==1);
    }
    SECTION("Unsupported nonzero wire delay is consumed without immediate false shot") {
        Replay f;f::BitWriter writer;packet(writer);event(writer,1,false,false,37);
        REQUIRE(f.session->apply_record(f.record(writer.bytes())));
        const auto output=f.host->drain_remote_effects(1);CHECK(output.count==0);CHECK(output.statistics.unresolved==1);
    }
    SECTION("Conflicting explicit entity and packet ordinal cannot select an emitter") {
        Replay f;f::BitWriter writer;packet(writer);event(writer,1,false,false,0,2);
        REQUIRE(f.session->apply_record(f.record(writer.bytes())));
        const auto output=f.host->drain_remote_effects(1);CHECK(output.count==0);CHECK(output.statistics.unresolved==1);
    }
    SECTION("Integer masquerading as an event coordinate rejects only the presentation") {
        Replay f(true);f::BitWriter writer;packet(writer);event(writer,1,false,true);
        REQUIRE(f.session->apply_record(f.record(writer.bytes())));
        const auto output=f.host->drain_remote_effects(1);CHECK(output.count==0);CHECK(output.statistics.unresolved==1);
    }
}
