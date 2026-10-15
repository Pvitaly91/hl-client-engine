#include "python_357_test_fixture.hpp"
#include "crossbow_test_fixture.hpp"
#include <hlclient/app/live_visual_control.hpp>
#include <hlclient/goldsrc/usercmd_input_adapter.hpp>
#include <hlclient/goldsrc/live_runtime_stage.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <limits>
namespace f=hlclient::tests::python_fixture;
namespace api=hlclient::game_api;
namespace c=hlclient::client;
namespace g=hlclient::goldsrc;
namespace input=hlclient::input;
namespace gameplay=hlclient::gameplay_input;
using Catch::Approx;
namespace {
gameplay::GameplayInputIntent neutral_intent(input::InputStateTracker& tracker) {
    tracker.begin_frame();const auto snapshot=tracker.publish_snapshot();tracker.end_frame();
    const auto bindings=gameplay::GameplayInputBindings::project_default_v1();REQUIRE(bindings);
    auto result=gameplay::GameplayInputIntentBuilder{}.build(snapshot,*bindings.bindings,{},.02);
    REQUIRE(result);return std::move(*result.intent);
}
template<class Module>
void fov(Module& m,double value,std::size_t record,bool message) {
    m.observed.publication_revision=record;
    m.observed.client_metadata.source=c::RuntimeObservationSource{record,record,0,0,8};
    if(message) {
        m.observed.weapon_hud.field_of_view=static_cast<std::uint8_t>(value);
        m.observed.weapon_hud.field_of_view_source=c::RuntimeObservationSource{record,record,0,16,24};
    } else m.observed.receiving_client->field_of_view=value;
    m.host.observe(m.observed,static_cast<double>(record)*.02);
}
TEST_CASE("E18 E19 confirmed zoom scales physical mouse look from the same resolved FOV",
          "[e18][e19][camera][sensitivity][gameplay-input]") {
    const auto exercise=[](auto& m,double zoom) {
        input::InputStateTracker tracker;tracker.begin_frame();
        tracker.apply_event(input::InputEvent::focus_gained());
        tracker.apply_event(input::InputEvent::capture_acquired());
        tracker.apply_event(input::InputEvent::mouse_motion(90,-45));
        const auto snapshot=tracker.publish_snapshot();tracker.end_frame();
        const auto bindings=gameplay::GameplayInputBindings::project_default_v1();REQUIRE(bindings);
        auto base=m.host.movement_policy().mouse_look;
        base.degrees_per_pixel_x=.2;base.degrees_per_pixel_y=.3;
        const auto check=[&](double expected_fov,double expected_scale) {
            CAPTURE(zoom,expected_fov,expected_scale);
            const auto policy=m.host.camera(m.observed,0);
            CHECK(policy.horizontal_field_of_view_degrees==expected_fov);
            CHECK(policy.mouse_sensitivity_scale==Approx(expected_scale));
            auto look=base;look.sensitivity_scale*=policy.mouse_sensitivity_scale;
            for(const double seconds:{.005,.02,.1}) {
                auto built=gameplay::GameplayInputIntentBuilder{}.build(snapshot,*bindings.bindings,look,seconds);
                REQUIRE(built);
                CHECK(built.intent->look_delta_yaw_degrees()==Approx(-18*expected_scale));
                CHECK(built.intent->look_delta_pitch_degrees()==Approx(13.5*expected_scale));
                for(const double aspect:{4./3.,16./9.,16./10.}) {
                    CAPTURE(aspect,seconds);
                    // Each aspect starts from the same physical sample in its
                    // own camera/world; a changed publication must never reuse
                    // an already committed input revision in the same world.
                    hlclient::app::LiveVisualCameraController camera;
                    REQUIRE(camera.apply_local_look(*built.intent));
                    CHECK(camera.yaw_degrees()==Approx(-18*expected_scale));
                    CHECK(camera.pitch_degrees()==Approx(13.5*expected_scale));
                    c::ClientWorldState world;
                    REQUIRE(camera.update(&m.observed,*built.intent,world,policy,false,{},aspect));
                    CHECK(camera.yaw_degrees()==Approx(-18*expected_scale)); // no second input application
                    CHECK(camera.pitch_degrees()==Approx(13.5*expected_scale));
                }
            }
            CHECK(base.sensitivity_scale==1.0);
        };
        check(90,1);
        m.host.submit(f::command(1,.02,g::kReferenceGoldSrcButtonAttack2),.02);
        check(90,1); // An RMB command alone is not FOV authority.
        fov(m,zoom,2,true);check(zoom,zoom/90*1.2);
        fov(m,0,1,false);check(zoom,zoom/90*1.2); // An older publication cannot change either output.
        fov(m,75,3,false);check(75,1); // No weapon-ID constant or projection interpolation.
        fov(m,zoom,4,false);check(zoom,zoom/90*1.2);
        m.observed.client_metadata.freshness=c::RuntimeObservationFreshness::retained;
        m.observed.receiving_client->field_of_view=0;m.host.observe(m.observed,.09);
        check(zoom,zoom/90*1.2);
        m.observed.client_metadata.freshness=c::RuntimeObservationFreshness::observed_in_record;
        fov(m,0,5,true);check(90,1);
        fov(m,zoom,6,false);check(zoom,zoom/90*1.2);
        fov(m,90,7,true);check(90,1); // Explicit default must not multiply by 1.2.
        fov(m,255,8,true);check(90,1);
        fov(m,zoom,9,true);check(zoom,zoom/90*1.2);
        m.observed.weapon_hud.active_weapon_id=2;m.host.observe(m.observed,.2);check(90,1);
        m.host.teardown();CHECK(m.host.camera(m.observed,0).mouse_sensitivity_scale==1);
    };
    f::Module python;exercise(python,40);
    hlclient::tests::crossbow_fixture::Module crossbow;exercise(crossbow,20);
}

TEST_CASE("Half-Life unzoomed mouse sensitivity is independent of selected weapon",
          "[sensitivity][weapon-switch][gameplay-input]") {
    f::Module m;
    input::InputStateTracker tracker;tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    tracker.apply_event(input::InputEvent::mouse_motion(50,-25));
    const auto snapshot=tracker.publish_snapshot();tracker.end_frame();
    const auto bindings=gameplay::GameplayInputBindings::project_default_v1();REQUIRE(bindings);
    const auto base=m.host.movement_policy().mouse_look;
    const auto original=gameplay::GameplayInputIntentBuilder{}.build(snapshot,*bindings.bindings,base,.01);
    REQUIRE(original);
    for(std::uint8_t id=1;id<=15;++id) {
        CAPTURE(id);
        m.observed.weapon_hud.active_weapon_id=id;
        fov(m,0,std::size_t(id)+2,false);
        auto look=m.host.movement_policy().mouse_look;
        const auto policy=m.host.camera(m.observed,0);
        CHECK(policy.horizontal_field_of_view_degrees==90);
        CHECK(policy.mouse_sensitivity_scale==1);
        look.sensitivity_scale*=policy.mouse_sensitivity_scale;
        const auto current=gameplay::GameplayInputIntentBuilder{}.build(snapshot,*bindings.bindings,look,.01);
        REQUIRE(current);
        CHECK(current.intent->look_delta_yaw_degrees()==original.intent->look_delta_yaw_degrees());
        CHECK(current.intent->look_delta_pitch_degrees()==original.intent->look_delta_pitch_degrees());
    }
}

TEST_CASE("E18 E19 zoom sensitivity clears with projection reload death respawn HUD and session floors",
          "[e18][e19][camera][sensitivity][lifecycle]") {
    const auto exercise=[](auto& m,double zoom,unsigned reason) {
        CAPTURE(zoom,reason);
        fov(m,zoom,2,true);
        REQUIRE(m.host.camera(m.observed,0).mouse_sensitivity_scale==Approx(zoom/90*1.2));
        if(reason==0) m.observed.weapon_slots[0].in_reload=true;
        if(reason==1) m.observed.receiving_client->health=0;
        if(reason==2) ++m.observed.lifecycle.life_epoch;
        if(reason==3) {
            const std::array body{std::byte{0}};
            const std::array message{api::GameMessageView{api::GameMessageKind::user_message,"ResetHUD",body,{3,3,3,0,8}}};
            auto staged=m.host.stage_record({&m.observed,m.observed,message,1,8});REQUIRE(staged);
            m.observed.weapon_hud=staged.state->weapon_hud;m.observed.lifecycle=staged.state->lifecycle;
            m.host.commit_record(std::move(*staged.state));
        }
        if(reason==4) {
            m.host.reset({2,2,1});
            m.observed.generation=2;m.observed.client_metadata.generation=2;
            m.observed.receiving_client->field_of_view.reset();
            m.observed.weapon_hud.field_of_view.reset();
            m.observed.weapon_hud.field_of_view_source.reset();
        }
        if(reason==5) {
            m.observed.weapon_slots[0].clip=2;
            m.observed.weapon_hud.clips[*m.observed.weapon_hud.active_weapon_id]=2;
            // A clip correction needs a fresh authoritative clientdata source,
            // not just a render/publication revision with retained old data.
            fov(m,zoom,3,false);
            m.host.submit(f::command(1,.08,g::kReferenceGoldSrcButtonReload),.08);
            REQUIRE(m.host.sample(.08).action==api::LocalWeaponAction::reload);
        }
        ++m.observed.publication_revision;m.host.observe(m.observed,.1);
        const auto reset=m.host.camera(m.observed,0);
        CHECK(reset.horizontal_field_of_view_degrees==90);
        CHECK(reset.mouse_sensitivity_scale==1);
        m.observed.weapon_slots[0].in_reload=false;m.observed.receiving_client->health=100;
        m.host.observe(m.observed,.12);
        CHECK(m.host.camera(m.observed,0).mouse_sensitivity_scale==1);
    };
    for(unsigned reason=0;reason<6;++reason) {
        f::Module python;exercise(python,40,reason);
        hlclient::tests::crossbow_fixture::Module crossbow;exercise(crossbow,20,reason);
    }
}
}
TEST_CASE("E18 registered unsigned SetFOV uses atomic owning publication and exact body bounds", "[e18][game-message][transaction][camera]") {
    f::Module m;std::array body{std::byte{40}};
    const std::array message{api::GameMessageView{api::GameMessageKind::user_message,"SetFOV",body,{2,2,2,0,8},std::uint8_t{201}}};
    auto staged=m.host.stage_record({&m.observed,m.observed,message,1,8});REQUIRE(staged);
    CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==90);
    REQUIRE(staged.state->weapon_hud.field_of_view_source);
    CHECK(staged.state->weapon_hud.field_of_view==40);
    body[0]=std::byte{0};CHECK(staged.state->weapon_hud.field_of_view==40); // no RX pointer
    m.observed.weapon_hud=staged.state->weapon_hud;m.host.commit_record(std::move(*staged.state));
    m.host.observe(m.observed,.04);CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==40);
    const std::array malformed{std::byte{0},std::byte{40}};
    const std::array suffix{
        api::GameMessageView{api::GameMessageKind::user_message,"SetFOV",body,{3,3,3,0,8},std::uint8_t{88}},
        api::GameMessageView{api::GameMessageKind::user_message,"SetFOV",malformed,{3,3,3,8,24},std::uint8_t{88}}};
    const auto rejected=m.host.stage_record({&m.observed,m.observed,suffix,1,8});CHECK_FALSE(rejected);
    REQUIRE(rejected.message_failure);CHECK(rejected.message_failure->message_name=="SetFOV");
    CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==40);
    auto zero=m.host.stage_record({&m.observed,m.observed,std::span{suffix}.first(1),1,8});REQUIRE(zero);
    m.observed.weapon_hud=zero.state->weapon_hud;m.host.commit_record(std::move(*zero.state));
    m.host.observe(m.observed,.06);CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==90);
}
TEST_CASE("E18 FOV source order fresh retained unavailable reset invalid and new life cannot churn", "[e18][camera][lifecycle]") {
    f::Module m;fov(m,40,2,true);CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==40);
    fov(m,0,1,false);CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==40);
    fov(m,0,3,false);CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==90);
    fov(m,40,4,true);CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==40);
    auto stale=f::state(6,2);stale.lifecycle.life_epoch=99;stale.weapon_hud.active_weapon_id=2;
    m.host.observe(stale,.081);CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==40);
    m.observed.client_metadata.freshness=c::RuntimeObservationFreshness::retained;
    m.observed.client_metadata.source=c::RuntimeObservationSource{5,5,0,0,8};
    m.host.observe(m.observed,.1);CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==40);
    m.observed.weapon_hud.active_weapon_id=2;m.host.observe(m.observed,.12);
    CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==90);
    m.observed.weapon_hud.active_weapon_id=3;m.host.observe(m.observed,.14);
    CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==90);
    m.observed.client_metadata.freshness=c::RuntimeObservationFreshness::observed_in_record;
    fov(m,255,6,true);CHECK(m.host.camera(m.observed,0).projection_status==api::CameraIntent::ProjectionStatus::invalid);
    CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==90);
    fov(m,40,7,false);CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==40);
    m.observed.receiving_client->health=0;m.host.observe(m.observed,.16);
    CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==90);
    m.observed.receiving_client->health=100;++m.observed.lifecycle.life_epoch;m.host.observe(m.observed,.18);
    CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==90);
    m.host.reset({2,2,1});CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==90);
}
TEST_CASE("E18 reload clears zoom before conventional reload presentation and retained FOV cannot resurrect", "[e18][camera][reload]") {
    f::Module m;m.observed=f::state(2);fov(m,40,2,true);
    REQUIRE(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==40);
    m.host.submit(f::command(1,.06,g::kReferenceGoldSrcButtonReload),.06);
    REQUIRE(m.host.sample(.06).action==api::LocalWeaponAction::reload);
    CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==90);
    m.host.observe(m.observed,.08);CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==90);
}
TEST_CASE("E18 zoomed fire retains FOV and server Python death notice score remain authoritative", "[e18][e13][e14][camera][lifecycle]") {
    f::Module m;fov(m,40,2,true);m.host.submit(f::command(1,.06),.06);
    REQUIRE(m.host.sample(.06).primary_fire_starts==1);
    CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==40);
    m.observed.weapon_slots[0].clip=5;m.observed.weapon_hud.clips[3]=5;
    fov(m,40,3,false);CHECK(m.host.sample(.07).primary_fire_confirmed==1);
    CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==40);
    const std::array death{std::byte{2},std::byte{1},std::byte{'3'},std::byte{'5'},std::byte{'7'},std::byte{0}};
    const std::array messages{api::GameMessageView{api::GameMessageKind::user_message,"DeathMsg",death,{4,4,4,0,48}}};
    auto killed=m.host.stage_record({&m.observed,m.observed,messages,1,8});REQUIRE(killed);
    REQUIRE(killed.state->multiplayer.notices[0]);CHECK(killed.state->multiplayer.notices[0]->weapon=="357");
    CHECK(killed.state->multiplayer.players[1].frags==0);
    m.observed.lifecycle=killed.state->lifecycle;m.observed.publication_revision=4;
    m.host.commit_record(std::move(*killed.state));m.host.observe(m.observed,.08);
    CHECK(m.host.camera(m.observed,0).horizontal_field_of_view_degrees==90);
    const std::array score{std::byte{2},std::byte{1},std::byte{0},std::byte{0},std::byte{0},
        std::byte{0},std::byte{0},std::byte{0},std::byte{0}};
    const std::array scores{api::GameMessageView{api::GameMessageKind::user_message,"ScoreInfo",score,{5,5,5,0,72}}};
    auto scored=m.host.stage_record({&m.observed,m.observed,scores,1,8});REQUIRE(scored);
    CHECK(scored.state->multiplayer.players[1].frags==1);
}
TEST_CASE("E18 horizontal game FOV converts at three actual aspects without input pose or physics change", "[e18][game-api][camera]") {
    for(const double aspect:{4.0/3.0,16.0/9.0,16.0/10.0}) {
        f::Module m;input::InputStateTracker tracker;hlclient::app::LiveVisualCameraController controller;
        c::ClientWorldState world;const auto normal=m.host.camera(m.observed,0);
        REQUIRE(controller.update(&m.observed,neutral_intent(tracker),world,normal,false,{},aspect));
        const auto baseline=world.camera();fov(m,40,2,true);
        REQUIRE(controller.update(&m.observed,neutral_intent(tracker),world,m.host.camera(m.observed,0),false,{},aspect));
        constexpr double rad=.01745329251994329577;
        CHECK(world.camera().vertical_field_of_view_radians==Approx(2*std::atan(std::tan(20*rad)/aspect)));
        CHECK(f::same(world.camera().position,baseline.position));CHECK(f::same(world.camera().target,baseline.target));
        CHECK(controller.yaw_degrees()==0);CHECK(controller.pitch_degrees()==0);
        fov(m,0,3,true);
        REQUIRE(controller.update(&m.observed,neutral_intent(tracker),world,m.host.camera(m.observed,0),false,{},aspect));
        CHECK(world.camera()==baseline);
    }
}
TEST_CASE("E18 Python RMB edge mask changes only prehistory secondary while other weapons retain hold", "[e18][game-api][input-adapter][reference-actions]") {
    f::Module m;const auto mask=m.host.edge_triggered_buttons(m.observed);REQUIRE(mask!=0);
    input::InputStateTracker tracker;tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());tracker.apply_event(input::InputEvent::capture_acquired());
    tracker.apply_event(input::InputEvent::mouse_button_pressed(input::PhysicalMouseButton::right));
    const auto snapshot=tracker.publish_snapshot();tracker.end_frame();
    auto bindings=gameplay::GameplayInputBindings::project_default_v1();REQUIRE(bindings);
    auto intent=gameplay::GameplayInputIntentBuilder{}.build(snapshot,*bindings.bindings,{},.02);REQUIRE(intent);
    auto camera=hlclient::gameplay_camera::GameplayCameraState::create({});REQUIRE(camera);
    g::GoldSrcUserCmdBuildContext context;context.command_sequence=*g::GoldSrcUserCmdSequence::create(1);
    context.command_msec=20;context.command_sample_duration_nanoseconds=20'000'000;
    context.reference_button_policy=m.host.movement_policy().button_policy;
    context.edge_triggered_buttons=mask;context.one_shot_buttons=mask;
    g::LiveVisualButtonLatch latch;latch.observe(mask,true,mask);
    auto first=g::GoldSrcUserCmdInputAdapter{}.build_reference_wire(*intent.intent,*camera.state,context);REQUIRE(first);
    CHECK(first.command->buttons==g::kReferenceGoldSrcButtonAttack2);REQUIRE(first.one_shot_plan);
    CHECK(latch.pending()==mask);REQUIRE(first.one_shot_plan->commit_after_history_insert(context.command_sequence));
    latch.consume_after_history_insert(first.one_shot_plan->consumes_buttons());context.one_shot_buttons=latch.pending();
    auto held=g::GoldSrcUserCmdInputAdapter{}.build_reference_wire(*intent.intent,*camera.state,context);REQUIRE(held);
    CHECK(held.command->buttons==0);CHECK(first.command->buttons==2048); // immutable history value
    m.observed.weapon_hud.active_weapon_id=4;context.edge_triggered_buttons=m.host.edge_triggered_buttons(m.observed);
    auto mp5=g::GoldSrcUserCmdInputAdapter{}.build_reference_wire(*intent.intent,*camera.state,context);REQUIRE(mp5);
    CHECK(mp5.command->buttons==g::kReferenceGoldSrcButtonAttack2);
    m.host.teardown();CHECK(m.host.edge_triggered_buttons(m.observed)==0);
}
