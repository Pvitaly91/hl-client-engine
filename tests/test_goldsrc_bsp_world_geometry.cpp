#include <hlclient/goldsrc/bsp/goldsrc_bsp_format.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>

#include "synthetic_goldsrc_bsp_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace bsp = hlclient::goldsrc::bsp;
namespace fixture = hlclient::tests;
using Catch::Approx;

[[nodiscard]] fixture::SyntheticBspBuilder make_two_face_builder()
{
    fixture::SyntheticBspBuilder builder;
    constexpr std::array vertices{
        fixture::SyntheticBspVector3{0.0F, 0.0F, 0.0F},
        fixture::SyntheticBspVector3{64.0F, 0.0F, 0.0F},
        fixture::SyntheticBspVector3{64.0F, 64.0F, 0.0F},
        fixture::SyntheticBspVector3{0.0F, 64.0F, 0.0F},
    };
    constexpr std::array edges{
        fixture::SyntheticBspEdge{0U, 0U},
        fixture::SyntheticBspEdge{0U, 1U},
        fixture::SyntheticBspEdge{1U, 2U},
        fixture::SyntheticBspEdge{2U, 0U},
        fixture::SyntheticBspEdge{0U, 2U},
        fixture::SyntheticBspEdge{2U, 3U},
        fixture::SyntheticBspEdge{3U, 0U},
    };
    constexpr std::array<std::int32_t, 6U> surfedges{
        -3, -2, -1, -6, -5, -4};
    std::array faces{fixture::SyntheticBspFace{}, fixture::SyntheticBspFace{}};
    faces[0].surfedge_count = 3;
    faces[1].first_surfedge = 3;
    faces[1].surfedge_count = 3;
    auto node = fixture::SyntheticBspNode{};
    node.face_count = 2U;
    std::array leaves{fixture::SyntheticBspLeaf{}, fixture::SyntheticBspLeaf{}};
    leaves[0].contents = -2;
    leaves[0].marksurface_count = 0U;
    leaves[1].marksurface_count = 2U;
    constexpr std::array<std::uint16_t, 2U> marksurfaces{0U, 1U};
    auto model = fixture::SyntheticBspModel{};
    model.face_count = 2;

    builder.set_vertices(vertices)
        .set_edges(edges)
        .set_surfedges(surfedges)
        .set_faces(faces)
        .set_nodes(std::span{&node, 1U})
        .set_leaves(leaves)
        .set_marksurfaces(marksurfaces)
        .set_models(std::span{&model, 1U});
    return builder;
}

[[nodiscard]] fixture::SyntheticBspBuilder make_two_quad_builder()
{
    fixture::SyntheticBspBuilder builder;
    constexpr auto vertices = fixture::synthetic_quad_vertices();
    constexpr std::array edges{
        fixture::SyntheticBspEdge{0U, 0U},
        fixture::SyntheticBspEdge{0U, 1U},
        fixture::SyntheticBspEdge{1U, 2U},
        fixture::SyntheticBspEdge{2U, 3U},
        fixture::SyntheticBspEdge{3U, 0U},
    };
    constexpr std::array<std::int32_t, 8U> surfedges{
        -4, -3, -2, -1, -4, -3, -2, -1};
    std::array faces{fixture::SyntheticBspFace{}, fixture::SyntheticBspFace{}};
    faces[0].surfedge_count = 4;
    faces[1].first_surfedge = 4;
    faces[1].surfedge_count = 4;
    auto node = fixture::SyntheticBspNode{};
    node.face_count = 2U;
    std::array leaves{fixture::SyntheticBspLeaf{}, fixture::SyntheticBspLeaf{}};
    leaves[0].contents = -2;
    leaves[0].marksurface_count = 0U;
    leaves[1].marksurface_count = 2U;
    constexpr std::array<std::uint16_t, 2U> marksurfaces{0U, 1U};
    auto model = fixture::SyntheticBspModel{};
    model.face_count = 2;

    builder.set_vertices(vertices)
        .set_edges(edges)
        .set_surfedges(surfedges)
        .set_faces(faces)
        .set_nodes(std::span{&node, 1U})
        .set_leaves(leaves)
        .set_marksurfaces(marksurfaces)
        .set_models(std::span{&model, 1U});
    return builder;
}

TEST_CASE("Convex faces use deterministic triangle-fan geometry",
    "[goldsrc-bsp][geometry][triangulation]")
{
    struct Case {
        std::span<const fixture::SyntheticBspVector3> vertices;
        std::size_t expected_triangles;
    };
    constexpr auto triangle = fixture::synthetic_triangle_vertices();
    constexpr auto quad = fixture::synthetic_quad_vertices();
    constexpr auto pentagon = fixture::synthetic_pentagon_vertices();
    const std::array cases{
        Case{triangle, 1U}, Case{quad, 2U}, Case{pentagon, 3U}};

    for (const auto& test_case : cases) {
        INFO(test_case.vertices.size());
        fixture::SyntheticBspBuilder builder;
        const auto result = bsp::GoldSrcBspParser::parse(
            builder.set_convex_polygon(test_case.vertices).build());
        INFO((result.error ? result.error->context : std::string{}));
        REQUIRE(result);
        const auto& world = result.document->world_asset;
        CHECK(world.vertices.size() == test_case.vertices.size());
        CHECK(world.indices.size() == test_case.expected_triangles * 3U);
        CHECK(world.statistics.emitted_triangle_count == test_case.expected_triangles);
        for (std::size_t triangle_index = 0U;
             triangle_index < test_case.expected_triangles;
             ++triangle_index) {
            const auto index = triangle_index * 3U;
            CHECK(world.indices[index] == 0U);
            CHECK(world.indices[index + 1U] == triangle_index + 1U);
            CHECK(world.indices[index + 2U] == triangle_index + 2U);
        }
    }
}

TEST_CASE("World geometry preserves source coordinates, flat normals, and winding",
    "[goldsrc-bsp][geometry][coordinates]")
{
    const auto result = bsp::GoldSrcBspParser::parse(
        fixture::literal_minimal_goldsrc_bsp_v30());
    REQUIRE(result);
    const auto& world = result.document->world_asset;
    CHECK(world.coordinate_space == assets::WorldCoordinateSpace::source_native_goldsrc_z_up);
    CHECK(world.texture_coordinate_space == assets::WorldTextureCoordinateSpace::texel_units);
    REQUIRE(world.indices == std::vector<std::uint32_t>{0U, 1U, 2U, 0U, 2U, 3U});
    for (const auto& vertex : world.vertices) {
        CHECK(vertex.normal.x == Approx(0.0F));
        CHECK(vertex.normal.y == Approx(0.0F));
        CHECK(vertex.normal.z == Approx(1.0F));
    }

    for (std::size_t index = 0U; index < world.indices.size(); index += 3U) {
        const auto& a = world.vertices[world.indices[index]].position;
        const auto& b = world.vertices[world.indices[index + 1U]].position;
        const auto& c = world.vertices[world.indices[index + 2U]].position;
        const auto cross_z = ((b.x - a.x) * (c.y - a.y)) -
            ((b.y - a.y) * (c.x - a.x));
        CHECK(cross_z > 0.0F);
    }
}

TEST_CASE("Back-side faces derive a flipped normal from their source plane",
    "[goldsrc-bsp][geometry][side]")
{
    constexpr std::array clockwise{
        fixture::SyntheticBspVector3{0.0F, 0.0F, 0.0F},
        fixture::SyntheticBspVector3{0.0F, 64.0F, 0.0F},
        fixture::SyntheticBspVector3{64.0F, 64.0F, 0.0F},
        fixture::SyntheticBspVector3{64.0F, 0.0F, 0.0F},
    };
    fixture::SyntheticBspBuilder builder;
    builder.set_convex_polygon(clockwise);
    auto face = fixture::SyntheticBspFace{};
    face.side = 1;
    builder.set_faces(std::span{&face, 1U});
    const auto result = bsp::GoldSrcBspParser::parse(builder.build());
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    for (const auto& vertex : result.document->world_asset.vertices) {
        CHECK(vertex.normal.z == Approx(-1.0F));
    }
}

TEST_CASE("Degenerate, nonplanar, and wrong-winding polygons fail typed",
    "[goldsrc-bsp][geometry][mutation]")
{
    SECTION("degenerate")
    {
        constexpr std::array vertices{
            fixture::SyntheticBspVector3{0.0F, 0.0F, 0.0F},
            fixture::SyntheticBspVector3{32.0F, 0.0F, 0.0F},
            fixture::SyntheticBspVector3{64.0F, 0.0F, 0.0F},
        };
        fixture::SyntheticBspBuilder builder;
        const auto result = bsp::GoldSrcBspParser::parse(
            builder.set_convex_polygon(vertices).build());
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::degenerate_face);
    }

    SECTION("nonplanar")
    {
        auto vertices = fixture::synthetic_quad_vertices();
        vertices[2].z = 1.0F;
        fixture::SyntheticBspBuilder builder;
        const auto result = bsp::GoldSrcBspParser::parse(
            builder.set_convex_polygon(vertices).build());
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::nonplanar_face);
    }

    SECTION("winding opposite the selected face normal")
    {
        constexpr std::array clockwise{
            fixture::SyntheticBspVector3{0.0F, 0.0F, 0.0F},
            fixture::SyntheticBspVector3{0.0F, 64.0F, 0.0F},
            fixture::SyntheticBspVector3{64.0F, 0.0F, 0.0F},
        };
        fixture::SyntheticBspBuilder builder;
        const auto result = bsp::GoldSrcBspParser::parse(
            builder.set_convex_polygon(clockwise).build());
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_face_winding);
    }

    SECTION("self-intersection that passes local turns and fan winding")
    {
        constexpr std::array vertices{
            fixture::SyntheticBspVector3{0.0F, 0.0F, 0.0F},
            fixture::SyntheticBspVector3{5.0F, 3.0F, 0.0F},
            fixture::SyntheticBspVector3{-6.0F, 3.0F, 0.0F},
            fixture::SyntheticBspVector3{-6.0F, -5.0F, 0.0F},
            fixture::SyntheticBspVector3{5.0F, 1.0F, 0.0F},
            fixture::SyntheticBspVector3{0.0F, 3.0F, 0.0F},
        };
        fixture::SyntheticBspBuilder builder;
        const auto result = bsp::GoldSrcBspParser::parse(
            builder.set_convex_polygon(vertices).build());
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_face_winding);
    }
}

TEST_CASE("Texture coordinates are raw source texel-space S and T",
    "[goldsrc-bsp][geometry][uv]")
{
    fixture::SyntheticBspBuilder builder;
    auto texinfo = fixture::SyntheticBspTexinfo{};
    texinfo.s_vector = {2.0F, 0.0F, 0.0F, 3.0F};
    texinfo.t_vector = {0.0F, -1.0F, 0.0F, 5.0F};
    texinfo.flags = static_cast<std::int32_t>(bsp::kGoldSrcBspTexSpecialFlag | 0x100U);
    builder.set_texinfo(std::span{&texinfo, 1U});
    const auto result = bsp::GoldSrcBspParser::parse(builder.build());
    REQUIRE(result);
    const auto& world = result.document->world_asset;
    CHECK(world.vertices[0].texture_coordinate.x == Approx(3.0F));
    CHECK(world.vertices[0].texture_coordinate.y == Approx(5.0F));
    CHECK(world.vertices[1].texture_coordinate.x == Approx(131.0F));
    CHECK(world.vertices[2].texture_coordinate.y == Approx(-59.0F));
    REQUIRE(world.materials.size() == 1U);
    CHECK(world.materials[0].source_texture_flags == texinfo.flags);
    CHECK(world.materials[0].source_texinfo_index == 0U);
    CHECK(world.surfaces[0].special_surface);
}

TEST_CASE("Face-local vertices, source face order, materials, and bounds are owning",
    "[goldsrc-bsp][geometry][ownership]")
{
    auto builder = make_two_face_builder();
    const auto result = bsp::GoldSrcBspParser::parse(builder.build());
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    const auto& world = result.document->world_asset;
    CHECK(world.vertices.size() == 6U);
    CHECK(world.indices.size() == 6U);
    REQUIRE(world.surfaces.size() == 2U);
    CHECK(world.surfaces[0].source_surface_ordinal == 0U);
    CHECK(world.surfaces[1].source_surface_ordinal == 1U);
    CHECK(world.surfaces[0].first_index == 0U);
    CHECK(world.surfaces[1].first_index == 3U);
    CHECK(world.surfaces[0].first_vertex == 0U);
    CHECK(world.surfaces[0].vertex_count == 3U);
    CHECK(world.surfaces[1].first_vertex == 3U);
    CHECK(world.surfaces[1].vertex_count == 3U);
    CHECK(world.materials.size() == 1U);
    CHECK(world.surfaces[0].material_index == 0U);
    CHECK(world.surfaces[1].material_index == 0U);
    CHECK(world.vertices[0].position.x == world.vertices[3].position.x);
    CHECK(world.vertices[0].position.y == world.vertices[3].position.y);
    CHECK(world.bounds.minimum.x == Approx(0.0F));
    CHECK(world.bounds.minimum.y == Approx(0.0F));
    CHECK(world.bounds.maximum.x == Approx(64.0F));
    CHECK(world.bounds.maximum.y == Approx(64.0F));
    CHECK(world.surfaces[0].bounds.maximum.x == Approx(64.0F));
    CHECK(world.surfaces[1].bounds.maximum.y == Approx(64.0F));
}

TEST_CASE("Material identity is the first-use source texinfo index",
    "[goldsrc-bsp][geometry][materials]")
{
    auto builder = make_two_face_builder();
    const std::array texinfo{
        fixture::SyntheticBspTexinfo{}, fixture::SyntheticBspTexinfo{}};
    builder.set_texinfo(texinfo);
    std::array faces{fixture::SyntheticBspFace{}, fixture::SyntheticBspFace{}};
    faces[0].surfedge_count = 3;
    faces[1].first_surfedge = 3;
    faces[1].surfedge_count = 3;
    faces[1].texinfo_index = 1;
    builder.set_faces(faces);
    const auto result = bsp::GoldSrcBspParser::parse(builder.build());
    REQUIRE(result);
    REQUIRE(result.document->world_asset.materials.size() == 2U);
    CHECK(result.document->world_asset.materials[0].source_texinfo_index == 0U);
    CHECK(result.document->world_asset.materials[1].source_texinfo_index == 1U);
    CHECK(result.document->world_asset.surfaces[0].material_index == 0U);
    CHECK(result.document->world_asset.surfaces[1].material_index == 1U);
}

TEST_CASE("Output geometry, material, and validation-work limits are exact",
    "[goldsrc-bsp][geometry][limits]")
{
    auto limits = bsp::GoldSrcBspImportLimits{};
    limits.maximum_output_vertices = 4U;
    limits.maximum_output_indices = 6U;
    limits.maximum_output_surfaces = 1U;
    REQUIRE(bsp::GoldSrcBspParser::parse(
        fixture::literal_minimal_goldsrc_bsp_v30(), limits));

    SECTION("vertex limit plus one")
    {
        limits.maximum_output_vertices = 3U;
        const auto result = bsp::GoldSrcBspParser::parse(
            fixture::literal_minimal_goldsrc_bsp_v30(), limits);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::geometry_limit_exceeded);
    }
    SECTION("index limit plus one")
    {
        limits.maximum_output_indices = 5U;
        const auto result = bsp::GoldSrcBspParser::parse(
            fixture::literal_minimal_goldsrc_bsp_v30(), limits);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::geometry_limit_exceeded);
    }
    SECTION("surface limit and limit plus one")
    {
        auto two_faces = make_two_face_builder();
        limits.maximum_output_vertices = 6U;
        limits.maximum_output_surfaces = 2U;
        REQUIRE(bsp::GoldSrcBspParser::parse(two_faces.build(), limits));
        limits.maximum_output_surfaces = 1U;
        const auto result = bsp::GoldSrcBspParser::parse(two_faces.build(), limits);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::geometry_limit_exceeded);
    }
    SECTION("material limit and limit plus one")
    {
        auto two_materials = make_two_face_builder();
        const std::array texinfo{
            fixture::SyntheticBspTexinfo{}, fixture::SyntheticBspTexinfo{}};
        two_materials.set_texinfo(texinfo);
        std::array faces{fixture::SyntheticBspFace{}, fixture::SyntheticBspFace{}};
        faces[0].surfedge_count = 3;
        faces[1].first_surfedge = 3;
        faces[1].surfedge_count = 3;
        faces[1].texinfo_index = 1;
        two_materials.set_faces(faces);
        limits.maximum_output_vertices = 6U;
        limits.maximum_output_surfaces = 2U;
        limits.maximum_output_materials = 2U;
        REQUIRE(bsp::GoldSrcBspParser::parse(two_materials.build(), limits));
        limits.maximum_output_materials = 1U;
        const auto result = bsp::GoldSrcBspParser::parse(two_materials.build(), limits);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::geometry_limit_exceeded);
    }
    SECTION("polygon edge-pair work limit and limit plus one")
    {
        limits.maximum_polygon_edge_pair_tests = 2U;
        REQUIRE(bsp::GoldSrcBspParser::parse(
            fixture::literal_minimal_goldsrc_bsp_v30(), limits));
        limits.maximum_polygon_edge_pair_tests = 1U;
        const auto result = bsp::GoldSrcBspParser::parse(
            fixture::literal_minimal_goldsrc_bsp_v30(), limits);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::geometry_limit_exceeded);
    }
    SECTION("polygon edge-pair work budget aggregates across faces")
    {
        auto two_quads = make_two_quad_builder();
        limits.maximum_output_vertices = 8U;
        limits.maximum_output_indices = 12U;
        limits.maximum_output_surfaces = 2U;
        limits.maximum_polygon_edge_pair_tests = 4U;
        REQUIRE(bsp::GoldSrcBspParser::parse(two_quads.build(), limits));
        limits.maximum_polygon_edge_pair_tests = 3U;
        const auto result = bsp::GoldSrcBspParser::parse(
            two_quads.build(), limits);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::geometry_limit_exceeded);
    }
}

TEST_CASE("Repeated parsing is deterministic and the world owns its output",
    "[goldsrc-bsp][geometry][determinism]")
{
    assets::WorldAsset retained;
    {
        auto bytes = fixture::literal_minimal_goldsrc_bsp_v30();
        const auto first = bsp::GoldSrcBspParser::parse(bytes);
        const auto second = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE(first);
        REQUIRE(second);
        CHECK(first.document->world_asset.indices == second.document->world_asset.indices);
        CHECK(first.document->world_asset.vertices.size() ==
            second.document->world_asset.vertices.size());
        CHECK(first.document->world_asset.materials[0].texture_name ==
            second.document->world_asset.materials[0].texture_name);
        retained = first.document->world_asset;
        std::fill(bytes.begin(), bytes.end(), std::byte{0xCC});
    }
    REQUIRE(retained.vertices.size() == 4U);
    REQUIRE(retained.indices == std::vector<std::uint32_t>{0U, 1U, 2U, 0U, 2U, 3U});
    CHECK(retained.materials[0].texture_name == "TEST_QUAD");
}

} // namespace
