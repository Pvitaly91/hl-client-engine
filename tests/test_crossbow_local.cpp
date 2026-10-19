#include "crossbow_test_fixture.hpp"
#include <hlclient/games/halflife/firearm_profile.hpp>
#include <hlclient/app/temporary_effect_presentation.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <hlclient/app/live_visual_control.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/goldsrc/usercmd_input_adapter.hpp>
#include <hlclient/goldsrc/live_runtime_stage.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
namespace f=hlclient::tests::crossbow_fixture;
namespace api=hlclient::game_api;
namespace hl=hlclient::games::halflife;
namespace g=hlclient::goldsrc;
using Catch::Approx;
TEST_CASE("E19 exact Crossbow catalogue and model roles authorize ordinary fire without local projectile", "[e19][weapon-presentation][game-api]") {
    for(const unsigned clients:{1U,8U}) {
        f::Module m(clients);m.host.submit(f::command(),.02);const auto shot=m.host.sample(.02);
        REQUIRE(shot.identity);REQUIRE(shot.visual);CHECK(shot.identity->weapon_id==6);
        CHECK(shot.visual->sequence==4);CHECK(shot.visual->body==1);CHECK(shot.local_punch_pitch_degrees==Approx(-2));
        CHECK(m.host.hud(m.observed,.02).clip==5);CHECK(m.host.hud(m.observed,.02).primary_reserve==10);
        const auto v=m.host.local_visuals({{}, {}, {}, {}, {}, .02});
        CHECK_FALSE(v.flash);CHECK_FALSE(v.light);CHECK_FALSE(v.shell);CHECK_FALSE(v.world_impact);
        CHECK(v.extra_impact_count==0);CHECK(v.extra_shell_count==0);CHECK(m.host.drain_temporary_effects(.02).count==0);
        const auto pose=m.host.viewmodel(m.observed,f::model(),.04,shot.visual);
        CHECK(pose.status==api::ViewmodelStatus::ready);CHECK(pose.sequence==4);CHECK(pose.body==1);
    }
    for(unsigned bad=0;bad<4;++bad) {
        f::Module m;auto model=f::model();
        if(bad==0)m.observed.weapon_hud.catalogue[0].command_name="weapon_357";
        if(bad==1)model.resource_name="models/v_other.mdl";
        if(bad==2)model.sequences[4].label="reload";
        if(bad==3)model.supported_bodies[1]=false;
        m.host.bind_model(model);m.host.observe(m.observed,0);m.host.submit(f::command(),.02);
        CHECK(m.host.sample(.02).actions_started==0);
    }
}
TEST_CASE("E19 post-shot clip reserve select loaded empty or no new animation", "[e19][weapon-presentation][echo]") {
    for(const auto values:{std::pair{4,10},std::pair{0,10},std::pair{0,0}}) {
        f::Module m;(void)m.host.drain_audio();const auto before=m.host.sample(0);
        auto e=f::event(false,1,2,.03,values.first,values.second);m.host.committed_scripted_event(e);
        const auto shot=m.host.sample(.03);REQUIRE(shot.visual);REQUIRE(shot.identity);
        CHECK(shot.visual->sequence==(values.first ? 4U : values.second ? 6U : before.visual->sequence));
        if(!values.first && !values.second) CHECK(shot.visual->restart_identity==before.visual->restart_identity);
        CHECK(m.host.drain_audio().count==2);
        m.host.committed_scripted_event(e);(void)m.host.sample(.04);CHECK(m.host.drain_audio().count==0);
        CHECK(m.host.sample(.04).actions_started==1);
    }
    for(unsigned bad=0;bad<3;++bad) {
        f::Module m;auto e=f::event();if(bad==0)e.integer_parameter1.reset();
        if(bad==1)e.integer_parameter1=6;if(bad==2)e.integer_parameter2=-1;
        m.host.committed_scripted_event(e);CHECK(m.host.sample(.03).actions_started==0);
    }
}
TEST_CASE("E19 accepted ordinary shot echo preserves restart and server cooldown controls repeat", "[e19][weapon-presentation][cooldown]") {
    f::Module m;m.host.submit(f::command(),.02);const auto shot=m.host.sample(.02);REQUIRE(shot.visual);
    m.host.committed_scripted_event(f::event());(void)m.host.sample(.03);
    m.observed=f::state(4,3);m.observed.weapon_slots[0].next_primary_attack=.95;
    m.host.observe(m.observed,.07);CHECK(m.host.sample(.07).visual->restart_identity==shot.visual->restart_identity);
    m.host.submit(f::command(2,.77),.77);CHECK(m.host.sample(.77).actions_started==1);
    m.observed=f::state(4,4);m.host.observe(m.observed,1.02);
    m.host.submit(f::command(3,1.03),1.03);CHECK(m.host.sample(1.03).actions_started==2);
    CHECK(m.host.hud(m.observed,1.03).clip==4); // never client ammo authority
}
TEST_CASE("E19 exact loaded empty deploy and holster service roles retain server ownership", "[e19][weapon-presentation][lifecycle]") {
    for(const int clip:{5,0}) for(const bool holster:{false,true}) {
        f::Module m;m.observed=f::state(clip,2);
        const auto sequence=static_cast<std::uint8_t>((holster ? 10 : 8)+(clip ? 0 : 1));
        m.observed.receiving_client->weapon_animation=sequence;
        m.observed.weapon_hud.animation_sequence=sequence;m.observed.weapon_hud.animation_body=0;
        m.observed.weapon_hud.animation_source=hlclient::client::RuntimeObservationSource{2,2,0,0,16};
        m.host.observe(m.observed,.03);const auto state=m.host.sample(.03);REQUIRE(state.visual);
        CHECK(state.visual->sequence==sequence);CHECK(state.visual->source==api::LocalWeaponAnimationSource::service_event);
        CHECK(state.actions_started==0);const auto restart=state.visual->restart_identity;
        m.host.observe(m.observed,.04);CHECK(m.host.sample(.04).visual->restart_identity==restart);
        CHECK(m.host.drain_audio().count==0);CHECK(m.host.drain_temporary_effects(.04).count==0);
    }
}
TEST_CASE("E19 underwater ordinary fire allowed while empty reload dead and secondary never fire", "[e19][lifecycle][weapon-presentation]") {
    f::Module water;water.observed.receiving_client->water_level=3;water.host.observe(water.observed,0);
    water.host.submit(f::command(),.02);CHECK(water.host.sample(.02).primary_fire_starts==1);
    for(unsigned blocked=0;blocked<4;++blocked) {
        f::Module m;m.observed=f::state(blocked==0 ? 0 : 5,2);
        if(blocked==1)m.observed.weapon_slots[0].in_reload=true;
        if(blocked==2)m.observed.receiving_client->health=0;
        m.host.observe(m.observed,.001);(void)m.host.drain_audio();
        m.host.submit(f::command(1,.02,blocked==3 ? g::kReferenceGoldSrcButtonAttack2 : g::kReferenceGoldSrcButtonAttack),.02);
        CHECK(m.host.sample(.02).primary_fire_starts==0);CHECK(m.host.drain_audio().count==0);
    }
}
TEST_CASE("E19 zoom FOV20 makes a single static presentation query and fixed bolt with no damage or local explosion", "[e19][composition][temporary-effects]") {
    f::Module m;m.zoom();(void)m.host.drain_audio();m.host.submit(f::command(),.02);
    const auto shot=m.host.sample(.02);REQUIRE(shot.identity);CHECK(shot.local_punch_pitch_degrees==0);
    CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==20);
    const auto frame=m.host.local_visuals({{}, {}, {}, {}, {}, .02});REQUIRE(frame.world_impact);
    CHECK(frame.world_impact->maximum_distance_units==8192);CHECK(frame.world_impact->suppress_decal);
    CHECK_FALSE(frame.world_impact->particles);CHECK_FALSE(frame.world_impact->tracer);
    CHECK_FALSE(frame.flash);CHECK_FALSE(frame.light);CHECK_FALSE(frame.shell);
    const auto hit=hlclient::assets::AssetVector3{0,80,80};
    m.host.accepted_world_impact(frame.world_impact->action,hit,.03);
    const auto batch=m.host.drain_temporary_effects(.03);REQUIRE(batch.count==2);
    REQUIRE(batch.effects[0].model);CHECK(batch.effects[0].model->lifetime_seconds==5);
    CHECK(batch.effects[0].origin.x==10);CHECK(batch.effects[0].model->angles_degrees.y==Approx(180));
    CHECK_FALSE(batch.effects[0].light);CHECK_FALSE(batch.effects[0].sound);CHECK_FALSE(batch.effects[0].sprite);
    REQUIRE(batch.effects[1].particles);CHECK(batch.effects[1].origin.x==0);
    hlclient::app::TemporaryEffectPresentation owner;owner.reset(1);owner.consume(batch,.03);
    REQUIRE(owner.models().size()==1);owner.consume(batch,.03);CHECK(owner.models().size()==1);
    hlclient::renderer::RenderScene scene;owner.present(scene,4.9);REQUIRE(owner.models().size()==1);
    CHECK(owner.models()[0].origin.x==10);CHECK(scene.world_effect_light_count==0);
    m.host.bind_model({});m.host.cancel_uncommitted();CHECK(m.host.drain_temporary_effects(4.9).count==0);
    CHECK(m.host.hud(m.observed,4.9).clip==5);
    m.observed=f::state(5,3);m.observed.receiving_client->health=0;m.host.observe(m.observed,4.9);
    scene={};owner.present(scene,4.9);CHECK(owner.models().size()==1); // published world effect outlives local life
    CHECK(m.host.hud(m.observed,4.9).health==0);
    scene={};owner.present(scene,5.03);CHECK(owner.models().empty());
    m.host.accepted_world_impact(frame.world_impact->action,hit,.04);CHECK(m.host.drain_temporary_effects(.04).count==0);
}
TEST_CASE("E19 confirmed zoom event is immutable once even when listener camera changes", "[e19][weapon-visuals][echo]") {
    f::Module m;auto e=f::event(true);e.angles={30,0,0};m.host.committed_scripted_event(e);
    const auto shot=m.host.sample(.03);REQUIRE(shot.identity);CHECK(shot.local_punch_pitch_degrees==0);
    const auto v=m.host.local_visuals({{}, {0,1,0}, {}, {}, {}, .03});REQUIRE(v.world_impact);
    CHECK(v.world_impact->direction.x==Approx(std::sqrt(.75)).margin(1e-6));
    CHECK(v.world_impact->direction.z==Approx(-.5));
    m.host.committed_scripted_event(e);(void)m.host.sample(.04);
    CHECK_FALSE(m.host.local_visuals({{}, {}, {}, {}, {}, .04}).world_impact);
}
TEST_CASE("E19 late sniper echo refines provisional ordinary presentation without duplicate launch or projectile", "[e19][weapon-visuals][echo]") {
    f::Module m;(void)m.host.drain_audio();m.host.submit(f::command(),.02);
    const auto provisional=m.host.sample(.02);REQUIRE(provisional.visual);
    CHECK_FALSE(m.host.local_visuals({{}, {}, {}, {}, {}, .02}).world_impact);
    CHECK(m.host.drain_audio().count==2);
    auto e=f::event(true);m.host.committed_scripted_event(e);const auto refined=m.host.sample(.03);
    REQUIRE(refined.visual);CHECK(refined.visual->restart_identity==provisional.visual->restart_identity);
    CHECK(refined.local_punch_pitch_degrees==0);CHECK(m.host.drain_audio().count==0);
    const auto frame=m.host.local_visuals({{}, {}, {}, {}, {}, .03});REQUIRE(frame.world_impact);
    CHECK_FALSE(frame.shell);CHECK_FALSE(frame.flash);CHECK_FALSE(frame.world_impact->tracer);
    m.host.committed_scripted_event(e);(void)m.host.sample(.04);
    CHECK_FALSE(m.host.local_visuals({{}, {}, {}, {}, {}, .04}).world_impact);
    CHECK(m.host.drain_audio().count==0);CHECK(m.host.drain_temporary_effects(.04).count==0);
}
TEST_CASE("E19 conventional reload resets zoom retains server ammo and creates no fabricated marker audio", "[e19][reload][camera][weapon-audio]") {
    f::Module m;m.zoom();m.observed=f::state(2,3);m.observed.receiving_client->field_of_view=20;
    m.host.observe(m.observed,.01);(void)m.host.drain_audio();
    m.host.submit(f::command(1,.02,g::kReferenceGoldSrcButtonReload),.02);
    const auto reload=m.host.sample(.02);REQUIRE(reload.visual);CHECK(reload.visual->sequence==7);
    CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==90);
    m.observed=f::state(2,4);m.observed.weapon_slots[0].in_reload=true;m.host.observe(m.observed,.05);
    CHECK(m.host.sample(.05).reload_confirmed==1);(void)m.host.sample(2.4);CHECK(m.host.drain_audio().count==0);
    CHECK(m.host.hud(m.observed,2.4).clip==2);m.observed=f::state(5,5,7);m.host.observe(m.observed,4.52);
    CHECK(m.host.hud(m.observed,4.52).clip==5);CHECK(m.host.hud(m.observed,4.52).primary_reserve==7);
}
TEST_CASE("E19 zoom resets on holster death map and never becomes a secondary attack effect", "[e19][camera][lifecycle]") {
    for(unsigned reason=0;reason<3;++reason) {
        f::Module m;m.zoom();CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==20);
        m.observed.publication_revision=3;
        if(reason==0)m.observed.weapon_hud.active_weapon_id=2;
        if(reason==1)m.observed.receiving_client->health=0;
        if(reason==2){m.host.reset({2,2,1});m.observed.generation=2;}
        m.host.observe(m.observed,.03);CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==90);
    }
    f::Module m;CHECK(m.host.edge_triggered_buttons(m.observed)!=0);
    m.observed.weapon_hud.catalogue[0].command_name="weapon_wrong";CHECK(m.host.edge_triggered_buttons(m.observed)==0);
}
TEST_CASE("E19 confirmed horizontal 20 FOV uses stock reference aspect while RMB hold commits one edge to wire history", "[e19][camera][input-adapter][game-api]") {
    namespace input=hlclient::input;namespace gameplay=hlclient::gameplay_input;
    for(const double aspect:{4.0/3,16.0/9,16.0/10}) {
        f::Module m;m.zoom();input::InputStateTracker tracker;tracker.begin_frame();const auto snapshot=tracker.publish_snapshot();tracker.end_frame();
        const auto bindings=gameplay::GameplayInputBindings::project_default_v1();REQUIRE(bindings);
        const auto intent=gameplay::GameplayInputIntentBuilder{}.build(snapshot,*bindings.bindings,{},.02);REQUIRE(intent);
        hlclient::app::LiveVisualCameraController camera;hlclient::client::ClientWorldState world;
        REQUIRE(camera.update(&m.observed,*intent.intent,world,m.host.camera(m.observed,0),false,{},aspect));
        CHECK(m.host.camera(m.observed,0).field_of_view_reference_aspect_ratio==4./3.);
        CHECK(world.camera().vertical_field_of_view_radians==Approx(2*std::atan(std::tan(10*.01745329251994329577)/(4./3.))));
    }
    f::Module m;const auto mask=m.host.edge_triggered_buttons(m.observed);input::InputStateTracker tracker;tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());tracker.apply_event(input::InputEvent::capture_acquired());
    tracker.apply_event(input::InputEvent::mouse_button_pressed(input::PhysicalMouseButton::right));
    const auto snapshot=tracker.publish_snapshot();tracker.end_frame();const auto bindings=gameplay::GameplayInputBindings::project_default_v1();REQUIRE(bindings);
    const auto intent=gameplay::GameplayInputIntentBuilder{}.build(snapshot,*bindings.bindings,{},.02);REQUIRE(intent);
    const auto camera=hlclient::gameplay_camera::GameplayCameraState::create({});REQUIRE(camera);
    g::GoldSrcUserCmdBuildContext context;context.command_sequence=*g::GoldSrcUserCmdSequence::create(1);context.command_msec=20;
    context.command_sample_duration_nanoseconds=20'000'000;context.reference_button_policy=m.host.movement_policy().button_policy;
    context.edge_triggered_buttons=mask;context.one_shot_buttons=mask;g::LiveVisualButtonLatch latch;latch.observe(mask,true,mask);
    auto first=g::GoldSrcUserCmdInputAdapter{}.build_reference_wire(*intent.intent,*camera.state,context);REQUIRE(first);REQUIRE(first.one_shot_plan);
    CHECK(first.command->buttons==g::kReferenceGoldSrcButtonAttack2);CHECK(latch.pending()==mask);
    REQUIRE(first.one_shot_plan->commit_after_history_insert(context.command_sequence));latch.consume_after_history_insert(first.one_shot_plan->consumes_buttons());
    context.one_shot_buttons=latch.pending();const auto held=g::GoldSrcUserCmdInputAdapter{}.build_reference_wire(*intent.intent,*camera.state,context);REQUIRE(held);
    CHECK(held.command->buttons==0);CHECK(first.command->buttons==2048);
}
TEST_CASE("Crossbow R immediately after a shot starts one reload before primary cooldown expires",
          "[e19][reload-sync][weapon-presentation][reload]") {
    for(const bool acknowledged:{false,true}) {
        CAPTURE(acknowledged);f::Module m;
        m.observed=f::state(4,2);m.host.observe(m.observed,.001);
        m.host.submit(f::command(1,.02),.02);const auto shot=m.host.sample(.02);
        REQUIRE(shot.identity);CHECK(shot.identity->kind==api::LocalWeaponAction::primary_fire);
        (void)m.host.drain_audio();
        if(acknowledged) {
            m.observed=f::state(3,3);m.observed.weapon_slots[0].next_primary_attack=.73;
            m.observed.receiving_client->next_weapon_attack=0;
            m.host.observe(m.observed,.04);
            CHECK(m.host.sample(.04).primary_fire_confirmed==1);
        }
        m.host.submit(f::command(2,.06,g::kReferenceGoldSrcButtonReload),.06);
        const auto reload=m.host.sample(.06);REQUIRE(reload.identity);REQUIRE(reload.visual);
        CHECK(reload.identity->kind==api::LocalWeaponAction::reload);
        CHECK(reload.identity->command_sequence==2);CHECK(reload.reload_starts==1);
        CHECK(reload.visual->sequence==7);CHECK(reload.visual->body==0);
        CHECK(reload.visual->started_at_seconds==Approx(.06));
        // Presentation must not transfer ammunition while predicting the pose.
        CHECK(m.host.hud(m.observed,.06).clip==(acknowledged ? 3 : 4));
        m.host.submit(f::command(3,.08,g::kReferenceGoldSrcButtonReload),.08);
        const auto repeated=m.host.sample(.12);REQUIRE(repeated.visual);
        CHECK(repeated.reload_starts==1);CHECK(repeated.visual->restart_identity==reload.visual->restart_identity);
        CHECK(repeated.frame_coordinate==Approx(1.8));CHECK(m.host.drain_audio().count==0);
    }
}
TEST_CASE("Crossbow held R retries after the player attack gate clears without a new key edge",
          "[e19][reload-sync][weapon-presentation][reload]") {
    f::Module m;m.observed=f::state(2,2);m.observed.receiving_client->next_weapon_attack=.3;
    m.host.observe(m.observed,.001);m.host.submit(f::command(1,.02,g::kReferenceGoldSrcButtonReload),.02);
    CHECK(m.host.sample(.02).reload_starts==0);
    m.observed=f::state(2,3);m.observed.receiving_client->next_weapon_attack=0;
    m.observed.weapon_slots[0].next_primary_attack=.65;m.host.observe(m.observed,.08);
    const auto held=f::command(2,.10,g::kReferenceGoldSrcButtonReload);
    m.host.submit(held,.10);const auto started=m.host.sample(.10);REQUIRE(started.visual);
    CHECK(started.reload_starts==1);CHECK(started.visual->sequence==7);
    CHECK(started.visual->started_at_seconds==Approx(.10));
    m.host.submit(held,.12);m.host.submit(f::command(3,.14,g::kReferenceGoldSrcButtonReload),.14);
    const auto stable=m.host.sample(.20);REQUIRE(stable.visual);
    CHECK(stable.reload_starts==1);CHECK(stable.duplicate_submissions==1);
    CHECK(stable.visual->restart_identity==started.visual->restart_identity);
    CHECK(stable.frame_coordinate==Approx(3));CHECK(m.host.hud(m.observed,.20).clip==2);
}
TEST_CASE("Crossbow server reload occurrences advance unchanged roles and matching confirmations retain their timeline",
          "[e19][reload-sync][weapon-presentation][reload][echo]") {
    f::Module m;m.observed=f::state(0,2);m.observed.receiving_client->weapon_animation=7;
    m.host.observe(m.observed,.001);(void)m.host.sample(4.6);(void)m.host.drain_audio();
    std::uint64_t previous_restart{};
    for(unsigned cycle=0;cycle<2;++cycle) {
        CAPTURE(cycle);const double at=4.7+cycle*5.;const auto record=3+cycle*4;
        auto loading=f::state(0,record);loading.receiving_client->weapon_animation=7;
        loading.receiving_client->next_weapon_attack=4.5;loading.weapon_slots[0].in_reload=true;
        m.host.observe(loading,at);const auto started=m.host.sample(at);REQUIRE(started.visual);
        CHECK(started.visual->sequence==7);CHECK(started.visual->body==0);
        CHECK(started.visual->source==api::LocalWeaponAnimationSource::confirmed_weapon_state);
        CHECK(started.visual->restart_identity!=previous_restart);CHECK_FALSE(started.identity);
        CHECK(started.reload_starts==0);CHECK(started.visual->started_at_seconds==Approx(at));
        previous_restart=started.visual->restart_identity;
        m.host.observe(loading,at+.05); // Exact duplicate state is not a new occurrence.
        loading.publication_revision=record+1;
        loading.client_metadata.source=hlclient::client::RuntimeObservationSource{
            .record_identity=record+1,.record_ordinal=record+1};
        m.host.observe(loading,at+.10);CHECK(m.host.sample(at+.10).visual->restart_identity==previous_restart);
        // A first exact service confirmation and the unchanged client state both
        // acknowledge the existing server reload rather than replaying its start.
        loading.publication_revision=record+2;
        loading.client_metadata.source=hlclient::client::RuntimeObservationSource{
            .record_identity=record+2,.record_ordinal=record+2};
        loading.weapon_hud.animation_source=loading.client_metadata.source;
        loading.weapon_hud.animation_sequence=7;loading.weapon_hud.animation_body=0;
        m.host.observe(loading,at+.20);const auto exact=m.host.sample(at+.20);REQUIRE(exact.visual);
        CHECK(exact.visual->restart_identity==previous_restart);
        CHECK(exact.visual->source==api::LocalWeaponAnimationSource::service_event);
        CHECK(exact.frame_coordinate==Approx(6));
        m.host.observe(loading,at+.25);
        // Another exact message in the same active reload is an echo too.
        loading.client_metadata.source->start_bit_offset=32;
        loading.weapon_hud.animation_source=loading.client_metadata.source;
        m.host.observe(loading,at+.30);
        CHECK(m.host.sample(at+.30).visual->restart_identity==previous_restart);
        const auto advanced=m.host.sample(at+.50);REQUIRE(advanced.visual);
        CHECK(advanced.visual->restart_identity==previous_restart);CHECK(advanced.frame_coordinate==Approx(15));
        CHECK(m.host.hud(loading,at+.50).clip==0);CHECK(m.host.drain_audio().count==0);
        auto loaded=f::state(5,record+3,5);loaded.receiving_client->weapon_animation=7;
        m.host.observe(loaded,at+4.6);const auto idle=m.host.sample(at+4.6);REQUIRE(idle.visual);
        CHECK(idle.visual->sequence==0);CHECK(m.host.hud(loaded,at+4.6).clip==5);
    }
}
TEST_CASE("Crossbow reload occurrence requires a fresh alive partial-magazine weapon slot",
          "[e19][reload-sync][weapon-presentation][reload][bounds]") {
    for(unsigned variant=0;variant<7;++variant) {
        CAPTURE(variant);f::Module m;auto initial=f::state(0,2);
        initial.receiving_client->weapon_animation=7;m.host.observe(initial,.001);(void)m.host.sample(4.6);
        auto candidate=f::state(0,3);candidate.receiving_client->weapon_animation=7;
        candidate.weapon_slots[0].in_reload=true;
        if(variant==0)candidate.client_metadata.freshness=hlclient::client::RuntimeObservationFreshness::retained;
        if(variant==1)candidate.client_metadata.freshness=hlclient::client::RuntimeObservationFreshness::unavailable;
        if(variant==2)candidate.client_metadata.source=initial.client_metadata.source;
        if(variant==3)candidate.weapon_slots.clear();
        if(variant==4)candidate.weapon_slots[0].in_reload.reset();
        if(variant==5)candidate.receiving_client->health=0;
        if(variant==6){candidate.weapon_slots[0].clip=5;candidate.weapon_hud.clips[6]=5;}
        m.host.observe(candidate,4.7);const auto result=m.host.sample(4.7);
        CHECK((!result.visual || result.visual->sequence!=7));CHECK(result.reload_starts==0);
        CHECK(result.primary_fire_starts==0);CHECK(m.host.drain_audio().count==0);
    }
}
TEST_CASE("Crossbow first shot from a full clip can immediately enter a predicted reload",
          "[e19][reload-sync][weapon-presentation][reload]") {
    f::Module m;m.host.submit(f::command(1,.02),.02);const auto shot=m.host.sample(.02);
    REQUIRE(shot.identity);CHECK(shot.status==api::LocalWeaponActionStatus::predicted_pending);
    CHECK(m.host.hud(m.observed,.02).clip==5);
    m.host.submit(f::command(2,.06,g::kReferenceGoldSrcButtonReload),.06);
    const auto reload=m.host.sample(.06);REQUIRE(reload.identity);REQUIRE(reload.visual);
    CHECK(reload.identity->kind==api::LocalWeaponAction::reload);CHECK(reload.visual->sequence==7);
    CHECK(reload.reload_starts==1);CHECK(reload.primary_fire_starts==1);
    CHECK(m.host.hud(m.observed,.06).clip==5); // The presentation reservation is not an ammo write.
}
TEST_CASE("Crossbow reload retains one timeline through late predecessor client service and scripted fire confirmations",
          "[e19][reload-sync][weapon-presentation][reload][echo]") {
    for(const bool acknowledged:{false,true}) for(const bool sniper:{false,true}) {
        CAPTURE(acknowledged,sniper);f::Module m;
        if(sniper)m.zoom();
        m.observed=f::state(4,3);if(sniper)m.observed.receiving_client->field_of_view=20;
        m.host.observe(m.observed,.005);m.host.submit(f::command(1,.02),.02);
        const auto shot=m.host.sample(.02);REQUIRE(shot.identity);(void)m.host.drain_audio();
        if(acknowledged) {
            m.observed=f::state(3,4);m.observed.weapon_slots[0].next_primary_attack=.73;
            m.host.observe(m.observed,.04);CHECK(m.host.sample(.04).primary_fire_confirmed==1);
        }
        m.host.submit(f::command(2,.06,g::kReferenceGoldSrcButtonReload),.06);
        const auto reload=m.host.sample(.06);REQUIRE(reload.identity);REQUIRE(reload.visual);
        REQUIRE(reload.identity->kind==api::LocalWeaponAction::reload);
        const auto stable_reload=[&](double at) {
            const auto shown=m.host.sample(at);REQUIRE(shown.identity);REQUIRE(shown.visual);
            CHECK(shown.identity==reload.identity);CHECK(shown.visual->sequence==7);
            CHECK(shown.visual->body==0);CHECK(shown.visual->restart_identity==reload.visual->restart_identity);
            CHECK(shown.visual->started_at_seconds==reload.visual->started_at_seconds);
            CHECK(shown.frame_coordinate==Approx((at-.06)*30));
            CHECK(shown.primary_fire_starts==1);CHECK(shown.reload_starts==1);
            CHECK(shown.recoil_starts==(sniper ? 0U : 1U));
        };
        auto late=f::state(3,5);late.receiving_client->weapon_animation=4;
        late.weapon_slots[0].next_primary_attack=.69;m.host.observe(late,.08);stable_reload(.08);
        late=f::state(3,6);late.receiving_client->weapon_animation=0;
        m.host.observe(late,.10);stable_reload(.10);
        late=f::state(3,7);late.weapon_hud.animation_source=late.client_metadata.source;
        late.weapon_hud.animation_sequence=4;late.weapon_hud.animation_body=1;
        m.host.observe(late,.12);stable_reload(.12);
        auto echo=f::event(sniper,1,8,.14,3,10);m.host.committed_scripted_event(echo);stable_reload(.14);
        m.host.committed_scripted_event(echo);stable_reload(.15);
        auto loading=f::state(3,9);loading.receiving_client->weapon_animation=7;
        loading.receiving_client->next_weapon_attack=4.4;loading.weapon_slots[0].in_reload=true;
        m.host.observe(loading,.16);stable_reload(.16);
        const auto confirmed=m.host.sample(.16);
        CHECK(confirmed.primary_fire_confirmed==1);CHECK(confirmed.reload_confirmed==1);
        CHECK(confirmed.actions_started==2);CHECK(confirmed.actions_confirmed==2);
        CHECK(confirmed.recoil_confirmed==(sniper ? 0U : 1U));CHECK(confirmed.recoil_rejected==0);
        CHECK(m.host.drain_audio().count==0);CHECK(m.host.hud(loading,.16).clip==3);
    }
}
TEST_CASE("Crossbow sniper predecessor receipts expire without fabricated recoil and pose protection has the original deadline",
          "[e19][reload-sync][weapon-presentation][reload][bounds]") {
    for(const bool acknowledged:{false,true}) {
        CAPTURE(acknowledged);f::Module m;m.zoom();m.observed=f::state(4,3);
        m.observed.receiving_client->field_of_view=20;m.host.observe(m.observed,.005);
        m.host.submit(f::command(1,.02),.02);const auto shot=m.host.sample(.02);
        REQUIRE(shot.identity);CHECK(shot.recoil_starts==0);(void)m.host.drain_audio();
        if(acknowledged) {
            m.observed=f::state(3,4);m.host.observe(m.observed,.04);
            CHECK(m.host.sample(.04).primary_fire_confirmed==1);
        }
        m.host.submit(f::command(2,.06,g::kReferenceGoldSrcButtonReload),.06);
        const auto reload=m.host.sample(.06);REQUIRE(reload.visual);CHECK(reload.visual->sequence==7);
        auto loading=f::state(acknowledged ? 3 : 4,5);loading.receiving_client->weapon_animation=7;
        loading.receiving_client->next_weapon_attack=4.4;loading.weapon_slots[0].in_reload=true;
        m.host.observe(loading,.10);const auto expired=m.host.sample(2.30);REQUIRE(expired.visual);
        CHECK(expired.visual->sequence==7);CHECK(expired.visual->restart_identity==reload.visual->restart_identity);
        CHECK(expired.reload_starts==1);CHECK(expired.reload_confirmed==1);
        CHECK(expired.primary_fire_confirmed==(acknowledged ? 1U : 0U));
        CHECK(expired.actions_rejected==(acknowledged ? 0U : 1U));
        CHECK(expired.recoil_starts==0);CHECK(expired.recoil_confirmed==0);CHECK(expired.recoil_rejected==0);
        // Once the original shot's .02 + 2.25 deadline expires, a genuinely
        // newer authoritative pose must be allowed to replace the reload.
        loading.publication_revision=6;loading.client_metadata.source=hlclient::client::RuntimeObservationSource{
            .record_identity=6,.record_ordinal=6};
        loading.weapon_hud.animation_source=loading.client_metadata.source;
        loading.weapon_hud.animation_sequence=4;loading.weapon_hud.animation_body=1;
        m.host.observe(loading,2.40);const auto corrected=m.host.sample(2.40);REQUIRE(corrected.visual);
        CHECK(corrected.visual->sequence==4);CHECK(corrected.visual->restart_identity!=reload.visual->restart_identity);
        CHECK(corrected.recoil_starts==0);CHECK(corrected.recoil_confirmed==0);CHECK(corrected.recoil_rejected==0);
    }
}
TEST_CASE("Crossbow reload respects eligible secondary priority and retries when the secondary timer blocks it",
          "[e19][reload-sync][weapon-presentation][reload][priority]") {
    for(const bool secondary_ready:{false,true}) {
        CAPTURE(secondary_ready);f::Module m;m.observed=f::state(2,2);
        m.observed.weapon_slots[0].next_secondary_attack=secondary_ready ? 0 : .5;
        m.host.observe(m.observed,.001);
        m.host.submit(f::command(1,.02,g::kReferenceGoldSrcButtonReload|g::kReferenceGoldSrcButtonAttack2),.02);
        const auto result=m.host.sample(.02);CHECK(result.reload_starts==(secondary_ready ? 0U : 1U));
        CHECK(result.primary_fire_starts==0);CHECK(m.host.hud(m.observed,.02).clip==2);
        if(!secondary_ready){REQUIRE(result.visual);CHECK(result.visual->sequence==7);}
    }
}
TEST_CASE("Crossbow server-only reload retains a pending predecessor through split ammo and fire confirmations",
          "[e19][reload-sync][weapon-presentation][reload][echo]") {
    for(const bool sniper:{false,true}) for(const bool clip_first:{false,true}) {
        CAPTURE(sniper,clip_first);f::Module m;if(sniper)m.zoom();
        m.observed=f::state(4,3);if(sniper)m.observed.receiving_client->field_of_view=20;
        m.host.observe(m.observed,.005);m.host.submit(f::command(1,.02),.02);
        REQUIRE(m.host.sample(.02).primary_fire_starts==1);(void)m.host.drain_audio();
        auto loading=f::state(4,4);loading.receiving_client->weapon_animation=7;
        loading.receiving_client->next_weapon_attack=4.5;loading.weapon_slots[0].in_reload=true;
        m.host.observe(loading,.06);const auto reload=m.host.sample(.06);REQUIRE(reload.visual);
        CHECK_FALSE(reload.identity);CHECK(reload.visual->sequence==7);CHECK(reload.visual->body==0);
        CHECK(reload.visual->source==api::LocalWeaponAnimationSource::confirmed_weapon_state);
        CHECK(reload.primary_fire_confirmed==0);CHECK(reload.actions_rejected==0);CHECK(reload.reload_starts==0);
        const auto stable=[&](double at) {
            const auto shown=m.host.sample(at);REQUIRE(shown.visual);CHECK_FALSE(shown.identity);
            CHECK(shown.visual->sequence==7);CHECK(shown.visual->body==0);
            CHECK(shown.visual->restart_identity==reload.visual->restart_identity);
            CHECK(shown.visual->started_at_seconds==reload.visual->started_at_seconds);
            CHECK(shown.frame_coordinate==Approx((at-.06)*30));CHECK(shown.reload_starts==0);
            CHECK(shown.primary_fire_starts==1);CHECK(shown.actions_rejected==0);
        };
        auto late=f::state(4,5);late.receiving_client->weapon_animation=4;
        late.receiving_client->next_weapon_attack=4.4;late.weapon_slots[0].in_reload=true;
        m.host.observe(late,.08);stable(.08);
        late=f::state(4,6);late.receiving_client->weapon_animation=4;
        late.receiving_client->next_weapon_attack=4.4;late.weapon_slots[0].in_reload=true;
        late.weapon_hud.animation_source=late.client_metadata.source;
        late.weapon_hud.animation_sequence=4;late.weapon_hud.animation_body=1;
        m.host.observe(late,.10);stable(.10);
        const auto clip_record=clip_first ? 7U : 8U;const auto event_record=clip_first ? 8U : 7U;
        const double clip_time=clip_first ? .12 : .14;const double event_time=clip_first ? .14 : .12;
        auto consumed=f::state(3,clip_record);consumed.receiving_client->weapon_animation=7;
        consumed.receiving_client->next_weapon_attack=4.3;consumed.weapon_slots[0].in_reload=true;
        auto echo=f::event(sniper,1,event_record,event_time,3,10);
        if(clip_first){m.host.observe(consumed,clip_time);stable(clip_time);}
        m.host.committed_scripted_event(echo);stable(event_time);
        if(!clip_first){m.host.observe(consumed,clip_time);stable(clip_time);}
        m.host.committed_scripted_event(echo);stable(.20);
        const auto confirmed=m.host.sample(.20);CHECK(confirmed.primary_fire_confirmed==1);
        CHECK(confirmed.actions_confirmed==1);CHECK(confirmed.reload_confirmed==0);
        CHECK(confirmed.recoil_starts==(sniper ? 0U : 1U));
        CHECK(confirmed.recoil_confirmed==(sniper ? 0U : 1U));CHECK(confirmed.recoil_rejected==0);
        CHECK(m.host.hud(consumed,.20).clip==3);CHECK(m.host.drain_audio().count==0);
    }
}
