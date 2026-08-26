#pragma once

#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/renderer/render_scene.hpp>
#include <hlclient/world_visibility/world_view_frustum.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace hlclient::world_spatial {
class WorldSpatialPackage;
}

namespace hlclient::interactive_preview {

enum class InteractiveEntityVisibilityRefilterErrorCode {
    invalid_output_frame_revision,
    invalid_retained_entity_number,
    scene_package_mismatch,
    invalid_world_spatial_context,
    frustum_creation_failed,
    frame_rebuild_failed,
    source_limit_exceeded,
    unable_to_retain_frame,
};

[[nodiscard]] std::string_view to_string(
    InteractiveEntityVisibilityRefilterErrorCode code) noexcept;

struct InteractiveEntityVisibilityRefilterError {
    InteractiveEntityVisibilityRefilterErrorCode code{
        InteractiveEntityVisibilityRefilterErrorCode::
            invalid_output_frame_revision};
    std::optional<world_visibility::WorldViewFrustumErrorCode> frustum_error;
    std::optional<entity_render::EntityRenderFrameErrorCode>
        frame_builder_error;
    std::optional<std::uint32_t> entity_number;
    std::string_view context;
};

// Caller-owned camera and optional PVS context for one atomic refilter. The
// source frame resource id is retained; output_frame_revision is the explicit
// identity of the newly built immutable frame.
struct InteractiveEntityVisibilityRefilterInput {
    renderer::RenderCamera camera{};
    renderer::RenderExtent extent{};
    std::uint64_t output_frame_revision{0U};
    const world_spatial::WorldSpatialPackage* spatial_package{nullptr};
    std::optional<std::uint32_t> camera_leaf_index;
    // Synthetic entity-first-person viewers may retain the controlled anchor
    // while refiltering every other candidate. No stock self-visibility
    // behavior is inferred.
    std::optional<std::uint32_t> retained_entity_number;
    entity_render::RuntimeEntityVisualLimits limits{};
};

struct InteractiveEntityVisibilityRefilterStatistics {
    std::size_t source_candidate_count{0U};
    std::size_t reset_culled_by_pvs_count{0U};
    std::size_t reset_culled_by_frustum_count{0U};
    std::size_t result_visible_count{0U};
};

struct InteractiveEntityVisibilityRefilterResult {
    std::shared_ptr<const entity_render::EntityRenderFrame> frame;
    std::optional<InteractiveEntityVisibilityRefilterError> error;
    InteractiveEntityVisibilityRefilterStatistics statistics{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return frame != nullptr && !error.has_value();
    }
};

// Renderer-neutral CPU boundary. Prior camera-dependent PVS and frustum culls
// are reset to visible candidates before the new camera-derived frustum is
// applied. Semantic unavailability/unsupported statuses remain untouched.
class InteractiveEntityVisibilityRefilter final {
public:
    [[nodiscard]] InteractiveEntityVisibilityRefilterResult refilter(
        const entity_render::EntitySceneRenderPackage& scene_package,
        const entity_render::EntityRenderFrame& source_frame,
        const InteractiveEntityVisibilityRefilterInput& input) const noexcept;
};

} // namespace hlclient::interactive_preview
