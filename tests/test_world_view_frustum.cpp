#include <hlclient/world_visibility/world_view_frustum.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

namespace assets = hlclient::assets;
namespace renderer = hlclient::renderer;
namespace visibility = hlclient::world_visibility;

[[nodiscard]] renderer::RenderCamera canonical_camera()
{
    renderer::RenderCamera camera;
    camera.position = {0.0F, 0.0F, 0.0F};
    camera.target = {0.0F, 0.0F, -1.0F};
    camera.up = {0.0F, 1.0F, 0.0F};
    camera.vertical_field_of_view_radians = 1.57079632679F;
    camera.near_plane = 1.0F;
    camera.far_plane = 10.0F;
    return camera;
}

[[nodiscard]] visibility::WorldBoundsClassification classify(
    const visibility::WorldViewFrustum& frustum,
    const assets::WorldBounds& bounds)
{
    const auto result = frustum.classify(bounds);
    REQUIRE(result);
    REQUIRE(result.classification);
    return *result.classification;
}

TEST_CASE("World view frustum follows the canonical right-handed OpenGL camera",
          "[world-visibility][frustum]")
{
    const auto created = visibility::WorldViewFrustum::from_camera(
        canonical_camera(), {100, 100});
    REQUIRE(created);
    REQUIRE(created.frustum);
    const auto& frustum = *created.frustum;

    CHECK(classify(frustum, {{-0.5F, -0.5F, -3.0F}, {0.5F, 0.5F, -2.0F}}) ==
        visibility::WorldBoundsClassification::inside);
    CHECK(classify(frustum, {{-10.0F, -0.5F, -3.0F}, {-9.0F, 0.5F, -2.0F}}) ==
        visibility::WorldBoundsClassification::outside);
    CHECK(classify(frustum, {{9.0F, -0.5F, -3.0F}, {10.0F, 0.5F, -2.0F}}) ==
        visibility::WorldBoundsClassification::outside);
    CHECK(classify(frustum, {{-0.5F, 9.0F, -3.0F}, {0.5F, 10.0F, -2.0F}}) ==
        visibility::WorldBoundsClassification::outside);
    CHECK(classify(frustum, {{-0.5F, -10.0F, -3.0F}, {0.5F, -9.0F, -2.0F}}) ==
        visibility::WorldBoundsClassification::outside);
    CHECK(classify(frustum, {{-0.1F, -0.1F, -0.5F}, {0.1F, 0.1F, -0.2F}}) ==
        visibility::WorldBoundsClassification::outside);
    CHECK(classify(frustum, {{-0.1F, -0.1F, -12.0F}, {0.1F, 0.1F, -11.0F}}) ==
        visibility::WorldBoundsClassification::outside);
}

TEST_CASE("World view frustum classifies boundary and large AABBs conservatively",
          "[world-visibility][frustum]")
{
    const auto created = visibility::WorldViewFrustum::from_camera(
        canonical_camera(), {100, 100});
    REQUIRE(created.frustum);
    const auto& frustum = *created.frustum;

    CHECK(classify(frustum, {{-0.25F, -0.25F, -1.5F}, {0.25F, 0.25F, -0.5F}}) ==
        visibility::WorldBoundsClassification::intersecting);
    CHECK(classify(frustum,
              {{-100'000.0F, -100'000.0F, -100'000.0F},
                  {100'000.0F, 100'000.0F, 100'000.0F}}) ==
        visibility::WorldBoundsClassification::intersecting);

    const auto invalid = frustum.classify(
        {{1.0F, 0.0F, 0.0F}, {-1.0F, 1.0F, 1.0F}});
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error == visibility::WorldViewFrustumErrorCode::invalid_bounds);
}

TEST_CASE("Frustum extraction reads rows from column-major OpenGL storage",
          "[world-visibility][frustum]")
{
    renderer::RenderMatrix4 identity;
    const auto created = visibility::WorldViewFrustum::from_view_projection(identity);
    REQUIRE(created.frustum);
    const auto& frustum = *created.frustum;

    const auto& left = frustum.plane(visibility::WorldFrustumPlaneIndex::left);
    CHECK(left.normal.x == Catch::Approx(1.0F));
    CHECK(left.normal.y == Catch::Approx(0.0F));
    CHECK(left.normal.z == Catch::Approx(0.0F));
    CHECK(left.signed_offset == Catch::Approx(1.0F));
    const auto& near = frustum.plane(visibility::WorldFrustumPlaneIndex::near);
    CHECK(near.normal.z == Catch::Approx(1.0F));
    CHECK(near.signed_offset == Catch::Approx(1.0F));
    CHECK(classify(frustum, {{-0.5F, -0.5F, -0.5F}, {0.5F, 0.5F, 0.5F}}) ==
        visibility::WorldBoundsClassification::inside);
    CHECK(classify(frustum, {{1.5F, 0.0F, 0.0F}, {2.0F, 0.5F, 0.5F}}) ==
        visibility::WorldBoundsClassification::outside);
}

TEST_CASE("Frustum extraction is deterministic and rejects invalid inputs",
          "[world-visibility][frustum]")
{
    const auto view_projection = renderer::camera_view_projection(
        canonical_camera(), {320, 200});
    REQUIRE(view_projection.matrix);
    const auto first = visibility::WorldViewFrustum::from_view_projection(
        *view_projection.matrix);
    const auto second = visibility::WorldViewFrustum::from_view_projection(
        *view_projection.matrix);
    REQUIRE(first.frustum);
    REQUIRE(second.frustum);
    CHECK(std::ranges::equal(first.frustum->planes(), second.frustum->planes()));

    auto non_finite = *view_projection.matrix;
    non_finite.values[7U] = std::numeric_limits<float>::quiet_NaN();
    auto rejected = visibility::WorldViewFrustum::from_view_projection(non_finite);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error ==
        visibility::WorldViewFrustumErrorCode::non_finite_matrix);

    non_finite.values[7U] = std::numeric_limits<float>::infinity();
    rejected = visibility::WorldViewFrustum::from_view_projection(non_finite);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error ==
        visibility::WorldViewFrustumErrorCode::non_finite_matrix);

    renderer::RenderMatrix4 zero;
    zero.values.fill(0.0F);
    rejected = visibility::WorldViewFrustum::from_view_projection(zero);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error == visibility::WorldViewFrustumErrorCode::degenerate_plane);

    auto camera = canonical_camera();
    camera.near_plane = camera.far_plane;
    rejected = visibility::WorldViewFrustum::from_camera(camera, {100, 100});
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error == visibility::WorldViewFrustumErrorCode::invalid_camera);
    rejected = visibility::WorldViewFrustum::from_camera(canonical_camera(), {0, 100});
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error == visibility::WorldViewFrustumErrorCode::invalid_extent);
}

} // namespace
