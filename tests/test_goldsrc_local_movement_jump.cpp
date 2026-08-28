#include "local_movement_test_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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

} // namespace
