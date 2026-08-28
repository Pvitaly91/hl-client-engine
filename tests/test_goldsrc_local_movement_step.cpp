#include "local_movement_test_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

namespace fixture = hlclient::tests::local_movement;
namespace movement = hlclient::goldsrc::movement;
namespace player = hlclient::movement;

enum class StepTraceInjection {
    horizontal_startsolid,
    horizontal_allsolid,
    step_up_liquid,
    step_down_liquid,
};

class StepTraceInjectionCollision final
    : public movement::ILocalMovementCollision {
public:
    explicit StepTraceInjectionCollision(
        const StepTraceInjection injection) noexcept
        : injection_{injection}
    {
    }

    [[nodiscard]] movement::LocalMovementCollisionProfile profile()
        const noexcept override
    {
        return base_.profile();
    }

    [[nodiscard]] bool valid() const noexcept override
    {
        return base_.valid();
    }

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
        auto traced = base_.trace_hull(start, end, hull, scratch, config);
        ++trace_count_;
        if (!traced || !traced.result) {
            return traced;
        }
        const bool horizontal_injection = trace_count_ == 4U &&
            (injection_ == StepTraceInjection::horizontal_startsolid ||
                injection_ == StepTraceInjection::horizontal_allsolid);
        if (horizontal_injection) {
            movement::LocalMovementTrace injected;
            injected.start_solid = true;
            injected.all_solid =
                injection_ == StepTraceInjection::horizontal_allsolid;
            injected.fraction = 0.0;
            injected.end_position = start;
            injected.in_open = true;
            injected.start_contents = {
                player::PlayerMovementContents::empty, -1};
            injected.end_contents = injected.start_contents;
            injected.blocking_contents =
                movement::LocalMovementCollisionContents{
                    player::PlayerMovementContents::solid, -2};
            injected.collision_profile = profile();
            traced.result = injected;
        } else if ((trace_count_ == 3U &&
                       injection_ == StepTraceInjection::step_up_liquid) ||
            (trace_count_ == 5U &&
                injection_ == StepTraceInjection::step_down_liquid)) {
            traced.result->in_liquid = true;
        }
        return traced;
    }

private:
    fixture::DeterministicLocalMovementCollision base_;
    StepTraceInjection injection_;
    mutable std::size_t trace_count_{0U};
};

TEST_CASE("The bounded step path wins direct-vs-step progress below step size",
    "[goldsrc][movement][kernel][step][selection]")
{
    fixture::DeterministicLocalMovementCollision collision;
    collision.add_step(20.0F, 100.0F, -64.0F, 64.0F, 12.0F);
    const auto result = fixture::simulate(
        fixture::make_state(),
        fixture::make_command(1U, 100U, 320.0F),
        collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->origin().x > 4.0F);
    CHECK(result.state->origin().z == Catch::Approx(48.0F).margin(1.0e-4F));
    CHECK(result.state->ground_state().grounded());
    CHECK(result.statistics.step_attempt_count > 0U);
    CHECK(result.statistics.step_success_count > 0U);
    REQUIRE_FALSE(result.touches.empty());
    bool saw_step_landing = false;
    for (const auto& touch : result.touches) {
        CHECK(touch.phase !=
            hlclient::movement::PlayerMovementPhase::direct_slide);
        saw_step_landing = saw_step_landing ||
            touch.phase == hlclient::movement::PlayerMovementPhase::step_down;
    }
    CHECK(saw_step_landing);
}

TEST_CASE("An obstacle exactly at the pinned step limit is climbable",
    "[goldsrc][movement][kernel][step][exact-limit]")
{
    fixture::DeterministicLocalMovementCollision collision;
    collision.add_step(20.0F, 100.0F, -64.0F, 64.0F, 18.0F);
    const auto result = fixture::simulate(
        fixture::make_state(),
        fixture::make_command(1U, 100U, 320.0F),
        collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->origin().x > 4.0F);
    CHECK(result.state->origin().z == Catch::Approx(54.0F).margin(1.0e-4F));
    CHECK(result.state->ground_state().grounded());
    CHECK(result.statistics.step_attempt_count > 0U);
    CHECK(result.statistics.step_success_count > 0U);
    CHECK(result.statistics.start_solid_count == 0U);
    CHECK(result.statistics.all_solid_count == 0U);
    REQUIRE_FALSE(result.touches.empty());
    for (const auto& touch : result.touches) {
        CHECK(touch.phase !=
            hlclient::movement::PlayerMovementPhase::direct_slide);
    }
}

TEST_CASE("An obstacle above step size remains blocking",
    "[goldsrc][movement][kernel][step][limit]")
{
    fixture::DeterministicLocalMovementCollision collision;
    collision.add_step(20.0F, 100.0F, -64.0F, 64.0F, 24.0F);
    const auto result = fixture::simulate(
        fixture::make_state(),
        fixture::make_command(1U, 100U, 320.0F),
        collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->origin().x == Catch::Approx(4.0F).margin(1.0e-4F));
    CHECK(result.state->origin().z == Catch::Approx(36.0F).margin(1.0e-4F));
    CHECK(result.statistics.step_attempt_count > 0U);
    CHECK(result.statistics.step_success_count == 0U);
    REQUIRE_FALSE(result.touches.empty());
    for (const auto& touch : result.touches) {
        CHECK(touch.phase ==
            hlclient::movement::PlayerMovementPhase::direct_slide);
    }
}

TEST_CASE("Low overhead clearance rejects the step candidate transactionally",
    "[goldsrc][movement][kernel][step][overhead]")
{
    fixture::DeterministicLocalMovementCollision collision;
    collision.add_step(20.0F, 100.0F, -64.0F, 64.0F, 12.0F);
    collision.add_ceiling(80.0F);
    const auto result = fixture::simulate(
        fixture::make_state(),
        fixture::make_command(1U, 100U, 320.0F),
        collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->origin().x == Catch::Approx(4.0F).margin(1.0e-4F));
    CHECK(result.state->origin().z == Catch::Approx(36.0F).margin(1.0e-4F));
    CHECK(result.statistics.step_attempt_count > 0U);
    CHECK(result.statistics.step_success_count == 0U);
    REQUIRE_FALSE(result.touches.empty());
    for (const auto& touch : result.touches) {
        CHECK(touch.phase ==
            hlclient::movement::PlayerMovementPhase::direct_slide);
    }
}

TEST_CASE("Unobstructed equal progress keeps the direct no-step candidate",
    "[goldsrc][movement][kernel][step][tie]")
{
    fixture::DeterministicLocalMovementCollision collision;
    const auto result = fixture::simulate(
        fixture::make_state(),
        fixture::make_command(1U, 30U, 160.0F),
        collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->origin().z == 36.0F);
    CHECK(result.statistics.step_attempt_count == 3U);
    CHECK(result.statistics.step_success_count == 0U);
}

TEST_CASE("The ground probe snaps a nearby falling player downward",
    "[goldsrc][movement][kernel][step][downward-snap]")
{
    fixture::DeterministicLocalMovementCollision collision;
    const auto result = fixture::simulate(
        fixture::make_state(
            {0.0F, 0.0F, 37.0F}, {0.0F, 0.0F, -10.0F},
            hlclient::movement::PlayerMovementMode::airborne),
        fixture::make_command(1U), collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->origin().z == Catch::Approx(36.0F));
    CHECK(result.state->velocity().z == 0.0F);
    CHECK(result.state->mode() ==
        hlclient::movement::PlayerMovementMode::walking);
    CHECK(result.state->ground_state().grounded());
    CHECK(result.state->ground_state().walkable());
    CHECK(result.statistics.ground_probe_count > 0U);
    CHECK(result.statistics.start_solid_count == 0U);
    CHECK(result.statistics.all_solid_count == 0U);
}

TEST_CASE("Step-horizontal solid starts fail the entire command transactionally",
    "[goldsrc][movement][kernel][step][solid-start][transactional]")
{
    const auto initial = fixture::make_state();
    const auto signature =
        player::local_player_movement_state_signature(initial);

    SECTION("startsolid")
    {
        StepTraceInjectionCollision collision{
            StepTraceInjection::horizontal_startsolid};
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U, 10U, 100.0F), collision);
        REQUIRE_FALSE(result);
        CHECK_FALSE(result.state);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            movement::LocalMovementSimulationErrorCode::player_startsolid);
        CHECK(result.touches.empty());
        CHECK(player::local_player_movement_state_signature(initial) ==
            signature);
    }

    SECTION("allsolid")
    {
        StepTraceInjectionCollision collision{
            StepTraceInjection::horizontal_allsolid};
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U, 10U, 100.0F), collision);
        REQUIRE_FALSE(result);
        CHECK_FALSE(result.state);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            movement::LocalMovementSimulationErrorCode::player_allsolid);
        CHECK(result.touches.empty());
        CHECK(player::local_player_movement_state_signature(initial) ==
            signature);
    }
}

TEST_CASE("Vertical step traces cannot cross unsupported liquid",
    "[goldsrc][movement][kernel][step][liquid][transactional]")
{
    const auto initial = fixture::make_state();
    const auto signature =
        player::local_player_movement_state_signature(initial);

    for (const auto injection : {
             StepTraceInjection::step_up_liquid,
             StepTraceInjection::step_down_liquid}) {
        StepTraceInjectionCollision collision{injection};
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U, 10U, 100.0F), collision);
        REQUIRE_FALSE(result);
        CHECK_FALSE(result.state);
        REQUIRE(result.error);
        CHECK(result.error->code == movement::
            LocalMovementSimulationErrorCode::liquid_movement_unsupported);
        CHECK(result.touches.empty());
        CHECK(player::local_player_movement_state_signature(initial) ==
            signature);
    }
}

} // namespace
