#include <hlclient/goldsrc/bsp/goldsrc_face_geometry_builder.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
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

void require_preserved_source_boundary(
    const FaceFixture& fixture,
    const bsp::GoldSrcFaceGeometryBuildResult& result)
{
    INFO((result.error ? bsp::to_string(result.error->code)
                       : std::string_view{"none"}));
    REQUIRE(result);
    REQUIRE(result.candidate);
    const auto& candidate = *result.candidate;
    const auto corner_count = fixture.vertices.size();
    REQUIRE(candidate.corners.size() == corner_count);
    REQUIRE(candidate.local_triangle_indices.size() == (corner_count - 2U) * 3U);
    CHECK(candidate.diagnostic.reconstructed_corner_count == corner_count);
    CHECK(candidate.diagnostic.canonical_corner_count == corner_count);
    CHECK(candidate.diagnostic.removed_collinear_corner_count == 0U);

    auto bounds = assets::WorldBounds{fixture.vertices.front(), fixture.vertices.front()};
    for (std::size_t index = 0U; index < corner_count; ++index) {
        const auto source_index = index == 0U ? 0U : corner_count - index;
        const auto& source = fixture.vertices[source_index];
        const auto& corner = candidate.corners[index];
        CHECK(corner.source_vertex_index == source_index);
        CHECK(corner.position.x == source.x);
        CHECK(corner.position.y == source.y);
        CHECK(corner.position.z == source.z);
        const auto coordinate = [&source](const std::array<float, 4U>& axis) {
            return static_cast<float>(
                static_cast<double>(source.x) * static_cast<double>(axis[0U]) +
                static_cast<double>(source.y) * static_cast<double>(axis[1U]) +
                static_cast<double>(source.z) * static_cast<double>(axis[2U]) +
                static_cast<double>(axis[3U]));
        };
        CHECK(corner.texture_coordinate.x == coordinate(fixture.texinfo.s));
        CHECK(corner.texture_coordinate.y == coordinate(fixture.texinfo.t));
        bounds.minimum.x = std::min(bounds.minimum.x, source.x);
        bounds.minimum.y = std::min(bounds.minimum.y, source.y);
        bounds.minimum.z = std::min(bounds.minimum.z, source.z);
        bounds.maximum.x = std::max(bounds.maximum.x, source.x);
        bounds.maximum.y = std::max(bounds.maximum.y, source.y);
        bounds.maximum.z = std::max(bounds.maximum.z, source.z);
    }
    CHECK(candidate.bounds.minimum.x == bounds.minimum.x);
    CHECK(candidate.bounds.minimum.y == bounds.minimum.y);
    CHECK(candidate.bounds.minimum.z == bounds.minimum.z);
    CHECK(candidate.bounds.maximum.x == bounds.maximum.x);
    CHECK(candidate.bounds.maximum.y == bounds.maximum.y);
    CHECK(candidate.bounds.maximum.z == bounds.maximum.z);

    auto triangle_area_sum = 0.0;
    std::vector<bool> referenced(corner_count, false);
    for (std::size_t triangle = 0U;
         triangle < candidate.local_triangle_indices.size();
         triangle += 3U) {
        for (std::size_t edge = 0U; edge < 3U; ++edge) {
            const auto index = candidate.local_triangle_indices[triangle + edge];
            REQUIRE(index < corner_count);
            referenced[index] = true;
        }
        const auto winding = triangle_normal_dot(candidate, triangle);
        CHECK(std::isfinite(winding));
        CHECK(winding > 0.0);
        triangle_area_sum += winding;
    }
    CHECK(triangle_area_sum == Approx(-candidate.diagnostic.signed_area_normal_dot));
    CHECK(candidate.diagnostic.minimum_triangle_winding_dot > 0.0);

    // Keeping unused corners is insufficient: each original boundary segment
    // must occur exactly once, in canonical direction, in the emitted mesh.
    for (std::size_t first = 0U; first < corner_count; ++first) {
        CHECK(referenced[first]);
        const auto next = (first + 1U) % corner_count;
        std::size_t forward_count = 0U;
        std::size_t reverse_count = 0U;
        for (std::size_t triangle = 0U;
             triangle < candidate.local_triangle_indices.size();
             triangle += 3U) {
            for (std::size_t edge = 0U; edge < 3U; ++edge) {
                const auto a = candidate.local_triangle_indices[triangle + edge];
                const auto b = candidate.local_triangle_indices[
                    triangle + (edge + 1U) % 3U];
                forward_count += a == first && b == next ? 1U : 0U;
                reverse_count += a == next && b == first ? 1U : 0U;
            }
        }
        CHECK(forward_count == 1U);
        CHECK(reverse_count == 0U);
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

TEST_CASE("Centroid area and boundary splits retain deterministic source geometry",
    "[goldsrc-bsp][face-orientation][collinear][bsp-seam]")
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
        require_preserved_source_boundary(fixture, first);
        require_preserved_source_boundary(fixture, second);
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

    SECTION("multiple interior split points survive in bounded source order")
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
        require_preserved_source_boundary(fixture, first);
        require_preserved_source_boundary(fixture, second);
        CHECK(first.candidate->local_triangle_indices ==
            second.candidate->local_triangle_indices);
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
        require_preserved_source_boundary(fixture, result);
        CHECK(result.candidate->diagnostic.distance_tolerance == Approx(0.01));
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
        require_preserved_source_boundary(fixture, result);
        CHECK(result.candidate->diagnostic.distance_tolerance > 0.0001);
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

TEST_CASE("Boundary split chains survive every source anchor and surfedge encoding",
    "[goldsrc-bsp][face-orientation][collinear][bsp-seam]")
{
    const std::vector<assets::AssetVector3> clockwise{
        {0.0F, 16.0F, 0.0F},
        {0.0F, 32.0F, 0.0F},
        {0.0F, 64.0F, 0.0F},
        {16.0F, 64.0F, 0.0F},
        {48.0F, 64.0F, 0.0F},
        {64.0F, 64.0F, 0.0F},
        {64.0F, 48.0F, 0.0F},
        {64.0F, 16.0F, 0.0F},
        {64.0F, 0.0F, 0.0F},
        {48.0F, 0.0F, 0.0F},
        {16.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 0.0F},
    };
    for (const auto side : std::array<std::int16_t, 2U>{0, 1}) {
        for (const auto signs : std::array{
                 SurfedgeSigns::positive, SurfedgeSigns::negative,
                 SurfedgeSigns::mixed}) {
            for (std::size_t anchor = 0U; anchor < clockwise.size(); ++anchor) {
                INFO("side=" << side << " signs=" << static_cast<int>(signs)
                             << " source anchor=" << anchor);
                auto positions = clockwise;
                std::rotate(positions.begin(),
                    positions.begin() + static_cast<std::ptrdiff_t>(anchor),
                    positions.end());
                if (side == 1) {
                    std::reverse(positions.begin() + 1, positions.end());
                }
                auto fixture = make_fixture(std::move(positions), side, signs);
                fixture.texinfo.s = {0.5F, -2.0F, 0.0F, 7.0F};
                fixture.texinfo.t = {-1.25F, 0.75F, 0.0F, -13.0F};
                fixture.limits.maximum_face_edges = clockwise.size();
                const auto first = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
                const auto second = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
                require_preserved_source_boundary(fixture, first);
                require_preserved_source_boundary(fixture, second);
                CHECK(first.candidate->local_triangle_indices ==
                    second.candidate->local_triangle_indices);
                CHECK(first.polygon_edge_pair_test_count ==
                    clockwise.size() * (clockwise.size() - 3U) / 2U);
                CHECK(first.polygon_edge_pair_test_count ==
                    second.polygon_edge_pair_test_count);
                CHECK(first.candidate->emitted_face_normal.z ==
                    (side == 0 ? 1.0F : -1.0F));
            }
        }
    }
}

TEST_CASE("Near-collinear boundary bends retain exact source positions and UVs",
    "[goldsrc-bsp][face-orientation][collinear][bsp-seam]")
{
    for (const auto drift : std::array{-0.0001F, 0.0001F}) {
        for (const auto side : std::array<std::int16_t, 2U>{0, 1}) {
            INFO("drift=" << drift << " side=" << side);
            std::vector<assets::AssetVector3> positions{
                {0.0F, 0.0F, 0.0F},
                {drift, 16.0F, 0.0F},
                {-drift, 32.0F, 0.0F},
                {drift, 48.0F, 0.0F},
                {0.0F, 64.0F, 0.0F},
                {64.0F, 64.0F, 0.0F},
                {64.0F, 0.0F, 0.0F},
            };
            if (side == 1) {
                std::reverse(positions.begin() + 1, positions.end());
            }
            auto fixture = make_fixture(std::move(positions), side, SurfedgeSigns::mixed);
            fixture.texinfo.s = {2.0F, 0.5F, 0.0F, 3.0F};
            fixture.texinfo.t = {-0.75F, -1.0F, 0.0F, -5.0F};
            const auto first = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
            const auto second = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
            require_preserved_source_boundary(fixture, first);
            require_preserved_source_boundary(fixture, second);
            CHECK(first.candidate->local_triangle_indices ==
                second.candidate->local_triangle_indices);
            CHECK(first.candidate->diagnostic.distance_tolerance > std::abs(drift));
        }
    }
}

TEST_CASE("Thin boundary bends may meet or cross a temporary fan diagonal",
    "[goldsrc-bsp][face-orientation][collinear][bsp-seam][thin-face]")
{
    for (const auto bend : std::array{0.00005F, 0.000075F}) {
        const std::vector<assets::AssetVector3> clockwise{
            {0.0F, 0.0F, 0.0F},
            {bend, 32.0F, 0.0F},
            {0.0F, 64.0F, 0.0F},
            {0.0001F, 64.0F, 0.0F},
            {0.0001F, 0.0F, 0.0F},
        };
        for (const auto side : std::array<std::int16_t, 2U>{0, 1}) {
            for (const auto signs : std::array{
                     SurfedgeSigns::positive, SurfedgeSigns::negative,
                     SurfedgeSigns::mixed}) {
                for (std::size_t anchor = 0U; anchor < clockwise.size(); ++anchor) {
                    INFO("bend=" << bend << " side=" << side
                                 << " signs=" << static_cast<int>(signs)
                                 << " source anchor=" << anchor);
                    auto positions = clockwise;
                    std::rotate(positions.begin(),
                        positions.begin() + static_cast<std::ptrdiff_t>(anchor),
                        positions.end());
                    if (side == 1) {
                        std::reverse(positions.begin() + 1, positions.end());
                    }
                    auto fixture = make_fixture(std::move(positions), side, signs);
                    fixture.texinfo.s = {12.5F, 0.0F, 0.0F, 0.0F};
                    fixture.texinfo.t = {-2.0F, 0.125F, 0.0F, 3.0F};
                    const auto first = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
                    const auto second = bsp::GoldSrcFaceGeometryBuilder::build(fixture.input());
                    require_preserved_source_boundary(fixture, first);
                    require_preserved_source_boundary(fixture, second);
                    CHECK(first.candidate->local_triangle_indices ==
                        second.candidate->local_triangle_indices);
                    CHECK(first.polygon_edge_pair_test_count == 5U);
                }
            }
        }
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
