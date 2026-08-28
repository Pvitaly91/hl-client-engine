#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>

#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace hlclient::goldsrc::collision {
namespace {

using CollisionHullOrdinal =
    hlclient::collision::CollisionHullOrdinal;
using SourceHullOrdinal = bsp::GoldSrcBspCollisionHullOrdinal;

[[nodiscard]] CollisionWorldBuildResult failure(
    const CollisionWorldBuildErrorCode code,
    const std::string_view context,
    const std::optional<std::uint32_t> model = std::nullopt,
    const std::optional<CollisionHullOrdinal> hull = std::nullopt,
    const std::optional<std::uint32_t> record = std::nullopt)
{
    constexpr std::size_t maximum_context = 160U;
    return CollisionWorldBuildResult{
        {},
        CollisionWorldBuildError{
            code,
            model,
            hull,
            record,
            std::string{context.substr(0U, maximum_context)},
        },
    };
}

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (right > (std::numeric_limits<std::size_t>::max)() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool checked_product(
    const std::size_t count,
    const std::size_t element_size,
    std::size_t& result) noexcept
{
    if (count != 0U &&
        element_size > (std::numeric_limits<std::size_t>::max)() / count) {
        return false;
    }
    result = count * element_size;
    return true;
}

template<class Record>
[[nodiscard]] bool account_records(
    const std::size_t count,
    std::size_t& total) noexcept
{
    std::size_t bytes = 0U;
    return checked_product(count, sizeof(Record), bytes) &&
        checked_add(total, bytes, total);
}

[[nodiscard]] bool finite_vector(
    const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool finite_bounds(const assets::WorldBounds& value) noexcept
{
    return finite_vector(value.minimum) && finite_vector(value.maximum) &&
        value.minimum.x <= value.maximum.x &&
        value.minimum.y <= value.maximum.y &&
        value.minimum.z <= value.maximum.z;
}

[[nodiscard]] std::optional<CollisionHullOrdinal> convert_ordinal(
    const SourceHullOrdinal ordinal) noexcept
{
    switch (ordinal) {
    case SourceHullOrdinal::point:
        return CollisionHullOrdinal::point;
    case SourceHullOrdinal::standing_32x32x72:
        return CollisionHullOrdinal::standing_32x32x72;
    case SourceHullOrdinal::large_64_cube:
        return CollisionHullOrdinal::large_64_cube;
    case SourceHullOrdinal::duck_32x32x36:
        return CollisionHullOrdinal::duck_32x32x36;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<hlclient::collision::CollisionContents>
convert_contents(const bsp::GoldSrcContentsCode contents) noexcept
{
    return hlclient::collision::decode_goldsrc_contents(
        hlclient::collision::GoldSrcContentsCode{contents.value});
}

[[nodiscard]] bool vector_equal(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] bool exact_profile(
    const bsp::GoldSrcBspCollisionSourcePointHull& source,
    const hlclient::collision::CollisionHullProfile& expected) noexcept
{
    return source.tree_domain == bsp::GoldSrcBspCollisionTreeDomain::node_leaf &&
        source.compatibility_profile ==
            bsp::GoldSrcBspCollisionCompatibilityProfile::
                valve_bsp_v30_clip_hulls_v1 &&
        source.evidence_profile ==
            bsp::GoldSrcBspCollisionEvidenceProfile::
                public_valve_bsp_compiler_and_original_map_validation &&
        vector_equal(source.extents.minimum, expected.clip_mins) &&
        vector_equal(source.extents.maximum, expected.clip_maxs);
}

[[nodiscard]] bool exact_profile(
    const bsp::GoldSrcBspCollisionSourceClipHull& source,
    const hlclient::collision::CollisionHullProfile& expected) noexcept
{
    return source.tree_domain ==
               bsp::GoldSrcBspCollisionTreeDomain::clipnode_contents &&
        source.compatibility_profile ==
            bsp::GoldSrcBspCollisionCompatibilityProfile::
                valve_bsp_v30_clip_hulls_v1 &&
        source.evidence_profile ==
            bsp::GoldSrcBspCollisionEvidenceProfile::
                public_valve_bsp_compiler_and_original_map_validation &&
        vector_equal(source.extents.minimum, expected.clip_mins) &&
        vector_equal(source.extents.maximum, expected.clip_maxs);
}

struct TraversalBudget {
    std::size_t steps{0U};
    std::size_t links{0U};
    const GoldSrcCollisionBuildLimits& limits;

    [[nodiscard]] bool consume() noexcept
    {
        return steps < limits.maximum_validation_steps &&
            links < limits.maximum_reachable_links && (++steps, ++links, true);
    }
};

struct GraphFrame {
    std::uint32_t index{0U};
    std::size_t next_child{0U};
};

enum class GraphValidationState : std::uint8_t {
    valid,
    invalid_node_reference,
    invalid_leaf_reference,
    invalid_clipnode_reference,
    invalid_contents,
    cycle,
    limit,
};

struct GraphValidationResult {
    GraphValidationState state{GraphValidationState::valid};
    std::uint32_t record{0U};
};

struct GraphValidationOutput {
    GraphValidationResult result{};
    std::uint64_t reachable_count{0U};
    std::uint64_t unreachable_count{0U};
    std::uint64_t maximum_root_depth{0U};
};

[[nodiscard]] GraphValidationOutput validate_node_graph(
    const bsp::GoldSrcBspCollisionSource& source,
    const std::span<const std::uint32_t> roots,
    TraversalBudget& budget)
{
    std::vector<std::uint8_t> colors(source.nodes.size(), 0U);
    std::vector<std::uint8_t> reachable(source.nodes.size(), 0U);
    std::vector<std::size_t> subtree_depths(source.nodes.size(), 0U);
    std::vector<GraphFrame> stack;
    stack.reserve(source.nodes.size());

    const auto traverse = [&](const std::uint32_t root,
                              const bool mark_reachable) -> GraphValidationResult {
        if (root >= source.nodes.size()) {
            return {GraphValidationState::invalid_node_reference, root};
        }
        if (colors[root] == 2U) {
            if (mark_reachable) {
                reachable[root] = 1U;
            }
            return {};
        }
        stack.clear();
        colors[root] = 1U;
        if (mark_reachable) {
            reachable[root] = 1U;
        }
        stack.push_back(GraphFrame{root, 0U});
        while (!stack.empty()) {
            auto& frame = stack.back();
            if (!budget.consume()) {
                return {GraphValidationState::limit, frame.index};
            }
            if (frame.index >= source.nodes.size()) {
                return {GraphValidationState::invalid_node_reference,
                        frame.index};
            }
            const auto& node = source.nodes[frame.index];
            if (node.source_plane_index >= source.planes.size()) {
                return {GraphValidationState::invalid_node_reference,
                        frame.index};
            }
            if (frame.next_child < node.children.size()) {
                const auto child_ordinal = frame.next_child++;
                const auto& child = node.children[child_ordinal];
                if (const auto* next = std::get_if<
                        bsp::GoldSrcBspCollisionSourceNodeReference>(&child)) {
                    if (next->source_node_index >= source.nodes.size()) {
                        return {GraphValidationState::invalid_node_reference,
                                next->source_node_index};
                    }
                    if (mark_reachable) {
                        reachable[next->source_node_index] = 1U;
                    }
                    if (colors[next->source_node_index] == 1U) {
                        return {GraphValidationState::cycle,
                                next->source_node_index};
                    }
                    if (colors[next->source_node_index] == 0U) {
                        colors[next->source_node_index] = 1U;
                        stack.push_back(
                            GraphFrame{next->source_node_index, 0U});
                    }
                } else {
                    const auto leaf = std::get<
                        bsp::GoldSrcBspCollisionSourceLeafReference>(child);
                    if (leaf.source_leaf_index >= source.leaves.size()) {
                        return {GraphValidationState::invalid_leaf_reference,
                                leaf.source_leaf_index};
                    }
                    if (!convert_contents(
                            source.leaves[leaf.source_leaf_index].contents)) {
                        return {GraphValidationState::invalid_contents,
                                leaf.source_leaf_index};
                    }
                }
                continue;
            }

            std::size_t depth = 1U;
            for (const auto& child : node.children) {
                if (const auto* next = std::get_if<
                        bsp::GoldSrcBspCollisionSourceNodeReference>(&child)) {
                    depth = std::max(
                        depth, subtree_depths[next->source_node_index] + 1U);
                }
            }
            if (depth > budget.limits.maximum_tree_depth) {
                return {GraphValidationState::limit, frame.index};
            }
            subtree_depths[frame.index] = depth;
            colors[frame.index] = 2U;
            stack.pop_back();
        }
        return {};
    };

    GraphValidationOutput output;
    for (const auto root : roots) {
        output.result = traverse(root, true);
        if (output.result.state != GraphValidationState::valid) {
            return output;
        }
    }
    for (std::size_t index = 0U; index < source.nodes.size(); ++index) {
        if (colors[index] == 0U) {
            output.result = traverse(static_cast<std::uint32_t>(index), false);
            if (output.result.state != GraphValidationState::valid) {
                return output;
            }
        }
    }
    output.reachable_count = static_cast<std::uint64_t>(
        std::count(reachable.begin(), reachable.end(), std::uint8_t{1U}));
    output.unreachable_count = static_cast<std::uint64_t>(source.nodes.size()) -
        output.reachable_count;
    for (const auto root : roots) {
        output.maximum_root_depth = std::max(
            output.maximum_root_depth,
            static_cast<std::uint64_t>(subtree_depths[root]));
    }
    return output;
}

[[nodiscard]] GraphValidationOutput validate_clip_graph(
    const bsp::GoldSrcBspCollisionSource& source,
    const std::span<const std::uint32_t> roots,
    TraversalBudget& budget)
{
    std::vector<std::uint8_t> colors(source.clipnodes.size(), 0U);
    std::vector<std::uint8_t> reachable(source.clipnodes.size(), 0U);
    std::vector<std::size_t> subtree_depths(source.clipnodes.size(), 0U);
    std::vector<GraphFrame> stack;
    stack.reserve(source.clipnodes.size());

    const auto traverse = [&](const std::uint32_t root,
                              const bool mark_reachable) -> GraphValidationResult {
        if (root >= source.clipnodes.size()) {
            return {GraphValidationState::invalid_clipnode_reference, root};
        }
        if (colors[root] == 2U) {
            if (mark_reachable) {
                reachable[root] = 1U;
            }
            return {};
        }
        stack.clear();
        colors[root] = 1U;
        if (mark_reachable) {
            reachable[root] = 1U;
        }
        stack.push_back(GraphFrame{root, 0U});
        while (!stack.empty()) {
            auto& frame = stack.back();
            if (!budget.consume()) {
                return {GraphValidationState::limit, frame.index};
            }
            if (frame.index >= source.clipnodes.size()) {
                return {GraphValidationState::invalid_clipnode_reference,
                        frame.index};
            }
            const auto& node = source.clipnodes[frame.index];
            if (node.source_plane_index >= source.planes.size()) {
                return {GraphValidationState::invalid_clipnode_reference,
                        frame.index};
            }
            if (frame.next_child < node.children.size()) {
                const auto child_ordinal = frame.next_child++;
                const auto& child = node.children[child_ordinal];
                if (const auto* next = std::get_if<
                        bsp::GoldSrcBspCollisionSourceClipnodeReference>(&child)) {
                    if (next->source_clipnode_index >= source.clipnodes.size()) {
                        return {GraphValidationState::invalid_clipnode_reference,
                                next->source_clipnode_index};
                    }
                    if (mark_reachable) {
                        reachable[next->source_clipnode_index] = 1U;
                    }
                    if (colors[next->source_clipnode_index] == 1U) {
                        return {GraphValidationState::cycle,
                                next->source_clipnode_index};
                    }
                    if (colors[next->source_clipnode_index] == 0U) {
                        colors[next->source_clipnode_index] = 1U;
                        stack.push_back(
                            GraphFrame{next->source_clipnode_index, 0U});
                    }
                } else if (!convert_contents(
                               std::get<bsp::GoldSrcContentsCode>(child))) {
                    return {GraphValidationState::invalid_contents, frame.index};
                }
                continue;
            }

            std::size_t depth = 1U;
            for (const auto& child : node.children) {
                if (const auto* next = std::get_if<
                        bsp::GoldSrcBspCollisionSourceClipnodeReference>(&child)) {
                    depth = std::max(
                        depth,
                        subtree_depths[next->source_clipnode_index] + 1U);
                }
            }
            if (depth > budget.limits.maximum_tree_depth) {
                return {GraphValidationState::limit, frame.index};
            }
            subtree_depths[frame.index] = depth;
            colors[frame.index] = 2U;
            stack.pop_back();
        }
        return {};
    };

    GraphValidationOutput output;
    for (const auto root : roots) {
        output.result = traverse(root, true);
        if (output.result.state != GraphValidationState::valid) {
            return output;
        }
    }
    for (std::size_t index = 0U; index < source.clipnodes.size(); ++index) {
        if (colors[index] == 0U) {
            output.result = traverse(static_cast<std::uint32_t>(index), false);
            if (output.result.state != GraphValidationState::valid) {
                return output;
            }
        }
    }
    output.reachable_count = static_cast<std::uint64_t>(
        std::count(reachable.begin(), reachable.end(), std::uint8_t{1U}));
    output.unreachable_count =
        static_cast<std::uint64_t>(source.clipnodes.size()) -
        output.reachable_count;
    for (const auto root : roots) {
        output.maximum_root_depth = std::max(
            output.maximum_root_depth,
            static_cast<std::uint64_t>(subtree_depths[root]));
    }
    return output;
}

[[nodiscard]] CollisionWorldBuildResult graph_failure(
    const GraphValidationResult graph,
    const std::optional<std::uint32_t> model = std::nullopt,
    const std::optional<CollisionHullOrdinal> hull = std::nullopt)
{
    switch (graph.state) {
    case GraphValidationState::valid:
        break;
    case GraphValidationState::invalid_node_reference:
        return failure(
            CollisionWorldBuildErrorCode::invalid_node_reference,
            "collision node graph contains an out-of-range reference",
            model,
            hull,
            graph.record);
    case GraphValidationState::invalid_leaf_reference:
        return failure(
            CollisionWorldBuildErrorCode::invalid_leaf_reference,
            "collision node graph contains an out-of-range leaf reference",
            model,
            hull,
            graph.record);
    case GraphValidationState::invalid_clipnode_reference:
        return failure(
            CollisionWorldBuildErrorCode::invalid_clipnode_reference,
            "collision clipnode graph contains an out-of-range reference",
            model,
            hull,
            graph.record);
    case GraphValidationState::invalid_contents:
        return failure(
            CollisionWorldBuildErrorCode::invalid_contents,
            "collision tree contains an unsupported contents terminal",
            model,
            hull,
            graph.record);
    case GraphValidationState::cycle:
        return failure(
            CollisionWorldBuildErrorCode::cycle_detected,
            "collision tree contains a cycle",
            model,
            hull,
            graph.record);
    case GraphValidationState::limit:
        return failure(
            CollisionWorldBuildErrorCode::traversal_limit_exceeded,
            "collision tree validation exceeded a configured bound",
            model,
            hull,
            graph.record);
    }
    return {};
}

[[nodiscard]] hlclient::collision::CollisionHull make_point_hull(
    const bsp::GoldSrcBspCollisionSourcePointHull& source,
    const hlclient::collision::CollisionHullProfile& profile)
{
    return hlclient::collision::CollisionHull{
        CollisionHullOrdinal::point,
        hlclient::collision::CollisionHullTreeDomain::node_leaf,
        hlclient::collision::CollisionHullRoot{
            hlclient::collision::CollisionHullRootKind::node,
            source.root.source_node_index,
            {},
        },
        profile,
    };
}

[[nodiscard]] hlclient::collision::CollisionHull make_clip_hull(
    const bsp::GoldSrcBspCollisionSourceClipHull& source,
    const CollisionHullOrdinal ordinal,
    const hlclient::collision::CollisionHullProfile& profile)
{
    hlclient::collision::CollisionHullRoot root;
    if (const auto* clipnode = std::get_if<
            bsp::GoldSrcBspCollisionSourceClipnodeReference>(&source.root)) {
        root.kind = hlclient::collision::CollisionHullRootKind::clipnode;
        root.index = clipnode->source_clipnode_index;
    } else {
        root.kind = hlclient::collision::CollisionHullRootKind::terminal;
        root.terminal = *convert_contents(
            std::get<bsp::GoldSrcContentsCode>(source.root));
    }
    return hlclient::collision::CollisionHull{
        ordinal,
        hlclient::collision::CollisionHullTreeDomain::clipnode,
        root,
        profile,
    };
}

} // namespace

bool valid_goldsrc_collision_build_limits(
    const GoldSrcCollisionBuildLimits& limits) noexcept
{
    return limits.maximum_planes > 0U &&
        limits.maximum_planes <= hlclient::collision::kCollisionHardMaximumPlanes &&
        limits.maximum_nodes > 0U &&
        limits.maximum_nodes <= hlclient::collision::kCollisionHardMaximumNodes &&
        limits.maximum_leaves > 0U &&
        limits.maximum_leaves <= hlclient::collision::kCollisionHardMaximumLeaves &&
        limits.maximum_clipnodes > 0U &&
        limits.maximum_clipnodes <=
            hlclient::collision::kCollisionHardMaximumClipnodes &&
        limits.maximum_models > 0U &&
        limits.maximum_models <= hlclient::collision::kCollisionHardMaximumModels &&
        limits.maximum_hulls >= hlclient::collision::kCollisionHullCount &&
        limits.maximum_hulls <= kGoldSrcCollisionHardMaximumHulls &&
        limits.maximum_reachable_links > 0U &&
        limits.maximum_reachable_links <=
            kGoldSrcCollisionHardMaximumReachableLinks &&
        limits.maximum_validation_steps > 0U &&
        limits.maximum_validation_steps <=
            kGoldSrcCollisionHardMaximumValidationSteps &&
        limits.maximum_tree_depth > 0U &&
        limits.maximum_tree_depth <= kGoldSrcCollisionHardMaximumTreeDepth &&
        limits.maximum_collision_bytes > 0U &&
        limits.maximum_collision_bytes <= kGoldSrcCollisionHardMaximumBytes;
}

std::string_view to_string(const CollisionWorldBuildErrorCode code) noexcept
{
    switch (code) {
    case CollisionWorldBuildErrorCode::invalid_configuration:
        return "invalid_configuration";
    case CollisionWorldBuildErrorCode::unsupported_profile:
        return "unsupported_profile";
    case CollisionWorldBuildErrorCode::count_limit_exceeded:
        return "count_limit_exceeded";
    case CollisionWorldBuildErrorCode::memory_limit_exceeded:
        return "memory_limit_exceeded";
    case CollisionWorldBuildErrorCode::invalid_plane:
        return "invalid_plane";
    case CollisionWorldBuildErrorCode::invalid_contents:
        return "invalid_contents";
    case CollisionWorldBuildErrorCode::invalid_node_reference:
        return "invalid_node_reference";
    case CollisionWorldBuildErrorCode::invalid_leaf_reference:
        return "invalid_leaf_reference";
    case CollisionWorldBuildErrorCode::invalid_clipnode_reference:
        return "invalid_clipnode_reference";
    case CollisionWorldBuildErrorCode::invalid_model:
        return "invalid_model";
    case CollisionWorldBuildErrorCode::invalid_hull_profile:
        return "invalid_hull_profile";
    case CollisionWorldBuildErrorCode::cycle_detected:
        return "cycle_detected";
    case CollisionWorldBuildErrorCode::traversal_limit_exceeded:
        return "traversal_limit_exceeded";
    case CollisionWorldBuildErrorCode::unable_to_publish:
        return "unable_to_publish";
    }
    return "unknown";
}

CollisionWorldBuildResult GoldSrcCollisionWorldBuilder::build(
    const bsp::GoldSrcBspCollisionSource& source,
    const GoldSrcCollisionBuildLimits& limits)
{
    if (!valid_goldsrc_collision_build_limits(limits)) {
        return failure(
            CollisionWorldBuildErrorCode::invalid_configuration,
            "collision build limits are invalid");
    }
    if (source.compatibility_profile !=
            bsp::GoldSrcBspCollisionCompatibilityProfile::
                valve_bsp_v30_clip_hulls_v1 ||
        source.evidence_profile !=
            bsp::GoldSrcBspCollisionEvidenceProfile::
                public_valve_bsp_compiler_and_original_map_validation ||
        source.source_bsp_version != bsp::kGoldSrcBspVersion) {
        return failure(
            CollisionWorldBuildErrorCode::unsupported_profile,
            "collision source profile is unsupported");
    }
    if (source.planes.empty() || source.nodes.empty() || source.leaves.empty() ||
        source.models.empty() || source.planes.size() > limits.maximum_planes ||
        source.nodes.size() > limits.maximum_nodes ||
        source.leaves.size() > limits.maximum_leaves ||
        source.clipnodes.size() > limits.maximum_clipnodes ||
        source.models.size() > limits.maximum_models ||
        source.models.size() > limits.maximum_hulls /
                                   hlclient::collision::kCollisionHullCount) {
        return failure(
            CollisionWorldBuildErrorCode::count_limit_exceeded,
            "collision source count exceeds a configured bound");
    }

    std::size_t retained_bytes = 0U;
    if (!account_records<hlclient::collision::CollisionPlane>(
            source.planes.size(), retained_bytes) ||
        !account_records<hlclient::collision::CollisionNode>(
            source.nodes.size(), retained_bytes) ||
        !account_records<hlclient::collision::CollisionLeaf>(
            source.leaves.size(), retained_bytes) ||
        !account_records<hlclient::collision::CollisionClipnode>(
            source.clipnodes.size(), retained_bytes) ||
        !account_records<hlclient::collision::CollisionModel>(
            source.models.size(), retained_bytes) ||
        retained_bytes > limits.maximum_collision_bytes) {
        return failure(
            CollisionWorldBuildErrorCode::memory_limit_exceeded,
            "collision package memory accounting exceeds a configured bound");
    }

    try {
        std::vector<hlclient::collision::CollisionPlane> planes;
        planes.reserve(source.planes.size());
        for (std::size_t index = 0U; index < source.planes.size(); ++index) {
            const auto& plane = source.planes[index];
            const auto length_squared =
                static_cast<double>(plane.normal.x) * plane.normal.x +
                static_cast<double>(plane.normal.y) * plane.normal.y +
                static_cast<double>(plane.normal.z) * plane.normal.z;
            if (plane.source_plane_index != index ||
                !finite_vector(plane.normal) || !std::isfinite(plane.distance) ||
                plane.source_type < 0 || plane.source_type > 5 ||
                std::abs(length_squared - 1.0) > 1.0e-5) {
                return failure(
                    CollisionWorldBuildErrorCode::invalid_plane,
                    "collision plane is not finite and normalized",
                    std::nullopt,
                    std::nullopt,
                    plane.source_plane_index);
            }
            planes.push_back(hlclient::collision::CollisionPlane{
                plane.normal,
                plane.distance,
                plane.source_plane_index,
                plane.source_type,
            });
        }

        std::vector<hlclient::collision::CollisionLeaf> leaves;
        leaves.reserve(source.leaves.size());
        for (std::size_t index = 0U; index < source.leaves.size(); ++index) {
            const auto& leaf = source.leaves[index];
            const auto contents = convert_contents(leaf.contents);
            if (leaf.source_leaf_index != index || !contents ||
                !finite_bounds(leaf.bounds)) {
                return failure(
                    contents
                        ? CollisionWorldBuildErrorCode::invalid_leaf_reference
                        : CollisionWorldBuildErrorCode::invalid_contents,
                    "collision leaf is invalid",
                    std::nullopt,
                    std::nullopt,
                    leaf.source_leaf_index);
            }
            leaves.push_back(hlclient::collision::CollisionLeaf{
                leaf.source_leaf_index,
                *contents,
            });
        }

        std::vector<hlclient::collision::CollisionNode> nodes;
        nodes.reserve(source.nodes.size());
        for (std::size_t index = 0U; index < source.nodes.size(); ++index) {
            const auto& node = source.nodes[index];
            if (node.source_node_index != index ||
                node.source_plane_index >= source.planes.size()) {
                return failure(
                    CollisionWorldBuildErrorCode::invalid_node_reference,
                    "collision node metadata is invalid",
                    std::nullopt,
                    std::nullopt,
                    node.source_node_index);
            }
            hlclient::collision::CollisionNode converted;
            converted.plane_index = node.source_plane_index;
            for (std::size_t side = 0U; side < node.children.size(); ++side) {
                if (const auto* child = std::get_if<
                        bsp::GoldSrcBspCollisionSourceNodeReference>(
                            &node.children[side])) {
                    if (child->source_node_index >= source.nodes.size()) {
                        return failure(
                            CollisionWorldBuildErrorCode::invalid_node_reference,
                            "collision node child is outside the node domain",
                            std::nullopt,
                            std::nullopt,
                            child->source_node_index);
                    }
                    converted.children[side] = {
                        hlclient::collision::CollisionNodeChildKind::node,
                        child->source_node_index,
                    };
                } else {
                    const auto leaf = std::get<
                        bsp::GoldSrcBspCollisionSourceLeafReference>(
                        node.children[side]);
                    if (leaf.source_leaf_index >= source.leaves.size()) {
                        return failure(
                            CollisionWorldBuildErrorCode::invalid_leaf_reference,
                            "collision node child is outside the leaf domain",
                            std::nullopt,
                            std::nullopt,
                            leaf.source_leaf_index);
                    }
                    converted.children[side] = {
                        hlclient::collision::CollisionNodeChildKind::leaf,
                        leaf.source_leaf_index,
                    };
                }
            }
            nodes.push_back(converted);
        }

        std::vector<hlclient::collision::CollisionClipnode> clipnodes;
        clipnodes.reserve(source.clipnodes.size());
        for (std::size_t index = 0U; index < source.clipnodes.size(); ++index) {
            const auto& node = source.clipnodes[index];
            if (node.source_clipnode_index != index ||
                node.source_plane_index >= source.planes.size()) {
                return failure(
                    CollisionWorldBuildErrorCode::invalid_clipnode_reference,
                    "collision clipnode metadata is invalid",
                    std::nullopt,
                    std::nullopt,
                    node.source_clipnode_index);
            }
            hlclient::collision::CollisionClipnode converted;
            converted.plane_index = node.source_plane_index;
            for (std::size_t side = 0U; side < node.children.size(); ++side) {
                if (const auto* child = std::get_if<
                        bsp::GoldSrcBspCollisionSourceClipnodeReference>(
                            &node.children[side])) {
                    if (child->source_clipnode_index >= source.clipnodes.size()) {
                        return failure(
                            CollisionWorldBuildErrorCode::invalid_clipnode_reference,
                            "collision clipnode child is outside the clipnode domain",
                            std::nullopt,
                            std::nullopt,
                            child->source_clipnode_index);
                    }
                    converted.children[side].kind =
                        hlclient::collision::CollisionClipnodeChildKind::clipnode;
                    converted.children[side].index =
                        child->source_clipnode_index;
                } else {
                    const auto contents = convert_contents(
                        std::get<bsp::GoldSrcContentsCode>(
                            node.children[side]));
                    if (!contents) {
                        return failure(
                            CollisionWorldBuildErrorCode::invalid_contents,
                            "clipnode has an unsupported contents terminal",
                            std::nullopt,
                            std::nullopt,
                            node.source_clipnode_index);
                    }
                    converted.children[side].kind =
                        hlclient::collision::CollisionClipnodeChildKind::terminal;
                    converted.children[side].terminal = *contents;
                }
            }
            clipnodes.push_back(converted);
        }

        std::vector<std::uint32_t> node_roots;
        node_roots.reserve(source.models.size());
        std::vector<std::uint32_t> clipnode_roots;
        clipnode_roots.reserve(
            source.models.size() * (hlclient::collision::kCollisionHullCount - 1U));
        std::uint64_t direct_terminal_root_count = 0U;
        std::vector<hlclient::collision::CollisionModel> models;
        models.reserve(source.models.size());
        for (const auto& model : source.models) {
            if (model.source_model_index >= source.models.size() ||
                model.source_model_index != models.size() ||
                !finite_vector(model.source_origin) ||
                !finite_bounds(model.source_bounds)) {
                return failure(
                    CollisionWorldBuildErrorCode::invalid_model,
                    "collision model metadata is invalid",
                    model.source_model_index);
            }

            const auto point_ordinal = convert_ordinal(model.point_hull.ordinal);
            const auto standing_ordinal =
                convert_ordinal(model.standing_hull.ordinal);
            const auto large_ordinal = convert_ordinal(model.large_hull.ordinal);
            const auto duck_ordinal = convert_ordinal(model.duck_hull.ordinal);
            if (!point_ordinal || !standing_ordinal || !large_ordinal ||
                !duck_ordinal || *point_ordinal != CollisionHullOrdinal::point ||
                *standing_ordinal !=
                    CollisionHullOrdinal::standing_32x32x72 ||
                *large_ordinal != CollisionHullOrdinal::large_64_cube ||
                *duck_ordinal != CollisionHullOrdinal::duck_32x32x36) {
                return failure(
                    CollisionWorldBuildErrorCode::invalid_hull_profile,
                    "collision model hull ordinals are invalid",
                    model.source_model_index);
            }

            const auto point_profile =
                hlclient::collision::standard_collision_hull_profile(
                    *point_ordinal);
            const auto standing_profile =
                hlclient::collision::standard_collision_hull_profile(
                    *standing_ordinal);
            const auto large_profile =
                hlclient::collision::standard_collision_hull_profile(
                    *large_ordinal);
            const auto duck_profile =
                hlclient::collision::standard_collision_hull_profile(
                    *duck_ordinal);
            if (!point_profile || !standing_profile || !large_profile ||
                !duck_profile ||
                !exact_profile(model.point_hull, *point_profile) ||
                !exact_profile(model.standing_hull, *standing_profile) ||
                !exact_profile(model.large_hull, *large_profile) ||
                !exact_profile(model.duck_hull, *duck_profile)) {
                return failure(
                    CollisionWorldBuildErrorCode::invalid_hull_profile,
                    "collision model does not use the exact four-hull profile",
                    model.source_model_index);
            }

            if (model.point_hull.root.source_node_index >= source.nodes.size()) {
                return failure(
                    CollisionWorldBuildErrorCode::invalid_node_reference,
                    "point hull root is outside the node domain",
                    model.source_model_index,
                    *point_ordinal,
                    model.point_hull.root.source_node_index);
            }
            node_roots.push_back(model.point_hull.root.source_node_index);
            for (const auto* source_hull : std::array{
                     &model.standing_hull, &model.large_hull, &model.duck_hull}) {
                const auto ordinal = *convert_ordinal(source_hull->ordinal);
                if (const auto* root = std::get_if<
                        bsp::GoldSrcBspCollisionSourceClipnodeReference>(
                        &source_hull->root)) {
                    if (root->source_clipnode_index >= source.clipnodes.size()) {
                        return failure(
                            CollisionWorldBuildErrorCode::
                                invalid_clipnode_reference,
                            "clip hull root is outside the clipnode domain",
                            model.source_model_index,
                            ordinal,
                            root->source_clipnode_index);
                    }
                    clipnode_roots.push_back(root->source_clipnode_index);
                } else {
                    const auto contents = std::get<bsp::GoldSrcContentsCode>(
                        source_hull->root);
                    if (!convert_contents(contents)) {
                        return failure(
                            CollisionWorldBuildErrorCode::invalid_contents,
                            "clip hull direct root has unsupported contents",
                            model.source_model_index,
                            ordinal);
                    }
                    ++direct_terminal_root_count;
                }
            }

            hlclient::collision::CollisionModel converted;
            converted.source_model_index = model.source_model_index;
            converted.source_origin = model.source_origin;
            converted.source_bounds = model.source_bounds;
            converted.first_source_face = model.first_source_face;
            converted.source_face_count = model.source_face_count;
            converted.hulls[0U] =
                make_point_hull(model.point_hull, *point_profile);
            converted.hulls[1U] = make_clip_hull(
                model.standing_hull, *standing_ordinal, *standing_profile);
            converted.hulls[2U] = make_clip_hull(
                model.large_hull, *large_ordinal, *large_profile);
            converted.hulls[3U] = make_clip_hull(
                model.duck_hull, *duck_ordinal, *duck_profile);
            models.push_back(converted);
        }

        TraversalBudget budget{0U, 0U, limits};
        const auto node_graph = validate_node_graph(source, node_roots, budget);
        if (node_graph.result.state != GraphValidationState::valid) {
            return graph_failure(node_graph.result);
        }
        const auto clipnode_graph =
            validate_clip_graph(source, clipnode_roots, budget);
        if (clipnode_graph.result.state != GraphValidationState::valid) {
            return graph_failure(clipnode_graph.result);
        }

        const auto model_hull_root_count =
            static_cast<std::uint64_t>(source.models.size()) *
            hlclient::collision::kCollisionHullCount;
        const auto maximum_tree_depth = std::max(
            node_graph.maximum_root_depth,
            clipnode_graph.maximum_root_depth);
        auto package = std::make_shared<
            const hlclient::collision::CollisionWorldPackage>(
            std::move(planes),
            std::move(nodes),
            std::move(leaves),
            std::move(clipnodes),
            std::move(models),
            hlclient::collision::CollisionWorldIdentity{
                source.source_fingerprint,
                static_cast<std::uint64_t>(source.source_bsp_version),
            },
            hlclient::collision::CollisionWorldStatistics{
                static_cast<std::uint64_t>(source.planes.size()),
                static_cast<std::uint64_t>(source.nodes.size()),
                static_cast<std::uint64_t>(source.leaves.size()),
                static_cast<std::uint64_t>(source.clipnodes.size()),
                static_cast<std::uint64_t>(source.models.size()),
                node_graph.reachable_count,
                clipnode_graph.reachable_count,
                clipnode_graph.unreachable_count,
                model_hull_root_count,
                direct_terminal_root_count,
                maximum_tree_depth,
            });
        return CollisionWorldBuildResult{std::move(package), std::nullopt};
    } catch (const std::bad_alloc&) {
        return failure(
            CollisionWorldBuildErrorCode::memory_limit_exceeded,
            "collision package allocation failed");
    } catch (...) {
        return failure(
            CollisionWorldBuildErrorCode::unable_to_publish,
            "collision package construction failed");
    }
}

CollisionWorldBuildResult GoldSrcCollisionWorldBuilder::build(
    const bsp::GoldSrcBspParsedDocument& document,
    const GoldSrcCollisionBuildLimits& limits)
{
    return build(document.collision_source, limits);
}

} // namespace hlclient::goldsrc::collision
