#include <hlclient/collision/collision_world_query.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace collision = hlclient::collision;

[[nodiscard]] collision::CollisionContents contents(const std::int32_t raw)
{
    const auto decoded = collision::decode_goldsrc_contents({raw});
    REQUIRE(decoded);
    return *decoded;
}

[[nodiscard]] std::array<collision::CollisionHull, 4U> hulls()
{
    std::array<collision::CollisionHull, 4U> output{};
    for (std::size_t index = 0U; index < output.size(); ++index) {
        const auto ordinal = collision::collision_hull_ordinal(index);
        REQUIRE(ordinal);
        const auto profile = collision::standard_collision_hull_profile(*ordinal);
        REQUIRE(profile);
        output[index] = {
            *ordinal,
            index == 0U ? collision::CollisionHullTreeDomain::node_leaf
                        : collision::CollisionHullTreeDomain::clipnode,
            collision::CollisionHullRoot{
                index == 0U ? collision::CollisionHullRootKind::node
                            : collision::CollisionHullRootKind::clipnode,
                0U,
                contents(-1),
            },
            *profile,
        };
    }
    return output;
}

[[nodiscard]] collision::CollisionModel model()
{
    return {
        0U,
        {},
        assets::WorldBounds{{-128.0F, -128.0F, -128.0F},
            {128.0F, 128.0F, 128.0F}},
        0U,
        1U,
        hulls(),
    };
}

[[nodiscard]] std::shared_ptr<const collision::CollisionWorldPackage>
single_plane_package(
    const std::int32_t front_contents = -1,
    const std::int32_t back_contents = -2)
{
    return std::make_shared<const collision::CollisionWorldPackage>(
        std::vector<collision::CollisionPlane>{
            {{1.0F, 0.0F, 0.0F}, 0.0, 42U, 0}},
        std::vector<collision::CollisionNode>{
            {0U,
                {collision::CollisionNodeChild{
                     collision::CollisionNodeChildKind::leaf, 1U},
                    collision::CollisionNodeChild{
                        collision::CollisionNodeChildKind::leaf, 0U}}}},
        std::vector<collision::CollisionLeaf>{
            {0U, contents(back_contents)},
            {1U, contents(front_contents)}},
        std::vector<collision::CollisionClipnode>{
            {0U,
                {collision::CollisionClipnodeChild{
                     collision::CollisionClipnodeChildKind::terminal,
                     0U,
                     contents(front_contents)},
                    collision::CollisionClipnodeChild{
                        collision::CollisionClipnodeChildKind::terminal,
                        0U,
                        contents(back_contents)}}}},
        std::vector<collision::CollisionModel>{model()});
}

[[nodiscard]] std::shared_ptr<const collision::CollisionWorldPackage>
node_package(
    std::vector<collision::CollisionPlane> planes,
    std::vector<collision::CollisionNode> nodes,
    std::vector<collision::CollisionLeaf> leaves)
{
    return std::make_shared<const collision::CollisionWorldPackage>(
        std::move(planes),
        std::move(nodes),
        std::move(leaves),
        std::vector<collision::CollisionClipnode>{
            {0U,
                {collision::CollisionClipnodeChild{
                     collision::CollisionClipnodeChildKind::terminal,
                     0U,
                     contents(-1)},
                    collision::CollisionClipnodeChild{
                        collision::CollisionClipnodeChildKind::terminal,
                        0U,
                        contents(-2)}}}},
        std::vector<collision::CollisionModel>{model()});
}

[[nodiscard]] collision::CollisionTraceRequest trace_request(
    const assets::AssetVector3 start,
    const assets::AssetVector3 end,
    const collision::CollisionHullOrdinal hull =
        collision::CollisionHullOrdinal::point)
{
    collision::CollisionTraceRequest output;
    output.start = start;
    output.end = end;
    output.hull = hull;
    return output;
}

void check_fraction_and_end(
    const collision::CollisionTraceResult& result,
    const double fraction,
    const assets::AssetVector3 expected)
{
    CHECK(result.trace_profile == collision::CollisionTraceCompatibilityProfile::
            project_deterministic_bsp_hull_trace_v1);
    CHECK(result.trace_evidence_profile ==
        collision::CollisionTraceEvidenceProfile::
            public_bsp_structure_and_independent_fixtures);
    CHECK(result.fraction == Catch::Approx(fraction).margin(1.0e-12));
    CHECK(result.end_position.x == Catch::Approx(expected.x).margin(1.0e-6F));
    CHECK(result.end_position.y == Catch::Approx(expected.y).margin(1.0e-6F));
    CHECK(result.end_position.z == Catch::Approx(expected.z).margin(1.0e-6F));
}

TEST_CASE("Trace fixtures cover convex box slab empty pocket and crossings",
    "[collision][trace][fixtures]")
{
    collision::CollisionQueryScratch scratch;

    SECTION("convex box")
    {
        collision::CollisionWorldQuery query{node_package(
            {{{1.0F, 0.0F, 0.0F}, 1.0, 100U, 0},
                {{1.0F, 0.0F, 0.0F}, -1.0, 101U, 0},
                {{0.0F, 1.0F, 0.0F}, 1.0, 102U, 1},
                {{0.0F, 1.0F, 0.0F}, -1.0, 103U, 1},
                {{0.0F, 0.0F, 1.0F}, 1.0, 104U, 2},
                {{0.0F, 0.0F, 1.0F}, -1.0, 105U, 2}},
            {{0U, {{{collision::CollisionNodeChildKind::leaf, 0U},
                       {collision::CollisionNodeChildKind::node, 1U}}}},
                {1U, {{{collision::CollisionNodeChildKind::node, 2U},
                         {collision::CollisionNodeChildKind::leaf, 0U}}}},
                {2U, {{{collision::CollisionNodeChildKind::leaf, 0U},
                         {collision::CollisionNodeChildKind::node, 3U}}}},
                {3U, {{{collision::CollisionNodeChildKind::node, 4U},
                         {collision::CollisionNodeChildKind::leaf, 0U}}}},
                {4U, {{{collision::CollisionNodeChildKind::leaf, 0U},
                         {collision::CollisionNodeChildKind::node, 5U}}}},
                {5U, {{{collision::CollisionNodeChildKind::leaf, 1U},
                         {collision::CollisionNodeChildKind::leaf, 0U}}}}},
            {{0U, contents(-1)}, {1U, contents(-2)}})};
        const auto traced = query.trace_line(
            trace_request({2.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}), scratch);
        REQUIRE(traced);
        check_fraction_and_end(*traced.result, 0.5, {1.0F, 0.0F, 0.0F});
        REQUIRE(traced.result->collision_plane);
        CHECK(traced.result->collision_plane->source_plane_index == 100U);
    }

    SECTION("slab")
    {
        collision::CollisionWorldQuery query{node_package(
            {{{1.0F, 0.0F, 0.0F}, 1.0, 110U, 0},
                {{1.0F, 0.0F, 0.0F}, -1.0, 111U, 0}},
            {{0U, {{{collision::CollisionNodeChildKind::leaf, 0U},
                       {collision::CollisionNodeChildKind::node, 1U}}}},
                {1U, {{{collision::CollisionNodeChildKind::leaf, 1U},
                         {collision::CollisionNodeChildKind::leaf, 0U}}}}},
            {{0U, contents(-1)}, {1U, contents(-2)}})};
        const auto traced = query.trace_line(
            trace_request({2.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}), scratch);
        REQUIRE(traced);
        check_fraction_and_end(*traced.result, 0.5, {1.0F, 0.0F, 0.0F});
    }

    SECTION("empty pocket")
    {
        collision::CollisionWorldQuery query{node_package(
            {{{1.0F, 0.0F, 0.0F}, 1.0, 120U, 0},
                {{1.0F, 0.0F, 0.0F}, -1.0, 121U, 0}},
            {{0U, {{{collision::CollisionNodeChildKind::leaf, 1U},
                       {collision::CollisionNodeChildKind::node, 1U}}}},
                {1U, {{{collision::CollisionNodeChildKind::leaf, 0U},
                         {collision::CollisionNodeChildKind::leaf, 1U}}}}},
            {{0U, contents(-1)}, {1U, contents(-2)}})};
        const auto traced = query.trace_line(
            trace_request({0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F}), scratch);
        REQUIRE(traced);
        check_fraction_and_end(*traced.result, 0.5, {1.0F, 0.0F, 0.0F});
    }

    SECTION("multiple crossings stop at first solid interval")
    {
        collision::CollisionWorldQuery query{node_package(
            {{{1.0F, 0.0F, 0.0F}, 3.0, 130U, 0},
                {{1.0F, 0.0F, 0.0F}, 1.0, 131U, 0},
                {{1.0F, 0.0F, 0.0F}, -1.0, 132U, 0},
                {{1.0F, 0.0F, 0.0F}, -3.0, 133U, 0}},
            {{0U, {{{collision::CollisionNodeChildKind::leaf, 0U},
                       {collision::CollisionNodeChildKind::node, 1U}}}},
                {1U, {{{collision::CollisionNodeChildKind::leaf, 1U},
                         {collision::CollisionNodeChildKind::node, 2U}}}},
                {2U, {{{collision::CollisionNodeChildKind::leaf, 0U},
                         {collision::CollisionNodeChildKind::node, 3U}}}},
                {3U, {{{collision::CollisionNodeChildKind::leaf, 1U},
                         {collision::CollisionNodeChildKind::leaf, 0U}}}}},
            {{0U, contents(-1)}, {1U, contents(-2)}})};
        const auto traced = query.trace_line(
            trace_request({4.0F, 0.0F, 0.0F}, {-4.0F, 0.0F, 0.0F}), scratch);
        REQUIRE(traced);
        check_fraction_and_end(*traced.result, 0.125, {3.0F, 0.0F, 0.0F});
        REQUIRE(traced.result->collision_plane);
        CHECK(traced.result->collision_plane->source_plane_index == 130U);
        CHECK(traced.result->traversal_statistics.fraction_split_count == 4U);
    }
}

TEST_CASE("Iterative hull trace reports empty to solid impact exactly",
    "[collision][trace][basic]")
{
    collision::CollisionWorldQuery query{single_plane_package()};
    collision::CollisionQueryScratch scratch;
    const auto traced = query.trace_line(
        trace_request({1.0F, 2.0F, 3.0F}, {-1.0F, 4.0F, 5.0F}), scratch);
    REQUIRE(traced);
    const auto& result = *traced.result;
    CHECK_FALSE(result.start_solid);
    CHECK_FALSE(result.all_solid);
    CHECK(result.in_open);
    CHECK_FALSE(result.in_liquid);
    check_fraction_and_end(result, 0.5, {0.0F, 3.0F, 4.0F});
    REQUIRE(result.collision_plane);
    CHECK(result.collision_plane->normal.x == Catch::Approx(1.0F));
    CHECK(result.collision_plane->normal.y == Catch::Approx(0.0F));
    CHECK(result.collision_plane->distance == Catch::Approx(0.0));
    CHECK(result.collision_plane->source_plane_index == 42U);
    CHECK(result.collision_plane->orientation ==
        collision::CollisionPlaneOrientation::source);
    REQUIRE(result.hit);
    CHECK(result.hit->kind == collision::CollisionTraceHitKind::world);
    REQUIRE(result.blocking_contents);
    CHECK(result.blocking_contents->source.raw == -2);
}

TEST_CASE("Trace startsolid and allsolid remain distinct",
    "[collision][trace][flags]")
{
    collision::CollisionWorldQuery query{single_plane_package()};
    collision::CollisionQueryScratch scratch;

    const auto leaves_solid = query.trace_line(
        trace_request({-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}), scratch);
    REQUIRE(leaves_solid);
    CHECK(leaves_solid.result->start_solid);
    CHECK_FALSE(leaves_solid.result->all_solid);
    CHECK(leaves_solid.result->fraction == 1.0);
    CHECK(leaves_solid.result->end_position.x == 1.0F);
    CHECK_FALSE(leaves_solid.result->collision_plane);
    CHECK_FALSE(leaves_solid.result->hit);

    const auto remains_solid = query.trace_line(
        trace_request({-1.0F, 0.0F, 0.0F}, {-2.0F, 0.0F, 0.0F}), scratch);
    REQUIRE(remains_solid);
    CHECK(remains_solid.result->start_solid);
    CHECK(remains_solid.result->all_solid);
    check_fraction_and_end(*remains_solid.result, 0.0, {-1.0F, 0.0F, 0.0F});
    CHECK_FALSE(remains_solid.result->collision_plane);
    REQUIRE(remains_solid.result->hit);

    const auto near_plane_solid = query.trace_line(
        trace_request(
            {-1.0e-7F, 0.0F, 0.0F}, {-2.0e-7F, 0.0F, 0.0F}),
        scratch);
    REQUIRE(near_plane_solid);
    CHECK(near_plane_solid.result->start_solid);
    CHECK(near_plane_solid.result->all_solid);
    check_fraction_and_end(
        *near_plane_solid.result, 0.0, {-1.0e-7F, 0.0F, 0.0F});

    const auto remains_empty = query.trace_line(
        trace_request({1.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F}), scratch);
    REQUIRE(remains_empty);
    CHECK_FALSE(remains_empty.result->start_solid);
    CHECK_FALSE(remains_empty.result->all_solid);
    check_fraction_and_end(*remains_empty.result, 1.0, {2.0F, 0.0F, 0.0F});
    CHECK_FALSE(remains_empty.result->collision_plane);
}

TEST_CASE("Exact boundary and independent literal fractions are deterministic",
    "[collision][trace][fraction][tolerance]")
{
    collision::CollisionWorldQuery query{single_plane_package()};
    collision::CollisionQueryScratch scratch;

    const auto immediate = query.trace_line(
        trace_request({0.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}), scratch);
    REQUIRE(immediate);
    CHECK_FALSE(immediate.result->start_solid);
    check_fraction_and_end(*immediate.result, 0.0, {0.0F, 0.0F, 0.0F});

    const auto quarter = query.trace_line(
        trace_request({1.0F, 0.0F, 0.0F}, {-3.0F, 0.0F, 0.0F}), scratch);
    REQUIRE(quarter);
    check_fraction_and_end(*quarter.result, 0.25, {0.0F, 0.0F, 0.0F});

    const auto three_quarters = query.trace_line(
        trace_request({3.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}), scratch);
    REQUIRE(three_quarters);
    check_fraction_and_end(*three_quarters.result,
        0.75,
        {0.0F, 0.0F, 0.0F});

    const auto tolerance_boundary = query.trace_line(
        trace_request({1.0e-7F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}), scratch);
    REQUIRE(tolerance_boundary);
    check_fraction_and_end(*tolerance_boundary.result,
        0.0,
        {1.0e-7F, 0.0F, 0.0F});

    const auto entirely_inside_tolerance_band = query.trace_line(
        trace_request(
            {1.0e-7F, 0.0F, 0.0F}, {-1.0e-7F, 0.0F, 0.0F}),
        scratch);
    REQUIRE(entirely_inside_tolerance_band);
    check_fraction_and_end(*entirely_inside_tolerance_band.result,
        0.5,
        {0.0F, 0.0F, 0.0F});

    collision::CollisionWorldQuery endpoint_query{
        single_plane_package(-2, -1)};
    const auto endpoint_only = endpoint_query.trace_line(
        trace_request({-1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}),
        scratch);
    REQUIRE(endpoint_only);
    check_fraction_and_end(
        *endpoint_only.result, 1.0, {0.0F, 0.0F, 0.0F});
    CHECK(endpoint_only.result->end_contents.category ==
        collision::CollisionContentsCategory::solid);
    CHECK_FALSE(endpoint_only.result->collision_plane);
    CHECK_FALSE(endpoint_only.result->hit);
}

TEST_CASE("Reverse crossing inverts source plane against motion",
    "[collision][trace][plane][reverse]")
{
    collision::CollisionWorldQuery query{single_plane_package(-2, -1)};
    collision::CollisionQueryScratch scratch;
    const auto traced = query.trace_line(
        trace_request({-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}), scratch);
    REQUIRE(traced);
    check_fraction_and_end(*traced.result, 0.5, {0.0F, 0.0F, 0.0F});
    REQUIRE(traced.result->collision_plane);
    CHECK(traced.result->collision_plane->normal.x == Catch::Approx(-1.0F));
    CHECK(traced.result->collision_plane->orientation ==
        collision::CollisionPlaneOrientation::inverted_source);

    const auto near_boundary = query.trace_line(
        trace_request(
            {-1.0e-7F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}),
        scratch);
    REQUIRE(near_boundary);
    check_fraction_and_end(
        *near_boundary.result, 0.0, {-1.0e-7F, 0.0F, 0.0F});
    REQUIRE(near_boundary.result->collision_plane);
    CHECK(near_boundary.result->collision_plane->normal.x ==
        Catch::Approx(-1.0F));
    CHECK(near_boundary.result->collision_plane->orientation ==
        collision::CollisionPlaneOrientation::inverted_source);
}

TEST_CASE("Zero-length trace is a stationary contents test",
    "[collision][trace][zero-length]")
{
    collision::CollisionWorldQuery query{single_plane_package()};
    collision::CollisionQueryScratch scratch;

    const auto empty = query.trace_line(
        trace_request({1.0F, 2.0F, 3.0F}, {1.0F, 2.0F, 3.0F}), scratch);
    REQUIRE(empty);
    CHECK_FALSE(empty.result->start_solid);
    CHECK_FALSE(empty.result->all_solid);
    check_fraction_and_end(*empty.result, 1.0, {1.0F, 2.0F, 3.0F});

    const auto solid = query.trace_line(
        trace_request({-1.0F, 2.0F, 3.0F}, {-1.0F, 2.0F, 3.0F}), scratch);
    REQUIRE(solid);
    CHECK(solid.result->start_solid);
    CHECK(solid.result->all_solid);
    check_fraction_and_end(*solid.result, 0.0, {-1.0F, 2.0F, 3.0F});
    CHECK_FALSE(solid.result->collision_plane);
}

TEST_CASE("All standard hulls use their pre-expanded tree without extra offsets",
    "[collision][trace][hulls]")
{
    collision::CollisionWorldQuery query{single_plane_package()};
    collision::CollisionQueryScratch scratch;
    for (std::size_t index = 0U; index < collision::kCollisionHullCount; ++index) {
        const auto ordinal = collision::collision_hull_ordinal(index);
        REQUIRE(ordinal);
        const auto traced = query.trace_hull(trace_request(
            {1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}, *ordinal), scratch);
        REQUIRE(traced);
        check_fraction_and_end(*traced.result, 0.5, {0.0F, 0.0F, 0.0F});
    }
}

TEST_CASE("Trace tracks liquid intervals independently from blocking policy",
    "[collision][trace][liquid]")
{
    collision::CollisionWorldQuery query{single_plane_package(-3, -2)};
    collision::CollisionQueryScratch scratch;
    const auto traced = query.trace_line(
        trace_request({1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}), scratch);
    REQUIRE(traced);
    CHECK(traced.result->in_liquid);
    CHECK_FALSE(traced.result->start_solid);
    CHECK(traced.result->fraction == Catch::Approx(0.5));
}

TEST_CASE("Startsolid trace can leave and deterministically hit a later solid",
    "[collision][trace][startsolid][reentry]")
{
    const auto nested_package =
        std::make_shared<const collision::CollisionWorldPackage>(
            std::vector<collision::CollisionPlane>{
                {{1.0F, 0.0F, 0.0F}, 0.0, 10U, 0},
                {{1.0F, 0.0F, 0.0F}, -1.0, 11U, 0}},
            std::vector<collision::CollisionNode>{
                {0U,
                    {collision::CollisionNodeChild{
                         collision::CollisionNodeChildKind::leaf, 0U},
                        collision::CollisionNodeChild{
                            collision::CollisionNodeChildKind::node, 1U}}},
                {1U,
                    {collision::CollisionNodeChild{
                         collision::CollisionNodeChildKind::leaf, 1U},
                        collision::CollisionNodeChild{
                            collision::CollisionNodeChildKind::leaf, 0U}}}},
            std::vector<collision::CollisionLeaf>{
                {0U, contents(-2)}, {1U, contents(-1)}},
            std::vector<collision::CollisionClipnode>{
                {0U,
                    {collision::CollisionClipnodeChild{
                         collision::CollisionClipnodeChildKind::terminal,
                         0U,
                         contents(-1)},
                        collision::CollisionClipnodeChild{
                            collision::CollisionClipnodeChildKind::terminal,
                            0U,
                            contents(-2)}}}},
            std::vector<collision::CollisionModel>{model()});
    collision::CollisionWorldQuery query{nested_package};
    collision::CollisionQueryScratch scratch;
    const auto traced = query.trace_line(
        trace_request({0.5F, 0.0F, 0.0F}, {-2.0F, 0.0F, 0.0F}), scratch);
    REQUIRE(traced);
    CHECK(traced.result->start_solid);
    CHECK_FALSE(traced.result->all_solid);
    check_fraction_and_end(*traced.result, 0.6, {-1.0F, 0.0F, 0.0F});
    REQUIRE(traced.result->collision_plane);
    CHECK(traced.result->collision_plane->source_plane_index == 11U);
}

TEST_CASE("Iterative traversal permits shared DAGs and rejects active cycles",
    "[collision][trace][dag][security]")
{
    auto shared_model = model();
    const auto shared_package =
        std::make_shared<const collision::CollisionWorldPackage>(
            std::vector<collision::CollisionPlane>{
                {{1.0F, 0.0F, 0.0F}, 0.0, 0U, 0},
                {{0.0F, 1.0F, 0.0F}, 0.0, 1U, 1}},
            std::vector<collision::CollisionNode>{
                {0U,
                    {collision::CollisionNodeChild{
                         collision::CollisionNodeChildKind::node, 1U},
                        collision::CollisionNodeChild{
                            collision::CollisionNodeChildKind::node, 1U}}},
                {1U,
                    {collision::CollisionNodeChild{
                         collision::CollisionNodeChildKind::leaf, 1U},
                        collision::CollisionNodeChild{
                            collision::CollisionNodeChildKind::leaf, 0U}}}},
            std::vector<collision::CollisionLeaf>{
                {0U, contents(-2)}, {1U, contents(-1)}},
            std::vector<collision::CollisionClipnode>{
                {0U,
                    {collision::CollisionClipnodeChild{
                         collision::CollisionClipnodeChildKind::terminal,
                         0U,
                         contents(-1)},
                        collision::CollisionClipnodeChild{
                            collision::CollisionClipnodeChildKind::terminal,
                            0U,
                            contents(-2)}}}},
            std::vector<collision::CollisionModel>{std::move(shared_model)});
    collision::CollisionWorldQuery shared_query{shared_package};
    collision::CollisionQueryScratch scratch;
    const auto shared = shared_query.trace_line(
        trace_request({1.0F, 1.0F, 0.0F}, {-1.0F, 1.0F, 0.0F}), scratch);
    REQUIRE(shared);
    CHECK(shared.result->fraction == 1.0);

    auto cycle_model = model();
    const auto cycle_package =
        std::make_shared<const collision::CollisionWorldPackage>(
            std::vector<collision::CollisionPlane>{
                {{1.0F, 0.0F, 0.0F}, 0.0, 0U, 0}},
            std::vector<collision::CollisionNode>{
                {0U,
                    {collision::CollisionNodeChild{
                         collision::CollisionNodeChildKind::node, 0U},
                        collision::CollisionNodeChild{
                            collision::CollisionNodeChildKind::leaf, 0U}}}},
            std::vector<collision::CollisionLeaf>{
                {0U, contents(-2)}, {1U, contents(-1)}},
            std::vector<collision::CollisionClipnode>{
                {0U,
                    {collision::CollisionClipnodeChild{
                         collision::CollisionClipnodeChildKind::terminal,
                         0U,
                         contents(-1)},
                        collision::CollisionClipnodeChild{
                            collision::CollisionClipnodeChildKind::terminal,
                            0U,
                            contents(-2)}}}},
            std::vector<collision::CollisionModel>{std::move(cycle_model)});
    collision::CollisionWorldQuery cycle_query{cycle_package};
    const auto cycle = cycle_query.trace_line(
        trace_request({2.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}), scratch);
    REQUIRE_FALSE(cycle);
    CHECK(cycle.error->code == collision::CollisionQueryErrorCode::cycle_detected);
}

TEST_CASE("Trace rejects nonfinite input pending profiles and bounded work overflow",
    "[collision][trace][limits][security]")
{
    collision::CollisionWorldQuery query{single_plane_package()};
    collision::CollisionQueryScratch scratch;
    auto invalid = trace_request(
        {std::numeric_limits<float>::infinity(), 0.0F, 0.0F},
        {-1.0F, 0.0F, 0.0F});
    auto result = query.trace_line(invalid, scratch);
    REQUIRE_FALSE(result);
    CHECK(result.error->code == collision::CollisionQueryErrorCode::invalid_segment);

    invalid = trace_request({1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F});
    invalid.trace_profile = collision::CollisionTraceCompatibilityProfile::
        stock_engine_trace_behavior_evidence_pending;
    result = query.trace_line(invalid, scratch);
    REQUIRE_FALSE(result);
    CHECK(result.error->code ==
        collision::CollisionQueryErrorCode::unsupported_trace_profile);

    auto stack_limited =
        trace_request({1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F});
    stack_limited.limits.maximum_stack_entries = 1U;
    result = query.trace_line(stack_limited, scratch);
    REQUIRE_FALSE(result);
    CHECK(result.error->code ==
        collision::CollisionQueryErrorCode::stack_limit_exceeded);

    auto scratch_limited =
        trace_request({1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F});
    scratch_limited.limits.maximum_query_scratch_bytes = 1U;
    result = query.trace_line(scratch_limited, scratch);
    REQUIRE_FALSE(result);
    CHECK(result.error->code ==
        collision::CollisionQueryErrorCode::scratch_limit_exceeded);

    auto split_limited =
        trace_request({1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F});
    split_limited.limits.maximum_fraction_splits = 1U;
    const auto one_split = query.trace_line(split_limited, scratch);
    REQUIRE(one_split);
    CHECK(one_split.result->traversal_statistics.fraction_split_count == 1U);
}

TEST_CASE("Trace rejects malformed hull records and traversal step exhaustion",
    "[collision][trace][malformed][security]")
{
    collision::CollisionQueryScratch scratch;

    SECTION("invalid child")
    {
        collision::CollisionWorldQuery query{node_package(
            {{{1.0F, 0.0F, 0.0F}, 0.0, 200U, 0}},
            {{0U, {{{collision::CollisionNodeChildKind::node, 99U},
                       {collision::CollisionNodeChildKind::leaf, 0U}}}}},
            {{0U, contents(-1)}})};
        const auto traced = query.trace_line(
            trace_request({1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}), scratch);
        REQUIRE_FALSE(traced);
        REQUIRE(traced.error);
        CHECK(traced.error->code ==
            collision::CollisionQueryErrorCode::invalid_child);
    }

    SECTION("invalid plane index")
    {
        collision::CollisionWorldQuery query{node_package(
            {{{1.0F, 0.0F, 0.0F}, 0.0, 201U, 0}},
            {{1U, {{{collision::CollisionNodeChildKind::leaf, 0U},
                       {collision::CollisionNodeChildKind::leaf, 0U}}}}},
            {{0U, contents(-1)}})};
        const auto traced = query.trace_line(
            trace_request({1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}), scratch);
        REQUIRE_FALSE(traced);
        REQUIRE(traced.error);
        CHECK(traced.error->code ==
            collision::CollisionQueryErrorCode::invalid_plane);
    }

    SECTION("nonfinite plane")
    {
        collision::CollisionWorldQuery query{node_package(
            {{{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
                 0.0,
                 202U,
                 0}},
            {{0U, {{{collision::CollisionNodeChildKind::leaf, 0U},
                       {collision::CollisionNodeChildKind::leaf, 0U}}}}},
            {{0U, contents(-1)}})};
        const auto traced = query.trace_line(
            trace_request({1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}), scratch);
        REQUIRE_FALSE(traced);
        REQUIRE(traced.error);
        CHECK(traced.error->code ==
            collision::CollisionQueryErrorCode::invalid_plane);
    }

    SECTION("trace traversal step limit")
    {
        collision::CollisionWorldQuery query{single_plane_package()};
        auto request =
            trace_request({1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F});
        request.limits.maximum_traversal_steps = 1U;
        const auto traced = query.trace_line(request, scratch);
        REQUIRE_FALSE(traced);
        REQUIRE(traced.error);
        CHECK(traced.error->code == collision::CollisionQueryErrorCode::
            traversal_step_limit_exceeded);
    }
}

TEST_CASE("Finite float endpoints cannot overflow the internal motion vector",
    "[collision][trace][finite][security]")
{
    collision::CollisionWorldQuery query{single_plane_package()};
    collision::CollisionQueryScratch scratch;
    const auto maximum = std::numeric_limits<float>::max();
    const auto traced = query.trace_line(
        trace_request({maximum, 0.0F, 0.0F}, {-maximum, 0.0F, 0.0F}),
        scratch);
    REQUIRE(traced);
    CHECK(std::isfinite(traced.result->fraction));
    CHECK(traced.result->fraction == Catch::Approx(0.5));
    CHECK(std::isfinite(traced.result->end_position.x));
    REQUIRE(traced.result->collision_plane);
    CHECK(std::isfinite(traced.result->collision_plane->normal.x));
    CHECK(std::isfinite(traced.result->collision_plane->distance));
}

} // namespace
