#include "local_movement_test_fixture.hpp"

#include <hlclient/gameplay_camera/render_camera_adapter.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <hlclient/local_player/player_walk_failure_latch.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>
#include <hlclient/renderer/render_scene.hpp>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace fixture = hlclient::tests::local_movement;
namespace gameplay = hlclient::gameplay_input;
namespace input = hlclient::input;
namespace local_player = hlclient::local_player;

[[nodiscard]] gameplay::GameplayInputIntent jump_intent()
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    tracker.apply_event(input::InputEvent::key_pressed(
        input::PhysicalKey::space));
    const auto snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    auto bindings = gameplay::GameplayInputBindings::project_default_v1();
    REQUIRE(bindings);
    auto intent = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, *bindings.bindings, gameplay::MouseLookConfig{}, 0.01);
    REQUIRE(intent);
    return std::move(*intent.intent);
}

[[nodiscard]] gameplay::GameplayInputIntent neutral_intent()
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    const auto snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    auto bindings = gameplay::GameplayInputBindings::project_default_v1();
    REQUIRE(bindings);
    auto intent = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, *bindings.bindings, gameplay::MouseLookConfig{}, 0.01);
    REQUIRE(intent);
    return std::move(*intent.intent);
}

[[nodiscard]] gameplay::GameplayInputIntent forward_intent()
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::w));
    const auto snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    auto bindings = gameplay::GameplayInputBindings::project_default_v1();
    REQUIRE(bindings);
    auto intent = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, *bindings.bindings, gameplay::MouseLookConfig{}, 0.01);
    REQUIRE(intent);
    return std::move(*intent.intent);
}

TEST_CASE("Player-walk failure latch is one-way and retains bounded categories",
    "[local-player][player-walk][failure-latch]")
{
    local_player::LocalPlayerMovementControllerUpdateResult failed;
    failed.error = local_player::LocalPlayerMovementControllerError{
        local_player::LocalPlayerMovementControllerErrorCode::
            movement_simulation_failed,
        hlclient::goldsrc::GoldSrcUserCmdSchedulerError{
            hlclient::goldsrc::GoldSrcUserCmdSchedulerErrorCode::
                allocation_failed,
            "test"},
        std::nullopt,
        hlclient::goldsrc::movement::LocalMovementSimulationError{
            hlclient::goldsrc::movement::LocalMovementSimulationErrorCode::
                collision_query_failed,
            hlclient::goldsrc::movement::LocalMovementCollisionError{
                hlclient::goldsrc::movement::
                    LocalMovementCollisionErrorCode::world_query_failed,
                std::nullopt,
                std::nullopt},
            "test"},
        std::nullopt,
        "test"};

    local_player::PlayerWalkFailureLatch latch;
    REQUIRE(latch.simulation_enabled());
    hlclient::goldsrc::movement::PlayerWallContactDiagnosticFrame diagnostic;
    diagnostic.slide_bump_ordinal = 3U;
    diagnostic.clip_plane_count = 2U;
    diagnostic.distinct_plane_count = 2U;
    diagnostic.fraction_class = hlclient::goldsrc::movement::
        PlayerMovementTraceFractionClass::near_zero;
    diagnostic.result = hlclient::goldsrc::movement::
        PlayerMovementDiagnosticResult::stable_stop;
    diagnostic.start_solid = false;
    diagnostic.all_solid = false;
    const local_player::PlayerWalkFailureContext context{
        17U, 23U, 29U, 31U, 37U, true, diagnostic};
    const auto first = latch.latch(failed, context);
    CHECK(first.newly_latched);
    CHECK(first.clear_input_requested);
    CHECK(first.release_mouse_capture_requested);
    CHECK(first.keep_rendering);
    CHECK_FALSE(latch.simulation_enabled());
    REQUIRE(latch.failure_latched());
    REQUIRE(latch.summary());
    CHECK(latch.summary()->context.frame_ordinal == 17U);
    CHECK(latch.summary()->scheduler_error == hlclient::goldsrc::
        GoldSrcUserCmdSchedulerErrorCode::allocation_failed);
    CHECK(latch.summary()->movement_error == hlclient::goldsrc::movement::
        LocalMovementSimulationErrorCode::collision_query_failed);
    CHECK(latch.summary()->collision_error == hlclient::goldsrc::movement::
        LocalMovementCollisionErrorCode::world_query_failed);
    REQUIRE(latch.summary()->context.movement_diagnostic);
    CHECK(latch.summary()->context.movement_diagnostic->slide_bump_ordinal ==
        3U);
    CHECK(latch.summary()->context.movement_diagnostic->clip_plane_count ==
        2U);
    CHECK(latch.summary()->context.movement_diagnostic->fraction_class ==
        hlclient::goldsrc::movement::PlayerMovementTraceFractionClass::
            near_zero);
    CHECK(latch.summary()->context.movement_diagnostic->result ==
        hlclient::goldsrc::movement::PlayerMovementDiagnosticResult::
            stable_stop);

    const auto repeated = latch.latch(failed, {});
    CHECK_FALSE(repeated.newly_latched);
    CHECK_FALSE(repeated.clear_input_requested);
    CHECK_FALSE(repeated.release_mouse_capture_requested);
    CHECK(repeated.keep_rendering);
    CHECK(latch.summary()->context.frame_ordinal == 17U);
}

TEST_CASE("Viewer discard clears pending input without changing committed state or camera",
    "[local-player][player-walk][failure-latch][transactional]")
{
    local_player::LocalPlayerMovementController controller{
        fixture::make_state(), fixture::make_environment()};
    REQUIRE(controller.valid_configuration());
    fixture::DeterministicLocalMovementCollision collision;
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch scratch;

    const auto initial = controller.update(
        0, jump_intent(), collision, scratch);
    REQUIRE(initial);
    REQUIRE(controller.pending_one_shots() != 0U);
    const auto signature =
        hlclient::movement::local_player_movement_state_signature(
            controller.player_state());
    const auto camera_revision = controller.camera().revision();

    collision.fail_traces();
    const auto failed = controller.update(
        10'000'000, jump_intent(), collision, scratch);
    REQUIRE_FALSE(failed);
    controller.discard_pending_input();

    CHECK(controller.pending_one_shots() == 0U);
    CHECK(hlclient::movement::local_player_movement_state_signature(
              controller.player_state()) == signature);
    CHECK(controller.camera().revision() == camera_revision);
}

TEST_CASE("Viewer scheduler catches up a bounded hitch and rejects excessive lag",
    "[local-player][player-walk][viewer][scheduler][hitch]")
{
    local_player::LocalPlayerMovementControllerConfig config;
    config.scheduler.maximum_commands_per_update =
        hlclient::goldsrc::kMaximumUserCmdsPerSchedulerUpdate;
    local_player::LocalPlayerMovementController controller{
        fixture::make_state(), fixture::make_environment(), config};
    REQUIRE(controller.valid_configuration());
    fixture::DeterministicLocalMovementCollision collision;
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch scratch;
    REQUIRE(controller.update(0, neutral_intent(), collision, scratch));

    const auto caught_up = controller.update(
        500'000'000, neutral_intent(), collision, scratch);
    REQUIRE(caught_up);
    CHECK(caught_up.generated_command_count == 50U);

    const auto excessive = controller.update(
        1'150'000'000, neutral_intent(), collision, scratch);
    REQUIRE_FALSE(excessive);
    REQUIRE(excessive.error);
    CHECK(excessive.error->code == local_player::
        LocalPlayerMovementControllerErrorCode::scheduler_failed);
    REQUIRE(excessive.error->scheduler_error);
    CHECK(excessive.error->scheduler_error->code == hlclient::goldsrc::
        GoldSrcUserCmdSchedulerErrorCode::lag_limit_exceeded);

    local_player::PlayerWalkFailureLatch latch;
    const auto decision = latch.latch(excessive, {});
    CHECK(decision.newly_latched);
    REQUIRE(latch.summary());
    CHECK(latch.summary()->scheduler_error == hlclient::goldsrc::
        GoldSrcUserCmdSchedulerErrorCode::lag_limit_exceeded);
}

TEST_CASE("Controller reports exact matches from committed wall touches",
    "[local-player][player-walk][wall-contact][committed-touch]")
{
    fixture::DeterministicLocalMovementCollision collision;
    collision.add_positive_x_wall(48.0F);
    local_player::LocalPlayerMovementController probe_controller{
        fixture::make_state(), fixture::make_environment()};
    REQUIRE(probe_controller.valid_configuration());
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch probe_scratch;
    auto probe_start = probe_controller.player_state().origin();
    probe_start.z += probe_controller.environment().step_size() + 1.0F;
    auto probe_end = probe_start;
    probe_end.x += 2'048.0F;
    const auto probe = collision.trace_hull(
        probe_start,
        probe_end,
        hlclient::movement::PlayerMovementHull::standing,
        probe_scratch.collision,
        probe_controller.config().movement.collision_query);
    REQUIRE(probe);
    REQUIRE(probe.result);
    REQUIRE(probe.result->collision_plane);
    REQUIRE(probe.result->hit);
    const local_player::LocalPlayerMovementCommittedTouchFilter selected{
        *probe.result->hit, *probe.result->collision_plane};
    auto unrelated = selected;
    unrelated.plane.source_plane_index = UINT32_MAX;

    const auto run = [&](const auto& filter) {
        local_player::LocalPlayerMovementController controller{
            fixture::make_state(), fixture::make_environment()};
        hlclient::goldsrc::movement::GoldSrcLocalMovementScratch scratch;
        const auto intent = forward_intent();
        std::uint64_t matches = 0U;
        for (std::int64_t frame = 0; frame < 200; ++frame) {
            const auto update = controller.update(
                frame * 10'000'000,
                intent,
                collision,
                scratch,
                &filter);
            REQUIRE(update);
            REQUIRE(update.committed_touch_match_count <=
                UINT64_MAX - matches);
            matches += update.committed_touch_match_count;
        }
        return matches;
    };

    CHECK(run(selected) > 0U);
    CHECK(run(unrelated) == 0U);
}

TEST_CASE("Viewer-style frame loop keeps rendering after a typed movement failure",
    "[local-player][player-walk][failure-latch][frame-loop][null-renderer]")
{
    constexpr std::size_t frame_count = 8U;
    constexpr std::size_t injected_failure_frame = 2U;
    local_player::LocalPlayerMovementController controller{
        fixture::make_state(), fixture::make_environment()};
    REQUIRE(controller.valid_configuration());
    fixture::DeterministicLocalMovementCollision collision;
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch scratch;
    auto bindings_result = gameplay::GameplayInputBindings::project_default_v1();
    REQUIRE(bindings_result);
    REQUIRE(bindings_result.bindings);
    auto bindings = std::move(*bindings_result.bindings);

    std::optional<input::InputStateTracker> tracker{std::in_place};
    bool capture_active = false;
    std::size_t capture_release_count = 0U;
    std::size_t movement_update_attempt_count = 0U;
    std::size_t rendered_after_failure_count = 0U;
    std::optional<std::uint64_t> latched_state_signature;
    std::optional<std::uint64_t> latched_camera_revision;
    local_player::PlayerWalkFailureLatch latch;
    hlclient::renderer::null::NullRenderer renderer;
    renderer.initialize();

    for (std::size_t frame = 0U; frame < frame_count; ++frame) {
        if (tracker) {
            tracker->begin_frame();
            if (frame == 0U) {
                tracker->apply_event(input::InputEvent::focus_gained());
                tracker->apply_event(input::InputEvent::capture_acquired());
                tracker->apply_event(input::InputEvent::key_pressed(
                    input::PhysicalKey::w));
            }
            const auto snapshot = tracker->publish_snapshot();
            capture_active = snapshot.captured();
            tracker->end_frame();

            if (latch.simulation_enabled()) {
                auto intent = gameplay::GameplayInputIntentBuilder{}.build(
                    snapshot, bindings, gameplay::MouseLookConfig{}, 0.01);
                REQUIRE(intent);
                REQUIRE(intent.intent);
                if (frame == injected_failure_frame) {
                    collision.fail_traces();
                }
                ++movement_update_attempt_count;
                const auto update = controller.update(
                    static_cast<std::int64_t>(frame) * 10'000'000,
                    *intent.intent,
                    collision,
                    scratch);
                if (!update) {
                    const auto decision = latch.latch(
                        update,
                        local_player::PlayerWalkFailureContext{
                            frame,
                            controller.player_state().source_command_sequence(),
                            hlclient::movement::
                                local_player_movement_state_signature(
                                    controller.player_state()),
                            controller.camera().revision(),
                            0U,
                            capture_active,
                            scratch.last_diagnostic});
                    REQUIRE(decision.newly_latched);
                    REQUIRE(decision.keep_rendering);
                    REQUIRE(decision.clear_input_requested);
                    REQUIRE(decision.release_mouse_capture_requested);
                    controller.discard_pending_input();
                    tracker.reset();
                    capture_active = false;
                    ++capture_release_count;
                    latched_state_signature = hlclient::movement::
                        local_player_movement_state_signature(
                            controller.player_state());
                    latched_camera_revision = controller.camera().revision();
                }
            }
        }

        auto camera = hlclient::gameplay_camera::build_render_camera(
            controller.camera());
        REQUIRE(camera);
        REQUIRE(camera.camera);
        hlclient::renderer::RenderScene scene;
        scene.camera = *camera.camera;
        renderer.render(scene, {96, 96});
        if (latch.failure_latched() && frame > injected_failure_frame) {
            ++rendered_after_failure_count;
        }
    }

    REQUIRE(latch.failure_latched());
    REQUIRE(latch.summary());
    CHECK(latch.summary()->controller_error == local_player::
        LocalPlayerMovementControllerErrorCode::movement_simulation_failed);
    CHECK(latch.summary()->movement_error == hlclient::goldsrc::movement::
        LocalMovementSimulationErrorCode::collision_query_failed);
    CHECK_FALSE(latch.simulation_enabled());
    CHECK_FALSE(tracker.has_value());
    CHECK_FALSE(capture_active);
    CHECK(capture_release_count == 1U);
    CHECK(controller.pending_one_shots() == 0U);
    CHECK(movement_update_attempt_count == injected_failure_frame + 1U);
    CHECK(rendered_after_failure_count ==
        frame_count - injected_failure_frame - 1U);
    CHECK(renderer.statistics().rendered_frames == frame_count);
    REQUIRE(latched_state_signature);
    REQUIRE(latched_camera_revision);
    CHECK(hlclient::movement::local_player_movement_state_signature(
              controller.player_state()) == *latched_state_signature);
    CHECK(controller.camera().revision() == *latched_camera_revision);
    const int final_status = latch.failure_latched() ? 1 : 0;
    CHECK(final_status != 0);
}

} // namespace
