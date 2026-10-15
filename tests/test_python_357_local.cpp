#include "python_357_test_fixture.hpp"
#include <hlclient/games/halflife/firearm_profile.hpp>
#include <hlclient/games/halflife/local_weapon_presentation.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <set>
namespace f=hlclient::tests::python_fixture;
namespace api=hlclient::game_api;
namespace hl=hlclient::games::halflife;
namespace g=hlclient::goldsrc;
using Catch::Approx;

TEST_CASE("E18 Python aliases catalogue model roles and multiplayer body jointly authorize fire", "[e18][weapon-presentation][game-api]") {
    for(const auto alias:{"weapon_357","weapon_python"}) for(const unsigned clients:{1U,8U}) {
        f::Module m(clients);m.observed.weapon_hud.catalogue[0].command_name=alias;
        m.host.observe(m.observed,0);m.host.submit(f::command(),.02);
        const auto shot=m.host.sample(.02);REQUIRE(shot.identity);REQUIRE(shot.visual);
        CHECK(shot.visual->sequence==2);CHECK(shot.visual->body==(clients>1 ? 1U : 0U));
        CHECK(shot.local_punch_pitch_degrees==Approx(-10));
        CHECK(m.observed.weapon_slots[0].clip==6);CHECK(m.host.hud(m.observed,.02).clip==6);
        m.host.submit(f::command(),.02);CHECK(m.host.sample(.02).actions_started==1);
        const auto pose=m.host.viewmodel(m.observed,f::model(),.04,shot.visual);
        CHECK(pose.status==api::ViewmodelStatus::ready);CHECK(pose.body==shot.visual->body);
    }
    for(unsigned bad=0;bad<4;++bad) {
        f::Module m;auto model=f::model();
        if(bad==0) m.observed.weapon_hud.catalogue[0].command_name="weapon_other";
        if(bad==1) model.resource_name="models/v_other.mdl";
        if(bad==2) model.sequences[2].label="reload";
        if(bad==3) model.supported_bodies[1]=false;
        m.host.bind_model(model);m.host.observe(m.observed,0);m.host.submit(f::command(),.02);
        CHECK(m.host.sample(.02).actions_started==0);
    }
}
TEST_CASE("E18 Python confirmed fire retains restart then enforces 750 ms cycle without echo", "[e18][weapon-presentation][cooldown]") {
    f::Module m;m.host.submit(f::command(),.02);const auto shot=m.host.sample(.02);REQUIRE(shot.visual);
    m.observed=f::state(5,2);m.host.observe(m.observed,.06);
    auto confirmed=m.host.sample(.06);REQUIRE(confirmed.visual);
    CHECK(confirmed.actions_confirmed==1);CHECK(confirmed.visual->sequence==2);
    CHECK(confirmed.visual->restart_identity==shot.visual->restart_identity);
    m.host.submit(f::command(2,.769),.769);CHECK(m.host.sample(.769).actions_started==1);
    m.host.submit(f::command(3,.77),.77);CHECK(m.host.sample(.77).actions_started==2);
    CHECK(m.observed.weapon_slots[0].clip==5);
}
TEST_CASE("E18 Python empty underwater reload and secondary never create successful fire effects", "[e18][weapon-presentation][lifecycle]") {
    for(unsigned blocked=0;blocked<5;++blocked) {
        f::Module m;
        if(blocked==0) {m.observed=f::state(0);m.observed.weapon_hud.reserve_ammo[1]=0;}
        if(blocked==1) m.observed.receiving_client->water_level=3;
        if(blocked==2) m.observed.weapon_slots[0].in_reload=true;
        if(blocked==3) m.observed.receiving_client->health=0;
        m.observed.publication_revision=2;
        m.observed.client_metadata.source=hlclient::client::RuntimeObservationSource{2,2,0,0,8};
        m.host.observe(m.observed,.001);(void)m.host.drain_audio();
        m.host.submit(f::command(1,.02,blocked==4 ? g::kReferenceGoldSrcButtonAttack2 : g::kReferenceGoldSrcButtonAttack),.02);
        CHECK(m.host.sample(.02).primary_fire_starts==0);
        CHECK(m.host.drain_audio().count==0);
        const auto visuals=m.host.local_visuals({{}, {}, {}, {}, {}, .02});
        CHECK_FALSE(visuals.world_impact);CHECK_FALSE(visuals.shell);CHECK_FALSE(visuals.flash);
    }
}
TEST_CASE("E18 Python conventional cylinder reload confirms clip transfer and sound marker once", "[e18][weapon-presentation][weapon-audio][reload]") {
    f::Module m;m.observed=f::state(2,2);m.host.observe(m.observed,.001);(void)m.host.drain_audio();
    m.host.submit(f::command(1,.02,g::kReferenceGoldSrcButtonReload),.02);
    const auto reload=m.host.sample(.02);REQUIRE(reload.visual);CHECK(reload.visual->sequence==3);
    CHECK(reload.visual->body==1);CHECK(m.observed.weapon_slots[0].clip==2);
    m.observed=f::state(2,3);m.observed.weapon_slots[0].in_reload=true;
    m.host.observe(m.observed,.04);CHECK(m.host.sample(.04).reload_confirmed==1);
    m.host.submit(f::command(2,1),1);CHECK(m.host.sample(1).primary_fire_starts==0);
    (void)m.host.sample(1.965);auto audio=m.host.drain_audio();REQUIRE(audio.count==1);
    CHECK(audio.cues[0].reference.name()=="weapons/357_reload1.wav");CHECK(audio.cues[0].kind==api::LocalSoundKind::reload);
    (void)m.host.sample(1.98);CHECK(m.host.drain_audio().count==0);
    m.observed=f::state(6,4);m.observed.weapon_hud.reserve_ammo[1]=14;
    m.host.observe(m.observed,2.03);m.host.submit(f::command(3,2.04),2.04);
    CHECK(m.host.sample(2.04).primary_fire_starts==1);CHECK(m.host.hud(m.observed,2.04).clip==6);
}
TEST_CASE("E18 normal module Python fire audio flash light single impact zero brass zero tracer and echo dedup", "[e18][composition][weapon-visuals][weapon-audio]") {
    f::Module m;auto prepare=m.host.drain_audio();std::set<std::string> found;
    for(std::size_t i=0;i<prepare.prepare_count;++i) if(prepare.prepare[i].name().starts_with("weapons/357_"))
        found.emplace(prepare.prepare[i].name());
    CHECK(found.size()==4);m.host.submit(f::command(),.02);auto shot=m.host.sample(.02);REQUIRE(shot.identity);
    const auto audio=m.host.drain_audio();REQUIRE(audio.count==1);
    CHECK(audio.cues[0].reference.name()==hl::python_sound(*shot.identity));
    CHECK(audio.cues[0].volume>=.8F);CHECK(audio.cues[0].volume<=.9F);CHECK(audio.cues[0].pitch==1);
    const auto effects=m.host.local_visuals({{}, {}, {}, {}, {}, .02});
    REQUIRE(effects.world_impact);CHECK(effects.world_impact->maximum_distance_units==8192);
    CHECK_FALSE(effects.world_impact->tracer);CHECK(effects.extra_impact_count==0);
    CHECK_FALSE(effects.shell);CHECK(effects.extra_shell_count==0);
    m.host.committed_scripted_event(f::event());m.host.committed_scripted_event(f::event());
    auto echo=m.host.sample(.03);REQUIRE(echo.visual);CHECK(echo.visual->restart_identity==shot.visual->restart_identity);
    CHECK(m.host.drain_audio().count==0);CHECK(m.host.drain_remote_audio().count==0);
    CHECK_FALSE(m.host.local_visuals({{}, {}, {}, {}, {}, .03}).world_impact);
    CHECK(echo.duplicate_events==1);CHECK(echo.actions_started==1);
}
TEST_CASE("E18 Python last round keeps fire role and committed only shot retains event direction once", "[e18][composition][weapon-visuals][echo]") {
    f::Module last;last.observed=f::state(1,2);last.host.observe(last.observed,.001);
    last.host.submit(f::command(),.02);REQUIRE(last.host.sample(.02).visual);
    last.observed=f::state(0,3);last.host.observe(last.observed,.04);
    const auto confirmed=last.host.sample(.04);REQUIRE(confirmed.visual);CHECK(confirmed.visual->sequence==2U);
    CHECK(confirmed.primary_fire_confirmed==1U);CHECK(last.host.hud(last.observed,.04).clip==0);
    f::Module committed;(void)committed.host.drain_audio();auto event=f::event();event.spread_x=.006F;event.spread_y=-.004F;
    committed.host.committed_scripted_event(event);const auto shot=committed.host.sample(.03);REQUIRE(shot.identity);
    CHECK(shot.identity->command_sequence==0U);CHECK(shot.primary_fire_confirmed==1U);
    const auto effects=committed.host.local_visuals({{}, {1,0,0}, {0,-1,0}, {0,0,1}, {}, .03});
    REQUIRE(effects.world_impact);CHECK(effects.world_impact->direction.y/effects.world_impact->direction.x==Approx(-.006F).margin(1e-6));
    CHECK(effects.world_impact->direction.z/effects.world_impact->direction.x==Approx(.004F).margin(1e-6));
    CHECK_FALSE(effects.shell);CHECK_FALSE(effects.world_impact->tracer);CHECK(committed.host.drain_audio().count==1U);
    committed.host.committed_scripted_event(event);(void)committed.host.sample(.04);
    CHECK(committed.host.drain_audio().count==0U);CHECK_FALSE(committed.host.local_visuals({{}, {}, {}, {}, {}, .04}).world_impact);
}
