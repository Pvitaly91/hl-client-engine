#include "satchel_test_fixture.hpp"
#include <hlclient/games/halflife/inventory.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
namespace {namespace f=hlclient::tests::satchel_fixture;namespace api=hlclient::game_api;
namespace g=hlclient::goldsrc;using Catch::Approx;}
TEST_CASE("E25 first throw switches charge to radio draw without canonical ammo or projectile mutation","[e25][satchel-local][no-stock]") {
    f::Controller m;const auto before=m.observed;m.submit(1,.02);
    const auto out=m.controller.sample(.02);REQUIRE(out.identity);REQUIRE(out.visual);
    CHECK(out.identity->kind==api::LocalWeaponAction::deploy_charge);CHECK(out.identity->model_index==85);
    CHECK(out.visual->sequence==2);CHECK(m.controller.satchel_state().charge_ready==1);
    CHECK(m.controller.satchel_state().reserved_ammo==1);CHECK(m.observed==before);
    CHECK(m.controller.satchel_state().last_throw_context==f::command(1,.02).shot_context);
    CHECK(m.controller.drain_audio().count==0);CHECK(out.primary_fire_starts==0);CHECK(out.recoil_starts==0);
}
TEST_CASE("E25 radio permits additional throws only at one second cadence","[e25][satchel-local][input]") {
    f::Controller m;m.submit(1,.02);m.submit(2,.5);CHECK(m.controller.satchel_state().throws==1);
    m.submit(3,1.02);CHECK(m.controller.satchel_state().throws==2);CHECK(m.controller.satchel_state().reserved_ammo==2);
    CHECK(m.controller.satchel_state().charge_ready==1);CHECK(m.controller.sample(1.02).visual->sequence==2);
    CHECK(m.controller.satchel_presentation_model()->resource_name=="models/v_satchel_radio.mdl");
}
TEST_CASE("E25 zero pocket radio detonates without visible world entities","[e25][satchel-local][select-empty]") {
    f::Controller m(0,1);m.submit(1,.02,g::kReferenceGoldSrcButtonAttack2);
    const auto out=m.controller.sample(.02);REQUIRE(out.visual);CHECK(out.visual->sequence==3);
    CHECK(out.action==api::LocalWeaponAction::remote_detonate);CHECK(m.controller.satchel_state().charge_ready==2);
    CHECK(m.controller.satchel_state().detonations==1);CHECK(m.observed.packet_entities.empty());
}
TEST_CASE("E25 secondary in charge mode and reload have no fabricated behavior","[e25][satchel-local][input]") {
    f::Controller m;m.submit(1,.02,g::kReferenceGoldSrcButtonAttack2);m.submit(2,.04,g::kReferenceGoldSrcButtonReload);
    m.submit(3,.06,g::kReferenceGoldSrcButtonAttack|g::kReferenceGoldSrcButtonAttack2);
    CHECK(m.controller.satchel_state().throws==0);CHECK(m.controller.satchel_state().detonations==0);
    CHECK(m.controller.satchel_state().charge_ready==0);CHECK(m.controller.drain_audio().count==0);
}
TEST_CASE("E25 state two blocks primary and held radio fire until neutral idle transition","[e25][satchel-local][input]") {
    f::Controller m(2,1);m.submit(1,.02,g::kReferenceGoldSrcButtonAttack2);
    for(unsigned i=2;i<30;++i)m.submit(i,.02*i,g::kReferenceGoldSrcButtonAttack|g::kReferenceGoldSrcButtonAttack2);
    CHECK(m.controller.satchel_state().throws==0);CHECK(m.controller.satchel_state().detonations==1);
    CHECK(m.controller.satchel_state().charge_ready==2);m.submit(30,.6,0);
    CHECK(m.controller.satchel_state().charge_ready==0);CHECK(m.controller.sample(.6).visual->sequence==2);
    CHECK(m.controller.satchel_presentation_model()->resource_name=="models/v_satchel.mdl");
    m.submit(31,.8);CHECK(m.controller.satchel_state().throws==0);
    m.submit(32,1.1);CHECK(m.controller.satchel_state().throws==1);
}
TEST_CASE("E25 last deployed charge stays selectable until final detonation retirement","[e25][satchel-local][exhaustion]") {
    f::Controller m(1);m.submit(1,.02);auto confirmed=f::state(0,1,2);confirmed.weapon_slots[0].auxiliary_integer1_updated_in_record=true;
    m.observe(confirmed,.1);CHECK(m.controller.satchel_state().reserved_ammo==0);
    REQUIRE(hlclient::games::halflife::build_weapon_selection_request(confirmed,14));
    CHECK(hlclient::games::halflife::cycle_owned_weapon(confirmed,2,1)==14);
    m.submit(2,.52,g::kReferenceGoldSrcButtonAttack2);m.submit(3,1.02,0);
    CHECK(m.controller.satchel_waiting_retirement());CHECK_FALSE(m.controller.sample(1.02).visual);
    auto retired=f::state(0,0,3);retired.weapon_slots[0].auxiliary_integer1_updated_in_record=true;m.observe(retired,1.1);
    CHECK_FALSE(hlclient::games::halflife::build_weapon_selection_request(retired,14));
    auto pickup=f::state(1,0,4);m.observe(pickup,1.2);CHECK_FALSE(m.controller.satchel_waiting_retirement());
    CHECK(hlclient::games::halflife::build_weapon_selection_request(pickup,14));
}
TEST_CASE("E25 first throw survives matching server model echo without a restarted action","[e25][satchel-local][reconciliation]") {
    f::Controller m;m.submit(1,.02);const auto predicted=m.controller.sample(.02);
    auto confirm=f::state(2,1,2);confirm.receiving_client->weapon_animation=2;confirm.weapon_slots[0].auxiliary_integer1_updated_in_record=true;
    m.observe(confirm,.1);const auto out=m.controller.sample(.1);
    CHECK(out.identity==predicted.identity);CHECK(out.visual->restart_identity==predicted.visual->restart_identity);
    CHECK(m.controller.satchel_state().reserved_ammo==0);CHECK(out.status==api::LocalWeaponActionStatus::server_confirmed);
    m.observe(confirm,.2);CHECK(m.controller.sample(.2).visual->restart_identity==out.visual->restart_identity);
}
TEST_CASE("E25 fresh server correction can reject throw or detonation without mutating entities","[e25][satchel-local][reconciliation]") {
    SECTION("throw rejected") {
        f::Controller m;m.submit(1,.02);auto rejected=f::state(3,0,2);rejected.weapon_slots[0].auxiliary_integer1_updated_in_record=true;
        m.observe(rejected,.2);CHECK(m.controller.satchel_state().charge_ready==0);CHECK(m.controller.satchel_state().reserved_ammo==0);
        CHECK(m.controller.satchel_presentation_model()->resource_name=="models/v_satchel.mdl");
    }
    SECTION("detonation rejected") {
        f::Controller m(0,1);m.submit(1,.02,g::kReferenceGoldSrcButtonAttack2);auto rejected=f::state(0,1,2);
        rejected.weapon_slots[0].auxiliary_integer1_updated_in_record=true;m.observe(rejected,.2);
        CHECK(m.controller.satchel_state().charge_ready==1);CHECK_FALSE(m.controller.satchel_waiting_retirement());
    }
}
TEST_CASE("E25 inherited iuser1 does not undo a newer provisional mode","[e25][satchel-local][freshness]") {
    f::Controller m;m.submit(1,.02);auto retained=f::state(3,0,2);m.observe(retained,.1);
    CHECK(m.controller.satchel_state().charge_ready==1);CHECK(m.controller.satchel_state().throws==1);
}
TEST_CASE("E25 duplicate commands and render replay keep action identities and counts","[e25][satchel-local][replay]") {
    f::Controller m;m.submit(1,.02);const auto original=m.controller.sample(.02);m.submit(1,.02);
    for(unsigned i=1;i<=20;++i)(void)m.controller.sample(.02+i*.005);
    CHECK(m.controller.satchel_state().throws==1);CHECK(m.controller.satchel_state().reserved_ammo==1);
    CHECK(m.controller.sample(.13).identity==original.identity);
    auto conflict=f::command(1,.02,0);m.controller.submit(conflict,.14);
    CHECK(m.controller.sample(.14).error==api::LocalWeaponPresentationError::conflicting_command);
}
TEST_CASE("E25 30 60 144 and 240 FPS preserve two throws and one radio action","[e25][satchel-local][fps]") {
    for(const unsigned fps:{30U,60U,144U,240U}) {
        f::Controller m;std::uint32_t seq{};
        for(unsigned tick=1;tick<=90;++tick) {
            const double at=tick*.02;m.submit(++seq,at,tick<=52 ? g::kReferenceGoldSrcButtonAttack :
                tick>=77 ? g::kReferenceGoldSrcButtonAttack2 : 0);
            const double end=at+.019;for(double render=at;render<end;render+=1.0/fps)(void)m.controller.sample(render);
        }
        CHECK(m.controller.satchel_state().throws==2);CHECK(m.controller.satchel_state().detonations==1);
    }
}
TEST_CASE("E25 missing invalid iuser1 and out of range ammo cannot authorize actions","[e25][satchel-local][unavailable]") {
    for(const int phase:{-1,3,512}) {f::Controller m(3,phase);m.submit(1,.02);CHECK(m.controller.satchel_state().throws==0);}
    f::Controller missing;auto s=f::state(3,0,2);s.weapon_slots[0].auxiliary_integer1.reset();missing.observe(s,.01);
    missing.submit(1,.02);CHECK(missing.controller.satchel_state().throws==0);
    f::Controller over(6);over.submit(1,.02);CHECK(over.controller.satchel_state().throws==0);
}
TEST_CASE("E25 missing radio resource skips prediction without a false charge or launch sound","[e25][satchel-local][resources]") {
    f::Controller m;const std::array only{f::model()};m.controller.bind_model_variants(only);m.submit(1,.02);
    CHECK(m.controller.satchel_state().throws==0);CHECK(m.controller.satchel_state().charge_ready==0);
    CHECK(m.controller.drain_audio().count==0);CHECK(m.controller.sample(.02).error==api::LocalWeaponPresentationError::unsupported_model);
}
TEST_CASE("E25 focus cancellation requires release and preserves accepted command dedup","[e25][satchel-local][focus]") {
    f::Controller m;m.submit(1,.02);m.controller.cancel_uncommitted();m.submit(2,.04);m.submit(3,.06,0);
    m.submit(4,.5);CHECK(m.controller.satchel_state().throws==1);m.submit(5,1.02);
    CHECK(m.controller.satchel_state().throws==2);
}
TEST_CASE("E25 death and respawn clear radio prediction without a local explosion","[e25][satchel-local][lifecycle]") {
    f::Module m;m.host.submit(f::command(1,.02),.02);auto dead=f::state(2,1,2);
    dead.receiving_client->health=0;dead.lifecycle.state=hlclient::client::LocalPlayerLifeState::dead;
    dead.lifecycle.deaths=1;dead.lifecycle.life_epoch=2;m.host.observe(dead,.1);
    CHECK_FALSE(m.host.sample(.1).visual);CHECK(m.host.drain_temporary_effects(.1).count==0);
    auto alive=f::state(1,0,3);alive.lifecycle.deaths=1;alive.lifecycle.life_epoch=3;
    m.host.bind_model(f::model());m.host.observe(alive,.2);m.host.submit(f::command(2,.22,0),.22);
    m.host.submit(f::command(3,.24),.24);CHECK(m.host.sample(.24).action==api::LocalWeaponAction::deploy_charge);
}
TEST_CASE("E25 normal GameClientHost model variants HUD and effects keep server ammo authority","[e25][satchel-local][game-api]") {
    f::Module m;CHECK(m.host.local_model_preparation().size()==2);m.host.submit(f::command(1,.02),.02);
    api::HudPresentationContext diagnostic_context;
    diagnostic_context.diagnostic_overlay=true; // Retained text projection is deliberately checked here, not the normal sprite HUD.
    m.host.set_hud_context(diagnostic_context);
    const auto shot=m.host.sample(.02);const auto pose=m.host.viewmodel(m.observed,f::model(),.02,shot.visual);
    CHECK(pose.status==api::ViewmodelStatus::ready);CHECK(pose.model_index==85);CHECK(pose.sequence==2);
    const auto hud=m.host.hud(m.observed,.02);CHECK(hud.weapon_name=="Satchel Charge");CHECK_FALSE(hud.clip);CHECK(hud.primary_reserve==3);
    REQUIRE_FALSE(hud.draw.texts.empty());
    CHECK(hud.draw.texts[0].text.find("CLIP")==std::string::npos);CHECK(m.host.drain_audio().count==0);
    api::LocalVisualContext context;context.now_seconds=.02;const auto visual=m.host.local_visuals(context);
    CHECK_FALSE(visual.flash);CHECK_FALSE(visual.shell);CHECK_FALSE(visual.world_impact);
    CHECK(m.host.drain_temporary_effects(.02).count==0);CHECK(m.host.drain_remote_effects(.02).count==0);
}
TEST_CASE("E25 explicit server state two preserves the actual radio fire pose without an event","[e25][satchel-local][reconciliation]") {
    f::Controller m(2,1);auto server=f::state(2,2,2);server.receiving_client->weapon_animation=3;
    server.weapon_slots[0].auxiliary_integer1_updated_in_record=true;server.weapon_slots[0].time_weapon_idle=.5;
    m.observe(server,.1);const auto fire=m.controller.sample(.1);REQUIRE(fire.visual);CHECK(fire.visual->sequence==3);
    CHECK(fire.primary_fire_starts==0);CHECK(m.controller.satchel_state().detonations==0);
    m.submit(1,.2);CHECK(m.controller.satchel_state().throws==0);
}
TEST_CASE("E25 charge model never applies a retained radio fire sequence after local idle transition","[e25][satchel-local][late-event]") {
    f::Controller m(2,1);m.submit(1,.02,g::kReferenceGoldSrcButtonAttack2);m.submit(2,.52,0);
    auto late=f::state(2,1,2);late.weapon_hud.animation_sequence=3;late.weapon_hud.animation_body=0;
    late.weapon_hud.animation_source=late.client_metadata.source;m.observe(late,.6);
    CHECK(m.controller.satchel_presentation_model()->resource_name=="models/v_satchel.mdl");
    REQUIRE(m.controller.sample(.6).visual);CHECK(m.controller.sample(.6).visual->sequence!=3);
}
TEST_CASE("E25 resource revision clears old action and waits for released input","[e25][satchel-local][resource-boundary]") {
    f::Controller m;m.submit(1,.02);std::array variants{f::model(),f::model(true)};
    for(auto& variant:variants)variant.resource_revision=3;m.controller.bind_model_variants(variants);
    CHECK_FALSE(m.controller.sample(.04).identity);CHECK(m.controller.satchel_state().reserved_ammo==0);
    auto fresh=f::state(3,0,2);m.controller.bind_model(variants[0]);m.controller.observe(fresh,.06);
    m.submit(2,.08);CHECK(m.controller.satchel_state().throws==0);m.submit(3,.1,0);m.submit(4,.12);
    CHECK(m.controller.satchel_state().throws==1);
}
TEST_CASE("E25 coalesced consume pickup and animation confirmation settle exactly one receipt","[e25][satchel-local][inventory]") {
    f::Controller m;m.submit(1,.02);m.submit(2,1.02);REQUIRE(m.controller.satchel_state().reserved_ammo==2);
    auto confirm=f::state(3,1,2);confirm.receiving_client->weapon_animation=2;confirm.weapon_slots[0].auxiliary_integer1_updated_in_record=true;
    confirm.weapon_hud.animation_source=confirm.client_metadata.source;confirm.weapon_hud.animation_sequence=2;confirm.weapon_hud.animation_body=0;
    m.observe(confirm,1.1);CHECK(m.controller.satchel_state().reserved_ammo==1);CHECK(m.observed.weapon_hud.reserve_ammo[4]==3);
    m.observe(confirm,1.2);CHECK(m.controller.satchel_state().reserved_ammo==1);
}
TEST_CASE("E25 one real ammo decrement cannot settle a second receipt through pose confirmation","[e25][satchel-local][inventory]") {
    f::Controller m;m.submit(1,.02);m.submit(2,1.02);REQUIRE(m.controller.satchel_state().reserved_ammo==2);
    auto confirm=f::state(2,1,2);confirm.receiving_client->weapon_animation=2;confirm.weapon_slots[0].auxiliary_integer1_updated_in_record=true;
    confirm.weapon_hud.animation_source=confirm.client_metadata.source;confirm.weapon_hud.animation_sequence=2;confirm.weapon_hud.animation_body=0;
    m.observe(confirm,1.1);CHECK(m.controller.satchel_state().reserved_ammo==1);
}
TEST_CASE("E25 late explicit rejection releases a completed draw reservation","[e25][satchel-local][late-confirmation]") {
    f::Controller m;m.submit(1,.02);(void)m.controller.sample(2);auto rejected=f::state(3,0,2);
    rejected.weapon_slots[0].auxiliary_integer1_updated_in_record=true;m.observe(rejected,2.1);
    CHECK(m.controller.satchel_state().reserved_ammo==0);CHECK(m.controller.satchel_state().charge_ready==0);
    m.submit(2,2.2);CHECK(m.controller.satchel_state().throws==2);
}
TEST_CASE("E25 switch away and back retains authoritative empty radio usability","[e25][satchel-local][selection]") {
    f::Controller m(0,1);auto other=f::state(0,1,2);other.weapon_hud.active_weapon_id=2;
    other.receiving_client->viewmodel_index=80;m.controller.bind_model({});m.controller.observe(other,.1);
    auto returned=f::state(0,1,3);m.observe(returned,.2);CHECK(m.controller.satchel_state().charge_ready==1);
    CHECK(m.controller.satchel_presentation_model()->resource_name=="models/v_satchel_radio.mdl");
    REQUIRE(hlclient::games::halflife::build_weapon_selection_request(returned,14));
    m.submit(1,.22,g::kReferenceGoldSrcButtonAttack2);CHECK(m.controller.satchel_state().detonations==1);
}
