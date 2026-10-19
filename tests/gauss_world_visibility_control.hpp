#pragma once

#include <hlclient/renderer/render_scene.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>
#include <hlclient/world_visibility/world_visibility_resolver.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace hlclient::tests {

enum class RuntimeWorldVisibilityStatus : std::uint8_t {
    not_observed, selected, cached, no_scene, invalid_camera_or_extent,
    invalid_scene, resolve_failed, draw_list_failed, allocation_failed,
    revision_exhausted
};

struct RuntimeWorldVisibilityStatistics {
    std::uint64_t adapter_rebuilds{},resolves{},cache_hits{},fallbacks{},failures{};
    RuntimeWorldVisibilityStatus status{RuntimeWorldVisibilityStatus::not_observed};
    std::optional<std::uint32_t> camera_leaf;
    world_visibility::WorldPvsFallbackReason pvs_fallback{
        world_visibility::WorldPvsFallbackReason::none};
    world_visibility::WorldVisibilityStatistics selection{};
    world_visibility::WorldVisibleDrawListStatistics draw_list{};
};

// Rejected 71b97 experiment, retained only as a test/performance control.
// Actual Crossfire pixels did not match and moving-camera cost regressed.
// This is not a runtime application API or a production rendering path.
// Final-camera render-only selection. It never changes ClientWorldState,
// network/prediction/collision or runtime brush entities. The cache holds one
// immutable scene and one exact camera/extent result, with existing resolver
// and draw-list bounds. On failure the caller retains its original all-world
// rendering path, never a stale previous-camera subset.
class RuntimeWorldVisibility final {
public:
    explicit RuntimeWorldVisibility(
        world_visibility::WorldVisibilityLimits limits = {},
        world_visibility::WorldVisibleDrawListLimits draw_limits = {}) noexcept;
    [[nodiscard]] bool apply(renderer::RenderScene& scene,
        renderer::RenderExtent extent) noexcept;
    void reset() noexcept;
    [[nodiscard]] const RuntimeWorldVisibilityStatistics& statistics() const noexcept;

private:
    void prepare_adapters(
        const std::shared_ptr<const world_scene_render::WorldSceneRenderPackage>& package);
    void publish(renderer::RenderStaticWorld& world) const noexcept;
    world_visibility::WorldVisibilityLimits limits_;
    world_visibility::WorldVisibleDrawListLimits draw_limits_;
    std::shared_ptr<const world_scene_render::WorldSceneRenderPackage> package_;
    std::vector<world_visibility::WorldVisibilityBrushInstanceInput> visibility_brushes_;
    std::vector<world_visibility::WorldVisibleBrushModelInput> draw_models_;
    std::vector<world_visibility::WorldVisibleBrushInstanceDrawInput> draw_brushes_;
    std::shared_ptr<const world_visibility::WorldVisibilitySet> visibility_;
    std::shared_ptr<const world_visibility::WorldVisibleDrawList> draw_list_;
    std::optional<renderer::RenderCamera> camera_;
    renderer::RenderExtent extent_{};
    std::uint64_t next_revision_{1U};
    RuntimeWorldVisibilityStatistics statistics_;
};

} // namespace hlclient::tests
