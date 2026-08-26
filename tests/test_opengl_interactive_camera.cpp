#include "entity_render/entity_opengl_test_support.hpp"
#include "world_render_test_fixture.hpp"

#include <hlclient/gameplay_camera/entity_first_person_camera.hpp>
#include <hlclient/gameplay_camera/first_person_camera.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/renderer/render_scene.hpp>
#include <hlclient/world_render/world_render_types.hpp>

#include <glad/gl.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace camera = hlclient::gameplay_camera;
namespace entity = hlclient::entity_render;
namespace entity_build_fixture = hlclient::tests::entity_render_fixture;
namespace entity_fixture = hlclient::tests::entity_opengl_fixture;
namespace gameplay = hlclient::gameplay_input;
namespace input = hlclient::input;
namespace renderer = hlclient::renderer;
namespace world_fixture = hlclient::tests::world_render_fixture;
namespace world_render = hlclient::world_render;

constexpr std::size_t repeated_campaign_count = 20U;
constexpr double campaign_frame_seconds = 0.025;
constexpr renderer::RenderExtent test_extent{96, 96};
constexpr std::array<std::byte, 4U> test_clear_pixel{
    std::byte{5U}, std::byte{8U}, std::byte{10U}, std::byte{255U}};

[[nodiscard]] gameplay::GameplayInputBindings default_bindings()
{
    auto built = gameplay::GameplayInputBindings::project_default_v1();
    REQUIRE(built);
    REQUIRE(built.bindings);
    return std::move(*built.bindings);
}

[[nodiscard]] gameplay::GameplayInputIntent active_intent(
    const gameplay::GameplayInputBindings& bindings,
    const bool move_forward,
    const std::int32_t mouse_x,
    const std::int32_t mouse_y)
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    if (move_forward) {
        tracker.apply_event(
            input::InputEvent::key_pressed(input::PhysicalKey::w));
    }
    if (mouse_x != 0 || mouse_y != 0) {
        tracker.apply_event(
            input::InputEvent::mouse_motion(mouse_x, mouse_y));
    }
    const auto snapshot = tracker.publish_snapshot();
    tracker.end_frame();
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

[[nodiscard]] camera::GameplayCameraState world_camera()
{
    camera::GameplayCameraStateCreateInfo create_info;
    create_info.position = {8.0F, -12.0F, 8.0F};
    create_info.yaw_degrees = 90.0;
    create_info.pitch_degrees = -22.0;
    create_info.mode = camera::GameplayCameraMode::free_flight;
    auto created = camera::GameplayCameraState::create(create_info);
    INFO((created.error ? created.error->context : std::string_view{}));
    REQUIRE(created);
    REQUIRE(created.state);
    return std::move(*created.state);
}

[[nodiscard]] renderer::RenderScene world_scene(
    const std::shared_ptr<const world_render::WorldRenderPackage>& package,
    const renderer::RenderCamera& render_camera)
{
    renderer::RenderScene scene;
    scene.clear_color = {0.02F, 0.03F, 0.04F, 1.0F};
    scene.camera = render_camera;
    scene.static_world.emplace(renderer::RenderStaticWorld{
        package,
        renderer::RenderCullMode::none,
        renderer::RenderBaselineLightStylePolicy::source_slot_zero,
    });
    return scene;
}

[[nodiscard]] camera::GameplayCameraSourceFrameIdentity frame_identity(
    const hlclient::entity_render::EntityRenderFrame& frame) noexcept
{
    return {
        frame.resource_id(), frame.resource_revision(), frame.frame_signature()};
}

[[nodiscard]] camera::GameplayCameraState entity_camera(
    const camera::GameplayCameraSourceFrameIdentity identity)
{
    camera::GameplayCameraStateCreateInfo create_info;
    create_info.position = {0.0F, -12.0F, 3.0F};
    create_info.yaw_degrees = 90.0;
    create_info.pitch_degrees = -9.462322208025617;
    create_info.mode = camera::GameplayCameraMode::entity_first_person;
    create_info.anchor_metadata = {
        camera::GameplayCameraAnchorStatus::attached,
        1U,
        identity,
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
    const hlclient::entity_render::EntityRenderFrame& frame,
    const std::optional<hlclient::assets::AssetVector3>& explicit_eye_offset =
        std::nullopt)
{
    REQUIRE_FALSE(frame.studio_instances().empty());
    const auto& instance = frame.studio_instances().front();
    REQUIRE(instance.entity_number == 1U);
    const auto& origin = instance.transform.origin;
    camera::EntityFirstPersonCameraAnchorCreateInfo create_info;
    create_info.entity_number = instance.entity_number;
    create_info.interpolated_origin = origin;
    create_info.explicit_local_eye_offset = explicit_eye_offset.value_or(
        hlclient::assets::AssetVector3{
            -origin.x, -12.0F - origin.y, 3.0F - origin.z});
    create_info.source_frame_identity = frame_identity(frame);
    create_info.evidence_profile = camera::
        GameplayCameraAnchorEvidenceProfile::explicit_synthetic_playback_v1;
    auto created = camera::EntityFirstPersonCameraAnchor::create(create_info);
    INFO((created.error ? created.error->context : std::string_view{}));
    REQUIRE(created);
    REQUIRE(created.anchor);
    return std::move(*created.anchor);
}

[[nodiscard]] entity_fixture::SceneAndFrame mixed_entity_scene()
{
    auto package_result = entity_build_fixture::scene_package(
        entity_build_fixture::render_assets(true, true));
    REQUIRE(package_result);
    REQUIRE(package_result.package);
    auto package =
        std::make_shared<const hlclient::entity_render::EntitySceneRenderPackage>(
            std::move(*package_result.package));
    auto frame = entity_fixture::make_frame(*package, 1U, 1U, 1U);
    return {std::move(package), std::move(frame)};
}

[[nodiscard]] std::shared_ptr<const entity::EntityRenderFrame>
moved_mixed_entity_frame(
    const entity::EntitySceneRenderPackage& package,
    const entity::EntityRenderFrame& source,
    const float translation_x)
{
    entity::EntityRenderFrameBuildInput input;
    input.resource_id = source.resource_id();
    input.resource_revision = source.resource_revision() + 1U;
    input.interpolation = source.interpolation();
    input.studio_poses.assign(
        source.studio_poses().begin(), source.studio_poses().end());
    input.studio_instances.assign(
        source.studio_instances().begin(), source.studio_instances().end());
    input.sprite_instances.assign(
        source.sprite_instances().begin(), source.sprite_instances().end());
    input.unsupported_instances.assign(source.unsupported_instances().begin(),
        source.unsupported_instances().end());
    REQUIRE_FALSE(input.studio_instances.empty());
    auto& controlled = input.studio_instances.front();
    controlled.transform.origin.x += translation_x;
    controlled.interpolated_bounds.minimum.x += translation_x;
    controlled.interpolated_bounds.maximum.x += translation_x;
    auto built = entity::EntityRenderFrameBuilder{}.build(
        package, std::move(input));
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.frame);
    return std::make_shared<const entity::EntityRenderFrame>(
        std::move(*built.frame));
}

TEST_CASE("OpenGL world camera campaigns change pixels without static reupload 20 out of 20",
    "[renderer][opengl][input][gameplay-camera][world][integration][actual-context][campaign][repeat-20]")
{
    auto package_result = world_fixture::make_package();
    REQUIRE(package_result);
    REQUIRE(package_result.package);
    auto package = std::make_shared<const world_render::WorldRenderPackage>(
        std::move(*package_result.package));

    auto context = entity_fixture::try_context();
    if (!context || !entity_fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    const auto bindings = default_bindings();
    const auto config = camera::FirstPersonCameraConfig::project_default_v1();
    std::optional<std::vector<std::byte>> reference_initial_pixels;
    std::optional<std::vector<std::byte>> reference_updated_pixels;
    std::size_t completed_campaigns = 0U;

    for (std::size_t campaign = 0U; campaign < repeated_campaign_count;
         ++campaign) {
        INFO("campaign " << campaign + 1U << " of " << repeated_campaign_count);
        const auto initial = world_camera();
        const auto initial_render_camera = camera::build_render_camera(initial);
        REQUIRE(initial_render_camera);
        REQUIRE(initial_render_camera.camera);
        context->renderer().render(
            world_scene(package, *initial_render_camera.camera), test_extent);
        const auto initial_pixels = entity_fixture::framebuffer();
        REQUIRE(entity_fixture::has_non_clear_pixel(
            initial_pixels, test_clear_pixel));

        const auto intent = active_intent(bindings, true, 20, -10);
        REQUIRE(intent.input_sequence() == 1U);
        REQUIRE(intent.forward_axis() == 1.0F);
        REQUIRE(intent.look_delta_yaw_degrees() == Catch::Approx(-2.0));
        REQUIRE(intent.look_delta_pitch_degrees() == Catch::Approx(1.0));
        const auto updated = camera::LocalFreeFlightCameraController{}.update(
            initial, intent, campaign_frame_seconds, config);
        INFO((updated.error ? updated.error->context : std::string_view{}));
        REQUIRE(updated);
        REQUIRE(updated.camera);
        REQUIRE(updated.revision_changed);
        REQUIRE(updated.camera->yaw_degrees() == Catch::Approx(88.0));
        REQUIRE(updated.camera->pitch_degrees() == Catch::Approx(-21.0));
        REQUIRE(updated.camera->position().x ==
            Catch::Approx(8.279196F).margin(1.0e-5F));
        REQUIRE(updated.camera->position().y ==
            Catch::Approx(-4.004873F).margin(1.0e-5F));
        REQUIRE(updated.camera->position().z == 8.0F);
        REQUIRE(updated.movement_distance == Catch::Approx(8.0));
        const auto updated_render_camera =
            camera::build_render_camera(*updated.camera);
        REQUIRE(updated_render_camera);
        REQUIRE(updated_render_camera.camera);
        const auto initial_matrix = renderer::camera_view_projection(
            *initial_render_camera.camera, test_extent);
        const auto updated_matrix = renderer::camera_view_projection(
            *updated_render_camera.camera, test_extent);
        REQUIRE(initial_matrix);
        REQUIRE(updated_matrix);
        REQUIRE(*initial_matrix.matrix != *updated_matrix.matrix);
        context->renderer().render(
            world_scene(package, *updated_render_camera.camera), test_extent);
        const auto updated_pixels = entity_fixture::framebuffer();
        REQUIRE(entity_fixture::has_non_clear_pixel(
            updated_pixels, test_clear_pixel));
        REQUIRE(updated_pixels != initial_pixels);

        if (!reference_initial_pixels) {
            reference_initial_pixels = initial_pixels;
            reference_updated_pixels = updated_pixels;
        } else {
            REQUIRE(initial_pixels == *reference_initial_pixels);
            REQUIRE(updated_pixels == *reference_updated_pixels);
        }
        REQUIRE(context->renderer().statistics().upload_count == 1U);
        ++completed_campaigns;
    }

    CHECK(completed_campaigns == repeated_campaign_count);
    CHECK(context->renderer().statistics().upload_count == 1U);
    CHECK(context->renderer().statistics().active_world_resources);
    CHECK(context->renderer().statistics().rendered_frame_count == 40U);
    CHECK(context->renderer().statistics().draw_call_count == 40U);
    CHECK(context->renderer().statistics().triangle_count == 80U);
    CHECK(glGetError() == GL_NO_ERROR);
    context->release_renderer();
    CHECK(glGetError() == GL_NO_ERROR);
}

TEST_CASE("OpenGL entity first-person campaigns change pixels without asset reupload 20 out of 20",
    "[renderer][opengl][input][gameplay-camera][entity-first-person][integration][actual-context][campaign][repeat-20]")
{
    auto world_result = world_fixture::make_package();
    REQUIRE(world_result);
    REQUIRE(world_result.package);
    auto world = std::make_shared<const world_render::WorldRenderPackage>(
        std::move(*world_result.package));
    auto entities = mixed_entity_scene();
    const auto moved_frame = moved_mixed_entity_frame(
        *entities.package, *entities.frame, 1.0F);
    const entity_fixture::SceneAndFrame moved_entities{
        entities.package, moved_frame};
    const auto identity = frame_identity(*entities.frame);
    const auto anchor = entity_anchor(*entities.frame);
    const auto moved_anchor = entity_anchor(
        *moved_frame, anchor.explicit_local_eye_offset());

    auto context = entity_fixture::try_context();
    if (!context || !entity_fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    const auto bindings = default_bindings();
    const auto config = camera::FirstPersonCameraConfig::project_default_v1();
    std::optional<std::vector<std::byte>> reference_initial_pixels;
    std::optional<std::vector<std::byte>> reference_updated_pixels;
    std::size_t completed_campaigns = 0U;

    for (std::size_t campaign = 0U; campaign < repeated_campaign_count;
         ++campaign) {
        INFO("campaign " << campaign + 1U << " of " << repeated_campaign_count);
        const auto initial = entity_camera(identity);
        const auto initial_render_camera = camera::build_render_camera(initial);
        REQUIRE(initial_render_camera);
        REQUIRE(initial_render_camera.camera);
        auto initial_scene = entity_fixture::render_scene(entities);
        initial_scene.camera = *initial_render_camera.camera;
        initial_scene.static_world.emplace(renderer::RenderStaticWorld{
            world,
            renderer::RenderCullMode::none,
            renderer::RenderBaselineLightStylePolicy::source_slot_zero,
        });
        context->renderer().render(initial_scene, test_extent);
        const auto initial_pixels = entity_fixture::framebuffer();
        REQUIRE(entity_fixture::has_non_clear_pixel(
            initial_pixels, test_clear_pixel));
        const auto initial_statistics =
            context->renderer().entity_statistics();
        const auto initial_world_statistics =
            context->renderer().statistics();
        REQUIRE(initial_world_statistics.draw_call_count > 0U);

        const auto intent = active_intent(bindings, false, 50, 0);
        REQUIRE(intent.input_sequence() == 1U);
        REQUIRE(intent.look_delta_yaw_degrees() == Catch::Approx(-5.0));
        REQUIRE(intent.look_delta_pitch_degrees() == 0.0);
        const auto updated =
            camera::EntityFirstPersonCameraController{}.update(
                initial,
                intent,
                std::optional<camera::EntityFirstPersonCameraAnchor>{
                    moved_anchor},
                config);
        INFO((updated.error ? updated.error->context : std::string_view{}));
        REQUIRE(updated);
        REQUIRE(updated.camera);
        REQUIRE(updated.revision_changed);
        REQUIRE(updated.camera->yaw_degrees() == Catch::Approx(85.0));
        REQUIRE(updated.camera->pitch_degrees() ==
            Catch::Approx(-9.462322208025617));
        REQUIRE(updated.camera->position().x == 1.0F);
        REQUIRE(updated.camera->position().y == -12.0F);
        REQUIRE(updated.camera->position().z == 3.0F);
        REQUIRE(updated.camera->anchor_metadata().source_frame_identity ==
            frame_identity(*moved_frame));
        const auto updated_render_camera =
            camera::build_render_camera(*updated.camera);
        REQUIRE(updated_render_camera);
        REQUIRE(updated_render_camera.camera);
        const auto initial_matrix = renderer::camera_view_projection(
            *initial_render_camera.camera, test_extent);
        const auto updated_matrix = renderer::camera_view_projection(
            *updated_render_camera.camera, test_extent);
        REQUIRE(initial_matrix);
        REQUIRE(updated_matrix);
        REQUIRE(*initial_matrix.matrix != *updated_matrix.matrix);
        auto updated_scene = entity_fixture::render_scene(moved_entities);
        updated_scene.camera = *updated_render_camera.camera;
        updated_scene.static_world.emplace(renderer::RenderStaticWorld{
            world,
            renderer::RenderCullMode::none,
            renderer::RenderBaselineLightStylePolicy::source_slot_zero,
        });
        context->renderer().render(updated_scene, test_extent);
        const auto updated_pixels = entity_fixture::framebuffer();
        REQUIRE(entity_fixture::has_non_clear_pixel(
            updated_pixels, test_clear_pixel));
        REQUIRE(updated_pixels != initial_pixels);
        const auto updated_statistics =
            context->renderer().entity_statistics();
        REQUIRE(updated_statistics.studio_draw_count >
            initial_statistics.studio_draw_count);
        REQUIRE(updated_statistics.sprite_draw_count >
            initial_statistics.sprite_draw_count);
        REQUIRE(context->renderer().statistics().draw_call_count >
            initial_world_statistics.draw_call_count);

        if (!reference_initial_pixels) {
            reference_initial_pixels = initial_pixels;
            reference_updated_pixels = updated_pixels;
        } else {
            REQUIRE(initial_pixels == *reference_initial_pixels);
            REQUIRE(updated_pixels == *reference_updated_pixels);
        }
        REQUIRE(context->renderer()
                    .entity_statistics()
                    .studio_asset_upload_count == 1U);
        REQUIRE(context->renderer()
                    .entity_statistics()
                    .sprite_asset_upload_count == 1U);
        REQUIRE(context->renderer().statistics().upload_count == 1U);
        ++completed_campaigns;
    }

    CHECK(completed_campaigns == repeated_campaign_count);
    CHECK(context->renderer().entity_statistics().studio_asset_upload_count ==
        1U);
    CHECK(context->renderer().entity_statistics().sprite_asset_upload_count ==
        1U);
    CHECK(context->renderer().entity_statistics().active_entity_resources);
    CHECK(context->renderer().statistics().active_world_resources);
    CHECK(context->renderer().statistics().upload_count == 1U);
    CHECK(context->renderer().entity_statistics().entity_frame_count == 40U);
    CHECK(glGetError() == GL_NO_ERROR);
    context->release_renderer();
    CHECK(glGetError() == GL_NO_ERROR);
}

} // namespace
