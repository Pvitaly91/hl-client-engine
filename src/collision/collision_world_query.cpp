#include <hlclient/collision/collision_world_query.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace hlclient::collision {
namespace {

template <typename Function>
class ScopeExit final {
public:
    explicit ScopeExit(Function function) noexcept
        : function_{std::move(function)}
    {
    }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ~ScopeExit() noexcept { function_(); }

private:
    Function function_;
};

constexpr double kMaximumPlaneDistanceEpsilon = 1.0e-2;
constexpr double kMaximumFractionEpsilon = 1.0e-6;
constexpr double kMaximumProgressFraction = 1.0e-6;
constexpr double kPlaneUnitTolerance = 1.0e-3;

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    output = left + right;
    return true;
}

[[nodiscard]] bool checked_multiply(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    output = left * right;
    return true;
}

[[nodiscard]] bool finite_vector(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool ordered_bounds(const assets::WorldBounds& bounds) noexcept
{
    return finite_vector(bounds.minimum) && finite_vector(bounds.maximum) &&
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

[[nodiscard]] bool same_vector(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] bool valid_plane(const CollisionPlane& plane) noexcept
{
    if (!finite_vector(plane.normal) || !std::isfinite(plane.distance) ||
        plane.source_type < 0 || plane.source_type > 5) {
        return false;
    }
    const auto length_squared =
        static_cast<double>(plane.normal.x) * plane.normal.x +
        static_cast<double>(plane.normal.y) * plane.normal.y +
        static_cast<double>(plane.normal.z) * plane.normal.z;
    if (!std::isfinite(length_squared) || !(length_squared > 0.0)) {
        return false;
    }
    const auto length = std::sqrt(length_squared);
    return std::isfinite(length) &&
        std::abs(length - 1.0) <= kPlaneUnitTolerance;
}

[[nodiscard]] std::optional<CollisionContents> checked_contents(
    const CollisionContents& value) noexcept
{
    const auto decoded = decode_goldsrc_contents(value.source);
    if (!decoded || decoded->category != value.category) {
        return std::nullopt;
    }
    return decoded;
}

[[nodiscard]] bool supported_trace_profile(
    const CollisionTraceCompatibilityProfile profile) noexcept
{
    return profile == CollisionTraceCompatibilityProfile::
        project_deterministic_bsp_hull_trace_v1;
}

[[nodiscard]] CollisionPointContentsQueryResult point_failure(
    const CollisionQueryErrorCode code,
    const std::uint32_t model_index,
    const std::optional<std::uint32_t> source_element_index,
    const std::size_t steps) noexcept
{
    return {
        std::nullopt,
        CollisionQueryError{code, model_index, source_element_index, steps},
    };
}

[[nodiscard]] CollisionPositionTestQueryResult position_failure(
    CollisionQueryError error) noexcept
{
    return {std::nullopt, std::move(error)};
}

[[nodiscard]] CollisionTraceQueryResult trace_failure(
    const CollisionQueryErrorCode code,
    const std::uint32_t model_index,
    const std::optional<std::uint32_t> source_element_index,
    const std::size_t steps) noexcept
{
    return {
        std::nullopt,
        CollisionQueryError{code, model_index, source_element_index, steps},
    };
}

struct ResolvedHull {
    const CollisionModel* model{nullptr};
    const CollisionHull* hull{nullptr};
};

[[nodiscard]] std::optional<CollisionQueryErrorCode> validate_package_shape(
    const CollisionWorldPackage& package) noexcept
{
    if (package.compatibility_profile() !=
            CollisionWorldCompatibilityProfile::
                valve_bsp_v30_clip_hulls_v1 ||
        package.evidence_profile() !=
            CollisionWorldEvidenceProfile::
                public_valve_bsp_compiler_and_original_map_validation ||
        package.planes().empty() || package.nodes().empty() ||
        package.leaves().empty() || package.models().empty() ||
        package.planes().size() > kCollisionHardMaximumPlanes ||
        package.nodes().size() > kCollisionHardMaximumNodes ||
        package.leaves().size() > kCollisionHardMaximumLeaves ||
        package.clipnodes().size() > kCollisionHardMaximumClipnodes ||
        package.models().size() > kCollisionHardMaximumModels) {
        return CollisionQueryErrorCode::invalid_package;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<CollisionQueryErrorCode> resolve_hull(
    const CollisionWorldPackage& package,
    const std::uint32_t source_model_index,
    const CollisionHullOrdinal ordinal,
    ResolvedHull& output) noexcept
{
    if (!collision_hull_ordinal(static_cast<std::size_t>(ordinal))) {
        return CollisionQueryErrorCode::invalid_hull;
    }

    const CollisionModel* found = nullptr;
    for (const auto& candidate : package.models()) {
        if (candidate.source_model_index != source_model_index) {
            continue;
        }
        if (found != nullptr) {
            return CollisionQueryErrorCode::invalid_package;
        }
        found = &candidate;
    }
    if (found == nullptr) {
        return CollisionQueryErrorCode::invalid_model;
    }
    if (!finite_vector(found->source_origin) ||
        !ordered_bounds(found->source_bounds)) {
        return CollisionQueryErrorCode::invalid_model;
    }

    const auto* hull = found->hull(ordinal);
    if (hull == nullptr) {
        return CollisionQueryErrorCode::invalid_hull;
    }
    const auto expected_profile = standard_collision_hull_profile(ordinal);
    if (!expected_profile || hull->profile != *expected_profile ||
        !same_vector(hull->profile.clip_mins, expected_profile->clip_mins) ||
        !same_vector(hull->profile.clip_maxs, expected_profile->clip_maxs)) {
        return CollisionQueryErrorCode::invalid_hull;
    }

    if (ordinal == CollisionHullOrdinal::point) {
        if (hull->domain != CollisionHullTreeDomain::node_leaf ||
            hull->root.kind != CollisionHullRootKind::node) {
            return CollisionQueryErrorCode::invalid_hull;
        }
    } else if (hull->domain != CollisionHullTreeDomain::clipnode ||
        (hull->root.kind != CollisionHullRootKind::clipnode &&
            hull->root.kind != CollisionHullRootKind::terminal)) {
        return CollisionQueryErrorCode::invalid_hull;
    }

    output = ResolvedHull{found, hull};
    return std::nullopt;
}

[[nodiscard]] detail::CollisionScratchReference root_reference(
    const CollisionHullRoot& root) noexcept
{
    using Kind = detail::CollisionScratchReferenceKind;
    switch (root.kind) {
    case CollisionHullRootKind::node:
        return {Kind::node, root.index, {}};
    case CollisionHullRootKind::clipnode:
        return {Kind::clipnode, root.index, {}};
    case CollisionHullRootKind::terminal:
        return {Kind::terminal, 0U, root.terminal};
    }
    return {static_cast<Kind>(0xFFU), root.index, root.terminal};
}

[[nodiscard]] std::optional<detail::CollisionScratchReference>
node_child_reference(const CollisionNodeChild& child) noexcept
{
    using Kind = detail::CollisionScratchReferenceKind;
    switch (child.kind) {
    case CollisionNodeChildKind::node:
        return detail::CollisionScratchReference{Kind::node, child.index, {}};
    case CollisionNodeChildKind::leaf:
        return detail::CollisionScratchReference{Kind::leaf, child.index, {}};
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<detail::CollisionScratchReference>
clipnode_child_reference(const CollisionClipnodeChild& child) noexcept
{
    using Kind = detail::CollisionScratchReferenceKind;
    switch (child.kind) {
    case CollisionClipnodeChildKind::clipnode:
        return detail::CollisionScratchReference{
            Kind::clipnode, child.index, {}};
    case CollisionClipnodeChildKind::terminal:
        return detail::CollisionScratchReference{
            Kind::terminal, 0U, child.terminal};
    }
    return std::nullopt;
}

[[nodiscard]] double signed_distance(
    const assets::AssetVector3& point,
    const CollisionPlane& plane) noexcept
{
    return static_cast<double>(point.x) * plane.normal.x +
        static_cast<double>(point.y) * plane.normal.y +
        static_cast<double>(point.z) * plane.normal.z - plane.distance;
}

struct DoubleVector3 {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

[[nodiscard]] DoubleVector3 double_point_at_fraction(
    const assets::AssetVector3& start,
    const assets::AssetVector3& end,
    const double fraction) noexcept
{
    return {
        std::fma(fraction,
            static_cast<double>(end.x) - start.x,
            static_cast<double>(start.x)),
        std::fma(fraction,
            static_cast<double>(end.y) - start.y,
            static_cast<double>(start.y)),
        std::fma(fraction,
            static_cast<double>(end.z) - start.z,
            static_cast<double>(start.z)),
    };
}

[[nodiscard]] bool finite_vector(const DoubleVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] double signed_distance(
    const DoubleVector3& point,
    const CollisionPlane& plane) noexcept
{
    return point.x * plane.normal.x + point.y * plane.normal.y +
        point.z * plane.normal.z - plane.distance;
}

[[nodiscard]] assets::AssetVector3 point_at_fraction(
    const assets::AssetVector3& start,
    const assets::AssetVector3& end,
    const double fraction) noexcept
{
    return {
        static_cast<float>(std::fma(
            fraction,
            static_cast<double>(end.x) - start.x,
            static_cast<double>(start.x))),
        static_cast<float>(std::fma(
            fraction,
            static_cast<double>(end.y) - start.y,
            static_cast<double>(start.y))),
        static_cast<float>(std::fma(
            fraction,
            static_cast<double>(end.z) - start.z,
            static_cast<double>(start.z))),
    };
}

[[nodiscard]] bool same_point(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return same_vector(left, right);
}

void snap_single_near_crossing_endpoint(
    double& start_distance,
    double& end_distance,
    const CollisionTraceToleranceProfile& tolerance) noexcept
{
    const bool start_near =
        std::abs(start_distance) <= tolerance.plane_distance_epsilon;
    const bool end_near =
        std::abs(end_distance) <= tolerance.plane_distance_epsilon;
    // Preserve the raw ratio when both crossing endpoints are in the band;
    // snapping both would destroy their signs and produce a zero denominator.
    if (start_near == end_near) {
        return;
    }
    if (start_near) {
        start_distance = 0.0;
    } else {
        end_distance = 0.0;
    }
}

[[nodiscard]] CollisionTraceHit hit_for_model(
    const std::uint32_t model_index) noexcept
{
    return {
        model_index == 0U ? CollisionTraceHitKind::world
                          : CollisionTraceHitKind::collision_model,
        model_index,
    };
}

[[nodiscard]] std::optional<CollisionPlaneHit> make_plane_hit(
    const CollisionWorldPackage& package,
    const detail::CollisionScratchBoundary boundary,
    const DoubleVector3& motion,
    const CollisionTraceToleranceProfile& tolerance) noexcept
{
    if (!boundary.present ||
        static_cast<std::size_t>(boundary.plane_index) >=
            package.planes().size()) {
        return std::nullopt;
    }
    const auto& source = package.planes()[boundary.plane_index];
    if (!valid_plane(source)) {
        return std::nullopt;
    }
    const auto length = std::sqrt(
        static_cast<double>(source.normal.x) * source.normal.x +
        static_cast<double>(source.normal.y) * source.normal.y +
        static_cast<double>(source.normal.z) * source.normal.z);
    const auto sign = boundary.inverted ? -1.0 : 1.0;
    CollisionPlaneHit output;
    output.normal = {
        static_cast<float>(sign * source.normal.x / length),
        static_cast<float>(sign * source.normal.y / length),
        static_cast<float>(sign * source.normal.z / length),
    };
    output.distance = sign * source.distance / length;
    output.source_plane_index = source.source_plane_index;
    output.orientation = boundary.inverted
        ? CollisionPlaneOrientation::inverted_source
        : CollisionPlaneOrientation::source;
    if (!finite_vector(output.normal) || !std::isfinite(output.distance)) {
        return std::nullopt;
    }
    const auto motion_dot = static_cast<double>(output.normal.x) * motion.x +
        static_cast<double>(output.normal.y) * motion.y +
        static_cast<double>(output.normal.z) * motion.z;
    if (!std::isfinite(motion_dot) ||
        motion_dot > tolerance.plane_distance_epsilon) {
        return std::nullopt;
    }
    return output;
}

} // namespace

bool valid_collision_query_limits(const CollisionQueryLimits& limits) noexcept
{
    return limits.maximum_traversal_steps > 0U &&
        limits.maximum_traversal_steps <=
            kCollisionHardMaximumTraversalSteps &&
        limits.maximum_stack_entries > 0U &&
        limits.maximum_stack_entries <= kCollisionHardMaximumStackEntries &&
        limits.maximum_fraction_splits > 0U &&
        limits.maximum_fraction_splits <=
            kCollisionHardMaximumFractionSplits &&
        limits.maximum_query_scratch_bytes > 0U &&
        limits.maximum_query_scratch_bytes <=
            kCollisionHardMaximumQueryScratchBytes;
}

bool valid_collision_trace_tolerance_profile(
    const CollisionTraceToleranceProfile& profile) noexcept
{
    return std::isfinite(profile.plane_distance_epsilon) &&
        profile.plane_distance_epsilon >= 0.0 &&
        profile.plane_distance_epsilon <= kMaximumPlaneDistanceEpsilon &&
        std::isfinite(profile.fraction_epsilon) &&
        profile.fraction_epsilon > 0.0 &&
        profile.fraction_epsilon <= kMaximumFractionEpsilon &&
        std::isfinite(profile.minimum_progress_fraction) &&
        profile.minimum_progress_fraction > 0.0 &&
        profile.minimum_progress_fraction <= kMaximumProgressFraction;
}

std::string_view to_string(const CollisionQueryErrorCode code) noexcept
{
    switch (code) {
    case CollisionQueryErrorCode::invalid_configuration:
        return "invalid_configuration";
    case CollisionQueryErrorCode::invalid_point: return "invalid_point";
    case CollisionQueryErrorCode::invalid_segment: return "invalid_segment";
    case CollisionQueryErrorCode::invalid_package: return "invalid_package";
    case CollisionQueryErrorCode::invalid_model: return "invalid_model";
    case CollisionQueryErrorCode::invalid_hull: return "invalid_hull";
    case CollisionQueryErrorCode::invalid_plane: return "invalid_plane";
    case CollisionQueryErrorCode::invalid_child: return "invalid_child";
    case CollisionQueryErrorCode::invalid_contents: return "invalid_contents";
    case CollisionQueryErrorCode::unsupported_contents_policy:
        return "unsupported_contents_policy";
    case CollisionQueryErrorCode::unsupported_trace_profile:
        return "unsupported_trace_profile";
    case CollisionQueryErrorCode::unsupported_arbitrary_hull:
        return "unsupported_arbitrary_hull";
    case CollisionQueryErrorCode::cycle_detected: return "cycle_detected";
    case CollisionQueryErrorCode::traversal_step_limit_exceeded:
        return "traversal_step_limit_exceeded";
    case CollisionQueryErrorCode::stack_limit_exceeded:
        return "stack_limit_exceeded";
    case CollisionQueryErrorCode::fraction_split_limit_exceeded:
        return "fraction_split_limit_exceeded";
    case CollisionQueryErrorCode::scratch_limit_exceeded:
        return "scratch_limit_exceeded";
    case CollisionQueryErrorCode::unable_to_prepare_scratch:
        return "unable_to_prepare_scratch";
    case CollisionQueryErrorCode::insufficient_fraction_progress:
        return "insufficient_fraction_progress";
    case CollisionQueryErrorCode::non_finite_result:
        return "non_finite_result";
    }
    return "unknown";
}

std::size_t CollisionQueryScratch::retained_bytes() const noexcept
{
    std::size_t frame_bytes = 0U;
    std::size_t output = 0U;
    if (!checked_multiply(
            frames_.capacity(), sizeof(detail::CollisionScratchFrame), frame_bytes) ||
        !checked_add(frame_bytes, active_nodes_.capacity(), output) ||
        !checked_add(output, active_clipnodes_.capacity(), output)) {
        return std::numeric_limits<std::size_t>::max();
    }
    return output;
}

std::optional<CollisionQueryErrorCode> CollisionQueryScratch::prepare(
    const std::size_t node_count,
    const std::size_t clipnode_count,
    const CollisionQueryLimits& limits) noexcept
{
    std::size_t frame_bytes = 0U;
    std::size_t required_bytes = 0U;
    if (!checked_multiply(limits.maximum_stack_entries,
            sizeof(detail::CollisionScratchFrame),
            frame_bytes) ||
        !checked_add(frame_bytes, node_count, required_bytes) ||
        !checked_add(required_bytes, clipnode_count, required_bytes) ||
        required_bytes > limits.maximum_query_scratch_bytes) {
        return CollisionQueryErrorCode::scratch_limit_exceeded;
    }
    if (retained_bytes() > limits.maximum_query_scratch_bytes) {
        return CollisionQueryErrorCode::scratch_limit_exceeded;
    }
    try {
        if (active_nodes_.size() < node_count) {
            active_nodes_.resize(node_count, 0U);
        }
        if (active_clipnodes_.size() < clipnode_count) {
            active_clipnodes_.resize(clipnode_count, 0U);
        }
        if (frames_.capacity() < limits.maximum_stack_entries) {
            frames_.reserve(limits.maximum_stack_entries);
        }
    } catch (const std::bad_alloc&) {
        return CollisionQueryErrorCode::unable_to_prepare_scratch;
    } catch (const std::length_error&) {
        return CollisionQueryErrorCode::unable_to_prepare_scratch;
    }
    if (retained_bytes() > limits.maximum_query_scratch_bytes) {
        return CollisionQueryErrorCode::scratch_limit_exceeded;
    }
    stack_limit_ = limits.maximum_stack_entries;
    reset();
    return std::nullopt;
}

void CollisionQueryScratch::reset() noexcept
{
    frames_.clear();
    std::fill(
        active_nodes_.begin(), active_nodes_.end(), std::uint8_t{0U});
    std::fill(active_clipnodes_.begin(),
        active_clipnodes_.end(),
        std::uint8_t{0U});
}

CollisionWorldQuery::CollisionWorldQuery(
    std::shared_ptr<const CollisionWorldPackage> package) noexcept
    : package_{std::move(package)}
{
}

const std::shared_ptr<const CollisionWorldPackage>& CollisionWorldQuery::package()
    const noexcept
{
    return package_;
}

CollisionPointContentsQueryResult CollisionWorldQuery::point_contents(
    const CollisionPointContentsRequest& request,
    CollisionQueryScratch& scratch) const
{
    if (!valid_collision_query_limits(request.limits)) {
        return point_failure(CollisionQueryErrorCode::invalid_configuration,
            request.source_model_index,
            std::nullopt,
            0U);
    }
    if (!supported_collision_contents_policy(request.contents_policy)) {
        return point_failure(
            CollisionQueryErrorCode::unsupported_contents_policy,
            request.source_model_index,
            std::nullopt,
            0U);
    }
    if (!finite_vector(request.point)) {
        return point_failure(CollisionQueryErrorCode::invalid_point,
            request.source_model_index,
            std::nullopt,
            0U);
    }
    if (package_ == nullptr) {
        return point_failure(CollisionQueryErrorCode::invalid_package,
            request.source_model_index,
            std::nullopt,
            0U);
    }
    if (const auto package_error = validate_package_shape(*package_)) {
        return point_failure(*package_error,
            request.source_model_index,
            std::nullopt,
            0U);
    }

    ResolvedHull resolved;
    if (const auto error = resolve_hull(*package_,
            request.source_model_index,
            request.hull,
            resolved)) {
        return point_failure(*error,
            request.source_model_index,
            std::nullopt,
            0U);
    }
    if (const auto scratch_error = scratch.prepare(
            package_->nodes().size(),
            package_->clipnodes().size(),
            request.limits)) {
        return point_failure(*scratch_error,
            request.source_model_index,
            std::nullopt,
            0U);
    }
    const ScopeExit reset_scratch{[&scratch]() noexcept { scratch.reset(); }};

    auto reference = root_reference(resolved.hull->root);
    std::size_t steps = 0U;
    while (true) {
        using Kind = detail::CollisionScratchReferenceKind;
        switch (reference.kind) {
        case Kind::terminal: {
            const auto contents = checked_contents(reference.terminal);
            scratch.reset();
            if (!contents) {
                return point_failure(CollisionQueryErrorCode::invalid_contents,
                    request.source_model_index,
                    std::nullopt,
                    steps);
            }
            return {
                CollisionPointContentsResult{
                    *contents,
                    request.source_model_index,
                    request.hull,
                    steps,
                    CollisionTraceCompatibilityProfile::
                        project_deterministic_bsp_hull_trace_v1,
                },
                std::nullopt,
            };
        }
        case Kind::leaf: {
            if (static_cast<std::size_t>(reference.index) >=
                package_->leaves().size()) {
                scratch.reset();
                return point_failure(CollisionQueryErrorCode::invalid_child,
                    request.source_model_index,
                    reference.index,
                    steps);
            }
            const auto contents = checked_contents(
                package_->leaves()[reference.index].contents);
            scratch.reset();
            if (!contents) {
                return point_failure(CollisionQueryErrorCode::invalid_contents,
                    request.source_model_index,
                    reference.index,
                    steps);
            }
            return {
                CollisionPointContentsResult{
                    *contents,
                    request.source_model_index,
                    request.hull,
                    steps,
                    CollisionTraceCompatibilityProfile::
                        project_deterministic_bsp_hull_trace_v1,
                },
                std::nullopt,
            };
        }
        case Kind::node: {
            if (steps >= request.limits.maximum_traversal_steps) {
                scratch.reset();
                return point_failure(
                    CollisionQueryErrorCode::traversal_step_limit_exceeded,
                    request.source_model_index,
                    reference.index,
                    steps);
            }
            if (static_cast<std::size_t>(reference.index) >=
                package_->nodes().size()) {
                scratch.reset();
                return point_failure(CollisionQueryErrorCode::invalid_child,
                    request.source_model_index,
                    reference.index,
                    steps);
            }
            if (scratch.active_nodes_[reference.index] != 0U) {
                scratch.reset();
                return point_failure(CollisionQueryErrorCode::cycle_detected,
                    request.source_model_index,
                    reference.index,
                    steps);
            }
            scratch.active_nodes_[reference.index] = 1U;
            ++steps;
            const auto& node = package_->nodes()[reference.index];
            if (static_cast<std::size_t>(node.plane_index) >=
                    package_->planes().size() ||
                !valid_plane(package_->planes()[node.plane_index])) {
                scratch.reset();
                return point_failure(CollisionQueryErrorCode::invalid_plane,
                    request.source_model_index,
                    node.plane_index,
                    steps);
            }
            const auto distance = signed_distance(
                request.point, package_->planes()[node.plane_index]);
            if (!std::isfinite(distance)) {
                scratch.reset();
                return point_failure(CollisionQueryErrorCode::non_finite_result,
                    request.source_model_index,
                    node.plane_index,
                    steps);
            }
            const auto child = node_child_reference(
                node.children[distance >= 0.0 ? 0U : 1U]);
            if (!child) {
                scratch.reset();
                return point_failure(CollisionQueryErrorCode::invalid_child,
                    request.source_model_index,
                    reference.index,
                    steps);
            }
            reference = *child;
            break;
        }
        case Kind::clipnode: {
            if (steps >= request.limits.maximum_traversal_steps) {
                scratch.reset();
                return point_failure(
                    CollisionQueryErrorCode::traversal_step_limit_exceeded,
                    request.source_model_index,
                    reference.index,
                    steps);
            }
            if (static_cast<std::size_t>(reference.index) >=
                package_->clipnodes().size()) {
                scratch.reset();
                return point_failure(CollisionQueryErrorCode::invalid_child,
                    request.source_model_index,
                    reference.index,
                    steps);
            }
            if (scratch.active_clipnodes_[reference.index] != 0U) {
                scratch.reset();
                return point_failure(CollisionQueryErrorCode::cycle_detected,
                    request.source_model_index,
                    reference.index,
                    steps);
            }
            scratch.active_clipnodes_[reference.index] = 1U;
            ++steps;
            const auto& node = package_->clipnodes()[reference.index];
            if (static_cast<std::size_t>(node.plane_index) >=
                    package_->planes().size() ||
                !valid_plane(package_->planes()[node.plane_index])) {
                scratch.reset();
                return point_failure(CollisionQueryErrorCode::invalid_plane,
                    request.source_model_index,
                    node.plane_index,
                    steps);
            }
            const auto distance = signed_distance(
                request.point, package_->planes()[node.plane_index]);
            if (!std::isfinite(distance)) {
                scratch.reset();
                return point_failure(CollisionQueryErrorCode::non_finite_result,
                    request.source_model_index,
                    node.plane_index,
                    steps);
            }
            const auto child = clipnode_child_reference(
                node.children[distance >= 0.0 ? 0U : 1U]);
            if (!child) {
                scratch.reset();
                return point_failure(CollisionQueryErrorCode::invalid_child,
                    request.source_model_index,
                    reference.index,
                    steps);
            }
            reference = *child;
            break;
        }
        default:
            scratch.reset();
            return point_failure(CollisionQueryErrorCode::invalid_child,
                request.source_model_index,
                reference.index,
                steps);
        }
    }
}

CollisionPositionTestQueryResult CollisionWorldQuery::test_position(
    const CollisionPointContentsRequest& request,
    CollisionQueryScratch& scratch) const
{
    auto queried = point_contents(request, scratch);
    if (!queried || !queried.result) {
        if (queried.error) {
            return position_failure(std::move(*queried.error));
        }
        return position_failure(CollisionQueryError{
            CollisionQueryErrorCode::invalid_package,
            request.source_model_index,
            std::nullopt,
            0U});
    }
    const auto& point = *queried.result;
    return {
        CollisionPositionTestResult{
            blocks(request.contents_policy, point.contents.category)
                ? CollisionPositionStatus::blocking
                : CollisionPositionStatus::free,
            point.contents,
            point.traversal_depth,
        },
        std::nullopt,
    };
}

CollisionTraceQueryResult CollisionWorldQuery::trace_line(
    const CollisionTraceRequest& request,
    CollisionQueryScratch& scratch) const
{
    if (request.source_model_index != 0U ||
        request.hull != CollisionHullOrdinal::point) {
        return trace_failure(CollisionQueryErrorCode::invalid_hull,
            request.source_model_index,
            std::nullopt,
            0U);
    }
    return trace_model_hull(request, scratch);
}

CollisionTraceQueryResult CollisionWorldQuery::trace_hull(
    const CollisionTraceRequest& request,
    CollisionQueryScratch& scratch) const
{
    if (request.source_model_index != 0U) {
        return trace_failure(CollisionQueryErrorCode::invalid_model,
            request.source_model_index,
            std::nullopt,
            0U);
    }
    return trace_model_hull(request, scratch);
}

CollisionTraceQueryResult CollisionWorldQuery::trace_model_hull(
    const CollisionTraceRequest& request,
    CollisionQueryScratch& scratch) const
{
    if (!valid_collision_query_limits(request.limits) ||
        !valid_collision_trace_tolerance_profile(request.tolerance)) {
        return trace_failure(CollisionQueryErrorCode::invalid_configuration,
            request.source_model_index,
            std::nullopt,
            0U);
    }
    if (!supported_collision_contents_policy(request.contents_policy)) {
        return trace_failure(
            CollisionQueryErrorCode::unsupported_contents_policy,
            request.source_model_index,
            std::nullopt,
            0U);
    }
    if (!supported_trace_profile(request.trace_profile)) {
        return trace_failure(
            CollisionQueryErrorCode::unsupported_trace_profile,
            request.source_model_index,
            std::nullopt,
            0U);
    }
    if (!finite_vector(request.start) || !finite_vector(request.end)) {
        return trace_failure(CollisionQueryErrorCode::invalid_segment,
            request.source_model_index,
            std::nullopt,
            0U);
    }
    if (package_ == nullptr) {
        return trace_failure(CollisionQueryErrorCode::invalid_package,
            request.source_model_index,
            std::nullopt,
            0U);
    }
    if (const auto package_error = validate_package_shape(*package_)) {
        return trace_failure(*package_error,
            request.source_model_index,
            std::nullopt,
            0U);
    }
    ResolvedHull resolved;
    if (const auto error = resolve_hull(*package_,
            request.source_model_index,
            request.hull,
            resolved)) {
        return trace_failure(*error,
            request.source_model_index,
            std::nullopt,
            0U);
    }

    const CollisionPointContentsRequest start_request{
        request.start,
        request.source_model_index,
        request.hull,
        request.contents_policy,
        request.limits,
    };
    const CollisionPointContentsRequest end_request{
        request.end,
        request.source_model_index,
        request.hull,
        request.contents_policy,
        request.limits,
    };
    auto start_query = point_contents(start_request, scratch);
    if (!start_query || !start_query.result) {
        return {std::nullopt, std::move(start_query.error)};
    }
    auto end_query = point_contents(end_request, scratch);
    if (!end_query || !end_query.result) {
        return {std::nullopt, std::move(end_query.error)};
    }

    CollisionTraceResult output;
    output.start_contents = start_query.result->contents;
    output.end_contents = end_query.result->contents;
    output.start_solid = blocks(
        request.contents_policy, output.start_contents.category);
    output.in_open = is_open_space(output.start_contents.category) ||
        is_open_space(output.end_contents.category);
    output.in_liquid = is_liquid(output.start_contents.category) ||
        is_liquid(output.end_contents.category);
    output.trace_profile = request.trace_profile;
    output.trace_evidence_profile = CollisionTraceEvidenceProfile::
        public_bsp_structure_and_independent_fixtures;
    output.collision_profile = package_->compatibility_profile();
    output.traversal_statistics.point_contents_steps =
        start_query.result->traversal_depth + end_query.result->traversal_depth;

    if (same_point(request.start, request.end)) {
        output.all_solid = output.start_solid;
        output.fraction = output.start_solid ? 0.0 : 1.0;
        output.end_position = request.start;
        if (output.start_solid) {
            output.hit = hit_for_model(request.source_model_index);
            output.blocking_contents = output.start_contents;
        }
        return {std::move(output), std::nullopt};
    }

    if (const auto scratch_error = scratch.prepare(
            package_->nodes().size(),
            package_->clipnodes().size(),
            request.limits)) {
        return trace_failure(*scratch_error,
            request.source_model_index,
            std::nullopt,
            0U);
    }
    const ScopeExit reset_scratch{[&scratch]() noexcept { scratch.reset(); }};

    auto push_frame = [&](const detail::CollisionScratchFrame& frame) {
        if (scratch.frames_.size() >= scratch.stack_limit_) {
            return false;
        }
        scratch.frames_.push_back(frame);
        output.traversal_statistics.maximum_stack_entries = std::max(
            output.traversal_statistics.maximum_stack_entries,
            scratch.frames_.size());
        return true;
    };

    if (!push_frame(detail::CollisionScratchFrame{
            false,
            root_reference(resolved.hull->root),
            0.0,
            1.0,
            {},
        })) {
        scratch.reset();
        return trace_failure(CollisionQueryErrorCode::stack_limit_exceeded,
            request.source_model_index,
            std::nullopt,
            0U);
    }

    const DoubleVector3 motion{
        static_cast<double>(request.end.x) - request.start.x,
        static_cast<double>(request.end.y) - request.start.y,
        static_cast<double>(request.end.z) - request.start.z,
    };
    bool previous_blocking = output.start_solid;
    bool visited_nonblocking = !output.start_solid;
    std::optional<double> candidate_fraction;
    std::optional<detail::CollisionScratchBoundary> candidate_boundary;
    std::optional<CollisionContents> candidate_contents;
    double last_terminal_fraction = 0.0;
    bool saw_terminal = false;

    auto fail_trace = [&](const CollisionQueryErrorCode code,
                          const std::optional<std::uint32_t> element) {
        const auto steps = output.traversal_statistics.traversal_steps;
        scratch.reset();
        return trace_failure(
            code, request.source_model_index, element, steps);
    };

    while (!scratch.frames_.empty()) {
        const auto frame = scratch.frames_.back();
        scratch.frames_.pop_back();

        using Kind = detail::CollisionScratchReferenceKind;
        if (frame.leave) {
            if (frame.reference.kind == Kind::node &&
                static_cast<std::size_t>(frame.reference.index) <
                    scratch.active_nodes_.size()) {
                scratch.active_nodes_[frame.reference.index] = 0U;
            } else if (frame.reference.kind == Kind::clipnode &&
                static_cast<std::size_t>(frame.reference.index) <
                    scratch.active_clipnodes_.size()) {
                scratch.active_clipnodes_[frame.reference.index] = 0U;
            }
            continue;
        }
        if (output.traversal_statistics.traversal_steps >=
            request.limits.maximum_traversal_steps) {
            return fail_trace(
                CollisionQueryErrorCode::traversal_step_limit_exceeded,
                frame.reference.index);
        }
        ++output.traversal_statistics.traversal_steps;

        if (frame.reference.kind == Kind::terminal ||
            frame.reference.kind == Kind::leaf) {
            CollisionContents terminal;
            if (frame.reference.kind == Kind::leaf) {
                if (static_cast<std::size_t>(frame.reference.index) >=
                    package_->leaves().size()) {
                    return fail_trace(CollisionQueryErrorCode::invalid_child,
                        frame.reference.index);
                }
                terminal = package_->leaves()[frame.reference.index].contents;
            } else {
                terminal = frame.reference.terminal;
            }
            const auto decoded = checked_contents(terminal);
            if (!decoded) {
                return fail_trace(CollisionQueryErrorCode::invalid_contents,
                    frame.reference.kind == Kind::leaf
                        ? std::optional{frame.reference.index}
                        : std::nullopt);
            }
            if (!std::isfinite(frame.start_fraction) ||
                !std::isfinite(frame.end_fraction) ||
                frame.start_fraction < 0.0 || frame.end_fraction > 1.0 ||
                frame.start_fraction > frame.end_fraction ||
                (saw_terminal && frame.start_fraction +
                        request.tolerance.fraction_epsilon <
                    last_terminal_fraction)) {
                return fail_trace(
                    CollisionQueryErrorCode::non_finite_result,
                    std::nullopt);
            }
            saw_terminal = true;
            last_terminal_fraction = frame.start_fraction;
            ++output.traversal_statistics.terminal_interval_count;
            output.in_open = output.in_open || is_open_space(decoded->category);
            output.in_liquid = output.in_liquid || is_liquid(decoded->category);
            const bool blocking = blocks(
                request.contents_policy, decoded->category);
            if (!blocking) {
                visited_nonblocking = true;
                previous_blocking = false;
            } else {
                if (!previous_blocking && !candidate_fraction) {
                    if (!frame.entry_boundary.present) {
                        return fail_trace(
                            CollisionQueryErrorCode::invalid_package,
                            std::nullopt);
                    }
                    candidate_fraction = frame.start_fraction;
                    candidate_boundary = frame.entry_boundary;
                    candidate_contents = *decoded;
                }
                previous_blocking = true;
            }
            continue;
        }

        const CollisionPlane* plane = nullptr;
        std::array<detail::CollisionScratchReference, 2U> children{};
        if (frame.reference.kind == Kind::node) {
            if (static_cast<std::size_t>(frame.reference.index) >=
                package_->nodes().size()) {
                return fail_trace(CollisionQueryErrorCode::invalid_child,
                    frame.reference.index);
            }
            if (scratch.active_nodes_[frame.reference.index] != 0U) {
                return fail_trace(CollisionQueryErrorCode::cycle_detected,
                    frame.reference.index);
            }
            const auto& node = package_->nodes()[frame.reference.index];
            if (static_cast<std::size_t>(node.plane_index) >=
                    package_->planes().size() ||
                !valid_plane(package_->planes()[node.plane_index])) {
                return fail_trace(CollisionQueryErrorCode::invalid_plane,
                    node.plane_index);
            }
            const auto first = node_child_reference(node.children[0U]);
            const auto second = node_child_reference(node.children[1U]);
            if (!first || !second) {
                return fail_trace(CollisionQueryErrorCode::invalid_child,
                    frame.reference.index);
            }
            children = {*first, *second};
            plane = &package_->planes()[node.plane_index];
            scratch.active_nodes_[frame.reference.index] = 1U;
        } else if (frame.reference.kind == Kind::clipnode) {
            if (static_cast<std::size_t>(frame.reference.index) >=
                package_->clipnodes().size()) {
                return fail_trace(CollisionQueryErrorCode::invalid_child,
                    frame.reference.index);
            }
            if (scratch.active_clipnodes_[frame.reference.index] != 0U) {
                return fail_trace(CollisionQueryErrorCode::cycle_detected,
                    frame.reference.index);
            }
            const auto& node = package_->clipnodes()[frame.reference.index];
            if (static_cast<std::size_t>(node.plane_index) >=
                    package_->planes().size() ||
                !valid_plane(package_->planes()[node.plane_index])) {
                return fail_trace(CollisionQueryErrorCode::invalid_plane,
                    node.plane_index);
            }
            const auto first = clipnode_child_reference(node.children[0U]);
            const auto second = clipnode_child_reference(node.children[1U]);
            if (!first || !second) {
                return fail_trace(CollisionQueryErrorCode::invalid_child,
                    frame.reference.index);
            }
            children = {*first, *second};
            plane = &package_->planes()[node.plane_index];
            scratch.active_clipnodes_[frame.reference.index] = 1U;
        } else {
            return fail_trace(CollisionQueryErrorCode::invalid_child,
                frame.reference.index);
        }

        detail::CollisionScratchFrame leave = frame;
        leave.leave = true;
        if (!push_frame(leave)) {
            return fail_trace(CollisionQueryErrorCode::stack_limit_exceeded,
                frame.reference.index);
        }

        const auto interval_start = double_point_at_fraction(
            request.start, request.end, frame.start_fraction);
        const auto interval_end = double_point_at_fraction(
            request.start, request.end, frame.end_fraction);
        if (!finite_vector(interval_start) || !finite_vector(interval_end)) {
            return fail_trace(CollisionQueryErrorCode::non_finite_result,
                plane->source_plane_index);
        }
        auto start_distance = signed_distance(interval_start, *plane);
        auto end_distance = signed_distance(interval_end, *plane);
        if (!std::isfinite(start_distance) || !std::isfinite(end_distance)) {
            return fail_trace(CollisionQueryErrorCode::non_finite_result,
                plane->source_plane_index);
        }

        // Raw signs define the tree side. In particular, a same-side segment
        // just inside solid must not be moved across a plane by tolerance.
        if (start_distance >= 0.0 && end_distance >= 0.0) {
            if (!push_frame(detail::CollisionScratchFrame{
                    false,
                    children[0U],
                    frame.start_fraction,
                    frame.end_fraction,
                    frame.entry_boundary,
                })) {
                return fail_trace(
                    CollisionQueryErrorCode::stack_limit_exceeded,
                    frame.reference.index);
            }
            continue;
        }
        if (start_distance < 0.0 && end_distance < 0.0) {
            if (!push_frame(detail::CollisionScratchFrame{
                    false,
                    children[1U],
                    frame.start_fraction,
                    frame.end_fraction,
                    frame.entry_boundary,
                })) {
                return fail_trace(
                    CollisionQueryErrorCode::stack_limit_exceeded,
                    frame.reference.index);
            }
            continue;
        }
        const bool front_to_back = start_distance >= 0.0;
        snap_single_near_crossing_endpoint(
            start_distance, end_distance, request.tolerance);
        if (output.traversal_statistics.fraction_split_count >=
            request.limits.maximum_fraction_splits) {
            return fail_trace(
                CollisionQueryErrorCode::fraction_split_limit_exceeded,
                frame.reference.index);
        }
        ++output.traversal_statistics.fraction_split_count;
        const auto denominator = start_distance - end_distance;
        if (!std::isfinite(denominator) || denominator == 0.0) {
            return fail_trace(CollisionQueryErrorCode::non_finite_result,
                plane->source_plane_index);
        }
        auto local_fraction = start_distance / denominator;
        if (!std::isfinite(local_fraction) ||
            local_fraction < -request.tolerance.fraction_epsilon ||
            local_fraction > 1.0 + request.tolerance.fraction_epsilon) {
            return fail_trace(CollisionQueryErrorCode::non_finite_result,
                plane->source_plane_index);
        }
        local_fraction = std::clamp(local_fraction, 0.0, 1.0);
        auto middle_fraction = std::fma(local_fraction,
            frame.end_fraction - frame.start_fraction,
            frame.start_fraction);
        if (!std::isfinite(middle_fraction) ||
            middle_fraction < frame.start_fraction -
                    request.tolerance.fraction_epsilon ||
            middle_fraction > frame.end_fraction +
                    request.tolerance.fraction_epsilon) {
            return fail_trace(CollisionQueryErrorCode::non_finite_result,
                plane->source_plane_index);
        }
        middle_fraction = std::clamp(
            middle_fraction, frame.start_fraction, frame.end_fraction);
        const auto near_progress = middle_fraction - frame.start_fraction;
        const auto far_progress = frame.end_fraction - middle_fraction;
        if ((near_progress > request.tolerance.fraction_epsilon &&
                near_progress <
                    request.tolerance.minimum_progress_fraction) ||
            (far_progress > request.tolerance.fraction_epsilon &&
                far_progress <
                    request.tolerance.minimum_progress_fraction)) {
            return fail_trace(
                CollisionQueryErrorCode::insufficient_fraction_progress,
                plane->source_plane_index);
        }

        const std::size_t near_child = front_to_back ? 0U : 1U;
        const std::size_t far_child = front_to_back ? 1U : 0U;
        const detail::CollisionScratchBoundary crossing{
            true,
            static_cast<std::uint32_t>(plane - package_->planes().data()),
            !front_to_back,
        };
        // Far is pushed first so the near interval is always processed first.
        if (!push_frame(detail::CollisionScratchFrame{
                false,
                children[far_child],
                middle_fraction,
                frame.end_fraction,
                crossing,
            }) ||
            !push_frame(detail::CollisionScratchFrame{
                false,
                children[near_child],
                frame.start_fraction,
                middle_fraction,
                frame.entry_boundary,
            })) {
            return fail_trace(CollisionQueryErrorCode::stack_limit_exceeded,
                frame.reference.index);
        }
    }

    scratch.reset();
    if (!saw_terminal) {
        return trace_failure(CollisionQueryErrorCode::invalid_package,
            request.source_model_index,
            std::nullopt,
            output.traversal_statistics.traversal_steps);
    }

    output.all_solid = output.start_solid && !visited_nonblocking;
    if (output.all_solid) {
        output.fraction = 0.0;
        output.end_position = request.start;
        output.hit = hit_for_model(request.source_model_index);
        output.blocking_contents = output.start_contents;
        return {std::move(output), std::nullopt};
    }
    if (!candidate_fraction) {
        output.fraction = 1.0;
        output.end_position = request.end;
        return {std::move(output), std::nullopt};
    }
    if (!candidate_boundary || !candidate_contents ||
        !std::isfinite(*candidate_fraction) || *candidate_fraction < 0.0 ||
        *candidate_fraction > 1.0) {
        return trace_failure(CollisionQueryErrorCode::non_finite_result,
            request.source_model_index,
            std::nullopt,
            output.traversal_statistics.traversal_steps);
    }
    // A real nonblocking-to-blocking entry at the requested endpoint remains
    // a collision. Fraction one alone is not a no-hit predicate; absence of
    // collision metadata is the exact no-hit form.
    const auto hit_plane = make_plane_hit(
        *package_, *candidate_boundary, motion, request.tolerance);
    if (!hit_plane) {
        return trace_failure(CollisionQueryErrorCode::invalid_plane,
            request.source_model_index,
            candidate_boundary->plane_index,
            output.traversal_statistics.traversal_steps);
    }
    output.fraction = *candidate_fraction;
    output.end_position = point_at_fraction(
        request.start, request.end, output.fraction);
    if (!finite_vector(output.end_position)) {
        return trace_failure(CollisionQueryErrorCode::non_finite_result,
            request.source_model_index,
            std::nullopt,
            output.traversal_statistics.traversal_steps);
    }
    output.collision_plane = *hit_plane;
    output.hit = hit_for_model(request.source_model_index);
    output.blocking_contents = *candidate_contents;
    return {std::move(output), std::nullopt};
}

} // namespace hlclient::collision
