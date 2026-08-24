#include <hlclient/goldsrc/brush_models/goldsrc_brush_transform.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

namespace {

namespace assets = hlclient::assets;
namespace brush = hlclient::goldsrc::brush_models;
namespace renderer = hlclient::renderer;

constexpr float kMargin = 1.0e-5F;

void check_vector(
    const assets::AssetVector3& actual,
    const assets::AssetVector3& expected)
{
    CHECK(actual.x == Catch::Approx(expected.x).margin(kMargin));
    CHECK(actual.y == Catch::Approx(expected.y).margin(kMargin));
    CHECK(actual.z == Catch::Approx(expected.z).margin(kMargin));
}

[[nodiscard]] assets::AssetVector3 subtract(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right)
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] assets::AssetVector3 cross(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right)
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

TEST_CASE("GoldSrc brush zero transform is exact identity",
    "[goldsrc-brush][transform]")
{
    const auto built = brush::make_brush_submodel_transform({}, {});
    REQUIRE(built);
    CHECK(built.transform->model_matrix == renderer::RenderMatrix4{});
    CHECK(built.transform->inverse_model_matrix == renderer::RenderMatrix4{});
    CHECK(built.transform->translation.x == 0.0F);
    CHECK(built.transform->rotation_degrees.y == 0.0F);
    CHECK(built.transform->coordinate_profile ==
        brush::BrushSubmodelCoordinateProfile::qcsg_entity_origin_relative_v1);
    CHECK(built.transform->transform_profile ==
        brush::BrushSubmodelTransformProfile::valve_angle_matrix_entity_origin_v1);
    check_vector(brush::transform_brush_point(
        *built.transform, {1.0F, -2.0F, 3.0F}),
        {1.0F, -2.0F, 3.0F});
}

TEST_CASE("Brush translation and inverse use compiler entity origin",
    "[goldsrc-brush][transform][translation]")
{
    const auto built = brush::make_brush_submodel_transform(
        {10.0F, -20.0F, 30.0F}, {});
    REQUIRE(built);
    check_vector(brush::transform_brush_point(
        *built.transform, {1.0F, 2.0F, 3.0F}),
        {11.0F, -18.0F, 33.0F});

    const auto local = renderer::transform(
        built.transform->inverse_model_matrix,
        {11.0F, -18.0F, 33.0F, 1.0F});
    CHECK(local.x == Catch::Approx(1.0F).margin(kMargin));
    CHECK(local.y == Catch::Approx(2.0F).margin(kMargin));
    CHECK(local.z == Catch::Approx(3.0F).margin(kMargin));
    CHECK(local.w == Catch::Approx(1.0F));
}

TEST_CASE("Valve angle profile applies yaw pitch and roll in GoldSrc axes",
    "[goldsrc-brush][transform][rotation]")
{
    SECTION("positive yaw rotates local X to positive Y")
    {
        const auto built = brush::make_brush_submodel_transform(
            {}, {0.0F, 90.0F, 0.0F});
        REQUIRE(built);
        check_vector(brush::transform_brush_point(
            *built.transform, {1.0F, 0.0F, 0.0F}),
            {0.0F, 1.0F, 0.0F});
        check_vector(brush::transform_brush_point(
            *built.transform, {0.0F, 1.0F, 0.0F}),
            {-1.0F, 0.0F, 0.0F});
    }
    SECTION("positive pitch rotates local X to negative Z")
    {
        const auto built = brush::make_brush_submodel_transform(
            {}, {90.0F, 0.0F, 0.0F});
        REQUIRE(built);
        check_vector(brush::transform_brush_point(
            *built.transform, {1.0F, 0.0F, 0.0F}),
            {0.0F, 0.0F, -1.0F});
        check_vector(brush::transform_brush_point(
            *built.transform, {0.0F, 0.0F, 1.0F}),
            {1.0F, 0.0F, 0.0F});
    }
    SECTION("positive roll rotates local Y to positive Z")
    {
        const auto built = brush::make_brush_submodel_transform(
            {}, {0.0F, 0.0F, 90.0F});
        REQUIRE(built);
        check_vector(brush::transform_brush_point(
            *built.transform, {0.0F, 1.0F, 0.0F}),
            {0.0F, 0.0F, 1.0F});
        check_vector(brush::transform_brush_point(
            *built.transform, {0.0F, 0.0F, 1.0F}),
            {0.0F, -1.0F, 0.0F});
    }
    SECTION("composition is yaw times pitch times roll")
    {
        const auto built = brush::make_brush_submodel_transform(
            {}, {90.0F, 90.0F, 0.0F});
        REQUIRE(built);
        check_vector(brush::transform_brush_point(
            *built.transform, {1.0F, 0.0F, 0.0F}),
            {0.0F, 0.0F, -1.0F});
        check_vector(brush::transform_brush_point(
            *built.transform, {0.0F, 1.0F, 0.0F}),
            {-1.0F, 0.0F, 0.0F});
        check_vector(brush::transform_brush_point(
            *built.transform, {0.0F, 0.0F, 1.0F}),
            {0.0F, 1.0F, 0.0F});
    }
}

TEST_CASE("Brush bounds transform all eight corners",
    "[goldsrc-brush][transform][bounds]")
{
    const auto built = brush::make_brush_submodel_transform(
        {10.0F, 20.0F, 30.0F}, {0.0F, 90.0F, 0.0F});
    REQUIRE(built);
    const auto bounds = brush::transform_brush_bounds(
        assets::WorldBounds{{-1.0F, -2.0F, -3.0F}, {1.0F, 2.0F, 3.0F}},
        *built.transform);
    REQUIRE(bounds);
    // Independently: yaw 90 maps (x,y,z) to (-y,x,z), then adds origin.
    check_vector(bounds.bounds->minimum, {8.0F, 19.0F, 27.0F});
    check_vector(bounds.bounds->maximum, {12.0F, 21.0F, 33.0F});
}

TEST_CASE("Brush normal rotation and triangle winding preserve handedness",
    "[goldsrc-brush][transform][normal][winding]")
{
    const auto built = brush::make_brush_submodel_transform(
        {3.0F, 4.0F, 5.0F}, {20.0F, 35.0F, -15.0F});
    REQUIRE(built);

    const assets::AssetVector3 a{0.0F, 0.0F, 0.0F};
    const assets::AssetVector3 b{1.0F, 0.0F, 0.0F};
    const assets::AssetVector3 c{0.0F, 1.0F, 0.0F};
    const auto transformed_a = brush::transform_brush_point(*built.transform, a);
    const auto transformed_b = brush::transform_brush_point(*built.transform, b);
    const auto transformed_c = brush::transform_brush_point(*built.transform, c);
    const auto winding_normal = cross(
        subtract(transformed_b, transformed_a),
        subtract(transformed_c, transformed_a));
    const auto expected_normal = brush::transform_brush_normal(
        *built.transform, {0.0F, 0.0F, 1.0F});
    check_vector(winding_normal, expected_normal);

    const auto length_squared = expected_normal.x * expected_normal.x +
        expected_normal.y * expected_normal.y +
        expected_normal.z * expected_normal.z;
    CHECK(length_squared == Catch::Approx(1.0F).margin(kMargin));
}

TEST_CASE("Nonzero dmodel origin stays evidence-gated",
    "[goldsrc-brush][transform][coordinate-profile]")
{
    REQUIRE(brush::make_brush_submodel_transform(
        {1.0F, 2.0F, 3.0F}, {}, {}));
    const auto unsupported = brush::make_brush_submodel_transform(
        {1.0F, 2.0F, 3.0F}, {}, {0.25F, 0.0F, 0.0F});
    REQUIRE_FALSE(unsupported);
    CHECK(unsupported.error->code ==
        brush::BrushSubmodelTransformErrorCode::unsupported_source_model_origin);
}

TEST_CASE("Brush transform rejects non-finite inputs and invalid bounds",
    "[goldsrc-brush][transform][safety]")
{
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    auto built = brush::make_brush_submodel_transform({nan, 0.0F, 0.0F}, {});
    REQUIRE_FALSE(built);
    CHECK(built.error->code ==
        brush::BrushSubmodelTransformErrorCode::non_finite_input);

    built = brush::make_brush_submodel_transform(
        {}, {std::numeric_limits<float>::infinity(), 0.0F, 0.0F});
    REQUIRE_FALSE(built);
    CHECK(built.error->code ==
        brush::BrushSubmodelTransformErrorCode::non_finite_input);

    const auto identity = brush::make_brush_submodel_transform({}, {});
    REQUIRE(identity);
    auto bounds = brush::transform_brush_bounds(
        assets::WorldBounds{{1.0F, 0.0F, 0.0F}, {-1.0F, 1.0F, 1.0F}},
        *identity.transform);
    REQUIRE_FALSE(bounds);
    CHECK(bounds.error->code ==
        brush::BrushSubmodelTransformErrorCode::invalid_local_bounds);
    bounds = brush::transform_brush_bounds(
        assets::WorldBounds{{0.0F, 0.0F, 0.0F}, {nan, 1.0F, 1.0F}},
        *identity.transform);
    REQUIRE_FALSE(bounds);
    CHECK(bounds.error->code ==
        brush::BrushSubmodelTransformErrorCode::invalid_local_bounds);
}

} // namespace
