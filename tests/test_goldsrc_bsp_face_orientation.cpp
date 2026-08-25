#include <hlclient/goldsrc/bsp/goldsrc_face_geometry_builder.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace bsp = hlclient::goldsrc::bsp;
using Catch::Approx;

enum class SurfedgeSigns {
    positive,
    negative,
    mixed,
};

struct FaceFixture {
    std::vector<assets::AssetVector3> vertices;
    std::vector<bsp::GoldSrcFaceGeometrySourceEdge> edges;
    std::vector<std::int32_t> surfedges;
    bsp::GoldSrcFaceGeometrySourcePlane plane{
        assets::AssetVector3{0.0F, 0.0F, 1.0F}, 0.0};
    bsp::GoldSrcFaceGeometrySourceFace face{3U, 0, 0, 0};
    bsp::GoldSrcFaceGeometrySourceTexinfo texinfo{
        {1.0F, 0.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F, 0.0F}};
    bsp::GoldSrcFaceGeometryLimits limits{};

    [[nodiscard]] bsp::GoldSrcFaceGeometryBuildInput input() const noexcept
    {
        return {
            7U,
            11U,
            plane,
            face,
            edges,
            surfedges,
            vertices,
            texinfo,
            limits,
            bsp::GoldSrcFaceOrientationCompatibilityProfile::
                valve_qbsp_clockwise_wire_to_counter_clockwise_render,
        };
    }
};

[[nodiscard]] FaceFixture make_fixture(
    std::vector<assets::AssetVector3> raw_wire_positions,
    const std::int16_t side = 0,
    const SurfedgeSigns signs = SurfedgeSigns::positive)
{
    FaceFixture fixture;
    fixture.vertices = std::move(raw_wire_positions);
    fixture.edges.reserve(fixture.vertices.size() + 1U);
    fixture.surfedges.reserve(fixture.vertices.size());
    fixture.edges.push_back({{0U, 0U}});
    for (std::size_t index = 0U; index < fixture.vertices.size(); ++index) {
        const auto start = static_cast<std::uint16_t>(index);
        const auto end = static_cast<std::uint16_t>(
            (index + 1U) % fixture.vertices.size());
        const bool negative = signs == SurfedgeSigns::negative ||
            (signs == SurfedgeSigns::mixed && (index % 2U) != 0U);
        fixture.edges.push_back(negative
                ? bsp::GoldSrcFaceGeometrySourceEdge{{end, start}}
                : bsp::GoldSrcFaceGeometrySourceEdge{{start, end}});
        const auto edge_index = static_cast<std::int32_t>(index + 1U);
        fixture.surfedges.push_back(negative ? -edge_index : edge_index);
    }
    fixture.face.side = side;
    fixture.face.surfedge_count = static_cast<std::int16_t>(fixture.vertices.size());
    return fixture;
}

[[nodiscard]] std::vector<assets::AssetVector3> side_zero_clockwise_quad()
{
    return {
        {0.0F, 0.0F, 0.0F},
        {0.0F, 64.0F, 0.0F},
        {64.0F, 64.0F, 0.0F},
        {64.0F, 0.0F, 0.0F},
    };
}

[[nodiscard]] std::vector<assets::AssetVector3> side_one_clockwise_quad()
{
    // The emitted side-one normal is -Z, so clockwise relative to that normal
    // is counter-clockwise when viewed from +Z.
    return {
        {0.0F, 0.0F, 0.0F},
        {64.0F, 0.0F, 0.0F},
        {64.0F, 64.0F, 0.0F},
        {0.0F, 64.0F, 0.0F},
    };
}

[[nodiscard]] double triangle_normal_dot(
    const bsp::GoldSrcFaceGeometryCandidate& candidate,
    const std::size_t first_index)
{
    const auto& first = candidate.corners[
        candidate.local_triangle_indices[first_index]].position;
    const auto& second = candidate.corners[
        candidate.local_triangle_indices[first_index + 1U]].position;
    const auto& third = candidate.corners[
        candidate.local_triangle_indices[first_index + 2U]].position;
    const auto abx = static_cast<double>(second.x) - static_cast<double>(first.x);
    const auto aby = static_cast<double>(second.y) - static_cast<double>(first.y);
    const auto abz = static_cast<double>(second.z) - static_cast<double>(first.z);
    const auto acx = static_cast<double>(third.x) - static_cast<double>(first.x);
    const auto acy = static_cast<double>(third.y) - static_cast<double>(first.y);
    const auto acz = static_cast<double>(third.z) - static_cast<double>(first.z);
    const auto cross_x = aby * acz - abz * acy;
    const auto cross_y = abz * acx - abx * acz;
    const auto cross_z = abx * acy - aby * acx;
    return cross_x * static_cast<double>(candidate.emitted_face_normal.x) +
        cross_y * static_cast<double>(candidate.emitted_face_normal.y) +
        cross_z * static_cast<double>(candidate.emitted_face_normal.z);
}

void require_canonical_quad(
    const bsp::GoldSrcFaceGeometryBuildResult& result,
    const std::array<std::uint32_t, 4U>& expected_source_indices)
{
    INFO((result.error ? bsp::to_string(result.error->code)
                       : std::string_view{"none"}));
    REQUIRE(result);
    REQUIRE(result.candidate);
    const auto& candidate = *result.candidate;
    REQUIRE(candidate.corners.size() == expected_source_indices.size());
    for (std::size_t index = 0U; index < expected_source_indices.size(); ++index) {
        CHECK(candidate.corners[index].source_vertex_index ==
            expected_source_indices[index]);
    }
    CHECK(candidate.local_triangle_indices ==
        std::vector<std::uint32_t>{0U, 1U, 2U, 0U, 2U, 3U});
    for (std::size_t index = 0U;
         index < candidate.local_triangle_indices.size();
         index += 3U) {
        CHECK(triangle_normal_dot(candidate, index) >
            candidate.diagnostic.maximum_triangle_winding_tolerance);
    }
}

TEST_CASE("Valve qbsp wire loops canonicalize for both face sides and surfedge signs",
    "[goldsrc-bsp][face-orientation][profile]")
{
    SECTION("side zero with positive surfedges")
    {
        const auto fixture = make_fixture(side_zero_clockwise_quad());
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        require_canonical_quad(result, {0U, 3U, 2U, 1U});
        CHECK(result.candidate->emitted_face_normal.z == Approx(1.0F));
        CHECK(result.candidate->diagnostic.signed_area_normal_dot < 0.0);
        CHECK(result.candidate->diagnostic.positive_surfedge_count == 4U);
        CHECK(result.candidate->diagnostic.negative_surfedge_count == 0U);
    }

    SECTION("side zero with negative surfedges")
    {
        const auto fixture = make_fixture(
            side_zero_clockwise_quad(), 0, SurfedgeSigns::negative);
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        require_canonical_quad(result, {0U, 3U, 2U, 1U});
        CHECK(result.candidate->diagnostic.positive_surfedge_count == 0U);
        CHECK(result.candidate->diagnostic.negative_surfedge_count == 4U);
    }

    SECTION("side zero with mixed surfedges")
    {
        const auto fixture = make_fixture(
            side_zero_clockwise_quad(), 0, SurfedgeSigns::mixed);
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        require_canonical_quad(result, {0U, 3U, 2U, 1U});
        CHECK(result.candidate->diagnostic.positive_surfedge_count == 2U);
        CHECK(result.candidate->diagnostic.negative_surfedge_count == 2U);
    }

    SECTION("side one flips the emitted normal and retains canonical winding")
    {
        const auto fixture = make_fixture(side_one_clockwise_quad(), 1);
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        require_canonical_quad(result, {0U, 3U, 2U, 1U});
        CHECK(result.candidate->emitted_face_normal.z == Approx(-1.0F));
        CHECK(result.candidate->diagnostic.signed_area_normal_dot < 0.0);
    }

    SECTION("side one with negative surfedges retains canonical winding")
    {
        const auto fixture = make_fixture(
            side_one_clockwise_quad(), 1, SurfedgeSigns::negative);
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        require_canonical_quad(result, {0U, 3U, 2U, 1U});
        CHECK(result.candidate->emitted_face_normal.z == Approx(-1.0F));
        CHECK(result.candidate->diagnostic.positive_surfedge_count == 0U);
        CHECK(result.candidate->diagnostic.negative_surfedge_count == 4U);
    }

    SECTION("side one with mixed surfedges retains canonical winding")
    {
        const auto fixture = make_fixture(
            side_one_clockwise_quad(), 1, SurfedgeSigns::mixed);
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        require_canonical_quad(result, {0U, 3U, 2U, 1U});
        CHECK(result.candidate->emitted_face_normal.z == Approx(-1.0F));
        CHECK(result.candidate->diagnostic.positive_surfedge_count == 2U);
        CHECK(result.candidate->diagnostic.negative_surfedge_count == 2U);
    }
}

TEST_CASE("Centroid area and evidence-gated cleanup retain deterministic geometry",
    "[goldsrc-bsp][face-orientation][collinear]")
{
    SECTION("the first three wire corners may contain an interior split point")
    {
        auto fixture = make_fixture({
            {0.0F, 0.0F, 0.0F},
            {0.0F, 32.0F, 0.0F},
            {0.0F, 64.0F, 0.0F},
            {64.0F, 64.0F, 0.0F},
            {64.0F, 0.0F, 0.0F},
        });
        const auto first = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        const auto second = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        require_canonical_quad(first, {0U, 4U, 3U, 2U});
        require_canonical_quad(second, {0U, 4U, 3U, 2U});
        CHECK(first.candidate->diagnostic.removed_collinear_corner_count == 1U);
        CHECK(first.candidate->diagnostic.reconstructed_corner_count == 5U);
        CHECK(first.candidate->diagnostic.canonical_corner_count == 4U);
        CHECK(first.candidate->local_triangle_indices ==
            second.candidate->local_triangle_indices);
        for (std::size_t index = 0U; index < first.candidate->corners.size(); ++index) {
            const auto& corner = first.candidate->corners[index];
            CHECK(corner.texture_coordinate.x == Approx(corner.position.x));
            CHECK(corner.texture_coordinate.y == Approx(corner.position.y));
            CHECK(corner.source_vertex_index ==
                second.candidate->corners[index].source_vertex_index);
        }
    }

    SECTION("multiple interior split points are removed in bounded source order")
    {
        auto fixture = make_fixture({
            {0.0F, 0.0F, 0.0F},
            {0.0F, 16.0F, 0.0F},
            {0.0F, 32.0F, 0.0F},
            {0.0F, 64.0F, 0.0F},
            {64.0F, 64.0F, 0.0F},
            {64.0F, 32.0F, 0.0F},
            {64.0F, 0.0F, 0.0F},
        });
        fixture.limits.maximum_face_edges = 7U;
        const auto first = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        const auto second = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        require_canonical_quad(first, {0U, 6U, 4U, 3U});
        require_canonical_quad(second, {0U, 6U, 4U, 3U});
        CHECK(first.candidate->diagnostic.reconstructed_corner_count == 7U);
        CHECK(first.candidate->diagnostic.removed_collinear_corner_count == 3U);
        CHECK(first.candidate->diagnostic.canonical_corner_count == 4U);
        CHECK(first.candidate->diagnostic.removed_collinear_corner_count ==
            first.candidate->diagnostic.reconstructed_corner_count -
                first.candidate->diagnostic.canonical_corner_count);
        CHECK(first.candidate->corners.front().source_vertex_index == 0U);
        for (std::size_t index = 0U; index < first.candidate->corners.size(); ++index) {
            const auto& first_corner = first.candidate->corners[index];
            const auto& second_corner = second.candidate->corners[index];
            CHECK(first_corner.source_vertex_index == second_corner.source_vertex_index);
            CHECK(first_corner.texture_coordinate.x == Approx(first_corner.position.x));
            CHECK(first_corner.texture_coordinate.y == Approx(first_corner.position.y));
            CHECK(first_corner.texture_coordinate.x ==
                Approx(second_corner.texture_coordinate.x));
            CHECK(first_corner.texture_coordinate.y ==
                Approx(second_corner.texture_coordinate.y));
        }

        fixture.limits.maximum_face_edges = 6U;
        const auto over_limit =
            bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        REQUIRE_FALSE(over_limit);
        REQUIRE(over_limit.error);
        CHECK(over_limit.error->code ==
            bsp::GoldSrcFaceGeometryErrorCode::geometry_limit_exceeded);
        CHECK(over_limit.error->diagnostic.removed_collinear_corner_count == 0U);
    }

    SECTION("an endpoint-scale split is not silently treated as interior")
    {
        const auto fixture = make_fixture({
            {0.0F, 0.0F, 0.0F},
            {0.0F, 0.0001F, 0.0F},
            {0.0F, 64.0F, 0.0F},
            {64.0F, 64.0F, 0.0F},
            {64.0F, 0.0F, 0.0F},
        });
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            bsp::GoldSrcFaceGeometryErrorCode::self_intersecting_face);
        CHECK(result.error->diagnostic.distance_tolerance > 0.0001);
        CHECK(result.error->diagnostic.removed_collinear_corner_count == 0U);
    }

    SECTION("a non-interior collinear backtrack remains malformed")
    {
        const auto fixture = make_fixture({
            {0.0F, 0.0F, 0.0F},
            {0.0F, 80.0F, 0.0F},
            {0.0F, 64.0F, 0.0F},
            {64.0F, 64.0F, 0.0F},
            {64.0F, 0.0F, 0.0F},
        });
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            bsp::GoldSrcFaceGeometryErrorCode::self_intersecting_face);
        CHECK(result.error->diagnostic.removed_collinear_corner_count == 0U);
    }

    SECTION("the three-corner minimum is rejected before cleanup can cross it")
    {
        const auto fixture = make_fixture({
            {0.0F, 0.0F, 0.0F},
            {0.0F, 32.0F, 0.0F},
            {0.0F, 64.0F, 0.0F},
        });
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            bsp::GoldSrcFaceGeometryErrorCode::degenerate_face);
        CHECK(result.error->diagnostic.reconstructed_corner_count == 3U);
        CHECK(result.error->diagnostic.removed_collinear_corner_count == 0U);
        CHECK(result.error->diagnostic.canonical_corner_count == 0U);
        CHECK(result.error->diagnostic.area_vector_magnitude == Approx(0.0));
        CHECK(result.error->diagnostic.signed_area_normal_dot == Approx(0.0));
    }

    SECTION("large translated coordinates use the capped distance tolerance")
    {
        auto fixture = make_fixture({
            {100'000.0F, 100'000.0F, 0.0F},
            {100'000.0F, 100'032.0F, 0.0F},
            {100'000.0F, 100'064.0F, 0.0F},
            {100'064.0F, 100'064.0F, 0.0F},
            {100'064.0F, 100'000.0F, 0.0F},
        });
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        require_canonical_quad(result, {0U, 4U, 3U, 2U});
        CHECK(result.candidate->diagnostic.distance_tolerance == Approx(0.01));
        CHECK(result.candidate->diagnostic.removed_collinear_corner_count == 1U);
    }

    SECTION("float-scale line drift remains an interior collinear split")
    {
        auto fixture = make_fixture({
            {0.0F, 0.0F, 0.0F},
            {0.0001F, 32.0F, 0.0F},
            {0.0F, 64.0F, 0.0F},
            {64.0F, 64.0F, 0.0F},
            {64.0F, 0.0F, 0.0F},
        });
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        require_canonical_quad(result, {0U, 4U, 3U, 2U});
        CHECK(result.candidate->diagnostic.distance_tolerance > 0.0001);
        CHECK(result.candidate->diagnostic.removed_collinear_corner_count == 1U);
    }

    SECTION("small valid geometry remains above its independent area tolerance")
    {
        auto fixture = make_fixture({
            {0.0F, 0.0F, 0.0F},
            {0.0F, 0.001F, 0.0F},
            {0.001F, 0.001F, 0.0F},
            {0.001F, 0.0F, 0.0F},
        });
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        require_canonical_quad(result, {0U, 3U, 2U, 1U});
        CHECK(result.candidate->diagnostic.polygon_area_tolerance ==
            Approx(1.0e-12));
        CHECK(result.candidate->diagnostic.maximum_triangle_winding_tolerance ==
            Approx(1.0e-12));
    }

    SECTION("negative coordinates retain the first source anchor")
    {
        auto fixture = make_fixture({
            {-128.0F, -128.0F, 0.0F},
            {-128.0F, -64.0F, 0.0F},
            {-64.0F, -64.0F, 0.0F},
            {-64.0F, -128.0F, 0.0F},
        });
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        require_canonical_quad(result, {0U, 3U, 2U, 1U});
        CHECK(result.candidate->corners.front().position.x == Approx(-128.0F));
        CHECK(result.candidate->corners.front().position.y == Approx(-128.0F));
    }
}

TEST_CASE("Malformed face geometry remains rejected by the compatibility builder",
    "[goldsrc-bsp][face-orientation][malformed]")
{
    SECTION("face sides outside the GoldSrc zero-or-one domain are rejected")
    {
        const auto fixture = make_fixture(side_zero_clockwise_quad(), 2);
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            bsp::GoldSrcFaceGeometryErrorCode::invalid_face_side);
        CHECK(result.error->diagnostic.face_side == 2);
    }

    SECTION("a zero plane normal is rejected")
    {
        auto fixture = make_fixture(side_zero_clockwise_quad());
        fixture.plane.normal = {0.0F, 0.0F, 0.0F};
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            bsp::GoldSrcFaceGeometryErrorCode::invalid_plane);
    }

    SECTION("a finite but non-unit plane normal is rejected")
    {
        auto fixture = make_fixture(side_zero_clockwise_quad());
        fixture.plane.normal = {0.0F, 0.0F, 2.0F};
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            bsp::GoldSrcFaceGeometryErrorCode::invalid_plane);
    }

    SECTION("a finite nonzero clockwise area below tolerance is ambiguous")
    {
        const auto fixture = make_fixture({
            {0.0F, 0.0F, 0.0F},
            {0.0F, 1.0e-7F, 0.0F},
            {1.0e-7F, 0.0F, 0.0F},
        });
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            bsp::GoldSrcFaceGeometryErrorCode::degenerate_face);
        CHECK(result.error->diagnostic.area_vector_magnitude > 0.0);
        CHECK(result.error->diagnostic.area_vector_magnitude <=
            result.error->diagnostic.polygon_area_tolerance);
        CHECK(result.error->diagnostic.signed_area_normal_dot < 0.0);
        CHECK(-result.error->diagnostic.signed_area_normal_dot <=
            result.error->diagnostic.polygon_area_tolerance);
        CHECK(result.error->diagnostic.polygon_area_tolerance ==
            Approx(1.0e-12));
    }

    SECTION("renderer-canonical wire order is reversed for the Valve profile")
    {
        auto fixture = make_fixture({
            {0.0F, 0.0F, 0.0F},
            {64.0F, 0.0F, 0.0F},
            {64.0F, 64.0F, 0.0F},
            {0.0F, 64.0F, 0.0F},
        });
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            bsp::GoldSrcFaceGeometryErrorCode::invalid_face_winding);
        CHECK(result.error->diagnostic.signed_area_normal_dot > 0.0);
    }

    SECTION("broken oriented-edge adjacency is not repaired")
    {
        auto fixture = make_fixture(side_zero_clockwise_quad());
        fixture.surfedges[1U] = fixture.surfedges[2U];
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            bsp::GoldSrcFaceGeometryErrorCode::broken_face_edge_loop);
    }

    SECTION("nonplanar corners retain a typed failure")
    {
        auto positions = side_zero_clockwise_quad();
        positions[2U].z = 0.03F;
        const auto fixture = make_fixture(std::move(positions));
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            bsp::GoldSrcFaceGeometryErrorCode::nonplanar_face);
        CHECK(result.error->diagnostic.maximum_planarity_deviation > 0.02);
    }

    SECTION("self-intersecting loops fail before area orientation")
    {
        const auto fixture = make_fixture({
            {0.0F, 0.0F, 0.0F},
            {64.0F, 64.0F, 0.0F},
            {0.0F, 64.0F, 0.0F},
            {64.0F, 0.0F, 0.0F},
        });
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            bsp::GoldSrcFaceGeometryErrorCode::self_intersecting_face);
    }

    SECTION("concave loops are not accepted after canonical conversion")
    {
        const auto fixture = make_fixture({
            {0.0F, 0.0F, 0.0F},
            {0.0F, 64.0F, 0.0F},
            {32.0F, 32.0F, 0.0F},
            {64.0F, 64.0F, 0.0F},
            {64.0F, 0.0F, 0.0F},
        });
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            bsp::GoldSrcFaceGeometryErrorCode::concave_face);
    }

    SECTION("the non-adjacent edge-pair budget is exact")
    {
        auto fixture = make_fixture(side_zero_clockwise_quad());
        fixture.limits.maximum_polygon_edge_pair_tests = 1U;
        const auto result = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            bsp::GoldSrcFaceGeometryErrorCode::geometry_limit_exceeded);
        CHECK(result.polygon_edge_pair_test_count == 1U);
    }
}

} // namespace
