#include "local_movement_test_fixture.hpp"
#include "move_vars_test_fixture.hpp"

#include <hlclient/collision/collision_contents.hpp>
#include <hlclient/collision/collision_world_package.hpp>
#include <hlclient/goldsrc/movement/local_movement_collision.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace {

namespace fixture = hlclient::tests::local_movement;
namespace collision = hlclient::collision;
namespace movement = hlclient::goldsrc::movement;
namespace player = hlclient::movement;
using Vector = hlclient::assets::AssetVector3;

[[nodiscard]] collision::CollisionContents contents(const std::int32_t raw)
{
    const auto result = collision::decode_goldsrc_contents({raw});
    REQUIRE(result);
    return *result;
}

// Project-owned, already expanded hull trees. The player hull is selected once
// by the production adapter; no installed map or extra Minkowski expansion is
// involved. The positive side of the supplied plane is empty.
[[nodiscard]] std::shared_ptr<const collision::CollisionWorldPackage>
plane_world(const std::vector<Vector>& normals)
{
    std::array<collision::CollisionHull, collision::kCollisionHullCount> hulls{};
    for (std::size_t index = 0U; index < hulls.size(); ++index) {
        const auto ordinal = collision::collision_hull_ordinal(index);
        REQUIRE(ordinal);
        const auto profile = collision::standard_collision_hull_profile(*ordinal);
        REQUIRE(profile);
        hulls[index] = collision::CollisionHull{
            *ordinal,
            index == 0U ? collision::CollisionHullTreeDomain::node_leaf
                        : collision::CollisionHullTreeDomain::clipnode,
            collision::CollisionHullRoot{
                index == 0U ? collision::CollisionHullRootKind::node
                            : collision::CollisionHullRootKind::clipnode,
                0U, contents(-1)},
            *profile};
    }
    const collision::CollisionModel model{
        0U, {}, {{-1024.0F, -1024.0F, -1024.0F},
                    {1024.0F, 1024.0F, 1024.0F}},
        0U, 1U, hulls};
    REQUIRE_FALSE(normals.empty());
    std::vector<collision::CollisionPlane> planes;
    std::vector<collision::CollisionNode> nodes;
    std::vector<collision::CollisionClipnode> clipnodes;
    for (std::size_t index = 0U; index < normals.size(); ++index) {
        const auto ordinal = static_cast<std::uint32_t>(index);
        const bool last = index + 1U == normals.size();
        planes.push_back({normals[index], 0.0, 77U + ordinal, 3});
        nodes.push_back({ordinal,
            {collision::CollisionNodeChild{
                 last ? collision::CollisionNodeChildKind::leaf
                      : collision::CollisionNodeChildKind::node,
                 last ? 1U : ordinal + 1U},
                collision::CollisionNodeChild{
                    collision::CollisionNodeChildKind::leaf, 0U}}});
        clipnodes.push_back({ordinal,
            {collision::CollisionClipnodeChild{
                 last ? collision::CollisionClipnodeChildKind::terminal
                      : collision::CollisionClipnodeChildKind::clipnode,
                 last ? 0U : ordinal + 1U, contents(-1)},
                collision::CollisionClipnodeChild{
                    collision::CollisionClipnodeChildKind::terminal,
                    0U, contents(-2)}}});
    }
    return std::make_shared<const collision::CollisionWorldPackage>(
        std::move(planes), std::move(nodes),
        std::vector<collision::CollisionLeaf>{
            {0U, contents(-2)}, {1U, contents(-1)}},
        std::move(clipnodes),
        std::vector<collision::CollisionModel>{model});
}

[[nodiscard]] std::shared_ptr<const collision::CollisionWorldPackage>
one_plane_world(const Vector normal)
{
    return plane_world({normal});
}

[[nodiscard]] double precise_dot(const Vector velocity, const Vector normal)
{
    return static_cast<double>(velocity.x) * normal.x +
        static_cast<double>(velocity.y) * normal.y +
        static_cast<double>(velocity.z) * normal.z;
}

[[nodiscard]] player::LocalPlayerMovementState reference_air_state(
    const Vector origin, const Vector velocity)
{
    auto info = player::local_player_movement_state_create_info(
        fixture::make_state(origin, velocity, player::PlayerMovementMode::airborne));
    info.command_profile = player::GoldSrcMovementCommandProfile::reference_wire_jump_duck_v2;
    info.compatibility_profile = player::GoldSrcMovementCompatibilityProfile::
        public_valve_pm_shared_dry_actions_subset_v2;
    const auto created = player::LocalPlayerMovementState::create(info);
    REQUIRE(created);
    return *created.state;
}

[[nodiscard]] hlclient::goldsrc::GoldSrcUserCmdState reference_command(
    const std::uint32_t sequence)
{
    namespace goldsrc = hlclient::goldsrc;
    const auto identity = goldsrc::GoldSrcUserCmdSequence::create(sequence);
    REQUIRE(identity);
    auto info = goldsrc::goldsrc_usercmd_default_create_info(*identity);
    info.msec = 10U;
    info.sample_duration_nanoseconds = 10'000'000U;
    info.compatibility_profile = goldsrc::GoldSrcUserCmdCompatibilityProfile::
        public_goldsrc48_jump_duck_prediction_v2;
    info.input_mapping_profile = goldsrc::GoldSrcUserCmdInputMappingProfile::
        reference_immutable_wire_v1;
    info.schema_binding_profile = goldsrc::GoldSrcUserCmdSchemaBindingProfile::
        public_goldsrc48_usercmd_schema_v1;
    const auto created = goldsrc::GoldSrcUserCmdState::create(info);
    REQUIRE(created);
    return *created.state;
}

void require_free(const movement::ILocalMovementCollision& world,
    const player::LocalPlayerMovementState& state,
    movement::GoldSrcLocalMovementScratch& scratch,
    const movement::GoldSrcLocalMovementConfig& config)
{
    const auto tested = world.test_position(state.origin(), state.hull(),
        scratch.collision, config.collision_query);
    REQUIRE(tested);
    REQUIRE(tested.result);
    CHECK(tested.result->status == movement::LocalMovementPositionStatus::free);
}

TEST_CASE("Normal reference oblique wall projection preserves an independent tangent",
    "[goldsrc][movement][wall-contact][e32][tangent-rounding][production-query]")
{
    // These two normal components occur in the retained incident, but the wall
    // orientation, seed and no-input command here are synthetic controls, not
    // a historical replay. In float, clipping (-12.25,-2) gives (-12,1), whose
    // plane dot remains -5.96046448e-8 even after a second identical clip.
    constexpr float a = 0.08304548263549805F;
    constexpr float b = 0.9965457320213318F;
    for (const bool swap_axes : {false, true}) {
        for (const float sign : {-1.0F, 1.0F}) {
            for (const float speed_scale : {0.5F, 1.0F, 2.0F}) {
                CAPTURE(swap_axes, sign, speed_scale);
                const Vector normal = swap_axes
                    ? Vector{sign * b, sign * a, 0.0F}
                    : Vector{sign * a, sign * b, 0.0F};
                const Vector start = swap_axes
                    ? Vector{sign * 0.01F / b, 0.0F, 100.0F}
                    : Vector{0.0F, sign * 0.01F / b, 100.0F};
                const Vector velocity = swap_axes
                    ? Vector{sign * -2.0F * speed_scale,
                          sign * -12.25F * speed_scale,
                          330.0F * speed_scale + 4.0F}
                    : Vector{sign * -12.25F * speed_scale,
                          sign * -2.0F * speed_scale,
                          330.0F * speed_scale + 4.0F};
                movement::WorldOnlyMovementCollision world{
                    one_plane_world(normal)};
                movement::GoldSrcLocalMovementScratch scratch;
                movement::GoldSrcLocalMovementConfig config;
                const auto environment = fixture::make_environment();
                const auto initial = reference_air_state(start, velocity);
                require_free(world, initial, scratch, config);
                const auto result = movement::GoldSrcLocalMovementKernel::simulate(
                    initial, reference_command(1U), environment,
                    world, scratch, config);
                REQUIRE(result);
                REQUIRE(result.state);
                REQUIRE(result.statistics.collision_hit_count > 0U);
                REQUIRE_FALSE(result.touches.empty());
                const auto expected_z = 330.0F * speed_scale - 4.0F;
                CHECK(result.state->velocity().z == Catch::Approx(expected_z));
                CHECK(std::hypot(result.state->velocity().x,
                          result.state->velocity().y) > 5.0F * speed_scale);
                CHECK(result.state->origin().z > start.z + speed_scale);
                CHECK(movement::movement_dot(result.state->velocity(), normal)
                    >= 0.0F);
                CHECK(precise_dot(result.state->velocity(), normal) >= 0.0);
                require_free(world, *result.state, scratch, config);
                std::optional<player::LocalPlayerMovementState> current{
                    *result.state};
                for (std::uint32_t sequence = 2U; sequence <= 4U; ++sequence) {
                    const auto previous_z = current->origin().z;
                    const auto next = movement::GoldSrcLocalMovementKernel::simulate(
                        *current, reference_command(sequence), environment,
                        world, scratch, config);
                    REQUIRE(next);
                    REQUIRE(next.state);
                    CHECK(std::hypot(next.state->velocity().x,
                              next.state->velocity().y) > 5.0F * speed_scale);
                    CHECK(next.state->origin().z > previous_z + speed_scale);
                    CHECK(precise_dot(next.state->velocity(), normal) >= 0.0);
                    require_free(world, *next.state, scratch, config);
                    current.emplace(*next.state);
                }
            }
        }
    }
}

TEST_CASE("A synthetic slope keeps horizontal tangent at the recorded normal",
    "[goldsrc][movement][wall-contact][e32][tangent-rounding][production-query]")
{
    constexpr Vector normal{
        0.08304548263549805F, 0.0F, 0.9965457320213318F};
    movement::WorldOnlyMovementCollision world{one_plane_world(normal)};
    movement::GoldSrcLocalMovementScratch scratch;
    movement::GoldSrcLocalMovementConfig config;
    // Keep this synthetic airborne approach outside its ground probe until the
    // real sweep hits. This changes no collision or velocity tolerance.
    config.ground_probe_distance = 0.001;
    const auto environment = fixture::make_environment();
    const auto initial = fixture::make_state(
        {0.0F, 0.0F, 0.01F / normal.z}, {-12.25F, 330.25F, 2.0F},
        player::PlayerMovementMode::airborne);
    require_free(world, initial, scratch, config);
    const auto result = movement::GoldSrcLocalMovementKernel::simulate(
        initial, fixture::make_command(1U), environment, world, scratch, config);
    REQUIRE(result);
    REQUIRE(result.state);
    REQUIRE(result.statistics.collision_hit_count > 0U);
    CHECK(result.state->velocity().y == 330.25F);
    CHECK(result.state->origin().y > 3.0F);
    require_free(world, *result.state, scratch, config);
}

TEST_CASE("Two production oblique planes preserve the feasible crease",
    "[goldsrc][movement][wall-contact][e32][tangent-rounding][production-query]")
{
    constexpr Vector first{0.70710677F, 0.70710677F, 0.0F};
    constexpr Vector second{0.08304548263549805F, 0.9965457320213318F, 0.0F};
    movement::WorldOnlyMovementCollision world{plane_world({first, second})};
    movement::GoldSrcLocalMovementScratch scratch;
    const movement::GoldSrcLocalMovementConfig config;
    const auto environment = fixture::make_environment();
    const double sum = 0.05 / first.x;
    const auto x = static_cast<float>((sum * second.y - 0.01) /
        (second.y - second.x));
    const auto y = static_cast<float>(sum - x);
    const auto initial = reference_air_state(
        {x, y, 100.0F}, {-12.25F, -2.0F, 334.0F});
    require_free(world, initial, scratch, config);
    const auto result = movement::GoldSrcLocalMovementKernel::simulate(
        initial, reference_command(1U), environment, world, scratch, config);
    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.statistics.clip_plane_count >= 2U);
    CHECK(result.state->velocity().z == 326.0F);
    CHECK(precise_dot(result.state->velocity(), first) >= 0.0);
    CHECK(precise_dot(result.state->velocity(), second) >= 0.0);
    require_free(world, *result.state, scratch, config);
}

TEST_CASE("Velocity rounding repair respects a low per-axis MoveVars bound",
    "[goldsrc][movement][wall-contact][e32][tangent-rounding][production-query]")
{
    namespace move_vars = hlclient::test::move_vars_fixture;
    move_vars::Values values;
    values.maximum_velocity = 16.0F;
    const auto parsed = hlclient::goldsrc::MoveVarsParser{}.parse(
        move_vars::move_vars_message(values), 0U);
    REQUIRE(parsed);
    const auto built = movement::GoldSrcMovementEnvironmentBuilder::from_move_vars(
        *parsed.state);
    REQUIRE(built);
    constexpr Vector normal{0.08304548263549805F, 0.9965457320213318F, 0.0F};
    movement::WorldOnlyMovementCollision world{one_plane_world(normal)};
    movement::GoldSrcLocalMovementScratch scratch;
    const movement::GoldSrcLocalMovementConfig config;
    const auto initial = reference_air_state(
        {0.0F, 0.01F / normal.y, 100.0F}, {-12.25F, -2.0F, 20.0F});
    const auto result = movement::GoldSrcLocalMovementKernel::simulate(
        initial, reference_command(1U), *built.environment,
        world, scratch, config);
    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->velocity().z == 12.0F);
    CHECK(std::abs(result.state->velocity().x) <= values.maximum_velocity);
    CHECK(std::abs(result.state->velocity().y) <= values.maximum_velocity);
    CHECK(precise_dot(result.state->velocity(), normal) >= 0.0);
    require_free(world, *result.state, scratch, config);
}

TEST_CASE("Selected slide stop diagnostics exclude discarded step candidates",
    "[goldsrc][movement][wall-contact][e32][tangent-rounding][diagnostics]")
{
    const auto environment = fixture::make_environment();
    const movement::GoldSrcLocalMovementConfig config;
    SECTION("Disabled capture owns no event; enabled capture owns the actual sweep") {
        for (const bool enabled : {false, true}) {
            CAPTURE(enabled);
            movement::WorldOnlyMovementCollision world{
                one_plane_world({1.0F, 0.0F, 0.0F})};
            movement::GoldSrcLocalMovementScratch scratch;
            scratch.capture_velocity_stops = enabled;
            const auto initial = fixture::make_state(
                {0.1F, 0.0F, 100.0F}, {-100.0F, 0.0F, 4.0F},
                player::PlayerMovementMode::airborne);
            const auto result = movement::GoldSrcLocalMovementKernel::simulate(
                initial, fixture::make_command(1U), environment,
                world, scratch, config);
            REQUIRE(result);
            CHECK(scratch.first_velocity_stop.has_value() == enabled);
            if (enabled) {
                const auto& event = *scratch.first_velocity_stop;
                CHECK(event.command_sequence == 1U);
                CHECK(event.command_msec == 10U);
                CHECK(event.simulation_start_ns == 0U);
                CHECK(event.simulation_end_ns == 10'000'000U);
                CHECK(event.candidate == movement::VelocityStopCandidate::air);
                CHECK(event.before_velocity.x == -100.0F);
                CHECK(event.after_velocity.x == 0.0F);
                CHECK(event.after_velocity.y == 0.0F);
                CHECK(event.after_velocity.z == 0.0F);
                REQUIRE(event.trace.collision_plane);
                CHECK(event.trace.collision_plane->normal.x == 1.0F);
                CHECK(event.query_start.x == initial.origin().x);
                CHECK(event.query_end.x < event.query_start.x);
                CHECK(event.clip_plane_count == 1U);
            }
        }
    }
    SECTION("A successful step discards the stopped direct candidate") {
        fixture::DeterministicLocalMovementCollision world;
        world.add_step(20.0F, 60.0F, -64.0F, 64.0F, 8.0F);
        movement::GoldSrcLocalMovementScratch scratch;
        scratch.capture_velocity_stops = true;
        const auto initial = fixture::make_state(
            {0.0F, 0.0F, 36.0F}, {320.0F, 0.0F, 0.0F});
        const auto result = movement::GoldSrcLocalMovementKernel::simulate(
            initial, fixture::make_command(1U, 20U, 320.0F),
            environment, world, scratch, config);
        REQUIRE(result);
        REQUIRE(result.state);
        REQUIRE(result.statistics.step_success_count > 0U);
        CHECK(result.state->velocity().x > 300.0F);
        CHECK(result.state->origin().x > 4.0F);
        CHECK_FALSE(scratch.first_velocity_stop);
        require_free(world, *result.state, scratch, config);
    }
}

TEST_CASE("Real axial wall and opposing motion retain a physical stop",
    "[goldsrc][movement][wall-contact][e32][tangent-rounding][negative-control]")
{
    SECTION("An axial wall removes the entering component") {
        movement::WorldOnlyMovementCollision world{
            one_plane_world({1.0F, 0.0F, 0.0F})};
        movement::GoldSrcLocalMovementScratch scratch;
        const movement::GoldSrcLocalMovementConfig config;
        const auto environment = fixture::make_environment();
        const auto initial = fixture::make_state(
            {0.1F, 0.0F, 100.0F}, {-100.0F, 0.0F, 4.0F},
            player::PlayerMovementMode::airborne);
        const auto result = movement::GoldSrcLocalMovementKernel::simulate(
            initial, fixture::make_command(1U), environment, world, scratch, config);
        REQUIRE(result);
        REQUIRE(result.state);
        CHECK(result.state->velocity().x == 0.0F);
        CHECK(result.state->velocity().y == 0.0F);
        require_free(world, *result.state, scratch, config);
    }
    SECTION("Two real walls remove both horizontal components") {
        fixture::DeterministicLocalMovementCollision world{false};
        world.add_positive_x_wall(16.1F);
        world.add_positive_y_wall(16.1F);
        movement::GoldSrcLocalMovementScratch scratch;
        const movement::GoldSrcLocalMovementConfig config;
        const auto environment = fixture::make_environment();
        const auto initial = fixture::make_state(
            {0.0F, 0.0F, 100.0F}, {100.0F, 100.0F, 4.0F},
            player::PlayerMovementMode::airborne);
        const auto result = movement::GoldSrcLocalMovementKernel::simulate(
            initial, fixture::make_command(1U), environment, world, scratch, config);
        REQUIRE(result);
        REQUIRE(result.state);
        CHECK(result.state->velocity().x == 0.0F);
        CHECK(result.state->velocity().y == 0.0F);
        require_free(world, *result.state, scratch, config);
    }
}

} // namespace
