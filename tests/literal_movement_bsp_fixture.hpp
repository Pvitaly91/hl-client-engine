#pragma once

#include "synthetic_goldsrc_bsp_fixture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace hlclient::tests::literal_movement_bsp {

// This fixture is a real BSP v30 collision graph, not an implementation of
// ILocalMovementCollision.  The point, standing and duck hulls are authored
// independently and pass through the canonical parser and collision builder
// before movement tests query them.
inline constexpr float kFloorZ = 0.0F;
inline constexpr float kCeilingZ = 128.0F;
inline constexpr float kWestWallX = -160.0F;
inline constexpr float kEastWallX = 192.0F;
inline constexpr float kSouthWallY = -160.0F;
inline constexpr float kNorthWallY = 160.0F;

inline constexpr float kValidStepMinimumX = 32.0F;
inline constexpr float kValidStepMaximumX = 64.0F;
inline constexpr float kValidStepMinimumY = -24.0F;
inline constexpr float kValidStepMaximumY = 24.0F;
inline constexpr float kValidStepHeight = 12.0F;

inline constexpr float kHighStepMinimumX = 32.0F;
inline constexpr float kHighStepMaximumX = 64.0F;
inline constexpr float kHighStepMinimumY = 52.0F;
inline constexpr float kHighStepMaximumY = 92.0F;
inline constexpr float kHighStepHeight = 28.0F;

inline constexpr float kWalkableRampMinimumX = -96.0F;
inline constexpr float kWalkableRampMaximumX = -48.0F;
inline constexpr float kWalkableRampMinimumY = -120.0F;
inline constexpr float kWalkableRampMaximumY = -64.0F;
inline constexpr float kWalkableRampSlope = 0.5F;
inline constexpr float kWalkableRampMaximumZ = 24.0F;

inline constexpr float kSteepRampMinimumX = -32.0F;
inline constexpr float kSteepRampMaximumX = 0.0F;
inline constexpr float kSteepRampMinimumY = 64.0F;
inline constexpr float kSteepRampMaximumY = 120.0F;
inline constexpr float kSteepRampSlope = 2.0F;
inline constexpr float kSteepRampMaximumZ = 64.0F;

inline constexpr std::array<float, 3U> kWaterMinimum{-128.0F, 64.0F, 0.0F};
inline constexpr std::array<float, 3U> kWaterMaximum{-96.0F, 96.0F, 64.0F};

struct HullHalfExtents {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

struct Halfspace {
    SyntheticBspVector3 normal{};
    float distance{0.0F};
    bool inside_front{true};
};

struct ConvexVolume {
    std::vector<Halfspace> constraints;
    std::int16_t contents{-2};
};

[[nodiscard]] inline Halfspace normalized_halfspace(
    const SyntheticBspVector3 normal,
    const float distance,
    const bool inside_front)
{
    const auto length = std::sqrt(
        normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    return {
        {normal.x / length, normal.y / length, normal.z / length},
        distance / length,
        inside_front,
    };
}

[[nodiscard]] inline ConvexVolume box(
    const std::array<float, 3U>& minimum,
    const std::array<float, 3U>& maximum,
    const std::int16_t contents = -2)
{
    return {{
                normalized_halfspace({-1.0F, 0.0F, 0.0F}, -minimum[0U], false),
                normalized_halfspace({1.0F, 0.0F, 0.0F}, maximum[0U], false),
                normalized_halfspace({0.0F, -1.0F, 0.0F}, -minimum[1U], false),
                normalized_halfspace({0.0F, 1.0F, 0.0F}, maximum[1U], false),
                normalized_halfspace({0.0F, 0.0F, -1.0F}, -minimum[2U], false),
                normalized_halfspace({0.0F, 0.0F, 1.0F}, maximum[2U], false),
            },
        contents};
}

[[nodiscard]] inline ConvexVolume ramp(
    const float minimum_x,
    const float maximum_x,
    const float minimum_y,
    const float maximum_y,
    const float slope,
    const float intercept)
{
    return {{
                normalized_halfspace({-1.0F, 0.0F, 0.0F}, -minimum_x, false),
                normalized_halfspace({1.0F, 0.0F, 0.0F}, maximum_x, false),
                normalized_halfspace({0.0F, -1.0F, 0.0F}, -minimum_y, false),
                normalized_halfspace({0.0F, 1.0F, 0.0F}, maximum_y, false),
                normalized_halfspace({0.0F, 0.0F, -1.0F}, -kFloorZ, false),
                // z <= slope*x + intercept
                normalized_halfspace({-slope, 0.0F, 1.0F}, intercept, false),
            },
        -2};
}

[[nodiscard]] inline std::vector<ConvexVolume> authored_volumes(
    const bool include_water)
{
    std::vector<ConvexVolume> volumes;
    volumes.reserve(include_water ? 14U : 13U);

    // Infinite world shell planes.
    volumes.push_back({{
                           normalized_halfspace(
                               {0.0F, 0.0F, 1.0F}, kFloorZ, false),
                       },
        -2});
    volumes.push_back({{
                           normalized_halfspace(
                               {0.0F, 0.0F, -1.0F}, -kCeilingZ, false),
                       },
        -2});
    volumes.push_back({{
                           normalized_halfspace(
                               {1.0F, 0.0F, 0.0F}, kWestWallX, false),
                       },
        -2});
    volumes.push_back({{
                           normalized_halfspace(
                               {-1.0F, 0.0F, 0.0F}, -kEastWallX, false),
                       },
        -2});
    volumes.push_back({{
                           normalized_halfspace(
                               {0.0F, 1.0F, 0.0F}, kSouthWallY, false),
                       },
        -2});
    volumes.push_back({{
                           normalized_halfspace(
                               {0.0F, -1.0F, 0.0F}, -kNorthWallY, false),
                       },
        -2});

    volumes.push_back(box(
        {kValidStepMinimumX, kValidStepMinimumY, kFloorZ},
        {kValidStepMaximumX, kValidStepMaximumY, kValidStepHeight}));
    volumes.push_back(box(
        {kHighStepMinimumX, kHighStepMinimumY, kFloorZ},
        {kHighStepMaximumX, kHighStepMaximumY, kHighStepHeight}));

    // Two boxes form an L-shaped wall and a literal inside corner.
    volumes.push_back(box(
        {72.0F, 64.0F, kFloorZ}, {80.0F, 136.0F, 96.0F}));
    volumes.push_back(box(
        {72.0F, 128.0F, kFloorZ}, {144.0F, 136.0F, 96.0F}));

    // A low slab admits the 36-unit duck hull and rejects the 72-unit
    // standing hull, covering ceiling contact and blocked unduck.
    volumes.push_back(box(
        {-64.0F, 16.0F, 52.0F}, {-16.0F, 48.0F, 64.0F}));

    volumes.push_back(ramp(
        kWalkableRampMinimumX,
        kWalkableRampMaximumX,
        kWalkableRampMinimumY,
        kWalkableRampMaximumY,
        kWalkableRampSlope,
        48.0F));
    volumes.push_back(ramp(
        kSteepRampMinimumX,
        kSteepRampMaximumX,
        kSteepRampMinimumY,
        kSteepRampMaximumY,
        kSteepRampSlope,
        64.0F));

    if (include_water) {
        volumes.push_back(box(kWaterMinimum, kWaterMaximum, -3));
    }
    return volumes;
}

[[nodiscard]] inline Halfspace expanded(
    Halfspace value,
    const HullHalfExtents extents) noexcept
{
    const auto support = std::abs(value.normal.x) * extents.x +
        std::abs(value.normal.y) * extents.y +
        std::abs(value.normal.z) * extents.z;
    value.distance += value.inside_front ? -support : support;
    return value;
}

[[nodiscard]] inline std::int32_t plane_type(
    const SyntheticBspVector3& normal) noexcept
{
    if (normal.x == 1.0F && normal.y == 0.0F && normal.z == 0.0F) {
        return 0;
    }
    if (normal.x == 0.0F && normal.y == 1.0F && normal.z == 0.0F) {
        return 1;
    }
    if (normal.x == 0.0F && normal.y == 0.0F && normal.z == 1.0F) {
        return 2;
    }
    return 3;
}

[[nodiscard]] inline std::int32_t append_plane(
    std::vector<SyntheticBspPlane>& planes,
    const Halfspace& halfspace)
{
    const auto index = static_cast<std::int32_t>(planes.size());
    planes.push_back({
        halfspace.normal,
        halfspace.distance,
        plane_type(halfspace.normal),
    });
    return index;
}

[[nodiscard]] inline std::int32_t compile_clip_tree(
    const std::span<const ConvexVolume> volumes,
    const HullHalfExtents extents,
    std::vector<SyntheticBspPlane>& planes,
    std::vector<SyntheticBspClipnode>& nodes)
{
    std::int16_t fallback = -1;
    for (auto volume = volumes.rbegin(); volume != volumes.rend(); ++volume) {
        std::int16_t pass = volume->contents;
        for (auto constraint = volume->constraints.rbegin();
             constraint != volume->constraints.rend(); ++constraint) {
            const auto hull_constraint = expanded(*constraint, extents);
            const auto plane = append_plane(planes, hull_constraint);
            const auto index = static_cast<std::int16_t>(nodes.size());
            SyntheticBspClipnode node;
            node.plane_index = plane;
            node.children = hull_constraint.inside_front
                ? std::array<std::int16_t, 2U>{pass, fallback}
                : std::array<std::int16_t, 2U>{fallback, pass};
            nodes.push_back(node);
            pass = index;
        }
        fallback = pass;
    }
    return fallback;
}

[[nodiscard]] inline std::int32_t compile_point_tree(
    const std::span<const ConvexVolume> volumes,
    std::vector<SyntheticBspPlane>& planes,
    std::vector<SyntheticBspNode>& nodes)
{
    // Leaves are ordered empty, solid, water, making their encoded node-child
    // references exactly -1, -2 and -3 respectively.
    std::int16_t fallback = -1;
    for (auto volume = volumes.rbegin(); volume != volumes.rend(); ++volume) {
        std::int16_t pass = volume->contents;
        for (auto constraint = volume->constraints.rbegin();
             constraint != volume->constraints.rend(); ++constraint) {
            const auto plane = append_plane(planes, *constraint);
            const auto index = static_cast<std::int16_t>(nodes.size());
            SyntheticBspNode node;
            node.plane_index = plane;
            node.children = constraint->inside_front
                ? std::array<std::int16_t, 2U>{pass, fallback}
                : std::array<std::int16_t, 2U>{fallback, pass};
            node.minimum = {-192, -192, 0};
            node.maximum = {192, 192, 128};
            node.first_face = 0U;
            node.face_count = 0U;
            nodes.push_back(node);
            pass = index;
        }
        fallback = pass;
    }
    return fallback;
}

[[nodiscard]] inline std::vector<std::byte> make_bsp_v30()
{
    auto point_volumes = authored_volumes(true);
    auto clip_volumes = authored_volumes(false);
    // Plane zero belongs to SyntheticBspBuilder's retained Z=0 render quad.
    std::vector<SyntheticBspPlane> planes{
        SyntheticBspPlane{{0.0F, 0.0F, 1.0F}, 0.0F, 2}};
    std::vector<SyntheticBspNode> point_nodes;
    std::vector<SyntheticBspClipnode> clipnodes;

    const auto point_root = compile_point_tree(
        point_volumes, planes, point_nodes);
    const auto standing_root = compile_clip_tree(
        clip_volumes, {16.0F, 16.0F, 36.0F}, planes, clipnodes);
    const auto duck_root = compile_clip_tree(
        clip_volumes, {16.0F, 16.0F, 18.0F}, planes, clipnodes);

    SyntheticBspBuilder builder;
    const std::array leaves{
        SyntheticBspLeaf{-1, -1, {-192, -192, 0}, {192, 192, 128}, 0U, 0U, {}},
        SyntheticBspLeaf{-2, -1, {-192, -192, -1}, {192, 192, 128}, 0U, 0U, {}},
        SyntheticBspLeaf{-3, -1, {-128, 64, 0}, {-96, 96, 64}, 0U, 0U, {}},
    };
    SyntheticBspModel model;
    model.minimum = {-192.0F, -192.0F, -1.0F};
    model.maximum = {192.0F, 192.0F, 128.0F};
    model.headnodes = {point_root, standing_root, -1, duck_root};
    model.visibility_leaf_count = 2;
    model.first_face = 0;
    // Retain the builder's one harmless render face because the canonical
    // parser requires every world model to own at least one source face.
    model.face_count = 1;

    return builder.set_planes(planes)
        .set_nodes(point_nodes)
        .set_leaves(leaves)
        .set_clipnodes(clipnodes)
        .set_models(std::span{&model, 1U})
        .build();
}

} // namespace hlclient::tests::literal_movement_bsp
