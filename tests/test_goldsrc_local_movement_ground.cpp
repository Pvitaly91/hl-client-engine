#include "local_movement_test_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>

namespace {

namespace fixture = hlclient::tests::local_movement;
namespace assets = hlclient::assets;
namespace local = hlclient::goldsrc::movement;
namespace player = hlclient::movement;

enum class MalformedCollisionOutput {
    invalid_position_status,
    invalid_point_contents,
    non_finite_trace_fraction,
    out_of_range_trace_fraction,
    inconsistent_trace_endpoint,
    mismatched_trace_profile,
    invalid_trace_contents,
    invalid_trace_hit,
    non_unit_trace_plane,
};

class MalformedCollision final : public local::ILocalMovementCollision {
public:
    explicit MalformedCollision(const MalformedCollisionOutput output) noexcept
        : output_{output}
    {
    }

    [[nodiscard]] local::LocalMovementCollisionProfile profile()
        const noexcept override
    {
        return local::LocalMovementCollisionProfile::
            explicit_synthetic_static_brush_v1;
    }

    [[nodiscard]] bool valid() const noexcept override { return true; }

    [[nodiscard]] local::LocalMovementPointContentsQueryResult point_contents(
        const assets::AssetVector3&,
        hlclient::collision::CollisionQueryScratch&,
        const local::LocalMovementCollisionQueryConfig&) const override
    {
        local::LocalMovementPointContents result;
        result.contents = {player::PlayerMovementContents::empty, -1};
        if (output_ == MalformedCollisionOutput::invalid_point_contents) {
            result.contents.category =
                static_cast<player::PlayerMovementContents>(255U);
        }
        return {result, std::nullopt};
    }

    [[nodiscard]] local::LocalMovementPositionQueryResult test_position(
        const assets::AssetVector3&,
        player::PlayerMovementHull,
        hlclient::collision::CollisionQueryScratch&,
        const local::LocalMovementCollisionQueryConfig&) const override
    {
        local::LocalMovementPositionTest result;
        result.contents = {player::PlayerMovementContents::empty, -1};
        if (output_ == MalformedCollisionOutput::invalid_position_status) {
            result.status =
                static_cast<local::LocalMovementPositionStatus>(255U);
        }
        return {result, std::nullopt};
    }

    [[nodiscard]] local::LocalMovementTraceQueryResult trace_hull(
        const assets::AssetVector3& start,
        const assets::AssetVector3& end,
        player::PlayerMovementHull,
        hlclient::collision::CollisionQueryScratch&,
        const local::LocalMovementCollisionQueryConfig&) const override
    {
        local::LocalMovementTrace result;
        result.fraction = 1.0;
        result.end_position = end;
        result.in_open = true;
        result.start_contents = {player::PlayerMovementContents::empty, -1};
        result.end_contents = {player::PlayerMovementContents::empty, -1};
        result.collision_profile = profile();
        switch (output_) {
        case MalformedCollisionOutput::non_finite_trace_fraction:
            result.fraction = std::numeric_limits<double>::quiet_NaN();
            break;
        case MalformedCollisionOutput::out_of_range_trace_fraction:
            result.fraction = 1.5;
            break;
        case MalformedCollisionOutput::inconsistent_trace_endpoint:
            result.end_position.x += 10.0F;
            break;
        case MalformedCollisionOutput::mismatched_trace_profile:
            result.collision_profile =
                local::LocalMovementCollisionProfile::world_only_v1;
            break;
        case MalformedCollisionOutput::invalid_trace_contents:
            result.end_contents.category =
                static_cast<player::PlayerMovementContents>(255U);
            break;
        case MalformedCollisionOutput::invalid_trace_hit:
        case MalformedCollisionOutput::non_unit_trace_plane:
            result.fraction = 0.5;
            result.end_position = {
                start.x + (end.x - start.x) * 0.5F,
                start.y + (end.y - start.y) * 0.5F,
                start.z + (end.z - start.z) * 0.5F};
            result.collision_plane = player::PlayerMovementPlane{
                output_ == MalformedCollisionOutput::non_unit_trace_plane
                    ? assets::AssetVector3{0.0F, 0.0F, 2.0F}
                    : assets::AssetVector3{0.0F, 0.0F, 1.0F},
                0.0,
                std::nullopt};
            result.hit = player::PlayerMovementHitIdentity{
                output_ == MalformedCollisionOutput::invalid_trace_hit
                    ? static_cast<player::PlayerMovementHitKind>(255U)
                    : player::PlayerMovementHitKind::
                        explicit_synthetic_brush,
                1U,
                1U,
                std::nullopt};
            result.blocking_contents =
                local::LocalMovementCollisionContents{
                    player::PlayerMovementContents::solid, -2};
            break;
        case MalformedCollisionOutput::invalid_position_status:
        case MalformedCollisionOutput::invalid_point_contents:
            break;
        }
        return {result, std::nullopt};
    }

private:
    MalformedCollisionOutput output_;
};

TEST_CASE("A stationary standing player remains grounded on the fixture floor",
    "[goldsrc][movement][kernel][ground]")
{
    fixture::DeterministicLocalMovementCollision collision;
    const auto result = fixture::simulate(
        fixture::make_state(), fixture::make_command(1U), collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->origin().x == 0.0F);
    CHECK(result.state->origin().y == 0.0F);
    CHECK(result.state->origin().z == 36.0F);
    CHECK(result.state->velocity().x == 0.0F);
    CHECK(result.state->mode() == player::PlayerMovementMode::walking);
    CHECK(result.state->ground_state().grounded());
    CHECK(result.state->ground_state().walkable());
    CHECK(result.state->ground_state().plane().normal.z == 1.0F);
    CHECK(result.statistics.ground_probe_count >= 2U);
}

TEST_CASE("Ground wish acceleration follows command yaw and literal dt",
    "[goldsrc][movement][kernel][ground][acceleration]")
{
    fixture::DeterministicLocalMovementCollision collision;

    SECTION("yaw zero accelerates along positive X")
    {
        const auto result = fixture::simulate(
            fixture::make_state(),
            fixture::make_command(1U, 10U, 100.0F), collision);
        REQUIRE(result);
        REQUIRE(result.state);
        CHECK(result.state->velocity().x == Catch::Approx(10.0F));
        CHECK(result.state->velocity().y == Catch::Approx(0.0F));
        CHECK(result.state->origin().x == Catch::Approx(0.1F));
        CHECK(result.statistics.grounded_command_count == 1U);
    }

    SECTION("yaw ninety accelerates along positive Y")
    {
        const auto result = fixture::simulate(
            fixture::make_state(),
            fixture::make_command(1U, 10U, 100.0F, 0.0F, 0U, 90.0F),
            collision);
        REQUIRE(result);
        REQUIRE(result.state);
        CHECK(result.state->velocity().x == Catch::Approx(0.0F).margin(1.0e-5F));
        CHECK(result.state->velocity().y == Catch::Approx(10.0F));
        CHECK(result.state->origin().y == Catch::Approx(0.1F));
    }
}

TEST_CASE("Ground friction is horizontal and preserves a grounded Z component",
    "[goldsrc][movement][kernel][ground][friction]")
{
    fixture::DeterministicLocalMovementCollision collision;
    const auto result = fixture::simulate(
        fixture::make_state({0.0F, 0.0F, 36.0F}, {100.0F, 0.0F, 0.0F}),
        fixture::make_command(1U), collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->velocity().x == Catch::Approx(96.0F));
    CHECK(result.state->velocity().y == 0.0F);
    CHECK(result.state->velocity().z == 0.0F);
    CHECK(result.state->origin().x == Catch::Approx(0.96F));
}

TEST_CASE("State friction multiplier participates in friction and acceleration",
    "[goldsrc][movement][kernel][ground][friction][multiplier]")
{
    fixture::DeterministicLocalMovementCollision collision;

    SECTION("friction drop")
    {
        const auto result = fixture::simulate(
            fixture::make_state(
                {0.0F, 0.0F, 36.0F}, {100.0F, 0.0F, 0.0F},
                player::PlayerMovementMode::walking,
                player::PlayerMovementHull::standing, 0U, 0U, 1.0F, 0.5F),
            fixture::make_command(1U), collision);
        REQUIRE(result);
        REQUIRE(result.state);
        CHECK(result.state->velocity().x == Catch::Approx(98.0F));
        CHECK(result.state->friction_multiplier() == 0.5F);
    }

    SECTION("acceleration scale")
    {
        const auto result = fixture::simulate(
            fixture::make_state(
                {0.0F, 0.0F, 36.0F}, {},
                player::PlayerMovementMode::walking,
                player::PlayerMovementHull::standing, 0U, 0U, 1.0F, 0.5F),
            fixture::make_command(1U, 10U, 100.0F), collision);
        REQUIRE(result);
        REQUIRE(result.state);
        CHECK(result.state->velocity().x == Catch::Approx(5.0F));
        CHECK(result.state->origin().x == Catch::Approx(0.05F));
    }
}

TEST_CASE("Wish speed is capped by the movement environment maximum",
    "[goldsrc][movement][kernel][ground][maximum-speed]")
{
    fixture::DeterministicLocalMovementCollision collision;
    const auto result = fixture::simulate(
        fixture::make_state(),
        fixture::make_command(1U, 10U, 1'000.0F), collision);

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->velocity().x == Catch::Approx(32.0F));
    CHECK(result.state->origin().x == Catch::Approx(0.32F));
}

TEST_CASE("Ground state and output signatures are repeatable",
    "[goldsrc][movement][kernel][ground][determinism]")
{
    fixture::DeterministicLocalMovementCollision first_collision;
    fixture::DeterministicLocalMovementCollision second_collision;
    const auto initial = fixture::make_state();
    const auto command = fixture::make_command(1U, 30U, 240.0F, -80.0F);
    const auto first = fixture::simulate(initial, command, first_collision);
    const auto second = fixture::simulate(initial, command, second_collision);

    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first.state);
    REQUIRE(second.state);
    CHECK(first.deterministic_state_signature ==
        second.deterministic_state_signature);
    CHECK(first.deterministic_state_signature ==
        player::local_player_movement_state_signature(*first.state));
    CHECK(first.state->origin().x == second.state->origin().x);
    CHECK(first.state->origin().y == second.state->origin().y);
    CHECK(first.state->origin().z == second.state->origin().z);
    CHECK(first.state->velocity().x == second.state->velocity().x);
    CHECK(first.state->velocity().y == second.state->velocity().y);
    CHECK(first.state->velocity().z == second.state->velocity().z);
    CHECK(first.statistics.substep_count == second.statistics.substep_count);
}

TEST_CASE("Kernel rejects malformed collision-provider results transactionally",
    "[goldsrc][movement][kernel][ground][security][transactional]")
{
    constexpr std::array malformed_outputs{
        MalformedCollisionOutput::invalid_position_status,
        MalformedCollisionOutput::invalid_point_contents,
        MalformedCollisionOutput::non_finite_trace_fraction,
        MalformedCollisionOutput::out_of_range_trace_fraction,
        MalformedCollisionOutput::inconsistent_trace_endpoint,
        MalformedCollisionOutput::mismatched_trace_profile,
        MalformedCollisionOutput::invalid_trace_contents,
        MalformedCollisionOutput::invalid_trace_hit,
        MalformedCollisionOutput::non_unit_trace_plane,
    };
    for (const auto output : malformed_outputs) {
        const auto previous = fixture::make_state();
        const auto signature =
            player::local_player_movement_state_signature(previous);
        const MalformedCollision collision{output};
        const auto result = fixture::simulate(
            previous, fixture::make_command(1U), collision);

        CHECK_FALSE(result);
        CHECK_FALSE(result.state);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            local::LocalMovementSimulationErrorCode::collision_query_failed);
        REQUIRE(result.error->collision_error);
        CHECK(result.error->collision_error->code ==
            local::LocalMovementCollisionErrorCode::non_finite_result);
        CHECK(result.touches.empty());
        CHECK(player::local_player_movement_state_signature(previous) ==
            signature);
    }
}

TEST_CASE("Executable dry-walk profile closes the walkable slope threshold",
    "[goldsrc][movement][kernel][ground][configuration]")
{
    fixture::DeterministicLocalMovementCollision collision;
    local::GoldSrcLocalMovementConfig config;
    config.minimum_walkable_normal_z = 0.5;
    const auto result = fixture::simulate(
        fixture::make_state(), fixture::make_command(1U), collision, config);

    REQUIRE_FALSE(result);
    CHECK_FALSE(result.state);
    REQUIRE(result.error);
    CHECK(result.error->code ==
        local::LocalMovementSimulationErrorCode::invalid_configuration);
}

} // namespace
