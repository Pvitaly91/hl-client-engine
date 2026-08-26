#include <hlclient/entity_render/entity_scene_package_stage.hpp>

#include <hlclient/world_scene_render/world_scene_render_types.hpp>

#include <new>
#include <string>
#include <utility>

namespace hlclient::entity_render {
namespace {

[[nodiscard]] std::string bounded_context(const std::string_view context)
{
    return std::string{context.substr(
        0U, entity_visual::kEntityPipelineStageDiagnosticTextLimit)};
}

[[nodiscard]] bool terminal_state(
    const EntityScenePackageStageState state) noexcept
{
    switch (state) {
    case EntityScenePackageStageState::entity_scene_package_ready:
    case EntityScenePackageStageState::render_asset_failed:
    case EntityScenePackageStageState::cancelled:
    case EntityScenePackageStageState::timed_out:
        return true;
    case EntityScenePackageStageState::waiting_for_visual_assets:
    case EntityScenePackageStageState::building_studio_render_assets:
    case EntityScenePackageStageState::building_sprite_render_assets:
    case EntityScenePackageStageState::building_entity_scene_package:
    case EntityScenePackageStageState::backpressure:
        return false;
    }
    return true;
}

} // namespace

EntityScenePackageStageResult::EntityScenePackageStageResult(
    std::shared_ptr<const entity_visual::EntityVisualAssetStageResult>
        visual_assets,
    std::shared_ptr<const EntitySceneRenderPackage> scene_package,
    std::shared_ptr<const world_scene_render::WorldSceneRenderPackage>
        world_scene) noexcept
    : visual_assets_{std::move(visual_assets)},
      scene_package_{std::move(scene_package)},
      world_scene_{std::move(world_scene)}
{
}

const std::shared_ptr<const entity_visual::EntityVisualAssetStageResult>&
EntityScenePackageStageResult::visual_assets() const noexcept
{
    return visual_assets_;
}

const std::shared_ptr<const EntitySceneRenderPackage>&
EntityScenePackageStageResult::scene_package() const noexcept
{
    return scene_package_;
}

const std::shared_ptr<const world_scene_render::WorldSceneRenderPackage>&
EntityScenePackageStageResult::world_scene() const noexcept
{
    return world_scene_;
}

EntityScenePackageStage::EntityScenePackageStage(
    const entity_visual::EntityPipelineStageLimits limits) noexcept
    : limits_{limits}
{
}

void EntityScenePackageStage::begin(
    const entity_visual::EntityPipelineStageTimePoint now) noexcept
{
    if (begun_ || terminal()) {
        set_terminal_failure(
            EntityScenePackageStageState::render_asset_failed,
            entity_visual::EntityPipelineStageErrorCode::invalid_transition,
            "Entity scene package stage was started more than once");
        return;
    }
    if (!entity_visual::valid_entity_pipeline_stage_limits(limits_)) {
        set_terminal_failure(
            EntityScenePackageStageState::render_asset_failed,
            entity_visual::EntityPipelineStageErrorCode::invalid_configuration,
            "Invalid entity scene package stage limits");
        return;
    }
    begun_ = true;
    started_at_ = now;
    last_update_at_ = now;
}

void EntityScenePackageStage::update(
    const entity_visual::EntityPipelineStageTimePoint now) noexcept
{
    if (terminal() || !begun_) {
        return;
    }
    if (!last_update_at_ || !started_at_ || now < *last_update_at_) {
        set_terminal_failure(
            EntityScenePackageStageState::render_asset_failed,
            entity_visual::EntityPipelineStageErrorCode::time_moved_backwards,
            "Entity scene package stage time moved backwards");
        return;
    }
    last_update_at_ = now;
    if (limits_.timeout && now - *started_at_ >= *limits_.timeout) {
        set_terminal_failure(
            EntityScenePackageStageState::timed_out,
            entity_visual::EntityPipelineStageErrorCode::operation_failed,
            "Entity scene package stage timed out");
    }
}

void EntityScenePackageStage::cancel() noexcept
{
    if (!terminal()) {
        state_ = EntityScenePackageStageState::cancelled;
        result_.reset();
    }
}

void EntityScenePackageStage::signal_backpressure() noexcept
{
    if (begun_ && !terminal() &&
        state_ != EntityScenePackageStageState::backpressure) {
        resume_state_ = state_;
        state_ = EntityScenePackageStageState::backpressure;
    }
}

bool EntityScenePackageStage::resume_from_backpressure() noexcept
{
    if (state_ != EntityScenePackageStageState::backpressure ||
        backpressure_limit_reached_) {
        return false;
    }
    state_ = resume_state_;
    return true;
}

bool EntityScenePackageStage::provide_visual_assets(
    std::shared_ptr<const entity_visual::EntityVisualAssetStageResult>
        visual_assets,
    std::shared_ptr<const world_scene_render::WorldSceneRenderPackage>
        world_scene) noexcept
{
    if (!begun_) {
        set_terminal_failure(
            EntityScenePackageStageState::render_asset_failed,
            entity_visual::EntityPipelineStageErrorCode::invalid_transition,
            "Entity scene package stage has not started");
        return false;
    }
    if (!visual_assets || !visual_assets->snapshot_history() ||
        !visual_assets->environment() || !visual_assets->manifest() ||
        !visual_assets->library() ||
        visual_assets->library()->resource_id() == 0U ||
        visual_assets->library()->resource_revision() == 0U) {
        set_terminal_failure(
            EntityScenePackageStageState::render_asset_failed,
            entity_visual::EntityPipelineStageErrorCode::invalid_input,
            "Entity scene package stage requires immutable visual assets");
        return false;
    }
    if (state_ != EntityScenePackageStageState::waiting_for_visual_assets) {
        set_terminal_failure(
            EntityScenePackageStageState::render_asset_failed,
            entity_visual::EntityPipelineStageErrorCode::invalid_transition,
            "Visual assets arrived in the wrong scene package stage state");
        return false;
    }
    visual_assets_ = std::move(visual_assets);
    world_scene_ = std::move(world_scene);
    return transition(
        EntityScenePackageStageState::waiting_for_visual_assets,
        EntityScenePackageStageState::building_studio_render_assets);
}

bool EntityScenePackageStage::studio_render_assets_built() noexcept
{
    return transition(
        EntityScenePackageStageState::building_studio_render_assets,
        EntityScenePackageStageState::building_sprite_render_assets);
}

bool EntityScenePackageStage::sprite_render_assets_built() noexcept
{
    return transition(
        EntityScenePackageStageState::building_sprite_render_assets,
        EntityScenePackageStageState::building_entity_scene_package);
}

bool EntityScenePackageStage::publish_scene_package(
    std::shared_ptr<const EntitySceneRenderPackage> scene_package) noexcept
{
    if (state_ !=
        EntityScenePackageStageState::building_entity_scene_package) {
        set_terminal_failure(
            EntityScenePackageStageState::render_asset_failed,
            entity_visual::EntityPipelineStageErrorCode::invalid_transition,
            "Entity scene package was published in the wrong stage state");
        return false;
    }
    if (!visual_assets_ || !scene_package ||
        scene_package->resource_id() == 0U ||
        scene_package->resource_revision() == 0U ||
        scene_package->asset_library().get() !=
            visual_assets_->library().get() ||
        scene_package->asset_library_identity().resource_id !=
            visual_assets_->library()->resource_id() ||
        scene_package->asset_library_identity().revision !=
            visual_assets_->library()->resource_revision()) {
        set_terminal_failure(
            EntityScenePackageStageState::render_asset_failed,
            entity_visual::EntityPipelineStageErrorCode::invalid_input,
            "Entity scene package does not retain the exact visual asset library revision");
        return false;
    }

    const auto world_association = scene_package->world_scene_association();
    if ((world_scene_ &&
            (!world_association ||
                world_association->resource_id != world_scene_->resource_id() ||
                world_association->revision !=
                    world_scene_->resource_revision())) ||
        (!world_scene_ && world_association)) {
        set_terminal_failure(
            EntityScenePackageStageState::render_asset_failed,
            entity_visual::EntityPipelineStageErrorCode::invalid_input,
            "Entity scene package world-scene association does not match retained ownership");
        return false;
    }

    try {
        result_ = std::shared_ptr<const EntityScenePackageStageResult>{
            new EntityScenePackageStageResult{
                visual_assets_, std::move(scene_package), world_scene_}};
    } catch (const std::bad_alloc&) {
        set_terminal_failure(
            EntityScenePackageStageState::render_asset_failed,
            entity_visual::EntityPipelineStageErrorCode::unable_to_retain_result,
            "Unable to retain immutable entity scene package stage result");
        return false;
    } catch (...) {
        set_terminal_failure(
            EntityScenePackageStageState::render_asset_failed,
            entity_visual::EntityPipelineStageErrorCode::unable_to_retain_result,
            "Unable to publish immutable entity scene package stage result");
        return false;
    }
    if (!transition(
            EntityScenePackageStageState::building_entity_scene_package,
            EntityScenePackageStageState::entity_scene_package_ready)) {
        result_.reset();
        return false;
    }
    return true;
}

void EntityScenePackageStage::finish_render_asset_failed(
    const std::string_view context) noexcept
{
    set_terminal_failure(
        EntityScenePackageStageState::render_asset_failed,
        entity_visual::EntityPipelineStageErrorCode::operation_failed,
        context.empty() ? "Entity render asset construction failed" : context);
}

EntityScenePackageStageState EntityScenePackageStage::state() const noexcept
{
    return state_;
}

bool EntityScenePackageStage::terminal() const noexcept
{
    return terminal_state(state_);
}

std::size_t EntityScenePackageStage::transition_count() const noexcept
{
    return transition_count_;
}

const std::shared_ptr<const EntityScenePackageStageResult>&
EntityScenePackageStage::result() const noexcept
{
    return result_;
}

const std::optional<entity_visual::EntityPipelineStageError>&
EntityScenePackageStage::error() const noexcept
{
    return error_;
}

bool EntityScenePackageStage::transition(
    const EntityScenePackageStageState expected,
    const EntityScenePackageStageState next) noexcept
{
    if (!begun_ || state_ != expected) {
        set_terminal_failure(
            EntityScenePackageStageState::render_asset_failed,
            entity_visual::EntityPipelineStageErrorCode::invalid_transition,
            "Invalid entity scene package stage transition");
        return false;
    }
    if (transition_count_ >= limits_.maximum_transitions) {
        resume_state_ = state_;
        state_ = EntityScenePackageStageState::backpressure;
        backpressure_limit_reached_ = true;
        try {
            error_ = entity_visual::EntityPipelineStageError{
                entity_visual::EntityPipelineStageErrorCode::
                    transition_limit_reached,
                bounded_context(
                    "Entity scene package stage transition limit reached")};
        } catch (...) {
            error_ = entity_visual::EntityPipelineStageError{
                entity_visual::EntityPipelineStageErrorCode::
                    transition_limit_reached,
                {}};
        }
        return false;
    }
    ++transition_count_;
    state_ = next;
    return true;
}

void EntityScenePackageStage::set_terminal_failure(
    const EntityScenePackageStageState state,
    const entity_visual::EntityPipelineStageErrorCode code,
    const std::string_view context) noexcept
{
    if (terminal()) {
        return;
    }
    result_.reset();
    state_ = state;
    try {
        error_ = entity_visual::EntityPipelineStageError{
            code, bounded_context(context)};
    } catch (...) {
        error_ = entity_visual::EntityPipelineStageError{code, {}};
    }
}

} // namespace hlclient::entity_render
