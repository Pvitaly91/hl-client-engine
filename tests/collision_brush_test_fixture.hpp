#pragma once

#include <hlclient/collision/collision_world_package.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace hlclient::tests::collision_brush_fixture {

namespace collision = hlclient::collision;

[[nodiscard]] inline collision::CollisionContents contents(
    const std::int32_t raw)
{
    return *collision::decode_goldsrc_contents({raw});
}

[[nodiscard]] inline std::array<collision::CollisionHull, 4U> hulls(
    const std::uint32_t node_root,
    const std::uint32_t clipnode_root)
{
    std::array<collision::CollisionHull, 4U> result{};
    for (std::size_t index = 0U; index < result.size(); ++index) {
        const auto ordinal = *collision::collision_hull_ordinal(index);
        result[index] = collision::CollisionHull{
            ordinal,
            index == 0U ? collision::CollisionHullTreeDomain::node_leaf
                        : collision::CollisionHullTreeDomain::clipnode,
            collision::CollisionHullRoot{
                index == 0U ? collision::CollisionHullRootKind::node
                            : collision::CollisionHullRootKind::clipnode,
                index == 0U ? node_root : clipnode_root,
                contents(-1),
            },
            *collision::standard_collision_hull_profile(ordinal),
        };
    }
    return result;
}

[[nodiscard]] inline collision::CollisionModel model(
    const std::uint32_t source_model_index,
    const std::uint32_t node_root,
    const std::uint32_t clipnode_root)
{
    return collision::CollisionModel{
        source_model_index,
        {},
        assets::WorldBounds{{-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}},
        0U,
        1U,
        hulls(node_root, clipnode_root),
    };
}

// World root is node/clipnode 0. Brush models share node/clipnode 1. A
// blocking world uses plane 0; brush-local collision always uses plane 1.
[[nodiscard]] inline std::shared_ptr<const collision::CollisionWorldPackage>
package(
    const bool world_blocks,
    const double world_plane_distance = 0.0,
    const std::size_t brush_model_count = 2U,
    const bool brush_front_blocks = false)
{
    std::vector<collision::CollisionModel> models;
    models.reserve(brush_model_count + 1U);
    models.push_back(model(0U, 0U, 0U));
    for (std::size_t index = 0U; index < brush_model_count; ++index) {
        models.push_back(model(
            static_cast<std::uint32_t>(index + 1U), 1U, 1U));
    }

    return std::make_shared<const collision::CollisionWorldPackage>(
        std::vector<collision::CollisionPlane>{
            {{1.0F, 0.0F, 0.0F}, world_plane_distance, 10U, 0},
            {{1.0F, 0.0F, 0.0F}, 0.0, 20U, 0},
        },
        std::vector<collision::CollisionNode>{
            {0U,
                {collision::CollisionNodeChild{
                     collision::CollisionNodeChildKind::leaf, 1U},
                    collision::CollisionNodeChild{
                        collision::CollisionNodeChildKind::leaf,
                        world_blocks ? 0U : 1U}}},
            {1U,
                {collision::CollisionNodeChild{
                     collision::CollisionNodeChildKind::leaf,
                     brush_front_blocks ? 0U : 1U},
                    collision::CollisionNodeChild{
                        collision::CollisionNodeChildKind::leaf,
                        brush_front_blocks ? 1U : 0U}}},
        },
        std::vector<collision::CollisionLeaf>{
            {0U, contents(-2)},
            {1U, contents(-1)},
        },
        std::vector<collision::CollisionClipnode>{
            {0U,
                {collision::CollisionClipnodeChild{
                     collision::CollisionClipnodeChildKind::terminal,
                     0U,
                     contents(-1)},
                    collision::CollisionClipnodeChild{
                        collision::CollisionClipnodeChildKind::terminal,
                        0U,
                        contents(world_blocks ? -2 : -1)}}},
            {1U,
                {collision::CollisionClipnodeChild{
                     collision::CollisionClipnodeChildKind::terminal,
                     0U,
                     contents(brush_front_blocks ? -2 : -1)},
                    collision::CollisionClipnodeChild{
                        collision::CollisionClipnodeChildKind::terminal,
                        0U,
                        contents(brush_front_blocks ? -1 : -2)}}},
        },
        std::move(models),
        collision::CollisionWorldIdentity{
            assets::AssetSourceFingerprint{0x1234U, 0x5678U}, 30U}
    );
}

// A closed axis-aligned collision box in model 1. Its deliberately smaller
// dmodel bounds prove that broad-phase rejection comes from the retained BSP
// hull tree rather than trusting source-model metadata.
[[nodiscard]] inline std::shared_ptr<const collision::CollisionWorldPackage>
bounded_brush_package(const std::size_t brush_model_count = 2U)
{
    std::vector<collision::CollisionNode> nodes;
    nodes.reserve(7U);
    nodes.push_back(collision::CollisionNode{
        0U,
        {collision::CollisionNodeChild{
             collision::CollisionNodeChildKind::leaf, 1U},
            collision::CollisionNodeChild{
                collision::CollisionNodeChildKind::leaf, 1U}},
    });
    std::vector<collision::CollisionClipnode> clipnodes;
    clipnodes.reserve(7U);
    clipnodes.push_back(collision::CollisionClipnode{
        0U,
        {collision::CollisionClipnodeChild{
             collision::CollisionClipnodeChildKind::terminal,
             0U,
             contents(-1)},
            collision::CollisionClipnodeChild{
                collision::CollisionClipnodeChildKind::terminal,
                0U,
                contents(-1)}},
    });

    for (std::uint32_t index = 1U; index <= 6U; ++index) {
        const bool upper_plane = (index % 2U) != 0U;
        const bool final_plane = index == 6U;
        const collision::CollisionNodeChild empty_leaf{
            collision::CollisionNodeChildKind::leaf, 1U};
        const collision::CollisionNodeChild next_or_solid{
            final_plane ? collision::CollisionNodeChildKind::leaf
                        : collision::CollisionNodeChildKind::node,
            final_plane ? 0U : index + 1U,
        };
        nodes.push_back(collision::CollisionNode{
            index,
            upper_plane
                ? std::array{empty_leaf, next_or_solid}
                : std::array{next_or_solid, empty_leaf},
        });

        const collision::CollisionClipnodeChild empty_terminal{
            collision::CollisionClipnodeChildKind::terminal,
            0U,
            contents(-1),
        };
        const collision::CollisionClipnodeChild next_or_solid_terminal{
            final_plane
                ? collision::CollisionClipnodeChildKind::terminal
                : collision::CollisionClipnodeChildKind::clipnode,
            final_plane ? 0U : index + 1U,
            final_plane ? contents(-2) : contents(-1),
        };
        clipnodes.push_back(collision::CollisionClipnode{
            index,
            upper_plane
                ? std::array{empty_terminal, next_or_solid_terminal}
                : std::array{next_or_solid_terminal, empty_terminal},
        });
    }

    std::vector<collision::CollisionModel> models;
    models.reserve(brush_model_count + 1U);
    models.push_back(model(0U, 0U, 0U));
    for (std::size_t index = 0U; index < brush_model_count; ++index) {
        auto brush = model(
            static_cast<std::uint32_t>(index + 1U), 1U, 1U);
        brush.source_bounds = {
            {-0.25F, -0.25F, -0.25F},
            {0.25F, 0.25F, 0.25F},
        };
        models.push_back(std::move(brush));
    }
    return std::make_shared<const collision::CollisionWorldPackage>(
        std::vector<collision::CollisionPlane>{
            {{1.0F, 0.0F, 0.0F}, 0.0, 10U, 0},
            {{1.0F, 0.0F, 0.0F}, 1.0, 20U, 0},
            {{1.0F, 0.0F, 0.0F}, -1.0, 21U, 0},
            {{0.0F, 1.0F, 0.0F}, 1.0, 22U, 1},
            {{0.0F, 1.0F, 0.0F}, -1.0, 23U, 1},
            {{0.0F, 0.0F, 1.0F}, 1.0, 24U, 2},
            {{0.0F, 0.0F, 1.0F}, -1.0, 25U, 2},
        },
        std::move(nodes),
        std::vector<collision::CollisionLeaf>{
            {0U, contents(-2)},
            {1U, contents(-1)},
        },
        std::move(clipnodes),
        std::move(models),
        collision::CollisionWorldIdentity{
            assets::AssetSourceFingerprint{0x2468U, 0x1357U}, 30U});
}

} // namespace hlclient::tests::collision_brush_fixture
