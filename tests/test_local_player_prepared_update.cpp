#include "local_movement_test_fixture.hpp"

#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <hlclient/local_player/local_player_movement_controller.hpp>
#include <hlclient/prediction/local_prediction.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace fixture = hlclient::tests::local_movement;
namespace gameplay = hlclient::gameplay_input;
namespace input = hlclient::input;
namespace local_player = hlclient::local_player;
namespace prediction = hlclient::prediction;

class IntentStream final {
public:
    IntentStream()
    {
        auto built = gameplay::GameplayInputBindings::project_default_v1();
        REQUIRE(built);
        bindings_.emplace(std::move(*built.bindings));
    }

    [[nodiscard]] gameplay::GameplayInputIntent next(
        const std::initializer_list<input::InputEvent> events = {})
    {
        tracker_.begin_frame();
        for (const auto& event : events) {
            tracker_.apply_event(event);
        }
        const auto snapshot = tracker_.publish_snapshot();
        tracker_.end_frame();
        auto built = gameplay::GameplayInputIntentBuilder{}.build(
            snapshot, *bindings_, gameplay::MouseLookConfig{}, 0.01);
        REQUIRE(built);
        return std::move(*built.intent);
    }

private:
    input::InputStateTracker tracker_;
    std::optional<gameplay::GameplayInputBindings> bindings_;
};

[[nodiscard]] local_player::LocalPlayerMovementController make_controller()
{
    return {fixture::make_state(), fixture::make_environment()};
}

[[nodiscard]] prediction::PredictionSessionIdentity make_session(
    const hlclient::movement::LocalPlayerMovementState& initial)
{
    prediction::PredictionSessionIdentity session;
    session.session_generation = 1U;
    session.prediction_generation = 1U;
    session.collision_world_primary = 11U;
    session.collision_world_secondary = 12U;
    session.collision_world_revision = 1U;
    session.collision_scene_signature = 13U;
    session.movement_environment_signature =
        prediction::prediction_movement_environment_signature(
            fixture::make_environment());
    session.movement_config_signature =
        prediction::prediction_movement_config_signature({});
    session.spawn_initial_state_signature =
        hlclient::movement::local_player_movement_state_signature(initial);
    REQUIRE(session.valid());
    return session;
}

[[nodiscard]] std::vector<prediction::PredictedCommandAppend> history_appends(
    const local_player::LocalPlayerMovementPreparedUpdate& prepared)
{
    std::vector<prediction::PredictedCommandAppend> appends;
    appends.reserve(prepared.commands().size());
    for (const auto& item : prepared.commands()) {
        prediction::PredictionTouchSummary touch;
        touch.touch_count = item.touch_summary.touch_count;
        touch.first_hit_kind = item.touch_summary.first_hit_kind;
        touch.last_hit_kind = item.touch_summary.last_hit_kind;
        touch.start_solid = item.statistics.start_solid_count != 0U;
        touch.all_solid = item.statistics.all_solid_count != 0U;
        touch.deterministic_signature =
            item.touch_summary.deterministic_signature;
        touch.accounted_bytes = sizeof(prediction::PredictionTouchSummary);
        appends.push_back({item.command, item.pre_state, item.post_state,
            item.statistics, touch});
    }
    return appends;
}

TEST_CASE("Prepared local movement is immutable until one atomic commit",
    "[prediction][prepared-update][transaction]")
{
    auto controller = make_controller();
    fixture::DeterministicLocalMovementCollision collision;
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch scratch;
    IntentStream intent_stream;
    const auto focused = intent_stream.next({input::InputEvent::focus_gained(),
        input::InputEvent::capture_acquired(),
        input::InputEvent::key_pressed(input::PhysicalKey::w)});

    const auto initialized = controller.update(0, focused, collision, scratch);
    REQUIRE(initialized);
    const auto before_revision = controller.revision();
    const auto before_signature =
        hlclient::movement::local_player_movement_state_signature(
            controller.player_state());
    const auto held = intent_stream.next();
    auto result = controller.prepare_update(
        30'000'000, held, collision, scratch);
    REQUIRE(result);
    REQUIRE(result.prepared_update);
    auto& prepared = *result.prepared_update;

    CHECK(controller.revision() == before_revision);
    CHECK(hlclient::movement::local_player_movement_state_signature(
              controller.player_state()) == before_signature);
    REQUIRE(prepared.commands().size() == 3U);
    CHECK(prepared.commands().front().command->command_sequence().value() ==
        1U);
    CHECK(prepared.commands().back().command->command_sequence().value() == 3U);
    for (std::size_t index = 1U; index < prepared.commands().size(); ++index) {
        CHECK(prepared.commands()[index - 1U].post_state ==
            prepared.commands()[index].pre_state);
    }
    CHECK(prepared.commands().back().post_state.get() != nullptr);
    CHECK(hlclient::movement::local_player_movement_state_signature(
              *prepared.commands().back().post_state) ==
        hlclient::movement::local_player_movement_state_signature(
            prepared.final_player_state()));

    const auto committed =
        controller.commit_prepared_update(std::move(prepared));
    REQUIRE(committed);
    CHECK(controller.revision() == before_revision + 1U);
    CHECK(committed.generated_command_count == 3U);
    CHECK(controller.player_state().source_command_sequence() == 3U);

    const auto duplicate_commit =
        controller.commit_prepared_update(std::move(prepared));
    REQUIRE_FALSE(duplicate_commit);
    REQUIRE(duplicate_commit.error);
    CHECK(duplicate_commit.error->code ==
        local_player::LocalPlayerMovementControllerErrorCode::
            prepared_update_already_consumed);
}

TEST_CASE("Prepared movement ownership transfers exactly once across moves",
    "[prediction][prepared-update][move][ownership]")
{
    STATIC_REQUIRE(std::is_nothrow_move_constructible_v<
        local_player::LocalPlayerMovementPreparedUpdate>);
    STATIC_REQUIRE(std::is_nothrow_move_assignable_v<
        local_player::LocalPlayerMovementPreparedUpdate>);

    auto controller = make_controller();
    fixture::DeterministicLocalMovementCollision collision;
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch scratch;
    IntentStream intent_stream;
    const auto focused = intent_stream.next({input::InputEvent::focus_gained(),
        input::InputEvent::capture_acquired()});
    REQUIRE(controller.update(0, focused, collision, scratch));
    const auto idle = intent_stream.next();

    SECTION("move construction invalidates the source token")
    {
        auto prepared = controller.prepare_update(
            10'000'000, idle, collision, scratch);
        REQUIRE(prepared);
        REQUIRE(prepared.prepared_update);
        auto source = std::move(*prepared.prepared_update);
        const auto expected_revision = source.base_controller_revision();
        const auto expected_identity = source.plan_identity();
        REQUIRE(expected_identity != 0U);
        REQUIRE(source.commands().size() == 1U);

        local_player::LocalPlayerMovementPreparedUpdate destination{
            std::move(source)};

        CHECK_FALSE(source.consumable());
        CHECK(source.base_controller_revision() == 0U);
        CHECK(source.plan_identity() == 0U);
        CHECK(source.commands().empty());
        const auto source_preflight =
            controller.preflight_prepared_update(source);
        REQUIRE(source_preflight);
        CHECK(source_preflight->code == local_player::
            LocalPlayerMovementControllerErrorCode::
                prepared_update_already_consumed);
        const auto revision_before_failed_commit = controller.revision();
        const auto source_commit =
            controller.commit_prepared_update(std::move(source));
        REQUIRE_FALSE(source_commit);
        REQUIRE(source_commit.error);
        CHECK(source_commit.error->code == local_player::
            LocalPlayerMovementControllerErrorCode::
                prepared_update_already_consumed);
        CHECK(controller.revision() == revision_before_failed_commit);

        CHECK(destination.consumable());
        CHECK(destination.base_controller_revision() == expected_revision);
        CHECK(destination.plan_identity() == expected_identity);
        REQUIRE(destination.commands().size() == 1U);
        const auto committed =
            controller.commit_prepared_update(std::move(destination));
        REQUIRE(committed);
        CHECK(committed.generated_command_count == 1U);
    }

    SECTION("move assignment invalidates the source and replaces the target")
    {
        auto one_command = controller.prepare_update(
            10'000'000, idle, collision, scratch);
        auto two_commands = controller.prepare_update(
            20'000'000, idle, collision, scratch);
        REQUIRE(one_command);
        REQUIRE(two_commands);
        auto source = std::move(*one_command.prepared_update);
        auto destination = std::move(*two_commands.prepared_update);
        REQUIRE(source.commands().size() == 1U);
        REQUIRE(destination.commands().size() == 2U);
        const auto expected_identity = source.plan_identity();
        const auto replaced_identity = destination.plan_identity();
        REQUIRE(expected_identity != replaced_identity);

        destination = std::move(source);

        CHECK_FALSE(source.consumable());
        CHECK(source.base_controller_revision() == 0U);
        CHECK(source.plan_identity() == 0U);
        CHECK(source.commands().empty());
        REQUIRE(controller.preflight_prepared_update(source));
        CHECK(destination.consumable());
        CHECK(destination.plan_identity() == expected_identity);
        CHECK(destination.plan_identity() != replaced_identity);
        REQUIRE(destination.commands().size() == 1U);

        const auto source_commit =
            controller.commit_prepared_update(std::move(source));
        REQUIRE_FALSE(source_commit);
        REQUIRE(source_commit.error);
        CHECK(source_commit.error->code == local_player::
            LocalPlayerMovementControllerErrorCode::
                prepared_update_already_consumed);
        const auto committed =
            controller.commit_prepared_update(std::move(destination));
        REQUIRE(committed);
        CHECK(committed.generated_command_count == 1U);
    }

    SECTION("self move assignment consumes the sole ownership token")
    {
        auto prepared = controller.prepare_update(
            10'000'000, idle, collision, scratch);
        REQUIRE(prepared);
        auto plan = std::move(*prepared.prepared_update);
        REQUIRE(plan.consumable());

        plan = std::move(plan);

        CHECK_FALSE(plan.consumable());
        CHECK(plan.base_controller_revision() == 0U);
        CHECK(plan.plan_identity() == 0U);
        CHECK(plan.commands().empty());
        const auto preflight = controller.preflight_prepared_update(plan);
        REQUIRE(preflight);
        CHECK(preflight->code == local_player::
            LocalPlayerMovementControllerErrorCode::
                prepared_update_already_consumed);
        const auto committed =
            controller.commit_prepared_update(std::move(plan));
        REQUIRE_FALSE(committed);
        REQUIRE(committed.error);
        CHECK(committed.error->code == local_player::
            LocalPlayerMovementControllerErrorCode::
                prepared_update_already_consumed);
    }
}

TEST_CASE("Prepared plans can be abandoned and stale plans cannot commit",
    "[prediction][prepared-update][abandon][stale]")
{
    auto controller = make_controller();
    fixture::DeterministicLocalMovementCollision collision;
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch scratch;
    IntentStream intent_stream;
    const auto focused = intent_stream.next({input::InputEvent::focus_gained(),
        input::InputEvent::capture_acquired()});
    REQUIRE(controller.update(0, focused, collision, scratch));
    const auto idle = intent_stream.next();

    auto abandoned = controller.prepare_update(
        10'000'000, idle, collision, scratch);
    REQUIRE(abandoned);
    const auto original_revision = controller.revision();
    const auto original_signature =
        hlclient::movement::local_player_movement_state_signature(
            controller.player_state());
    controller.abandon_prepared_update(*abandoned.prepared_update);
    CHECK(controller.revision() == original_revision);
    CHECK(hlclient::movement::local_player_movement_state_signature(
              controller.player_state()) == original_signature);
    CHECK_FALSE(abandoned.prepared_update->consumable());

    auto stale = controller.prepare_update(
        10'000'000, idle, collision, scratch);
    REQUIRE(stale);
    REQUIRE(controller.update(10'000'000, idle, collision, scratch));
    const auto stale_commit = controller.commit_prepared_update(
        std::move(*stale.prepared_update));
    REQUIRE_FALSE(stale_commit);
    REQUIRE(stale_commit.error);
    CHECK(stale_commit.error->code ==
        local_player::LocalPlayerMovementControllerErrorCode::
            stale_prepared_update);
}

TEST_CASE("History backpressure abandons movement without losing jump input",
    "[prediction][prepared-update][history][one-shot]")
{
    auto controller = make_controller();
    const auto initial_state = controller.player_state();
    prediction::LocalPredictionHistoryLimits limits;
    limits.maximum_entries = 1U;
    limits.maximum_authority_delay_commands = 1U;
    limits.maximum_replay_commands = 1U;
    auto initial_history =
        prediction::LocalPredictionHistoryState::create_initial(
            initial_state, make_session(initial_state), limits);
    REQUIRE(initial_history);

    fixture::DeterministicLocalMovementCollision collision;
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch scratch;
    IntentStream intent_stream;
    const auto jump_pressed = intent_stream.next({input::InputEvent::focus_gained(),
        input::InputEvent::capture_acquired(),
        input::InputEvent::key_pressed(input::PhysicalKey::space)});
    controller.capture_pending_input(jump_pressed);
    auto latched = controller.prepare_update(0, jump_pressed, collision, scratch);
    REQUIRE(latched);
    REQUIRE(latched.prepared_update->commands().empty());
    REQUIRE(controller.commit_prepared_update(
        std::move(*latched.prepared_update)));
    const auto jump_mask = gameplay::gameplay_button_mask(
        gameplay::GameplayButton::jump);
    CHECK((controller.pending_one_shots() & jump_mask) != 0U);

    const auto held = intent_stream.next();
    const auto before_revision = controller.revision();
    const auto before_signature =
        hlclient::movement::local_player_movement_state_signature(
            controller.player_state());
    auto prepared = controller.prepare_update(
        20'000'000, held, collision, scratch);
    REQUIRE(prepared);
    REQUIRE(prepared.prepared_update->commands().size() == 2U);
    CHECK((prepared.prepared_update->commands().front().command->buttons() &
              hlclient::goldsrc::kSyntheticGoldSrcButtonJump) != 0U);
    auto appends = history_appends(*prepared.prepared_update);
    const auto append = prediction::append_local_prediction_commands(
        *initial_history.history, appends);
    REQUIRE_FALSE(append);
    REQUIRE(append.error);
    CHECK(append.error->code ==
        prediction::PredictionErrorCode::prediction_history_backpressure);
    controller.abandon_prepared_update(*prepared.prepared_update);

    CHECK(controller.revision() == before_revision);
    CHECK(hlclient::movement::local_player_movement_state_signature(
              controller.player_state()) == before_signature);
    CHECK((controller.pending_one_shots() & jump_mask) != 0U);
    CHECK(initial_history.history->size() == 0U);

    auto retry = controller.prepare_update(
        10'000'000, held, collision, scratch);
    REQUIRE(retry);
    REQUIRE(retry.prepared_update->commands().size() == 1U);
    REQUIRE(controller.commit_prepared_update(
        std::move(*retry.prepared_update)));
    CHECK(controller.pending_one_shots() == 0U);
    CHECK(controller.player_state().source_command_sequence() == 1U);
}

} // namespace
