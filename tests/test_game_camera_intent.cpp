#include <hlclient/app/live_visual_control.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
