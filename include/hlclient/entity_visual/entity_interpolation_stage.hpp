#pragma once

#include <hlclient/entity_visual/entity_pipeline_stage_types.hpp>
#include <hlclient/entity_visual/entity_visual_asset_stage.hpp>
#include <hlclient/goldsrc/entity_snapshot_interpolation.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>

namespace hlclient::entity_render {
class EntityRenderFrame;
}

namespace hlclient::entity_visual {

enum class EntityInterpolationStageState {
    idle,
    waiting_for_visual_assets,
    selecting_snapshot_pair,
    projecting_entities,
    interpolating_entities,
    evaluating_studio_poses,
    selecting_sprite_frames,
    building_entity_frame,
    entity_frame_ready,
    timeline_error,
    pose_error,
    unsupported_visual,
    cancelled,
    timed_out,
    backpressure,
    failed,
};

class EntityInterpolationStageResult final {
public:
    EntityInterpolationStageResult(const EntityInterpolationStageResult&) =
        default;
    EntityInterpolationStageResult(EntityInterpolationStageResult&&) noexcept =
        default;
    EntityInterpolationStageResult& operator=(
        const EntityInterpolationStageResult&) = delete;
    EntityInterpolationStageResult& operator=(
        EntityInterpolationStageResult&&) noexcept = delete;
    ~EntityInterpolationStageResult() = default;

    [[nodiscard]] const std::shared_ptr<const EntityVisualAssetStageResult>&
    visual_assets() const noexcept;
    [[nodiscard]] const std::shared_ptr<
        const goldsrc::InterpolatedEntityFrame>& interpolated_frame()
        const noexcept;
    [[nodiscard]] const std::shared_ptr<
        const entity_render::EntityRenderFrame>& render_frame() const noexcept;

private:
    friend class EntityInterpolationStage;

    EntityInterpolationStageResult(
        std::shared_ptr<const EntityVisualAssetStageResult> visual_assets,
        std::shared_ptr<const goldsrc::InterpolatedEntityFrame>
            interpolated_frame,
        std::shared_ptr<const entity_render::EntityRenderFrame> render_frame)
        noexcept;

    std::shared_ptr<const EntityVisualAssetStageResult> visual_assets_;
    std::shared_ptr<const goldsrc::InterpolatedEntityFrame>
        interpolated_frame_;
    std::shared_ptr<const entity_render::EntityRenderFrame> render_frame_;
};

// Pure sequencing/ownership contract. Pair selection, projection,
// interpolation, pose evaluation, sprite selection and renderer-neutral frame
// composition occur in external CPU workers and report completion through
// these state transitions. Terminal publication retains both the exact
// interpolated input and its coherent EntityRenderFrame result.
class EntityInterpolationStage final {
public:
    explicit EntityInterpolationStage(
        EntityPipelineStageLimits limits = {}) noexcept;

    void begin(EntityPipelineStageTimePoint now) noexcept;
    void update(EntityPipelineStageTimePoint now) noexcept;
    void cancel() noexcept;
    void signal_backpressure() noexcept;
    [[nodiscard]] bool resume_from_backpressure() noexcept;

    [[nodiscard]] bool provide_visual_assets(
        std::shared_ptr<const EntityVisualAssetStageResult> visual_assets)
        noexcept;
    [[nodiscard]] bool snapshot_pair_selected() noexcept;
    [[nodiscard]] bool entities_projected() noexcept;
    [[nodiscard]] bool entities_interpolated() noexcept;
    [[nodiscard]] bool studio_poses_evaluated() noexcept;
    [[nodiscard]] bool sprite_frames_selected() noexcept;
    [[nodiscard]] bool publish_entity_frame(
        std::shared_ptr<const goldsrc::InterpolatedEntityFrame>
            interpolated_frame,
        std::shared_ptr<const entity_render::EntityRenderFrame> render_frame)
        noexcept;

    void finish_timeline_error(std::string_view context = {}) noexcept;
    void finish_pose_error(std::string_view context = {}) noexcept;
    void finish_unsupported_visual(std::string_view context = {}) noexcept;
    void fail(std::string_view context = {}) noexcept;

    [[nodiscard]] EntityInterpolationStageState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] std::size_t transition_count() const noexcept;
    [[nodiscard]] const std::shared_ptr<const EntityInterpolationStageResult>&
    result() const noexcept;
    [[nodiscard]] const std::optional<EntityPipelineStageError>& error()
        const noexcept;

private:
    [[nodiscard]] bool transition(
        EntityInterpolationStageState expected,
        EntityInterpolationStageState next) noexcept;
    void set_terminal_failure(
        EntityInterpolationStageState state,
        EntityPipelineStageErrorCode code,
        std::string_view context) noexcept;

    EntityPipelineStageLimits limits_;
    EntityInterpolationStageState state_{EntityInterpolationStageState::idle};
    EntityInterpolationStageState resume_state_{
        EntityInterpolationStageState::idle};
    bool backpressure_limit_reached_{false};
    std::size_t transition_count_{0U};
    std::optional<EntityPipelineStageTimePoint> started_at_;
    std::optional<EntityPipelineStageTimePoint> last_update_at_;
    std::shared_ptr<const EntityVisualAssetStageResult> visual_assets_;
    std::shared_ptr<const EntityInterpolationStageResult> result_;
    std::optional<EntityPipelineStageError> error_;
};

[[nodiscard]] constexpr std::string_view to_string(
    EntityInterpolationStageState state) noexcept
{
    switch (state) {
    case EntityInterpolationStageState::idle: return "idle";
    case EntityInterpolationStageState::waiting_for_visual_assets:
        return "waiting_for_visual_assets";
    case EntityInterpolationStageState::selecting_snapshot_pair:
        return "selecting_snapshot_pair";
    case EntityInterpolationStageState::projecting_entities:
        return "projecting_entities";
    case EntityInterpolationStageState::interpolating_entities:
        return "interpolating_entities";
    case EntityInterpolationStageState::evaluating_studio_poses:
        return "evaluating_studio_poses";
    case EntityInterpolationStageState::selecting_sprite_frames:
        return "selecting_sprite_frames";
    case EntityInterpolationStageState::building_entity_frame:
        return "building_entity_frame";
    case EntityInterpolationStageState::entity_frame_ready:
        return "entity_frame_ready";
    case EntityInterpolationStageState::timeline_error:
        return "timeline_error";
    case EntityInterpolationStageState::pose_error: return "pose_error";
    case EntityInterpolationStageState::unsupported_visual:
        return "unsupported_visual";
    case EntityInterpolationStageState::cancelled: return "cancelled";
    case EntityInterpolationStageState::timed_out: return "timed_out";
    case EntityInterpolationStageState::backpressure: return "backpressure";
    case EntityInterpolationStageState::failed: return "failed";
    }
    return "unknown";
}

} // namespace hlclient::entity_visual
