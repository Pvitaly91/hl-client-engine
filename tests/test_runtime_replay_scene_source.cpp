#include <hlclient/app/runtime_replay_scene_source.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace {

namespace app = hlclient::app;
namespace client = hlclient::client;
namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] goldsrc::RuntimeReplayFixture fixture(
    const goldsrc::RuntimeReplayFixtureKind kind =
        goldsrc::RuntimeReplayFixtureKind::basic_mixed,
    const std::uint64_t generation = 1U,
    const std::uint64_t identity_base = 1'000U)
{
    auto built = goldsrc::make_runtime_replay_fixture(
        {kind, generation, 100U, identity_base});
    REQUIRE(built);
    REQUIRE(built.fixture);
    return std::move(*built.fixture);
}

[[nodiscard]] std::unique_ptr<app::RuntimeReplaySceneSource> source(
    goldsrc::RuntimeReplayFixture replay_fixture,
    const app::RuntimeReplaySchedulingLimits limits = {})
{
    auto created = app::RuntimeReplaySceneSource::create(
        std::move(replay_fixture), limits);
    REQUIRE(created);
    REQUIRE(created.source);
    return std::move(created.source);
}

[[nodiscard]] const client::RuntimePacketEntityObservation* find_entity(
    const client::RuntimeClientObservationState& state,
    const std::uint32_t number)
{
    const auto found = std::find_if(
        state.packet_entities.begin(), state.packet_entities.end(),
        [number](const auto& entity) {
            return entity.entity_number == number;
        });
    return found == state.packet_entities.end() ? nullptr : &*found;
}

} // namespace

TEST_CASE("Application replay source schedules owning records through updates",
          "[application-runtime-replay][scheduling][ownership]")
{
    auto replay = fixture();
    CHECK(replay.baseline_consumed_bits == 88U);
    auto playback = source(
        std::move(replay), app::RuntimeReplaySchedulingLimits{1U, 1'024U});
    CHECK(playback->state() == app::RuntimeReplaySourceState::not_started);
    CHECK_FALSE(playback->world_state().runtime_observation());
    REQUIRE(playback->start());
    REQUIRE(playback->world_state().runtime_observation());
    CHECK(playback->world_state().runtime_publication_revision() == 1U);

    std::shared_ptr<const client::RuntimeClientObservationState> first;
    for (std::size_t update = 0U; update < 4U; ++update) {
        REQUIRE(playback->update(std::chrono::duration<double>{1.0 / 60.0}));
        const auto summary = playback->summary();
        CHECK(summary.applied_records == update + 1U);
        CHECK(summary.pending_records == 3U - update);
        if (update == 0U) {
            first = playback->world_state().runtime_observation();
        }
    }
    const auto summary = playback->summary();
    CHECK(summary.state == app::RuntimeReplaySourceState::completed);
    CHECK(summary.input_records == 4U);
    CHECK(summary.applied_records == 4U);
    CHECK(summary.failed_records == 0U);
    CHECK(summary.pending_records == 0U);
    CHECK(summary.application_updates == 4U);
    CHECK(summary.session_finish_count == 1U);
    REQUIRE(playback->world_state().runtime_observation());
    CHECK(playback->world_state().runtime_observation() != first);
    const auto& state = *playback->world_state().runtime_observation();
    const auto* entity = find_entity(state, 2U);
    REQUIRE(entity);
    REQUIRE(entity->origin.x);
    CHECK(*entity->origin.x == Catch::Approx(10.0));
    REQUIRE(state.receiving_client);
    REQUIRE(state.receiving_client->health);
    CHECK(*state.receiving_client->health == Catch::Approx(80.0));
    REQUIRE(state.weapon_slots.size() > 2U);
    REQUIRE(state.weapon_slots[2U].clip);
    CHECK(*state.weapon_slots[2U].clip == 8);
    CHECK(state.client_metadata.freshness ==
          client::RuntimeObservationFreshness::retained);
    CHECK(state.entity_metadata.freshness ==
          client::RuntimeObservationFreshness::observed_in_record);
    CHECK(playback->world_state().world_revision() == 0U);
    CHECK(playback->world_state().scene_revision() == 0U);
}

TEST_CASE("Replay budgets and application timing preserve canonical state",
          "[application-runtime-replay][determinism][backlog]")
{
    auto one = source(
        fixture(goldsrc::RuntimeReplayFixtureKind::basic_mixed, 1U, 2'000U),
        {1U, 1'024U});
    auto many = source(
        fixture(goldsrc::RuntimeReplayFixtureKind::basic_mixed, 1U, 2'000U),
        {4U, 1'024U});
    REQUIRE(one->start());
    REQUIRE(many->start());

    REQUIRE(one->update(std::chrono::duration<double>{0.0}));
    CHECK(one->summary().pending_records == 3U);
    REQUIRE(one->update(std::chrono::duration<double>{0.25}));
    REQUIRE(one->update(std::chrono::duration<double>{1.5}));
    REQUIRE(one->update(std::chrono::duration<double>{0.001}));
    REQUIRE(many->update(std::chrono::duration<double>{99.0}));
    REQUIRE(one->world_state().runtime_observation());
    REQUIRE(many->world_state().runtime_observation());
    CHECK(one->summary().application_updates == 4U);
    CHECK(many->summary().application_updates == 1U);
    CHECK(one->world_state().elapsed_seconds() !=
          many->world_state().elapsed_seconds());
    CHECK(one->world_state().runtime_observation()->canonical_state_hash ==
          many->world_state().runtime_observation()->canonical_state_hash);
    CHECK(*one->world_state().runtime_observation() ==
          *many->world_state().runtime_observation());

    auto byte_limited_fixture = fixture(
        goldsrc::RuntimeReplayFixtureKind::basic_mixed, 1U, 4'000U);
    const auto first_size = byte_limited_fixture.records.front().payload.bytes.size();
    auto byte_limited = source(
        std::move(byte_limited_fixture), {4U, first_size});
    REQUIRE(byte_limited->start());
    REQUIRE(byte_limited->update(std::chrono::duration<double>{0.0}));
    CHECK(byte_limited->summary().applied_records == 1U);
    CHECK(byte_limited->summary().pending_records == 3U);
    CHECK(byte_limited->state() == app::RuntimeReplaySourceState::running);
}

TEST_CASE("Oversized and malformed replay records stop without retry or mutation",
          "[application-runtime-replay][failure][transaction]")
{
    SECTION("oversized record") {
        auto replay = fixture();
        const auto too_small = replay.records.front().payload.bytes.size() - 1U;
        auto playback = source(std::move(replay), {4U, too_small});
        REQUIRE(playback->start());
        const auto initial = playback->world_state().runtime_observation();
        const auto update = playback->update(std::chrono::duration<double>{0.0});
        CHECK_FALSE(update);
        CHECK(playback->state() == app::RuntimeReplaySourceState::failed);
        CHECK(playback->summary().applied_records == 0U);
        CHECK(playback->summary().failed_records == 1U);
        CHECK(playback->summary().pending_records == 3U);
        CHECK(playback->summary().session_finish_count == 1U);
        CHECK(playback->last_error()->code ==
              app::RuntimeReplaySourceErrorCode::record_too_large);
        CHECK(playback->world_state().runtime_observation() == initial);
    }

    SECTION("missing entity base after one good record") {
        auto playback = source(fixture(
            goldsrc::RuntimeReplayFixtureKind::missing_entity_base));
        REQUIRE(playback->start());
        const auto update = playback->update(std::chrono::duration<double>{0.0});
        CHECK_FALSE(update);
        const auto summary = playback->summary();
        CHECK(summary.applied_records == 1U);
        CHECK(summary.failed_records == 1U);
        CHECK(summary.pending_records == 1U);
        CHECK(summary.application_updates == 1U);
        CHECK(summary.session_finish_count == 1U);
        REQUIRE(summary.terminal_error);
        CHECK(summary.terminal_error->code ==
              app::RuntimeReplaySourceErrorCode::record_failed);
        REQUIRE(summary.terminal_error->session_error);
        CHECK(summary.terminal_error->session_error->recovery ==
              goldsrc::RuntimeReplayRecoveryStatus::
                  entity_full_snapshot_required);
        REQUIRE(playback->world_state().runtime_observation());
        const auto committed = playback->world_state().runtime_observation();
        CHECK(committed->publication_revision == 2U);
        CHECK(playback->world_state().runtime_publication_revision() == 2U);
        CHECK_FALSE(playback->update(std::chrono::duration<double>{0.0}));
        CHECK(playback->world_state().runtime_observation() == committed);
        CHECK(playback->summary().pending_records == 1U);
    }
}

TEST_CASE("Replay stop update limit and invalid elapsed are terminal and bounded",
          "[application-runtime-replay][lifecycle][limit]")
{
    SECTION("explicit stop is idempotent") {
        auto playback = source(fixture());
        REQUIRE(playback->start());
        playback->stop();
        playback->stop();
        CHECK(playback->state() == app::RuntimeReplaySourceState::stopped);
        CHECK(playback->summary().pending_records == 4U);
        CHECK(playback->summary().session_finish_count == 1U);
        CHECK(playback->last_error()->code ==
              app::RuntimeReplaySourceErrorCode::explicitly_stopped);
    }

    SECTION("application limit is not EOF") {
        auto playback = source(fixture(), {1U, 1'024U});
        REQUIRE(playback->start());
        REQUIRE(playback->update(std::chrono::duration<double>{0.0}));
        playback->stop(
            app::RuntimeReplayStopReason::application_update_limit_reached);
        CHECK(playback->state() == app::RuntimeReplaySourceState::stopped);
        CHECK(playback->summary().applied_records == 1U);
        CHECK(playback->summary().pending_records == 3U);
        CHECK(playback->last_error()->code ==
              app::RuntimeReplaySourceErrorCode::
                  application_update_limit_reached);
    }

    SECTION("invalid application clocks fail without decoded changes") {
        for (const auto invalid : {
                 -1.0, (std::numeric_limits<double>::infinity)(),
                 (std::numeric_limits<double>::quiet_NaN)()}) {
            auto playback = source(fixture());
            REQUIRE(playback->start());
            const auto initial = playback->world_state().runtime_observation();
            CHECK_FALSE(playback->update(std::chrono::duration<double>{invalid}));
            CHECK(playback->state() == app::RuntimeReplaySourceState::failed);
            CHECK(playback->last_error()->code ==
                  app::RuntimeReplaySourceErrorCode::invalid_elapsed_time);
            CHECK(playback->world_state().runtime_observation() == initial);
            CHECK(playback->summary().session_finish_count == 1U);
        }
    }
}

TEST_CASE("Replay restart begins a new generation without old bases",
          "[application-runtime-replay][restart][generation]")
{
    auto playback = source(fixture(), {4U, 1'024U});
    REQUIRE(playback->start());
    REQUIRE(playback->update(std::chrono::duration<double>{0.0}));
    CHECK(playback->state() == app::RuntimeReplaySourceState::completed);
    const auto generation_one_hash =
        playback->world_state().runtime_observation()->canonical_state_hash;

    auto same_generation = fixture(
        goldsrc::RuntimeReplayFixtureKind::basic_mixed, 1U, 5'000U);
    const auto rejected = playback->restart(std::move(same_generation));
    CHECK_FALSE(rejected);
    CHECK(rejected.error->code ==
          app::RuntimeReplaySourceErrorCode::restart_generation_not_newer);
    CHECK(playback->world_state().runtime_observation()->canonical_state_hash ==
          generation_one_hash);

    auto generation_two = fixture(
        goldsrc::RuntimeReplayFixtureKind::basic_mixed, 2U, 6'000U);
    generation_two.records.erase(
        generation_two.records.begin(), generation_two.records.begin() + 2);
    REQUIRE(playback->restart(std::move(generation_two)));
    REQUIRE(playback->world_state().runtime_observation());
    CHECK(playback->world_state().runtime_observation()->generation == 2U);
    const auto generation_two_initial =
        playback->world_state().runtime_observation();
    const auto update = playback->update(std::chrono::duration<double>{0.0});
    CHECK_FALSE(update);
    CHECK(playback->state() == app::RuntimeReplaySourceState::failed);
    REQUIRE(playback->last_error()->session_error);
    CHECK(playback->last_error()->session_error->recovery ==
          goldsrc::RuntimeReplayRecoveryStatus::clientdata_no_base_required);
    CHECK(playback->world_state().runtime_observation() ==
          generation_two_initial);
}

TEST_CASE("Application replay source rejects invalid construction",
          "[application-runtime-replay][configuration]")
{
    CHECK(app::valid_runtime_replay_scheduling_limits({1U, 1U}));
    CHECK_FALSE(app::valid_runtime_replay_scheduling_limits({0U, 1U}));
    CHECK_FALSE(app::valid_runtime_replay_scheduling_limits({1U, 0U}));
    CHECK_FALSE(app::valid_runtime_replay_scheduling_limits({
        app::kMaximumApplicationReplayRecordsPerUpdate + 1U, 1U}));
    CHECK_FALSE(app::valid_runtime_replay_scheduling_limits({
        1U, app::kMaximumApplicationReplayBytesPerUpdate + 1U}));

    auto invalid = fixture();
    invalid.initialization.generation = 0U;
    const auto created = app::RuntimeReplaySceneSource::create(
        std::move(invalid));
    CHECK_FALSE(created);
    REQUIRE(created.error);
    CHECK(created.error->code ==
          app::RuntimeReplaySourceErrorCode::invalid_configuration);

    const auto absent_capture = std::filesystem::temp_directory_path() /
        ("hlclient-absent-functional-capture-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto capture = app::RuntimeReplaySceneSource::create_capture(
        absent_capture);
    CHECK_FALSE(capture);
    CHECK_FALSE(capture.source);
    REQUIRE(capture.error);
    CHECK(capture.error->code ==
          app::RuntimeReplaySourceErrorCode::capture_load_failed);
    REQUIRE(capture.error->capture_error);
    CHECK(capture.error->capture_error->code ==
          goldsrc::RuntimeReplayCaptureErrorCode::corpus_load_failed);
}
