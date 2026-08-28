#include "local_movement_test_fixture.hpp"
#include "literal_movement_bsp_fixture.hpp"

#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <hlclient/goldsrc/movement/local_movement_collision.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

namespace fixture = hlclient::tests::local_movement;
namespace literal = hlclient::tests::literal_movement_bsp;
namespace goldsrc = hlclient::goldsrc;
namespace goldsrc_bsp = hlclient::goldsrc::bsp;
namespace goldsrc_collision = hlclient::goldsrc::collision;
namespace movement = hlclient::goldsrc::movement;
namespace player = hlclient::movement;

[[nodiscard]] std::shared_ptr<const hlclient::collision::CollisionWorldPackage>
literal_world_package()
{
    const auto parsed = goldsrc_bsp::GoldSrcBspParser::parse(
        literal::make_bsp_v30());
    INFO((parsed.error ? parsed.error->context : std::string{}));
    REQUIRE(parsed);
    REQUIRE(parsed.document);
    const auto built = goldsrc_collision::GoldSrcCollisionWorldBuilder::build(
        parsed.document->collision_source);
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.package);
    return built.package;
}

enum class RepeatedCampaign : std::uint8_t {
    settle,
    walk_slide,
    jump_land,
    step,
    duck_stand,
};

struct RepeatedCampaignSummary {
    std::uint64_t signature{0U};
    std::uint64_t trace_count{0U};
    std::uint64_t collision_count{0U};
    std::uint64_t step_count{0U};
    std::uint64_t jump_count{0U};
    std::uint64_t duck_enter_count{0U};
    std::uint64_t duck_exit_count{0U};
    std::uint64_t start_solid_count{0U};
    std::uint64_t all_solid_count{0U};
    std::uint32_t command_count{0U};
    bool grounded{false};
    player::PlayerMovementHull hull{player::PlayerMovementHull::standing};

    [[nodiscard]] friend bool operator==(
        const RepeatedCampaignSummary&,
        const RepeatedCampaignSummary&) = default;
};

struct LiteralKernelCampaign {
    std::optional<player::LocalPlayerMovementState> state;
    std::vector<player::PlayerMovementTouch> touches;
    std::optional<movement::LocalMovementSimulationErrorCode> error;
    std::uint32_t failure_sequence{0U};
    hlclient::assets::AssetVector3 last_origin{};
    std::uint64_t collision_count{0U};
    std::uint64_t step_attempt_count{0U};
    std::uint64_t step_success_count{0U};
    std::uint64_t jump_count{0U};
    std::uint64_t duck_enter_count{0U};
    std::uint64_t duck_exit_count{0U};
    std::uint64_t stand_blocked_count{0U};
    std::uint64_t start_solid_count{0U};
    std::uint64_t all_solid_count{0U};
};

[[nodiscard]] LiteralKernelCampaign run_literal_commands(
    player::LocalPlayerMovementState initial,
    const movement::WorldOnlyMovementCollision& collision,
    const std::span<const goldsrc::GoldSrcUserCmdState> commands)
{
    LiteralKernelCampaign campaign;
    campaign.state.emplace(std::move(initial));
    campaign.last_origin = campaign.state->origin();
    for (const auto& command : commands) {
        auto simulated = fixture::simulate(*campaign.state, command, collision);
        campaign.collision_count += simulated.statistics.collision_hit_count;
        campaign.step_attempt_count += simulated.statistics.step_attempt_count;
        campaign.step_success_count += simulated.statistics.step_success_count;
        campaign.jump_count += simulated.statistics.jump_count;
        campaign.duck_enter_count += simulated.statistics.duck_enter_count;
        campaign.duck_exit_count += simulated.statistics.duck_exit_count;
        campaign.stand_blocked_count += simulated.statistics.stand_blocked_count;
        campaign.start_solid_count += simulated.statistics.start_solid_count;
        campaign.all_solid_count += simulated.statistics.all_solid_count;
        campaign.touches.insert(
            campaign.touches.end(), simulated.touches.begin(),
            simulated.touches.end());
        if (!simulated || !simulated.state) {
            if (simulated.error) {
                campaign.error = simulated.error->code;
            }
            campaign.failure_sequence = simulated.command_sequence;
            campaign.state.reset();
            return campaign;
        }
        campaign.state.emplace(std::move(*simulated.state));
        campaign.last_origin = campaign.state->origin();
    }
    return campaign;
}

[[nodiscard]] std::vector<goldsrc::GoldSrcUserCmdState> literal_commands(
    const std::uint32_t count,
    const float forward,
    const float side = 0.0F,
    const std::uint16_t buttons = 0U,
    const float yaw = 0.0F)
{
    std::vector<goldsrc::GoldSrcUserCmdState> commands;
    commands.reserve(count);
    for (std::uint32_t sequence = 1U; sequence <= count; ++sequence) {
        commands.push_back(fixture::make_command(
            sequence, 10U, forward, side, buttons, yaw));
    }
    return commands;
}

TEST_CASE("A literal movement BSP owns distinct point standing and duck trees",
    "[goldsrc][movement][integration][literal-bsp][collision]")
{
    const auto bytes = literal::make_bsp_v30();
    REQUIRE(bytes.size() > hlclient::tests::kSyntheticBspHeaderSize);
    const auto parsed = goldsrc_bsp::GoldSrcBspParser::parse(bytes);
    INFO((parsed.error ? parsed.error->context : std::string{}));
    REQUIRE(parsed);
    REQUIRE(parsed.document);

    const auto& source = parsed.document->collision_source;
    REQUIRE(source.models.size() == 1U);
    CHECK(source.planes.size() > 100U);
    CHECK(source.nodes.size() > 40U);
    CHECK(source.clipnodes.size() > 80U);
    CHECK(source.leaves.size() == 3U);
    CHECK(source.leaves[0U].contents.value == -1);
    CHECK(source.leaves[1U].contents.value == -2);
    CHECK(source.leaves[2U].contents.value == -3);

    const auto& model = source.models[0U];
    REQUIRE(std::holds_alternative<
        goldsrc_bsp::GoldSrcBspCollisionSourceClipnodeReference>(
        model.standing_hull.root));
    REQUIRE(std::holds_alternative<
        goldsrc_bsp::GoldSrcBspCollisionSourceClipnodeReference>(
        model.duck_hull.root));
    const auto standing_root = std::get<
        goldsrc_bsp::GoldSrcBspCollisionSourceClipnodeReference>(
        model.standing_hull.root).source_clipnode_index;
    const auto duck_root = std::get<
        goldsrc_bsp::GoldSrcBspCollisionSourceClipnodeReference>(
        model.duck_hull.root).source_clipnode_index;
    CHECK(standing_root != duck_root);

    const auto built = goldsrc_collision::GoldSrcCollisionWorldBuilder::build(
        source);
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.package);
    CHECK(built.package->models().size() == 1U);
}

TEST_CASE("World-only queries hit every literal movement BSP feature",
    "[goldsrc][movement][integration][literal-bsp][world-only]")
{
    const movement::WorldOnlyMovementCollision collision{
        literal_world_package()};
    hlclient::collision::CollisionQueryScratch scratch;
    REQUIRE(collision.valid());

    const auto open = collision.point_contents({0.0F, 0.0F, 36.0F}, scratch);
    REQUIRE(open);
    CHECK(open.result->contents.category ==
        player::PlayerMovementContents::empty);

    const auto floor_contents = collision.point_contents(
        {0.0F, 0.0F, -1.0F}, scratch);
    REQUIRE(floor_contents);
    CHECK(floor_contents.result->contents.category ==
        player::PlayerMovementContents::solid);

    const auto water = collision.point_contents(
        {-112.0F, 80.0F, 16.0F}, scratch);
    REQUIRE(water);
    CHECK(water.result->contents.category ==
        player::PlayerMovementContents::water);
    CHECK(water.result->contents.source_goldsrc_code == -3);

    const auto corner = collision.point_contents(
        {100.0F, 132.0F, 40.0F}, scratch);
    REQUIRE(corner);
    CHECK(corner.result->contents.category ==
        player::PlayerMovementContents::solid);

    const auto standing_floor = collision.trace_hull(
        {0.0F, 0.0F, 60.0F}, {0.0F, 0.0F, 20.0F},
        player::PlayerMovementHull::standing, scratch);
    REQUIRE(standing_floor);
    REQUIRE(standing_floor.result->collision_plane);
    CHECK(standing_floor.result->fraction == Catch::Approx(0.6));
    CHECK(standing_floor.result->end_position.z == Catch::Approx(36.0F));
    CHECK(standing_floor.result->collision_plane->normal.z ==
        Catch::Approx(1.0F));

    const auto duck_floor = collision.trace_hull(
        {0.0F, 0.0F, 60.0F}, {0.0F, 0.0F, 0.0F},
        player::PlayerMovementHull::ducked, scratch);
    REQUIRE(duck_floor);
    CHECK(duck_floor.result->fraction == Catch::Approx(0.7));
    CHECK(duck_floor.result->end_position.z == Catch::Approx(18.0F));

    const auto ceiling = collision.trace_hull(
        {0.0F, 0.0F, 60.0F}, {0.0F, 0.0F, 110.0F},
        player::PlayerMovementHull::standing, scratch);
    REQUIRE(ceiling);
    REQUIRE(ceiling.result->collision_plane);
    CHECK(ceiling.result->fraction == Catch::Approx(0.64));
    CHECK(ceiling.result->end_position.z == Catch::Approx(92.0F));
    CHECK(ceiling.result->collision_plane->normal.z ==
        Catch::Approx(-1.0F));

    const auto wall = collision.trace_hull(
        {140.0F, 0.0F, 36.0F}, {190.0F, 0.0F, 36.0F},
        player::PlayerMovementHull::standing, scratch);
    REQUIRE(wall);
    REQUIRE(wall.result->collision_plane);
    CHECK(wall.result->fraction == Catch::Approx(0.72));
    CHECK(wall.result->end_position.x == Catch::Approx(176.0F));
    CHECK(wall.result->collision_plane->normal.x == Catch::Approx(-1.0F));

    const auto corner_leg = collision.trace_hull(
        {100.0F, 100.0F, 36.0F}, {100.0F, 140.0F, 36.0F},
        player::PlayerMovementHull::standing, scratch);
    REQUIRE(corner_leg);
    REQUIRE(corner_leg.result->collision_plane);
    CHECK(corner_leg.result->fraction == Catch::Approx(0.3));
    CHECK(corner_leg.result->end_position.y == Catch::Approx(112.0F));
    CHECK(corner_leg.result->collision_plane->normal.y ==
        Catch::Approx(-1.0F));

    const auto valid_step = collision.trace_hull(
        {0.0F, 0.0F, 36.0F}, {32.0F, 0.0F, 36.0F},
        player::PlayerMovementHull::standing, scratch);
    REQUIRE(valid_step);
    REQUIRE(valid_step.result->collision_plane);
    CHECK(valid_step.result->fraction == Catch::Approx(0.5));
    CHECK(valid_step.result->end_position.x == Catch::Approx(16.0F));
    CHECK(valid_step.result->collision_plane->normal.x ==
        Catch::Approx(-1.0F));

    const auto above_valid_step = collision.trace_hull(
        {0.0F, 0.0F, 49.0F}, {90.0F, 0.0F, 49.0F},
        player::PlayerMovementHull::standing, scratch);
    REQUIRE(above_valid_step);
    CHECK(above_valid_step.result->fraction == Catch::Approx(1.0));

    const auto high_step = collision.trace_hull(
        {0.0F, 40.0F, 36.0F}, {64.0F, 40.0F, 36.0F},
        player::PlayerMovementHull::standing, scratch);
    REQUIRE(high_step);
    CHECK(high_step.result->fraction == Catch::Approx(0.25));
    CHECK(high_step.result->end_position.x == Catch::Approx(16.0F));

    const auto walkable_ramp = collision.trace_hull(
        {-72.0F, -90.0F, 80.0F}, {-72.0F, -90.0F, 40.0F},
        player::PlayerMovementHull::standing, scratch);
    REQUIRE(walkable_ramp);
    REQUIRE(walkable_ramp.result->collision_plane);
    CHECK(walkable_ramp.result->end_position.z == Catch::Approx(56.0F));
    CHECK(walkable_ramp.result->collision_plane->normal.z ==
        Catch::Approx(0.89442719F));
    CHECK(walkable_ramp.result->collision_plane->normal.z >= 0.7F);

    const auto steep_ramp = collision.trace_hull(
        {-16.0F, 90.0F, 100.0F}, {-16.0F, 90.0F, 50.0F},
        player::PlayerMovementHull::ducked, scratch);
    REQUIRE(steep_ramp);
    REQUIRE(steep_ramp.result->collision_plane);
    CHECK(steep_ramp.result->end_position.z == Catch::Approx(82.0F));
    CHECK(steep_ramp.result->collision_plane->normal.z ==
        Catch::Approx(0.44721359F));
    CHECK(steep_ramp.result->collision_plane->normal.z < 0.7F);

    const auto standing_free = collision.test_position(
        {0.0F, 0.0F, 36.0F}, player::PlayerMovementHull::standing, scratch);
    REQUIRE(standing_free);
    CHECK(standing_free.result->status ==
        movement::LocalMovementPositionStatus::free);
    const auto standing_below_floor = collision.test_position(
        {0.0F, 0.0F, 35.0F}, player::PlayerMovementHull::standing, scratch);
    REQUIRE(standing_below_floor);
    CHECK(standing_below_floor.result->status ==
        movement::LocalMovementPositionStatus::blocking);
}

TEST_CASE("The movement kernel traverses the parsed literal BSP deterministically",
    "[goldsrc][movement][kernel][integration][literal-bsp][trajectory]")
{
    const movement::WorldOnlyMovementCollision collision{
        literal_world_package()};
    REQUIRE(collision.valid());

    SECTION("settle on the floor")
    {
        const auto commands = literal_commands(100U, 0.0F);
        const auto first = run_literal_commands(
            fixture::make_state(
                {0.0F, 0.0F, 80.0F}, {},
                player::PlayerMovementMode::airborne),
            collision, commands);
        const auto second = run_literal_commands(
            fixture::make_state(
                {0.0F, 0.0F, 80.0F}, {},
                player::PlayerMovementMode::airborne),
            collision, commands);
        REQUIRE(first.state);
        REQUIRE(second.state);
        CHECK_FALSE(first.error);
        CHECK(first.state->ground_state().grounded());
        CHECK(first.state->origin().z == Catch::Approx(36.0F));
        CHECK(first.start_solid_count == 0U);
        CHECK(first.all_solid_count == 0U);
        CHECK(player::local_player_movement_state_signature(*first.state) ==
            player::local_player_movement_state_signature(*second.state));
    }

    SECTION("walk up the twelve-unit step")
    {
        const auto commands = literal_commands(35U, 240.0F);
        const auto first = run_literal_commands(
            fixture::make_state(), collision, commands);
        const auto second = run_literal_commands(
            fixture::make_state(), collision, commands);
        REQUIRE(first.state);
        REQUIRE(second.state);
        CHECK(first.step_attempt_count > 0U);
        CHECK(first.step_success_count > 0U);
        CHECK(first.state->ground_state().grounded());
        CHECK(first.state->origin().x > literal::kValidStepMinimumX);
        CHECK(first.state->origin().z == Catch::Approx(48.0F));
        CHECK(first.start_solid_count == 0U);
        CHECK(first.all_solid_count == 0U);
        CHECK(player::local_player_movement_state_signature(*first.state) ==
            player::local_player_movement_state_signature(*second.state));
        CHECK(first.step_success_count == second.step_success_count);
    }

    SECTION("the twenty-eight-unit step blocks a floor approach")
    {
        // Start less than one 10 ms sweep from contact.  A single command is
        // intentional: it proves the too-high candidate is rejected without
        // making a subsequent command depend on binary32 boundary re-entry.
        const auto commands = literal_commands(1U, 240.0F);
        const auto first = run_literal_commands(
            fixture::make_state(
                {15.0F, 40.0F, 36.0F}, {240.0F, 0.0F, 0.0F}),
            collision, commands);
        const auto second = run_literal_commands(
            fixture::make_state(
                {15.0F, 40.0F, 36.0F}, {240.0F, 0.0F, 0.0F}),
            collision, commands);
        const auto diagnostic_error = first.error
            ? static_cast<unsigned int>(*first.error)
            : std::numeric_limits<unsigned int>::max();
        INFO(first.failure_sequence);
        INFO(diagnostic_error);
        INFO(first.last_origin.x);
        INFO(first.last_origin.y);
        INFO(first.last_origin.z);
        REQUIRE(first.state);
        REQUIRE(second.state);
        CHECK(first.step_attempt_count > 0U);
        CHECK(first.step_success_count == 0U);
        CHECK(first.collision_count > 0U);
        CHECK(first.state->origin().x == Catch::Approx(16.0F));
        CHECK(first.state->origin().z == Catch::Approx(36.0F));
        CHECK(first.start_solid_count == 0U);
        CHECK(first.all_solid_count == 0U);
        CHECK(player::local_player_movement_state_signature(*first.state) ==
            player::local_player_movement_state_signature(*second.state));
    }

    SECTION("diagonal input slides along the east wall")
    {
        const auto commands = literal_commands(35U, 240.0F, 120.0F);
        const auto first = run_literal_commands(
            fixture::make_state({140.0F, 0.0F, 36.0F}),
            collision, commands);
        const auto second = run_literal_commands(
            fixture::make_state({140.0F, 0.0F, 36.0F}),
            collision, commands);
        REQUIRE(first.state);
        REQUIRE(second.state);
        CHECK(first.collision_count > 0U);
        CHECK(first.state->origin().x == Catch::Approx(176.0F));
        CHECK(std::abs(first.state->origin().y) > 1.0F);
        CHECK(first.start_solid_count == 0U);
        CHECK(first.all_solid_count == 0U);
        CHECK(player::local_player_movement_state_signature(*first.state) ==
            player::local_player_movement_state_signature(*second.state));
    }

    SECTION("jump and land on the literal floor")
    {
        std::vector<goldsrc::GoldSrcUserCmdState> commands;
        commands.reserve(120U);
        commands.push_back(fixture::make_command(
            1U, 10U, 0.0F, 0.0F,
            goldsrc::kSyntheticGoldSrcButtonJump));
        for (std::uint32_t sequence = 2U; sequence <= 120U; ++sequence) {
            commands.push_back(fixture::make_command(sequence));
        }
        const auto first = run_literal_commands(
            fixture::make_state(), collision, commands);
        const auto second = run_literal_commands(
            fixture::make_state(), collision, commands);
        REQUIRE(first.state);
        REQUIRE(second.state);
        CHECK(first.jump_count == 1U);
        CHECK(first.state->ground_state().grounded());
        CHECK(first.state->origin().z == Catch::Approx(36.0F));
        CHECK(first.start_solid_count == 0U);
        CHECK(first.all_solid_count == 0U);
        CHECK(player::local_player_movement_state_signature(*first.state) ==
            player::local_player_movement_state_signature(*second.state));
    }

    SECTION("the low ceiling blocks stand until the ducked player exits")
    {
        std::vector<goldsrc::GoldSrcUserCmdState> commands;
        commands.push_back(fixture::make_command(1U));
        for (std::uint32_t sequence = 2U; sequence <= 31U; ++sequence) {
            commands.push_back(fixture::make_command(
                sequence, 10U, -240.0F, 0.0F,
                goldsrc::kSyntheticGoldSrcButtonDuck));
        }
        commands.push_back(fixture::make_command(32U));
        const auto first = run_literal_commands(
            fixture::make_state(
                {-40.0F, 32.0F, 18.0F}, {},
                player::PlayerMovementMode::walking,
                player::PlayerMovementHull::ducked,
                0U, goldsrc::kSyntheticGoldSrcButtonDuck),
            collision, commands);
        const auto second = run_literal_commands(
            fixture::make_state(
                {-40.0F, 32.0F, 18.0F}, {},
                player::PlayerMovementMode::walking,
                player::PlayerMovementHull::ducked,
                0U, goldsrc::kSyntheticGoldSrcButtonDuck),
            collision, commands);
        REQUIRE(first.state);
        REQUIRE(second.state);
        CHECK(first.stand_blocked_count == 1U);
        CHECK(first.duck_exit_count == 1U);
        CHECK(first.state->hull() == player::PlayerMovementHull::standing);
        CHECK(first.state->origin().x < -80.0F);
        CHECK(first.state->origin().z == Catch::Approx(36.0F));
        CHECK(first.start_solid_count == 0U);
        CHECK(first.all_solid_count == 0U);
        CHECK(player::local_player_movement_state_signature(*first.state) ==
            player::local_player_movement_state_signature(*second.state));
    }

    SECTION("water fails typed without publishing partial movement")
    {
        const auto initial = fixture::make_state(
            {-112.0F, 80.0F, 36.0F}, {},
            player::PlayerMovementMode::airborne);
        const auto initial_signature =
            player::local_player_movement_state_signature(initial);
        const std::array commands{fixture::make_command(1U)};
        const auto result = run_literal_commands(
            initial, collision, commands);
        CHECK_FALSE(result.state);
        REQUIRE(result.error);
        CHECK(*result.error == movement::
            LocalMovementSimulationErrorCode::liquid_movement_unsupported);
        CHECK(player::local_player_movement_state_signature(initial) ==
            initial_signature);
    }
}

[[nodiscard]] std::optional<RepeatedCampaignSummary> run_repeated_campaign(
    const RepeatedCampaign campaign)
{
    fixture::DeterministicLocalMovementCollision collision;
    std::optional<player::LocalPlayerMovementState> state;
    std::uint32_t command_count = 0U;
    switch (campaign) {
    case RepeatedCampaign::settle:
        state.emplace(fixture::make_state(
            {0.0F, 0.0F, 100.0F}, {},
            player::PlayerMovementMode::airborne));
        command_count = 100U;
        break;
    case RepeatedCampaign::walk_slide:
        collision.add_positive_x_wall(48.0F);
        state.emplace(fixture::make_state());
        command_count = 80U;
        break;
    case RepeatedCampaign::jump_land:
        state.emplace(fixture::make_state());
        command_count = 120U;
        break;
    case RepeatedCampaign::step:
        collision.add_step(20.0F, 100.0F, -64.0F, 64.0F, 12.0F);
        state.emplace(fixture::make_state());
        command_count = 50U;
        break;
    case RepeatedCampaign::duck_stand:
        state.emplace(fixture::make_state());
        command_count = 20U;
        break;
    }

    RepeatedCampaignSummary summary;
    for (std::uint32_t sequence = 1U; sequence <= command_count; ++sequence) {
        float forward = 0.0F;
        float side = 0.0F;
        std::uint16_t buttons = 0U;
        if (campaign == RepeatedCampaign::walk_slide) {
            forward = 240.0F;
            side = 160.0F;
        } else if (campaign == RepeatedCampaign::step) {
            forward = sequence <= 30U ? 240.0F : 0.0F;
        } else if (campaign == RepeatedCampaign::jump_land && sequence == 1U) {
            buttons = goldsrc::kSyntheticGoldSrcButtonJump;
        } else if (campaign == RepeatedCampaign::duck_stand &&
            sequence <= 10U) {
            buttons = goldsrc::kSyntheticGoldSrcButtonDuck;
        }
        auto simulated = fixture::simulate(
            *state,
            fixture::make_command(
                sequence, 10U, forward, side, buttons),
            collision);
        if (!simulated || !simulated.state) {
            return std::nullopt;
        }
        summary.trace_count += simulated.statistics.trace_count;
        summary.collision_count +=
            simulated.statistics.collision_hit_count;
        summary.step_count += simulated.statistics.step_success_count;
        summary.jump_count += simulated.statistics.jump_count;
        summary.duck_enter_count += simulated.statistics.duck_enter_count;
        summary.duck_exit_count += simulated.statistics.duck_exit_count;
        summary.start_solid_count +=
            simulated.statistics.start_solid_count;
        summary.all_solid_count += simulated.statistics.all_solid_count;
        state.emplace(std::move(*simulated.state));
    }
    summary.signature =
        player::local_player_movement_state_signature(*state);
    summary.command_count = command_count;
    summary.grounded = state->ground_state().grounded();
    summary.hull = state->hull();
    return summary;
}

TEST_CASE("A local movement command campaign is deterministic end to end",
    "[goldsrc][movement][kernel][integration][determinism]")
{
    const auto run_campaign = [] {
        fixture::DeterministicLocalMovementCollision collision;
        std::optional<player::LocalPlayerMovementState> state{
            fixture::make_state()};
        const std::vector<goldsrc::GoldSrcUserCmdState> commands{
            fixture::make_command(1U, 50U, 200.0F),
            fixture::make_command(
                2U, 10U, 200.0F, 0.0F,
                goldsrc::kSyntheticGoldSrcButtonJump),
            fixture::make_command(
                3U, 20U, 200.0F, -50.0F,
                goldsrc::kSyntheticGoldSrcButtonJump),
            fixture::make_command(4U, 20U, 200.0F, -50.0F),
        };
        std::vector<std::uint64_t> signatures;
        for (const auto& command : commands) {
            auto result = fixture::simulate(*state, command, collision);
            if (!result || !result.state) {
                return std::pair{
                    std::vector<std::uint64_t>{},
                    std::optional<player::LocalPlayerMovementState>{}};
            }
            signatures.push_back(result.deterministic_state_signature);
            state.emplace(std::move(*result.state));
        }
        return std::pair{std::move(signatures), std::move(state)};
    };

    auto [first_signatures, first_state] = run_campaign();
    auto [second_signatures, second_state] = run_campaign();
    REQUIRE(first_state);
    REQUIRE(second_state);
    REQUIRE(first_signatures.size() == 4U);
    CHECK(first_signatures == second_signatures);
    CHECK(player::local_player_movement_state_signature(*first_state) ==
        player::local_player_movement_state_signature(*second_state));
    CHECK(first_state->source_command_sequence() == 4U);
    CHECK(first_state->state_revision() == 5U);
    CHECK(first_state->simulation_time_nanoseconds() == 100'000'000ULL);
    CHECK(first_state->old_buttons() == 0U);
}

TEST_CASE("Repeated local movement campaigns pass 20 out of 20",
    "[goldsrc][movement][kernel][integration][campaign][determinism]")
{
    constexpr std::array campaigns{
        RepeatedCampaign::settle,
        RepeatedCampaign::walk_slide,
        RepeatedCampaign::jump_land,
        RepeatedCampaign::step,
        RepeatedCampaign::duck_stand,
    };
    for (const auto campaign : campaigns) {
        std::optional<RepeatedCampaignSummary> expected;
        for (std::size_t iteration = 0U; iteration < 20U; ++iteration) {
            INFO(static_cast<unsigned int>(campaign));
            INFO(iteration);
            const auto summary = run_repeated_campaign(campaign);
            REQUIRE(summary);
            CHECK(summary->trace_count <=
                static_cast<std::uint64_t>(summary->command_count) * 32U);
            CHECK(summary->start_solid_count == 0U);
            CHECK(summary->all_solid_count == 0U);
            CHECK(summary->grounded);
            CHECK(summary->hull == player::PlayerMovementHull::standing);
            if (expected) {
                CHECK(*summary == *expected);
            } else {
                expected = summary;
            }
        }
        REQUIRE(expected);
        switch (campaign) {
        case RepeatedCampaign::settle: break;
        case RepeatedCampaign::walk_slide:
            CHECK(expected->collision_count > 0U);
            break;
        case RepeatedCampaign::jump_land:
            CHECK(expected->jump_count == 1U);
            break;
        case RepeatedCampaign::step:
            CHECK(expected->step_count > 0U);
            break;
        case RepeatedCampaign::duck_stand:
            CHECK(expected->duck_enter_count == 1U);
            CHECK(expected->duck_exit_count == 1U);
            break;
        }
    }
}

TEST_CASE("Liquid and collision-query failures do not publish partial state",
    "[goldsrc][movement][kernel][integration][transaction]")
{
    const auto initial = fixture::make_state();
    const auto initial_signature =
        player::local_player_movement_state_signature(initial);

    SECTION("initial liquid contents")
    {
        fixture::DeterministicLocalMovementCollision collision;
        collision.add_liquid(
            {-8.0F, -8.0F, 30.0F}, {8.0F, 8.0F, 42.0F});
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U), collision);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == movement::
            LocalMovementSimulationErrorCode::liquid_movement_unsupported);
        CHECK_FALSE(result.state);
        CHECK(result.deterministic_state_signature == 0U);
    }

    SECTION("point-contents query error")
    {
        fixture::DeterministicLocalMovementCollision collision;
        collision.fail_point_contents();
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U), collision);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == movement::
            LocalMovementSimulationErrorCode::collision_query_failed);
        REQUIRE(result.error->collision_error);
        CHECK_FALSE(result.state);
    }

    SECTION("allsolid start")
    {
        fixture::DeterministicLocalMovementCollision collision;
        collision.add_box(
            {-32.0F, -32.0F, -16.0F}, {32.0F, 32.0F, 96.0F});
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U), collision);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == movement::
            LocalMovementSimulationErrorCode::player_allsolid);
        CHECK(result.statistics.all_solid_count == 1U);
        CHECK_FALSE(result.state);
    }

    SECTION("startsolid without allsolid")
    {
        fixture::DeterministicLocalMovementCollision collision;
        collision.add_box(
            {-32.0F, -32.0F, -16.0F}, {32.0F, 32.0F, 96.0F});
        collision.zero_length_blocking_is_all_solid(false);
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U), collision);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == movement::
            LocalMovementSimulationErrorCode::player_startsolid);
        CHECK(result.statistics.start_solid_count == 1U);
        CHECK_FALSE(result.state);
    }

    CHECK(player::local_player_movement_state_signature(initial) ==
        initial_signature);
}

TEST_CASE("Every dry-walk liquid category and ladder mode fails typed",
    "[goldsrc][movement][kernel][integration][unsupported]")
{
    const auto check_liquid = [](const player::PlayerMovementContents contents) {
        fixture::DeterministicLocalMovementCollision collision;
        collision.add_liquid(
            {-8.0F, -8.0F, 30.0F}, {8.0F, 8.0F, 42.0F}, contents);
        return fixture::simulate(
            fixture::make_state(), fixture::make_command(1U), collision);
    };

    for (const auto contents : {
             player::PlayerMovementContents::water,
             player::PlayerMovementContents::slime,
             player::PlayerMovementContents::lava,
             player::PlayerMovementContents::current}) {
        const auto result = check_liquid(contents);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == movement::
            LocalMovementSimulationErrorCode::liquid_movement_unsupported);
        CHECK_FALSE(result.state);
    }

    fixture::DeterministicLocalMovementCollision collision{false};
    const auto ladder = fixture::simulate(
        fixture::make_state(
            {0.0F, 0.0F, 100.0F}, {},
            player::PlayerMovementMode::unsupported_ladder),
        fixture::make_command(1U), collision);
    REQUIRE_FALSE(ladder);
    REQUIRE(ladder.error);
    CHECK(ladder.error->code == movement::
        LocalMovementSimulationErrorCode::ladder_movement_unsupported);
    CHECK_FALSE(ladder.state);
}

TEST_CASE("Sequence revision and simulation-time bounds fail closed",
    "[goldsrc][movement][kernel][integration][bounds]")
{
    fixture::DeterministicLocalMovementCollision collision;

    SECTION("non-contiguous command")
    {
        const auto result = fixture::simulate(
            fixture::make_state(), fixture::make_command(2U), collision);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == movement::
            LocalMovementSimulationErrorCode::invalid_command_sequence);
    }

    SECTION("revision exhaustion")
    {
        const auto initial = fixture::make_state(
            {0.0F, 0.0F, 36.0F}, {}, player::PlayerMovementMode::walking,
            player::PlayerMovementHull::standing, 0U, 0U, 1.0F, 1.0F, 0U,
            std::numeric_limits<std::uint64_t>::max());
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U), collision);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == movement::
            LocalMovementSimulationErrorCode::state_revision_exhausted);
    }

    SECTION("simulation clock overflow")
    {
        const auto initial = fixture::make_state(
            {0.0F, 0.0F, 36.0F}, {}, player::PlayerMovementMode::walking,
            player::PlayerMovementHull::standing, 0U, 0U, 1.0F, 1.0F,
            std::numeric_limits<std::uint64_t>::max() - 5'000'000ULL);
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U, 10U), collision);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == movement::
            LocalMovementSimulationErrorCode::simulation_time_overflow);
    }
}

} // namespace
