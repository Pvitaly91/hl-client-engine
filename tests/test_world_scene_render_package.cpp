#include <hlclient/world_scene_render/world_scene_render_types.hpp>

#include "world_render_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace renderer = hlclient::renderer;
namespace scene_render = hlclient::world_scene_render;
namespace spatial = hlclient::world_spatial;
namespace fixture = hlclient::tests::world_render_fixture;

template <typename Type>
concept HasNativePathSurface =
    requires(const Type& value) { value.native_path(); } ||
    requires(const Type& value) { value.native_path; } ||
    requires(const Type& value) { value.filesystem_path(); } ||
    requires(const Type& value) { value.filesystem_path; };

template <typename Type>
concept HasOpenGlIdSurface =
    requires(const Type& value) { value.renderer_handle(); } ||
    requires(const Type& value) { value.vao; } ||
    requires(const Type& value) { value.vbo; } ||
    requires(const Type& value) { value.ebo; } ||
    requires(const Type& value) { value.gl_id; } ||
    requires(const Type& value) { value.opengl_texture; };

[[nodiscard]] spatial::WorldSpatialPackage make_spatial_package(
    const bool leaf_one_solid = false,
    const bool leaf_one_pvs_addressable = true)
{
    const assets::WorldBounds bounds{
        {0.0F, 0.0F, -1.0F}, {16.0F, 16.0F, 1.0F}};
    spatial::WorldSpatialPlane plane{{1.0F, 0.0F, 0.0F}, 0.0F, 0};
    spatial::WorldSpatialNode node;
    node.plane_index = 0U;
    node.children = {
        spatial::WorldSpatialNodeChild{
            spatial::WorldSpatialNodeChildKind::leaf, 1U},
        spatial::WorldSpatialNodeChild{
            spatial::WorldSpatialNodeChildKind::leaf, 1U},
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
    leaf_one.pvs_bit_addressable = leaf_one_pvs_addressable;
    leaf_one.solid_or_special = leaf_one_solid;
    leaf_one.surface_membership.source_leaf_index = 1U;
    leaf_one.surface_membership.source_marksurface_count = 1U;
    leaf_one.surface_membership.world_surface_indices = {0U};

    std::vector<std::vector<std::byte>> rows{{std::byte{0x01U}}};
    std::vector<std::optional<std::uint32_t>> leaf_rows{std::nullopt, 0U};
    return spatial::WorldSpatialPackage{
        {plane},
        {node},
        {leaf_zero, leaf_one},
        spatial::WorldPvsTable{1U, 1U, std::move(rows), std::move(leaf_rows), 0U},
        spatial::WorldSpatialModelMetadata{0U, 1U, bounds},
        spatial::WorldSpatialStatistics{1U, 1U, 2U, 1U, 1U, 1U, 1U},
        spatial::WorldSpatialCompatibilityProfile::
            goldsrc_bsp_v30_leaf_one_is_pvs_bit_zero,
        spatial::WorldSpatialEvidenceProfile::canonical_validated_bsp_records,
    };
}

[[nodiscard]] std::shared_ptr<const hlclient::world_render::WorldRenderPackage>
make_render_package(const std::size_t surface_count = 1U)
{
    fixture::FixtureOptions options;
    options.atlas_page_count = surface_count;
    auto built = fixture::make_package(options);
    REQUIRE(built);
    return std::make_shared<const hlclient::world_render::WorldRenderPackage>(
        std::move(*built.package));
}

[[nodiscard]] scene_render::BrushSubmodelRenderLibrary make_brush_library(
    const std::shared_ptr<const hlclient::world_render::WorldRenderPackage>& world)
{
    std::vector<scene_render::BrushSubmodelRenderModel> models;
    models.emplace_back(
        1U, world->bounds(), std::vector<std::uint32_t>{0U});
    return {world, std::move(models)};
}

[[nodiscard]] scene_render::BrushSubmodelRenderInstance
make_supported_instance(
    const std::shared_ptr<const hlclient::world_render::WorldRenderPackage>& world,
    const std::uint32_t source_instance_index = 7U,
    const std::uint32_t source_model_index = 1U,
    const float translation = 32.0F)
{
    scene_render::BrushSubmodelRenderInstance instance;
    instance.source_instance_index = source_instance_index;
    instance.source_entity_ordinal = 2U;
    instance.source_model_index = source_model_index;
    instance.model_transform.values[12U] = translation;
    instance.transformed_bounds = world->bounds();
    instance.transformed_bounds.minimum.x += translation;
    instance.transformed_bounds.maximum.x += translation;
    instance.touched_leaf_indices = {1U};
    instance.support_status =
        scene_render::BrushSubmodelRenderSupportStatus::supported_static_opaque;
    return instance;
}

[[nodiscard]] scene_render::WorldSceneRenderPackageBuildResult
build_two_by_two_scene(const scene_render::WorldSceneRenderPackageLimits& limits)
{
    const auto world = make_render_package(2U);
    std::vector<scene_render::BrushSubmodelRenderModel> models;
    models.emplace_back(1U, world->bounds(), std::vector<std::uint32_t>{0U});
    models.emplace_back(2U, world->bounds(), std::vector<std::uint32_t>{1U});
    scene_render::BrushSubmodelRenderLibrary library{world, std::move(models)};
    std::vector instances{
        make_supported_instance(world, 7U, 1U, 32.0F),
        make_supported_instance(world, 8U, 2U, 64.0F),
    };
    scene_render::WorldSceneRenderPackageBuilder builder;
    return builder.build(world,
        make_spatial_package(),
        std::move(library),
        std::move(instances),
        limits);
}

TEST_CASE("World scene package API exposes neither native paths nor OpenGL IDs",
    "[world-scene-render][package][boundary]")
{
    STATIC_REQUIRE_FALSE(
        HasNativePathSurface<scene_render::WorldSceneRenderPackage>);
    STATIC_REQUIRE_FALSE(
        HasNativePathSurface<scene_render::BrushSubmodelRenderLibrary>);
    STATIC_REQUIRE_FALSE(
        HasNativePathSurface<scene_render::BrushSubmodelRenderModel>);
    STATIC_REQUIRE_FALSE(
        HasNativePathSurface<scene_render::BrushSubmodelRenderInstance>);
    STATIC_REQUIRE_FALSE(
        HasOpenGlIdSurface<scene_render::WorldSceneRenderPackage>);
    STATIC_REQUIRE_FALSE(
        HasOpenGlIdSurface<scene_render::BrushSubmodelRenderLibrary>);
    STATIC_REQUIRE_FALSE(
        HasOpenGlIdSurface<scene_render::BrushSubmodelRenderModel>);
    STATIC_REQUIRE_FALSE(
        HasOpenGlIdSurface<scene_render::BrushSubmodelRenderInstance>);
}

TEST_CASE("World scene render package owns exact world and spatial adapters",
    "[world-scene-render][package]")
{
    const auto world = make_render_package();
    scene_render::WorldSceneRenderPackageBuilder builder;
    auto built = builder.build(world, make_spatial_package());
    REQUIRE(built);
    REQUIRE(built.package);

    const auto& package = *built.package;
    CHECK(package.world_package() == world);
    REQUIRE(package.world_surfaces().size() == 1U);
    CHECK(package.world_surfaces()[0U].source_surface_index == 0U);
    CHECK(package.world_surfaces()[0U].first_index == 0U);
    CHECK(package.world_surfaces()[0U].index_count == 6U);
    CHECK(package.statistics().world_surface_count == 1U);
    CHECK(package.statistics().brush_instance_count == 0U);
    CHECK(package.resource_id() != 0U);
    CHECK(package.resource_revision() != 0U);
    CHECK(package.compatibility_profile() == scene_render::
        WorldSceneRenderCompatibilityProfile::
            goldsrc_static_world_visibility_and_brush_instances_v1);
}

TEST_CASE("World scene package retains one library for reusable brush instances",
    "[world-scene-render][brush]")
{
    const auto world = make_render_package();

    scene_render::WorldSceneRenderPackageBuilder builder;
    auto built = builder.build(
        world,
        make_spatial_package(),
        make_brush_library(world),
        {make_supported_instance(world)});
    REQUIRE(built);
    REQUIRE(built.package);
    CHECK(built.package->brush_library().models().size() == 1U);
    CHECK(built.package->brush_instances().size() == 1U);
    CHECK(built.package->statistics().supported_brush_instance_count == 1U);
    CHECK(built.package->bounds().maximum.x == 48.0F);
}

TEST_CASE("World scene package owns inputs after their construction scope ends",
    "[world-scene-render][package][ownership]")
{
    std::weak_ptr<const hlclient::world_render::WorldRenderPackage> weak_world;
    scene_render::WorldSceneRenderPackageBuilder builder;
    auto built = [&]() {
        const auto world = make_render_package();
        weak_world = world;
        auto spatial_package = make_spatial_package();
        auto library = make_brush_library(world);
        std::vector instances{make_supported_instance(world)};
        return builder.build(world,
            std::move(spatial_package),
            std::move(library),
            std::move(instances));
    }();

    REQUIRE(built.package);
    REQUIRE_FALSE(weak_world.expired());
    {
        const auto retained_world = weak_world.lock();
        REQUIRE(retained_world);
        CHECK(built.package->world_package() == retained_world);
    }
    CHECK(built.package->spatial_package().leaves().size() == 2U);
    CHECK(built.package->brush_library().models().size() == 1U);
    REQUIRE(built.package->brush_instances().size() == 1U);
    CHECK(built.package->brush_instances()[0U].source_instance_index == 7U);
    built.package.reset();
    CHECK(weak_world.expired());
}

TEST_CASE("World scene package enforces every exact cardinality limit",
    "[world-scene-render][package][limits]")
{
    scene_render::WorldSceneRenderPackageLimits exact;
    exact.maximum_world_surfaces = 2U;
    exact.maximum_brush_models = 2U;
    exact.maximum_brush_surfaces = 2U;
    exact.maximum_brush_instances = 2U;
    exact.maximum_instance_leaf_links = 2U;
    const auto accepted = build_two_by_two_scene(exact);
    REQUIRE(accepted.package);
    CHECK_FALSE(accepted.error);
    CHECK(accepted.package->statistics().world_surface_count == 2U);
    CHECK(accepted.package->statistics().brush_model_count == 2U);
    CHECK(accepted.package->statistics().brush_surface_count == 2U);
    CHECK(accepted.package->statistics().brush_instance_count == 2U);

    SECTION("world surfaces at limit plus one")
    {
        exact.maximum_world_surfaces = 1U;
        const auto rejected = build_two_by_two_scene(exact);
        CHECK_FALSE(rejected.package);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
            scene_render::WorldSceneRenderErrorCode::invalid_world_package);
    }
    SECTION("brush models at limit plus one")
    {
        exact.maximum_brush_models = 1U;
        const auto rejected = build_two_by_two_scene(exact);
        CHECK_FALSE(rejected.package);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
            scene_render::WorldSceneRenderErrorCode::invalid_brush_library);
    }
    SECTION("brush surfaces at limit plus one")
    {
        exact.maximum_brush_surfaces = 1U;
        const auto rejected = build_two_by_two_scene(exact);
        CHECK_FALSE(rejected.package);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
            scene_render::WorldSceneRenderErrorCode::invalid_brush_library);
    }
    SECTION("brush instances at limit plus one")
    {
        exact.maximum_brush_instances = 1U;
        const auto rejected = build_two_by_two_scene(exact);
        CHECK_FALSE(rejected.package);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
            scene_render::WorldSceneRenderErrorCode::invalid_brush_instance);
    }
    SECTION("instance leaf links at limit plus one")
    {
        exact.maximum_instance_leaf_links = 1U;
        const auto rejected = build_two_by_two_scene(exact);
        CHECK_FALSE(rejected.package);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
            scene_render::WorldSceneRenderErrorCode::invalid_brush_instance);
    }
}

TEST_CASE("World scene failure never publishes a partial package",
    "[world-scene-render][package][transactional]")
{
    const auto world = make_render_package();
    auto first = make_supported_instance(world, 7U, 1U, 32.0F);
    auto second = make_supported_instance(world, 7U, 1U, 64.0F);
    scene_render::WorldSceneRenderPackageBuilder builder;
    const auto rejected = builder.build(world,
        make_spatial_package(),
        make_brush_library(world),
        {std::move(first), std::move(second)});

    REQUIRE_FALSE(rejected);
    CHECK_FALSE(rejected.package);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
        scene_render::WorldSceneRenderErrorCode::duplicate_brush_instance);
    CHECK_FALSE(rejected.error->context.empty());
}

TEST_CASE("World scene boundary rejects forged supported brush instances",
    "[world-scene-render][brush][validation]")
{
    const auto world = make_render_package();
    scene_render::WorldSceneRenderPackageBuilder builder;

    SECTION("supported instance requires a referenced render model")
    {
        auto instance = make_supported_instance(world);
        instance.source_model_index = 2U;
        const auto built = builder.build(world,
            make_spatial_package(),
            make_brush_library(world),
            {std::move(instance)});
        REQUIRE_FALSE(built.package);
        REQUIRE(built.error);
        CHECK(built.error->code ==
            scene_render::WorldSceneRenderErrorCode::missing_brush_model);
        CHECK(built.error->element_index == 0U);
    }

    SECTION("supported instance requires at least one touched leaf")
    {
        auto instance = make_supported_instance(world);
        instance.touched_leaf_indices.clear();
        const auto built = builder.build(world,
            make_spatial_package(),
            make_brush_library(world),
            {std::move(instance)});
        REQUIRE_FALSE(built.package);
        REQUIRE(built.error);
        CHECK(built.error->code ==
            scene_render::WorldSceneRenderErrorCode::invalid_brush_instance);
        CHECK(built.error->element_index == 0U);
    }

    SECTION("leaf zero cannot be forged as visible membership")
    {
        auto instance = make_supported_instance(world);
        instance.touched_leaf_indices = {0U};
        const auto built = builder.build(world,
            make_spatial_package(),
            make_brush_library(world),
            {std::move(instance)});
        REQUIRE_FALSE(built.package);
        REQUIRE(built.error);
        CHECK(built.error->code ==
            scene_render::WorldSceneRenderErrorCode::invalid_brush_instance);
        CHECK(built.error->element_index == 0U);
    }

    SECTION("out of range touched leaf cannot cross the package boundary")
    {
        auto instance = make_supported_instance(world);
        instance.touched_leaf_indices = {2U};
        const auto built = builder.build(world,
            make_spatial_package(),
            make_brush_library(world),
            {std::move(instance)});
        REQUIRE_FALSE(built.package);
        REQUIRE(built.error);
        CHECK(built.error->code ==
            scene_render::WorldSceneRenderErrorCode::invalid_brush_instance);
        CHECK(built.error->element_index == 0U);
    }

    SECTION("solid touched leaf cannot be forged as visible membership")
    {
        auto instance = make_supported_instance(world);
        const auto built = builder.build(world,
            make_spatial_package(true),
            make_brush_library(world),
            {std::move(instance)});
        REQUIRE_FALSE(built.package);
        REQUIRE(built.error);
        CHECK(built.error->code ==
            scene_render::WorldSceneRenderErrorCode::invalid_brush_instance);
        CHECK(built.error->element_index == 0U);
    }

    SECTION("non PVS touched leaf cannot be forged as visible membership")
    {
        auto instance = make_supported_instance(world);
        const auto built = builder.build(world,
            make_spatial_package(false, false),
            make_brush_library(world),
            {std::move(instance)});
        REQUIRE_FALSE(built.package);
        REQUIRE(built.error);
        CHECK(built.error->code ==
            scene_render::WorldSceneRenderErrorCode::invalid_brush_instance);
        CHECK(built.error->element_index == 0U);
    }

    SECTION("published bounds must exactly match all transformed model corners")
    {
        auto instance = make_supported_instance(world);
        instance.transformed_bounds.maximum.x += 1.0F;
        const auto built = builder.build(world,
            make_spatial_package(),
            make_brush_library(world),
            {std::move(instance)});
        REQUIRE_FALSE(built.package);
        REQUIRE(built.error);
        CHECK(built.error->code ==
            scene_render::WorldSceneRenderErrorCode::invalid_brush_instance);
        CHECK(built.error->element_index == 0U);
    }
}

TEST_CASE("World scene package rejects incomplete spatial and brush state",
    "[world-scene-render][validation]")
{
    const auto world = make_render_package();
    scene_render::WorldSceneRenderPackageBuilder builder;

    SECTION("spatial leaf references an unavailable surface")
    {
        auto spatial_package = make_spatial_package();
        // Build a deliberately independent invalid package to keep the public
        // scene builder responsible for cross-package reference validation.
        auto leaves = std::vector<spatial::WorldSpatialLeaf>{
            spatial_package.leaves().begin(), spatial_package.leaves().end()};
        leaves[1U].surface_membership.world_surface_indices = {1U};
        auto invalid_spatial = spatial::WorldSpatialPackage{
            {spatial_package.planes().begin(), spatial_package.planes().end()},
            {spatial_package.nodes().begin(), spatial_package.nodes().end()},
            std::move(leaves),
            spatial::WorldPvsTable{
                1U,
                1U,
                {{std::byte{0x01U}}},
                {std::nullopt, 0U},
                0U},
            spatial_package.world_model(),
            spatial_package.statistics(),
            spatial_package.compatibility_profile(),
            spatial_package.evidence_profile()};
        const auto built = builder.build(world, std::move(invalid_spatial));
        REQUIRE_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code ==
            scene_render::WorldSceneRenderErrorCode::invalid_spatial_package);
    }

    SECTION("library model table cannot omit aggregate surfaces")
    {
        scene_render::BrushSubmodelRenderLibrary library{world, {}};
        const auto built = builder.build(
            world, make_spatial_package(), std::move(library));
        REQUIRE_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code ==
            scene_render::WorldSceneRenderErrorCode::invalid_brush_library);
    }
}

} // namespace
