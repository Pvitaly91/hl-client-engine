#pragma once

#include <hlclient/entity_visual/entity_pipeline_stage_types.hpp>
#include <hlclient/entity_visual/entity_visual_asset_library.hpp>
#include <hlclient/goldsrc/entity_snapshot.hpp>
#include <hlclient/goldsrc/precache_manifest.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::entity_visual {

enum class EntityVisualAssetStageState {
    idle,
    waiting_for_snapshot_history,
    collecting_visual_references,
    resolving_model_slots,
    importing_visual_assets,
    visual_asset_library_ready,
    projection_evidence_pending,
    missing_asset,
    unsupported_asset,
    import_failed,
    cancelled,
    timed_out,
    backpressure,
    failed,
};

class EntityVisualAssetStageResult final {
public:
    EntityVisualAssetStageResult(const EntityVisualAssetStageResult&) = default;
    EntityVisualAssetStageResult(EntityVisualAssetStageResult&&) noexcept =
        default;
    EntityVisualAssetStageResult& operator=(
        const EntityVisualAssetStageResult&) = delete;
    EntityVisualAssetStageResult& operator=(
        EntityVisualAssetStageResult&&) noexcept = delete;
    ~EntityVisualAssetStageResult() = default;

    [[nodiscard]] const std::shared_ptr<
        const goldsrc::EntitySnapshotHistoryState>& snapshot_history()
        const noexcept;
    [[nodiscard]] const std::shared_ptr<
        const local_resources::LocalResourceEnvironment>& environment()
        const noexcept;
    [[nodiscard]] const std::shared_ptr<const goldsrc::PrecacheManifestState>&
    manifest() const noexcept;
    [[nodiscard]] const std::shared_ptr<const EntityVisualAssetLibraryState>&
    library() const noexcept;
    [[nodiscard]] std::span<const EntityVisualBindingState> bindings()
        const noexcept;

private:
    friend class EntityVisualAssetStage;

    EntityVisualAssetStageResult(
        std::shared_ptr<const goldsrc::EntitySnapshotHistoryState>
            snapshot_history,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        std::shared_ptr<const goldsrc::PrecacheManifestState> manifest,
        std::shared_ptr<const EntityVisualAssetLibraryState> library,
        std::vector<EntityVisualBindingState> bindings) noexcept;

    std::shared_ptr<const goldsrc::EntitySnapshotHistoryState>
        snapshot_history_;
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment_;
    std::shared_ptr<const goldsrc::PrecacheManifestState> manifest_;
    std::shared_ptr<const EntityVisualAssetLibraryState> library_;
    std::vector<EntityVisualBindingState> bindings_;
};

// State contract only. External composition code performs projection,
// resolution and import work, then reports each completed boundary here.
class EntityVisualAssetStage final {
public:
    explicit EntityVisualAssetStage(
        EntityPipelineStageLimits limits = {}) noexcept;

    void begin(EntityPipelineStageTimePoint now) noexcept;
    void update(EntityPipelineStageTimePoint now) noexcept;
    void cancel() noexcept;
    void signal_backpressure() noexcept;
    [[nodiscard]] bool resume_from_backpressure() noexcept;

    [[nodiscard]] bool provide_snapshot_history(
        std::shared_ptr<const goldsrc::EntitySnapshotHistoryState> history,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        std::shared_ptr<const goldsrc::PrecacheManifestState> manifest)
        noexcept;
    [[nodiscard]] bool visual_references_collected() noexcept;
    [[nodiscard]] bool model_slots_resolved() noexcept;
    [[nodiscard]] bool publish_library(
        std::shared_ptr<const EntityVisualAssetLibraryState> library,
        std::vector<EntityVisualBindingState> bindings) noexcept;

    void finish_projection_evidence_pending(
        std::string_view context = {}) noexcept;
    void finish_missing_asset(std::string_view context = {}) noexcept;
    void finish_unsupported_asset(std::string_view context = {}) noexcept;
    void finish_import_failed(std::string_view context = {}) noexcept;
    void fail(std::string_view context = {}) noexcept;

    [[nodiscard]] EntityVisualAssetStageState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] std::size_t transition_count() const noexcept;
    [[nodiscard]] const std::shared_ptr<const EntityVisualAssetStageResult>&
    result() const noexcept;
    [[nodiscard]] const std::optional<EntityPipelineStageError>& error()
        const noexcept;

private:
    [[nodiscard]] bool transition(
        EntityVisualAssetStageState expected,
        EntityVisualAssetStageState next) noexcept;
    void set_terminal_failure(
        EntityVisualAssetStageState state,
        EntityPipelineStageErrorCode code,
        std::string_view context) noexcept;

    EntityPipelineStageLimits limits_;
    EntityVisualAssetStageState state_{EntityVisualAssetStageState::idle};
    EntityVisualAssetStageState resume_state_{
        EntityVisualAssetStageState::idle};
    bool backpressure_limit_reached_{false};
    std::size_t transition_count_{0U};
    std::optional<EntityPipelineStageTimePoint> started_at_;
    std::optional<EntityPipelineStageTimePoint> last_update_at_;
    std::shared_ptr<const goldsrc::EntitySnapshotHistoryState>
        snapshot_history_;
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment_;
    std::shared_ptr<const goldsrc::PrecacheManifestState> manifest_;
    std::shared_ptr<const EntityVisualAssetStageResult> result_;
    std::optional<EntityPipelineStageError> error_;
};

[[nodiscard]] constexpr std::string_view to_string(
    EntityVisualAssetStageState state) noexcept
{
    switch (state) {
    case EntityVisualAssetStageState::idle: return "idle";
    case EntityVisualAssetStageState::waiting_for_snapshot_history:
        return "waiting_for_snapshot_history";
    case EntityVisualAssetStageState::collecting_visual_references:
        return "collecting_visual_references";
    case EntityVisualAssetStageState::resolving_model_slots:
        return "resolving_model_slots";
    case EntityVisualAssetStageState::importing_visual_assets:
        return "importing_visual_assets";
    case EntityVisualAssetStageState::visual_asset_library_ready:
        return "visual_asset_library_ready";
    case EntityVisualAssetStageState::projection_evidence_pending:
        return "projection_evidence_pending";
    case EntityVisualAssetStageState::missing_asset: return "missing_asset";
    case EntityVisualAssetStageState::unsupported_asset:
        return "unsupported_asset";
    case EntityVisualAssetStageState::import_failed: return "import_failed";
    case EntityVisualAssetStageState::cancelled: return "cancelled";
    case EntityVisualAssetStageState::timed_out: return "timed_out";
    case EntityVisualAssetStageState::backpressure: return "backpressure";
    case EntityVisualAssetStageState::failed: return "failed";
    }
    return "unknown";
}

} // namespace hlclient::entity_visual
