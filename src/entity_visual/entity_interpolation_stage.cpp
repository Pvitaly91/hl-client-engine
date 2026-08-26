#include <hlclient/entity_visual/entity_interpolation_stage.hpp>

#include <hlclient/entity_render/entity_scene_render.hpp>

#include <cstdint>
#include <limits>
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
    const EntityInterpolationStageState state) noexcept
{
    switch (state) {
    case EntityInterpolationStageState::entity_frame_ready:
    case EntityInterpolationStageState::timeline_error:
    case EntityInterpolationStageState::pose_error:
    case EntityInterpolationStageState::unsupported_visual:
    case EntityInterpolationStageState::cancelled:
    case EntityInterpolationStageState::timed_out:
    case EntityInterpolationStageState::failed:
        return true;
    case EntityInterpolationStageState::idle:
    case EntityInterpolationStageState::waiting_for_visual_assets:
    case EntityInterpolationStageState::selecting_snapshot_pair:
    case EntityInterpolationStageState::projecting_entities:
    case EntityInterpolationStageState::interpolating_entities:
    case EntityInterpolationStageState::evaluating_studio_poses:
    case EntityInterpolationStageState::selecting_sprite_frames:
    case EntityInterpolationStageState::building_entity_frame:
    case EntityInterpolationStageState::backpressure:
        return false;
    }
    return true;
}

[[nodiscard]] bool add_count(
    const std::size_t value, std::size_t& total) noexcept
{
    if (value > std::numeric_limits<std::size_t>::max() - total) {
        return false;
    }
    total += value;
    return true;
}

[[nodiscard]] bool same_vector(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] bool same_transform(
    const entity_render::EntityRenderTransform& transform,
    const goldsrc::InterpolatedEntityState& entity) noexcept
{
    return same_vector(transform.origin, entity.position()) &&
        same_vector(transform.rotation_degrees, entity.angles_degrees()) &&
        transform.uniform_scale == entity.scale().x;
}

[[nodiscard]] bool coherent_entity_set(
    const goldsrc::InterpolatedEntityFrame& interpolated_frame,
    const entity_render::EntityRenderFrame& render_frame) noexcept
{
    const auto studio = render_frame.studio_instances();
    const auto sprites = render_frame.sprite_instances();
    const auto unsupported = render_frame.unsupported_instances();
    std::size_t studio_index = 0U;
    std::size_t sprite_index = 0U;
    std::size_t unsupported_index = 0U;

    enum class CandidateKind { studio, sprite, unsupported };
    for (const auto& entity : interpolated_frame.entities()) {
        bool has_candidate = false;
        std::uint32_t candidate_number = 0U;
        CandidateKind candidate_kind = CandidateKind::unsupported;
        const auto consider = [&](const std::uint32_t number,
                                  const CandidateKind kind) {
            if (!has_candidate || number < candidate_number) {
                has_candidate = true;
                candidate_number = number;
                candidate_kind = kind;
            }
        };
        if (studio_index < studio.size()) {
            consider(studio[studio_index].entity_number,
                CandidateKind::studio);
        }
        if (sprite_index < sprites.size()) {
            consider(sprites[sprite_index].entity_number,
                CandidateKind::sprite);
        }
        if (unsupported_index < unsupported.size()) {
            consider(unsupported[unsupported_index].entity_number,
                CandidateKind::unsupported);
        }
        if (!has_candidate || candidate_number != entity.entity_number()) {
            return false;
        }

        switch (candidate_kind) {
        case CandidateKind::studio:
            if (!same_transform(studio[studio_index].transform, entity)) {
                return false;
            }
            ++studio_index;
            break;
        case CandidateKind::sprite:
            if (!same_transform(sprites[sprite_index].transform, entity)) {
                return false;
            }
            ++sprite_index;
            break;
        case CandidateKind::unsupported:
            ++unsupported_index;
            break;
        }
    }
    return studio_index == studio.size() && sprite_index == sprites.size() &&
        unsupported_index == unsupported.size();
}

[[nodiscard]] bool coherent_render_frame(
    const goldsrc::InterpolatedEntityFrame& interpolated_frame,
    const entity_render::EntityRenderFrame& render_frame) noexcept
{
    const auto scene_identity = render_frame.scene_package_identity();
    const auto& interpolation = render_frame.interpolation();
    const auto& statistics = render_frame.statistics();
    const auto entities = interpolated_frame.entities();

    if (interpolated_frame.evidence_profile() !=
            goldsrc::EntityInterpolationEvidenceProfile::
                synthetic_explicit_projection_v1 ||
        interpolated_frame.statistics().entity_count != entities.size() ||
        render_frame.resource_id() == 0U ||
        render_frame.resource_revision() == 0U ||
        render_frame.frame_signature() == 0U ||
        scene_identity.resource_id == 0U || scene_identity.revision == 0U ||
        interpolation.profile !=
            entity_render::EntityRenderInterpolationProfile::
                synthetic_seconds_v1 ||
        interpolation.sample_time_seconds !=
            interpolated_frame.sample_seconds() ||
        interpolation.alpha !=
            static_cast<float>(interpolated_frame.alpha()) ||
        interpolation.previous_state_identity !=
            interpolated_frame.previous_snapshot_reference() ||
        interpolation.current_state_identity !=
            interpolated_frame.current_snapshot_reference() ||
        statistics.candidate_count != entities.size() ||
        statistics.studio_instance_count !=
            render_frame.studio_instances().size() ||
        statistics.sprite_instance_count !=
            render_frame.sprite_instances().size() ||
        statistics.unsupported_instance_count !=
            render_frame.unsupported_instances().size() ||
        statistics.pose_count != render_frame.studio_poses().size() ||
        statistics.draw_count != render_frame.draw_commands().size()) {
        return false;
    }

    std::size_t categorized_count = 0U;
    if (!add_count(statistics.studio_instance_count, categorized_count) ||
        !add_count(statistics.sprite_instance_count, categorized_count) ||
        !add_count(statistics.unsupported_instance_count, categorized_count) ||
        categorized_count != statistics.candidate_count) {
        return false;
    }

    std::size_t status_count = 0U;
    if (!add_count(statistics.visible_count, status_count) ||
        !add_count(statistics.culled_by_pvs_count, status_count) ||
        !add_count(statistics.culled_by_frustum_count, status_count) ||
        !add_count(statistics.unavailable_count, status_count) ||
        !add_count(statistics.projection_pending_count, status_count) ||
        !add_count(statistics.unsupported_visual_count, status_count) ||
        status_count != statistics.candidate_count) {
        return false;
    }

    std::size_t bone_matrix_count = 0U;
    for (const auto& pose : render_frame.studio_poses()) {
        if (!add_count(pose.bone_matrices.size(), bone_matrix_count)) {
            return false;
        }
    }
    return bone_matrix_count == statistics.total_bone_matrix_count &&
        coherent_entity_set(interpolated_frame, render_frame);
}

} // namespace

EntityInterpolationStageResult::EntityInterpolationStageResult(
    std::shared_ptr<const EntityVisualAssetStageResult> visual_assets,
    std::shared_ptr<const goldsrc::InterpolatedEntityFrame> interpolated_frame,
    std::shared_ptr<const entity_render::EntityRenderFrame> render_frame)
    noexcept
    : visual_assets_{std::move(visual_assets)},
      interpolated_frame_{std::move(interpolated_frame)},
      render_frame_{std::move(render_frame)}
{
}

const std::shared_ptr<const EntityVisualAssetStageResult>&
EntityInterpolationStageResult::visual_assets() const noexcept
{
    return visual_assets_;
}

const std::shared_ptr<const goldsrc::InterpolatedEntityFrame>&
EntityInterpolationStageResult::interpolated_frame() const noexcept
{
    return interpolated_frame_;
}

const std::shared_ptr<const entity_render::EntityRenderFrame>&
EntityInterpolationStageResult::render_frame() const noexcept
{
    return render_frame_;
}

EntityInterpolationStage::EntityInterpolationStage(
    const EntityPipelineStageLimits limits) noexcept
    : limits_{limits}
{
}

void EntityInterpolationStage::begin(const EntityPipelineStageTimePoint now)
    noexcept
{
    if (state_ != EntityInterpolationStageState::idle) {
        set_terminal_failure(
            EntityInterpolationStageState::failed,
            EntityPipelineStageErrorCode::invalid_transition,
            "Entity interpolation stage was started more than once");
        return;
    }
    if (!valid_entity_pipeline_stage_limits(limits_)) {
        set_terminal_failure(
            EntityInterpolationStageState::failed,
            EntityPipelineStageErrorCode::invalid_configuration,
            "Invalid entity interpolation stage limits");
        return;
    }
    started_at_ = now;
    last_update_at_ = now;
    static_cast<void>(transition(
        EntityInterpolationStageState::idle,
        EntityInterpolationStageState::waiting_for_visual_assets));
}

void EntityInterpolationStage::update(const EntityPipelineStageTimePoint now)
    noexcept
{
    if (terminal() || state_ == EntityInterpolationStageState::idle) {
        return;
    }
    if (!last_update_at_ || !started_at_ || now < *last_update_at_) {
        set_terminal_failure(
            EntityInterpolationStageState::failed,
            EntityPipelineStageErrorCode::time_moved_backwards,
            "Entity interpolation stage time moved backwards");
        return;
    }
    last_update_at_ = now;
    if (limits_.timeout && now - *started_at_ >= *limits_.timeout) {
        set_terminal_failure(
            EntityInterpolationStageState::timed_out,
            EntityPipelineStageErrorCode::operation_failed,
            "Entity interpolation stage timed out");
    }
}

void EntityInterpolationStage::cancel() noexcept
{
    if (!terminal()) {
        state_ = EntityInterpolationStageState::cancelled;
        result_.reset();
    }
}

void EntityInterpolationStage::signal_backpressure() noexcept
{
    if (!terminal() && state_ != EntityInterpolationStageState::idle &&
        state_ != EntityInterpolationStageState::backpressure) {
        resume_state_ = state_;
        state_ = EntityInterpolationStageState::backpressure;
    }
}

bool EntityInterpolationStage::resume_from_backpressure() noexcept
{
    if (state_ != EntityInterpolationStageState::backpressure ||
        backpressure_limit_reached_) {
        return false;
    }
    state_ = resume_state_;
    return true;
}

bool EntityInterpolationStage::provide_visual_assets(
    std::shared_ptr<const EntityVisualAssetStageResult> visual_assets) noexcept
{
    if (!visual_assets || !visual_assets->snapshot_history() ||
        !visual_assets->library()) {
        set_terminal_failure(
            EntityInterpolationStageState::failed,
            EntityPipelineStageErrorCode::invalid_input,
            "Interpolation stage requires immutable visual assets and snapshot history");
        return false;
    }
    if (state_ != EntityInterpolationStageState::waiting_for_visual_assets) {
        set_terminal_failure(
            EntityInterpolationStageState::failed,
            EntityPipelineStageErrorCode::invalid_transition,
            "Visual assets arrived in the wrong interpolation stage state");
        return false;
    }
    visual_assets_ = std::move(visual_assets);
    return transition(
        EntityInterpolationStageState::waiting_for_visual_assets,
        EntityInterpolationStageState::selecting_snapshot_pair);
}

bool EntityInterpolationStage::snapshot_pair_selected() noexcept
{
    return transition(
        EntityInterpolationStageState::selecting_snapshot_pair,
        EntityInterpolationStageState::projecting_entities);
}

bool EntityInterpolationStage::entities_projected() noexcept
{
    return transition(
        EntityInterpolationStageState::projecting_entities,
        EntityInterpolationStageState::interpolating_entities);
}

bool EntityInterpolationStage::entities_interpolated() noexcept
{
    return transition(
        EntityInterpolationStageState::interpolating_entities,
        EntityInterpolationStageState::evaluating_studio_poses);
}

bool EntityInterpolationStage::studio_poses_evaluated() noexcept
{
    return transition(
        EntityInterpolationStageState::evaluating_studio_poses,
        EntityInterpolationStageState::selecting_sprite_frames);
}

bool EntityInterpolationStage::sprite_frames_selected() noexcept
{
    return transition(
        EntityInterpolationStageState::selecting_sprite_frames,
        EntityInterpolationStageState::building_entity_frame);
}

bool EntityInterpolationStage::publish_entity_frame(
    std::shared_ptr<const goldsrc::InterpolatedEntityFrame> interpolated_frame,
    std::shared_ptr<const entity_render::EntityRenderFrame> render_frame)
    noexcept
{
    if (state_ != EntityInterpolationStageState::building_entity_frame) {
        set_terminal_failure(
            EntityInterpolationStageState::failed,
            EntityPipelineStageErrorCode::invalid_transition,
            "Entity frame was published in the wrong interpolation state");
        return false;
    }
    if (!visual_assets_ || !interpolated_frame || !render_frame ||
        interpolated_frame->previous_snapshot_reference() == 0U ||
        interpolated_frame->current_snapshot_reference() == 0U ||
        !coherent_render_frame(*interpolated_frame, *render_frame)) {
        set_terminal_failure(
            EntityInterpolationStageState::failed,
            EntityPipelineStageErrorCode::invalid_input,
            "Interpolation and renderer-neutral frame evidence is incomplete or incoherent");
        return false;
    }
    try {
        result_ = std::shared_ptr<const EntityInterpolationStageResult>{
            new EntityInterpolationStageResult{
                visual_assets_,
                std::move(interpolated_frame),
                std::move(render_frame)}};
    } catch (const std::bad_alloc&) {
        set_terminal_failure(
            EntityInterpolationStageState::failed,
            EntityPipelineStageErrorCode::unable_to_retain_result,
            "Unable to retain immutable interpolation stage result");
        return false;
    } catch (...) {
        set_terminal_failure(
            EntityInterpolationStageState::failed,
            EntityPipelineStageErrorCode::unable_to_retain_result,
            "Unable to publish immutable interpolation stage result");
        return false;
    }
    if (!transition(
            EntityInterpolationStageState::building_entity_frame,
            EntityInterpolationStageState::entity_frame_ready)) {
        result_.reset();
        return false;
    }
    return true;
}

void EntityInterpolationStage::finish_timeline_error(
    const std::string_view context) noexcept
{
    set_terminal_failure(
        EntityInterpolationStageState::timeline_error,
        EntityPipelineStageErrorCode::operation_failed,
        context.empty() ? "Entity snapshot timeline selection failed" : context);
}

void EntityInterpolationStage::finish_pose_error(
    const std::string_view context) noexcept
{
    set_terminal_failure(
        EntityInterpolationStageState::pose_error,
        EntityPipelineStageErrorCode::operation_failed,
        context.empty() ? "Entity Studio pose evaluation failed" : context);
}

void EntityInterpolationStage::finish_unsupported_visual(
    const std::string_view context) noexcept
{
    set_terminal_failure(
        EntityInterpolationStageState::unsupported_visual,
        EntityPipelineStageErrorCode::operation_failed,
        context.empty() ? "Entity visual profile is unsupported" : context);
}

void EntityInterpolationStage::fail(const std::string_view context) noexcept
{
    set_terminal_failure(
        EntityInterpolationStageState::failed,
        EntityPipelineStageErrorCode::operation_failed,
        context.empty() ? "Entity interpolation stage failed" : context);
}

EntityInterpolationStageState EntityInterpolationStage::state() const noexcept
{
    return state_;
}

bool EntityInterpolationStage::terminal() const noexcept
{
    return terminal_state(state_);
}

std::size_t EntityInterpolationStage::transition_count() const noexcept
{
    return transition_count_;
}

const std::shared_ptr<const EntityInterpolationStageResult>&
EntityInterpolationStage::result() const noexcept
{
    return result_;
}

const std::optional<EntityPipelineStageError>&
EntityInterpolationStage::error() const noexcept
{
    return error_;
}

bool EntityInterpolationStage::transition(
    const EntityInterpolationStageState expected,
    const EntityInterpolationStageState next) noexcept
{
    if (state_ != expected) {
        set_terminal_failure(
            EntityInterpolationStageState::failed,
            EntityPipelineStageErrorCode::invalid_transition,
            "Invalid entity interpolation stage transition");
        return false;
    }
    if (transition_count_ >= limits_.maximum_transitions) {
        resume_state_ = state_;
        state_ = EntityInterpolationStageState::backpressure;
        backpressure_limit_reached_ = true;
        try {
            error_ = EntityPipelineStageError{
                EntityPipelineStageErrorCode::transition_limit_reached,
                bounded_context(
                    "Entity interpolation stage transition limit reached")};
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

void EntityInterpolationStage::set_terminal_failure(
    const EntityInterpolationStageState state,
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
