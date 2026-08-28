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

TEST_CASE("Canonical BSP decode publishes owning spatial source records",
    "[goldsrc-bsp][spatial][handoff]")
{
    auto source = fixture::literal_minimal_goldsrc_bsp_v30();
    auto parsed = bsp::GoldSrcBspParser::parse(source);
    REQUIRE(parsed);
    REQUIRE(parsed.document);

    const auto& spatial = parsed.document->spatial_source;
    REQUIRE(spatial.planes.size() == 1U);
    REQUIRE(spatial.nodes.size() == 1U);
    REQUIRE(spatial.leaves.size() == 2U);
    REQUIRE(spatial.marksurface_face_ordinals.size() == 1U);
    CHECK(spatial.nodes[0].encoded_children[0] == -1);
    CHECK(spatial.nodes[0].encoded_children[1] == -2);
    CHECK(spatial.marksurface_face_ordinals[0] == 0U);
    CHECK(spatial.world_model.render_headnode == 0);
    CHECK(spatial.world_model.visible_leaf_count == 1);
    CHECK(spatial.source_face_count == 1U);

    source.assign(source.size(), std::byte{0});
    CHECK(spatial.planes[0].normal.z == 1.0F);
    CHECK(spatial.nodes[0].bounds.maximum.x == 65.0F);
    CHECK(spatial.leaves[1].marksurface_count == 1U);
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

TEST_CASE("The canonical parser rejects an in-range hull-0 node cycle",
    "[goldsrc-bsp][spatial][cycle]")
{
    fixture::SyntheticBspBuilder builder;
    auto node = fixture::SyntheticBspNode{};
    node.children[0] = 0;
    const auto result = bsp::GoldSrcBspParser::parse(
        builder.set_nodes(std::span{&node, 1U}).build());
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(result.document);
    REQUIRE(result.error);
    CHECK(result.error->code == bsp::GoldSrcBspErrorCode::node_cycle);
    CHECK(result.error->lump_id == bsp::GoldSrcBspLumpId::nodes);
}

} // namespace
