#include "local_movement_test_fixture.hpp"
#include <hlclient/goldsrc/reference_prediction_command.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

namespace fixture = hlclient::tests::local_movement;
namespace goldsrc = hlclient::goldsrc;
namespace movement = hlclient::goldsrc::movement;
namespace player = hlclient::movement;

TEST_CASE("Grounded duck and stand transitions preserve the hull foot",
    "[goldsrc][movement][kernel][duck]")
{
    fixture::DeterministicLocalMovementCollision collision;
    const auto ducked = fixture::simulate(
        fixture::make_state(),
        fixture::make_command(
            1U, 10U, 0.0F, 0.0F,
            goldsrc::kSyntheticGoldSrcButtonDuck),
        collision);

    REQUIRE(ducked);
    REQUIRE(ducked.state);
    CHECK(ducked.state->hull() == player::PlayerMovementHull::ducked);
    CHECK(ducked.state->origin().z == 18.0F);
    CHECK(ducked.state->view_offset().z == movement::kValveDuckViewOffsetZ);
    CHECK(ducked.statistics.duck_enter_count == 1U);

    const auto stood = fixture::simulate(
        *ducked.state, fixture::make_command(2U), collision);
    REQUIRE(stood);
    REQUIRE(stood.state);
    CHECK(stood.state->hull() == player::PlayerMovementHull::standing);
    CHECK(stood.state->origin().z == 36.0F);
    CHECK(stood.state->view_offset().z ==
        movement::kValveStandingViewOffsetZ);
    CHECK(stood.statistics.duck_exit_count == 1U);
}

TEST_CASE("A low ceiling keeps a grounded player ducked without failing",
    "[goldsrc][movement][kernel][duck][clearance]")
{
    fixture::DeterministicLocalMovementCollision collision;
    collision.add_ceiling(50.0F);
    const auto initial = fixture::make_state(
        {0.0F, 0.0F, 18.0F}, {}, player::PlayerMovementMode::walking,
        player::PlayerMovementHull::ducked, 1U,
        goldsrc::kSyntheticGoldSrcButtonDuck);
    const auto result = fixture::simulate(
        initial, fixture::make_command(2U), collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->hull() == player::PlayerMovementHull::ducked);
    CHECK(result.state->origin().z == 18.0F);
    CHECK(result.state->view_offset().z == movement::kValveDuckViewOffsetZ);
    CHECK(result.statistics.stand_blocked_count == 1U);
    CHECK(result.statistics.duck_exit_count == 0U);
}

TEST_CASE("Reference crouch walking survives a blocked stand attempt",
    "[goldsrc][movement][kernel][duck][reference-actions][clearance]")
{
    fixture::DeterministicLocalMovementCollision low_ceiling;
    low_ceiling.add_ceiling(50.0F);
    auto info = player::local_player_movement_state_create_info(
        fixture::make_state({0.0F, 0.0F, 18.0F}, {},
            player::PlayerMovementMode::walking,
            player::PlayerMovementHull::ducked, 1U,
            goldsrc::kSyntheticGoldSrcButtonDuck));
    info.command_profile = player::GoldSrcMovementCommandProfile::
        reference_wire_jump_duck_v2;
    info.compatibility_profile = player::GoldSrcMovementCompatibilityProfile::
        public_valve_pm_shared_dry_actions_subset_v2;
    info.duck_time_milliseconds = 560U;
    const auto initial = player::LocalPlayerMovementState::create(info);
    REQUIRE(initial);
    goldsrc::GoldSrcWireUserCmd wire;
    wire.msec = 20U;
    wire.forward = 400;
    const auto release = goldsrc::reference_jump_duck_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(2U), wire);
    REQUIRE(release);
    movement::GoldSrcLocalMovementScratch scratch;
    const auto blocked = movement::GoldSrcLocalMovementKernel::simulate(
        *initial.state, *release.state, fixture::make_environment(),
        low_ceiling, scratch);
    REQUIRE(blocked);
    CHECK(blocked.state->hull() == player::PlayerMovementHull::ducked);
    CHECK(blocked.statistics.stand_blocked_count == 1U);
    CHECK(blocked.state->origin().x > initial.state->origin().x);
    fixture::DeterministicLocalMovementCollision open_space;
    const auto next = goldsrc::reference_jump_duck_movement_command(
        *goldsrc::GoldSrcUserCmdSequence::create(3U), wire);
    REQUIRE(next);
    const auto stood = movement::GoldSrcLocalMovementKernel::simulate(
        *blocked.state, *next.state, fixture::make_environment(),
        open_space, scratch);
    REQUIRE(stood);
    CHECK(stood.state->hull() == player::PlayerMovementHull::standing);
    CHECK(stood.state->duck_time_milliseconds() == 0U);
}

TEST_CASE("Airborne duck changes hull around the same center",
    "[goldsrc][movement][kernel][duck][airborne]")
{
    fixture::DeterministicLocalMovementCollision collision{false};
    const auto initial = fixture::make_state(
        {3.0F, -2.0F, 100.0F}, {}, player::PlayerMovementMode::airborne);
    const auto result = fixture::simulate(
        initial,
        fixture::make_command(
            1U, 10U, 0.0F, 0.0F,
            goldsrc::kSyntheticGoldSrcButtonDuck),
        collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->hull() == player::PlayerMovementHull::ducked);
    CHECK(result.state->origin().x == 3.0F);
    CHECK(result.state->origin().y == -2.0F);
    // Gravity changes Z after the center-preserving transition itself.
    CHECK(result.state->origin().z == Catch::Approx(99.96F));
    CHECK(result.state->view_offset().z == movement::kValveDuckViewOffsetZ);
}

} // namespace
