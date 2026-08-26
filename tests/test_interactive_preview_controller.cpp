#include <hlclient/client/client_world_state.hpp>
#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/gameplay_camera/first_person_camera.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/input/input_event.hpp>
#include <hlclient/input/input_source.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <hlclient/interactive_preview/interactive_preview_controller.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>
#include <hlclient/world_preview/world_preview_scene_source.hpp>

#include "entity_render/entity_render_test_fixture.hpp"
#include "world_render_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace {

namespace camera = hlclient::gameplay_camera;
namespace entity = hlclient::entity_render;
namespace entity_fixture = hlclient::tests::entity_render_fixture;
namespace gameplay = hlclient::gameplay_input;
namespace input = hlclient::input;
namespace preview = hlclient::interactive_preview;
namespace world_fixture = hlclient::tests::world_render_fixture;

[[nodiscard]] gameplay::GameplayInputBindings default_bindings()
{
    auto built = gameplay::GameplayInputBindings::project_default_v1();
    REQUIRE(built);
    REQUIRE(built.bindings);
    return std::move(*built.bindings);
}

[[nodiscard]] camera::GameplayCameraState initial_camera(
    const camera::GameplayCameraMode mode =
        camera::GameplayCameraMode::free_flight)
{
    camera::GameplayCameraStateCreateInfo create_info;
    create_info.position = {0.0F, 0.0F, 32.0F};
    create_info.mode = mode;
    if (mode == camera::GameplayCameraMode::entity_first_person) {
        create_info.anchor_metadata = {
            camera::GameplayCameraAnchorStatus::attached,
            1U,
            camera::GameplayCameraSourceFrameIdentity{1U, 1U, 1U},
            camera::GameplayCameraAnchorEvidenceProfile::
                explicit_synthetic_playback_v1,
        };
    }
    auto built = camera::GameplayCameraState::create(create_info);
    REQUIRE(built);
    REQUIRE(built.state);
    return std::move(*built.state);
}

[[nodiscard]] input::InputSnapshot active_movement_snapshot(
    input::InputStateTracker& tracker)
{
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::w));
    tracker.apply_event(input::InputEvent::mouse_motion(10, -5));
    auto snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    return snapshot;
}

[[nodiscard]] std::shared_ptr<const entity::EntitySceneRenderPackage>
entity_package()
{
    auto built = entity_fixture::scene_package(
        entity_fixture::render_assets(true, false));
    REQUIRE(built);
    REQUIRE(built.package);
    return std::make_shared<const entity::EntitySceneRenderPackage>(
        std::move(*built.package));
}

[[nodiscard]] std::shared_ptr<const entity::EntitySceneRenderPackage>
mixed_entity_package()
{
    auto built = entity_fixture::scene_package(
        entity_fixture::render_assets(true, true));
    REQUIRE(built);
    REQUIRE(built.package);
    return std::make_shared<const entity::EntitySceneRenderPackage>(
        std::move(*built.package));
}

[[nodiscard]] std::shared_ptr<const entity::EntityRenderFrame>
entity_frame(const entity::EntitySceneRenderPackage& package,
    const hlclient::assets::AssetVector3 origin,
    const std::uint64_t revision,
    const bool include_sprite = false)
{
    const std::array<float, 16U> identity{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
    entity::EntityRenderFrameBuildInput input;
    input.resource_id = 0xE460U;
    input.resource_revision = revision;
    input.interpolation = {0.5,
        0.0,
        1.0,
        0.5F,
        revision,
        revision + 1U,
        entity::EntityRenderInterpolationProfile::synthetic_seconds_v1};
    input.studio_poses.push_back(
        {package.studio_assets()[0U]->source_identity(), {identity}});
    entity::StudioEntityRenderInstance instance;
    instance.entity_number = 1U;
    instance.studio_asset_index = 0U;
    instance.pose_index = 0U;
    instance.transform.origin = origin;
    instance.interpolated_bounds = {
        {origin.x - 1.0F, origin.y - 1.0F, origin.z - 1.0F},
        {origin.x + 1.0F, origin.y + 1.0F, origin.z + 1.0F}};
    input.studio_instances.push_back(instance);
    if (include_sprite) {
        REQUIRE(package.sprite_assets().size() == 1U);
        entity::SpriteEntityRenderInstance sprite;
        sprite.entity_number = 2U;
        sprite.sprite_asset_index = 0U;
        sprite.selected_frame_index = 0U;
        sprite.orientation = package.sprite_assets()[0U]->orientation();
        sprite.texture_format_support =
            package.sprite_assets()[0U]->texture_support_status();
        sprite.transform.origin = {origin.x + 4.0F, origin.y, origin.z};
        sprite.transform.uniform_scale = 0.35F;
        sprite.bounds = {{origin.x + 3.0F, origin.y - 1.0F, origin.z - 1.0F},
            {origin.x + 5.0F, origin.y + 1.0F, origin.z + 1.0F}};
        input.sprite_instances.push_back(sprite);
    }
    auto built = entity::EntityRenderFrameBuilder{}.build(
        package, std::move(input));
    REQUIRE(built);
    REQUIRE(built.frame);
    return std::make_shared<const entity::EntityRenderFrame>(
        std::move(*built.frame));
}

} // namespace

TEST_CASE("Interactive preview publishes deterministic free-flight camera state",
    "[input][camera][interactive-preview][client]")
{
    auto created = preview::InteractivePreviewController::create(
        preview::InteractivePreviewMode::free_flight_world,
        default_bindings(),
        camera::FirstPersonCameraConfig::project_default_v1(),
        initial_camera());
    REQUIRE(created);
    REQUIRE(created.controller);
    auto controller = std::move(*created.controller);

    input::InputStateTracker tracker;
    const auto first = active_movement_snapshot(tracker);
    hlclient::client::ClientWorldState state;
    const auto updated = controller.update(first, 0.05, state);
    REQUIRE(updated);
    CHECK(updated.camera_revision_changed);
    CHECK(updated.input_sequence == 1U);
    CHECK(state.input_revision() == 1U);
    CHECK(state.camera_revision() == controller.camera()->revision());
    CHECK(controller.camera()->position().z == 32.0F);
    CHECK(controller.camera()->position().x != 0.0F);
    CHECK(std::abs(controller.camera()->yaw_degrees()) > 0.0);
    CHECK(std::abs(controller.camera()->pitch_degrees()) > 0.0);
    const auto retained_controller_camera = controller.camera();

    const auto stable_camera = state.camera();
    const auto stable_revision = state.camera_revision();
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_lost());
    const auto lost_focus = tracker.publish_snapshot();
    tracker.end_frame();
    const auto stopped = controller.update(lost_focus, 0.05, state);
    REQUIRE(stopped);
    CHECK_FALSE(stopped.camera_revision_changed);
    CHECK(state.camera_revision() == stable_revision);
    CHECK(state.camera().position.x == stable_camera.position.x);
    CHECK(state.camera().position.y == stable_camera.position.y);
    CHECK(state.camera().position.z == stable_camera.position.z);
    CHECK(retained_controller_camera->revision() == stable_revision);
    CHECK(retained_controller_camera->position().x == stable_camera.position.x);
    REQUIRE(state.interactive_camera_metadata());
    CHECK(state.interactive_camera_metadata()->mode ==
        hlclient::client::InteractiveCameraMode::free_flight_world);

    const auto duplicate = controller.update(lost_focus, 0.05, state);
    CHECK_FALSE(duplicate);
    REQUIRE(duplicate.error);
    CHECK(duplicate.error->code ==
        preview::InteractivePreviewErrorCode::
            non_monotonic_input_sequence);
    CHECK(state.camera().position.x == stable_camera.position.x);
    CHECK(state.camera().position.y == stable_camera.position.y);
    CHECK(state.camera().position.z == stable_camera.position.z);
    CHECK(controller.statistics().published_update_count == 2U);
    CHECK(controller.statistics().focus_reset_count == 1U);
}

TEST_CASE("Interactive entity camera freezes deterministically when anchor is missing",
    "[input][camera][interactive-preview][entity-render]")
{
    auto created = preview::InteractivePreviewController::create(
        preview::InteractivePreviewMode::entity_first_person,
        default_bindings(),
        camera::FirstPersonCameraConfig::project_default_v1(),
        initial_camera(camera::GameplayCameraMode::entity_first_person),
        1U,
        {0.0F, 0.0F, 28.0F});
    REQUIRE(created);
    auto controller = std::move(*created.controller);

    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    tracker.apply_event(input::InputEvent::mouse_motion(20, 0));
    const auto snapshot = tracker.publish_snapshot();
    tracker.end_frame();

    hlclient::client::ClientWorldState state;
    const auto frozen = controller.update(snapshot, 0.016, state, nullptr);
    REQUIRE(frozen);
    CHECK(frozen.status ==
        preview::InteractivePreviewUpdateStatus::
            anchor_missing_camera_frozen);
    REQUIRE(state.interactive_camera_metadata());
    CHECK(state.interactive_camera_metadata()->controlled_entity == 1U);
    CHECK(state.interactive_camera_metadata()->controlled_entity_status ==
        hlclient::client::ControlledEntityCameraStatus::anchor_missing);
    CHECK(controller.statistics().anchor_missing_count == 1U);
}

TEST_CASE("Project-default entity preview starts with typed missing-anchor identity",
    "[input][camera][interactive-preview][entity-render][initialization]")
{
    const hlclient::client::RenderCameraState render_camera{
        {1.0F, 2.0F, 3.0F},
        {2.0F, 2.0F, 3.0F},
        {0.0F, 0.0F, 1.0F},
        1.0F,
        0.1F,
        4'096.0F,
    };
    auto created = preview::InteractivePreviewController::
        create_project_default_v1(
            preview::InteractivePreviewMode::entity_first_person,
            render_camera,
            7U);
    REQUIRE(created);
    REQUIRE(created.controller);
    auto controller = std::move(*created.controller);
    CHECK(controller.camera()->anchor_metadata().status ==
        camera::GameplayCameraAnchorStatus::anchor_entity_missing);
    CHECK(controller.camera()->anchor_metadata().entity_number == 7U);

    hlclient::client::ClientWorldState state;
    REQUIRE(controller.seed_world_state_camera(state));
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    const auto snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    const auto missing = controller.update(snapshot, 0.016, state, nullptr);
    REQUIRE(missing);
    CHECK(missing.status == preview::InteractivePreviewUpdateStatus::
        anchor_missing_camera_frozen);
    REQUIRE(state.interactive_camera_metadata());
    CHECK(state.interactive_camera_metadata()->controlled_entity == 7U);
    CHECK(state.interactive_camera_metadata()->controlled_entity_status ==
        hlclient::client::ControlledEntityCameraStatus::anchor_missing);
}

TEST_CASE("Interactive entity camera follows the exact synthetic entity frame",
    "[input][camera][interactive-preview][entity-render][follow]")
{
    const auto package = entity_package();
    const auto first_frame = entity_frame(*package, {10.0F, 20.0F, 30.0F}, 1U);
    const auto second_frame = entity_frame(*package, {12.0F, 22.0F, 32.0F}, 2U);
    auto created = preview::InteractivePreviewController::create(
        preview::InteractivePreviewMode::entity_first_person,
        default_bindings(),
        camera::FirstPersonCameraConfig::project_default_v1(),
        initial_camera(camera::GameplayCameraMode::entity_first_person),
        1U,
        {0.0F, 0.0F, 28.0F});
    REQUIRE(created);
    REQUIRE(created.controller);
    auto controller = std::move(*created.controller);

    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    const auto first_snapshot = tracker.publish_snapshot();
    tracker.end_frame();

    hlclient::client::ClientWorldState state;
    const auto first = controller.update(
        first_snapshot, 0.016, state, first_frame.get());
    REQUIRE(first);
    CHECK(first.camera_revision_changed);
    CHECK(controller.camera()->position().x == 10.0F);
    CHECK(controller.camera()->position().y == 20.0F);
    CHECK(controller.camera()->position().z == 58.0F);

    tracker.begin_frame();
    const auto second_snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    const auto followed = controller.update(
        second_snapshot, 0.016, state, second_frame.get());
    REQUIRE(followed);
    CHECK(followed.camera_revision_changed);
    CHECK(controller.camera()->position().x == 12.0F);
    CHECK(controller.camera()->position().y == 22.0F);
    CHECK(controller.camera()->position().z == 60.0F);
    REQUIRE(controller.camera()->anchor_metadata().source_frame_identity);
    CHECK(controller.camera()->anchor_metadata().source_frame_identity->
        resource_revision == second_frame->resource_revision());
    REQUIRE(state.interactive_camera_metadata());
    CHECK(state.interactive_camera_metadata()->controlled_entity_status ==
        hlclient::client::ControlledEntityCameraStatus::anchored);
    CHECK(first_frame->studio_instances()[0U].transform.origin.x == 10.0F);
    CHECK(second_frame->studio_instances()[0U].transform.origin.x == 12.0F);
}

TEST_CASE("Interactive preview forwards click capture and Escape release gestures",
    "[input][camera][interactive-preview][capture]")
{
    auto created = preview::InteractivePreviewController::create(
        preview::InteractivePreviewMode::free_flight_world,
        default_bindings(),
        camera::FirstPersonCameraConfig::project_default_v1(),
        initial_camera());
    REQUIRE(created);
    REQUIRE(created.controller);
    auto controller = std::move(*created.controller);
    input::InputStateTracker tracker;
    hlclient::client::ClientWorldState state;

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    const auto click = tracker.publish_snapshot();
    tracker.end_frame();
    const auto capture = controller.update(click, 0.016, state);
    REQUIRE(capture);
    CHECK(capture.capture_mouse_requested);
    CHECK_FALSE(capture.release_mouse_requested);

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::capture_acquired());
    const auto captured = tracker.publish_snapshot();
    tracker.end_frame();
    const auto settled = controller.update(captured, 0.016, state);
    REQUIRE(settled);
    CHECK_FALSE(settled.capture_mouse_requested);
    CHECK_FALSE(settled.release_mouse_requested);

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::key_pressed(
        input::PhysicalKey::escape));
    const auto escape = tracker.publish_snapshot();
    tracker.end_frame();
    const auto release = controller.update(escape, 0.016, state);
    REQUIRE(release);
    CHECK_FALSE(release.capture_mouse_requested);
    CHECK(release.release_mouse_requested);
    CHECK(controller.statistics().capture_request_count == 1U);
    CHECK(controller.statistics().release_request_count == 1U);
}

TEST_CASE("Scripted offline free-flight preview renders without OS input",
    "[input][camera][interactive-preview][scripted][renderer][null]")
{
    auto built_world = world_fixture::make_package();
    REQUIRE(built_world);
    REQUIRE(built_world.package);
    auto world =
        std::make_shared<const hlclient::world_render::WorldRenderPackage>(
            std::move(*built_world.package));
    const auto world_revision = world->resource_revision();

    hlclient::world_preview::WorldPreviewSceneOptions options;
    options.camera_mode =
        hlclient::world_preview::WorldPreviewCameraMode::free_flight;
    hlclient::world_preview::WorldPreviewSceneSource source{world, options};
    auto created = preview::InteractivePreviewController::
        create_project_default_v1(
            preview::InteractivePreviewMode::free_flight_world,
            source.world_state().camera());
    REQUIRE(created);
    REQUIRE(created.controller);
    auto controller = std::move(*created.controller);

    input::ScriptedInputSource scripted{{
        {input::InputEvent::focus_gained(),
            input::InputEvent::mouse_button_pressed(
                input::PhysicalMouseButton::left)},
        {input::InputEvent::capture_acquired(),
            input::InputEvent::key_pressed(input::PhysicalKey::w),
            input::InputEvent::mouse_motion(20, -10)},
        {input::InputEvent::key_released(input::PhysicalKey::w),
            input::InputEvent::mouse_button_released(
                input::PhysicalMouseButton::left),
            input::InputEvent::key_pressed(input::PhysicalKey::escape),
            input::InputEvent::key_released(input::PhysicalKey::escape),
            input::InputEvent::capture_released()},
    }};
    input::InputStateTracker tracker;
    hlclient::renderer::null::NullRenderer renderer;
    renderer.initialize();
    const auto initial_position = source.world_state().camera().position;
    bool saw_capture_request = false;
    bool saw_release_request = false;

    while (!scripted.exhausted()) {
        scripted.begin_frame();
        tracker.begin_frame();
        auto event = input::InputEvent::focus_lost();
        while (scripted.poll_event(event)) {
            tracker.apply_event(event);
        }
        scripted.end_frame();
        const auto snapshot = tracker.publish_snapshot();
        tracker.end_frame();

        const auto camera_update = controller.update(
            snapshot, 0.025, source);
        REQUIRE(camera_update);
        saw_capture_request = saw_capture_request ||
            camera_update.capture_mouse_requested;
        saw_release_request = saw_release_request ||
            camera_update.release_mouse_requested;
        const auto scene_update = source.update(hlclient::client::FrameTime{0.025});
        REQUIRE(scene_update);
        renderer.render(
            hlclient::client::build_render_scene(source.world_state()),
            {640, 480});
    }

    CHECK(saw_capture_request);
    CHECK(saw_release_request);
    CHECK(scripted.total_event_count() == 10U);
    CHECK(tracker.published_frame_count() == 3U);
    REQUIRE(tracker.last_published_snapshot());
    CHECK_FALSE(tracker.last_published_snapshot()->captured());
    CHECK_FALSE(tracker.last_published_snapshot()->key_held(
        input::PhysicalKey::w));
    CHECK_FALSE(tracker.last_published_snapshot()->mouse_button_held(
        input::PhysicalMouseButton::left));
    CHECK(source.world_state().camera().position.x != initial_position.x);
    CHECK(source.world_state().world_revision() == world_revision);
    CHECK(source.world_state().static_world() == world);
    const auto statistics = renderer.statistics();
    CHECK(statistics.rendered_frames == 3U);
    CHECK(statistics.static_world_present);
    CHECK(statistics.package_revision == world_revision);
    CHECK(statistics.camera_valid);
}

TEST_CASE("Scripted entity-first-person preview carries world Studio and Sprite",
    "[input][camera][interactive-preview][scripted][entity-render][world][renderer][null]")
{
    auto built_world = world_fixture::make_package();
    REQUIRE(built_world);
    REQUIRE(built_world.package);
    auto world =
        std::make_shared<const hlclient::world_render::WorldRenderPackage>(
            std::move(*built_world.package));
    const auto entities = mixed_entity_package();
    const auto first_frame = entity_frame(
        *entities, {0.0F, 0.0F, 0.0F}, 1U, true);
    const auto moved_frame = entity_frame(
        *entities, {2.0F, 0.0F, 0.0F}, 2U, true);

    hlclient::world_preview::WorldPreviewSceneOptions options;
    options.camera_mode =
        hlclient::world_preview::WorldPreviewCameraMode::entity_first_person;
    hlclient::world_preview::WorldPreviewSceneSource source{world, options};
    auto created = preview::InteractivePreviewController::
        create_project_default_v1(
            preview::InteractivePreviewMode::entity_first_person,
            source.world_state().camera(),
            1U,
            {0.0F, -12.0F, 3.0F});
    REQUIRE(created);
    REQUIRE(created.controller);
    auto controller = std::move(*created.controller);
    REQUIRE(controller.seed_world_state_camera(source));

    input::ScriptedInputSource scripted{{
        {input::InputEvent::focus_gained(),
            input::InputEvent::capture_acquired()},
        {input::InputEvent::mouse_motion(10, 0)},
        {input::InputEvent::focus_lost()},
    }};
    const std::array frames{first_frame, moved_frame, moved_frame};
    input::InputStateTracker tracker;
    hlclient::renderer::null::NullRenderer renderer;
    renderer.initialize();
    std::optional<float> first_camera_x;
    std::optional<double> first_camera_yaw;

    while (!scripted.exhausted()) {
        const auto frame_index = scripted.frame_index();
        scripted.begin_frame();
        tracker.begin_frame();
        auto event = input::InputEvent::focus_lost();
        while (scripted.poll_event(event)) {
            tracker.apply_event(event);
        }
        scripted.end_frame();
        const auto snapshot = tracker.publish_snapshot();
        tracker.end_frame();

        const auto updated = controller.update(snapshot,
            0.025,
            source,
            frames[frame_index].get());
        REQUIRE(updated);
        REQUIRE(source.publish_dynamic_entities(
            entities, frames[frame_index]));
        REQUIRE(source.update(hlclient::client::FrameTime{0.025}));
        const auto scene =
            hlclient::client::build_render_scene(source.world_state());
        REQUIRE(scene.static_world);
        REQUIRE(scene.dynamic_entities);
        CHECK(scene.dynamic_entities->package == entities);
        CHECK(scene.dynamic_entities->frame == frames[frame_index]);
        renderer.render(scene, {640, 480});

        if (!first_camera_x) {
            first_camera_x = controller.camera()->position().x;
            first_camera_yaw = controller.camera()->yaw_degrees();
        }
    }

    REQUIRE(first_camera_x);
    REQUIRE(first_camera_yaw);
    CHECK(*first_camera_x == 0.0F);
    CHECK(controller.camera()->position().x == 2.0F);
    CHECK(controller.camera()->yaw_degrees() != *first_camera_yaw);
    CHECK_FALSE(tracker.last_published_snapshot()->captured());
    CHECK(scripted.total_event_count() == 4U);
    const auto statistics = renderer.statistics();
    CHECK(statistics.rendered_frames == 3U);
    CHECK(statistics.static_world_present);
    CHECK(statistics.dynamic_entity_package_present);
    CHECK(statistics.studio_entity_instance_count == 1U);
    CHECK(statistics.sprite_entity_instance_count == 1U);
    CHECK(statistics.visible_entity_count == 2U);
    CHECK(statistics.camera_valid);
}

TEST_CASE("Interactive preview rejects mismatched mode without publication",
    "[input][camera][interactive-preview][limits]")
{
    auto invalid = preview::InteractivePreviewController::create(
        preview::InteractivePreviewMode::entity_first_person,
        default_bindings(),
        camera::FirstPersonCameraConfig::project_default_v1(),
        initial_camera(),
        1U);
    CHECK_FALSE(invalid);
    REQUIRE(invalid.error);
    CHECK(invalid.error->code ==
        preview::InteractivePreviewErrorCode::invalid_configuration);

    auto mismatched_anchor = preview::InteractivePreviewController::create(
        preview::InteractivePreviewMode::entity_first_person,
        default_bindings(),
        camera::FirstPersonCameraConfig::project_default_v1(),
        initial_camera(camera::GameplayCameraMode::entity_first_person),
        2U);
    CHECK_FALSE(mismatched_anchor);
    REQUIRE(mismatched_anchor.error);
    CHECK(mismatched_anchor.error->code ==
        preview::InteractivePreviewErrorCode::invalid_configuration);

    const hlclient::client::RenderCameraState valid_render_camera{
        {0.0F, -1.0F, 0.0F},
        {0.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
        1.0F,
        0.1F,
        4'096.0F,
    };
    auto invalid_render_camera = valid_render_camera;
    invalid_render_camera.up = {};
    CHECK_FALSE(preview::InteractivePreviewController::
        create_project_default_v1(
            preview::InteractivePreviewMode::free_flight_world,
            invalid_render_camera));
    invalid_render_camera = valid_render_camera;
    invalid_render_camera.up = {0.0F, 1.0F, 0.0F};
    CHECK_FALSE(preview::InteractivePreviewController::
        create_project_default_v1(
            preview::InteractivePreviewMode::free_flight_world,
            invalid_render_camera));
    invalid_render_camera = valid_render_camera;
    invalid_render_camera.vertical_field_of_view_radians = 3.2F;
    CHECK_FALSE(preview::InteractivePreviewController::
        create_project_default_v1(
            preview::InteractivePreviewMode::free_flight_world,
            invalid_render_camera));

    camera::FirstPersonCameraConfigCreateInfo narrow_config_info;
    narrow_config_info.maximum_position_magnitude = 1.0;
    auto narrow_config = camera::FirstPersonCameraConfig::create(
        narrow_config_info);
    REQUIRE(narrow_config);
    REQUIRE(narrow_config.config);
    auto incompatible_config = preview::InteractivePreviewController::create(
        preview::InteractivePreviewMode::free_flight_world,
        default_bindings(),
        std::move(*narrow_config.config),
        initial_camera());
    CHECK_FALSE(incompatible_config);
    REQUIRE(incompatible_config.error);
    CHECK(incompatible_config.error->code ==
        preview::InteractivePreviewErrorCode::invalid_configuration);
}

TEST_CASE("Interactive preview accepts the exact float minimum FOV bound",
    "[input][camera][interactive-preview][limits][fov]")
{
    constexpr float minimum_fov_radians = static_cast<float>(
        20.0 * 3.141592653589793238462643383279502884 / 180.0);
    hlclient::client::RenderCameraState render_camera{
        {0.0F, -1.0F, 0.0F},
        {0.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
        minimum_fov_radians,
        0.1F,
        4'096.0F,
    };

    auto exact_minimum = preview::InteractivePreviewController::
        create_project_default_v1(
            preview::InteractivePreviewMode::free_flight_world,
            render_camera);
    REQUIRE(exact_minimum);
    REQUIRE(exact_minimum.controller);
    CHECK(exact_minimum.controller->camera()->vertical_fov_radians() ==
        20.0 * 3.141592653589793238462643383279502884 / 180.0);

    render_camera.vertical_field_of_view_radians =
        std::nextafter(minimum_fov_radians, 0.0F);
    const auto below_minimum = preview::InteractivePreviewController::
        create_project_default_v1(
            preview::InteractivePreviewMode::free_flight_world,
            render_camera);
    CHECK_FALSE(below_minimum);
    REQUIRE(below_minimum.error);
    CHECK(below_minimum.error->code ==
        preview::InteractivePreviewErrorCode::invalid_configuration);
}

TEST_CASE("Interactive preview accepts the renderer float maximum FOV bound",
    "[input][camera][interactive-preview][limits][fov]")
{
    constexpr double maximum_fov_radians =
        140.0 * 3.141592653589793238462643383279502884 / 180.0;
    constexpr float renderer_maximum_fov_radians =
        static_cast<float>(maximum_fov_radians);
    static_assert(static_cast<double>(renderer_maximum_fov_radians) <
        maximum_fov_radians);
    hlclient::client::RenderCameraState render_camera{
        {0.0F, -1.0F, 0.0F},
        {0.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
        renderer_maximum_fov_radians,
        0.1F,
        4'096.0F,
    };

    const auto exact_maximum = preview::InteractivePreviewController::
        create_project_default_v1(
            preview::InteractivePreviewMode::free_flight_world,
            render_camera);
    REQUIRE(exact_maximum);
    REQUIRE(exact_maximum.controller);
    CHECK(exact_maximum.controller->camera()->vertical_fov_radians() ==
        static_cast<double>(renderer_maximum_fov_radians));

    render_camera.vertical_field_of_view_radians = std::nextafter(
        renderer_maximum_fov_radians,
        std::numeric_limits<float>::infinity());
    const auto above_maximum = preview::InteractivePreviewController::
        create_project_default_v1(
            preview::InteractivePreviewMode::free_flight_world,
            render_camera);
    CHECK_FALSE(above_maximum);
    REQUIRE(above_maximum.error);
    CHECK(above_maximum.error->code ==
        preview::InteractivePreviewErrorCode::invalid_configuration);
}
