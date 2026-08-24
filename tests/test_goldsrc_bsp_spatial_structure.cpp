#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>

#include "synthetic_goldsrc_bsp_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace {

namespace bsp = hlclient::goldsrc::bsp;
namespace fixture = hlclient::tests;

TEST_CASE("Node plane, node-child, and leaf-child references are bounded",
    "[goldsrc-bsp][spatial][nodes]")
{
    SECTION("baseline leaf children")
    {
        REQUIRE(bsp::GoldSrcBspParser::parse(
            fixture::literal_minimal_goldsrc_bsp_v30()));
    }

    SECTION("valid child node")
    {
        fixture::SyntheticBspBuilder builder;
        std::array nodes{fixture::SyntheticBspNode{}, fixture::SyntheticBspNode{}};
        nodes[0].children[0] = 1;
        const auto result = bsp::GoldSrcBspParser::parse(builder.set_nodes(nodes).build());
        INFO((result.error ? result.error->context : std::string{}));
        REQUIRE(result);
    }

    SECTION("invalid plane reference")
    {
        const auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                               .write_i32(fixture::SyntheticBspLumpId::nodes, 0U, 1)
                               .take();
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_node_reference);
    }

    SECTION("invalid positive child")
    {
        const auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                               .write_i16(fixture::SyntheticBspLumpId::nodes, 4U, 1)
                               .take();
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_node_reference);
    }

    SECTION("invalid encoded leaf child")
    {
        const auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                               .write_i16(fixture::SyntheticBspLumpId::nodes, 4U, -3)
                               .take();
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_node_reference);
    }
}

TEST_CASE("Leaves and marksurfaces retain only validated references",
    "[goldsrc-bsp][spatial][leaves]")
{
    SECTION("leaf contents domain")
    {
        for (const auto contents : std::array<std::int32_t, 2U>{0, -16}) {
            INFO(contents);
            const auto bytes = fixture::SyntheticBspCorruptor{
                fixture::literal_minimal_goldsrc_bsp_v30()}
                                   .write_i32(
                                       fixture::SyntheticBspLumpId::leaves,
                                       28U,
                                       contents)
                                   .take();
            const auto result = bsp::GoldSrcBspParser::parse(bytes);
            REQUIRE_FALSE(result);
            REQUIRE(result.error->code ==
                bsp::GoldSrcBspErrorCode::invalid_leaf_reference);
        }
    }

    SECTION("leaf marksurface range")
    {
        const auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                               .write_u16(fixture::SyntheticBspLumpId::leaves, 28U + 20U, 1U)
                               .take();
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code ==
            bsp::GoldSrcBspErrorCode::invalid_leaf_reference);
    }

    SECTION("marksurface face reference")
    {
        const auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                               .write_u16(fixture::SyntheticBspLumpId::marksurfaces, 0U, 1U)
                               .take();
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code ==
            bsp::GoldSrcBspErrorCode::invalid_marksurface_reference);
    }
}

TEST_CASE("Visibility offsets are bounded without PVS decompression",
    "[goldsrc-bsp][spatial][visibility]")
{
    SECTION("minus one means absent")
    {
        REQUIRE(bsp::GoldSrcBspParser::parse(
            fixture::literal_minimal_goldsrc_bsp_v30()));
    }

    SECTION("valid byte offset is accepted without interpreting PVS bytes")
    {
        fixture::SyntheticBspBuilder builder;
        builder.lump(fixture::SyntheticBspLumpId::visibility) = {
            std::byte{0xFF}, std::byte{0x00}};
        auto leaves = std::array{fixture::SyntheticBspLeaf{}, fixture::SyntheticBspLeaf{}};
        leaves[0].contents = -2;
        leaves[0].marksurface_count = 0U;
        leaves[1].visibility_offset = 1;
        builder.set_leaves(leaves);
        const auto result = bsp::GoldSrcBspParser::parse(builder.build());
        INFO((result.error ? result.error->context : std::string{}));
        REQUIRE(result);
    }

    SECTION("offset at end is invalid")
    {
        fixture::SyntheticBspBuilder builder;
        builder.lump(fixture::SyntheticBspLumpId::visibility) = {std::byte{0xFF}};
        auto leaves = std::array{fixture::SyntheticBspLeaf{}, fixture::SyntheticBspLeaf{}};
        leaves[0].contents = -2;
        leaves[0].marksurface_count = 0U;
        leaves[1].visibility_offset = 1;
        builder.set_leaves(leaves);
        const auto result = bsp::GoldSrcBspParser::parse(builder.build());
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_leaf_reference);
    }
}

TEST_CASE("Clipnode planes and child domains are checked without collision traversal",
    "[goldsrc-bsp][spatial][clipnodes]")
{
    const auto parse_clipnode = [](const fixture::SyntheticBspClipnode& clipnode) {
        fixture::SyntheticBspBuilder builder;
        auto model = fixture::SyntheticBspModel{};
        model.headnodes[1] = 0;
        builder.set_clipnodes(std::span{&clipnode, 1U})
            .set_models(std::span{&model, 1U});
        return bsp::GoldSrcBspParser::parse(builder.build());
    };

    SECTION("valid contents children")
    {
        REQUIRE(parse_clipnode(fixture::SyntheticBspClipnode{}));
    }

    SECTION("invalid plane")
    {
        auto clipnode = fixture::SyntheticBspClipnode{};
        clipnode.plane_index = 1;
        const auto result = parse_clipnode(clipnode);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_clipnode_reference);
    }

    SECTION("invalid positive child")
    {
        auto clipnode = fixture::SyntheticBspClipnode{};
        clipnode.children[0] = 1;
        const auto result = parse_clipnode(clipnode);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_clipnode_reference);
    }

    SECTION("unsupported negative contents child")
    {
        auto clipnode = fixture::SyntheticBspClipnode{};
        clipnode.children[0] = -16;
        const auto result = parse_clipnode(clipnode);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_clipnode_reference);
    }
}

TEST_CASE("An in-range cycle is not traversed by the M4.1 metadata validator",
    "[goldsrc-bsp][spatial][cycle]")
{
    fixture::SyntheticBspBuilder builder;
    auto node = fixture::SyntheticBspNode{};
    node.children[0] = 0;
    const auto result = bsp::GoldSrcBspParser::parse(
        builder.set_nodes(std::span{&node, 1U}).build());
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    CHECK(result.document->world_asset.surfaces.size() == 1U);
}

} // namespace
