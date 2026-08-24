#pragma once

#include <hlclient/renderer/render_scene.hpp>
#include <hlclient/world_spatial/world_spatial_types.hpp>
#include <hlclient/world_visibility/world_visible_draw_list.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hlclient::world_visibility {

struct WorldVisibilityBrushInstanceInput {
    std::uint32_t source_instance_index{0U};
    assets::WorldBounds transformed_bounds{};
    std::span<const std::uint32_t> touched_leaf_indices;
    bool supported_for_static_opaque_rendering{false};
};

struct WorldVisibilityResolveInput {
    const world_spatial::WorldSpatialPackage* spatial_package{nullptr};
    std::span<const WorldVisibleSurfaceInput> world_surfaces;
    std::span<const WorldVisibilityBrushInstanceInput> brush_instances;
    renderer::RenderCamera camera{};
    renderer::RenderExtent extent{};
    WorldVisibilityMode mode{WorldVisibilityMode::all};
    WorldPvsFallbackPolicy pvs_fallback_policy{
        WorldPvsFallbackPolicy::frustum_only};
    std::uint64_t revision{1U};
    WorldVisibilitySceneIdentity scene_identity{};
    bool brush_instances_enabled{true};
};

// Stable, allocation-free signature of the immutable spatial/surface/brush
// adapters. Camera, mode, fallback, revision, limits, and the runtime brush
// enable switch are intentionally excluded.
[[nodiscard]] std::uint64_t world_visibility_input_signature(
    const WorldVisibilityResolveInput& input) noexcept;

struct WorldVisibilityLimits {
    std::size_t maximum_visible_leaves{8'192U};
    std::size_t maximum_visible_world_surfaces{65'535U};
    std::size_t maximum_visible_brush_instances{4'096U};
    std::size_t maximum_draw_commands{131'072U};
    std::size_t maximum_surface_dedup_bytes{1U * 1024U * 1024U};
    std::size_t maximum_spatial_query_steps{65'536U};
};

enum class WorldVisibilityErrorCode {
    invalid_configuration,
    invalid_camera,
    invalid_extent,
    invalid_world_surface,
    duplicate_world_surface,
    invalid_brush_instance,
    duplicate_brush_instance,
    invalid_touched_leaf,
    invalid_spatial_surface_reference,
    invalid_spatial_package,
    invalid_frustum,
    visible_leaf_limit_exceeded,
    visible_world_surface_limit_exceeded,
    visible_brush_instance_limit_exceeded,
    draw_command_limit_exceeded,
    surface_dedup_limit_exceeded,
    unable_to_retain_visibility,
};

[[nodiscard]] std::string_view to_string(
    WorldVisibilityErrorCode code) noexcept;

struct WorldVisibilityError {
    WorldVisibilityErrorCode code{
        WorldVisibilityErrorCode::invalid_configuration};
    std::optional<std::size_t> element_index;
    std::string message;
};

struct WorldVisibilityResolveResult {
    std::optional<WorldVisibilitySet> visibility;
    std::optional<WorldVisibilityError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return visibility.has_value();
    }
};

class WorldVisibilityResolver final {
public:
    [[nodiscard]] WorldVisibilityResolveResult resolve(
        const WorldVisibilityResolveInput& input,
        const WorldVisibilityLimits& limits = {}) const;
};

} // namespace hlclient::world_visibility
