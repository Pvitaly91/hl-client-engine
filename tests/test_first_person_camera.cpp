#include <hlclient/gameplay_camera/first_person_camera.hpp>
#include <hlclient/gameplay_camera/render_camera_adapter.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/gameplay_input/gameplay_input_limits.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <hlclient/renderer/render_camera_math.hpp>

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
namespace renderer = hlclient::renderer;

[[nodiscard]] gameplay_input::GameplayInputIntent intent_from_events(
    const std::initializer_list<input::InputEvent> events,
    const double sample_duration_seconds = 0.05)
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
        sample_duration_seconds);
    REQUIRE(built);
    return std::move(*built.intent);
}

[[nodiscard]] camera::GameplayCameraState make_camera(
    const hlclient::assets::AssetVector3 position = {},
    const double yaw_degrees = 0.0,
    const double pitch_degrees = 0.0,
    const std::uint64_t revision = 1U,
    const camera::GameplayCameraMode mode =
        camera::GameplayCameraMode::free_flight)
{
    camera::GameplayCameraStateCreateInfo create_info;
    create_info.position = position;
    create_info.yaw_degrees = yaw_degrees;
    create_info.pitch_degrees = pitch_degrees;
    create_info.revision = revision;
    create_info.mode = mode;
    const auto created = camera::GameplayCameraState::create(create_info);
    INFO((created.error ? created.error->context : std::string_view{}));
    REQUIRE(created);
    return std::move(*created.state);
}

[[nodiscard]] camera::FirstPersonCameraConfig make_config(
    camera::FirstPersonCameraConfigCreateInfo create_info = {})
{
    auto created = camera::FirstPersonCameraConfig::create(create_info);
    INFO((created.error ? created.error->context : std::string_view{}));
    REQUIRE(created);
    return std::move(*created.config);
}

TEST_CASE("First-person camera publishes only the local Z-up compatibility profile",
          "[gameplay-camera][profile][immutable]")
{
    static_assert(!std::is_copy_assignable_v<camera::GameplayCameraState>);
    static_assert(!std::is_move_assignable_v<camera::GameplayCameraState>);
    static_assert(!std::is_copy_assignable_v<camera::FirstPersonCameraConfig>);

    const auto state = make_camera();
    CHECK(state.compatibility_profile() ==
        camera::GameplayCameraCompatibilityProfile::
            local_first_person_z_up_v1);
    CHECK(state.evidence_profile() ==
        camera::GameplayCameraEvidenceProfile::
            project_owned_local_first_person_camera_v1);
    CHECK(camera::to_string(state.compatibility_profile()) ==
        "local_first_person_z_up_v1");
    CHECK(camera::to_string(state.evidence_profile()) ==
        "project_owned_local_first_person_camera_v1");

    camera::GameplayCameraStateCreateInfo pending;
    pending.compatibility_profile = camera::GameplayCameraCompatibilityProfile::
        stock_view_angles_evidence_pending;
    pending.evidence_profile =
        camera::GameplayCameraEvidenceProfile::evidence_pending_m4_6_2;
    const auto rejected = camera::GameplayCameraState::create(pending);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
        camera::GameplayCameraErrorCode::unsupported_compatibility_profile);
}

TEST_CASE("Z-up first-person basis has exact documented cardinal directions",
          "[gameplay-camera][basis][z-up]")
{
    const auto forward_zero = camera::forward_from_yaw_pitch(0.0, 0.0);
    REQUIRE(forward_zero);
    CHECK(forward_zero->x == Catch::Approx(1.0F));
    CHECK(forward_zero->y == Catch::Approx(0.0F).margin(1.0e-6F));
    CHECK(forward_zero->z == Catch::Approx(0.0F).margin(1.0e-6F));

    const auto forward_ninety = camera::forward_from_yaw_pitch(90.0, 0.0);
    REQUIRE(forward_ninety);
    CHECK(forward_ninety->x == Catch::Approx(0.0F).margin(1.0e-6F));
    CHECK(forward_ninety->y == Catch::Approx(1.0F));
    CHECK(forward_ninety->z == Catch::Approx(0.0F).margin(1.0e-6F));

    const auto upward = camera::forward_from_yaw_pitch(0.0, 45.0);
    const auto downward = camera::forward_from_yaw_pitch(0.0, -45.0);
    REQUIRE(upward);
    REQUIRE(downward);
    CHECK(upward->z > 0.0F);
    CHECK(downward->z < 0.0F);

    const auto horizontal = camera::horizontal_forward_from_yaw(37.0);
    const auto right = camera::right_from_yaw(0.0);
    REQUIRE(horizontal);
    REQUIRE(right);
    CHECK(horizontal->z == 0.0F);
    CHECK(right->x == Catch::Approx(0.0F).margin(1.0e-6F));
    CHECK(right->y == Catch::Approx(-1.0F));
    CHECK(right->z == 0.0F);
    CHECK(camera::world_up().x == 0.0F);
    CHECK(camera::world_up().y == 0.0F);
    CHECK(camera::world_up().z == 1.0F);
}

TEST_CASE("Yaw normalization and pitch clamping cover every boundary",
          "[gameplay-camera][angles][bounds]")
{
    CHECK(*camera::normalize_yaw_degrees(0.0) == 0.0);
    CHECK(*camera::normalize_yaw_degrees(179.999) ==
        Catch::Approx(179.999));
    CHECK(*camera::normalize_yaw_degrees(180.0) == -180.0);
    CHECK(*camera::normalize_yaw_degrees(-180.0) == -180.0);
    CHECK(*camera::normalize_yaw_degrees(360.0) == 0.0);
    CHECK(*camera::normalize_yaw_degrees(-360.0) == 0.0);
    CHECK(*camera::normalize_yaw_degrees(540.0) == -180.0);
    CHECK(*camera::normalize_yaw_degrees(-540.0) == -180.0);
    CHECK_FALSE(camera::normalize_yaw_degrees(
        std::numeric_limits<double>::infinity()));
    CHECK_FALSE(camera::normalize_yaw_degrees(
        std::numeric_limits<double>::quiet_NaN()));

    CHECK(*camera::clamp_pitch_degrees(100.0, -89.0, 89.0) == 89.0);
    CHECK(*camera::clamp_pitch_degrees(-100.0, -89.0, 89.0) == -89.0);
    CHECK(*camera::clamp_pitch_degrees(12.0, -89.0, 89.0) == 12.0);
    CHECK_FALSE(camera::clamp_pitch_degrees(0.0, 10.0, -10.0));
    CHECK_FALSE(camera::clamp_pitch_degrees(
        std::numeric_limits<double>::quiet_NaN(), -89.0, 89.0));
}

TEST_CASE("Gameplay camera converts exactly to a finite renderer camera",
          "[gameplay-camera][render-camera]")
{
    const auto state = make_camera({10.0F, 20.0F, 30.0F}, 90.0, 30.0);
    const auto built = camera::build_render_camera(state);
    REQUIRE(built);
    REQUIRE(built.camera);
    CHECK(built.camera->position.x == 10.0F);
    CHECK(built.camera->position.y == 20.0F);
    CHECK(built.camera->position.z == 30.0F);
    CHECK(built.camera->target.x == Catch::Approx(10.0F).margin(1.0e-5F));
    CHECK(built.camera->target.y == Catch::Approx(20.866025F));
    CHECK(built.camera->target.z == Catch::Approx(30.5F));
    CHECK(built.camera->up.z == 1.0F);
    CHECK(renderer::is_valid(*built.camera));
    const auto matrix = renderer::camera_view_projection(
        *built.camera, renderer::RenderExtent{1'280, 720});
    REQUIRE(matrix);
    CHECK(renderer::is_finite(*matrix.matrix));
}

TEST_CASE("First-person configuration is bounded and project-owned",
          "[gameplay-camera][config][limits]")
{
    const auto defaults = camera::FirstPersonCameraConfig::project_default_v1();
    CHECK(defaults.minimum_pitch_degrees() == -89.0);
    CHECK(defaults.maximum_pitch_degrees() == 89.0);
    CHECK(defaults.base_move_speed() == 320.0);
    CHECK(defaults.speed_multiplier() == 2.0);
    CHECK(defaults.maximum_frame_duration_seconds() == 0.1);
    CHECK(defaults.maximum_position_magnitude() == 10'000'000.0);

    camera::FirstPersonCameraConfigCreateInfo invalid;
    invalid.minimum_pitch_degrees = invalid.maximum_pitch_degrees;
    CHECK_FALSE(camera::FirstPersonCameraConfig::create(invalid));
    invalid = {};
    invalid.base_move_speed = 0.0;
    CHECK_FALSE(camera::FirstPersonCameraConfig::create(invalid));
    invalid = {};
    invalid.speed_multiplier = 17.0;
    CHECK_FALSE(camera::FirstPersonCameraConfig::create(invalid));
    invalid = {};
    invalid.maximum_frame_duration_seconds = 1.01;
    CHECK_FALSE(camera::FirstPersonCameraConfig::create(invalid));
    invalid = {};
    invalid.vertical_fov_radians = 0.1;
    CHECK_FALSE(camera::FirstPersonCameraConfig::create(invalid));
    invalid = {};
    invalid.far_plane = invalid.near_plane;
    CHECK_FALSE(camera::FirstPersonCameraConfig::create(invalid));
    invalid = {};
    invalid.near_plane = std::numeric_limits<double>::denorm_min();
    CHECK_FALSE(camera::FirstPersonCameraConfig::create(invalid));
    invalid = {};
    invalid.near_plane = 4'096.0;
    invalid.far_plane = std::nextafter(4'096.0, 5'000.0);
    CHECK_FALSE(camera::FirstPersonCameraConfig::create(invalid));
    invalid = {};
    invalid.mouse_look_config.degrees_per_pixel_x =
        std::numeric_limits<double>::quiet_NaN();
    CHECK_FALSE(camera::FirstPersonCameraConfig::create(invalid));
}

TEST_CASE("First-person camera enforces the exact documented FOV bounds",
          "[gameplay-camera][config][state][limits][fov]")
{
    constexpr double minimum_fov_radians =
        gameplay_input::kGameplayInputSafetyHardLimits.
            minimum_vertical_fov_radians;
    constexpr double maximum_fov_radians =
        gameplay_input::kGameplayInputSafetyHardLimits.
            maximum_vertical_fov_radians;

    camera::FirstPersonCameraConfigCreateInfo config_info;
    config_info.vertical_fov_radians = minimum_fov_radians;
    const auto exact_minimum_config =
        camera::FirstPersonCameraConfig::create(config_info);
    REQUIRE(exact_minimum_config);
    REQUIRE(exact_minimum_config.config);
    CHECK(exact_minimum_config.config->vertical_fov_radians() ==
        minimum_fov_radians);

    config_info.vertical_fov_radians = std::nextafter(
        minimum_fov_radians, -std::numeric_limits<double>::infinity());
    CHECK_FALSE(camera::FirstPersonCameraConfig::create(config_info));

    config_info.vertical_fov_radians = maximum_fov_radians;
    const auto exact_config =
        camera::FirstPersonCameraConfig::create(config_info);
    REQUIRE(exact_config);
    REQUIRE(exact_config.config);
    CHECK(exact_config.config->vertical_fov_radians() ==
        maximum_fov_radians);

    config_info.vertical_fov_radians = std::nextafter(
        maximum_fov_radians, std::numeric_limits<double>::infinity());
    CHECK_FALSE(camera::FirstPersonCameraConfig::create(config_info));

    camera::GameplayCameraStateCreateInfo state_info;
    state_info.vertical_fov_radians = minimum_fov_radians;
    const auto exact_minimum_state =
        camera::GameplayCameraState::create(state_info);
    REQUIRE(exact_minimum_state);
    REQUIRE(exact_minimum_state.state);
    CHECK(exact_minimum_state.state->vertical_fov_radians() ==
        minimum_fov_radians);

    state_info.vertical_fov_radians = std::nextafter(
        minimum_fov_radians, -std::numeric_limits<double>::infinity());
    CHECK_FALSE(camera::GameplayCameraState::create(state_info));

    state_info.vertical_fov_radians = maximum_fov_radians;
    const auto exact_state = camera::GameplayCameraState::create(state_info);
    REQUIRE(exact_state);
    REQUIRE(exact_state.state);
    CHECK(exact_state.state->vertical_fov_radians() ==
        maximum_fov_radians);

    state_info.vertical_fov_radians = std::nextafter(
        maximum_fov_radians, std::numeric_limits<double>::infinity());
    CHECK_FALSE(camera::GameplayCameraState::create(state_info));
}

TEST_CASE("Gameplay camera rejects unknown closed-enum values",
    "[gameplay-camera][state][validation][enum]")
{
    camera::GameplayCameraStateCreateInfo create_info;
    create_info.mode = static_cast<camera::GameplayCameraMode>(0x7fff);
    auto unknown_mode = camera::GameplayCameraState::create(create_info);
    REQUIRE_FALSE(unknown_mode);
    REQUIRE(unknown_mode.error);
    CHECK(unknown_mode.error->code ==
        camera::GameplayCameraErrorCode::invalid_configuration);

    create_info = {};
    create_info.mode = camera::GameplayCameraMode::entity_first_person;
    create_info.anchor_metadata.status =
        static_cast<camera::GameplayCameraAnchorStatus>(0x7fff);
    auto unknown_anchor_status =
        camera::GameplayCameraState::create(create_info);
    REQUIRE_FALSE(unknown_anchor_status);
    REQUIRE(unknown_anchor_status.error);
    CHECK(unknown_anchor_status.error->code ==
        camera::GameplayCameraErrorCode::invalid_configuration);
}

TEST_CASE("Zero input and capture metadata alone leave camera bit-stable",
          "[gameplay-camera][free-flight][revision][zero-input]")
{
    const auto previous = make_camera({4.0F, 5.0F, 6.0F}, 12.0, -7.0, 9U);
    const auto config = camera::FirstPersonCameraConfig::project_default_v1();

    const auto zero = intent_from_events(
        {input::InputEvent::focus_gained(),
            input::InputEvent::capture_acquired()});
    const auto updated = camera::LocalFreeFlightCameraController{}.update(
        previous, zero, 0.05, config);
    REQUIRE(updated);
    REQUIRE(updated.camera);
    CHECK(updated.status == camera::GameplayCameraUpdateStatus::unchanged);
    CHECK_FALSE(updated.revision_changed);
    CHECK(updated.camera->revision() == 9U);
    CHECK(updated.camera->position().x == 4.0F);
    CHECK(updated.camera->position().y == 5.0F);
    CHECK(updated.camera->position().z == 6.0F);
    CHECK(updated.camera->yaw_degrees() == 12.0);
    CHECK(updated.camera->pitch_degrees() == -7.0);
}

TEST_CASE("Uncaptured free-flight state obeys the active pitch bounds",
          "[gameplay-camera][free-flight][pitch][uncaptured][limits]")
{
    const auto previous = make_camera({0.0F, 0.0F, 0.0F}, 450.0, 80.0, 4U);
    camera::FirstPersonCameraConfigCreateInfo config_info;
    config_info.minimum_pitch_degrees = -45.0;
    config_info.maximum_pitch_degrees = 45.0;
    const auto config = camera::FirstPersonCameraConfig::create(config_info);
    REQUIRE(config);
    REQUIRE(config.config);
    const auto uncaptured = intent_from_events(
        {input::InputEvent::focus_gained()});
    const auto updated = camera::LocalFreeFlightCameraController{}.update(
        previous, uncaptured, 0.0, *config.config);
    REQUIRE(updated);
    REQUIRE(updated.camera);
    CHECK(updated.camera->yaw_degrees() == 90.0);
    CHECK(updated.camera->pitch_degrees() == 45.0);
    CHECK(updated.camera->revision() == 5U);
    CHECK(updated.revision_changed);
}

TEST_CASE("Mouse look is displacement-based, captured, wrapped and clamped",
          "[gameplay-camera][mouse-look][timing]")
{
    const auto previous = make_camera({}, 175.0, 80.0);
    const auto config = camera::FirstPersonCameraConfig::project_default_v1();

    const auto intent_16 = intent_from_events(
        {input::InputEvent::focus_gained(),
            input::InputEvent::capture_acquired(),
            input::InputEvent::mouse_motion(200, -500)},
        0.016);
    const auto intent_33 = intent_from_events(
        {input::InputEvent::focus_gained(),
            input::InputEvent::capture_acquired(),
            input::InputEvent::mouse_motion(200, -500)},
        0.033);
    const auto intent_100 = intent_from_events(
        {input::InputEvent::focus_gained(),
            input::InputEvent::capture_acquired(),
            input::InputEvent::mouse_motion(200, -500)},
        0.1);

    const auto at_16 = camera::LocalFreeFlightCameraController{}.update(
        previous, intent_16, 0.016, config);
    const auto at_33 = camera::LocalFreeFlightCameraController{}.update(
        previous, intent_33, 0.033, config);
    const auto at_100 = camera::LocalFreeFlightCameraController{}.update(
        previous, intent_100, 0.1, config);
    REQUIRE(at_16);
    REQUIRE(at_33);
    REQUIRE(at_100);
    CHECK(at_16.camera->yaw_degrees() == 155.0);
    CHECK(at_16.camera->pitch_degrees() == 89.0);
    CHECK(at_33.camera->yaw_degrees() == at_16.camera->yaw_degrees());
    CHECK(at_33.camera->pitch_degrees() == at_16.camera->pitch_degrees());
    CHECK(at_100.camera->yaw_degrees() == at_16.camera->yaw_degrees());
    CHECK(at_100.camera->pitch_degrees() == at_16.camera->pitch_degrees());
    CHECK(at_16.camera->revision() == previous.revision() + 1U);

    const auto uncaptured = intent_from_events(
        {input::InputEvent::focus_gained(),
            input::InputEvent::mouse_motion(200, -500)});
    const auto ignored = camera::LocalFreeFlightCameraController{}.update(
        previous, uncaptured, 0.05, config);
    REQUIRE(ignored);
    CHECK(ignored.camera->yaw_degrees() == previous.yaw_degrees());
    CHECK(ignored.camera->pitch_degrees() == previous.pitch_degrees());
    CHECK_FALSE(ignored.revision_changed);
}

TEST_CASE("Free-flight movement uses horizontal basis and explicit vertical input",
          "[gameplay-camera][free-flight][movement]")
{
    camera::FirstPersonCameraConfigCreateInfo config_info;
    config_info.base_move_speed = 10.0;
    const auto config = make_config(config_info);
    const auto previous = make_camera({}, 0.0, 80.0);

    const auto forward = camera::LocalFreeFlightCameraController{}.update(
        previous,
        intent_from_events({input::InputEvent::focus_gained(),
            input::InputEvent::key_pressed(input::PhysicalKey::w)}),
        0.1,
        config);
    REQUIRE(forward);
    CHECK(forward.camera->position().x == Catch::Approx(1.0F));
    CHECK(forward.camera->position().y == Catch::Approx(0.0F).margin(1.0e-6F));
    CHECK(forward.camera->position().z == Catch::Approx(0.0F));

    const auto backward = camera::LocalFreeFlightCameraController{}.update(
        previous,
        intent_from_events({input::InputEvent::focus_gained(),
            input::InputEvent::key_pressed(input::PhysicalKey::s)}),
        0.1,
        config);
    REQUIRE(backward);
    CHECK(backward.camera->position().x == Catch::Approx(-1.0F));

    const auto strafe = camera::LocalFreeFlightCameraController{}.update(
        previous,
        intent_from_events({input::InputEvent::focus_gained(),
            input::InputEvent::key_pressed(input::PhysicalKey::d)}),
        0.1,
        config);
    REQUIRE(strafe);
    CHECK(strafe.camera->position().x == Catch::Approx(0.0F).margin(1.0e-6F));
    CHECK(strafe.camera->position().y == Catch::Approx(-1.0F));

    const auto upward = camera::LocalFreeFlightCameraController{}.update(
        previous,
        intent_from_events({input::InputEvent::focus_gained(),
            input::InputEvent::key_pressed(input::PhysicalKey::space)}),
        0.1,
        config);
    const auto downward = camera::LocalFreeFlightCameraController{}.update(
        previous,
        intent_from_events({input::InputEvent::focus_gained(),
            input::InputEvent::key_pressed(input::PhysicalKey::left_control)}),
        0.1,
        config);
    REQUIRE(upward);
    REQUIRE(downward);
    CHECK(upward.camera->position().z == Catch::Approx(1.0F));
    CHECK(downward.camera->position().z == Catch::Approx(-1.0F));
}

TEST_CASE("Free-flight normalizes diagonals and applies the speed modifier once",
          "[gameplay-camera][free-flight][diagonal][speed]")
{
    camera::FirstPersonCameraConfigCreateInfo config_info;
    config_info.base_move_speed = 10.0;
    config_info.speed_multiplier = 2.0;
    const auto config = make_config(config_info);
    const auto previous = make_camera();

    const auto diagonal = camera::LocalFreeFlightCameraController{}.update(
        previous,
        intent_from_events({input::InputEvent::focus_gained(),
            input::InputEvent::key_pressed(input::PhysicalKey::w),
            input::InputEvent::key_pressed(input::PhysicalKey::d)}),
        0.1,
        config);
    REQUIRE(diagonal);
    CHECK(diagonal.movement_distance == Catch::Approx(1.0));
    CHECK(diagonal.camera->position().x ==
        Catch::Approx(0.70710677F).margin(1.0e-6F));
    CHECK(diagonal.camera->position().y ==
        Catch::Approx(-0.70710677F).margin(1.0e-6F));

    const auto fast = camera::LocalFreeFlightCameraController{}.update(
        previous,
        intent_from_events({input::InputEvent::focus_gained(),
            input::InputEvent::key_pressed(input::PhysicalKey::w),
            input::InputEvent::key_pressed(input::PhysicalKey::left_shift)}),
        0.1,
        config);
    REQUIRE(fast);
    CHECK(fast.movement_distance == Catch::Approx(2.0));
    CHECK(fast.camera->position().x == Catch::Approx(2.0F));

    const auto cancelled = camera::LocalFreeFlightCameraController{}.update(
        previous,
        intent_from_events({input::InputEvent::focus_gained(),
            input::InputEvent::key_pressed(input::PhysicalKey::w),
            input::InputEvent::key_pressed(input::PhysicalKey::s)}),
        0.1,
        config);
    REQUIRE(cancelled);
    CHECK(cancelled.movement_distance == 0.0);
    CHECK_FALSE(cancelled.revision_changed);
}

TEST_CASE("Free-flight bounds duration position and revision transactionally",
          "[gameplay-camera][free-flight][safety][transaction]")
{
    const auto forward = intent_from_events(
        {input::InputEvent::focus_gained(),
            input::InputEvent::key_pressed(input::PhysicalKey::w)});
    const auto defaults = camera::FirstPersonCameraConfig::project_default_v1();
    const auto previous = make_camera();

    const auto capped = camera::LocalFreeFlightCameraController{}.update(
        previous, forward, 1.0, defaults);
    REQUIRE(capped);
    CHECK(capped.status == camera::GameplayCameraUpdateStatus::duration_clamped);
    CHECK(capped.applied_duration_seconds == 0.1);
    CHECK(capped.camera->position().x == Catch::Approx(32.0F));

    const auto zero_duration =
        camera::LocalFreeFlightCameraController{}.update(
            previous, forward, 0.0, defaults);
    REQUIRE(zero_duration);
    CHECK(zero_duration.camera->position().x == 0.0F);
    CHECK_FALSE(zero_duration.revision_changed);

    const auto negative = camera::LocalFreeFlightCameraController{}.update(
        previous, forward, -0.001, defaults);
    REQUIRE_FALSE(negative);
    CHECK_FALSE(negative.camera);
    REQUIRE(negative.error);
    CHECK(negative.error->code ==
        camera::GameplayCameraErrorCode::invalid_duration);

    const auto non_finite = camera::LocalFreeFlightCameraController{}.update(
        previous,
        forward,
        std::numeric_limits<double>::quiet_NaN(),
        defaults);
    REQUIRE_FALSE(non_finite);
    CHECK_FALSE(non_finite.camera);
    REQUIRE(non_finite.error);
    CHECK(non_finite.error->code ==
        camera::GameplayCameraErrorCode::invalid_duration);

    camera::FirstPersonCameraConfigCreateInfo position_limit_info;
    position_limit_info.maximum_position_magnitude = 10.0;
    const auto position_limited_config = make_config(position_limit_info);
    const auto near_limit = make_camera({9.0F, 0.0F, 0.0F});
    const auto position_limited =
        camera::LocalFreeFlightCameraController{}.update(
            near_limit, forward, 0.1, position_limited_config);
    REQUIRE_FALSE(position_limited);
    CHECK_FALSE(position_limited.camera);
    REQUIRE(position_limited.error);
    CHECK(position_limited.error->code ==
        camera::GameplayCameraErrorCode::position_limit_exceeded);
    const auto already_outside_limit = make_camera({11.0F, 0.0F, 0.0F});
    const auto uncaptured = intent_from_events(
        {input::InputEvent::focus_gained()});
    const auto stationary_position_limited =
        camera::LocalFreeFlightCameraController{}.update(
            already_outside_limit,
            uncaptured,
            0.0,
            position_limited_config);
    REQUIRE_FALSE(stationary_position_limited);
    REQUIRE(stationary_position_limited.error);
    CHECK(stationary_position_limited.error->code ==
        camera::GameplayCameraErrorCode::position_limit_exceeded);

    camera::FirstPersonCameraConfigCreateInfo revision_limit_info;
    revision_limit_info.maximum_camera_revisions = 1U;
    const auto revision_limited_config = make_config(revision_limit_info);
    const auto revision_limited =
        camera::LocalFreeFlightCameraController{}.update(
            previous, forward, 0.1, revision_limited_config);
    REQUIRE_FALSE(revision_limited);
    CHECK_FALSE(revision_limited.camera);
    REQUIRE(revision_limited.error);
    CHECK(revision_limited.error->code ==
        camera::GameplayCameraErrorCode::revision_limit_exceeded);
    const auto already_over_revision = make_camera({}, 0.0, 0.0, 2U);
    const auto stationary_revision_limited =
        camera::LocalFreeFlightCameraController{}.update(
            already_over_revision,
            uncaptured,
            0.0,
            revision_limited_config);
    REQUIRE_FALSE(stationary_revision_limited);
    REQUIRE(stationary_revision_limited.error);
    CHECK(stationary_revision_limited.error->code ==
        camera::GameplayCameraErrorCode::revision_limit_exceeded);
}

TEST_CASE("Focus loss stops movement and look without gravity or drift",
          "[gameplay-camera][free-flight][focus-loss][no-physics]")
{
    const auto previous = make_camera({100.0F, 200.0F, 300.0F}, 25.0, -15.0);
    const auto lost_focus = intent_from_events(
        {input::InputEvent::focus_gained(),
            input::InputEvent::capture_acquired(),
            input::InputEvent::key_pressed(input::PhysicalKey::w),
            input::InputEvent::mouse_motion(100, 100),
            input::InputEvent::focus_lost()});
    const auto result = camera::LocalFreeFlightCameraController{}.update(
        previous,
        lost_focus,
        0.1,
        camera::FirstPersonCameraConfig::project_default_v1());
    REQUIRE(result);
    REQUIRE(result.camera);
    CHECK_FALSE(result.focused);
    CHECK_FALSE(result.captured);
    CHECK_FALSE(result.revision_changed);
    CHECK(result.camera->position().x == previous.position().x);
    CHECK(result.camera->position().y == previous.position().y);
    CHECK(result.camera->position().z == previous.position().z);
    CHECK(result.camera->yaw_degrees() == previous.yaw_degrees());
    CHECK(result.camera->pitch_degrees() == previous.pitch_degrees());
}

TEST_CASE("Free-flight controller rejects historical preview modes",
          "[gameplay-camera][free-flight][mode]")
{
    const auto intent = intent_from_events({input::InputEvent::focus_gained()});
    const auto historical = make_camera(
        {}, 0.0, 0.0, 1U, camera::GameplayCameraMode::orbit);
    const auto result = camera::LocalFreeFlightCameraController{}.update(
        historical,
        intent,
        0.05,
        camera::FirstPersonCameraConfig::project_default_v1());
    REQUIRE_FALSE(result);
    CHECK_FALSE(result.camera);
    REQUIRE(result.error);
    CHECK(result.error->code == camera::GameplayCameraErrorCode::invalid_input);
}

TEST_CASE("Free-flight scripted camera campaign is deterministic twenty times",
          "[gameplay-camera][free-flight][campaign][repeat]")
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
        tracker.apply_event(
            input::InputEvent::key_pressed(input::PhysicalKey::w));
        tracker.apply_event(input::InputEvent::mouse_motion(20, -10));
        const auto first_snapshot = tracker.publish_snapshot();
        tracker.end_frame();
        CHECK(first_snapshot.sequence() == 1U);
        const auto first = camera::LocalFreeFlightCameraController{}.update(
            make_camera(),
            build_intent(first_snapshot),
            0.05,
            camera::FirstPersonCameraConfig::project_default_v1());
        REQUIRE(first);

        tracker.begin_frame();
        const auto second_snapshot = tracker.publish_snapshot();
        tracker.end_frame();
        CHECK(second_snapshot.sequence() == 2U);
        const auto second = camera::LocalFreeFlightCameraController{}.update(
            *first.camera,
            build_intent(second_snapshot),
            0.05,
            camera::FirstPersonCameraConfig::project_default_v1());
        REQUIRE(second);

        tracker.begin_frame();
        tracker.apply_event(
            input::InputEvent::key_released(input::PhysicalKey::w));
        tracker.apply_event(input::InputEvent::capture_released());
        const auto third_snapshot = tracker.publish_snapshot();
        tracker.end_frame();
        CHECK(third_snapshot.sequence() == 3U);
        CHECK_FALSE(third_snapshot.key_held(input::PhysicalKey::w));
        CHECK_FALSE(third_snapshot.captured());
        const auto third = camera::LocalFreeFlightCameraController{}.update(
            *second.camera,
            build_intent(third_snapshot),
            0.05,
            camera::FirstPersonCameraConfig::project_default_v1());
        REQUIRE(third);
        REQUIRE(third.camera);
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
