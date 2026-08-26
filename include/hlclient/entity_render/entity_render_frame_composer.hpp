#pragma once

#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/entity_visual/entity_visual_projection.hpp>
#include <hlclient/goldsrc/entity_snapshot_interpolation.hpp>
#include <hlclient/goldsrc/sprite/goldsrc_sprite_playback.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_pose.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::entity_render {

// Caller-owned identity and visibility context for one atomic composition.
// The expected package identity prevents an interpolated frame from being
// paired with a different immutable asset package. The frame identity is also
// the bounded per-frame Studio pose-cache token.
struct EntityRenderFrameCompositionInput {
    EntityRenderResourceIdentity expected_scene_package_identity{};
    EntityRenderResourceIdentity frame_identity{};
    double previous_time_seconds{0.0};
    double current_time_seconds{0.0};
    const world_visibility::WorldViewFrustum* view_frustum{nullptr};
    const world_spatial::WorldSpatialPackage* spatial_package{nullptr};
    std::optional<std::uint32_t> camera_leaf_index;
};

enum class EntityRenderFrameComposerErrorCode {
    invalid_configuration,
    scene_package_mismatch,
    interpolation_evidence_pending,
    missing_asset_library,
    render_asset_mismatch,
    missing_skeletal_data,
    studio_pose_failed,
    sprite_selection_failed,
    non_finite_transformed_bounds,
    render_frame_rejected,
    source_limit_exceeded,
    unable_to_retain_composition,
};

[[nodiscard]] std::string_view to_string(
    EntityRenderFrameComposerErrorCode code) noexcept;

struct EntityRenderFrameComposerError {
    EntityRenderFrameComposerErrorCode code{
        EntityRenderFrameComposerErrorCode::invalid_configuration};
    std::optional<std::uint32_t> entity_number;
    std::optional<entity_visual::EntityVisualModelReference> model_reference;
    std::optional<goldsrc::studio::StudioPoseErrorCode> studio_pose_error;
    std::optional<goldsrc::sprite::SpritePlaybackErrorCode>
        sprite_playback_error;
    std::optional<EntityRenderFrameErrorCode> render_frame_error;
    std::string context;
};

struct EntityRenderFrameCompositionResult {
    std::optional<EntityRenderFrame> frame;
    std::optional<EntityRenderFrameComposerError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return frame.has_value() && !error.has_value();
    }
};

// Production renderer-neutral bridge from an exact interpolated visual frame
// to an immutable render frame. Asset lookup is typed and exact; pose and
// Sprite failures reject the whole composition, while intentionally
// unsupported render profiles are retained as typed unsupported metadata.
class EntityRenderFrameComposer final {
public:
    [[nodiscard]] EntityRenderFrameCompositionResult compose(
        const EntitySceneRenderPackage& scene_package,
        const goldsrc::InterpolatedEntityFrame& interpolated_frame,
        const EntityRenderFrameCompositionInput& input,
        goldsrc::studio::StudioPoseCache& pose_cache,
        const RuntimeEntityVisualLimits& limits = {},
        const goldsrc::sprite::SpritePlaybackLimits& sprite_limits = {}) const;
};

} // namespace hlclient::entity_render
