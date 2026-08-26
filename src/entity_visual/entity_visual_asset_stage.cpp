#include <hlclient/entity_visual/entity_visual_asset_stage.hpp>

#include <new>
#include <utility>

namespace hlclient::entity_visual {
namespace {

[[nodiscard]] std::string bounded_context(const std::string_view context)
{
    return std::string{context.substr(
        0U, kEntityPipelineStageDiagnosticTextLimit)};
}

[[nodiscard]] bool terminal_state(
    const EntityVisualAssetStageState state) noexcept
{
    switch (state) {
    case EntityVisualAssetStageState::visual_asset_library_ready:
    case EntityVisualAssetStageState::projection_evidence_pending:
    case EntityVisualAssetStageState::missing_asset:
    case EntityVisualAssetStageState::unsupported_asset:
    case EntityVisualAssetStageState::import_failed:
    case EntityVisualAssetStageState::cancelled:
    case EntityVisualAssetStageState::timed_out:
    case EntityVisualAssetStageState::failed:
        return true;
    case EntityVisualAssetStageState::idle:
    case EntityVisualAssetStageState::waiting_for_snapshot_history:
    case EntityVisualAssetStageState::collecting_visual_references:
    case EntityVisualAssetStageState::resolving_model_slots:
    case EntityVisualAssetStageState::importing_visual_assets:
    case EntityVisualAssetStageState::backpressure:
        return false;
    }
    return true;
}

} // namespace

EntityVisualAssetStageResult::EntityVisualAssetStageResult(
    std::shared_ptr<const goldsrc::EntitySnapshotHistoryState> snapshot_history,
    std::shared_ptr<const local_resources::LocalResourceEnvironment> environment,
    std::shared_ptr<const goldsrc::PrecacheManifestState> manifest,
    std::shared_ptr<const EntityVisualAssetLibraryState> library,
    std::vector<EntityVisualBindingState> bindings) noexcept
    : snapshot_history_{std::move(snapshot_history)},
      environment_{std::move(environment)},
      manifest_{std::move(manifest)},
      library_{std::move(library)},
      bindings_{std::move(bindings)}
{
}

const std::shared_ptr<const goldsrc::EntitySnapshotHistoryState>&
EntityVisualAssetStageResult::snapshot_history() const noexcept
{
    return snapshot_history_;
}

const std::shared_ptr<const local_resources::LocalResourceEnvironment>&
EntityVisualAssetStageResult::environment() const noexcept
{
    return environment_;
}

const std::shared_ptr<const goldsrc::PrecacheManifestState>&
EntityVisualAssetStageResult::manifest() const noexcept
{
    return manifest_;
}

const std::shared_ptr<const EntityVisualAssetLibraryState>&
EntityVisualAssetStageResult::library() const noexcept
{
    return library_;
}

std::span<const EntityVisualBindingState>
EntityVisualAssetStageResult::bindings() const noexcept
{
    return bindings_;
}

EntityVisualAssetStage::EntityVisualAssetStage(
    const EntityPipelineStageLimits limits) noexcept
    : limits_{limits}
{
}

void EntityVisualAssetStage::begin(const EntityPipelineStageTimePoint now)
    noexcept
{
    if (state_ != EntityVisualAssetStageState::idle) {
        set_terminal_failure(
            EntityVisualAssetStageState::failed,
            EntityPipelineStageErrorCode::invalid_transition,
            "Entity visual asset stage was started more than once");
        return;
    }
    if (!valid_entity_pipeline_stage_limits(limits_)) {
        set_terminal_failure(
            EntityVisualAssetStageState::failed,
            EntityPipelineStageErrorCode::invalid_configuration,
            "Invalid entity visual asset stage limits");
        return;
    }
    started_at_ = now;
    last_update_at_ = now;
    static_cast<void>(transition(
        EntityVisualAssetStageState::idle,
        EntityVisualAssetStageState::waiting_for_snapshot_history));
}

void EntityVisualAssetStage::update(const EntityPipelineStageTimePoint now)
    noexcept
{
    if (terminal() || state_ == EntityVisualAssetStageState::idle) {
        return;
    }
    if (!last_update_at_ || !started_at_ || now < *last_update_at_) {
        set_terminal_failure(
            EntityVisualAssetStageState::failed,
            EntityPipelineStageErrorCode::time_moved_backwards,
            "Entity visual asset stage time moved backwards");
        return;
    }
    last_update_at_ = now;
    if (limits_.timeout && now - *started_at_ >= *limits_.timeout) {
        set_terminal_failure(
            EntityVisualAssetStageState::timed_out,
            EntityPipelineStageErrorCode::operation_failed,
            "Entity visual asset stage timed out");
    }
}

void EntityVisualAssetStage::cancel() noexcept
{
    if (!terminal()) {
        state_ = EntityVisualAssetStageState::cancelled;
        result_.reset();
    }
}

void EntityVisualAssetStage::signal_backpressure() noexcept
{
    if (!terminal() && state_ != EntityVisualAssetStageState::idle &&
        state_ != EntityVisualAssetStageState::backpressure) {
        resume_state_ = state_;
        state_ = EntityVisualAssetStageState::backpressure;
    }
}

bool EntityVisualAssetStage::resume_from_backpressure() noexcept
{
    if (state_ != EntityVisualAssetStageState::backpressure ||
        backpressure_limit_reached_) {
        return false;
    }
    state_ = resume_state_;
    return true;
}

bool EntityVisualAssetStage::provide_snapshot_history(
    std::shared_ptr<const goldsrc::EntitySnapshotHistoryState> history,
    std::shared_ptr<const local_resources::LocalResourceEnvironment> environment,
    std::shared_ptr<const goldsrc::PrecacheManifestState> manifest) noexcept
{
    if (!history || !environment || !manifest ||
        history->snapshot_count() == 0U) {
        set_terminal_failure(
            EntityVisualAssetStageState::failed,
            EntityPipelineStageErrorCode::invalid_input,
            "Entity visual asset stage requires immutable history, environment and manifest");
        return false;
    }
    if (state_ != EntityVisualAssetStageState::waiting_for_snapshot_history) {
        set_terminal_failure(
            EntityVisualAssetStageState::failed,
            EntityPipelineStageErrorCode::invalid_transition,
            "Snapshot history arrived in the wrong stage state");
        return false;
    }
    snapshot_history_ = std::move(history);
    environment_ = std::move(environment);
    manifest_ = std::move(manifest);
    return transition(
        EntityVisualAssetStageState::waiting_for_snapshot_history,
        EntityVisualAssetStageState::collecting_visual_references);
}

bool EntityVisualAssetStage::visual_references_collected() noexcept
{
    return transition(
        EntityVisualAssetStageState::collecting_visual_references,
        EntityVisualAssetStageState::resolving_model_slots);
}

bool EntityVisualAssetStage::model_slots_resolved() noexcept
{
    return transition(
        EntityVisualAssetStageState::resolving_model_slots,
        EntityVisualAssetStageState::importing_visual_assets);
}

bool EntityVisualAssetStage::publish_library(
    std::shared_ptr<const EntityVisualAssetLibraryState> library,
    std::vector<EntityVisualBindingState> bindings) noexcept
{
    if (state_ != EntityVisualAssetStageState::importing_visual_assets) {
        set_terminal_failure(
            EntityVisualAssetStageState::failed,
            EntityPipelineStageErrorCode::invalid_transition,
            "Visual asset library was published in the wrong stage state");
        return false;
    }
    if (!snapshot_history_ || !environment_ || !manifest_ || !library ||
        library->resource_id() == 0U || library->resource_revision() == 0U) {
        set_terminal_failure(
            EntityVisualAssetStageState::failed,
            EntityPipelineStageErrorCode::invalid_input,
            "Visual asset stage result ownership is incomplete");
        return false;
    }
    try {
        result_ = std::shared_ptr<const EntityVisualAssetStageResult>{
            new EntityVisualAssetStageResult{
                snapshot_history_,
                environment_,
                manifest_,
                std::move(library),
                std::move(bindings)}};
    } catch (const std::bad_alloc&) {
        set_terminal_failure(
            EntityVisualAssetStageState::failed,
            EntityPipelineStageErrorCode::unable_to_retain_result,
            "Unable to retain immutable visual asset stage result");
        return false;
    } catch (...) {
        set_terminal_failure(
            EntityVisualAssetStageState::failed,
            EntityPipelineStageErrorCode::unable_to_retain_result,
            "Unable to publish immutable visual asset stage result");
        return false;
    }
    if (!transition(
            EntityVisualAssetStageState::importing_visual_assets,
            EntityVisualAssetStageState::visual_asset_library_ready)) {
        result_.reset();
        return false;
    }
    return true;
}

void EntityVisualAssetStage::finish_projection_evidence_pending(
    const std::string_view context) noexcept
{
    set_terminal_failure(
        EntityVisualAssetStageState::projection_evidence_pending,
        EntityPipelineStageErrorCode::operation_failed,
        context.empty() ? "Entity visual projection evidence is pending"
                        : context);
}

void EntityVisualAssetStage::finish_missing_asset(
    const std::string_view context) noexcept
{
    set_terminal_failure(
        EntityVisualAssetStageState::missing_asset,
        EntityPipelineStageErrorCode::operation_failed,
        context.empty() ? "A referenced entity visual asset is missing"
                        : context);
}

void EntityVisualAssetStage::finish_unsupported_asset(
    const std::string_view context) noexcept
{
    set_terminal_failure(
        EntityVisualAssetStageState::unsupported_asset,
        EntityPipelineStageErrorCode::operation_failed,
        context.empty() ? "A referenced entity visual asset is unsupported"
                        : context);
}

void EntityVisualAssetStage::finish_import_failed(
    const std::string_view context) noexcept
{
    set_terminal_failure(
        EntityVisualAssetStageState::import_failed,
        EntityPipelineStageErrorCode::operation_failed,
        context.empty() ? "Entity visual asset import failed" : context);
}

void EntityVisualAssetStage::fail(const std::string_view context) noexcept
{
    set_terminal_failure(
        EntityVisualAssetStageState::failed,
        EntityPipelineStageErrorCode::operation_failed,
        context.empty() ? "Entity visual asset stage failed" : context);
}

EntityVisualAssetStageState EntityVisualAssetStage::state() const noexcept
{
    return state_;
}

bool EntityVisualAssetStage::terminal() const noexcept
{
    return terminal_state(state_);
}

std::size_t EntityVisualAssetStage::transition_count() const noexcept
{
    return transition_count_;
}

const std::shared_ptr<const EntityVisualAssetStageResult>&
EntityVisualAssetStage::result() const noexcept
{
    return result_;
}

const std::optional<EntityPipelineStageError>& EntityVisualAssetStage::error()
    const noexcept
{
    return error_;
}

bool EntityVisualAssetStage::transition(
    const EntityVisualAssetStageState expected,
    const EntityVisualAssetStageState next) noexcept
{
    if (state_ != expected) {
        set_terminal_failure(
            EntityVisualAssetStageState::failed,
            EntityPipelineStageErrorCode::invalid_transition,
            "Invalid entity visual asset stage transition");
        return false;
    }
    if (transition_count_ >= limits_.maximum_transitions) {
        resume_state_ = state_;
        state_ = EntityVisualAssetStageState::backpressure;
        backpressure_limit_reached_ = true;
        try {
            error_ = EntityPipelineStageError{
                EntityPipelineStageErrorCode::transition_limit_reached,
                bounded_context(
                    "Entity visual asset stage transition limit reached")};
        } catch (...) {
            error_ = EntityPipelineStageError{
                EntityPipelineStageErrorCode::transition_limit_reached, {}};
        }
        return false;
    }
    ++transition_count_;
    state_ = next;
    return true;
}

void EntityVisualAssetStage::set_terminal_failure(
    const EntityVisualAssetStageState state,
    const EntityPipelineStageErrorCode code,
    const std::string_view context) noexcept
{
    if (terminal()) {
        return;
    }
    result_.reset();
    state_ = state;
    try {
        error_ = EntityPipelineStageError{code, bounded_context(context)};
    } catch (...) {
        error_ = EntityPipelineStageError{code, {}};
    }
}

} // namespace hlclient::entity_visual
