#include "local_movement_test_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

namespace {

namespace fixture = hlclient::tests::local_movement;
namespace movement = hlclient::goldsrc::movement;
namespace player = hlclient::movement;

class MovingTraceAllSolidCollision final
    : public movement::ILocalMovementCollision {
public:
    [[nodiscard]] movement::LocalMovementCollisionProfile profile()
        const noexcept override
    {
        return base_.profile();
    }

    [[nodiscard]] bool valid() const noexcept override { return true; }

    [[nodiscard]] movement::LocalMovementPointContentsQueryResult
    point_contents(
        const hlclient::assets::AssetVector3& point,
        hlclient::collision::CollisionQueryScratch& scratch,
        const movement::LocalMovementCollisionQueryConfig& config)
        const override
    {
        return base_.point_contents(point, scratch, config);
    }

    [[nodiscard]] movement::LocalMovementPositionQueryResult test_position(
        const hlclient::assets::AssetVector3& origin,
        const player::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch& scratch,
        const movement::LocalMovementCollisionQueryConfig& config)
        const override
    {
        return base_.test_position(origin, hull, scratch, config);
    }

    [[nodiscard]] movement::LocalMovementTraceQueryResult trace_hull(
        const hlclient::assets::AssetVector3& start,
        const hlclient::assets::AssetVector3& end,
        const player::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch& scratch,
        const movement::LocalMovementCollisionQueryConfig& config)
        const override
    {
        if (start.x == end.x && start.y == end.y) {
            return base_.trace_hull(start, end, hull, scratch, config);
        }
        movement::LocalMovementTrace trace;
        trace.start_solid = true;
        trace.all_solid = true;
        trace.fraction = 0.0;
        trace.end_position = start;
        trace.start_contents = {
            player::PlayerMovementContents::empty, -1};
        trace.end_contents = trace.start_contents;
        trace.collision_profile = profile();
        return {trace, std::nullopt};
    }

private:
    fixture::DeterministicLocalMovementCollision base_;
};

TEST_CASE("An unobstructed airborne slide consumes the complete duration",
    "[goldsrc][movement][kernel][slide][no-hit]")
{
    fixture::DeterministicLocalMovementCollision collision;
    const auto result = fixture::simulate(
        fixture::make_state(
            {0.0F, 0.0F, 100.0F}, {100.0F, 50.0F, 0.0F},
            player::PlayerMovementMode::airborne),
        fixture::make_command(1U), collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->origin().x == Catch::Approx(1.0F));
    CHECK(result.state->origin().y == Catch::Approx(0.5F));
    CHECK(result.state->origin().z == Catch::Approx(99.96F));
    CHECK(result.statistics.collision_hit_count == 0U);
    CHECK(result.touches.empty());
}

TEST_CASE("An airborne slide clips into the floor and becomes grounded",
    "[goldsrc][movement][kernel][slide][floor]")
{
    fixture::DeterministicLocalMovementCollision collision;
    const auto result = fixture::simulate(
        fixture::make_state(
            {0.0F, 0.0F, 40.0F}, {0.0F, 0.0F, -500.0F},
            player::PlayerMovementMode::airborne),
        fixture::make_command(1U), collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->origin().z == Catch::Approx(36.0F));
    CHECK(result.state->velocity().z == 0.0F);
    CHECK(result.state->ground_state().grounded());
    REQUIRE_FALSE(result.touches.empty());
    CHECK(std::ranges::any_of(result.touches, [](const auto& touch) {
        return touch.plane.normal.z == 1.0F &&
            touch.phase == player::PlayerMovementPhase::airborne_slide;
    }));
}

TEST_CASE("Movement clips into a wall and keeps its tangential component",
    "[goldsrc][movement][kernel][slide]")
{
    fixture::DeterministicLocalMovementCollision collision;
    collision.add_positive_x_wall(20.0F);
    const auto result = fixture::simulate(
        fixture::make_state(),
        fixture::make_command(1U, 100U, 320.0F, -320.0F),
        collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->origin().x == Catch::Approx(4.0F).margin(1.0e-4F));
    CHECK(result.state->origin().y > 4.0F);
    CHECK(result.state->velocity().x == Catch::Approx(0.0F).margin(1.0e-4F));
    CHECK(result.state->velocity().y > 0.0F);
    CHECK(result.statistics.collision_hit_count > 0U);
    CHECK(result.statistics.clip_plane_count > 0U);
    REQUIRE_FALSE(result.touches.empty());
    CHECK(std::ranges::any_of(result.touches, [](const auto& touch) {
        return touch.plane.normal.x == -1.0F;
    }));
}

TEST_CASE("Two perpendicular walls stop motion at a deterministic corner",
    "[goldsrc][movement][kernel][slide][corner]")
{
    fixture::DeterministicLocalMovementCollision collision;
    collision.add_positive_x_wall(20.0F);
    collision.add_positive_y_wall(20.0F);
    const auto result = fixture::simulate(
        fixture::make_state(),
        fixture::make_command(1U, 100U, 320.0F, -320.0F),
        collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->origin().x == Catch::Approx(4.0F).margin(1.0e-4F));
    CHECK(result.state->origin().y == Catch::Approx(4.0F).margin(1.0e-4F));
    CHECK(result.state->velocity().x == Catch::Approx(0.0F).margin(1.0e-4F));
    CHECK(result.state->velocity().y == Catch::Approx(0.0F).margin(1.0e-4F));
    CHECK(result.statistics.clip_plane_count >= 2U);
    CHECK(std::ranges::any_of(result.touches, [](const auto& touch) {
        return touch.plane.normal.x == -1.0F;
    }));
    CHECK(std::ranges::any_of(result.touches, [](const auto& touch) {
        return touch.plane.normal.y == -1.0F;
    }));
}

TEST_CASE("Two perpendicular wall planes preserve motion along their crease",
    "[goldsrc][movement][kernel][slide][crease]")
{
    fixture::DeterministicLocalMovementCollision collision;
    collision.add_positive_x_wall(20.0F);
    collision.add_positive_y_wall(20.0F);
    const auto result = fixture::simulate(
        fixture::make_state(
            {0.0F, 0.0F, 100.0F}, {1'000.0F, 1'000.0F, 400.0F},
            player::PlayerMovementMode::airborne),
        fixture::make_command(1U), collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->origin().x == Catch::Approx(4.0F).margin(1.0e-4F));
    CHECK(result.state->origin().y == Catch::Approx(4.0F).margin(1.0e-4F));
    CHECK(result.state->origin().z > 100.0F);
    CHECK(result.state->velocity().x == 0.0F);
    CHECK(result.state->velocity().y == 0.0F);
    CHECK(result.state->velocity().z > 0.0F);
    CHECK(result.statistics.clip_plane_count == 2U);
}

TEST_CASE("Three independent clip planes stop the slide deterministically",
    "[goldsrc][movement][kernel][slide][three-plane]")
{
    fixture::DeterministicLocalMovementCollision collision;
    collision.add_positive_x_wall(20.0F);
    collision.add_positive_y_wall(20.0F);
    collision.add_ceiling(80.0F);
    const auto result = fixture::simulate(
        fixture::make_state(
            {0.0F, 0.0F, 40.0F}, {1'000.0F, 1'000.0F, 1'004.0F},
            player::PlayerMovementMode::airborne),
        fixture::make_command(1U), collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->origin().x == Catch::Approx(4.0F).margin(1.0e-4F));
    CHECK(result.state->origin().y == Catch::Approx(4.0F).margin(1.0e-4F));
    CHECK(result.state->origin().z == Catch::Approx(44.0F).margin(1.0e-4F));
    CHECK(result.state->velocity().x == 0.0F);
    CHECK(result.state->velocity().y == 0.0F);
    CHECK(result.state->velocity().z == Catch::Approx(-4.0F));
    CHECK(result.statistics.clip_plane_count == 3U);
    CHECK(result.touches.size() == 3U);
}

TEST_CASE("The configured bump bound stops unconsumed slide velocity",
    "[goldsrc][movement][kernel][slide][bump-limit]")
{
    fixture::DeterministicLocalMovementCollision collision;
    collision.add_positive_x_wall(20.0F);
    movement::GoldSrcLocalMovementConfig config;
    config.maximum_slide_bumps = 1U;
    const auto result = fixture::simulate(
        fixture::make_state(
            {0.0F, 0.0F, 100.0F}, {1'000.0F, 100.0F, 0.0F},
            player::PlayerMovementMode::airborne),
        fixture::make_command(1U), collision, config);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->origin().x == Catch::Approx(4.0F).margin(1.0e-4F));
    CHECK(result.state->origin().y == Catch::Approx(0.4F).margin(1.0e-4F));
    CHECK(result.state->velocity().x == 0.0F);
    CHECK(result.state->velocity().y == 0.0F);
    CHECK(result.statistics.slide_bump_count == 1U);
    CHECK(result.statistics.clip_plane_count == 1U);
}

TEST_CASE("An allsolid movement trace fails without publishing partial state",
    "[goldsrc][movement][kernel][slide][allsolid]")
{
    MovingTraceAllSolidCollision collision;
    const auto result = fixture::simulate(
        fixture::make_state(
            {0.0F, 0.0F, 100.0F}, {100.0F, 0.0F, 0.0F},
            player::PlayerMovementMode::airborne),
        fixture::make_command(1U), collision);

    REQUIRE_FALSE(result);
    CHECK_FALSE(result.state);
    REQUIRE(result.error);
    CHECK(result.error->code ==
        movement::LocalMovementSimulationErrorCode::player_allsolid);
    CHECK(result.statistics.all_solid_count == 1U);
    CHECK(result.deterministic_state_signature == 0U);
}

TEST_CASE("Wall clipping preserves the movement time remaining after impact",
    "[goldsrc][movement][kernel][slide][remaining-time]")
{
    fixture::DeterministicLocalMovementCollision collision;
    collision.add_positive_x_wall(20.0F);
    const auto result = fixture::simulate(
        fixture::make_state(
            {0.0F, 0.0F, 100.0F}, {100.0F, 100.0F, 0.0F},
            player::PlayerMovementMode::airborne),
        fixture::make_command(1U, 100U), collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->origin().x == Catch::Approx(4.0F).margin(1.0e-4F));
    CHECK(result.state->origin().y == Catch::Approx(10.0F).margin(1.0e-4F));
    CHECK(result.state->velocity().x == 0.0F);
    CHECK(result.state->velocity().y == Catch::Approx(100.0F));
    CHECK(result.statistics.collision_hit_count == 1U);
}

TEST_CASE("A head-on wall contact has stable state and touch identities",
    "[goldsrc][movement][kernel][slide][determinism]")
{
    fixture::DeterministicLocalMovementCollision first_collision;
    first_collision.add_positive_x_wall(20.0F);
    fixture::DeterministicLocalMovementCollision second_collision;
    second_collision.add_positive_x_wall(20.0F);
    const auto initial = fixture::make_state();
    const auto command = fixture::make_command(1U, 100U, 320.0F);
    const auto first = fixture::simulate(initial, command, first_collision);
    const auto second = fixture::simulate(initial, command, second_collision);

    REQUIRE(first);
    REQUIRE(second);
    CHECK(first.deterministic_state_signature ==
        second.deterministic_state_signature);
    REQUIRE(first.touches.size() == second.touches.size());
    for (std::size_t index = 0U; index < first.touches.size(); ++index) {
        CHECK(first.touches[index].hit == second.touches[index].hit);
        CHECK(first.touches[index].plane.normal.x ==
            second.touches[index].plane.normal.x);
        CHECK(first.touches[index].plane.normal.y ==
            second.touches[index].plane.normal.y);
        CHECK(first.touches[index].plane.normal.z ==
            second.touches[index].plane.normal.z);
        CHECK(first.touches[index].plane.distance ==
            second.touches[index].plane.distance);
        CHECK(first.touches[index].plane.source_plane_index ==
            second.touches[index].plane.source_plane_index);
        CHECK(first.touches[index].fraction == second.touches[index].fraction);
        CHECK(first.touches[index].phase == second.touches[index].phase);
    }
}

} // namespace
