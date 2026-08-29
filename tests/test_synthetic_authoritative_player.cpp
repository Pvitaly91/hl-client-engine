#include "literal_movement_bsp_fixture.hpp"
#include "local_movement_test_fixture.hpp"

#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <hlclient/goldsrc/movement/local_movement_collision.hpp>
#include <hlclient/prediction/synthetic_authoritative_player.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fixture = hlclient::tests::local_movement;
namespace literal = hlclient::tests::literal_movement_bsp;
namespace bsp = hlclient::goldsrc::bsp;
namespace goldsrc_collision = hlclient::goldsrc::collision;
namespace movement = hlclient::goldsrc::movement;
namespace player = hlclient::movement;
namespace prediction = hlclient::prediction;

[[nodiscard]] std::shared_ptr<const hlclient::collision::CollisionWorldPackage>
literal_world_package()
{
    const auto parsed = bsp::GoldSrcBspParser::parse(literal::make_bsp_v30());
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

struct SyntheticFixture {
    player::LocalPlayerMovementState initial{fixture::make_state()};
    movement::GoldSrcMovementEnvironment environment{
        fixture::make_environment()};
    movement::WorldOnlyMovementCollision collision{literal_world_package()};
    prediction::PredictionSessionIdentity session{};

    SyntheticFixture()
    {
        auto created = prediction::create_prediction_session_identity(
            1U, 1U, collision, environment, {}, initial);
        REQUIRE(created);
        session = *created.session;
    }
};

[[nodiscard]] prediction::SyntheticAuthoritativePlayerConfig exact_config(
    const SyntheticFixture& values)
{
    prediction::SyntheticAuthoritativePlayerConfig config;
    config.session = values.session;
    return config;
}

TEST_CASE("Synthetic authority owns an independent exact state",
    "[prediction][synthetic-authority][exact]")
{
    SyntheticFixture values;
    movement::GoldSrcLocalMovementScratch scratch;
    auto created = prediction::SyntheticAuthoritativePlayerSimulator::create(
        values.initial, values.environment, exact_config(values),
        values.collision, scratch);
    REQUIRE(created);
    auto& simulator = *created.simulator;
    const auto original_signature =
        player::local_player_movement_state_signature(values.initial);

    const auto command = fixture::make_command(1U, 10U, 120.0F);
    const auto result = simulator.simulate_command(
        command, values.collision, scratch);
    REQUIRE(result);
    REQUIRE(result.command_processed);
    REQUIRE(result.released_update);
    REQUIRE(result.released_update->acknowledgement().sequence());
    CHECK(result.released_update->acknowledgement().sequence()->value() == 1U);
    CHECK(simulator.current_state().source_command_sequence() == 1U);
    CHECK(values.initial.source_command_sequence() == 0U);
    CHECK(player::local_player_movement_state_signature(values.initial) ==
        original_signature);
    CHECK(simulator.statistics().processed_command_count == 1U);
}

TEST_CASE("Synthetic authority releases acknowledgements after a bounded delay",
    "[prediction][synthetic-authority][delay]")
{
    SyntheticFixture values;
    movement::GoldSrcLocalMovementScratch scratch;
    auto config = exact_config(values);
    config.scenario = prediction::SyntheticAuthoritativeScenario::
        delayed_authority;
    config.command_delay = 8U;
    auto created = prediction::SyntheticAuthoritativePlayerSimulator::create(
        values.initial, values.environment, config, values.collision, scratch);
    REQUIRE(created);

    for (std::uint32_t sequence = 1U; sequence <= 8U; ++sequence) {
        const auto result = created.simulator->simulate_command(
            fixture::make_command(sequence), values.collision, scratch);
        REQUIRE(result);
        CHECK_FALSE(result.released_update);
    }
    const auto ninth = created.simulator->simulate_command(
        fixture::make_command(9U), values.collision, scratch);
    REQUIRE(ninth);
    REQUIRE(ninth.released_update);
    REQUIRE(ninth.released_update->acknowledgement().sequence());
    CHECK(ninth.released_update->acknowledgement().sequence()->value() == 1U);
    CHECK(created.simulator->pending_delayed_update_count() == 8U);

    const auto flushed = created.simulator->release_next_delayed();
    REQUIRE(flushed);
    REQUIRE(flushed.released_update);
    CHECK(flushed.released_update->acknowledgement().sequence()->value() == 2U);

    config.command_delay =
        prediction::kMaximumSyntheticAuthorityDelayCommands;
    movement::GoldSrcLocalMovementScratch maximum_scratch;
    auto maximum = prediction::SyntheticAuthoritativePlayerSimulator::create(
        values.initial, values.environment, config, values.collision,
        maximum_scratch);
    REQUIRE(maximum);
    for (std::uint32_t sequence = 1U; sequence <= 64U; ++sequence) {
        const auto result = maximum.simulator->simulate_command(
            fixture::make_command(sequence), values.collision, maximum_scratch);
        REQUIRE(result);
        CHECK_FALSE(result.released_update);
    }
    const auto sixty_fifth = maximum.simulator->simulate_command(
        fixture::make_command(65U), values.collision, maximum_scratch);
    REQUIRE(sixty_fifth.released_update);
    CHECK(sixty_fifth.released_update->acknowledgement().sequence()->value() ==
        1U);
}

TEST_CASE("Synthetic corrections are typed and collision validated",
    "[prediction][synthetic-authority][correction][collision]")
{
    SyntheticFixture values;
    const auto run = [&values](
                         const prediction::SyntheticAuthoritativeScenario scenario,
                         const hlclient::assets::AssetVector3 delta,
                         const std::optional<hlclient::assets::AssetVector3>
                             teleport = std::nullopt) {
        movement::GoldSrcLocalMovementScratch scratch;
        auto config = exact_config(values);
        config.scenario = scenario;
        config.correction_command_sequence = 1U;
        config.teleport_origin = teleport;
        if (scenario == prediction::SyntheticAuthoritativeScenario::
                small_position_correction) {
            config.small_position_delta = delta;
        } else if (scenario == prediction::SyntheticAuthoritativeScenario::
                       velocity_correction) {
            config.velocity_delta = delta;
        } else if (scenario == prediction::SyntheticAuthoritativeScenario::
                       large_position_correction) {
            config.large_position_delta = delta;
        }
        auto created =
            prediction::SyntheticAuthoritativePlayerSimulator::create(
                values.initial, values.environment, config, values.collision,
                scratch);
        REQUIRE(created);
        return created.simulator->simulate_command(
            fixture::make_command(1U), values.collision, scratch);
    };

    const auto small = run(
        prediction::SyntheticAuthoritativeScenario::small_position_correction,
        {0.5F, 0.0F, 0.0F});
    REQUIRE(small);
    REQUIRE(small.released_update);
    CHECK(small.correction_applied);
    CHECK(small.released_update->movement_state().origin().x ==
        Catch::Approx(0.5F));

    const auto velocity = run(
        prediction::SyntheticAuthoritativeScenario::velocity_correction,
        {2.0F, 0.0F, 0.0F});
    REQUIRE(velocity);
    CHECK(velocity.correction_applied);
    CHECK(velocity.released_update->movement_state().velocity().x ==
        Catch::Approx(2.0F));

    const auto large = run(
        prediction::SyntheticAuthoritativeScenario::large_position_correction,
        {0.0F, -32.0F, 0.0F});
    REQUIRE(large);
    CHECK(large.released_update->movement_state().origin().y ==
        Catch::Approx(-32.0F));

    const auto teleport = run(
        prediction::SyntheticAuthoritativeScenario::teleport, {},
        hlclient::assets::AssetVector3{-100.0F, 0.0F, 36.0F});
    REQUIRE(teleport);
    CHECK(teleport.discontinuity ==
        prediction::AuthoritativePlayerDiscontinuity::teleport);
    CHECK(teleport.released_update->movement_state().origin().x ==
        Catch::Approx(-100.0F));

    const auto blocking = run(
        prediction::SyntheticAuthoritativeScenario::large_position_correction,
        {32.0F, 0.0F, 0.0F});
    REQUIRE_FALSE(blocking);
    REQUIRE(blocking.error);
    CHECK(blocking.error->code ==
        prediction::PredictionErrorCode::authoritative_state_blocking);
}

TEST_CASE("Synthetic source applies output backpressure transactionally",
    "[prediction][synthetic-authority][backpressure]")
{
    SyntheticFixture values;
    movement::GoldSrcLocalMovementScratch scratch;
    auto config = exact_config(values);
    config.maximum_pending_updates = 1U;
    auto created = prediction::SyntheticAuthoritativePlayerStateSource::create(
        values.initial, values.environment, config, values.collision, scratch);
    REQUIRE(created);
    auto& source = *created.source;

    REQUIRE(source.submit_command(
        fixture::make_command(1U), values.collision, scratch));
    const auto blocked = source.submit_command(
        fixture::make_command(2U), values.collision, scratch);
    REQUIRE_FALSE(blocked);
    REQUIRE(blocked.error);
    CHECK(blocked.error->code ==
        prediction::PredictionErrorCode::authoritative_update_backpressure);
    CHECK(source.simulator().current_state().source_command_sequence() == 1U);
    CHECK(source.statistics().backpressure_count == 1U);

    const auto first = source.poll_next();
    REQUIRE(first);
    REQUIRE(first.state);
    REQUIRE(source.submit_command(
        fixture::make_command(2U), values.collision, scratch));
    CHECK(source.simulator().current_state().source_command_sequence() == 2U);
}

TEST_CASE("Synthetic simulator and source retain ordinals across moves",
    "[prediction][synthetic-authority][move][ordinal]")
{
    SyntheticFixture values;
    movement::GoldSrcLocalMovementScratch scratch;

    auto simulator_created =
        prediction::SyntheticAuthoritativePlayerSimulator::create(
            values.initial, values.environment, exact_config(values),
            values.collision, scratch);
    REQUIRE(simulator_created);
    auto first_simulator = std::move(*simulator_created.simulator);
    auto second_simulator = std::move(first_simulator);
    const auto simulated = second_simulator.simulate_command(
        fixture::make_command(1U), values.collision, scratch);
    REQUIRE(simulated);
    REQUIRE(simulated.released_update);
    CHECK(simulated.released_update->update_identity().update_ordinal() == 1U);

    auto source_created =
        prediction::SyntheticAuthoritativePlayerStateSource::create(
            values.initial, values.environment, exact_config(values),
            values.collision, scratch);
    REQUIRE(source_created);
    auto first_source = std::move(*source_created.source);
    auto second_source = std::move(first_source);
    const auto submitted = second_source.submit_command(
        fixture::make_command(1U), values.collision, scratch);
    REQUIRE(submitted);
    auto polled = second_source.poll_next();
    REQUIRE(polled);
    REQUIRE(polled.state);
    CHECK(polled.state->update_identity().update_ordinal() == 1U);
}

TEST_CASE("Synthetic source injects one deterministic stale duplicate campaign",
    "[prediction][synthetic-authority][stale][duplicate]")
{
    SyntheticFixture values;
    movement::GoldSrcLocalMovementScratch scratch;
    auto config = exact_config(values);
    config.scenario = prediction::SyntheticAuthoritativeScenario::
        stale_and_duplicate_updates;
    config.maximum_pending_updates = 3U;
    config.correction_command_sequence = 2U;
    auto created = prediction::SyntheticAuthoritativePlayerStateSource::create(
        values.initial, values.environment, config, values.collision, scratch);
    REQUIRE(created);
    auto& source = *created.source;
    REQUIRE(source.submit_command(
        fixture::make_command(1U), values.collision, scratch));
    REQUIRE(source.poll_next().state);
    const auto submitted = source.submit_command(
        fixture::make_command(2U), values.collision, scratch);
    REQUIRE(submitted);
    CHECK(submitted.queued_update_count == 3U);

    std::vector<std::uint64_t> ordinals;
    std::vector<std::uint32_t> sequences;
    for (std::size_t index = 0U; index < 3U; ++index) {
        auto polled = source.poll_next();
        REQUIRE(polled);
        REQUIRE(polled.state);
        ordinals.push_back(polled.state->update_identity().update_ordinal());
        sequences.push_back(
            polled.state->acknowledgement().sequence()->value());
    }
    CHECK(ordinals == std::vector<std::uint64_t>{2U, 2U, 1U});
    CHECK(sequences == std::vector<std::uint32_t>{2U, 2U, 1U});
    CHECK(source.statistics().duplicate_update_count == 1U);
    CHECK(source.statistics().stale_update_count == 1U);
}

TEST_CASE("Synthetic authority is deterministic and rejects command disorder",
    "[prediction][synthetic-authority][determinism][ordering]")
{
    const auto campaign = [] {
        SyntheticFixture values;
        movement::GoldSrcLocalMovementScratch scratch;
        auto created =
            prediction::SyntheticAuthoritativePlayerSimulator::create(
                values.initial, values.environment, exact_config(values),
                values.collision, scratch);
        REQUIRE(created);
        for (std::uint32_t sequence = 1U; sequence <= 20U; ++sequence) {
            REQUIRE(created.simulator->simulate_command(
                fixture::make_command(sequence, 10U, 100.0F),
                values.collision, scratch));
        }
        return player::local_player_movement_state_signature(
            created.simulator->current_state());
    };
    CHECK(campaign() == campaign());

    SyntheticFixture values;
    movement::GoldSrcLocalMovementScratch scratch;
    auto created = prediction::SyntheticAuthoritativePlayerSimulator::create(
        values.initial, values.environment, exact_config(values),
        values.collision, scratch);
    REQUIRE(created);
    const auto gap = created.simulator->simulate_command(
        fixture::make_command(2U), values.collision, scratch);
    REQUIRE_FALSE(gap);
    CHECK(gap.error->code == prediction::PredictionErrorCode::
        prediction_command_gap);
    REQUIRE(created.simulator->simulate_command(
        fixture::make_command(1U), values.collision, scratch));
    const auto duplicate = created.simulator->simulate_command(
        fixture::make_command(1U), values.collision, scratch);
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.error->code == prediction::PredictionErrorCode::
        duplicate_predicted_command);
    CHECK(created.simulator->current_state().source_command_sequence() == 1U);
}

TEST_CASE("Replay scenarios inject bounded periodic velocity corrections",
    "[prediction][synthetic-authority][replay][correction]")
{
    constexpr std::array scenarios{
        std::pair{prediction::SyntheticAuthoritativeScenario::wall_replay,
            240U},
        std::pair{prediction::SyntheticAuthoritativeScenario::jump_replay,
            100U},
        std::pair{prediction::SyntheticAuthoritativeScenario::duck_replay,
            50U},
    };
    for (const auto& [scenario, period] : scenarios) {
        SyntheticFixture values;
        movement::GoldSrcLocalMovementScratch scratch;
        auto config = exact_config(values);
        config.scenario = scenario;
        config.correction_command_sequence = 2U;
        config.velocity_delta = {1.0F, 0.0F, 0.0F};
        auto created =
            prediction::SyntheticAuthoritativePlayerSimulator::create(
                values.initial, values.environment, config, values.collision,
                scratch);
        REQUIRE(created);
        for (std::uint32_t sequence = 1U; sequence <= period + 2U;
             ++sequence) {
            const auto result = created.simulator->simulate_command(
                fixture::make_command(sequence), values.collision, scratch);
            REQUIRE(result);
            CHECK(result.correction_applied ==
                (sequence == 2U || sequence == period + 2U));
        }
        CHECK(created.simulator->statistics().correction_count == 2U);
    }
}

} // namespace
