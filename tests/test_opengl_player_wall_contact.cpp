#include "entity_render/entity_opengl_test_support.hpp"
#include "local_movement_test_fixture.hpp"
#include "world_render_test_fixture.hpp"

#include <hlclient/gameplay_camera/render_camera_adapter.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/input/input_source.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <hlclient/local_player/local_player_movement_controller.hpp>
#include <hlclient/platform/sdl_window.hpp>
#include <hlclient/renderer/render_scene.hpp>
#include <hlclient/world_render/world_render_types.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>
#include <hlclient/world_spatial/world_spatial_types.hpp>

#include <glad/gl.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace camera = hlclient::gameplay_camera;
namespace entity_fixture = hlclient::tests::entity_opengl_fixture;
namespace fixture = hlclient::tests::local_movement;
namespace gameplay = hlclient::gameplay_input;
namespace input = hlclient::input;
namespace local_player = hlclient::local_player;
namespace renderer = hlclient::renderer;
namespace scene_render = hlclient::world_scene_render;
namespace world_fixture = hlclient::tests::world_render_fixture;
namespace world_render = hlclient::world_render;
namespace world_spatial = hlclient::world_spatial;

constexpr std::size_t frame_count = 1'000U;
constexpr renderer::RenderExtent extent{96, 96};
constexpr std::array<std::byte, 4U> clear_pixel{
    std::byte{5U}, std::byte{8U}, std::byte{10U}, std::byte{255U}};

[[nodiscard]] std::shared_ptr<const scene_render::WorldSceneRenderPackage>
scene_package(
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
            1U, 1U, {{std::byte{0x01U}}}, {std::nullopt, 0U}, 0U},
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

    std::vector<std::uint32_t> surface_indices;
    surface_indices.reserve(package->surface_ranges().size());
    for (std::size_t index = 0U; index < package->surface_ranges().size();
         ++index) {
        surface_indices.push_back(static_cast<std::uint32_t>(index));
    }
    std::vector<scene_render::BrushSubmodelRenderModel> brush_models;
    brush_models.emplace_back(1U, bounds, std::move(surface_indices));
    auto built = scene_render::WorldSceneRenderPackageBuilder{}.build(
        package,
        std::move(spatial),
        scene_render::BrushSubmodelRenderLibrary{
            package, std::move(brush_models)});
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.package);
    return std::make_shared<const scene_render::WorldSceneRenderPackage>(
        std::move(*built.package));
}

[[nodiscard]] input::ScriptedInputSource scripted_campaign()
{
    input::ScriptedInputSource::Script frames(frame_count);
    frames[0U] = {
        input::InputEvent::focus_gained(),
        input::InputEvent::capture_acquired(),
        input::InputEvent::mouse_motion(-900, 520),
        input::InputEvent::key_pressed(input::PhysicalKey::w),
    };
    frames[350U].push_back(
        input::InputEvent::key_pressed(input::PhysicalKey::d));
    frames[550U].push_back(
        input::InputEvent::key_released(input::PhysicalKey::w));
    frames[650U].push_back(
        input::InputEvent::key_released(input::PhysicalKey::d));
    frames[700U] = {
        input::InputEvent::key_pressed(input::PhysicalKey::w),
        input::InputEvent::key_pressed(input::PhysicalKey::space),
    };
    frames[701U].push_back(
        input::InputEvent::key_released(input::PhysicalKey::space));
    frames[820U].push_back(
        input::InputEvent::key_pressed(input::PhysicalKey::left_control));
    frames[920U].push_back(
        input::InputEvent::key_released(input::PhysicalKey::left_control));
    frames[999U].push_back(
        input::InputEvent::key_released(input::PhysicalKey::w));
    return input::ScriptedInputSource{
        std::move(frames),
        input::ScriptedInputSourceLimits{frame_count, 16U, 32U}};
}

[[nodiscard]] bool finite_state(
    const hlclient::movement::LocalPlayerMovementState& state) noexcept
{
    const auto finite = [](const hlclient::assets::AssetVector3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
            std::isfinite(value.z);
    };
    return finite(state.origin()) && finite(state.velocity()) &&
        finite(state.view_offset()) && finite(state.view_angles());
}

[[nodiscard]] bool same_statistics(
    const hlclient::movement::PlayerMovementStatistics& left,
    const hlclient::movement::PlayerMovementStatistics& right) noexcept
{
    return left.command_count == right.command_count &&
        left.substep_count == right.substep_count &&
        left.grounded_command_count == right.grounded_command_count &&
        left.airborne_command_count == right.airborne_command_count &&
        left.ground_probe_count == right.ground_probe_count &&
        left.trace_count == right.trace_count &&
        left.collision_hit_count == right.collision_hit_count &&
        left.slide_bump_count == right.slide_bump_count &&
        left.clip_plane_count == right.clip_plane_count &&
        left.step_attempt_count == right.step_attempt_count &&
        left.step_success_count == right.step_success_count &&
        left.jump_count == right.jump_count &&
        left.duck_enter_count == right.duck_enter_count &&
        left.duck_exit_count == right.duck_exit_count &&
        left.stand_blocked_count == right.stand_blocked_count &&
        left.start_solid_count == right.start_solid_count &&
        left.all_solid_count == right.all_solid_count &&
        left.total_horizontal_distance == right.total_horizontal_distance &&
        left.total_vertical_distance == right.total_vertical_distance;
}

TEST_CASE("OpenGL startup capability classification does not mask runtime failures",
    "[renderer][opengl][capability]")
{
    using Failure = hlclient::platform::OpenGlStartupCapabilityFailure;
    const hlclient::platform::OpenGlStartupCapabilityError unavailable{
        Failure::context_unavailable, "bounded test context"};
    CHECK(hlclient::platform::classify_opengl_startup_capability_failure(
              unavailable) == Failure::context_unavailable);

    const std::runtime_error missing_functions{
        "glad2 failed to load OpenGL functions"};
    CHECK(hlclient::platform::classify_opengl_startup_capability_failure(
              missing_functions) == Failure::function_loading_unavailable);

    const std::runtime_error generic_sdl_initialization{
        "SDL initialization failed: generic failure"};
    const std::runtime_error generic_context_failure{
        "OpenGL context creation failed: driver returned an unspecified error"};
    const std::runtime_error sdl_allocation_failure{
        "OpenGL context creation failed: Out of memory"};
    CHECK(hlclient::platform::classify_opengl_startup_capability_failure(
              generic_sdl_initialization) == Failure::none);
    CHECK(hlclient::platform::classify_opengl_startup_capability_failure(
              generic_context_failure) == Failure::none);
    CHECK(hlclient::platform::classify_opengl_startup_capability_failure(
              sdl_allocation_failure) == Failure::none);
    CHECK_FALSE(hlclient::platform::
        proves_opengl_startup_capability_unavailable(
            Failure::video_subsystem_unavailable,
            generic_sdl_initialization.what()));
    CHECK_FALSE(hlclient::platform::
        proves_opengl_startup_capability_unavailable(
            Failure::context_unavailable,
            generic_context_failure.what()));
    CHECK_FALSE(hlclient::platform::
        proves_opengl_startup_capability_unavailable(
            Failure::context_unavailable,
            sdl_allocation_failure.what()));
    CHECK(hlclient::platform::proves_opengl_startup_capability_unavailable(
        Failure::video_subsystem_unavailable,
        "No available video device"));
    CHECK(hlclient::platform::proves_opengl_startup_capability_unavailable(
        Failure::context_unavailable,
        "requested GL context is not supported"));

    const std::runtime_error swap_failure{"OpenGL buffer swap failed: test"};
    const std::runtime_error shader_failure{"shader compilation failed"};
    const std::logic_error programming_failure{"programming failure"};
    const std::bad_alloc allocation_failure;
    CHECK(hlclient::platform::classify_opengl_startup_capability_failure(
              swap_failure) == Failure::none);
    CHECK(hlclient::platform::classify_opengl_startup_capability_failure(
              shader_failure) == Failure::none);
    CHECK(hlclient::platform::classify_opengl_startup_capability_failure(
              programming_failure) == Failure::none);
    CHECK(hlclient::platform::classify_opengl_startup_capability_failure(
              allocation_failure) == Failure::none);
}

TEST_CASE("Actual OpenGL renders a 1000-frame scripted player wall campaign",
    "[renderer][opengl][player-walk][wall-contact][actual-context]")
{
    auto package_result = world_fixture::make_package();
    REQUIRE(package_result);
    REQUIRE(package_result.package);
    auto package = std::make_shared<const world_render::WorldRenderPackage>(
        std::move(*package_result.package));
    const auto world_scene_package = scene_package(package);
    std::unique_ptr<entity_fixture::HiddenContext> context;
    try {
        context = std::make_unique<entity_fixture::HiddenContext>();
    } catch (const std::exception& error) {
        const auto capability = hlclient::platform::
            classify_opengl_startup_capability_failure(error);
        if (capability !=
            hlclient::platform::OpenGlStartupCapabilityFailure::none) {
            SKIP("OpenGL window/context unavailable on this host");
        }
        throw;
    }
    if (!entity_fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    try {
        context->initialize_renderer();
    } catch (const std::exception& error) {
        const auto capability = hlclient::platform::
            classify_opengl_startup_capability_failure(error);
        if (capability !=
            hlclient::platform::OpenGlStartupCapabilityFailure::none) {
            SKIP("OpenGL 3.3 Core functions unavailable on this host");
        }
        // Shader, upload and other renderer failures are test failures.
        throw;
    }

    auto controller = local_player::LocalPlayerMovementController{
        fixture::make_state({8.0F, -40.0F, 36.0F}),
        fixture::make_environment()};
    REQUIRE(controller.valid_configuration());
    fixture::DeterministicLocalMovementCollision collision;
    collision.add_positive_y_wall(0.0F);
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch scratch;
    auto wall_probe_start = controller.player_state().origin();
    wall_probe_start.z += controller.environment().step_size() + 1.0F;
    auto wall_probe_end = wall_probe_start;
    wall_probe_end.y += 2'048.0F;
    const auto wall_probe = collision.trace_hull(
        wall_probe_start,
        wall_probe_end,
        hlclient::movement::PlayerMovementHull::standing,
        scratch.collision,
        controller.config().movement.collision_query);
    REQUIRE(wall_probe);
    REQUIRE(wall_probe.result);
    REQUIRE(wall_probe.result->collision_plane);
    REQUIRE(wall_probe.result->hit);
    REQUIRE(std::abs(wall_probe.result->collision_plane->normal.z) <= 0.2F);
    const local_player::LocalPlayerMovementCommittedTouchFilter
        selected_wall_filter{
            *wall_probe.result->hit, *wall_probe.result->collision_plane};
    auto bindings_result = gameplay::GameplayInputBindings::project_default_v1();
    REQUIRE(bindings_result);
    auto bindings = std::move(*bindings_result.bindings);
    auto source = scripted_campaign();
    input::InputStateTracker tracker;
    std::uint64_t collision_count = 0U;
    std::uint64_t committed_wall_contact_count = 0U;
    std::uint64_t jump_count = 0U;
    std::uint64_t duck_enter_count = 0U;
    std::uint64_t duck_exit_count = 0U;
    bool final_input_focused = false;
    bool final_input_captured = false;
    std::vector<hlclient::movement::PlayerMovementStatistics>
        rendered_pass_statistics;
    rendered_pass_statistics.reserve(frame_count);
    std::vector<std::uint64_t> rendered_pass_wall_contacts;
    rendered_pass_wall_contacts.reserve(frame_count);
    bool observed_non_clear_framebuffer = false;

    for (std::size_t frame = 0U; frame < frame_count; ++frame) {
        source.begin_frame();
        tracker.begin_frame();
        input::InputEvent event = input::InputEvent::focus_lost();
        while (source.poll_event(event)) {
            tracker.apply_event(event);
        }
        source.end_frame();
        const auto snapshot = tracker.publish_snapshot();
        if (frame + 1U == frame_count) {
            final_input_focused = snapshot.focused();
            final_input_captured = snapshot.captured();
        }
        tracker.end_frame();
        auto intent = gameplay::GameplayInputIntentBuilder{}.build(
            snapshot, bindings, gameplay::MouseLookConfig{}, 0.01);
        REQUIRE(intent);
        const auto update = controller.update(
            static_cast<std::int64_t>(frame) * 10'000'000,
            *intent.intent,
            collision,
            scratch,
            &selected_wall_filter);
        REQUIRE(update);
        rendered_pass_statistics.push_back(update.statistics);
        rendered_pass_wall_contacts.push_back(
            update.committed_touch_match_count);
        REQUIRE(update.committed_touch_match_count <=
            UINT64_MAX - committed_wall_contact_count);
        committed_wall_contact_count +=
            update.committed_touch_match_count;
        collision_count += update.statistics.collision_hit_count;
        jump_count += update.statistics.jump_count;
        duck_enter_count += update.statistics.duck_enter_count;
        duck_exit_count += update.statistics.duck_exit_count;
        REQUIRE(finite_state(controller.player_state()));
        const auto position = collision.test_position(
            controller.player_state().origin(),
            controller.player_state().hull(),
            scratch.collision,
            controller.config().movement.collision_query);
        REQUIRE(position);
        REQUIRE(position.result);
        REQUIRE(position.result->status == hlclient::goldsrc::movement::
            LocalMovementPositionStatus::free);

        const auto render_camera = camera::build_render_camera(
            controller.camera());
        REQUIRE(render_camera);
        REQUIRE(render_camera.camera);
        renderer::RenderScene scene;
        scene.clear_color = {0.02F, 0.03F, 0.04F, 1.0F};
        scene.camera = *render_camera.camera;
        scene.static_world.emplace(renderer::RenderStaticWorld{
            package,
            renderer::RenderCullMode::none,
            renderer::RenderBaselineLightStylePolicy::source_slot_zero});
        scene.static_world->scene_package = world_scene_package;
        context->renderer().render(scene, extent);
        if (frame == 0U || frame + 1U == frame_count) {
            observed_non_clear_framebuffer =
                observed_non_clear_framebuffer ||
                entity_fixture::has_non_clear_pixel(
                    entity_fixture::framebuffer(), clear_pixel);
        }
    }

    auto reference_controller = local_player::LocalPlayerMovementController{
        fixture::make_state({8.0F, -40.0F, 36.0F}),
        fixture::make_environment()};
    fixture::DeterministicLocalMovementCollision reference_collision;
    reference_collision.add_positive_y_wall(0.0F);
    const local_player::LocalPlayerMovementCommittedTouchFilter
        reference_wall_filter{
            *wall_probe.result->hit, *wall_probe.result->collision_plane};
    hlclient::goldsrc::movement::GoldSrcLocalMovementScratch reference_scratch;
    auto reference_source = scripted_campaign();
    input::InputStateTracker reference_tracker;
    for (std::size_t frame = 0U; frame < frame_count; ++frame) {
        reference_source.begin_frame();
        reference_tracker.begin_frame();
        input::InputEvent event = input::InputEvent::focus_lost();
        while (reference_source.poll_event(event)) {
            reference_tracker.apply_event(event);
        }
        reference_source.end_frame();
        const auto snapshot = reference_tracker.publish_snapshot();
        reference_tracker.end_frame();
        auto intent = gameplay::GameplayInputIntentBuilder{}.build(
            snapshot, bindings, gameplay::MouseLookConfig{}, 0.01);
        REQUIRE(intent);
        const auto update = reference_controller.update(
            static_cast<std::int64_t>(frame) * 10'000'000,
            *intent.intent,
            reference_collision,
            reference_scratch,
            &reference_wall_filter);
        REQUIRE(update);
        REQUIRE(same_statistics(
            update.statistics, rendered_pass_statistics[frame]));
        REQUIRE(update.committed_touch_match_count ==
            rendered_pass_wall_contacts[frame]);
    }
    CHECK(hlclient::movement::local_player_movement_state_signature(
              reference_controller.player_state()) ==
        hlclient::movement::local_player_movement_state_signature(
            controller.player_state()));

    const auto pixels = entity_fixture::framebuffer();
    CHECK((observed_non_clear_framebuffer ||
        entity_fixture::has_non_clear_pixel(pixels, clear_pixel)));
    CHECK(controller.player_state().source_command_sequence() ==
        frame_count - 1U);
    CHECK(collision_count > 0U);
    CHECK(committed_wall_contact_count > 0U);
    CHECK(jump_count > 0U);
    CHECK(duck_enter_count > 0U);
    CHECK(duck_exit_count > 0U);
    CHECK(final_input_focused);
    CHECK(final_input_captured);
    const auto& statistics = context->renderer().statistics();
    CHECK(statistics.rendered_frame_count == frame_count);
    CHECK(statistics.upload_count == 1U);
    CHECK(statistics.scene_upload_count == 1U);
    CHECK(statistics.brush_upload_count == 1U);
    CHECK(statistics.failed_upload_count == 0U);
    CHECK(glGetError() == GL_NO_ERROR);
    context->release_renderer();
    CHECK(glGetError() == GL_NO_ERROR);
}

} // namespace
