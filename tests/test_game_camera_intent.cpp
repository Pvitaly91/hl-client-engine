#include <hlclient/app/live_visual_control.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

TEST_CASE("Authoritative death camera transitions by absolute time without predicting or crossing lives",
          "[game-api][camera-policy][death-camera][e25]") {
  using namespace hlclient;
  input::InputStateTracker tracker;const auto bindings=gameplay_input::GameplayInputBindingsBuilder{}.build({});REQUIRE(bindings);
  const auto next_input=[&] {
    tracker.begin_frame();const auto snapshot=tracker.publish_snapshot();
    const auto built=gameplay_input::GameplayInputIntentBuilder{}.build(snapshot,*bindings.bindings,gameplay_input::MouseLookConfig{},.02);
    REQUIRE(built);tracker.end_frame();return *built.intent;
  };
  client::RuntimeClientObservationState state;state.generation=1;state.lifecycle.life_epoch=3;
  state.client_metadata={1,client::RuntimeObservationFreshness::observed_in_record,
    client::RuntimeObservationCompleteness::complete_reconstruction,client::RuntimeObservationSource{1,1,1,0,8}};
  state.receiving_client.emplace();state.receiving_client->origin={0,0,36};state.receiving_client->view_offset={0,0,28};
  state.receiving_client->health=100;
  app::LiveVisualCameraController camera;client::ClientWorldState world;game_api::CameraIntent policy;
  REQUIRE(camera.update(&state,next_input(),world,policy,false,{},4./3.,0.));
  state.lifecycle.state=client::LocalPlayerLifeState::dead;state.lifecycle.deaths=1;state.receiving_client->health=0;
  state.receiving_client->origin.x=10;state.receiving_client->view_offset.z=-8;
  state.client_metadata.source=client::RuntimeObservationSource{2,2,2,0,8};
  policy.allow_predicted_translation=false;policy.authoritative_translation_blend_seconds=.1;
  const auto before=state;
  const app::LiveVisualPredictedView forbidden_prediction{{999,999,999},{0,0,999}};
  REQUIRE(camera.update(&state,next_input(),world,policy,false,forbidden_prediction,4./3.,.1));
  CHECK(world.camera().position.x==0);CHECK(world.camera().position.z==64);
  REQUIRE(camera.update(&state,next_input(),world,policy,false,forbidden_prediction,4./3.,.15));
  CHECK(world.camera().position.x==Catch::Approx(5));CHECK(world.camera().position.z==Catch::Approx(46));
  REQUIRE(camera.update(&state,next_input(),world,policy,false,{},4./3.,.2));CHECK(world.camera().position.x==10);
  state.receiving_client->origin.x=20;state.client_metadata.source=client::RuntimeObservationSource{3,3,3,0,8};
  REQUIRE(camera.update(&state,next_input(),world,policy,false,{},4./3.,.2));
  REQUIRE(camera.update(&state,next_input(),world,policy,false,{},4./3.,.225));CHECK(world.camera().position.x==Catch::Approx(12.5));
  REQUIRE(camera.update(&state,next_input(),world,policy,false,{},4./3.,.25));CHECK(world.camera().position.x==Catch::Approx(15));
  REQUIRE(camera.update(&state,next_input(),world,policy,false,{},4./3.,.3));CHECK(world.camera().position.x==20);
  CHECK(before.receiving_client->origin.x==10);CHECK(state.receiving_client->origin.x==20); // no canonical/physics write
  // Old observation cannot retarget an active newer camera timeline.
  state.receiving_client->origin.x=-100;state.client_metadata.source=client::RuntimeObservationSource{2,2,2,0,8};
  REQUIRE(camera.update(&state,next_input(),world,policy,false,{},4./3.,.31));CHECK(world.camera().position.x==20);
  state.generation=state.client_metadata.generation=2;state.receiving_client->origin.x=40;
  REQUIRE(camera.update(&state,next_input(),world,policy,false,{},4./3.,.32));CHECK(world.camera().position.x==40);
  state.lifecycle.life_epoch=4;state.lifecycle.state=client::LocalPlayerLifeState::alive;state.receiving_client->health=100;
  state.receiving_client->origin.x=50;state.receiving_client->view_offset.z=28;policy={};
  REQUIRE(camera.update(&state,next_input(),world,policy,false,{},4./3.,.33));CHECK(world.camera().position.x==50);
  policy.allow_predicted_translation=false;policy.authoritative_translation_blend_seconds=.1;
  state.lifecycle.life_epoch=5;state.receiving_client->origin.x=60;
  REQUIRE(camera.update(&state,next_input(),world,policy,false,{},4./3.,.34));CHECK(world.camera().position.x==60);
  // Missing time does not start an implicit clock or extrapolate.
  state.receiving_client->origin.x=70;
  REQUIRE(camera.update(&state,next_input(),world,policy,false));CHECK(world.camera().position.x==70);
}

TEST_CASE("Core camera defers spawn correction until ready and consumes it once per generation",
          "[spawn-orientation][game-api][camera-policy][live-visual]") {
  using namespace hlclient;
  input::InputStateTracker tracker;
  const auto bindings = gameplay_input::GameplayInputBindingsBuilder{}.build({});
  REQUIRE(bindings);
  auto next_input = [&] {
    tracker.begin_frame();
    const auto snapshot = tracker.publish_snapshot();
    const auto result = gameplay_input::GameplayInputIntentBuilder{}.build(
        snapshot, *bindings.bindings, gameplay_input::MouseLookConfig{}, 0.02);
    REQUIRE(result);
    tracker.end_frame();
    return *result.intent;
  };
  client::RuntimeClientObservationState state;
  state.generation = 1;
  state.client_metadata = {1, client::RuntimeObservationFreshness::retained,
      client::RuntimeObservationCompleteness::complete_reconstruction,
      client::RuntimeObservationSource{2,2,2,0,8}};
  state.receiving_client.emplace();
  state.receiving_client->view_offset = {0.0,0.0,28.0};
  state.view_angle_correction = client::RuntimeViewAngleCorrection{
      0.0,180.0,0.0,{1,1,1,0,56}};
  app::LiveVisualCameraController camera;
  client::ClientWorldState world;
  game_api::CameraIntent policy;
  CHECK_FALSE(camera.update(&state,next_input(),world,policy));
  state.receiving_client->origin = {10.0,20.0,30.0};
  const auto first = camera.update(&state,next_input(),world,policy);
  REQUIRE(first);
  CHECK(first.sample->server_angle_correction_applied);
  CHECK(camera.yaw_degrees() == -180.0);
  CHECK(world.camera().target.x == Catch::Approx(9.0));
  const auto again = camera.update(&state,next_input(),world,policy);
  REQUIRE(again);
  CHECK_FALSE(again.sample->server_angle_correction_applied);
  // Generation is part of identity even if a new session reuses source IDs.
  state.generation = state.client_metadata.generation = 2;
  state.view_angle_correction->yaw_degrees = 90.0;
  const auto next = camera.update(&state,next_input(),world,policy);
  REQUIRE(next);
  CHECK(next.sample->server_angle_correction_applied);
  CHECK(camera.yaw_degrees() == 90.0);
  CHECK(world.camera().target.y == Catch::Approx(21.0));
}

TEST_CASE("Core camera follows neutral policy without deriving game eligibility from health",
          "[game-api][camera-policy][live-visual]") {
  using namespace hlclient;
  input::InputStateTracker tracker;
  tracker.begin_frame();
  const auto snapshot = tracker.publish_snapshot();
  const auto bindings = gameplay_input::GameplayInputBindingsBuilder{}.build({});
  REQUIRE(bindings);
  const auto input = gameplay_input::GameplayInputIntentBuilder{}.build(
      snapshot,*bindings.bindings,gameplay_input::MouseLookConfig{},0.02);
  REQUIRE(input);
  client::RuntimeClientObservationState state;
  state.generation = 1U;
  state.client_metadata.generation = 1U;
  state.client_metadata.freshness = client::RuntimeObservationFreshness::observed_in_record;
  state.client_metadata.source = client::RuntimeObservationSource{1U,1U,1U,0U,8U};
  state.receiving_client.emplace();
  state.receiving_client->origin = {10.0,20.0,30.0};
  state.receiving_client->view_offset = {0.0,0.0,2.0};
  state.lifecycle.state = client::LocalPlayerLifeState::dead;
  // No health, and a deliberately different policy permits predicted camera.
  // The generic controller must not silently choose Half-Life eligibility.
  game_api::CameraIntent policy;
  policy.allow_predicted_translation = true;
  policy.yaw_offset_degrees = 90.0;
  app::LiveVisualCameraController camera;
  client::ClientWorldState world;
  const app::LiveVisualPredictedView predicted{{100,200,300},{0,0,3}};
  const auto allowed = camera.update(&state,*input.intent,world,policy,false,predicted);
  REQUIRE(allowed);
  CHECK(allowed.sample->eye_position.x == 100.0F);
  CHECK(allowed.sample->eye_position.z == 303.0F);
  CHECK(world.camera().target.y == Catch::Approx(201.0F));
  CHECK(camera.yaw_degrees() == 0.0);
  // Publishing a changed camera requires the next real input-frame identity.
  // Policy substitution does not bypass ClientWorldState's monotonic contract.
  tracker.end_frame();
  tracker.begin_frame();
  const auto next_snapshot = tracker.publish_snapshot();
  const auto next_input = gameplay_input::GameplayInputIntentBuilder{}.build(
      next_snapshot,*bindings.bindings,gameplay_input::MouseLookConfig{},0.02);
  REQUIRE(next_input);
  CHECK(next_input.intent->input_sequence() == input.intent->input_sequence() + 1U);
  policy.allow_predicted_translation = false;
  const auto observed = camera.update(&state,*next_input.intent,world,policy,false,predicted);
  REQUIRE(observed);
  CHECK(observed.sample->eye_position.x == 10.0F);
  CHECK(observed.sample->eye_position.z == 32.0F);
  policy.status = game_api::CameraIntentStatus::receiving_client_not_alive;
  CHECK(camera.update(&state,*next_input.intent,world,policy).status ==
        app::LiveVisualViewStatus::receiving_client_not_alive);
  tracker.end_frame();
}

TEST_CASE("Core neutral horizontal projection uses actual aspect and rejects invalid policy atomically",
          "[e18][game-api][camera-policy][live-visual]") {
  using namespace hlclient;
  input::InputStateTracker tracker;
  const auto bindings=gameplay_input::GameplayInputBindingsBuilder{}.build({});REQUIRE(bindings);
  const auto next=[&] {
    tracker.begin_frame();const auto snapshot=tracker.publish_snapshot();tracker.end_frame();
    auto result=gameplay_input::GameplayInputIntentBuilder{}.build(snapshot,*bindings.bindings,{},.02);
    REQUIRE(result);return *result.intent;
  };
  client::RuntimeClientObservationState observed;observed.generation=1;
  observed.client_metadata={1,client::RuntimeObservationFreshness::observed_in_record,
      client::RuntimeObservationCompleteness::complete_reconstruction,client::RuntimeObservationSource{1,1,0,0,8}};
  observed.receiving_client.emplace();
  observed.receiving_client->origin={10.,20.,30.};observed.receiving_client->view_offset={0.,0.,2.};
  app::LiveVisualCameraController camera;client::ClientWorldState world;
  game_api::CameraIntent policy; // No concrete module, catalogue or installed game.
  for(double aspect:{4./3.,16./9.,16./10.}) {
    policy.horizontal_field_of_view_degrees=37.;
    REQUIRE(camera.update(&observed,next(),world,policy,false,{},aspect));
    CHECK(world.camera().vertical_field_of_view_radians==Catch::Approx(
        2*std::atan(std::tan(37.*std::acos(-1.)/360.)/aspect)));
    const auto before=world.camera();
    for(double invalid:{0.,180.,std::numeric_limits<double>::quiet_NaN()}) {
      policy.horizontal_field_of_view_degrees=invalid;
      CHECK_FALSE(camera.update(&observed,next(),world,policy,false,{},aspect));
      CHECK(world.camera()==before);
    }
  }
}
