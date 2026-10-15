#include "hand_grenade_test_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <limits>
#include <tuple>
namespace f=hlclient::tests::hand_grenade_fixture;
namespace api=hlclient::game_api;
namespace hl=hlclient::games::halflife;
namespace g=hlclient::goldsrc;
using Catch::Approx;

TEST_CASE("E24 exact Hand Grenade profile has no clip and verifies original sequence roles", "[e24][grenade-local][profile]") {
    CHECK(hl::hand_grenade_maximum_ammo==10);CHECK(hl::hand_grenade_default_give==5);
    CHECK(hl::hand_grenade_command("weapon_handgrenade"));CHECK_FALSE(hl::hand_grenade_command("weapon_grenade"));
    auto m=f::model();CHECK(hl::hand_grenade_sequences(m));
    m.sequences[2].label="fire";CHECK_FALSE(hl::hand_grenade_sequences(m));
    m=f::model();m.supported_bodies[0]=false;CHECK_FALSE(hl::hand_grenade_sequences(m));
    m=f::model();m.sequences[3].fps=0;CHECK_FALSE(hl::hand_grenade_sequences(m));
}
TEST_CASE("E24 normal module prime hold and throw produce viewmodel only without a firing event", "[e24][grenade-local][game-api]") {
    f::Module m;(void)m.host.drain_audio();
    m.host.submit(f::command(),.02);const auto prime=m.host.sample(.02);REQUIRE(prime.visual);REQUIRE(prime.identity);
    CHECK(prime.action==api::LocalWeaponAction::prime);CHECK(prime.visual->sequence==2);
    CHECK(prime.identity->weapon_id==12);CHECK(prime.primary_fire_starts==0);CHECK(prime.recoil_starts==0);
    auto hud=m.host.hud(m.observed,.02);CHECK_FALSE(hud.clip);CHECK(hud.primary_reserve==5);
    for(unsigned i=2;i<=100;++i) {const auto t=i*.02;m.host.submit(f::command(i,t),t);const auto p=m.host.sample(t);
        REQUIRE(p.visual);CHECK(p.visual->restart_identity==prime.visual->restart_identity);CHECK(p.actions_started==1);}
    m.host.submit(f::command(101,2.02,0),2.02);const auto thrown=m.host.sample(2.02);REQUIRE(thrown.visual);
    CHECK(thrown.action==api::LocalWeaponAction::throw_release);CHECK(thrown.visual->sequence==4);CHECK(thrown.actions_started==2);
    CHECK(m.host.drain_audio().count==0);CHECK(m.host.drain_temporary_effects(2.02).count==0);
    const auto effects=m.host.local_visuals({{}, {}, {}, {}, {}, 2.02});
    CHECK_FALSE(effects.flash);CHECK_FALSE(effects.light);CHECK_FALSE(effects.shell);CHECK_FALSE(effects.world_impact);
    CHECK(effects.extra_shell_count==0);CHECK(effects.extra_impact_count==0);CHECK(m.observed.packet_entities.empty());
    CHECK(m.host.hud(m.observed,2.02).primary_reserve==5);
}
TEST_CASE("E24 quick release waits half second from prime and freezes throw command context", "[e24][grenade-local][cook]") {
    f::Controller m;m.submit(1,.02);const auto prime=m.controller.sample(.02);REQUIRE(prime.visual);
    m.submit(2,.1,0,-90);CHECK(m.controller.hand_grenade_state().phase==hl::HandGrenadePhase::release_pending);
    CHECK(m.controller.hand_grenade_state().release_time==.1);CHECK(m.controller.hand_grenade_state().reserved_ammo==0);
    m.submit(3,.519,0);CHECK(m.controller.hand_grenade_state().throw_count==0);
    m.submit(4,.52,0);const auto state=m.controller.hand_grenade_state();REQUIRE(state.frozen_throw);
    CHECK(state.prime_command==1);CHECK(state.release_command==2);CHECK(state.throw_command==4);
    CHECK(state.throw_time==.52);CHECK(state.frozen_throw->remaining_fuse==Approx(2.5));CHECK(state.frozen_throw->sequence==4);
    CHECK(state.reserved_ammo==1);CHECK(state.throw_count==1);CHECK(state.prime_count==1);
    m.submit(5,.54,0,-90);CHECK(m.controller.hand_grenade_state().frozen_throw->sequence==4);
    CHECK(m.controller.hand_grenade_state().frozen_throw->input==state.frozen_throw->input);
    CHECK(m.observed.weapon_hud.reserve_ammo[4]==5);CHECK(m.observed.packet_entities.empty());
}
TEST_CASE("E24 cooked and overcooked release uses prime time and never expires while held", "[e24][grenade-local][cook]") {
    for(const double duration:{1.0,2.5,4.0,100.0}) {
        CAPTURE(duration);f::Controller m;m.submit(1,.02);m.submit(2,.02+duration);
        const auto held=m.controller.sample(.02+duration);REQUIRE(held.visual);CHECK(held.visual->sequence==2);
        CHECK(m.controller.hand_grenade_state().phase==hl::HandGrenadePhase::primed);
        CHECK(m.controller.hand_grenade_state().throw_count==0);CHECK(m.controller.hand_grenade_state().reserved_ammo==0);
        m.submit(3,.02+duration,0);const auto state=m.controller.hand_grenade_state();REQUIRE(state.frozen_throw);
        CHECK(state.frozen_throw->remaining_fuse==Approx(std::max(0.0,3-duration)));CHECK(state.throw_count==1);
        CHECK(m.controller.drain_audio().count==0);CHECK(m.observed.packet_entities.empty());
    }
}
TEST_CASE("E24 throw pitch punch strength and player velocity are frozen diagnostics not physics", "[e24][grenade-local][throw]") {
    for(const auto [pitch,sequence,speed]:{std::tuple{90.0F,3U,0.0},std::tuple{0.0F,4U,650.0},std::tuple{-90.0F,5U,1000.0}}) {
        f::Controller m;m.submit(1,.02);auto cmd=f::command(2,.52,0,pitch);
        cmd.shot_context->player_velocity=std::array<float,3>{120,-55,9};m.controller.submit(cmd,.52);
        const auto s=m.controller.hand_grenade_state();REQUIRE(s.frozen_throw);REQUIRE(s.frozen_throw->reference_velocity);
        CHECK(s.frozen_throw->sequence==sequence);CHECK(s.frozen_throw->speed==Approx(speed));
        CHECK((*s.frozen_throw->reference_velocity)[1]==Approx(-55));CHECK(m.observed.packet_entities.empty());
    }
    auto context=*f::command().shot_context;context.punch_angles_degrees=std::array<float,3>{45,0,0};
    REQUIRE(hl::hand_grenade_throw_context(context,.5));CHECK(hl::hand_grenade_throw_context(context,.5)->sequence==3);
    context.view_angles_degrees.reset();CHECK_FALSE(hl::hand_grenade_throw_context(context,.5));
    context=*f::command().shot_context;context.view_angles_degrees=std::array<float,3>{89,0,0};
    context.punch_angles_degrees=std::array<float,3>{2,0,0};
    const auto signed_speed=hl::hand_grenade_throw_context(context,.5);REQUIRE(signed_speed);
    CHECK(signed_speed->speed<0);CHECK(signed_speed->sequence==3); // SDK caps maximum only.
    context=*f::command().shot_context;(*context.player_velocity)[0]=std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(hl::hand_grenade_throw_context(context,.5));
}
TEST_CASE("E24 R has no reload and RMB neither primes nor releases a held grenade", "[e24][grenade-local][input]") {
    f::Controller m;m.submit(1,.02,g::kReferenceGoldSrcButtonAttack2);m.submit(2,.04,g::kReferenceGoldSrcButtonReload);
    CHECK(m.controller.sample(.04).actions_started==0);m.submit(3,.06);m.submit(4,.60,g::kReferenceGoldSrcButtonAttack2);
    CHECK(m.controller.hand_grenade_state().phase==hl::HandGrenadePhase::primed);
    CHECK(m.controller.hand_grenade_state().throw_count==0);m.submit(5,.62,g::kReferenceGoldSrcButtonReload);
    CHECK(m.controller.hand_grenade_state().throw_count==1);CHECK(m.controller.sample(.62).reload_starts==0);
}
TEST_CASE("E24 replay dedup protects both prime and release identities and reservation", "[e24][grenade-local][replay]") {
    f::Controller m;const auto first=f::command();m.controller.submit(first,.02);const auto p=m.controller.sample(.02);
    m.controller.submit(first,.04);CHECK(m.controller.sample(.04).duplicate_submissions==1);
    auto release=f::command(2,.52,0);m.controller.submit(release,.52);const auto t=m.controller.sample(.52);REQUIRE(t.visual);
    m.controller.submit(release,.54);const auto replayed=m.controller.sample(.54);REQUIRE(replayed.visual);
    CHECK(replayed.visual->restart_identity==t.visual->restart_identity);CHECK(replayed.actions_started==2);
    CHECK(m.controller.hand_grenade_state().reserved_ammo==1);CHECK(m.controller.hand_grenade_state().throw_count==1);
    release.shot_context->view_angles_degrees=std::array<float,3>{30,0,0};m.controller.submit(release,.56);
    CHECK(m.controller.sample(.56).error==api::LocalWeaponPresentationError::conflicting_command);
}
TEST_CASE("E24 command timeline including remaining fuse is independent of render FPS", "[e24][grenade-local][fps]") {
    std::optional<api::LocalWeaponActionIdentity> baseline;double reference_fuse=-1;
    for(unsigned fps:{30U,60U,144U,240U}) {
        f::Controller m;double previous=0;
        for(unsigned tick=1;tick<=100;++tick) {
            const auto t=tick*.02;
            for(double frame=(std::floor(previous*fps)+1)/fps;frame<t-1e-9;frame+=1.0/fps) (void)m.controller.sample(frame);
            m.submit(tick,t,tick<100 ? g::kReferenceGoldSrcButtonAttack : 0);(void)m.controller.sample(t);previous=t;
        }
        const auto p=m.controller.sample(2);const auto s=m.controller.hand_grenade_state();REQUIRE(p.identity);REQUIRE(s.frozen_throw);
        CHECK(s.prime_time==.02);CHECK(s.release_time==2);CHECK(s.throw_time==2);CHECK(s.throw_count==1);
        CHECK(s.frozen_throw->remaining_fuse==Approx(1.02));
        if(!baseline){baseline=p.identity;reference_fuse=s.frozen_throw->remaining_fuse;}
        else {CHECK(p.identity==baseline);CHECK(s.frozen_throw->remaining_fuse==reference_fuse);}
    }
}
TEST_CASE("E24 unchanged reconstructed zero does not erase prime but explicit confirmed clear corrects it", "[e24][grenade-local][fuser]") {
    f::Controller m;m.submit(1,.02);auto held=m.controller.sample(.02);REQUIRE(held.visual);
    auto inherited=f::state(5,2);m.controller.observe(inherited,.1);CHECK_FALSE(m.controller.hand_grenade_can_holster());
    auto primed=f::state(5,3);primed.server_time_seconds=100.1;primed.weapon_slots[0].auxiliary_float2=100.02;
    primed.weapon_slots[0].auxiliary_float2_updated_in_record=true;primed.weapon_slots[0].auxiliary_float3=0;
    primed.weapon_slots[0].auxiliary_float3_updated_in_record=true;primed.receiving_client->weapon_animation=2;
    m.controller.observe(primed,.1);CHECK(m.controller.hand_grenade_state().prime_time==.02);
    REQUIRE(m.controller.sample(.1).visual);CHECK(m.controller.sample(.1).visual->restart_identity==held.visual->restart_identity);
    auto cleared=f::state(5,4);cleared.weapon_slots[0].auxiliary_float2_updated_in_record=true;
    cleared.weapon_slots[0].auxiliary_float3_updated_in_record=true;m.controller.observe(cleared,.12);
    CHECK(m.controller.hand_grenade_can_holster());CHECK(m.controller.hand_grenade_state().phase==hl::HandGrenadePhase::ready);
    CHECK_FALSE(m.controller.hand_grenade_state().prime_time);CHECK(m.controller.hand_grenade_state().throw_count==0);
    m.submit(2,.6,0);CHECK(m.controller.hand_grenade_state().throw_count==0);
}
TEST_CASE("E24 absolute server fuser prime and release map through server clock not local epoch", "[e24][grenade-local][fuser]") {
    f::Controller m;auto s=f::state(5,2);s.server_time_seconds=1001;
    s.weapon_slots[0].auxiliary_float2=1000;s.weapon_slots[0].auxiliary_float3=1000.7;
    s.weapon_slots[0].auxiliary_float2_updated_in_record=true;s.weapon_slots[0].auxiliary_float3_updated_in_record=true;
    m.controller.observe(s,10);const auto state=m.controller.hand_grenade_state();
    CHECK(state.prime_time==Approx(9));CHECK(state.release_time==Approx(9.7));CHECK_FALSE(m.controller.hand_grenade_can_holster());
    m.submit(1,10.02,0);REQUIRE(m.controller.hand_grenade_state().frozen_throw);
    CHECK(m.controller.hand_grenade_state().frozen_throw->remaining_fuse==Approx(1.98));
}
TEST_CASE("E24 exact animation confirmation is idempotent and fresh idle can reject prime", "[e24][grenade-local][confirmation]") {
    f::Controller m;m.submit(1,.02);const auto original=m.controller.sample(.02);REQUIRE(original.visual);
    auto s=f::state(5,2);s.weapon_hud.animation_source=s.client_metadata.source;s.weapon_hud.animation_sequence=2;s.weapon_hud.animation_body=0;
    m.controller.observe(s,.04);const auto confirmed=m.controller.sample(.04);REQUIRE(confirmed.visual);
    CHECK(confirmed.visual->restart_identity==original.visual->restart_identity);CHECK(confirmed.actions_started==1);
    m.controller.observe(s,.06);CHECK(m.controller.sample(.06).actions_started==1);
    s=f::state(5,3);s.weapon_hud.animation_source=s.client_metadata.source;s.weapon_hud.animation_sequence=0;s.weapon_hud.animation_body=0;
    m.controller.observe(s,.08);CHECK(m.controller.hand_grenade_can_holster());CHECK(m.controller.hand_grenade_state().throw_count==0);
}
TEST_CASE("E24 last grenade waits for throw presentation then hides pending server retirement", "[e24][grenade-local][exhaustion]") {
    f::Controller m(1);m.submit(1,.02);CHECK_FALSE(m.controller.hand_grenade_can_holster());m.submit(2,.52,0);
    CHECK(m.controller.hand_grenade_can_holster());CHECK_FALSE(m.controller.hand_grenade_waiting_retirement());
    m.controller.observe(f::state(0,2),.6);CHECK(m.controller.hand_grenade_state().reserved_ammo==0);
    m.submit(3,1.019,0);CHECK_FALSE(m.controller.hand_grenade_waiting_retirement());m.submit(4,1.02,0);
    CHECK(m.controller.hand_grenade_waiting_retirement());CHECK_FALSE(m.controller.sample(1.02).visual);
    m.submit(5,2);CHECK(m.controller.hand_grenade_state().prime_count==1);
    m.controller.observe(f::state(5,3),2.02);CHECK_FALSE(m.controller.hand_grenade_waiting_retirement());
    m.submit(6,2.04);CHECK(m.controller.hand_grenade_state().prime_count==2);
}
TEST_CASE("E24 absent server updates cannot allocate more provisional throws than observed ammo", "[e24][grenade-local][bounds]") {
    f::Controller m(3);unsigned seq=0;
    for(unsigned i=0;i<3;++i){const auto base=.02+i*1.02;m.submit(++seq,base);m.submit(++seq,base+.5,0);m.submit(++seq,base+1,0);}
    CHECK(m.controller.hand_grenade_state().throw_count==3);CHECK(m.controller.hand_grenade_state().reserved_ammo==3);
    CHECK(m.controller.hand_grenade_waiting_retirement());m.submit(++seq,4);CHECK(m.controller.hand_grenade_state().prime_count==3);
    CHECK(m.observed.weapon_hud.reserve_ammo[4]==3);
}
TEST_CASE("E24 death focus reset and unsupported context never synthesize a delayed grenade", "[e24][grenade-local][lifecycle]") {
    for(bool focus:{false,true}) {
        f::Controller m;m.submit(1,.02);
        if(focus)m.controller.cancel_uncommitted();else {auto s=f::state(5,2);s.receiving_client->health=0;m.controller.observe(s,.04);}
        m.submit(2,1,0);CHECK(m.controller.hand_grenade_state().throw_count==0);CHECK(m.controller.drain_audio().count==0);
        CHECK(m.observed.packet_entities.empty());
    }
    f::Controller m;m.submit(1,.02);auto missing=f::command(2,.52,0);missing.shot_context.reset();m.controller.submit(missing,.52);
    CHECK(m.controller.sample(.52).error==api::LocalWeaponPresentationError::unsupported_pose);
    CHECK(m.controller.hand_grenade_state().throw_count==0);CHECK(m.controller.hand_grenade_state().reserved_ammo==0);
}
TEST_CASE("E24 empty missing bad ammo or model cannot authorize prime", "[e24][grenade-local][bounds]") {
    for(unsigned variant=0;variant<6;++variant) {
        f::Controller m;auto s=f::state(5,2);auto model=f::model();
        if(variant==0)s.weapon_hud.reserve_ammo[4]=0;
        if(variant==1)s.weapon_hud.reserve_ammo[4]=11;
        if(variant==2)s.weapon_hud.reserve_ammo[4].reset();
        if(variant==3)s.weapon_slots[0].clip=1;
        if(variant==4)s.weapon_hud.catalogue[0].command_name="weapon_other";
        if(variant==5)model.sequences[2].label="fire";
        m.controller.bind_model(model);m.controller.observe(s,0);m.submit(1,.02);
        CHECK(m.controller.hand_grenade_state().prime_count==0);CHECK(m.controller.sample(.02).actions_started==0);
    }
}
TEST_CASE("E24 module blocks group wheel and direct holster while primed and restores after throw", "[e24][grenade-local][game-api][inventory]") {
    f::Module m;REQUIRE(m.host.inventory_selection(m.observed,2));
    CHECK(m.host.select_group(m.observed,5,2)==12);CHECK(m.host.cycle_inventory(m.observed,2,1)==12);
    m.host.submit(f::command(),.02);(void)m.host.sample(.02);
    CHECK_FALSE(m.host.inventory_selection(m.observed,2));CHECK_FALSE(m.host.select_group(m.observed,2,12));
    CHECK_FALSE(m.host.cycle_inventory(m.observed,12,1));
    m.host.submit(f::command(2,.52,0),.52);(void)m.host.sample(.52);
    CHECK(m.host.inventory_selection(m.observed,2));CHECK(m.host.select_group(m.observed,2,12)==2);
    auto zero=f::state(0,2);m.host.observe(zero,.6);
    CHECK_FALSE(m.host.inventory_selection(zero,12));CHECK_FALSE(m.host.select_group(zero,5,2));
    auto pickup=f::state(5,3);m.host.observe(pickup,.62);CHECK(m.host.inventory_selection(pickup,12));
    CHECK(m.host.hud(pickup,.62).primary_reserve==5);CHECK_FALSE(m.host.hud(pickup,.62).clip);
}
TEST_CASE("E24 changed fuser origin corrects once without restarting and late prime cannot resurrect throw", "[e24][grenade-local][fuser]") {
    f::Controller m;m.submit(1,.02);auto s=f::state(5,2);s.server_time_seconds=100.1;
    s.weapon_slots[0].auxiliary_float2=100.02;s.weapon_slots[0].auxiliary_float3=0;
    s.weapon_slots[0].auxiliary_float2_updated_in_record=true;m.controller.observe(s,.1);
    const auto original=m.controller.sample(.1);REQUIRE(original.visual);
    s=f::state(5,3);s.server_time_seconds=100.2;s.weapon_slots[0].auxiliary_float2=100.1;
    s.weapon_slots[0].auxiliary_float3=0;s.weapon_slots[0].auxiliary_float2_updated_in_record=true;
    m.controller.observe(s,.2);CHECK(m.controller.hand_grenade_state().prime_time==Approx(.1));
    const auto corrected=m.controller.sample(.2);REQUIRE(corrected.visual);
    CHECK(corrected.visual->restart_identity==original.visual->restart_identity);CHECK(corrected.actions_started==1);
    m.submit(2,.6,0);CHECK(m.controller.hand_grenade_state().throw_count==1);
    s.publication_revision=4;s.client_metadata.source->record_identity=4;s.client_metadata.source->record_ordinal=4;
    s.server_time_seconds=100.6;m.controller.observe(s,.62);
    CHECK(m.controller.hand_grenade_state().phase==hl::HandGrenadePhase::throwing);
    CHECK(m.controller.hand_grenade_state().reserved_ammo==1);CHECK(m.controller.sample(.62).actions_started==2);
}
TEST_CASE("E24 focus cancellation requires a released input before any new prime", "[e24][grenade-local][lifecycle]") {
    f::Controller m;m.submit(1,.02);m.controller.cancel_uncommitted();
    m.submit(2,.2);m.submit(3,.8);CHECK(m.controller.hand_grenade_state().prime_count==0);
    m.submit(4,.82,0);CHECK(m.controller.hand_grenade_state().throw_count==0);
    m.submit(5,.84);CHECK(m.controller.hand_grenade_state().prime_count==1);
}
TEST_CASE("E24 animation-only committed record corrects pose without fresh clientdata", "[e24][grenade-local][confirmation]") {
    f::Controller m;m.submit(1,.02);m.submit(2,.52,0);const auto predicted=m.controller.sample(.52);REQUIRE(predicted.visual);
    auto s=f::state(5,2);s.client_metadata.freshness=hlclient::client::RuntimeObservationFreshness::retained;
    s.client_metadata.source=f::state().client_metadata.source;s.weapon_hud.animation_source=s.weapon_hud.last_message_source;
    s.weapon_hud.animation_sequence=5;s.weapon_hud.animation_body=0;m.controller.observe(s,.54);
    const auto corrected=m.controller.sample(.54);REQUIRE(corrected.visual);
    CHECK(corrected.visual->sequence==5);CHECK(corrected.visual->source==api::LocalWeaponAnimationSource::service_event);
    CHECK(corrected.actions_started==2);m.controller.observe(s,.56);
    CHECK(m.controller.sample(.56).visual->restart_identity==corrected.visual->restart_identity);
}
TEST_CASE("E24 changed clientdata throw pose corrects local strength but unchanged idle is retained", "[e24][grenade-local][confirmation]") {
    f::Controller m;m.submit(1,.02);m.submit(2,.52,0);
    m.controller.observe(f::state(5,2),.54);CHECK(m.controller.sample(.54).visual->sequence==4);
    auto s=f::state(5,3);s.receiving_client->weapon_animation=3;m.controller.observe(s,.56);
    const auto corrected=m.controller.sample(.56);REQUIRE(corrected.visual);CHECK(corrected.visual->sequence==3);
    s=f::state(5,4);s.receiving_client->weapon_animation=3;m.controller.observe(s,.58);
    CHECK(m.controller.sample(.58).visual->restart_identity==corrected.visual->restart_identity);
}
TEST_CASE("E24 rejected final throw can recover from fresh explicit clear without a new AmmoX", "[e24][grenade-local][exhaustion]") {
    f::Controller m(1);m.submit(1,.02);m.submit(2,.52,0);m.submit(3,1.02,0);
    REQUIRE(m.controller.hand_grenade_waiting_retirement());
    auto same=f::state(1,2);m.controller.observe(same,1.04);CHECK(m.controller.hand_grenade_waiting_retirement());
    auto rejected=f::state(1,3);rejected.weapon_hud.reserve_ammo_sources[4]=same.weapon_hud.reserve_ammo_sources[4];
    rejected.weapon_slots[0].auxiliary_float2_updated_in_record=true;
    m.controller.observe(rejected,1.3);CHECK_FALSE(m.controller.hand_grenade_waiting_retirement());
    CHECK(m.controller.hand_grenade_state().reserved_ammo==0);REQUIRE(m.controller.sample(1.3).visual);
    m.submit(4,1.32,0);m.submit(5,1.34);CHECK(m.controller.hand_grenade_state().prime_count==2);
}
TEST_CASE("E24 confirmed server prime survives focus loss but does not auto throw while cancelled", "[e24][grenade-local][lifecycle]") {
    f::Controller m;m.submit(1,.02);auto s=f::state(5,2);s.server_time_seconds=100.1;
    s.weapon_slots[0].auxiliary_float2=100.02;s.weapon_slots[0].auxiliary_float2_updated_in_record=true;
    m.controller.observe(s,.1);m.controller.cancel_uncommitted();CHECK_FALSE(m.controller.hand_grenade_can_holster());
    m.submit(2,.6);CHECK(m.controller.hand_grenade_state().throw_count==0);
    m.submit(3,.62,0);CHECK(m.controller.hand_grenade_state().throw_count==0);
    m.submit(4,.64,0);CHECK(m.controller.hand_grenade_state().throw_count==1);
}
TEST_CASE("E24 delayed dispatch preserves command prime throw times and zero timer stays ready", "[e24][grenade-local][fps]") {
    f::Controller m;m.controller.submit(f::command(1,.02),.2);
    auto fresh=f::state(5,2);fresh.weapon_slots[0].time_weapon_idle=0;m.controller.observe(fresh,.9);
    m.controller.submit(f::command(2,.52,0),1.0);const auto state=m.controller.hand_grenade_state();
    CHECK(state.prime_time==.02);CHECK(state.throw_time==.52);REQUIRE(state.frozen_throw);
    CHECK(state.frozen_throw->remaining_fuse==Approx(2.5));CHECK(state.throw_count==1);
}
TEST_CASE("E24 focus loss after throw retains tombstone against late server prime", "[e24][grenade-local][lifecycle][fuser]") {
    f::Controller m;m.submit(1,.02);m.submit(2,.52,0);m.controller.cancel_uncommitted();
    auto late=f::state(5,2);late.server_time_seconds=100.6;
    late.weapon_slots[0].auxiliary_float2=100.02;late.weapon_slots[0].auxiliary_float3=0;
    late.weapon_slots[0].auxiliary_float2_updated_in_record=true;
    late.weapon_slots[0].auxiliary_float3_updated_in_record=true;
    m.controller.observe(late,.6);m.submit(3,.62,0);m.submit(4,.64,0);
    CHECK(m.controller.hand_grenade_state().phase==hl::HandGrenadePhase::throwing);
    CHECK(m.controller.hand_grenade_state().prime_count==1);CHECK(m.controller.hand_grenade_state().throw_count==1);
    CHECK(m.controller.hand_grenade_state().throw_time==.52);CHECK(m.controller.hand_grenade_state().reserved_ammo==1);
    CHECK(m.controller.sample(.64).actions_started==2);CHECK(m.controller.drain_audio().count==0);
}
TEST_CASE("E24 first explicit server prime corrects predicted start without another pinpull", "[e24][grenade-local][fuser]") {
    f::Controller m;m.submit(1,.02);const auto predicted=m.controller.sample(.02);REQUIRE(predicted.visual);
    auto s=f::state(5,2);s.server_time_seconds=100.1;
    s.weapon_slots[0].auxiliary_float2=100.05;s.weapon_slots[0].auxiliary_float3=0;
    s.weapon_slots[0].auxiliary_float2_updated_in_record=true;
    m.controller.observe(s,.1);CHECK(m.controller.hand_grenade_state().prime_time==Approx(.05));
    const auto corrected=m.controller.sample(.1);REQUIRE(corrected.visual);
    CHECK(corrected.visual->restart_identity==predicted.visual->restart_identity);CHECK(corrected.actions_started==1);
    m.submit(2,.52,0);CHECK(m.controller.hand_grenade_state().throw_count==0);
    m.submit(3,.55,0);CHECK(m.controller.hand_grenade_state().throw_count==1);
    REQUIRE(m.controller.hand_grenade_state().frozen_throw);
    CHECK(m.controller.hand_grenade_state().frozen_throw->remaining_fuse==Approx(2.5));
}
TEST_CASE("E24 coalesced consumption pickup count and real throw pose settle one reservation", "[e24][grenade-local][exhaustion][confirmation]") {
    for(bool service:{false,true}) {
        f::Controller m(1);m.submit(1,.02);m.submit(2,.52,0);
        auto s=f::state(1,2);s.weapon_slots[0].auxiliary_float2_updated_in_record=true;
        if(service) {
            s.client_metadata.freshness=hlclient::client::RuntimeObservationFreshness::retained;
            s.client_metadata.source=f::state().client_metadata.source;
            s.weapon_hud.animation_source=s.weapon_hud.last_message_source;
            s.weapon_hud.animation_sequence=4;s.weapon_hud.animation_body=0;
        } else s.receiving_client->weapon_animation=4;
        m.controller.observe(s,.54);CHECK(m.controller.hand_grenade_state().reserved_ammo==0);
        m.controller.observe(s,.56);CHECK(m.controller.hand_grenade_state().reserved_ammo==0);
        m.submit(3,1.02,0);CHECK_FALSE(m.controller.hand_grenade_waiting_retirement());
        m.submit(4,1.04);CHECK(m.controller.hand_grenade_state().prime_count==2);
        CHECK(m.observed.weapon_hud.reserve_ammo[4]==1);
    }
}
TEST_CASE("E24 one server ammo decrement cannot also settle a second reservation by pose", "[e24][grenade-local][ammo][confirmation]") {
    f::Controller m(3);m.submit(1,.02);m.submit(2,.52,0);m.submit(3,1.02,0);
    m.submit(4,1.04);m.submit(5,1.54,0);REQUIRE(m.controller.hand_grenade_state().reserved_ammo==2);
    auto s=f::state(2,2);s.receiving_client->weapon_animation=4;
    s.weapon_slots[0].auxiliary_float2_updated_in_record=true;
    m.controller.observe(s,1.56);CHECK(m.controller.hand_grenade_state().reserved_ammo==1);
    m.controller.observe(s,1.58);CHECK(m.controller.hand_grenade_state().reserved_ammo==1);
    CHECK(m.controller.hand_grenade_state().throw_count==2);
}
TEST_CASE("E24 authoritative clientdata throw pose survives clearing prime before local minimum", "[e24][grenade-local][fuser][confirmation]") {
    for(unsigned sequence:{3U,4U,5U}) {
        f::Controller m;m.submit(1,.02);auto prime=f::state(5,2);prime.server_time_seconds=100.1;
        prime.weapon_slots[0].auxiliary_float2=100.02;prime.weapon_slots[0].auxiliary_float2_updated_in_record=true;
        prime.receiving_client->weapon_animation=2;m.controller.observe(prime,.1);m.submit(2,.1,0);
        auto thrown=f::state(4,3);thrown.weapon_slots[0].auxiliary_float2_updated_in_record=true;
        thrown.receiving_client->weapon_animation=sequence;CHECK_FALSE(thrown.weapon_hud.animation_source);
        m.controller.observe(thrown,.5);const auto pose=m.controller.sample(.5);REQUIRE(pose.visual);
        CHECK(pose.visual->sequence==sequence);CHECK(pose.visual->source==api::LocalWeaponAnimationSource::confirmed_weapon_state);
        CHECK(m.controller.hand_grenade_can_holster());CHECK(m.controller.hand_grenade_state().throw_count==0);
        CHECK(m.controller.hand_grenade_state().reserved_ammo==0);CHECK(pose.actions_started==1);
        CHECK(m.observed.packet_entities.empty());CHECK(m.controller.drain_audio().count==0);
        m.controller.observe(thrown,.51);CHECK(m.controller.sample(.51).visual->restart_identity==pose.visual->restart_identity);
    }
}
TEST_CASE("E24 rejecting a new prime cannot settle an older outstanding throw reservation", "[e24][grenade-local][ammo][confirmation]") {
    for(bool service:{false,true}) {
        f::Controller m(3);m.submit(1,.02);m.submit(2,.52,0);m.submit(3,1.02,0);m.submit(4,1.04);
        REQUIRE(m.controller.hand_grenade_state().reserved_ammo==1);
        if(service) {
            auto s=f::state(3,2);s.client_metadata.freshness=hlclient::client::RuntimeObservationFreshness::retained;
            s.client_metadata.source=f::state().client_metadata.source;s.weapon_hud.animation_source=s.weapon_hud.last_message_source;
            s.weapon_hud.animation_sequence=0;s.weapon_hud.animation_body=0;m.controller.observe(s,1.1);
        } else {
            auto prime=f::state(3,2);prime.server_time_seconds=101.1;prime.weapon_slots[0].auxiliary_float2=101.04;
            prime.weapon_slots[0].auxiliary_float2_updated_in_record=true;prime.receiving_client->weapon_animation=2;
            m.controller.observe(prime,1.1);
            auto clear=f::state(3,3);clear.weapon_slots[0].auxiliary_float2_updated_in_record=true;m.controller.observe(clear,1.2);
        }
        CHECK(m.controller.hand_grenade_can_holster());CHECK(m.controller.hand_grenade_state().reserved_ammo==1);
        CHECK(m.controller.hand_grenade_state().throw_count==1);CHECK(m.observed.weapon_hud.reserve_ammo[4]==3);
    }
}
