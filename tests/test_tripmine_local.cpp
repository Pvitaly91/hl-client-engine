#include "tripmine_test_fixture.hpp"
#include <hlclient/games/halflife/inventory.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
namespace {namespace f=hlclient::tests::tripmine_fixture;namespace api=hlclient::game_api;
namespace g=hlclient::goldsrc;using Catch::Approx;}
TEST_CASE("E26 eligible local attempt uses one 128-unit point query and DRAW without ammo or entity mutation","[e26][tripmine-local][no-stock]") {
    f::Controller c;const auto before=c.observed;c.submit();const auto p=c.controller.sample(.02);
    REQUIRE(p.visual);REQUIRE(p.identity);CHECK(p.visual->sequence==6);CHECK(p.visual->body==0);
    CHECK(p.action==api::LocalWeaponAction::deploy_charge);CHECK(p.status==api::LocalWeaponActionStatus::predicted_pending);
    CHECK(c.controller.tripmine_state().eligible==1);CHECK(c.controller.tripmine_state().confirmed==0);
    CHECK(c.controller.tripmine_state().reserved_ammo==1);CHECK(c.observed==before);
    REQUIRE(c.query.last);CHECK(c.query.last->start.z==80);CHECK(c.query.last->end.x-c.query.last->start.x==Approx(128));
    CHECK(c.query.calls==1);CHECK(p.recoil_starts==0);CHECK(p.primary_fire_starts==0);CHECK(c.controller.drain_audio().count==0);
    c.controller.prepare_presentation(c.query,.03,1);c.submit(1,.02);
    CHECK(c.query.calls==1);CHECK(c.controller.sample(.03).visual->restart_identity==p.visual->restart_identity);
}
TEST_CASE("E26 no-hit startsolid allsolid and unavailable queries do not authorize placement or ammo consumption","[e26][tripmine-local][placement]") {
    for(unsigned mode=0;mode<4;++mode) {
        f::Controller c;const auto before=c.observed;
        if(mode==0){c.query.result.fraction=1;c.query.result.hit_entity.reset();}
        if(mode==1)c.query.result.start_solid=true;
        if(mode==2)c.query.result.all_solid=true;
        if(mode==3)c.query.result.valid=false;
        c.submit();CHECK_FALSE(c.controller.sample(.02).identity);CHECK(c.controller.tripmine_state().eligible==0);
        CHECK(c.controller.tripmine_state().reserved_ammo==0);CHECK(c.observed==before);
    }
}
TEST_CASE("E26 local conveyor hit animation is not successful placement and fresh unchanged ammo remains authoritative","[e26][tripmine-local][conveyor]") {
    // EV callback does not see FL_CONVEYOR. Fake server explicitly rejects its
    // support; it publishes unchanged AmmoX and no monster entity, not a success.
    f::Controller c;c.submit();auto rejected=f::state(3,2);c.observe(rejected,.1);
    CHECK(c.controller.tripmine_state().confirmed==0);CHECK(c.observed.packet_entities.empty());
    c.observe(f::state(3,3),1.1);CHECK(c.controller.tripmine_state().reserved_ammo==0);
    CHECK(c.controller.tripmine_state().rejected==1);CHECK(c.observed.weapon_hud.reserve_ammo[4]==3);
}
TEST_CASE("E26 fresh authoritative consumption confirms once and final mine retires without a ghost selection","[e26][tripmine-local][exhaustible]") {
    f::Controller c(1);c.submit();const auto p=c.controller.sample(.02);c.observe(f::state(0,2),.1);
    CHECK(c.controller.tripmine_state().confirmed==1);CHECK(c.controller.tripmine_state().reserved_ammo==0);
    CHECK(c.controller.tripmine_waiting_retirement());CHECK_FALSE(c.controller.sample(.1).visual);
    CHECK_FALSE(hlclient::games::halflife::build_weapon_selection_request(c.observed,13));
    c.observe(c.observed,.2);CHECK(c.controller.tripmine_state().confirmed==1);
    c.observe(f::state(2,3),.3);CHECK_FALSE(c.controller.tripmine_waiting_retirement());
    CHECK(hlclient::games::halflife::build_weapon_selection_request(c.observed,13));
    CHECK(p.identity->weapon_id==13);
}
TEST_CASE("E26 dynamic tripfire slot local-only event callback and command echo are bounded and deduplicated","[e26][tripmine-local][event]") {
    f::Controller c;c.submit();const auto p=c.controller.sample(.02);
    c.controller.committed_scripted_event(f::event(),1);c.controller.prepare_presentation(c.query,.03,1);
    CHECK(c.controller.sample(.03).visual->restart_identity==p.visual->restart_identity);CHECK(c.query.calls==1);
    f::Controller only;only.controller.committed_scripted_event(f::event(),1);only.controller.prepare_presentation(only.query,.03,1);
    REQUIRE(only.controller.sample(.03).identity);CHECK(only.controller.tripmine_state().confirmed==0);
    only.controller.committed_scripted_event(f::event(),1);only.controller.prepare_presentation(only.query,.04,1);CHECK(only.query.calls==1);
    f::Controller remote;remote.controller.committed_scripted_event(f::event(2,2),1);remote.controller.prepare_presentation(remote.query,.03,1);
    CHECK(remote.query.calls==0);
    f::Controller wrong;wrong.controller.committed_scripted_event(f::event(2,1,32),1);wrong.controller.prepare_presentation(wrong.query,.03,1);
    CHECK(wrong.query.calls==0);
}
TEST_CASE("E26 RMB reload unsupported metadata invalid ammo and missing event mapping do not create effects","[e26][tripmine-local][eligibility]") {
    for(auto buttons:{g::kReferenceGoldSrcButtonAttack2,g::kReferenceGoldSrcButtonReload}) {
        f::Controller c;c.submit(1,.02,buttons);CHECK(c.controller.tripmine_state().attempts==0);
    }
    for(unsigned ammo:{0U,6U}){f::Controller c(ammo);c.submit();CHECK(c.query.calls==0);}
    f::Controller missing;missing.controller.configure_scripted_events({});missing.submit();CHECK(missing.query.calls==0);
    auto invalid=f::model();invalid.sequences[6].label="arbitrary";f::Controller bad;bad.controller.bind_model(invalid);
    bad.controller.observe(bad.observed,.01);bad.submit();CHECK(bad.query.calls==0);
}
TEST_CASE("E26 normal host composes no-clip HUD and placement without firearm visuals or local world effects","[e26][tripmine-local][game-api]") {
    f::Module c;c.submit();const auto p=c.host.sample(.02);REQUIRE(p.visual);CHECK(p.visual->sequence==6);
    const auto hud=c.host.hud(c.observed,.02);CHECK_FALSE(hud.clip);CHECK(hud.primary_reserve==3);
    const auto visuals=c.host.local_visuals({{100,80,80},{1,0,0},{0,-1,0},{0,0,1},{},.02});
    CHECK_FALSE(visuals.flash);CHECK_FALSE(visuals.shell);CHECK_FALSE(visuals.world_impact);CHECK_FALSE(visuals.light);
    CHECK(c.host.drain_temporary_effects(.02).count==0);CHECK(c.host.drain_audio().count==0);
}
TEST_CASE("E26 captured crouch downward geometry no timer arming and frame rate independence","[e26][tripmine-local][fps]") {
    for(unsigned fps:{30U,60U,144U,240U}) {
        f::Controller c;auto cmd=f::command();cmd.shot_context->eye={100,80,46};cmd.shot_context->direction={0,0,-1};
        c.controller.submit(cmd,.02);c.controller.prepare_presentation(c.query,.02,1);
        REQUIRE(c.query.last);CHECK(c.query.last->start.z==46);CHECK(c.query.last->end.z==Approx(-82));
        for(unsigned tick=1;tick<=fps;++tick)(void)c.controller.sample(.02+tick/double(fps));
        CHECK(c.controller.tripmine_state().eligible==1);CHECK(c.observed.packet_entities.empty());
        CHECK(c.controller.drain_audio().count==0);CHECK(c.controller.tripmine_state().confirmed==0);
    }
}
TEST_CASE("E26 inherited event pitch crouch eye and conflicting stale callbacks stay presentation only","[e26][tripmine-local][event]") {
    f::Controller c;auto s=f::state(3,2);s.receiving_client->view_offset={0.,0.,12.};c.observe(s,.01);
    auto e=f::event(3);e.origin={100,80,34};e.angles.x=-30;e.angles_from_entity=true;e.ducking=true;
    c.controller.committed_scripted_event(e,1);c.controller.prepare_presentation(c.query,.03,1);
    REQUIRE(c.query.last);CHECK(c.query.last->start.z==Approx(46));CHECK(c.query.last->end.z==Approx(-82));
    auto conflict=e;conflict.angles.y=90;c.controller.committed_scripted_event(conflict,1);
    CHECK(c.controller.sample(.04).error==api::LocalWeaponPresentationError::conflicting_event);
    c.controller.committed_scripted_event(f::event(1),1);c.controller.prepare_presentation(c.query,.05,1);CHECK(c.query.calls==1);
    CHECK(c.observed.packet_entities.empty());CHECK(c.observed.weapon_hud.reserve_ammo[4]==3);
}
TEST_CASE("E26 cancelled pending query and death life boundary cannot create a late local DRAW","[e26][tripmine-local][lifecycle]") {
    f::Module c;c.host.submit(f::command(),.02);c.host.cancel_uncommitted();c.host.prepare_presentation(c.query,.03);
    CHECK(c.query.calls==0);
    auto dead=f::state(3,2);dead.receiving_client->health=0;dead.lifecycle.state=hlclient::client::LocalPlayerLifeState::dead;
    dead.lifecycle.life_epoch=2;dead.lifecycle.deaths=1;c.host.observe(dead,.04);c.host.committed_scripted_event(f::event(3));
    c.host.prepare_presentation(c.query,.05);CHECK(c.query.calls==0);CHECK_FALSE(c.host.sample(.05).visual);
    auto alive=f::state(3,4);alive.lifecycle.life_epoch=3;alive.lifecycle.deaths=1;c.host.observe(alive,.1);
    c.host.submit(f::command(2,.12,0),.12);c.host.submit(f::command(3,.14),.14);c.host.prepare_presentation(c.query,.14);
    CHECK(c.query.calls==1);CHECK(c.host.drain_temporary_effects(.14).count==0);
}
