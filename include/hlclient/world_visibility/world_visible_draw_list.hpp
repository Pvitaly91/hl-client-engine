#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/assets/world_texture_types.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/world_render/world_render_types.hpp>
#include <hlclient/world_visibility/world_visibility_types.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::world_visibility {

enum class WorldVisibleObjectKind {
    world_surface,
    brush_instance_surface,
};

// Narrow adapter record populated from WorldRenderSurfaceRange. Keeping the
// builder input non-owning lets the final scene package remain the sole owner
// of render geometry and materials.
struct WorldVisibleSurfaceInput {
    std::uint32_t source_surface_index{0U};
    std::uint32_t first_index{0U};
    std::uint32_t index_count{0U};
    std::size_t render_material_index{0U};
    assets::WorldBounds bounds{};
    assets::WorldTextureAlphaMode alpha_mode{
        assets::WorldTextureAlphaMode::opaque};
    world_render::WorldRenderLightmapMode lightmap_mode{
        world_render::WorldRenderLightmapMode::unlit_white};
    std::optional<std::size_t> lightmap_atlas_page_index;
};

struct WorldVisibleBrushModelInput {
    std::uint32_t source_model_index{0U};
    std::size_t index_buffer_index_count{0U};
    std::size_t render_material_count{0U};
    std::span<const WorldVisibleSurfaceInput> surfaces;
};

struct WorldVisibleBrushInstanceDrawInput {
    std::uint32_t source_instance_index{0U};
    std::uint32_t source_model_index{0U};
    renderer::RenderMatrix4 model_transform{};
};

struct WorldVisibleDrawCommand {
    WorldVisibleObjectKind object_kind{WorldVisibleObjectKind::world_surface};
    std::uint32_t first_index{0U};
    std::uint32_t index_count{0U};
    std::size_t render_material_index{0U};
    renderer::RenderMatrix4 model_transform{};
    std::uint32_t source_surface_index{0U};
    std::optional<std::uint32_t> source_model_index;
    std::optional<std::uint32_t> source_instance_index;
    assets::WorldTextureAlphaMode alpha_mode{
        assets::WorldTextureAlphaMode::opaque};
    world_render::WorldRenderLightmapMode lightmap_mode{
        world_render::WorldRenderLightmapMode::unlit_white};
    std::optional<std::size_t> lightmap_atlas_page_index;
};

struct WorldVisibleDrawListStatistics {
    std::size_t command_count{0U};
    std::size_t world_command_count{0U};
    std::size_t brush_command_count{0U};
    std::size_t opaque_command_count{0U};
    std::size_t masked_command_count{0U};
    std::size_t triangle_count{0U};
};

class WorldVisibleDrawListBuilder;

class WorldVisibleDrawList final {
public:
    WorldVisibleDrawList(const WorldVisibleDrawList&) = delete;
    WorldVisibleDrawList& operator=(const WorldVisibleDrawList&) = delete;
    WorldVisibleDrawList(WorldVisibleDrawList&& other) noexcept;
    WorldVisibleDrawList& operator=(WorldVisibleDrawList&&) noexcept = delete;
    ~WorldVisibleDrawList() = default;

    [[nodiscard]] std::span<const WorldVisibleDrawCommand> commands() const noexcept;
    [[nodiscard]] const WorldVisibleDrawListStatistics& statistics() const noexcept;
    [[nodiscard]] std::uint64_t visibility_revision() const noexcept;
    [[nodiscard]] WorldVisibilitySceneIdentity scene_identity() const noexcept;
    [[nodiscard]] WorldVisibilityResultSignature result_signature() const noexcept;

private:
    friend class WorldVisibleDrawListBuilder;

    WorldVisibleDrawList(
        std::vector<WorldVisibleDrawCommand> commands,
        WorldVisibleDrawListStatistics statistics,
        std::uint64_t visibility_revision,
        WorldVisibilitySceneIdentity scene_identity,
        WorldVisibilityResultSignature result_signature) noexcept;

    std::vector<WorldVisibleDrawCommand> commands_;
    WorldVisibleDrawListStatistics statistics_{};
    std::uint64_t visibility_revision_{0U};
    WorldVisibilitySceneIdentity scene_identity_{};
    WorldVisibilityResultSignature result_signature_{};
};

struct WorldVisibleDrawListLimits {
    std::size_t maximum_draw_commands{131'072U};
};

struct WorldVisibleDrawListBuildInput {
    const WorldVisibilitySet* visibility{nullptr};
    std::span<const WorldVisibleSurfaceInput> world_surfaces;
    std::size_t world_index_buffer_index_count{0U};
    std::size_t world_render_material_count{0U};
    std::span<const WorldVisibleBrushModelInput> brush_models;
    std::span<const WorldVisibleBrushInstanceDrawInput> brush_instances;
};

[[nodiscard]] std::uint64_t world_visible_draw_input_signature(
    const WorldVisibleDrawListBuildInput& input) noexcept;

enum class WorldVisibleDrawListErrorCode {
    invalid_input,
    duplicate_surface_range,
    duplicate_brush_model,
    duplicate_brush_instance,
    visible_surface_not_found,
    visible_brush_instance_not_found,
    brush_model_not_found,
    invalid_index_range,
    invalid_material_reference,
    invalid_material_profile,
    non_finite_transform,
    command_limit_exceeded,
    unable_to_retain_draw_list,
};

[[nodiscard]] std::string_view to_string(
    WorldVisibleDrawListErrorCode code) noexcept;

struct WorldVisibleDrawListError {
    WorldVisibleDrawListErrorCode code{
        WorldVisibleDrawListErrorCode::invalid_input};
    std::optional<std::size_t> element_index;
    std::string message;
};

struct WorldVisibleDrawListBuildResult {
    std::optional<WorldVisibleDrawList> draw_list;
    std::optional<WorldVisibleDrawListError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return draw_list.has_value();
    }
};

class WorldVisibleDrawListBuilder final {
public:
    [[nodiscard]] WorldVisibleDrawListBuildResult build(
        const WorldVisibleDrawListBuildInput& input,
        const WorldVisibleDrawListLimits& limits = {}) const;
};

} // namespace hlclient::world_visibility
