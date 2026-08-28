#include <hlclient/collision/collision_world_query.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
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

[[nodiscard]] std::array<collision::CollisionHull, 4U> hulls(
    const bool direct_clip_terminal = false,
    const std::int32_t direct_contents = -1)
{
    std::array<collision::CollisionHull, 4U> output{};
    for (std::size_t index = 0U; index < output.size(); ++index) {
        const auto ordinal = collision::collision_hull_ordinal(index);
        REQUIRE(ordinal);
        const auto profile = collision::standard_collision_hull_profile(*ordinal);
        REQUIRE(profile);
        const bool direct = index != 0U && direct_clip_terminal;
        output[index] = {
            *ordinal,
            index == 0U ? collision::CollisionHullTreeDomain::node_leaf
                        : collision::CollisionHullTreeDomain::clipnode,
            collision::CollisionHullRoot{
                index == 0U
                    ? collision::CollisionHullRootKind::node
                    : (direct ? collision::CollisionHullRootKind::terminal
                              : collision::CollisionHullRootKind::clipnode),
                0U,
                contents(direct_contents),
            },
            *profile,
        };
    }
    return output;
}

[[nodiscard]] collision::CollisionModel model(
    std::array<collision::CollisionHull, 4U> model_hulls)
{
    return {
        0U,
        {},
        assets::WorldBounds{{-16.0F, -16.0F, -16.0F},
            {16.0F, 16.0F, 16.0F}},
        0U,
        1U,
        std::move(model_hulls),
    };
}

[[nodiscard]] std::shared_ptr<const collision::CollisionWorldPackage>
basic_package(const std::int32_t front_contents = -1)
{
    std::vector<collision::CollisionPlane> planes{
        {{1.0F, 0.0F, 0.0F}, 0.0, 100U, 0},
    };
    std::vector<collision::CollisionNode> nodes{
        {0U,
            {collision::CollisionNodeChild{
                 collision::CollisionNodeChildKind::leaf, 1U},
                collision::CollisionNodeChild{
                    collision::CollisionNodeChildKind::leaf, 0U}}},
    };
    std::vector<collision::CollisionLeaf> leaves{
        {0U, contents(-2)},
        {1U, contents(front_contents)},
    };
    std::vector<collision::CollisionClipnode> clipnodes{
        {0U,
            {collision::CollisionClipnodeChild{
                 collision::CollisionClipnodeChildKind::terminal,
                 0U,
                 contents(front_contents)},
                collision::CollisionClipnodeChild{
                    collision::CollisionClipnodeChildKind::terminal,
                    0U,
                    contents(-2)}}},
    };
    return std::make_shared<const collision::CollisionWorldPackage>(
        std::move(planes),
        std::move(nodes),
        std::move(leaves),
        std::move(clipnodes),
        std::vector<collision::CollisionModel>{model(hulls())});
}

[[nodiscard]] collision::CollisionPointContentsRequest request(
    const assets::AssetVector3 point,
    const collision::CollisionHullOrdinal hull =
        collision::CollisionHullOrdinal::point)
{
    collision::CollisionPointContentsRequest output;
    output.point = point;
    output.hull = hull;
    return output;
}

TEST_CASE("Point contents uses exact front back and plane-boundary policy",
    "[collision][point-contents]")
{
    collision::CollisionWorldQuery query{basic_package()};
    collision::CollisionQueryScratch scratch;

    const auto front = query.point_contents(request({1.0F, 0.0F, 0.0F}), scratch);
    REQUIRE(front);
    CHECK(front.result->contents.category ==
        collision::CollisionContentsCategory::empty);
    CHECK(front.result->contents.source.raw == -1);
    CHECK(front.result->traversal_depth == 1U);

    const auto back = query.point_contents(request({-1.0F, 0.0F, 0.0F}), scratch);
    REQUIRE(back);
    CHECK(back.result->contents.category ==
        collision::CollisionContentsCategory::solid);

    const auto tie = query.point_contents(request({0.0F, 0.0F, 0.0F}), scratch);
    REQUIRE(tie);
    CHECK(tie.result->contents.category ==
        collision::CollisionContentsCategory::empty);
}

TEST_CASE("All four exact compiler hulls answer point and position queries",
    "[collision][point-contents][hulls]")
{
    collision::CollisionWorldQuery query{basic_package()};
    collision::CollisionQueryScratch scratch;

    for (std::size_t index = 0U; index < collision::kCollisionHullCount; ++index) {
        const auto ordinal = collision::collision_hull_ordinal(index);
        REQUIRE(ordinal);
        const auto free = query.test_position(
            request({1.0F, 0.0F, 0.0F}, *ordinal), scratch);
        REQUIRE(free);
        CHECK(free.result->status == collision::CollisionPositionStatus::free);

        const auto blocked = query.test_position(
            request({-1.0F, 0.0F, 0.0F}, *ordinal), scratch);
        REQUIRE(blocked);
        CHECK(blocked.result->status ==
            collision::CollisionPositionStatus::blocking);
    }
}

TEST_CASE("Point contents follows nested planes without crossing tree domains",
    "[collision][point-contents][nested]")
{
    const auto nested_package =
        std::make_shared<const collision::CollisionWorldPackage>(
            std::vector<collision::CollisionPlane>{
                {{1.0F, 0.0F, 0.0F}, 0.0, 200U, 0},
                {{0.0F, 1.0F, 0.0F}, 0.0, 201U, 1}},
            std::vector<collision::CollisionNode>{
                {0U,
                    {collision::CollisionNodeChild{
                         collision::CollisionNodeChildKind::node, 1U},
                        collision::CollisionNodeChild{
                            collision::CollisionNodeChildKind::leaf, 0U}}},
                {1U,
                    {collision::CollisionNodeChild{
                         collision::CollisionNodeChildKind::leaf, 2U},
                        collision::CollisionNodeChild{
                            collision::CollisionNodeChildKind::leaf, 1U}}}},
            std::vector<collision::CollisionLeaf>{
                {0U, contents(-2)},
                {1U, contents(-1)},
                {2U, contents(-3)}},
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
            std::vector<collision::CollisionModel>{model(hulls())});
    collision::CollisionWorldQuery query{nested_package};
    collision::CollisionQueryScratch scratch;

    const auto water = query.point_contents(
        request({1.0F, 1.0F, 0.0F}), scratch);
    REQUIRE(water);
    CHECK(water.result->contents.category ==
        collision::CollisionContentsCategory::water);
    CHECK(water.result->traversal_depth == 2U);

    const auto empty = query.point_contents(
        request({1.0F, -1.0F, 0.0F}), scratch);
    REQUIRE(empty);
    CHECK(empty.result->contents.category ==
        collision::CollisionContentsCategory::empty);

    const auto solid = query.point_contents(
        request({-1.0F, 1.0F, 0.0F}), scratch);
    REQUIRE(solid);
    CHECK(solid.result->contents.category ==
        collision::CollisionContentsCategory::solid);
    CHECK(solid.result->traversal_depth == 1U);
}

TEST_CASE("Liquid contents remain nonblocking and direct roots remain typed",
    "[collision][point-contents][contents][direct-root]")
{
    collision::CollisionWorldQuery water_query{basic_package(-3)};
    collision::CollisionQueryScratch scratch;
    const auto water = water_query.test_position(
        request({1.0F, 0.0F, 0.0F}), scratch);
    REQUIRE(water);
    CHECK(water.result->contents.category ==
        collision::CollisionContentsCategory::water);
    CHECK(water.result->status == collision::CollisionPositionStatus::free);

    const auto direct_package =
        std::make_shared<const collision::CollisionWorldPackage>(
            std::vector<collision::CollisionPlane>{
                {{1.0F, 0.0F, 0.0F}, 0.0, 0U, 0}},
            std::vector<collision::CollisionNode>{
                {0U,
                    {collision::CollisionNodeChild{
                         collision::CollisionNodeChildKind::leaf, 1U},
                        collision::CollisionNodeChild{
                            collision::CollisionNodeChildKind::leaf, 0U}}}},
            std::vector<collision::CollisionLeaf>{
                {0U, contents(-2)}, {1U, contents(-1)}},
            std::vector<collision::CollisionClipnode>{},
            std::vector<collision::CollisionModel>{model(hulls(true, -3))});
    collision::CollisionWorldQuery direct_query{direct_package};
    const auto direct = direct_query.point_contents(request(
        {123.0F, -456.0F, 789.0F},
        collision::CollisionHullOrdinal::standing_32x32x72), scratch);
    REQUIRE(direct);
    CHECK(direct.result->contents.source.raw == -3);
    CHECK(direct.result->traversal_depth == 0U);
}

TEST_CASE("Point contents rejects malformed inputs cycles and hard limits",
    "[collision][point-contents][security]")
{
    collision::CollisionWorldQuery query{basic_package()};
    collision::CollisionQueryScratch scratch;

    auto invalid_request = request({
        std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
    auto invalid = query.point_contents(invalid_request, scratch);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error->code == collision::CollisionQueryErrorCode::invalid_point);

    invalid_request = request({1.0F, 0.0F, 0.0F});
    invalid_request.source_model_index = 9U;
    invalid = query.point_contents(invalid_request, scratch);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error->code == collision::CollisionQueryErrorCode::invalid_model);

    invalid_request = request({1.0F, 0.0F, 0.0F});
    invalid_request.hull = static_cast<collision::CollisionHullOrdinal>(99U);
    invalid = query.point_contents(invalid_request, scratch);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error->code == collision::CollisionQueryErrorCode::invalid_hull);

    invalid_request = request({1.0F, 0.0F, 0.0F});
    invalid_request.contents_policy = collision::CollisionContentsPolicy::
        stock_player_trace_contents_policy_pending;
    invalid = query.point_contents(invalid_request, scratch);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error->code ==
        collision::CollisionQueryErrorCode::unsupported_contents_policy);

    auto cycle_hulls = hulls();
    std::vector<collision::CollisionNode> cycle_nodes{
        {0U,
            {collision::CollisionNodeChild{
                 collision::CollisionNodeChildKind::node, 1U},
                collision::CollisionNodeChild{
                    collision::CollisionNodeChildKind::leaf, 0U}}},
        {0U,
            {collision::CollisionNodeChild{
                 collision::CollisionNodeChildKind::node, 0U},
                collision::CollisionNodeChild{
                    collision::CollisionNodeChildKind::leaf, 0U}}},
    };
    const auto cycle_package =
        std::make_shared<const collision::CollisionWorldPackage>(
            std::vector<collision::CollisionPlane>{
                {{1.0F, 0.0F, 0.0F}, 0.0, 0U, 0}},
            std::move(cycle_nodes),
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
            std::vector<collision::CollisionModel>{model(std::move(cycle_hulls))});
    collision::CollisionWorldQuery cycle_query{cycle_package};
    const auto cycle = cycle_query.point_contents(
        request({1.0F, 0.0F, 0.0F}), scratch);
    REQUIRE_FALSE(cycle);
    CHECK(cycle.error->code == collision::CollisionQueryErrorCode::cycle_detected);

    auto limited_request = request({1.0F, 0.0F, 0.0F});
    limited_request.limits.maximum_traversal_steps = 1U;
    const auto limited = cycle_query.point_contents(limited_request, scratch);
    REQUIRE_FALSE(limited);
    CHECK(limited.error->code == collision::CollisionQueryErrorCode::
        traversal_step_limit_exceeded);
}

TEST_CASE("Caller scratch is reused without growing on repeated point queries",
    "[collision][point-contents][scratch]")
{
    collision::CollisionWorldQuery query{basic_package()};
    collision::CollisionQueryScratch scratch;
    REQUIRE(query.point_contents(request({1.0F, 0.0F, 0.0F}), scratch));
    const auto retained = scratch.retained_bytes();
    REQUIRE(retained > 0U);
    for (std::size_t iteration = 0U; iteration < 16U; ++iteration) {
        REQUIRE(query.point_contents(request(
            {iteration % 2U == 0U ? 1.0F : -1.0F, 0.0F, 0.0F}), scratch));
        CHECK(scratch.retained_bytes() == retained);
    }
}

} // namespace
