#include "local_movement_test_fixture.hpp"

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
