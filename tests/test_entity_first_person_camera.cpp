#include <hlclient/gameplay_camera/entity_first_person_camera.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/input/input_state_tracker.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <initializer_list>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace {

namespace camera = hlclient::gameplay_camera;
namespace gameplay_input = hlclient::gameplay_input;
namespace input = hlclient::input;

[[nodiscard]] gameplay_input::GameplayInputIntent intent_from_events(
    const std::initializer_list<input::InputEvent> events)
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    for (const auto& event : events) {
        tracker.apply_event(event);
    }
    const auto snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    auto bindings = gameplay_input::GameplayInputBindings::project_default_v1();
    REQUIRE(bindings);
    const auto built = gameplay_input::GameplayInputIntentBuilder{}.build(
        snapshot,
        *bindings.bindings,
        gameplay_input::MouseLookConfig{},
        0.05);
    REQUIRE(built);
    return std::move(*built.intent);
}

[[nodiscard]] camera::GameplayCameraState make_entity_camera(
    const hlclient::assets::AssetVector3 position = {},
    const double yaw_degrees = 0.0,
    const double pitch_degrees = 0.0,
    const std::uint64_t revision = 1U,
    const camera::GameplayCameraAnchorMetadata metadata = {})
{
    camera::GameplayCameraStateCreateInfo create_info;
    create_info.position = position;
    create_info.yaw_degrees = yaw_degrees;
    create_info.pitch_degrees = pitch_degrees;
    create_info.mode = camera::GameplayCameraMode::entity_first_person;
    create_info.anchor_metadata = metadata;
    create_info.revision = revision;
    const auto created = camera::GameplayCameraState::create(create_info);
    INFO((created.error ? created.error->context : std::string_view{}));
    REQUIRE(created);
    return std::move(*created.state);
}

[[nodiscard]] camera::EntityFirstPersonCameraAnchor make_anchor(
    const std::uint32_t entity_number,
    const hlclient::assets::AssetVector3 origin,
    const hlclient::assets::AssetVector3 eye_offset,
    const camera::GameplayCameraSourceFrameIdentity source_identity,
    const std::optional<double> initial_yaw = std::nullopt,
    const std::optional<double> initial_pitch = std::nullopt)
{
    camera::EntityFirstPersonCameraAnchorCreateInfo create_info;
    create_info.entity_number = entity_number;
    create_info.interpolated_origin = origin;
    create_info.explicit_local_eye_offset = eye_offset;
    create_info.initial_yaw_degrees = initial_yaw;
    create_info.initial_pitch_degrees = initial_pitch;
    create_info.source_frame_identity = source_identity;
    const auto created = camera::EntityFirstPersonCameraAnchor::create(create_info);
    INFO((created.error ? created.error->context : std::string_view{}));
    REQUIRE(created);
    return std::move(*created.anchor);
}

[[nodiscard]] std::optional<camera::EntityFirstPersonCameraAnchor>
as_optional(const camera::EntityFirstPersonCameraAnchor& anchor)
{
    return std::optional<camera::EntityFirstPersonCameraAnchor>{anchor};
}

TEST_CASE("Entity first-person anchor is immutable explicit synthetic metadata",
          "[gameplay-camera][entity-anchor][immutable][evidence]")
{
    static_assert(
        !std::is_copy_assignable_v<camera::EntityFirstPersonCameraAnchor>);
    static_assert(
        !std::is_move_assignable_v<camera::EntityFirstPersonCameraAnchor>);

    const auto anchor = make_anchor(7U,
        {10.0F, 20.0F, 30.0F},
        {0.0F, 0.0F, 28.0F},
        {101U, 4U, 999U},
        450.0,
        12.0);
    CHECK(anchor.entity_number() == 7U);
    CHECK(anchor.interpolated_origin().x == 10.0F);
    CHECK(anchor.interpolated_origin().y == 20.0F);
    CHECK(anchor.interpolated_origin().z == 30.0F);
    CHECK(anchor.explicit_local_eye_offset().x == 0.0F);
    CHECK(anchor.explicit_local_eye_offset().y == 0.0F);
    CHECK(anchor.explicit_local_eye_offset().z == 28.0F);
    REQUIRE(anchor.initial_yaw_degrees());
    REQUIRE(anchor.initial_pitch_degrees());
    CHECK(*anchor.initial_yaw_degrees() == 90.0);
    CHECK(*anchor.initial_pitch_degrees() == 12.0);
    CHECK(anchor.source_frame_identity() ==
        camera::GameplayCameraSourceFrameIdentity{101U, 4U, 999U});
    CHECK(anchor.evidence_profile() ==
        camera::GameplayCameraAnchorEvidenceProfile::
            explicit_synthetic_playback_v1);
    CHECK(camera::to_string(anchor.evidence_profile()) ==
        "explicit_synthetic_playback_v1");
}

TEST_CASE("Entity anchor rejects every untrusted or stock-pending input",
          "[gameplay-camera][entity-anchor][validation]")
{
    camera::EntityFirstPersonCameraAnchorCreateInfo create_info;
    create_info.entity_number = 1U;
    create_info.source_frame_identity = {1U, 1U, 1U};

    {
        auto invalid = create_info;
        invalid.entity_number = 0U;
        const auto result =
            camera::EntityFirstPersonCameraAnchor::create(invalid);
        REQUIRE_FALSE(result);
        CHECK(result.error->code == camera::
            EntityFirstPersonCameraAnchorErrorCode::invalid_entity_number);
    }
    {
        auto invalid = create_info;
        invalid.interpolated_origin.x =
            std::numeric_limits<float>::quiet_NaN();
        const auto result =
            camera::EntityFirstPersonCameraAnchor::create(invalid);
        REQUIRE_FALSE(result);
        CHECK(result.error->code == camera::
            EntityFirstPersonCameraAnchorErrorCode::non_finite_origin);
    }
    {
        auto invalid = create_info;
        invalid.explicit_local_eye_offset.z =
            std::numeric_limits<float>::infinity();
        const auto result =
            camera::EntityFirstPersonCameraAnchor::create(invalid);
        REQUIRE_FALSE(result);
        CHECK(result.error->code == camera::
            EntityFirstPersonCameraAnchorErrorCode::non_finite_eye_offset);
    }
    {
        auto invalid = create_info;
        invalid.initial_pitch_degrees = 90.0;
        const auto result =
            camera::EntityFirstPersonCameraAnchor::create(invalid);
        REQUIRE_FALSE(result);
        CHECK(result.error->code == camera::
            EntityFirstPersonCameraAnchorErrorCode::invalid_initial_angles);
    }
    {
        auto invalid = create_info;
        invalid.source_frame_identity = {};
        const auto result =
            camera::EntityFirstPersonCameraAnchor::create(invalid);
        REQUIRE_FALSE(result);
        CHECK(result.error->code == camera::
            EntityFirstPersonCameraAnchorErrorCode::
                invalid_source_frame_identity);
    }
    {
        auto invalid = create_info;
        invalid.evidence_profile = camera::
            GameplayCameraAnchorEvidenceProfile::
                stock_player_eye_height_evidence_pending;
        const auto result =
            camera::EntityFirstPersonCameraAnchor::create(invalid);
        REQUIRE_FALSE(result);
        CHECK(result.error->code == camera::
            EntityFirstPersonCameraAnchorErrorCode::
                unsupported_evidence_profile);
    }
}

TEST_CASE("Entity camera follows interpolated origin plus explicit eye offset",
          "[gameplay-camera][entity-first-person][follow]")
{
    const auto previous = make_entity_camera();
    const auto anchor = make_anchor(1U,
        {10.0F, 20.0F, 30.0F},
        {0.0F, 0.0F, 28.0F},
        {41U, 2U, 111U},
        90.0,
        10.0);
    const auto intent = intent_from_events({input::InputEvent::focus_gained()});
    const auto updated = camera::EntityFirstPersonCameraController{}.update(
        previous,
        intent,
        as_optional(anchor),
        camera::FirstPersonCameraConfig::project_default_v1());
    REQUIRE(updated);
    REQUIRE(updated.camera);
    CHECK(updated.camera->position().x == 10.0F);
    CHECK(updated.camera->position().y == 20.0F);
    CHECK(updated.camera->position().z == 58.0F);
    CHECK(updated.camera->yaw_degrees() == 90.0);
    CHECK(updated.camera->pitch_degrees() == 10.0);
    CHECK(updated.camera->revision() == 2U);
    CHECK(updated.revision_changed);
    CHECK(updated.movement_distance ==
        Catch::Approx(std::sqrt(10.0 * 10.0 + 20.0 * 20.0 + 58.0 * 58.0)));
    CHECK(updated.camera->anchor_metadata().status ==
        camera::GameplayCameraAnchorStatus::attached);
    CHECK(updated.camera->anchor_metadata().entity_number == 1U);
    CHECK(updated.camera->anchor_metadata().source_frame_identity ==
        camera::GameplayCameraSourceFrameIdentity{41U, 2U, 111U});

    // The immutable anchor remains source metadata; camera look never mutates
    // the entity origin or any entity snapshot angle.
    CHECK(anchor.interpolated_origin().x == 10.0F);
    CHECK(anchor.interpolated_origin().y == 20.0F);
    CHECK(anchor.interpolated_origin().z == 30.0F);
}

TEST_CASE("Uncaptured entity anchor angles obey the active camera pitch bounds",
    "[gameplay-camera][entity-first-person][pitch][uncaptured][limits]")
{
    const auto previous = make_entity_camera();
    const auto anchor = make_anchor(2U,
        {1.0F, 2.0F, 3.0F},
        {0.0F, 0.0F, 28.0F},
        {42U, 1U, 200U},
        450.0,
        89.5);
    camera::FirstPersonCameraConfigCreateInfo config_info;
    config_info.minimum_pitch_degrees = -45.0;
    config_info.maximum_pitch_degrees = 45.0;
    const auto config = camera::FirstPersonCameraConfig::create(config_info);
    REQUIRE(config);
    REQUIRE(config.config);
    const auto uncaptured =
        intent_from_events({input::InputEvent::focus_gained()});
    const auto updated = camera::EntityFirstPersonCameraController{}.update(
        previous, uncaptured, as_optional(anchor), *config.config);
    REQUIRE(updated);
    REQUIRE(updated.camera);
    CHECK(updated.camera->yaw_degrees() == 90.0);
    CHECK(updated.camera->pitch_degrees() == 45.0);
    CHECK_FALSE(updated.captured);
}

TEST_CASE("Entity camera preserves local look while its anchor moves",
          "[gameplay-camera][entity-first-person][interpolation][mouse-look]")
{
    const auto initial_anchor = make_anchor(3U,
        {1.0F, 2.0F, 3.0F},
        {0.0F, 0.0F, 28.0F},
        {50U, 1U, 100U},
        45.0,
        5.0);
    const auto zero_intent =
        intent_from_events({input::InputEvent::focus_gained()});
    const auto attached = camera::EntityFirstPersonCameraController{}.update(
        make_entity_camera(),
        zero_intent,
        as_optional(initial_anchor),
        camera::FirstPersonCameraConfig::project_default_v1());
    REQUIRE(attached);

    const auto moved_anchor = make_anchor(3U,
        {11.0F, 22.0F, 33.0F},
        {0.0F, 0.0F, 28.0F},
        {50U, 2U, 200U},
        -90.0,
        -50.0);
    const auto mouse_intent = intent_from_events(
        {input::InputEvent::focus_gained(),
            input::InputEvent::capture_acquired(),
            input::InputEvent::mouse_motion(100, -50)});
    const auto followed = camera::EntityFirstPersonCameraController{}.update(
        *attached.camera,
        mouse_intent,
        as_optional(moved_anchor),
        camera::FirstPersonCameraConfig::project_default_v1());
    REQUIRE(followed);
    REQUIRE(followed.camera);
    CHECK(followed.camera->position().x == 11.0F);
    CHECK(followed.camera->position().y == 22.0F);
    CHECK(followed.camera->position().z == 61.0F);
    CHECK(followed.camera->yaw_degrees() == 35.0);
    CHECK(followed.camera->pitch_degrees() == 10.0);
    CHECK(followed.camera->revision() == attached.camera->revision() + 1U);
    CHECK(followed.camera->anchor_metadata().source_frame_identity ==
        camera::GameplayCameraSourceFrameIdentity{50U, 2U, 200U});
}

TEST_CASE("Entity removal freezes the last valid camera with typed lost-anchor status",
          "[gameplay-camera][entity-first-person][anchor-missing][freeze]")
{
    const auto anchor = make_anchor(9U,
        {5.0F, 6.0F, 7.0F},
        {0.0F, 0.0F, 28.0F},
        {71U, 8U, 99U},
        15.0,
        -10.0);
    const auto intent = intent_from_events(
        {input::InputEvent::focus_gained(),
            input::InputEvent::capture_acquired()});
    const auto attached = camera::EntityFirstPersonCameraController{}.update(
        make_entity_camera(),
        intent,
        as_optional(anchor),
        camera::FirstPersonCameraConfig::project_default_v1());
    REQUIRE(attached);
    REQUIRE(attached.camera);

    const auto missing = camera::EntityFirstPersonCameraController{}.update(
        *attached.camera,
        intent,
        std::nullopt,
        camera::FirstPersonCameraConfig::project_default_v1());
    REQUIRE_FALSE(missing);
    REQUIRE(missing.camera);
    REQUIRE(missing.error);
    CHECK(missing.error->code == camera::GameplayCameraErrorCode::anchor_missing);
    CHECK(missing.status ==
        camera::GameplayCameraUpdateStatus::anchor_entity_missing);
    CHECK_FALSE(missing.revision_changed);
    CHECK(missing.camera->revision() == attached.camera->revision());
    CHECK(missing.camera->position().x == attached.camera->position().x);
    CHECK(missing.camera->position().y == attached.camera->position().y);
    CHECK(missing.camera->position().z == attached.camera->position().z);
    CHECK(missing.camera->yaw_degrees() == attached.camera->yaw_degrees());
    CHECK(missing.camera->pitch_degrees() == attached.camera->pitch_degrees());
    CHECK(missing.camera->anchor_metadata().status ==
        camera::GameplayCameraAnchorStatus::anchor_entity_missing);
    CHECK(missing.camera->anchor_metadata().entity_number == 9U);
    CHECK_FALSE(missing.camera->anchor_metadata().source_frame_identity);

    const auto still_missing =
        camera::EntityFirstPersonCameraController{}.update(
            *missing.camera,
            intent,
            std::nullopt,
            camera::FirstPersonCameraConfig::project_default_v1());
    REQUIRE_FALSE(still_missing);
    REQUIRE(still_missing.camera);
    CHECK(still_missing.camera->revision() == missing.camera->revision());
    CHECK(still_missing.camera->position().z == missing.camera->position().z);
}

TEST_CASE("Missing entity anchor rejects a camera from a different configuration",
          "[gameplay-camera][entity-first-person][anchor-missing][config]")
{
    const auto previous =
        make_entity_camera({1.0F, 2.0F, 3.0F}, 0.0, 45.0, 7U);
    camera::FirstPersonCameraConfigCreateInfo narrowed_info;
    narrowed_info.minimum_pitch_degrees = -30.0;
    narrowed_info.maximum_pitch_degrees = 30.0;
    narrowed_info.vertical_fov_radians = 0.9;
    narrowed_info.near_plane = 0.2;
    narrowed_info.far_plane = 2'048.0;
    const auto narrowed = camera::FirstPersonCameraConfig::create(narrowed_info);
    REQUIRE(narrowed);
    REQUIRE(narrowed.config);

    const auto rejected = camera::EntityFirstPersonCameraController{}.update(
        previous,
        intent_from_events({input::InputEvent::focus_gained()}),
        std::nullopt,
        *narrowed.config);
    REQUIRE_FALSE(rejected);
    CHECK_FALSE(rejected.camera);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code == camera::GameplayCameraErrorCode::invalid_input);
}

TEST_CASE("Entity camera never infers a stock eye height",
          "[gameplay-camera][entity-first-person][explicit-eye-offset]")
{
    const auto intent = intent_from_events({input::InputEvent::focus_gained()});
    const auto zero_offset = make_anchor(
        1U, {2.0F, 4.0F, 8.0F}, {}, {90U, 1U, 1U});
    const auto at_origin = camera::EntityFirstPersonCameraController{}.update(
        make_entity_camera(),
        intent,
        as_optional(zero_offset),
        camera::FirstPersonCameraConfig::project_default_v1());
    REQUIRE(at_origin);
    CHECK(at_origin.camera->position().x == 2.0F);
    CHECK(at_origin.camera->position().y == 4.0F);
    CHECK(at_origin.camera->position().z == 8.0F);

    const auto explicit_offset = make_anchor(
        1U, {2.0F, 4.0F, 8.0F}, {3.0F, -1.0F, 28.0F}, {90U, 2U, 2U});
    const auto offset = camera::EntityFirstPersonCameraController{}.update(
        make_entity_camera(),
        intent,
        as_optional(explicit_offset),
        camera::FirstPersonCameraConfig::project_default_v1());
    REQUIRE(offset);
    CHECK(offset.camera->position().x == 5.0F);
    CHECK(offset.camera->position().y == 3.0F);
    CHECK(offset.camera->position().z == 36.0F);
}

TEST_CASE("Entity camera ignores movement intent and metadata-only frames do not revise pose",
          "[gameplay-camera][entity-first-person][revision][no-player-movement]")
{
    const auto initial_anchor = make_anchor(
        4U, {10.0F, 0.0F, 0.0F}, {}, {120U, 1U, 10U});
    const auto no_input = intent_from_events({input::InputEvent::focus_gained()});
    const auto attached = camera::EntityFirstPersonCameraController{}.update(
        make_entity_camera(),
        no_input,
        as_optional(initial_anchor),
        camera::FirstPersonCameraConfig::project_default_v1());
    REQUIRE(attached);

    const auto next_frame_same_pose = make_anchor(
        4U, {10.0F, 0.0F, 0.0F}, {}, {120U, 2U, 11U});
    const auto movement_intent = intent_from_events(
        {input::InputEvent::focus_gained(),
            input::InputEvent::key_pressed(input::PhysicalKey::w),
            input::InputEvent::key_pressed(input::PhysicalKey::space),
            input::InputEvent::key_pressed(input::PhysicalKey::left_shift)});
    const auto unchanged = camera::EntityFirstPersonCameraController{}.update(
        *attached.camera,
        movement_intent,
        as_optional(next_frame_same_pose),
        camera::FirstPersonCameraConfig::project_default_v1());
    REQUIRE(unchanged);
    CHECK_FALSE(unchanged.revision_changed);
    CHECK(unchanged.camera->revision() == attached.camera->revision());
    CHECK(unchanged.camera->position().x == attached.camera->position().x);
    CHECK(unchanged.camera->position().y == attached.camera->position().y);
    CHECK(unchanged.camera->position().z == attached.camera->position().z);
    CHECK(unchanged.camera->anchor_metadata().source_frame_identity ==
        camera::GameplayCameraSourceFrameIdentity{120U, 2U, 11U});
}

TEST_CASE("A newly controlled entity may provide explicit initial angles",
          "[gameplay-camera][entity-first-person][controlled-entity]")
{
    camera::GameplayCameraAnchorMetadata old_metadata;
    old_metadata.status = camera::GameplayCameraAnchorStatus::attached;
    old_metadata.entity_number = 1U;
    old_metadata.source_frame_identity =
        camera::GameplayCameraSourceFrameIdentity{1U, 1U, 1U};
    old_metadata.evidence_profile = camera::GameplayCameraAnchorEvidenceProfile::
        explicit_synthetic_playback_v1;
    const auto previous =
        make_entity_camera({1.0F, 2.0F, 3.0F}, 25.0, 5.0, 8U, old_metadata);
    const auto new_entity = make_anchor(2U,
        {1.0F, 2.0F, 3.0F},
        {},
        {1U, 2U, 2U},
        -120.0,
        30.0);
    const auto updated = camera::EntityFirstPersonCameraController{}.update(
        previous,
        intent_from_events({input::InputEvent::focus_gained()}),
        as_optional(new_entity),
        camera::FirstPersonCameraConfig::project_default_v1());
    REQUIRE(updated);
    CHECK(updated.camera->yaw_degrees() == -120.0);
    CHECK(updated.camera->pitch_degrees() == 30.0);
    CHECK(updated.camera->revision() == 9U);
    CHECK(updated.camera->anchor_metadata().entity_number == 2U);
}

TEST_CASE("Entity camera position and revision limits fail without partial publication",
          "[gameplay-camera][entity-first-person][safety][transaction]")
{
    camera::FirstPersonCameraConfigCreateInfo config_info;
    config_info.maximum_position_magnitude = 10.0;
    config_info.maximum_camera_revisions = 1U;
    auto config_result = camera::FirstPersonCameraConfig::create(config_info);
    REQUIRE(config_result);
    const auto anchor = make_anchor(
        1U, {11.0F, 0.0F, 0.0F}, {}, {10U, 1U, 1U});
    const auto failed = camera::EntityFirstPersonCameraController{}.update(
        make_entity_camera(),
        intent_from_events({input::InputEvent::focus_gained()}),
        as_optional(anchor),
        *config_result.config);
    REQUIRE_FALSE(failed);
    CHECK_FALSE(failed.camera);
    REQUIRE(failed.error);
    CHECK(failed.error->code ==
        camera::GameplayCameraErrorCode::position_limit_exceeded);

    const auto nearby = make_anchor(
        1U, {1.0F, 0.0F, 0.0F}, {}, {10U, 1U, 2U});
    const auto revision_failed =
        camera::EntityFirstPersonCameraController{}.update(
            make_entity_camera(),
            intent_from_events({input::InputEvent::focus_gained()}),
            as_optional(nearby),
            *config_result.config);
    REQUIRE_FALSE(revision_failed);
    CHECK_FALSE(revision_failed.camera);
    REQUIRE(revision_failed.error);
    CHECK(revision_failed.error->code ==
        camera::GameplayCameraErrorCode::revision_limit_exceeded);
}

TEST_CASE("Entity first-person camera campaign is deterministic twenty times",
          "[gameplay-camera][entity-first-person][campaign][repeat]")
{
    std::optional<hlclient::assets::AssetVector3> expected_position;
    std::optional<double> expected_yaw;
    std::optional<double> expected_pitch;
    for (std::size_t run = 0U; run < 20U; ++run) {
        input::InputStateTracker tracker;
        auto bindings =
            gameplay_input::GameplayInputBindings::project_default_v1();
        REQUIRE(bindings);
        const auto build_intent = [&](const input::InputSnapshot& snapshot) {
            auto built = gameplay_input::GameplayInputIntentBuilder{}.build(
                snapshot,
                *bindings.bindings,
                gameplay_input::MouseLookConfig{},
                0.05);
            REQUIRE(built);
            return std::move(*built.intent);
        };

        tracker.begin_frame();
        tracker.apply_event(input::InputEvent::focus_gained());
        tracker.apply_event(input::InputEvent::capture_acquired());
        const auto first_snapshot = tracker.publish_snapshot();
        tracker.end_frame();
        CHECK(first_snapshot.sequence() == 1U);
        const auto first_anchor = make_anchor(5U,
            {1.0F, 2.0F, 3.0F},
            {0.0F, 0.0F, 28.0F},
            {700U, 1U, 101U},
            30.0,
            5.0);
        const auto first = camera::EntityFirstPersonCameraController{}.update(
            make_entity_camera(),
            build_intent(first_snapshot),
            as_optional(first_anchor),
            camera::FirstPersonCameraConfig::project_default_v1());
        REQUIRE(first);

        tracker.begin_frame();
        tracker.apply_event(input::InputEvent::mouse_motion(30, -20));
        const auto second_snapshot = tracker.publish_snapshot();
        tracker.end_frame();
        CHECK(second_snapshot.sequence() == 2U);
        const auto second_anchor = make_anchor(5U,
            {11.0F, 12.0F, 13.0F},
            {0.0F, 0.0F, 28.0F},
            {700U, 2U, 102U});
        const auto second = camera::EntityFirstPersonCameraController{}.update(
            *first.camera,
            build_intent(second_snapshot),
            as_optional(second_anchor),
            camera::FirstPersonCameraConfig::project_default_v1());
        REQUIRE(second);

        tracker.begin_frame();
        tracker.apply_event(input::InputEvent::focus_lost());
        const auto third_snapshot = tracker.publish_snapshot();
        tracker.end_frame();
        CHECK(third_snapshot.sequence() == 3U);
        CHECK_FALSE(third_snapshot.focused());
        CHECK_FALSE(third_snapshot.captured());
        const auto third = camera::EntityFirstPersonCameraController{}.update(
            *second.camera,
            build_intent(third_snapshot),
            std::nullopt,
            camera::FirstPersonCameraConfig::project_default_v1());
        REQUIRE_FALSE(third);
        REQUIRE(third.camera);
        REQUIRE(third.error);
        CHECK(third.error->code ==
            camera::GameplayCameraErrorCode::anchor_missing);
        CHECK_FALSE(third.revision_changed);
        CHECK(third.camera->revision() == second.camera->revision());

        if (!expected_position) {
            expected_position = third.camera->position();
            expected_yaw = third.camera->yaw_degrees();
            expected_pitch = third.camera->pitch_degrees();
        } else {
            CHECK(third.camera->position().x == expected_position->x);
            CHECK(third.camera->position().y == expected_position->y);
            CHECK(third.camera->position().z == expected_position->z);
            CHECK(third.camera->yaw_degrees() == *expected_yaw);
            CHECK(third.camera->pitch_degrees() == *expected_pitch);
        }
    }
}

} // namespace
