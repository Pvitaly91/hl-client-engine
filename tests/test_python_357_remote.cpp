#include "python_357_test_fixture.hpp"
#include <hlclient/games/halflife/remote_effects.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/app/remote_effect_presentation.hpp>
#include "collision_brush_test_fixture.hpp"
#include "world_render_test_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
namespace f=hlclient::tests::python_fixture;
namespace api=hlclient::game_api;
namespace assets=hlclient::assets;
namespace g=hlclient::goldsrc;
namespace a=hlclient::audio;
using Catch::Approx;
namespace {
struct Samples final:g::SoundAssets {
    std::shared_ptr<const assets::AudioAsset> pcm;
    Samples() {auto data=std::make_shared<assets::AudioAsset>();data->sample_rate=48000;
        data->channel_count=1;data->interleaved_samples.assign(24000,.125F);pcm=std::move(data);}
    g::SoundAssetResult request(std::uint16_t) noexcept override {return {g::SoundAssetStatus::ready,pcm};}
    g::SoundAssetResult request_local(const api::LocalSoundReference&) noexcept override {return {g::SoundAssetStatus::ready,pcm};}
};
struct Sink:a::Output {
    a::OfflineOutput output;std::vector<a::Command> starts;
    bool submit(const a::Command& cmd) noexcept override {
        if(cmd.operation==a::Operation::start) starts.push_back(cmd);return output.submit(cmd);
    }
    void set_listener(a::Listener l) noexcept override {output.set_listener(l);}
};
}
TEST_CASE("E18 remote Python assigned event produces spatial fire flash light one impact no shell tracer or local recoil", "[e18][remote-effects][weapon-audio]") {
    f::Module m;(void)m.host.drain_remote_audio();auto event=f::event(3);event.spread_x=.006F;event.spread_y=-.004F;
    m.host.committed_scripted_event(event);const auto batch=m.host.drain_remote_effects(.03);
    REQUIRE(batch.count==1);const auto& shot=batch.effects[0];CHECK(shot.action.weapon_id==3);
    REQUIRE(shot.flash);REQUIRE(shot.light);REQUIRE(shot.impact);CHECK_FALSE(shot.shell);
    CHECK(shot.extra_shell_count==0);CHECK(shot.extra_impact_count==0);CHECK_FALSE(shot.impact->tracer);
    CHECK(shot.impact->maximum_distance_units==8192);CHECK_FALSE(shot.impact->suppress_surface_effects);
    CHECK(shot.light->radius_units==128);CHECK(shot.light->intensity==1);
    CHECK(shot.impact->direction.y/shot.impact->direction.x==Approx(-.006F).margin(1e-6));
    CHECK(shot.impact->direction.z/shot.impact->direction.x==Approx(.004F));
    const auto audio=m.host.drain_remote_audio();REQUIRE(audio.count==1);REQUIRE(audio.cues[0].world_origin);
    CHECK(f::same(*audio.cues[0].world_origin,event.origin));CHECK(audio.cues[0].channel==api::LocalSoundChannel::automatic);
    Samples samples;Sink sink;sink.set_listener({{90,80,52},{1,0,0},1,false});
    g::LocalAudio playback(sink,true);playback.update(audio,&samples,.03,true);REQUIRE(sink.starts.size()==1);
    std::array<float,512> pcm{};sink.output.mixer.render(pcm);
    CHECK(std::all_of(pcm.begin(),pcm.end(),[](float v){return std::isfinite(v);}));
    CHECK(std::any_of(pcm.begin(),pcm.end(),[](float v){return v!=0;}));
    const auto empty=m.host.sample(.03);CHECK(empty.primary_fire_starts==0);CHECK(empty.local_punch_pitch_degrees==0);
    m.host.committed_scripted_event(event);CHECK(m.host.drain_remote_effects(.04).count==0);
    CHECK(m.host.drain_remote_audio().count==0);
    m.host.remote_world_impact(shot.action,{0,80,80},.04);
    const auto impact=m.host.drain_remote_audio();REQUIRE(impact.count>=1);
    CHECK(impact.cues[0].reference.name()=="player/pl_step1.wav");
    // Isolated impact PCM, not a gunshot masking a missing material voice.
    Sink impact_sink;impact_sink.set_listener({{10,80,80},{-1,0,0},1,false});g::LocalAudio impact_playback(impact_sink,true);
    impact_playback.update(impact,&samples,.04,true);REQUIRE(impact_sink.starts.size()==impact.count);
    impact_sink.output.mixer.render(pcm);CHECK(std::any_of(pcm.begin(),pcm.end(),[](float v){return v!=0;}));
    m.host.remote_world_impact(shot.action,{0,80,80},.05);CHECK(m.host.drain_remote_audio().count==0);
}
TEST_CASE("E18 p357 socket validates its own Cylinder geometry and freezes merged pose without changing server sequence", "[e18][remote-effects][socket-profile]") {
    assets::SkeletalModelAssetData model;
    constexpr std::array names{"Bip01","Bip01 Pelvis","Bip01 Spine","Bip01 Spine1","Bip01 Spine2",
        "Bip01 Spine3","Bip01 Neck","Bip01 R Arm","Bip01 R Arm1","Bip01 R Arm2","Bip01 R Hand","Cylinder01"};
    for(std::size_t i=0;i<names.size();++i) {assets::ModelBone b;b.name=names[i];b.parent_index=static_cast<int>(i)-1;model.bones.push_back(b);}
    model.bones[11].default_translation={5.854939F,.808541F,2.847965F};
    model.bones[11].default_rotation_radians={-1.564202F,-3.113941F,-1.474388F};
    model.submodels.resize(1);assets::ModelSkinnedVertex v;v.position_bone_index=11;
    v.source_position={-1.06F,-4.49F,-3.67F};model.submodels[0].vertices.push_back(v);
    v.source_position={1.05F,1.56F,8.06F};model.submodels[0].vertices.push_back(v);
    f::Module m;auto plan=m.host.remote_weapon_socket_plan({71,{3,4},"models/p_357.mdl",model});
    CHECK(plan.source==api::RemoteSocketSource::validated_pmodel_socket);CHECK(plan.semantic_key==3);
    CHECK(plan.muzzle.local_origin.z==Approx(8.10F));CHECK(plan.muzzle.forward.z==1);
    auto bad=model;bad.bones[11].name="Line02";
    CHECK(m.host.remote_weapon_socket_plan({71,{3,5},"models/p_357.mdl",bad}).source==api::RemoteSocketSource::unavailable);
    api::RemoteWeaponPoseSample pose;pose.entity=3;pose.generation=pose.map_generation=1;
    pose.entity_lifetime=4;pose.model_slot=71;pose.current_model_slot=71;
    pose.resource_id=71;pose.source_record=pose.source_ordinal=1;pose.frame_identity=7;
    pose.observed_at_seconds=pose.presented_at_seconds=.02;pose.server_sequence=19;
    pose.semantic_known=pose.semantic_match=true;pose.semantic_key=3;pose.source=plan.source;
    pose.muzzle={{70,81,83},{-1,0,0},{0,1,0},{0,0,1}};pose.shell=pose.muzzle;
    m.host.remote_weapon_poses(std::span{&pose,1});auto event=f::event(3);event.weapon_model_index=71;
    m.host.committed_scripted_event(event);const auto batch=m.host.drain_remote_effects(.03);REQUIRE(batch.count==1);
    CHECK(batch.effects[0].pose.status==api::RemotePoseResolution::resolved);
    CHECK(batch.effects[0].pose.server_sequence==19);CHECK(f::same(batch.effects[0].muzzle_origin,pose.muzzle.origin));
    CHECK_FALSE(batch.effects[0].shell);CHECK_FALSE(batch.effects[0].impact->tracer);
    pose.muzzle.origin={170,181,183};m.host.remote_weapon_poses(std::span{&pose,1});
    CHECK(f::same(batch.effects[0].muzzle_origin,{70,81,83}));
    m.host.remote_slot_boundary(3);m.host.committed_scripted_event(f::event(3,3,.04));
    CHECK(m.host.drain_remote_effects(.04).count==0);
}
TEST_CASE("E18 unknown local echo wrong generation and expired Python events are inert", "[e18][remote-effects][lifecycle]") {
    f::Module m;auto event=f::event();m.host.committed_scripted_event(event);
    CHECK(m.host.drain_remote_effects(.03).count==0);
    event=f::event(3,3);event.event_index=31;m.host.committed_scripted_event(event);
    CHECK(m.host.drain_remote_effects(.03).count==0);
    event=f::event(3,4);event.generation=9;m.host.committed_scripted_event(event);
    CHECK(m.host.drain_remote_effects(.03).count==0);
    event=f::event(3,5);m.host.committed_scripted_event(event);CHECK(m.host.drain_remote_effects(.5).count==0);
}
