#include <hlclient/goldsrc/bsp/goldsrc_bsp_collision_source.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>

#include "synthetic_goldsrc_bsp_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace {

namespace bsp = hlclient::goldsrc::bsp;
namespace core_collision = hlclient::collision;
namespace fixture = hlclient::tests;
namespace goldsrc_collision = hlclient::goldsrc::collision;

constexpr std::array kLiteralClipnode{
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFE}, std::byte{0xFF},
};

TEST_CASE("An independent literal BSP owns every collision tree domain",
          "[goldsrc-bsp][collision-source][literal][package]")
{
    const auto result = bsp::GoldSrcBspParser::parse(
        fixture::literal_collision_goldsrc_bsp_v30());
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    REQUIRE(result.document);

    const auto& source = result.document->collision_source;
    REQUIRE(source.planes.size() == 1U);
    REQUIRE(source.nodes.size() == 1U);
    REQUIRE(source.leaves.size() == 2U);
    REQUIRE(source.clipnodes.size() == 1U);
    REQUIRE(source.models.size() == 1U);

    CHECK(source.nodes[0U].source_plane_index == 0U);
    REQUIRE(std::holds_alternative<
        bsp::GoldSrcBspCollisionSourceLeafReference>(
        source.nodes[0U].children[0U]));
    CHECK(std::get<bsp::GoldSrcBspCollisionSourceLeafReference>(
              source.nodes[0U].children[0U]).source_leaf_index == 0U);
    REQUIRE(std::holds_alternative<
        bsp::GoldSrcBspCollisionSourceLeafReference>(
        source.nodes[0U].children[1U]));
    CHECK(std::get<bsp::GoldSrcBspCollisionSourceLeafReference>(
              source.nodes[0U].children[1U]).source_leaf_index == 1U);
    CHECK(source.leaves[0U].contents.value == -2);
    CHECK(source.leaves[1U].contents.value == -1);

    const auto& clipnode = source.clipnodes[0U];
    CHECK(clipnode.source_plane_index == 0U);
    REQUIRE(std::holds_alternative<bsp::GoldSrcContentsCode>(
        clipnode.children[0U]));
    REQUIRE(std::holds_alternative<bsp::GoldSrcContentsCode>(
        clipnode.children[1U]));
    CHECK(std::get<bsp::GoldSrcContentsCode>(clipnode.children[0U]).value == -1);
    CHECK(std::get<bsp::GoldSrcContentsCode>(clipnode.children[1U]).value == -2);

    const auto& model = source.models[0U];
    CHECK(model.point_hull.root.source_node_index == 0U);
    for (const auto* hull :
         std::array{&model.standing_hull, &model.large_hull, &model.duck_hull}) {
        REQUIRE(std::holds_alternative<
            bsp::GoldSrcBspCollisionSourceClipnodeReference>(hull->root));
        CHECK(std::get<bsp::GoldSrcBspCollisionSourceClipnodeReference>(
                  hull->root).source_clipnode_index == 0U);
    }
    CHECK(source.statistics.terminal_empty_count == 2U);
    CHECK(source.statistics.terminal_solid_count == 2U);
    CHECK(source.statistics.model_hull_root_count == 4U);

    const auto built = goldsrc_collision::GoldSrcCollisionWorldBuilder::build(
        source);
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.package);
    REQUIRE(built.package->models().size() == 1U);
    const auto* world = built.package->model(0U);
    REQUIRE(world != nullptr);
    CHECK(world->hulls[0U].root.kind ==
        core_collision::CollisionHullRootKind::node);
    CHECK(world->hulls[0U].root.index == 0U);
    for (std::size_t hull = 1U; hull < world->hulls.size(); ++hull) {
        CHECK(world->hulls[hull].root.kind ==
            core_collision::CollisionHullRootKind::clipnode);
        CHECK(world->hulls[hull].root.index == 0U);
    }
}

[[nodiscard]] bsp::GoldSrcBspParseResult parse_clipnode(
    const fixture::SyntheticBspClipnode clipnode)
{
    fixture::SyntheticBspBuilder builder;
    auto model = fixture::SyntheticBspModel{};
    model.headnodes[1U] = 0;
    return bsp::GoldSrcBspParser::parse(
        builder.set_clipnodes(std::span{&clipnode, 1U}).set_models(std::span{&model, 1U}).build());
}

TEST_CASE("The canonical parser retains the exact eight-byte clipnode record",
          "[goldsrc-bsp][collision-source][clipnode][wire]")
{
    fixture::SyntheticBspBuilder builder;
    builder.lump(fixture::SyntheticBspLumpId::clipnodes)
        .assign(kLiteralClipnode.begin(), kLiteralClipnode.end());
    auto model = fixture::SyntheticBspModel{};
    model.headnodes = {0, 0, 0, 0};
    const auto result =
        bsp::GoldSrcBspParser::parse(builder.set_models(std::span{&model, 1U}).build());
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);

    const auto& source = result.document->collision_source;
    REQUIRE(source.clipnodes.size() == 1U);
    const auto& clipnode = source.clipnodes[0U];
    CHECK(clipnode.source_clipnode_index == 0U);
    CHECK(clipnode.source_plane_index == 0U);
    REQUIRE(std::holds_alternative<bsp::GoldSrcContentsCode>(clipnode.children[0U]));
    REQUIRE(std::holds_alternative<bsp::GoldSrcContentsCode>(clipnode.children[1U]));
    CHECK(std::get<bsp::GoldSrcContentsCode>(clipnode.children[0U]).value == -1);
    CHECK(std::get<bsp::GoldSrcContentsCode>(clipnode.children[1U]).value == -2);
    CHECK(source.statistics.clipnode_count == 1U);
    CHECK(source.statistics.reachable_clipnodes == 1U);
    CHECK(source.statistics.unreachable_clipnodes == 0U);
    CHECK(source.statistics.terminal_empty_count == 2U);
    CHECK(source.statistics.terminal_solid_count == 2U);
    CHECK(source.statistics.terminal_liquid_count == 0U);
    CHECK(source.statistics.terminal_special_count == 0U);
    CHECK(source.statistics.model_hull_root_count == 4U);
    CHECK(source.statistics.direct_terminal_root_count == 0U);
}

TEST_CASE("Every incomplete clipnode wire length is rejected before decode",
          "[goldsrc-bsp][collision-source][clipnode][truncation]")
{
    for (std::size_t retained = 1U; retained < kLiteralClipnode.size(); ++retained) {
        INFO(retained);
        fixture::SyntheticBspBuilder builder;
        builder.lump(fixture::SyntheticBspLumpId::clipnodes)
            .assign(kLiteralClipnode.begin(),
                    kLiteralClipnode.begin() + static_cast<std::ptrdiff_t>(retained));
        const auto result = bsp::GoldSrcBspParser::parse(builder.build());
        REQUIRE_FALSE(result);
        REQUIRE_FALSE(result.document);
        REQUIRE(result.error);
        CHECK(result.error->code == bsp::GoldSrcBspErrorCode::misaligned_fixed_lump_size);
        CHECK(result.error->lump_id == bsp::GoldSrcBspLumpId::clipnodes);
    }

    fixture::SyntheticBspBuilder empty;
    empty.clear_lump(fixture::SyntheticBspLumpId::clipnodes);
    CHECK(bsp::GoldSrcBspParser::parse(empty.build()));
}

TEST_CASE("Clipnode references and exact signed contents fail closed",
          "[goldsrc-bsp][collision-source][clipnode][reference]")
{
    SECTION("invalid plane")
    {
        auto clipnode = fixture::SyntheticBspClipnode{};
        clipnode.plane_index = 1;
        const auto result = parse_clipnode(clipnode);
        REQUIRE_FALSE(result);
        CHECK(result.error->code == bsp::GoldSrcBspErrorCode::invalid_clipnode_reference);
    }

    SECTION("invalid positive child")
    {
        auto clipnode = fixture::SyntheticBspClipnode{};
        clipnode.children[0U] = 1;
        const auto result = parse_clipnode(clipnode);
        REQUIRE_FALSE(result);
        CHECK(result.error->code == bsp::GoldSrcBspErrorCode::invalid_clipnode_reference);
    }

    for (const auto contents :
         std::array{std::int16_t{-16}, std::numeric_limits<std::int16_t>::min()}) {
        INFO(contents);
        auto clipnode = fixture::SyntheticBspClipnode{};
        clipnode.children[0U] = contents;
        const auto result = parse_clipnode(clipnode);
        REQUIRE_FALSE(result);
        CHECK(result.error->code == bsp::GoldSrcBspErrorCode::invalid_clipnode_reference);
    }
}

TEST_CASE("Node and clipnode cycles are rejected before document publication",
          "[goldsrc-bsp][collision-source][cycle]")
{
    SECTION("node self cycle")
    {
        fixture::SyntheticBspBuilder builder;
        auto node = fixture::SyntheticBspNode{};
        node.children[0U] = 0;
        const auto result =
            bsp::GoldSrcBspParser::parse(builder.set_nodes(std::span{&node, 1U}).build());
        REQUIRE_FALSE(result);
        REQUIRE_FALSE(result.document);
        CHECK(result.error->code == bsp::GoldSrcBspErrorCode::node_cycle);
    }

    SECTION("node two-record cycle")
    {
        fixture::SyntheticBspBuilder builder;
        std::array nodes{fixture::SyntheticBspNode{}, fixture::SyntheticBspNode{}};
        nodes[0U].children[0U] = 1;
        nodes[1U].children[0U] = 0;
        const auto result = bsp::GoldSrcBspParser::parse(builder.set_nodes(nodes).build());
        REQUIRE_FALSE(result);
        REQUIRE_FALSE(result.document);
        CHECK(result.error->code == bsp::GoldSrcBspErrorCode::node_cycle);
    }

    SECTION("clipnode self cycle")
    {
        fixture::SyntheticBspBuilder builder;
        auto clipnode = fixture::SyntheticBspClipnode{};
        clipnode.children[0U] = 0;
        auto model = fixture::SyntheticBspModel{};
        model.headnodes[1U] = 0;
        const auto result =
            bsp::GoldSrcBspParser::parse(builder.set_clipnodes(std::span{&clipnode, 1U})
                                             .set_models(std::span{&model, 1U})
                                             .build());
        REQUIRE_FALSE(result);
        REQUIRE_FALSE(result.document);
        CHECK(result.error->code == bsp::GoldSrcBspErrorCode::clipnode_cycle);
    }

    SECTION("clipnode two-record cycle")
    {
        fixture::SyntheticBspBuilder builder;
        std::array clipnodes{fixture::SyntheticBspClipnode{}, fixture::SyntheticBspClipnode{}};
        clipnodes[0U].children[0U] = 1;
        clipnodes[1U].children[0U] = 0;
        auto model = fixture::SyntheticBspModel{};
        model.headnodes[1U] = 0;
        const auto result = bsp::GoldSrcBspParser::parse(
            builder.set_clipnodes(clipnodes).set_models(std::span{&model, 1U}).build());
        REQUIRE_FALSE(result);
        REQUIRE_FALSE(result.document);
        CHECK(result.error->code == bsp::GoldSrcBspErrorCode::clipnode_cycle);
    }
}

TEST_CASE("Shared subtrees are retained and unreachable records are counted",
          "[goldsrc-bsp][collision-source][graph][shared]")
{
    fixture::SyntheticBspBuilder builder;
    std::array nodes{fixture::SyntheticBspNode{}, fixture::SyntheticBspNode{},
                     fixture::SyntheticBspNode{}, fixture::SyntheticBspNode{},
                     fixture::SyntheticBspNode{}};
    nodes[0U].children = {1, 2};
    nodes[1U].children[0U] = 3;
    nodes[2U].children[0U] = 3;

    std::array clipnodes{fixture::SyntheticBspClipnode{}, fixture::SyntheticBspClipnode{},
                         fixture::SyntheticBspClipnode{}, fixture::SyntheticBspClipnode{},
                         fixture::SyntheticBspClipnode{}};
    clipnodes[0U].children = {1, 2};
    clipnodes[1U].children = {3, -1};
    clipnodes[2U].children = {3, -2};
    clipnodes[4U].children = {-6, -1};

    auto model = fixture::SyntheticBspModel{};
    model.headnodes = {0, 0, 1, 2};
    const auto result = bsp::GoldSrcBspParser::parse(builder.set_nodes(nodes)
                                                         .set_clipnodes(clipnodes)
                                                         .set_models(std::span{&model, 1U})
                                                         .build());
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    const auto& statistics = result.document->collision_source.statistics;
    CHECK(statistics.reachable_hull0_nodes == 4U);
    CHECK(statistics.unreachable_hull0_nodes == 1U);
    CHECK(statistics.reachable_clipnodes == 4U);
    CHECK(statistics.unreachable_clipnodes == 1U);
    CHECK(statistics.maximum_tree_depth == 3U);
}

TEST_CASE("Cycles in globally unreachable collision records also fail closed",
          "[goldsrc-bsp][collision-source][graph][unreachable][cycle]")
{
    fixture::SyntheticBspBuilder builder;
    std::array clipnodes{fixture::SyntheticBspClipnode{}, fixture::SyntheticBspClipnode{}};
    clipnodes[1U].children[0U] = 1;
    auto model = fixture::SyntheticBspModel{};
    model.headnodes[1U] = 0;
    const auto result = bsp::GoldSrcBspParser::parse(
        builder.set_clipnodes(clipnodes).set_models(std::span{&model, 1U}).build());
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(result.document);
    CHECK(result.error->code == bsp::GoldSrcBspErrorCode::clipnode_cycle);
}

TEST_CASE("Collision models retain all typed roots and exact compiler extents",
          "[goldsrc-bsp][collision-source][models][hulls]")
{
    fixture::SyntheticBspBuilder builder;
    const std::array clipnodes{fixture::SyntheticBspClipnode{}, fixture::SyntheticBspClipnode{},
                               fixture::SyntheticBspClipnode{}};
    auto model = fixture::SyntheticBspModel{};
    model.minimum = {-10.0F, -20.0F, -30.0F};
    model.maximum = {40.0F, 50.0F, 60.0F};
    model.origin = {1.0F, 2.0F, 3.0F};
    model.headnodes = {0, 0, 1, 2};
    const auto bytes = builder.set_clipnodes(clipnodes).set_models(std::span{&model, 1U}).build();
    const auto result = bsp::GoldSrcBspParser::parse(bytes);
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    const auto& source = result.document->collision_source;
    REQUIRE(source.models.size() == 1U);
    const auto& retained = source.models[0U];
    CHECK(retained.source_model_index == 0U);
    CHECK(retained.source_origin.x == 1.0F);
    CHECK(retained.source_origin.y == 2.0F);
    CHECK(retained.source_origin.z == 3.0F);
    CHECK(retained.source_bounds.minimum.x == -10.0F);
    CHECK(retained.source_bounds.minimum.y == -20.0F);
    CHECK(retained.source_bounds.minimum.z == -30.0F);
    CHECK(retained.source_bounds.maximum.x == 40.0F);
    CHECK(retained.source_bounds.maximum.y == 50.0F);
    CHECK(retained.source_bounds.maximum.z == 60.0F);
    CHECK(retained.visible_leaf_count == 1U);
    CHECK(retained.first_source_face == 0U);
    CHECK(retained.source_face_count == 1U);
    CHECK(source.compatibility_profile ==
          bsp::GoldSrcBspCollisionCompatibilityProfile::valve_bsp_v30_clip_hulls_v1);
    CHECK(source.evidence_profile ==
          bsp::GoldSrcBspCollisionEvidenceProfile::
              public_valve_bsp_compiler_and_original_map_validation);
    CHECK(retained.point_hull.ordinal == bsp::GoldSrcBspCollisionHullOrdinal::point);
    CHECK(retained.point_hull.tree_domain == bsp::GoldSrcBspCollisionTreeDomain::node_leaf);
    CHECK(retained.point_hull.root.source_node_index == 0U);
    CHECK(retained.standing_hull.ordinal == bsp::GoldSrcBspCollisionHullOrdinal::standing_32x32x72);
    CHECK(retained.large_hull.ordinal == bsp::GoldSrcBspCollisionHullOrdinal::large_64_cube);
    CHECK(retained.duck_hull.ordinal == bsp::GoldSrcBspCollisionHullOrdinal::duck_32x32x36);
    CHECK(retained.standing_hull.tree_domain ==
          bsp::GoldSrcBspCollisionTreeDomain::clipnode_contents);
    CHECK(retained.large_hull.tree_domain ==
          bsp::GoldSrcBspCollisionTreeDomain::clipnode_contents);
    CHECK(retained.duck_hull.tree_domain ==
          bsp::GoldSrcBspCollisionTreeDomain::clipnode_contents);
    CHECK(retained.point_hull.compatibility_profile == source.compatibility_profile);
    CHECK(retained.standing_hull.compatibility_profile == source.compatibility_profile);
    CHECK(retained.large_hull.compatibility_profile == source.compatibility_profile);
    CHECK(retained.duck_hull.compatibility_profile == source.compatibility_profile);
    CHECK(retained.point_hull.evidence_profile == source.evidence_profile);
    CHECK(retained.standing_hull.evidence_profile == source.evidence_profile);
    CHECK(retained.large_hull.evidence_profile == source.evidence_profile);
    CHECK(retained.duck_hull.evidence_profile == source.evidence_profile);
    CHECK(std::get<bsp::GoldSrcBspCollisionSourceClipnodeReference>(retained.standing_hull.root)
              .source_clipnode_index == 0U);
    CHECK(std::get<bsp::GoldSrcBspCollisionSourceClipnodeReference>(retained.large_hull.root)
              .source_clipnode_index == 1U);
    CHECK(std::get<bsp::GoldSrcBspCollisionSourceClipnodeReference>(retained.duck_hull.root)
              .source_clipnode_index == 2U);

    CHECK(retained.point_hull.extents.minimum.x == 0.0F);
    CHECK(retained.point_hull.extents.minimum.y == 0.0F);
    CHECK(retained.point_hull.extents.minimum.z == 0.0F);
    CHECK(retained.point_hull.extents.maximum.x == 0.0F);
    CHECK(retained.point_hull.extents.maximum.y == 0.0F);
    CHECK(retained.point_hull.extents.maximum.z == 0.0F);
    CHECK(retained.standing_hull.extents.minimum.x == -16.0F);
    CHECK(retained.standing_hull.extents.minimum.y == -16.0F);
    CHECK(retained.standing_hull.extents.minimum.z == -36.0F);
    CHECK(retained.standing_hull.extents.maximum.x == 16.0F);
    CHECK(retained.standing_hull.extents.maximum.y == 16.0F);
    CHECK(retained.standing_hull.extents.maximum.z == 36.0F);
    CHECK(retained.large_hull.extents.minimum.x == -32.0F);
    CHECK(retained.large_hull.extents.minimum.y == -32.0F);
    CHECK(retained.large_hull.extents.minimum.z == -32.0F);
    CHECK(retained.large_hull.extents.maximum.x == 32.0F);
    CHECK(retained.large_hull.extents.maximum.y == 32.0F);
    CHECK(retained.large_hull.extents.maximum.z == 32.0F);
    CHECK(retained.duck_hull.extents.minimum.x == -16.0F);
    CHECK(retained.duck_hull.extents.minimum.y == -16.0F);
    CHECK(retained.duck_hull.extents.minimum.z == -18.0F);
    CHECK(retained.duck_hull.extents.maximum.x == 16.0F);
    CHECK(retained.duck_hull.extents.maximum.y == 16.0F);
    CHECK(retained.duck_hull.extents.maximum.z == 18.0F);
    CHECK(source.statistics.model_hull_root_count == 4U);
    CHECK(source.statistics.direct_terminal_root_count == 0U);
}

TEST_CASE("Every supported direct clip-hull terminal retains its exact code",
          "[goldsrc-bsp][collision-source][contents][direct-root]")
{
    for (std::int32_t contents = bsp::kGoldSrcBspMinimumContentsValue;
         contents <= bsp::kGoldSrcBspMaximumContentsValue; ++contents) {
        INFO(contents);
        fixture::SyntheticBspBuilder builder;
        auto model = fixture::SyntheticBspModel{};
        model.headnodes[1U] = contents;
        const auto result =
            bsp::GoldSrcBspParser::parse(builder.set_models(std::span{&model, 1U}).build());
        REQUIRE(result);
        const auto& root = result.document->collision_source.models[0U].standing_hull.root;
        REQUIRE(std::holds_alternative<bsp::GoldSrcContentsCode>(root));
        CHECK(std::get<bsp::GoldSrcContentsCode>(root).value == contents);
    }
}

TEST_CASE("Collision planes are the owning bit-consistent canonical decode",
          "[goldsrc-bsp][collision-source][planes][owning]")
{
    auto bytes = fixture::literal_minimal_goldsrc_bsp_v30();
    const auto expected_fingerprint = bsp::goldsrc_bsp_source_fingerprint(bytes);
    auto result = bsp::GoldSrcBspParser::parse(bytes);
    REQUIRE(result);
    REQUIRE(result.document);
    bytes.assign(bytes.size(), std::byte{0xA5});

    const auto& collision = result.document->collision_source;
    const auto& spatial = result.document->spatial_source;
    REQUIRE(collision.planes.size() == spatial.planes.size());
    REQUIRE(collision.planes.size() == 1U);
    CHECK(collision.planes[0U].source_plane_index == 0U);
    CHECK(std::bit_cast<std::uint32_t>(collision.planes[0U].normal.x) ==
          std::bit_cast<std::uint32_t>(spatial.planes[0U].normal.x));
    CHECK(std::bit_cast<std::uint32_t>(collision.planes[0U].normal.y) ==
          std::bit_cast<std::uint32_t>(spatial.planes[0U].normal.y));
    CHECK(std::bit_cast<std::uint32_t>(collision.planes[0U].normal.z) ==
          std::bit_cast<std::uint32_t>(spatial.planes[0U].normal.z));
    CHECK(std::bit_cast<std::uint64_t>(collision.planes[0U].distance) ==
          std::bit_cast<std::uint64_t>(spatial.planes[0U].distance));
    CHECK(collision.planes[0U].source_type == spatial.planes[0U].source_type);
    CHECK(collision.source_fingerprint == expected_fingerprint);
    CHECK(collision.source_bsp_version == bsp::kGoldSrcBspVersion);
    CHECK(collision.nodes[0U].source_node_index == 0U);
    REQUIRE(std::holds_alternative<bsp::GoldSrcBspCollisionSourceLeafReference>(
        collision.nodes[0U].children[0U]));
    CHECK(std::get<bsp::GoldSrcBspCollisionSourceLeafReference>(collision.nodes[0U].children[0U])
              .source_leaf_index == 0U);
    REQUIRE(std::holds_alternative<bsp::GoldSrcBspCollisionSourceLeafReference>(
        collision.nodes[0U].children[1U]));
    CHECK(std::get<bsp::GoldSrcBspCollisionSourceLeafReference>(collision.nodes[0U].children[1U])
              .source_leaf_index == 1U);
    CHECK(collision.leaves[0U].source_leaf_index == 0U);
    CHECK(collision.leaves[0U].contents.value == -2);
    CHECK(collision.leaves[1U].source_leaf_index == 1U);
    CHECK(collision.leaves[1U].contents.value == -1);
}

TEST_CASE("Collision-source publication is independent of brush geometry "
          "materialization",
          "[goldsrc-bsp][collision-source][options]")
{
    const auto result = bsp::GoldSrcBspParser::parse(fixture::literal_minimal_goldsrc_bsp_v30(), {},
                                                     bsp::GoldSrcBspParseOptions{false});
    REQUIRE(result);
    CHECK(result.document->brush_submodels.empty());
    CHECK(result.document->collision_source.models.size() == 1U);
    CHECK(result.document->collision_source.nodes.size() == 1U);
    CHECK(result.document->collision_source.leaves.size() == 2U);
}

TEST_CASE("Collision graph validation has an exact configurable step boundary",
          "[goldsrc-bsp][collision-source][limits][steps]")
{
    const auto bytes = fixture::literal_minimal_goldsrc_bsp_v30();
    const auto baseline = bsp::GoldSrcBspParser::parse(bytes);
    REQUIRE(baseline);
    const auto required_steps = static_cast<std::size_t>(
        baseline.document->collision_source.statistics.validation_step_count);
    REQUIRE(required_steps > 1U);

    auto limits = bsp::GoldSrcBspImportLimits{};
    limits.maximum_collision_validation_steps = required_steps;
    const auto exact = bsp::GoldSrcBspParser::parse(bytes, limits);
    REQUIRE(exact);
    CHECK(exact.document->collision_source.statistics.validation_step_count == required_steps);

    --limits.maximum_collision_validation_steps;
    const auto over = bsp::GoldSrcBspParser::parse(bytes, limits);
    REQUIRE_FALSE(over);
    REQUIRE_FALSE(over.document);
    REQUIRE(over.error);
    CHECK(over.error->code == bsp::GoldSrcBspErrorCode::collision_validation_limit_exceeded);
}

TEST_CASE("Collision record count limits accept exact and reject limit plus one",
          "[goldsrc-bsp][collision-source][limits][records]")
{
    fixture::SyntheticBspBuilder builder;
    const std::array clipnodes{fixture::SyntheticBspClipnode{}, fixture::SyntheticBspClipnode{}};
    auto model = fixture::SyntheticBspModel{};
    model.headnodes[1U] = 0;
    const auto bytes = builder.set_clipnodes(clipnodes).set_models(std::span{&model, 1U}).build();

    auto limits = bsp::GoldSrcBspImportLimits{};
    limits.maximum_clipnodes = 2U;
    REQUIRE(bsp::GoldSrcBspParser::parse(bytes, limits));
    limits.maximum_clipnodes = 1U;
    const auto over = bsp::GoldSrcBspParser::parse(bytes, limits);
    REQUIRE_FALSE(over);
    CHECK(over.error->code == bsp::GoldSrcBspErrorCode::count_limit_exceeded);
    CHECK(over.error->lump_id == bsp::GoldSrcBspLumpId::clipnodes);
}

TEST_CASE("Collision model count limits accept exact and reject limit plus one",
          "[goldsrc-bsp][collision-source][limits][models]")
{
    fixture::SyntheticBspBuilder builder;
    std::array models{fixture::SyntheticBspModel{}, fixture::SyntheticBspModel{}};
    models[1U].first_face = 1;
    models[1U].face_count = 0;
    models[1U].visibility_leaf_count = 0;
    const auto bytes = builder.set_models(models).build();
    const auto options = bsp::GoldSrcBspParseOptions{false};

    auto limits = bsp::GoldSrcBspImportLimits{};
    limits.maximum_models = 2U;
    REQUIRE(bsp::GoldSrcBspParser::parse(bytes, limits, options));
    limits.maximum_models = 1U;
    const auto over = bsp::GoldSrcBspParser::parse(bytes, limits, options);
    REQUIRE_FALSE(over);
    REQUIRE(over.error);
    CHECK(over.error->code == bsp::GoldSrcBspErrorCode::count_limit_exceeded);
    CHECK(over.error->lump_id == bsp::GoldSrcBspLumpId::models);
}

} // namespace
