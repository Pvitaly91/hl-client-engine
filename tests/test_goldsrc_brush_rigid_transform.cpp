#include <hlclient/goldsrc/brush_models/goldsrc_brush_rigid_transform.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_brush_transform.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>

namespace {

namespace assets = hlclient::assets;
namespace brush = hlclient::goldsrc::brush_models;

constexpr float kMargin = 1.0e-5F;

void check_vector(
    const assets::AssetVector3& actual,
    const assets::AssetVector3& expected)
{
    CHECK(actual.x == Catch::Approx(expected.x).margin(kMargin));
    CHECK(actual.y == Catch::Approx(expected.y).margin(kMargin));
    CHECK(actual.z == Catch::Approx(expected.z).margin(kMargin));
}

void check_round_trip(
    const brush::BrushRigidTransform& transform,
    const assets::AssetVector3& local)
{
    const auto world = brush::brush_rigid_local_to_world_point(transform, local);
    check_vector(
        brush::brush_rigid_world_to_local_point(transform, world), local);
}

TEST_CASE("Neutral GoldSrc brush rigid transform supports identity and translation",
    "[goldsrc-brush][rigid-transform]")
{
    const auto identity = brush::make_brush_rigid_transform({}, {});
    REQUIRE(identity);
    check_vector(identity.transform->rotation_basis.local_x_in_world,
        {1.0F, 0.0F, 0.0F});
    check_vector(identity.transform->rotation_basis.local_y_in_world,
        {0.0F, 1.0F, 0.0F});
    check_vector(identity.transform->rotation_basis.local_z_in_world,
        {0.0F, 0.0F, 1.0F});
    check_vector(brush::brush_rigid_local_to_world_point(
        *identity.transform, {1.0F, -2.0F, 3.0F}),
        {1.0F, -2.0F, 3.0F});

    const auto translated = brush::make_brush_rigid_transform(
        {10.0F, -20.0F, 30.0F}, {});
    REQUIRE(translated);
    check_vector(brush::brush_rigid_local_to_world_point(
        *translated.transform, {1.0F, 2.0F, 3.0F}),
        {11.0F, -18.0F, 33.0F});
    check_vector(brush::brush_rigid_world_to_local_point(
        *translated.transform, {11.0F, -18.0F, 33.0F}),
        {1.0F, 2.0F, 3.0F});
}

TEST_CASE("Neutral rigid basis follows Valve pitch yaw roll profile",
    "[goldsrc-brush][rigid-transform][rotation]")
{
    SECTION("yaw 90 rotates local X to positive Y")
    {
        const auto transform = brush::make_brush_rigid_transform(
            {}, {0.0F, 90.0F, 0.0F});
        REQUIRE(transform);
        check_vector(brush::brush_rigid_local_to_world_vector(
            *transform.transform, {1.0F, 0.0F, 0.0F}),
            {0.0F, 1.0F, 0.0F});
        check_vector(brush::brush_rigid_local_to_world_vector(
            *transform.transform, {0.0F, 1.0F, 0.0F}),
            {-1.0F, 0.0F, 0.0F});
        check_vector(brush::brush_rigid_local_to_world_normal(
            *transform.transform, {1.0F, 0.0F, 0.0F}),
            {0.0F, 1.0F, 0.0F});
    }
    SECTION("pitch 90 rotates local X to negative Z")
    {
        const auto transform = brush::make_brush_rigid_transform(
            {}, {90.0F, 0.0F, 0.0F});
        REQUIRE(transform);
        check_vector(brush::brush_rigid_local_to_world_vector(
            *transform.transform, {1.0F, 0.0F, 0.0F}),
            {0.0F, 0.0F, -1.0F});
    }
    SECTION("roll 90 rotates local Y to positive Z")
    {
        const auto transform = brush::make_brush_rigid_transform(
            {}, {0.0F, 0.0F, 90.0F});
        REQUIRE(transform);
        check_vector(brush::brush_rigid_local_to_world_vector(
            *transform.transform, {0.0F, 1.0F, 0.0F}),
            {0.0F, 0.0F, 1.0F});
    }
}

TEST_CASE("Neutral rigid transform inverses points vectors and normals",
    "[goldsrc-brush][rigid-transform][inverse]")
{
    const auto transform = brush::make_brush_rigid_transform(
        {37.0F, -11.0F, 5.0F}, {20.0F, 35.0F, -15.0F});
    REQUIRE(transform);
    check_round_trip(*transform.transform, {1.25F, -8.0F, 4.5F});

    const assets::AssetVector3 local_vector{2.0F, -3.0F, 7.0F};
    const auto world_vector = brush::brush_rigid_local_to_world_vector(
        *transform.transform, local_vector);
    check_vector(brush::brush_rigid_world_to_local_vector(
        *transform.transform, world_vector), local_vector);

    const assets::AssetVector3 local_normal{0.0F, 0.0F, 1.0F};
    const auto world_normal = brush::brush_rigid_local_to_world_normal(
        *transform.transform, local_normal);
    check_vector(brush::brush_rigid_world_to_local_normal(
        *transform.transform, world_normal), local_normal);
    const auto length_squared = world_normal.x * world_normal.x +
        world_normal.y * world_normal.y + world_normal.z * world_normal.z;
    CHECK(length_squared == Catch::Approx(1.0F).margin(kMargin));
}

TEST_CASE("Neutral rigid transform recomputes transformed bounds",
    "[goldsrc-brush][rigid-transform][bounds]")
{
    const auto transform = brush::make_brush_rigid_transform(
        {10.0F, 20.0F, 30.0F}, {0.0F, 90.0F, 0.0F});
    REQUIRE(transform);
    const auto bounds = brush::transform_brush_rigid_bounds(
        assets::WorldBounds{{-1.0F, -2.0F, -3.0F}, {1.0F, 2.0F, 3.0F}},
        *transform.transform);
    REQUIRE(bounds);
    check_vector(bounds.bounds->minimum, {8.0F, 19.0F, 27.0F});
    check_vector(bounds.bounds->maximum, {12.0F, 21.0F, 33.0F});
}

TEST_CASE("Neutral rigid transform rejects unsupported and non-finite input",
    "[goldsrc-brush][rigid-transform][safety]")
{
    const auto nonzero_origin = brush::make_brush_rigid_transform(
        {}, {}, {0.25F, 0.0F, 0.0F});
    REQUIRE_FALSE(nonzero_origin);
    REQUIRE(nonzero_origin.error);
    CHECK(nonzero_origin.error->code ==
        brush::BrushSubmodelTransformErrorCode::unsupported_source_model_origin);

    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto nonfinite_origin = brush::make_brush_rigid_transform(
        {nan, 0.0F, 0.0F}, {});
    REQUIRE_FALSE(nonfinite_origin);
    REQUIRE(nonfinite_origin.error);
    CHECK(nonfinite_origin.error->code ==
        brush::BrushSubmodelTransformErrorCode::non_finite_input);

    const auto nonfinite_angles = brush::make_brush_rigid_transform(
        {}, {0.0F, std::numeric_limits<float>::infinity(), 0.0F});
    REQUIRE_FALSE(nonfinite_angles);
    REQUIRE(nonfinite_angles.error);
    CHECK(nonfinite_angles.error->code ==
        brush::BrushSubmodelTransformErrorCode::non_finite_input);

    const auto nonfinite_source_origin = brush::make_brush_rigid_transform(
        {}, {}, {0.0F, nan, 0.0F});
    REQUIRE_FALSE(nonfinite_source_origin);
    REQUIRE(nonfinite_source_origin.error);
    CHECK(nonfinite_source_origin.error->code ==
        brush::BrushSubmodelTransformErrorCode::non_finite_input);

    const auto identity = brush::make_brush_rigid_transform({}, {});
    REQUIRE(identity);
    const auto invalid_bounds = brush::transform_brush_rigid_bounds(
        assets::WorldBounds{{1.0F, 0.0F, 0.0F}, {-1.0F, 1.0F, 1.0F}},
        *identity.transform);
    REQUIRE_FALSE(invalid_bounds);
    REQUIRE(invalid_bounds.error);
    CHECK(invalid_bounds.error->code ==
        brush::BrushSubmodelTransformErrorCode::invalid_local_bounds);
}

TEST_CASE("Neutral rigid transform rejects a basis that disagrees with retained angles",
    "[goldsrc-brush][rigid-transform][safety]")
{
    SECTION("tampered basis remains orthonormal but no longer matches the angles")
    {
        const auto made = brush::make_brush_rigid_transform(
            {}, {0.0F, 90.0F, 0.0F});
        REQUIRE(made);
        auto tampered = *made.transform;
        tampered.rotation_basis = {};

        CHECK_FALSE(brush::valid_brush_rigid_transform(tampered));
        const auto bounds = brush::transform_brush_rigid_bounds(
            assets::WorldBounds{{-1.0F, -1.0F, -1.0F},
                {1.0F, 1.0F, 1.0F}},
            tampered);
        REQUIRE_FALSE(bounds);
        REQUIRE(bounds.error);
        CHECK(bounds.error->code ==
            brush::BrushSubmodelTransformErrorCode::non_finite_input);
    }

    SECTION("stale basis is rejected after retained angles change")
    {
        const auto made = brush::make_brush_rigid_transform({}, {});
        REQUIRE(made);
        auto stale = *made.transform;
        stale.rotation_degrees = {15.0F, -30.0F, 5.0F};

        CHECK_FALSE(brush::valid_brush_rigid_transform(stale));
    }
}

TEST_CASE("Renderer brush adapter exactly reflects the neutral rigid basis",
    "[goldsrc-brush][rigid-transform][adapter-parity]")
{
    const assets::AssetVector3 origin{13.0F, -27.0F, 8.0F};
    const assets::AssetVector3 angles{20.0F, 35.0F, -15.0F};
    const auto rigid = brush::make_brush_rigid_transform(origin, angles);
    const auto adapter = brush::make_brush_submodel_transform(origin, angles);
    REQUIRE(rigid);
    REQUIRE(adapter);

    const auto& basis = rigid.transform->rotation_basis;
    const std::array<float, 16U> expected_model{
        basis.local_x_in_world.x, basis.local_x_in_world.y,
        basis.local_x_in_world.z, 0.0F,
        basis.local_y_in_world.x, basis.local_y_in_world.y,
        basis.local_y_in_world.z, 0.0F,
        basis.local_z_in_world.x, basis.local_z_in_world.y,
        basis.local_z_in_world.z, 0.0F,
        origin.x, origin.y, origin.z, 1.0F,
    };
    CHECK(adapter.transform->model_matrix.values == expected_model);

    const std::array<float, 16U> expected_inverse{
        basis.local_x_in_world.x, basis.local_y_in_world.x,
        basis.local_z_in_world.x, 0.0F,
        basis.local_x_in_world.y, basis.local_y_in_world.y,
        basis.local_z_in_world.y, 0.0F,
        basis.local_x_in_world.z, basis.local_y_in_world.z,
        basis.local_z_in_world.z, 0.0F,
        -(basis.local_x_in_world.x * origin.x +
            basis.local_x_in_world.y * origin.y +
            basis.local_x_in_world.z * origin.z),
        -(basis.local_y_in_world.x * origin.x +
            basis.local_y_in_world.y * origin.y +
            basis.local_y_in_world.z * origin.z),
        -(basis.local_z_in_world.x * origin.x +
            basis.local_z_in_world.y * origin.y +
            basis.local_z_in_world.z * origin.z),
        1.0F,
    };
    CHECK(adapter.transform->inverse_model_matrix.values == expected_inverse);

    const assets::AssetVector3 local_point{1.25F, -8.0F, 4.5F};
    check_vector(brush::transform_brush_point(*adapter.transform, local_point),
        brush::brush_rigid_local_to_world_point(*rigid.transform, local_point));
    const assets::AssetVector3 local_normal{0.25F, -0.5F, 0.75F};
    check_vector(brush::transform_brush_normal(*adapter.transform, local_normal),
        brush::brush_rigid_local_to_world_normal(*rigid.transform, local_normal));

    const assets::WorldBounds local_bounds{
        {-3.0F, -2.0F, -1.0F}, {5.0F, 7.0F, 11.0F}};
    const auto rigid_bounds = brush::transform_brush_rigid_bounds(
        local_bounds, *rigid.transform);
    const auto adapter_bounds = brush::transform_brush_bounds(
        local_bounds, *adapter.transform);
    REQUIRE(rigid_bounds);
    REQUIRE(adapter_bounds);
    check_vector(adapter_bounds.bounds->minimum, rigid_bounds.bounds->minimum);
    check_vector(adapter_bounds.bounds->maximum, rigid_bounds.bounds->maximum);
}

} // namespace
