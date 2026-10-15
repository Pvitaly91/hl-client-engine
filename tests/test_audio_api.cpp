#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/assets/animation_markers.hpp>
#include <hlclient/client/runtime_observation.hpp>
#include <limits>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include "local_movement_test_fixture.hpp"
namespace {
class ProjectAssets final : public hlclient::goldsrc::SoundAssets {
public:
    bool ready{};
    std::shared_ptr<hlclient::assets::AudioAsset> pcm=std::make_shared<hlclient::assets::AudioAsset>();
    ProjectAssets() {pcm->sample_rate=48000;pcm->channel_count=1;pcm->interleaved_samples={0.125F,0.25F};}
    hlclient::goldsrc::SoundAssetResult request(std::uint16_t) noexcept override {return {};}
    hlclient::goldsrc::SoundAssetResult request_local(const hlclient::game_api::LocalSoundReference&) noexcept override {
        return {ready ? hlclient::goldsrc::SoundAssetStatus::ready : hlclient::goldsrc::SoundAssetStatus::pending,pcm};
    }
};
}
TEST_CASE("E21 neutral persistent voices preserve cursor pitch and independent owners", "[e21][audio-api][core-only]") {
    namespace api=hlclient::game_api;namespace g=hlclient::goldsrc;
    hlclient::audio::OfflineOutput output;output.set_listener({{}, {0,-1,0},1,false});ProjectAssets assets;assets.ready=true;
    g::LocalAudio delivery(output);api::LocalAudioBatch batch;batch.session=1;batch.scope=1;batch.control_count=1;
    auto& c=batch.controls[0];c.identity=7;c.serial=1;c.scheduled_seconds=.02;c.pitch=1;
    constexpr std::string_view name="weapons/project.wav";std::copy(name.begin(),name.end(),c.reference.sample.begin());
    delivery.update(batch,&assets,.02,true);CHECK(output.mixer.active()==1);
    std::array<float,2> frame{};output.mixer.render(frame);CHECK(frame[0]==.125F);
    c.operation=api::LocalVoiceControl::Operation::change;c.serial=2;c.pitch=2;delivery.update(batch,&assets,.03,true);
    output.mixer.render(frame);CHECK(frame[0]==.25F);CHECK(output.mixer.statistics().started==1);CHECK(output.mixer.statistics().updated==1);
    c.operation=api::LocalVoiceControl::Operation::start;c.identity=8;c.serial=3;delivery.update(batch,&assets,.03,true);CHECK(output.mixer.active()==2);
    c.operation=api::LocalVoiceControl::Operation::stop;c.identity=7;c.serial=4;delivery.update(batch,&assets,.03,true);CHECK(output.mixer.active()==1);
    batch.session=2;batch.control_count=0;delivery.update(batch,&assets,.04,true);CHECK(output.mixer.active()==0);
    batch.session=1;batch.control_count=1;c.serial=99;delivery.update(batch,&assets,.04,true);CHECK(output.mixer.active()==0);
}
TEST_CASE("E21 persistent pending stop prevents a delayed charge sound", "[e21][audio-api][core-only]") {
    namespace api=hlclient::game_api;
    hlclient::audio::OfflineOutput output;ProjectAssets assets;hlclient::goldsrc::LocalAudio delivery(output);
    api::LocalAudioBatch b;b.session=1;b.scope=1;b.control_count=1;auto& c=b.controls[0];c.identity=c.serial=1;
    constexpr std::string_view name="weapons/project.wav";std::copy(name.begin(),name.end(),c.reference.sample.begin());
    delivery.update(b,&assets,0,true);c.serial=2;c.operation=api::LocalVoiceControl::Operation::stop;delivery.update(b,&assets,.02,true);
    assets.ready=true;b.control_count=0;delivery.update(b,&assets,.03,true);CHECK(output.mixer.statistics().started==0);
}
TEST_CASE("E21 immutable command impulse is simulation-only and deterministic on replay", "[e21][core-only][local-movement]") {
    namespace f=hlclient::tests::local_movement;namespace gm=hlclient::goldsrc::movement;
    auto original=f::make_command(1,20);const auto with=original.with_velocity_impulse({100,0,400});REQUIRE(with);
    CHECK(original.velocity_impulse()==std::array<float,3>{});CHECK(with.state->buttons()==original.buttons());
    f::DeterministicLocalMovementCollision collision;auto state=f::make_state();auto env=f::make_environment();gm::GoldSrcLocalMovementScratch scratch;
    const auto once=gm::GoldSrcLocalMovementKernel::simulate(state,*with.state,env,collision,scratch);REQUIRE(once);
    const auto replay=gm::GoldSrcLocalMovementKernel::simulate(state,*with.state,env,collision,scratch);REQUIRE(replay);
    CHECK(once.deterministic_state_signature==replay.deterministic_state_signature);CHECK(once.state->velocity().z>300);
    const auto next=f::make_command(2,20);const auto twice=gm::GoldSrcLocalMovementKernel::simulate(*once.state,next,env,collision,scratch);REQUIRE(twice);
    CHECK(twice.state->velocity().z<once.state->velocity().z);CHECK_FALSE(original.with_velocity_impulse({2001,0,0}));
}
TEST_CASE("E21 deferred stop cannot stop a reused voice lifetime or replay after session reset", "[e21][audio-api][core-only]") {
    namespace api=hlclient::game_api;hlclient::audio::OfflineOutput out;ProjectAssets assets;assets.ready=true;
    hlclient::goldsrc::LocalAudio delivery(out);api::LocalAudioBatch b;b.session=b.scope=1;b.control_count=1;
    auto& c=b.controls[0];c.identity=7;c.serial=1;c.scheduled_seconds=.02;
    constexpr std::string_view token="weapons/project.wav";std::copy(token.begin(),token.end(),c.reference.sample.begin());
    delivery.update(b,&assets,.02,true);REQUIRE(out.mixer.active()==1);
    c.operation=api::LocalVoiceControl::Operation::stop;c.serial=2;c.scheduled_seconds=.03;
    delivery.update(b,&assets,.02,true);CHECK(out.mixer.active()==1);
    c.operation=api::LocalVoiceControl::Operation::start;c.identity=8;c.serial=3;c.scheduled_seconds=.025;
    delivery.update(b,&assets,.025,true);CHECK(out.mixer.active()==2);
    b.control_count=0;delivery.update(b,&assets,.03,true);CHECK(out.mixer.active()==1);
    CHECK(out.mixer.statistics().started==2);CHECK(out.mixer.statistics().stopped==1);
    b.session=2;delivery.update(b,&assets,.04,true);CHECK(out.mixer.active()==0);
}
TEST_CASE("E17 neutral weapon extensions preserve optional ownership and legacy hash without game",
          "[e17][core-only][game-api][weapon-data]") {
    namespace client=hlclient::client;
    client::RuntimeClientObservationState state;
    state.generation=1;state.publication_revision=1;
    state.server_time_metadata.generation=1;state.entity_metadata.generation=1;
    state.client_metadata={1,client::RuntimeObservationFreshness::observed_in_record,
        client::RuntimeObservationCompleteness::complete_reconstruction,
        client::RuntimeObservationSource{1,1,0,0,8}};
    state.receiving_client.emplace();
    state.weapon_slots.push_back({.wire_slot=2,.clip=3,.weapon_id=7});
    state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
    REQUIRE(client::valid_runtime_observation(state));
    const auto legacy=state.canonical_state_hash;
    CHECK_FALSE(state.weapon_slots[0].pump_time);
    CHECK_FALSE(state.weapon_slots[0].special_reload_state);
    state.weapon_slots[0].special_reload_state=3; // Neutral integer, no game-policy fallback.
    state.weapon_slots[0].pump_time=0.5;
    CHECK(client::valid_runtime_observation(state));
    CHECK(client::runtime_observation_canonical_hash(state)==legacy);
    auto owning=state;
    state.weapon_slots[0].pump_time.reset();state.weapon_slots.clear();
    CHECK(owning.weapon_slots[0].pump_time==0.5);
    CHECK(owning.weapon_slots[0].special_reload_state==3);
    owning.weapon_slots[0].pump_time=std::numeric_limits<double>::quiet_NaN();
    CHECK_FALSE(client::valid_runtime_observation(owning));
}
TEST_CASE("Core audio API session reset cancels stale load without concrete game", "[audio-api][core-only]") {
    namespace api=hlclient::game_api;
    hlclient::audio::OfflineOutput output; output.set_listener({{}, {0,-1,0},1,false});
    hlclient::goldsrc::LocalAudio local(output); ProjectAssets assets;
    api::LocalAudioBatch b; b.session=1;b.scope=1;b.count=1;
    b.cues[0].scope=1;b.cues[0].serial=1;b.cues[0].scheduled_seconds=0.01;
    constexpr std::string_view name="weapons/project.wav";
    std::copy(name.begin(),name.end(),b.cues[0].reference.sample.begin());
    local.update(b,&assets,0.01,true);
    auto reset=b;reset.session=2;reset.count=0;local.update(reset,&assets,0.02,true);
    assets.ready=true;local.update(reset,&assets,0.03,true);CHECK(output.mixer.statistics().started==0);
    b.session=2;local.update(b,&assets,0.04,true);CHECK(output.mixer.statistics().presentation_started==1);
    std::array<float,6> pcm;output.mixer.render(pcm);
    CHECK(pcm==std::array<float,6>{0.125F,0.125F,0.25F,0.25F,0,0});
    local.update(b,&assets,0.05,true);CHECK(output.mixer.statistics().started==1);
    b.session=1;b.cues[0].serial=99;local.update(b,&assets,0.06,true);
    CHECK(output.mixer.statistics().started==1); // stale prior-session output cannot reset owner
}
TEST_CASE("Core marker metadata remains format neutral and bounded", "[audio-api][core-only]") {
    const std::array events{hlclient::assets::ModelSequenceEvent{0,87,4,{}},hlclient::assets::ModelSequenceEvent{1,88,5,{}}};
    auto zero=hlclient::assets::animation_markers(events,10,10,false,-1,0);REQUIRE(zero.count==1);CHECK(zero.values[0].ordinal==0);
    auto next=hlclient::assets::animation_markers(events,10,10,false,0,0.1);REQUIRE(next.count==1);CHECK(next.values[0].ordinal==1);
    CHECK(hlclient::assets::animation_markers(events,10,10,false,0.1,0.1).count==0);
}
TEST_CASE("Core world sound uses the same mixer without game linkage and has a bounded rolling identity window", "[audio-api][core-only][e16]") {
    namespace api=hlclient::game_api;
    hlclient::audio::OfflineOutput output;output.set_listener({{}, {0,1,0},1,false});
    ProjectAssets assets;assets.ready=true;
    hlclient::goldsrc::TemporaryAudio world{output};world.reset(1);
    api::TemporaryEffectsBatch batch;batch.count=1;
    auto& effect=batch.effects[0];effect.identity={1,1,0,0};effect.record_ordinal=1;effect.origin={0,64,0};
    effect.sound=api::TemporarySoundIntent{};
    constexpr std::string_view name="weapons/project.wav";
    std::copy(name.begin(),name.end(),effect.sound->reference.sample.begin());
    hlclient::goldsrc::TemporaryAudio inactive{output};
    effect.identity.generation=0;inactive.update(batch,&assets,0,true);
    CHECK(inactive.event_statistics().stale==1);CHECK(inactive.statistics().submitted==0);
    effect.identity.generation=1;
    world.update(batch,&assets,0,true);
    std::array<float,6> pcm{};output.mixer.render(pcm);
    CHECK(pcm[0]==0);CHECK(pcm[1]>.2F);CHECK(world.statistics().submitted==1);
    world.update(batch,&assets,0,true);CHECK(world.event_statistics().duplicate==1);
    for(std::uint64_t record=2;record<=300;++record) {
        effect.identity.record=record;effect.record_ordinal=record;world.update(batch,&assets,0,true);
        output.mixer.render(pcm);
    }
    CHECK(world.statistics().submitted==300);CHECK(world.event_statistics().capacity==0);
    effect.identity.record=1;effect.record_ordinal=1;world.update(batch,&assets,0,true);
    CHECK(world.event_statistics().stale==1);CHECK(world.statistics().submitted==300);
    effect.identity.record=301;effect.record_ordinal=301;effect.starts_at_seconds=.01;assets.ready=false;
    world.update(batch,&assets,.01,true);world.reset(2);assets.ready=true;
    world.update({},&assets,.02,true);CHECK(world.statistics().submitted==300);
    effect.identity={2,1,0,0};effect.record_ordinal=1;effect.starts_at_seconds=.02;world.update(batch,&assets,.02,true);
    CHECK(world.statistics().submitted==301);
}
TEST_CASE("Core mixer namespace teardown cannot stop unrelated server local or remote voices", "[audio-api][core-only][e16]") {
    hlclient::audio::Mixer mixer;ProjectAssets assets;
    const auto high=std::uint64_t{1}<<63;
    for(const auto identity:{std::uint64_t{37},high|1,high|(std::uint64_t{1}<<62)|4,
        high|(std::uint64_t{1}<<61)|4,high|(std::uint64_t{1}<<61)|5}) {
        hlclient::audio::Command start;start.voice=identity;start.asset=assets.pcm;
        mixer.command(start);
    }
    REQUIRE(mixer.active()==5);
    hlclient::audio::Command stop;stop.operation=hlclient::audio::Operation::stop_group;
    stop.voice=high|(std::uint64_t{1}<<61);stop.voice_group_mask=std::uint64_t{7}<<61;
    mixer.command(stop);CHECK(mixer.active()==3);CHECK(mixer.statistics().stopped==2);
    stop.voice_group_mask=0;mixer.command(stop);CHECK(mixer.active()==3);
    stop.voice_group_mask=std::uint64_t{7}<<61;stop.voice|=1;mixer.command(stop);
    CHECK(mixer.active()==3);CHECK(mixer.statistics().rejected==2);
}
TEST_CASE("Core movement pending cue survives model scope but not life epoch", "[audio-api][core-only][e8]") {
    namespace api=hlclient::game_api;
    hlclient::audio::OfflineOutput output; ProjectAssets assets;
    output.set_listener({{}, {0,-1,0},1,false});
    hlclient::goldsrc::LocalAudio local(output);
    api::LocalAudioBatch b; b.session=1; b.scope=1; b.movement_epoch=1; b.count=1;
    auto& cue=b.cues[0]; cue.scope=1; cue.serial=1; cue.scheduled_seconds=.01;
    cue.kind=api::LocalSoundKind::footstep; cue.channel=api::LocalSoundChannel::body;
    constexpr std::string_view name="player/project.wav";
    cue.reference.source=api::SoundReferenceSource::pinned_halflife_client_sound_profile;
    std::copy(name.begin(),name.end(),cue.reference.sample.begin());
    local.update(b,&assets,.01,true);
    auto model=b; model.count=0; model.scope=2; local.update(model,&assets,.02,true);
    assets.ready=true; local.update(model,&assets,.03,true);
    std::array<float,6> pcm{}; output.mixer.render(pcm); CHECK(pcm[0]>.1F);
    CHECK(local.statistics().movement_submitted==1);
    assets.ready=false; b.scope=2; b.cues[0].scope=2; b.cues[0].serial=2;
    b.cues[0].scheduled_seconds=.04; local.update(b,&assets,.04,true);
    auto life=b; life.count=0; life.movement_epoch=2; local.update(life,&assets,.05,true);
    assets.ready=true; local.update(life,&assets,.06,true);
    CHECK(local.statistics().movement_submitted==1); CHECK(local.statistics().cancelled==1);
}
