#include "entity_render/entity_opengl_test_support.hpp"
#include "local_movement_test_fixture.hpp"
#include "world_render_test_fixture.hpp"

#include <hlclient/gameplay_camera/entity_first_person_camera.hpp>
#include <hlclient/gameplay_camera/first_person_camera.hpp>
#include <hlclient/gameplay_camera/render_camera_adapter.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <hlclient/local_player/local_player_movement_controller.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/renderer/render_scene.hpp>
#include <hlclient/world_render/world_render_types.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>
#include <hlclient/world_spatial/world_spatial_types.hpp>

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
namespace local_movement_fixture = hlclient::tests::local_movement;
namespace local_player = hlclient::local_player;
namespace renderer = hlclient::renderer;
namespace world_fixture = hlclient::tests::world_render_fixture;
namespace world_render = hlclient::world_render;
namespace scene_render = hlclient::world_scene_render;
namespace world_spatial = hlclient::world_spatial;

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
    const std::int32_t mouse_y,
    const bool jump = false)
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    if (move_forward) {
        tracker.apply_event(
            input::InputEvent::key_pressed(input::PhysicalKey::w));
    }
    if (jump) {
        tracker.apply_event(
            input::InputEvent::key_pressed(input::PhysicalKey::space));
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

[[nodiscard]] std::shared_ptr<const scene_render::WorldSceneRenderPackage>
player_walk_scene_package(
    const std::shared_ptr<const world_render::WorldRenderPackage>& package)
{
    const auto bounds = package->bounds();
    world_spatial::WorldSpatialNode node;
    node.plane_index = 0U;
    node.children = {
        world_spatial::WorldSpatialNodeChild{
            world_spatial::WorldSpatialNodeChildKind::leaf, 1U},
        world_spatial::WorldSpatialNodeChild{
            world_spatial::WorldSpatialNodeChildKind::leaf, 0U},
    };
    node.bounds = bounds;

    world_spatial::WorldSpatialLeaf solid_leaf;
    solid_leaf.source_leaf_index = 0U;
    solid_leaf.bounds = bounds;
    solid_leaf.surface_membership.source_leaf_index = 0U;
    solid_leaf.solid_or_special = true;

    world_spatial::WorldSpatialLeaf visible_leaf;
    visible_leaf.source_leaf_index = 1U;
    visible_leaf.bounds = bounds;
    visible_leaf.pvs_row_index = 0U;
    visible_leaf.pvs_bit_addressable = true;
    visible_leaf.surface_membership.source_leaf_index = 1U;
    visible_leaf.surface_membership.source_marksurface_count =
        static_cast<std::uint32_t>(package->surface_ranges().size());
    for (std::size_t index = 0U; index < package->surface_ranges().size();
         ++index) {
        visible_leaf.surface_membership.world_surface_indices.push_back(
            static_cast<std::uint32_t>(index));
    }

    world_spatial::WorldSpatialPackage spatial{
        {world_spatial::WorldSpatialPlane{
            {1.0F, 0.0F, 0.0F}, 0.0F, 0}},
        {node},
        {solid_leaf, visible_leaf},
        world_spatial::WorldPvsTable{
            1U,
            1U,
            {{std::byte{0x01U}}},
            {std::nullopt, 0U},
            0U},
        world_spatial::WorldSpatialModelMetadata{0U, 1U, bounds},
        world_spatial::WorldSpatialStatistics{
            1U,
            1U,
            2U,
            package->surface_ranges().size(),
            package->surface_ranges().size(),
            1U,
            1U},
        world_spatial::WorldSpatialCompatibilityProfile::
            goldsrc_bsp_v30_leaf_one_is_pvs_bit_zero,
        world_spatial::WorldSpatialEvidenceProfile::
            canonical_validated_bsp_records};

    std::vector<std::uint32_t> brush_surface_indices;
    brush_surface_indices.reserve(package->surface_ranges().size());
    for (std::size_t index = 0U; index < package->surface_ranges().size();
         ++index) {
        brush_surface_indices.push_back(static_cast<std::uint32_t>(index));
    }
    std::vector<scene_render::BrushSubmodelRenderModel> brush_models;
    brush_models.emplace_back(
        1U, bounds, std::move(brush_surface_indices));
    scene_render::BrushSubmodelRenderLibrary brush_library{
        package, std::move(brush_models)};

    auto built = scene_render::WorldSceneRenderPackageBuilder{}.build(
        package, std::move(spatial), std::move(brush_library));
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.package);
    return std::make_shared<const scene_render::WorldSceneRenderPackage>(
        std::move(*built.package));
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

TEST_CASE("OpenGL player-walk movement changes camera pixels without resource reupload",
    "[renderer][opengl][input][gameplay-camera][player-walk][local-movement][integration][actual-context]")
{
    auto package_result = world_fixture::make_package();
    REQUIRE(package_result);
    REQUIRE(package_result.package);
    auto package = std::make_shared<const world_render::WorldRenderPackage>(
        std::move(*package_result.package));
    const auto scene_package = player_walk_scene_package(package);

    auto context = entity_fixture::try_context();
    if (!context || !entity_fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    const auto initial_state = local_movement_fixture::make_state(
        {8.0F, -40.0F, 36.0F});
    local_player::LocalPlayerMovementController controller{
        initial_state, local_movement_fixture::make_environment()};
    local_player::LocalPlayerMovementController reference_controller{
        initial_state, local_movement_fixture::make_environment()};
    REQUIRE(controller.valid_configuration());
    REQUIRE(reference_controller.valid_configuration());

    local_movement_fixture::DeterministicLocalMovementCollision collision;
    local_movement_fixture::DeterministicLocalMovementCollision
        reference_collision;
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch scratch;
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch reference_scratch;
    const auto bindings = default_bindings();
    const auto setup_intent = active_intent(bindings, true, -900, 520);
    REQUIRE(setup_intent.look_delta_yaw_degrees() == Catch::Approx(90.0));
    REQUIRE(setup_intent.look_delta_pitch_degrees() == Catch::Approx(-52.0));
    const auto setup =
        controller.update(0, setup_intent, collision, scratch);
    const auto reference_setup = reference_controller.update(
        0, setup_intent, reference_collision, reference_scratch);
    REQUIRE(setup);
    REQUIRE(reference_setup);
    REQUIRE(setup.generated_command_count == 0U);
    REQUIRE(reference_setup.generated_command_count == 0U);
    REQUIRE(setup.final_state_signature == reference_setup.final_state_signature);
    REQUIRE(controller.camera().mode() ==
        camera::GameplayCameraMode::player_walk);
    REQUIRE(controller.camera().yaw_degrees() == Catch::Approx(90.0));
    REQUIRE(controller.camera().pitch_degrees() == Catch::Approx(-52.0));

    const auto initial_camera = camera::build_render_camera(controller.camera());
    REQUIRE(initial_camera);
    REQUIRE(initial_camera.camera);
    auto scene = world_scene(package, *initial_camera.camera);
    REQUIRE(scene.static_world);
    scene.static_world->scene_package = scene_package;
    context->renderer().render(scene, test_extent);
    const auto initial_pixels = entity_fixture::framebuffer();
    REQUIRE(entity_fixture::has_non_clear_pixel(
        initial_pixels, test_clear_pixel));
    const auto initial_statistics = context->renderer().statistics();
    REQUIRE(initial_statistics.active_world_resources);
    REQUIRE(initial_statistics.scene_present);
    REQUIRE(initial_statistics.upload_count == 1U);
    REQUIRE(initial_statistics.scene_upload_count == 1U);
    REQUIRE(initial_statistics.brush_upload_count == 1U);

    constexpr std::int64_t movement_step_nanoseconds = 10'000'000;
    constexpr std::int64_t movement_step_count = 6;
    std::uint64_t jump_count = 0U;
    for (std::int64_t step = 1; step <= movement_step_count; ++step) {
        INFO("movement step " << step << " of " << movement_step_count);
        const auto command_intent = step == 1
            ? active_intent(bindings, false, 0, 0)
            : step == 4
            ? active_intent(bindings, true, -50, 0)
            : active_intent(bindings, true, 0, 0, step == 5);
        const auto update = controller.update(
            step * movement_step_nanoseconds,
            command_intent,
            collision,
            scratch);
        const auto reference_update = reference_controller.update(
            step * movement_step_nanoseconds,
            command_intent,
            reference_collision,
            reference_scratch);
        REQUIRE(update);
        REQUIRE(reference_update);
        REQUIRE(update.generated_command_count == 1U);
        REQUIRE(reference_update.generated_command_count == 1U);
        REQUIRE(update.final_state_signature ==
            reference_update.final_state_signature);
        jump_count += update.statistics.jump_count;
        REQUIRE(controller.camera().position().x == Catch::Approx(
            reference_controller.camera().position().x));
        REQUIRE(controller.camera().position().y == Catch::Approx(
            reference_controller.camera().position().y));
        REQUIRE(controller.camera().position().z == Catch::Approx(
            reference_controller.camera().position().z));
    }

    REQUIRE(controller.player_state().source_command_sequence() == 6U);
    REQUIRE(jump_count == 1U);
    REQUIRE_FALSE(controller.player_state().ground_state().grounded());
    REQUIRE(controller.player_state().origin().y > initial_state.origin().y);
    REQUIRE(controller.camera().position().y >
        initial_camera.camera->position.y);
    REQUIRE(controller.camera().position().x == Catch::Approx(
        controller.player_state().origin().x));
    REQUIRE(controller.camera().position().y == Catch::Approx(
        controller.player_state().origin().y));
    REQUIRE(controller.camera().position().z == Catch::Approx(
        controller.player_state().origin().z +
        controller.player_state().view_offset().z));

    const auto moved_camera = camera::build_render_camera(controller.camera());
    REQUIRE(moved_camera);
    REQUIRE(moved_camera.camera);
    const auto initial_matrix = renderer::camera_view_projection(
        *initial_camera.camera, test_extent);
    const auto moved_matrix = renderer::camera_view_projection(
        *moved_camera.camera, test_extent);
    REQUIRE(initial_matrix);
    REQUIRE(moved_matrix);
    REQUIRE(*initial_matrix.matrix != *moved_matrix.matrix);
    scene.camera = *moved_camera.camera;
    context->renderer().render(scene, test_extent);
    const auto moved_pixels = entity_fixture::framebuffer();
    REQUIRE(entity_fixture::has_non_clear_pixel(
        moved_pixels, test_clear_pixel));
    REQUIRE(moved_pixels != initial_pixels);

    const auto& moved_statistics = context->renderer().statistics();
    CHECK(moved_statistics.upload_count == initial_statistics.upload_count);
    CHECK(moved_statistics.scene_upload_count ==
        initial_statistics.scene_upload_count);
    CHECK(moved_statistics.brush_upload_count ==
        initial_statistics.brush_upload_count);
    CHECK(moved_statistics.rendered_frame_count == 2U);
    CHECK(moved_statistics.failed_upload_count == 0U);
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
