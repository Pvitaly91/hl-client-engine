#include "local_movement_test_fixture.hpp"
#include "vertical_lift_test_fixture.hpp"
#include "lift_top_e32_1_test_fixture.hpp"

#include <hlclient/app/movement_diagnostics.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <hlclient/goldsrc/reference_prediction_reconciliation.hpp>
#include <hlclient/goldsrc/reference_prediction_seed.hpp>
#include <hlclient/prediction/local_prediction.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace {
namespace client = hlclient::client;
namespace goldsrc = hlclient::goldsrc;
namespace collision = hlclient::goldsrc::collision;
namespace movement = hlclient::goldsrc::movement;
namespace player = hlclient::movement;
namespace prediction = hlclient::prediction;

goldsrc::ReferenceBrushCollisionContext lift_frame(
    const std::shared_ptr<const collision::BrushCollisionModelLibrary>& library,
    const double time, const double height, const std::uint64_t record,
    const unsigned actors, const float box_offset = 0.0F) {
  client::RuntimeClientObservationState observation;
  observation.generation = 1U;
  observation.publication_revision = record;
  observation.entity_metadata = {1U, client::RuntimeObservationFreshness::observed_in_record,
      client::RuntimeObservationCompleteness::complete_reconstruction,
      client::RuntimeObservationSource{record, static_cast<std::size_t>(record)}};
  observation.server_time_seconds = time;
  observation.server_time_metadata = observation.entity_metadata;
  client::RuntimePacketEntityObservation lift;
  lift.entity_number = 42U; lift.model_index = 27U; lift.solid = 4U;
  lift.brush_move_type = 7U; lift.origin = {0.0, 0.0, height};
  lift.angles = {0.0, 0.0, 0.0};
  observation.packet_entities.push_back(lift);
  // Authored collision observations only. Counts include the receiving player;
  // they do not claim the actual LAN bot count or run any bot AI.
  for (unsigned entity = 2U; entity <= actors; ++entity) {
    client::RuntimePacketEntityObservation box;
    box.entity_number = entity; box.solid = 2U;
    box.owner_entity_reference = 0U;
    box.origin = {512.0 + entity * 64.0 + box_offset, 0.0, height + 36.0};
    box.collision_mins = {-16.0, -16.0, -36.0};
    box.collision_maxs = {16.0, 16.0, 36.0};
    observation.packet_entities.push_back(box);
  }
  return goldsrc::build_reference_brush_collision(
      library, {{27U, "*1"}}, observation, 1U, 2U, 1U, true);
}

goldsrc::ReferencePredictionSeed lift_seed(const double height, const std::uint32_t boundary = 7U) {
  goldsrc::ReferencePredictionSeed seed;
  seed.generation = 1U; seed.record_identity = 2U;
  seed.command_boundary = *goldsrc::GoldSrcUserCmdSequence::create(boundary);
  seed.origin = {0.0, 0.0, height + 36.0};
  seed.velocity = {0.0, 0.0, 0.0}; seed.view_offset = {0.0, 0.0, 28.0};
  seed.base_velocity = {0.0, 0.0, 0.0}; seed.flags = 1U << 9U;
  seed.move_type = 3U; seed.use_hull = 0U;
  seed.gravity_multiplier = 1.0; seed.friction_multiplier = 1.0;
  return seed;
}

std::optional<player::LocalPlayerMovementState> timed_seed(
    const goldsrc::ReferenceBrushCollisionContext& context,
    movement::GoldSrcLocalMovementScratch& scratch,
    const movement::GoldSrcLocalMovementConfig& config, const double height,
    const std::uint32_t boundary = 7U) {
  const movement::BrushSceneMovementCollision provider{context.scene, context.boxes};
  const auto grounded = goldsrc::derive_reference_prediction_ground(
      lift_seed(height, boundary), {}, provider, scratch, config,
      player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
  if (!grounded.state || !context.server_time) return {};
  auto info = player::local_player_movement_state_create_info(*grounded.state);
  info.simulation_time_nanoseconds = static_cast<std::uint64_t>(std::llround(*context.server_time * 1e9));
  const auto result = player::LocalPlayerMovementState::create(info);
  return result.state;
}
} // namespace

TEST_CASE("E32 lift commands preserve raw endpoint and camera across actor and render load",
    "[e32][e32-lift][d4-lift][movement][prediction][brush][no-stock]") {
  const auto library = collision::build_brush_collision_model_library(
      hlclient::tests::vertical_lift::package());
  REQUIRE(library);
  const auto environment = hlclient::tests::local_movement::make_environment();
  const movement::GoldSrcLocalMovementConfig config;
  for (const unsigned actors : {2U, 8U, 16U, 32U})
  for (const double fps : {30.0, 60.0, 100.0, 144.0, 240.0, 0.0})
  for (const double speed : {100.0, -100.0, 0.0}) {
    CAPTURE(actors, fps, speed);
    auto previous = lift_frame(library.library, 1.0, 50.0, 1U, actors);
    auto current = lift_frame(library.library, 1.1, 50.0 + speed * .1, 2U, actors);
    REQUIRE(current.scene); REQUIRE(current.boxes.size() == actors - 1U);
    goldsrc::ReferenceVerticalSupportMotion support{previous, current};
    REQUIRE(support.active());
    movement::GoldSrcLocalMovementScratch scratch;
    const auto initial = timed_seed(current, scratch, config, 50.0 + speed * .1);
    REQUIRE(initial);
    movement::BrushSceneMovementCollision provider{current.scene, current.boxes};
    const auto identity = prediction::create_prediction_session_identity(1U, 1U,
        provider, environment, config, *initial,
        prediction::PredictionCompatibilityProfile::reference_carrier_jump_duck_weapon_use_v4,
        prediction::PredictionAcknowledgementProfile::reference_sent_carrier_boundary_v1);
    REQUIRE(identity);
    auto history = prediction::LocalPredictionHistoryState::create_initial(*initial, *identity.session).history;
    REQUIRE(history);
    unsigned render_samples = 0U;
    double next_frame = 1.1;
    std::uint64_t record = 2U;
    for (unsigned command_index = 1U; command_index <= 10U; ++command_index) {
      goldsrc::GoldSrcWireUserCmd wire; wire.msec = 20U;
      const auto sequence = *goldsrc::GoldSrcUserCmdSequence::create(7U + command_index);
      const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(sequence, wire);
      REQUIRE(command);
      const auto before = history->current_predicted_state();
      const auto moved = goldsrc::simulate_reference_movement(*before, *command.state,
          environment, provider, scratch, config, &support);
      INFO((moved.error ? moved.error->context : "ready")); REQUIRE(moved);
      const double end_time = 1.1 + command_index * .020;
      CHECK(moved.state->origin().z == Catch::Approx(86.0 + speed * (end_time - 1.0)).margin(.001));
      CHECK(moved.state->velocity().z == 0.0F);
      REQUIRE(moved.state->ground_state().hit());
      CHECK(moved.state->ground_state().hit()->source_entity_index == 42U);
      const auto after = std::make_shared<const player::LocalPlayerMovementState>(*moved.state);
      const prediction::PredictedCommandAppend append{
          std::make_shared<const goldsrc::GoldSrcUserCmdState>(*command.state), before, after,
          moved.statistics, prediction::summarize_prediction_touches(moved.touches, false, false)};
      const auto published = prediction::append_local_prediction_commands(*history, std::span{&append, 1U});
      REQUIRE(published); history = published.history;
      const auto signature = player::local_player_movement_state_signature(*after);
      while (next_frame <= end_time) {
        const auto sample = goldsrc::sample_reference_support_presentation(
            *before, *after, (next_frame - (end_time - .020)) / .020, &support, scratch.collision);
        REQUIRE(sample.scene);
        CHECK(sample.origin.z - sample.scene->instances().front().transform.translation.z ==
            Catch::Approx(36.0).margin(.001));
        const movement::BrushSceneMovementCollision rendered{sample.scene, support.entity_boxes()};
        const auto clear = rendered.test_position(sample.origin, after->hull(), scratch.collision);
        REQUIRE(clear); CHECK(clear.result->status == movement::LocalMovementPositionStatus::free);
        CHECK(player::local_player_movement_state_signature(*history->current_predicted_state()) == signature);
        ++render_samples;
        next_frame += fps > 0.0 ? 1.0 / fps : (render_samples % 2U ? .007 : .013);
      }
      if (command_index == 4U || command_index == 7U) {
        const auto boundary = 7U + command_index - 2U;
        const double time = 1.1 + (command_index - 2U) * .020;
        const double height = 50.0 + speed * (time - 1.0);
        auto updated = lift_frame(library.library, time, height, ++record, actors,
            static_cast<float>(command_index));
        goldsrc::ReferenceVerticalSupportMotion motion{current, updated}; REQUIRE(motion.active());
        movement::BrushSceneMovementCollision updated_provider{updated.scene, updated.boxes};
        const auto correction = timed_seed(updated, scratch, config, height, boundary); REQUIRE(correction);
        const auto replayed = goldsrc::rebase_reference_prediction(*history, *correction,
            environment, updated_provider, scratch, config, &motion);
        REQUIRE(replayed.history); REQUIRE(replayed.raw_position_error);
        CHECK(*replayed.raw_position_error <= .001);
        CHECK(replayed.replayed_commands == 2U);
        CHECK(replayed.history->current_predicted_state()->origin().z == Catch::Approx(after->origin().z).margin(.001));
        const auto duplicate = goldsrc::rebase_reference_prediction(*replayed.history, *correction,
            environment, updated_provider, scratch, config, &motion);
        REQUIRE(duplicate.history); REQUIRE(duplicate.raw_position_error);
        CHECK(*duplicate.raw_position_error <= .001);
        CHECK(duplicate.history->current_predicted_state()->origin().z == Catch::Approx(after->origin().z).margin(.001));
        history = replayed.history; previous = current; current = updated; support = motion;
      }
    }
    CHECK(history->current_predicted_state()->source_command_sequence() == 17U);
    CHECK(history->current_predicted_state()->simulation_time_nanoseconds() == 1'300'000'000ULL);
    CHECK(history->current_predicted_state()->origin().z == Catch::Approx(86.0 + speed * .3).margin(.001));
    CHECK(render_samples > 0U);
  }
}

TEST_CASE("E32 same-time support refresh retains motion and replaces blocker ownership",
    "[e32][e32-lift][d4-lift][movement][prediction][brush][no-stock]") {
  const auto library = collision::build_brush_collision_model_library(
      hlclient::tests::vertical_lift::package()); REQUIRE(library);
  for (const unsigned actors : {2U, 8U, 16U, 32U}) {
    CAPTURE(actors);
    const auto previous = lift_frame(library.library, 1.0, 50.0, 1U, actors);
    const auto current = lift_frame(library.library, 1.1, 60.0, 2U, actors);
    auto updated = lift_frame(library.library, 1.1, 60.0, 3U, actors, 128.0F);
    goldsrc::ReferenceVerticalSupportMotion support{previous, current}; REQUIRE(support.active());
    CHECK(support.refresh_same_time_context(updated)); CHECK(support.active());
    REQUIRE(support.entity_boxes().size() == actors - 1U);
    CHECK(support.entity_boxes().front().bounds.minimum.x == 752.0F);
    const auto sampled = support.sample(1.12); REQUIRE(sampled);
    CHECK(sampled->instances().front().transform.translation.z == Catch::Approx(62.0).margin(.001));
    movement::GoldSrcLocalMovementScratch scratch;
    movement::BrushSceneMovementCollision actual{sampled, support.entity_boxes()};
    const auto old_location = actual.test_position({640.0F, 0.0F, 96.0F}, player::PlayerMovementHull::standing, scratch.collision);
    REQUIRE(old_location); CHECK(old_location.result->status == movement::LocalMovementPositionStatus::free);
    const auto new_location = actual.test_position({768.0F, 0.0F, 96.0F}, player::PlayerMovementHull::standing, scratch.collision);
    REQUIRE(new_location); CHECK(new_location.result->status == movement::LocalMovementPositionStatus::blocking);
    REQUIRE(new_location.result->hit); CHECK(new_location.result->hit->kind == player::PlayerMovementHitKind::server_entity_box);
    CHECK(new_location.result->hit->source_entity_index == 2U);
    const movement::GoldSrcLocalMovementConfig config;
    const auto seed = timed_seed(updated, scratch, config, 60.0); REQUIRE(seed);
    goldsrc::GoldSrcWireUserCmd wire; wire.msec = 20U;
    const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(8U), wire); REQUIRE(command);
    const auto moved = goldsrc::simulate_reference_movement(*seed, *command.state,
        hlclient::tests::local_movement::make_environment(), actual, scratch, config, &support);
    REQUIRE(moved); CHECK(moved.state->origin().z == Catch::Approx(98.0).margin(.001));
    CHECK(moved.state->velocity().z == 0.0F);
    CHECK_FALSE(scratch.last_reference_collision_trace);
    CHECK_FALSE(scratch.last_reference_collision_position);
    // Source dedup must neither adopt modified borrowed data nor erase the
    // retained trajectory. It must not extend its original 250 ms horizon.
    CHECK_FALSE(support.refresh_same_time_context(updated));
    CHECK(support.sample(5.0)->instances().front().transform.translation.z == Catch::Approx(85.0).margin(.001));
  }
}

TEST_CASE("E32 support refresh rejects different clocks and clears changed instance trajectories",
    "[e32][e32-lift][d4-lift][movement][prediction][brush][no-stock]") {
  const auto library = collision::build_brush_collision_model_library(
      hlclient::tests::vertical_lift::package()); REQUIRE(library);
  const auto previous = lift_frame(library.library, 1.0, 50.0, 1U, 2U);
  const auto current = lift_frame(library.library, 1.1, 60.0, 2U, 2U);
  auto updated = lift_frame(library.library, 1.1, 60.0, 3U, 2U);
  goldsrc::ReferenceVerticalSupportMotion support{previous, current}; REQUIRE(support.active());
  bool coherent = true;
  SECTION("new source time requires fresh derivation") { updated.server_time = 1.2; coherent = false; }
  SECTION("missing source time does not invent a clock") { updated.server_time.reset(); coherent = false; }
  SECTION("different map generation is rejected") { updated.generation = 2U; coherent = false; }
  SECTION("different model library is rejected") {
    const auto other = collision::build_brush_collision_model_library(hlclient::tests::vertical_lift::package()); REQUIRE(other);
    updated = lift_frame(other.library, 1.1, 60.0, 3U, 2U); coherent = false;
  }
  SECTION("moving transform changed at same time is not assigned a guessed speed") {
    updated = lift_frame(library.library, 1.1, 61.0, 3U, 2U);
  }
  SECTION("removed support cannot survive in the owning scene") {
    updated.scene = std::make_shared<const collision::BrushCollisionScene>(library.library,
        std::vector<collision::BrushCollisionSceneInstance>{}, collision::BrushCollisionRoleProviderProfile::reference_entity_solid_bsp_v1);
    updated.pushers.clear();
  }
  SECTION("removed pusher role cannot keep inferred carry") { updated.pushers.clear(); }
  SECTION("replacement runtime identity cannot inherit old support velocity") {
    auto instances = std::vector<collision::BrushCollisionSceneInstance>{updated.scene->instances().begin(), updated.scene->instances().end()};
    instances.front().identity.source_entity_index = 77U;
    instances.front().identity.stable_instance_ordinal = 77U;
    updated.pushers = {instances.front().identity};
    updated.scene = std::make_shared<const collision::BrushCollisionScene>(library.library,
        std::move(instances), collision::BrushCollisionRoleProviderProfile::reference_entity_solid_bsp_v1);
  }
  CHECK(support.refresh_same_time_context(updated) == coherent);
  if (!coherent) {
    CHECK(support.active());
    CHECK(support.sample(1.12)->instances().front().transform.translation.z == Catch::Approx(62.0).margin(.001));
  } else {
    CHECK_FALSE(support.active());
    CHECK(support.sample(1.12) == updated.scene);
  }
}

TEST_CASE("E32 same-time change of a second pusher does not erase the unchanged lift",
    "[e32][e32-lift][d4-lift][movement][prediction][brush][no-stock]") {
  const auto library = collision::build_brush_collision_model_library(
      hlclient::tests::vertical_lift::package()); REQUIRE(library);
  const auto add_second = [&](goldsrc::ReferenceBrushCollisionContext frame, const float z) {
    auto instances = std::vector<collision::BrushCollisionSceneInstance>{frame.scene->instances().begin(), frame.scene->instances().end()};
    auto other = instances.front();
    other.identity = {53U, 1U, 53U};
    other.transform.translation.x = 256.0F;
    other.transform.translation.z = z;
    // Exact instance bounds come from the same tested builder used for motion.
    const collision::ExplicitSyntheticBrushCollisionRoleProvider roles{{
        {instances.front().identity, collision::BrushCollisionRole::solid},
        {other.identity, collision::BrushCollisionRole::solid}}};
    const std::array definitions{
        collision::BrushCollisionInstanceDefinition{instances.front().identity, instances.front().transform},
        collision::BrushCollisionInstanceDefinition{other.identity, other.transform}};
    const auto scene = collision::build_brush_collision_scene(library.library, definitions, roles);
    REQUIRE(scene); frame.scene = scene.scene; frame.pushers.push_back(other.identity);
    return frame;
  };
  const auto previous = add_second(lift_frame(library.library, 1.0, 50.0, 1U, 2U), 50.0F);
  const auto current = add_second(lift_frame(library.library, 1.1, 60.0, 2U, 2U), 50.0F);
  const auto updated = add_second(lift_frame(library.library, 1.1, 60.0, 3U, 2U), 51.0F);
  goldsrc::ReferenceVerticalSupportMotion support{previous, current}; REQUIRE(support.active());
  CHECK(support.refresh_same_time_context(updated)); CHECK(support.active());
  const auto sample = support.sample(1.12); REQUIRE(sample);
  const auto lift = std::find_if(sample->instances().begin(), sample->instances().end(),
      [](const auto& item) { return item.identity.source_entity_index == 42U; });
  const auto other = std::find_if(sample->instances().begin(), sample->instances().end(),
      [](const auto& item) { return item.identity.source_entity_index == 53U; });
  REQUIRE(lift != sample->instances().end()); REQUIRE(other != sample->instances().end());
  CHECK(lift->transform.translation.z == Catch::Approx(62.0).margin(.001));
  CHECK(other->transform.translation.z == 51.0F);
  CHECK(support.displacement({player::PlayerMovementHitKind::brush_entity, 1U, 42U, 42U}, 1.1, 1.12) == Catch::Approx(2.0).margin(.001));
  CHECK(support.displacement({player::PlayerMovementHitKind::brush_entity, 1U, 53U, 53U}, 1.1, 1.12) == 0.0F);
}

TEST_CASE("E32 failed lift carry retains the actual ceiling query and clears stale evidence",
    "[e32][e32-lift][d4-lift][movement][prediction][brush][diagnostics][no-stock]") {
  const auto library = collision::build_brush_collision_model_library(
      hlclient::tests::vertical_lift::package(true)); REQUIRE(library);
  const auto previous = lift_frame(library.library, 1.0, 77.0, 1U, 2U);
  const auto current = lift_frame(library.library, 1.1, 87.0, 2U, 2U);
  goldsrc::ReferenceVerticalSupportMotion support{previous, current}; REQUIRE(support.active());
  movement::GoldSrcLocalMovementScratch scratch;
  const movement::GoldSrcLocalMovementConfig config;
  const auto seed = timed_seed(current, scratch, config, 87.0); REQUIRE(seed);
  const movement::BrushSceneMovementCollision actual{current.scene, current.boxes};
  goldsrc::GoldSrcWireUserCmd wire; wire.msec = 20U;
  const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(
      *goldsrc::GoldSrcUserCmdSequence::create(8U), wire); REQUIRE(command);
  const auto signature = player::local_player_movement_state_signature(*seed);
  const auto blocked = goldsrc::simulate_reference_movement(*seed, *command.state,
      hlclient::tests::local_movement::make_environment(), actual, scratch, config, &support);
  REQUIRE_FALSE(blocked); REQUIRE(blocked.error);
  CHECK(blocked.error->code == movement::LocalMovementSimulationErrorCode::movement_trace_failed);
  REQUIRE(scratch.last_reference_collision_trace);
  REQUIRE(scratch.last_reference_collision_trace->hit);
  CHECK(scratch.last_reference_collision_trace->hit->kind == player::PlayerMovementHitKind::world);
  CHECK_FALSE(scratch.last_reference_collision_trace->start_solid);
  CHECK_FALSE(scratch.last_reference_collision_trace->all_solid);
  CHECK(scratch.last_reference_collision_trace->fraction < 1.0);
  REQUIRE(scratch.last_reference_collision_trace->collision_plane);
  CHECK(scratch.last_reference_collision_trace->collision_plane->normal.z == -1.0F);
  CHECK(scratch.last_reference_collision_hull == player::PlayerMovementHull::standing);
  REQUIRE(scratch.last_reference_collision_start); REQUIRE(scratch.last_reference_collision_end);
  CHECK(scratch.last_reference_collision_start->z == 123.0F);
  CHECK(scratch.last_reference_collision_end->z == 125.0F);
  CHECK(player::local_player_movement_state_signature(*seed) == signature);
  const auto first_failed_fraction = scratch.last_reference_collision_trace->fraction;
  CHECK(first_failed_fraction == Catch::Approx(.5).margin(1e-9));
  CHECK(scratch.last_reference_collision_trace->end_position.z == 124.0F);

  // Source-backed mechanism control, not a replay of missing LAN pusher
  // observations: a fresh rising sample can still predict a genuine ceiling
  // crossing. It must replace the previous query evidence and fail atomically.
  const auto approaching = lift_frame(library.library, 1.11, 87.75, 3U, 2U);
  goldsrc::ReferenceVerticalSupportMotion approaching_motion{current, approaching};
  REQUIRE(approaching_motion.active());
  const auto approaching_seed = timed_seed(approaching, scratch, config, 87.75);
  REQUIRE(approaching_seed);
  const auto approaching_signature = player::local_player_movement_state_signature(*approaching_seed);
  const movement::BrushSceneMovementCollision approaching_collision{approaching.scene, approaching.boxes};
  const auto blocked_again = goldsrc::simulate_reference_movement(*approaching_seed, *command.state,
      hlclient::tests::local_movement::make_environment(), approaching_collision, scratch, config,
      &approaching_motion);
  REQUIRE_FALSE(blocked_again); REQUIRE(blocked_again.error);
  CHECK_FALSE(blocked_again.state);
  CHECK(blocked_again.error->code == movement::LocalMovementSimulationErrorCode::movement_trace_failed);
  CHECK(player::local_player_movement_state_signature(*approaching_seed) == approaching_signature);
  CHECK(player::local_player_movement_state_signature(*seed) == signature);
  REQUIRE(scratch.last_reference_collision_trace);
  const auto& second_failed_trace = *scratch.last_reference_collision_trace;
  REQUIRE(second_failed_trace.hit);
  CHECK(second_failed_trace.hit->kind == player::PlayerMovementHitKind::world);
  CHECK_FALSE(second_failed_trace.start_solid); CHECK_FALSE(second_failed_trace.all_solid);
  CHECK(second_failed_trace.fraction == Catch::Approx(1.0 / 6.0).margin(1e-9));
  CHECK(second_failed_trace.fraction != first_failed_fraction);
  CHECK(second_failed_trace.end_position.z == 124.0F);
  REQUIRE(second_failed_trace.collision_plane);
  CHECK(second_failed_trace.collision_plane->normal.z == -1.0F);
  CHECK(scratch.last_reference_collision_hull == player::PlayerMovementHull::standing);
  CHECK_FALSE(scratch.last_reference_collision_position);
  REQUIRE(scratch.last_reference_collision_start); REQUIRE(scratch.last_reference_collision_end);
  CHECK(scratch.last_reference_collision_start->z == 123.75F);
  CHECK(scratch.last_reference_collision_end->z == 125.25F);

  // The next observed reversal replaces the old positive estimate immediately.
  // The independently authored -100 units/s and 20 ms command require z120.75;
  // merely remaining active or retaining the old endpoint would not pass.
  const auto reversed = lift_frame(library.library, 1.12, 86.75, 4U, 2U);
  goldsrc::ReferenceVerticalSupportMotion reversed_motion{approaching, reversed};
  REQUIRE(reversed_motion.active());
  const auto reversed_seed = timed_seed(reversed, scratch, config, 86.75); REQUIRE(reversed_seed);
  const auto reversed_signature = player::local_player_movement_state_signature(*reversed_seed);
  const movement::BrushSceneMovementCollision reversed_collision{reversed.scene, reversed.boxes};
  const auto after_reversal = goldsrc::simulate_reference_movement(*reversed_seed, *command.state,
      hlclient::tests::local_movement::make_environment(), reversed_collision, scratch, config,
      &reversed_motion);
  REQUIRE(after_reversal);
  CHECK(after_reversal.state->origin().z == Catch::Approx(120.75).margin(.001));
  CHECK(after_reversal.state->origin().x == 0.0F);
  CHECK(after_reversal.state->origin().y == 0.0F);
  CHECK(after_reversal.state->velocity().x == 0.0F);
  CHECK(after_reversal.state->velocity().y == 0.0F);
  CHECK(after_reversal.state->velocity().z == 0.0F);
  CHECK(player::local_player_movement_state_signature(*reversed_seed) == reversed_signature);
  CHECK_FALSE(scratch.last_reference_collision_trace);
  CHECK_FALSE(scratch.last_reference_collision_position);
  CHECK_FALSE(scratch.last_reference_collision_hull);
  CHECK_FALSE(scratch.last_reference_collision_start);
  CHECK_FALSE(scratch.last_reference_collision_end);

  // One coherent stopped interval recovers without moving the canonical seed.
  const auto stopped = lift_frame(library.library, 1.2, 87.0, 3U, 2U);
  goldsrc::ReferenceVerticalSupportMotion idle{current, stopped}; REQUIRE(idle.active());
  const auto recovered = timed_seed(stopped, scratch, config, 87.0); REQUIRE(recovered);
  const movement::BrushSceneMovementCollision stopped_collision{stopped.scene, stopped.boxes};
  const auto ready = goldsrc::simulate_reference_movement(*recovered, *command.state,
      hlclient::tests::local_movement::make_environment(), stopped_collision, scratch, config, &idle);
  REQUIRE(ready); CHECK(ready.state->origin().z == 123.0F);
  CHECK_FALSE(scratch.last_reference_collision_trace);
  CHECK_FALSE(scratch.last_reference_collision_position);
  CHECK_FALSE(scratch.last_reference_collision_hull);
}

TEST_CASE("Lift trajectory refresh preserves an already generated grounded endpoint",
    "[e32-lift][movement][prediction][brush][lift-trajectory-refresh][no-stock]") {
  const auto library = collision::build_brush_collision_model_library(
      hlclient::tests::vertical_lift::package()); REQUIRE(library);
  const auto old = lift_frame(library.library, 1.0, 50.0, 1U, 2U);
  const auto current = lift_frame(library.library, 1.1, 70.0, 2U, 2U);
  const goldsrc::ReferenceVerticalSupportMotion original_motion{old, current};
  REQUIRE(original_motion.active());
  const movement::GoldSrcLocalMovementConfig config;
  movement::GoldSrcLocalMovementScratch scratch;
  const auto environment = hlclient::tests::local_movement::make_environment();
  const auto seed = timed_seed(current, scratch, config, 70.0); REQUIRE(seed);
  const auto seed_signature = player::local_player_movement_state_signature(*seed);
  const movement::BrushSceneMovementCollision original_collision{current.scene, current.boxes};
  goldsrc::GoldSrcWireUserCmd wire; wire.msec = 20U;
  const auto first_command = goldsrc::reference_jump_duck_weapon_use_movement_command(
      *goldsrc::GoldSrcUserCmdSequence::create(8U), wire); REQUIRE(first_command);
  const auto first = goldsrc::simulate_reference_movement(*seed, *first_command.state,
      environment, original_collision, scratch, config, &original_motion); REQUIRE(first);
  REQUIRE(first.state->ground_state().grounded());
  REQUIRE(first.state->ground_state().hit());
  CHECK(first.state->origin().z == Catch::Approx(110.0).margin(.001));
  const auto generated_signature = player::local_player_movement_state_signature(*first.state);

  // A legal new pusher sample arrives between local commands, without a new
  // acknowledged player seed. Its 212.5 u/s trajectory replaces 200 u/s. At
  // the already generated endpoint time (1.12), its top is now 0.25 units
  // above the previous top: this is not a new player/server penetration.
  const auto updated = lift_frame(library.library, 1.11, 72.125, 3U, 2U);
  const goldsrc::ReferenceVerticalSupportMotion refreshed_motion{current, updated};
  REQUIRE(refreshed_motion.active());
  // The render in between packet arrival and the next local command must
  // keep the old, witnessed command pair free of its support too.
  for (const double alpha : {0.0, .1, .25, .5, .75, .9, 1.0}) {
    CAPTURE(alpha);
    const auto sample = goldsrc::sample_reference_support_presentation(
        *seed, *first.state, alpha, &refreshed_motion, scratch.collision);
    REQUIRE(sample.scene);
    CHECK(sample.origin.z == Catch::Approx(106.0 + 4.0 * alpha).margin(.001));
    const movement::BrushSceneMovementCollision sampled{sample.scene, updated.boxes};
    const auto path = sampled.trace_hull(sample.trace_start, sample.origin,
        first.state->hull(), scratch.collision, config.collision_query); REQUIRE(path);
    CHECK_FALSE(path.result->start_solid); CHECK_FALSE(path.result->all_solid);
    CHECK(path.result->fraction == 1.0);
  }
  const movement::BrushSceneMovementCollision refreshed_collision{updated.scene, updated.boxes};
  const auto second_command = goldsrc::reference_jump_duck_weapon_use_movement_command(
      *goldsrc::GoldSrcUserCmdSequence::create(9U), wire); REQUIRE(second_command);
  const auto second = goldsrc::simulate_reference_movement(*first.state, *second_command.state,
      environment, refreshed_collision, scratch, config, &refreshed_motion);
  INFO((second.error ? second.error->context : "no simulation error"));
  REQUIRE(second);
  CHECK(second.state->origin().z == Catch::Approx(114.5).margin(.001));
  CHECK(second.state->origin().x == 0.0F);
  CHECK(second.state->origin().y == 0.0F);
  CHECK(second.state->velocity().z == 0.0F);
  CHECK(second.state->simulation_time_nanoseconds() == 1'140'000'000ULL);
  CHECK(second.state->ground_state().grounded());
  CHECK(second.state->ground_state().hit() == first.state->ground_state().hit());
  const movement::BrushSceneMovementCollision final_collision{
      refreshed_motion.sample(1.14), updated.boxes};
  const auto clear = final_collision.test_position(second.state->origin(), second.state->hull(),
      scratch.collision, config.collision_query); REQUIRE(clear);
  CHECK(clear.result->status == movement::LocalMovementPositionStatus::free);
  CHECK(player::local_player_movement_state_signature(*seed) == seed_signature);
  CHECK(player::local_player_movement_state_signature(*first.state) == generated_signature);
}

TEST_CASE("Quantized lift poses and irregular source clocks retain a finite supported command path",
    "[e32-lift][movement][prediction][brush][lift-trajectory-refresh][lift-quantized-refresh][no-stock]") {
  // Independently authored 1/128-unit poses and binary32 svc_time clocks.
  // The old estimate is about195.8u/s; fresh estimates are about191.4/200u/s.
  // Both signs of the small projected phase difference must remain grounded.
  for (const double next_height : {72.875, 73.0234375}) {
    CAPTURE(next_height);
    const auto library = collision::build_brush_collision_model_library(
        hlclient::tests::vertical_lift::package()); REQUIRE(library);
    const double old_time = static_cast<double>(1.0F);
    const double current_time = static_cast<double>(1.1F);
    const double next_time = static_cast<double>(1.1172256F);
    constexpr double current_height = 69.578125;
    const auto old = lift_frame(library.library, old_time, 50.0, 1U, 2U);
    const auto current = lift_frame(library.library, current_time, current_height, 2U, 2U);
    const goldsrc::ReferenceVerticalSupportMotion original{old, current}; REQUIRE(original.active());
    const movement::GoldSrcLocalMovementConfig config;
    movement::GoldSrcLocalMovementScratch scratch;
    const auto environment = hlclient::tests::local_movement::make_environment();
    const auto seed = timed_seed(current, scratch, config, current_height); REQUIRE(seed);
    const movement::BrushSceneMovementCollision initial_collision{current.scene, current.boxes};
    goldsrc::GoldSrcWireUserCmd wire; wire.msec = 20U;
    const auto first_command = goldsrc::reference_jump_duck_weapon_use_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(8U), wire); REQUIRE(first_command);
    const auto first = goldsrc::simulate_reference_movement(*seed, *first_command.state,
        environment, initial_collision, scratch, config, &original); REQUIRE(first);
    const auto first_signature = player::local_player_movement_state_signature(*first.state);
    const auto updated = lift_frame(library.library, next_time, next_height, 3U, 2U);
    const goldsrc::ReferenceVerticalSupportMotion refreshed{current, updated}; REQUIRE(refreshed.active());
    const movement::BrushSceneMovementCollision fresh{updated.scene, updated.boxes};
    const auto second_command = goldsrc::reference_jump_duck_weapon_use_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(9U), wire); REQUIRE(second_command);
    const auto second = goldsrc::simulate_reference_movement(*first.state, *second_command.state,
        environment, fresh, scratch, config, &refreshed);
    INFO((second.error ? second.error->context : "no simulation error")); REQUIRE(second);
    const double speed = (next_height - current_height) / (next_time - current_time);
    REQUIRE(speed >= 191.0); REQUIRE(speed <= 209.0);
    const double end_seconds = static_cast<double>(second.state->simulation_time_nanoseconds()) * 1e-9;
    const float expected_brush_z = static_cast<float>(next_height + speed * (end_seconds - next_time));
    const float expected_contact_z = expected_brush_z + 36.0F;
    CHECK(std::isfinite(second.state->origin().z));
    CHECK(second.state->origin().z == Catch::Approx(expected_contact_z).margin(.001));
    CHECK(second.state->origin().z >= expected_contact_z);
    CHECK(second.state->origin().z > first.state->origin().z);
    CHECK(second.state->velocity().z == 0.0F);
    CHECK(second.state->ground_state().grounded());
    CHECK(second.state->ground_state().hit() == first.state->ground_state().hit());
    const movement::BrushSceneMovementCollision final_collision{refreshed.sample(end_seconds), updated.boxes};
    const auto clear = final_collision.test_position(second.state->origin(), second.state->hull(), scratch.collision);
    REQUIRE(clear); CHECK(clear.result->status == movement::LocalMovementPositionStatus::free);
    double last_z = first.state->origin().z;
    for (const double alpha : {0.0, .1, .25, .5, .75, .9, 1.0}) {
      CAPTURE(alpha);
      const auto sample = goldsrc::sample_reference_support_presentation(
          *first.state, *second.state, alpha, &refreshed, scratch.collision);
      REQUIRE(sample.scene); CHECK(std::isfinite(sample.origin.z)); CHECK(sample.origin.z >= last_z);
      const movement::BrushSceneMovementCollision sampled{sample.scene, updated.boxes};
      const auto path = sampled.trace_hull(sample.trace_start, sample.origin, second.state->hull(), scratch.collision);
      REQUIRE(path); CHECK_FALSE(path.result->start_solid); CHECK_FALSE(path.result->all_solid);
      CHECK(path.result->fraction == 1.0); last_z = sample.origin.z;
    }
    CHECK(player::local_player_movement_state_signature(*first.state) == first_signature);
  }
}

TEST_CASE("Lift trajectory refresh does not turn unrelated penetration into carry",
    "[e32-lift][movement][prediction][brush][lift-trajectory-refresh][no-stock]") {
  unsigned negative = 0U;
  SECTION("fresh authoritative seed is never repositioned") { negative = 0U; }
  SECTION("unrelated brush identity is not adopted") { negative = 1U; }
  SECTION("another source plane is not adopted") { negative = 2U; }
  SECTION("real world ceiling remains blocking") { negative = 3U; }
  SECTION("another server entity remains blocking") { negative = 4U; }
  SECTION("duck transition is not silently reattached") { negative = 5U; }
  SECTION("jump is not silently reattached") { negative = 6U; }
  CAPTURE(negative);
  // Leave 1/8 unit of legal clearance below the real world ceiling. Only the
  // refreshed 1/4-unit top correction, not the original carry, crosses it.
  const double height = negative == 3U ? 83.875 : 70.0;
  const auto library = collision::build_brush_collision_model_library(
      hlclient::tests::vertical_lift::package(negative == 3U)); REQUIRE(library);
  const auto old = lift_frame(library.library, 1.0, height - 20.0, 1U, 2U);
  const auto current = lift_frame(library.library, 1.1, height, 2U, 2U);
  const goldsrc::ReferenceVerticalSupportMotion original{old, current}; REQUIRE(original.active());
  const movement::GoldSrcLocalMovementConfig config;
  movement::GoldSrcLocalMovementScratch scratch;
  const auto environment = hlclient::tests::local_movement::make_environment();
  const auto seed = timed_seed(current, scratch, config, height); REQUIRE(seed);
  const movement::BrushSceneMovementCollision original_collision{current.scene, current.boxes};
  goldsrc::GoldSrcWireUserCmd wire; wire.msec = 20U;
  const auto first_command = goldsrc::reference_jump_duck_weapon_use_movement_command(
      *goldsrc::GoldSrcUserCmdSequence::create(8U), wire); REQUIRE(first_command);
  const auto first = goldsrc::simulate_reference_movement(*seed, *first_command.state,
      environment, original_collision, scratch, config, &original);
  INFO((first.error ? first.error->context : "clear original carry")); REQUIRE(first);
  auto info = player::local_player_movement_state_create_info(*first.state);
  REQUIRE(info.ground.hit); REQUIRE(info.ground.plane.source_plane_index);
  if (negative == 0U) info.state_revision = 1U;
  if (negative == 1U) {
    info.ground.hit->source_entity_index = 77U; info.ground.hit->stable_instance_ordinal = 77U;
  }
  if (negative == 2U) ++*info.ground.plane.source_plane_index;
  if (negative == 5U) { info.in_duck_transition = true; info.duck_time_milliseconds = 100U; }
  if (negative == 6U) wire.buttons = goldsrc::kReferenceGoldSrcButtonJump;
  const auto before = player::LocalPlayerMovementState::create(info); REQUIRE(before);
  const auto signature = player::local_player_movement_state_signature(*before.state);
  auto updated = lift_frame(library.library, 1.11, height + 2.125, 3U, 2U);
  if (negative == 4U)
    updated.boxes.push_back({99U, {{-16.0F, -16.0F, 146.125F}, {16.0F, 16.0F, 148.0F}}});
  const goldsrc::ReferenceVerticalSupportMotion refreshed{current, updated}; REQUIRE(refreshed.active());
  const movement::BrushSceneMovementCollision changed{updated.scene, updated.boxes};
  const auto next_command = goldsrc::reference_jump_duck_weapon_use_movement_command(
      *goldsrc::GoldSrcUserCmdSequence::create(9U), wire); REQUIRE(next_command);
  const auto result = goldsrc::simulate_reference_movement(*before.state, *next_command.state,
      environment, changed, scratch, config, &refreshed);
  CHECK_FALSE(result); CHECK_FALSE(result.state);
  CHECK(player::local_player_movement_state_signature(*before.state) == signature);
  REQUIRE(scratch.last_reference_collision_position);
  REQUIRE(scratch.last_reference_collision_position->hit);
  if (negative == 3U)
    CHECK(scratch.last_reference_collision_position->hit->kind == player::PlayerMovementHitKind::world);
  if (negative == 4U)
    CHECK(scratch.last_reference_collision_position->hit->source_entity_index == 99U);
}

TEST_CASE("A refreshed rising lift acquires an already generated coplanar world contact",
    "[lift-refreshed-top-handoff][movement][prediction][brush][no-stock]") {
  unsigned scenario = 0U;
  SECTION("generated world top is acquired without an interruption") { scenario = 0U; }
  SECTION("fresh authoritative world seed remains untouched") { scenario = 1U; }
  SECTION("actual observed penetration is not projected contact") { scenario = 2U; }
  SECTION("real world ceiling blocks the later carry") { scenario = 3U; }
  SECTION("other server entity blocks the projected contact") { scenario = 4U; }
  SECTION("detached world contact is not carried") { scenario = 5U; }
  SECTION("stopped lift cannot acquire a world contact") { scenario = 6U; }
  SECTION("descending lift cannot acquire a world contact") { scenario = 7U; }
  SECTION("jump is not reattached") { scenario = 8U; }
  SECTION("duck transition is not reattached") { scenario = 9U; }
  CAPTURE(scenario);
  const auto library = collision::build_brush_collision_model_library(
      hlclient::tests::lift_top_e32_1::package(false, false, scenario == 3U)); REQUIRE(library);
  const auto old = lift_frame(library.library, 1.0, 46.0, 1U, 2U);
  const auto current = lift_frame(library.library, 1.1, 66.0, 2U, 2U);
  const goldsrc::ReferenceVerticalSupportMotion original{old, current}; REQUIRE(original.active());
  const movement::GoldSrcLocalMovementConfig config;
  movement::GoldSrcLocalMovementScratch scratch;
  const auto environment = hlclient::tests::local_movement::make_environment();
  const movement::BrushSceneMovementCollision observed{current.scene, current.boxes};
  auto source = lift_seed(70.0); source.origin.x = scenario == 5U ? 84.0 : 52.0;
  const auto derived = goldsrc::derive_reference_prediction_ground(source, {}, observed, scratch, config,
      player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2); REQUIRE(derived.state);
  auto info = player::local_player_movement_state_create_info(*derived.state);
  info.simulation_time_nanoseconds = 1'100'000'000ULL;
  const auto initial = player::LocalPlayerMovementState::create(info); REQUIRE(initial);
  goldsrc::GoldSrcWireUserCmd wire; wire.msec = 20U;
  const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(
      *goldsrc::GoldSrcUserCmdSequence::create(8U), wire); REQUIRE(command);
  const auto first = goldsrc::simulate_reference_movement(*initial.state, *command.state,
      environment, observed, scratch, config, &original); REQUIRE(first);
  REQUIRE(first.state->ground_state().hit());
  REQUIRE(first.state->ground_state().hit()->kind == player::PlayerMovementHitKind::world);
  REQUIRE(first.state->origin().z == 106.0F);
  auto before_info = player::local_player_movement_state_create_info(*first.state);
  if (scenario == 1U) before_info.state_revision = 1U;
  if (scenario == 9U) { before_info.in_duck_transition = true; before_info.duck_time_milliseconds = 100U; }
  const auto before = player::LocalPlayerMovementState::create(before_info); REQUIRE(before);
  const auto signature = player::local_player_movement_state_signature(*before.state);
  const double updated_time = scenario == 2U ? 1.117 : 1.11;
  const double updated_height = scenario == 2U ? 70.125 : scenario == 6U ? 66.0 : scenario == 7U ? 64.0 : 68.125;
  auto updated = lift_frame(library.library, updated_time, updated_height, 3U, 2U);
  if (scenario == 4U)
    updated.boxes.push_back({99U, {{36.0F, -16.0F, 142.125F}, {68.0F, 16.0F, 144.0F}}});
  const goldsrc::ReferenceVerticalSupportMotion refreshed{current, updated}; REQUIRE(refreshed.active());
  const movement::BrushSceneMovementCollision fresh{updated.scene, updated.boxes};
  const auto observed_clear = fresh.test_position(before.state->origin(), before.state->hull(), scratch.collision);
  REQUIRE(observed_clear);
  CHECK((observed_clear.result->status == movement::LocalMovementPositionStatus::free) == (scenario != 2U));
  if (scenario == 8U) wire.buttons = goldsrc::kReferenceGoldSrcButtonJump;
  const auto next = goldsrc::reference_jump_duck_weapon_use_movement_command(
      *goldsrc::GoldSrcUserCmdSequence::create(9U), wire); REQUIRE(next);
  const auto result = goldsrc::simulate_reference_movement(*before.state, *next.state,
      environment, fresh, scratch, config, &refreshed);
  INFO((result.error ? result.error->context : "no simulation error"));
  if (scenario == 0U) {
    REQUIRE(result);
    CHECK(result.state->origin().z == Catch::Approx(110.5).margin(.001));
    CHECK(result.state->origin().x == 52.0F); CHECK(result.state->origin().y == 0.0F);
    CHECK(result.state->velocity().z == 0.0F);
    REQUIRE(result.state->ground_state().hit());
    CHECK(result.state->ground_state().hit()->source_entity_index == 42U);
  } else if (scenario >= 5U && scenario <= 7U) {
    REQUIRE(result);
    CHECK(result.state->origin().z == 106.0F);
    REQUIRE(result.state->ground_state().hit());
    CHECK(result.state->ground_state().hit()->kind == player::PlayerMovementHitKind::world);
  } else {
    CHECK_FALSE(result); CHECK_FALSE(result.state);
  }
  CHECK(player::local_player_movement_state_signature(*before.state) == signature);
}

TEST_CASE("E32 lift start stop reversal and evidence limits keep source-time carry",
    "[e32][e32-lift][d4-lift][movement][prediction][brush][no-stock]") {
  const auto library = collision::build_brush_collision_model_library(
      hlclient::tests::vertical_lift::package()); REQUIRE(library);
  const auto environment = hlclient::tests::local_movement::make_environment();
  const movement::GoldSrcLocalMovementConfig config;
  movement::GoldSrcLocalMovementScratch scratch;
  auto previous = lift_frame(library.library, 1.0, 50.0, 1U, 2U);
  std::uint64_t record = 1U;
  // Each listed pair supplies actual authored authoritative observations.
  // An unannounced future stop is deliberately not asserted to be predictable.
  for (const auto phase : std::array<std::array<double, 3>, 5>{{
      {1.1, 50.0, 0.0}, {1.2, 60.0, 100.0}, {1.3, 60.0, 0.0},
      {1.4, 50.0, -100.0}, {1.475, 42.5, -100.0}}}) {
    CAPTURE(phase);
    const auto current = lift_frame(library.library, phase[0], phase[1], ++record, 2U);
    goldsrc::ReferenceVerticalSupportMotion support{previous, current}; REQUIRE(support.active());
    const player::PlayerMovementHitIdentity hit{player::PlayerMovementHitKind::brush_entity, 1U, 42U, 42U};
    REQUIRE(support.velocity_z_for(hit)); CHECK(*support.velocity_z_for(hit) == Catch::Approx(phase[2]).margin(1e-8));
    CHECK_FALSE(support.velocity_z_for({player::PlayerMovementHitKind::world, 0U, {}, {}}));
    CHECK_FALSE(support.velocity_z_for({player::PlayerMovementHitKind::brush_entity, 1U, 77U, 77U}));
    const auto state = timed_seed(current, scratch, config, phase[1]); REQUIRE(state);
    goldsrc::GoldSrcWireUserCmd wire; wire.msec = 20U;
    const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(8U), wire); REQUIRE(command);
    const movement::BrushSceneMovementCollision provider{current.scene, current.boxes};
    const auto moved = goldsrc::simulate_reference_movement(*state, *command.state,
        environment, provider, scratch, config, &support); REQUIRE(moved);
    CHECK(moved.state->origin().z == Catch::Approx(phase[1] + 36.0 + phase[2] * .020).margin(.001));
    CHECK(moved.state->velocity().z == 0.0F);
    CHECK(state->origin().z == static_cast<float>(phase[1] + 36.0));
    previous = current;
  }
  for (const auto profile : std::array<std::array<double, 3>, 4>{{
      {.25, 256.0, 1.0}, {.2501, 100.0, 0.0}, {.1, 256.125, 0.0}, {.0005, 100.0, 0.0}}}) {
    CAPTURE(profile);
    const auto old = lift_frame(library.library, 2.0, 50.0, 100U, 2U);
    const auto current = lift_frame(library.library, 2.0 + profile[0],
        50.0 + profile[0] * profile[1], 101U, 2U);
    goldsrc::ReferenceVerticalSupportMotion support{old, current};
    CHECK(support.active() == (profile[2] == 1.0));
    if (!support.active()) CHECK(support.sample(3.0) == current.scene);
  }
}

TEST_CASE("E32 lift blocked by another entity fails atomically and recovers with fresh membership",
    "[e32][e32-lift][d4-lift][movement][prediction][brush][server-box][diagnostics][no-stock]") {
  const auto library = collision::build_brush_collision_model_library(
      hlclient::tests::vertical_lift::package()); REQUIRE(library);
  const auto previous = lift_frame(library.library, 1.0, 77.0, 1U, 2U);
  auto current = lift_frame(library.library, 1.1, 87.0, 2U, 2U);
  // Authored server BBOX ceiling. Standing hull at z123 is on its free lower
  // boundary; a genuine +2-unit carry would place it inside the other entity.
  current.boxes.front().bounds = {{-4.0F, -4.0F, 159.0F}, {4.0F, 4.0F, 167.0F}};
  goldsrc::ReferenceVerticalSupportMotion support{previous, current}; REQUIRE(support.active());
  movement::GoldSrcLocalMovementScratch scratch;
  const movement::GoldSrcLocalMovementConfig config;
  const auto seed = timed_seed(current, scratch, config, 87.0); REQUIRE(seed);
  const movement::BrushSceneMovementCollision provider{current.scene, current.boxes};
  goldsrc::GoldSrcWireUserCmd wire; wire.msec = 20U;
  const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(
      *goldsrc::GoldSrcUserCmdSequence::create(8U), wire); REQUIRE(command);
  const auto blocked = goldsrc::simulate_reference_movement(*seed, *command.state,
      hlclient::tests::local_movement::make_environment(), provider, scratch, config, &support);
  REQUIRE_FALSE(blocked); CHECK(seed->origin().z == 123.0F);
  REQUIRE(scratch.last_reference_collision_position);
  CHECK(scratch.last_reference_collision_position->status == movement::LocalMovementPositionStatus::blocking);
  REQUIRE(scratch.last_reference_collision_position->hit);
  CHECK(scratch.last_reference_collision_position->hit->kind == player::PlayerMovementHitKind::server_entity_box);
  CHECK(scratch.last_reference_collision_position->hit->source_entity_index == 2U);
  CHECK(scratch.last_reference_collision_hull == player::PlayerMovementHull::standing);
  auto updated = lift_frame(library.library, 1.1, 87.0, 3U, 2U, 128.0F);
  REQUIRE(support.refresh_same_time_context(updated));
  const movement::BrushSceneMovementCollision fresh{updated.scene, updated.boxes};
  const auto recovered = goldsrc::simulate_reference_movement(*seed, *command.state,
      hlclient::tests::local_movement::make_environment(), fresh, scratch, config, &support);
  REQUIRE(recovered); CHECK(recovered.state->origin().z == Catch::Approx(125.0).margin(.001));
  CHECK_FALSE(scratch.last_reference_collision_position);
  CHECK_FALSE(scratch.last_reference_collision_trace);
}

TEST_CASE("E32 walking on a rising lift detaches at its edge and lands on stationary world",
    "[e32][e32-lift][d4-lift][movement][prediction][brush][no-stock]") {
  const auto library = collision::build_brush_collision_model_library(
      hlclient::tests::vertical_lift::package()); REQUIRE(library);
  auto previous = lift_frame(library.library, 1.0, 50.0, 1U, 2U);
  auto current = lift_frame(library.library, 1.1, 60.0, 2U, 2U);
  goldsrc::ReferenceVerticalSupportMotion support{previous, current}; REQUIRE(support.active());
  movement::GoldSrcLocalMovementScratch scratch;
  const movement::GoldSrcLocalMovementConfig config;
  const auto initial = timed_seed(current, scratch, config, 60.0); REQUIRE(initial);
  std::optional<player::LocalPlayerMovementState> state{*initial};
  const auto environment = hlclient::tests::local_movement::make_environment();
  const movement::BrushSceneMovementCollision provider{current.scene, current.boxes};
  bool walked_on_support = false, detached = false, landed = false;
  for (unsigned index = 1U; index <= 100U; ++index) {
    goldsrc::GoldSrcWireUserCmd wire; wire.msec = 20U; wire.forward = 100;
    const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(7U + index), wire); REQUIRE(command);
    const auto moved = goldsrc::simulate_reference_movement(*state, *command.state,
        environment, provider, scratch, config, &support);
    INFO((moved.error ? moved.error->context : "ready")); REQUIRE(moved);
    if (moved.state->ground_state().hit() &&
        moved.state->ground_state().hit()->kind == player::PlayerMovementHitKind::brush_entity) {
      CHECK_FALSE(detached);
      CHECK(moved.state->origin().z == Catch::Approx(96.0 + index * 2.0).margin(.001));
      CHECK(moved.state->velocity().z == 0.0F);
      walked_on_support = moved.state->origin().x > .1F;
    }
    if (!moved.state->ground_state().grounded()) detached = true;
    if (moved.state->ground_state().hit() &&
        moved.state->ground_state().hit()->kind == player::PlayerMovementHitKind::world) {
      landed = true; CHECK(detached);
      CHECK(moved.state->origin().z == Catch::Approx(-92.0).margin(.05));
      CHECK(moved.state->velocity().z == 0.0F);
    }
    state.emplace(*moved.state);
    if (index % 4U == 0U) {
      previous = current;
      current = lift_frame(library.library, 1.1 + index * .020, 60.0 + index * 2.0, 2U + index, 2U);
      support = goldsrc::ReferenceVerticalSupportMotion{previous, current}; REQUIRE(support.active());
    }
  }
  CHECK(walked_on_support); CHECK(detached); CHECK(landed);
  REQUIRE(state->ground_state().hit());
  CHECK(state->ground_state().hit()->kind == player::PlayerMovementHitKind::world);
  CHECK(state->origin().z < 0.0F); // No sticky carry from the former ascending support.
}

TEST_CASE("E32 rising top contact survives a coplanar world-ground handoff",
    "[e32][e32-lift][lift-handoff-contact][movement][prediction][no-stock]") {
  const auto library = collision::build_brush_collision_model_library(
      hlclient::tests::lift_top_e32_1::package()); REQUIRE(library);
  const auto environment = hlclient::tests::local_movement::make_environment();
  const movement::GoldSrcLocalMovementConfig config;
  for (const auto hull : {player::PlayerMovementHull::standing, player::PlayerMovementHull::ducked})
  for (const double speed : {92.0, 192.0, 207.23809523809524})
  for (const std::uint8_t msec : std::array<std::uint8_t, 2>{20U, 50U}) {
    CAPTURE(hull, speed, msec);
    const auto previous = lift_frame(library.library, 1.0, 70.0 - speed * .1, 1U, 2U);
    const auto current = lift_frame(library.library, 1.1, 70.0, 2U, 2U);
    goldsrc::ReferenceVerticalSupportMotion support{previous, current}; REQUIRE(support.active());
    movement::GoldSrcLocalMovementScratch scratch;
    const movement::BrushSceneMovementCollision provider{current.scene, current.boxes};
    auto source = lift_seed(70.0);
    source.origin.x = 52.0;
    if (hull == player::PlayerMovementHull::ducked) {
      source.origin.z = 88.0; source.view_offset.z = 12.0; source.use_hull = 1U;
      source.flags |= (1U << 14U);
    }
    const auto derived = goldsrc::derive_reference_prediction_ground(source, {}, provider, scratch, config,
        player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    REQUIRE(derived.state);
    auto info = player::local_player_movement_state_create_info(*derived.state);
    info.simulation_time_nanoseconds = 1'100'000'000U;
    const auto initial = player::LocalPlayerMovementState::create(info); REQUIRE(initial);
    REQUIRE(initial.state->ground_state().hit());
    REQUIRE(initial.state->ground_state().hit()->kind == player::PlayerMovementHitKind::world);
    goldsrc::GoldSrcWireUserCmd wire; wire.msec = msec; wire.forward = 100;
    if (hull == player::PlayerMovementHull::ducked) wire.buttons = goldsrc::kReferenceGoldSrcButtonDuck;
    const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(8U), wire); REQUIRE(command);
    // The previous path selected the tied world plane, applied zero carry,
    // then rejected this otherwise valid command when the rising top arrived.
    const auto without_motion = goldsrc::simulate_reference_movement(*initial.state, *command.state,
        environment, provider, scratch, config); REQUIRE(without_motion);
    REQUIRE(without_motion.state->ground_state().hit()->kind == player::PlayerMovementHitKind::world);
    const movement::BrushSceneMovementCollision endpoint{support.sample(1.1 + msec * .001), current.boxes};
    const auto overlap = endpoint.test_position(without_motion.state->origin(), hull, scratch.collision);
    REQUIRE(overlap); REQUIRE(overlap.result->status == movement::LocalMovementPositionStatus::blocking);
    REQUIRE(overlap.result->hit); CHECK(overlap.result->hit->source_entity_index == 42U);
    const auto moved = goldsrc::simulate_reference_movement(*initial.state, *command.state,
        environment, provider, scratch, config, &support); REQUIRE(moved);
    CHECK(moved.state->origin().x == without_motion.state->origin().x);
    CHECK(moved.state->origin().y == without_motion.state->origin().y);
    CHECK(moved.state->origin().z == Catch::Approx(info.origin.z + speed * msec * .001).margin(.001));
    CHECK(moved.state->velocity().z == 0.0F);
    REQUIRE(moved.state->ground_state().hit());
    CHECK(moved.state->ground_state().hit()->source_entity_index == 42U);
    const auto clear = endpoint.test_position(moved.state->origin(), hull, scratch.collision);
    REQUIRE(clear); CHECK(clear.result->status == movement::LocalMovementPositionStatus::free);
    CHECK_FALSE(scratch.last_reference_collision_position);
    const auto repeated = goldsrc::simulate_reference_movement(*initial.state, *command.state,
        environment, provider, scratch, config, &support); REQUIRE(repeated);
    CHECK(repeated.deterministic_state_signature == moved.deterministic_state_signature);

    const auto identity = prediction::create_prediction_session_identity(1U, 1U, provider,
        environment, config, *initial.state,
        prediction::PredictionCompatibilityProfile::reference_carrier_jump_duck_weapon_use_v4,
        prediction::PredictionAcknowledgementProfile::reference_sent_carrier_boundary_v1); REQUIRE(identity);
    auto history = prediction::LocalPredictionHistoryState::create_initial(*initial.state, *identity.session).history;
    REQUIRE(history);
    const prediction::PredictedCommandAppend append{std::make_shared<const goldsrc::GoldSrcUserCmdState>(*command.state),
        history->current_predicted_state(), std::make_shared<const player::LocalPlayerMovementState>(*moved.state),
        moved.statistics, prediction::summarize_prediction_touches(moved.touches, false, false)};
    const auto published = prediction::append_local_prediction_commands(*history, std::span{&append, 1U}); REQUIRE(published);
    // A fresh stopped authority replaces derived motion immediately. No
    // retained lift velocity or second push may survive the canonical rebase.
    const auto stopped = lift_frame(library.library, 1.1 + msec * .001, 70.0, 3U, 2U);
    goldsrc::ReferenceVerticalSupportMotion stopped_motion{current, stopped}; REQUIRE(stopped_motion.active());
    auto authoritative = player::local_player_movement_state_create_info(*without_motion.state);
    const auto corrected = player::LocalPlayerMovementState::create(authoritative); REQUIRE(corrected);
    const movement::BrushSceneMovementCollision stopped_collision{stopped.scene, stopped.boxes};
    const auto rebased = goldsrc::rebase_reference_prediction(*published.history, *corrected.state,
        environment, stopped_collision, scratch, config, &stopped_motion);
    REQUIRE(rebased.history); CHECK(rebased.replayed_commands == 0U);
    CHECK(rebased.history->current_predicted_state()->origin().z == info.origin.z);
    const auto next = goldsrc::reference_jump_duck_weapon_use_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(9U), wire); REQUIRE(next);
    const auto after_stop = goldsrc::simulate_reference_movement(*rebased.history->current_predicted_state(),
        *next.state, environment, stopped_collision, scratch, config, &stopped_motion); REQUIRE(after_stop);
    CHECK(after_stop.state->origin().z == info.origin.z);
    CHECK(after_stop.state->ground_state().hit()->kind == player::PlayerMovementHitKind::world);
  }
}

TEST_CASE("E32 rising handoff never overrides a blocked carry or detached world ground",
    "[e32][e32-lift][lift-handoff-contact][movement][prediction][no-stock]") {
  bool ceiling = false, actor = false, detached = false, stopped = false, descending = false;
  bool deep = false, too_fast = false, below = false;
  SECTION("ceiling retains the real blockage") { ceiling = true; }
  SECTION("second actor retains the real blockage") { actor = true; }
  SECTION("detached landing receives no sticky carry") { detached = true; }
  SECTION("stopped support receives no carry") { stopped = true; }
  SECTION("descending support cannot lift a world-grounded player") { descending = true; }
  SECTION("already penetrating a side or deep top is not new contact") { deep = true; }
  SECTION("unsupported speed is not invented as contact motion") { too_fast = true; }
  SECTION("lower rising top does not reach world floor") { below = true; }
  const auto library = collision::build_brush_collision_model_library(
      hlclient::tests::lift_top_e32_1::package(false, false, ceiling)); REQUIRE(library);
  const double speed = stopped ? 0.0 : descending ? -200.0 : too_fast ? 300.0 : 200.0;
  const double height = below ? 65.0 : 70.0;
  const auto previous = lift_frame(library.library, 1.0, height - speed * .1, 1U, 2U);
  auto current = lift_frame(library.library, 1.1, height + (deep ? 1.0 : 0.0), 2U, 2U);
  if (actor) current.boxes.push_back({99U, {{40.0F, -16.0F, 143.0F}, {80.0F, 16.0F, 160.0F}}});
  goldsrc::ReferenceVerticalSupportMotion support{previous, current};
  movement::GoldSrcLocalMovementScratch scratch;
  const movement::GoldSrcLocalMovementConfig config;
  // Establish the seed against its genuinely free baseline; the deep-start
  // control changes the collision observation without silently moving it.
  const auto baseline = lift_frame(library.library, 1.1, 70.0, 2U, 2U);
  const movement::BrushSceneMovementCollision baseline_collision{baseline.scene, baseline.boxes};
  auto source = lift_seed(70.0); source.origin.x = detached ? 84.0 : 52.0;
  const auto derived = goldsrc::derive_reference_prediction_ground(source, {}, baseline_collision, scratch, config,
      player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2); REQUIRE(derived.state);
  auto info = player::local_player_movement_state_create_info(*derived.state);
  info.simulation_time_nanoseconds = 1'100'000'000U;
  const auto initial = player::LocalPlayerMovementState::create(info); REQUIRE(initial);
  goldsrc::GoldSrcWireUserCmd wire; wire.msec = 20U; wire.forward = 100;
  const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(
      *goldsrc::GoldSrcUserCmdSequence::create(8U), wire); REQUIRE(command);
  const movement::BrushSceneMovementCollision provider{current.scene, current.boxes};
  const auto moved = goldsrc::simulate_reference_movement(*initial.state, *command.state,
      hlclient::tests::local_movement::make_environment(), provider, scratch, config, &support);
  if (ceiling || actor || deep) {
    CHECK_FALSE(moved);
    CHECK(initial.state->origin().z == 106.0F);
  } else {
    REQUIRE(moved); CHECK(moved.state->origin().z == 106.0F);
    CHECK(moved.state->ground_state().hit()->kind == player::PlayerMovementHitKind::world);
    CHECK(moved.state->origin().x > initial.state->origin().x);
  }
}

TEST_CASE("E32 mid-interval source correction reports raw error and carries only replay suffix",
    "[e32][e32-lift][d4-lift][movement][prediction][brush][reconciliation][no-stock]") {
  const auto library = collision::build_brush_collision_model_library(
      hlclient::tests::vertical_lift::package()); REQUIRE(library);
  const auto old = lift_frame(library.library, 1.0, 50.0, 1U, 2U);
  const auto current = lift_frame(library.library, 1.1, 60.0, 2U, 2U);
  goldsrc::ReferenceVerticalSupportMotion support{old, current}; REQUIRE(support.active());
  const auto environment = hlclient::tests::local_movement::make_environment();
  const movement::GoldSrcLocalMovementConfig config;
  movement::GoldSrcLocalMovementScratch scratch;
  const movement::BrushSceneMovementCollision provider{current.scene, current.boxes};
  const auto seed = timed_seed(current, scratch, config, 60.0); REQUIRE(seed);
  const auto identity = prediction::create_prediction_session_identity(1U, 1U, provider,
      environment, config, *seed, prediction::PredictionCompatibilityProfile::reference_carrier_jump_duck_weapon_use_v4,
      prediction::PredictionAcknowledgementProfile::reference_sent_carrier_boundary_v1); REQUIRE(identity);
  auto history = prediction::LocalPredictionHistoryState::create_initial(*seed, *identity.session).history; REQUIRE(history);
  for (const unsigned sequence : {8U, 9U}) {
    goldsrc::GoldSrcWireUserCmd wire; wire.msec = 20U;
    const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(sequence), wire); REQUIRE(command);
    const auto before = history->current_predicted_state();
    const auto moved = goldsrc::simulate_reference_movement(*before, *command.state,
        environment, provider, scratch, config, &support); REQUIRE(moved);
    const auto after = std::make_shared<const player::LocalPlayerMovementState>(*moved.state);
    const prediction::PredictedCommandAppend append{std::make_shared<const goldsrc::GoldSrcUserCmdState>(*command.state),
        before, after, moved.statistics, prediction::summarize_prediction_touches(moved.touches, false, false)};
    const auto next = prediction::append_local_prediction_commands(*history, std::span{&append, 1U}); REQUIRE(next);
    history = next.history;
  }
  CHECK(history->current_predicted_state()->origin().z == Catch::Approx(100.0).margin(.001));
  const auto updated = lift_frame(library.library, 1.11, 61.0, 3U, 2U);
  goldsrc::ReferenceVerticalSupportMotion next_support{current, updated}; REQUIRE(next_support.active());
  const movement::BrushSceneMovementCollision updated_provider{updated.scene, updated.boxes};
  const auto correction = timed_seed(updated, scratch, config, 61.0, 8U); REQUIRE(correction);
  const auto replay = goldsrc::rebase_reference_prediction(*history, *correction,
      environment, updated_provider, scratch, config, &next_support);
  REQUIRE(replay.history); REQUIRE(replay.raw_position_error);
  CHECK(*replay.raw_position_error == Catch::Approx(1.0).margin(.001));
  CHECK(replay.replayed_commands == 1U);
  CHECK(replay.history->current_predicted_state()->origin().z == Catch::Approx(99.0).margin(.001));
  CHECK(replay.history->current_predicted_state()->simulation_time_nanoseconds() == 1'130'000'000ULL);
  CHECK(correction->origin().z == 97.0F);
  const auto repeated = goldsrc::rebase_reference_prediction(*replay.history, *correction,
      environment, updated_provider, scratch, config, &next_support);
  REQUIRE(repeated.history); REQUIRE(repeated.raw_position_error);
  CHECK(*repeated.raw_position_error <= .001);
  CHECK(repeated.history->current_predicted_state()->origin().z == Catch::Approx(99.0).margin(.001));
}

TEST_CASE("E32 lift seed distinguishes observed zero nonzero and unknown basevelocity",
    "[e32][e32-lift][d4-lift][prediction-seed][no-stock]") {
  client::RuntimeClientObservationState observed;
  observed.generation = 1U;
  const client::RuntimeObservationSource source{2U, 2U, 40U, 0U, 8U, 7U, false, false};
  observed.client_metadata = {1U, client::RuntimeObservationFreshness::observed_in_record,
      client::RuntimeObservationCompleteness::complete_reconstruction, source};
  observed.entity_metadata = observed.client_metadata;
  client::RuntimeReceivingClientObservation receiving;
  receiving.origin = {0.0, 0.0, 96.0}; receiving.velocity = {0.0, 0.0, 0.0};
  receiving.view_offset = {0.0, 0.0, 28.0}; receiving.flags = 1U << 9U;
  receiving.water_level = 0U; receiving.dead_flag = 0U; receiving.in_duck = false;
  observed.receiving_client = receiving;
  client::RuntimePacketEntityObservation entity;
  entity.entity_number = 1U; entity.player_movement_schema = true; entity.move_type = 3U;
  entity.use_hull = 0U; entity.gravity_multiplier = 1.0; entity.friction_multiplier = 1.0;
  entity.base_velocity = {0.0, 0.0, 0.0}; entity.spectator = false;
  observed.packet_entities.push_back(entity);
  goldsrc::ReferencePredictionAnchorResult anchor;
  anchor.status = goldsrc::ReferencePredictionAnchorStatus::bound;
  anchor.last_new_command = *goldsrc::GoldSrcUserCmdSequence::create(7U);
  anchor.last_new_value = goldsrc::GoldSrcWireUserCmd{};
  anchor.generation = 1U; anchor.source_record_identity = 2U; anchor.source_sequence = 40U;
  auto status = goldsrc::ReferencePredictionSeedStatus::ready;
  SECTION("explicit observed zero is supported") {}
  SECTION("nonzero vertical contribution remains explicit unsupported") {
    observed.packet_entities.front().base_velocity.z = 100.0;
    status = goldsrc::ReferencePredictionSeedStatus::unsupported_context;
  }
  SECTION("nonzero horizontal contribution remains explicit unsupported") {
    observed.packet_entities.front().base_velocity.x = 100.0;
    status = goldsrc::ReferencePredictionSeedStatus::unsupported_context;
  }
  SECTION("absent vertical component is not observed zero") {
    observed.packet_entities.front().base_velocity.z.reset();
    status = goldsrc::ReferencePredictionSeedStatus::missing_semantic_field;
  }
  const auto before = observed.packet_entities.front().base_velocity;
  const auto seed = goldsrc::inspect_reference_prediction_seed(observed, 0U, anchor);
  CHECK(seed.status == status);
  CHECK(observed.packet_entities.front().base_velocity == before);
  if (status != goldsrc::ReferencePredictionSeedStatus::ready) {
    CHECK_FALSE(seed.seed); CHECK(seed.field == goldsrc::ReferencePredictionSeedField::base_velocity);
  } else {
    REQUIRE(seed.seed); CHECK(seed.seed->base_velocity == before);
    const auto library = collision::build_brush_collision_model_library(
        hlclient::tests::vertical_lift::package()); REQUIRE(library);
    const auto frame = lift_frame(library.library, 1.1, 60.0, 2U, 2U);
    const movement::BrushSceneMovementCollision provider{frame.scene, frame.boxes};
    movement::GoldSrcLocalMovementScratch scratch;
    const auto grounded = goldsrc::derive_reference_prediction_ground(*seed.seed, {},
        provider, scratch, {}, player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2);
    REQUIRE(grounded.state); CHECK(grounded.state->origin().z == 96.0F);
    REQUIRE(grounded.state->ground_state().hit());
    CHECK(grounded.state->ground_state().hit()->source_entity_index == 42U);
  }
}

TEST_CASE("E32 matched lift replay workloads record diagnostic On Off CPU cost and correctness",
    "[e32][e32-lift][e32-benchmark][benchmark][movement][prediction][brush][no-stock]") {
  namespace app = hlclient::app;
  using Clock = std::chrono::steady_clock;
  const auto library = collision::build_brush_collision_model_library(
      hlclient::tests::vertical_lift::package()); REQUIRE(library);
  const auto environment = hlclient::tests::local_movement::make_environment();
  const movement::GoldSrcLocalMovementConfig config;
  constexpr unsigned warmup_commands = 32U, measured_commands = 128U;
  const auto microseconds = [](const auto start, const auto end) {
    return std::chrono::duration<double, std::micro>{end - start}.count();
  };
  const auto quantile = [](std::vector<double> values, const double fraction) {
    REQUIRE_FALSE(values.empty()); std::sort(values.begin(), values.end());
    const auto rank = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(values.size())));
    return values[std::clamp(rank, std::size_t{1U}, values.size()) - 1U];
  };
  for (const unsigned actors : {2U, 8U, 16U, 32U}) {
    std::array<std::optional<player::LocalPlayerMovementState>, 2> endpoints;
    // Swap order between scene sizes. Both paths use identical source records,
    // commands, samples and warm-up; only recorder enablement differs.
    for (unsigned order = 0U; order < 2U; ++order) {
      const unsigned mode = (actors == 8U || actors == 32U) ? 1U - order : order;
      CAPTURE(actors, mode);
      app::MovementIncidentRecorder recorder{mode == 1U};
      auto previous = lift_frame(library.library, 1.0, 50.0, 1U, actors);
      auto current = lift_frame(library.library, 1.1, 60.0, 2U, actors);
      goldsrc::ReferenceVerticalSupportMotion support{previous, current}; REQUIRE(support.active());
      movement::GoldSrcLocalMovementScratch scratch;
      const movement::BrushSceneMovementCollision provider{current.scene, current.boxes};
      const auto initial = timed_seed(current, scratch, config, 60.0); REQUIRE(initial);
      const auto identity = prediction::create_prediction_session_identity(1U, 1U, provider,
          environment, config, *initial, prediction::PredictionCompatibilityProfile::reference_carrier_jump_duck_weapon_use_v4,
          prediction::PredictionAcknowledgementProfile::reference_sent_carrier_boundary_v1); REQUIRE(identity);
      auto history = prediction::LocalPredictionHistoryState::create_initial(*initial, *identity.session).history; REQUIRE(history);
      std::uint64_t record = 2U, kernel_traces = 0U, replayed_commands = 0U;
      double maximum_raw_error = 0.0, maximum_camera_jump = 0.0;
      std::size_t scratch_high_water = 0U;
      std::vector<double> command_cpu, simulation_cpu, replay_cpu, collision_update_cpu;
      command_cpu.reserve(measured_commands); simulation_cpu.reserve(measured_commands);
      replay_cpu.reserve(measured_commands / 4U); collision_update_cpu.reserve(measured_commands / 4U);
      for (unsigned index = 1U; index <= warmup_commands + measured_commands; ++index) {
        const auto frame_begin = Clock::now();
        goldsrc::GoldSrcWireUserCmd wire; wire.msec = 20U;
        const auto sequence = *goldsrc::GoldSrcUserCmdSequence::create(7U + index);
        const auto command = goldsrc::reference_jump_duck_weapon_use_movement_command(sequence, wire); REQUIRE(command);
        const auto before = history->current_predicted_state();
        const auto simulation_begin = Clock::now();
        const auto moved = goldsrc::simulate_reference_movement(*before, *command.state,
            environment, provider, scratch, config, &support);
        const auto simulation_end = Clock::now(); REQUIRE(moved);
        const auto after = std::make_shared<const player::LocalPlayerMovementState>(*moved.state);
        const prediction::PredictedCommandAppend append{std::make_shared<const goldsrc::GoldSrcUserCmdState>(*command.state),
            before, after, moved.statistics, prediction::summarize_prediction_touches(moved.touches, false, false)};
        const auto next = prediction::append_local_prediction_commands(*history, std::span{&append, 1U}); REQUIRE(next);
        history = next.history;
        const double end_time = 1.1 + index * .020;
        const auto camera_before = goldsrc::sample_reference_support_presentation(
            *before, *after, .5, &support, scratch.collision);
        REQUIRE(camera_before.scene);
        std::optional<double> raw_error;
        std::optional<double> replay_cost;
        if (index % 4U == 0U) {
          const double source_time = end_time - .040;
          const double height = 50.0 + 100.0 * (source_time - 1.0);
          const auto collision_begin = Clock::now();
          auto updated = lift_frame(library.library, source_time, height, ++record, actors);
          goldsrc::ReferenceVerticalSupportMotion motion{current, updated};
          const movement::BrushSceneMovementCollision updated_provider{updated.scene, updated.boxes};
          const auto correction = timed_seed(updated, scratch, config, height, 7U + index - 2U);
          const auto collision_end = Clock::now(); REQUIRE(correction); REQUIRE(motion.active());
          const auto replay_begin = Clock::now();
          const auto replay = goldsrc::rebase_reference_prediction(*history, *correction,
              environment, updated_provider, scratch, config, &motion);
          const auto replay_end = Clock::now(); REQUIRE(replay.history); REQUIRE(replay.raw_position_error);
          CHECK(*replay.raw_position_error <= .001); CHECK(replay.replayed_commands == 2U);
          raw_error = replay.raw_position_error; replay_cost = microseconds(replay_begin, replay_end);
          maximum_raw_error = std::max(maximum_raw_error, *raw_error);
          replayed_commands += replay.replayed_commands;
          const auto* pair = replay.history->find_exact(sequence); REQUIRE(pair);
          const auto camera_after = goldsrc::sample_reference_support_presentation(*pair->pre_command_state(),
              *pair->post_command_state(), .5, &motion, scratch.collision);
          const double jump = std::hypot(static_cast<double>(camera_after.origin.x) - camera_before.origin.x,
              static_cast<double>(camera_after.origin.y) - camera_before.origin.y,
              static_cast<double>(camera_after.origin.z) - camera_before.origin.z);
          maximum_camera_jump = std::max(maximum_camera_jump, jump); CHECK(jump <= .001);
          history = replay.history; previous = current; current = updated; support = motion;
          if (index > warmup_commands) {
            replay_cpu.push_back(*replay_cost);
            collision_update_cpu.push_back(microseconds(collision_begin, collision_end));
          }
        }
        const auto value = history->current_predicted_state()->origin();
        CHECK(value.z == Catch::Approx(96.0 + index * 2.0).margin(.001));
        app::MovementFrameRecord observed;
        observed.monotonic_us = static_cast<std::uint64_t>(index) * 20'000U;
        observed.frame_number = index; observed.network_generation = 1U; observed.map_generation = 1U;
        observed.frame_interval_ms = 20.0; observed.prediction_active = true;
        observed.predicted_origin = app::MovementDiagnosticVector3{value.x, value.y, value.z};
        observed.presented_eye = app::MovementDiagnosticVector3{
            camera_before.origin.x, camera_before.origin.y, camera_before.origin.z + 28.0};
        observed.raw_correction_units = raw_error;
        observed.correction_ordinal = record;
        observed.replay_duration_ms = replay_cost ? std::optional<double>{*replay_cost * .001} : std::nullopt;
        static_cast<void>(recorder.observe(observed));
        const auto frame_end = Clock::now();
        kernel_traces += moved.statistics.trace_count;
        scratch_high_water = std::max(scratch_high_water,
            scratch.collision.retained_bytes() + scratch.direct_candidate_collision.retained_bytes() +
            scratch.step_candidate_collision.retained_bytes());
        if (index > warmup_commands) {
          command_cpu.push_back(microseconds(frame_begin, frame_end));
          simulation_cpu.push_back(microseconds(simulation_begin, simulation_end));
        }
      }
      endpoints[mode].emplace(*history->current_predicted_state());
      const auto statistics = recorder.statistics();
      CHECK(statistics.allocated_capacity_bytes <= app::MovementIncidentRecorder::maximum_capacity_bytes);
      CHECK(statistics.accepted_frames == (mode == 1U ? warmup_commands + measured_commands : 0U));
      CHECK(statistics.incidents == 0U);
      std::cout << "[e32-lift-performance] actors=" << actors << " diagnostics=" << (mode ? "on" : "off")
          << " scope=offline_command_harness_including_assertions counters_include_warmup=true"
          << " samples=" << measured_commands << " warmup_commands=" << warmup_commands
          << " command_cpu_p95_us=" << quantile(command_cpu, .95)
          << " command_cpu_p99_us=" << quantile(command_cpu, .99)
          << " simulation_p95_us=" << quantile(simulation_cpu, .95)
          << " replay_p95_us=" << quantile(replay_cpu, .95)
          << " collision_update_p95_us=" << quantile(collision_update_cpu, .95)
          << " kernel_traces=" << kernel_traces << " replayed_commands=" << replayed_commands
          << " raw_error_max=" << maximum_raw_error << " camera_jump_same_time_max=" << maximum_camera_jump
          << " fallback_count=0 scratch_high_water_bytes=" << scratch_high_water
          << " diagnostic_capacity_bytes=" << statistics.allocated_capacity_bytes
          << " network_commit_latency=not_measured studio_gpu=not_measured\n";
    }
    REQUIRE(endpoints[0]); REQUIRE(endpoints[1]);
    CHECK(player::local_player_movement_state_signature(*endpoints[0]) ==
        player::local_player_movement_state_signature(*endpoints[1]));
    CHECK(endpoints[0]->simulation_time_nanoseconds() == 4'300'000'000ULL);
    CHECK(endpoints[0]->origin().z == Catch::Approx(416.0).margin(.001));
  }
}
