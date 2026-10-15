#include "crossbow_test_fixture.hpp"
#include "local_resource_readiness_test_fixture.hpp"
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <iostream>
#include <hlclient/games/halflife/gauss.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <catch2/catch_approx.hpp>
#include "goldsrc_studio_test_fixture.hpp"
#include "goldsrc_sprite_test_fixture.hpp"
#include "synthetic_goldsrc_bsp_fixture.hpp"
#include "synthetic_goldsrc_wad3_fixture.hpp"
#include "python_357_pmodel_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include <hlclient/goldsrc/sound_assets.hpp>
#include <hlclient/app/world_impact_presentation.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>
#include <thread>
#include "world_render_test_fixture.hpp"
#include "local_movement_test_fixture.hpp"
#include <hlclient/renderer/render_camera_math.hpp>
namespace gauss_assets {
void ready(hlclient::goldsrc::ApprovedSoundAssets&);
void audio(hlclient::goldsrc::ApprovedSoundAssets&);
void gpu(hlclient::app::RuntimeReplayLocalAssets&);
}
namespace gf {
namespace api=hlclient::game_api;namespace c=hlclient::client;namespace f=hlclient::tests::crossbow_fixture;
api::LocalWeaponModelMetadata model() {
    auto m=f::model();m.resource_name="models/v_gauss.mdl";m.sequences.clear();m.supported_bodies[2]=m.selectable_bodies[2]=true;
    for(const auto name:{"idle","idle2","fidget","spinup","spin","fire","fire2","holster","draw"})
        m.sequences.push_back({30,31,std::string_view{name}=="spin",{},name});return m;
}
c::RuntimeClientObservationState state(int ammo=100) {
    auto s=f::state(-1,1,ammo);s.weapon_hud.active_weapon_id=9;s.weapon_hud.clips[9]=-1;
    auto& type=s.weapon_hud.catalogue[0];type.id=9;type.command_name="weapon_gauss";type.slot=3;type.position=1;
    s.receiving_client->owned_weapon_bits=1U<<9;s.receiving_client->auxiliary_float2=0;s.receiving_client->auxiliary_float3=0;
    s.weapon_slots[0].weapon_id=9;s.weapon_slots[0].auxiliary_integer2=0;return s;
}
struct Module {
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};c::RuntimeClientObservationState s=state();
    explicit Module(unsigned maximum=8) {
        host.reset({1,1,1});host.set_movement_audio_time_origin(0);api::GameRecordState r;r.multiplayer.maximum_clients=maximum;host.commit_record(std::move(r));
        host.bind_model(model());host.observe(s,0);
        std::array<api::ScriptedEventBinding,2> bindings{};bindings[0].index=900;bindings[1].index=901;
        constexpr std::string_view fire="events/gauss.sc",spin="events/gaussspin.sc";
        std::copy(fire.begin(),fire.end(),bindings[0].name.begin());std::copy(spin.begin(),spin.end(),bindings[1].name.begin());host.configure_scripted_events(bindings);
    }
    std::optional<hlclient::assets::AssetVector3> command(unsigned seq,double at,std::uint16_t buttons) {return host.command_movement_effect(f::command(seq,at,buttons));}
};
struct World:api::IStaticEffectTrace {
    mutable unsigned calls{};bool miss{},invalid{},reflection{},penetration{};
    api::StaticEffectTrace trace(hlclient::assets::AssetVector3,hlclient::assets::AssetVector3 b) const noexcept override {
        const auto i=calls++;if(invalid)return {};
        if(miss || (reflection && i>0) || (penetration && (i==1 || i>=3)))return {true,false,false,false,false,b,{}};
        const hlclient::assets::AssetVector3 point=i==2 ? hlclient::assets::AssetVector3{60,80,80} : hlclient::assets::AssetVector3{90,80,80};
        // Local fixture fires -X. Entry outward +X, exit outward -X.
        return {true,false,false,true,false,point,reflection ? hlclient::assets::AssetVector3{.3F,.9539392F,0} : hlclient::assets::AssetVector3{i==2 ? -1.0F : 1.0F,0,0}};
    }
};
api::CommittedScriptedEvent event(std::uint32_t emitter=2,std::uint64_t record=2) {
    auto e=f::event(false,emitter,record,.03);e.event_index=900;e.float_parameter1=20.0F;e.boolean_parameter1=true;e.boolean_parameter2=false;return e;
}
}
TEST_CASE("E21 normal module primary has no clip shell tracer projectile or E7 audio", "[e21][game-api][no-stock]") {
    gf::Module m;CHECK_FALSE(m.command(1,.02,1));const auto p=m.host.sample(.02);REQUIRE(p.identity);CHECK(p.identity->weapon_id==9);
    REQUIRE(p.visual);CHECK(p.visual->sequence==6);CHECK(p.visual->body==2);CHECK(p.local_punch_pitch_degrees==Catch::Approx(-2));
    CHECK(m.host.hud(m.s,.02).clip==std::nullopt);CHECK(m.host.hud(m.s,.02).primary_reserve==100);
    const auto a=m.host.drain_audio();REQUIRE(a.count==1);CHECK(a.cues[0].reference.name()=="weapons/gauss2.wav");
    CHECK_FALSE(m.host.local_visuals({}).shell);CHECK_FALSE(m.host.local_visuals({}).world_impact);
    gf::World world;const auto beam=m.host.beam_effects(world,.02,hlclient::assets::AssetVector3{98,79,78});
    REQUIRE(beam.count>=1);CHECK(beam.sprites[0].ribbon);CHECK(beam.sprites[0].width==Catch::Approx(1.75));CHECK(beam.sprites[0].start.x==98);
    CHECK(beam.impact_count==1);CHECK(beam.shots==1);CHECK(beam.penetrations==0);CHECK(m.host.drain_audio().count==0);
    m.command(1,.02,1);m.host.submit(gf::f::command(),.02);CHECK(m.host.sample(.02).actions_started==1);
    CHECK(m.host.beam_effects(world,7,{}).count==0);CHECK(m.host.beam_effects(world,7,{}).impact_count==0);
}
TEST_CASE("E21 charge spin pitch release and provisional impulse are once per immutable command", "[e21][prediction][no-stock]") {
    gf::Module m;m.command(1,.02,1U<<11);CHECK(m.host.sample(.02).visual->sequence==3);
    auto start=m.host.drain_audio();REQUIRE(start.control_count==1);CHECK(start.controls[0].operation==gf::api::LocalVoiceControl::Operation::start);CHECK(start.controls[0].pitch==Catch::Approx(1.1));
    for(unsigned i=2;i<=80;++i) {CHECK_FALSE(m.command(i,i*.02,1U<<11));(void)m.host.drain_audio();}
    CHECK(m.host.sample(1.6).visual->sequence==4);const auto impulse=m.command(81,1.62,0);REQUIRE(impulse);CHECK(impulse->x==Catch::Approx(1000));CHECK(impulse->z==0);
    const auto release=m.host.drain_audio();REQUIRE(release.control_count==1);CHECK(release.controls[0].operation==gf::api::LocalVoiceControl::Operation::stop);
    REQUIRE(release.count==1);CHECK(release.cues[0].volume==1);CHECK_FALSE(m.command(81,1.62,0));CHECK(m.host.drain_audio().count==0);
    CHECK(m.host.hud(m.s,1.62).primary_reserve==100);gf::World w;w.penetration=true;const auto beam=m.host.beam_effects(w,1.62,{});
    CHECK(beam.penetrations==1);CHECK(beam.impact_count==2);CHECK(beam.sprites[0].width==Catch::Approx(3.5));
}
TEST_CASE("E21 repeated short secondary releases immediately permit the next command owned charge",
          "[e21-repeat-secondary][e21][prediction][game-api][no-stock]") {
    for(const unsigned clients:{1U,8U})for(const bool fresh_ammo:{false,true}) {
        CAPTURE(clients,fresh_ammo);gf::Module module{clients};gf::World world;world.miss=true;
        std::uint32_t sequence=0;std::uint64_t previous_voice=0;
        for(unsigned cycle=0;cycle<5;++cycle) {
            const auto base=static_cast<double>(cycle)*.54;
            const auto start=base+.02,release=base+.52;
            CAPTURE(cycle,start,release);
            CHECK_FALSE(module.command(++sequence,start,1U<<11));
            const auto spin=module.host.sample(start);REQUIRE(spin.visual);CHECK(spin.visual->sequence==3);
            const auto starting=module.host.drain_audio();REQUIRE(starting.control_count==1);
            CHECK(starting.controls[0].operation==gf::api::LocalVoiceControl::Operation::start);
            CHECK(starting.controls[0].identity!=previous_voice);previous_voice=starting.controls[0].identity;
            // Repeated brief up/down edges before the initial spin-up deadline
            // are one charge, not a new reservation or an early shot.
            for(unsigned tick=2;tick<=25;++tick) {
                CHECK_FALSE(module.command(++sequence,base+tick*.02,tick%2 ? 1U<<11 : 0));
                const auto between=module.host.drain_audio();CHECK(between.count==0);CHECK(between.control_count==0);
                CHECK(module.host.sample(base+tick*.02).secondary_fire_starts==cycle);
            }
            const auto impulse=module.command(++sequence,release,0);REQUIRE(impulse);
            CHECK(impulse->x==Catch::Approx(1000*.5/(clients==1 ? 4.0 : 1.5)));
            const auto fired=module.host.sample(release);REQUIRE(fired.identity);REQUIRE(fired.visual);
            CHECK(fired.identity->command_sequence==sequence);CHECK(fired.secondary_fire_starts==cycle+1);
            CHECK(fired.primary_fire_starts==0);CHECK(fired.visual->sequence==6);
            const auto audio=module.host.drain_audio();REQUIRE(audio.count==1);REQUIRE(audio.control_count==1);
            CHECK(audio.cues[0].reference.name()=="weapons/gauss2.wav");
            CHECK(audio.controls[0].operation==gf::api::LocalVoiceControl::Operation::stop);
            CHECK(audio.controls[0].identity==previous_voice);
            const auto beam=module.host.beam_effects(world,release,{});CHECK(beam.shots==cycle+1);
            REQUIRE(beam.count==1);CHECK(beam.sprites[0].action==*fired.identity);
            CHECK(beam.sprites[0].width==Catch::Approx(3.5));CHECK(world.calls==cycle+1);
            CHECK_FALSE(module.command(sequence,release,0));
            module.host.submit(gf::f::command(sequence,release,0),release);
            auto echo=gf::event(1,cycle+2);echo.received_at_seconds=release+.001;
            echo.boolean_parameter1=false;echo.float_parameter1=static_cast<float>(impulse->x/5);
            module.host.committed_scripted_event(echo);module.host.committed_scripted_event(echo);
            CHECK(module.host.sample(release+.001).actions_confirmed==cycle+1);
            const auto duplicate=module.host.drain_audio();CHECK(duplicate.count==0);CHECK(duplicate.control_count==0);
            CHECK(module.host.beam_effects(world,release+.001,{}).shots==cycle+1);CHECK(world.calls==cycle+1);
            if(fresh_ammo) {
                ++module.s.publication_revision;module.s.weapon_hud.reserve_ammo[1]=static_cast<std::uint8_t>(99-cycle);
                module.host.observe(module.s,release+.002);
            }
            CHECK(module.host.hud(module.s,release+.002).primary_reserve==(fresh_ammo ? 99-cycle : 100));
        }
    }
}
TEST_CASE("E21 manual secondary release separates idle deadline attack readiness and stale charge echoes",
          "[e21-repeat-secondary][e21][prediction][lifecycle][no-stock]") {
    for(const bool primary:{false,true}) {
        CAPTURE(primary);gf::Module module;module.command(1,.02,1U<<11);(void)module.host.drain_audio();
        REQUIRE(module.command(2,.52,0));const auto fired=module.host.sample(.52);REQUIRE(fired.visual);
        (void)module.host.drain_audio();
        // The preceding charge can still be in flight. Removing an incorrect
        // attack gate must not let that echo reopen the completed action.
        module.s.publication_revision=2;module.s.weapon_slots[0].auxiliary_integer2=2;
        module.s.receiving_client->auxiliary_float2=.1;module.s.receiving_client->auxiliary_float3=.9;
        module.host.observe(module.s,.54);const auto delayed=module.host.sample(.54);REQUIRE(delayed.visual);
        CHECK(delayed.visual->sequence==6);CHECK(delayed.visual->restart_identity==fired.visual->restart_identity);
        const auto echo=module.host.drain_audio();CHECK(echo.count==0);CHECK(echo.control_count==0);
        CHECK_FALSE(module.command(3,.56,primary ? 1 : 1U<<11));const auto next=module.host.sample(.56);
        REQUIRE(next.visual);CHECK(next.visual->sequence==(primary ? 6U : 3U));
        CHECK(next.primary_fire_starts==(primary ? 1U : 0U));
        const auto audio=module.host.drain_audio();CHECK(audio.count==(primary ? 1U : 0U));
        CHECK(audio.control_count==(primary ? 0U : 1U));
    }
    SECTION("forced exhaustion still owns the SDK one second attack gate") {
        gf::Module module;module.s=gf::state(1);module.s.publication_revision=2;module.host.observe(module.s,0);
        module.command(1,.02,1U<<11);module.command(2,.53,1U<<11);REQUIRE(module.command(3,.55,1U<<11));
        (void)module.host.drain_audio();module.s.publication_revision=3;module.s.weapon_hud.reserve_ammo[1]=100;
        module.host.observe(module.s,.6);
        CHECK_FALSE(module.command(4,1.53,1));CHECK(module.host.sample(1.53).primary_fire_starts==0);
        CHECK(module.host.drain_audio().count==0);CHECK_FALSE(module.command(5,1.56,1U<<11));
        const auto next=module.host.sample(1.56);REQUIRE(next.visual);CHECK(next.visual->sequence==3);
        const auto start=module.host.drain_audio();REQUIRE(start.control_count==1);
        CHECK(start.controls[0].operation==gf::api::LocalVoiceControl::Operation::start);
    }
}
TEST_CASE("E21 delayed local stop echo cannot stop a newer command owned charge lifetime",
          "[e21-repeat-secondary][e21][audio][lifecycle][no-stock]") {
    gf::Module module;module.command(1,.02,1U<<11);(void)module.host.drain_audio();
    REQUIRE(module.command(2,.52,0));(void)module.host.drain_audio();
    module.command(3,1.56,1U<<11);const auto start=module.host.drain_audio();REQUIRE(start.control_count==1);
    auto stop=gf::event(1,3);stop.received_at_seconds=1.58;stop.boolean_parameter2=true;
    stop.float_parameter1.reset();stop.fire_delay_ticks=1;stop.resolution=gf::api::ScriptedEventResolution::unsupported_delay;
    module.host.committed_scripted_event(stop);module.host.committed_scripted_event(stop);
    const auto echo=module.host.drain_audio();CHECK(echo.count==0);CHECK(echo.control_count==0);
    REQUIRE(module.command(4,2.08,0));const auto release=module.host.drain_audio();REQUIRE(release.count==1);
    REQUIRE(release.control_count==1);CHECK(release.controls[0].operation==gf::api::LocalVoiceControl::Operation::stop);
    CHECK(release.controls[0].identity==start.controls[0].identity);CHECK(module.host.sample(2.08).secondary_fire_starts==2);
}
TEST_CASE("E21 completed secondary receipt survives a newer charge or primary action",
          "[e21-repeat-secondary][e21][prediction][lifecycle][no-stock]") {
    SECTION("old positive and terminal phases cannot age or cancel the new command charge") {
        for(const double old_burn:{.1,1000.0}) {
            CAPTURE(old_burn);gf::Module module;module.command(1,.02,1U<<11);REQUIRE(module.command(2,.52,0));
            (void)module.host.drain_audio();CHECK_FALSE(module.command(3,.54,1U<<11));
            const auto spin=module.host.sample(.54);REQUIRE(spin.visual);CHECK(spin.visual->sequence==3);
            const auto start=module.host.drain_audio();REQUIRE(start.control_count==1);
            module.s.publication_revision=2;module.s.weapon_hud.reserve_ammo[1]=99;
            module.s.weapon_slots[0].auxiliary_integer2=2;
            module.s.receiving_client->auxiliary_float2=old_burn;module.s.receiving_client->auxiliary_float3=.9;
            module.host.observe(module.s,.56);const auto delayed=module.host.sample(.56);REQUIRE(delayed.visual);
            CHECK(delayed.visual->sequence==3);CHECK(delayed.visual->restart_identity==spin.visual->restart_identity);
            CHECK_FALSE(module.command(4,.58,1U<<11));
            const auto echo=module.host.drain_audio();CHECK(echo.count==0);CHECK(echo.control_count==0);
            module.s.publication_revision=3;module.s.weapon_slots[0].auxiliary_integer2=0;
            module.s.receiving_client->auxiliary_float2=module.s.receiving_client->auxiliary_float3=0;
            module.host.observe(module.s,.60);const auto terminal=module.host.sample(.60);REQUIRE(terminal.visual);
            CHECK(terminal.visual->restart_identity==spin.visual->restart_identity);
            CHECK(module.host.drain_audio().control_count==0);
            CHECK_FALSE(module.command(5,1.02,0));CHECK(module.host.sample(1.02).secondary_fire_starts==1);
            const auto impulse=module.command(6,1.04,0);REQUIRE(impulse);
            CHECK(impulse->x==Catch::Approx(1000*.5/1.5));CHECK(module.host.sample(1.04).secondary_fire_starts==2);
            const auto release=module.host.drain_audio();REQUIRE(release.count==1);REQUIRE(release.control_count==1);
            CHECK(release.controls[0].identity==start.controls[0].identity);
            // The stale phase/sentinel must not burn an extra uranium or
            // rewrite the authoritative reserve while the new charge spins up.
            CHECK(module.host.hud(module.s,1.04).primary_reserve==99);
        }
    }
    SECTION("primary replacing the last action does not discard the completed secondary receipt") {
        gf::Module module;module.command(1,.02,1U<<11);REQUIRE(module.command(2,.52,0));
        (void)module.host.drain_audio();CHECK_FALSE(module.command(3,.54,1));
        const auto primary=module.host.sample(.54);REQUIRE(primary.identity);REQUIRE(primary.visual);
        CHECK(primary.identity->kind==gf::api::LocalWeaponAction::primary_fire);CHECK(primary.primary_fire_starts==1);
        (void)module.host.drain_audio();module.s.publication_revision=2;module.s.weapon_slots[0].auxiliary_integer2=2;
        module.s.receiving_client->auxiliary_float2=.1;module.s.receiving_client->auxiliary_float3=.9;
        module.host.observe(module.s,.56);const auto delayed=module.host.sample(.56);REQUIRE(delayed.visual);
        CHECK(delayed.identity==primary.identity);CHECK(delayed.visual->restart_identity==primary.visual->restart_identity);
        CHECK_FALSE(module.command(4,.58,1U<<11));
        const auto echo=module.host.drain_audio();CHECK(echo.count==0);CHECK(echo.control_count==0);
        CHECK_FALSE(module.command(5,.76,1U<<11));const auto next=module.host.sample(.76);REQUIRE(next.visual);
        CHECK(next.visual->sequence==3);CHECK(module.host.drain_audio().control_count==1);
    }
}
TEST_CASE("E21 completed secondary echo receipt expires and clears on actual lifecycle boundaries",
          "[e21-repeat-secondary][e21][prediction][lifecycle][no-stock]") {
    for(unsigned boundary=0;boundary<4;++boundary) {
        CAPTURE(boundary);gf::Module module;module.command(1,.02,1U<<11);REQUIRE(module.command(2,.52,0));
        (void)module.host.drain_audio();double at=.58;
        if(boundary==0)at=1.54; // Bounded suppression is not a permanent ban on server adoption.
        else if(boundary==1) {module.host.bind_model(gf::f::model());module.host.bind_model(gf::model());}
        else if(boundary==2) {
            module.s.publication_revision=2;module.s.weapon_hud.active_weapon_id=6;module.host.observe(module.s,.54);
            module.s.weapon_hud.active_weapon_id=9;
        } else {
            module.s.publication_revision=2;module.s.receiving_client->health=0;module.host.observe(module.s,.54);
            module.s.receiving_client->health=100;++module.s.lifecycle.life_epoch;
        }
        module.s.publication_revision=3;module.s.weapon_slots[0].auxiliary_integer2=2;
        module.s.receiving_client->auxiliary_float2=.1;module.s.receiving_client->auxiliary_float3=.9;
        module.host.observe(module.s,at);const auto adopted=module.host.sample(at);REQUIRE(adopted.visual);
        CHECK(adopted.visual->sequence==4);const auto voice=module.host.drain_audio();REQUIRE(voice.control_count==1);
        CHECK(voice.controls[0].operation==gf::api::LocalVoiceControl::Operation::start);
    }
    SECTION("fresh server attack readiness remains authoritative after manual release") {
        gf::Module module;module.command(1,.02,1U<<11);REQUIRE(module.command(2,.52,0));
        (void)module.host.drain_audio();module.s.publication_revision=2;module.s.receiving_client->next_weapon_attack=.4;
        module.host.observe(module.s,.54);CHECK_FALSE(module.command(3,.56,1));
        CHECK(module.host.sample(.56).primary_fire_starts==0);CHECK(module.host.drain_audio().count==0);
        CHECK_FALSE(module.command(4,.96,1));CHECK(module.host.sample(.96).primary_fire_starts==1);
        CHECK(module.host.drain_audio().count==1);
    }
}
TEST_CASE("E21 delayed local fire echo cannot confirm a newer charge animation",
          "[e21-repeat-secondary][e21][prediction][lifecycle][no-stock]") {
    gf::Module module;module.command(1,.02,1U<<11);REQUIRE(module.command(2,.52,0));
    auto echo=gf::event(1,2);echo.received_at_seconds=.521;echo.boolean_parameter1=false;echo.float_parameter1=200.F/3;
    module.host.committed_scripted_event(echo);CHECK(module.host.sample(.521).actions_confirmed==1);
    (void)module.host.drain_audio();gf::World world;world.miss=true;
    CHECK(module.host.beam_effects(world,.522,{}).shots==1);CHECK(world.calls==1);
    CHECK_FALSE(module.command(3,.54,1U<<11));(void)module.host.drain_audio();
    const auto before=module.host.sample(.56);REQUIRE(before.visual);CHECK(before.visual->sequence==3);
    echo.record=echo.record_ordinal=3;echo.received_at_seconds=.56;
    module.host.committed_scripted_event(echo);module.host.committed_scripted_event(echo);
    const auto after=module.host.sample(.56);REQUIRE(after.visual);
    CHECK(after.identity==before.identity);CHECK(after.actions_started==before.actions_started);
    CHECK(after.actions_confirmed==before.actions_confirmed);CHECK(after.status==before.status);
    CHECK(after.visual->status==before.visual->status);CHECK(after.visual->restart_identity==before.visual->restart_identity);
    CHECK(after.local_punch_pitch_degrees==before.local_punch_pitch_degrees);
    const auto audio=module.host.drain_audio();CHECK(audio.count==0);CHECK(audio.control_count==0);
    CHECK(module.host.beam_effects(world,.56,{}).shots==1);CHECK(world.calls==1);
    const auto impulse=module.command(4,1.04,0);REQUIRE(impulse);CHECK(impulse->x==Catch::Approx(1000*.5/1.5));
    CHECK(module.host.sample(1.04).secondary_fire_starts==2);
}
TEST_CASE("E21 primary empty water missing semantic fields reload and uncaptured commands fail closed", "[e21][no-stock]") {
    for(unsigned mode=0;mode<6;++mode) {
        gf::Module m;
        if(mode==0)m.s.weapon_hud.reserve_ammo[1]=1;
        if(mode==1)m.s.receiving_client->water_level=3;
        if(mode==2)m.s.weapon_slots[0].auxiliary_integer2.reset();
        if(mode==3)m.s.receiving_client->auxiliary_float3.reset();
        m.s.publication_revision=2;m.host.observe(m.s,.001);
        m.command(1,.02,mode==4 ? 1U<<13 : mode==5 ? 0 : 1);
        CHECK(m.host.sample(.02).actions_started==0);gf::World w;CHECK(m.host.beam_effects(w,.02,{}).shots==0);
    }
}
TEST_CASE("E21 reflection is bounded and primary never penetrates", "[e21][no-stock]") {
    gf::Module m;m.command(1,.02,1);gf::World w;w.reflection=true;const auto b=m.host.beam_effects(w,.02,{});
    CHECK(b.reflections==1);CHECK(b.penetrations==0);CHECK(w.calls==2);
    auto invalid=gf::World{};invalid.invalid=true;gf::Module n;n.command(1,.02,1);CHECK(n.host.beam_effects(invalid,.02,{}).count==0);
}
TEST_CASE("E21 Gauss local and remote profiles own bright yellow white solid streaks and surface normals", "[e21][game-api][visual-profile][no-stock]") {
    using V=hlclient::assets::AssetVector3;
    const gf::api::BeamSpriteFrame neutral;
    CHECK(neutral.geometry==gf::api::WorldLineGeometry::flat_billboard);
    CHECK(neutral.travel_speed_units_per_second==0);CHECK(neutral.segment_length_units==0);
    CHECK(neutral.emission_gain==1);CHECK_FALSE(neutral.surface_normal);
    for(const bool remote:{false,true})for(const bool primary:{true,false}) {
        CAPTURE(remote,primary);gf::Module module;const double at=remote ? .03 : primary ? .02 : 1.62;
        auto event=gf::event();event.boolean_parameter1=primary;event.float_parameter1=primary ? 20.F : 200.F;
        if(remote)module.host.committed_scripted_event(event);
        else if(primary)module.command(1,at,1);
        else {module.command(1,.02,1U<<11);module.command(2,.53,1U<<11);REQUIRE(module.command(3,at,0));}
        gf::World world;world.penetration=!primary;const auto beam=module.host.beam_effects(world,at,{});
        REQUIRE(beam.count>1);CHECK(beam.shots==1);CHECK(beam.impact_count==(primary ? 1U : 2U));
        std::size_t stationary{},moving{},ribbons{};bool entry{},exit{};
        for(const auto& item:std::span{beam.sprites.data(),beam.count}) {
            if(item.ribbon) {
                ++ribbons;CHECK(item.geometry==gf::api::WorldLineGeometry::solid_3d);
                CHECK(item.travel_speed_units_per_second==12000);CHECK(item.segment_length_units==144);
                CHECK(item.head_taper_fraction==Catch::Approx(.90));CHECK(item.taper_end_diameter_fraction==Catch::Approx(.20));
                CHECK(item.emission_gain==3);CHECK(item.opacity==1);
                CHECK(item.width==Catch::Approx(remote ? (primary ? 2.975 : 5.95) : (primary ? 1.75 : 3.5)));
                CHECK(item.expires-item.starts==Catch::Approx(.15));CHECK_FALSE(item.surface_normal);
                CHECK((item.color==(primary ? std::array<float,3>{1,1,0} : std::array<float,3>{1,1,1})));
                CHECK(item.head_glow.radius_units==Catch::Approx(2.5));CHECK(item.head_glow.light_radius_units==64);
                CHECK(item.head_glow.light_intensity==1);CHECK(gf::api::valid_transient_glow(item.head_glow));
            } else if(std::hypot(item.velocity.x,item.velocity.y,item.velocity.z)>0) {
                ++moving;CHECK_FALSE(item.surface_normal);
            } else {
                ++stationary;REQUIRE(item.surface_normal);
                CHECK(item.surface_normal->y==Catch::Approx(0));CHECK(item.surface_normal->z==Catch::Approx(0));
                CHECK(std::abs(item.surface_normal->x)==Catch::Approx(1));
                if(item.start.x==90) {entry=true;CHECK(item.surface_normal->x==Catch::Approx(1));}
                if(item.start.x==60) {exit=true;CHECK(item.surface_normal->x==Catch::Approx(-1));}
            }
        }
        CHECK(ribbons>=1);CHECK(stationary>=2);CHECK(moving>0);CHECK(entry);CHECK(exit==!primary);
        const auto query_count=world.calls;
        if(remote)module.host.committed_scripted_event(event);
        else module.command(primary ? 1 : 3,at,primary ? 1 : 0);
        const auto repeated=module.host.beam_effects(world,at+.001,V{999,999,999});
        CHECK(repeated.shots==1);CHECK(repeated.impact_count==0);CHECK(world.calls==query_count);
        REQUIRE(repeated.count>0);CHECK(repeated.sprites[0].start.x==beam.sprites[0].start.x);
        CHECK(repeated.sprites[0].end.x==beam.sprites[0].end.x);
        const auto extended=module.host.beam_effects(world,at+.125,{});
        CHECK(static_cast<std::size_t>(std::ranges::count_if(std::span{extended.sprites.data(),extended.count},[](const auto& item){return item.ribbon;}))==ribbons);
        CHECK(extended.shots==1);CHECK(extended.impact_count==0);CHECK(world.calls==query_count);
        const auto expired=module.host.beam_effects(world,at+.151,{});
        CHECK(std::ranges::none_of(std::span{expired.sprites.data(),expired.count},[](const auto& item){return item.ribbon;}));
        CHECK(expired.shots==1);CHECK(expired.impact_count==0);CHECK(world.calls==query_count);
        module.host.reset({2,2,1});const auto reset=module.host.beam_effects(world,at+.152,{});
        CHECK(reset.count==0);CHECK(reset.impact_count==0);
    }
    gf::Module grazing;grazing.command(1,.02,1);gf::World world;world.reflection=true;
    const auto reflected=grazing.host.beam_effects(world,.02,{});CHECK(reflected.reflections==1);
    const auto sprites=std::span{reflected.sprites.data(),reflected.count};
    const auto surface=std::ranges::find_if(sprites,[](const auto& item){return item.surface_normal.has_value();});
    REQUIRE(surface!=sprites.end());
    CHECK(surface->surface_normal->x==Catch::Approx(.3));CHECK(surface->surface_normal->y==Catch::Approx(.9539392));
}
TEST_CASE("E21 remote Gauss restores SDK model pitch for inherited and explicit angles", "[e21][gauss-remote-aim][remote-effects][game-api][no-stock]") {
    using V=hlclient::assets::AssetVector3;
    struct Literal {V angles,forward;};
    // SDK Gauss publishes pev->angles, not v_angle, even when explicitly
    // transmitted. Provenance changes storage, not this weapon's pitch units.
    const std::array<Literal,9> literals{{
        {{340,0,0},{.5F,0,-.8660254F}},
        {{-20,90,0},{0,.5F,-.8660254F}},
        {{20,180,0},{-.5F,0,.8660254F}},
        {{380,90,0},{0,.5F,.8660254F}},
        {{330,0,0},{0,0,-1}},
        {{-30,0,0},{0,0,-1}},
        {{30,0,0},{0,0,1}},
        {{390,0,0},{0,0,1}},
        {{0,180,0},{-1,0,0}}
    }};
    struct Query final:gf::api::IStaticEffectTrace {
        mutable V eye{},target{};mutable unsigned calls{};
        gf::api::StaticEffectTrace trace(V a,V b) const noexcept override {
            ++calls;eye=a;target=b;return {true,false,false,false,false,b,{}};
        }
    };
    for(const bool primary:{true,false})for(const bool ducking:{false,true})for(const bool inherited:{true,false})for(const auto& literal:literals) {
        CAPTURE(primary,ducking,inherited,literal.angles.x,literal.angles.y);
        gf::Module module;auto event=gf::event();event.origin={40,50,60};event.angles=literal.angles;
        event.angles_from_entity=inherited;event.ducking=ducking;
        event.boolean_parameter1=primary;event.float_parameter1=primary ? 20.F : 200.F;
        module.host.committed_scripted_event(event);Query query;
        const auto beam=module.host.beam_effects(query,.03,V{999,999,999});
        REQUIRE(query.calls==1);REQUIRE(beam.count==1);CHECK(beam.shots==1);CHECK(beam.impact_count==0);
        CHECK(query.eye.x==40);CHECK(query.eye.y==50);CHECK(query.eye.z==(ducking ? 72.F : 88.F));
        CHECK((query.target.x-query.eye.x)/8192==Catch::Approx(literal.forward.x).margin(1e-5));
        CHECK((query.target.y-query.eye.y)/8192==Catch::Approx(literal.forward.y).margin(1e-5));
        CHECK((query.target.z-query.eye.z)/8192==Catch::Approx(literal.forward.z).margin(1e-5));
        CHECK(beam.sprites[0].start.x==query.eye.x);CHECK(beam.sprites[0].start.z==query.eye.z);
        CHECK(beam.sprites[0].end.z==query.target.z);CHECK(beam.sprites[0].width==Catch::Approx(primary ? 2.975 : 5.95));
        CHECK(beam.sprites[0].expires-beam.sprites[0].starts==Catch::Approx(.15));
        CHECK(module.host.sample(.03).local_punch_pitch_degrees==0);
        REQUIRE(module.host.drain_audio().count==1);
        module.host.committed_scripted_event(event);const auto repeated=module.host.beam_effects(query,.031,{});
        CHECK(query.calls==1);CHECK(repeated.shots==1);CHECK(repeated.impact_count==0);CHECK(module.host.drain_audio().count==0);
        module.host.reset({2,2,1});module.host.committed_scripted_event(event);
        CHECK(module.host.beam_effects(query,.032,{}).count==0);CHECK(query.calls==1);
    }
}
TEST_CASE("E21 ten interaction one penetration and multiplayer vertical recoil bounds are independent", "[e21][prediction][no-stock]") {
    using V=hlclient::assets::AssetVector3;
    struct Grazing final:gf::api::IStaticEffectTrace {
        mutable unsigned calls{};
        gf::api::StaticEffectTrace trace(V a,V) const noexcept override {++calls;return {true,false,false,true,false,{a.x-100,a.y,a.z},{0,1,0}};}
    } grazing;
    gf::Module full;full.command(1,.02,1U<<11);full.command(2,.53,1U<<11);REQUIRE(full.command(3,1.62,0));
    const auto reflected=full.host.beam_effects(grazing,1.62,{});CHECK(grazing.calls==10);CHECK(reflected.reflections==10);CHECK(reflected.penetrations==0);
    struct TwoWalls final:gf::api::IStaticEffectTrace {
        mutable unsigned calls{};
        gf::api::StaticEffectTrace trace(V,V b) const noexcept override {
            const auto n=calls++;if(n==1)return {true,false,false,false,false,b,{}};
            return {true,false,false,true,false,{n==0 ? 90.F : n==2 ? 60.F : 30.F,80,80},{n==2 ? -1.F : 1.F,0,0}};
        }
    } walls;
    gf::Module second;second.command(1,.02,1U<<11);second.command(2,.53,1U<<11);REQUIRE(second.command(3,1.62,0));
    const auto punched=second.host.beam_effects(walls,1.62,{});CHECK(walls.calls==4);CHECK(punched.penetrations==1);CHECK(punched.impact_count==3);
    for(const unsigned clients:{1U,8U}) {
        gf::Module m(clients);const unsigned steps=clients==1 ? 201U : 76U;
        for(unsigned sequence=1;sequence<steps;++sequence) {auto c=gf::f::command(sequence,sequence*.02,1U<<11);c.shot_context->direction={0,0,-1};CHECK_FALSE(m.host.command_movement_effect(c));(void)m.host.drain_audio();}
        auto c=gf::f::command(steps,steps*.02,0);c.shot_context->direction={0,0,-1};const auto impulse=m.host.command_movement_effect(c);
        REQUIRE(impulse);CHECK(impulse->z==Catch::Approx(clients==1 ? 0 : 1000));CHECK(m.host.hud(m.s,c.sample_end_seconds).primary_reserve==100);
    }
}
TEST_CASE("E21 remote assigned event energy spin change and stop do not create observer recoil", "[e21][remote-effects][no-stock]") {
    gf::Module m;auto spin=gf::event();spin.event_index=901;spin.integer_parameter1=110;spin.boolean_parameter1=false;
    m.host.committed_scripted_event(spin);auto a=m.host.drain_audio();REQUIRE(a.control_count==1);CHECK(a.controls[0].identity!=0);CHECK(a.controls[0].world_origin.has_value());
    spin.record=spin.record_ordinal=3;spin.integer_parameter1=225;spin.boolean_parameter1=true;
    m.host.committed_scripted_event(spin);a=m.host.drain_audio();REQUIRE(a.control_count==1);CHECK(a.controls[0].operation==gf::api::LocalVoiceControl::Operation::change);
    auto fire=gf::event(2,4);fire.float_parameter1=200.0F;fire.boolean_parameter1=false;m.host.committed_scripted_event(fire);m.host.committed_scripted_event(fire);
    a=m.host.drain_audio();CHECK(a.count==1);CHECK(a.control_count==1);CHECK(m.host.sample(.03).local_punch_pitch_degrees==0);
    gf::World w;CHECK(m.host.beam_effects(w,.03,{}).shots==1);
    auto stop=gf::event(2,5);stop.float_parameter1.reset();stop.boolean_parameter2=true;stop.fire_delay_ticks=1;stop.resolution=gf::api::ScriptedEventResolution::unsupported_delay;
    m.host.committed_scripted_event(stop);a=m.host.drain_audio();CHECK(a.count==0);CHECK(a.control_count==0); // main fire already stopped this charge
    CHECK(m.host.beam_effects(w,.03,{}).shots==1);
}
TEST_CASE("E21 local event confirms but never duplicates normal prediction", "[e21][no-stock]") {
    gf::Module m;m.command(1,.02,1);(void)m.host.drain_audio();auto e=gf::event(1);m.host.committed_scripted_event(e);
    CHECK(m.host.sample(.03).status==gf::api::LocalWeaponActionStatus::server_confirmed);CHECK(m.host.drain_audio().count==0);
    gf::World w;CHECK(m.host.beam_effects(w,.03,{}).shots==1);m.host.reset({2,2,1});CHECK(m.host.beam_effects(w,.04,{}).count==0);
}
TEST_CASE("E21 queued world shot does not borrow another weapon muzzle and dry charged clicks obey cooldown", "[e21][lifecycle][no-stock]") {
    gf::Module m;m.command(1,.02,1);m.host.bind_model(gf::f::model());gf::World wall;
    const auto shot=m.host.beam_effects(wall,.03,hlclient::assets::AssetVector3{999,999,999});REQUIRE(shot.count>0);CHECK(shot.sprites[0].start.x==100);CHECK(shot.shots==1);
    gf::Module dry;dry.s.publication_revision=2;dry.s.weapon_hud.reserve_ammo[1]=1;dry.host.observe(dry.s,0);
    dry.command(1,.02,1U<<11);(void)dry.host.drain_audio();dry.command(2,.04,1);CHECK(dry.host.drain_audio().count==1);
    dry.command(3,.06,1);CHECK(dry.host.drain_audio().count==0);CHECK(dry.host.sample(.06).actions_started==0);
    gf::Module missing;missing.command(1,.02,1U<<11);(void)missing.host.drain_audio();
    missing.s.publication_revision=2;missing.s.receiving_client->auxiliary_float3.reset();missing.host.observe(missing.s,.1);
    CHECK_FALSE(missing.host.sample(.1).visual);CHECK(missing.host.drain_audio().control_count==1);
    CHECK_FALSE(missing.command(2,.6,0));gf::World no_shot;CHECK(missing.host.beam_effects(no_shot,.6,{}).shots==0);
}
TEST_CASE("E21 approved Gauss asset metadata read only", "[e21][installed-gauss]") {
    std::filesystem::path root;
#ifdef _WIN32
    wchar_t* value=nullptr;std::size_t count=0;
    if(_wdupenv_s(&value,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && value)root=value;std::free(value);
#endif
    if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())
        SKIP("No exact opt-in approved research root");
    namespace fixture=hlclient::tests;namespace app=hlclient::app;
    auto resources=fixture::readiness_fixture::parse_resource_list({{2,"maps/crossfire.bsp",9,0,0},
        {2,"models/v_gauss.mdl",73,0,0},{2,"models/p_gauss.mdl",71,0,0},
        {2,"models/w_gauss.mdl",74,0,0},{2,"sprites/smoke.spr",75,0,0},{2,"sprites/hotglow.spr",76,0,0}});
    auto made=app::RuntimeReplayLocalAssets::create(resources,fixture::readiness_fixture::parse_server_info("maps/crossfire.bsp"),root,"valve",
        app::ReplayLocalCameraPolicy::external_live_receiving_client);
    REQUIRE(made.projection);gf::c::ClientWorldState world;
    auto initial=gf::state();initial.entity_metadata={1,gf::c::RuntimeObservationFreshness::observed_in_record,
        gf::c::RuntimeObservationCompleteness::complete_reconstruction,gf::c::RuntimeObservationSource{1,1,0,0,8}};
    initial.server_time_metadata=initial.client_metadata=initial.entity_metadata;initial.server_time_seconds=0;
    initial.canonical_state_hash=gf::c::runtime_observation_canonical_hash(initial);
    const auto reset=made.projection->reset_generation(initial,world);INFO((reset.error ? reset.error->context : "ready"));REQUIRE(reset);
    const auto meta=made.projection->presentation_model(1,73);REQUIRE(meta);
    CHECK(hlclient::games::halflife::gauss_sequences(*meta));
    for(std::size_t i=0;i<meta->sequences.size();++i)std::cout<<"gauss-sequence "<<i<<" "<<meta->sequences[i].label<<" "<<meta->sequences[i].frame_count<<" "<<meta->sequences[i].fps<<"\n";
    for(unsigned i=0;i<3;++i)if(meta->supported_bodies[i])std::cout<<"gauss-body "<<i<<"\n";
    const auto p=made.projection->studio_model_data(71);REQUIRE(p);
    gf::Module module;const auto plan=module.host.remote_weapon_socket_plan({71,{},"models/p_gauss.mdl",*p});
    CHECK(plan.semantic_key==9);CHECK(plan.source==gf::api::RemoteSocketSource::validated_pmodel_socket);
    for(std::size_t i=0;i<p->bones.size();++i) {
        const auto& b=p->bones[i];std::cout<<"gauss-p-bone "<<i<<" "<<b.name<<" "<<b.default_translation.x<<","<<b.default_translation.y<<","<<b.default_translation.z<<" "<<b.default_rotation_radians.x<<","<<b.default_rotation_radians.y<<","<<b.default_rotation_radians.z<<"\n";
    }
    hlclient::assets::AssetVector3 lo{1e9F,1e9F,1e9F},hi{-1e9F,-1e9F,-1e9F};
    for(const auto& sub:p->submodels)for(const auto& v:sub.vertices) {
        lo.x=std::min(lo.x,v.source_position.x);lo.y=std::min(lo.y,v.source_position.y);lo.z=std::min(lo.z,v.source_position.z);
        hi.x=std::max(hi.x,v.source_position.x);hi.y=std::max(hi.y,v.source_position.y);hi.z=std::max(hi.z,v.source_position.z);
    }
    std::cout<<"gauss-p-bounds "<<lo.x<<","<<lo.y<<","<<lo.z<<" "<<hi.x<<","<<hi.y<<","<<hi.z<<"\n";
    std::cout<<"gauss-p-attachments "<<p->attachments.size()<<"\n";
    const auto v=made.projection->studio_model_data(73);REQUIRE(v);
    std::cout<<"gauss-v-attachments "<<v->attachments.size()<<"\n";
    for(const auto& a:v->attachments)std::cout<<"gauss-v-attachment "<<a.name<<" "<<a.bone_index<<" "<<a.local_origin.x<<","<<a.local_origin.y<<","<<a.local_origin.z<<"\n";
    REQUIRE(made.projection->studio_model_data(74));
    hlclient::goldsrc::ApprovedSoundAssets loader(made.projection->sound_resources());gauss_assets::ready(loader);gauss_assets::audio(loader);gauss_assets::gpu(*made.projection);
}
TEST_CASE("E21 render sampling rates cannot change charge actions pitch restart or impulse", "[e21][prediction][no-stock]") {
    for(const double fps:{30.,60.,144.,240.}) {
        gf::Module m;unsigned command=0;double sample=0;
        for(unsigned step=1;step<=80;++step) {
            const auto at=step*.02;m.command(++command,at,1U<<11);
            while(sample+1/fps<=at) {sample+=1/fps;(void)m.host.sample(sample);}
            (void)m.host.drain_audio();
        }
        const auto restart=m.host.sample(1.6).visual->restart_identity;
        CHECK(restart==2);const auto impulse=m.command(++command,1.62,0);REQUIRE(impulse);CHECK(impulse->x==Catch::Approx(1000));
        CHECK(m.host.sample(1.62).actions_started==1);CHECK_FALSE(m.command(command,1.62,0));
    }
}
TEST_CASE("E21 partial charge depleted ammo underwater overcharge death and holster are bounded", "[e21][lifecycle][no-stock]") {
    SECTION("partial release") {gf::Module m;m.command(1,.02,1U<<11);m.command(2,.53,1U<<11);const auto impulse=m.command(3,.62,0);REQUIRE(impulse);CHECK(impulse->x==Catch::Approx(400));}
    SECTION("timer eligibility decays without render or another record") {gf::Module m;m.s.publication_revision=2;m.s.receiving_client->next_weapon_attack=.1;m.host.observe(m.s,0);m.command(1,.02,1);CHECK(m.host.sample(.02).actions_started==0);m.command(2,.12,1);CHECK(m.host.sample(.12).actions_started==1);}
    SECTION("last ammo forces release") {gf::Module m;m.s.weapon_hud.reserve_ammo[1]=1;m.s.publication_revision=2;m.host.observe(m.s,0);m.command(1,.02,1U<<11);m.command(2,.53,1U<<11);REQUIRE(m.command(3,.55,1U<<11));CHECK(m.host.sample(.55).secondary_fire_starts==1);CHECK(m.host.hud(m.s,.55).primary_reserve==1);}
    SECTION("water cancel") {gf::Module m;m.command(1,.02,1U<<11);(void)m.host.drain_audio();m.s.receiving_client->water_level=3;m.s.publication_revision=2;m.host.observe(m.s,.1);const auto b=m.host.drain_audio();CHECK(b.control_count==1);CHECK(b.count==1);CHECK(b.cues[0].reference.name()=="weapons/electro4.wav");gf::World w;CHECK(m.host.beam_effects(w,.1,{}).shots==0);}
    SECTION("overcharge has no local health authority") {gf::Module m;m.command(1,.02,1U<<11);m.command(2,.53,1U<<11);(void)m.host.drain_audio();m.command(3,10.04,1U<<11);const auto b=m.host.drain_audio();CHECK(b.control_count==1);CHECK(b.count==2);CHECK(m.host.hud(m.s,10.04).health==100);CHECK(m.host.sample(10.04).secondary_fire_starts==0);}
    for(const bool death:{false,true}) {
        gf::Module m;m.command(1,.02,1U<<11);(void)m.host.drain_audio();
        if(death) {m.s.publication_revision=2;m.s.receiving_client->health=0;m.host.observe(m.s,.1);}else m.host.bind_model(gf::f::model());
        CHECK(m.host.drain_audio().control_count==1);CHECK_FALSE(m.command(2,.6,0));gf::World w;CHECK(m.host.beam_effects(w,.6,{}).shots==0);
    }
}
TEST_CASE("E21 fresh negotiated phase burn and full-charge timers reconcile without retained timer drift", "[e21][prediction][no-stock]") {
    gf::Module m;m.command(1,.02,1U<<11);(void)m.host.drain_audio();
    m.s.publication_revision=2;m.s.weapon_slots[0].auxiliary_integer2=2;
    m.s.receiving_client->auxiliary_float3=.5;m.s.receiving_client->auxiliary_float2=.1;
    m.host.observe(m.s,.4);CHECK(m.host.sample(.4).visual->sequence==4);
    m.s.publication_revision=3;m.s.client_metadata.freshness=gf::c::RuntimeObservationFreshness::retained;
    m.host.observe(m.s,.8);const auto delta=m.command(2,.9,0);REQUIRE(delta);CHECK(delta->x==Catch::Approx(1000));
    CHECK(m.host.hud(m.s,.9).primary_reserve==100);
    gf::Module resumed;resumed.s.publication_revision=2;resumed.s.weapon_slots[0].auxiliary_integer2=2;
    resumed.s.receiving_client->auxiliary_float3=.3;resumed.s.receiving_client->auxiliary_float2=1000;
    resumed.host.observe(resumed.s,1);CHECK(resumed.host.sample(1).visual->sequence==4);
    const auto fire=resumed.command(1,1.1,0);REQUIRE(fire);CHECK(fire->x==Catch::Approx(200*1.3/1.5*5));
}
TEST_CASE("E32 Gauss last one two or three charges automatically discharge once while secondary remains held",
          "[e32-gauss-last-ammo][e21][prediction][game-api][no-stock]") {
    namespace lm=hlclient::tests::local_movement;
    for(const unsigned ammunition:{1U,2U,3U}) {
        CAPTURE(ammunition);gf::Module module;
        module.s=gf::state(static_cast<int>(ammunition));module.s.publication_revision=2;
        module.host.observe(module.s,0);
        CHECK(module.host.command_movement_effect_phase()==gf::api::CommandMovementEffectPhase::after_movement);
        CHECK_FALSE(module.command(1,.02,1U<<11));
        const auto start=module.host.drain_audio();REQUIRE(start.control_count==1);
        CHECK(start.controls[0].operation==gf::api::LocalVoiceControl::Operation::start);
        CHECK_FALSE(module.command(2,.53,1U<<11));(void)module.host.drain_audio();
        auto impulse=module.command(3,.55,1U<<11);unsigned firing_sequence=3;double firing_time=.55;
        if(ammunition==3) {
            CHECK_FALSE(impulse);(void)module.host.drain_audio();
            impulse=module.command(4,.66,1U<<11);firing_sequence=4;firing_time=.66;
        }
        REQUIRE(impulse);
        CHECK(impulse->x==Catch::Approx(1000*(firing_time-.02)/1.5));
        CHECK(impulse->y==0);CHECK(impulse->z==0);
        const auto presentation=module.host.sample(firing_time);REQUIRE(presentation.identity);
        CHECK(presentation.identity->command_sequence==firing_sequence);
        CHECK(presentation.identity->kind==gf::api::LocalWeaponAction::secondary_fire);
        CHECK(presentation.actions_started==1);CHECK(presentation.secondary_fire_starts==1);
        CHECK(presentation.primary_fire_starts==0);REQUIRE(presentation.visual);
        CHECK(presentation.visual->sequence==6);
        // Reserve remains authoritative. Provisional consumption gates the
        // action but cannot rewrite the HUD/server observation.
        CHECK(module.host.hud(module.s,firing_time).primary_reserve==ammunition);
        const auto audio=module.host.drain_audio();REQUIRE(audio.count==1);
        CHECK(audio.cues[0].reference.name()=="weapons/gauss2.wav");
        REQUIRE(audio.control_count==1);
        CHECK(audio.controls[0].operation==gf::api::LocalVoiceControl::Operation::stop);
        CHECK_FALSE(module.command(firing_sequence,firing_time,1U<<11));
        module.host.submit(gf::f::command(firing_sequence,firing_time,1U<<11),firing_time);
        CHECK(module.host.drain_audio().count==0);
        CHECK_FALSE(module.command(firing_sequence+1,firing_time+.02,1U<<11));
        const auto during_cooldown=module.host.drain_audio();CHECK(during_cooldown.count==0);
        CHECK(during_cooldown.control_count==0);
        gf::World wall;const auto beam=module.host.beam_effects(wall,firing_time,{});
        CHECK(beam.shots==1);CHECK(beam.penetrations==1);
        // This secondary fixture permits one entry/exit and then a second
        // surface. Three impacts remain sub-events of the same single shot.
        REQUIRE(beam.impact_count==3);
        for(std::size_t index=0;index<beam.impact_count;++index) {
            auto expected=*presentation.identity;
            expected.sub_identity=static_cast<std::uint32_t>(index+1);
            CHECK(beam.impacts[index].action==expected);
        }
        const auto queries=wall.calls;
        CHECK(module.host.beam_effects(wall,firing_time+.001,{}).impact_count==0);
        CHECK(wall.calls==queries);
        auto echo=gf::event(1,3);echo.received_at_seconds=firing_time+.01;
        echo.boolean_parameter1=false;echo.float_parameter1=static_cast<float>(200*(firing_time-.02)/1.5);
        module.host.committed_scripted_event(echo);module.host.committed_scripted_event(echo);
        CHECK(module.host.sample(firing_time+.01).actions_confirmed==1);
        const auto confirmed_audio=module.host.drain_audio();CHECK(confirmed_audio.count==0);
        CHECK(confirmed_audio.control_count==0);
        const auto confirmed_beam=module.host.beam_effects(wall,firing_time+.01,{});
        CHECK(confirmed_beam.shots==1);CHECK(confirmed_beam.impact_count==0);CHECK(wall.calls==queries);
        // Apply the exact module result through the production neutral command
        // and kernel. Append/replay share this sidecar, never resubmit the game action.
        const auto command=lm::make_command(1,20,100);
        const auto with=command.with_post_velocity_impulse({impulse->x,impulse->y,impulse->z});REQUIRE(with);
        CHECK(with.state->velocity_impulse()==std::array<float,3>{});
        lm::DeterministicLocalMovementCollision collision(false);
        const auto initial=lm::make_state({0,0,128},{20,0,0},hlclient::movement::PlayerMovementMode::airborne);
        const auto baseline=lm::simulate(initial,command,collision);REQUIRE(baseline);
        const auto predicted=lm::simulate(initial,*with.state,collision);REQUIRE(predicted);
        const auto replayed=lm::simulate(initial,*with.state,collision);REQUIRE(replayed);
        CHECK(predicted.state->origin().x==baseline.state->origin().x);
        CHECK(predicted.state->origin().z==baseline.state->origin().z);
        CHECK(predicted.state->velocity().x==Catch::Approx(baseline.state->velocity().x+impulse->x));
        CHECK(predicted.deterministic_state_signature==replayed.deterministic_state_signature);
        CHECK(module.host.drain_audio().count==0);
    }
}
TEST_CASE("E32 fresh Gauss charging observation preserves last-ammo auto discharge eligibility",
          "[e32-gauss-last-ammo][e21][prediction][no-stock]") {
    for(const unsigned ammunition:{1U,2U,3U}) {
        CAPTURE(ammunition);gf::Module module;
        module.s=gf::state(static_cast<int>(ammunition));module.s.publication_revision=2;module.host.observe(module.s,0);
        CHECK_FALSE(module.command(1,.02,1U<<11));(void)module.host.drain_audio();
        CHECK_FALSE(module.command(2,.53,1U<<11));(void)module.host.drain_audio();
        // New committed source observes the charge after its original debit.
        // This is not retained-timer drift and does not invent local ammo.
        module.s.publication_revision=3;module.s.weapon_slots[0].auxiliary_integer2=2;
        module.s.weapon_hud.reserve_ammo[1]=static_cast<std::uint8_t>(ammunition==3 ? 1 : 0);
        module.s.receiving_client->auxiliary_float3=1;module.s.receiving_client->auxiliary_float2=.01;
        module.host.observe(module.s,.54);CHECK(module.host.sample(.54).visual->sequence==4);
        const auto impulse=module.command(3,.56,1U<<11);REQUIRE(impulse);
        CHECK(impulse->x==Catch::Approx(1000*.54/1.5));
        CHECK(module.host.sample(.56).secondary_fire_starts==1);
        const auto fired=module.host.drain_audio();REQUIRE(fired.count==1);
        CHECK(fired.cues[0].reference.name()=="weapons/gauss2.wav");
        CHECK_FALSE(module.command(3,.56,1U<<11));CHECK(module.host.drain_audio().count==0);
        CHECK(module.host.hud(module.s,.56).primary_reserve==(ammunition==3 ? 1U : 0U));
    }
}
TEST_CASE("E32 delayed Gauss charging rows cannot resurrect the completed last-ammo shot during cooldown",
          "[e32-gauss-last-ammo][e21][prediction][lifecycle][no-stock]") {
    gf::Module module;module.s=gf::state(1);module.s.publication_revision=2;module.host.observe(module.s,0);
    module.command(1,.02,1U<<11);(void)module.host.drain_audio();
    module.command(2,.53,1U<<11);(void)module.host.drain_audio();
    REQUIRE(module.command(3,.55,1U<<11));(void)module.host.drain_audio();
    const auto action=module.host.sample(.55).identity;REQUIRE(action);
    const auto firing_restart=module.host.sample(.55).visual->restart_identity;
    for(const double at:{.60,.64}) {
        module.s.publication_revision++;
        module.s.weapon_hud.reserve_ammo[1]=0;module.s.weapon_slots[0].auxiliary_integer2=2;
        module.s.receiving_client->auxiliary_float2=.1;module.s.receiving_client->auxiliary_float3=.9;
        module.host.observe(module.s,at);
        const auto frame=module.host.sample(at);REQUIRE(frame.visual);
        CHECK(frame.visual->sequence==6);CHECK(frame.visual->restart_identity==firing_restart);
        CHECK(frame.identity==action);CHECK(frame.secondary_fire_starts==1);
        const auto batch=module.host.drain_audio();CHECK(batch.count==0);CHECK(batch.control_count==0);
    }
    CHECK_FALSE(module.command(4,.66,1U<<11));
    const auto after=module.host.drain_audio();CHECK(after.count==0);CHECK(after.control_count==0);
    CHECK(module.host.sample(.66).secondary_fire_starts==1);
    module.s.publication_revision++;module.s.weapon_slots[0].auxiliary_integer2=0;
    module.s.receiving_client->auxiliary_float2=module.s.receiving_client->auxiliary_float3=0;
    module.host.observe(module.s,.68);CHECK(module.host.sample(.68).identity==action);
    CHECK(module.host.sample(.68).secondary_fire_starts==1);CHECK(module.host.drain_audio().count==0);
    // The guard is action-local, not a permanent ban on future charges.
    module.s.publication_revision++;module.s.weapon_hud.reserve_ammo[1]=3;module.host.observe(module.s,1.6);
    CHECK_FALSE(module.command(5,1.62,1U<<11));CHECK(module.host.sample(1.62).visual->sequence==3);
    const auto fresh_charge=module.host.drain_audio();REQUIRE(fresh_charge.control_count==1);
    CHECK(fresh_charge.controls[0].operation==gf::api::LocalVoiceControl::Operation::start);
}
TEST_CASE("E32 delayed charging prefix cannot postpone the command-owned Gauss last-ammo burn deadline",
          "[e32-gauss-last-ammo][e21][prediction][lifecycle][no-stock]") {
    gf::Module module;module.s=gf::state(3);module.s.publication_revision=2;module.host.observe(module.s,0);
    CHECK_FALSE(module.command(1,.02,1U<<11));(void)module.host.drain_audio();
    CHECK_FALSE(module.command(2,.53,1U<<11));(void)module.host.drain_audio();
    CHECK_FALSE(module.command(3,.56,1U<<11));(void)module.host.drain_audio();
    // A server prefix from before the command-owned .66 burn arrives at .65.
    // Its positive remaining burn/full timers may reconcile an earlier due
    // time, but receipt delay cannot push this already-owned deadline later.
    module.s.publication_revision=3;module.s.weapon_slots[0].auxiliary_integer2=2;
    module.s.weapon_hud.reserve_ammo[1]=1;
    module.s.receiving_client->auxiliary_float2=.08;
    module.s.receiving_client->auxiliary_float3=1;
    module.host.observe(module.s,.65);(void)module.host.drain_audio();
    const auto automatic=module.command(4,.66,1U<<11);REQUIRE(automatic);
    CHECK(automatic->x==Catch::Approx(1000*.64/1.5));
    const auto fired=module.host.sample(.66);REQUIRE(fired.identity);
    CHECK(fired.identity->command_sequence==4);CHECK(fired.secondary_fire_starts==1);
    const auto audio=module.host.drain_audio();REQUIRE(audio.count==1);
    CHECK(audio.cues[0].reference.name()=="weapons/gauss2.wav");
    REQUIRE(audio.control_count==1);
    CHECK(audio.controls[0].operation==gf::api::LocalVoiceControl::Operation::stop);
    // The later phase-zero row confirms authoritative depletion; it does not
    // create a second movement impulse, presentation action or sound.
    module.s.publication_revision=4;module.s.weapon_slots[0].auxiliary_integer2=0;
    module.s.weapon_hud.reserve_ammo[1]=0;
    module.s.receiving_client->auxiliary_float2=module.s.receiving_client->auxiliary_float3=0;
    module.host.observe(module.s,.70);
    CHECK(module.host.sample(.70).identity==fired.identity);
    CHECK(module.host.sample(.70).actions_confirmed==1);
    CHECK_FALSE(module.command(4,.66,1U<<11));CHECK_FALSE(module.command(5,.72,1U<<11));
    const auto confirmed=module.host.drain_audio();CHECK(confirmed.count==0);CHECK(confirmed.control_count==0);
    CHECK(module.host.sample(.72).secondary_fire_starts==1);
    gf::World wall;CHECK(module.host.beam_effects(wall,.72,{}).shots==1);
    CHECK(module.host.beam_effects(wall,.721,{}).impact_count==0);
    SECTION("terminal authority without a provisional discharge never fabricates recoil") {
        gf::Module cancelled;cancelled.s=gf::state(3);cancelled.s.publication_revision=2;
        cancelled.host.observe(cancelled.s,0);cancelled.command(1,.02,1U<<11);
        cancelled.s.publication_revision=3;cancelled.s.weapon_slots[0].auxiliary_integer2=1;
        cancelled.s.receiving_client->auxiliary_float3=1.4;cancelled.host.observe(cancelled.s,.1);
        (void)cancelled.host.drain_audio();
        cancelled.s.publication_revision=4;cancelled.s.weapon_slots[0].auxiliary_integer2=0;
        cancelled.s.weapon_hud.reserve_ammo[1]=0;cancelled.host.observe(cancelled.s,.4);
        CHECK_FALSE(cancelled.command(2,.7,0));CHECK(cancelled.host.sample(.7).secondary_fire_starts==0);
        gf::World no_hit;CHECK(cancelled.host.beam_effects(no_hit,.7,{}).shots==0);
    }
}
TEST_CASE("E21 duplicate numeric binding and invalid spin origin do not create a voice", "[e21][remote-effects][no-stock]") {
    gf::Module m;std::array<gf::api::ScriptedEventBinding,2> bindings{};
    bindings[0].index=900;bindings[1].index=900;
    constexpr std::string_view gauss="events/gauss.sc",other="events/other.sc";
    std::copy(gauss.begin(),gauss.end(),bindings[0].name.begin());std::copy(other.begin(),other.end(),bindings[1].name.begin());
    m.host.configure_scripted_events(bindings);m.host.committed_scripted_event(gf::event());CHECK(m.host.drain_audio().count==0);
    gf::Module n;auto spin=gf::event();spin.event_index=901;spin.integer_parameter1=110;spin.boolean_parameter1=false;
    spin.origin.x=std::numeric_limits<float>::quiet_NaN();n.host.committed_scripted_event(spin);CHECK(n.host.drain_audio().control_count==0);
}
TEST_CASE("E21 remote lifetime stop-only delayed event independent emitters and stale generation", "[e21][audio][lifecycle][no-stock]") {
    gf::Module m;auto a=gf::event(2,2);a.event_index=901;a.integer_parameter1=110;a.boolean_parameter1=false;
    m.host.committed_scripted_event(a);auto b=a;b.emitter_entity=3;b.record=b.record_ordinal=3;m.host.committed_scripted_event(b);
    const auto starts=m.host.drain_audio();REQUIRE(starts.control_count==2);CHECK(starts.controls[0].identity!=starts.controls[1].identity);
    auto stop=gf::event(2,4);stop.boolean_parameter2=true;stop.float_parameter1.reset();stop.fire_delay_ticks=1;
    stop.resolution=gf::api::ScriptedEventResolution::unsupported_delay;m.host.committed_scripted_event(stop);
    const auto stopped=m.host.drain_audio();REQUIRE(stopped.control_count==1);CHECK(stopped.controls[0].identity==starts.controls[0].identity);CHECK(stopped.controls[0].scheduled_seconds==Catch::Approx(.04));CHECK(stopped.count==0);
    m.host.remote_slot_boundary(3);CHECK(m.host.drain_audio().control_count==1);m.host.committed_scripted_event(b);CHECK(m.host.drain_audio().control_count==0);
    gf::World w;CHECK(m.host.beam_effects(w,.04,{}).shots==0);m.host.reset({2,2,1});m.host.committed_scripted_event(a);CHECK(m.host.drain_audio().control_count==0);
}
namespace gauss_assets {
namespace t=hlclient::tests;namespace api=hlclient::game_api;namespace app=hlclient::app;namespace g=hlclient::goldsrc;namespace c=hlclient::client;
std::vector<std::byte> viewmodel() {
    auto bytes=t::literal_minimal_goldsrc_studio_v10();const auto seqs=bytes.size(),count=gf::model().sequences.size();bytes.resize(seqs+count*188);
    t::studio_write_i32le(bytes,164,static_cast<int>(count));t::studio_write_i32le(bytes,168,static_cast<int>(seqs));
    t::studio_write_vector3(bytes,t::kSyntheticStudioVerticesOffset,16,-4,-5);
    t::studio_write_vector3(bytes,t::kSyntheticStudioVerticesOffset+12,16,4,-5);
    t::studio_write_vector3(bytes,t::kSyntheticStudioVerticesOffset+24,16,0,3);
    const auto metadata=gf::model();
    for(std::size_t i=0;i<count;++i) {
        const auto at=seqs+i*176,anim=seqs+count*176+i*12;const auto& seq=metadata.sequences[i];
        std::copy_n(bytes.begin()+t::kSyntheticStudioSequenceOffset,176,bytes.begin()+at);
        t::studio_write_fixed_string(bytes,at,32,seq.label);t::studio_write_f32le(bytes,at+32,static_cast<float>(seq.fps));
        t::studio_write_i32le(bytes,at+36,seq.looping ? 1 : 0);t::studio_write_i32le(bytes,at+56,static_cast<int>(seq.frame_count));
        t::studio_write_i32le(bytes,at+124,static_cast<int>(anim));
    }
    t::studio_write_i32le(bytes,72,static_cast<int>(bytes.size()));return bytes;
}
std::vector<std::byte> pmodel() {
    auto bytes=t::python_fixture::remote_model(true);unsigned bones{};for(unsigned i=0;i<4;++i)bones|=std::to_integer<unsigned>(bytes[144+i])<<(i*8);
    const auto at=bones+11*112;t::studio_write_fixed_string(bytes,at,32,"Box02");t::studio_write_vector3(bytes,at+64,8.02966F,.950843F,4.03907F);
    t::studio_write_vector3(bytes,at+76,1.54315F,.002126F,-3.04524F);
    t::studio_write_vector3(bytes,t::kSyntheticStudioVerticesOffset,-20.43F,-4.88F,-3.1F);
    t::studio_write_vector3(bytes,t::kSyntheticStudioVerticesOffset+12,15.84F,3.96F,-3.1F);
    t::studio_write_vector3(bytes,t::kSyntheticStudioVerticesOffset+24,-20.43F,0,3.53F);return bytes;
}
std::vector<std::byte> wav() {
    std::vector<std::byte> b;const auto text=[&](std::string_view s){for(char v:s)b.push_back(static_cast<std::byte>(v));};
    const auto number=[&](unsigned n,unsigned width=4){for(unsigned i=0;i<width;++i)b.push_back(static_cast<std::byte>((n>>(8*i))&255));};
    text("RIFF");number(36+4800);text("WAVEfmt ");number(16);number(1,2);number(1,2);number(22050);number(22050);number(1,2);number(8,2);text("data");number(4800);
    for(unsigned i=0;i<4800;++i)b.push_back(static_cast<std::byte>(i%32<16 ? 160 : 96));return b;
}
c::RuntimeClientObservationState initial() {
    auto s=gf::state();s.entity_metadata={1,c::RuntimeObservationFreshness::observed_in_record,c::RuntimeObservationCompleteness::complete_reconstruction,c::RuntimeObservationSource{1,1,0,0,8}};
    s.server_time_metadata=s.client_metadata=s.entity_metadata;s.server_time_seconds=0;s.canonical_state_hash=c::runtime_observation_canonical_hash(s);return s;
}
struct Owned {
    t::ScopedLocalResourceTestRoot root;std::unique_ptr<app::RuntimeReplayLocalAssets> assets;c::ClientWorldState world;
    Owned() {
        t::SyntheticBspBuilder bsp;
        const auto collision=t::literal_collision_goldsrc_bsp_v30();
        for(const auto id:{t::SyntheticBspLumpId::nodes,t::SyntheticBspLumpId::leaves,t::SyntheticBspLumpId::clipnodes,t::SyntheticBspLumpId::models}) {
            const auto descriptor=4+static_cast<std::size_t>(id)*8;
            const auto offset=t::synthetic_read_i32le(collision,descriptor),size=t::synthetic_read_i32le(collision,descriptor+4);
            bsp.lump(id).assign(collision.begin()+offset,collision.begin()+offset+size);
        }
        t::SyntheticBspNode floor;floor.children={-2,-1};bsp.set_nodes(std::array{floor});
        const std::string text="{\n\"classname\" \"worldspawn\"\n\"wad\" \"test.wad\"\n}\n";
        const auto eb=std::as_bytes(std::span{text});bsp.lump(t::SyntheticBspLumpId::entities).assign(eb.begin(),eb.end());
        t::SyntheticWad3Entry texture;texture.name="TEST_QUAD";texture.payload=t::synthetic_goldsrc_miptex("TEST_QUAD",64,64);
        root.write("valve","test.wad",t::synthetic_wad3({texture}).bytes);root.write("valve","maps/test.bsp",bsp.build());
        root.write("valve","models/v_gauss.mdl",viewmodel());root.write("valve","models/p_gauss.mdl",pmodel());root.write("valve","models/owned_player.mdl",t::python_fixture::remote_model(false));
        root.write("valve","sprites/smoke.spr",t::sprite_fixture::literal_single_sprite(1));
        // Independent black-neutral additive contact glow, with the same 64x64
        // extent as the approved original control. No private pixel data.
        auto glow=t::sprite_fixture::literal_header_and_palette(1,1,2,0,64,64);
        for(unsigned i=0;i<256;++i) {
            const auto at=t::sprite_fixture::kPaletteOffset+i*3;
            glow[at]=static_cast<std::byte>(i);glow[at+1]=static_cast<std::byte>(i*3/4);glow[at+2]=static_cast<std::byte>(i/8);
        }
        t::sprite_fixture::FrameSpec radial{-32,32,64,64,{}};
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
            const auto radius=std::hypot((static_cast<float>(x)+.5F-32)/32,(static_cast<float>(y)+.5F-32)/32);
            const auto gain=std::max(0.0F,1-radius);
            radial.pixels.push_back(static_cast<std::byte>(static_cast<unsigned>(255*gain*gain)));
        }
        t::sprite_fixture::append_single_entry(glow,radial);root.write("valve","sprites/hotglow.spr",glow);
        for(const auto& sound:hlclient::games::halflife::GaussPresentation::resources())root.write("valve",std::string{"sound/"}+std::string{sound.name()},wav());
        const auto resources=t::readiness_fixture::parse_resource_list({{2,"maps/test.bsp",9,0,0},{2,"models/v_gauss.mdl",73,0,0},{2,"models/p_gauss.mdl",71,0,0},{2,"models/owned_player.mdl",7,0,0},{2,"sprites/smoke.spr",75,0,0},{2,"sprites/hotglow.spr",76,0,0}});
        auto made=app::RuntimeReplayLocalAssets::create(resources,t::readiness_fixture::parse_server_info("maps/test.bsp"),root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
        REQUIRE(made.projection);assets=std::move(made.projection);REQUIRE(assets->reset_generation(initial(),world));
    }
};
void ready(g::ApprovedSoundAssets& loader) {
    for(const auto& r:hlclient::games::halflife::GaussPresentation::resources()) {
        auto a=loader.request_local(r);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{2};
        while(a.status==g::SoundAssetStatus::pending && std::chrono::steady_clock::now()<deadline) {std::this_thread::sleep_for(std::chrono::milliseconds{1});a=loader.request_local(r);}
        INFO(r.name());REQUIRE(a.status==g::SoundAssetStatus::ready);REQUIRE(a.asset);CHECK(std::ranges::any_of(a.asset->interleaved_samples,[](float f){return std::isfinite(f)&&f!=0;}));
    }
}
void audio(g::ApprovedSoundAssets& loader) {
    gf::Module m;hlclient::audio::OfflineOutput output;output.set_listener({{}, {0,-1,0},1,false});g::LocalAudio delivery(output);
    // A warmed nonblocking loader may still return pending while its worker
    // owns the cache lock. Force that first tick deterministically, then drive
    // the real application retry contract without resubmitting action/control.
    struct BusyAssets final:g::SoundAssets {
        g::ApprovedSoundAssets& backing;bool busy{true};
        explicit BusyAssets(g::ApprovedSoundAssets& source):backing(source){}
        g::SoundAssetResult request(std::uint16_t index) noexcept override {return backing.request(index);}
        g::SoundAssetResult request_local(const api::LocalSoundReference& r) noexcept override {
            return busy ? g::SoundAssetResult{} : backing.request_local(r);
        }
    } assets{loader};
    const auto deliver=[&](api::LocalAudioBatch batch,double at) {
        assets.busy=true;delivery.update(batch,&assets,at,true);
        CHECK(output.mixer.active()==0); // pending is not played
        assets.busy=false;batch.count=batch.control_count=batch.prepare_count=0;
        for(unsigned tick=1;tick<=125 && output.mixer.active()==0;++tick) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
            delivery.update(batch,&assets,at+tick*.001,true);
        }
    };
    m.command(1,.02,1U<<11);deliver(m.host.drain_audio(),.02);
    CHECK(delivery.statistics().resource_pending>0);
    INFO("pending="<<delivery.statistics().resource_pending<<" submitted="<<delivery.statistics().submitted
        <<" limits="<<delivery.statistics().limits<<" cancelled="<<delivery.statistics().cancelled
        <<" mixer_rejected="<<output.mixer.statistics().rejected);
    REQUIRE(output.mixer.active()==1);std::array<float,9600> pcm{};output.mixer.render(pcm);
    CHECK(std::ranges::any_of(pcm,[](float f){return std::isfinite(f)&&std::abs(f)>.0001F;}));
    m.command(2,.53,1U<<11);REQUIRE(m.command(3,.62,0));deliver(m.host.drain_audio(),.62);
    CHECK(output.mixer.statistics().stopped==1);CHECK(output.mixer.active()==1);CHECK(output.mixer.statistics().started==2);
    output.mixer.render(pcm);CHECK(std::ranges::all_of(pcm,[](float f){return std::isfinite(f);}));
    CHECK(std::ranges::any_of(pcm,[](float f){return std::abs(f)>.0001F;}));
}
void surface_glow_gpu(app::RuntimeReplayLocalAssets& assets,t::entity_opengl_fixture::HiddenContext& gl) {
    namespace renderer=hlclient::renderer;using V=hlclient::assets::AssetVector3;
    const auto wall=[](float x) {
        auto world=t::world_render_fixture::make_world();
        for(auto& v:world.vertices) {const auto p=v.position;v.position={x,32+p.x*6,32+p.y*6};v.normal={1,0,0};}
        world.bounds={{x,32,32},{x,128,128}};world.surfaces[0].bounds=world.bounds;
        auto built=hlclient::world_render::WorldRenderPackageBuilder{}.build(
            {std::move(world),t::world_render_fixture::make_texture_set(false)},t::world_render_fixture::make_lightmap_set({}));
        REQUIRE(built);return renderer::RenderStaticWorld{
            std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*built.package))};
    };
    renderer::RenderScene scene;scene.static_world=wall(90);
    gf::Module module;module.command(1,.02,1);gf::World hit;
    const auto effects=module.host.beam_effects(hit,.04,{});REQUIRE(effects.count>2);
    const auto glow=effects.sprites[2];REQUIRE(glow.surface_normal);CHECK_FALSE(glow.ribbon);
    const auto present=[&](std::span<const api::BeamSpriteFrame> frames,double at=.04) {
        scene.world_line_count=scene.world_flash_count=scene.world_effect_light_count=0;
        assets.present_entity_effects(scene,at,frames);
    };
    const auto draw=[&] {gl.renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);
        return gl.renderer().observe_framebuffer({320,240},scene.clear_color,true);};
    const auto pixel=[&](V point) {
        const auto matrix=renderer::camera_view_projection(scene.camera,{320,240});REQUIRE(matrix);
        const auto& m=matrix.matrix->values;
        const auto w=m[3]*point.x+m[7]*point.y+m[11]*point.z+m[15];REQUIRE(w>0);
        const auto x=static_cast<int>((((m[0]*point.x+m[4]*point.y+m[8]*point.z+m[12])/w)*.5F+.5F)*320);
        const auto y=static_cast<int>((((m[1]*point.x+m[5]*point.y+m[9]*point.z+m[13])/w)*.5F+.5F)*240);
        REQUIRE(x>1);REQUIRE(x<318);REQUIRE(y>1);REQUIRE(y<238);return std::size_t(y*320+x)*4;
    };
    for(const V eye:std::array<V,2>{{{130,80,80},{130,55,98}}}) {
        CAPTURE(eye.x,eye.y,eye.z);scene.camera={eye,{90,80,80},{0,0,1},1.04719755F,.1F,8192};
        present({});const auto baseline=draw();present(std::span{&glow,1});const auto fixed=draw();
        REQUIRE(scene.follow_trail_entities);const auto& instance=scene.follow_trail_entities->frame->sprite_instances()[0];
        CHECK(instance.orientation==hlclient::assets::SpriteOrientation::oriented);
        CHECK(instance.transform.origin.x==Catch::Approx(90.125F));
        // Both sides of the contact must brighten. The old view-parallel plane
        // at the exact hit lost a half under oblique depth testing.
        for(const float side:{-2.0F,2.0F}) {
            const auto p=pixel({90.125F,80+side,80});
            CHECK(int(fixed.rgba8[p])+int(fixed.rgba8[p+1])>int(baseline.rgba8[p])+int(baseline.rgba8[p+1])+4);
        }
        // These points are inside the sprite rectangle, but in its actual
        // black-neutral corners: no opaque quad or background tint is allowed.
        for(const float y:{-5.7F,5.7F})for(const float z:{-5.7F,5.7F}) {
            const auto p=pixel({90.125F,80+y,80+z});
            for(unsigned c=0;c<3;++c)CHECK(fixed.rgba8[p+c]==baseline.rgba8[p+c]);
        }
        present({},7);CHECK(draw().rgba8==baseline.rgba8);
    }
    scene.camera={{130,55,98},{90,80,80},{0,0,1},1.04719755F,.1F,8192};
    scene.static_world=wall(110);present({});const auto occluded_baseline=draw();
    present(std::span{&glow,1});CHECK(draw().rgba8==occluded_baseline.rgba8);
}
void gpu(app::RuntimeReplayLocalAssets& assets) {
    auto gl=t::entity_opengl_fixture::try_context(320,240);if(!gl || !t::entity_opengl_fixture::capable_context())SKIP("OpenGL 3.3 unavailable");gl->initialize_renderer();
    gf::Module module;auto meta=assets.presentation_model(1,73);REQUIRE(meta);CHECK(hlclient::games::halflife::gauss_sequences(*meta));module.host.bind_model(meta);module.host.observe(module.s,0);
    module.command(1,.02,1);const auto shot=module.host.sample(.04);REQUIRE(shot.visual);
    hlclient::renderer::RenderScene scene;const auto pose=module.host.viewmodel(module.s,meta,.04,shot.visual);REQUIRE(pose.status==api::ViewmodelStatus::ready);
    scene.first_person_entities=assets.materialize_viewmodel(pose);REQUIRE(scene.first_person_entities);REQUIRE(assets.viewmodel_attachment_zero());
    const auto draw=[&]{gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);return gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);};
    const auto first=draw();REQUIRE(first.non_clear_pixel_count>0);scene.camera.position={100,200,300};scene.camera.target={0,0,0};CHECK(std::ranges::equal(first.rgba8,draw().rgba8));
    scene.first_person_entities.reset();scene.camera={{100,60,95},{90,80,80},{0,0,1},1.04719755F,.1F,8192};const auto baseline=draw();
    gf::World wall;const auto beams=module.host.beam_effects(wall,.04,hlclient::assets::AssetVector3{100,80,80});REQUIRE(beams.count>1);
    const auto present=[&](double at,std::span<const api::BeamSpriteFrame> frames) {
        scene.world_line_count=scene.world_flash_count=scene.world_effect_light_count=0;
        assets.present_entity_effects(scene,at,frames);
    };
    present(.04,{beams.sprites.data(),beams.count});REQUIRE(scene.follow_trail_entities);REQUIRE(scene.world_line_count==1);
    const auto visible=draw();CHECK_FALSE(std::ranges::equal(baseline.rgba8,visible.rgba8));
    // Isolate the volume and glow: either alone must change pixels, otherwise
    // a visible endpoint could hide a broken/missing production beam pass.
    present(.04,{beams.sprites.data(),1});REQUIRE(scene.world_line_count==1);CHECK(scene.world_lines[0].geometry==api::WorldLineGeometry::solid_3d);
    CHECK(scene.world_lines[0].width_units==Catch::Approx(1.75));
    CHECK(scene.world_lines[0].emission_gain==3);const auto primary=draw();CHECK_FALSE(std::ranges::equal(baseline.rgba8,primary.rgba8));
    present(.04,{beams.sprites.data()+1,1});CHECK_FALSE(std::ranges::equal(baseline.rgba8,draw().rgba8));
    gf::Module charged;charged.host.bind_model(meta);charged.host.observe(charged.s,0);
    charged.command(1,.02,1U<<11);charged.command(2,.53,1U<<11);REQUIRE(charged.command(3,1.62,0));
    gf::World thin;thin.penetration=true;const auto secondary=charged.host.beam_effects(thin,1.62,hlclient::assets::AssetVector3{100,80,80});REQUIRE(secondary.penetrations==1);
    present(1.62,{secondary.sprites.data(),1});REQUIRE(scene.world_line_count==1);CHECK(scene.world_lines[0].width_units==Catch::Approx(3.5));const auto wide=draw();CHECK_FALSE(std::ranges::equal(baseline.rgba8,wide.rgba8));CHECK_FALSE(std::ranges::equal(primary.rgba8,wide.rgba8));
    auto cleanup=module.host.beam_effects(wall,7,{});present(7,{cleanup.sprites.data(),cleanup.count});CHECK_FALSE(scene.follow_trail_entities);
    CHECK(std::ranges::equal(baseline.rgba8,draw().rgba8));surface_glow_gpu(assets,*gl);gl->release_renderer();
}
}
TEST_CASE("E21 owned approved resources normal module persistent spin and isolated discharge produce PCM", "[e21][audio][composition][no-stock]") {
    gauss_assets::Owned owned;gf::Module m;m.host.bind_model(owned.assets->presentation_model(1,73));m.host.observe(m.s,0);
    hlclient::goldsrc::ApprovedSoundAssets loader(owned.assets->sound_resources());gauss_assets::ready(loader);
    hlclient::audio::OfflineOutput output;output.set_listener({{}, {0,-1,0},1,false});hlclient::goldsrc::LocalAudio audio(output);
    const auto deliver=[&](gf::api::LocalAudioBatch batch,double at,std::uint64_t starts) {
        audio.update(batch,&loader,at,true);
        // Even warmed resources can report pending while the worker owns its
        // cache lock. Continue the existing sink, never resubmit an action.
        batch.count=batch.control_count=batch.prepare_count=0;
        for(unsigned tick=1;tick<=125 && output.mixer.statistics().started<starts;++tick) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
            audio.update(batch,&loader,at+tick*.0001,true);
        }
    };
    m.command(1,.02,1U<<11);deliver(m.host.drain_audio(),.02,1);REQUIRE(output.mixer.active()==1);CHECK(output.mixer.statistics().started==1);
    std::array<float,512> pcm{};output.mixer.render(pcm);CHECK(std::ranges::any_of(pcm,[](float f){return f!=0;}));
    for(unsigned i=2;i<=80;++i) {m.command(i,i*.02,1U<<11);audio.update(m.host.drain_audio(),&loader,i*.02,true);}
    CHECK(output.mixer.statistics().started==1);CHECK(output.mixer.active()==1);CHECK(output.mixer.statistics().updated>1);
    REQUIRE(m.command(81,1.62,0));deliver(m.host.drain_audio(),1.62,2);CHECK(output.mixer.statistics().stopped==1);CHECK(output.mixer.statistics().started==2);
    output.mixer.render(pcm);CHECK(std::ranges::any_of(pcm,[](float f){return f!=0;}));CHECK(std::ranges::all_of(pcm,[](float f){return std::isfinite(f);}));
    gf::World world;CHECK(m.host.beam_effects(world,1.62,{}).shots==1);audio.update(m.host.drain_audio(),&loader,1.62,true);CHECK(output.mixer.statistics().started==2);
}
TEST_CASE("E21 volume materialization is bounded immutable and independent of sprite readiness", "[e21][composition][visual-profile][no-stock]") {
    gauss_assets::Owned owned;gf::Module module;module.command(1,.02,1);gf::World miss;miss.miss=true;
    const auto effects=module.host.beam_effects(miss,.04,{});REQUIRE(effects.count==1);
    auto beam=effects.sprites[0];beam.virtual_sprite.fill('x');
    hlclient::renderer::RenderScene scene;
    const auto present=[&](double at,std::span<const gf::api::BeamSpriteFrame> frames) {
        scene.world_line_count=scene.world_flash_count=scene.world_effect_light_count=0;
        owned.assets->present_entity_effects(scene,at,frames);
    };
    present(.04,std::span{&beam,1});REQUIRE(scene.world_line_count==1);
    const auto first=scene.world_lines[0];
    CHECK(std::hypot(first.end.x-first.start.x,first.end.y-first.start.y,first.end.z-first.start.z)==Catch::Approx(144));
    CHECK(scene.world_flash_count==1);CHECK(scene.world_effect_light_count==1);CHECK_FALSE(scene.follow_trail_entities);
    present(.045,std::span{&beam,1});REQUIRE(scene.world_line_count==1);const auto moving=scene.world_lines[0];
    CHECK(std::hypot(moving.start.x-first.start.x,moving.start.y-first.start.y,moving.start.z-first.start.z)==Catch::Approx(60));
    present(.045,std::span{&beam,1});REQUIRE(scene.world_line_count==1);CHECK(scene.world_lines[0].start.x==moving.start.x);
    CHECK(miss.calls==1); // Materialization never retraces or repeats authority.
    present(.125,std::span{&beam,1});REQUIRE(scene.world_line_count==1);
    CHECK(scene.world_lines[0].color[3]==Catch::Approx(.30)); // Still visible past the previous .12 causal deadline.
    present(beam.expires-.001,std::span{&beam,1});REQUIRE(scene.world_line_count==1);
    CHECK(scene.world_lines[0].color[3]>0);CHECK(scene.world_lines[0].color[3]<.01F);CHECK(miss.calls==1);
    auto invalid=beam;invalid.emission_gain=5;present(.04,std::span{&invalid,1});CHECK(scene.world_line_count==0);
    invalid=beam;invalid.width=6.01F;present(.04,std::span{&invalid,1});CHECK(scene.world_line_count==0);
    auto maximum=beam;maximum.width=6;present(.04,std::span{&maximum,1});REQUIRE(scene.world_line_count==1);CHECK(scene.world_lines[0].width_units==6);
    invalid=beam;invalid.motion_starts=.05;present(.04,std::span{&invalid,1});CHECK(scene.world_line_count==0);
    invalid=beam;invalid.surface_normal=hlclient::assets::AssetVector3{1,0,0};present(.04,std::span{&invalid,1});CHECK(scene.world_line_count==0);
    invalid=beam;invalid.action.generation=2;present(.04,std::span{&invalid,1});CHECK(scene.world_line_count==0);
    present(beam.expires,std::span{&beam,1});CHECK(scene.world_line_count==0);
    std::array<gf::api::BeamSpriteFrame,128> many;many.fill(beam);present(.04,many);
    CHECK(scene.world_line_count==scene.world_lines.size());CHECK(owned.assets->follow_resource_statistics().unsupported>=64);
    gf::Module close;close.command(1,.02,1);gf::World hit;const auto near_effects=close.host.beam_effects(hit,.04,{});
    REQUIRE(near_effects.count>1);present(.04,std::span{near_effects.sprites.data(),1});REQUIRE(scene.world_line_count==1);
    CHECK(scene.world_lines[0].end.x==Catch::Approx(90)); // First materialization survives causal-clock delay.
    CHECK(scene.world_flash_count==0);CHECK(scene.world_effect_light_count==1);
    present(.045,std::span{near_effects.sprites.data(),1});CHECK(scene.world_line_count==0);
}
TEST_CASE("E21 owned approved models and smoke hotglow use normal production GPU passes", "[e21][opengl][composition][no-stock]") {
    gauss_assets::Owned owned;gauss_assets::gpu(*owned.assets);
}
TEST_CASE("E21 normal static BSP query drives first segment and reuses clipped decal owner without material audio", "[e21][composition][bsp][no-stock]") {
    gauss_assets::Owned owned;gf::Module module;auto command=gf::f::command();command.shot_context->eye={32,32,16};command.shot_context->direction={0,0,-1};
    const auto probe=owned.assets->trace({32,32,16},{32,32,-8192});
    CAPTURE(probe.valid,probe.start_solid,probe.all_solid,probe.unsupported,probe.hit,probe.point.z);
    REQUIRE(probe.valid);REQUIRE_FALSE(probe.start_solid);REQUIRE_FALSE(probe.all_solid);REQUIRE_FALSE(probe.unsupported);
    (void)module.host.command_movement_effect(command);const auto out=module.host.beam_effects(*owned.assets,.02,hlclient::assets::AssetVector3{32,32,14});
    REQUIRE(out.count>=1);CHECK(out.sprites[0].end.z==Catch::Approx(0).margin(.05));REQUIRE(out.impact_count==1);
    hlclient::app::WorldImpactPresentation decals;const auto hit=decals.submit(out.impacts[0],owned.assets->collision_world_package(),owned.assets->surface_scene()->world_package().get(),{},true,.02);
    CHECK(hit.status==hlclient::app::WorldImpactStatus::hit);CHECK(decals.active()==1);
    const auto audio=module.host.drain_audio();REQUIRE(audio.count==1);CHECK(audio.cues[0].reference.name()=="weapons/gauss2.wav");
}
TEST_CASE("E21 owned pGauss merges server pose and starts beam at evaluated socket then retires on death", "[e21][remote-effects][composition][opengl][no-stock]") {
    gauss_assets::Owned owned;gf::Module module;auto s=gauss_assets::initial();s.server_time_seconds=1;
    s.publication_revision=10;s.entity_metadata.source->record_identity=s.entity_metadata.source->record_ordinal=10;
    gf::c::RuntimePacketEntityObservation e;e.entity_number=2;e.player_movement_schema=e.ordinary_visual_schema=true;
    e.model_index=7;e.weapon_model_index=71;e.origin={0,0,16};e.angles={0,90,0};e.sequence=1;e.frame=0;e.body=e.skin=e.gait_sequence=0;
    e.animation_time_seconds=1;e.frame_rate=1;e.controllers.fill(127);e.blending.fill(127);s.packet_entities={e};s.canonical_state_hash=gf::c::runtime_observation_canonical_hash(s);
    REQUIRE(owned.assets->reset_generation(s,owned.world));REQUIRE(owned.assets->present_entities(module.host,10,1,owned.world,{320,240}));
    REQUIRE(owned.assets->present_entities(module.host,10.21,1,owned.world,{320,240}));const auto frame=owned.world.entity_frame();REQUIRE(frame);REQUIRE(frame->studio_instances().size()==2);
    const auto poses=owned.assets->remote_weapon_poses();REQUIRE(poses.size()==1);CHECK(poses[0].semantic_key==9);CHECK(poses[0].source==gf::api::RemoteSocketSource::validated_pmodel_socket);
    const auto& parent=frame->studio_poses()[frame->studio_instances()[0].pose_index].bone_matrices;
    const auto& child=frame->studio_poses()[frame->studio_instances()[1].pose_index].bone_matrices;
    REQUIRE(parent.size()==12);REQUIRE(child.size()==12);for(std::size_t i=0;i<11;++i)CHECK(parent[i]==child[i]);
    auto event=gf::event(2,2);event.received_at_seconds=10.21;event.weapon_model_index=71;module.host.committed_scripted_event(event);
    gf::World wall;const auto beam=module.host.beam_effects(wall,10.21,{});REQUIRE(beam.count>0);CHECK(beam.sprites[0].start.x==poses[0].muzzle.origin.x);CHECK(beam.sprites[0].start.y==poses[0].muzzle.origin.y);
    CHECK(module.host.sample(10.21).local_punch_pitch_degrees==0);
    auto gl=hlclient::tests::entity_opengl_fixture::try_context(320,240);if(!gl || !hlclient::tests::entity_opengl_fixture::capable_context())SKIP("OpenGL 3.3 unavailable");gl->initialize_renderer();
    auto scene=gf::c::build_render_scene(owned.world);scene.static_world.reset();scene.camera={{96,-64,48},{0,0,16},{0,0,1},1.04719755F,.1F,8192};
    gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);CHECK(gl->renderer().observe_framebuffer({320,240},scene.clear_color,true).non_clear_pixel_count>0);
    s.publication_revision=11;s.server_time_seconds=1.1;s.entity_metadata.source->record_identity=s.entity_metadata.source->record_ordinal=11;s.packet_entities[0].weapon_model_index=0;s.canonical_state_hash=gf::c::runtime_observation_canonical_hash(s);
    REQUIRE(owned.assets->project_entities(s,owned.world));REQUIRE(owned.assets->present_entities(module.host,10.4,1,owned.world,{320,240}));CHECK(owned.world.entity_frame()->studio_instances().size()==1);gl->release_renderer();
}
TEST_CASE("E21 observer remote Gauss downward aim reaches owned BSP floor and normal render lines", "[e21][gauss-remote-aim][remote-effects][composition][bsp][no-stock]") {
    gauss_assets::Owned owned;
    for(const bool primary:{true,false})for(const bool ducking:{false,true})for(const bool inherited:{true,false}) {
        CAPTURE(primary,ducking,inherited);gf::Module module;auto event=gf::event();
        event.origin={32,32,16};event.angles={330,0,0};event.angles_from_entity=inherited;event.ducking=ducking;
        event.boolean_parameter1=primary;event.float_parameter1=primary ? 20.F : 200.F;
        module.host.committed_scripted_event(event);
        const auto out=module.host.beam_effects(*owned.assets,.03,hlclient::assets::AssetVector3{999,999,999});
        REQUIRE(out.count>=1);REQUIRE(out.impact_count==1);CHECK(out.sprites[0].start.z==(ducking ? 28.F : 44.F));
        CHECK(out.sprites[0].end.z==Catch::Approx(0).margin(.05));CHECK(out.sprites[0].end.z<out.sprites[0].start.z);
        hlclient::renderer::RenderScene scene;
        owned.assets->present_entity_effects(scene,.03,{out.sprites.data(),out.count});
        REQUIRE(scene.world_line_count==1);CHECK(scene.world_lines[0].end.z<scene.world_lines[0].start.z);
        CHECK(scene.world_lines[0].end.z==Catch::Approx(0).margin(.05));
        CHECK(scene.world_lines[0].width_units==Catch::Approx(primary ? 2.975 : 5.95));
        CHECK(module.host.sample(.03).local_punch_pitch_degrees==0);
        module.host.committed_scripted_event(event);const auto repeat=module.host.beam_effects(*owned.assets,.031,{});
        CHECK(repeat.shots==1);CHECK(repeat.impact_count==0);
    }
}
