#include "local_movement_test_fixture.hpp"

#include <hlclient/client/client_world_state.hpp>
#include <hlclient/gameplay_camera/first_person_camera.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <hlclient/local_player/local_player_movement_controller.hpp>
#include <hlclient/world_preview/world_preview_scene_source.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

namespace {

namespace fixture = hlclient::tests::local_movement;
namespace camera = hlclient::gameplay_camera;
namespace gameplay = hlclient::gameplay_input;
namespace input = hlclient::input;
namespace local_player = hlclient::local_player;
namespace movement = hlclient::movement;

using Catch::Approx;

class IntentStream final {
public:
    IntentStream()
    {
        auto built = gameplay::GameplayInputBindings::project_default_v1();
        REQUIRE(built);
        bindings_.emplace(std::move(*built.bindings));
    }

    [[nodiscard]] gameplay::GameplayInputIntent next(
        const std::initializer_list<input::InputEvent> events = {},
        const double duration_seconds = 0.01)
    {
        tracker_.begin_frame();
        for (const auto& event : events) {
            tracker_.apply_event(event);
        }
        const auto snapshot = tracker_.publish_snapshot();
        tracker_.end_frame();
        auto built = gameplay::GameplayInputIntentBuilder{}.build(
            snapshot,
            *bindings_,
            gameplay::MouseLookConfig{},
            duration_seconds);
        REQUIRE(built);
        return std::move(*built.intent);
    }

private:
    input::InputStateTracker tracker_;
    std::optional<gameplay::GameplayInputBindings> bindings_;
};

[[nodiscard]] local_player::LocalPlayerMovementController make_controller(
    movement::LocalPlayerMovementState state = fixture::make_state())
{
    return local_player::LocalPlayerMovementController{
        std::move(state), fixture::make_environment()};
}

void check_camera_anchored_to_player(
    const local_player::LocalPlayerMovementController& controller)
{
    const auto& state = controller.player_state();
    const auto& position = controller.camera().position();
    CHECK(position.x == Approx(state.origin().x + state.view_offset().x));
    CHECK(position.y == Approx(state.origin().y + state.view_offset().y));
    CHECK(position.z == Approx(state.origin().z + state.view_offset().z));
    CHECK(controller.camera().mode() == camera::GameplayCameraMode::player_walk);
}

TEST_CASE("Player-walk camera follows standing and duck movement anchors",
    "[local-player][player-walk][camera][duck]")
{
    auto controller = make_controller();
    REQUIRE(controller.valid_configuration());
    check_camera_anchored_to_player(controller);
    CHECK(controller.player_state().view_offset().z ==
        Approx(hlclient::goldsrc::movement::kValveStandingViewOffsetZ));
    CHECK(controller.camera().position().z == Approx(64.0F));

    fixture::DeterministicLocalMovementCollision collision;
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch scratch;
    IntentStream input_stream;
    auto pressed = input_stream.next({
        input::InputEvent::focus_gained(),
        input::InputEvent::capture_acquired(),
        input::InputEvent::key_pressed(input::PhysicalKey::left_control),
    });
    const auto initialized = controller.update(0, pressed, collision, scratch);
    REQUIRE(initialized);
    CHECK(initialized.generated_command_count == 0U);

    auto held = input_stream.next();
    const auto ducked =
        controller.update(10'000'000, held, collision, scratch);
    REQUIRE(ducked);
    CHECK(controller.player_state().hull() ==
        movement::PlayerMovementHull::ducked);
    CHECK(controller.player_state().view_offset().z ==
        Approx(hlclient::goldsrc::movement::kValveDuckViewOffsetZ));
    CHECK(controller.player_state().origin().z == Approx(18.0F));
    check_camera_anchored_to_player(controller);
    CHECK(controller.camera().position().z == Approx(30.0F));

    auto released = input_stream.next({
        input::InputEvent::key_released(input::PhysicalKey::left_control),
    });
    const auto standing =
        controller.update(20'000'000, released, collision, scratch);
    REQUIRE(standing);
    CHECK(controller.player_state().hull() ==
        movement::PlayerMovementHull::standing);
    CHECK(controller.player_state().view_offset().z ==
        Approx(hlclient::goldsrc::movement::kValveStandingViewOffsetZ));
    CHECK(controller.player_state().origin().z == Approx(36.0F));
    check_camera_anchored_to_player(controller);
    CHECK(controller.camera().position().z == Approx(64.0F));
}

TEST_CASE("Player-walk look revisions change once and remain stable at rest",
    "[local-player][player-walk][camera][look][revision]")
{
    auto controller = make_controller();
    REQUIRE(controller.valid_configuration());
    const auto initial_revision = controller.camera().revision();

    fixture::DeterministicLocalMovementCollision collision;
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch scratch;
    IntentStream input_stream;
    auto look = input_stream.next({
        input::InputEvent::focus_gained(),
        input::InputEvent::capture_acquired(),
        input::InputEvent::mouse_motion(10, -5),
    });
    const auto changed = controller.update(0, look, collision, scratch);
    REQUIRE(changed);
    CHECK(changed.generated_command_count == 0U);
    CHECK(changed.camera_revision_changed);
    CHECK_FALSE(changed.player_state_changed);
    CHECK(controller.camera().revision() == initial_revision + 1U);
    CHECK(controller.camera().yaw_degrees() == Approx(-1.0));
    CHECK(controller.camera().pitch_degrees() == Approx(0.5));
    check_camera_anchored_to_player(controller);

    const auto changed_revision = controller.camera().revision();
    auto idle = input_stream.next();
    const auto unchanged =
        controller.update(5'000'000, idle, collision, scratch);
    REQUIRE(unchanged);
    CHECK(unchanged.generated_command_count == 0U);
    CHECK_FALSE(unchanged.camera_revision_changed);
    CHECK(controller.camera().revision() == changed_revision);
    CHECK(controller.camera().yaw_degrees() == Approx(-1.0));
    CHECK(controller.camera().pitch_degrees() == Approx(0.5));
}

struct ChunkingSummary {
    std::uint64_t signature{0U};
    std::uint32_t command_sequence{0U};
    hlclient::assets::AssetVector3 origin{};
    hlclient::assets::AssetVector3 camera_position{};
};

template<class RenderTimes>
[[nodiscard]] ChunkingSummary run_render_schedule(
    const RenderTimes& render_times)
{
    auto controller = make_controller();
    REQUIRE(controller.valid_configuration());
    fixture::DeterministicLocalMovementCollision collision;
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch scratch;
    IntentStream input_stream;
    bool first = true;
    for (const auto render_time : render_times) {
        auto intent = first
            ? input_stream.next({
                  input::InputEvent::focus_gained(),
                  input::InputEvent::capture_acquired(),
                  input::InputEvent::key_pressed(input::PhysicalKey::w),
              })
            : input_stream.next();
        first = false;
        const auto updated =
            controller.update(render_time, intent, collision, scratch);
        REQUIRE(updated);
    }
    return {
        movement::local_player_movement_state_signature(
            controller.player_state()),
        controller.player_state().source_command_sequence(),
        controller.player_state().origin(),
        controller.camera().position(),
    };
}

[[nodiscard]] std::vector<std::int64_t> even_render_schedule(
    const std::size_t frames_per_second)
{
    std::vector<std::int64_t> times;
    times.reserve(frames_per_second + 1U);
    for (std::size_t frame = 0U; frame <= frames_per_second; ++frame) {
        times.push_back(static_cast<std::int64_t>(
            (1'000'000'000ULL * frame) / frames_per_second));
    }
    return times;
}

TEST_CASE("Fixed movement cadence is independent of render update chunking",
    "[local-player][player-walk][fixed-step][render-independent]")
{
    constexpr std::array<std::int64_t, 9U> dense_times{
        0,
        2'000'000,
        5'000'000,
        10'000'000,
        11'000'000,
        20'000'000,
        25'000'000,
        30'000'000,
        40'000'000,
    };
    constexpr std::array<std::int64_t, 3U> sparse_times{
        0,
        25'000'000,
        40'000'000,
    };
    const auto dense = run_render_schedule(dense_times);
    const auto sparse = run_render_schedule(sparse_times);
    CHECK(dense.command_sequence == 4U);
    CHECK(sparse.command_sequence == dense.command_sequence);
    CHECK(sparse.signature == dense.signature);
    CHECK(sparse.origin.x == Approx(dense.origin.x));
    CHECK(sparse.origin.y == Approx(dense.origin.y));
    CHECK(sparse.origin.z == Approx(dense.origin.z));
    CHECK(sparse.camera_position.x == Approx(dense.camera_position.x));
    CHECK(sparse.camera_position.y == Approx(dense.camera_position.y));
    CHECK(sparse.camera_position.z == Approx(dense.camera_position.z));
}

TEST_CASE("Fixed movement cadence matches 30 60 144 and irregular FPS",
    "[local-player][player-walk][fixed-step][render-fps]")
{
    const auto at_30 = run_render_schedule(even_render_schedule(30U));
    const auto at_60 = run_render_schedule(even_render_schedule(60U));
    const auto at_144 = run_render_schedule(even_render_schedule(144U));
    const std::vector<std::int64_t> irregular_times{
        0,
        7'000'000,
        31'000'000,
        42'000'000,
        78'000'000,
        111'000'000,
        159'000'000,
        207'000'000,
        250'000'000,
        301'000'000,
        333'000'000,
        389'000'000,
        444'000'000,
        501'000'000,
        555'000'000,
        612'000'000,
        666'000'000,
        720'000'000,
        777'000'000,
        834'000'000,
        889'000'000,
        945'000'000,
        1'000'000'000,
    };
    const auto irregular = run_render_schedule(irregular_times);

    REQUIRE(at_30.command_sequence == 100U);
    CHECK(at_60.command_sequence == at_30.command_sequence);
    CHECK(at_144.command_sequence == at_30.command_sequence);
    CHECK(irregular.command_sequence == at_30.command_sequence);
    CHECK(at_60.signature == at_30.signature);
    CHECK(at_144.signature == at_30.signature);
    CHECK(irregular.signature == at_30.signature);
    CHECK(at_60.origin.x == Approx(at_30.origin.x));
    CHECK(at_144.origin.x == Approx(at_30.origin.x));
    CHECK(irregular.origin.x == Approx(at_30.origin.x));
    CHECK(at_60.camera_position.x == Approx(at_30.camera_position.x));
    CHECK(at_144.camera_position.x == Approx(at_30.camera_position.x));
    CHECK(irregular.camera_position.x == Approx(at_30.camera_position.x));
}

TEST_CASE("Player controller retains one jump edge until a fixed command",
    "[local-player][player-walk][jump][one-shot]")
{
    auto controller = make_controller();
    fixture::DeterministicLocalMovementCollision collision;
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch scratch;
    IntentStream input_stream;
    auto pressed = input_stream.next({
        input::InputEvent::focus_gained(),
        input::InputEvent::capture_acquired(),
        input::InputEvent::key_pressed(input::PhysicalKey::space),
    });
    const auto initialized = controller.update(0, pressed, collision, scratch);
    REQUIRE(initialized);
    CHECK(controller.pending_one_shots() ==
        gameplay::gameplay_button_mask(gameplay::GameplayButton::jump));

    std::uint64_t jump_count = 0U;
    for (std::int64_t tick = 1; tick <= 150; ++tick) {
        auto held = input_stream.next();
        const auto updated = controller.update(
            tick * 10'000'000, held, collision, scratch);
        REQUIRE(updated);
        jump_count += updated.statistics.jump_count;
    }
    CHECK(jump_count == 1U);
    CHECK(controller.pending_one_shots() == 0U);
    CHECK(controller.player_state().ground_state().grounded());

    auto released = input_stream.next({
        input::InputEvent::key_released(input::PhysicalKey::space),
    });
    const auto release_update = controller.update(
        1'510'000'000, released, collision, scratch);
    REQUIRE(release_update);
    CHECK(release_update.statistics.jump_count == 0U);

    auto pressed_again = input_stream.next({
        input::InputEvent::key_pressed(input::PhysicalKey::space),
    });
    const auto second_jump = controller.update(
        1'520'000'000, pressed_again, collision, scratch);
    REQUIRE(second_jump);
    CHECK(second_jump.statistics.jump_count == 1U);
    CHECK_FALSE(controller.player_state().ground_state().grounded());
}

TEST_CASE("Focus loss clears pending edges and produces neutral movement",
    "[local-player][player-walk][focus-loss][input]")
{
    SECTION("a pending jump is discarded before the first command")
    {
        auto controller = make_controller();
        fixture::DeterministicLocalMovementCollision collision;
        hlclient::goldsrc::movement::GoldSrcLocalMovementScratch scratch;
        IntentStream input_stream;
        auto pressed = input_stream.next({
            input::InputEvent::focus_gained(),
            input::InputEvent::capture_acquired(),
            input::InputEvent::key_pressed(input::PhysicalKey::space),
        });
        REQUIRE(controller.update(0, pressed, collision, scratch));
        REQUIRE(controller.pending_one_shots() != 0U);

        auto lost = input_stream.next({input::InputEvent::focus_lost()});
        CHECK_FALSE(lost.focused());
        CHECK(lost.forward_axis() == 0.0F);
        REQUIRE(controller.update(5'000'000, lost, collision, scratch));
        CHECK(controller.pending_one_shots() == 0U);

        auto unfocused = input_stream.next();
        const auto first_command = controller.update(
            10'000'000, unfocused, collision, scratch);
        REQUIRE(first_command);
        CHECK(first_command.statistics.jump_count == 0U);
        CHECK(controller.player_state().ground_state().grounded());
    }

    SECTION("held movement decelerates after focus loss")
    {
        auto controller = make_controller();
        fixture::DeterministicLocalMovementCollision collision;
        hlclient::goldsrc::movement::GoldSrcLocalMovementScratch scratch;
        IntentStream input_stream;
        auto forward = input_stream.next({
            input::InputEvent::focus_gained(),
            input::InputEvent::capture_acquired(),
            input::InputEvent::key_pressed(input::PhysicalKey::w),
        });
        REQUIRE(controller.update(0, forward, collision, scratch));
        auto held = input_stream.next();
        REQUIRE(controller.update(10'000'000, held, collision, scratch));
        const auto speed_before_loss = controller.player_state().velocity().x;
        REQUIRE(speed_before_loss > 0.0F);

        auto lost = input_stream.next({input::InputEvent::focus_lost()});
        CHECK_FALSE(lost.focused());
        CHECK(lost.forward_axis() == 0.0F);
        REQUIRE(controller.update(20'000'000, lost, collision, scratch));
        CHECK(controller.player_state().velocity().x < speed_before_loss);

        for (std::int64_t tick = 3; tick <= 40; ++tick) {
            auto neutral = input_stream.next();
            REQUIRE(controller.update(
                tick * 10'000'000, neutral, collision, scratch));
        }
        CHECK(controller.player_state().velocity().x ==
            Approx(0.0F).margin(1.0e-6));
        CHECK(controller.player_state().velocity().y ==
            Approx(0.0F).margin(1.0e-6));
    }
}

TEST_CASE("Controller failure preserves scheduler state and pending one-shots",
    "[local-player][player-walk][transactional][failure]")
{
    auto controller = make_controller();
    fixture::DeterministicLocalMovementCollision collision;
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch scratch;
    IntentStream input_stream;
    auto pressed = input_stream.next({
        input::InputEvent::focus_gained(),
        input::InputEvent::capture_acquired(),
        input::InputEvent::key_pressed(input::PhysicalKey::space),
    });
    REQUIRE(controller.update(0, pressed, collision, scratch));
    const auto initial_signature =
        movement::local_player_movement_state_signature(
            controller.player_state());

    collision.fail_traces();
    auto held = input_stream.next();
    const auto failed = controller.update(
        10'000'000, held, collision, scratch);
    REQUIRE_FALSE(failed);
    REQUIRE(failed.error);
    CHECK(failed.error->code == local_player::
        LocalPlayerMovementControllerErrorCode::movement_simulation_failed);
    CHECK(movement::local_player_movement_state_signature(
              controller.player_state()) == initial_signature);
    CHECK(controller.player_state().source_command_sequence() == 0U);
    CHECK(controller.pending_one_shots() ==
        gameplay::gameplay_button_mask(gameplay::GameplayButton::jump));

    collision.fail_traces(false);
    const auto retried = controller.update(
        10'000'000, held, collision, scratch);
    REQUIRE(retried);
    CHECK(retried.generated_command_count == 1U);
    CHECK(retried.statistics.jump_count == 1U);
    CHECK(controller.player_state().source_command_sequence() == 1U);
    CHECK(controller.pending_one_shots() == 0U);
}

TEST_CASE("Player-walk camera metadata does not mutate scene resources",
    "[local-player][player-walk][client-world][resource-revision]")
{
    hlclient::client::ClientWorldState world;
    const auto world_revision = world.world_revision();
    const auto scene_revision = world.scene_revision();
    const auto visibility_revision = world.visibility_revision();
    const hlclient::client::RenderCameraState player_camera;
    const hlclient::client::InteractiveCameraMetadata metadata{
        1U,
        1U,
        hlclient::client::InteractiveCameraMode::player_walk,
        std::nullopt,
        hlclient::client::ControlledEntityCameraStatus::not_applicable,
    };
    REQUIRE(world.publish_interactive_camera(player_camera, metadata));
    CHECK(world.world_revision() == world_revision);
    CHECK(world.scene_revision() == scene_revision);
    CHECK(world.visibility_revision() == visibility_revision);
    CHECK(world.input_revision() == 1U);
    CHECK(world.camera_revision() == 1U);
    REQUIRE(world.interactive_camera_metadata());
    CHECK(world.interactive_camera_metadata()->mode ==
        hlclient::client::InteractiveCameraMode::player_walk);
}

TEST_CASE("Player-walk is additive to the historical free-fly modes",
    "[local-player][player-walk][free-fly][regression]")
{
    CHECK(camera::GameplayCameraMode::player_walk !=
        camera::GameplayCameraMode::free_flight);
    CHECK(camera::to_string(camera::GameplayCameraMode::player_walk) ==
        "player_walk");
    CHECK(camera::to_string(camera::GameplayCameraMode::free_flight) ==
        "free_flight");
    CHECK(hlclient::world_preview::WorldPreviewCameraMode::player_walk !=
        hlclient::world_preview::WorldPreviewCameraMode::free_flight);
    CHECK(hlclient::client::InteractiveCameraMode::player_walk !=
        hlclient::client::InteractiveCameraMode::free_flight_world);
}

} // namespace
