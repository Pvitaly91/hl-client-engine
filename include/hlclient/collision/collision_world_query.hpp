#pragma once

#include <hlclient/collision/collision_world_package.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

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

enum class CollisionQueryErrorCode : std::uint8_t {
    invalid_configuration,
    invalid_point,
    invalid_segment,
    invalid_package,
    invalid_model,
    invalid_hull,
    invalid_plane,
    invalid_child,
    invalid_contents,
    unsupported_contents_policy,
    unsupported_trace_profile,
    unsupported_arbitrary_hull,
    cycle_detected,
    traversal_step_limit_exceeded,
    stack_limit_exceeded,
    fraction_split_limit_exceeded,
    scratch_limit_exceeded,
    unable_to_prepare_scratch,
    insufficient_fraction_progress,
    non_finite_result,
};

[[nodiscard]] std::string_view to_string(CollisionQueryErrorCode code) noexcept;

struct CollisionQueryError {
    CollisionQueryErrorCode code{CollisionQueryErrorCode::invalid_configuration};
    std::optional<std::uint32_t> source_model_index;
    std::optional<std::uint32_t> source_element_index;
    std::size_t traversal_steps{0U};
};

struct CollisionPointContentsRequest {
    assets::AssetVector3 point{};
    std::uint32_t source_model_index{0U};
    CollisionHullOrdinal hull{CollisionHullOrdinal::point};
    CollisionContentsPolicy contents_policy{
        CollisionContentsPolicy::project_solid_only_v1};
    CollisionQueryLimits limits{};
};

struct CollisionPointContentsResult {
    CollisionContents contents{};
    std::uint32_t source_model_index{0U};
    CollisionHullOrdinal hull{CollisionHullOrdinal::point};
    std::size_t traversal_depth{0U};
    CollisionTraceCompatibilityProfile trace_profile{
        CollisionTraceCompatibilityProfile::
            project_deterministic_bsp_hull_trace_v1};
};

struct CollisionPointContentsQueryResult {
    std::optional<CollisionPointContentsResult> result;
    std::optional<CollisionQueryError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return result.has_value();
    }
};

enum class CollisionPositionStatus : std::uint8_t {
    free,
    blocking,
};

struct CollisionPositionTestResult {
    CollisionPositionStatus status{CollisionPositionStatus::free};
    CollisionContents contents{};
    std::size_t traversal_depth{0U};
};

struct CollisionPositionTestQueryResult {
    std::optional<CollisionPositionTestResult> result;
    std::optional<CollisionQueryError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return result.has_value();
    }
};

struct CollisionTraceRequest {
    assets::AssetVector3 start{};
    assets::AssetVector3 end{};
    std::uint32_t source_model_index{0U};
    CollisionHullOrdinal hull{CollisionHullOrdinal::point};
    CollisionContentsPolicy contents_policy{
        CollisionContentsPolicy::project_solid_only_v1};
    CollisionTraceCompatibilityProfile trace_profile{
        CollisionTraceCompatibilityProfile::
            project_deterministic_bsp_hull_trace_v1};
    CollisionTraceToleranceProfile tolerance{};
    CollisionQueryLimits limits{};
};

enum class CollisionPlaneOrientation : std::uint8_t {
    source,
    inverted_source,
};

struct CollisionPlaneHit {
    assets::AssetVector3 normal{};
    double distance{0.0};
    std::uint32_t source_plane_index{0U};
    CollisionPlaneOrientation orientation{CollisionPlaneOrientation::source};
};

enum class CollisionTraceHitKind : std::uint8_t {
    world,
    collision_model,
};

struct CollisionTraceHit {
    CollisionTraceHitKind kind{CollisionTraceHitKind::world};
    std::uint32_t source_model_index{0U};
};

struct CollisionTraceTraversalStatistics {
    std::size_t point_contents_steps{0U};
    std::size_t traversal_steps{0U};
    std::size_t maximum_stack_entries{0U};
    std::size_t fraction_split_count{0U};
    std::size_t terminal_interval_count{0U};
};

struct CollisionTraceResult {
    bool all_solid{false};
    bool start_solid{false};
    // Project trace metadata records any visited open/liquid interval, rather
    // than claiming the stock pmtrace endpoint-only behavior.
    bool in_open{false};
    bool in_liquid{false};
    double fraction{1.0};
    assets::AssetVector3 end_position{};
    std::optional<CollisionPlaneHit> collision_plane;
    std::optional<CollisionTraceHit> hit;
    CollisionContents start_contents{};
    CollisionContents end_contents{};
    std::optional<CollisionContents> blocking_contents;
    CollisionTraceTraversalStatistics traversal_statistics{};
    CollisionTraceCompatibilityProfile trace_profile{
        CollisionTraceCompatibilityProfile::
            project_deterministic_bsp_hull_trace_v1};
    CollisionTraceEvidenceProfile trace_evidence_profile{
        CollisionTraceEvidenceProfile::
            public_bsp_structure_and_independent_fixtures};
    CollisionWorldCompatibilityProfile collision_profile{
        CollisionWorldCompatibilityProfile::valve_bsp_v30_clip_hulls_v1};
};

struct CollisionTraceQueryResult {
    std::optional<CollisionTraceResult> result;
    std::optional<CollisionQueryError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return result.has_value();
    }
};

namespace detail {

enum class CollisionScratchReferenceKind : std::uint8_t {
    node,
    leaf,
    clipnode,
    terminal,
};

struct CollisionScratchReference {
    CollisionScratchReferenceKind kind{CollisionScratchReferenceKind::terminal};
    std::uint32_t index{0U};
    CollisionContents terminal{};
};

struct CollisionScratchBoundary {
    bool present{false};
    std::uint32_t plane_index{0U};
    bool inverted{false};
};

struct CollisionScratchFrame {
    bool leave{false};
    CollisionScratchReference reference{};
    double start_fraction{0.0};
    double end_fraction{1.0};
    CollisionScratchBoundary entry_boundary{};
};

} // namespace detail

// Scratch grows only when first used with a larger validated package/limit;
// subsequent queries reuse the storage. It is caller-owned, so separate
// callers make CollisionWorldQuery reentrant without mutable global state.
class CollisionQueryScratch final {
public:
    CollisionQueryScratch() = default;
    CollisionQueryScratch(const CollisionQueryScratch&) = delete;
    CollisionQueryScratch& operator=(const CollisionQueryScratch&) = delete;
    CollisionQueryScratch(CollisionQueryScratch&&) noexcept = default;
    CollisionQueryScratch& operator=(CollisionQueryScratch&&) noexcept = default;

    [[nodiscard]] std::size_t retained_bytes() const noexcept;

private:
    friend class CollisionWorldQuery;

    [[nodiscard]] std::optional<CollisionQueryErrorCode> prepare(
        std::size_t node_count,
        std::size_t clipnode_count,
        const CollisionQueryLimits& limits) noexcept;
    void reset() noexcept;

    std::vector<detail::CollisionScratchFrame> frames_;
    std::vector<std::uint8_t> active_nodes_;
    std::vector<std::uint8_t> active_clipnodes_;
    std::size_t stack_limit_{0U};
};

class CollisionWorldQuery final {
public:
    explicit CollisionWorldQuery(
        std::shared_ptr<const CollisionWorldPackage> package) noexcept;

    [[nodiscard]] const std::shared_ptr<const CollisionWorldPackage>& package()
        const noexcept;

    [[nodiscard]] CollisionPointContentsQueryResult point_contents(
        const CollisionPointContentsRequest& request,
        CollisionQueryScratch& scratch) const;

    [[nodiscard]] CollisionPositionTestQueryResult test_position(
        const CollisionPointContentsRequest& request,
        CollisionQueryScratch& scratch) const;

    [[nodiscard]] CollisionTraceQueryResult trace_line(
        const CollisionTraceRequest& request,
        CollisionQueryScratch& scratch) const;

    [[nodiscard]] CollisionTraceQueryResult trace_hull(
        const CollisionTraceRequest& request,
        CollisionQueryScratch& scratch) const;

    [[nodiscard]] CollisionTraceQueryResult trace_model_hull(
        const CollisionTraceRequest& request,
        CollisionQueryScratch& scratch) const;

private:
    std::shared_ptr<const CollisionWorldPackage> package_;
};

} // namespace hlclient::collision
