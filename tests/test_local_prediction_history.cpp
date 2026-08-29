#include "local_movement_test_fixture.hpp"

#include <hlclient/prediction/prediction_history.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>

namespace {

namespace goldsrc = hlclient::goldsrc;
namespace movement = hlclient::movement;
namespace prediction = hlclient::prediction;
namespace fixture = hlclient::tests::local_movement;

[[nodiscard]] goldsrc::GoldSrcUserCmdSequence sequence(
    const std::uint32_t value)
{
    const auto created = goldsrc::GoldSrcUserCmdSequence::create(value);
    if (!created) {
        std::terminate();
    }
    return *created;
}

[[nodiscard]] prediction::PredictionSessionIdentity session_for(
    const movement::LocalPlayerMovementState& initial_state)
{
    prediction::PredictionSessionIdentity session;
    session.session_generation = 2U;
    session.prediction_generation = 6U;
    session.collision_world_primary = 0x1234U;
    session.collision_world_secondary = 0x5678U;
    session.collision_world_revision = 3U;
    session.collision_scene_signature = 0x9012U;
    session.movement_environment_signature = 0x3456U;
    session.movement_config_signature = 0x7890U;
    session.spawn_initial_state_signature =
        movement::local_player_movement_state_signature(initial_state);
    return session;
}

[[nodiscard]] prediction::LocalPredictionHistoryState::CreateResult
create_initial(const prediction::LocalPredictionHistoryLimits& limits = {})
{
    auto initial_state = fixture::make_state();
    const auto session = session_for(initial_state);
    return prediction::LocalPredictionHistoryState::create_initial(
        std::move(initial_state), session, limits);
}

[[nodiscard]] std::shared_ptr<const prediction::LocalPredictionHistoryState>
initial_history(const prediction::LocalPredictionHistoryLimits& limits = {})
{
    auto created = create_initial(limits);
    if (!created.history) {
        std::terminate();
    }
    return std::move(created.history);
}

[[nodiscard]] movement::LocalPlayerMovementState post_state_for(
    const movement::LocalPlayerMovementState& pre_state,
    const std::uint32_t command_sequence)
{
    auto info = movement::local_player_movement_state_create_info(pre_state);
    info.origin.x += 1.0F;
    info.source_command_sequence = command_sequence;
    info.simulation_time_nanoseconds += 10'000'000U;
    ++info.state_revision;
    const auto created = movement::LocalPlayerMovementState::create(info);
    if (!created.state) {
        std::terminate();
    }
    return *created.state;
}

[[nodiscard]] prediction::PredictedCommandAppend make_append(
    const movement::LocalPlayerMovementState& pre_state,
    const std::uint32_t command_sequence,
    const prediction::PredictionTouchSummary touch_summary = {})
{
    prediction::PredictedCommandAppend append;
    append.command = std::make_shared<const goldsrc::GoldSrcUserCmdState>(
        fixture::make_command(command_sequence, 10U, 100.0F));
    append.pre_command_state =
        std::make_shared<const movement::LocalPlayerMovementState>(pre_state);
    append.post_command_state =
        std::make_shared<const movement::LocalPlayerMovementState>(
            post_state_for(pre_state, command_sequence));
    append.simulation_statistics.command_count = 1U;
    append.simulation_statistics.substep_count = 1U;
    append.touch_summary = touch_summary;
    return append;
}

[[nodiscard]] prediction::LocalPredictionAppendResult append_one(
    const prediction::LocalPredictionHistoryState& history,
    const prediction::PredictedCommandAppend& append)
{
    const std::array commands{append};
    return prediction::append_local_prediction_commands(history, commands);
}

void check_append_error(
    const prediction::LocalPredictionAppendResult& result,
    const prediction::PredictionErrorCode expected)
{
    REQUIRE_FALSE(result);
    CHECK_FALSE(result.history);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
    CHECK_FALSE(result.error->context.empty());
}

void check_create_error(
    const prediction::LocalPredictionHistoryState::CreateResult& result,
    const prediction::PredictionErrorCode expected)
{
    REQUIRE_FALSE(result);
    CHECK_FALSE(result.history);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
    CHECK_FALSE(result.error->context.empty());
}

} // namespace

TEST_CASE("Local prediction history starts from an immutable command-zero anchor",
    "[prediction][history][initial]")
{
    const auto created = create_initial();

    REQUIRE(created);
    REQUIRE(created.history);
    CHECK_FALSE(created.error);
    const auto& history = *created.history;
    REQUIRE(history.session().valid());
    CHECK(history.anchor().session() == history.session());
    REQUIRE(history.anchor().movement_state());
    CHECK(history.anchor().movement_state()->source_command_sequence() == 0U);
    CHECK_FALSE(history.anchor().acknowledgement().has_sequence());
    CHECK_FALSE(history.anchor().authority_update_identity());
    CHECK_FALSE(history.anchor().authority_update_ordinal());
    CHECK_FALSE(history.anchor().authority_state_signature());
    CHECK(history.anchor().state_signature() ==
        movement::local_player_movement_state_signature(
            *history.anchor().movement_state()));
    CHECK(history.current_predicted_state() ==
        history.anchor().movement_state());
    CHECK(history.size() == 0U);
    CHECK(history.entries().empty());
    CHECK_FALSE(history.oldest_command_sequence());
    CHECK_FALSE(history.newest_command_sequence());
    CHECK(history.find_exact(goldsrc::GoldSrcUserCmdSequence{}) == nullptr);
    CHECK(history.revision() == 1U);
    CHECK(history.accounted_state_bytes() ==
        sizeof(movement::LocalPlayerMovementState));
    CHECK(history.accounted_command_bytes() == 0U);
    CHECK(history.accounted_touch_summary_bytes() == 0U);
    CHECK(history.statistics().publication_count == 1U);
    CHECK(history.statistics().total_appended_commands == 0U);
    CHECK(history.statistics().high_water_mark == 0U);
    CHECK(prediction::local_prediction_history_signature(history) != 0U);
}

TEST_CASE("Local prediction history appends contiguous commands with exact lookup",
    "[prediction][history][append]")
{
    const auto initial = initial_history();
    const auto first = make_append(*initial->current_predicted_state(), 1U);
    const auto second = make_append(*first.post_command_state, 2U);
    const std::array commands{first, second};

    const auto appended =
        prediction::append_local_prediction_commands(*initial, commands);

    REQUIRE(appended);
    REQUIRE(appended.history);
    CHECK_FALSE(appended.error);
    CHECK(appended.appended_command_count == 2U);
    CHECK(appended.history_size == 2U);
    CHECK(appended.prediction_revision == 2U);
    CHECK(appended.final_predicted_state == second.post_command_state);
    CHECK(appended.history->current_predicted_state() ==
        second.post_command_state);
    REQUIRE(appended.history->oldest_command_sequence());
    REQUIRE(appended.history->newest_command_sequence());
    CHECK(appended.history->oldest_command_sequence()->value() == 1U);
    CHECK(appended.history->newest_command_sequence()->value() == 2U);

    const auto* first_entry = appended.history->find_exact(sequence(1U));
    const auto* second_entry = appended.history->find_exact(sequence(2U));
    REQUIRE(first_entry != nullptr);
    REQUIRE(second_entry != nullptr);
    CHECK(appended.history->find_exact(sequence(3U)) == nullptr);
    CHECK(first_entry->command_sequence().value() == 1U);
    CHECK(first_entry->command() == first.command);
    CHECK(first_entry->pre_command_state() == first.pre_command_state);
    CHECK(first_entry->post_command_state() == first.post_command_state);
    CHECK(first_entry->pre_state_signature() ==
        movement::local_player_movement_state_signature(
            *first.pre_command_state));
    CHECK(first_entry->post_state_signature() ==
        movement::local_player_movement_state_signature(
            *first.post_command_state));
    CHECK(first_entry->simulation_statistics().command_count == 1U);
    CHECK(first_entry->prediction_generation() ==
        initial->session().prediction_generation);
    CHECK(first_entry->entry_ordinal() == 1U);
    CHECK(second_entry->entry_ordinal() == 2U);
    CHECK(appended.history->statistics().total_appended_commands == 2U);
    CHECK(appended.history->statistics().publication_count == 2U);
    CHECK(appended.history->statistics().high_water_mark == 2U);
    CHECK(initial->size() == 0U);
    CHECK(initial->revision() == 1U);
}

TEST_CASE("Local prediction history classifies non-contiguous sequences",
    "[prediction][history][sequence-validation]")
{
    SECTION("gap")
    {
        const auto history = initial_history();
        const auto result = append_one(
            *history, make_append(*history->current_predicted_state(), 2U));
        check_append_error(result,
            prediction::PredictionErrorCode::prediction_command_gap);
        REQUIRE(result.error->command_sequence);
        CHECK(result.error->command_sequence->value() == 2U);
        CHECK(history->size() == 0U);
    }
    SECTION("duplicate")
    {
        const auto initial = initial_history();
        const auto first = append_one(
            *initial, make_append(*initial->current_predicted_state(), 1U));
        REQUIRE(first.history);
        const auto duplicate = append_one(
            *first.history,
            make_append(*first.history->current_predicted_state(), 1U));
        check_append_error(duplicate,
            prediction::PredictionErrorCode::duplicate_predicted_command);
    }
    SECTION("out of order")
    {
        const auto initial = initial_history();
        const auto first = make_append(*initial->current_predicted_state(), 1U);
        const auto second = make_append(*first.post_command_state, 2U);
        const std::array commands{first, second};
        const auto two =
            prediction::append_local_prediction_commands(*initial, commands);
        REQUIRE(two.history);
        const auto old = append_one(
            *two.history,
            make_append(*two.history->current_predicted_state(), 1U));
        check_append_error(old,
            prediction::PredictionErrorCode::out_of_order_predicted_command);
    }
}

TEST_CASE("Local prediction history rejects broken immutable state continuity",
    "[prediction][history][continuity]")
{
    SECTION("pre-state differs from current publication")
    {
        const auto history = initial_history();
        auto mismatched_pre_info =
            movement::local_player_movement_state_create_info(
                *history->current_predicted_state());
        mismatched_pre_info.origin.y += 5.0F;
        const auto mismatched_pre_created =
            movement::LocalPlayerMovementState::create(mismatched_pre_info);
        REQUIRE(mismatched_pre_created.state);
        auto append = make_append(*history->current_predicted_state(), 1U);
        append.pre_command_state =
            std::make_shared<const movement::LocalPlayerMovementState>(
                *mismatched_pre_created.state);
        check_append_error(append_one(*history, append),
            prediction::PredictionErrorCode::prediction_command_gap);
    }
    SECTION("post-state sequence differs from command")
    {
        const auto history = initial_history();
        auto append = make_append(*history->current_predicted_state(), 1U);
        append.post_command_state =
            std::make_shared<const movement::LocalPlayerMovementState>(
                post_state_for(*history->current_predicted_state(), 2U));
        check_append_error(append_one(*history, append),
            prediction::PredictionErrorCode::prediction_command_gap);
    }
    SECTION("append contains a null immutable value")
    {
        const auto history = initial_history();
        const prediction::PredictedCommandAppend append;
        check_append_error(append_one(*history, append),
            prediction::PredictionErrorCode::invalid_configuration);
    }
}

TEST_CASE("Local prediction history applies entry and authority-delay backpressure",
    "[prediction][history][backpressure]")
{
    SECTION("exact configured bound succeeds")
    {
        prediction::LocalPredictionHistoryLimits limits;
        limits.maximum_entries = 1U;
        limits.maximum_authority_delay_commands = 1U;
        const auto history = initial_history(limits);
        const auto result = append_one(
            *history, make_append(*history->current_predicted_state(), 1U));
        REQUIRE(result.history);
        CHECK(result.history->size() == 1U);
    }
    SECTION("entry capacity fails closed without eviction")
    {
        prediction::LocalPredictionHistoryLimits limits;
        limits.maximum_entries = 2U;
        limits.maximum_authority_delay_commands = 2U;
        const auto initial = initial_history(limits);
        const auto first = make_append(*initial->current_predicted_state(), 1U);
        const auto second = make_append(*first.post_command_state, 2U);
        const std::array commands{first, second};
        const auto full =
            prediction::append_local_prediction_commands(*initial, commands);
        REQUIRE(full.history);
        const auto blocked = append_one(
            *full.history,
            make_append(*full.history->current_predicted_state(), 3U));
        check_append_error(blocked,
            prediction::PredictionErrorCode::prediction_history_backpressure);
        CHECK(full.history->size() == 2U);
        CHECK(full.history->find_exact(sequence(1U)) != nullptr);
        CHECK(full.history->find_exact(sequence(2U)) != nullptr);
    }
    SECTION("authority-delay bound applies to an append batch")
    {
        prediction::LocalPredictionHistoryLimits limits;
        limits.maximum_entries = 3U;
        limits.maximum_authority_delay_commands = 1U;
        const auto history = initial_history(limits);
        const auto first = make_append(*history->current_predicted_state(), 1U);
        const auto second = make_append(*first.post_command_state, 2U);
        const std::array commands{first, second};
        check_append_error(
            prediction::append_local_prediction_commands(*history, commands),
            prediction::PredictionErrorCode::prediction_history_backpressure);
    }
}

TEST_CASE("Local prediction history accounts retained bytes at exact bounds",
    "[prediction][history][memory-bounds]")
{
    const auto touch_summary = prediction::summarize_prediction_touches(
        std::span<const movement::PlayerMovementTouch>{}, false, false);
    const auto exact_state_bytes =
        sizeof(movement::LocalPlayerMovementState) * 3U;
    const auto exact_command_bytes = sizeof(goldsrc::GoldSrcUserCmdState);
    const auto exact_touch_bytes = touch_summary.accounted_bytes;

    SECTION("exact byte limits succeed")
    {
        prediction::LocalPredictionHistoryLimits limits;
        limits.maximum_retained_state_bytes = exact_state_bytes;
        limits.maximum_retained_command_bytes = exact_command_bytes;
        limits.maximum_touch_summary_bytes = exact_touch_bytes;
        const auto history = initial_history(limits);
        const auto result = append_one(
            *history, make_append(
                *history->current_predicted_state(), 1U, touch_summary));
        REQUIRE(result.history);
        CHECK(result.history->accounted_state_bytes() == exact_state_bytes);
        CHECK(result.history->accounted_command_bytes() == exact_command_bytes);
        CHECK(result.history->accounted_touch_summary_bytes() ==
            exact_touch_bytes);
    }
    SECTION("state byte limit")
    {
        prediction::LocalPredictionHistoryLimits limits;
        limits.maximum_retained_state_bytes = exact_state_bytes - 1U;
        const auto history = initial_history(limits);
        check_append_error(
            append_one(*history,
                make_append(*history->current_predicted_state(), 1U)),
            prediction::PredictionErrorCode::prediction_history_full);
    }
    SECTION("command byte limit")
    {
        prediction::LocalPredictionHistoryLimits limits;
        limits.maximum_retained_command_bytes = exact_command_bytes - 1U;
        const auto history = initial_history(limits);
        check_append_error(
            append_one(*history,
                make_append(*history->current_predicted_state(), 1U)),
            prediction::PredictionErrorCode::prediction_history_full);
    }
    SECTION("touch-summary byte limit")
    {
        prediction::LocalPredictionHistoryLimits limits;
        limits.maximum_touch_summary_bytes = exact_touch_bytes - 1U;
        const auto history = initial_history(limits);
        check_append_error(
            append_one(*history, make_append(
                *history->current_predicted_state(), 1U, touch_summary)),
            prediction::PredictionErrorCode::prediction_history_full);
    }
}

TEST_CASE("Local prediction history validates all externally bounded limits",
    "[prediction][history][limits]")
{
    const auto check_invalid = [](const auto mutate) {
        prediction::LocalPredictionHistoryLimits limits;
        mutate(limits);
        CHECK_FALSE(prediction::valid_local_prediction_history_limits(limits));
        check_create_error(create_initial(limits),
            prediction::PredictionErrorCode::invalid_configuration);
    };

    check_invalid([](auto& value) { value.maximum_entries = 0U; });
    check_invalid([](auto& value) {
        value.maximum_entries =
            prediction::kHardMaximumPredictionHistoryEntries + 1U;
    });
    check_invalid([](auto& value) {
        value.maximum_retained_state_bytes = 0U;
    });
    check_invalid([](auto& value) {
        value.maximum_retained_command_bytes = 0U;
    });
    check_invalid([](auto& value) {
        value.maximum_authority_delay_commands = 0U;
    });
    check_invalid([](auto& value) {
        value.maximum_authority_delay_commands = value.maximum_entries + 1U;
    });
    check_invalid([](auto& value) { value.maximum_replay_commands = 0U; });
    check_invalid([](auto& value) {
        value.maximum_replay_commands =
            prediction::kHardMaximumPredictionReplayCommands + 1U;
    });
    check_invalid([](auto& value) { value.maximum_history_revision = 0U; });
    check_invalid([](auto& value) {
        value.maximum_touch_summary_bytes = 0U;
    });
}

TEST_CASE("Local prediction history reports exhausted publication revision",
    "[prediction][history][revision]")
{
    prediction::LocalPredictionHistoryLimits limits;
    limits.maximum_history_revision = 1U;
    const auto history = initial_history(limits);
    const auto result = append_one(
        *history, make_append(*history->current_predicted_state(), 1U));
    check_append_error(result,
        prediction::PredictionErrorCode::revision_exhausted);
    CHECK(history->revision() == 1U);
    CHECK(history->size() == 0U);
}

TEST_CASE("Local prediction history publications own their immutable inputs",
    "[prediction][history][ownership]")
{
    const auto initial = initial_history();
    auto append = make_append(*initial->current_predicted_state(), 1U);
    const auto command = append.command;
    const auto pre_state = append.pre_command_state;
    const auto post_state = append.post_command_state;
    auto result = append_one(*initial, append);
    REQUIRE(result.history);

    append.command.reset();
    append.pre_command_state.reset();
    append.post_command_state.reset();

    const auto* retained = result.history->find_exact(sequence(1U));
    REQUIRE(retained != nullptr);
    CHECK(retained->command() == command);
    CHECK(retained->pre_command_state() == pre_state);
    CHECK(retained->post_command_state() == post_state);
    CHECK(result.history->current_predicted_state() == post_state);
    CHECK(initial->size() == 0U);
    CHECK(initial->current_predicted_state() ==
        initial->anchor().movement_state());
}

TEST_CASE("Empty local prediction append republishes without mutation",
    "[prediction][history][empty-append]")
{
    const auto initial = initial_history();
    const auto before_signature =
        prediction::local_prediction_history_signature(*initial);

    const auto republished = prediction::append_local_prediction_commands(
        *initial, std::span<const prediction::PredictedCommandAppend>{});

    REQUIRE(republished);
    REQUIRE(republished.history);
    CHECK(republished.history.get() != initial.get());
    CHECK(republished.appended_command_count == 0U);
    CHECK(republished.history_size == 0U);
    CHECK(republished.prediction_revision == initial->revision());
    CHECK(republished.history->statistics().publication_count ==
        initial->statistics().publication_count);
    CHECK(prediction::local_prediction_history_signature(
        *republished.history) == before_signature);
}

TEST_CASE("Prediction touch summaries retain bounded deterministic evidence",
    "[prediction][history][touch-summary]")
{
    std::array<movement::PlayerMovementTouch, 2U> touches;
    touches[0U].hit.kind = movement::PlayerMovementHitKind::world;
    touches[0U].hit.source_model_index = 1U;
    touches[0U].fraction = 0.25;
    touches[0U].phase = movement::PlayerMovementPhase::ground_probe;
    touches[0U].source_command_sequence = 1U;
    touches[1U].hit.kind =
        movement::PlayerMovementHitKind::explicit_synthetic_brush;
    touches[1U].hit.source_model_index = 9U;
    touches[1U].fraction = 0.75;
    touches[1U].phase = movement::PlayerMovementPhase::direct_slide;
    touches[1U].source_command_sequence = 1U;

    const auto first =
        prediction::summarize_prediction_touches(touches, true, false);
    const auto same =
        prediction::summarize_prediction_touches(touches, true, false);
    const auto changed =
        prediction::summarize_prediction_touches(touches, false, false);

    CHECK(first.touch_count == 2U);
    REQUIRE(first.first_hit_kind);
    REQUIRE(first.last_hit_kind);
    CHECK(*first.first_hit_kind == movement::PlayerMovementHitKind::world);
    CHECK(*first.last_hit_kind ==
        movement::PlayerMovementHitKind::explicit_synthetic_brush);
    CHECK(first.start_solid);
    CHECK_FALSE(first.all_solid);
    CHECK(first.accounted_bytes == sizeof(prediction::PredictionTouchSummary));
    CHECK(first.deterministic_signature != 0U);
    CHECK(first.deterministic_signature == same.deterministic_signature);
    CHECK(first.deterministic_signature != changed.deterministic_signature);
}

TEST_CASE("Local prediction history signature changes with publication content",
    "[prediction][history][signature]")
{
    const auto initial = initial_history();
    const auto before = prediction::local_prediction_history_signature(*initial);
    const auto appended = append_one(
        *initial, make_append(*initial->current_predicted_state(), 1U));
    REQUIRE(appended.history);
    const auto after =
        prediction::local_prediction_history_signature(*appended.history);

    CHECK(before != 0U);
    CHECK(after != 0U);
    CHECK(before != after);
    CHECK(after ==
        prediction::local_prediction_history_signature(*appended.history));
}

TEST_CASE("Prediction history signature binds complete retained command content",
    "[prediction][history][signature][command-content]")
{
    const auto first_initial = initial_history();
    const auto second_initial = initial_history();
    auto first_append = make_append(
        *first_initial->current_predicted_state(), 1U);
    auto second_append = make_append(
        *second_initial->current_predicted_state(), 1U);
    second_append.command =
        std::make_shared<const goldsrc::GoldSrcUserCmdState>(
            fixture::make_command(1U, 10U, 125.0F));

    const auto first = append_one(*first_initial, first_append);
    const auto second = append_one(*second_initial, second_append);
    REQUIRE(first.history);
    REQUIRE(second.history);
    REQUIRE(first.history->entries().size() == 1U);
    REQUIRE(second.history->entries().size() == 1U);
    CHECK(first.history->entries().front().command_sequence() ==
        second.history->entries().front().command_sequence());
    CHECK(first.history->entries().front().pre_state_signature() ==
        second.history->entries().front().pre_state_signature());
    CHECK(first.history->entries().front().post_state_signature() ==
        second.history->entries().front().post_state_signature());
    CHECK(first.history->entries().front().command()->forward_move() !=
        second.history->entries().front().command()->forward_move());

    CHECK(prediction::local_prediction_history_signature(*first.history) !=
        prediction::local_prediction_history_signature(*second.history));
}
