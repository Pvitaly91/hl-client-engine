#include "world_render_test_fixture.hpp"

#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>
#include <hlclient/world_preview/world_preview_scene_source.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>
#include <hlclient/world_spatial/world_spatial_types.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

namespace client = hlclient::client;
namespace fixture = hlclient::tests::world_render_fixture;
namespace renderer = hlclient::renderer;
namespace scene_render = hlclient::world_scene_render;
namespace spatial = hlclient::world_spatial;
namespace visibility = hlclient::world_visibility;
namespace world_preview = hlclient::world_preview;
namespace world_render = hlclient::world_render;

template <typename Type>
concept HasGoldSrcOrNetworkState = requires(Type value) {
    value.bsp;
    value.netchan;
    value.resource_list;
};

[[nodiscard]] std::shared_ptr<const world_render::WorldRenderPackage> make_shared_package()
{
    auto built = fixture::make_package();
    if (!built || !built.package) {
        throw std::runtime_error{"Unable to create synthetic render package"};
    }
    return std::make_shared<const world_render::WorldRenderPackage>(
        std::move(*built.package));
}

[[nodiscard]] spatial::WorldSpatialPackage make_spatial_package(
    const bool camera_in_leaf_zero = false)
{
    const hlclient::assets::WorldBounds bounds{
        {0.0F, 0.0F, -1.0F}, {16.0F, 16.0F, 1.0F}};
    spatial::WorldSpatialPlane plane{{1.0F, 0.0F, 0.0F}, 0.0F, 0};
    spatial::WorldSpatialNode node;
    node.plane_index = 0U;
    const auto camera_leaf = camera_in_leaf_zero ? 0U : 1U;
    node.children = {
        spatial::WorldSpatialNodeChild{
            spatial::WorldSpatialNodeChildKind::leaf, camera_leaf},
        spatial::WorldSpatialNodeChild{
            spatial::WorldSpatialNodeChildKind::leaf, camera_leaf},
    };
    node.bounds = bounds;

    spatial::WorldSpatialLeaf leaf_zero;
    leaf_zero.source_leaf_index = 0U;
    leaf_zero.bounds = bounds;
    leaf_zero.surface_membership.source_leaf_index = 0U;
    leaf_zero.solid_or_special = true;

    spatial::WorldSpatialLeaf leaf_one;
    leaf_one.source_leaf_index = 1U;
    leaf_one.bounds = bounds;
    leaf_one.pvs_row_index = 0U;
    leaf_one.pvs_bit_addressable = true;
    leaf_one.surface_membership.source_leaf_index = 1U;
    leaf_one.surface_membership.source_marksurface_count = 1U;
    leaf_one.surface_membership.world_surface_indices = {0U};

    std::vector<std::vector<std::byte>> rows{{std::byte{0x01U}}};
    std::vector<std::optional<std::uint32_t>> leaf_rows{std::nullopt, 0U};
    return spatial::WorldSpatialPackage{
        {plane},
        {node},
        {leaf_zero, leaf_one},
        spatial::WorldPvsTable{
            1U, 1U, std::move(rows), std::move(leaf_rows), 0U},
        spatial::WorldSpatialModelMetadata{0U, 1U, bounds},
        spatial::WorldSpatialStatistics{1U, 1U, 2U, 1U, 1U, 1U, 1U},
        spatial::WorldSpatialCompatibilityProfile::
            goldsrc_bsp_v30_leaf_one_is_pvs_bit_zero,
        spatial::WorldSpatialEvidenceProfile::canonical_validated_bsp_records,
    };
}

[[nodiscard]] std::shared_ptr<const scene_render::WorldSceneRenderPackage>
make_shared_scene_package(
    const bool with_brush = false,
    const bool camera_in_leaf_zero = false)
{
    auto world = make_shared_package();
    std::vector<scene_render::BrushSubmodelRenderModel> models;
    std::vector<scene_render::BrushSubmodelRenderInstance> instances;
    if (with_brush) {
        models.emplace_back(
            1U, world->bounds(), std::vector<std::uint32_t>{0U});

        scene_render::BrushSubmodelRenderInstance instance;
        instance.source_instance_index = 7U;
        instance.source_entity_ordinal = 3U;
        instance.source_model_index = 1U;
        instance.model_transform.values[12U] = 2.0F;
        instance.transformed_bounds = world->bounds();
        instance.transformed_bounds.minimum.x += 2.0F;
        instance.transformed_bounds.maximum.x += 2.0F;
        instance.touched_leaf_indices = {1U};
        instance.support_status = scene_render::
            BrushSubmodelRenderSupportStatus::supported_static_opaque;
        instances.push_back(std::move(instance));
    }
    scene_render::BrushSubmodelRenderLibrary library{
        with_brush ? world : decltype(world){}, std::move(models)};

    scene_render::WorldSceneRenderPackageBuilder builder;
    auto built = builder.build(
        std::move(world),
        make_spatial_package(camera_in_leaf_zero),
        std::move(library),
        std::move(instances));
    if (!built || !built.package) {
        throw std::runtime_error{"Unable to create synthetic scene package"};
    }
    return std::make_shared<const scene_render::WorldSceneRenderPackage>(
        std::move(*built.package));
}

[[nodiscard]] std::shared_ptr<const scene_render::WorldSceneRenderPackage>
make_resizable_scene_package()
{
    fixture::FixtureOptions options;
    options.atlas_page_count = 2U;
    auto built_world = fixture::make_package(options);
    if (!built_world || !built_world.package) {
        throw std::runtime_error{
            "Unable to create resizable world package fixture"};
    }
    auto world = std::make_shared<const world_render::WorldRenderPackage>(
        std::move(*built_world.package));
    scene_render::WorldSceneRenderPackageBuilder builder;
    auto built_scene = builder.build(
        std::move(world), make_spatial_package());
    if (!built_scene || !built_scene.package) {
        throw std::runtime_error{
            "Unable to create resizable scene package fixture"};
    }
    return std::make_shared<const scene_render::WorldSceneRenderPackage>(
        std::move(*built_scene.package));
}

TEST_CASE("World preview assigns an immutable package and bounds-derived static camera",
          "[world-preview][scene]")
{
    STATIC_REQUIRE_FALSE(HasGoldSrcOrNetworkState<renderer::RenderScene>);
    auto package = make_shared_package();
    world_preview::WorldPreviewSceneOptions options;
    options.minimum_radius = 1.0F;
    options.camera_mode = world_preview::WorldPreviewCameraMode::static_camera;
    world_preview::WorldPreviewSceneSource source{package, options};

    CHECK(source.world_state().static_world() == package);
    CHECK(source.world_state().world_revision() == package->resource_revision());
    CHECK(source.world_center().x == 8.0F);
    CHECK(source.world_center().y == 8.0F);
    CHECK(source.world_center().z == 0.0F);
    CHECK(source.world_radius() == Catch::Approx(std::sqrt(128.0F)));
    CHECK_FALSE(source.world_state().connection_requested());

    const auto initial = source.world_state().camera();
    REQUIRE(source.update(std::chrono::duration<double>{20.0}));
    const auto after = source.world_state().camera();
    CHECK(after.position.x == initial.position.x);
    CHECK(after.position.y == initial.position.y);
    CHECK(after.position.z == initial.position.z);
    CHECK(after.target.x == source.world_center().x);
    CHECK(after.target.y == source.world_center().y);
    CHECK(after.target.z == source.world_center().z);
    CHECK_FALSE(source.world_state().connection_requested());
}

TEST_CASE("World preview orbit is deterministic, bounded and input-independent",
          "[world-preview][scene]")
{
    auto package = make_shared_package();
    world_preview::WorldPreviewSceneOptions options;
    options.camera_mode = world_preview::WorldPreviewCameraMode::orbit;
    options.orbit_angular_velocity_radians_per_second = 0.25F;
    world_preview::WorldPreviewSceneSource first{package, options};
    world_preview::WorldPreviewSceneSource second{package, options};
    const auto initial = first.world_state().camera().position;

    REQUIRE(first.update(std::chrono::duration<double>{2.0}));
    REQUIRE(second.update(std::chrono::duration<double>{2.0}));
    const auto first_camera = first.world_state().camera();
    const auto second_camera = second.world_state().camera();
    CHECK(first_camera.position.x == Catch::Approx(second_camera.position.x));
    CHECK(first_camera.position.y == Catch::Approx(second_camera.position.y));
    CHECK(first_camera.position.z == Catch::Approx(second_camera.position.z));
    CHECK(first_camera.position.x != initial.x);
    CHECK(first_camera.position.y != initial.y);
    CHECK(first_camera.position.z == Catch::Approx(initial.z));
    CHECK(first_camera.target.x == second_camera.target.x);
    CHECK(first.world_state().elapsed_seconds() == 2.0);
    CHECK_FALSE(first.world_state().connection_requested());
}

TEST_CASE("Client world state converts exactly to renderer-neutral static world scene",
          "[world-preview][scene]")
{
    auto package = make_shared_package();
    world_preview::WorldPreviewSceneOptions options;
    options.cull_mode = client::PreviewWorldCullMode::back;
    world_preview::WorldPreviewSceneSource source{package, options};
    const auto scene = client::build_render_scene(source.world_state());

    CHECK(scene.clear_color.red == 0.035F);
    CHECK(scene.clear_color.green == 0.055F);
    CHECK(scene.clear_color.blue == 0.085F);
    CHECK(scene.camera.position.x == source.world_state().camera().position.x);
    CHECK(scene.camera.position.y == source.world_state().camera().position.y);
    CHECK(scene.camera.position.z == source.world_state().camera().position.z);
    REQUIRE(scene.static_world);
    CHECK(scene.static_world->package == package);
    CHECK(scene.static_world->cull_mode == renderer::RenderCullMode::back);
    CHECK(scene.static_world->light_style_policy ==
        renderer::RenderBaselineLightStylePolicy::source_slot_zero);

    renderer::null::NullRenderer null_renderer;
    null_renderer.initialize();
    null_renderer.render(scene, {800, 600});
    const auto statistics = null_renderer.statistics();
    CHECK(statistics.rendered_frames == 1U);
    CHECK(statistics.static_world_present);
    CHECK(statistics.package_resource_id == package->resource_id());
    CHECK(statistics.package_revision == package->resource_revision());
    CHECK(statistics.camera_valid);
}

TEST_CASE("World preview publishes immutable scene visibility and draw commands",
          "[world-preview][scene][visibility]")
{
    const auto package = make_shared_scene_package();
    world_preview::WorldPreviewSceneSource source{package};
    const auto& state = source.world_state();

    CHECK(state.static_world() == package->world_package());
    CHECK(state.world_scene() == package);
    CHECK(state.world_revision() == package->world_package()->resource_revision());
    CHECK(state.scene_revision() == package->resource_revision());
    REQUIRE(state.world_visibility());
    REQUIRE(state.visible_draw_list());
    CHECK(state.visibility_revision() == 1U);
    CHECK(state.world_visibility()->revision() == 1U);
    CHECK(state.world_visibility()->visible_world_surface_indices().size() == 1U);
    CHECK(state.visible_draw_list()->statistics().world_command_count == 1U);
    CHECK(state.visible_draw_list()->statistics().brush_command_count == 0U);

    const auto render_scene = client::build_render_scene(state);
    REQUIRE(render_scene.static_world);
    CHECK(render_scene.static_world->package == package->world_package());
    CHECK(render_scene.static_world->scene_package == package);
    CHECK(render_scene.static_world->visible_draw_list == state.visible_draw_list());
    REQUIRE(render_scene.static_world->visibility_summary);
    CHECK(render_scene.static_world->visibility_summary->revision == 1U);
    CHECK(render_scene.static_world->visibility_summary->scene_resource_id ==
        package->resource_id());
    CHECK(render_scene.static_world->visibility_summary
              ->scene_resource_revision == package->resource_revision());
    const auto result_signature = state.world_visibility()->result_signature();
    CHECK(render_scene.static_world->visibility_summary
              ->result_signature_first == result_signature.first);
    CHECK(render_scene.static_world->visibility_summary
              ->result_signature_second == result_signature.second);
    CHECK(render_scene.static_world->visibility_summary
              ->visible_world_surface_count == 1U);

    renderer::null::NullRenderer null_renderer;
    null_renderer.initialize();
    null_renderer.render(render_scene, {800, 600});
    const auto statistics = null_renderer.statistics();
    CHECK(statistics.scene_package_present);
    CHECK(statistics.scene_resource_id == package->resource_id());
    CHECK(statistics.scene_revision == package->resource_revision());
    CHECK(statistics.visibility_present);
    CHECK(statistics.visible_draw_list_present);
    CHECK(statistics.visibility_revision == 1U);
    CHECK(statistics.visible_world_surface_count == 1U);
    CHECK(statistics.visible_brush_instance_count == 0U);
    CHECK(statistics.visible_draw_command_count == 1U);

    const auto initial_scene_revision = state.scene_revision();
    const auto initial_world_revision = state.world_revision();
    REQUIRE(source.update(std::chrono::duration<double>{0.1}));
    CHECK(source.world_state().world_scene() == package);
    CHECK(source.world_state().scene_revision() == initial_scene_revision);
    CHECK(source.world_state().world_revision() == initial_world_revision);
    CHECK(source.world_state().visibility_revision() == 2U);
}

TEST_CASE("World preview supports all renderer-neutral visibility modes",
          "[world-preview][scene][visibility]")
{
    const auto package = make_shared_scene_package();
    const std::vector<visibility::WorldVisibilityMode> modes{
        visibility::WorldVisibilityMode::all,
        visibility::WorldVisibilityMode::frustum_only,
        visibility::WorldVisibilityMode::pvs_only,
        visibility::WorldVisibilityMode::pvs_and_frustum,
    };
    for (const auto mode : modes) {
        world_preview::WorldPreviewSceneOptions options;
        options.visibility_mode = mode;
        world_preview::WorldPreviewSceneSource source{package, options};
        REQUIRE(source.world_state().world_visibility());
        const auto& result = *source.world_state().world_visibility();
        CHECK(result.requested_mode() == mode);
        CHECK(result.applied_mode() == mode);
        CHECK(result.fallback_reason() ==
            visibility::WorldPvsFallbackReason::none);
        CHECK(result.visible_world_surface_indices().size() == 1U);
        if (mode == visibility::WorldVisibilityMode::pvs_only ||
            mode == visibility::WorldVisibilityMode::pvs_and_frustum) {
            CHECK(result.camera_leaf_index() == 1U);
        }
    }
}

TEST_CASE("World preview visibility follows the current drawable aspect",
          "[world-preview][scene][visibility][resize]")
{
    const auto package = make_resizable_scene_package();
    world_preview::WorldPreviewSceneOptions options;
    options.camera_mode = world_preview::WorldPreviewCameraMode::spawn;
    options.spawn_camera = world_preview::WorldPreviewSpawnCameraDescriptor{
        {8.0F, -20.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
    };
    options.visibility_mode =
        visibility::WorldVisibilityMode::frustum_only;
    options.visibility_extent = {100, 100};
    world_preview::WorldPreviewSceneSource source{package, options};

    REQUIRE(source.world_state().world_visibility());
    CHECK(source.world_state().world_visibility()
              ->visible_world_surface_indices()
              .size() == 1U);
    const auto square_revision = source.world_state().visibility_revision();

    REQUIRE(source.set_render_extent({400, 100}));
    REQUIRE(source.update(client::FrameTime{0.0}));
    CHECK(source.options().visibility_extent ==
        (renderer::RenderExtent{400, 100}));
    REQUIRE(source.world_state().world_visibility());
    CHECK(source.world_state().world_visibility()
              ->visible_world_surface_indices()
              .size() == 2U);
    CHECK(source.world_state().visibility_revision() == square_revision + 1U);

    REQUIRE(source.set_render_extent({0, 0}));
    CHECK(source.options().visibility_extent ==
        (renderer::RenderExtent{400, 100}));
    CHECK_FALSE(source.set_render_extent({-1, 100}));
    CHECK_FALSE(source.set_render_extent({-1, 0}));
    CHECK_FALSE(source.set_render_extent({0, -1}));
}

TEST_CASE("World preview applies the requested PVS fallback deterministically",
          "[world-preview][scene][visibility]")
{
    const auto package = make_shared_scene_package(false, true);
    world_preview::WorldPreviewSceneOptions options;
    options.visibility_mode = visibility::WorldVisibilityMode::pvs_and_frustum;
    options.pvs_fallback_policy =
        visibility::WorldPvsFallbackPolicy::frustum_only;
    world_preview::WorldPreviewSceneSource source{package, options};

    REQUIRE(source.world_state().world_visibility());
    const auto& result = *source.world_state().world_visibility();
    CHECK(result.requested_mode() ==
        visibility::WorldVisibilityMode::pvs_and_frustum);
    CHECK(result.applied_mode() ==
        visibility::WorldVisibilityMode::frustum_only);
    CHECK(result.fallback_reason() ==
        visibility::WorldPvsFallbackReason::camera_in_leaf_zero);
    CHECK(result.visible_world_surface_indices().size() == 1U);
    CHECK(source.fallback_warning_count() == 1U);
    REQUIRE(source.update(client::FrameTime{0.0}));
    REQUIRE(source.update(client::FrameTime{0.0}));
    CHECK(source.fallback_warning_count() == 1U);
}

TEST_CASE("World preview brush mode gates exact static instance commands",
          "[world-preview][scene][brush]")
{
    const auto package = make_shared_scene_package(true);

    world_preview::WorldPreviewSceneSource brushes_off{package};
    REQUIRE(brushes_off.world_state().world_visibility());
    REQUIRE(brushes_off.world_state().visible_draw_list());
    CHECK(brushes_off.world_state().world_visibility()
              ->visible_brush_instance_indices()
              .empty());
    CHECK(brushes_off.world_state().visible_draw_list()
              ->statistics()
              .brush_command_count == 0U);

    world_preview::WorldPreviewSceneOptions options;
    options.brush_submodels =
        world_preview::WorldPreviewBrushSubmodelsMode::static_instances;
    world_preview::WorldPreviewSceneSource brushes_on{package, options};
    REQUIRE(brushes_on.world_state().world_visibility());
    REQUIRE(brushes_on.world_state().visible_draw_list());
    const auto visible_instances = brushes_on.world_state().world_visibility()
        ->visible_brush_instance_indices();
    REQUIRE(visible_instances.size() == 1U);
    CHECK(visible_instances[0U] == 7U);
    const auto& draw_list = *brushes_on.world_state().visible_draw_list();
    CHECK(draw_list.statistics().world_command_count == 1U);
    CHECK(draw_list.statistics().brush_command_count == 1U);
    REQUIRE(draw_list.commands().size() == 2U);
    const auto& brush_command = draw_list.commands()[1U];
    CHECK(brush_command.object_kind ==
        visibility::WorldVisibleObjectKind::brush_instance_surface);
    CHECK(brush_command.source_model_index == 1U);
    CHECK(brush_command.source_instance_index == 7U);
    CHECK(brush_command.model_transform.values[12U] == 2.0F);

    const auto render_scene = client::build_render_scene(
        brushes_on.world_state());
    renderer::null::NullRenderer null_renderer;
    null_renderer.initialize();
    null_renderer.render(render_scene, {800, 600});
    const auto statistics = null_renderer.statistics();
    CHECK(statistics.scene_package_present);
    CHECK(statistics.visibility_present);
    CHECK(statistics.visible_draw_list_present);
    CHECK(statistics.visible_world_surface_count == 1U);
    CHECK(statistics.visible_brush_instance_count == 1U);
    CHECK(statistics.visible_draw_command_count == 2U);
}

TEST_CASE("World preview spawn camera is optional with a bounds fallback",
          "[world-preview][scene][camera]")
{
    const auto package = make_shared_scene_package();
    world_preview::WorldPreviewSceneOptions options;
    options.camera_mode = world_preview::WorldPreviewCameraMode::spawn;
    options.spawn_camera = world_preview::WorldPreviewSpawnCameraDescriptor{
        {4.0F, 4.0F, 4.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
    };
    world_preview::WorldPreviewSceneSource spawn_source{package, options};
    CHECK(spawn_source.spawn_camera_applied());
    const auto spawn_camera = spawn_source.world_state().camera();
    CHECK(spawn_camera.position.x == 4.0F);
    CHECK(spawn_camera.position.y == 4.0F);
    CHECK(spawn_camera.position.z == 4.0F);
    CHECK(spawn_camera.target.x == 4.0F);
    CHECK(spawn_camera.target.y == 5.0F);
    CHECK(spawn_camera.target.z == 4.0F);

    options.spawn_camera.reset();
    world_preview::WorldPreviewSceneSource fallback_source{package, options};
    CHECK_FALSE(fallback_source.spawn_camera_applied());
    const auto fallback_camera = fallback_source.world_state().camera();
    CHECK(fallback_camera.target.x == fallback_source.world_center().x);
    CHECK(fallback_camera.target.y == fallback_source.world_center().y);
    CHECK(fallback_camera.target.z == fallback_source.world_center().z);
}

TEST_CASE("World preview and RenderScene preserve shared package lifetime",
          "[world-preview][scene]")
{
    auto package = make_shared_package();
    std::weak_ptr<const world_render::WorldRenderPackage> weak = package;
    {
        world_preview::WorldPreviewSceneSource source{package};
        package.reset();
        REQUIRE_FALSE(weak.expired());
        {
            auto scene = client::build_render_scene(source.world_state());
            REQUIRE(scene.static_world);
            REQUIRE(scene.static_world->package);
            CHECK(scene.static_world->package->vertices().size() == 4U);
        }
        REQUIRE_FALSE(weak.expired());
    }
    CHECK(weak.expired());
}

TEST_CASE("World preview rejects invalid diagnostic camera prerequisites",
          "[world-preview][scene]")
{
    CHECK_THROWS_AS(
        (world_preview::WorldPreviewSceneSource{
            std::shared_ptr<const world_render::WorldRenderPackage>{}}),
        std::invalid_argument);

    auto package = make_shared_package();
    world_preview::WorldPreviewSceneOptions options;
    options.isometric_direction = {};
    CHECK_THROWS_AS(
        (world_preview::WorldPreviewSceneSource{package, options}),
        std::invalid_argument);

    options = {};
    options.orbit_angular_velocity_radians_per_second = 2.0F;
    CHECK_THROWS_AS(
        (world_preview::WorldPreviewSceneSource{package, options}),
        std::invalid_argument);

    options = {};
    options.camera_mode = static_cast<world_preview::WorldPreviewCameraMode>(0x7fU);
    CHECK_THROWS_AS(
        (world_preview::WorldPreviewSceneSource{package, options}),
        std::invalid_argument);

    options = {};
    options.cull_mode = static_cast<client::PreviewWorldCullMode>(0x7fU);
    CHECK_THROWS_AS(
        (world_preview::WorldPreviewSceneSource{package, options}),
        std::invalid_argument);

    options = {};
    options.visibility_mode = visibility::WorldVisibilityMode::pvs_only;
    CHECK_THROWS_AS(
        (world_preview::WorldPreviewSceneSource{package, options}),
        std::invalid_argument);

    options = {};
    options.brush_submodels =
        world_preview::WorldPreviewBrushSubmodelsMode::static_instances;
    CHECK_THROWS_AS(
        (world_preview::WorldPreviewSceneSource{package, options}),
        std::invalid_argument);

    const auto scene_package = make_shared_scene_package();
    options = {};
    options.visibility_mode =
        static_cast<visibility::WorldVisibilityMode>(0x7fU);
    CHECK_THROWS_AS(
        (world_preview::WorldPreviewSceneSource{scene_package, options}),
        std::invalid_argument);

    options = {};
    options.pvs_fallback_policy =
        static_cast<visibility::WorldPvsFallbackPolicy>(0x7fU);
    CHECK_THROWS_AS(
        (world_preview::WorldPreviewSceneSource{scene_package, options}),
        std::invalid_argument);

    options = {};
    options.brush_submodels =
        static_cast<world_preview::WorldPreviewBrushSubmodelsMode>(0x7fU);
    CHECK_THROWS_AS(
        (world_preview::WorldPreviewSceneSource{scene_package, options}),
        std::invalid_argument);

    options = {};
    options.visibility_extent = {};
    CHECK_THROWS_AS(
        (world_preview::WorldPreviewSceneSource{scene_package, options}),
        std::invalid_argument);

    options = {};
    options.camera_mode = world_preview::WorldPreviewCameraMode::spawn;
    options.spawn_camera = world_preview::WorldPreviewSpawnCameraDescriptor{
        {}, {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    CHECK_THROWS_AS(
        (world_preview::WorldPreviewSceneSource{scene_package, options}),
        std::invalid_argument);

    client::ClientWorldState state;
    CHECK_FALSE(state.set_preview_render_options(
        client::PreviewRenderOptions{
            static_cast<client::PreviewWorldCullMode>(0x7fU)}));
    CHECK(state.preview_render_options().cull_mode ==
        client::PreviewWorldCullMode::none);
}

TEST_CASE("Client static-world state can be cleared without retaining the package",
          "[world-preview][scene]")
{
    auto package = make_shared_package();
    std::weak_ptr<const world_render::WorldRenderPackage> weak = package;
    client::ClientWorldState state;
    state.set_static_world(package);
    package.reset();
    REQUIRE_FALSE(weak.expired());
    CHECK(state.world_revision() != 0U);
    state.clear_static_world();
    CHECK_FALSE(state.static_world());
    CHECK(state.world_revision() == 0U);
    CHECK(weak.expired());
}

TEST_CASE("Client world state clears scene and visibility revisions independently",
          "[world-preview][scene][visibility]")
{
    const auto package = make_shared_scene_package();
    world_preview::WorldPreviewSceneSource source{package};
    client::ClientWorldState state;
    state.set_world_scene(package);
    REQUIRE(state.scene_revision() != 0U);
    CHECK(state.visibility_revision() == 0U);

    REQUIRE(state.set_world_visibility(
        source.world_state().world_visibility(),
        source.world_state().visible_draw_list()));
    CHECK(state.visibility_revision() ==
        source.world_state().visibility_revision());

    const auto foreign_package = make_shared_scene_package(true);
    world_preview::WorldPreviewSceneSource foreign_source{foreign_package};
    const auto retained_visibility = state.world_visibility();
    const auto retained_draw_list = state.visible_draw_list();
    CHECK_FALSE(state.set_world_visibility(
        foreign_source.world_state().world_visibility(),
        foreign_source.world_state().visible_draw_list()));
    CHECK(state.world_visibility() == retained_visibility);
    CHECK(state.visible_draw_list() == retained_draw_list);

    const auto foreign_spatial_package =
        make_shared_scene_package(false, true);
    CHECK(foreign_spatial_package->resource_id() == package->resource_id());
    CHECK(foreign_spatial_package->resource_revision() !=
        package->resource_revision());
    world_preview::WorldPreviewSceneSource foreign_spatial_source{
        foreign_spatial_package};
    CHECK_FALSE(state.set_world_visibility(
        foreign_spatial_source.world_state().world_visibility(),
        foreign_spatial_source.world_state().visible_draw_list()));
    CHECK(state.world_visibility() == retained_visibility);
    CHECK(state.visible_draw_list() == retained_draw_list);
    state.clear_world_visibility();
    CHECK(state.world_scene() == package);
    CHECK(state.scene_revision() == package->resource_revision());
    CHECK(state.visibility_revision() == 0U);

    state.clear_static_world();
    CHECK_FALSE(state.static_world());
    CHECK_FALSE(state.world_scene());
    CHECK_FALSE(state.world_visibility());
    CHECK_FALSE(state.visible_draw_list());
    CHECK(state.world_revision() == 0U);
    CHECK(state.scene_revision() == 0U);
    CHECK(state.visibility_revision() == 0U);
}

TEST_CASE("Client world state rejects independently mixed visibility artifacts",
          "[world-preview][scene][visibility][pairing][regression]")
{
    const auto package = make_shared_scene_package();
    world_preview::WorldPreviewSceneSource all_source{package};
    world_preview::WorldPreviewSceneOptions frustum_options;
    frustum_options.visibility_mode =
        visibility::WorldVisibilityMode::frustum_only;
    world_preview::WorldPreviewSceneSource frustum_source{
        package, frustum_options};

    const auto& all_visibility =
        all_source.world_state().world_visibility();
    const auto& all_draw_list =
        all_source.world_state().visible_draw_list();
    const auto& frustum_visibility =
        frustum_source.world_state().world_visibility();
    const auto& frustum_draw_list =
        frustum_source.world_state().visible_draw_list();
    REQUIRE(all_visibility);
    REQUIRE(all_draw_list);
    REQUIRE(frustum_visibility);
    REQUIRE(frustum_draw_list);
    REQUIRE(all_visibility->revision() == frustum_visibility->revision());
    REQUIRE(all_visibility->scene_identity() ==
        frustum_visibility->scene_identity());
    CHECK(all_visibility->result_signature() ==
        all_draw_list->result_signature());
    CHECK(frustum_visibility->result_signature() ==
        frustum_draw_list->result_signature());
    REQUIRE(all_visibility->result_signature() !=
        frustum_visibility->result_signature());

    client::ClientWorldState state;
    state.set_world_scene(package);
    REQUIRE(state.set_world_visibility(all_visibility, all_draw_list));
    const auto retained_visibility = state.world_visibility();
    const auto retained_draw_list = state.visible_draw_list();

    CHECK_FALSE(state.set_world_visibility(
        all_visibility, frustum_draw_list));
    CHECK(state.world_visibility() == retained_visibility);
    CHECK(state.visible_draw_list() == retained_draw_list);
    CHECK_FALSE(state.set_world_visibility(
        frustum_visibility, all_draw_list));
    CHECK(state.world_visibility() == retained_visibility);
    CHECK(state.visible_draw_list() == retained_draw_list);
}

} // namespace
