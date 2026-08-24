#include <hlclient/world_spatial/world_spatial_types.hpp>
#include <hlclient/world_visibility/world_visibility_resolver.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace {

namespace renderer = hlclient::renderer;
namespace spatial = hlclient::world_spatial;
namespace visibility = hlclient::world_visibility;

struct SpatialOptions {
    std::byte camera_row{std::byte{0x01}};
    bool camera_row_available{true};
    bool camera_in_leaf_zero{false};
    bool camera_leaf_solid{false};
    bool duplicate_surface_membership{false};
    bool candidate_leaf_solid{false};
};

[[nodiscard]] spatial::WorldSpatialPackage make_spatial_package(
    const SpatialOptions options = {})
{
    spatial::WorldSpatialPlane plane;
    plane.normal = {1.0F, 0.0F, 0.0F};

    spatial::WorldSpatialNode root;
    root.plane_index = 0U;
    root.children[0U] = {
        spatial::WorldSpatialNodeChildKind::leaf,
        options.camera_in_leaf_zero ? 0U : 1U,
    };
    root.children[1U] = {
        spatial::WorldSpatialNodeChildKind::leaf,
        2U,
    };
    root.bounds = {{-12.0F, -2.0F, -5.0F}, {12.0F, 2.0F, 1.0F}};

    spatial::WorldSpatialLeaf solid;
    solid.source_leaf_index = 0U;
    solid.contents = -2;
    solid.bounds = root.bounds;
    solid.surface_membership.source_leaf_index = 0U;
    solid.solid_or_special = true;

    spatial::WorldSpatialLeaf front;
    front.source_leaf_index = 1U;
    front.contents = -1;
    front.bounds = {{0.0F, -2.0F, -5.0F}, {12.0F, 2.0F, 1.0F}};
    front.pvs_row_index = options.camera_row_available
        ? std::optional<std::uint32_t>{0U}
        : std::nullopt;
    front.surface_membership.source_leaf_index = 1U;
    front.surface_membership.world_surface_indices = {0U, 1U};
    front.pvs_bit_addressable = true;
    front.solid_or_special = options.camera_leaf_solid;

    spatial::WorldSpatialLeaf back;
    back.source_leaf_index = 2U;
    back.contents = -1;
    back.bounds = {{-12.0F, -2.0F, -5.0F}, {0.0F, 2.0F, 1.0F}};
    back.pvs_row_index = 1U;
    back.surface_membership.source_leaf_index = 2U;
    back.surface_membership.world_surface_indices =
        options.duplicate_surface_membership
        ? std::vector<std::uint32_t>{0U, 2U, 3U}
        : std::vector<std::uint32_t>{2U, 3U};
    back.pvs_bit_addressable = true;
    back.solid_or_special = options.candidate_leaf_solid;

    std::vector<std::optional<std::uint32_t>> leaf_rows{
        std::nullopt,
        options.camera_row_available
            ? std::optional<std::uint32_t>{0U}
            : std::nullopt,
        1U,
    };
    spatial::WorldPvsTable pvs{
        1U,
        2U,
        {{options.camera_row}, {std::byte{0x02}}, {std::byte{0x03}}},
        std::move(leaf_rows),
        2U,
    };
    spatial::WorldSpatialModelMetadata model;
    model.root_node_index = 0U;
    model.visible_leaf_count = 2U;
    model.bounds = root.bounds;
    spatial::WorldSpatialStatistics statistics;
    statistics.plane_count = 1U;
    statistics.node_count = 1U;
    statistics.leaf_count = 3U;
    statistics.mapped_world_surface_link_count =
        options.duplicate_surface_membership ? 5U : 4U;
    statistics.unique_pvs_row_count = 3U;

    return spatial::WorldSpatialPackage{
        {plane},
        {root},
        {solid, front, back},
        std::move(pvs),
        model,
        statistics,
        spatial::WorldSpatialCompatibilityProfile::
            goldsrc_bsp_v30_leaf_one_is_pvs_bit_zero,
        spatial::WorldSpatialEvidenceProfile::canonical_validated_bsp_records,
    };
}

[[nodiscard]] std::vector<visibility::WorldVisibleSurfaceInput> make_surfaces()
{
    return {
        {0U, 0U, 3U, 0U,
            {{0.5F, -0.5F, -3.0F}, {1.5F, 0.5F, -2.0F}}},
        {1U, 3U, 3U, 0U,
            {{10.0F, -0.5F, -3.0F}, {11.0F, 0.5F, -2.0F}}},
        {2U, 6U, 3U, 0U,
            {{-1.0F, -0.5F, -3.0F}, {0.0F, 0.5F, -2.0F}}},
        {3U, 9U, 3U, 0U,
            {{-11.0F, -0.5F, -3.0F}, {-10.0F, 0.5F, -2.0F}}},
    };
}

struct BrushFixture {
    std::array<std::uint32_t, 1U> leaf1{1U};
    std::array<std::uint32_t, 1U> leaf2{2U};
    std::vector<visibility::WorldVisibilityBrushInstanceInput> instances;

    BrushFixture()
    {
        instances = {
            {7U, {{0.5F, -0.5F, -3.0F}, {1.5F, 0.5F, -2.0F}}, leaf1, true},
            {8U, {{-1.0F, -0.5F, -3.0F}, {0.0F, 0.5F, -2.0F}}, leaf2, true},
            {9U, {{0.5F, -0.5F, -3.0F}, {1.5F, 0.5F, -2.0F}}, leaf1, false},
            {10U, {{10.0F, -0.5F, -3.0F}, {11.0F, 0.5F, -2.0F}}, leaf1, true},
        };
    }
};

[[nodiscard]] renderer::RenderCamera camera()
{
    renderer::RenderCamera result;
    result.position = {1.0F, 0.0F, 0.0F};
    result.target = {1.0F, 0.0F, -1.0F};
    result.up = {0.0F, 1.0F, 0.0F};
    result.vertical_field_of_view_radians = 1.57079632679F;
    result.near_plane = 1.0F;
    result.far_plane = 10.0F;
    return result;
}

[[nodiscard]] visibility::WorldVisibilityResolveInput make_input(
    const spatial::WorldSpatialPackage& package,
    const std::span<const visibility::WorldVisibleSurfaceInput> surfaces,
    const std::span<const visibility::WorldVisibilityBrushInstanceInput> brushes,
    const visibility::WorldVisibilityMode mode)
{
    return {
        &package,
        surfaces,
        brushes,
        camera(),
        {100, 100},
        mode,
        visibility::WorldPvsFallbackPolicy::frustum_only,
        4U,
    };
}

TEST_CASE("World visibility modes select exact deterministic world and brush sets",
          "[world-visibility][resolver]")
{
    const auto package = make_spatial_package();
    const auto surfaces = make_surfaces();
    const BrushFixture brushes;
    const visibility::WorldVisibilityResolver resolver;

    const auto all = resolver.resolve(make_input(package, surfaces,
        brushes.instances, visibility::WorldVisibilityMode::all));
    REQUIRE(all.visibility);
    CHECK(std::ranges::equal(all.visibility->visible_world_surface_indices(),
        std::array{0U, 1U, 2U, 3U}));
    CHECK(std::ranges::equal(all.visibility->visible_brush_instance_indices(),
        std::array{7U, 8U, 10U}));
    CHECK(all.visibility->statistics().total_brush_instance_count == 4U);
    CHECK(all.visibility->statistics().supported_brush_instance_count == 3U);
    CHECK(all.visibility->statistics().brush_instance_culled_by_pvs_count == 0U);
    CHECK(all.visibility->statistics().brush_instance_culled_by_frustum_count == 0U);

    const auto frustum = resolver.resolve(make_input(package, surfaces,
        brushes.instances, visibility::WorldVisibilityMode::frustum_only));
    REQUIRE(frustum.visibility);
    CHECK(std::ranges::equal(frustum.visibility->visible_world_surface_indices(),
        std::array{0U, 2U}));
    CHECK(std::ranges::equal(frustum.visibility->visible_brush_instance_indices(),
        std::array{7U, 8U}));
    CHECK(frustum.visibility->statistics().supported_brush_instance_count == 3U);
    CHECK(frustum.visibility->statistics().pvs_visible_brush_instance_count == 3U);
    CHECK(frustum.visibility->statistics()
            .brush_instance_culled_by_pvs_count == 0U);
    CHECK(frustum.visibility->statistics()
            .brush_instance_culled_by_frustum_count == 1U);

    const auto pvs = resolver.resolve(make_input(package, surfaces,
        brushes.instances, visibility::WorldVisibilityMode::pvs_only));
    REQUIRE(pvs.visibility);
    CHECK(pvs.visibility->camera_leaf_index() == 1U);
    CHECK(std::ranges::equal(pvs.visibility->visible_leaf_indices(),
        std::array{1U}));
    CHECK(std::ranges::equal(pvs.visibility->visible_world_surface_indices(),
        std::array{0U, 1U}));
    CHECK(std::ranges::equal(pvs.visibility->visible_brush_instance_indices(),
        std::array{7U, 10U}));
    CHECK(pvs.visibility->statistics().supported_brush_instance_count == 3U);
    CHECK(pvs.visibility->statistics().pvs_visible_brush_instance_count == 2U);
    CHECK(pvs.visibility->statistics().brush_instance_culled_by_pvs_count == 1U);
    CHECK(pvs.visibility->statistics()
            .brush_instance_culled_by_frustum_count == 0U);

    const auto combined = resolver.resolve(make_input(package, surfaces,
        brushes.instances, visibility::WorldVisibilityMode::pvs_and_frustum));
    REQUIRE(combined.visibility);
    CHECK(std::ranges::equal(combined.visibility->visible_world_surface_indices(),
        std::array{0U}));
    CHECK(std::ranges::equal(combined.visibility->visible_brush_instance_indices(),
        std::array{7U}));
    CHECK(combined.visibility->statistics().world_surface_culled_by_pvs_count == 2U);
    CHECK(combined.visibility->statistics().world_surface_culled_by_frustum_count == 1U);
    CHECK(combined.visibility->statistics().supported_brush_instance_count == 3U);
    CHECK(combined.visibility->statistics().brush_instance_culled_by_pvs_count == 1U);
    CHECK(combined.visibility->statistics()
            .brush_instance_culled_by_frustum_count == 1U);
}

TEST_CASE("PVS camera leaf is included explicitly even when its row clears self",
          "[world-visibility][resolver]")
{
    const auto package = make_spatial_package(
        SpatialOptions{std::byte{0x00}, true, false, false, false});
    const auto surfaces = make_surfaces();
    const visibility::WorldVisibilityResolver resolver;
    const auto resolved = resolver.resolve(make_input(package, surfaces, {},
        visibility::WorldVisibilityMode::pvs_only));
    REQUIRE(resolved.visibility);
    CHECK(std::ranges::equal(resolved.visibility->visible_leaf_indices(),
        std::array{1U}));
    CHECK(std::ranges::equal(resolved.visibility->visible_world_surface_indices(),
        std::array{0U, 1U}));
}

TEST_CASE("PVS candidate selection excludes solid or special leaves",
          "[world-visibility][resolver][pvs][solid]")
{
    const auto package = make_spatial_package(
        SpatialOptions{std::byte{0x03}, true, false, false, false, true});
    const auto surfaces = make_surfaces();
    const BrushFixture brushes;
    const visibility::WorldVisibilityResolver resolver;
    const auto resolved = resolver.resolve(make_input(
        package,
        surfaces,
        brushes.instances,
        visibility::WorldVisibilityMode::pvs_only));

    REQUIRE(resolved.visibility);
    CHECK(std::ranges::equal(
        resolved.visibility->visible_leaf_indices(), std::array{1U}));
    CHECK(std::ranges::equal(
        resolved.visibility->visible_world_surface_indices(),
        std::array{0U, 1U}));
    CHECK(std::ranges::equal(
        resolved.visibility->visible_brush_instance_indices(),
        std::array{7U, 10U}));
}

TEST_CASE("PVS fallback policies are explicit for a solid leaf",
          "[world-visibility][resolver]")
{
    const auto package = make_spatial_package(
        SpatialOptions{std::byte{0x01}, true, true, false, false});
    const auto surfaces = make_surfaces();
    const visibility::WorldVisibilityResolver resolver;
    const BrushFixture brushes;
    auto input = make_input(package, surfaces, brushes.instances,
        visibility::WorldVisibilityMode::pvs_and_frustum);

    input.pvs_fallback_policy = visibility::WorldPvsFallbackPolicy::frustum_only;
    const auto frustum_fallback = resolver.resolve(input);
    REQUIRE(frustum_fallback.visibility);
    CHECK(frustum_fallback.visibility->applied_mode() ==
        visibility::WorldVisibilityMode::frustum_only);
    CHECK(frustum_fallback.visibility->fallback_reason() ==
        visibility::WorldPvsFallbackReason::camera_in_leaf_zero);
    CHECK(std::ranges::equal(
        frustum_fallback.visibility->visible_world_surface_indices(),
        std::array{0U, 2U}));
    CHECK(frustum_fallback.visibility->statistics()
            .supported_brush_instance_count == 3U);
    CHECK(frustum_fallback.visibility->statistics()
            .pvs_visible_brush_instance_count == 3U);
    CHECK(frustum_fallback.visibility->statistics()
            .frustum_visible_brush_instance_count == 2U);
    CHECK(frustum_fallback.visibility->statistics()
            .visible_brush_instance_count == 2U);
    CHECK(frustum_fallback.visibility->statistics()
            .brush_instance_culled_by_pvs_count == 0U);
    CHECK(frustum_fallback.visibility->statistics()
            .brush_instance_culled_by_frustum_count == 1U);

    input.pvs_fallback_policy = visibility::WorldPvsFallbackPolicy::all_surfaces;
    const auto all_fallback = resolver.resolve(input);
    REQUIRE(all_fallback.visibility);
    CHECK(all_fallback.visibility->applied_mode() ==
        visibility::WorldVisibilityMode::all);
    CHECK(all_fallback.visibility->visible_world_surface_indices().size() == 4U);
    CHECK(all_fallback.visibility->statistics().supported_brush_instance_count == 3U);
    CHECK(all_fallback.visibility->statistics()
            .pvs_visible_brush_instance_count == 3U);
    CHECK(all_fallback.visibility->statistics()
            .frustum_visible_brush_instance_count == 3U);
    CHECK(all_fallback.visibility->statistics().visible_brush_instance_count == 3U);
    CHECK(all_fallback.visibility->statistics()
            .brush_instance_culled_by_pvs_count == 0U);
    CHECK(all_fallback.visibility->statistics()
            .brush_instance_culled_by_frustum_count == 0U);

    input.pvs_fallback_policy = visibility::WorldPvsFallbackPolicy::fail_closed;
    const auto closed_fallback = resolver.resolve(input);
    REQUIRE(closed_fallback.visibility);
    CHECK(closed_fallback.visibility->applied_mode() == input.mode);
    CHECK(closed_fallback.visibility->visible_world_surface_indices().empty());
    CHECK(closed_fallback.visibility->statistics()
            .world_surface_culled_by_pvs_count == 4U);
    CHECK(closed_fallback.visibility->statistics()
            .supported_brush_instance_count == 3U);
    CHECK(closed_fallback.visibility->statistics()
            .pvs_visible_brush_instance_count == 0U);
    CHECK(closed_fallback.visibility->statistics()
            .frustum_visible_brush_instance_count == 0U);
    CHECK(closed_fallback.visibility->statistics()
            .visible_brush_instance_count == 0U);
    CHECK(closed_fallback.visibility->statistics()
            .brush_instance_culled_by_pvs_count == 3U);
    CHECK(closed_fallback.visibility->statistics()
            .brush_instance_culled_by_frustum_count == 0U);
}

TEST_CASE("Missing PVS row uses the configured non-fatal fallback",
          "[world-visibility][resolver]")
{
    const auto package = make_spatial_package(
        SpatialOptions{std::byte{0x01}, false, false, false, false});
    const auto surfaces = make_surfaces();
    const visibility::WorldVisibilityResolver resolver;
    const auto resolved = resolver.resolve(make_input(package, surfaces, {},
        visibility::WorldVisibilityMode::pvs_and_frustum));
    REQUIRE(resolved.visibility);
    CHECK(resolved.visibility->fallback_reason() ==
        visibility::WorldPvsFallbackReason::pvs_row_unavailable);
    CHECK(resolved.visibility->applied_mode() ==
        visibility::WorldVisibilityMode::frustum_only);
}

TEST_CASE("A nonzero solid camera leaf has a distinct typed fallback reason",
          "[world-visibility][resolver]")
{
    const auto package = make_spatial_package(
        SpatialOptions{std::byte{0x01}, true, false, true, false});
    const auto surfaces = make_surfaces();
    const visibility::WorldVisibilityResolver resolver;
    const auto resolved = resolver.resolve(make_input(package, surfaces, {},
        visibility::WorldVisibilityMode::pvs_only));
    REQUIRE(resolved.visibility);
    CHECK(resolved.visibility->fallback_reason() ==
        visibility::WorldPvsFallbackReason::camera_in_solid_leaf);
    CHECK(resolved.visibility->applied_mode() ==
        visibility::WorldVisibilityMode::frustum_only);
}

TEST_CASE("Visibility deduplicates cross-leaf surfaces and preserves source order",
          "[world-visibility][resolver]")
{
    const auto package = make_spatial_package(
        SpatialOptions{std::byte{0x03}, true, false, false, true});
    auto surfaces = make_surfaces();
    std::ranges::reverse(surfaces);
    const auto original_first = surfaces.front().source_surface_index;
    const auto original_links =
        package.statistics().mapped_world_surface_link_count;
    const visibility::WorldVisibilityResolver resolver;
    const auto resolved = resolver.resolve(make_input(package, surfaces, {},
        visibility::WorldVisibilityMode::pvs_only));
    REQUIRE(resolved.visibility);
    CHECK(std::ranges::equal(resolved.visibility->visible_world_surface_indices(),
        std::array{0U, 1U, 2U, 3U}));
    CHECK(surfaces.front().source_surface_index == original_first);
    CHECK(package.statistics().mapped_world_surface_link_count == original_links);
}

TEST_CASE("Visibility resolver rejects invalid inputs and enforces exact limits",
          "[world-visibility][resolver]")
{
    const auto package = make_spatial_package();
    const auto surfaces = make_surfaces();
    const visibility::WorldVisibilityResolver resolver;
    auto input = make_input(package, surfaces, {},
        visibility::WorldVisibilityMode::all);

    visibility::WorldVisibilityLimits exact;
    exact.maximum_visible_world_surfaces = 4U;
    exact.maximum_surface_dedup_bytes = 4U;
    REQUIRE(resolver.resolve(input, exact));

    auto over = exact;
    over.maximum_visible_world_surfaces = 3U;
    const auto surface_limit_rejected = resolver.resolve(input, over);
    REQUIRE_FALSE(surface_limit_rejected);
    REQUIRE(surface_limit_rejected.error);
    CHECK(surface_limit_rejected.error->code ==
        visibility::WorldVisibilityErrorCode::visible_world_surface_limit_exceeded);

    over = exact;
    over.maximum_surface_dedup_bytes = 3U;
    const auto dedup_limit_rejected = resolver.resolve(input, over);
    REQUIRE_FALSE(dedup_limit_rejected);
    REQUIRE(dedup_limit_rejected.error);
    CHECK(dedup_limit_rejected.error->code ==
        visibility::WorldVisibilityErrorCode::surface_dedup_limit_exceeded);

    over = exact;
    over.maximum_draw_commands = 4U;
    REQUIRE(resolver.resolve(input, over));
    over.maximum_draw_commands = 3U;
    const auto command_limit_rejected = resolver.resolve(input, over);
    REQUIRE_FALSE(command_limit_rejected);
    REQUIRE(command_limit_rejected.error);
    CHECK(command_limit_rejected.error->code ==
        visibility::WorldVisibilityErrorCode::draw_command_limit_exceeded);

    const auto all_visible_package = make_spatial_package(
        SpatialOptions{std::byte{0x03}, true, false, false, false});
    input = make_input(all_visible_package, surfaces, {},
        visibility::WorldVisibilityMode::pvs_only);
    over = exact;
    over.maximum_visible_leaves = 1U;
    const auto leaf_limit_rejected = resolver.resolve(input, over);
    REQUIRE_FALSE(leaf_limit_rejected);
    REQUIRE(leaf_limit_rejected.error);
    CHECK(leaf_limit_rejected.error->code ==
        visibility::WorldVisibilityErrorCode::visible_leaf_limit_exceeded);

    input.camera.target = input.camera.position;
    const auto camera_rejected = resolver.resolve(input);
    REQUIRE_FALSE(camera_rejected);
    REQUIRE(camera_rejected.error);
    CHECK(camera_rejected.error->code ==
        visibility::WorldVisibilityErrorCode::invalid_camera);
}

} // namespace
