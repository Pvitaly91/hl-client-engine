#include "goldsrc_lightmap_test_fixture.hpp"

#include <hlclient/goldsrc/lightmaps/goldsrc_world_lightmap_import.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace lightmaps = hlclient::goldsrc::lightmaps;
namespace fixture = hlclient::tests::lightmap_fixture;
using Catch::Approx;

namespace {

[[nodiscard]] lightmaps::GoldSrcLightmapExtentResult calculate(
    const fixture::SurfaceDescription& description,
    const std::size_t maximum_samples = 16'384U)
{
    const auto world = fixture::make_world(description);
    return lightmaps::calculate_goldsrc_lightmap_extents(
        world, world.surfaces[0U], maximum_samples);
}

} // namespace

TEST_CASE("GoldSrc lightmap extents use exact floor and ceil block math",
    "[goldsrc-lightmap][extents]")
{
    SECTION("an exact 16-unit face has two samples per axis")
    {
        const auto result = calculate({});
        REQUIRE(result);
        CHECK(result.extents->texture_min_s == 0);
        CHECK(result.extents->texture_min_t == 0);
        CHECK(result.extents->extent_s == 16U);
        CHECK(result.extents->extent_t == 16U);
        CHECK(result.extents->sample_width == 2U);
        CHECK(result.extents->sample_height == 2U);
    }

    SECTION("a non-aligned positive minimum is floored without pre-rounding")
    {
        fixture::SurfaceDescription description;
        description.minimum_s = 1.25F;
        description.minimum_t = 7.75F;
        const auto result = calculate(description);
        REQUIRE(result);
        CHECK(result.extents->texture_min_s == 0);
        CHECK(result.extents->texture_min_t == 0);
        CHECK(result.extents->sample_width == 2U);
        CHECK(result.extents->sample_height == 2U);
    }

    SECTION("a non-aligned maximum is ceiled to the next block")
    {
        fixture::SurfaceDescription description;
        description.maximum_s = 16.125F;
        description.maximum_t = 31.5F;
        const auto result = calculate(description);
        REQUIRE(result);
        CHECK(result.extents->extent_s == 32U);
        CHECK(result.extents->extent_t == 32U);
        CHECK(result.extents->sample_width == 3U);
        CHECK(result.extents->sample_height == 3U);
    }

    SECTION("negative S and T use mathematical floor and ceil")
    {
        fixture::SurfaceDescription description;
        description.minimum_s = -17.0F;
        description.minimum_t = -31.5F;
        description.maximum_s = -1.0F;
        description.maximum_t = -0.25F;
        const auto result = calculate(description);
        REQUIRE(result);
        CHECK(result.extents->texture_min_s == -32);
        CHECK(result.extents->texture_min_t == -32);
        CHECK(result.extents->extent_s == 32U);
        CHECK(result.extents->extent_t == 32U);
        CHECK(result.extents->sample_width == 3U);
        CHECK(result.extents->sample_height == 3U);
    }

    SECTION("mixed negative and positive coordinates retain the full range")
    {
        fixture::SurfaceDescription description;
        description.minimum_s = -8.0F;
        description.minimum_t = -20.0F;
        description.maximum_s = 24.0F;
        description.maximum_t = 9.0F;
        const auto result = calculate(description);
        REQUIRE(result);
        CHECK(result.extents->texture_min_s == -16);
        CHECK(result.extents->texture_min_t == -32);
        CHECK(result.extents->extent_s == 48U);
        CHECK(result.extents->extent_t == 48U);
        CHECK(result.extents->sample_width == 4U);
        CHECK(result.extents->sample_height == 4U);
    }

    SECTION("width and height are extent divided by 16 plus one")
    {
        fixture::SurfaceDescription description;
        description.minimum_s = -1.0F;
        description.minimum_t = 4.0F;
        description.maximum_s = 33.0F;
        description.maximum_t = 20.0F;
        const auto result = calculate(description);
        REQUIRE(result);
        CHECK(result.extents->sample_width ==
            result.extents->extent_s / 16U + 1U);
        CHECK(result.extents->sample_height ==
            result.extents->extent_t / 16U + 1U);
        CHECK(result.extents->sample_width == 5U);
        CHECK(result.extents->sample_height == 3U);
    }
}

TEST_CASE("GoldSrc lightmap local coordinates map to bounded sample centers",
    "[goldsrc-lightmap][extents][coordinates]")
{
    fixture::SurfaceDescription description;
    description.minimum_s = -8.0F;
    description.minimum_t = 4.0F;
    description.maximum_s = 24.0F;
    description.maximum_t = 20.0F;
    const auto world = fixture::make_world(description);
    const auto result = lightmaps::calculate_goldsrc_lightmap_extents(
        world, world.surfaces[0U]);
    REQUIRE(result);
    for (std::size_t vertex_index = 0U; vertex_index < 4U; ++vertex_index) {
        const auto& coordinate = world.vertices[vertex_index].texture_coordinate;
        const auto local_s =
            (static_cast<double>(coordinate.x) - result.extents->texture_min_s) /
            16.0;
        const auto local_t =
            (static_cast<double>(coordinate.y) - result.extents->texture_min_t) /
            16.0;
        CHECK(std::isfinite(local_s));
        CHECK(std::isfinite(local_t));
        CHECK(local_s >= Approx(0.0).margin(
            lightmaps::kGoldSrcLightmapCoordinateTolerance));
        CHECK(local_t >= Approx(0.0).margin(
            lightmaps::kGoldSrcLightmapCoordinateTolerance));
        CHECK(local_s <= Approx(result.extents->sample_width - 1U).margin(
            lightmaps::kGoldSrcLightmapCoordinateTolerance));
        CHECK(local_t <= Approx(result.extents->sample_height - 1U).margin(
            lightmaps::kGoldSrcLightmapCoordinateTolerance));
    }

    // The atlas formula intentionally addresses texel centers without a CPU
    // vertical flip. A 2x2 inner rectangle at (1,1) in an 8x4 page maps its
    // top-left and bottom-right source samples asymmetrically.
    const auto top_left_u = (1.0 + 0.0 + 0.5) / 8.0;
    const auto top_left_v = (1.0 + 0.0 + 0.5) / 4.0;
    const auto bottom_right_u = (1.0 + 1.0 + 0.5) / 8.0;
    const auto bottom_right_v = (1.0 + 1.0 + 0.5) / 4.0;
    CHECK(top_left_u == Approx(0.1875));
    CHECK(top_left_v == Approx(0.375));
    CHECK(bottom_right_u == Approx(0.3125));
    CHECK(bottom_right_v == Approx(0.625));
}

TEST_CASE("GoldSrc lightmap extent validation rejects hostile coordinates and ranges",
    "[goldsrc-lightmap][extents][validation]")
{
    SECTION("NaN is rejected")
    {
        auto world = fixture::make_world();
        world.vertices[1U].texture_coordinate.x =
            std::numeric_limits<float>::quiet_NaN();
        const auto result = lightmaps::calculate_goldsrc_lightmap_extents(
            world, world.surfaces[0U]);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == lightmaps::
                GoldSrcLightmapExtentErrorCode::non_finite_texture_coordinate);
        CHECK(result.error->vertex_index == 1U);
    }

    SECTION("positive and negative infinity are rejected")
    {
        auto world = fixture::make_world();
        world.vertices[0U].texture_coordinate.y =
            std::numeric_limits<float>::infinity();
        auto result = lightmaps::calculate_goldsrc_lightmap_extents(
            world, world.surfaces[0U]);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == lightmaps::
                GoldSrcLightmapExtentErrorCode::non_finite_texture_coordinate);

        world.vertices[0U].texture_coordinate.y =
            -std::numeric_limits<float>::infinity();
        result = lightmaps::calculate_goldsrc_lightmap_extents(
            world, world.surfaces[0U]);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == lightmaps::
                GoldSrcLightmapExtentErrorCode::non_finite_texture_coordinate);
    }

    SECTION("sample limit and limit plus one are distinguished")
    {
        REQUIRE(calculate({}, 4U));
        const auto result = calculate({}, 3U);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            lightmaps::GoldSrcLightmapExtentErrorCode::sample_limit_exceeded);
    }

    SECTION("face-local range cannot omit required corners or escape ownership")
    {
        auto world = fixture::make_world();
        world.surfaces[0U].vertex_count = 2U;
        auto result = lightmaps::calculate_goldsrc_lightmap_extents(
            world, world.surfaces[0U]);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == lightmaps::
                GoldSrcLightmapExtentErrorCode::invalid_surface_vertex_range);

        world.surfaces[0U].vertex_count = 4U;
        world.surfaces[0U].first_vertex = 2U;
        result = lightmaps::calculate_goldsrc_lightmap_extents(
            world, world.surfaces[0U]);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == lightmaps::
                GoldSrcLightmapExtentErrorCode::invalid_surface_vertex_range);
    }

    SECTION("very large finite coordinates fail checked range conversion")
    {
        auto world = fixture::make_world();
        world.vertices[2U].texture_coordinate.x =
            std::numeric_limits<float>::max();
        const auto result = lightmaps::calculate_goldsrc_lightmap_extents(
            world, world.surfaces[0U]);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == lightmaps::
                GoldSrcLightmapExtentErrorCode::coordinate_range_overflow);
    }
}

TEST_CASE("GoldSrc lightmap extent calculation is deterministic",
    "[goldsrc-lightmap][extents][determinism]")
{
    fixture::SurfaceDescription description;
    description.minimum_s = -31.25F;
    description.minimum_t = 0.125F;
    description.maximum_s = 64.5F;
    description.maximum_t = 47.75F;
    const auto first = calculate(description);
    const auto second = calculate(description);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first.extents->texture_min_s == second.extents->texture_min_s);
    CHECK(first.extents->texture_min_t == second.extents->texture_min_t);
    CHECK(first.extents->extent_s == second.extents->extent_s);
    CHECK(first.extents->extent_t == second.extents->extent_t);
    CHECK(first.extents->sample_width == second.extents->sample_width);
    CHECK(first.extents->sample_height == second.extents->sample_height);
}
