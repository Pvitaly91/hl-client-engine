#include "local_movement_test_fixture.hpp"
#include "vertical_lift_test_fixture.hpp"
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <hlclient/goldsrc/reference_prediction_seed.hpp>
#include <hlclient/goldsrc/reference_prediction_reconciliation.hpp>
#include <hlclient/prediction/local_prediction.hpp>
#include <hlclient/prediction/prediction_state_comparison.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <span>

namespace {

namespace fixture = hlclient::tests::local_movement;
namespace goldsrc = hlclient::goldsrc;
namespace movement = hlclient::goldsrc::movement;
namespace player = hlclient::movement;

TEST_CASE("A grounded synthetic jump edge assigns the pinned Valve impulse",
    "[goldsrc][movement][kernel][jump]")
{
    fixture::DeterministicLocalMovementCollision collision;
    const auto result = fixture::simulate(
        fixture::make_state(),
        fixture::make_command(
            1U, 10U, 0.0F, 0.0F,
            goldsrc::kSyntheticGoldSrcButtonJump),
        collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.statistics.jump_count == 1U);
    CHECK(result.state->mode() == player::PlayerMovementMode::airborne);
    CHECK_FALSE(result.state->ground_state().grounded());
    CHECK(result.state->velocity().z == Catch::Approx(
        movement::kValveJumpImpulse - 8.0F));
    CHECK(result.state->origin().z == Catch::Approx(
        36.0F + (movement::kValveJumpImpulse - 4.0F) * 0.01F));
    CHECK((result.state->old_buttons() &
        goldsrc::kSyntheticGoldSrcButtonJump) != 0U);
}

TEST_CASE("Held jump does not retrigger until a release creates a new edge",
    "[goldsrc][movement][kernel][jump][edge]")
{
    fixture::DeterministicLocalMovementCollision collision;
    const auto held_initial = fixture::make_state(
        {0.0F, 0.0F, 36.0F}, {}, player::PlayerMovementMode::walking,
        player::PlayerMovementHull::standing, 1U,
        goldsrc::kSyntheticGoldSrcButtonJump);
    const auto held = fixture::simulate(
        held_initial,
        fixture::make_command(
            2U, 10U, 0.0F, 0.0F,
            goldsrc::kSyntheticGoldSrcButtonJump),
        collision);

    REQUIRE(held);
    REQUIRE(held.state);
    CHECK(held.statistics.jump_count == 0U);
    CHECK(held.state->mode() == player::PlayerMovementMode::walking);
    CHECK(held.state->velocity().z == 0.0F);

    const auto released = fixture::simulate(
        *held.state, fixture::make_command(3U), collision);
    REQUIRE(released);
    REQUIRE(released.state);
    CHECK(released.state->old_buttons() == 0U);

    const auto pressed_again = fixture::simulate(
        *released.state,
        fixture::make_command(
            4U, 10U, 0.0F, 0.0F,
            goldsrc::kSyntheticGoldSrcButtonJump),
        collision);
    REQUIRE(pressed_again);
    CHECK(pressed_again.statistics.jump_count == 1U);
}

TEST_CASE("A jump press while airborne records the button without retriggering",
    "[goldsrc][movement][kernel][jump][airborne]")
{
    fixture::DeterministicLocalMovementCollision collision;
    const auto result = fixture::simulate(
        fixture::make_state(
            {0.0F, 0.0F, 100.0F}, {},
            player::PlayerMovementMode::airborne),
        fixture::make_command(
            1U, 10U, 0.0F, 0.0F,
            goldsrc::kSyntheticGoldSrcButtonJump),
        collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.statistics.jump_count == 0U);
    CHECK(result.state->mode() == player::PlayerMovementMode::airborne);
    CHECK(result.state->velocity().z == Catch::Approx(-8.0F));
    CHECK(result.state->origin().z == Catch::Approx(99.96F));
    CHECK((result.state->old_buttons() &
        goldsrc::kSyntheticGoldSrcButtonJump) != 0U);
}

TEST_CASE("A grounded ducked player jumps without changing hull or center policy",
    "[goldsrc][movement][kernel][jump][ducked]")
{
    fixture::DeterministicLocalMovementCollision collision;
    const auto result = fixture::simulate(
        fixture::make_state(
            {0.0F, 0.0F, 18.0F}, {},
            player::PlayerMovementMode::walking,
            player::PlayerMovementHull::ducked),
        fixture::make_command(
            1U, 10U, 0.0F, 0.0F,
            goldsrc::kSyntheticGoldSrcButtonJump |
                goldsrc::kSyntheticGoldSrcButtonDuck),
        collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.statistics.jump_count == 1U);
    CHECK(result.state->mode() == player::PlayerMovementMode::airborne);
    CHECK(result.state->hull() == player::PlayerMovementHull::ducked);
    CHECK(result.state->view_offset().z == movement::kValveDuckViewOffsetZ);
    CHECK(result.state->origin().z == Catch::Approx(
        18.0F + (movement::kValveJumpImpulse - 4.0F) * 0.01F));
    CHECK(result.state->velocity().z == Catch::Approx(
        movement::kValveJumpImpulse - 8.0F));
}

TEST_CASE("A jump clips against a low ceiling and remains out of solid",
    "[goldsrc][movement][kernel][jump][ceiling]")
{
    fixture::DeterministicLocalMovementCollision collision;
    collision.add_ceiling(80.0F);
    const auto result = fixture::simulate(
        fixture::make_state(),
        fixture::make_command(
            1U, 100U, 0.0F, 0.0F,
            goldsrc::kSyntheticGoldSrcButtonJump),
        collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.statistics.jump_count == 1U);
    CHECK(result.statistics.start_solid_count == 0U);
    CHECK(result.statistics.all_solid_count == 0U);
    CHECK(result.state->mode() == player::PlayerMovementMode::airborne);
    CHECK(result.state->origin().z + movement::kValveStandingHullMaximumZ <=
        Catch::Approx(80.0F).margin(1.0e-4F));
    bool touched_ceiling = false;
    for (const auto& touch : result.touches) {
        touched_ceiling = touched_ceiling ||
            touch.plane.normal.z == -1.0F;
    }
    CHECK(touched_ceiling);
}

TEST_CASE("Upward velocity above the ground-snap cutoff stays airborne",
    "[goldsrc][movement][kernel][jump][ground-probe]")
{
    fixture::DeterministicLocalMovementCollision collision;
    const auto result = fixture::simulate(
        fixture::make_state(
            {0.0F, 0.0F, 36.0F}, {0.0F, 0.0F, 200.0F}),
        fixture::make_command(1U), collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->mode() == player::PlayerMovementMode::airborne);
    CHECK_FALSE(result.state->ground_state().grounded());
    CHECK(result.state->origin().z > 36.0F);
}

TEST_CASE("Authoritative crouch jump uses the configured boost once with exact duck timing",
    "[longjump][goldsrc][movement][kernel][jump]")
{
    fixture::DeterministicLocalMovementCollision collision;
    auto info = player::local_player_movement_state_create_info(
        fixture::make_state({0.0F, 0.0F, 36.0F}, {100.0F, 0.0F, 0.0F}));
    info.command_profile = player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2;
    info.compatibility_profile = player::GoldSrcMovementCompatibilityProfile::public_valve_pm_shared_dry_actions_subset_v2;
    info.crouch_jump_enabled = true;
    goldsrc::GoldSrcWireUserCmd wire;
    wire.msec = 10U;
    wire.buttons = goldsrc::kReferenceGoldSrcButtonDuck | goldsrc::kReferenceGoldSrcButtonJump;
    wire.angle_turns[1] = 16384U; // 90 degrees, independently known forward axis.
    movement::GoldSrcLocalMovementConfig config;
    config.crouch_jump = movement::ReferenceCrouchJumpPolicy{560.0F, std::sqrt(89600.0F), 50.0F};
    bool boost = true, jump = true;
    float expected_horizontal = 560.0F;
    SECTION("new duck edge and jump") {}
    SECTION("pitch participates in forward projection") {
        wire.angle_turns[0] = 8192U;
        expected_horizontal = 560.0F / std::sqrt(2.0F);
    }
    SECTION("completed duck retains live timer") {
        info.hull = player::PlayerMovementHull::ducked; info.origin.z = 18.0F;
        info.old_buttons = goldsrc::kSyntheticGoldSrcButtonDuck;
        info.duck_time_milliseconds = 11U;
    }
    SECTION("timer expires before jump") {
        info.hull = player::PlayerMovementHull::ducked; info.origin.z = 18.0F;
        info.old_buttons = goldsrc::kSyntheticGoldSrcButtonDuck;
        info.duck_time_milliseconds = 10U; boost = false;
    }
    SECTION("absent server ability") { info.crouch_jump_enabled = false; boost = false; }
    SECTION("no game selected boost") { config.crouch_jump.reset(); boost = false; }
    SECTION("no duck button") { wire.buttons = goldsrc::kReferenceGoldSrcButtonJump; boost = false; }
    SECTION("speed at strict threshold after half gravity") { info.velocity = {50.0F, 0.0F, 4.0F}; boost = false; }
    SECTION("full velocity speed above threshold") { info.velocity = {0.0F, 0.0F, 100.0F}; }
    SECTION("held jump cannot boost again") { info.old_buttons = goldsrc::kSyntheticGoldSrcButtonJump; boost = false; jump = false; }
    SECTION("airborne cannot boost") {
        info.origin.z = 150.0F; info.mode = player::PlayerMovementMode::airborne;
        info.ground = {}; boost = false; jump = false;
    }
    const auto initial = player::LocalPlayerMovementState::create(info); REQUIRE(initial);
    const auto command = goldsrc::reference_jump_duck_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(1U), wire); REQUIRE(command);
    movement::GoldSrcLocalMovementScratch scratch;
    const auto result = movement::GoldSrcLocalMovementKernel::simulate(*initial.state,
        *command.state, fixture::make_environment(), collision, scratch, config);
    REQUIRE(result); REQUIRE(result.state);
    CHECK(result.state->crouch_jump_enabled() == info.crouch_jump_enabled);
    CHECK(result.statistics.jump_count == (jump ? 1U : 0U));
    if (jump) {
        const auto expected_z = boost ? std::sqrt(89600.0F) : movement::kValveJumpImpulse;
        CHECK(result.state->velocity().z == Catch::Approx(expected_z - 8.0F));
        CHECK(result.state->origin().z == Catch::Approx(info.origin.z + (expected_z - 4.0F) * 0.01F));
    }
    if (boost) {
        CHECK(result.state->velocity().x == Catch::Approx(0.0F).margin(0.001F));
        CHECK(result.state->velocity().y == Catch::Approx(expected_horizontal));
        CHECK(result.state->origin().y == Catch::Approx(expected_horizontal * 0.01F));
    }
    // Immutable replay of the same seed/action is deterministic, and a held
    // next action preserves flight instead of creating another jump.
    const auto replay = movement::GoldSrcLocalMovementKernel::simulate(*initial.state,
        *command.state, fixture::make_environment(), collision, scratch, config);
    REQUIRE(replay);
    CHECK(replay.deterministic_state_signature == result.deterministic_state_signature);
    const auto next = goldsrc::reference_jump_duck_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(2U), wire); REQUIRE(next);
    const auto held = movement::GoldSrcLocalMovementKernel::simulate(*result.state,
        *next.state, fixture::make_environment(), collision, scratch, config);
    REQUIRE(held); CHECK(held.statistics.jump_count == 0U);
}

TEST_CASE("Crouch jump capability participates in immutable state identity",
    "[longjump][movement][prediction]")
{
    auto info = player::local_player_movement_state_create_info(fixture::make_state());
    const auto absent = player::LocalPlayerMovementState::create(info); REQUIRE(absent);
    info.crouch_jump_enabled = true;
    const auto enabled = player::LocalPlayerMovementState::create(info); REQUIRE(enabled);
    CHECK(player::local_player_movement_state_signature(*absent.state) !=
        player::local_player_movement_state_signature(*enabled.state));
    const auto comparison = hlclient::prediction::compare_prediction_states(*absent.state, *enabled.state);
    REQUIRE(comparison); CHECK_FALSE(comparison.metrics->exact_physical_state_match);
    const auto copied = player::LocalPlayerMovementState::create(
        player::local_player_movement_state_create_info(*enabled.state)); REQUIRE(copied);
    CHECK(copied.state->crouch_jump_enabled());
    CHECK(player::local_player_movement_state_signature(*copied.state) ==
        player::local_player_movement_state_signature(*enabled.state));
}

TEST_CASE("Longjump BSP seed and authoritative ACK replay preserve the boost exactly once",
    "[longjump][core-only][prediction][reference-ground][replay]")
{
    namespace pred = hlclient::prediction;
    const movement::WorldOnlyMovementCollision collision{hlclient::tests::vertical_lift::package()};
    const auto environment = fixture::make_environment();
    movement::GoldSrcLocalMovementScratch scratch;
    movement::GoldSrcLocalMovementConfig config;
    config.crouch_jump = movement::ReferenceCrouchJumpPolicy{560.0F, std::sqrt(89600.0F), 50.0F};
    goldsrc::ReferencePredictionSeed seed;
    seed.generation = 1U; seed.record_identity = 10U;
    seed.command_boundary = *goldsrc::GoldSrcUserCmdSequence::create(7U);
    seed.origin = {0.0, 0.0, -92.0}; // Authored point floor -128 + standing halfheight36.
    seed.velocity = {100.0, 0.0, 0.0}; seed.view_offset = {0.0, 0.0, 28.0};
    seed.base_velocity = {0.0, 0.0, 0.0}; seed.flags = 1U << 9U; seed.move_type = 3U;
    seed.gravity_multiplier = seed.friction_multiplier = 1.0;
    seed.crouch_jump_enabled = true;
    constexpr auto profile = player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2;
    const auto initial = goldsrc::derive_reference_prediction_ground(seed, {}, collision, scratch, config, profile);
    REQUIRE(initial.state); REQUIRE(initial.state->crouch_jump_enabled());
    const auto session = pred::create_prediction_session_identity(1U, 1U, collision, environment,
        config, *initial.state, pred::PredictionCompatibilityProfile::reference_carrier_jump_duck_weapon_use_v4,
        pred::PredictionAcknowledgementProfile::reference_sent_carrier_boundary_v1);
    REQUIRE(session);
    auto history = pred::LocalPredictionHistoryState::create_initial(*initial.state, *session.session).history;
    REQUIRE(history);
    goldsrc::GoldSrcWireUserCmd wire;
    wire.msec = 20U; wire.buttons = goldsrc::kReferenceGoldSrcButtonDuck | goldsrc::kReferenceGoldSrcButtonJump;
    const auto append = [&](const goldsrc::GoldSrcUserCmdState& command,
            const movement::LocalMovementSimulationResult& step) {
        const pred::PredictedCommandAppend row{std::make_shared<const goldsrc::GoldSrcUserCmdState>(command),
            history->current_predicted_state(), std::make_shared<const player::LocalPlayerMovementState>(*step.state),
            step.statistics, pred::summarize_prediction_touches(step.touches, false, false)};
        const auto appended = pred::append_local_prediction_commands(*history, std::span{&row, 1U});
        REQUIRE(appended); history = appended.history;
    };
    const auto command = goldsrc::reference_jump_duck_secondary_weapon_use_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(8U), wire); REQUIRE(command);
    const auto jumped = goldsrc::simulate_reference_movement(*initial.state, *command.state,
        environment, collision, scratch, config); REQUIRE(jumped);
    CHECK(jumped.statistics.jump_count == 1U);
    CHECK(jumped.state->velocity().x == Catch::Approx(560.0F));
    CHECK(jumped.state->origin().x == Catch::Approx(11.2F));
    append(*command.state, jumped);

    // The server endpoint is independently reconstructed as an airborne seed.
    seed.record_identity = 11U; seed.command_boundary = command.state->command_sequence();
    seed.origin = {jumped.state->origin().x, jumped.state->origin().y, jumped.state->origin().z};
    seed.velocity = {jumped.state->velocity().x, jumped.state->velocity().y, jumped.state->velocity().z};
    seed.flags = 0U; seed.in_duck = jumped.state->in_duck_transition();
    seed.duck_time_milliseconds = jumped.state->duck_time_milliseconds();
    seed.old_buttons = jumped.state->old_buttons();
    const auto canonical = goldsrc::derive_reference_prediction_ground(seed, wire, collision, scratch, config, profile);
    REQUIRE(canonical.state); CHECK(canonical.state->crouch_jump_enabled());
    auto timed = player::local_player_movement_state_create_info(*canonical.state);
    timed.simulation_time_nanoseconds = jumped.state->simulation_time_nanoseconds();
    timed.state_revision = jumped.state->state_revision();
    const auto correction = player::LocalPlayerMovementState::create(timed); REQUIRE(correction);
    const auto next_command = goldsrc::reference_jump_duck_secondary_weapon_use_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(9U), wire); REQUIRE(next_command);
    const auto next = goldsrc::simulate_reference_movement(*jumped.state, *next_command.state,
        environment, collision, scratch, config); REQUIRE(next);
    CHECK(next.statistics.jump_count == 0U); append(*next_command.state, next);
    const auto rebased = goldsrc::rebase_reference_prediction(*history, *correction.state,
        environment, collision, scratch, config);
    REQUIRE(rebased.history); CHECK(rebased.replayed_commands == 1U);
    CHECK(rebased.raw_position_error == 0.0);
    CHECK(player::local_player_movement_state_signature(*rebased.history->current_predicted_state()) ==
        player::local_player_movement_state_signature(*next.state));
    CHECK(rebased.history->current_predicted_state()->velocity().x == Catch::Approx(560.0F));
    CHECK(rebased.history->current_predicted_state()->crouch_jump_enabled());
}

} // namespace
