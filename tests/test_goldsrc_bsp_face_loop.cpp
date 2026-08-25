#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>

#include "synthetic_goldsrc_bsp_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace {

namespace bsp = hlclient::goldsrc::bsp;
namespace fixture = hlclient::tests;

[[nodiscard]] bsp::GoldSrcBspParseResult parse_loop(
    const std::span<const fixture::SyntheticBspVector3> vertices,
    const std::span<const fixture::SyntheticBspEdge> edges,
    const std::span<const std::int32_t> surfedges,
    const bsp::GoldSrcBspImportLimits& limits = {})
{
    fixture::SyntheticBspBuilder builder;
    auto face = fixture::SyntheticBspFace{};
    face.surfedge_count = static_cast<std::int16_t>(surfedges.size());
    builder.set_vertices(vertices)
        .set_edges(edges)
        .set_surfedges(surfedges)
        .set_faces(std::span{&face, 1U});
    return bsp::GoldSrcBspParser::parse(builder.build(), limits);
}

TEST_CASE("Positive surfedge loops reconstruct supported convex polygons",
    "[goldsrc-bsp][face-loop][positive]")
{
    SECTION("triangle")
    {
        fixture::SyntheticBspBuilder builder;
        const auto vertices = fixture::synthetic_triangle_vertices();
        const auto result = bsp::GoldSrcBspParser::parse(
            builder.set_convex_polygon(vertices).build());
        INFO((result.error ? result.error->context : std::string{}));
        REQUIRE(result);
        CHECK(result.document->world_asset.indices.size() == 3U);
    }

    SECTION("quad")
    {
        fixture::SyntheticBspBuilder builder;
        const auto vertices = fixture::synthetic_quad_vertices();
        const auto result = bsp::GoldSrcBspParser::parse(
            builder.set_convex_polygon(vertices).build());
        REQUIRE(result);
        CHECK(result.document->world_asset.indices.size() == 6U);
    }

    SECTION("pentagon")
    {
        fixture::SyntheticBspBuilder builder;
        const auto vertices = fixture::synthetic_pentagon_vertices();
        const auto result = bsp::GoldSrcBspParser::parse(
            builder.set_convex_polygon(vertices).build());
        REQUIRE(result);
        CHECK(result.document->world_asset.indices.size() == 9U);
    }
}

TEST_CASE("Negative and mixed surfedge signs obey the oriented-edge rule",
    "[goldsrc-bsp][face-loop][orientation]")
{
    const auto vertices = fixture::synthetic_triangle_vertices();

    SECTION("all negative")
    {
        constexpr std::array edges{
            fixture::SyntheticBspEdge{0U, 0U},
            fixture::SyntheticBspEdge{2U, 0U},
            fixture::SyntheticBspEdge{1U, 2U},
            fixture::SyntheticBspEdge{0U, 1U},
        };
        constexpr std::array<std::int32_t, 3U> surfedges{-1, -2, -3};
        const auto result = parse_loop(vertices, edges, surfedges);
        INFO((result.error ? result.error->context : std::string{}));
        REQUIRE(result);
        REQUIRE(result.document->world_asset.vertices.size() == 3U);
        CHECK(result.document->world_asset.vertices[0].position.x == 0.0F);
        CHECK(result.document->world_asset.vertices[1].position.x == 64.0F);
        CHECK(result.document->world_asset.vertices[2].position.y == 64.0F);
    }

    SECTION("mixed positive and negative")
    {
        constexpr std::array edges{
            fixture::SyntheticBspEdge{0U, 0U},
            fixture::SyntheticBspEdge{0U, 2U},
            fixture::SyntheticBspEdge{1U, 2U},
            fixture::SyntheticBspEdge{1U, 0U},
        };
        constexpr std::array<std::int32_t, 3U> surfedges{1, -2, 3};
        const auto result = parse_loop(vertices, edges, surfedges);
        INFO((result.error ? result.error->context : std::string{}));
        REQUIRE(result);
    }
}

TEST_CASE("Broken edge loops are never repaired", "[goldsrc-bsp][face-loop][mutation]")
{
    const auto vertices = fixture::synthetic_triangle_vertices();

    SECTION("broken adjacency")
    {
        constexpr std::array edges{
            fixture::SyntheticBspEdge{0U, 0U},
            fixture::SyntheticBspEdge{0U, 1U},
            fixture::SyntheticBspEdge{0U, 2U},
            fixture::SyntheticBspEdge{2U, 0U},
        };
        constexpr std::array<std::int32_t, 3U> surfedges{1, 2, 3};
        const auto result = parse_loop(vertices, edges, surfedges);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::broken_face_edge_loop);
    }

    SECTION("final edge does not close")
    {
        constexpr std::array edges{
            fixture::SyntheticBspEdge{0U, 0U},
            fixture::SyntheticBspEdge{0U, 1U},
            fixture::SyntheticBspEdge{1U, 2U},
            fixture::SyntheticBspEdge{2U, 1U},
        };
        constexpr std::array<std::int32_t, 3U> surfedges{1, 2, 3};
        const auto result = parse_loop(vertices, edges, surfedges);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::broken_face_edge_loop);
    }

    SECTION("consecutive duplicate vertex")
    {
        constexpr std::array edges{
            fixture::SyntheticBspEdge{0U, 0U},
            fixture::SyntheticBspEdge{0U, 1U},
            fixture::SyntheticBspEdge{1U, 1U},
            fixture::SyntheticBspEdge{1U, 0U},
        };
        constexpr std::array<std::int32_t, 3U> surfedges{1, 2, 3};
        const auto result = parse_loop(vertices, edges, surfedges);
        REQUIRE_FALSE(result);
        REQUIRE((result.error->code == bsp::GoldSrcBspErrorCode::broken_face_edge_loop ||
            result.error->code == bsp::GoldSrcBspErrorCode::degenerate_face));
    }

    SECTION("fewer than three unique vertices")
    {
        constexpr std::array edges{
            fixture::SyntheticBspEdge{0U, 0U},
            fixture::SyntheticBspEdge{0U, 1U},
            fixture::SyntheticBspEdge{1U, 0U},
            fixture::SyntheticBspEdge{0U, 0U},
        };
        constexpr std::array<std::int32_t, 3U> surfedges{1, 2, 3};
        const auto result = parse_loop(vertices, edges, surfedges);
        REQUIRE_FALSE(result);
    }
}

TEST_CASE("Surfedge, edge, and vertex references are range checked",
    "[goldsrc-bsp][face-loop][references]")
{
    SECTION("INT32_MIN is not passed to abs")
    {
        auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                         .write_i32(fixture::SyntheticBspLumpId::surfedges, 0U,
                             std::numeric_limits<std::int32_t>::min())
                         .take();
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_surfedge_reference);
    }

    SECTION("edge index out of range")
    {
        auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                         .write_i32(fixture::SyntheticBspLumpId::surfedges, 0U, 5)
                         .take();
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_edge_reference);
    }

    SECTION("vertex index out of range")
    {
        auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                         .write_u16(fixture::SyntheticBspLumpId::edges, 6U, 4U)
                         .take();
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_vertex_reference);
    }

    SECTION("face surfedge range exceeds the lump")
    {
        auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                         .write_i32(fixture::SyntheticBspLumpId::faces, 4U, 2)
                         .take();
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_face_reference);
    }
}

TEST_CASE("Face-edge count observes its exact configured limit",
    "[goldsrc-bsp][face-loop][limits]")
{
    auto limits = bsp::GoldSrcBspImportLimits{};
    limits.maximum_face_edges = 4U;
    REQUIRE(bsp::GoldSrcBspParser::parse(
        fixture::literal_minimal_goldsrc_bsp_v30(), limits));

    limits.maximum_face_edges = 3U;
    const auto result = bsp::GoldSrcBspParser::parse(
        fixture::literal_minimal_goldsrc_bsp_v30(), limits);
    REQUIRE_FALSE(result);
    REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::count_limit_exceeded);
}

} // namespace
