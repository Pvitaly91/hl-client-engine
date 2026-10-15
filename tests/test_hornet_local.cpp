#include "hornet_test_fixture.hpp"
#include <hlclient/games/halflife/hornet_profile.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <set>
#include <tuple>
namespace f=hlclient::tests::hornet_fixture;
namespace api=hlclient::game_api;
namespace hl=hlclient::games::halflife;
namespace g=hlclient::goldsrc;
using Catch::Approx;

TEST_CASE("E23 local TRACK and FAST share shoot role audio family and small positive punch", "[e23][hornet-local][game-api][weapon-audio]") {
    for(const bool fast:{false,true}) {
        f::Module m;auto prepared=m.host.drain_audio();std::set<std::string> samples;
        for(std::size_t i=0;i<prepared.prepare_count;++i)
            if(prepared.prepare[i].name().starts_with("agrunt/")) samples.emplace(prepared.prepare[i].name());
        CHECK(samples==std::set<std::string>{"agrunt/ag_fire1.wav","agrunt/ag_fire2.wav","agrunt/ag_fire3.wav"});
        m.host.submit(f::command(1,.02,fast ? g::kReferenceGoldSrcButtonAttack2 : g::kReferenceGoldSrcButtonAttack),.02);
        const auto p=m.host.sample(.02);REQUIRE(p.identity);REQUIRE(p.visual);
        CHECK(p.identity->weapon_id==11);CHECK(p.identity->kind==(fast ? api::LocalWeaponAction::secondary_fire : api::LocalWeaponAction::primary_fire));
        CHECK(p.visual->sequence==5);CHECK(p.visual->body==1);CHECK(p.local_punch_pitch_degrees==hl::hornet_visual_pitch(*p.identity));
        CHECK(p.local_punch_pitch_degrees>=0);CHECK(p.local_punch_pitch_degrees<=2);
        const auto audio=m.host.drain_audio();REQUIRE(audio.count==1);
        CHECK(audio.cues[0].reference.name()==hl::hornet_launch_sound(*p.identity));
        CHECK(audio.cues[0].reference.source==api::SoundReferenceSource::module_profile_dependency);
        CHECK(audio.cues[0].pitch==1);CHECK(audio.cues[0].volume==1);
        const auto visual=m.host.local_visuals({{}, {}, {}, {}, {}, .02});
        CHECK_FALSE(visual.flash);CHECK_FALSE(visual.light);CHECK_FALSE(visual.shell);CHECK_FALSE(visual.world_impact);
        CHECK(visual.extra_shell_count==0);CHECK(visual.extra_impact_count==0);
        CHECK(m.host.drain_temporary_effects(.02).count==0);CHECK(m.observed.packet_entities.empty());
        const auto hud=m.host.hud(m.observed,.02);CHECK_FALSE(hud.clip);CHECK(hud.primary_reserve==8);
        const auto vm=m.host.viewmodel(m.observed,f::model(),.02,p.visual);
        CHECK(vm.status==api::ViewmodelStatus::ready);CHECK(vm.sequence==5);CHECK(vm.body==1);
        m.host.submit(f::command(1,.02,fast ? g::kReferenceGoldSrcButtonAttack2 : g::kReferenceGoldSrcButtonAttack),.02);
        CHECK(m.host.sample(.02).actions_started==1);CHECK(m.host.drain_audio().count==0);
    }
}
TEST_CASE("E23 no-clip ammo authority rejects empty malformed or unsupported fire and ignores R", "[e23][hornet-local][bounds]") {
    for(unsigned variant=0;variant<9;++variant) {
        CAPTURE(variant);f::Module m;auto model=f::model();
        if(variant==0)m.observed.weapon_hud.reserve_ammo[2]=0;
        if(variant==1)m.observed.weapon_hud.reserve_ammo[2]=9;
        if(variant==2)m.observed.weapon_hud.reserve_ammo[2].reset();
        if(variant==3)m.observed.weapon_slots[0].clip=1;
        if(variant==4)m.observed.weapon_hud.catalogue[0].command_name="weapon_other";
        if(variant==5)model.sequences[5].label="fire";
        if(variant==6)model.supported_bodies[1]=false;
        if(variant==7)m.observed.receiving_client->health=0;
        m.observed.publication_revision=2;
        m.observed.client_metadata.source=hlclient::client::RuntimeObservationSource{.record_identity=2,.record_ordinal=2};
        m.observed.weapon_hud.last_message_source=m.observed.client_metadata.source;
        m.host.bind_model(model);m.host.observe(m.observed,0);(void)m.host.drain_audio();
        m.host.submit(f::command(1,.02,variant==8 ? g::kReferenceGoldSrcButtonReload : g::kReferenceGoldSrcButtonAttack),.02);
        auto result=m.host.sample(.02);CHECK(result.actions_started==0);CHECK(result.reload_starts==0);
        CHECK(m.host.drain_audio().count==0);
    }
}
TEST_CASE("E23 primary 250ms and fast 100ms cooldowns preserve separate and shared timer rules", "[e23][hornet-local][cooldown]") {
    for(bool fast:{false,true}) {
        f::Module m;const auto button=fast ? g::kReferenceGoldSrcButtonAttack2 : g::kReferenceGoldSrcButtonAttack;
        m.host.submit(f::command(1,.02,button),.02);(void)m.host.sample(.02);
        m.ammo(7,2,.03);const auto deadline=fast ? .12 : .27;
        m.host.submit(f::command(2,deadline-.001,button),deadline-.001);CHECK(m.host.sample(deadline-.001).actions_started==1);
        m.host.submit(f::command(3,deadline,button),deadline);CHECK(m.host.sample(deadline).actions_started==2);
    }
    f::Module primary;primary.host.submit(f::command(),.02);(void)primary.host.sample(.02);
    primary.host.submit(f::command(2,.04,g::kReferenceGoldSrcButtonAttack2),.04);
    CHECK(primary.host.sample(.04).actions_started==2); // TRACK did not set secondary timer.
    f::Module fast;fast.host.submit(f::command(1,.02,g::kReferenceGoldSrcButtonAttack2),.02);(void)fast.host.sample(.02);
    fast.host.submit(f::command(2,.119),.119);CHECK(fast.host.sample(.119).actions_started==1);
    fast.host.submit(f::command(3,.12),.12);CHECK(fast.host.sample(.12).actions_started==2);
}
TEST_CASE("E23 last Hornet adds exact MP SP primary or fixed fast empty cooldown without local recharge", "[e23][hornet-local][cooldown][ammo]") {
    for(unsigned clients:{1U,8U}) for(bool fast:{false,true}) {
        CAPTURE(clients,fast);f::Module m(1,clients);
        const auto button=fast ? g::kReferenceGoldSrcButtonAttack2 : g::kReferenceGoldSrcButtonAttack;
        m.host.submit(f::command(1,.02,button),.02);(void)m.host.sample(.02);m.ammo(0,2,.03);
        CHECK(m.host.hud(m.observed,.03).primary_reserve==0);
        m.host.submit(f::command(2,.2,button),.2);CHECK(m.host.sample(.2).actions_started==1);
        m.ammo(1,3,.3);const double deadline=.02+(fast ? .6 : clients>1 ? .55 : .75);
        m.host.submit(f::command(3,deadline-.001,button),deadline-.001);CHECK(m.host.sample(deadline-.001).actions_started==1);
        m.host.submit(f::command(4,deadline,button),deadline);CHECK(m.host.sample(deadline).actions_started==2);
        CHECK(m.host.hud(m.observed,deadline).primary_reserve==1);CHECK(m.observed.weapon_slots[0].clip==-1);
    }
}
TEST_CASE("E23 rapid FAST reservations and fresh server catch-up remain bounded and server owned", "[e23][hornet-local][ammo]") {
    f::Module m;
    for(unsigned i=0;i<8;++i) {
        const auto t=.02+i*.1;m.host.submit(f::command(i+1,t,g::kReferenceGoldSrcButtonAttack2),t);
        CHECK(m.host.sample(t).actions_started==i+1);CHECK(m.host.hud(m.observed,t).primary_reserve==8);
    }
    m.ammo(0,2,.73);CHECK(m.host.sample(.73).actions_confirmed==8);
    for(unsigned i=9;i<=20;++i) {
        const auto t=.73+(i-8)*.1;m.host.submit(f::command(i,t,g::kReferenceGoldSrcButtonAttack2),t);
        CHECK(m.host.sample(t).actions_started==8);CHECK(m.host.hud(m.observed,t).primary_reserve==0);
    }
    for(const auto [count,record,time]:{std::tuple{2U,3U,2.0},std::tuple{5U,4U,2.9},std::tuple{8U,5U,3.8},std::tuple{3U,6U,3.9}}) {
        m.ammo(count,record,time);CHECK(m.host.hud(m.observed,time).primary_reserve==static_cast<int>(count));
        CHECK(m.host.sample(time).actions_started==8);
    }
    m.host.submit(f::command(21,4),4);CHECK(m.host.sample(4).actions_started==9);
}
TEST_CASE("E23 late server echo after ammo and animation completion never repeats Hornet presentation", "[e23][hornet-local][echo]") {
    for(bool fast:{false,true}) {
        f::Module m;(void)m.host.drain_audio();m.host.submit(f::command(1,.02,
            fast ? g::kReferenceGoldSrcButtonAttack2 : g::kReferenceGoldSrcButtonAttack),.02);
        const auto shot=m.host.sample(.02);REQUIRE(shot.visual);CHECK(m.host.drain_audio().count==1);
        m.ammo(7,2,.03);CHECK(m.host.sample(.03).actions_confirmed==1);
        const auto done=m.host.sample(.4);REQUIRE(done.visual);CHECK(done.visual->sequence==0);
        auto e=f::event(fast ? 1 : 0,1,3,.42);m.host.committed_scripted_event(e);m.host.committed_scripted_event(e);
        const auto echoed=m.host.sample(.42);REQUIRE(echoed.visual);CHECK(echoed.visual->restart_identity==done.visual->restart_identity);
        CHECK(echoed.actions_started==1);CHECK(echoed.actions_confirmed==1);CHECK(echoed.duplicate_events==1);
        CHECK(echoed.local_punch_pitch_degrees==0);CHECK(m.host.drain_audio().count==0);
    }
}
TEST_CASE("E23 rapid fast echoes match separate receipts without replacing newer pose", "[e23][hornet-local][echo]") {
    f::Module m;(void)m.host.drain_audio();
    m.host.submit(f::command(1,.02,g::kReferenceGoldSrcButtonAttack2),.02);(void)m.host.sample(.02);
    m.host.submit(f::command(2,.12,g::kReferenceGoldSrcButtonAttack2),.12);const auto latest=m.host.sample(.12);
    REQUIRE(latest.visual);CHECK(m.host.drain_audio().count==2);
    m.host.committed_scripted_event(f::event(1,1,2,.14));m.host.committed_scripted_event(f::event(1,1,3,.15));
    const auto result=m.host.sample(.15);REQUIRE(result.visual);
    CHECK(result.actions_started==2);CHECK(result.actions_confirmed==2);CHECK(result.visual->restart_identity==latest.visual->restart_identity);
    CHECK(m.host.drain_audio().count==0);m.ammo(6,4,.16);CHECK(m.host.sample(.16).actions_confirmed==2);
}
TEST_CASE("E23 event binding is assigned strict and cannot invent fire for unsupported mode", "[e23][hornet-local][event]") {
    for(int mode:{-1,0,1,2}) {
        CAPTURE(mode);f::Module m;(void)m.host.drain_audio();auto e=f::event(mode);
        const std::array bindings{f::binding(707)};m.host.configure_scripted_events(bindings);
        m.host.committed_scripted_event(e);CHECK(m.host.sample(.03).actions_started==0);
        e.event_index=707;e.received_at_seconds=.04;m.host.committed_scripted_event(e);
        auto p=m.host.sample(.04);CHECK(p.actions_started==((mode==0 || mode==1) ? 1U : 0U));
        if(mode==0 || mode==1) {REQUIRE(p.visual);CHECK(p.visual->sequence==5);CHECK(p.actions_confirmed==1);}
        else CHECK(p.error==api::LocalWeaponPresentationError::unsupported_event);
    }
    f::Module absent;auto e=f::event();e.integer_parameter1.reset();absent.host.committed_scripted_event(e);
    CHECK(absent.host.sample(.03).actions_started==0);
}
TEST_CASE("E23 reserve regeneration while holstered is received not locally granted or selected away", "[e23][hornet-local][lifecycle]") {
    f::Module m(0);m.observed.weapon_hud.catalogue.push_back({.id=1,.command_name="weapon_crowbar"});
    m.observed.receiving_client->owned_weapon_bits=(1U<<1)|(1U<<11);m.host.observe(m.observed,0);
    REQUIRE(m.host.inventory_selection(m.observed,11));CHECK(m.observed.weapon_hud.active_weapon_id==11);
    m.host.submit(f::command(1,.02,g::kReferenceGoldSrcButtonReload),.02);
    CHECK(m.host.sample(.02).reload_starts==0);CHECK(m.host.hud(m.observed,2).primary_reserve==0);
    auto holstered=m.observed;holstered.publication_revision=2;holstered.weapon_hud.active_weapon_id=1;
    holstered.receiving_client->viewmodel_index=90;m.host.observe(holstered,2);
    m.ammo(5,3,3);CHECK(m.host.hud(m.observed,3).primary_reserve==5);
    m.host.submit(f::command(2,3.02),3.02);CHECK(m.host.sample(3.02).actions_started==1);
    auto dead=f::state(4,4);dead.receiving_client->health=0;m.host.observe(dead,3.03);
    CHECK_FALSE(m.host.sample(3.03).visual);CHECK(m.host.drain_audio().count==0);
}
TEST_CASE("E23 command and server-ammo timeline is invariant across render rates", "[e23][hornet-local][fps]") {
    std::vector<api::LocalWeaponActionIdentity> baseline;
    for(unsigned fps:{30U,60U,144U,240U}) {
        f::Module m;std::vector<api::LocalWeaponActionIdentity> actions;
        unsigned command=0,record=1;double previous=0;
        for(unsigned tick=1;tick<=50;++tick) {
            const double now=tick*.02;
            for(double frame=(std::floor(previous*fps)+1)/fps;frame<now-1e-9;frame+=1.0/fps) (void)m.host.sample(frame);
            if(tick%5==1 && tick<=36) {
                m.host.submit(f::command(++command,now,g::kReferenceGoldSrcButtonAttack2),now);
                const auto action=m.host.sample(now);REQUIRE(action.identity);actions.push_back(*action.identity);
                m.ammo(8-static_cast<unsigned>(actions.size()),++record,now);
            } else (void)m.host.sample(now);
            previous=now;
        }
        CHECK(actions.size()==8);CHECK(m.host.hud(m.observed,1).primary_reserve==0);
        if(baseline.empty()) baseline=actions;else CHECK(actions==baseline);
    }
}
TEST_CASE("E23 coalesced recharge absolute count releases only confirmed sampled reservation", "[e23][hornet-local][ammo][echo]") {
    for(bool same_record:{false,true}) {
        f::Module m(1);m.host.submit(f::command(),.02);(void)m.host.sample(.02);
        auto echo=f::event(0,1,2,.04);m.host.committed_scripted_event(echo);
        auto update=f::state(1,same_record ? 2U : 3U);
        update.weapon_hud.reserve_ammo_sources[2]->start_bit_offset=40;
        m.host.observe(update,.32);m.host.submit(f::command(2,.57),.57);
        CHECK(m.host.sample(.57).actions_started==2);CHECK(m.host.hud(update,.57).primary_reserve==1);
    }
    // An unrelated newer client record retaining an OLD AmmoX cannot free it.
    f::Module retained(1);retained.host.submit(f::command(),.02);(void)retained.host.sample(.02);
    retained.host.committed_scripted_event(f::event(0,1,2,.04));
    auto update=f::state(1,3);update.weapon_hud.reserve_ammo_sources[2]=f::state(1).weapon_hud.reserve_ammo_sources[2];
    retained.host.observe(update,.32);retained.host.submit(f::command(2,.57),.57);
    CHECK(retained.host.sample(.57).actions_started==1);
    // Echo for action1 + absolute count1 must NOT release still-unsampled action2.
    f::Module pending(2);pending.host.submit(f::command(1,.02,g::kReferenceGoldSrcButtonAttack2),.02);
    (void)pending.host.sample(.02);pending.host.committed_scripted_event(f::event(1,1,2,.04));
    pending.host.submit(f::command(2,.12,g::kReferenceGoldSrcButtonAttack2),.12);(void)pending.host.sample(.12);
    pending.ammo(1,3,.13);pending.host.submit(f::command(3,.73,g::kReferenceGoldSrcButtonAttack2),.73);
    CHECK(pending.host.sample(.73).actions_started==2);
}
TEST_CASE("E23 focus cancellation retains fired Hornet echo tombstone but clears transient action", "[e23][hornet-local][lifecycle][echo]") {
    f::Module m;(void)m.host.drain_audio();m.host.submit(f::command(),.02);
    const auto fired=m.host.sample(.02);REQUIRE(fired.identity);CHECK(m.host.drain_audio().count==1);
    m.host.cancel_uncommitted();m.host.committed_scripted_event(f::event(0,1,2,.1));
    const auto cancelled=m.host.sample(.1);CHECK(cancelled.actions_started==1);
    CHECK_FALSE(cancelled.identity);CHECK(cancelled.local_punch_pitch_degrees==0);CHECK(m.host.drain_audio().count==0);
    m.host.submit(f::command(2,.2),.2);CHECK(m.host.sample(.2).actions_started==1);
    m.host.submit(f::command(3,.27),.27);CHECK(m.host.sample(.27).actions_started==2);
}
TEST_CASE("E23 committed-only fire does not infer last-round cooldown from post-shot reserve", "[e23][hornet-local][cooldown][event]") {
    for(bool fast:{false,true}) {
        f::Module m(1);m.host.committed_scripted_event(f::event(fast ? 1 : 0));
        CHECK(m.host.sample(.03).actions_started==1);
        const auto at=fast ? .13 : .28;
        m.host.submit(f::command(1,at,fast ? g::kReferenceGoldSrcButtonAttack2 : g::kReferenceGoldSrcButtonAttack),at);
        CHECK(m.host.sample(at).actions_started==2);
    }
    f::Module timer(1);timer.host.committed_scripted_event(f::event());(void)timer.host.sample(.03);
    auto observed=f::state(1,3);observed.weapon_slots[0].next_primary_attack=.55;
    timer.host.observe(observed,.04);timer.host.submit(f::command(1,.28),.28);
    CHECK(timer.host.sample(.28).actions_started==1); // Actual server timer wins.
}
