#include <hlclient/gameplay_camera/entity_first_person_camera.hpp>
#include <hlclient/gameplay_camera/first_person_camera.hpp>
#include <hlclient/gameplay_camera/render_camera_adapter.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/input/input_source.hpp>
#include <hlclient/input/input_state_tracker.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

namespace camera = hlclient::gameplay_camera;
namespace gameplay = hlclient::gameplay_input;
namespace input = hlclient::input;

constexpr std::size_t repeated_campaign_count = 20U;
constexpr double campaign_frame_seconds = 0.025;

[[nodiscard]] gameplay::GameplayInputBindings default_bindings()
{
    auto built = gameplay::GameplayInputBindings::project_default_v1();
    REQUIRE(built);
    REQUIRE(built.bindings);
    return std::move(*built.bindings);
}

[[nodiscard]] input::InputSnapshot consume_frame(
    input::IInputSource& source,
    input::InputStateTracker& tracker)
{
    source.begin_frame();
    tracker.begin_frame();
    auto event = input::InputEvent::focus_lost();
    while (source.poll_event(event)) {
        tracker.apply_event(event);
    }
    auto snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    source.end_frame();
    return snapshot;
}

[[nodiscard]] input::InputSnapshot active_snapshot(
    input::InputStateTracker& tracker,
    const std::int32_t mouse_x,
    const std::int32_t mouse_y,
    const bool move_forward)
{
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    if (move_forward) {
        tracker.apply_event(
            input::InputEvent::key_pressed(input::PhysicalKey::w));
    }
    tracker.apply_event(input::InputEvent::mouse_motion(mouse_x, mouse_y));
    auto snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    return snapshot;
}

[[nodiscard]] gameplay::GameplayInputIntent intent_for(
    const input::InputSnapshot& snapshot,
    const gameplay::GameplayInputBindings& bindings)
{
    auto built = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot,
        bindings,
        gameplay::MouseLookConfig{},
        campaign_frame_seconds);
    INFO((built.error ? built.error->message : std::string{}));
    REQUIRE(built);
    REQUIRE(built.intent);
    return std::move(*built.intent);
}

[[nodiscard]] camera::GameplayCameraState free_flight_camera()
{
    camera::GameplayCameraStateCreateInfo create_info;
    create_info.position = {10.0F, -20.0F, 12.0F};
    create_info.yaw_degrees = 90.0;
    create_info.pitch_degrees = -15.0;
    create_info.mode = camera::GameplayCameraMode::free_flight;
    auto created = camera::GameplayCameraState::create(create_info);
    INFO((created.error ? created.error->context : std::string_view{}));
    REQUIRE(created);
    REQUIRE(created.state);
    return std::move(*created.state);
}

[[nodiscard]] camera::GameplayCameraState entity_camera(
    const std::uint64_t frame_signature)
{
    camera::GameplayCameraStateCreateInfo create_info;
    create_info.position = {0.0F, -12.0F, 3.0F};
    create_info.yaw_degrees = 90.0;
    create_info.pitch_degrees = -10.0;
    create_info.mode = camera::GameplayCameraMode::entity_first_person;
    create_info.anchor_metadata = {
        camera::GameplayCameraAnchorStatus::attached,
        1U,
        camera::GameplayCameraSourceFrameIdentity{0xE100U, 1U, frame_signature},
        camera::GameplayCameraAnchorEvidenceProfile::
            explicit_synthetic_playback_v1,
    };
    auto created = camera::GameplayCameraState::create(create_info);
    INFO((created.error ? created.error->context : std::string_view{}));
    REQUIRE(created);
    REQUIRE(created.state);
    return std::move(*created.state);
}

[[nodiscard]] camera::EntityFirstPersonCameraAnchor entity_anchor(
    const std::uint64_t resource_revision,
    const std::uint64_t frame_signature,
    const float origin_x)
{
    camera::EntityFirstPersonCameraAnchorCreateInfo create_info;
    create_info.entity_number = 1U;
    create_info.interpolated_origin = {origin_x, -12.0F, 3.0F};
    create_info.source_frame_identity = {
        0xE100U, resource_revision, frame_signature};
    create_info.evidence_profile = camera::
        GameplayCameraAnchorEvidenceProfile::explicit_synthetic_playback_v1;
    auto created = camera::EntityFirstPersonCameraAnchor::create(create_info);
    INFO((created.error ? created.error->context : std::string_view{}));
    REQUIRE(created);
    REQUIRE(created.anchor);
    return std::move(*created.anchor);
}

TEST_CASE("Input state scripted campaigns complete deterministically 20 out of 20",
    "[input][integration][campaign][repeat-20]")
{
    std::size_t completed_campaigns = 0U;
    for (std::size_t campaign = 0U; campaign < repeated_campaign_count;
         ++campaign) {
        INFO("campaign " << campaign + 1U << " of " << repeated_campaign_count);
        input::ScriptedInputSource source{{
            {input::InputEvent::focus_gained(),
                input::InputEvent::capture_acquired(),
                input::InputEvent::key_pressed(input::PhysicalKey::w),
                input::InputEvent::mouse_button_pressed(
                    input::PhysicalMouseButton::left),
                input::InputEvent::mouse_motion(12, -7)},
            {},
            {input::InputEvent::focus_lost()},
            {input::InputEvent::key_pressed(input::PhysicalKey::a),
                input::InputEvent::focus_gained()},
        }};
        input::InputStateTracker tracker;

        const auto pressed = consume_frame(source, tracker);
        REQUIRE(pressed.sequence() == 1U);
        REQUIRE(pressed.focused());
        REQUIRE(pressed.captured());
        REQUIRE(pressed.key_held(input::PhysicalKey::w));
        REQUIRE(pressed.key_pressed(input::PhysicalKey::w));
        REQUIRE(pressed.mouse_button_held(input::PhysicalMouseButton::left));
        REQUIRE(pressed.relative_mouse_delta() ==
            input::RelativeMouseDelta{12, -7});

        const auto held = consume_frame(source, tracker);
        REQUIRE(held.sequence() == 2U);
        REQUIRE(held.key_held(input::PhysicalKey::w));
        REQUIRE_FALSE(held.key_pressed(input::PhysicalKey::w));
        REQUIRE(held.relative_mouse_delta() == input::RelativeMouseDelta{});

        const auto lost = consume_frame(source, tracker);
        REQUIRE(lost.sequence() == 3U);
        REQUIRE_FALSE(lost.focused());
        REQUIRE_FALSE(lost.captured());
        REQUIRE_FALSE(lost.key_held(input::PhysicalKey::w));
        REQUIRE(lost.key_released(input::PhysicalKey::w));
        REQUIRE(lost.mouse_button_released(
            input::PhysicalMouseButton::left));
        REQUIRE(lost.reset_reason() == input::InputResetReason::focus_lost);

        const auto regained = consume_frame(source, tracker);
        REQUIRE(regained.sequence() == 4U);
        REQUIRE(regained.focused());
        REQUIRE_FALSE(regained.captured());
        REQUIRE_FALSE(regained.key_held(input::PhysicalKey::a));
        REQUIRE(regained.statistics().ignored_unfocused_event_count == 1U);
        REQUIRE(source.exhausted());
        ++completed_campaigns;
    }
    CHECK(completed_campaigns == repeated_campaign_count);
}

TEST_CASE("Free-flight camera pipeline completes deterministically 20 out of 20",
    "[input][gameplay-input][gameplay-camera][integration][campaign][repeat-20]")
{
    const auto bindings = default_bindings();
    const auto config = camera::FirstPersonCameraConfig::project_default_v1();
    std::size_t completed_campaigns = 0U;

    for (std::size_t campaign = 0U; campaign < repeated_campaign_count;
         ++campaign) {
        INFO("campaign " << campaign + 1U << " of " << repeated_campaign_count);
        input::InputStateTracker tracker;
        const auto snapshot = active_snapshot(tracker, 25, -10, true);
        const auto intent = intent_for(snapshot, bindings);
        REQUIRE(intent.input_sequence() == 1U);
        REQUIRE(intent.forward_axis() == 1.0F);
        REQUIRE(intent.look_delta_yaw_degrees() == Catch::Approx(-2.5));
        REQUIRE(intent.look_delta_pitch_degrees() == Catch::Approx(1.0));

        const auto initial = free_flight_camera();
        const auto updated = camera::LocalFreeFlightCameraController{}.update(
            initial, intent, campaign_frame_seconds, config);
        INFO((updated.error ? updated.error->context : std::string_view{}));
        REQUIRE(updated);
        REQUIRE(updated.camera);
        REQUIRE(updated.status == camera::GameplayCameraUpdateStatus::updated);
        REQUIRE(updated.revision_changed);
        REQUIRE(updated.camera->revision() == 2U);
        REQUIRE(updated.camera->yaw_degrees() == Catch::Approx(87.5));
        REQUIRE(updated.camera->pitch_degrees() == Catch::Approx(-14.0));
        REQUIRE(updated.movement_distance == Catch::Approx(8.0));
        REQUIRE(updated.camera->position().x ==
            Catch::Approx(10.348955F).margin(1.0e-5F));
        REQUIRE(updated.camera->position().y ==
            Catch::Approx(-12.007615F).margin(1.0e-5F));
        REQUIRE(updated.camera->position().z == 12.0F);

        const auto render_camera = camera::build_render_camera(*updated.camera);
        REQUIRE(render_camera);
        REQUIRE(render_camera.camera);
        REQUIRE(render_camera.camera->target.x !=
                render_camera.camera->position.x);

        tracker.begin_frame();
        tracker.apply_event(input::InputEvent::focus_lost());
        const auto stopped_snapshot = tracker.publish_snapshot();
        tracker.end_frame();
        REQUIRE(stopped_snapshot.sequence() == 2U);
        REQUIRE_FALSE(stopped_snapshot.focused());
        REQUIRE_FALSE(stopped_snapshot.captured());
        REQUIRE_FALSE(stopped_snapshot.key_held(input::PhysicalKey::w));
        REQUIRE(stopped_snapshot.key_released(input::PhysicalKey::w));
        const auto stopped_intent = intent_for(stopped_snapshot, bindings);
        REQUIRE(stopped_intent.input_sequence() == 2U);
        REQUIRE(stopped_intent.forward_axis() == 0.0F);
        const auto stopped = camera::LocalFreeFlightCameraController{}.update(
            *updated.camera, stopped_intent, campaign_frame_seconds, config);
        REQUIRE(stopped);
        REQUIRE(stopped.camera);
        REQUIRE_FALSE(stopped.revision_changed);
        REQUIRE(stopped.camera->revision() == updated.camera->revision());
        REQUIRE(stopped.camera->position().x == updated.camera->position().x);
        REQUIRE(stopped.camera->position().y == updated.camera->position().y);
        REQUIRE(stopped.camera->position().z == updated.camera->position().z);
        ++completed_campaigns;
    }
    CHECK(completed_campaigns == repeated_campaign_count);
}

TEST_CASE("Entity first-person camera pipeline completes deterministically 20 out of 20",
    "[input][gameplay-input][gameplay-camera][entity-first-person][integration][campaign][repeat-20]")
{
    const auto bindings = default_bindings();
    const auto config = camera::FirstPersonCameraConfig::project_default_v1();
    std::size_t completed_campaigns = 0U;

    for (std::size_t campaign = 0U; campaign < repeated_campaign_count;
         ++campaign) {
        INFO("campaign " << campaign + 1U << " of " << repeated_campaign_count);
        input::InputStateTracker tracker;
        const auto snapshot = active_snapshot(tracker, -30, 20, true);
        const auto intent = intent_for(snapshot, bindings);
        REQUIRE(intent.forward_axis() == 1.0F);
        REQUIRE(intent.look_delta_yaw_degrees() == Catch::Approx(3.0));
        REQUIRE(intent.look_delta_pitch_degrees() == Catch::Approx(-2.0));

        const auto initial = entity_camera(100U);
        const auto anchor = entity_anchor(2U, 200U, 4.0F);
        const auto updated =
            camera::EntityFirstPersonCameraController{}.update(
                initial,
                intent,
                std::optional<camera::EntityFirstPersonCameraAnchor>{anchor},
                config);
        INFO((updated.error ? updated.error->context : std::string_view{}));
        REQUIRE(updated);
        REQUIRE(updated.camera);
        REQUIRE(updated.status == camera::GameplayCameraUpdateStatus::updated);
        REQUIRE(updated.revision_changed);
        REQUIRE(updated.camera->revision() == 2U);
        REQUIRE(updated.camera->position().x == 4.0F);
        REQUIRE(updated.camera->position().y == -12.0F);
        REQUIRE(updated.camera->position().z == 3.0F);
        REQUIRE(updated.camera->yaw_degrees() == Catch::Approx(93.0));
        REQUIRE(updated.camera->pitch_degrees() == Catch::Approx(-12.0));
        REQUIRE(updated.movement_distance == Catch::Approx(4.0));
        REQUIRE(updated.camera->anchor_metadata().entity_number == 1U);
        REQUIRE(updated.camera->anchor_metadata().source_frame_identity ==
            camera::GameplayCameraSourceFrameIdentity{0xE100U, 2U, 200U});

        const auto render_camera = camera::build_render_camera(*updated.camera);
        REQUIRE(render_camera);
        REQUIRE(render_camera.camera);

        tracker.begin_frame();
        tracker.apply_event(input::InputEvent::focus_lost());
        const auto stopped_snapshot = tracker.publish_snapshot();
        tracker.end_frame();
        REQUIRE(stopped_snapshot.sequence() == 2U);
        REQUIRE_FALSE(stopped_snapshot.focused());
        REQUIRE_FALSE(stopped_snapshot.captured());
        REQUIRE_FALSE(stopped_snapshot.key_held(input::PhysicalKey::w));
        const auto stopped_intent = intent_for(stopped_snapshot, bindings);
        REQUIRE(stopped_intent.input_sequence() == 2U);
        REQUIRE(stopped_intent.forward_axis() == 0.0F);
        const auto stopped =
            camera::EntityFirstPersonCameraController{}.update(
                *updated.camera,
                stopped_intent,
                std::optional<camera::EntityFirstPersonCameraAnchor>{anchor},
                config);
        REQUIRE(stopped);
        REQUIRE(stopped.camera);
        REQUIRE_FALSE(stopped.revision_changed);
        REQUIRE(stopped.camera->revision() == updated.camera->revision());
        REQUIRE(stopped.camera->position().x == updated.camera->position().x);
        REQUIRE(stopped.camera->position().y == updated.camera->position().y);
        REQUIRE(stopped.camera->position().z == updated.camera->position().z);
        ++completed_campaigns;
    }
    CHECK(completed_campaigns == repeated_campaign_count);
}

} // namespace
