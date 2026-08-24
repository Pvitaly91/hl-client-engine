#include <hlclient/goldsrc/spatial/goldsrc_spatial_package_builder.hpp>
#include <hlclient/world_spatial/world_spatial_query.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace goldsrc_spatial = hlclient::goldsrc::spatial;
namespace world_spatial = hlclient::world_spatial;

[[nodiscard]] assets::WorldBounds bounds(
    const assets::AssetVector3 minimum,
    const assets::AssetVector3 maximum) noexcept
{
    return assets::WorldBounds{minimum, maximum};
}

[[nodiscard]] assets::WorldBounds cube(
    const float minimum,
    const float maximum) noexcept
{
    return bounds(
        assets::AssetVector3{minimum, minimum, minimum},
        assets::AssetVector3{maximum, maximum, maximum});
}

[[nodiscard]] world_spatial::WorldSpatialPackage basic_package()
{
    const std::array planes{
        goldsrc_spatial::GoldSrcSpatialSourcePlane{
            assets::AssetVector3{1.0F, 0.0F, 0.0F},
            0.0,
            0},
    };
    const std::array nodes{
        goldsrc_spatial::GoldSrcSpatialSourceNode{
            0,
            {-2, -3},
            cube(-16.0F, 16.0F),
            std::nullopt,
            std::nullopt},
    };
    const std::array leaves{
        goldsrc_spatial::GoldSrcSpatialSourceLeaf{
            -2, -1, cube(-16.0F, 16.0F), 0U, 0U},
        goldsrc_spatial::GoldSrcSpatialSourceLeaf{
            -1, -1, cube(0.0F, 16.0F), 0U, 0U},
        goldsrc_spatial::GoldSrcSpatialSourceLeaf{
            -1, -1, cube(-16.0F, 0.0F), 0U, 0U},
    };
    const auto result = goldsrc_spatial::GoldSrcSpatialPackageBuilder::build(
        goldsrc_spatial::GoldSrcSpatialBuildInput{
            planes,
            nodes,
            leaves,
            {},
            {},
            goldsrc_spatial::GoldSrcSpatialSourceModel{
                cube(-16.0F, 16.0F), 0, 2},
            0U,
            {},
        });
    REQUIRE(result);
    return std::move(*result.package);
}

[[nodiscard]] world_spatial::WorldSpatialPackage malformed_cycle_package()
{
    std::vector<world_spatial::WorldSpatialPlane> planes{
        {assets::AssetVector3{1.0F, 0.0F, 0.0F}, 0.0F, 0},
    };
    std::vector<world_spatial::WorldSpatialNode> nodes{
        {0U,
         {world_spatial::WorldSpatialNodeChild{
              world_spatial::WorldSpatialNodeChildKind::node,
              1U},
          world_spatial::WorldSpatialNodeChild{
              world_spatial::WorldSpatialNodeChildKind::leaf,
              1U}},
         cube(-16.0F, 16.0F),
         std::nullopt,
         std::nullopt},
        {0U,
         {world_spatial::WorldSpatialNodeChild{
              world_spatial::WorldSpatialNodeChildKind::node,
              0U},
          world_spatial::WorldSpatialNodeChild{
              world_spatial::WorldSpatialNodeChildKind::leaf,
              2U}},
         cube(-16.0F, 16.0F),
         std::nullopt,
         std::nullopt},
    };
    std::vector<world_spatial::WorldSpatialLeaf> leaves(3U);
    for (std::uint32_t index = 0U; index < leaves.size(); ++index) {
        leaves[index].source_leaf_index = index;
        leaves[index].contents = index == 0U ? -2 : -1;
        leaves[index].bounds = cube(-16.0F, 16.0F);
        leaves[index].solid_or_special = index == 0U;
    }
    std::vector<std::vector<std::byte>> rows{{std::byte{0x03U}}};
    std::vector<std::optional<std::uint32_t>> row_indices{
        std::nullopt, 0U, 0U};
    return world_spatial::WorldSpatialPackage{
        std::move(planes),
        std::move(nodes),
        std::move(leaves),
        world_spatial::WorldPvsTable{
            1U, 2U, std::move(rows), std::move(row_indices), 0U},
        world_spatial::WorldSpatialModelMetadata{0U, 2U, cube(-16.0F, 16.0F)},
        {},
        world_spatial::WorldSpatialCompatibilityProfile::
            goldsrc_bsp_v30_leaf_one_is_pvs_bit_zero,
        world_spatial::WorldSpatialEvidenceProfile::canonical_validated_bsp_records,
    };
}

[[nodiscard]] world_spatial::WorldSpatialPackage malformed_child_kind_package()
{
    std::vector<world_spatial::WorldSpatialPlane> planes{
        {assets::AssetVector3{1.0F, 0.0F, 0.0F}, 0.0F, 0},
    };
    std::vector<world_spatial::WorldSpatialNode> nodes{
        {0U,
         {world_spatial::WorldSpatialNodeChild{
              static_cast<world_spatial::WorldSpatialNodeChildKind>(0xFFU),
              1U},
          world_spatial::WorldSpatialNodeChild{
              world_spatial::WorldSpatialNodeChildKind::leaf,
              2U}},
         cube(-16.0F, 16.0F),
         std::nullopt,
         std::nullopt},
    };
    std::vector<world_spatial::WorldSpatialLeaf> leaves(3U);
    for (std::uint32_t index = 0U; index < leaves.size(); ++index) {
        leaves[index].source_leaf_index = index;
        leaves[index].contents = index == 0U ? -2 : -1;
        leaves[index].bounds = cube(-16.0F, 16.0F);
        leaves[index].solid_or_special = index == 0U;
    }
    std::vector<std::vector<std::byte>> rows{{std::byte{0x03U}}};
    std::vector<std::optional<std::uint32_t>> row_indices{
        std::nullopt, 0U, 0U};
    return world_spatial::WorldSpatialPackage{
        std::move(planes),
        std::move(nodes),
        std::move(leaves),
        world_spatial::WorldPvsTable{
            1U, 2U, std::move(rows), std::move(row_indices), 0U},
        world_spatial::WorldSpatialModelMetadata{0U, 2U, cube(-16.0F, 16.0F)},
        {},
        world_spatial::WorldSpatialCompatibilityProfile::
            goldsrc_bsp_v30_leaf_one_is_pvs_bit_zero,
        world_spatial::WorldSpatialEvidenceProfile::canonical_validated_bsp_records,
    };
}

TEST_CASE("Point-to-leaf follows front, back, and exact plane tie policy",
    "[world-spatial][query][point]")
{
    const auto package = basic_package();

    const auto front = world_spatial::WorldSpatialQuery::locate_point(
        package,
        assets::AssetVector3{1.0F, 0.0F, 0.0F});
    REQUIRE(front);
    CHECK(front.result->leaf_index == 1U);
    CHECK(front.result->pvs_available);

    const auto back = world_spatial::WorldSpatialQuery::locate_point(
        package,
        assets::AssetVector3{-1.0F, 0.0F, 0.0F});
    REQUIRE(back);
    CHECK(back.result->leaf_index == 2U);

    const auto tie = world_spatial::WorldSpatialQuery::locate_point(
        package,
        assets::AssetVector3{0.0F, 0.0F, 0.0F});
    REQUIRE(tie);
    CHECK(tie.result->leaf_index == 1U);
    CHECK(tie.result->traversal_depth == 1U);
}

TEST_CASE("Point-to-leaf rejects invalid input, cycles, and bounded-depth overflow",
    "[world-spatial][query][point][errors]")
{
    const auto package = basic_package();
    const auto invalid = world_spatial::WorldSpatialQuery::locate_point(
        package,
        assets::AssetVector3{
            std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
    REQUIRE_FALSE(invalid);
    REQUIRE(invalid.error);
    CHECK(invalid.error->code ==
        world_spatial::WorldSpatialQueryErrorCode::invalid_position);

    const auto cyclic = malformed_cycle_package();
    const auto cycle = world_spatial::WorldSpatialQuery::locate_point(
        cyclic,
        assets::AssetVector3{1.0F, 0.0F, 0.0F});
    REQUIRE_FALSE(cycle);
    REQUIRE(cycle.error);
    CHECK(cycle.error->code ==
        world_spatial::WorldSpatialQueryErrorCode::cycle_detected);

    world_spatial::WorldSpatialQueryLimits limits;
    limits.maximum_query_steps = 1U;
    const auto bounded = world_spatial::WorldSpatialQuery::locate_point(
        cyclic,
        assets::AssetVector3{1.0F, 0.0F, 0.0F},
        limits);
    REQUIRE_FALSE(bounded);
    REQUIRE(bounded.error);
    CHECK(bounded.error->code == world_spatial::
        WorldSpatialQueryErrorCode::query_step_limit_exceeded);

    const auto invalid_kind = world_spatial::WorldSpatialQuery::locate_point(
        malformed_child_kind_package(),
        assets::AssetVector3{1.0F, 0.0F, 0.0F});
    REQUIRE_FALSE(invalid_kind);
    REQUIRE(invalid_kind.error);
    CHECK(invalid_kind.error->code == world_spatial::
        WorldSpatialQueryErrorCode::invalid_child_reference);
}

TEST_CASE("Box-to-leaves handles front, back, and straddling bounds",
    "[world-spatial][query][box]")
{
    const auto package = basic_package();

    const auto front = world_spatial::WorldSpatialQuery::collect_intersecting_leaves(
        package,
        bounds(
            assets::AssetVector3{2.0F, -1.0F, -1.0F},
            assets::AssetVector3{4.0F, 1.0F, 1.0F}));
    REQUIRE(front);
    CHECK(front.result->leaf_indices == std::vector<std::uint32_t>{1U});

    const auto back = world_spatial::WorldSpatialQuery::collect_intersecting_leaves(
        package,
        bounds(
            assets::AssetVector3{-4.0F, -1.0F, -1.0F},
            assets::AssetVector3{-2.0F, 1.0F, 1.0F}));
    REQUIRE(back);
    CHECK(back.result->leaf_indices == std::vector<std::uint32_t>{2U});

    const auto straddling =
        world_spatial::WorldSpatialQuery::collect_intersecting_leaves(
            package,
            cube(-1.0F, 1.0F));
    REQUIRE(straddling);
    CHECK(straddling.result->leaf_indices ==
        std::vector<std::uint32_t>{1U, 2U});

    // Representative transformed brush bounds: a local [-1,1] cube shifted
    // to x=8 touches only the front leaf.
    const auto transformed =
        world_spatial::WorldSpatialQuery::collect_intersecting_leaves(
            package,
            bounds(
                assets::AssetVector3{7.0F, -1.0F, -1.0F},
                assets::AssetVector3{9.0F, 1.0F, 1.0F}));
    REQUIRE(transformed);
    CHECK(transformed.result->leaf_indices ==
        std::vector<std::uint32_t>{1U});
}

TEST_CASE("Box-to-leaves is cycle-safe, deduplicated, and bounded",
    "[world-spatial][query][box][errors]")
{
    const auto cyclic = malformed_cycle_package();
    const auto cycle = world_spatial::WorldSpatialQuery::collect_intersecting_leaves(
        cyclic,
        cube(1.0F, 2.0F));
    REQUIRE_FALSE(cycle);
    REQUIRE(cycle.error);
    CHECK(cycle.error->code ==
        world_spatial::WorldSpatialQueryErrorCode::cycle_detected);

    const auto package = basic_package();
    world_spatial::WorldSpatialQueryLimits limits;
    limits.maximum_box_query_leaves = 1U;
    const auto limited = world_spatial::WorldSpatialQuery::collect_intersecting_leaves(
        package,
        cube(-1.0F, 1.0F),
        limits);
    REQUIRE_FALSE(limited);
    REQUIRE(limited.error);
    CHECK(limited.error->code ==
        world_spatial::WorldSpatialQueryErrorCode::leaf_limit_exceeded);

    const auto invalid = world_spatial::WorldSpatialQuery::collect_intersecting_leaves(
        package,
        bounds(
            assets::AssetVector3{1.0F, 0.0F, 0.0F},
            assets::AssetVector3{-1.0F, 0.0F, 0.0F}));
    REQUIRE_FALSE(invalid);
    REQUIRE(invalid.error);
    CHECK(invalid.error->code ==
        world_spatial::WorldSpatialQueryErrorCode::invalid_bounds);

    const auto invalid_kind =
        world_spatial::WorldSpatialQuery::collect_intersecting_leaves(
            malformed_child_kind_package(),
            cube(1.0F, 2.0F));
    REQUIRE_FALSE(invalid_kind);
    REQUIRE(invalid_kind.error);
    CHECK(invalid_kind.error->code == world_spatial::
        WorldSpatialQueryErrorCode::invalid_child_reference);
}

} // namespace
