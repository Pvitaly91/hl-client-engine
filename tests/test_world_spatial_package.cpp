#include <hlclient/goldsrc/spatial/goldsrc_spatial_package_builder.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>

#include "synthetic_goldsrc_bsp_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace goldsrc_spatial = hlclient::goldsrc::spatial;

[[nodiscard]] assets::WorldBounds bounds(
    const float minimum,
    const float maximum) noexcept
{
    return assets::WorldBounds{
        assets::AssetVector3{minimum, minimum, minimum},
        assets::AssetVector3{maximum, maximum, maximum},
    };
}

[[nodiscard]] assets::WorldSurface surface(const std::uint32_t source_ordinal)
{
    assets::WorldSurface value;
    value.source_surface_ordinal = source_ordinal;
    value.bounds = bounds(-1.0F, 1.0F);
    return value;
}

struct SpatialFixture {
    std::vector<goldsrc_spatial::GoldSrcSpatialSourcePlane> planes{
        {assets::AssetVector3{2.0F, 0.0F, 0.0F}, 0.0, 0},
    };
    std::vector<goldsrc_spatial::GoldSrcSpatialSourceNode> nodes{
        {0, {-2, -3}, bounds(-16.0F, 16.0F), 0U, 2U},
    };
    std::vector<goldsrc_spatial::GoldSrcSpatialSourceLeaf> leaves{
        {-2, -1, bounds(-16.0F, 16.0F), 0U, 0U},
        {-1, 0, bounds(0.0F, 16.0F), 0U, 3U},
        {-1, 1, bounds(-16.0F, 0.0F), 3U, 1U},
    };
    std::vector<std::uint32_t> marksurfaces{0U, 0U, 2U, 0U};
    std::vector<std::byte> visibility{std::byte{0x01U}, std::byte{0x02U}};
    std::vector<assets::WorldSurface> world_surfaces{surface(0U), surface(1U)};
    std::vector<std::uint32_t> submodel_face_ordinals{2U};
    goldsrc_spatial::GoldSrcSpatialSourceModel model{
        bounds(-16.0F, 16.0F),
        0,
        2,
    };
    std::uint32_t source_face_count{3U};

    [[nodiscard]] goldsrc_spatial::GoldSrcSpatialBuildInput input() const noexcept
    {
        return goldsrc_spatial::GoldSrcSpatialBuildInput{
            planes,
            nodes,
            leaves,
            marksurfaces,
            visibility,
            model,
            source_face_count,
            world_surfaces,
            submodel_face_ordinals,
        };
    }
};

TEST_CASE("World spatial package owns normalized canonical BSP spatial state",
    "[world-spatial][package]")
{
    SpatialFixture fixture;
    auto result = goldsrc_spatial::GoldSrcSpatialPackageBuilder::build(
        fixture.input());
    REQUIRE(result);
    REQUIRE(result.package);

    const auto& package = *result.package;
    REQUIRE(package.planes().size() == 1U);
    CHECK(package.planes()[0U].normal.x == 1.0F);
    CHECK(package.world_model().root_node_index == 0U);
    CHECK(package.world_model().visible_leaf_count == 2U);
    REQUIRE(package.nodes().size() == 1U);
    CHECK(package.nodes()[0U].children[0U].kind ==
        hlclient::world_spatial::WorldSpatialNodeChildKind::leaf);
    CHECK(package.nodes()[0U].children[0U].index == 1U);
    CHECK(package.nodes()[0U].children[1U].index == 2U);
    REQUIRE(package.leaves().size() == 3U);
    CHECK(package.leaves()[0U].source_leaf_index == 0U);
    CHECK(package.leaves()[0U].solid_or_special);
    CHECK_FALSE(package.leaves()[0U].pvs_bit_addressable);
    CHECK_FALSE(package.leaves()[0U].pvs_row_index.has_value());
    CHECK(package.leaves()[1U].pvs_bit_addressable);
    CHECK(package.statistics().mapped_world_surface_link_count == 2U);

    fixture.planes.clear();
    fixture.nodes.clear();
    fixture.leaves.clear();
    fixture.marksurfaces.clear();
    fixture.visibility.clear();
    fixture.world_surfaces.clear();
    CHECK(package.nodes().size() == 1U);
    CHECK(package.leaves().size() == 3U);
}

TEST_CASE("Canonical BSP parser handoff builds a spatial package without reparse",
    "[world-spatial][package][goldsrc-integration]")
{
    auto parsed = hlclient::goldsrc::bsp::GoldSrcBspParser::parse(
        hlclient::tests::literal_minimal_goldsrc_bsp_v30());
    REQUIRE(parsed);
    REQUIRE(parsed.document);
    const auto& source = parsed.document->spatial_source;

    auto built = goldsrc_spatial::GoldSrcSpatialPackageBuilder::build(
        goldsrc_spatial::GoldSrcSpatialBuildInput{
            source.planes,
            source.nodes,
            source.leaves,
            source.marksurface_face_ordinals,
            source.visibility_bytes,
            source.world_model,
            source.source_face_count,
            parsed.document->world_asset.surfaces,
            source.submodel_face_ordinals,
        });
    REQUIRE(built);
    REQUIRE(built.package);
    CHECK(built.package->world_model().root_node_index == 0U);
    CHECK(built.package->world_model().visible_leaf_count == 1U);
    REQUIRE(built.package->leaves().size() == 2U);
    CHECK_FALSE(built.package->leaves()[0U].pvs_bit_addressable);
    CHECK(built.package->leaves()[1U].pvs_bit_addressable);
    REQUIRE(built.package->leaves()[1U]
                .surface_membership.world_surface_indices.size() == 1U);
    CHECK(built.package->leaves()[1U]
              .surface_membership.world_surface_indices[0U] == 0U);
}

TEST_CASE("Marksurfaces map exactly to world surface source ordinals",
    "[world-spatial][package][marksurfaces]")
{
    SpatialFixture fixture;
    const auto result = goldsrc_spatial::GoldSrcSpatialPackageBuilder::build(
        fixture.input());
    REQUIRE(result);
    const auto leaves = result.package->leaves();
    REQUIRE(leaves[1U].surface_membership.world_surface_indices.size() == 1U);
    CHECK(leaves[1U].surface_membership.world_surface_indices[0U] == 0U);
    // The duplicate face 0 is deduplicated, and valid face 2 is a submodel-
    // only face absent from model-0 WorldAsset.
    CHECK(leaves[1U].surface_membership.source_marksurface_count == 3U);
    REQUIRE(leaves[2U].surface_membership.world_surface_indices.size() == 1U);
    CHECK(leaves[2U].surface_membership.world_surface_indices[0U] == 0U);

    SECTION("invalid source face reference is fatal")
    {
        fixture.marksurfaces[0U] = fixture.source_face_count;
        const auto invalid = goldsrc_spatial::GoldSrcSpatialPackageBuilder::build(
            fixture.input());
        REQUIRE_FALSE(invalid);
        REQUIRE(invalid.error);
        CHECK(invalid.error->code == goldsrc_spatial::
            GoldSrcSpatialImportErrorCode::invalid_marksurface_reference);
    }

    SECTION("valid but unowned source face reference is fatal")
    {
        ++fixture.source_face_count;
        fixture.marksurfaces[2U] = fixture.source_face_count - 1U;
        const auto invalid = goldsrc_spatial::GoldSrcSpatialPackageBuilder::build(
            fixture.input());
        REQUIRE_FALSE(invalid);
        REQUIRE(invalid.error);
        CHECK(invalid.error->code == goldsrc_spatial::
            GoldSrcSpatialImportErrorCode::invalid_marksurface_reference);
        CHECK(invalid.error->source_element_index == 2U);
    }
}

TEST_CASE("World PVS rows use leaf one as bit zero and deduplicate rows",
    "[world-spatial][pvs]")
{
    SpatialFixture fixture;
    const auto result = goldsrc_spatial::GoldSrcSpatialPackageBuilder::build(
        fixture.input());
    REQUIRE(result);
    const auto& pvs = result.package->pvs_table();
    CHECK(pvs.row_byte_count() == 1U);
    CHECK(pvs.visible_leaf_count() == 2U);
    CHECK(pvs.leaf_is_visible_from(1U, 1U) == std::optional{true});
    CHECK(pvs.leaf_is_visible_from(1U, 2U) == std::optional{false});
    CHECK(pvs.leaf_is_visible_from(2U, 1U) == std::optional{false});
    CHECK(pvs.leaf_is_visible_from(2U, 2U) == std::optional{true});
    CHECK(pvs.leaf_is_visible_from(1U, 0U) == std::optional{false});

    SECTION("duplicate source offsets decode once")
    {
        fixture.leaves[2U].visibility_offset = 0;
        const auto duplicate = goldsrc_spatial::GoldSrcSpatialPackageBuilder::build(
            fixture.input());
        REQUIRE(duplicate);
        CHECK(duplicate.package->pvs_table().unique_row_count() == 2U);
        CHECK(duplicate.package->leaves()[1U].pvs_row_index ==
            duplicate.package->leaves()[2U].pvs_row_index);
    }

    SECTION("minus one maps to the explicit masked all-visible row")
    {
        fixture.leaves[1U].visibility_offset = -1;
        const auto all_visible = goldsrc_spatial::GoldSrcSpatialPackageBuilder::build(
            fixture.input());
        REQUIRE(all_visible);
        CHECK(all_visible.package->pvs_table().leaf_is_visible_from(1U, 1U) ==
            std::optional{true});
        CHECK(all_visible.package->pvs_table().leaf_is_visible_from(1U, 2U) ==
            std::optional{true});
    }

    SECTION("leaves beyond model visleafs retain no PVS row")
    {
        fixture.leaves.push_back(
            {-1, 0, bounds(-4.0F, 4.0F), 0U, 0U});
        const auto extra_leaf =
            goldsrc_spatial::GoldSrcSpatialPackageBuilder::build(
                fixture.input());
        REQUIRE(extra_leaf);
        REQUIRE(extra_leaf.package);
        REQUIRE(extra_leaf.package->leaves().size() == 4U);
        CHECK_FALSE(extra_leaf.package->leaves()[3U].pvs_bit_addressable);
        CHECK_FALSE(extra_leaf.package->leaves()[3U].pvs_row_index);
        REQUIRE(extra_leaf.package->pvs_table().leaf_row_indices().size() == 4U);
        CHECK_FALSE(
            extra_leaf.package->pvs_table().leaf_row_indices()[3U]);
        CHECK_FALSE(extra_leaf.package->pvs_table().row_for_leaf(3U));
    }
}

TEST_CASE("World PVS bit numbering crosses bytes and masks final padding",
    "[world-spatial][pvs][bit-numbering]")
{
    SpatialFixture fixture;
    fixture.model.visible_leaf_count = 9;
    while (fixture.leaves.size() < 10U) {
        fixture.leaves.push_back(
            {-1, 0, bounds(-4.0F, 4.0F), 0U, 0U});
    }
    for (std::size_t leaf_index = 1U;
         leaf_index < fixture.leaves.size();
         ++leaf_index) {
        fixture.leaves[leaf_index].visibility_offset = 0;
    }
    fixture.visibility = {std::byte{0x80U}, std::byte{0xFFU}};

    const auto result = goldsrc_spatial::GoldSrcSpatialPackageBuilder::build(
        fixture.input());
    REQUIRE(result);
    REQUIRE(result.package);
    const auto& pvs = result.package->pvs_table();
    CHECK(pvs.row_byte_count() == 2U);
    CHECK(pvs.visible_leaf_count() == 9U);
    CHECK(pvs.leaf_is_visible_from(1U, 0U) == std::optional{false});
    CHECK(pvs.leaf_is_visible_from(1U, 1U) == std::optional{false});
    CHECK(pvs.leaf_is_visible_from(1U, 8U) == std::optional{true});
    CHECK(pvs.leaf_is_visible_from(1U, 9U) == std::optional{true});
    CHECK(pvs.leaf_is_visible_from(1U, 10U) == std::optional{false});
    const auto row = pvs.row_for_leaf(1U);
    REQUIRE(row);
    REQUIRE(row->size() == 2U);
    CHECK(std::to_integer<std::uint8_t>((*row)[1U]) == 0x01U);
}

TEST_CASE("World spatial package rejects invalid model, child, and cycle state",
    "[world-spatial][package][validation]")
{
    SpatialFixture fixture;

    SECTION("visible leaf count mismatch")
    {
        fixture.model.visible_leaf_count = 3;
        const auto result = goldsrc_spatial::GoldSrcSpatialPackageBuilder::build(
            fixture.input());
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            goldsrc_spatial::GoldSrcSpatialImportErrorCode::invalid_world_model);
    }

    SECTION("INT16_MIN leaf conversion edge")
    {
        fixture.nodes[0U].encoded_children[0U] =
            std::numeric_limits<std::int16_t>::min();
        const auto result = goldsrc_spatial::GoldSrcSpatialPackageBuilder::build(
            fixture.input());
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            goldsrc_spatial::GoldSrcSpatialImportErrorCode::invalid_node);
    }

    SECTION("reachable two-node cycle")
    {
        fixture.nodes = {
            {0, {1, -2}, bounds(-16.0F, 16.0F), 0U, 2U},
            {0, {0, -3}, bounds(-16.0F, 16.0F), 0U, 2U},
        };
        const auto result = goldsrc_spatial::GoldSrcSpatialPackageBuilder::build(
            fixture.input());
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            goldsrc_spatial::GoldSrcSpatialImportErrorCode::node_cycle);
    }
}

TEST_CASE("World spatial import limits accept exact values and reject limit plus one",
    "[world-spatial][package][limits]")
{
    SpatialFixture fixture;
    goldsrc_spatial::GoldSrcSpatialImportLimits limits;
    limits.maximum_marksurface_links = fixture.marksurfaces.size();
    REQUIRE(goldsrc_spatial::GoldSrcSpatialPackageBuilder::build(
        fixture.input(),
        limits));

    limits.maximum_marksurface_links = fixture.marksurfaces.size() - 1U;
    const auto over = goldsrc_spatial::GoldSrcSpatialPackageBuilder::build(
        fixture.input(),
        limits);
    REQUIRE_FALSE(over);
    REQUIRE(over.error);
    CHECK(over.error->code ==
        goldsrc_spatial::GoldSrcSpatialImportErrorCode::count_limit_exceeded);

    SECTION("aggregate PVS storage exact limit and limit minus one")
    {
        auto pvs_limits = goldsrc_spatial::GoldSrcSpatialImportLimits{};
        pvs_limits.maximum_decompressed_pvs_bytes = 3U;
        REQUIRE(goldsrc_spatial::GoldSrcSpatialPackageBuilder::build(
            fixture.input(), pvs_limits));

        pvs_limits.maximum_decompressed_pvs_bytes = 2U;
        const auto pvs_over =
            goldsrc_spatial::GoldSrcSpatialPackageBuilder::build(
                fixture.input(), pvs_limits);
        REQUIRE_FALSE(pvs_over);
        CHECK_FALSE(pvs_over.package);
        REQUIRE(pvs_over.error);
        CHECK(pvs_over.error->code == goldsrc_spatial::
            GoldSrcSpatialImportErrorCode::pvs_table_limit_exceeded);
    }
}

} // namespace
