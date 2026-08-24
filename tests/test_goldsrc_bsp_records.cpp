#include <hlclient/goldsrc/bsp/goldsrc_bsp_format.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>

#include "synthetic_goldsrc_bsp_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <type_traits>

namespace {

namespace bsp = hlclient::goldsrc::bsp;
namespace fixture = hlclient::tests;
using Catch::Approx;

TEST_CASE("GoldSrc fixed records have explicit wire sizes", "[goldsrc-bsp][records][wire]")
{
    STATIC_REQUIRE(bsp::kGoldSrcBspPlaneWireSize == 20U);
    STATIC_REQUIRE(bsp::kGoldSrcBspVertexWireSize == 12U);
    STATIC_REQUIRE(bsp::kGoldSrcBspNodeWireSize == 24U);
    STATIC_REQUIRE(bsp::kGoldSrcBspTexinfoWireSize == 40U);
    STATIC_REQUIRE(bsp::kGoldSrcBspFaceWireSize == 20U);
    STATIC_REQUIRE(bsp::kGoldSrcBspClipnodeWireSize == 8U);
    STATIC_REQUIRE(bsp::kGoldSrcBspLeafWireSize == 28U);
    STATIC_REQUIRE(bsp::kGoldSrcBspMarksurfaceWireSize == 2U);
    STATIC_REQUIRE(bsp::kGoldSrcBspEdgeWireSize == 4U);
    STATIC_REQUIRE(bsp::kGoldSrcBspSurfedgeWireSize == 4U);
    STATIC_REQUIRE(bsp::kGoldSrcBspModelWireSize == 64U);
    STATIC_REQUIRE(bsp::kGoldSrcBspMiptexHeaderWireSize == 40U);
}

TEST_CASE("The literal fixture decodes every fixed-record family exactly",
    "[goldsrc-bsp][records][literal]")
{
    const auto result = bsp::GoldSrcBspParser::parse(fixture::literal_minimal_goldsrc_bsp_v30());
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    const auto count = [&result](const bsp::GoldSrcBspLumpId id) {
        return result.document->lump_element_counts[bsp::goldsrc_bsp_lump_index(id)];
    };
    CHECK(count(bsp::GoldSrcBspLumpId::planes) == 1U);
    CHECK(count(bsp::GoldSrcBspLumpId::vertices) == 4U);
    CHECK(count(bsp::GoldSrcBspLumpId::nodes) == 1U);
    CHECK(count(bsp::GoldSrcBspLumpId::texinfo) == 1U);
    CHECK(count(bsp::GoldSrcBspLumpId::faces) == 1U);
    CHECK(count(bsp::GoldSrcBspLumpId::clipnodes) == 0U);
    CHECK(count(bsp::GoldSrcBspLumpId::leaves) == 2U);
    CHECK(count(bsp::GoldSrcBspLumpId::marksurfaces) == 1U);
    CHECK(count(bsp::GoldSrcBspLumpId::edges) == 5U);
    CHECK(count(bsp::GoldSrcBspLumpId::surfedges) == 4U);
    CHECK(count(bsp::GoldSrcBspLumpId::models) == 1U);

    const auto& world = result.document->world_asset;
    REQUIRE(world.vertices.size() == 4U);
    CHECK(world.vertices[0].position.x == Approx(0.0F));
    CHECK(world.vertices[1].position.x == Approx(64.0F));
    CHECK(world.vertices[2].position.y == Approx(64.0F));
    CHECK(world.vertices[3].position.y == Approx(64.0F));
    for (const auto& vertex : world.vertices) {
        CHECK(vertex.position.z == Approx(0.0F));
        CHECK(vertex.normal.x == Approx(0.0F));
        CHECK(vertex.normal.y == Approx(0.0F));
        CHECK(vertex.normal.z == Approx(1.0F));
        CHECK(vertex.texture_coordinate.x == Approx(vertex.position.x));
        CHECK(vertex.texture_coordinate.y == Approx(vertex.position.y));
    }
    REQUIRE(world.surfaces.size() == 1U);
    CHECK(world.surfaces[0].source_surface_ordinal == 0U);
    CHECK(world.surfaces[0].light_styles ==
        std::array<std::uint8_t, 4U>{0xFFU, 0xFFU, 0xFFU, 0xFFU});
    CHECK_FALSE(world.surfaces[0].lightmap_offset.has_value());
    REQUIRE(world.source_model_bounds.has_value());
    CHECK(world.source_model_bounds->minimum.x == Approx(-1.0F));
    CHECK(world.source_model_bounds->maximum.x == Approx(65.0F));
}

TEST_CASE("A stock-shaped clipnode is decoded structurally", "[goldsrc-bsp][records][clipnode]")
{
    fixture::SyntheticBspBuilder builder;
    const std::array clipnodes{fixture::SyntheticBspClipnode{}};
    auto model = fixture::SyntheticBspModel{};
    model.headnodes[1] = 0;
    builder.set_clipnodes(clipnodes).set_models(std::span{&model, 1U});
    const auto result = bsp::GoldSrcBspParser::parse(builder.build());
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    CHECK(result.document->lump_element_counts[bsp::goldsrc_bsp_lump_index(
              bsp::GoldSrcBspLumpId::clipnodes)] == 1U);
}

TEST_CASE("Every source float family rejects NaN and infinities",
    "[goldsrc-bsp][records][float][mutation]")
{
    struct Mutation { fixture::SyntheticBspLumpId lump; std::size_t offset; };
    constexpr std::array mutations{
        Mutation{fixture::SyntheticBspLumpId::planes, 0U},
        Mutation{fixture::SyntheticBspLumpId::planes, 12U},
        Mutation{fixture::SyntheticBspLumpId::vertices, 0U},
        Mutation{fixture::SyntheticBspLumpId::texinfo, 0U},
        Mutation{fixture::SyntheticBspLumpId::texinfo, 28U},
        Mutation{fixture::SyntheticBspLumpId::models, 0U},
        Mutation{fixture::SyntheticBspLumpId::models, 32U},
    };
    constexpr std::array patterns{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    for (const auto& mutation : mutations) {
        for (const auto pattern : patterns) {
            INFO(static_cast<std::size_t>(mutation.lump));
            INFO(mutation.offset);
            INFO(std::bit_cast<std::uint32_t>(pattern));
            const auto bytes = fixture::SyntheticBspCorruptor{
                fixture::literal_minimal_goldsrc_bsp_v30()}
                                   .write_f32(mutation.lump, mutation.offset, pattern)
                                   .take();
            const auto result = bsp::GoldSrcBspParser::parse(bytes);
            REQUIRE_FALSE(result);
            REQUIRE_FALSE(result.document.has_value());
            REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_float);
            REQUIRE(result.error->context.size() <=
                bsp::kGoldSrcBspMaximumDiagnosticContextBytes);
        }
    }
}

TEST_CASE("Plane normalization and type are validated", "[goldsrc-bsp][records][plane]")
{
    const auto check = [](const std::size_t offset, const auto value) {
        auto corruptor = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()};
        if constexpr (std::is_floating_point_v<std::remove_cv_t<decltype(value)>>) {
            corruptor.write_f32(fixture::SyntheticBspLumpId::planes, offset, value);
        } else {
            corruptor.write_i32(fixture::SyntheticBspLumpId::planes, offset, value);
        }
        const auto result = bsp::GoldSrcBspParser::parse(std::move(corruptor).take());
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_plane);
    };
    SECTION("zero normal") { check(8U, 0.0F); }
    SECTION("non-unit normal") { check(8U, 2.0F); }
    SECTION("unsupported type") { check(16U, std::int32_t{6}); }
}

TEST_CASE("Big-endian-looking input is not treated as native structs",
    "[goldsrc-bsp][records][endian]")
{
    auto bytes = fixture::literal_minimal_goldsrc_bsp_v30();
    bytes[0] = std::byte{0x00};
    bytes[1] = std::byte{0x00};
    bytes[2] = std::byte{0x00};
    bytes[3] = std::byte{0x1E};
    const auto result = bsp::GoldSrcBspParser::parse(bytes);
    REQUIRE_FALSE(result);
    REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::unsupported_version);
}

TEST_CASE("Light offsets use explicit signed little-endian semantics",
    "[goldsrc-bsp][records][lighting]")
{
    fixture::SyntheticBspBuilder builder;
    builder.lump(fixture::SyntheticBspLumpId::lighting) = {std::byte{0x11}};
    auto face = fixture::SyntheticBspFace{};
    face.light_offset = 0;
    builder.set_faces(std::span{&face, 1U});
    const auto result = bsp::GoldSrcBspParser::parse(builder.build());
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    CHECK(result.document->world_asset.surfaces[0].lightmap_offset == 0U);

    face.light_offset = 1;
    builder.set_faces(std::span{&face, 1U});
    const auto invalid = bsp::GoldSrcBspParser::parse(builder.build());
    REQUIRE_FALSE(invalid);
    REQUIRE(invalid.error->code == bsp::GoldSrcBspErrorCode::invalid_light_offset);
}

} // namespace
