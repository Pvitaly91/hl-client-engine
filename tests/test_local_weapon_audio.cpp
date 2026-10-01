#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/goldsrc/sound_assets.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_model_importer.hpp>
#include <hlclient/assets/animation_markers.hpp>
#include <hlclient/assets/wav_importer.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <algorithm>
#include <cmath>
#include <string>
namespace {
namespace api=hlclient::game_api;
namespace g=hlclient::goldsrc;
namespace a=hlclient::audio;
namespace assets=hlclient::assets;
namespace client=hlclient::client;
assets::ModelSequenceEvent marker(int frame,std::string_view name,int number=5004) {
    assets::ModelSequenceEvent e; e.frame=frame; e.event_number=number; e.source_type=7;
    for(char c:name) e.options.push_back(static_cast<std::byte>(c)); return e;
}
api::LocalWeaponModelMetadata model(unsigned id=2) {
    api::LocalWeaponModelMetadata m; m.generation=1; m.model_index=id==2?59:60; m.resource_revision=3;
    m.resource_name=id==2?"models/v_9mmhandgun.mdl":"models/v_crowbar.mdl";
    m.supported_bodies.fill(true); m.selectable_bodies.fill(true);
    m.sequences.resize(10,{30,46,false,{}});
    m.sequences[5].events={marker(0,"weapons/project.wav"),marker(3,"weapons/project.wav"),marker(6,"weapons/project2.wav")};
    m.sequences[6].events=m.sequences[5].events;
    m.sequences[7].events={marker(0,"weapons/project.wav")};
    return m;
}
client::RuntimeClientObservationState state(unsigned ordinal=1,unsigned id=2,int clip=8) {
    client::RuntimeClientObservationState s; s.generation=1; s.publication_revision=ordinal;
    s.lifecycle.life_epoch=1; s.lifecycle.state=client::LocalPlayerLifeState::alive;
    s.client_metadata.generation=1; s.client_metadata.freshness=client::RuntimeObservationFreshness::observed_in_record;
    s.client_metadata.source=client::RuntimeObservationSource{.record_identity=ordinal,.record_ordinal=ordinal};
    s.receiving_client.emplace(); s.receiving_client->viewmodel_index=id==2?59:60; s.receiving_client->health=100;
    s.weapon_hud.active_weapon_id=static_cast<std::uint8_t>(id);
    s.weapon_hud.catalogue.push_back({.id=static_cast<std::uint8_t>(id),.command_name=id==2?"weapon_9mmhandgun":"weapon_crowbar",.primary_ammo_type=1});
    s.weapon_hud.reserve_ammo[1]=68; s.weapon_hud.clips[id]=static_cast<std::int16_t>(clip);
    s.weapon_slots.push_back({.wire_slot=static_cast<std::uint8_t>(id),.clip=clip,.in_reload=false,.next_primary_attack=0.0,.weapon_id=static_cast<std::uint8_t>(id)});
    return s;
}
struct Fixture {
    api::GameClientHost host;
    Fixture(unsigned id=2,int clip=8,bool mute_fire=false)
        : host(hlclient::games::halflife::make_half_life_client_module(mute_fire)) {
        host.reset({1,1,{1}}); host.bind_model(model(id)); host.observe(state(1,id,clip),0);
    }
    api::LocalAudioBatch sample(double t) { (void)host.sample(t); return host.drain_audio(); }
    void command(unsigned n,unsigned buttons,double t) {host.submit({1,n,static_cast<std::uint16_t>(buttons),t},t);}
};
api::LocalWorldSurfaceHit impact_surface(std::string_view name,unsigned index=3) {
    api::LocalWorldSurfaceHit hit;
    hit.source_surface_index=index;hit.source_material_index=index;
    std::copy(name.begin(),name.end(),hit.texture_name.begin());
    return hit;
}
struct Loader : g::SoundAssets {
    g::SoundAssetResult result;
    Loader() {auto pcm=std::make_shared<assets::AudioAsset>(); pcm->sample_rate=48000; pcm->channel_count=1;
        pcm->interleaved_samples.assign(4800,0.125F); result={g::SoundAssetStatus::ready,pcm};}
    g::SoundAssetResult request(std::uint16_t) noexcept override {return result;}
    g::SoundAssetResult request_local(const api::LocalSoundReference&) noexcept override {return result;}
};
struct Sink : a::Output {
    a::OfflineOutput output; std::vector<a::Command> starts;
    bool submit(const a::Command& c) noexcept override {if(c.operation==a::Operation::start) starts.push_back(c); return output.submit(c);}
    void set_listener(a::Listener l) noexcept override {output.set_listener(l);}
};
std::vector<std::byte> read(const std::filesystem::path& p) {
    std::ifstream in(p,std::ios::binary); REQUIRE(in);
    std::vector<char> chars{std::istreambuf_iterator<char>{in},{}}; std::vector<std::byte> bytes(chars.size());
    std::transform(chars.begin(),chars.end(),bytes.begin(),[](char c){return static_cast<std::byte>(c);}); return bytes;
}
}
TEST_CASE("E2 accepted B1 actions own audio, not held sampling or confirmation", "[weapon-audio][game-module]") {
    Fixture f; f.command(1,0,0.02); CHECK(f.sample(0.02).count==0); // capture command has no attack
    f.command(2,1,0.04); auto shot=f.sample(0.04); REQUIRE(shot.count==1);
    CHECK(shot.cues[0].reference.name()=="weapons/pl_gun3.wav"); CHECK(shot.cues[0].scheduled_seconds==0.04);
    for(unsigned i=3;i<16;++i) {f.command(i,1,i*0.02); CHECK(f.sample(i*0.02).count==0);}
    f.host.submit({1,2,1,0.04},0.31); CHECK(f.sample(0.31).count==0);
    f.host.observe(state(2,2,7),0.32); CHECK(f.sample(0.32).count==0);
    f.command(16,1,0.35); auto second=f.sample(0.35); REQUIRE(second.count==1);
    CHECK(second.cues[0].serial!=shot.cues[0].serial); CHECK(second.statistics.actions==2);
    Fixture empty(2,0); empty.command(1,1,0.02); CHECK(empty.sample(0.02).count==0);
}

TEST_CASE("E5 local impact cue uses the E3 mixer as a one-shot world source",
          "[world-impacts][audio]") {
    Loader loader;Sink sink;
    sink.set_listener({{0,0,0},{0,1,0},1,false});
    g::LocalAudio local(sink);
    api::LocalAudioBatch batch;
    batch.session=1;batch.scope=1;batch.count=1;
    auto& cue=batch.cues[0];
    cue.reference.source=api::SoundReferenceSource::pinned_halflife_client_sound_profile;
    constexpr char sample[]="player/pl_step1.wav";
    std::copy_n(sample,sizeof(sample),cue.reference.sample.begin());
    cue.scope=1;cue.serial=1;cue.kind=api::LocalSoundKind::impact;
    cue.channel=api::LocalSoundChannel::automatic;
    cue.world_origin=assets::AssetVector3{10,10,0};
    cue.attenuation=1.0F;cue.scheduled_seconds=0.1;
    local.update(batch,&loader,0.1,true);
    REQUIRE(sink.starts.size()==1U);
    CHECK_FALSE(sink.starts[0].local);
    CHECK(sink.starts[0].origin.y==10);
    CHECK(sink.starts[0].attenuation==1.0F);
    local.update(batch,&loader,0.1,true);
    CHECK(sink.starts.size()==1U);
    std::array<float,16> samples{};
    sink.output.mixer.render(samples);
    CHECK(sink.output.mixer.statistics().presentation_started==1U);
    CHECK(local.statistics().impact_submitted==1U);
    CHECK(samples[1]>samples[0]); // positive-right impact pans right
    Loader absent_loader; absent_loader.result.status=g::SoundAssetStatus::missing;
    Sink absent_sink; g::LocalAudio absent(absent_sink);
    absent.update(batch,&absent_loader,0.1,true);
    CHECK(absent_sink.starts.empty());
    CHECK(absent.statistics().impact_missing==1U);
}
TEST_CASE("E5.1 fire, material hit and casing contact own distinct spatial mixer voices",
          "[weapon-audio][world-impacts][shell-audio]") {
    Loader loader;
    api::LocalAudioBatch batch;
    batch.session=1;batch.scope=1;batch.count=3;
    const std::array names{"weapons/pl_gun3.wav","player/pl_step1.wav","player/pl_shell1.wav"};
    const std::array kinds{api::LocalSoundKind::fire,api::LocalSoundKind::impact,
        api::LocalSoundKind::shell_contact};
    for(std::size_t i=0;i<3;++i) {
        auto& cue=batch.cues[i];
        cue.reference.source=api::SoundReferenceSource::pinned_halflife_client_sound_profile;
        std::copy(names[i],names[i]+std::char_traits<char>::length(names[i])+1,
            cue.reference.sample.begin());
        cue.scope=1;cue.serial=i+1;cue.command_sequence=17;cue.kind=kinds[i];
        cue.channel=i==0 ? api::LocalSoundChannel::weapon : api::LocalSoundChannel::automatic;
        cue.scheduled_seconds=0.1;
        if(i) {cue.world_origin=assets::AssetVector3{10,static_cast<float>(i*10),0};
            cue.attenuation=0.8F;}
    }
    Sink sink;
    sink.set_listener({{0,0,0},{0,1,0},1,false});
    g::LocalAudio local(sink);
    local.update(batch,&loader,0.1,true);
    REQUIRE(sink.starts.size()==3U);
    CHECK(sink.starts[0].voice!=sink.starts[1].voice);
    CHECK(sink.starts[1].voice!=sink.starts[2].voice);
    CHECK(sink.starts[1].origin.y==10.0F);
    CHECK(sink.starts[2].origin.y==20.0F);
    CHECK(local.statistics().impact_submitted==1U);
    CHECK(local.statistics().shell_submitted==1U);
    CHECK(sink.output.mixer.statistics().presentation_started==3U);
    local.update(batch,&loader,0.1,true);
    CHECK(sink.starts.size()==3U);
    for(std::size_t i=1;i<3;++i) {
        Sink isolated;
        isolated.set_listener({{0,0,0},{0,1,0},1,false});
        g::LocalAudio one(isolated);
        api::LocalAudioBatch single=batch;single.count=1;
        single.cues[0]=batch.cues[i];single.cues[0].serial=1;
        one.update(single,&loader,0.1,true);
        REQUIRE(isolated.starts.size()==1U);
        std::array<float,128> pcm{};
        isolated.output.mixer.render(pcm);
        CHECK(std::all_of(pcm.begin(),pcm.end(),[](float value){return std::isfinite(value);}));
        CHECK(std::any_of(pcm.begin(),pcm.end(),[](float value){return std::abs(value)>0.001F;}));
        CHECK(pcm[1]>pcm[0]); // world-space source to listener right
    }
}

TEST_CASE("E6 accepted crowbar world hit mixes swing, strike and concrete contact independently",
          "[crowbar-impacts][audio][composition]") {
    Fixture f(1);
    api::LocalWeaponSubmittedCommand command{1,1,1,0.04};
    command.shot_context=api::LocalWeaponSubmittedCommand::ShotContext{{5,8,8},{-1,0,0}};
    f.host.submit(command,0.04);
    (void)f.host.sample(0.04);
    const auto request=f.host.local_visuals({{5,8,8},{-1,0,0},{0,-1,0},{0,0,1},{},0.04}).world_impact;
    REQUIRE(request);
    REQUIRE(f.host.resolved_world_impact(request->action,
        api::LocalWorldImpactOutcome::static_world_hit,{{0,8,8}},0.05));
    (void)f.host.sample(0.05);
    auto batch=f.host.drain_audio();
    REQUIRE(batch.count==3U);
    CHECK(batch.cues[0].kind==api::LocalSoundKind::swing);
    CHECK(batch.cues[1].kind==api::LocalSoundKind::impact);
    CHECK(batch.cues[2].kind==api::LocalSoundKind::impact);
    CHECK(batch.cues[1].reference.name()!=batch.cues[2].reference.name());
    Loader loader;
    Sink sink;
    sink.set_listener({{0,8,8},{0,-1,0},1.0F,false});
    g::LocalAudio local(sink);
    local.update(batch,&loader,0.05,true);
    REQUIRE(sink.starts.size()==3U);
    CHECK(sink.starts[0].voice!=sink.starts[1].voice);
    CHECK(sink.starts[1].voice!=sink.starts[2].voice);
    CHECK(local.statistics().impact_submitted==2U);
    CHECK(sink.output.mixer.statistics().presentation_started==3U);
    for(std::size_t i=1;i<3;++i) {
        CHECK(sink.starts[i].origin.x==0.0F);
        CHECK(sink.starts[i].origin.y==8.0F);
        CHECK(sink.starts[i].attenuation==Catch::Approx(0.8F));
    }
    // A/B: isolate the two contact samples so a nonzero swing cannot mask
    // silent hit PCM in this project-owned offline fixture.
    auto impacts=batch;
    impacts.cues[0]=batch.cues[1]; impacts.cues[1]=batch.cues[2]; impacts.count=2U;
    Sink isolated;
    isolated.set_listener({{0,8,8},{0,-1,0},1.0F,false});
    g::LocalAudio isolated_audio(isolated);
    isolated_audio.update(impacts,&loader,0.05,true);
    std::array<float,64U> pcm{};
    isolated.output.mixer.render(pcm);
    CHECK(std::any_of(pcm.begin(),pcm.end(),[](float v){return std::isfinite(v)&&std::abs(v)>1e-5F;}));
    CHECK(isolated.output.mixer.statistics().presentation_started==2U);
    isolated_audio.update(impacts,&loader,0.06,true);
    CHECK(isolated.starts.size()==2U); // duplicate delivery is not a new voice
    loader.result.status=g::SoundAssetStatus::missing;
    Sink missing_sink;g::LocalAudio missing(missing_sink);
    missing.update(impacts,&loader,0.05,true);
    CHECK(missing.statistics().impact_missing==2U);
}
TEST_CASE("E5.1 real Half-Life host composes fire, accepted hit and later shell audio",
          "[e9][weapon-audio][world-impacts][shell-audio][composition]") {
    Fixture f;
    auto m=model();
    m.sequences[3].events.push_back(marker(0,"11",5001));
    f.host.bind_model(m);
    api::LocalWeaponSubmittedCommand command{1,17,1,0.04};
    command.shot_context=api::LocalWeaponSubmittedCommand::ShotContext{{5,8,8},{-1,0,0}};
    f.host.submit(command,0.04);
    (void)f.host.sample(0.04);
    api::LocalVisualContext view;
    view.eye={5,8,8};view.forward={-1,0,0};view.right={0,1,0};view.up={0,0,1};
    view.now_seconds=0.04;
    const auto visual=f.host.local_visuals(view);
    REQUIRE(visual.world_impact);
    REQUIRE(visual.shell);
    f.host.accepted_world_impact(visual.world_impact->action,{0,8,8},0.05);
    f.host.shell_contact({visual.shell->action,1,{1,7,0},105.0F,0.12});
    const auto batch=f.host.drain_audio();
    REQUIRE(batch.count==3U);
    CHECK(batch.cues[0].kind==api::LocalSoundKind::fire);
    CHECK(batch.cues[1].kind==api::LocalSoundKind::impact);
    CHECK(batch.cues[2].kind==api::LocalSoundKind::shell_contact);
    CHECK(batch.cues[1].reference.name()=="player/pl_step1.wav");
    CHECK(batch.cues[2].reference.name()=="player/pl_shell1.wav");
    Loader loader;Sink sink;
    g::ServerAudio remote{sink};remote.reset(1); // session setup precedes local presentation
    sink.set_listener({{5,8,8},{0,1,0},1,false});
    g::LocalAudio local(sink);
    local.update(batch,&loader,0.12,true);
    REQUIRE(sink.starts.size()==3U);
    CHECK(local.statistics().impact_submitted==1U);
    CHECK(local.statistics().shell_submitted==1U);
    CHECK(sink.output.mixer.statistics().presentation_started==3U);
    f.host.shell_contact({visual.shell->action,1,{1,7,0},105.0F,0.12});
    CHECK(f.host.drain_audio().count==0U);
    f.host.set_movement_audio_time_origin(0);
    f.host.configure_movement_materials("C CONCRETE\n");
    api::MovementAudioObservation step;step.generation=1;step.life_epoch=1;step.command_sequence=18;
    step.command_milliseconds=20;step.scheduled_seconds=.14;step.horizontal_speed=step.total_speed=320;
    step.before_mode=step.after_mode=api::MovementAudioMode::ground;step.before_grounded=step.after_grounded=true;
    step.movevars_footsteps=true;step.dry_context=true;step.surface_status=api::MovementSurfaceStatus::found;step.texture_name="CONCRETE";
    f.host.observe_movement_audio(step,false);auto steps=f.host.drain_audio();REQUIRE(steps.count==1);
    local.update(steps,&loader,.14,true);
    remote.present({},{{5,8,8},{0,1,0},1,false},{},7);
    g::CommittedSound received;received.generation=1;received.ordinal=1;received.cursor=40;received.record=91;
    received.opcode=g::RuntimeControlOpcode::svc_sound;received.sound.entity_reference=19;received.sound.channel=3;
    received.sound.sound_reference=5;received.sound.origin={5,18,8};
    remote.consume(received,{});remote.update(&loader,{});
    CHECK(sink.output.mixer.active()==5); // fire, impact, shell, local step, remote event
    CHECK(sink.starts.size()==5); // identical PCM does not imply identical cause
    remote.consume(received,{});local.update(steps,&loader,.14,true);
    CHECK(sink.starts.size()==5);
}

TEST_CASE("E7 Glock and crowbar world hits select table material without changing neutral mixer", "[e7][world-impacts][audio]") {
    struct Case {std::string_view texture,sample;float volume;};
    constexpr std::array cases{
        Case{"CONCRETE","player/pl_step1.wav",.9F},
        Case{"METAL","player/pl_metal1.wav",.9F},
        Case{"WOOD","debris/wood1.wav",.9F},
        Case{"TILE","player/pl_tile1.wav",.8F},
        Case{"GLASS","debris/glass1.wav",.8F},
        Case{"UNKNOWN","player/pl_step1.wav",.9F}
    };
    for(const auto& c:cases) {
        Fixture f{2,8,true};
        f.host.configure_movement_materials("C CONCRETE\nM METAL\nW WOOD\nT TILE\nY GLASS\n");
        api::LocalWeaponSubmittedCommand command{1,1,1,0.04};
        command.shot_context=api::LocalWeaponSubmittedCommand::ShotContext{{5,8,8},{-1,0,0}};
        f.host.submit(command,0.04);(void)f.host.sample(0.04);
        const auto visual=f.host.local_visuals({{5,8,8},{-1,0,0},{0,-1,0},{0,0,1},{},0.04});
        REQUIRE(visual.world_impact);
        f.host.accepted_world_impact(visual.world_impact->action,{0,8,8},0.05,
            impact_surface(c.texture));
        f.host.accepted_world_impact(visual.world_impact->action,{0,8,8},0.05,
            impact_surface(c.texture));
        const auto batch=f.host.drain_audio();
        REQUIRE(batch.count==1U); // muted fire; duplicate impact cannot replay
        CHECK(batch.cues[0].reference.name()==c.sample);
        CHECK(batch.cues[0].volume==Catch::Approx(c.volume));
        REQUIRE(batch.cues[0].world_origin);
        CHECK(batch.cues[0].world_origin->x==0.0F);
        Loader loader;Sink sink;
        sink.set_listener({{0,8,8},{0,1,0},1,false});
        g::LocalAudio local(sink);local.update(batch,&loader,0.05,true);
        CHECK(sink.starts.size()==1U);
        std::array<float,32> pcm{};sink.output.mixer.render(pcm);
        CHECK(std::any_of(pcm.begin(),pcm.end(),[](float x){return std::abs(x)>1e-5F;}));
    }
    for(const auto& c:std::array{cases[0],cases[1],cases[2]}) {
        Fixture f{1};
        f.host.configure_movement_materials("C CONCRETE\nM METAL\nW WOOD\n");
        api::LocalWeaponSubmittedCommand command{1,1,1,0.04};
        command.shot_context=api::LocalWeaponSubmittedCommand::ShotContext{{5,8,8},{-1,0,0}};
        f.host.submit(command,0.04);(void)f.host.sample(0.04);
        const auto visual=f.host.local_visuals({{5,8,8},{-1,0,0},{0,-1,0},{0,0,1},{},0.04});
        REQUIRE(visual.world_impact);
        REQUIRE(f.host.resolved_world_impact(visual.world_impact->action,
            api::LocalWorldImpactOutcome::static_world_hit,{{0,8,8}},0.05,
            impact_surface(c.texture)));
        const auto batch=f.host.drain_audio();
        REQUIRE(batch.count==3U); // swing + strike + material
        CHECK(batch.cues[2].reference.name()==c.sample);
        CHECK(batch.cues[2].volume==Catch::Approx(c.volume));
        CHECK(batch.cues[1].volume==Catch::Approx(
            c.texture=="CONCRETE" ? .6F : c.texture=="METAL" ? .3F : .2F));
    }
}
TEST_CASE("E7.1 accepted Glock BSP hit keeps material and optional ricochet as separate voices",
          "[e7][ricochet][world-impacts][audio]") {
    Fixture f{2,8,true}; // mute only the fire sample for isolated listening proof
    f.host.configure_movement_materials("M METAL\n");
    const auto profile=f.host.local_impact_assets();
    REQUIRE(profile);
    REQUIRE(profile->supplemental_sound_count==5U);
    CHECK(profile->supplemental_sounds[3].name()=="weapons/ric4.wav");
    api::LocalWeaponSubmittedCommand command{1,11,1,0.04};
    command.shot_context=api::LocalWeaponSubmittedCommand::ShotContext{{5,8,8},{-1,0,0}};
    f.host.submit(command,0.04);
    (void)f.host.sample(0.04);
    const auto visual=f.host.local_visuals({{5,8,8},{-1,0,0},{0,-1,0},{0,0,1},{},0.04});
    REQUIRE(visual.world_impact);
    f.host.accepted_world_impact(visual.world_impact->action,{0,8,8},0.05,
        impact_surface("METAL"));
    f.host.accepted_world_impact(visual.world_impact->action,{0,8,8},0.05,
        impact_surface("METAL"));
    const auto batch=f.host.drain_audio();
    REQUIRE(batch.count==2U);
    CHECK(batch.cues[0].reference.name()=="player/pl_metal1.wav");
    CHECK(batch.cues[1].reference.name()=="weapons/ric4.wav");
    CHECK(batch.cues[0].marker_ordinal==0U);
    CHECK(batch.cues[1].marker_ordinal==1U);
    CHECK(batch.cues[0].command_sequence==11U);
    CHECK(batch.cues[1].command_sequence==11U);
    REQUIRE(batch.cues[0].world_origin);
    REQUIRE(batch.cues[1].world_origin);
    CHECK(batch.cues[0].world_origin->x==batch.cues[1].world_origin->x);
    CHECK(batch.cues[0].world_origin->y==batch.cues[1].world_origin->y);
    CHECK(batch.cues[0].world_origin->z==batch.cues[1].world_origin->z);
    CHECK(f.host.drain_audio().count==0U);
    Loader loader;Sink sink;
    sink.set_listener({{0,8,8},{0,1,0},1,false});
    g::LocalAudio local(sink);
    local.update(batch,&loader,0.05,true);
    REQUIRE(sink.starts.size()==2U);
    CHECK(sink.starts[0].voice!=sink.starts[1].voice);
    CHECK(local.statistics().impact_submitted==2U);
    std::array<float,64U> pcm{};
    sink.output.mixer.render(pcm);
    CHECK(std::any_of(pcm.begin(),pcm.end(),[](float x){return std::isfinite(x)&&std::abs(x)>1e-5F;}));
    local.update(batch,&loader,0.06,true);
    CHECK(sink.starts.size()==2U);
    struct MissingRic final : Loader {
        g::SoundAssetResult request_local(const api::LocalSoundReference& r) noexcept override {
            return r.name().starts_with("weapons/ric")
                ? g::SoundAssetResult{g::SoundAssetStatus::missing,{}} : result;
        }
    } missing_ric;
    Sink partial_sink;g::LocalAudio partial(partial_sink);
    partial.update(batch,&missing_ric,0.05,true);
    CHECK(partial_sink.starts.size()==1U);
    CHECK(partial.statistics().impact_submitted==1U);
    CHECK(partial.statistics().impact_missing==1U);
}
TEST_CASE("Opt-in manual diagnostic mutes only the Glock fire voice, not hit or shell",
          "[weapon-audio][world-impacts][shell-audio][audio-diagnostic]") {
    Fixture f{2,8,true};
    auto m=model();
    m.sequences[3].events.push_back(marker(0,"11",5001));
    f.host.bind_model(m);
    api::LocalWeaponSubmittedCommand command{1,17,1,0.04};
    command.shot_context=api::LocalWeaponSubmittedCommand::ShotContext{{5,8,8},{-1,0,0}};
    f.host.submit(command,0.04);
    (void)f.host.sample(0.04);
    api::LocalVisualContext view;
    view.eye={5,8,8};view.forward={-1,0,0};view.right={0,1,0};view.up={0,0,1};
    view.now_seconds=0.04;
    const auto visual=f.host.local_visuals(view);
    REQUIRE(visual.world_impact);
    REQUIRE(visual.shell);
    f.host.accepted_world_impact(visual.world_impact->action,{0,8,8},0.05);
    f.host.shell_contact({visual.shell->action,1,{1,7,0},105.0F,0.12});
    const auto batch=f.host.drain_audio();
    REQUIRE(batch.count==2U);
    CHECK(batch.statistics.fire==1U); // gameplay action was still accepted
    CHECK(batch.cues[0].kind==api::LocalSoundKind::impact);
    CHECK(batch.cues[1].kind==api::LocalSoundKind::shell_contact);
    CHECK(batch.cues[0].command_sequence==17U);
    CHECK(batch.cues[1].command_sequence==17U);
    Loader loader;Sink sink;
    sink.set_listener({{5,8,8},{0,1,0},1,false});
    g::LocalAudio local(sink);
    local.update(batch,&loader,0.12,true);
    REQUIRE(sink.starts.size()==2U);
    CHECK(local.statistics().impact_submitted==1U);
    CHECK(local.statistics().shell_submitted==1U);
    std::array<float,128> pcm{};
    sink.output.mixer.render(pcm);
    CHECK(std::any_of(pcm.begin(),pcm.end(),[](float v){return std::abs(v)>0.001F;}));
    local.update(batch,&loader,0.12,true);
    CHECK(sink.starts.size()==2U);
}
TEST_CASE("E5.1 pending fire sample does not serialize a ready world hit behind it",
          "[weapon-audio][world-impacts][audio]") {
    struct DelayedFire final : Loader {
        g::SoundAssetResult request_local(const api::LocalSoundReference& r) noexcept override {
            return r.name().starts_with("weapons/")
                ? g::SoundAssetResult{g::SoundAssetStatus::pending,{}}
                : result;
        }
    } loader;
    api::LocalAudioBatch batch;
    batch.session=1;batch.scope=1;batch.count=2;
    for(std::size_t i=0;i<2;++i) {
        auto& cue=batch.cues[i];
        cue.reference.source=api::SoundReferenceSource::pinned_halflife_client_sound_profile;
        const auto name=i==0 ? "weapons/pl_gun3.wav" : "player/pl_step1.wav";
        std::copy(name,name+std::char_traits<char>::length(name)+1,cue.reference.sample.begin());
        cue.scope=1;cue.serial=i+1;cue.scheduled_seconds=0.1;
        cue.kind=i==0 ? api::LocalSoundKind::fire : api::LocalSoundKind::impact;
        if(i) cue.world_origin=assets::AssetVector3{10,0,0};
    }
    Sink sink;g::LocalAudio local(sink);
    local.update(batch,&loader,0.1,true);
    REQUIRE(sink.starts.size()==1U);
    CHECK(sink.starts[0].origin.x==10.0F);
    CHECK(local.statistics().impact_submitted==1U);
    CHECK(local.statistics().resource_pending>=1U);
    batch.count=0;batch.scope=2;
    local.update(batch,&loader,0.12,true);
    CHECK(sink.starts.size()==1U);
}
TEST_CASE("E2 marker identities and due times are independent of render cadence", "[weapon-audio]") {
    std::vector<double> oracle;
    for(int fps:{30,60,144,37}) {
        Fixture f; f.command(1,8192,0.02);
        std::vector<double> due; std::vector<std::uint64_t> ids;
        for(int i=0;i<fps;++i) {
            double t=0.02+static_cast<double>(i)/fps;
            auto batch=f.sample(t);
            for(std::size_t n=0;n<batch.count;++n) {due.push_back(batch.cues[n].scheduled_seconds);ids.push_back(batch.cues[n].serial);}
            CHECK(f.sample(t).count==0);
        }
        REQUIRE(due.size()==3); CHECK(due[0]==Catch::Approx(0.02)); CHECK(due[1]==Catch::Approx(0.12)); CHECK(due[2]==Catch::Approx(0.22));
        if(oracle.empty()) oracle=due; else CHECK(due==oracle);
        CHECK(ids==std::vector<std::uint64_t>{1,2,3});
    }
    Fixture f; f.command(1,8192,0.02); REQUIRE(f.sample(0.02).count==1);
    auto crossed=f.sample(0.24); REQUIRE(crossed.count==2); CHECK(crossed.cues[0].reference.name()=="weapons/project.wav");
    CHECK(crossed.cues[1].reference.name()=="weapons/project2.wav");
    CHECK(f.sample(0.1).count==0); // rewind does not replay
    Fixture irregular; irregular.command(1,8192,0.02); std::vector<double> irregular_due;
    for(double t:{0.02,0.021,0.073,0.201,0.239,0.5}) {
        auto batch=irregular.sample(t);
        for(std::size_t i=0;i<batch.count;++i) irregular_due.push_back(batch.cues[i].scheduled_seconds);
    }
    CHECK(irregular_due==oracle);
}
TEST_CASE("E2 cancellation confirmation correction restart and invalid markers", "[weapon-audio]") {
    Fixture f; f.command(1,8192,0.02); REQUIRE(f.sample(0.02).count==1);
    auto confirmed=state(2); confirmed.weapon_slots[0].in_reload=true;
    f.host.observe(confirmed,0.05); CHECK(f.sample(0.05).count==0);
    confirmed.publication_revision=3; confirmed.weapon_hud.animation_source=client::RuntimeObservationSource{.record_identity=3,.record_ordinal=3};
    confirmed.weapon_hud.animation_sequence=5; confirmed.weapon_hud.animation_body=0;
    f.host.observe(confirmed,0.06); CHECK(f.sample(0.06).count==0); // correction frame 0 not repeated
    CHECK(f.sample(0.17).count==1); // corrected future marker is eligible
    f.host.cancel_uncommitted(); CHECK(f.sample(0.3).count==0);
    Fixture corrected; corrected.command(1,8192,0.02); (void)corrected.sample(0.02);
    REQUIRE(corrected.sample(0.13).count==1);
    auto correction=state(2); correction.weapon_hud.animation_sequence=5; correction.weapon_hud.animation_body=0;
    correction.weapon_hud.animation_source=client::RuntimeObservationSource{.record_identity=2,.record_ordinal=2};
    corrected.host.observe(correction,0.14); const auto correction_report=corrected.sample(0.14);
    CHECK(correction_report.count==0); CHECK(correction_report.statistics.timeline_corrections==1);
    CHECK(correction_report.statistics.duplicates==0); // correction is not a repeated marker
    CHECK(corrected.sample(0.25).count==0); // already consumed ordinal 1, not another clip-in
    CHECK(corrected.sample(0.35).count==1); // previously unconsumed ordinal 2
    Fixture dead; dead.command(1,8192,0.02); (void)dead.sample(0.02);
    auto s=state(2); s.lifecycle.state=client::LocalPlayerLifeState::dead; s.lifecycle.deaths=1; s.receiving_client->health=0;
    dead.host.observe(s,0.05); CHECK(dead.sample(0.24).count==0);
    Fixture switched; switched.command(1,8192,0.02); (void)switched.sample(0.02);
    switched.host.bind_model(model(1)); switched.host.observe(state(2,1),0.05); CHECK(switched.sample(0.24).count==0);
    Fixture invalid; auto m=model(); m.sequences[6].events={marker(0,"../secret.wav"),marker(1,"weapons/ok.wav",5001)};
    invalid.host.bind_model(m); invalid.command(1,8192,0.02); CHECK(invalid.sample(0.1).count==0);
    Fixture item; m=model(); m.sequences[6].events={marker(0,"items/9mmclip2.wav")};
    item.host.bind_model(m); item.command(1,8192,0.02); auto item_cue=item.sample(0.02);
    REQUIRE(item_cue.count==1); CHECK(item_cue.cues[0].reference.name()=="items/9mmclip2.wav");
    Fixture late; late.command(1,8192,0.02); auto dropped=late.sample(0.8); CHECK(dropped.count==0); CHECK(dropped.statistics.late==3);
    Fixture deploy; auto d=state(2); d.receiving_client->weapon_animation=7;
    deploy.host.observe(d,0.01); auto first=deploy.sample(0.01); REQUIRE(first.count==1); CHECK(first.cues[0].kind==api::LocalSoundKind::deploy);
    d=state(3); d.weapon_hud.animation_sequence=7; d.weapon_hud.animation_body=0;
    d.weapon_hud.animation_source=client::RuntimeObservationSource{.record_identity=3,.record_ordinal=3};
    deploy.host.observe(d,0.1); CHECK(deploy.sample(0.1).count==1); CHECK(deploy.sample(0.1).count==0);
}
TEST_CASE("E2 generic marker traversal admits zero and bounds loops and stale catchup", "[weapon-audio][studio]") {
    const std::array events{marker(0,"weapons/a.wav"),marker(3,"weapons/b.wav"),marker(3,"weapons/b.wav")};
    auto zero=assets::animation_markers(events,10,6,true,-1,0); REQUIRE(zero.count==1);
    auto a=assets::animation_markers(events,10,6,true,0,0.3); REQUIRE(a.count==2);
    auto b=assets::animation_markers(events,10,6,true,0.3,0.5); REQUIRE(b.count==1); CHECK(b.values[0].loop==1);
    CHECK(assets::animation_markers(events,10,6,true,0.5,0.5).count==0);
    CHECK(assets::animation_markers(events,10,6,true,0.5,0.1).count==0);
    auto c=assets::animation_markers(events,10,6,true,0.5,1000); CHECK(c.count<=32); CHECK(c.late>0);
}
TEST_CASE("E2 E1 sink plays local swing and independent server impacts with real PCM", "[weapon-audio][audio]") {
    Fixture f(1); f.command(1,1,0.02); auto swing=f.sample(0.02); REQUIRE(swing.count==1);
    Loader loader; Sink sink; sink.set_listener({{}, {0,-1,0},1,false}); g::ServerAudio server(sink); g::LocalAudio local(sink);
    g::CommittedSound hit; hit.generation=1; hit.record=1;hit.ordinal=1;hit.cursor=1;hit.sound.sound_reference=3;hit.sound.channel=3;hit.sound.entity_reference=1;
    hit.opcode=g::RuntimeControlOpcode::svc_sound; hit.sound.volume=255;
    server.consume(hit,{}); server.update(&loader,{});
    local.update(swing,&loader,0.02,true); local.update(swing,&loader,0.02,true);
    REQUIRE(sink.starts.size()==2); CHECK(sink.starts[0].voice!=sink.starts[1].voice);
    CHECK(sink.starts[1].scheduled_seconds==0.02); CHECK(sink.output.mixer.statistics().started==2);
    CHECK(sink.output.mixer.statistics().presentation_started==1);
    std::array<float,64> pcm; sink.output.mixer.render(pcm); CHECK(pcm[0]>0.125F);
    hit.record=2; hit.ordinal=2; hit.sound.entity_reference=2; server.consume(hit,{}); server.update(&loader,{});
    CHECK(sink.starts.size()==3); CHECK(local.statistics().duplicates==1);
    for(auto block:{1U,37U,480U}) {
        a::OfflineOutput output; output.set_listener({{}, {0,-1,0},1,false}); g::LocalAudio playback(output);
        playback.update(swing,&loader,0.02,true); std::vector<float> result(block*2); std::size_t frames=0;
        while(frames<5000) {output.mixer.render(result);frames+=block;}
        CHECK(output.mixer.statistics().started==1); CHECK(output.mixer.active()==0);
    }
}
TEST_CASE("E2 pending loads cancellation mute missing and invalid references fail harmlessly", "[weapon-audio][audio]") {
    Fixture f; f.command(1,1,0.02); auto shot=f.sample(0.02); Loader loader; loader.result.status=g::SoundAssetStatus::pending;
    Sink sink; g::LocalAudio local(sink); local.update(shot,&loader,0.02,true);
    f.host.cancel_uncommitted(); auto cancelled=f.sample(0.03); local.update(cancelled,&loader,0.03,true);
    loader.result.status=g::SoundAssetStatus::ready; local.update(cancelled,&loader,0.04,true); CHECK(sink.starts.empty());
    Sink muted_sink; g::LocalAudio muted(muted_sink); muted.update(shot,&loader,0.02,false);
    muted.update(shot,&loader,0.04,true); CHECK(muted_sink.starts.empty());
    Sink missing_sink; g::LocalAudio missing(missing_sink); loader.result.status=g::SoundAssetStatus::missing;
    missing.update(shot,&loader,0.02,true); CHECK(missing.statistics().missing==1); CHECK(missing_sink.starts.empty());
    Sink unsupported_sink; g::LocalAudio unsupported(unsupported_sink); loader.result.status=g::SoundAssetStatus::unsupported;
    unsupported.update(shot,&loader,0.02,true); CHECK(unsupported.statistics().missing==0); CHECK(unsupported.statistics().limits==1);
    Sink late_sink; g::LocalAudio late(late_sink); loader.result.status=g::SoundAssetStatus::pending; late.update(shot,&loader,0.02,true);
    loader.result.status=g::SoundAssetStatus::ready; shot.count=0; late.update(shot,&loader,1,true); CHECK(late_sink.starts.empty()); CHECK(late.statistics().late==1);
    for(auto name:{"../x.wav","C:/x.wav","sound/sound/x.wav","weapons/a.wav;quit","weapons/../../x.wav","/weapons/a.wav"}) {
        api::LocalSoundReference r; std::copy_n(name,std::char_traits<char>::length(name),r.sample.begin()); CHECK_FALSE(g::valid_local_sound_reference(r));
    }
    a::Playback disabled(false); g::LocalAudio absent(disabled); shot.count=1; absent.update(shot,&loader,0.02,false);
    CHECK(disabled.status()=="disabled"); CHECK(absent.statistics().muted==1);
}
TEST_CASE("E2 opt in original MDL event records and referenced WAV decode read only", "[weapon-audio][local-weapon-audio]") {
    const auto* root=SDL_getenv("HLCLIENT_LOCAL_GAME_ROOT"); if(!root) SKIP("opt-in read-only assets unavailable");
    for(const auto* name:{"v_9mmhandgun.mdl","v_crowbar.mdl"}) {
        const auto path=std::filesystem::path(root)/"valve"/"models"/name;
        auto source=assets::AssetSource::create(std::string{"models/"}+name,read(path)); REQUIRE(source);
        auto imported=g::studio::GoldSrcStudioModelImporter{}.import(*source.source); REQUIRE(imported); REQUIRE(imported.value().skeletal_data);
        std::cout<<"E2 model="<<name<<'\n';
        for(std::size_t seq=0;seq<imported.value().skeletal_data->sequences.size();++seq) {
            const auto& data=imported.value().skeletal_data->sequences[seq];
            std::cout<<"sequence="<<seq<<" label="<<data.label<<" fps="<<data.frames_per_second<<" frames="<<data.frame_count<<" events="<<data.events.size()<<'\n';
            for(const auto& e:data.events) {
                std::string token; for(auto c:e.options) token.push_back(static_cast<char>(c));
                std::cout<<" frame="<<e.frame<<" event="<<e.event_number<<" type="<<e.source_type<<" options="<<token<<'\n';
                if(e.event_number!=5004) continue;
                api::LocalSoundReference r; REQUIRE(token.size()<r.sample.size()); std::copy(token.begin(),token.end(),r.sample.begin());
                REQUIRE(g::valid_local_sound_reference(r));
                auto wav=assets::AssetSource::create("sound/"+token,read(std::filesystem::path(root)/"valve"/"sound"/token)); REQUIRE(wav);
                auto pcm=assets::WavImporter{}.import(*wav.source); REQUIRE(pcm); CHECK_FALSE(pcm.value().interleaved_samples.empty());
            }
        }
    }
    for(const auto* name:{"weapons/pl_gun3.wav","weapons/cbar_miss1.wav",
            "weapons/cbar_hit1.wav","weapons/cbar_hit2.wav",
            "player/pl_step1.wav","player/pl_shell1.wav"}) {
        auto source=assets::AssetSource::create(std::string{"sound/"}+name,read(std::filesystem::path(root)/"valve"/"sound"/name)); REQUIRE(source);
        auto pcm=assets::WavImporter{}.import(*source.source); REQUIRE(pcm); CHECK_FALSE(pcm.value().interleaved_samples.empty());
    }
}
