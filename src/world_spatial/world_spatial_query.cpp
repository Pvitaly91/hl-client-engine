#include <hlclient/world_spatial/world_spatial_query.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hlclient::world_spatial {
namespace {

[[nodiscard]] bool finite_vector(const assets::AssetVector3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool valid_bounds(const assets::WorldBounds& bounds) noexcept
{
    return finite_vector(bounds.minimum) && finite_vector(bounds.maximum) &&
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

[[nodiscard]] bool valid_plane(const WorldSpatialPlane& plane) noexcept
{
    return finite_vector(plane.normal) && std::isfinite(plane.distance) &&
        (plane.normal.x != 0.0F || plane.normal.y != 0.0F ||
         plane.normal.z != 0.0F);
}

[[nodiscard]] double signed_distance(
    const assets::AssetVector3 point,
    const WorldSpatialPlane& plane) noexcept
{
    return static_cast<double>(point.x) * plane.normal.x +
        static_cast<double>(point.y) * plane.normal.y +
        static_cast<double>(point.z) * plane.normal.z - plane.distance;
}

[[nodiscard]] WorldPointLeafQueryResult point_failure(
    const WorldSpatialQueryErrorCode code,
    const std::optional<std::uint32_t> node_index,
    const std::size_t steps) noexcept
{
    return WorldPointLeafQueryResult{
        std::nullopt,
        WorldSpatialQueryError{code, node_index, steps},
    };
}

[[nodiscard]] WorldBoxLeavesQueryResult box_failure(
    const WorldSpatialQueryErrorCode code,
    const std::optional<std::uint32_t> node_index,
    const std::size_t steps) noexcept
{
    return WorldBoxLeavesQueryResult{
        std::nullopt,
        WorldSpatialQueryError{code, node_index, steps},
    };
}

enum class BoxPlaneClassification : std::uint8_t {
    front,
    back,
    straddling,
};

[[nodiscard]] BoxPlaneClassification classify_bounds(
    const assets::WorldBounds& bounds,
    const WorldSpatialPlane& plane) noexcept
{
    assets::AssetVector3 negative_vertex{};
    assets::AssetVector3 positive_vertex{};
    const auto select_axis = [](
                                 const float normal,
                                 const float minimum,
                                 const float maximum,
                                 float& negative,
                                 float& positive) noexcept {
        if (normal >= 0.0F) {
            negative = minimum;
            positive = maximum;
        } else {
            negative = maximum;
            positive = minimum;
        }
    };
    select_axis(
        plane.normal.x,
        bounds.minimum.x,
        bounds.maximum.x,
        negative_vertex.x,
        positive_vertex.x);
    select_axis(
        plane.normal.y,
        bounds.minimum.y,
        bounds.maximum.y,
        negative_vertex.y,
        positive_vertex.y);
    select_axis(
        plane.normal.z,
        bounds.minimum.z,
        bounds.maximum.z,
        negative_vertex.z,
        positive_vertex.z);

    const auto minimum_distance = signed_distance(negative_vertex, plane);
    const auto maximum_distance = signed_distance(positive_vertex, plane);
    if (minimum_distance >= 0.0) {
        return BoxPlaneClassification::front;
    }
    if (maximum_distance < 0.0) {
        return BoxPlaneClassification::back;
    }
    return BoxPlaneClassification::straddling;
}

} // namespace

bool valid_world_spatial_query_limits(const WorldSpatialQueryLimits& limits) noexcept
{
    return limits.maximum_query_steps > 0U &&
        limits.maximum_query_steps <= kWorldSpatialHardMaximumQuerySteps &&
        limits.maximum_box_query_leaves > 0U &&
        limits.maximum_box_query_leaves <=
            kWorldSpatialHardMaximumBoxQueryLeaves;
}

std::string_view to_string(const WorldSpatialQueryErrorCode code) noexcept
{
    switch (code) {
    case WorldSpatialQueryErrorCode::invalid_configuration:
        return "invalid_configuration";
    case WorldSpatialQueryErrorCode::invalid_position: return "invalid_position";
    case WorldSpatialQueryErrorCode::invalid_bounds: return "invalid_bounds";
    case WorldSpatialQueryErrorCode::invalid_package: return "invalid_package";
    case WorldSpatialQueryErrorCode::invalid_plane_reference:
        return "invalid_plane_reference";
    case WorldSpatialQueryErrorCode::invalid_child_reference:
        return "invalid_child_reference";
    case WorldSpatialQueryErrorCode::cycle_detected: return "cycle_detected";
    case WorldSpatialQueryErrorCode::query_step_limit_exceeded:
        return "query_step_limit_exceeded";
    case WorldSpatialQueryErrorCode::leaf_limit_exceeded:
        return "leaf_limit_exceeded";
    }
    return "unknown";
}

WorldPointLeafQueryResult WorldSpatialQuery::locate_point(
    const WorldSpatialPackage& package,
    const assets::AssetVector3 position,
    const WorldSpatialQueryLimits& limits)
{
    if (!valid_world_spatial_query_limits(limits)) {
        return point_failure(
            WorldSpatialQueryErrorCode::invalid_configuration,
            std::nullopt,
            0U);
    }
    if (!finite_vector(position)) {
        return point_failure(
            WorldSpatialQueryErrorCode::invalid_position,
            std::nullopt,
            0U);
    }
    const auto nodes = package.nodes();
    const auto leaves = package.leaves();
    const auto planes = package.planes();
    if (planes.size() > kWorldSpatialHardMaximumQueryPlanes ||
        nodes.size() > kWorldSpatialHardMaximumQueryNodes ||
        leaves.size() > kWorldSpatialHardMaximumQueryPackageLeaves) {
        return point_failure(
            WorldSpatialQueryErrorCode::invalid_package,
            std::nullopt,
            0U);
    }
    auto node_index = package.world_model().root_node_index;
    if (static_cast<std::size_t>(node_index) >= nodes.size()) {
        return point_failure(
            WorldSpatialQueryErrorCode::invalid_package,
            node_index,
            0U);
    }

    std::vector<bool> visited(nodes.size(), false);
    std::size_t traversal_depth = 0U;
    while (true) {
        if (traversal_depth >= limits.maximum_query_steps) {
            return point_failure(
                WorldSpatialQueryErrorCode::query_step_limit_exceeded,
                node_index,
                traversal_depth);
        }
        if (static_cast<std::size_t>(node_index) >= nodes.size()) {
            return point_failure(
                WorldSpatialQueryErrorCode::invalid_child_reference,
                node_index,
                traversal_depth);
        }
        if (visited[node_index]) {
            return point_failure(
                WorldSpatialQueryErrorCode::cycle_detected,
                node_index,
                traversal_depth);
        }
        visited[node_index] = true;
        ++traversal_depth;

        const auto& node = nodes[node_index];
        if (static_cast<std::size_t>(node.plane_index) >= planes.size() ||
            !valid_plane(planes[node.plane_index])) {
            return point_failure(
                WorldSpatialQueryErrorCode::invalid_plane_reference,
                node_index,
                traversal_depth);
        }
        const auto& plane = planes[node.plane_index];
        const auto child = node.children[
            signed_distance(position, plane) >= 0.0 ? 0U : 1U];
        if (child.kind == WorldSpatialNodeChildKind::node) {
            node_index = child.index;
            continue;
        }
        if (child.kind != WorldSpatialNodeChildKind::leaf) {
            return point_failure(
                WorldSpatialQueryErrorCode::invalid_child_reference,
                node_index,
                traversal_depth);
        }
        if (static_cast<std::size_t>(child.index) >= leaves.size()) {
            return point_failure(
                WorldSpatialQueryErrorCode::invalid_child_reference,
                node_index,
                traversal_depth);
        }
        const auto& leaf = leaves[child.index];
        return WorldPointLeafQueryResult{
            WorldPointLeafResult{
                child.index,
                leaf.contents,
                leaf.solid_or_special,
                package.pvs_table().leaf_has_usable_row(child.index),
                traversal_depth,
            },
            std::nullopt,
        };
    }
}

WorldBoxLeavesQueryResult WorldSpatialQuery::collect_intersecting_leaves(
    const WorldSpatialPackage& package,
    const assets::WorldBounds& bounds,
    const WorldSpatialQueryLimits& limits)
{
    if (!valid_world_spatial_query_limits(limits)) {
        return box_failure(
            WorldSpatialQueryErrorCode::invalid_configuration,
            std::nullopt,
            0U);
    }
    if (!valid_bounds(bounds)) {
        return box_failure(
            WorldSpatialQueryErrorCode::invalid_bounds,
            std::nullopt,
            0U);
    }
    const auto nodes = package.nodes();
    const auto leaves = package.leaves();
    const auto planes = package.planes();
    if (planes.size() > kWorldSpatialHardMaximumQueryPlanes ||
        nodes.size() > kWorldSpatialHardMaximumQueryNodes ||
        leaves.size() > kWorldSpatialHardMaximumQueryPackageLeaves) {
        return box_failure(
            WorldSpatialQueryErrorCode::invalid_package,
            std::nullopt,
            0U);
    }
    const auto root_node_index = package.world_model().root_node_index;
    if (static_cast<std::size_t>(root_node_index) >= nodes.size()) {
        return box_failure(
            WorldSpatialQueryErrorCode::invalid_package,
            root_node_index,
            0U);
    }

    enum class VisitState : std::uint8_t {
        unvisited,
        active,
        complete,
    };
    struct Frame {
        WorldSpatialNodeChild child{};
        bool exit_node{false};
    };

    std::vector<VisitState> states(nodes.size(), VisitState::unvisited);
    std::vector<bool> emitted_leaf(leaves.size(), false);
    std::vector<Frame> stack;
    stack.reserve(nodes.size() * 2U + 1U);
    stack.push_back(Frame{
        WorldSpatialNodeChild{WorldSpatialNodeChildKind::node, root_node_index},
        false,
    });
    WorldBoxLeavesResult output;
    output.leaf_indices.reserve(std::min(
        leaves.size(),
        limits.maximum_box_query_leaves));

    while (!stack.empty()) {
        const auto frame = stack.back();
        stack.pop_back();
        if (frame.child.kind == WorldSpatialNodeChildKind::leaf) {
            if (static_cast<std::size_t>(frame.child.index) >= leaves.size()) {
                return box_failure(
                    WorldSpatialQueryErrorCode::invalid_child_reference,
                    std::nullopt,
                    output.traversal_steps);
            }
            if (!emitted_leaf[frame.child.index]) {
                if (output.leaf_indices.size() >= limits.maximum_box_query_leaves) {
                    return box_failure(
                        WorldSpatialQueryErrorCode::leaf_limit_exceeded,
                        std::nullopt,
                        output.traversal_steps);
                }
                emitted_leaf[frame.child.index] = true;
                output.leaf_indices.push_back(frame.child.index);
            }
            continue;
        }
        if (frame.child.kind != WorldSpatialNodeChildKind::node) {
            return box_failure(
                WorldSpatialQueryErrorCode::invalid_child_reference,
                std::nullopt,
                output.traversal_steps);
        }

        const auto node_index = frame.child.index;
        if (static_cast<std::size_t>(node_index) >= nodes.size()) {
            return box_failure(
                WorldSpatialQueryErrorCode::invalid_child_reference,
                node_index,
                output.traversal_steps);
        }
        if (frame.exit_node) {
            states[node_index] = VisitState::complete;
            continue;
        }
        if (states[node_index] == VisitState::active) {
            return box_failure(
                WorldSpatialQueryErrorCode::cycle_detected,
                node_index,
                output.traversal_steps);
        }
        if (states[node_index] == VisitState::complete) {
            continue;
        }
        if (output.traversal_steps >= limits.maximum_query_steps) {
            return box_failure(
                WorldSpatialQueryErrorCode::query_step_limit_exceeded,
                node_index,
                output.traversal_steps);
        }
        ++output.traversal_steps;
        states[node_index] = VisitState::active;

        const auto& node = nodes[node_index];
        if (static_cast<std::size_t>(node.plane_index) >= planes.size() ||
            !valid_plane(planes[node.plane_index])) {
            return box_failure(
                WorldSpatialQueryErrorCode::invalid_plane_reference,
                node_index,
                output.traversal_steps);
        }
        stack.push_back(Frame{
            WorldSpatialNodeChild{WorldSpatialNodeChildKind::node, node_index},
            true,
        });
        const auto classification = classify_bounds(bounds, planes[node.plane_index]);
        switch (classification) {
        case BoxPlaneClassification::front:
            stack.push_back(Frame{node.children[0U], false});
            break;
        case BoxPlaneClassification::back:
            stack.push_back(Frame{node.children[1U], false});
            break;
        case BoxPlaneClassification::straddling:
            // Back is pushed first so the deterministic output visits the
            // front child before the back child.
            stack.push_back(Frame{node.children[1U], false});
            stack.push_back(Frame{node.children[0U], false});
            break;
        }
    }

    return WorldBoxLeavesQueryResult{std::move(output), std::nullopt};
}

} // namespace hlclient::world_spatial
