#pragma once

#include <hlclient/renderer/render_scene.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>
#include <hlclient/world_visibility/world_view_frustum.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::renderer::opengl {

enum class PreparedWorldVisibilityStatus {
    unprepared,
    ready,
    invalid_spatial,
    invalid_geometry,
    allocation_failed,
};

enum class PreparedWorldPvsFallback {
    none,
    policy_disabled,
    invalid_or_absent_pvs,
    row_unavailable,
    solid_or_special_leaf,
    liquid_or_content_boundary,
};

[[nodiscard]] std::string_view to_string(PreparedWorldPvsFallback value) noexcept;

struct PreparedWorldVisibilityStatistics {
    // Per-selection counters. Query work is separate from render-tree work.
    std::uint64_t visited_nodes{};
    std::uint64_t query_nodes{};
    std::uint64_t pvs_rejected_nodes{};
    std::uint64_t frustum_rejected_nodes{};
    std::uint64_t considered_surfaces{};
    std::uint64_t selected_surfaces{};
    std::uint64_t tested_bounds{};
    std::uint64_t backface_rejected_surfaces{};
    std::uint64_t pvs_candidate_surfaces{};
    std::optional<std::uint32_t> camera_leaf;
    PreparedWorldPvsFallback fallback{PreparedWorldPvsFallback::none};
    // Lifetime counters of this cache. Selection performs no allocation.
    std::uint64_t preparation_count{};
    std::uint64_t pvs_rebuilds{};
    std::uint64_t cache_hits{};
    std::size_t prepared_bytes{};
    // Retained nonempty vector-storage count, not a global malloc counter or
    // a count of temporary cold validation allocations.
    std::size_t storage_allocation_count{};
};

// Internal trusted adapter, not the public DTO resolver. The enclosing GPU
// scene cache must retain the immutable scene owner until this object is reset.
// Validation and all storage growth occur in prepare(), never in select().
class PreparedWorldVisibility final {
public:
    PreparedWorldVisibility() = default;
    PreparedWorldVisibility(const PreparedWorldVisibility&) = delete;
    PreparedWorldVisibility& operator=(const PreparedWorldVisibility&) = delete;
    PreparedWorldVisibility(PreparedWorldVisibility&&) noexcept = default;
    PreparedWorldVisibility& operator=(PreparedWorldVisibility&&) noexcept = default;

    [[nodiscard]] bool prepare(
        const world_scene_render::WorldSceneRenderPackage& scene) noexcept;
    [[nodiscard]] bool select(
        const RenderCamera& final_camera,
        const world_visibility::WorldViewFrustum& frustum,
        bool cull_back,
        bool use_pvs = true,
        bool cull_retained_sky = false) noexcept;
    void reset() noexcept;

    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] PreparedWorldVisibilityStatus status() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> selected_surfaces() const noexcept;
    // Context candidates indexed by immutable surface_ranges position. Updated
    // only on pvs_rebuilds, and a conservative superset of selected_surfaces().
    [[nodiscard]] std::span<const std::uint8_t> pvs_surfaces() const noexcept;
    [[nodiscard]] const PreparedWorldVisibilityStatistics& statistics() const noexcept;

private:
    static constexpr std::uint32_t invalid_index = 0xffffffffU;
    struct Node {
        assets::WorldBounds render_bounds{};
        std::uint32_t parent{invalid_index};
        std::uint32_t depth{};
        std::uint32_t first_surface{};
        std::uint32_t surface_count{};
        bool has_geometry{};
    };
    struct Surface {
        assets::WorldBounds bounds{};
        std::array<double, 3U> normal{};
        double plane_offset{};
        std::uint32_t owner{invalid_index};
        bool plane_valid{};
        bool unlinked{};
        bool sky{};
    };
    struct StackEntry {
        std::uint32_t node{};
        std::uint8_t plane_mask{};
    };

    [[nodiscard]] bool prepare_impl(
        const world_scene_render::WorldSceneRenderPackage& scene);
    [[nodiscard]] std::uint32_t locate_leaf(assets::AssetVector3 point) noexcept;
    [[nodiscard]] bool content_boundary(
        assets::AssetVector3 point, double radius, std::uint32_t camera_leaf) noexcept;
    [[nodiscard]] std::uint32_t common_ancestor(
        std::uint32_t left, std::uint32_t right) const noexcept;
    void rebuild_pvs(std::uint32_t leaf, PreparedWorldPvsFallback fallback) noexcept;
    void mark_node_ancestors(std::uint32_t node) noexcept;

    const world_scene_render::WorldSceneRenderPackage* scene_{};
    PreparedWorldVisibilityStatus status_{PreparedWorldVisibilityStatus::unprepared};
    std::vector<Node> nodes_;
    std::vector<Surface> surfaces_;
    std::vector<std::uint32_t> leaf_parent_;
    std::vector<std::uint32_t> owned_surfaces_;
    std::vector<std::uint32_t> always_surfaces_;
    std::vector<double> plane_lengths_;
    std::vector<std::uint8_t> pvs_nodes_;
    std::vector<std::uint8_t> pvs_surfaces_;
    std::vector<std::uint8_t> selected_;
    std::vector<StackEntry> stack_;
    std::vector<std::uint32_t> query_stack_;
    std::optional<std::uint32_t> context_leaf_;
    PreparedWorldPvsFallback context_fallback_{PreparedWorldPvsFallback::none};
    bool pvs_valid_{};
    std::uint64_t context_candidate_count_{};
    PreparedWorldVisibilityStatistics statistics_{};
};

} // namespace hlclient::renderer::opengl
