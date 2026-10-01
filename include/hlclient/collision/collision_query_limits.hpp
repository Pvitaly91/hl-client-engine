#pragma once

#include <cstddef>

namespace hlclient::collision {

inline constexpr std::size_t kCollisionDefaultMaximumTraversalSteps = 131'072U;
inline constexpr std::size_t kCollisionHardMaximumTraversalSteps = 1'048'576U;
inline constexpr std::size_t kCollisionDefaultMaximumStackEntries = 65'536U;
inline constexpr std::size_t kCollisionHardMaximumStackEntries = 131'072U;
inline constexpr std::size_t kCollisionDefaultMaximumFractionSplits = 65'536U;
inline constexpr std::size_t kCollisionHardMaximumFractionSplits = 131'072U;
inline constexpr std::size_t kCollisionDefaultMaximumQueryScratchBytes =
    16U * 1024U * 1024U;
inline constexpr std::size_t kCollisionHardMaximumQueryScratchBytes =
    64U * 1024U * 1024U;

struct CollisionQueryLimits {
    std::size_t maximum_traversal_steps{
        kCollisionDefaultMaximumTraversalSteps};
    std::size_t maximum_stack_entries{kCollisionDefaultMaximumStackEntries};
    std::size_t maximum_fraction_splits{
        kCollisionDefaultMaximumFractionSplits};
    std::size_t maximum_query_scratch_bytes{
        kCollisionDefaultMaximumQueryScratchBytes};
};

[[nodiscard]] bool valid_collision_query_limits(
    const CollisionQueryLimits& limits) noexcept;

struct CollisionTraceToleranceProfile {
    // Project-owned tolerance profile; this is not claimed to be the stock
    // engine DIST_EPSILON behavior.
    double plane_distance_epsilon{1.0e-6};
    double fraction_epsilon{1.0e-12};
    double minimum_progress_fraction{1.0e-12};
};

[[nodiscard]] bool valid_collision_trace_tolerance_profile(
    const CollisionTraceToleranceProfile& profile) noexcept;

} // namespace hlclient::collision
