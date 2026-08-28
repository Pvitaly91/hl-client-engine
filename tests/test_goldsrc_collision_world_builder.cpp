#include <catch2/catch_test_macros.hpp>

#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>

#include <variant>

namespace {

namespace bsp = hlclient::goldsrc::bsp;
namespace collision = hlclient::collision;
namespace goldsrc_collision = hlclient::goldsrc::collision;

[[nodiscard]] bsp::GoldSrcBspCollisionSource make_source()
{
    bsp::GoldSrcBspCollisionSource source;
    source.planes.push_back(bsp::GoldSrcBspCollisionSourcePlane{
        0U,
        {1.0F, 0.0F, 0.0F},
        0.0,
        0,
    });
    source.leaves.push_back(bsp::GoldSrcBspCollisionSourceLeaf{
        0U,
        bsp::GoldSrcContentsCode{-1},
        {{0.0F, 0.0F, 0.0F}, {16.0F, 16.0F, 16.0F}},
    });
    source.leaves.push_back(bsp::GoldSrcBspCollisionSourceLeaf{
        1U,
        bsp::GoldSrcContentsCode{-2},
        {{-16.0F, -16.0F, -16.0F}, {0.0F, 16.0F, 16.0F}},
    });
    source.nodes.push_back(bsp::GoldSrcBspCollisionSourceNode{
        0U,
        0U,
        {bsp::GoldSrcBspCollisionSourceLeafReference{0U},
         bsp::GoldSrcBspCollisionSourceLeafReference{1U}},
        {{-16.0F, -16.0F, -16.0F}, {16.0F, 16.0F, 16.0F}},
        0U,
        1U,
    });
    source.clipnodes.push_back(bsp::GoldSrcBspCollisionSourceClipnode{
        0U,
        0U,
        {bsp::GoldSrcContentsCode{-1}, bsp::GoldSrcContentsCode{-2}},
    });

    bsp::GoldSrcBspCollisionSourceModel model;
    model.source_model_index = 0U;
    model.source_bounds =
        {{-16.0F, -16.0F, -16.0F}, {16.0F, 16.0F, 16.0F}};
    model.point_hull.root = bsp::GoldSrcBspCollisionSourceNodeReference{0U};
    model.standing_hull.root =
        bsp::GoldSrcBspCollisionSourceClipnodeReference{0U};
    model.large_hull.ordinal =
        bsp::GoldSrcBspCollisionHullOrdinal::large_64_cube;
    model.large_hull.extents = bsp::kGoldSrcBspLargeHullExtents;
    model.large_hull.root =
        bsp::GoldSrcBspCollisionSourceClipnodeReference{0U};
    model.duck_hull.ordinal =
        bsp::GoldSrcBspCollisionHullOrdinal::duck_32x32x36;
    model.duck_hull.extents = bsp::kGoldSrcBspDuckHullExtents;
    model.duck_hull.root =
        bsp::GoldSrcBspCollisionSourceClipnodeReference{0U};
    model.source_face_count = 1U;
    source.models.push_back(model);
    source.statistics = bsp::GoldSrcBspCollisionSourceStatistics{
        1U, 1U, 2U, 1U, 1U, 1U, 0U, 1U, 0U,
        4U, 4U, 0U, 0U, 4U, 0U, 2U, 8U,
    };
    return source;
}

} // namespace

TEST_CASE("GoldSrc collision builder publishes the exact four-hull package")
{
    const auto built =
        goldsrc_collision::GoldSrcCollisionWorldBuilder::build(make_source());

    REQUIRE(built);
    REQUIRE(built.package);
    CHECK(built.package->planes().size() == 1U);
    CHECK(built.package->nodes().size() == 1U);
    CHECK(built.package->leaves().size() == 2U);
    CHECK(built.package->clipnodes().size() == 1U);
    REQUIRE(built.package->models().size() == 1U);
    const auto& model = built.package->models().front();
    REQUIRE(model.hull(collision::CollisionHullOrdinal::point));
    REQUIRE(model.hull(
        collision::CollisionHullOrdinal::standing_32x32x72));
    REQUIRE(model.hull(collision::CollisionHullOrdinal::large_64_cube));
    REQUIRE(model.hull(collision::CollisionHullOrdinal::duck_32x32x36));
    CHECK(model.hulls[0U].domain ==
          collision::CollisionHullTreeDomain::node_leaf);
    CHECK(model.hulls[1U].domain ==
          collision::CollisionHullTreeDomain::clipnode);
    CHECK(model.hulls[1U].profile.clip_mins.x == -16.0F);
    CHECK(model.hulls[1U].profile.clip_maxs.z == 36.0F);
    CHECK(model.hulls[2U].profile.clip_mins.x == -32.0F);
    CHECK(model.hulls[3U].profile.clip_mins.z == -18.0F);
}

TEST_CASE("GoldSrc collision builder supports validated direct terminals")
{
    auto source = make_source();
    source.models[0U].standing_hull.root = bsp::GoldSrcContentsCode{-1};
    source.statistics.direct_terminal_root_count = 99U;

    const auto built =
        goldsrc_collision::GoldSrcCollisionWorldBuilder::build(source);

    REQUIRE(built);
    const auto* hull = built.package->models()[0U].hull(
        collision::CollisionHullOrdinal::standing_32x32x72);
    REQUIRE(hull);
    CHECK(hull->root.kind == collision::CollisionHullRootKind::terminal);
    CHECK(hull->root.terminal.category ==
          collision::CollisionContentsCategory::empty);
    CHECK(built.package->statistics().direct_terminal_root_count == 1U);
}

TEST_CASE("GoldSrc collision builder rejects unsupported contents transactionally")
{
    auto source = make_source();
    source.clipnodes[0U].children[1U] = bsp::GoldSrcContentsCode{-16};

    const auto built =
        goldsrc_collision::GoldSrcCollisionWorldBuilder::build(source);

    CHECK_FALSE(built);
    CHECK_FALSE(built.package);
    REQUIRE(built.error);
    CHECK(built.error->code ==
          goldsrc_collision::CollisionWorldBuildErrorCode::invalid_contents);
}

TEST_CASE("GoldSrc collision builder detects node and clipnode cycles")
{
    SECTION("node self-cycle")
    {
        auto source = make_source();
        source.nodes[0U].children[0U] =
            bsp::GoldSrcBspCollisionSourceNodeReference{0U};
        const auto built =
            goldsrc_collision::GoldSrcCollisionWorldBuilder::build(source);
        CHECK_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code ==
              goldsrc_collision::CollisionWorldBuildErrorCode::cycle_detected);
    }

    SECTION("clipnode self-cycle")
    {
        auto source = make_source();
        source.clipnodes[0U].children[0U] =
            bsp::GoldSrcBspCollisionSourceClipnodeReference{0U};
        const auto built =
            goldsrc_collision::GoldSrcCollisionWorldBuilder::build(source);
        CHECK_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code ==
              goldsrc_collision::CollisionWorldBuildErrorCode::cycle_detected);
    }
}

TEST_CASE("GoldSrc collision builder validates globally unreachable records")
{
    SECTION("unreachable node cycle")
    {
        auto source = make_source();
        auto unreachable = source.nodes.front();
        unreachable.source_node_index = 1U;
        unreachable.children[0U] =
            bsp::GoldSrcBspCollisionSourceNodeReference{1U};
        source.nodes.push_back(unreachable);

        const auto built =
            goldsrc_collision::GoldSrcCollisionWorldBuilder::build(source);
        CHECK_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code ==
              goldsrc_collision::CollisionWorldBuildErrorCode::cycle_detected);
    }

    SECTION("unreachable clipnode cycle")
    {
        auto source = make_source();
        auto unreachable = source.clipnodes.front();
        unreachable.source_clipnode_index = 1U;
        unreachable.children[0U] =
            bsp::GoldSrcBspCollisionSourceClipnodeReference{1U};
        source.clipnodes.push_back(unreachable);

        const auto built =
            goldsrc_collision::GoldSrcCollisionWorldBuilder::build(source);
        CHECK_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code ==
              goldsrc_collision::CollisionWorldBuildErrorCode::cycle_detected);
    }

    SECTION("unreachable node reference")
    {
        auto source = make_source();
        auto unreachable = source.nodes.front();
        unreachable.source_node_index = 1U;
        unreachable.children[0U] =
            bsp::GoldSrcBspCollisionSourceNodeReference{2U};
        source.nodes.push_back(unreachable);

        const auto built =
            goldsrc_collision::GoldSrcCollisionWorldBuilder::build(source);
        CHECK_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code ==
              goldsrc_collision::CollisionWorldBuildErrorCode::
                  invalid_node_reference);
    }

    SECTION("unreachable leaf reference")
    {
        auto source = make_source();
        auto unreachable = source.nodes.front();
        unreachable.source_node_index = 1U;
        unreachable.children[0U] =
            bsp::GoldSrcBspCollisionSourceLeafReference{2U};
        source.nodes.push_back(unreachable);

        const auto built =
            goldsrc_collision::GoldSrcCollisionWorldBuilder::build(source);
        CHECK_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code ==
              goldsrc_collision::CollisionWorldBuildErrorCode::
                  invalid_leaf_reference);
    }

    SECTION("unreachable clipnode reference")
    {
        auto source = make_source();
        auto unreachable = source.clipnodes.front();
        unreachable.source_clipnode_index = 1U;
        unreachable.children[0U] =
            bsp::GoldSrcBspCollisionSourceClipnodeReference{2U};
        source.clipnodes.push_back(unreachable);

        const auto built =
            goldsrc_collision::GoldSrcCollisionWorldBuilder::build(source);
        CHECK_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code ==
              goldsrc_collision::CollisionWorldBuildErrorCode::
                  invalid_clipnode_reference);
    }
}

TEST_CASE("GoldSrc collision builder requires canonical record identities")
{
    SECTION("plane identity")
    {
        auto source = make_source();
        source.planes[0U].source_plane_index = 1U;
        const auto built =
            goldsrc_collision::GoldSrcCollisionWorldBuilder::build(source);
        CHECK_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code ==
              goldsrc_collision::CollisionWorldBuildErrorCode::invalid_plane);
    }

    SECTION("plane type")
    {
        auto source = make_source();
        source.planes[0U].source_type = 6;
        const auto built =
            goldsrc_collision::GoldSrcCollisionWorldBuilder::build(source);
        CHECK_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code ==
              goldsrc_collision::CollisionWorldBuildErrorCode::invalid_plane);
    }

    SECTION("leaf identity")
    {
        auto source = make_source();
        source.leaves[1U].source_leaf_index = 0U;
        const auto built =
            goldsrc_collision::GoldSrcCollisionWorldBuilder::build(source);
        CHECK_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code ==
              goldsrc_collision::CollisionWorldBuildErrorCode::
                  invalid_leaf_reference);
    }

    SECTION("node identity")
    {
        auto source = make_source();
        source.nodes[0U].source_node_index = 1U;
        const auto built =
            goldsrc_collision::GoldSrcCollisionWorldBuilder::build(source);
        CHECK_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code ==
              goldsrc_collision::CollisionWorldBuildErrorCode::
                  invalid_node_reference);
    }

    SECTION("clipnode identity")
    {
        auto source = make_source();
        source.clipnodes[0U].source_clipnode_index = 1U;
        const auto built =
            goldsrc_collision::GoldSrcCollisionWorldBuilder::build(source);
        CHECK_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code ==
              goldsrc_collision::CollisionWorldBuildErrorCode::
                  invalid_clipnode_reference);
    }

    SECTION("model identity")
    {
        auto source = make_source();
        source.models[0U].source_model_index = 1U;
        const auto built =
            goldsrc_collision::GoldSrcCollisionWorldBuilder::build(source);
        CHECK_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code ==
              goldsrc_collision::CollisionWorldBuildErrorCode::invalid_model);
    }
}

TEST_CASE("GoldSrc collision builder recomputes package statistics")
{
    auto source = make_source();
    source.statistics = {};

    const auto built =
        goldsrc_collision::GoldSrcCollisionWorldBuilder::build(source);

    REQUIRE(built);
    const auto& statistics = built.package->statistics();
    CHECK(statistics.plane_count == 1U);
    CHECK(statistics.node_count == 1U);
    CHECK(statistics.leaf_count == 2U);
    CHECK(statistics.clipnode_count == 1U);
    CHECK(statistics.model_count == 1U);
    CHECK(statistics.reachable_hull0_nodes == 1U);
    CHECK(statistics.reachable_clipnodes == 1U);
    CHECK(statistics.unreachable_clipnodes == 0U);
    CHECK(statistics.model_hull_root_count == 4U);
    CHECK(statistics.direct_terminal_root_count == 0U);
    CHECK(statistics.maximum_tree_depth == 1U);
}

TEST_CASE("GoldSrc collision builder recomputes global reachability statistics")
{
    auto source = make_source();
    auto unreachable_node = source.nodes.front();
    unreachable_node.source_node_index = 1U;
    source.nodes.push_back(unreachable_node);
    auto unreachable_clipnode = source.clipnodes.front();
    unreachable_clipnode.source_clipnode_index = 1U;
    source.clipnodes.push_back(unreachable_clipnode);
    source.statistics = {};

    const auto built =
        goldsrc_collision::GoldSrcCollisionWorldBuilder::build(source);

    REQUIRE(built);
    const auto& statistics = built.package->statistics();
    CHECK(statistics.node_count == 2U);
    CHECK(statistics.reachable_hull0_nodes == 1U);
    CHECK(statistics.clipnode_count == 2U);
    CHECK(statistics.reachable_clipnodes == 1U);
    CHECK(statistics.unreachable_clipnodes == 1U);
    CHECK(statistics.maximum_tree_depth == 1U);
}

TEST_CASE("GoldSrc collision builder enforces exact and limit-plus-one counts")
{
    auto source = make_source();
    auto limits = goldsrc_collision::GoldSrcCollisionBuildLimits{};
    limits.maximum_planes = source.planes.size();
    CHECK(goldsrc_collision::GoldSrcCollisionWorldBuilder::build(
        source, limits));

    source.planes.push_back(source.planes.front());
    source.planes.back().source_plane_index = 1U;
    const auto built =
        goldsrc_collision::GoldSrcCollisionWorldBuilder::build(source, limits);
    CHECK_FALSE(built);
    REQUIRE(built.error);
    CHECK(built.error->code ==
          goldsrc_collision::CollisionWorldBuildErrorCode::count_limit_exceeded);
}

TEST_CASE("GoldSrc collision builder rejects package memory limit transactionally")
{
    const auto source = make_source();
    auto limits = goldsrc_collision::GoldSrcCollisionBuildLimits{};
    limits.maximum_collision_bytes = 1U;

    const auto built =
        goldsrc_collision::GoldSrcCollisionWorldBuilder::build(source, limits);

    CHECK_FALSE(built);
    CHECK_FALSE(built.package);
    REQUIRE(built.error);
    CHECK(built.error->code == goldsrc_collision::CollisionWorldBuildErrorCode::
        memory_limit_exceeded);
}
