#include "snark_test_fixture.hpp"
#include <hlclient/games/halflife/inventory.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <limits>

namespace {
namespace f=hlclient::tests::snark_fixture;
namespace api=hlclient::game_api;
namespace hl=hlclient::games::halflife;
namespace g=hlclient::goldsrc;
using Catch::Approx;
}

TEST_CASE("E27 Snark profile validates six imported poses rather than a guessed firearm model",
          "[e27][snark-local][profile]") {
    CHECK(hl::snark_weapon_id==15);CHECK(hl::snark_maximum_ammo==15);
    CHECK(hl::snark_default_give==5);CHECK(hl::snark_throw_cycle==Approx(.3));
    CHECK(hl::snark_recovery_seconds==Approx(1));
    CHECK(hl::snark_up_sequence==4);CHECK(hl::snark_throw_sequence==5);
    CHECK(hl::snark_command("weapon_snark"));CHECK_FALSE(hl::snark_command("weapon_squeak"));
    CHECK(hl::snark_sequences(f::model()));
    for(unsigned mode=0;mode<7;++mode) {
        CAPTURE(mode);auto model=f::model();
        if(mode==0)model.resource_name="models/v_grenade.mdl";
        if(mode==1)model.sequences.pop_back();
        if(mode==2)model.supported_bodies[0]=false;
        if(mode==3)model.selectable_bodies[0]=false;
        if(mode==4)model.sequences[4].frame_count=0;
        if(mode==5)model.sequences[5].fps=std::numeric_limits<double>::quiet_NaN();
        if(mode==6)model.sequences[5].fps=0;
        CHECK_FALSE(hl::snark_sequences(model));
    }
}

TEST_CASE("E27 normal host clearance starts THROW with no clip ammo world audio or firearm effects",
          "[e27][snark-local][game-api][no-stock]") {
    f::Module c;const auto before=c.observed;c.submit();const auto p=c.host.sample(.02);
    REQUIRE(p.visual);REQUIRE(p.identity);CHECK(p.visual->sequence==5);CHECK(p.visual->body==0);
    CHECK(p.action==api::LocalWeaponAction::throw_release);
    CHECK(p.status==api::LocalWeaponActionStatus::predicted_pending);
    CHECK(p.identity->weapon_id==15);CHECK(p.identity->command_sequence==1);
    CHECK(p.primary_fire_starts==0);CHECK(p.recoil_starts==0);CHECK(p.local_punch_pitch_degrees==0);
    REQUIRE(c.query.last);CHECK(c.query.calls==1);CHECK(c.query.effect_calls==0);
    CHECK(c.query.last->start.x==Approx(120));CHECK(c.query.last->end.x==Approx(164));
    CHECK(c.query.last->start.z==Approx(52));CHECK(c.query.last->end.z==Approx(52));
    CHECK(c.query.last->generation==1);CHECK(c.query.last->ignore_entity==1);
    CHECK(c.query.last->server_box_solid_code==2);CHECK(c.query.last->player_solid_code==3);
    const auto hud=c.host.hud(c.observed,.02);CHECK_FALSE(hud.clip);CHECK(hud.primary_reserve==5);
    const auto visuals=c.host.local_visuals({{100,80,80},{1,0,0},{0,-1,0},{0,0,1},{},.02});
    CHECK_FALSE(visuals.flash);CHECK_FALSE(visuals.shell);CHECK_FALSE(visuals.world_impact);CHECK_FALSE(visuals.light);
    CHECK(visuals.extra_shell_count==0);CHECK(visuals.extra_impact_count==0);
    CHECK(c.host.drain_temporary_effects(.02).count==0);CHECK(c.host.drain_audio().count==0);
    CHECK(c.observed==before);CHECK(c.observed.packet_entities.empty());
}

TEST_CASE("E27 standing crouched and downward sweeps use exact frozen origin and normalized 20 to 64 geometry",
          "[e27][snark-local][geometry][fps]") {
    for(unsigned fps:{30U,60U,144U,240U}) for(unsigned mode=0;mode<3;++mode) {
        CAPTURE(fps,mode);f::Controller c;const auto before=c.observed;
        auto cmd=f::command();cmd.shot_context->eye={900,800,700};
        cmd.shot_context->player_origin=std::array<float,3>{10,20,30};
        cmd.shot_context->player_ducked=mode!=0;
        cmd.shot_context->direction=mode==2 ? hlclient::assets::AssetVector3{0,0,-4} :
            hlclient::assets::AssetVector3{4,0,0};
        c.controller.submit(cmd,.02);
        // Mutating the caller's value is not permission to retarget its submission.
        cmd.shot_context->player_origin=std::array<float,3>{1000,2000,3000};
        cmd.shot_context->player_ducked=false;cmd.shot_context->direction={0,1,0};
        c.controller.prepare_presentation(c.query,.02,1);REQUIRE(c.query.last);
        const auto& trace=*c.query.last;
        if(mode==2) {
            CHECK(trace.start.x==Approx(10));CHECK(trace.end.x==Approx(10));
            CHECK(trace.start.z==Approx(28));CHECK(trace.end.z==Approx(-16));
        } else {
            CHECK(trace.start.x==Approx(30));CHECK(trace.end.x==Approx(74));
            CHECK(trace.start.z==Approx(mode==1 ? 48 : 30));CHECK(trace.end.z==trace.start.z);
        }
        CHECK(trace.start.y==Approx(20));CHECK(trace.end.y==Approx(20));
        for(unsigned tick=1;tick<=fps;++tick)(void)c.controller.sample(.02+tick/double(fps));
        CHECK(c.query.calls==1);CHECK(c.controller.snark_state().eligible==1);
        CHECK(c.controller.snark_state().confirmed==0);CHECK(c.controller.drain_audio().count==0);
        CHECK(c.observed==before);CHECK(c.observed.packet_entities.empty());
    }
}

TEST_CASE("E27 missing exact origin duck state or invalid direction never substitutes a view eye",
          "[e27][snark-local][geometry][unavailable]") {
    for(unsigned mode=0;mode<6;++mode) {
        CAPTURE(mode);f::Controller c;auto cmd=f::command();
        if(mode==0)cmd.shot_context->player_origin.reset();
        if(mode==1)cmd.shot_context->player_ducked.reset();
        if(mode==2)cmd.shot_context->direction={};
        if(mode==3)cmd.shot_context->direction.x=std::numeric_limits<float>::quiet_NaN();
        if(mode==4)(*cmd.shot_context->player_origin)[2]=std::numeric_limits<float>::infinity();
        if(mode==5)cmd.shot_context.reset();
        c.controller.submit(cmd,.02);c.controller.prepare_presentation(c.query,.02,1);
        CHECK(c.query.calls==0);CHECK(c.controller.snark_state().eligible==0);
        CHECK(c.controller.snark_state().reserved_ammo==0);CHECK(c.controller.snark_state().confirmed==0);
        CHECK_FALSE(c.controller.sample(.02).identity);
    }
}

TEST_CASE("E27 wall clearance threshold startsolid allsolid and invalid queries cannot authorize THROW",
          "[e27][snark-local][clearance]") {
    for(unsigned mode=0;mode<8;++mode) {
        CAPTURE(mode);f::Controller c;const auto before=c.observed;
        if(mode==0)c.query.result.fraction=.25;
        if(mode==1)c.query.result.fraction=.249;
        if(mode==2)c.query.result.start_solid=true;
        if(mode==3)c.query.result.all_solid=true;
        if(mode==4)c.query.result.valid=false;
        if(mode==5)c.query.result.fraction=-.1;
        if(mode==6)c.query.result.fraction=1.1;
        if(mode==7)c.query.result.fraction=std::numeric_limits<double>::quiet_NaN();
        c.submit();CHECK(c.query.calls==1);CHECK(c.controller.snark_state().eligible==0);
        CHECK(c.controller.snark_state().reserved_ammo==0);CHECK_FALSE(c.controller.sample(.02).identity);
        CHECK(c.controller.snark_state().confirmed==0);CHECK(c.observed==before);
        if(mode<4)CHECK(c.controller.snark_state().blocked==1);
        else CHECK(c.controller.snark_state().unavailable==1);
    }
    for(double fraction:{.25001,.5,1.0}) {
        CAPTURE(fraction);f::Controller c;c.query.result.fraction=fraction;
        c.query.result.hit_entity.reset();c.submit();
        REQUIRE(c.controller.sample(.02).visual);CHECK(c.controller.sample(.02).visual->sequence==5);
        CHECK(c.controller.snark_state().eligible==1);CHECK(c.controller.snark_state().reserved_ammo==1);
    }
}

TEST_CASE("E27 held primary repeats only on new eligible command after the point three second cycle",
          "[e27][snark-local][cadence]") {
    f::Controller c;c.submit();const auto first=c.controller.sample(.02);REQUIRE(first.visual);
    c.submit(2,.319);CHECK(c.query.calls==1);
    c.submit(3,.32);const auto second=c.controller.sample(.32);REQUIRE(second.visual);
    CHECK(c.query.calls==2);CHECK(c.controller.snark_state().eligible==2);
    CHECK(second.visual->restart_identity!=first.visual->restart_identity);
    CHECK(c.controller.snark_state().reserved_ammo==2);CHECK(c.observed.weapon_hud.reserve_ammo[4]==5);
    c.submit(3,.32);c.controller.prepare_presentation(c.query,.33,1);CHECK(c.query.calls==2);
    CHECK(c.controller.snark_state().confirmed==0);CHECK(c.observed.packet_entities.empty());
}

TEST_CASE("E27 command replay compares captured origin and duck state without restarting its throw",
          "[e27][snark-local][replay]") {
    for(unsigned mode=0;mode<2;++mode) {
        CAPTURE(mode);f::Controller c;auto cmd=f::command();c.controller.submit(cmd,.02);
        c.controller.prepare_presentation(c.query,.02,1);const auto first=c.controller.sample(.02);REQUIRE(first.visual);
        c.controller.submit(cmd,.03);CHECK(c.query.calls==1);
        if(mode==0)(*cmd.shot_context->player_origin)[0]+=1;
        else cmd.shot_context->player_ducked=true;
        c.controller.submit(cmd,.04);c.controller.prepare_presentation(c.query,.04,1);
        const auto replay=c.controller.sample(.04);REQUIRE(replay.visual);
        CHECK(replay.error==api::LocalWeaponPresentationError::conflicting_command);
        CHECK(replay.visual->restart_identity==first.visual->restart_identity);
        CHECK(c.query.calls==1);CHECK(c.controller.snark_state().eligible==1);
    }
}

TEST_CASE("E27 R and RMB do not manufacture reload throws sounds recoil or world actors",
          "[e27][snark-local][eligibility]") {
    for(auto buttons:{g::kReferenceGoldSrcButtonReload,g::kReferenceGoldSrcButtonAttack2,
            static_cast<std::uint16_t>(g::kReferenceGoldSrcButtonAttack|g::kReferenceGoldSrcButtonAttack2)}) {
        CAPTURE(buttons);f::Module c;c.submit(1,.02,buttons);const auto p=c.host.sample(.02);
        CHECK(c.query.calls==0);CHECK_FALSE(p.identity);CHECK(p.reload_starts==0);CHECK(p.recoil_starts==0);
        CHECK(c.host.drain_audio().count==0);CHECK(c.host.drain_temporary_effects(.02).count==0);
        CHECK(c.observed.packet_entities.empty());CHECK(c.observed.weapon_hud.reserve_ammo[4]==5);
    }
    for(unsigned ammo:{0U,16U}) {f::Controller c(ammo);c.submit();CHECK(c.query.calls==0);}
    f::Controller missing;missing.controller.configure_scripted_events({});missing.submit();CHECK(missing.query.calls==0);
    f::Controller wrong;auto model=f::model();model.sequences.resize(5);wrong.controller.bind_model(model);
    wrong.controller.observe(wrong.observed,.01);wrong.submit();CHECK(wrong.query.calls==0);
}

TEST_CASE("E27 only fresh source-stamped AmmoX decrease settles a reservation once",
          "[e27][snark-local][authority]") {
    f::Controller c;c.submit();
    auto inherited=c.observed;inherited.weapon_hud.reserve_ammo[4]=4;
    inherited.client_metadata.freshness=hlclient::client::RuntimeObservationFreshness::retained;
    c.observe(inherited,.05);CHECK(c.controller.snark_state().confirmed==0);
    CHECK(c.controller.snark_state().reserved_ammo==1);
    c.controller.committed_scripted_event(f::event(2,1,37,.06),1);
    c.controller.prepare_presentation(c.query,.06,1);CHECK(c.query.calls==1);
    CHECK(c.controller.snark_state().confirmed==0);CHECK(c.controller.snark_state().reserved_ammo==1);
    auto consumed=f::state(4,3);c.observe(consumed,.1);
    CHECK(c.controller.snark_state().confirmed==1);CHECK(c.controller.snark_state().reserved_ammo==0);
    c.observe(consumed,.12);CHECK(c.controller.snark_state().confirmed==1);
    CHECK(c.observed.packet_entities.empty());CHECK(c.observed.weapon_hud.reserve_ammo[4]==4);
}

TEST_CASE("E27 fresh unchanged reserve is not success and pending receipt expires at one point two five seconds",
          "[e27][snark-local][authority][timeout]") {
    f::Controller c;c.submit();c.observe(f::state(5,2),.1);
    CHECK(c.controller.snark_state().confirmed==0);CHECK(c.controller.snark_state().reserved_ammo==1);
    (void)c.controller.sample(1.269);CHECK(c.controller.snark_state().reserved_ammo==1);
    (void)c.controller.sample(1.27);CHECK(c.controller.snark_state().reserved_ammo==0);
    CHECK(c.controller.snark_state().rejected==1);CHECK(c.observed.weapon_hud.reserve_ammo[4]==5);
    c.observe(f::state(4,3),1.3);CHECK(c.controller.snark_state().confirmed==0);
    CHECK(c.observed.packet_entities.empty());
}

TEST_CASE("E27 repeated held throws have bounded reservations and a batched decrease cannot confirm twice",
          "[e27][snark-local][bounded][authority]") {
    f::Controller c(15);
    for(unsigned sequence=1;sequence<=96;++sequence) {
        const double at=.02+(sequence-1)*.31;c.submit(sequence,at);(void)c.controller.sample(at);
        CHECK(c.controller.snark_state().reserved_ammo<=8);
        CHECK(c.observed.weapon_hud.reserve_ammo[4]==15);CHECK(c.observed.packet_entities.empty());
    }
    REQUIRE(c.controller.snark_state().reserved_ammo==5);
    const auto rejected_before=c.controller.snark_state().rejected;
    (void)c.controller.sample(29.5);
    CHECK(c.controller.snark_state().rejected==rejected_before+1);
    const auto pending=c.controller.snark_state().reserved_ammo;REQUIRE(pending==4);
    c.observe(f::state(12,2),29.5);CHECK(c.controller.snark_state().confirmed==3);
    CHECK(c.controller.snark_state().reserved_ammo==pending-3);
    CHECK(c.controller.snark_state().reserved_ammo==1);
    c.observe(c.observed,29.51);CHECK(c.controller.snark_state().confirmed==3);
    CHECK(c.controller.drain_audio().count==0);
}

TEST_CASE("E27 local event uses advertised arbitrary slot and cannot confirm command or spawn a Snark",
          "[e27][snark-local][event]") {
    f::Controller c;const std::array bindings{f::binding(700)};c.controller.configure_scripted_events(bindings);
    c.submit();const auto first=c.controller.sample(.02);REQUIRE(first.visual);
    const auto echo=f::event(2,1,700,.03);c.controller.committed_scripted_event(echo,1);
    c.controller.prepare_presentation(c.query,.03,1);c.controller.committed_scripted_event(echo,1);
    c.controller.prepare_presentation(c.query,.04,1);const auto after=c.controller.sample(.04);REQUIRE(after.visual);
    CHECK(c.query.calls==1);CHECK(after.visual->restart_identity==first.visual->restart_identity);
    CHECK(c.controller.snark_state().duplicates==1);CHECK(c.controller.snark_state().confirmed==0);
    CHECK(c.controller.snark_state().reserved_ammo==1);CHECK(c.observed.weapon_hud.reserve_ammo[4]==5);
    CHECK(c.observed.packet_entities.empty());CHECK(c.controller.drain_audio().count==0);
}

TEST_CASE("E27 committed-only crouch callback reconstructs entity pitch and ignores the observed view offset",
          "[e27][snark-local][event][geometry]") {
    f::Controller c;auto observed=f::state(5,2);observed.receiving_client->view_offset={0.,0.,999.};
    c.observe(observed,.01);auto e=f::event(3);e.origin={10,20,30};e.ducking=true;
    e.angles.x=-30;e.angles_from_entity=true;
    c.controller.committed_scripted_event(e,1);c.controller.prepare_presentation(c.query,.03,1);
    const auto p=c.controller.sample(.03);REQUIRE(p.visual);REQUIRE(c.query.last);
    CHECK(p.visual->sequence==5);CHECK(p.visual->source==api::LocalWeaponAnimationSource::service_event);
    CHECK(c.query.last->start.z==Approx(28));CHECK(c.query.last->end.z==Approx(-16));
    CHECK(c.query.last->start.x==Approx(10).margin(.0001));CHECK(c.query.last->end.x==Approx(10).margin(.0001));
    CHECK(c.controller.snark_state().eligible==1);CHECK(c.controller.snark_state().reserved_ammo==0);
    CHECK(c.controller.snark_state().confirmed==0);CHECK(c.observed.weapon_hud.reserve_ammo[4]==5);
    CHECK(c.observed.packet_entities.empty());
}

TEST_CASE("E27 duplicate blocked callback stays blocked even when a later query would be clear",
          "[e27][snark-local][event][duplicates]") {
    f::Controller c;c.query.result.fraction=.25;const auto e=f::event();
    c.controller.committed_scripted_event(e,1);c.controller.prepare_presentation(c.query,.03,1);
    CHECK(c.query.calls==1);CHECK(c.controller.snark_state().blocked==1);
    c.query.result.fraction=1;c.controller.committed_scripted_event(e,1);
    c.controller.prepare_presentation(c.query,.04,1);CHECK(c.query.calls==1);
    CHECK(c.controller.snark_state().duplicates==1);CHECK(c.controller.snark_state().eligible==0);
    CHECK_FALSE(c.controller.sample(.04).identity);CHECK(c.controller.snark_state().confirmed==0);
}

TEST_CASE("E27 blocked command echo cannot become an independent eligible throw",
          "[e27][snark-local][event][duplicates]") {
    f::Controller c;c.query.result.fraction=.2;c.submit();c.query.result.fraction=1;
    c.controller.committed_scripted_event(f::event(),1);c.controller.prepare_presentation(c.query,.03,1);
    CHECK(c.query.calls==1);CHECK(c.controller.snark_state().eligible==0);
    CHECK(c.controller.snark_state().reserved_ammo==0);CHECK(c.controller.snark_state().confirmed==0);
}

TEST_CASE("E27 conflicting callback origin angles and duck state are not new occurrences",
          "[e27][snark-local][event][conflict]") {
    for(unsigned mode=0;mode<3;++mode) {
        CAPTURE(mode);f::Controller c;const auto e=f::event();
        c.controller.committed_scripted_event(e,1);c.controller.prepare_presentation(c.query,.03,1);
        const auto first=c.controller.sample(.03);REQUIRE(first.visual);auto conflict=e;
        if(mode==0)conflict.origin.x+=1;
        if(mode==1)conflict.angles.y=90;
        if(mode==2)conflict.ducking=true;
        c.controller.committed_scripted_event(conflict,1);c.controller.prepare_presentation(c.query,.04,1);
        const auto p=c.controller.sample(.04);REQUIRE(p.visual);
        CHECK(p.error==api::LocalWeaponPresentationError::conflicting_event);
        CHECK(p.visual->restart_identity==first.visual->restart_identity);CHECK(c.query.calls==1);
        CHECK(c.controller.snark_state().confirmed==0);CHECK(c.controller.snark_state().reserved_ammo==0);
    }
}

TEST_CASE("E27 remote wrong-slot stale-generation delayed and prebinding callbacks remain inert",
          "[e27][snark-local][event][local-only]") {
    for(unsigned mode=0;mode<8;++mode) {
        CAPTURE(mode);f::Controller c;auto e=f::event();
        if(mode==0)e.emitter_entity=2;
        if(mode==1)e.event_index=38;
        if(mode==2)e.generation=2;
        if(mode==3)e.fire_delay_ticks=1;
        if(mode==4)e.weapon_model_index=76;
        if(mode==5)e.record=e.record_ordinal=1;
        if(mode==6)e.resolution=api::ScriptedEventResolution::missing_arguments;
        if(mode==7)e.received_at_seconds=-.01;
        c.controller.committed_scripted_event(e,1);c.controller.prepare_presentation(c.query,.03,1);
        CHECK(c.query.calls==0);CHECK(c.controller.snark_state().eligible==0);
        CHECK(c.controller.snark_state().reserved_ammo==0);CHECK(c.observed.packet_entities.empty());
    }
    f::Controller missing;missing.controller.committed_scripted_event(f::event(),std::nullopt);
    missing.controller.prepare_presentation(missing.query,.03,std::nullopt);CHECK(missing.query.calls==0);
}

TEST_CASE("E27 fresh mappings are required after reset and conflicting advertised slots fail closed",
          "[e27][snark-local][mapping]") {
    f::Controller c;auto other=f::binding();constexpr std::string_view name="events/tripfire.sc";
    other.name.fill(0);std::copy(name.begin(),name.end(),other.name.begin());
    const std::array conflict{f::binding(),other};c.controller.configure_scripted_events(conflict);
    c.submit();CHECK(c.query.calls==0);
    f::Module m;m.host.reset({2,2,2});auto model=f::model();model.generation=2;m.host.bind_model(model);
    auto observed=f::state();observed.generation=observed.client_metadata.generation=2;m.observe(observed,.1);
    m.host.committed_scripted_event(f::event(2,1,37,.11));m.host.prepare_presentation(m.query,.11);
    auto cmd=f::command(1,.12);cmd.generation=2;m.host.submit(cmd,.12);m.host.prepare_presentation(m.query,.12);
    CHECK(m.query.calls==0);
    const std::array bindings{f::binding(701)};m.host.configure_scripted_events(bindings);
    cmd=f::command(2,.14);cmd.generation=2;m.host.submit(cmd,.14);m.host.prepare_presentation(m.query,.14);
    CHECK(m.query.calls==1);CHECK(m.observed.packet_entities.empty());
}

TEST_CASE("E27 UP is command-driven after one second and primary or secondary hold prevents redraw",
          "[e27][snark-local][recovery][fps]") {
    for(unsigned fps:{30U,60U,144U,240U}) {
        CAPTURE(fps);f::Controller c;c.submit();c.observe(f::state(4,2),.1);
        for(unsigned frame=1;frame<fps;++frame)(void)c.controller.sample(.02+frame/double(fps));
        c.submit(2,1.019,0);const auto before=c.controller.sample(1.019);REQUIRE(before.visual);
        CHECK(before.visual->sequence==5);
        c.submit(3,1.02,g::kReferenceGoldSrcButtonAttack2);const auto held=c.controller.sample(1.02);REQUIRE(held.visual);
        CHECK(held.visual->sequence!=4);CHECK(c.query.calls==1);
        c.submit(4,1.04,0);const auto up=c.controller.sample(1.04);REQUIRE(up.visual);
        CHECK(up.visual->sequence==4);CHECK(up.visual->body==0);CHECK(up.visual->started_at_seconds==Approx(1.04));
        CHECK(c.controller.sample(1.05).visual->restart_identity==up.visual->restart_identity);
        CHECK(c.controller.snark_state().eligible==1);CHECK(c.controller.snark_state().confirmed==1);
        CHECK(c.controller.drain_audio().count==0);
    }
    f::Controller held;held.submit();held.submit(2,.32);CHECK(held.query.calls==2);
    held.submit(3,1.319,0);REQUIRE(held.controller.sample(1.319).visual);
    CHECK(held.controller.sample(1.319).visual->sequence!=4);
    held.submit(4,1.32,0);REQUIRE(held.controller.sample(1.32).visual);
    CHECK(held.controller.sample(1.32).visual->sequence==4);
}

TEST_CASE("E27 last authoritative ammo preserves THROW before deadline then retires and pickup revives",
          "[e27][snark-local][exhaustible]") {
    f::Controller c(1);c.submit();const auto thrown=c.controller.sample(.02);REQUIRE(thrown.visual);
    c.observe(f::state(0,2),.1);CHECK(c.controller.snark_state().confirmed==1);
    CHECK_FALSE(c.controller.snark_waiting_retirement());REQUIRE(c.controller.sample(.1).visual);
    CHECK(c.controller.sample(.1).visual->sequence==5);
    CHECK_FALSE(hl::build_weapon_selection_request(c.observed,15));
    c.submit(2,1.019,0);CHECK_FALSE(c.controller.snark_waiting_retirement());
    c.submit(3,1.02,0);CHECK(c.controller.snark_waiting_retirement());
    CHECK_FALSE(c.controller.sample(1.02).visual);CHECK(c.query.calls==1);
    c.observe(f::state(2,3),1.1);CHECK_FALSE(c.controller.snark_waiting_retirement());
    REQUIRE(c.controller.sample(1.1).visual);CHECK(hl::build_weapon_selection_request(c.observed,15));
    CHECK(c.controller.snark_state().confirmed==1);CHECK(c.observed.packet_entities.empty());
}

TEST_CASE("E27 initial server UP uses imported animation duration and enters idle exactly once",
          "[e27][snark-local][metadata]") {
    hl::LocalWeaponPresentationController controller;auto model=f::model();
    model.sequences[4].fps=20;model.sequences[4].frame_count=5;
    controller.bind_model(model);controller.observe(f::state(),.01);
    const auto up=controller.sample(.01);REQUIRE(up.visual);CHECK(up.visual->sequence==4);
    const auto middle=controller.sample(.16);REQUIRE(middle.visual);
    CHECK(middle.visual->sequence==4);CHECK(middle.frame_coordinate==Approx(3));
    const auto idle=controller.sample(.22);REQUIRE(idle.visual);CHECK(idle.visual->sequence==0);
    const auto later=controller.sample(.25);REQUIRE(later.visual);
    CHECK(later.visual->restart_identity==idle.visual->restart_identity);
    CHECK(controller.snark_state().attempts==0);CHECK(controller.snark_state().reserved_ammo==0);
}

TEST_CASE("E27 exhaustible flag twenty four valid reserve and inert catalogue token gate inventory selection",
          "[e27][snark-local][inventory]") {
    CHECK(hl::build_weapon_selection_request(f::state(),15));
    for(unsigned mode=0;mode<7;++mode) {
        CAPTURE(mode);auto state=f::state();
        if(mode==0)state.weapon_hud.reserve_ammo[4]=0;
        if(mode==1)state.weapon_hud.reserve_ammo[4]=16;
        if(mode==2)state.weapon_hud.reserve_ammo[4].reset();
        if(mode==3)state.weapon_hud.catalogue.front().flags=8;
        if(mode==4)state.weapon_hud.catalogue.front().flags=16;
        if(mode==5)state.weapon_hud.catalogue.front().primary_ammo_type=-1;
        if(mode==6)state.weapon_hud.catalogue.front().command_name="weapon_snark;quit";
        CHECK_FALSE(hl::build_weapon_selection_request(state,15));
        CHECK(hl::cycle_owned_weapon(state,2,1)!=15);
    }
}

TEST_CASE("E27 cancellation clears pending query and requires release while retaining submitted echo tombstone",
          "[e27][snark-local][focus]") {
    f::Module pending;pending.host.submit(f::command(),.02);pending.host.cancel_uncommitted();
    pending.host.prepare_presentation(pending.query,.03);CHECK(pending.query.calls==0);
    pending.host.submit(f::command(2,.04),.04);pending.host.prepare_presentation(pending.query,.04);
    CHECK(pending.query.calls==0);pending.submit(3,.06,0);pending.submit(4,.34);CHECK(pending.query.calls==1);
    f::Controller accepted;accepted.submit();accepted.controller.cancel_uncommitted();
    accepted.controller.committed_scripted_event(f::event(),1);
    accepted.controller.prepare_presentation(accepted.query,.03,1);
    CHECK(accepted.query.calls==1);CHECK(accepted.controller.snark_state().reserved_ammo==0);
    CHECK(accepted.controller.snark_state().confirmed==0);CHECK(accepted.observed.packet_entities.empty());
}

TEST_CASE("E27 model rebind cancels pending attempt and stale model-bound callback cannot restart it",
          "[e27][snark-local][binding]") {
    f::Controller c;c.controller.submit(f::command(),.02);auto rebound=f::model();++rebound.resource_revision;
    c.controller.bind_model(rebound);c.observe(f::state(5,3),.03);
    c.controller.prepare_presentation(c.query,.04,1);CHECK(c.query.calls==0);
    c.controller.committed_scripted_event(f::event(2,1,37,.04),1);
    c.controller.prepare_presentation(c.query,.04,1);CHECK(c.query.calls==0);
    c.submit(2,.06);CHECK(c.query.calls==1);CHECK(c.observed.packet_entities.empty());
}

TEST_CASE("E27 death and respawn life barrier reject late callbacks and held respawn press",
          "[e27][snark-local][lifecycle]") {
    f::Module c;c.host.submit(f::command(),.02);
    auto dead=f::state(5,2);dead.receiving_client->health=0;
    dead.lifecycle.state=hlclient::client::LocalPlayerLifeState::dead;dead.lifecycle.life_epoch=1;dead.lifecycle.deaths=1;
    dead.lifecycle.last_death_source=dead.client_metadata.source;c.observe(dead,.04);
    c.host.committed_scripted_event(f::event(3,1,37,.05));c.host.prepare_presentation(c.query,.05);
    CHECK(c.query.calls==0);CHECK_FALSE(c.host.sample(.05).visual);
    auto alive=f::state(5,4);alive.lifecycle=dead.lifecycle;
    alive.lifecycle.state=hlclient::client::LocalPlayerLifeState::alive;alive.lifecycle.life_epoch=2;alive.lifecycle.respawns=1;
    c.observe(alive,.1);c.host.committed_scripted_event(f::event(5,1,37,.11));
    c.host.prepare_presentation(c.query,.11);CHECK(c.query.calls==0);
    c.submit(2,.12);CHECK(c.query.calls==0);c.submit(3,.14,0);c.submit(4,.16);CHECK(c.query.calls==1);
    CHECK(c.host.drain_temporary_effects(.16).count==0);CHECK(c.host.drain_audio().count==0);
    CHECK(c.observed.packet_entities.empty());
}

TEST_CASE("E27 distinct local callbacks queued before presentation each receive their own clearance trace",
          "[e27][snark-local][event][queue]") {
    f::Controller c;const auto before=c.observed;
    auto first=f::event(2,1,37,.02);first.entry_ordinal=0;
    auto second=f::event(3,1,37,.03);second.entry_ordinal=1;second.origin={200,100,60};
    c.controller.committed_scripted_event(first,1);c.controller.committed_scripted_event(second,1);
    CHECK(c.query.calls==0);c.controller.prepare_presentation(c.query,.03,1);
    const auto p=c.controller.sample(.03);REQUIRE(p.visual);REQUIRE(p.identity);REQUIRE(c.query.last);
    CHECK(c.query.calls==2);CHECK(c.controller.snark_state().attempts==2);
    CHECK(c.controller.snark_state().eligible==2);CHECK(p.actions_started==2);
    CHECK(p.visual->sequence==5);CHECK(p.visual->started_at_seconds==Approx(.03));
    CHECK(p.visual->source==api::LocalWeaponAnimationSource::service_event);
    CHECK(p.identity->command_sequence==0);CHECK(p.identity->sub_identity==2);
    CHECK(c.query.last->start.x==Approx(220));CHECK(c.query.last->end.x==Approx(264));
    CHECK(c.query.last->start.y==Approx(100));CHECK(c.query.last->start.z==Approx(60));
    c.controller.committed_scripted_event(first,1);c.controller.prepare_presentation(c.query,.04,1);
    CHECK(c.query.calls==2);CHECK(c.controller.snark_state().duplicates==1);
    CHECK(c.controller.snark_state().reserved_ammo==0);CHECK(c.controller.snark_state().confirmed==0);
    CHECK(c.controller.drain_audio().count==0);CHECK(c.observed==before);
}

TEST_CASE("E27 event first then matching immutable command before presentation coalesces one throw",
          "[e27][snark-local][event][queue][replay]") {
    f::Controller c;auto event=f::event(2,1,37,.02);event.origin={42,20,30};
    auto command=f::command();command.shot_context->player_origin=std::array<float,3>{42,20,30};
    c.controller.committed_scripted_event(event,1);c.controller.submit(command,.03);
    c.controller.prepare_presentation(c.query,.03,1);const auto p=c.controller.sample(.03);
    REQUIRE(p.visual);REQUIRE(p.identity);REQUIRE(c.query.last);
    CHECK(c.query.calls==1);CHECK(c.controller.snark_state().eligible==1);CHECK(p.actions_started==1);
    CHECK(p.identity->command_sequence==1);CHECK(p.identity->sub_identity==0);
    CHECK(c.controller.snark_state().reserved_ammo==1);CHECK(c.controller.snark_state().confirmed==0);
    CHECK(c.query.last->start.x==Approx(62));CHECK(c.query.last->end.x==Approx(106));
    c.controller.committed_scripted_event(event,1);c.controller.prepare_presentation(c.query,.04,1);
    CHECK(c.query.calls==1);CHECK(c.controller.sample(.04).visual->restart_identity==p.visual->restart_identity);
    CHECK(c.controller.snark_state().duplicates==1);CHECK(c.observed.weapon_hud.reserve_ammo[4]==5);
    CHECK(c.observed.packet_entities.empty());
}

TEST_CASE("E27 callback backlog is bounded to eight queries with explicit unavailable overflow",
          "[e27][snark-local][event][queue][bounded]") {
    f::Controller c;
    for(unsigned index=0;index<12;++index) {
        auto event=f::event(index+2,1,37,.03);event.entry_ordinal=index;event.origin.x+=index;
        c.controller.committed_scripted_event(event,1);
    }
    CHECK(c.query.calls==0);CHECK(c.controller.snark_state().attempts==12);
    CHECK(c.controller.snark_state().unavailable==4);
    c.controller.prepare_presentation(c.query,.03,1);const auto p=c.controller.sample(.03);
    REQUIRE(p.visual);REQUIRE(p.identity);REQUIRE(c.query.last);
    CHECK(c.query.calls==8);CHECK(c.controller.snark_state().eligible==8);CHECK(p.actions_started==8);
    CHECK(p.identity->sub_identity==8);CHECK(c.query.last->start.x==Approx(127));
    c.controller.prepare_presentation(c.query,.04,1);CHECK(c.query.calls==8);
    CHECK(c.controller.snark_state().reserved_ammo==0);CHECK(c.controller.snark_state().confirmed==0);
    CHECK(c.controller.drain_audio().count==0);CHECK(c.observed.weapon_hud.reserve_ammo[4]==5);
    CHECK(c.observed.packet_entities.empty());
}

TEST_CASE("E27 blocked submitted attempt still recovers UP after one second without a THROW or ammo claim",
          "[e27][snark-local][clearance][recovery][sdk]") {
    f::Controller c;const auto before=c.observed;const auto initial=c.controller.sample(0);REQUIRE(initial.visual);
    c.query.result.fraction=.25;c.submit();const auto blocked=c.controller.sample(.02);
    CHECK_FALSE(blocked.identity);CHECK(blocked.actions_started==0);
    CHECK(c.controller.snark_state().blocked==1);CHECK(c.controller.snark_state().eligible==0);
    CHECK(c.controller.snark_state().reserved_ammo==0);CHECK(c.controller.snark_state().confirmed==0);
    c.submit(2,1.019,0);const auto waiting=c.controller.sample(1.019);REQUIRE(waiting.visual);
    CHECK(waiting.visual->sequence!=5);
    CHECK(waiting.visual->source!=api::LocalWeaponAnimationSource::provisional_command);
    c.submit(3,1.02,0);const auto up=c.controller.sample(1.02);REQUIRE(up.visual);
    CHECK(up.visual->sequence==4);CHECK(up.visual->started_at_seconds==Approx(1.02));
    CHECK(up.visual->restart_identity!=waiting.visual->restart_identity);
    CHECK(up.visual->source==api::LocalWeaponAnimationSource::provisional_command);
    CHECK(up.actions_started==0);CHECK(c.query.calls==1);CHECK(c.observed==before);
    CHECK(c.controller.drain_audio().count==0);CHECK(c.observed.packet_entities.empty());
}

TEST_CASE("E27 blocked event only callback does not infer client JustThrown recovery",
          "[e27][snark-local][clearance][recovery][sdk]") {
    f::Controller c;c.query.result.fraction=.25;
    c.controller.committed_scripted_event(f::event(),1);c.controller.prepare_presentation(c.query,.03,1);
    CHECK(c.controller.snark_state().blocked==1);CHECK(c.controller.snark_state().eligible==0);
    const auto idle=c.controller.sample(1.02);REQUIRE(idle.visual);CHECK(idle.visual->sequence==0);
    c.submit(1,1.04,0);const auto released=c.controller.sample(1.04);REQUIRE(released.visual);
    CHECK(released.visual->sequence==0);CHECK(released.visual->restart_identity==idle.visual->restart_identity);
    CHECK_FALSE(released.identity);CHECK(c.controller.snark_state().reserved_ammo==0);
    CHECK(c.controller.snark_state().confirmed==0);CHECK(c.query.calls==1);
    CHECK(c.controller.drain_audio().count==0);CHECK(c.observed.weapon_hud.reserve_ammo[4]==5);
    CHECK(c.observed.packet_entities.empty());
}

TEST_CASE("E27 fresh svc weaponanim UP or idle consumes recovery without a provisional redraw restart",
          "[e27][snark-local][recovery][authority][weaponanim]") {
    for(const std::uint8_t sequence:{std::uint8_t{4},std::uint8_t{0},std::uint8_t{3}}) {
        CAPTURE(sequence);f::Controller c;c.submit();c.observe(f::state(4,2),.1);
        auto authoritative=f::state(4,3);authoritative.weapon_hud.animation_sequence=sequence;
        authoritative.weapon_hud.animation_body=std::uint8_t{0};
        authoritative.weapon_hud.animation_source=authoritative.client_metadata.source;
        c.observe(authoritative,1.03);const auto corrected=c.controller.sample(1.03);REQUIRE(corrected.visual);
        CHECK(corrected.visual->sequence==sequence);
        CHECK(corrected.visual->source==api::LocalWeaponAnimationSource::service_event);
        CHECK(corrected.visual->started_at_seconds==Approx(1.03));
        c.submit(2,1.04,0);const auto released=c.controller.sample(1.04);REQUIRE(released.visual);
        CHECK(released.visual->sequence==sequence);
        CHECK(released.visual->restart_identity==corrected.visual->restart_identity);
        CHECK(released.visual->source==api::LocalWeaponAnimationSource::service_event);
        CHECK(released.visual->started_at_seconds==corrected.visual->started_at_seconds);
        c.submit(3,1.06,0);c.observe(authoritative,1.08);const auto retained=c.controller.sample(1.08);REQUIRE(retained.visual);
        CHECK(retained.visual->restart_identity==corrected.visual->restart_identity);
        CHECK(retained.visual->source==api::LocalWeaponAnimationSource::service_event);
        CHECK(retained.visual->started_at_seconds==corrected.visual->started_at_seconds);
        CHECK(c.controller.snark_state().confirmed==1);CHECK(c.controller.snark_state().reserved_ammo==0);
        CHECK(c.query.calls==1);CHECK(c.observed.packet_entities.empty());CHECK(c.controller.drain_audio().count==0);
    }
}

TEST_CASE("E27 fresh changed clientdata THROW to UP correction consumes recovery and preserves server timeline",
          "[e27][snark-local][recovery][authority][clientdata]") {
    f::Controller c;c.submit();auto server_throw=f::state(5,2);server_throw.receiving_client->weapon_animation=5;
    c.observe(server_throw,.1);const auto thrown=c.controller.sample(.1);REQUIRE(thrown.visual);
    CHECK(thrown.visual->sequence==5);
    auto server_up=f::state(5,3);server_up.receiving_client->weapon_animation=4;
    c.observe(server_up,1.03);const auto corrected=c.controller.sample(1.03);REQUIRE(corrected.visual);
    CHECK(corrected.visual->sequence==4);CHECK(corrected.visual->started_at_seconds==Approx(1.03));
    CHECK(corrected.visual->source==api::LocalWeaponAnimationSource::clientdata_state);
    c.submit(2,1.04,0);const auto released=c.controller.sample(1.04);REQUIRE(released.visual);
    CHECK(released.visual->sequence==4);CHECK(released.visual->restart_identity==corrected.visual->restart_identity);
    CHECK(released.visual->source==api::LocalWeaponAnimationSource::clientdata_state);
    CHECK(released.visual->started_at_seconds==corrected.visual->started_at_seconds);
    c.observe(server_up,1.06);const auto retained=c.controller.sample(1.06);REQUIRE(retained.visual);
    CHECK(retained.visual->restart_identity==corrected.visual->restart_identity);
    CHECK(c.controller.snark_state().confirmed==0);CHECK(c.controller.snark_state().reserved_ammo==1);
    CHECK(c.observed.weapon_hud.reserve_ammo[4]==5);CHECK(c.query.calls==1);
    CHECK(retained.actions_confirmed==0);
    CHECK(c.observed.packet_entities.empty());CHECK(c.controller.drain_audio().count==0);
}
