#include "local_movement_test_fixture.hpp"
#include "lift_top_e32_1_test_fixture.hpp"
#include "vertical_lift_test_fixture.hpp"

#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <hlclient/goldsrc/reference_prediction_reconciliation.hpp>
#include <hlclient/goldsrc/reference_prediction_seed.hpp>
#include <hlclient/prediction/local_prediction.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <span>

namespace {
namespace goldsrc = hlclient::goldsrc;
namespace collision = hlclient::goldsrc::collision;
namespace movement = hlclient::goldsrc::movement;
namespace player = hlclient::movement;
namespace prediction = hlclient::prediction;
namespace lift = hlclient::tests::vertical_lift;

std::optional<player::LocalPlayerMovementState> seed(
    const goldsrc::ReferenceBrushCollisionContext& frame,
    const double x, const double height, const std::uint32_t boundary,
    movement::GoldSrcLocalMovementScratch& scratch, const std::uint16_t old_buttons = 0U) {
  goldsrc::ReferencePredictionSeed source;
  source.generation = 1U; source.record_identity = frame.record;
  source.command_boundary = *goldsrc::GoldSrcUserCmdSequence::create(boundary);
  source.origin = {x, 0.0, height + 36.0}; source.velocity = {0.0, 0.0, 0.0};
  source.view_offset = {0.0, 0.0, 28.0}; source.base_velocity = {0.0, 0.0, 0.0};
  source.flags = 1U << 9U; source.move_type = 3U; source.use_hull = 0U;
  source.gravity_multiplier = 1.0; source.friction_multiplier = 1.0;
  source.old_buttons = old_buttons;
  const movement::BrushSceneMovementCollision provider{frame.scene, frame.boxes};
  const auto grounded = goldsrc::derive_reference_prediction_ground(source, {}, provider, scratch, {},
      player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
  if (!grounded.state || !frame.server_time) return {};
  auto info = player::local_player_movement_state_create_info(*grounded.state);
  info.simulation_time_nanoseconds = static_cast<std::uint64_t>(std::llround(*frame.server_time * 1e9));
  return player::LocalPlayerMovementState::create(info).state;
}
} // namespace

TEST_CASE("E32.1 observed lift stop preserves the witnessed command pair at the same sample",
    "[e321][e321-b][d4-lift][movement][prediction][presentation][no-stock]") {
  const auto library = collision::build_brush_collision_model_library(
      hlclient::tests::lift_top_e32_1::package()); REQUIRE(library);
  const auto previous = lift::context(library.library, 1.0, 50.0, 1U);
  const auto last_moving = lift::context(library.library, 1.1, 70.0, 2U);
  goldsrc::ReferenceVerticalSupportMotion moving{previous, last_moving}; REQUIRE(moving.active());
  movement::GoldSrcLocalMovementScratch scratch;
  const movement::GoldSrcLocalMovementConfig config;
  const auto environment = hlclient::tests::local_movement::make_environment();
  const movement::BrushSceneMovementCollision provider{last_moving.scene};
  const movement::BrushSceneMovementCollision initial_provider{previous.scene};
  const auto initial = seed(previous, 0.0, 50.0, 7U, scratch); REQUIRE(initial);
  const auto identity = prediction::create_prediction_session_identity(1U, 1U, initial_provider,
      environment, config, *initial, prediction::PredictionCompatibilityProfile::reference_carrier_jump_duck_weapon_use_v4,
      prediction::PredictionAcknowledgementProfile::reference_sent_carrier_boundary_v1); REQUIRE(identity);
  auto history = prediction::LocalPredictionHistoryState::create_initial(*initial, *identity.session).history; REQUIRE(history);
  for (unsigned sequence = 8U; sequence <= 12U; ++sequence) {
    goldsrc::GoldSrcWireUserCmd wire; wire.msec = 20U;
    const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(sequence), wire); REQUIRE(command);
    const auto before = history->current_predicted_state();
    const auto moved = goldsrc::simulate_reference_movement(*before, *command.state,
        environment, provider, scratch, config, &moving); REQUIRE(moved);
    CHECK(moved.state->origin().z == Catch::Approx(86.0 + (sequence - 7U) * 4.0).margin(.001));
    const auto after = std::make_shared<const player::LocalPlayerMovementState>(*moved.state);
    const prediction::PredictedCommandAppend append{std::make_shared<const goldsrc::GoldSrcUserCmdState>(*command.state),
        before, after, moved.statistics, prediction::summarize_prediction_touches(moved.touches, false, false)};
    const auto published = prediction::append_local_prediction_commands(*history, std::span{&append, 1U}); REQUIRE(published);
    history = published.history;
  }
  const auto* pair = history->find_exact(*goldsrc::GoldSrcUserCmdSequence::create(12U)); REQUIRE(pair);
  const auto from = pair->pre_command_state();
  const auto before = pair->post_command_state();
  CHECK(from->origin().z == Catch::Approx(102.0).margin(.001));
  CHECK(before->origin().z == Catch::Approx(106.0).margin(.001));
  const auto stopped_frame = lift::context(library.library, 1.12, 70.0, 3U);
  goldsrc::ReferenceVerticalSupportMotion stopped{last_moving, stopped_frame}; REQUIRE(stopped.active());
  const auto correction = seed(stopped_frame, 0.0, 70.0, 12U, scratch); REQUIRE(correction);
  const movement::BrushSceneMovementCollision stopped_provider{stopped_frame.scene};
  const auto replayed = goldsrc::rebase_reference_prediction(*history, *correction,
      environment, stopped_provider, scratch, config, &stopped);
  REQUIRE(replayed.history); REQUIRE(replayed.raw_position_error);
  CHECK(*replayed.raw_position_error <= .001); CHECK(replayed.replayed_commands == 0U);
  REQUIRE(correction->ground_state().hit());
  REQUIRE(stopped.velocity_z_for(*correction->ground_state().hit()));
  CHECK(*stopped.velocity_z_for(*correction->ground_state().hit()) == 0.0);
  for (const double alpha : {0.0, .05, .25, .5, .75, .95, 1.0}) {
    CAPTURE(alpha);
    const auto old_sample = goldsrc::sample_reference_support_presentation(*from, *before,
        alpha, &moving, scratch.collision);
    const auto sample = goldsrc::sample_reference_support_presentation(*from,
        *replayed.history->current_predicted_state(), alpha, &stopped, scratch.collision);
    REQUIRE(old_sample.scene); REQUIRE(sample.scene);
    CHECK(sample.origin.z == Catch::Approx(102.0 + alpha * 4.0).margin(.001));
    CHECK(sample.origin.z == Catch::Approx(old_sample.origin.z).margin(.001));
    CHECK(sample.origin.z - sample.scene->instances().front().transform.translation.z ==
        Catch::Approx(36.0).margin(.001));
    const movement::BrushSceneMovementCollision presented{sample.scene};
    const auto body = presented.trace_hull(sample.trace_start, sample.origin, from->hull(), scratch.collision);
    REQUIRE(body); CHECK_FALSE(body.result->start_solid); CHECK_FALSE(body.result->all_solid);
    CHECK(body.result->fraction == 1.0);
    CHECK(player::local_player_movement_state_signature(*replayed.history->current_predicted_state()) ==
        player::local_player_movement_state_signature(*correction));
  }
  // The contact-based renderer must not attach an unrelated entity/plane or
  // interpolate an unvalidated physical hull transition.
  for (unsigned negative = 0U; negative < 3U; ++negative) {
    CAPTURE(negative);
    auto info = player::local_player_movement_state_create_info(*correction);
    if (negative == 0U) {
      info.ground.hit->source_entity_index = 77U;
      info.ground.hit->stable_instance_ordinal = 77U;
    } else if (negative == 1U) {
      REQUIRE(info.ground.plane.source_plane_index);
      ++*info.ground.plane.source_plane_index;
    } else {
      info.hull = player::PlayerMovementHull::ducked;
      info.origin.z = 88.0F; info.view_offset.z = 12.0F;
      info.ground.plane.distance = 88.0; info.ground.contact_position.z = 88.0F;
    }
    const auto unmatched = player::LocalPlayerMovementState::create(info); REQUIRE(unmatched);
    const auto sample = goldsrc::sample_reference_support_presentation(*from,
        *unmatched.state, .25, &stopped, scratch.collision);
    CHECK(sample.scene == stopped_frame.scene);
  }
  // Both endpoint hulls are free. A current remote BBOX intersects only the
  // interpolated chord, so matching support metadata cannot bypass it.
  auto start_info = player::local_player_movement_state_create_info(*from);
  start_info.origin.x = -24.0F; start_info.ground.contact_position.x = -24.0F;
  auto end_info = player::local_player_movement_state_create_info(*correction);
  end_info.origin.x = 24.0F; end_info.ground.contact_position.x = 24.0F;
  const auto negative_from = player::LocalPlayerMovementState::create(start_info); REQUIRE(negative_from);
  const auto negative_to = player::LocalPlayerMovementState::create(end_info); REQUIRE(negative_to);
  auto blocked_frame = stopped_frame;
  blocked_frame.boxes.push_back({3U, {{-4.F, -4.F, 100.F}, {4.F, 4.F, 108.F}}});
  goldsrc::ReferenceVerticalSupportMotion blocked_support{last_moving, blocked_frame}; REQUIRE(blocked_support.active());
  const movement::BrushSceneMovementCollision start_checked{moving.sample(1.08), blocked_frame.boxes};
  const movement::BrushSceneMovementCollision end_checked{blocked_frame.scene, blocked_frame.boxes};
  const auto start_free = start_checked.test_position(negative_from.state->origin(), negative_from.state->hull(), scratch.collision);
  const auto end_free = end_checked.test_position(negative_to.state->origin(), negative_to.state->hull(), scratch.collision);
  REQUIRE(start_free); REQUIRE(end_free);
  CHECK(start_free.result->status == movement::LocalMovementPositionStatus::free);
  CHECK(end_free.result->status == movement::LocalMovementPositionStatus::free);
  const auto blocked_sample = goldsrc::sample_reference_support_presentation(*negative_from.state,
      *negative_to.state, .5, &blocked_support, scratch.collision);
  CHECK(blocked_sample.scene == blocked_frame.scene);
  const movement::BrushSceneMovementCollision actual_blocker{blocked_sample.scene, blocked_frame.boxes};
  const auto blocked = actual_blocker.test_position(blocked_sample.origin, negative_to.state->hull(), scratch.collision);
  REQUIRE(blocked); CHECK(blocked.result->status == movement::LocalMovementPositionStatus::blocking);
}

TEST_CASE("E32.1 rising lift stops then stands and exits onto its finite upper floor",
    "[e321][e321-b][d4-lift][movement][prediction][upper-floor][no-stock]") {
  unsigned scenario = 0U;
  SECTION("standing centre") { scenario = 0U; }
  SECTION("walking forward onto upper floor") { scenario = 1U; }
  SECTION("walking backward onto mirrored upper floor") { scenario = 2U; }
  SECTION("edge position exits onto upper floor") { scenario = 3U; }
  SECTION("duck walk unduck upper-floor exit") { scenario = 4U; }
  SECTION("jump lands on upper floor") { scenario = 5U; }
  SECTION("real upper-floor wall stops movement") { scenario = 6U; }
  SECTION("low ceiling limits jump without disabling collision") { scenario = 7U; }
  SECTION("primary attack during stop and exit with three remote collision entities") { scenario = 8U; }
  const auto library = collision::build_brush_collision_model_library(
      hlclient::tests::lift_top_e32_1::package(scenario == 2U, scenario == 6U, scenario == 7U)); REQUIRE(library);
  const auto environment = hlclient::tests::local_movement::make_environment();
  const movement::GoldSrcLocalMovementConfig config;
  REQUIRE(environment.stop_speed() == 100.F);
  REQUIRE(environment.friction() == 4.F);
  REQUIRE(environment.acceleration() == 10.F);
  for (const double fps : {30.0, 60.0, 100.0, 144.0, 240.0, 0.0}) {
    CAPTURE(scenario, fps);
    movement::GoldSrcLocalMovementScratch scratch;
    const double x = scenario == 3U ? 46.0 : 0.0;
    auto old = lift::context(library.library, 1.0, 50.0, 1U);
    auto current = lift::context(library.library, 1.1, 70.0, 2U);
    const auto add_remote_boxes = [&](auto& frame) {
      if (scenario != 8U) return;
      for (unsigned entity = 2U; entity <= 4U; ++entity) {
        const float position = 512.0F + static_cast<float>(entity) * 64.0F;
        frame.boxes.push_back({entity, {{position - 16.F, 112.F, 70.F}, {position + 16.F, 144.F, 142.F}}});
      }
    };
    add_remote_boxes(old); add_remote_boxes(current);
    goldsrc::ReferenceVerticalSupportMotion motion{old, current}; REQUIRE(motion.active());
    auto state = seed(old, x, 50.0, 7U, scratch); REQUIRE(state);
    const movement::BrushSceneMovementCollision provider{current.scene};
    for (unsigned sequence = 8U; sequence <= 12U; ++sequence) {
      goldsrc::GoldSrcWireUserCmd wire; wire.msec = 20U;
      if (scenario == 8U) wire.buttons = goldsrc::kReferenceGoldSrcButtonAttack;
      const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(
          *goldsrc::GoldSrcUserCmdSequence::create(sequence), wire); REQUIRE(command);
      const auto moved = goldsrc::simulate_reference_movement(*state, *command.state,
          environment, provider, scratch, config, &motion); REQUIRE(moved);
      state.emplace(*moved.state);
    }
    CHECK(state->origin().z == Catch::Approx(106.0).margin(.001));
    old = current; current = lift::context(library.library, 1.12, 70.0, 3U);
    add_remote_boxes(current);
    motion = goldsrc::ReferenceVerticalSupportMotion{old, current}; REQUIRE(motion.active());
    const auto stop_seed = seed(current, x, 70.0, 12U, scratch, state->old_buttons()); REQUIRE(stop_seed);
    const movement::BrushSceneMovementCollision stopped_provider{current.scene, current.boxes};
    const auto session = prediction::create_prediction_session_identity(1U, 1U, stopped_provider,
        environment, config, *state, prediction::PredictionCompatibilityProfile::reference_carrier_jump_duck_weapon_use_v4,
        prediction::PredictionAcknowledgementProfile::reference_sent_carrier_boundary_v1); REQUIRE(session);
    const auto before_stop_history = prediction::LocalPredictionHistoryState::create_initial(*state, *session.session); REQUIRE(before_stop_history);
    const auto stopped_rebase = goldsrc::rebase_reference_prediction(*before_stop_history.history, *stop_seed,
        environment, stopped_provider, scratch, config, &motion);
    REQUIRE(stopped_rebase.history); REQUIRE(stopped_rebase.raw_position_error);
    CHECK(*stopped_rebase.raw_position_error <= .001); CHECK(stopped_rebase.replayed_commands == 0U);
    state.emplace(*stopped_rebase.history->current_predicted_state());
    std::optional<player::LocalPlayerMovementState> ordinary_state;
    movement::GoldSrcLocalMovementScratch ordinary_scratch;
    if (scenario == 8U) {
      auto ordinary_info = player::local_player_movement_state_create_info(*state);
      ordinary_info.old_buttons &= ~goldsrc::kReferenceGoldSrcButtonAttack;
      const auto ordinary_initial = player::LocalPlayerMovementState::create(ordinary_info);
      REQUIRE(ordinary_initial.state);
      ordinary_state.emplace(*ordinary_initial.state);
    }
    bool world_ground = false, airborne = false, ducked = false, ceiling_contact = false;
    float maximum_z = state->origin().z;
    std::uint64_t jumps = 0U;
    unsigned support_changes = 0U;
    auto previous_support = state->ground_state().hit();
    double next_frame = 1.12;
    for (unsigned index = 1U; index <= 100U; ++index) {
      goldsrc::GoldSrcWireUserCmd wire; wire.msec = 20U;
      wire.forward = scenario == 0U ? 0 : scenario == 2U ? -100 : 100;
      if (scenario == 4U && index <= 25U) wire.buttons = 4U;
      if ((scenario == 5U || scenario == 7U) && index == 1U) wire.buttons = 2U;
      if (scenario == 8U) wire.buttons = goldsrc::kReferenceGoldSrcButtonForward | goldsrc::kReferenceGoldSrcButtonAttack;
      const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(
          *goldsrc::GoldSrcUserCmdSequence::create(12U + index), wire); REQUIRE(command);
      if (scenario == 8U) {
        CHECK(command.state->msec() == 20U); CHECK(command.state->forward_move() == 100.F);
        CHECK(command.state->buttons() == (goldsrc::kReferenceGoldSrcButtonForward | goldsrc::kReferenceGoldSrcButtonAttack));
        CHECK(motion.entity_boxes().size() == 3U);
      }
      const auto before = *state;
      const auto moved = goldsrc::simulate_reference_movement(before, *command.state,
          environment, provider, scratch, config, &motion);
      INFO((moved.error ? moved.error->context : "ready")); REQUIRE(moved);
      if (scenario == 8U) {
        // Independently replay the same route and scene with only IN_ATTACK
        // removed. Presentation/fire must not add physical queries or work.
        auto ordinary_wire = wire;
        ordinary_wire.buttons &= ~goldsrc::kReferenceGoldSrcButtonAttack;
        const auto ordinary_command = goldsrc::reference_jump_duck_weapon_use_movement_command(
            *goldsrc::GoldSrcUserCmdSequence::create(12U + index), ordinary_wire); REQUIRE(ordinary_command);
        const auto ordinary = goldsrc::simulate_reference_movement(*ordinary_state,
            *ordinary_command.state, environment, provider, ordinary_scratch, config, &motion); REQUIRE(ordinary);
        const auto& a = moved.statistics;
        const auto& b = ordinary.statistics;
        CHECK(a.command_count == b.command_count); CHECK(a.substep_count == b.substep_count);
        CHECK(a.grounded_command_count == b.grounded_command_count); CHECK(a.airborne_command_count == b.airborne_command_count);
        CHECK(a.ground_probe_count == b.ground_probe_count); CHECK(a.trace_count == b.trace_count);
        CHECK(a.collision_hit_count == b.collision_hit_count); CHECK(a.slide_bump_count == b.slide_bump_count);
        CHECK(a.clip_plane_count == b.clip_plane_count); CHECK(a.step_attempt_count == b.step_attempt_count);
        CHECK(a.step_success_count == b.step_success_count); CHECK(a.jump_count == b.jump_count);
        CHECK(a.duck_enter_count == b.duck_enter_count); CHECK(a.duck_exit_count == b.duck_exit_count);
        CHECK(a.stand_blocked_count == b.stand_blocked_count); CHECK(a.start_solid_count == b.start_solid_count);
        CHECK(a.all_solid_count == b.all_solid_count);
        CHECK(a.total_horizontal_distance == b.total_horizontal_distance);
        CHECK(a.total_vertical_distance == b.total_vertical_distance);
        ordinary_state.emplace(*ordinary.state);
      }
      jumps += moved.statistics.jump_count;
      maximum_z = std::max(maximum_z, moved.state->origin().z);
      airborne |= !moved.state->ground_state().grounded();
      for (const auto& touch : moved.touches) ceiling_contact |= touch.plane.normal.z < -.7F;
      ducked |= moved.state->hull() == player::PlayerMovementHull::ducked;
      if (moved.state->ground_state().hit() != previous_support) ++support_changes;
      previous_support = moved.state->ground_state().hit();
      if (moved.state->ground_state().hit() && moved.state->ground_state().hit()->kind == player::PlayerMovementHitKind::world)
        world_ground = true;
      const double time = 1.12 + index * .020;
      const auto sampled_scene = motion.sample(time); REQUIRE(sampled_scene);
      const movement::BrushSceneMovementCollision actual{sampled_scene, motion.entity_boxes()};
      const auto clear = actual.test_position(moved.state->origin(), moved.state->hull(), scratch.collision);
      REQUIRE(clear); CHECK(clear.result->status == movement::LocalMovementPositionStatus::free);
      if (moved.state->ground_state().grounded()) {
        const double expected = moved.state->hull() == player::PlayerMovementHull::standing ? 106.0 : 88.0;
        CHECK(moved.state->origin().z == Catch::Approx(expected).margin(.05));
        CHECK(moved.state->velocity().z == 0.0F);
      }
      while (next_frame <= time) {
        const auto sample = goldsrc::sample_reference_support_presentation(before, *moved.state,
            (next_frame - (time - .020)) / .020, &motion, scratch.collision);
        REQUIRE(sample.scene);
        // Hull transitions have the existing discrete-body/eye policy; test
        // ordinary grounded pairs rather than demanding an interpolated hull.
        if (before.hull() == moved.state->hull() && before.ground_state().grounded() && moved.state->ground_state().grounded()) {
          const movement::BrushSceneMovementCollision displayed{sample.scene, motion.entity_boxes()};
          const auto tested = displayed.test_position(sample.origin, moved.state->hull(), scratch.collision);
          REQUIRE(tested); CHECK(tested.result->status == movement::LocalMovementPositionStatus::free);
        }
        next_frame += fps > 0.0 ? 1.0 / fps : (index % 2U ? .007 : .013);
      }
      state.emplace(*moved.state);
      if (index % 4U == 0U) {
        old = current; current = lift::context(library.library, time, 70.0, 3U + index);
        add_remote_boxes(current);
        motion = goldsrc::ReferenceVerticalSupportMotion{old, current}; REQUIRE(motion.active());
      }
    }
    REQUIRE(state->ground_state().hit());
    if (scenario == 0U) {
      CHECK(state->origin().x == 0.0F); CHECK(state->origin().z == 106.0F);
      CHECK(state->ground_state().hit()->source_entity_index == 42U); CHECK_FALSE(world_ground);
    } else {
      CHECK(world_ground); CHECK(state->ground_state().hit()->kind == player::PlayerMovementHitKind::world);
      CHECK(std::abs(state->origin().x) > 80.0F);
    }
    if (scenario == 4U) { CHECK(ducked); CHECK(state->hull() == player::PlayerMovementHull::standing); }
    if (scenario == 5U) { CHECK(airborne); CHECK(jumps == 1U); }
    if (scenario == 7U) {
      // Its2-unit standing head clearance is inside the source ground probe:
      // the accepted jump can be recategorized grounded after a real ceiling
      // clip. Require the blocker and exactly one jump, not an airborne flag.
      CHECK(ceiling_contact); CHECK(jumps == 1U);
      CHECK(maximum_z <= 108.0F);
    }
    if (scenario == 6U) CHECK(state->origin().x <= 144.0F);
    if (scenario == 1U || scenario == 8U) {
      // Independent pinned SDK-profile scalar oracle: dt=.02, stopspeed100,
      // friction4 and accel10 with wish100. Friction removes8 below100;
      // acceleration adds20. First seven velocities20,32,44,56,68,80,92
      // cover7.84units, then commands8..100 cover93*2=186units.
      CHECK(state->origin().x == Catch::Approx(193.84).margin(.002));
    }
    if (scenario == 8U) {
      CHECK(support_changes == 1U); CHECK(jumps == 0U);
      CHECK(state->simulation_time_nanoseconds() == 3'120'000'000ULL);
      CHECK(state->old_buttons() == (goldsrc::kReferenceGoldSrcButtonForward | goldsrc::kReferenceGoldSrcButtonAttack));
      REQUIRE(ordinary_state);
      CHECK(ordinary_state->origin().x == Catch::Approx(193.84).margin(.002));
      // The held Attack bit is deliberately different input state, not a
      // physics difference. Compare the complete final signature after
      // removing only that bit, retaining Forward and every physical field.
      auto physical_info = player::local_player_movement_state_create_info(*state);
      physical_info.old_buttons &= ~goldsrc::kReferenceGoldSrcButtonAttack;
      const auto physical = player::LocalPlayerMovementState::create(physical_info); REQUIRE(physical);
      CHECK(player::local_player_movement_state_signature(*physical.state) ==
          player::local_player_movement_state_signature(*ordinary_state));
    }
    CHECK(state->origin().z == Catch::Approx(106.0).margin(.05));
  }
}
