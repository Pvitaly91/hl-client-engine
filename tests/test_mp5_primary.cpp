#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/games/halflife/firearm_profile.hpp>
#include <hlclient/games/halflife/local_weapon_presentation.hpp>
#include <hlclient/games/halflife/remote_effects.hpp>
#include <hlclient/games/halflife/weapon_visuals.hpp>
#include <hlclient/games/halflife/inventory.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <set>
#include <vector>
namespace {
namespace api=hlclient::game_api;
namespace hl=hlclient::games::halflife;
namespace c=hlclient::client;
namespace g=hlclient::goldsrc;
using Catch::Approx;
api::LocalWeaponModelMetadata model() {
    api::LocalWeaponModelMetadata m;m.generation=1;m.model_index=21;m.resource_revision=1;
    m.resource_name="models/v_9mmAR.mdl";m.supported_bodies.fill(true);
    constexpr std::array<std::string_view,8> labels{"longidle","idle1","grenade","reload","deploy","shoot","shoot","shoot"};
    for(const auto label:labels) m.sequences.push_back({10,7,false,{},std::string(label)});
    m.sequences[0].looping=m.sequences[1].looping=true;
    m.sequences[3].fps=30;m.sequences[3].frame_count=47;
    for(unsigned i=5;i<8;++i) m.sequences[i].events.push_back({0,5001,0,{std::byte{'1'},std::byte{'1'}}});
    return m;
}
c::RuntimeClientObservationState state(int clip=50,std::size_t ordinal=1,std::string name="weapon_9mmAR") {
    c::RuntimeClientObservationState s;s.generation=1;s.publication_revision=ordinal;
    s.client_metadata.generation=1;s.client_metadata.freshness=c::RuntimeObservationFreshness::observed_in_record;
    s.client_metadata.source=c::RuntimeObservationSource{.record_identity=ordinal,.record_ordinal=ordinal};
    s.receiving_client.emplace();s.receiving_client->viewmodel_index=21;s.receiving_client->health=100;
    s.receiving_client->owned_weapon_bits=1U<<4;s.receiving_client->weapon_animation=0;
    s.weapon_hud.active_weapon_id=4;s.weapon_hud.clips[4]=static_cast<std::int16_t>(clip);
    s.weapon_hud.reserve_ammo[1]=100;
    s.weapon_hud.catalogue.push_back({.id=4,.command_name=std::move(name),.primary_ammo_type=1,
        .slot=2,.source={.record_identity=ordinal,.record_ordinal=ordinal}});
    s.weapon_slots.push_back({.wire_slot=4,.clip=clip,.in_reload=false,.next_primary_attack=0.0,.weapon_id=4});
    return s;
}
api::LocalWeaponSubmittedCommand command(unsigned seq,double t,bool attack=true) {
    api::LocalWeaponSubmittedCommand result{1,seq,static_cast<std::uint16_t>(attack ? g::kReferenceGoldSrcButtonAttack : 0),t};
    result.shot_context=api::LocalWeaponSubmittedCommand::ShotContext{{0,0,28},{1,0,0}};return result;
}
api::ScriptedEventBinding binding(unsigned index,std::string_view name) {
    api::ScriptedEventBinding b;b.index=static_cast<std::uint16_t>(index);std::copy(name.begin(),name.end(),b.name.begin());return b;
}
api::CommittedScriptedEvent event(unsigned record,unsigned entity=2,unsigned index=71) {
    api::CommittedScriptedEvent e;e.generation=1;e.record=record;e.record_ordinal=record;e.emitter_entity=entity;
    e.event_index=static_cast<std::uint16_t>(index);e.received_at_seconds=1;e.origin={100,0,0};
    e.spread_x=.1F;e.spread_y=.05F;return e;
}
struct Loader final:g::SoundAssets {
    g::SoundAssetResult request(std::uint16_t) noexcept override{return {};}
    g::SoundAssetResult request_local(const api::LocalSoundReference&) noexcept override {
        auto a=std::make_shared<hlclient::assets::AudioAsset>();a->sample_rate=48000;a->channel_count=1;
        a->interleaved_samples.assign(24000,.125F);return {g::SoundAssetStatus::ready,a};
    }
};
}
TEST_CASE("E15 MP5 requires server catalogue and imported compatible sequence profile", "[e15][weapon-presentation]") {
    hl::LocalWeaponPresentationController controller;controller.bind_model(model());
    auto s=state();controller.observe(s,0);
    REQUIRE(hl::build_weapon_selection_request(s,4));CHECK(hl::select_owned_weapon_group(s,3,{})==4);
    CHECK(hl::cycle_owned_weapon(s,{},1)==4);
    controller.submit(command(1,.02),.02);auto p=controller.sample(.02);
    REQUIRE(p.identity);REQUIRE(p.visual);CHECK(p.visual->sequence>=5);CHECK(p.visual->sequence<=7);
    CHECK(p.visual->body==2);CHECK(p.local_punch_pitch_degrees>=-2);CHECK(p.local_punch_pitch_degrees<=2);
    CHECK(s.weapon_slots[0].clip==50);
    const auto restart=p.visual->restart_identity;
    controller.observe(state(49,2),.04);p=controller.sample(.04);
    CHECK(p.status==api::LocalWeaponActionStatus::server_confirmed);CHECK(p.visual->restart_identity==restart);
    controller.submit(command(1,.02),.04);CHECK(controller.sample(.04).primary_fire_starts==1);
    for(const auto name:{"weapon_mp5","weapon_9mmAR"}) {
        hl::LocalWeaponPresentationController positive;positive.bind_model(model());positive.observe(state(50,1,name),0);
        positive.submit(command(1,.02),.02);CHECK(positive.sample(.02).primary_fire_starts==1);
    }
    for(const auto name:{"weapon_other","weapon_9mmar"}) {
        hl::LocalWeaponPresentationController negative;negative.bind_model(model());negative.observe(state(50,1,name),0);
        negative.submit(command(1,.02),.02);CHECK(negative.sample(.02).primary_fire_starts==0);
    }
    auto incompatible=model();incompatible.sequences[5].label="unrelated";
    controller.bind_model(incompatible);controller.submit(command(2,.2),.2);
    CHECK(controller.sample(.2).primary_fire_starts==1);
}
TEST_CASE("E15 MP5 command cadence is identical across render FPS and does not author ammo", "[e15][weapon-presentation]") {
    std::vector<unsigned> baseline;
    for(const auto fps:{30,60,144,240}) {
        hl::LocalWeaponPresentationController controller;controller.bind_model(model());controller.observe(state(),0);
        std::vector<unsigned> shots;unsigned cmd=0;double render=0;
        for(unsigned tick=1;tick<=30;++tick) {
            const auto t=tick*.02;auto before=controller.sample(t).primary_fire_starts;
            controller.submit(command(++cmd,t),t);
            auto p=controller.sample(t);
            if(p.primary_fire_starts!=before) {REQUIRE(p.identity);shots.push_back(p.identity->command_sequence);}
            // Same fresh canonical timeline, independent of render sampling.
            if(tick%5==1) controller.observe(state(50-static_cast<int>(shots.size()),tick+1),t);
            while(render+1.0/fps<t+.02) {render+=1.0/fps;if(render>=t) (void)controller.sample(render);}
        }
        CHECK(shots==std::vector<unsigned>{1,6,11,16,21,26});
        if(baseline.empty()) baseline=shots;else CHECK(shots==baseline);
        controller.submit(command(++cmd,.64,false),.64);
        CHECK(controller.sample(.7).primary_fire_starts==6);
    }
}
TEST_CASE("E15 automatic pending burst is bounded and batched server clip confirms same actions", "[e15]") {
    hl::LocalWeaponPresentationController controller;controller.bind_model(model());controller.observe(state(3),0);
    for(unsigned tick=1;tick<=16;++tick) {const auto t=tick*.02;controller.submit(command(tick,t),t);(void)controller.sample(t);}
    CHECK(controller.sample(.32).primary_fire_starts==3);
    controller.observe(state(0,2),.34);auto p=controller.sample(.34);
    CHECK(p.primary_fire_confirmed==3);CHECK(p.actions_confirmed==3);
    controller.submit(command(17,.36),.36);CHECK(controller.sample(.36).primary_fire_starts==3);
    auto empty=state(0,3);empty.receiving_client->weapon_animation=0;controller.observe(empty,.8);
    auto reload=command(18,.82,false);reload.buttons=g::kReferenceGoldSrcButtonReload;
    controller.submit(reload,.82);p=controller.sample(.82);REQUIRE(p.visual);CHECK(p.visual->sequence==3);
    CHECK(p.reload_starts==1);CHECK(empty.weapon_slots[0].clip==0);
    auto loaded=state(50,4);loaded.weapon_hud.reserve_ammo[1]=50;controller.observe(loaded,1.4);
    CHECK(controller.sample(1.4).reload_confirmed==1);
}
TEST_CASE("E15 completed fire retains late acknowledgement without replay effects", "[e15]") {
    hl::LocalWeaponPresentationController controller;controller.bind_model(model());controller.observe(state(),0);
    controller.submit(command(1,.02),.02);CHECK(controller.sample(.02).primary_fire_starts==1);
    CHECK(controller.sample(.63).animation_completed);
    controller.observe(state(49,2),.65);
    const auto confirmed=controller.sample(.65);
    CHECK(confirmed.actions_confirmed==1);CHECK(confirmed.primary_fire_starts==1);
    controller.submit(command(1,.02),.66);CHECK(controller.sample(.66).primary_fire_starts==1);
}
TEST_CASE("MP5 server automatic reload repeats an unchanged role through normal viewmodel and audio paths",
          "[e15][mp5-auto-reload][weapon-presentation][local-audio][game-api]") {
    auto imported=model();
    imported.selectable_bodies.fill(true);
    // Project-owned marker fixture: actual MP5 reload timing, no installed asset dependency.
    std::vector<std::byte> sample_name;
    for(const auto c:std::string_view{"weapons/e15_fixture_reload.wav"}) sample_name.push_back(static_cast<std::byte>(c));
    imported.sequences[3].events={{5,5004,0,sample_name},{24,5004,0,sample_name}};
    api::GameClientHost host{hl::make_half_life_client_module()};host.reset({1,1,1});host.bind_model(imported);
    auto initial=state(1);initial.receiving_client->weapon_animation=3;
    host.observe(initial,0);(void)host.sample(1.6);(void)host.drain_audio();
    std::uint64_t previous_restart=0;
    for(unsigned cycle=0;cycle<2;++cycle) {
        CAPTURE(cycle);const double t=1.62+cycle*2.0;const auto record=2+cycle*4;
        host.submit(command(1+cycle*2,t),t);const auto shot=host.sample(t);
        REQUIRE(shot.identity);CHECK(shot.identity->kind==api::LocalWeaponAction::primary_fire);
        CHECK(host.drain_audio().count==1);
        api::LocalVisualContext context;context.now_seconds=t;context.eye={0,0,28};
        context.forward={1,0,0};context.right={0,-1,0};(void)host.local_visuals(context);
        auto empty=state(0,record);empty.receiving_client->weapon_animation=3;
        host.observe(empty,t+.02);host.submit(command(2+cycle*2,t+.04,false),t+.04);
        CHECK(host.sample(t+.04).primary_fire_confirmed==cycle+1);
        // No R command or new weaponanim/service event: only the fresh server reload edge.
        auto reload=state(0,record+1);reload.receiving_client->weapon_animation=3;
        reload.weapon_slots[0].in_reload=true;reload.receiving_client->next_weapon_attack=1.5;
        host.observe(reload,t+.14);const auto started=host.sample(t+.14);REQUIRE(started.visual);
        CHECK(started.visual->sequence==3);CHECK(started.visual->body==0);
        CHECK(started.visual->source==api::LocalWeaponAnimationSource::confirmed_weapon_state);
        CHECK_FALSE(started.identity);CHECK(started.reload_starts==0);
        CHECK(started.primary_fire_starts==cycle+1);CHECK(started.visual->restart_identity!=previous_restart);
        previous_restart=started.visual->restart_identity;
        CHECK(host.hud(reload,t+.14).clip==0);CHECK(reload.weapon_slots[0].clip==0);
        CHECK(host.drain_audio().count==0);
        const auto advanced=host.sample(t+.34);REQUIRE(advanced.visual);
        const auto viewmodel=host.viewmodel(reload,imported,t+.34,advanced.visual);
        CHECK(viewmodel.status==api::ViewmodelStatus::ready);CHECK(viewmodel.sequence==3);
        CHECK(viewmodel.body==0);CHECK(viewmodel.frame_coordinate==Approx(6));
        const auto audio=host.drain_audio();REQUIRE(audio.count==1);
        CHECK(audio.cues[0].kind==api::LocalSoundKind::reload);CHECK(audio.cues[0].sequence==3);
        CHECK(audio.cues[0].command_sequence==0);
        hlclient::audio::OfflineOutput output;g::LocalAudio mixer{output};Loader loader;
        mixer.update(audio,&loader,t+.34,true);std::array<float,128> pcm{};output.mixer.render(pcm);
        CHECK(std::any_of(pcm.begin(),pcm.end(),[](float value){return std::isfinite(value)&&value!=0;}));
        context.now_seconds=t+.34;const auto visual=host.local_visuals(context);
        CHECK_FALSE(visual.flash);CHECK_FALSE(visual.shell);CHECK_FALSE(visual.world_impact);
        host.observe(reload,t+.35);host.submit(command(2+cycle*2,t+.04,false),t+.35);
        CHECK(host.sample(t+.35).visual->restart_identity==previous_restart);CHECK(host.drain_audio().count==0);
        auto repeated=reload;repeated.publication_revision=record+2;
        repeated.client_metadata.source=c::RuntimeObservationSource{.record_identity=record+2,.record_ordinal=record+2};
        host.observe(repeated,t+.36);CHECK(host.sample(t+.36).visual->restart_identity==previous_restart);
        // Matching exact confirmation upgrades provenance without replaying reload markers.
        repeated.weapon_hud.animation_source=*repeated.client_metadata.source;
        repeated.weapon_hud.animation_sequence=3;repeated.weapon_hud.animation_body=0;
        host.observe(repeated,t+.37);const auto exact=host.sample(t+.37);REQUIRE(exact.visual);
        CHECK(exact.visual->source==api::LocalWeaponAnimationSource::service_event);
        CHECK(exact.visual->restart_identity==previous_restart);CHECK(host.drain_audio().count==0);
        (void)host.sample(t+.96);const auto second=host.drain_audio();REQUIRE(second.count==1);
        CHECK(second.cues[0].kind==api::LocalSoundKind::reload);CHECK(second.cues[0].marker_ordinal==1);
        (void)host.sample(t+.97);CHECK(host.drain_audio().count==0);
        auto loaded=state(1,record+3);loaded.receiving_client->weapon_animation=3;
        loaded.weapon_hud.reserve_ammo[1]=static_cast<std::uint8_t>(99-cycle);
        host.observe(loaded,t+1.7);const auto idle=host.sample(t+1.7);REQUIRE(idle.visual);
        CHECK(idle.visual->sequence==0);CHECK(host.hud(loaded,t+1.7).clip==1);
    }
}
TEST_CASE("MP5 automatic reload needs fresh server evidence, not empty ammo or held attack",
          "[e15][mp5-auto-reload][weapon-presentation][bounds]") {
    for(unsigned variant=0;variant<8;++variant) {
        CAPTURE(variant);hl::LocalWeaponPresentationController controller;controller.bind_model(model());
        auto initial=state(0);initial.receiving_client->weapon_animation=3;
        if(variant==7)initial.weapon_hud.reserve_ammo[1]=0;
        controller.observe(initial,0);(void)controller.sample(1.6);(void)controller.drain_audio();
        controller.submit(command(1,1.62),1.62);controller.submit(command(2,1.64,false),1.64);
        const auto before=controller.sample(1.64);CHECK(before.reload_starts==0);CHECK(before.primary_fire_starts==0);
        auto candidate=state(0,2);candidate.receiving_client->weapon_animation=3;candidate.weapon_slots[0].in_reload=true;
        if(variant==0)candidate.client_metadata.freshness=c::RuntimeObservationFreshness::retained;
        if(variant==1)candidate.client_metadata.freshness=c::RuntimeObservationFreshness::unavailable;
        if(variant==2)candidate.weapon_slots[0].in_reload.reset();
        if(variant==3)candidate.weapon_slots.clear();
        if(variant==4){candidate.weapon_slots[0].clip=50;candidate.weapon_hud.clips[4]=50;}
        if(variant==5){candidate.publication_revision=1;candidate.client_metadata.source=initial.client_metadata.source;}
        if(variant==6)candidate.receiving_client->health=0;
        if(variant==7){candidate.weapon_slots[0].in_reload=false;candidate.weapon_hud.reserve_ammo[1]=0;}
        controller.observe(candidate,1.7);const auto result=controller.sample(1.7);
        CHECK((!result.visual || result.visual->sequence!=3));CHECK(result.reload_starts==0);
        CHECK(result.primary_fire_starts==0);CHECK(controller.drain_audio().count==0);
    }
}
TEST_CASE("MP5 automatic reload observation preserves manual R timeline and confirmation",
          "[e15][mp5-auto-reload][weapon-presentation]") {
    for(const int clip:{0,17}) {
        CAPTURE(clip);hl::LocalWeaponPresentationController controller;controller.bind_model(model());
        controller.observe(state(clip),0);auto reload=command(1,.02,false);
        reload.buttons=g::kReferenceGoldSrcButtonReload;controller.submit(reload,.02);
        const auto pending=controller.sample(.02);REQUIRE(pending.visual);REQUIRE(pending.identity);
        auto confirmed=state(clip,2);confirmed.weapon_slots[0].in_reload=true;
        confirmed.receiving_client->weapon_animation=3;confirmed.receiving_client->next_weapon_attack=1.5;
        controller.observe(confirmed,.04);const auto result=controller.sample(.2);REQUIRE(result.visual);
        CHECK(result.reload_starts==1);CHECK(result.reload_confirmed==1);CHECK(result.identity==pending.identity);
        CHECK(result.visual->restart_identity==pending.visual->restart_identity);
        CHECK(result.visual->started_at_seconds==pending.visual->started_at_seconds);
        CHECK(result.visual->sequence==3);CHECK(result.visual->body==0);CHECK(result.frame_coordinate==Approx(5.4));
        controller.observe(confirmed,.21);controller.submit(reload,.21);controller.submit(command(2,.22),.22);
        const auto repeated=controller.sample(.22);CHECK(repeated.reload_confirmed==1);CHECK(repeated.primary_fire_starts==0);
        CHECK(repeated.visual->restart_identity==pending.visual->restart_identity);
    }
}
TEST_CASE("E15 first pickup binds before command and publishes first muzzle effects", "[e15]") {
    api::GameClientHost host{hl::make_half_life_client_module()};host.reset({1,1,1});
    auto selected=state();host.bind_model(model());host.observe(selected,0);
    host.submit(command(1,.02),.02);REQUIRE(host.sample(.02).identity);
    api::LocalVisualContext context;context.now_seconds=.02;context.eye={128,64,300};
    context.forward={1,0,0};context.right={0,-1,0};
    const auto frame=host.local_visuals(context);REQUIRE(frame.flash);REQUIRE(frame.world_impact);
    REQUIRE(frame.world_impact->tracer);CHECK(frame.world_impact->tracer->follow_local_muzzle);
    CHECK_FALSE(host.local_visuals(context).world_impact);
}
TEST_CASE("E15 local production module emits MP5 sound shell flash and alternating tracer impact intents", "[e15][weapon-visuals][local-audio]") {
    api::GameClientHost host{hl::make_half_life_client_module()};host.reset({1,1,1});host.bind_model(model());host.observe(state(),0);
    auto ready=host.drain_audio();CHECK(ready.prepare_count>=2);
    hlclient::audio::OfflineOutput output;g::LocalAudio audio{output};Loader loader;
    std::set<std::string> sounds;
    for(unsigned shot=0;shot<6;++shot) {
        const auto t=.02+shot*.1;
        host.submit(command(shot+1,t),t);auto p=host.sample(t);REQUIRE(p.identity);
        api::LocalVisualContext context;context.now_seconds=t;context.eye={0,0,28};context.forward={1,0,0};context.right={0,-1,0};
        auto v=host.local_visuals(context);REQUIRE(v.flash);REQUIRE(v.light);REQUIRE(v.shell);REQUIRE(v.world_impact);
        CHECK(v.shell->model_name.data()==std::string("models/shell.mdl"));CHECK(v.shell->expires_at_seconds-t==Approx(2.5));
        CHECK(v.world_impact->tracer.has_value()==(shot%2==0));CHECK(v.world_impact->suppress_surface_effects==(shot%2==0));
        CHECK(v.world_impact->particles.has_value()==(shot%2!=0));
        if(v.world_impact->tracer) {
            CHECK(api::LocalWorldImpactRequest::Tracer{}.width_units==Approx(.65));
            CHECK(v.world_impact->tracer->width_units==Approx(.65));
            CHECK(v.world_impact->tracer->start_offset_units>=0);
            CHECK(v.world_impact->tracer->start_offset_units<=16);
            CHECK(v.world_impact->tracer->lifetime_seconds==Approx(.1014));
            CHECK(v.world_impact->tracer->travel_speed_units_per_second==12000);
            CHECK(v.world_impact->tracer->segment_length_units==48);
            CHECK(v.world_impact->tracer->head_taper_fraction==Approx(.90));
            CHECK(v.world_impact->tracer->taper_end_diameter_fraction==Approx(.20));
            CHECK(v.world_impact->tracer->geometry==api::WorldLineGeometry::solid_3d);
            CHECK(v.world_impact->tracer->head_glow.radius_units==Approx(1.25));
            CHECK(v.world_impact->tracer->head_glow.light_radius_units==48);
            CHECK(v.world_impact->tracer->head_glow.light_intensity==Approx(.35));
        } else {
            REQUIRE(v.world_impact->particles);const auto& sparks=*v.world_impact->particles;
            CHECK(sparks.count==16);CHECK(sparks.minimum_speed_units==42);CHECK(sparks.maximum_speed_units==78);
            CHECK(sparks.spread_scale==Approx(1.2));CHECK(sparks.emissive);
            CHECK(sparks.contact_glow.radius_units==2);CHECK(sparks.contact_glow.light_radius_units==64);
            CHECK(sparks.contact_glow.light_intensity==Approx(.55));CHECK(sparks.contact_glow_seconds==Approx(.06));
        }
        auto cues=host.drain_audio();REQUIRE(cues.count==1);sounds.emplace(cues.cues[0].reference.name());
        CHECK(cues.cues[0].kind==api::LocalSoundKind::fire);CHECK(cues.cues[0].channel==api::LocalSoundChannel::automatic);
        audio.update(cues,&loader,t,true);
        std::array<float,128> pcm{};output.mixer.render(pcm);CHECK(std::any_of(pcm.begin(),pcm.end(),[](float x){return x!=0 && std::isfinite(x);}));
        (void)host.sample(t);CHECK(host.drain_audio().count==0);CHECK_FALSE(host.local_visuals(context).shell);
        host.observe(state(49-static_cast<int>(shot),shot+2),t+.01);
    }
    CHECK(sounds==std::set<std::string>{"weapons/hks1.wav","weapons/hks2.wav"});
    CHECK(audio.statistics().submitted==6);
}
TEST_CASE("E15 remote MP5 binding spread independent emitters echo and separate secondary launch", "[e15][remote-effects]") {
    for(const auto receiving:{1U,2U}) {
        api::GameClientHost host{hl::make_half_life_client_module()};host.reset({1,1,receiving});host.set_movement_audio_time_origin(0);
        const std::array bindings{binding(71,"events/mp5.sc"),binding(900,"events/mp52.sc"),binding(13,"events/glock1.sc")};
        host.configure_scripted_events(bindings);CHECK(host.drain_remote_audio().prepare_count==7);
        const auto shooter=receiving==1 ? 2U : 1U;
        for(unsigned i=0;i<6;++i) {
            auto e=event(i+1,shooter);host.committed_scripted_event(e);auto batch=host.drain_remote_effects(1);
            REQUIRE(batch.count==1);const auto& effect=batch.effects[0];REQUIRE(effect.impact);REQUIRE(effect.shell);
            CHECK(effect.action.weapon_id==4);CHECK(effect.impact->tracer.has_value()==(i%2==0));
            if(effect.impact->tracer) {
                CHECK(effect.impact->tracer->width_units==Approx(1.30));
                CHECK(effect.impact->tracer->start_offset_units>=0);
                CHECK(effect.impact->tracer->start_offset_units<=16);
                CHECK(effect.impact->tracer->lifetime_seconds==Approx(.1014));
                CHECK(effect.impact->tracer->travel_speed_units_per_second==12000);
                CHECK(effect.impact->tracer->segment_length_units==48);
                CHECK(effect.impact->tracer->head_taper_fraction==Approx(.90));
                CHECK(effect.impact->tracer->taper_end_diameter_fraction==Approx(.20));
                CHECK(effect.impact->tracer->geometry==api::WorldLineGeometry::solid_3d);
                CHECK_FALSE(effect.impact->tracer->follow_local_muzzle);
                CHECK(effect.impact->tracer->head_glow.radius_units==Approx(1.25));
                CHECK(effect.impact->tracer->head_glow.light_radius_units==48);
                CHECK(effect.impact->tracer->head_glow.light_intensity==Approx(.35));
            } else {
                REQUIRE(effect.impact->particles);const auto& sparks=*effect.impact->particles;
                CHECK(sparks.count==16);CHECK(sparks.minimum_speed_units==42);CHECK(sparks.maximum_speed_units==78);
                CHECK(sparks.spread_scale==Approx(1.2));CHECK(sparks.emissive);
                CHECK(sparks.contact_glow.radius_units==2);CHECK(sparks.contact_glow.light_radius_units==64);
                CHECK(sparks.contact_glow.light_intensity==Approx(.55));CHECK(sparks.contact_glow_seconds==Approx(.06));
            }
            CHECK(effect.impact->direction.y/effect.impact->direction.x==Approx(-.1));
            CHECK(effect.impact->direction.z/effect.impact->direction.x==Approx(.05));
            auto cues=host.drain_remote_audio();REQUIRE(cues.count==1);REQUIRE(cues.cues[0].world_origin);
            CHECK(cues.cues[0].world_origin->x==100);CHECK(cues.cues[0].reference.name()==hl::mp5_sound(effect.action));
            host.committed_scripted_event(e);CHECK(host.drain_remote_effects(1).count==0);CHECK(host.drain_remote_audio().count==0);
        }
        host.committed_scripted_event(event(20,3));auto independent=host.drain_remote_effects(1);
        REQUIRE(independent.count==1);CHECK(independent.effects[0].impact->tracer.has_value());
        host.committed_scripted_event(event(21,shooter,900));auto secondary=host.drain_remote_effects(1);
        CHECK(secondary.count==0);CHECK(secondary.statistics.secondary_launch==1);
        const auto independent_audio=host.drain_remote_audio();REQUIRE(independent_audio.count==2);
        CHECK(independent_audio.cues[0].kind==api::LocalSoundKind::fire);
        CHECK(independent_audio.cues[1].kind==api::LocalSoundKind::secondary_launch);
        host.committed_scripted_event(event(22,receiving));CHECK(host.drain_remote_effects(1).statistics.local_echo==1);
        CHECK(host.drain_remote_audio().count==0);
        host.remote_slot_boundary(shooter);host.committed_scripted_event(event(23,shooter));CHECK(host.drain_remote_effects(1).count==0);
        host.reset({2,2,receiving});host.configure_scripted_events(bindings);auto fresh=event(1,shooter);fresh.generation=2;
        host.committed_scripted_event(fresh);auto reset=host.drain_remote_effects(1);REQUIRE(reset.count==1);CHECK(reset.effects[0].impact->tracer);
        host.teardown();CHECK(host.drain_remote_effects(1).count==0);
    }
}
TEST_CASE("E15 inspected MP5 socket profile is distinct and revision checked", "[e15][remote-effects]") {
    hlclient::assets::SkeletalModelAssetData asset;
    const std::array<std::string_view,12> names{"Bip01","Bip01 Pelvis","Bip01 Spine","Bip01 Spine1",
        "Bip01 Spine2","Bip01 Spine3","Bip01 Neck","Bip01 R Arm","Bip01 R Arm1","Bip01 R Arm2","Bip01 R Hand","Line02"};
    for(std::size_t i=0;i<names.size();++i) {
        hlclient::assets::ModelBone bone;bone.name=names[i];bone.parent_index=static_cast<int>(i)-1;
        asset.bones.push_back(bone);
    }
    asset.bones[11].default_translation={10.5165F,.478351F,1.89628F};
    asset.bones[11].default_rotation_radians={1.54306F,.080641F,-3.04742F};
    hlclient::assets::ModelSubmodel sub;
    hlclient::assets::ModelSkinnedVertex low,high;
    low.position_bone_index=high.position_bone_index=11;
    low.source_position={-7.61F,-6.11F,-.53F};high.source_position={12.38F,5.2F,1.6F};
    sub.vertices={low,high};asset.submodels.push_back(sub);
    api::GameClientHost host{hl::make_half_life_client_module()};host.reset({1,1,1});
    const auto plan=host.remote_weapon_socket_plan({1,{},"models/p_9mmAR.mdl",asset});
    REQUIRE(plan.semantic_match);CHECK(plan.semantic_key==4);
    CHECK(plan.source==api::RemoteSocketSource::validated_pmodel_socket);
    CHECK(plan.muzzle.bone_index==11);CHECK(plan.muzzle.local_origin.x==Approx(-7.7));
    CHECK(plan.muzzle.local_origin.x!=plan.shell.local_origin.x);
    // Same numeric asset cache identity cannot turn a Glock token into MP5.
    const auto mismatch=host.remote_weapon_socket_plan({1,{},"models/p_9mmhandgun.mdl",asset});
    CHECK(mismatch.source==api::RemoteSocketSource::unavailable);CHECK(mismatch.semantic_key==2);
    asset.bones[11].name="unrelated";
    CHECK(host.remote_weapon_socket_plan({2,{},"models/p_9mmAR.mdl",asset}).source==api::RemoteSocketSource::unavailable);
}
TEST_CASE("E15 normal local and remote MP5 tracer profiles retain width ratio and stable varied starts", "[e15][weapon-visuals][remote-effects]") {
    using Profile=std::array<float,2>; // diameter, forward start distance
    api::GameClientHost local{hl::make_half_life_client_module()};
    const auto local_run=[&] {
        local.reset({1,1,1});local.bind_model(model());local.observe(state(),0);
        CHECK(local.drain_audio().prepare_count>=2);
        std::vector<Profile> profiles;
        for(unsigned shot=0;shot<12;++shot) {
            constexpr auto phase="local";CAPTURE(phase,shot);
            const auto t=.02+shot*.1;
            const auto submitted=command(shot+1,t);
            local.submit(submitted,t);REQUIRE(local.sample(t).identity);
            api::LocalVisualContext context;context.now_seconds=t;context.eye={0,0,28};
            context.forward={1,0,0};context.right={0,-1,0};
            const auto frame=local.local_visuals(context);REQUIRE(frame.world_impact);
            CHECK(frame.world_impact->tracer.has_value()==(shot%2==0));
            if(frame.world_impact->tracer) {
                const auto& tracer=*frame.world_impact->tracer;
                profiles.push_back({tracer.width_units,tracer.start_offset_units});
            }
            CHECK(local.drain_audio().count==1);
            // Repeated command/sample/frame cannot choose a new random start.
            local.submit(submitted,t);(void)local.sample(t);
            CHECK_FALSE(local.local_visuals(context).world_impact);
            CHECK(local.drain_audio().count==0);
            local.observe(state(49-static_cast<int>(shot),shot+2),t+.01);
        }
        return profiles;
    };
    const auto local_first=local_run();CHECK(local_run()==local_first);
    REQUIRE(local_first.size()==6);

    api::GameClientHost remote{hl::make_half_life_client_module()};
    const std::array bindings{binding(71,"events/mp5.sc")};
    const auto remote_run=[&] {
        remote.reset({1,1,1});remote.set_movement_audio_time_origin(0);
        remote.configure_scripted_events(bindings);CHECK(remote.drain_remote_audio().prepare_count==5);
        std::vector<Profile> profiles;
        for(unsigned shot=0;shot<12;++shot) {
            constexpr auto phase="remote";CAPTURE(phase,shot);
            const auto committed=event(shot+1);
            remote.committed_scripted_event(committed);
            const auto batch=remote.drain_remote_effects(1);REQUIRE(batch.count==1);
            REQUIRE(batch.effects[0].impact);
            const auto& impact=*batch.effects[0].impact;
            CHECK(impact.tracer.has_value()==(shot%2==0));
            if(impact.tracer) profiles.push_back({impact.tracer->width_units,impact.tracer->start_offset_units});
            CHECK(remote.drain_remote_audio().count==1);
            remote.committed_scripted_event(committed);
            CHECK(remote.drain_remote_effects(1).count==0);
            CHECK(remote.drain_remote_effects(1).count==0);
            CHECK(remote.drain_remote_audio().count==0);
        }
        return profiles;
    };
    const auto remote_first=remote_run();CHECK(remote_run()==remote_first);
    REQUIRE(remote_first.size()==local_first.size());
    std::set<float> local_offsets,remote_offsets;
    for(std::size_t i=0;i<local_first.size();++i) {
        CHECK(local_first[i][0]==Approx(.65));CHECK(remote_first[i][0]==Approx(1.30));
        CHECK(remote_first[i][0]==Approx(local_first[i][0]*2));
        CHECK(local_first[i][1]>=0);CHECK(local_first[i][1]<=16);
        CHECK(remote_first[i][1]>=0);CHECK(remote_first[i][1]<=16);
        local_offsets.insert(local_first[i][1]);remote_offsets.insert(remote_first[i][1]);
    }
    CHECK(local_offsets.size()>1);CHECK(remote_offsets.size()>1);
    CHECK(local_offsets!=remote_offsets);
}
TEST_CASE("E15 tracer start variation includes session emitter and canonical identities", "[e15][weapon-visuals]") {
    api::LocalWeaponActionIdentity action;action.generation=1;action.command_sequence=7;
    action.canonical_pre_revision=4;
    const auto baseline=hl::mp5_tracer_start_offset(action);
    CHECK(baseline>=0);CHECK(baseline<=16);
    auto other=action;other.generation+=1ULL<<32;
    CHECK(hl::mp5_tracer_start_offset(other)!=baseline);
    other=action;other.emitter_entity=2;
    CHECK(hl::mp5_tracer_start_offset(other)!=baseline);
    other=action;other.canonical_pre_revision+=1ULL<<32;
    CHECK(hl::mp5_tracer_start_offset(other)!=baseline);
    other=action;other.started_at_seconds=1234;
    CHECK(hl::mp5_tracer_start_offset(other)==baseline); // no clock jitter
}
