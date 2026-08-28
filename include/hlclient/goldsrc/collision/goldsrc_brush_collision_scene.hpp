#pragma once

#include <hlclient/collision/collision_world_query.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_brush_rigid_transform.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc::collision {

struct BrushCollisionModelLibraryBuildResult;

enum class BrushCollisionRole : std::uint8_t {
    solid,
    non_solid,
    unsupported,
    evidence_pending,
};

[[nodiscard]] std::string_view to_string(BrushCollisionRole role) noexcept;

enum class BrushCollisionRoleProviderProfile : std::uint8_t {
    explicit_synthetic_brush_solidity_v1,
    stock_brush_solidity_evidence_pending,
};

[[nodiscard]] std::string_view to_string(
    BrushCollisionRoleProviderProfile profile) noexcept;

struct BrushCollisionInstanceIdentity {
    std::uint64_t stable_instance_ordinal{0U};
    std::uint32_t source_model_index{0U};
    std::optional<std::uint32_t> source_entity_index;

    [[nodiscard]] friend bool operator==(
        const BrushCollisionInstanceIdentity&,
        const BrushCollisionInstanceIdentity&) = default;
};

class IBrushCollisionRoleProvider {
public:
    virtual ~IBrushCollisionRoleProvider() = default;

    [[nodiscard]] virtual BrushCollisionRoleProviderProfile profile()
        const noexcept = 0;
    [[nodiscard]] virtual BrushCollisionRole role_for(
        const BrushCollisionInstanceIdentity& identity) const noexcept = 0;
};

struct SyntheticBrushCollisionRoleBinding {
    BrushCollisionInstanceIdentity identity{};
    BrushCollisionRole role{BrushCollisionRole::evidence_pending};
};

// Test/tool-only provider. Roles exist only when explicitly bound to an exact
// instance identity. Missing or duplicate identities fail closed.
class ExplicitSyntheticBrushCollisionRoleProvider final
    : public IBrushCollisionRoleProvider {
public:
    explicit ExplicitSyntheticBrushCollisionRoleProvider(
        std::vector<SyntheticBrushCollisionRoleBinding> bindings) noexcept;

    [[nodiscard]] BrushCollisionRoleProviderProfile profile()
        const noexcept override;
    [[nodiscard]] BrushCollisionRole role_for(
        const BrushCollisionInstanceIdentity& identity) const noexcept override;
    [[nodiscard]] std::span<const SyntheticBrushCollisionRoleBinding>
    bindings() const noexcept;

private:
    std::vector<SyntheticBrushCollisionRoleBinding> bindings_;
};

// Production-safe stock boundary. Entity classname, render mode and model name
// are deliberately not interpreted as solidity evidence.
class StockBrushCollisionRoleProvider final
    : public IBrushCollisionRoleProvider {
public:
    [[nodiscard]] BrushCollisionRoleProviderProfile profile()
        const noexcept override;
    [[nodiscard]] BrushCollisionRole role_for(
        const BrushCollisionInstanceIdentity& identity) const noexcept override;
};

struct BrushCollisionModel {
    std::uint32_t source_model_index{0U};
    assets::WorldBounds local_bounds{};
    assets::AssetVector3 source_origin{};
    std::array<hlclient::collision::CollisionHullRoot,
        hlclient::collision::kCollisionHullCount> hull_roots{};
    hlclient::collision::CollisionWorldIdentity collision_identity{};
    std::shared_ptr<const hlclient::collision::CollisionWorldPackage>
        collision_world;

    // Returns a builder-derived, tree-proven model-local superset of blocking
    // space. Absence means broad-phase rejection is forbidden, not that the
    // hull is empty.
    [[nodiscard]] const assets::WorldBounds* conservative_blocking_bounds(
        hlclient::collision::CollisionHullOrdinal hull) const noexcept;

private:
    std::array<std::optional<assets::WorldBounds>,
        hlclient::collision::kCollisionHullCount>
        conservative_blocking_bounds_{};
    std::shared_ptr<const hlclient::collision::CollisionWorldPackage>
        broad_phase_proof_source_;
    std::optional<std::uint32_t> broad_phase_proof_model_index_;

    friend BrushCollisionModelLibraryBuildResult
    build_brush_collision_model_library(
        std::shared_ptr<const hlclient::collision::CollisionWorldPackage>
            collision_world) noexcept;
};

enum class BrushCollisionModelLibraryErrorCode : std::uint8_t {
    invalid_package,
    world_model_missing,
    invalid_model,
    duplicate_model,
    unable_to_publish,
};

[[nodiscard]] std::string_view to_string(
    BrushCollisionModelLibraryErrorCode code) noexcept;

struct BrushCollisionModelLibraryError {
    BrushCollisionModelLibraryErrorCode code{
        BrushCollisionModelLibraryErrorCode::invalid_package};
    std::optional<std::uint32_t> source_model_index;
};

class BrushCollisionModelLibrary final {
public:
    BrushCollisionModelLibrary() = default;
    BrushCollisionModelLibrary(
        std::shared_ptr<const hlclient::collision::CollisionWorldPackage>
            collision_world,
        std::vector<BrushCollisionModel> models) noexcept;

    [[nodiscard]] const std::shared_ptr<
        const hlclient::collision::CollisionWorldPackage>&
    collision_world() const noexcept;
    [[nodiscard]] std::span<const BrushCollisionModel> models() const noexcept;
    [[nodiscard]] const BrushCollisionModel* model(
        std::uint32_t source_model_index) const noexcept;

private:
    std::shared_ptr<const hlclient::collision::CollisionWorldPackage>
        collision_world_;
    std::vector<BrushCollisionModel> models_;
};

struct BrushCollisionModelLibraryBuildResult {
    std::shared_ptr<const BrushCollisionModelLibrary> library;
    std::optional<BrushCollisionModelLibraryError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return library != nullptr && !error.has_value();
    }
};

[[nodiscard]] BrushCollisionModelLibraryBuildResult
build_brush_collision_model_library(
    std::shared_ptr<const hlclient::collision::CollisionWorldPackage>
        collision_world) noexcept;

struct ExplicitBrushCollisionTraceRequest {
    assets::AssetVector3 start{};
    assets::AssetVector3 end{};
    hlclient::collision::CollisionHullOrdinal hull{
        hlclient::collision::CollisionHullOrdinal::point};
    hlclient::collision::CollisionContentsPolicy contents_policy{
        hlclient::collision::CollisionContentsPolicy::project_solid_only_v1};
    hlclient::collision::CollisionTraceCompatibilityProfile trace_profile{
        hlclient::collision::CollisionTraceCompatibilityProfile::
            project_deterministic_bsp_hull_trace_v1};
    hlclient::collision::CollisionTraceToleranceProfile tolerance{};
    hlclient::collision::CollisionQueryLimits query_limits{};
};

enum class ExplicitBrushCollisionTraceErrorCode : std::uint8_t {
    invalid_configuration,
    invalid_model,
    invalid_transform,
    invalid_segment,
    invalid_hull,
    invalid_bounds,
    collision_query_failed,
    non_finite_result,
};

[[nodiscard]] std::string_view to_string(
    ExplicitBrushCollisionTraceErrorCode code) noexcept;

struct ExplicitBrushCollisionTraceError {
    ExplicitBrushCollisionTraceErrorCode code{
        ExplicitBrushCollisionTraceErrorCode::invalid_model};
    std::optional<hlclient::collision::CollisionQueryError> query_error;
};

struct ExplicitBrushCollisionTraceResult {
    hlclient::collision::CollisionTraceResult trace{};
    assets::WorldBounds expanded_world_bounds{};
    bool broad_phase_rejected{false};
};

struct ExplicitBrushCollisionTraceQueryResult {
    std::optional<ExplicitBrushCollisionTraceResult> result;
    std::optional<ExplicitBrushCollisionTraceError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return result.has_value() && !error.has_value();
    }
};

[[nodiscard]] ExplicitBrushCollisionTraceQueryResult
trace_explicit_brush_model(
    const BrushCollisionModel& model,
    const brush_models::BrushRigidTransform& transform,
    const ExplicitBrushCollisionTraceRequest& request,
    hlclient::collision::CollisionQueryScratch& scratch);

inline constexpr std::size_t kDefaultMaximumBrushCollisionSceneInstances =
    4'096U;
inline constexpr std::size_t kHardMaximumBrushCollisionSceneInstances =
    65'536U;
inline constexpr std::size_t kDefaultMaximumBrushCollisionCandidates = 1'024U;
inline constexpr std::size_t kHardMaximumBrushCollisionCandidates = 65'536U;
inline constexpr std::size_t kDefaultMaximumBrushCollisionModelTraces = 1'024U;
inline constexpr std::size_t kHardMaximumBrushCollisionModelTraces = 65'536U;

struct BrushCollisionSceneBuildLimits {
    std::size_t maximum_instances{
        kDefaultMaximumBrushCollisionSceneInstances};
};

[[nodiscard]] bool valid_brush_collision_scene_build_limits(
    const BrushCollisionSceneBuildLimits& limits) noexcept;

struct BrushCollisionSceneQueryLimits {
    std::size_t maximum_brush_candidates{
        kDefaultMaximumBrushCollisionCandidates};
    std::size_t maximum_model_traces{
        kDefaultMaximumBrushCollisionModelTraces};
};

[[nodiscard]] bool valid_brush_collision_scene_query_limits(
    const BrushCollisionSceneQueryLimits& limits) noexcept;

struct BrushCollisionInstanceDefinition {
    BrushCollisionInstanceIdentity identity{};
    brush_models::BrushRigidTransform transform{};
};

struct BrushCollisionSceneInstance {
    BrushCollisionInstanceIdentity identity{};
    brush_models::BrushRigidTransform transform{};
    assets::WorldBounds transformed_bounds{};
    BrushCollisionRole role{BrushCollisionRole::evidence_pending};
    BrushCollisionRoleProviderProfile role_provider_profile{
        BrushCollisionRoleProviderProfile::
            stock_brush_solidity_evidence_pending};
};

enum class BrushCollisionSceneBuildErrorCode : std::uint8_t {
    invalid_configuration,
    invalid_library,
    instance_limit_exceeded,
    invalid_instance_identity,
    duplicate_instance_identity,
    model_not_found,
    invalid_transform,
    invalid_transformed_bounds,
    unsupported_role_provider,
    invalid_role,
    unable_to_publish,
};

[[nodiscard]] std::string_view to_string(
    BrushCollisionSceneBuildErrorCode code) noexcept;

struct BrushCollisionSceneBuildError {
    BrushCollisionSceneBuildErrorCode code{
        BrushCollisionSceneBuildErrorCode::invalid_configuration};
    std::optional<BrushCollisionInstanceIdentity> instance;
};

class BrushCollisionScene final {
public:
    BrushCollisionScene() = default;
    BrushCollisionScene(
        std::shared_ptr<const BrushCollisionModelLibrary> model_library,
        std::vector<BrushCollisionSceneInstance> instances,
        BrushCollisionRoleProviderProfile role_provider_profile) noexcept;

    [[nodiscard]] const std::shared_ptr<const BrushCollisionModelLibrary>&
    model_library() const noexcept;
    [[nodiscard]] std::span<const BrushCollisionSceneInstance> instances()
        const noexcept;
    [[nodiscard]] BrushCollisionRoleProviderProfile role_provider_profile()
        const noexcept;

private:
    std::shared_ptr<const BrushCollisionModelLibrary> model_library_;
    std::vector<BrushCollisionSceneInstance> instances_;
    BrushCollisionRoleProviderProfile role_provider_profile_{
        BrushCollisionRoleProviderProfile::
            stock_brush_solidity_evidence_pending};
};

struct BrushCollisionSceneBuildResult {
    std::shared_ptr<const BrushCollisionScene> scene;
    std::optional<BrushCollisionSceneBuildError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return scene != nullptr && !error.has_value();
    }
};

[[nodiscard]] BrushCollisionSceneBuildResult build_brush_collision_scene(
    std::shared_ptr<const BrushCollisionModelLibrary> model_library,
    std::span<const BrushCollisionInstanceDefinition> instances,
    const IBrushCollisionRoleProvider& role_provider,
    const BrushCollisionSceneBuildLimits& limits = {}) noexcept;

struct BrushCollisionSceneTraceRequest {
    assets::AssetVector3 start{};
    assets::AssetVector3 end{};
    bool include_world{true};
    // When present, only the exact listed brush identities are eligible. The
    // caller-owned span needs to remain valid for the duration of trace_hull.
    std::optional<std::span<const BrushCollisionInstanceIdentity>>
        explicit_brush_instances;
    // Ignore wins over an explicit-list match.
    std::optional<BrushCollisionInstanceIdentity> ignored_instance;
    hlclient::collision::CollisionHullOrdinal hull{
        hlclient::collision::CollisionHullOrdinal::point};
    hlclient::collision::CollisionContentsPolicy contents_policy{
        hlclient::collision::CollisionContentsPolicy::project_solid_only_v1};
    hlclient::collision::CollisionTraceCompatibilityProfile trace_profile{
        hlclient::collision::CollisionTraceCompatibilityProfile::
            project_deterministic_bsp_hull_trace_v1};
    hlclient::collision::CollisionTraceToleranceProfile tolerance{};
    hlclient::collision::CollisionQueryLimits query_limits{};
    BrushCollisionSceneQueryLimits scene_limits{};
};

struct BrushCollisionSceneHit {
    hlclient::collision::CollisionTraceHitKind kind{
        hlclient::collision::CollisionTraceHitKind::world};
    std::optional<BrushCollisionInstanceIdentity> brush_instance;
};

struct BrushCollisionSceneTraceStatistics {
    std::size_t instance_count{0U};
    std::size_t solid_instance_count{0U};
    std::size_t non_solid_instance_count{0U};
    std::size_t unsupported_instance_count{0U};
    std::size_t evidence_pending_instance_count{0U};
    std::size_t broad_phase_test_count{0U};
    std::size_t broad_phase_rejection_count{0U};
    std::size_t brush_candidate_count{0U};
    std::size_t brush_model_trace_count{0U};
};

enum class BrushCollisionSceneAllSolidClassification : std::uint8_t {
    // No world trace was requested and no brush BSP trace was required: there
    // were no eligible solid instances or every one was safely broad-phase
    // rejected. The selected segment is therefore exactly nonblocking.
    exact_without_model_trace,
    // No brush model was traced, so the world trace is the complete scene.
    exact_from_world_only,
    // At least one traced object alone blocks the complete segment.
    proven_true_by_single_object,
    // No single object proved all-solid. Exact union-of-interval evidence is
    // deliberately deferred; CollisionTraceResult::all_solid must not be
    // interpreted as an exact scene-wide false in this state.
    multi_object_interval_union_evidence_pending,
};

[[nodiscard]] std::string_view to_string(
    BrushCollisionSceneAllSolidClassification classification) noexcept;

struct BrushCollisionSceneTraceResult {
    hlclient::collision::CollisionTraceResult trace{};
    std::optional<BrushCollisionSceneHit> scene_hit;
    BrushCollisionSceneTraceStatistics statistics{};
    BrushCollisionSceneAllSolidClassification all_solid_classification{
        BrushCollisionSceneAllSolidClassification::exact_from_world_only};
};

enum class BrushCollisionSceneQueryErrorCode : std::uint8_t {
    invalid_configuration,
    invalid_scene,
    world_query_failed,
    invalid_instance,
    candidate_limit_exceeded,
    model_trace_limit_exceeded,
    brush_trace_failed,
    non_finite_result,
};

[[nodiscard]] std::string_view to_string(
    BrushCollisionSceneQueryErrorCode code) noexcept;

struct BrushCollisionSceneQueryError {
    BrushCollisionSceneQueryErrorCode code{
        BrushCollisionSceneQueryErrorCode::invalid_configuration};
    std::optional<BrushCollisionInstanceIdentity> instance;
    std::optional<hlclient::collision::CollisionQueryError> query_error;
    std::optional<ExplicitBrushCollisionTraceErrorCode> brush_trace_error;
};

struct BrushCollisionSceneTraceQueryResult {
    std::optional<BrushCollisionSceneTraceResult> result;
    std::optional<BrushCollisionSceneQueryError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return result.has_value() && !error.has_value();
    }
};

class BrushCollisionSceneQuery final {
public:
    explicit BrushCollisionSceneQuery(
        std::shared_ptr<const BrushCollisionScene> scene) noexcept;

    [[nodiscard]] const std::shared_ptr<const BrushCollisionScene>& scene()
        const noexcept;
    [[nodiscard]] BrushCollisionSceneTraceQueryResult trace_hull(
        const BrushCollisionSceneTraceRequest& request,
        hlclient::collision::CollisionQueryScratch& scratch) const;

private:
    std::shared_ptr<const BrushCollisionScene> scene_;
};

} // namespace hlclient::goldsrc::collision
