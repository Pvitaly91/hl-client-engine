#pragma once

#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/entity_visual/entity_pipeline_stage_types.hpp>
#include <hlclient/entity_visual/entity_visual_asset_stage.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>

namespace hlclient::world_scene_render {
class WorldSceneRenderPackage;
}

namespace hlclient::entity_render {

enum class EntityScenePackageStageState {
    waiting_for_visual_assets,
    building_studio_render_assets,
    building_sprite_render_assets,
    building_entity_scene_package,
    entity_scene_package_ready,
    render_asset_failed,
    cancelled,
    timed_out,
    backpressure,
};

class EntityScenePackageStageResult final {
public:
    EntityScenePackageStageResult(const EntityScenePackageStageResult&) =
        default;
    EntityScenePackageStageResult(EntityScenePackageStageResult&&) noexcept =
        default;
    EntityScenePackageStageResult& operator=(
        const EntityScenePackageStageResult&) = delete;
    EntityScenePackageStageResult& operator=(
        EntityScenePackageStageResult&&) noexcept = delete;
    ~EntityScenePackageStageResult() = default;

    [[nodiscard]] const std::shared_ptr<
        const entity_visual::EntityVisualAssetStageResult>& visual_assets()
        const noexcept;
    [[nodiscard]] const std::shared_ptr<const EntitySceneRenderPackage>&
    scene_package() const noexcept;
    [[nodiscard]] const std::shared_ptr<
        const world_scene_render::WorldSceneRenderPackage>& world_scene()
        const noexcept;

private:
    friend class EntityScenePackageStage;

    EntityScenePackageStageResult(
        std::shared_ptr<const entity_visual::EntityVisualAssetStageResult>
            visual_assets,
        std::shared_ptr<const EntitySceneRenderPackage> scene_package,
        std::shared_ptr<const world_scene_render::WorldSceneRenderPackage>
            world_scene) noexcept;

    std::shared_ptr<const entity_visual::EntityVisualAssetStageResult>
        visual_assets_;
    std::shared_ptr<const EntitySceneRenderPackage> scene_package_;
    std::shared_ptr<const world_scene_render::WorldSceneRenderPackage>
        world_scene_;
};

// Pure sequencing/ownership contract. External CPU workers construct render
// assets and the scene package, then report completion at these boundaries.
// This class performs no importer, filesystem, network or renderer work.
class EntityScenePackageStage final {
public:
    explicit EntityScenePackageStage(
        entity_visual::EntityPipelineStageLimits limits = {}) noexcept;

    void begin(entity_visual::EntityPipelineStageTimePoint now) noexcept;
    void update(entity_visual::EntityPipelineStageTimePoint now) noexcept;
    void cancel() noexcept;
    void signal_backpressure() noexcept;
    [[nodiscard]] bool resume_from_backpressure() noexcept;

    [[nodiscard]] bool provide_visual_assets(
        std::shared_ptr<const entity_visual::EntityVisualAssetStageResult>
            visual_assets,
        std::shared_ptr<const world_scene_render::WorldSceneRenderPackage>
            world_scene = {}) noexcept;
    [[nodiscard]] bool studio_render_assets_built() noexcept;
    [[nodiscard]] bool sprite_render_assets_built() noexcept;
    [[nodiscard]] bool publish_scene_package(
        std::shared_ptr<const EntitySceneRenderPackage> scene_package) noexcept;

    void finish_render_asset_failed(std::string_view context = {}) noexcept;

    [[nodiscard]] EntityScenePackageStageState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] std::size_t transition_count() const noexcept;
    [[nodiscard]] const std::shared_ptr<const EntityScenePackageStageResult>&
    result() const noexcept;
    [[nodiscard]] const std::optional<
        entity_visual::EntityPipelineStageError>& error() const noexcept;

private:
    [[nodiscard]] bool transition(
        EntityScenePackageStageState expected,
        EntityScenePackageStageState next) noexcept;
    void set_terminal_failure(
        EntityScenePackageStageState state,
        entity_visual::EntityPipelineStageErrorCode code,
        std::string_view context) noexcept;

    entity_visual::EntityPipelineStageLimits limits_;
    EntityScenePackageStageState state_{
        EntityScenePackageStageState::waiting_for_visual_assets};
    EntityScenePackageStageState resume_state_{
        EntityScenePackageStageState::waiting_for_visual_assets};
    bool begun_{false};
    bool backpressure_limit_reached_{false};
    std::size_t transition_count_{0U};
    std::optional<entity_visual::EntityPipelineStageTimePoint> started_at_;
    std::optional<entity_visual::EntityPipelineStageTimePoint> last_update_at_;
    std::shared_ptr<const entity_visual::EntityVisualAssetStageResult>
        visual_assets_;
    std::shared_ptr<const world_scene_render::WorldSceneRenderPackage>
        world_scene_;
    std::shared_ptr<const EntityScenePackageStageResult> result_;
    std::optional<entity_visual::EntityPipelineStageError> error_;
};

[[nodiscard]] constexpr std::string_view to_string(
    EntityScenePackageStageState state) noexcept
{
    switch (state) {
    case EntityScenePackageStageState::waiting_for_visual_assets:
        return "waiting_for_visual_assets";
    case EntityScenePackageStageState::building_studio_render_assets:
        return "building_studio_render_assets";
    case EntityScenePackageStageState::building_sprite_render_assets:
        return "building_sprite_render_assets";
    case EntityScenePackageStageState::building_entity_scene_package:
        return "building_entity_scene_package";
    case EntityScenePackageStageState::entity_scene_package_ready:
        return "entity_scene_package_ready";
    case EntityScenePackageStageState::render_asset_failed:
        return "render_asset_failed";
    case EntityScenePackageStageState::cancelled: return "cancelled";
    case EntityScenePackageStageState::timed_out: return "timed_out";
    case EntityScenePackageStageState::backpressure: return "backpressure";
    }
    return "unknown";
}

} // namespace hlclient::entity_render
