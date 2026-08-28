#include <hlclient/collision/collision_world_package.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <type_traits>
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

[[nodiscard]] std::array<collision::CollisionHull, 4U> standard_hulls()
{
    std::array<collision::CollisionHull, 4U> output{};
    for (std::size_t index = 0U; index < output.size(); ++index) {
        const auto ordinal = collision::collision_hull_ordinal(index);
        REQUIRE(ordinal);
        const auto profile = collision::standard_collision_hull_profile(*ordinal);
        REQUIRE(profile);
        output[index] = collision::CollisionHull{
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

[[nodiscard]] collision::CollisionModel model(const std::uint32_t index)
{
    return collision::CollisionModel{
        index,
        {},
        assets::WorldBounds{{-64.0F, -64.0F, -64.0F},
            {64.0F, 64.0F, 64.0F}},
        0U,
        1U,
        standard_hulls(),
    };
}

TEST_CASE("Collision package owns neutral planes trees and ordered models",
    "[collision][package]")
{
    std::vector<collision::CollisionPlane> planes{
        {{1.0F, 0.0F, 0.0F}, 0.0, 17U, 0},
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
        {1U, contents(-1)},
    };
    std::vector<collision::CollisionClipnode> clipnodes{
        {0U,
            {collision::CollisionClipnodeChild{
                 collision::CollisionClipnodeChildKind::terminal,
                 0U,
                 contents(-1)},
                collision::CollisionClipnodeChild{
                    collision::CollisionClipnodeChildKind::terminal,
                    0U,
                    contents(-2)}}},
    };
    std::vector<collision::CollisionModel> models{model(0U), model(1U)};
    const collision::CollisionWorldIdentity identity{
        assets::AssetSourceFingerprint{11U, 22U}, 7U};
    collision::CollisionWorldStatistics statistics;
    statistics.plane_count = 1U;
    statistics.node_count = 1U;
    statistics.leaf_count = 2U;
    statistics.clipnode_count = 1U;
    statistics.model_count = 2U;
    statistics.model_hull_root_count = 8U;

    const collision::CollisionWorldPackage package{
        planes, nodes, leaves, clipnodes, models, identity, statistics};
    planes.clear();
    nodes.clear();
    leaves.clear();
    clipnodes.clear();
    models.clear();

    STATIC_REQUIRE(std::is_same_v<
        decltype(package.planes()),
        std::span<const collision::CollisionPlane>>);
    REQUIRE(package.planes().size() == 1U);
    CHECK(package.planes()[0U].source_plane_index == 17U);
    REQUIRE(package.nodes().size() == 1U);
    REQUIRE(package.leaves().size() == 2U);
    REQUIRE(package.clipnodes().size() == 1U);
    REQUIRE(package.models().size() == 2U);
    REQUIRE(package.model(0U) != nullptr);
    REQUIRE(package.model(1U) != nullptr);
    CHECK(package.model(2U) == nullptr);
    CHECK(package.identity().source_fingerprint ==
        std::optional{assets::AssetSourceFingerprint{11U, 22U}});
    CHECK(package.identity().source_revision == 7U);
    CHECK(package.statistics().plane_count == 1U);
    CHECK(package.statistics().model_count == 2U);
    CHECK(package.statistics().model_hull_root_count == 8U);
}

TEST_CASE("Collision package exposes exactly four compiler hull profiles",
    "[collision][package][hulls]")
{
    const auto hulls = standard_hulls();
    REQUIRE(hulls.size() == collision::kCollisionHullCount);

    CHECK(hulls[0U].profile.clip_mins.x == 0.0F);
    CHECK(hulls[0U].profile.clip_maxs.z == 0.0F);
    CHECK(hulls[1U].profile.clip_mins.x == -16.0F);
    CHECK(hulls[1U].profile.clip_mins.z == -36.0F);
    CHECK(hulls[1U].profile.clip_maxs.z == 36.0F);
    CHECK(hulls[2U].profile.clip_mins.x == -32.0F);
    CHECK(hulls[2U].profile.clip_maxs.z == 32.0F);
    CHECK(hulls[3U].profile.clip_mins.x == -16.0F);
    CHECK(hulls[3U].profile.clip_mins.z == -18.0F);
    CHECK(hulls[3U].profile.clip_maxs.z == 18.0F);

    const collision::CollisionModel value = model(0U);
    CHECK(value.hull(collision::CollisionHullOrdinal::point) == &value.hulls[0U]);
    CHECK(value.hull(collision::CollisionHullOrdinal::duck_32x32x36) ==
        &value.hulls[3U]);
    CHECK(value.hull(static_cast<collision::CollisionHullOrdinal>(99U)) ==
        nullptr);
    CHECK_FALSE(collision::standard_collision_hull_profile(
        static_cast<collision::CollisionHullOrdinal>(99U)));
}

TEST_CASE("Duplicate model identities are not resolved by iteration order",
    "[collision][package][identity]")
{
    collision::CollisionWorldPackage package{
        {{{1.0F, 0.0F, 0.0F}, 0.0, 0U, 0}},
        {{0U,
            {collision::CollisionNodeChild{
                 collision::CollisionNodeChildKind::leaf, 1U},
                collision::CollisionNodeChild{
                    collision::CollisionNodeChildKind::leaf, 0U}}}},
        {{0U, contents(-2)}, {1U, contents(-1)}},
        {},
        {model(0U), model(0U)},
    };
    CHECK(package.model(0U) == nullptr);
}

} // namespace
