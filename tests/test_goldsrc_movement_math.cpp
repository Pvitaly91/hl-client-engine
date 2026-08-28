#include <hlclient/goldsrc/movement/goldsrc_movement_math.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

namespace {

namespace assets = hlclient::assets;
namespace movement = hlclient::goldsrc::movement;

TEST_CASE("Pinned Valve dry-walk literals remain exact named constants",
          "[goldsrc][movement][math][evidence]")
{
    CHECK(movement::kValveStandingHullMinimumZ == -36.0F);
    CHECK(movement::kValveStandingHullMaximumZ == 36.0F);
    CHECK(movement::kValveDuckHullMinimumZ == -18.0F);
    CHECK(movement::kValveDuckHullMaximumZ == 18.0F);
    CHECK(movement::kValveStandingViewOffsetZ == 28.0F);
    CHECK(movement::kValveDuckViewOffsetZ == 12.0F);
    CHECK(movement::kValveStopEpsilon == 0.1F);
    CHECK(movement::kValveMaximumClipPlanes == 5U);
    CHECK(movement::kValveMaximumSlideBumps == 4U);
    CHECK(movement::kValveGroundProbeDistance == 2.0F);
    CHECK(movement::kValveMinimumWalkableNormalZ == 0.7F);
    CHECK(movement::kValveMaximumGroundSnapUpwardVelocity == 180.0F);
    CHECK(movement::kValveAirWishSpeedCap == 30.0F);
    CHECK(movement::kValveJumpImpulse ==
        Catch::Approx(std::sqrt(2.0F * 800.0F * 45.0F)));
}

TEST_CASE("Yaw-only wish direction follows GoldSrc horizontal axes and speed cap",
          "[goldsrc][movement][math][wish]")
{
    SECTION("yaw zero uses +X forward and -Y right")
    {
        const auto wish = movement::yaw_only_wish_direction(
            0.0F, 300.0F, 400.0F, 320.0F);
        REQUIRE(wish);
        CHECK(wish.wish->direction.x == Catch::Approx(0.6F));
        CHECK(wish.wish->direction.y == Catch::Approx(-0.8F));
        CHECK(wish.wish->direction.z == 0.0F);
        CHECK(wish.wish->uncapped_speed == Catch::Approx(500.0F));
        CHECK(wish.wish->speed == 320.0F);
    }

    SECTION("yaw ninety rotates forward to +Y and right to +X")
    {
        const auto forward = movement::yaw_only_wish_direction(
            90.0F, 100.0F, 0.0F, 320.0F);
        REQUIRE(forward);
        CHECK(forward.wish->direction.x == Catch::Approx(0.0F).margin(1.0e-6F));
        CHECK(forward.wish->direction.y == Catch::Approx(1.0F));

        const auto right = movement::yaw_only_wish_direction(
            90.0F, 0.0F, 100.0F, 320.0F);
        REQUIRE(right);
        CHECK(right.wish->direction.x == Catch::Approx(1.0F));
        CHECK(right.wish->direction.y == Catch::Approx(0.0F).margin(1.0e-6F));
    }

    SECTION("neutral input has a stable zero direction")
    {
        const auto neutral = movement::yaw_only_wish_direction(
            -123.0F, 0.0F, 0.0F, 320.0F);
        REQUIRE(neutral);
        CHECK(neutral.wish->direction.x == 0.0F);
        CHECK(neutral.wish->direction.y == 0.0F);
        CHECK(neutral.wish->speed == 0.0F);
        CHECK(neutral.wish->uncapped_speed == 0.0F);
    }
}

TEST_CASE("Yaw-only wish direction supports opposite input and wrapped yaw",
          "[goldsrc][movement][math][wish][policy]")
{
    SECTION("opposite forward and side inputs reverse their horizontal axes")
    {
        const auto backward = movement::yaw_only_wish_direction(
            0.0F, -120.0F, 0.0F, 320.0F);
        REQUIRE(backward);
        CHECK(backward.wish->direction.x == -1.0F);
        CHECK(backward.wish->direction.y == 0.0F);
        CHECK(backward.wish->direction.z == 0.0F);
        CHECK(backward.wish->speed == 120.0F);

        const auto opposite_side = movement::yaw_only_wish_direction(
            0.0F, 0.0F, -75.0F, 320.0F);
        REQUIRE(opposite_side);
        CHECK(opposite_side.wish->direction.x == 0.0F);
        CHECK(opposite_side.wish->direction.y == 1.0F);
        CHECK(opposite_side.wish->direction.z == 0.0F);
    }

    SECTION("the yaw-only API is periodic and never introduces pitch")
    {
        const auto base = movement::yaw_only_wish_direction(
            90.0F, 100.0F, 25.0F, 320.0F);
        const auto wrapped = movement::yaw_only_wish_direction(
            450.0F, 100.0F, 25.0F, 320.0F);
        REQUIRE(base);
        REQUIRE(wrapped);
        CHECK(wrapped.wish->direction.x ==
            Catch::Approx(base.wish->direction.x).margin(1.0e-6F));
        CHECK(wrapped.wish->direction.y ==
            Catch::Approx(base.wish->direction.y).margin(1.0e-6F));
        CHECK(wrapped.wish->direction.z == 0.0F);
        CHECK(wrapped.wish->speed == Catch::Approx(base.wish->speed));
    }
}

TEST_CASE("Horizontal ground friction uses stop-speed control and preserves Z",
          "[goldsrc][movement][math][friction]")
{
    const auto moving = movement::apply_horizontal_ground_friction(
        {100.0F, 0.0F, 17.0F}, 100.0F, 4.0F, 1.0F, 0.1F);
    REQUIRE(moving);
    CHECK(moving.value->x == Catch::Approx(60.0F));
    CHECK(moving.value->y == 0.0F);
    CHECK(moving.value->z == 17.0F);

    const auto below_stop = movement::apply_horizontal_ground_friction(
        {10.0F, 0.0F, -7.0F}, 100.0F, 4.0F, 1.0F, 0.1F);
    REQUIRE(below_stop);
    CHECK(below_stop.value->x == 0.0F);
    CHECK(below_stop.value->z == -7.0F);

    const auto below_epsilon = movement::apply_horizontal_ground_friction(
        {0.05F, 0.0F, 2.0F}, 100.0F, 4.0F, 1.0F, 1.0F);
    REQUIRE(below_epsilon);
    CHECK(below_epsilon.value->x == 0.0F);
    CHECK(below_epsilon.value->y == 0.0F);
    CHECK(below_epsilon.value->z == 2.0F);
}

TEST_CASE("Zero friction terms are identities and friction cannot reverse motion",
          "[goldsrc][movement][math][friction][boundary]")
{
    const assets::AssetVector3 initial{10.0F, -20.0F, 7.0F};
    const auto zero_coefficient = movement::apply_horizontal_ground_friction(
        initial, 100.0F, 0.0F, 1.0F, 0.5F);
    const auto zero_multiplier = movement::apply_horizontal_ground_friction(
        initial, 100.0F, 4.0F, 0.0F, 0.5F);
    const auto zero_duration = movement::apply_horizontal_ground_friction(
        initial, 100.0F, 4.0F, 1.0F, 0.0F);
    REQUIRE(zero_coefficient);
    REQUIRE(zero_multiplier);
    REQUIRE(zero_duration);
    CHECK(zero_coefficient.value->x == initial.x);
    CHECK(zero_coefficient.value->y == initial.y);
    CHECK(zero_coefficient.value->z == initial.z);
    CHECK(zero_multiplier.value->x == initial.x);
    CHECK(zero_multiplier.value->y == initial.y);
    CHECK(zero_multiplier.value->z == initial.z);
    CHECK(zero_duration.value->x == initial.x);
    CHECK(zero_duration.value->y == initial.y);
    CHECK(zero_duration.value->z == initial.z);

    const auto excessive_drop = movement::apply_horizontal_ground_friction(
        initial, 100.0F, 100.0F, 1.0F, 1.0F);
    REQUIRE(excessive_drop);
    CHECK(excessive_drop.value->x == 0.0F);
    CHECK(excessive_drop.value->y == 0.0F);
    CHECK(excessive_drop.value->z == initial.z);
    CHECK(movement::movement_dot(*excessive_drop.value, initial) >= 0.0F);
}

TEST_CASE("Ground acceleration follows current-add-cap operation order",
          "[goldsrc][movement][math][acceleration]")
{
    const auto accelerated = movement::accelerate_ground(
        {}, {1.0F, 0.0F, 0.0F}, 200.0F, 10.0F, 1.0F, 0.01F);
    REQUIRE(accelerated);
    CHECK(accelerated.value->x == Catch::Approx(20.0F));

    const auto capped = movement::accelerate_ground(
        {195.0F, 0.0F, 3.0F}, {1.0F, 0.0F, 0.0F},
        200.0F, 10.0F, 1.0F, 0.01F);
    REQUIRE(capped);
    CHECK(capped.value->x == 200.0F);
    CHECK(capped.value->z == 3.0F);

    const auto no_add = movement::accelerate_ground(
        {210.0F, 0.0F, 3.0F}, {1.0F, 0.0F, 0.0F},
        200.0F, 10.0F, 1.0F, 0.01F);
    REQUIRE(no_add);
    CHECK(no_add.value->x == 210.0F);
}

TEST_CASE("Zero acceleration inputs are identities and opposite acceleration adds",
          "[goldsrc][movement][math][acceleration][boundary]")
{
    const assets::AssetVector3 initial{50.0F, 0.0F, 3.0F};
    const auto zero_speed = movement::accelerate_ground(
        initial, {}, 0.0F, 10.0F, 1.0F, 0.01F);
    const auto zero_acceleration = movement::accelerate_ground(
        initial, {-1.0F, 0.0F, 0.0F}, 100.0F, 0.0F, 1.0F, 0.01F);
    const auto zero_multiplier = movement::accelerate_ground(
        initial, {-1.0F, 0.0F, 0.0F}, 100.0F, 10.0F, 0.0F, 0.01F);
    const auto zero_duration = movement::accelerate_ground(
        initial, {-1.0F, 0.0F, 0.0F}, 100.0F, 10.0F, 1.0F, 0.0F);
    REQUIRE(zero_speed);
    REQUIRE(zero_acceleration);
    REQUIRE(zero_multiplier);
    REQUIRE(zero_duration);
    CHECK(zero_speed.value->x == initial.x);
    CHECK(zero_speed.value->y == initial.y);
    CHECK(zero_speed.value->z == initial.z);
    CHECK(zero_acceleration.value->x == initial.x);
    CHECK(zero_acceleration.value->y == initial.y);
    CHECK(zero_acceleration.value->z == initial.z);
    CHECK(zero_multiplier.value->x == initial.x);
    CHECK(zero_multiplier.value->y == initial.y);
    CHECK(zero_multiplier.value->z == initial.z);
    CHECK(zero_duration.value->x == initial.x);
    CHECK(zero_duration.value->y == initial.y);
    CHECK(zero_duration.value->z == initial.z);

    const auto opposite = movement::accelerate_ground(
        initial, {-1.0F, 0.0F, 0.0F}, 100.0F, 10.0F, 1.0F, 0.01F);
    REQUIRE(opposite);
    CHECK(opposite.value->x == Catch::Approx(40.0F));
    CHECK(opposite.value->y == 0.0F);
    CHECK(opposite.value->z == initial.z);
}

TEST_CASE("Air acceleration keeps Valve 30-cap asymmetry literal",
          "[goldsrc][movement][math][air]")
{
    const auto asymmetric = movement::accelerate_air(
        {}, {1.0F, 0.0F, 0.0F}, 320.0F, 1.0F, 1.0F, 0.01F);
    REQUIRE(asymmetric);
    // 1 * uncapped_wish_speed(320) * 0.01 = 3.2, not 0.3.
    CHECK(asymmetric.value->x == Catch::Approx(3.2F));

    const auto capped_add = movement::accelerate_air(
        {29.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F},
        320.0F, 10.0F, 1.0F, 0.01F);
    REQUIRE(capped_add);
    CHECK(capped_add.value->x == 30.0F);

    const auto no_add = movement::accelerate_air(
        {30.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F},
        320.0F, 10.0F, 1.0F, 0.01F);
    REQUIRE(no_add);
    CHECK(no_add.value->x == 30.0F);
}

TEST_CASE("Component stop epsilon is strict and clip velocity slides",
          "[goldsrc][movement][math][clip]")
{
    const auto stopped = movement::apply_component_stop_epsilon(
        {0.099F, -0.099F, 0.1F});
    REQUIRE(stopped);
    CHECK(stopped.value->x == 0.0F);
    CHECK(stopped.value->y == 0.0F);
    CHECK(stopped.value->z == 0.1F);

    const auto negative_boundary = movement::apply_component_stop_epsilon(
        {-0.1F, 0.0F, 0.0F});
    REQUIRE(negative_boundary);
    CHECK(negative_boundary.value->x == -0.1F);

    const auto clipped = movement::clip_velocity(
        {10.0F, -5.0F, 3.0F}, {1.0F, 0.0F, 0.0F});
    REQUIRE(clipped);
    CHECK(clipped.value->x == 0.0F);
    CHECK(clipped.value->y == -5.0F);
    CHECK(clipped.value->z == 3.0F);
}

TEST_CASE("Clip velocity handles floor ceiling angled and away-facing planes",
          "[goldsrc][movement][math][clip][planes]")
{
    const auto floor = movement::clip_velocity(
        {4.0F, -2.0F, -30.0F}, {0.0F, 0.0F, 1.0F});
    REQUIRE(floor);
    CHECK(floor.value->x == 4.0F);
    CHECK(floor.value->y == -2.0F);
    CHECK(floor.value->z == 0.0F);

    const auto ceiling = movement::clip_velocity(
        {4.0F, -2.0F, 30.0F}, {0.0F, 0.0F, -1.0F});
    REQUIRE(ceiling);
    CHECK(ceiling.value->x == 4.0F);
    CHECK(ceiling.value->y == -2.0F);
    CHECK(ceiling.value->z == 0.0F);

    constexpr float diagonal = 0.7071067811865475F;
    const assets::AssetVector3 angled_normal{diagonal, 0.0F, diagonal};
    const auto angled = movement::clip_velocity(
        {-20.0F, 7.0F, -20.0F}, angled_normal);
    REQUIRE(angled);
    CHECK(angled.value->x == Catch::Approx(0.0F).margin(1.0e-5F));
    CHECK(angled.value->y == 7.0F);
    CHECK(angled.value->z == Catch::Approx(0.0F).margin(1.0e-5F));
    CHECK(movement::movement_dot(*angled.value, angled_normal) >=
        -movement::kValveStopEpsilon);

    const assets::AssetVector3 wall_normal{1.0F, 0.0F, 0.0F};
    const auto moving_away = movement::clip_velocity(
        {30.0F, 8.0F, -4.0F}, wall_normal);
    REQUIRE(moving_away);
    CHECK(moving_away.value->x == 0.0F);
    CHECK(moving_away.value->y == 8.0F);
    CHECK(moving_away.value->z == -4.0F);
    CHECK(movement::movement_dot(*moving_away.value, wall_normal) >=
        -movement::kValveStopEpsilon);
}

TEST_CASE("Clip velocity reports public floor and wall blocked axes",
          "[goldsrc][movement][math][clip][metadata]")
{
    const auto floor = movement::clip_velocity_against_plane(
        {1.0F, 2.0F, -3.0F}, {0.0F, 0.0F, 1.0F});
    REQUIRE(floor);
    CHECK(floor.value->velocity.z == 0.0F);
    CHECK(floor.value->blocked_axes ==
        movement::GoldSrcClipBlockedAxis::floor);

    const auto wall = movement::clip_velocity_against_plane(
        {-3.0F, 2.0F, 1.0F}, {1.0F, 0.0F, 0.0F});
    REQUIRE(wall);
    CHECK(wall.value->velocity.x == 0.0F);
    CHECK(wall.value->blocked_axes ==
        movement::GoldSrcClipBlockedAxis::wall_or_step);

    constexpr float diagonal = 0.7071067811865475F;
    const auto slope = movement::clip_velocity_against_plane(
        {-3.0F, 2.0F, -3.0F}, {diagonal, 0.0F, diagonal});
    REQUIRE(slope);
    CHECK(slope.value->blocked_axes ==
        movement::GoldSrcClipBlockedAxis::floor);

    const auto ceiling = movement::clip_velocity_against_plane(
        {1.0F, 2.0F, 3.0F}, {0.0F, 0.0F, -1.0F});
    REQUIRE(ceiling);
    CHECK(ceiling.value->blocked_axes ==
        movement::GoldSrcClipBlockedAxis::none);
}

TEST_CASE("Maximum velocity clamps per axis and rejects NaN transactionally",
          "[goldsrc][movement][math][velocity][security]")
{
    const auto clamped = movement::clamp_velocity_per_axis(
        {3'000.0F, -4'000.0F, 100.0F}, 2'000.0F);
    REQUIRE(clamped);
    CHECK(clamped.value->x == 2'000.0F);
    CHECK(clamped.value->y == -2'000.0F);
    CHECK(clamped.value->z == 100.0F);

    const auto per_axis = movement::clamp_velocity_per_axis(
        {2'000.0F, 2'000.0F, 0.0F}, 2'000.0F);
    REQUIRE(per_axis);
    CHECK(per_axis.value->x == 2'000.0F);
    CHECK(per_axis.value->y == 2'000.0F);

    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto rejected = movement::clamp_velocity_per_axis(
        {nan, 1.0F, 2.0F}, 2'000.0F);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
        movement::GoldSrcMovementMathErrorCode::non_finite_input);
    CHECK_FALSE(rejected.value);
}

TEST_CASE("Movement math rejects malformed scalar and direction inputs",
          "[goldsrc][movement][math][validation]")
{
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(movement::yaw_only_wish_direction(
        nan, 1.0F, 0.0F, 320.0F));
    CHECK_FALSE(movement::yaw_only_wish_direction(
        0.0F, 1.0F, 0.0F, 0.0F));
    CHECK_FALSE(movement::apply_horizontal_ground_friction(
        {}, -1.0F, 4.0F, 1.0F, 0.01F));
    CHECK_FALSE(movement::apply_horizontal_ground_friction(
        {}, 100.0F, -1.0F, 1.0F, 0.01F));
    CHECK_FALSE(movement::apply_horizontal_ground_friction(
        {}, 100.0F, 4.0F, -1.0F, 0.01F));
    CHECK_FALSE(movement::apply_horizontal_ground_friction(
        {}, 100.0F, 4.0F, 1.0F, -0.01F));
    CHECK_FALSE(movement::accelerate_ground(
        {}, {2.0F, 0.0F, 0.0F}, 100.0F, 10.0F, 1.0F, 0.01F));
    CHECK_FALSE(movement::accelerate_ground(
        {}, {1.0F, 0.0F, 0.0F}, -1.0F, 10.0F, 1.0F, 0.01F));
    CHECK_FALSE(movement::accelerate_ground(
        {}, {1.0F, 0.0F, 0.0F}, 100.0F, -1.0F, 1.0F, 0.01F));
    CHECK_FALSE(movement::accelerate_ground(
        {}, {1.0F, 0.0F, 0.0F}, 100.0F, 10.0F, -1.0F, 0.01F));
    CHECK_FALSE(movement::accelerate_air(
        {}, {1.0F, 0.0F, 0.0F}, 100.0F, -1.0F, 1.0F, 0.01F));
    CHECK_FALSE(movement::clip_velocity(
        {}, {2.0F, 0.0F, 0.0F}));
    CHECK_FALSE(movement::apply_component_stop_epsilon({}, -0.1F));
    CHECK_FALSE(movement::clamp_velocity_per_axis({}, 0.0F));
}

} // namespace
