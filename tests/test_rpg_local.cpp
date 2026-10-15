#include "rpg_test_fixture.hpp"
#include <hlclient/games/halflife/rpg_profile.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
namespace f=hlclient::tests::rpg_fixture;
namespace x=hlclient::tests::crossbow_fixture;
namespace api=hlclient::game_api;
namespace hl=hlclient::games::halflife;
using Catch::Approx;
TEST_CASE("E20 RPG accepted launch uses original rocket cue without grenade overlay", "[e20][weapon-presentation][game-api]") {
    f::Module m;const auto ready=m.host.drain_audio();
    CHECK(std::any_of(ready.prepare.begin(),ready.prepare.begin()+ready.prepare_count,
        [](const auto& sample){return sample.name()=="weapons/rocketfire1.wav";}));
    CHECK_FALSE(std::any_of(ready.prepare.begin(),ready.prepare.begin()+ready.prepare_count,
        [](const auto& sample){return sample.name()=="weapons/glauncher.wav" || sample.name()=="weapons/glauncher2.wav";}));
    m.host.submit(x::command(),.02);const auto p=m.host.sample(.02);
    REQUIRE(p.identity);REQUIRE(p.visual);CHECK(p.identity->weapon_id==8);
    CHECK(p.visual->sequence==3);CHECK(p.visual->body==1);CHECK(p.local_punch_pitch_degrees==Approx(-5));
    const auto audio=m.host.drain_audio();REQUIRE(audio.count==1);
    CHECK(audio.cues[0].reference.name()=="weapons/rocketfire1.wav");CHECK(audio.cues[0].volume==Approx(.9));
    CHECK(audio.cues[0].reference.source==api::SoundReferenceSource::pinned_halflife_client_sound_profile);
    for(std::size_t i=0;i<audio.count;++i) {CHECK(audio.cues[i].pitch==1);CHECK(audio.cues[i].channel==api::LocalSoundChannel::automatic);}
    const auto v=m.host.local_visuals({{}, {}, {}, {}, {}, .02});
    CHECK_FALSE(v.flash);CHECK_FALSE(v.light);CHECK_FALSE(v.shell);CHECK_FALSE(v.world_impact);
    CHECK(v.extra_impact_count==0);CHECK(v.extra_shell_count==0);CHECK(m.host.drain_temporary_effects(.02).count==0);
    CHECK(m.host.hud(m.observed,.02).clip==1);
    m.host.submit(x::command(),.02);CHECK(m.host.sample(.02).actions_started==1);CHECK(m.host.drain_audio().count==0);
    auto e=x::event();m.host.committed_scripted_event(e);m.host.committed_scripted_event(e);
    CHECK(m.host.sample(.03).actions_started==1);CHECK(m.host.drain_audio().count==0);
    const auto pose=m.host.viewmodel(m.observed,f::model(),.04,p.visual);CHECK(pose.sequence==3);CHECK(pose.body==1);
}
TEST_CASE("E20 assigned RPG event works locally and remotely without observer cosmetics", "[e20][remote-effects][weapon-audio]") {
    for(unsigned emitter:{1U,2U}) {
        f::Module m;(void)m.host.drain_audio();auto e=x::event(false,emitter);
        const auto ready=m.host.drain_remote_audio();
        CHECK(std::any_of(ready.prepare.begin(),ready.prepare.begin()+ready.prepare_count,
            [](const auto& sample){return sample.name()=="weapons/rocketfire1.wav";}));
        CHECK_FALSE(std::any_of(ready.prepare.begin(),ready.prepare.begin()+ready.prepare_count,
            [](const auto& sample){return sample.name()=="weapons/glauncher.wav" || sample.name()=="weapons/glauncher2.wav";}));
        m.host.committed_scripted_event(e);(void)m.host.sample(.03);
        if(emitter==1) {CHECK(m.host.drain_audio().count==1);CHECK(m.host.drain_remote_audio().count==0);}
        else {CHECK(m.host.drain_remote_effects(.03).count==0);auto audio=m.host.drain_remote_audio();REQUIRE(audio.count==1);
            REQUIRE(audio.cues[0].world_origin);CHECK(audio.cues[0].reference.name()=="weapons/rocketfire1.wav");
            CHECK(audio.cues[0].world_origin->x==e.origin.x);CHECK(audio.cues[0].world_origin->y==e.origin.y);
            CHECK(audio.cues[0].world_origin->z==e.origin.z);CHECK(audio.cues[0].volume==Approx(.9));
            CHECK(audio.cues[0].pitch==1);CHECK(audio.cues[0].channel==api::LocalSoundChannel::automatic);
            CHECK(m.host.sample(.03).local_punch_pitch_degrees==0);}
        m.host.committed_scripted_event(e);CHECK(m.host.drain_remote_effects(.04).count==0);
        CHECK(m.host.drain_remote_audio().count==0);CHECK(m.host.drain_audio().count==0);
    }
}
TEST_CASE("E20 RPG reload holster and missing auxiliary state policy is server evidence", "[e20][weapon-presentation][game-api]") {
    for(int spot:{-1,0,1,2}) for(int count:{-1,0,1,511,512}) {
        f::Module m(0);auto& v=m.observed.receiving_client->auxiliary_vector2;
        v.y=spot<0 ? std::nullopt : std::optional<double>{spot};v.z=count<0 ? std::nullopt : std::optional<double>{count};
        m.observed.weapon_hud.catalogue.push_back({.id=1,.command_name="weapon_crowbar",.source={1,1,1,0,8}});
        m.observed.receiving_client->owned_weapon_bits=(1U<<8)|(1U<<1);
        m.host.observe(m.observed,0);
        const bool allowed=(spot==0 || spot==1) && count>=0 && count<=511 && (spot==0 || count==0);
        CHECK(hl::rpg_launcher_state(m.observed).can_reload_or_holster()==allowed);
        CHECK(m.host.inventory_selection(m.observed,1).has_value()==allowed);
        m.host.submit(x::command(1,.02,hlclient::goldsrc::kReferenceGoldSrcButtonReload),.02);
        const auto p=m.host.sample(.02);CHECK(p.reload_starts==(allowed ? 1U : 0U));
        if(allowed) {REQUIRE(p.visual);CHECK(p.visual->sequence==2);}
    }
}
TEST_CASE("RPG repeated server reload advances the viewmodel when clientdata role stays unchanged",
          "[e20][rpg-reload][weapon-presentation][game-api]") {
    f::Module m(0);
    auto old=f::state(0,2);old.receiving_client->weapon_animation=2;
    old.weapon_slots[0].in_reload=false;m.host.observe(old,0);
    REQUIRE(m.host.sample(2.1).visual->sequence==8);
    auto reload=f::state(0,3);reload.receiving_client->weapon_animation=2;
    reload.receiving_client->auxiliary_vector2={}; // Authoritative reload is independent of provisional guards.
    reload.receiving_client->next_weapon_attack=2;reload.weapon_slots[0].in_reload=true;
    m.host.observe(reload,2.2);const auto start=m.host.sample(2.2);REQUIRE(start.visual);
    CHECK(start.visual->sequence==2);CHECK(start.visual->body==0);
    CHECK(start.visual->source==api::LocalWeaponAnimationSource::confirmed_weapon_state);
    const auto restart=start.visual->restart_identity;
    for(unsigned record=4;record<=5;++record) {
        auto fresh=f::state(0,record);fresh.receiving_client->weapon_animation=2;
        fresh.receiving_client->next_weapon_attack=2-(record-3)*.1;
        fresh.weapon_slots[0].in_reload=true;m.host.observe(fresh,2.2+(record-3)*.1);
        const auto pose=m.host.sample(2.2+(record-3)*.1);REQUIRE(pose.visual);
        CHECK(pose.visual->sequence==2);CHECK(pose.visual->restart_identity==restart);
        CHECK(pose.frame_coordinate==Approx((record-3)*3.0));
    }
    const auto later=m.host.sample(2.7);REQUIRE(later.visual);
    const auto intent=m.host.viewmodel(reload,f::model(),2.7,later.visual);
    CHECK(intent.status==api::ViewmodelStatus::ready);CHECK(intent.sequence==2);
    CHECK(intent.frame_coordinate==Approx(15));CHECK(m.host.hud(reload,2.7).clip==0);
    CHECK(later.reload_starts==0);CHECK_FALSE(later.identity);CHECK(m.host.drain_audio().count==0);
    auto complete=f::state(1,6,3);complete.receiving_client->weapon_animation=2;
    complete.weapon_slots[0].in_reload=false;m.host.observe(complete,4.25);
    CHECK(m.host.sample(4.25).visual->sequence==0);CHECK(m.host.hud(complete,4.25).clip==1);
    auto empty=f::state(0,7,3);empty.receiving_client->weapon_animation=2;
    empty.weapon_slots[0].in_reload=false;m.host.observe(empty,4.3);
    auto again=f::state(0,8,3);again.receiving_client->weapon_animation=2;
    again.weapon_slots[0].in_reload=true;again.receiving_client->next_weapon_attack=2;
    m.host.observe(again,4.4);const auto next=m.host.sample(4.5);REQUIRE(next.visual);
    CHECK(next.visual->sequence==2);CHECK(next.visual->restart_identity!=restart);
    CHECK(next.frame_coordinate==Approx(3));CHECK(next.primary_fire_starts==0);
    auto service=again;service.publication_revision=9;
    service.client_metadata.freshness=hlclient::client::RuntimeObservationFreshness::retained;
    service.weapon_hud.animation_source=hlclient::client::RuntimeObservationSource{9,9,1,0,8};
    service.weapon_hud.animation_sequence=2;service.weapon_hud.animation_body=0;
    m.host.observe(service,4.6);const auto exact=m.host.sample(4.6);REQUIRE(exact.visual);
    CHECK(exact.visual->restart_identity==next.visual->restart_identity);
    CHECK(exact.visual->source==api::LocalWeaponAnimationSource::service_event);
    CHECK(exact.frame_coordinate==Approx(6));m.host.observe(service,4.7);
    CHECK(m.host.sample(4.7).frame_coordinate==Approx(9));
}
TEST_CASE("RPG reload confirmation retains a provisional timeline and exact service precedence",
          "[e20][rpg-reload][weapon-presentation][lifecycle]") {
    f::Module m(0);m.host.submit(x::command(1,.02,hlclient::goldsrc::kReferenceGoldSrcButtonReload),.02);
    const auto provisional=m.host.sample(.02);REQUIRE(provisional.visual);
    auto observed=f::state(0,2);observed.weapon_slots[0].in_reload=true;
    observed.receiving_client->next_weapon_attack=2;observed.receiving_client->weapon_animation=2;
    m.host.observe(observed,.03);const auto confirmed=m.host.sample(.52);REQUIRE(confirmed.visual);
    CHECK(confirmed.reload_starts==1);CHECK(confirmed.reload_confirmed==1);
    CHECK(confirmed.visual->restart_identity==provisional.visual->restart_identity);
    CHECK(confirmed.visual->started_at_seconds==provisional.visual->started_at_seconds);
    CHECK(confirmed.frame_coordinate==Approx(15));CHECK(m.host.drain_audio().count==0);
    auto service=f::state(0,3);service.weapon_slots[0].in_reload=true;
    service.weapon_hud.animation_source=*service.client_metadata.source;
    service.weapon_hud.animation_sequence=5;service.weapon_hud.animation_body=0;
    m.host.observe(service,.6);const auto exact=m.host.sample(.6);REQUIRE(exact.visual);
    CHECK(exact.visual->sequence==5);CHECK(exact.visual->source==api::LocalWeaponAnimationSource::service_event);
    service.receiving_client->health=0;service.publication_revision=4;m.host.observe(service,.7);
    CHECK_FALSE(m.host.sample(.7).visual);CHECK(m.host.drain_audio().count==0);
}
TEST_CASE("RPG reload does not invent a pose from retained missing or inconsistent flags",
          "[e20][rpg-reload][weapon-presentation][bounds]") {
    for(unsigned variant=0;variant<5;++variant) {
        CAPTURE(variant);f::Module m(0);auto old=f::state(0,2);
        old.receiving_client->weapon_animation=2;old.weapon_slots[0].in_reload=false;
        m.host.observe(old,0);(void)m.host.sample(2.1);
        auto candidate=f::state(0,3);candidate.receiving_client->weapon_animation=2;
        candidate.weapon_slots[0].in_reload=true;
        if(variant==0)candidate.client_metadata.freshness=hlclient::client::RuntimeObservationFreshness::retained;
        if(variant==1)candidate.client_metadata.freshness=hlclient::client::RuntimeObservationFreshness::unavailable;
        if(variant==2)candidate.weapon_slots[0].in_reload.reset();
        if(variant==3)candidate.weapon_slots.clear();
        if(variant==4){candidate.weapon_slots[0].clip=1;candidate.weapon_hud.clips[8]=1;}
        m.host.observe(candidate,2.2);const auto pose=m.host.sample(2.2);
        CHECK((!pose.visual || pose.visual->sequence!=2));CHECK(pose.reload_starts==0);
    }
}
TEST_CASE("E20 RPG empty idle role catalogue guard and secondary no FOV policy", "[e20][weapon-presentation][input]") {
    f::Module empty(0);empty.observed.weapon_hud.reserve_ammo[1]=0;empty.observed.publication_revision=2;
    empty.observed.weapon_hud.last_message_source=hlclient::client::RuntimeObservationSource{2,2,1,0,8};
    empty.host.observe(empty.observed,.001);empty.host.submit(x::command(),.02);
    CHECK(empty.host.sample(.02).primary_fire_starts==0);CHECK(empty.host.drain_audio().count==0);
    CHECK(empty.host.sample(.02).visual->sequence==8);
    f::Module m;CHECK(m.host.edge_triggered_buttons(m.observed)!=0);
    m.host.submit(x::command(1,.02,hlclient::goldsrc::kReferenceGoldSrcButtonAttack2),.02);
    CHECK(m.host.sample(.02).actions_started==0);CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==90.0);
    for(unsigned bad=0;bad<3;++bad) {
        f::Module wrong;auto meta=f::model();
        if(bad==0)wrong.observed.weapon_hud.catalogue[0].command_name="weapon_crossbow";
        if(bad==1)meta.sequences[3].label="shoot";if(bad==2)meta.resource_name="models/v_other.mdl";
        wrong.host.bind_model(meta);wrong.host.observe(wrong.observed,0);wrong.host.submit(x::command(),.02);
        CHECK(wrong.host.sample(.02).actions_started==0);
    }
}
