#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/games/halflife/firearm_profile.hpp>
#include <hlclient/games/halflife/local_weapon_presentation.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/goldsrc/reference_client_move.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <set>

namespace {
namespace api=hlclient::game_api;
namespace hl=hlclient::games::halflife;
namespace c=hlclient::client;
namespace g=hlclient::goldsrc;
using Catch::Approx;
api::LocalWeaponModelMetadata secondary_model() {
    api::LocalWeaponModelMetadata m;m.generation=1;m.model_index=21;m.resource_revision=7;
    m.resource_name="models/v_9mmAR.mdl";m.supported_bodies[0]=m.supported_bodies[2]=true;
    m.selectable_bodies[0]=m.selectable_bodies[2]=true;
    constexpr std::array<std::string_view,8> labels{"longidle","idle1","grenade","reload","deploy","shoot","shoot","shoot"};
    for(const auto label:labels) m.sequences.push_back({10,7,false,{},std::string(label)});
    m.sequences[0].looping=m.sequences[1].looping=true;
    m.sequences[3].frame_count=47;m.sequences[3].fps=30;
    return m;
}
c::RuntimeClientObservationState secondary_state(std::uint8_t grenades=3,std::size_t ordinal=1) {
    c::RuntimeClientObservationState s;s.generation=1;s.publication_revision=ordinal;
    s.client_metadata.generation=1;s.client_metadata.freshness=c::RuntimeObservationFreshness::observed_in_record;
    s.client_metadata.source=c::RuntimeObservationSource{.record_identity=ordinal,.record_ordinal=ordinal};
    s.receiving_client.emplace();s.receiving_client->viewmodel_index=21;s.receiving_client->health=100;
    s.receiving_client->owned_weapon_bits=1U<<4;s.receiving_client->weapon_animation=0;
    s.receiving_client->water_level=0;
    s.weapon_hud.active_weapon_id=4;s.weapon_hud.clips[4]=50;
    s.weapon_hud.reserve_ammo[1]=100;s.weapon_hud.reserve_ammo[3]=grenades;
    s.weapon_hud.catalogue.push_back({.id=4,.command_name="weapon_9mmAR",.primary_ammo_type=1,
        .secondary_ammo_type=3,.slot=2,.source={.record_identity=ordinal,.record_ordinal=ordinal}});
    s.weapon_slots.push_back({.wire_slot=4,.clip=50,.in_reload=false,.next_primary_attack=0.0,
        .weapon_id=4,.next_secondary_attack=0.0,.time_weapon_idle=0.0});
    return s;
}
api::LocalWeaponSubmittedCommand secondary_command(unsigned sequence,double time,
    std::uint16_t buttons=g::kReferenceGoldSrcButtonAttack2) {
    return {1,sequence,buttons,time};
}
api::ScriptedEventBinding launch_binding(std::uint16_t index=900,std::string_view name="events/mp52.sc") {
    api::ScriptedEventBinding binding;binding.index=index;
    std::copy(name.begin(),name.end(),binding.name.begin());return binding;
}
api::CommittedScriptedEvent launch_event(std::uint64_t record=2,std::uint32_t emitter=1,
    double time=.04,std::uint16_t index=900) {
    api::CommittedScriptedEvent event;event.generation=1;event.record=event.record_ordinal=record;
    event.emitter_entity=emitter;event.event_index=index;event.origin={64,16,28};
    event.received_at_seconds=time;return event;
}
void prepare(api::GameClientHost& host,const c::RuntimeClientObservationState& state=secondary_state()) {
    host.reset({1,1,1});host.set_movement_audio_time_origin(0);
    const std::array bindings{launch_binding()};host.configure_scripted_events(bindings);
    host.bind_model(secondary_model());host.observe(state,0);
}
struct LaunchLoader final:g::SoundAssets {
    std::set<std::string> requested;
    g::SoundAssetResult request(std::uint16_t) noexcept override { return {}; }
    g::SoundAssetResult request_local(const api::LocalSoundReference& reference) noexcept override {
        requested.emplace(reference.name());
        auto asset=std::make_shared<hlclient::assets::AudioAsset>();
        asset->sample_rate=48000;asset->channel_count=1;asset->interleaved_samples.assign(4800,.125F);
        return {g::SoundAssetStatus::ready,asset};
    }
};
}

TEST_CASE("E16 normal module secondary emits validated launch pose recoil and isolated audible cue only", "[e16][weapon-presentation][local-audio]") {
    api::GameClientHost host{hl::make_half_life_client_module()};const auto canonical=secondary_state();prepare(host,canonical);
    hlclient::audio::OfflineOutput output;g::LocalAudio playback{output};LaunchLoader assets;
    const auto preload=host.drain_audio();playback.update(preload,&assets,0,true);
    CHECK(assets.requested.contains("weapons/glauncher.wav"));CHECK(assets.requested.contains("weapons/glauncher2.wav"));
    const auto wire_command=secondary_command(1,.02);host.submit(wire_command,.02);
    const auto shot=host.sample(.02);REQUIRE(shot.identity);REQUIRE(shot.visual);
    CHECK(shot.action==api::LocalWeaponAction::secondary_launch);CHECK(shot.visual->sequence==2);CHECK(shot.visual->body==2);
    CHECK(shot.local_punch_pitch_degrees==Approx(-10));CHECK(shot.primary_fire_starts==0);CHECK(shot.secondary_launch_starts==1);
    const auto camera=host.camera(canonical,shot.local_punch_pitch_degrees);
    CHECK(camera.status==api::CameraIntentStatus::ready);CHECK(camera.local_pitch_offset_degrees==Approx(10));
    api::LocalVisualContext context;context.now_seconds=.02;context.eye={0,0,28};context.forward={1,0,0};context.right={0,-1,0};
    const auto visuals=host.local_visuals(context);
    CHECK_FALSE(visuals.flash);CHECK_FALSE(visuals.light);CHECK_FALSE(visuals.shell);CHECK_FALSE(visuals.world_impact);
    CHECK(canonical.weapon_hud.reserve_ammo[3]==3);CHECK(canonical.weapon_slots[0].clip==50);
    CHECK(wire_command.buttons==g::kReferenceGoldSrcButtonAttack2);CHECK_FALSE(wire_command.shot_context);
    const auto cues=host.drain_audio();REQUIRE(cues.count==1);
    CHECK(cues.cues[0].kind==api::LocalSoundKind::secondary_launch);CHECK_FALSE(cues.cues[0].world_origin);
    CHECK(cues.cues[0].reference.name()==hl::mp5_launch_sound(*shot.identity));
    CHECK(cues.cues[0].pitch>=.94F);CHECK(cues.cues[0].pitch<=1.09F);
    CHECK(cues.cues[0].channel==api::LocalSoundChannel::automatic);
    playback.update(cues,&assets,.02,true);std::array<float,256> pcm{};output.mixer.render(pcm);
    CHECK(playback.statistics().submitted==1);
    CHECK(std::any_of(pcm.begin(),pcm.end(),[](float sample){return std::isfinite(sample)&&sample!=0;}));
    host.submit(wire_command,.02);(void)host.sample(.02);CHECK(host.drain_audio().count==0);
    CHECK_FALSE(host.local_visuals(context).world_impact);
}

TEST_CASE("E16 normal viewmodel consumes MP5 primary and secondary provisional poses over retained server idle", "[e16][e15][weapon-presentation][first-person]") {
    for(const auto button:{g::kReferenceGoldSrcButtonAttack,g::kReferenceGoldSrcButtonAttack2}) {
        CAPTURE(button);api::GameClientHost host{hl::make_half_life_client_module()};
        const auto canonical=secondary_state();const auto metadata=secondary_model();prepare(host,canonical);
        (void)host.drain_audio();
        host.submit(secondary_command(1,.02,button),.02);const auto first=host.sample(.02);
        REQUIRE(first.visual);REQUIRE(first.identity);CHECK(canonical.receiving_client->weapon_animation==0U);
        const auto started=host.viewmodel(canonical,metadata,.02,first.visual);
        REQUIRE(started.status==api::ViewmodelStatus::ready);
        CHECK(started.sequence==first.visual->sequence);CHECK(started.body==2U);CHECK(started.frame_coordinate==0.0);
        CHECK(started.sequence!=0U); // The pre-fix production guard incorrectly kept server idle.
        if(button==g::kReferenceGoldSrcButtonAttack2) CHECK(started.sequence==2U);
        else {CHECK(started.sequence>=5U);CHECK(started.sequence<=7U);}
        const auto later=host.sample(.08);
        const auto animated=host.viewmodel(canonical,metadata,.08,later.visual);
        CHECK(animated.sequence==started.sequence);CHECK(animated.body==started.body);
        CHECK(animated.frame_coordinate==Approx(.6));
        CHECK(host.viewmodel(canonical,metadata,.08,later.visual).frame_coordinate==animated.frame_coordinate);
        const auto cue=host.drain_audio();REQUIRE(cue.count==1U);
        CHECK(cue.cues[0].kind==(button==g::kReferenceGoldSrcButtonAttack2 ?
            api::LocalSoundKind::secondary_launch : api::LocalSoundKind::fire));
        auto acknowledged=secondary_state(button==g::kReferenceGoldSrcButtonAttack2 ? 2U : 3U,2);
        if(button==g::kReferenceGoldSrcButtonAttack) {
            acknowledged.weapon_slots[0].clip=49;acknowledged.weapon_hud.clips[4]=49;
        }
        host.observe(acknowledged,.1);const auto confirmed=host.sample(.1);
        REQUIRE(confirmed.visual);CHECK(confirmed.visual->restart_identity==first.visual->restart_identity);
        const auto stable=host.viewmodel(acknowledged,metadata,.1,confirmed.visual);
        CHECK(stable.sequence==started.sequence);CHECK(stable.frame_coordinate==Approx(.8));
        CHECK(host.drain_audio().count==0U);
    }
}

TEST_CASE("E16 viewmodel refuses incompatible MP5 local pose and unselectable body", "[e16][weapon-presentation][first-person]") {
    for(const bool invalid_profile:{false,true}) {
        api::GameClientHost host{hl::make_half_life_client_module()};host.reset({1,1,1});
        auto metadata=secondary_model();
        if(invalid_profile) metadata.sequences[2].label="unrelated";
        else metadata.selectable_bodies[2]=false;
        const api::LocalWeaponVisual invented{2U,2U,1U,.02};
        const auto view=host.viewmodel(secondary_state(),metadata,.08,invented);
        REQUIRE(view.status==api::ViewmodelStatus::ready);
        CHECK(view.sequence==0U);CHECK(view.body==0U); // Retained server pose, never an unvalidated override.
    }
}

TEST_CASE("E16 authoritative grenade ammo and independent secondary cooldown gate held commands", "[e16][weapon-presentation]") {
    api::GameClientHost host{hl::make_half_life_client_module()};prepare(host);
    host.submit(secondary_command(1,.02),.02);const auto first=host.sample(.02);REQUIRE(first.visual);
    const auto restart=first.visual->restart_identity;
    host.observe(secondary_state(2,2),.04);CHECK(host.sample(.04).secondary_launch_confirmed==1);
    CHECK(host.sample(.04).visual->restart_identity==restart);
    for(unsigned tick=3;tick<=50;++tick) {
        const auto time=tick*.02;host.submit(secondary_command(tick,time),time);(void)host.sample(time);
    }
    CHECK(host.sample(1).secondary_launch_starts==1);
    host.submit(secondary_command(51,1.02),1.02);CHECK(host.sample(1.02).secondary_launch_starts==2);
    host.observe(secondary_state(1,3),1.04);CHECK(host.sample(1.04).secondary_launch_confirmed==2);
    host.submit(secondary_command(52,1.1,g::kReferenceGoldSrcButtonAttack),1.1);
    CHECK(host.sample(1.1).primary_fire_starts==0);
    host.submit(secondary_command(53,2.04,g::kReferenceGoldSrcButtonAttack),2.04);
    CHECK(host.sample(2.04).primary_fire_starts==1);
    CHECK(host.sample(2.04).secondary_launch_starts==2);
}

TEST_CASE("E16 eligibility needs server secondary ammo rejects full submersion and validates grenade role", "[e16][weapon-presentation]") {
    for(unsigned variant=0;variant<6;++variant) {
        CAPTURE(variant);auto state=secondary_state();auto model=secondary_model();
        switch(variant) {
        case 0:state.weapon_hud.reserve_ammo[3]=0;break;
        case 1:state.weapon_hud.reserve_ammo[3].reset();break;
        case 2:state.weapon_hud.catalogue[0].secondary_ammo_type=-1;break;
        case 3:state.receiving_client->water_level=3;break;
        case 4:state.weapon_slots[0].in_reload=true;break;
        case 5:model.sequences[2].label="unrelated";break;
        }
        hl::LocalWeaponPresentationController controller;controller.bind_model(model);controller.observe(state,0);
        (void)controller.drain_audio();controller.submit(secondary_command(1,.02),.02);
        CHECK(controller.sample(.02).secondary_launch_starts==0);CHECK(controller.drain_audio().count==0);
    }
    for(const auto water:std::array<std::optional<std::uint32_t>,4>{std::nullopt,0U,1U,2U}) {
        auto state=secondary_state();state.receiving_client->water_level=water;
        state.weapon_slots[0].clip=0;state.weapon_hud.clips[4]=0; // primary clip is not grenade ammo
        hl::LocalWeaponPresentationController controller;controller.bind_model(secondary_model());controller.observe(state,0);
        controller.submit(secondary_command(1,.02),.02);CHECK(controller.sample(.02).secondary_launch_starts==1);
    }
    auto blocked=secondary_state();blocked.weapon_slots[0].next_secondary_attack=.5;
    hl::LocalWeaponPresentationController controller;controller.bind_model(secondary_model());controller.observe(blocked,0);
    controller.submit(secondary_command(1,.02),.02);CHECK(controller.sample(.02).secondary_launch_starts==0);
    controller.submit(secondary_command(2,.52),.52);CHECK(controller.sample(.52).secondary_launch_starts==1);
}

TEST_CASE("E16 both attack bits choose secondary without primary effects and reserve is never locally decremented", "[e16]") {
    api::GameClientHost host{hl::make_half_life_client_module()};auto state=secondary_state(1);prepare(host,state);
    host.submit(secondary_command(1,.02,g::kReferenceGoldSrcButtonAttack2|g::kReferenceGoldSrcButtonAttack),.02);
    auto shot=host.sample(.02);CHECK(shot.primary_fire_starts==0);CHECK(shot.secondary_launch_starts==1);
    (void)host.sample(.7); // completed animation retains pending confirmation
    host.submit(secondary_command(2,1.04),1.04);CHECK(host.sample(1.04).secondary_launch_starts==1);
    CHECK(state.weapon_hud.reserve_ammo[3]==1);
    host.observe(secondary_state(0,2),1.06);CHECK(host.sample(1.06).secondary_launch_confirmed==1);
    host.submit(secondary_command(3,2.1),2.1);CHECK(host.sample(2.1).secondary_launch_starts==1);
}

TEST_CASE("E16 secondary does not lose an outstanding primary acknowledgement", "[e16][e15]") {
    api::GameClientHost host{hl::make_half_life_client_module()};prepare(host);
    host.submit(secondary_command(1,.02,g::kReferenceGoldSrcButtonAttack),.02);(void)host.sample(.02);
    host.submit(secondary_command(2,.12),.12);auto launched=host.sample(.12);
    CHECK(launched.primary_fire_starts==1);CHECK(launched.secondary_launch_starts==1);
    CHECK(launched.action==api::LocalWeaponAction::secondary_launch);
    auto confirmed=secondary_state(2,2);confirmed.weapon_slots[0].clip=49;confirmed.weapon_hud.clips[4]=49;
    host.observe(confirmed,.14);launched=host.sample(.14);
    CHECK(launched.primary_fire_confirmed==1);CHECK(launched.secondary_launch_confirmed==1);
    CHECK(launched.actions_confirmed==2);
}

TEST_CASE("E16 primary cooldown also gates secondary while zero server timer is retained", "[e16][e15][weapon-presentation]") {
    api::GameClientHost host{hl::make_half_life_client_module()};prepare(host);(void)host.drain_audio();
    host.submit(secondary_command(1,.02,g::kReferenceGoldSrcButtonAttack),.02);
    CHECK(host.sample(.02).primary_fire_starts==1);REQUIRE(host.drain_audio().count==1);
    // No server update follows the provisional primary. The retained zero timer
    // cannot erase the reference primary's 100 ms cross-attack cooldown.
    host.submit(secondary_command(2,.04),.04);CHECK(host.sample(.04).secondary_launch_starts==0);
    host.submit(secondary_command(3,.119),.119);CHECK(host.sample(.119).secondary_launch_starts==0);
    CHECK(host.drain_audio().count==0);
    host.submit(secondary_command(4,.12),.12);CHECK(host.sample(.12).secondary_launch_starts==1);
    const auto launch=host.drain_audio();REQUIRE(launch.count==1);
    CHECK(launch.cues[0].kind==api::LocalSoundKind::secondary_launch);
}

TEST_CASE("E16 assigned mp52 local echo confirms once without pose restart recoil or sound replay", "[e16][remote-effects]") {
    api::GameClientHost host{hl::make_half_life_client_module()};prepare(host);(void)host.drain_audio();
    host.submit(secondary_command(1,.02),.02);const auto first=host.sample(.02);REQUIRE(first.visual);
    REQUIRE(host.drain_audio().count==1);
    const auto event=launch_event();host.committed_scripted_event(event);
    auto confirmed=host.sample(.04);CHECK(confirmed.secondary_launch_confirmed==1);
    CHECK(confirmed.visual->restart_identity==first.visual->restart_identity);
    CHECK(confirmed.local_punch_pitch_degrees>first.local_punch_pitch_degrees);
    CHECK(host.drain_audio().count==0);CHECK(host.drain_remote_effects(.04).count==0);
    CHECK(host.drain_remote_audio().count==0);
    host.committed_scripted_event(event);(void)host.sample(.04);
    CHECK(host.drain_audio().count==0);CHECK(host.sample(.04).secondary_launch_confirmed==1);
}

TEST_CASE("E16 late launch confirmation retains finished animation no repeated side effects", "[e16]") {
    for(const bool event_confirmation:{false,true}) {
        api::GameClientHost host{hl::make_half_life_client_module()};prepare(host);(void)host.drain_audio();
        host.submit(secondary_command(1,.02),.02);(void)host.sample(.02);REQUIRE(host.drain_audio().count==1);
        const auto finished=host.sample(.7);CHECK(finished.animation_completed);CHECK_FALSE(finished.identity);REQUIRE(finished.visual);
        if(event_confirmation) host.committed_scripted_event(launch_event(2,1,.75));
        else host.observe(secondary_state(2,2),.75);
        const auto confirmed=host.sample(.75);CHECK(confirmed.secondary_launch_confirmed==1);
        CHECK(confirmed.secondary_launch_starts==1);CHECK(confirmed.visual->restart_identity==finished.visual->restart_identity);
        CHECK(host.drain_audio().count==0);CHECK(confirmed.local_punch_pitch_degrees>-10);
        CHECK(confirmed.local_punch_pitch_degrees<=0);
        CHECK(host.sample(1.1).local_punch_pitch_degrees==0);
    }
}

TEST_CASE("E16 only exact registered local launch event presents standalone and resets fail closed", "[e16][remote-effects]") {
    api::GameClientHost host{hl::make_half_life_client_module()};prepare(host);(void)host.drain_audio();
    host.committed_scripted_event(launch_event(2,1,.1,901));CHECK(host.sample(.1).secondary_launch_starts==0);
    host.committed_scripted_event(launch_event(3,1,.12));auto shown=host.sample(.12);
    CHECK(shown.secondary_launch_starts==1);CHECK(shown.secondary_launch_confirmed==1);REQUIRE(shown.visual);CHECK(shown.visual->sequence==2);
    CHECK(host.drain_audio().count==1);
    host.committed_scripted_event(launch_event(3,1,.12));CHECK(host.drain_audio().count==0);
    const std::array unrelated{launch_binding(900,"events/unrelated.sc")};host.configure_scripted_events(unrelated);
    host.committed_scripted_event(launch_event(4,1,.14));CHECK(host.sample(.14).secondary_launch_starts==1);
    host.teardown();host.committed_scripted_event(launch_event(5,1,.16));CHECK(host.drain_audio().count==0);
    prepare(host);auto stale=launch_event(6,1,.18);stale.generation=9;host.committed_scripted_event(stale);
    CHECK(host.sample(.18).secondary_launch_starts==0);
}

TEST_CASE("E16 remote mp52 produces independent spatial launch sounds without primary package", "[e16][remote-effects][local-audio]") {
    api::GameClientHost host{hl::make_half_life_client_module()};prepare(host);(void)host.drain_remote_audio();
    hlclient::audio::OfflineOutput output;g::LocalAudio playback{output,true};LaunchLoader loader;
    for(const auto entity:{2U,3U}) {
        auto event=launch_event(entity,entity,.1);event.origin={static_cast<float>(entity*8),0,28};
        host.committed_scripted_event(event);const auto effects=host.drain_remote_effects(.1);
        CHECK(effects.count==0);CHECK(effects.statistics.fire==0);CHECK(effects.statistics.impacts==0);
        CHECK(effects.statistics.shell_contacts==0);CHECK(effects.statistics.secondary_launch==entity-1);
        const auto audio=host.drain_remote_audio();REQUIRE(audio.count==1);const auto& cue=audio.cues[0];
        CHECK(cue.kind==api::LocalSoundKind::secondary_launch);REQUIRE(cue.world_origin);
        CHECK(cue.world_origin->x==Approx(entity*8));CHECK(cue.attenuation==Approx(.8));
        CHECK(cue.channel==api::LocalSoundChannel::automatic);CHECK(cue.pitch>=.94F);CHECK(cue.pitch<=1.09F);
        CHECK((cue.reference.name()=="weapons/glauncher.wav" || cue.reference.name()=="weapons/glauncher2.wav"));
        playback.update(audio,&loader,.1,true);
        host.committed_scripted_event(event);CHECK(host.drain_remote_effects(.1).count==0);CHECK(host.drain_remote_audio().count==0);
    }
    std::array<float,256> pcm{};output.mixer.render(pcm);
    CHECK(playback.statistics().submitted==2);
    CHECK(std::any_of(pcm.begin(),pcm.end(),[](float sample){return std::isfinite(sample)&&sample!=0;}));
    CHECK(host.sample(.1).secondary_launch_starts==0);CHECK(host.sample(.1).local_punch_pitch_degrees==0);
    auto missing=launch_event(7,2,.1);missing.resolution=api::ScriptedEventResolution::missing_arguments;
    host.committed_scripted_event(missing);CHECK(host.drain_remote_effects(.1).count==0);CHECK(host.drain_remote_audio().count==0);
    host.reset({2,2,1});host.committed_scripted_event(launch_event(8,2,.1));CHECK(host.drain_remote_audio().count==0);
}

TEST_CASE("E16 direct controller generation change requires fresh assigned mp52 binding", "[e16][remote-effects][life]") {
    hl::LocalWeaponPresentationController controller;controller.bind_model(secondary_model());
    const std::array bindings{launch_binding()};controller.configure_scripted_events(bindings);
    controller.observe(secondary_state(),0);(void)controller.drain_audio();
    controller.committed_scripted_event(launch_event(2,1,.1),1);
    CHECK(controller.sample(.1).secondary_launch_starts==1);
    auto model=secondary_model();model.generation=2;controller.bind_model(model);
    auto state=secondary_state();state.generation=2;state.client_metadata.generation=2;
    controller.observe(state,.2);(void)controller.drain_audio();
    auto event=launch_event(3,1,.22);event.generation=2;
    controller.committed_scripted_event(event,1);CHECK(controller.sample(.22).secondary_launch_starts==0);
    controller.configure_scripted_events(bindings);event.record=event.record_ordinal=4;
    controller.committed_scripted_event(event,1);CHECK(controller.sample(.22).secondary_launch_starts==1);
    CHECK(controller.drain_audio().count==1);
}

TEST_CASE("E16 secondary launch presentation is scoped to the selected compatible MP5", "[e16][weapon-presentation]") {
    for(const auto weapon:{1U,2U,7U}) {
        CAPTURE(weapon);auto state=secondary_state();auto model=secondary_model();
        state.weapon_hud.active_weapon_id=static_cast<std::uint8_t>(weapon);
        state.weapon_hud.catalogue[0].id=static_cast<std::uint8_t>(weapon);
        state.weapon_hud.catalogue[0].command_name=weapon==1 ? "weapon_crowbar" :
            weapon==2 ? "weapon_9mmhandgun" : "weapon_unknown";
        state.weapon_slots[0].weapon_id=weapon;
        model.resource_name=weapon==1 ? "models/v_crowbar.mdl" :
            weapon==2 ? "models/v_9mmhandgun.mdl" : "models/v_unknown.mdl";
        model.sequences.resize(10,{10,7,false});model.supported_bodies.fill(true);
        hl::LocalWeaponPresentationController controller;controller.bind_model(model);
        const std::array bindings{launch_binding()};controller.configure_scripted_events(bindings);
        controller.observe(state,0);(void)controller.drain_audio();
        controller.submit(secondary_command(1,.02),.02);
        controller.committed_scripted_event(launch_event(2,1,.04),1);
        const auto sample=controller.sample(.04);
        CHECK(sample.secondary_launch_starts==0);CHECK(sample.primary_fire_starts==0);
        CHECK(sample.local_punch_pitch_degrees==0);CHECK(controller.drain_audio().count==0);
    }
}

TEST_CASE("E16 launch cadence and recoil are independent of rendering samples", "[e16][weapon-presentation]") {
    std::vector<std::uint32_t> baseline;
    for(const auto fps:{30,60,144,240}) {
        hl::LocalWeaponPresentationController controller;controller.bind_model(secondary_model());controller.observe(secondary_state(),0);
        std::vector<std::uint32_t> launches;double render=0;unsigned confirmations=0;
        for(unsigned tick=1;tick<=106;++tick) {
            const auto time=tick*.02;const auto before=controller.sample(time).secondary_launch_starts;
            controller.submit(secondary_command(tick,time),time);const auto shown=controller.sample(time);
            if(shown.secondary_launch_starts!=before) {
                REQUIRE(shown.identity);launches.push_back(shown.identity->command_sequence);
                CHECK(shown.local_punch_pitch_degrees==Approx(-10));
                ++confirmations;
                controller.observe(secondary_state(static_cast<std::uint8_t>(3-confirmations),confirmations+1),time);
            }
            while(render+1.0/fps<time+.02) {render+=1.0/fps;if(render>=time) (void)controller.sample(render);}
        }
        CHECK(launches==std::vector<std::uint32_t>{1,51,101});
        if(baseline.empty()) baseline=launches;else CHECK(launches==baseline);
    }
}
