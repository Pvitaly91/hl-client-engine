#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/assets/animation_markers.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
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
