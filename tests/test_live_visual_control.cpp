#include <hlclient/app/live_visual_control.hpp>
#include <hlclient/goldsrc/live_runtime_stage.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <hlclient/goldsrc/usercmd_scheduler.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <stdexcept>
#include <vector>

namespace {

hlclient::gameplay_input::GameplayInputIntent intent(
    const std::uint64_t frames, const bool captured = true,
    const int mouse_x = 0, const int mouse_y = 0) {
  hlclient::input::InputStateTracker tracker;
  REQUIRE(frames > 0U);
  for (std::uint64_t index = 0; index < frames; ++index) {
    tracker.begin_frame();
    if (index == 0U) {
      tracker.apply_event(hlclient::input::InputEvent::focus_gained());
      if (captured) {
        tracker.apply_event(hlclient::input::InputEvent::capture_acquired());
      }
    }
    if (index + 1U == frames && (mouse_x != 0 || mouse_y != 0)) {
      tracker.apply_event(
          hlclient::input::InputEvent::mouse_motion(mouse_x, mouse_y));
    }
    const auto snapshot = tracker.publish_snapshot();
    if (index + 1U == frames) {
      auto bindings =
          hlclient::gameplay_input::GameplayInputBindings::project_default_v1();
      REQUIRE(bindings);
      auto built = hlclient::gameplay_input::GameplayInputIntentBuilder{}.build(
          snapshot, *bindings.bindings,
          hlclient::gameplay_input::MouseLookConfig{}, 0.02);
      REQUIRE(built);
      tracker.end_frame();
      return *built.intent;
    }
    tracker.end_frame();
  }
  throw std::logic_error{"unreachable live visual input helper state"};
}

hlclient::client::RuntimeClientObservationState observation(
    const std::uint64_t revision = 1U) {
  using namespace hlclient::client;
  RuntimeClientObservationState value;
  value.generation = 1U;
  value.publication_revision = revision;
  const RuntimeObservationSource source{
      revision, static_cast<std::size_t>(revision),
      10U + static_cast<std::uint32_t>(revision), 0U, 8U};
  value.server_time_seconds = 1.0;
  value.server_time_metadata = {
      1U, RuntimeObservationFreshness::retained,
      RuntimeObservationCompleteness::complete_reconstruction, source};
  value.entity_metadata = {
      1U, RuntimeObservationFreshness::unavailable,
      RuntimeObservationCompleteness::unavailable, std::nullopt};
  value.client_metadata = {
      1U, RuntimeObservationFreshness::observed_in_record,
      RuntimeObservationCompleteness::complete_reconstruction, source};
  RuntimeReceivingClientObservation client;
  client.health = 100.0;
  client.origin = {{10.0}, {20.0}, {30.0}};
  client.view_offset = {std::nullopt, std::nullopt, 28.0};
  value.receiving_client = client;
  value.canonical_state_hash = runtime_observation_canonical_hash(value);
  REQUIRE(valid_runtime_observation(value));
  return value;
}

} // namespace

TEST_CASE("Live visual input waits for one presented retained world frame") {
  using hlclient::app::live_visual_render_ready_for_input;
  CHECK_FALSE(live_visual_render_ready_for_input(0U, 1U, 1U));
  CHECK_FALSE(live_visual_render_ready_for_input(1U, 0U, 1U));
  CHECK_FALSE(live_visual_render_ready_for_input(1U, 1U, 0U));
  CHECK(live_visual_render_ready_for_input(1U, 1U, 1U));

  // Supported dynamic models may be absent, unsupported, or outside the
  // current view. The world presentation alone must not deadlock mouse look.
  CHECK(live_visual_render_ready_for_input(2U, 1U, 3U));
}

TEST_CASE("Live visual scheduler begins at fresh post-presentation time") {
  using hlclient::app::live_visual_render_ready_for_input;
  using namespace hlclient;

  goldsrc::GoldSrcUserCmdSchedulerConfig config;
  config.command_interval_nanoseconds = 20'000'000U;
  config.maximum_commands_per_update = 8U;
  goldsrc::GoldSrcUserCmdScheduler scheduler{config};
  gameplay_camera::GameplayCameraStateCreateInfo camera_info;
  auto camera = gameplay_camera::GameplayCameraState::create(camera_info);
  REQUIRE(camera);
  const auto input_state = intent(1U);

  // Sign-on and a five-second CPU/GPU preparation interval produce no
  // scheduler state and therefore no command backlog.
  CHECK_FALSE(live_visual_render_ready_for_input(0U, 0U, 0U));
  CHECK_FALSE(scheduler.state().initialized);
  constexpr std::int64_t presentation_completed = 5'000'000'000LL;
  CHECK(live_visual_render_ready_for_input(1U, 1U, 1U));
  const auto activated = scheduler.update(
      presentation_completed, input_state, *camera.state);
  REQUIRE(activated);
  CHECK(activated.requests.empty());
  CHECK(scheduler.state().next_sample_time_nanoseconds ==
        presentation_completed + 20'000'000LL);

  const auto first_sample = scheduler.update(
      presentation_completed + 20'000'000LL, input_state, *camera.state);
  REQUIRE(first_sample);
  REQUIRE(first_sample.requests.size() == 1U);
  CHECK(first_sample.requests.front().sample_time_nanoseconds ==
        presentation_completed + 20'000'000LL);

  // Later resource/camera revisions do not reset the retained scheduler.
  CHECK(live_visual_render_ready_for_input(2U, 1U, 2U));
  const auto second_sample = scheduler.update(
      presentation_completed + 40'000'000LL, input_state, *camera.state);
  REQUIRE(second_sample);
  REQUIRE(second_sample.requests.size() == 1U);
  CHECK(second_sample.requests.front().command_sequence.value() == 2U);

  // A real excessive post-activation stall remains a typed bounded failure.
  const auto stalled = scheduler.update(
      presentation_completed + 400'000'000LL, input_state, *camera.state);
  REQUIRE_FALSE(stalled);
  REQUIRE(stalled.error);
  CHECK(stalled.error->code ==
        goldsrc::GoldSrcUserCmdSchedulerErrorCode::lag_limit_exceeded);
  CHECK(stalled.requests.empty());
}

TEST_CASE(
    "Live visual camera uses receiving-client eye and local mouse orientation") {
  hlclient::app::LiveVisualCameraController controller;
  hlclient::client::ClientWorldState world;
  auto state = observation();

  const auto first = controller.update(&state, intent(1U), world);
  REQUIRE(first);
  REQUIRE(first.sample);
  CHECK(first.sample->fresh_server_sample);
  CHECK(first.sample->eye_position.x == 10.0F);
  CHECK(first.sample->eye_position.y == 20.0F);
  CHECK(first.sample->eye_position.z == 58.0F);
  CHECK(first.sample->vertical_only_view_offset_contract);

  const auto before = world.camera().position;
  const auto looked =
      controller.update(&state, intent(2U, true, 20, -10), world);
  REQUIRE(looked);
  REQUIRE(looked.sample);
  CHECK_FALSE(looked.sample->fresh_server_sample);
  CHECK(looked.local_orientation_changed);
  CHECK(world.camera().position.x == before.x);
  CHECK(world.camera().position.y == before.y);
  CHECK(world.camera().position.z == before.z);

  auto moved = observation(2U);
  moved.receiving_client->origin = {{13.0}, {17.0}, {31.0}};
  moved.canonical_state_hash =
      hlclient::client::runtime_observation_canonical_hash(moved);
  const auto server_move = controller.update(&moved, intent(3U), world);
  REQUIRE(server_move);
  REQUIRE(server_move.sample);
  CHECK(server_move.sample->fresh_server_sample);
  CHECK(world.camera().position.x == 13.0F);
  CHECK(world.camera().position.y == 17.0F);
  CHECK(world.camera().position.z == 59.0F);
  CHECK(server_move.sample->eye_position.x == 13.0F);
  CHECK(server_move.sample->eye_position.y == 17.0F);
  const auto server_hash = moved.canonical_state_hash;
  const auto predicted = controller.update(
      &moved, intent(4U), world, false,
      hlclient::app::LiveVisualPredictedView{
          {14.0F, 17.5F, 31.0F}, {0.0F, 0.0F, 28.0F}});
  REQUIRE(predicted);
  CHECK(predicted.sample->origin.x == 13.0F);
  CHECK(predicted.sample->eye_position.x == 14.0F);
  CHECK(predicted.sample->eye_position.y == 17.5F);
  CHECK(world.camera().position.x == 14.0F);
  CHECK(moved.canonical_state_hash == server_hash);
  auto retained = observation(3U);
  retained.receiving_client->origin = {{13.0}, {17.0}, {31.0}};
  retained.client_metadata.freshness =
      hlclient::client::RuntimeObservationFreshness::retained;
  retained.canonical_state_hash =
      hlclient::client::runtime_observation_canonical_hash(retained);
  const auto reused = controller.update(&retained, intent(5U), world);
  REQUIRE(reused);
  REQUIRE(reused.sample);
  CHECK_FALSE(reused.sample->fresh_server_sample);
}

TEST_CASE(
    "Live visual camera rejects stale generation dead and incomplete views") {
  hlclient::app::LiveVisualCameraController controller;
  hlclient::client::ClientWorldState world;
  auto state = observation();
  state.client_metadata.generation = 2U;
  CHECK(controller.update(&state, intent(1U), world).status ==
        hlclient::app::LiveVisualViewStatus::generation_mismatch);

  state = observation();
  state.receiving_client->health = 0.0;
  CHECK(controller.update(&state, intent(1U), world).status ==
        hlclient::app::LiveVisualViewStatus::receiving_client_not_alive);

  state = observation();
  state.receiving_client->view_offset.z.reset();
  CHECK(controller.update(&state, intent(1U), world).status ==
        hlclient::app::LiveVisualViewStatus::view_offset_unavailable);
}

TEST_CASE("Live visual camera applies each typed server angle correction once") {
  hlclient::app::LiveVisualCameraController controller;
  hlclient::client::ClientWorldState world;
  auto state = observation();
  state.view_angle_correction = hlclient::client::RuntimeViewAngleCorrection{
      12.0, 34.0, 0.0, *state.client_metadata.source};
  state.canonical_state_hash =
      hlclient::client::runtime_observation_canonical_hash(state);

  const auto corrected =
      controller.update(&state, intent(1U, true, 50, 25), world);
  REQUIRE(corrected);
  REQUIRE(corrected.sample);
  CHECK(corrected.sample->server_angle_correction_applied);
  CHECK(corrected.sample->local_pitch_degrees == 12.0);
  CHECK(corrected.sample->local_yaw_degrees == 34.0);

  const auto continued =
      controller.update(&state, intent(2U, true, 10, 0), world);
  REQUIRE(continued);
  REQUIRE(continued.sample);
  CHECK_FALSE(continued.sample->server_angle_correction_applied);
  CHECK(continued.sample->local_yaw_degrees != 34.0);
}

TEST_CASE("G server evaluator requires fresh origin velocity and view offset",
          "[live-visual][jump-duck][observation]") {
  using namespace hlclient;
  using Phase = goldsrc::LiveUserCmdInputPhase;
  auto make = [](const std::uint64_t revision, const Phase phase,
                 const double z, const double vz, const double view_z) {
    goldsrc::LiveUserCmdServerSample sample;
    sample.generation = 1U;
    sample.publication_revision = revision;
    sample.source = {revision, static_cast<std::size_t>(revision),
                     static_cast<std::uint32_t>(revision), 0U, 8U};
    sample.health = 100.0;
    sample.phase = phase;
    sample.origin = {0.0, 0.0, z};
    sample.velocity = {0.0, 0.0, vz};
    sample.view_offset = {std::nullopt, std::nullopt, view_z};
    sample.freshness =
        client::RuntimeObservationFreshness::observed_in_record;
    return sample;
  };
  const std::array<std::size_t, goldsrc::kLiveUserCmdPhaseCount> sent{
      100U, 60U, 100U, 75U, 125U};
  std::vector<goldsrc::LiveUserCmdServerSample> samples{
      make(1U, Phase::neutral_before, 100.0, 0.0, 28.0),
      make(2U, Phase::neutral_before, 100.0, 0.0, 28.0),
      make(3U, Phase::neutral_before, 100.0, 0.0, 28.0),
      make(4U, Phase::forward, 103.0, 90.0, 28.0),
      make(5U, Phase::neutral_middle, 105.0, -50.0, 28.0),
      make(6U, Phase::backward, 100.0, 0.0, 12.0),
      make(7U, Phase::neutral_tail, 100.0, 0.0, 28.0)};
  const auto verified = goldsrc::evaluate_live_jump_duck(samples, sent);
  CHECK(verified.outcome == goldsrc::LiveJumpDuckOutcome::verified);
  CHECK(verified.jump_observed);
  CHECK(verified.descent_observed);
  CHECK(verified.duck_observed);
  CHECK(verified.release_response_observed);

  auto static_samples = samples;
  for (auto &sample : static_samples) {
    sample.origin.z = 100.0;
    sample.velocity.z = 0.0;
    sample.view_offset.z = 28.0;
  }
  CHECK(goldsrc::evaluate_live_jump_duck(static_samples, sent).outcome ==
        goldsrc::LiveJumpDuckOutcome::
            buttons_transmitted_server_effect_unverified);
  auto stale = samples;
  stale[3].freshness = client::RuntimeObservationFreshness::retained;
  CHECK(goldsrc::evaluate_live_jump_duck(stale, sent).outcome ==
        goldsrc::LiveJumpDuckOutcome::observation_context_blocked);
  auto mixed_generation = samples;
  mixed_generation[4].generation = 2U;
  CHECK(goldsrc::evaluate_live_jump_duck(mixed_generation, sent).outcome ==
        goldsrc::LiveJumpDuckOutcome::observation_context_blocked);
  auto spawn_fall = samples;
  spawn_fall[0].origin.z = 120.0;
  spawn_fall[0].velocity.z = -16.0;
  CHECK(goldsrc::evaluate_live_jump_duck(spawn_fall, sent).outcome ==
        goldsrc::LiveJumpDuckOutcome::environment_limited);
  spawn_fall.push_back(make(8U, Phase::neutral_tail, 100.0, 0.0, 28.0));
  spawn_fall.insert(spawn_fall.begin() + 3,
                    make(4U, Phase::neutral_before, 100.0, 0.0, 28.0));
  for (std::size_t index = 4U; index < spawn_fall.size(); ++index)
    ++spawn_fall[index].publication_revision;
  CHECK(goldsrc::evaluate_live_jump_duck(spawn_fall, sent).outcome ==
        goldsrc::LiveJumpDuckOutcome::verified);
  auto unsettled = samples;
  unsettled[2].origin.z = 105.0;
  unsettled[2].velocity.z = -16.0;
  CHECK(goldsrc::evaluate_live_jump_duck(unsettled, sent).outcome ==
        goldsrc::LiveJumpDuckOutcome::environment_limited);
  auto low_ceiling = samples;
  low_ceiling.back().view_offset.z = 12.0;
  CHECK(goldsrc::evaluate_live_jump_duck(low_ceiling, sent).outcome ==
        goldsrc::LiveJumpDuckOutcome::environment_limited);
}

TEST_CASE("H1 speed observation requires distinct fresh server movement in both phases",
          "[live-visual][speed][observation]") {
  using namespace hlclient;
  using Phase = goldsrc::LiveUserCmdInputPhase;
  const auto make = [](std::uint64_t sequence, Phase phase,
                       double x, double vx) {
    goldsrc::LiveUserCmdServerSample sample;
    sample.generation = 1U;
    sample.publication_revision = sequence;
    sample.source = {sequence, static_cast<std::size_t>(sequence),
                     static_cast<std::uint32_t>(sequence), 0U, 8U};
    sample.health = 100.0;
    sample.phase = phase;
    sample.origin = {x, 0.0, 0.0};
    sample.velocity = {vx, 0.0, 0.0};
    sample.freshness = client::RuntimeObservationFreshness::observed_in_record;
    return sample;
  };
  const std::array<std::size_t, goldsrc::kLiveUserCmdPhaseCount> sent{
      100U, 30U, 50U, 50U, 50U};
  std::vector<goldsrc::LiveUserCmdServerSample> samples{
      make(1U, Phase::neutral_before, 0.0, 0.0),
      make(2U, Phase::neutral_before, 0.0, 0.0),
      make(3U, Phase::neutral_before, 0.0, 0.0),
      make(4U, Phase::forward, 1.0, 100.0),
      make(5U, Phase::forward, 15.0, 250.0),
      make(6U, Phase::neutral_middle, 16.0, 0.0),
      make(7U, Phase::backward, 16.0, 300.0),
      make(8U, Phase::backward, 20.0, 100.0),
      make(9U, Phase::neutral_tail, 21.0, 0.0)};
  CHECK(goldsrc::evaluate_live_speed_check(samples, sent, 0.0) ==
        goldsrc::LiveUserCmdMotionOutcome::verified);
  auto stationary = samples;
  for (auto &sample : stationary) {
    sample.origin.x = 0.0;
    sample.velocity.x = 0.0;
  }
  CHECK(goldsrc::evaluate_live_speed_check(stationary, sent, 0.0) ==
        goldsrc::LiveUserCmdMotionOutcome::server_motion_unverified);
  auto retained = samples;
  retained[4].freshness = client::RuntimeObservationFreshness::retained;
  CHECK(goldsrc::evaluate_live_speed_check(retained, sent, 0.0) ==
        goldsrc::LiveUserCmdMotionOutcome::observation_context_blocked);
  auto no_shift_motion = samples;
  no_shift_motion[7].origin.x = 16.0;
  CHECK(goldsrc::evaluate_live_speed_check(no_shift_motion, sent, 0.0) ==
        goldsrc::LiveUserCmdMotionOutcome::server_motion_unverified);
  auto unsettled = samples;
  unsettled[2].origin.z = 3.0;
  unsettled[2].velocity.z = -16.0;
  CHECK(goldsrc::evaluate_live_speed_check(unsettled, sent, 0.0) ==
        goldsrc::LiveUserCmdMotionOutcome::observation_context_blocked);
}
