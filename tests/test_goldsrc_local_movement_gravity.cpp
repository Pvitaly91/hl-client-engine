#include "local_movement_test_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

namespace fixture = hlclient::tests::local_movement;
namespace movement = hlclient::goldsrc::movement;
namespace player = hlclient::movement;

TEST_CASE("Local movement applies Valve split gravity around the air move",
    "[goldsrc][movement][kernel][gravity]")
{
    fixture::DeterministicLocalMovementCollision collision{false};
    const auto initial = fixture::make_state(
        {0.0F, 0.0F, 100.0F}, {}, player::PlayerMovementMode::airborne);
    const auto result = fixture::simulate(
        initial, fixture::make_command(1U, 10U), collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->velocity().z == Catch::Approx(-8.0F));
    CHECK(result.state->origin().z == Catch::Approx(99.96F));
    CHECK(result.state->mode() == player::PlayerMovementMode::airborne);
    CHECK(result.statistics.command_count == 1U);
    CHECK(result.statistics.substep_count == 1U);
    CHECK(result.state->simulation_time_nanoseconds() == 10'000'000ULL);
}

TEST_CASE("Command msec is divided into bounded deterministic substeps",
    "[goldsrc][movement][kernel][gravity][substep]")
{
    fixture::DeterministicLocalMovementCollision collision{false};
    const auto initial = fixture::make_state(
        {0.0F, 0.0F, 100.0F}, {}, player::PlayerMovementMode::airborne);
    const auto result = fixture::simulate(
        initial, fixture::make_command(1U, 25U), collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.statistics.substep_count == 3U);
    CHECK(result.state->velocity().z == Catch::Approx(-20.0F).margin(1.0e-4F));
    CHECK(result.state->origin().z == Catch::Approx(99.75F).margin(1.0e-4F));
    CHECK(result.state->simulation_time_nanoseconds() == 25'000'000ULL);
}

TEST_CASE("Gravity multiplier and maximum velocity are applied explicitly",
    "[goldsrc][movement][kernel][gravity][velocity]")
{
    fixture::DeterministicLocalMovementCollision collision{false};

    SECTION("per-player gravity multiplier")
    {
        const auto initial = fixture::make_state(
            {0.0F, 0.0F, 100.0F}, {}, player::PlayerMovementMode::airborne,
            player::PlayerMovementHull::standing, 0U, 0U, 2.0F);
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U, 10U), collision);
        REQUIRE(result);
        REQUIRE(result.state);
        CHECK(result.state->velocity().z == Catch::Approx(-16.0F));
        CHECK(result.state->origin().z == Catch::Approx(99.92F));
        CHECK(result.state->gravity_multiplier() == 2.0F);
    }

    SECTION("MoveVars maximum velocity clamps each axis")
    {
        const auto initial = fixture::make_state(
            {0.0F, 0.0F, 100.0F}, {3'000.0F, 0.0F, 0.0F},
            player::PlayerMovementMode::airborne);
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U, 10U), collision);
        REQUIRE(result);
        REQUIRE(result.state);
        CHECK(result.state->velocity().x == 2'000.0F);
        CHECK(result.state->origin().x == 20.0F);
    }

    SECTION("MoveVars maximum velocity also bounds grounded movement")
    {
        fixture::DeterministicLocalMovementCollision grounded_collision;
        const auto initial = fixture::make_state(
            {0.0F, 0.0F, 36.0F}, {3'000.0F, 0.0F, 0.0F});
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U, 10U), grounded_collision);
        REQUIRE(result);
        REQUIRE(result.state);
        CHECK(result.state->velocity().x <= 2'000.0F);
        CHECK(result.state->origin().x <= 20.0F);
    }
}

TEST_CASE("An airborne player lands through the bounded ground probe",
    "[goldsrc][movement][kernel][gravity][landing]")
{
    fixture::DeterministicLocalMovementCollision collision;
    const auto initial = fixture::make_state(
        {0.0F, 0.0F, 40.0F}, {0.0F, 0.0F, -100.0F},
        player::PlayerMovementMode::airborne);
    const auto result = fixture::simulate(
        initial, fixture::make_command(1U, 50U), collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->mode() == player::PlayerMovementMode::walking);
    CHECK(result.state->ground_state().grounded());
    CHECK(result.state->origin().z == 36.0F);
    CHECK(result.state->velocity().z == 0.0F);
}

TEST_CASE("Air acceleration enforces the Valve 30-unit add-speed cap",
    "[goldsrc][movement][kernel][gravity][air-acceleration]")
{
    fixture::DeterministicLocalMovementCollision collision{false};
    const auto result = fixture::simulate(
        fixture::make_state(
            {0.0F, 0.0F, 100.0F}, {},
            player::PlayerMovementMode::airborne),
        fixture::make_command(1U, 10U, 320.0F), collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->velocity().x == Catch::Approx(30.0F));
    CHECK(result.state->origin().x == Catch::Approx(0.3F));
    CHECK(result.state->velocity().z == Catch::Approx(-8.0F));
}

TEST_CASE("Invalid duration and excessive substeps fail transactionally",
    "[goldsrc][movement][kernel][gravity][validation]")
{
    fixture::DeterministicLocalMovementCollision collision{false};
    const auto initial = fixture::make_state(
        {0.0F, 0.0F, 100.0F}, {}, player::PlayerMovementMode::airborne);
    const auto initial_signature =
        player::local_player_movement_state_signature(initial);

    SECTION("zero msec")
    {
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U, 0U), collision);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == movement::
            LocalMovementSimulationErrorCode::invalid_command_duration);
        CHECK_FALSE(result.state);
    }

    SECTION("configured substep bound")
    {
        movement::GoldSrcLocalMovementConfig config;
        config.maximum_substeps_per_command = 2U;
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U, 25U), collision, config);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == movement::
            LocalMovementSimulationErrorCode::substep_limit_exceeded);
        CHECK_FALSE(result.state);
    }

    CHECK(player::local_player_movement_state_signature(initial) ==
        initial_signature);
}

} // namespace
