#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/world_render/world_render_types.hpp>
#include <hlclient/world_spatial/world_spatial_types.hpp>
#include <hlclient/world_visibility/world_visible_draw_list.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::world_scene_render {

enum class WorldSceneRenderCompatibilityProfile {
    goldsrc_static_world_visibility_and_brush_instances_v1,
};

enum class WorldSceneRenderEvidenceProfile {
    canonical_bsp_m4_1_textures_m4_2_lightmaps_m4_3_and_m4_4_spatial,
};

enum class BrushSubmodelRenderSupportStatus {
    supported_static_opaque,
    unsupported_transform,
    unsupported_rendermode,
    invalid_model_reference,
    missing_model_geometry,
    invalid_entity_metadata,
    outside_world_spatial_tree,
    no_visible_leaf_membership,
};

class WorldSceneRenderPackageBuilder;

// Immutable owning renderer-neutral brush-model metadata. Construction sets
// the complete value atomically; consumers receive only const views.
class BrushSubmodelRenderModel final {
public:
    BrushSubmodelRenderModel(
        std::uint32_t source_model_index,
        assets::WorldBounds local_bounds,
        std::vector<std::uint32_t> render_surface_indices,
        std::vector<world_visibility::WorldVisibleSurfaceInput> surfaces = {})
        noexcept;

    BrushSubmodelRenderModel(const BrushSubmodelRenderModel&) = default;
    BrushSubmodelRenderModel(BrushSubmodelRenderModel&&) noexcept = default;
    BrushSubmodelRenderModel& operator=(const BrushSubmodelRenderModel&) = delete;
    BrushSubmodelRenderModel& operator=(BrushSubmodelRenderModel&&) = delete;
    ~BrushSubmodelRenderModel() = default;

    [[nodiscard]] std::uint32_t source_model_index() const noexcept;
    [[nodiscard]] const assets::WorldBounds& local_bounds() const noexcept;
    [[nodiscard]] std::span<const std::uint32_t> render_surface_indices()
        const noexcept;
    [[nodiscard]] std::span<const world_visibility::WorldVisibleSurfaceInput>
    surfaces() const noexcept;

private:
    friend class WorldSceneRenderPackageBuilder;

    std::uint32_t source_model_index_{0U};
    assets::WorldBounds local_bounds_{};
    std::vector<std::uint32_t> render_surface_indices_;
    std::vector<world_visibility::WorldVisibleSurfaceInput> surfaces_;
};

// Immutable owning library. The default value represents an empty library;
// the value constructor publishes the render package and complete model table
// together, so no partially mutated public state can escape.
class BrushSubmodelRenderLibrary final {
public:
    BrushSubmodelRenderLibrary() noexcept = default;
    BrushSubmodelRenderLibrary(
        std::shared_ptr<const world_render::WorldRenderPackage> render_package,
        std::vector<BrushSubmodelRenderModel> models) noexcept;

    BrushSubmodelRenderLibrary(const BrushSubmodelRenderLibrary&) = default;
    BrushSubmodelRenderLibrary(BrushSubmodelRenderLibrary&&) noexcept = default;
    BrushSubmodelRenderLibrary& operator=(
        const BrushSubmodelRenderLibrary&) = delete;
    BrushSubmodelRenderLibrary& operator=(BrushSubmodelRenderLibrary&&) = delete;
    ~BrushSubmodelRenderLibrary() = default;

    [[nodiscard]] const std::shared_ptr<
        const world_render::WorldRenderPackage>& render_package() const noexcept;
    [[nodiscard]] std::span<const BrushSubmodelRenderModel> models()
        const noexcept;

private:
    friend class WorldSceneRenderPackageBuilder;

    std::shared_ptr<const world_render::WorldRenderPackage> render_package_;
    std::vector<BrushSubmodelRenderModel> models_;
};

struct BrushSubmodelRenderInstance {
    std::uint32_t source_instance_index{0U};
    std::size_t source_entity_ordinal{0U};
    std::optional<std::uint32_t> source_model_index;
    renderer::RenderMatrix4 model_transform{};
    assets::WorldBounds transformed_bounds{};
    std::vector<std::uint32_t> touched_leaf_indices;
    BrushSubmodelRenderSupportStatus support_status{
        BrushSubmodelRenderSupportStatus::invalid_entity_metadata};
};

struct WorldSceneRendererResourceIdentity {
    std::uint64_t resource_id{0U};
    std::uint64_t revision{0U};

    [[nodiscard]] friend bool operator==(
        const WorldSceneRendererResourceIdentity&,
        const WorldSceneRendererResourceIdentity&) = default;
};

struct WorldSceneRenderStatistics {
    std::size_t world_surface_count{0U};
    std::size_t brush_model_count{0U};
    std::size_t brush_surface_count{0U};
    std::size_t brush_instance_count{0U};
    std::size_t supported_brush_instance_count{0U};
    std::size_t unsupported_brush_instance_count{0U};
};

enum class WorldSceneRenderErrorCode {
    invalid_world_package,
    invalid_spatial_package,
    invalid_world_surface_range,
    invalid_brush_library,
    invalid_brush_model,
    duplicate_brush_model,
    duplicate_brush_surface,
    invalid_brush_instance,
    duplicate_brush_instance,
    missing_brush_model,
    invalid_scene_bounds,
    unable_to_retain_scene,
};

[[nodiscard]] std::string_view to_string(
    WorldSceneRenderErrorCode code) noexcept;

struct WorldSceneRenderError {
    WorldSceneRenderErrorCode code{
        WorldSceneRenderErrorCode::invalid_world_package};
    std::optional<std::size_t> element_index;
    std::string context;
};

class WorldSceneRenderPackage final {
public:
    WorldSceneRenderPackage(const WorldSceneRenderPackage&) = delete;
    WorldSceneRenderPackage& operator=(const WorldSceneRenderPackage&) = delete;
    WorldSceneRenderPackage(WorldSceneRenderPackage&&) noexcept = default;
    WorldSceneRenderPackage& operator=(WorldSceneRenderPackage&&) noexcept = delete;
    ~WorldSceneRenderPackage() = default;

    [[nodiscard]] const std::shared_ptr<const world_render::WorldRenderPackage>&
    world_package() const noexcept;
    [[nodiscard]] const world_spatial::WorldSpatialPackage& spatial_package()
        const noexcept;
    [[nodiscard]] const BrushSubmodelRenderLibrary& brush_library() const noexcept;
    [[nodiscard]] std::span<const BrushSubmodelRenderInstance> brush_instances()
        const noexcept;
    [[nodiscard]] std::span<const world_visibility::WorldVisibleSurfaceInput>
    world_surfaces() const noexcept;
    [[nodiscard]] const assets::WorldBounds& bounds() const noexcept;
    [[nodiscard]] const WorldSceneRenderStatistics& statistics() const noexcept;
    [[nodiscard]] WorldSceneRendererResourceIdentity resource_identity()
        const noexcept;
    [[nodiscard]] world_visibility::WorldVisibilitySceneIdentity
    visibility_scene_identity() const noexcept;
    [[nodiscard]] std::uint64_t resource_id() const noexcept;
    [[nodiscard]] std::uint64_t resource_revision() const noexcept;
    [[nodiscard]] WorldSceneRenderCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] WorldSceneRenderEvidenceProfile evidence_profile() const noexcept;

private:
    friend class WorldSceneRenderPackageBuilder;

    WorldSceneRenderPackage(
        std::shared_ptr<const world_render::WorldRenderPackage> world_package,
        world_spatial::WorldSpatialPackage spatial_package,
        BrushSubmodelRenderLibrary brush_library,
        std::vector<BrushSubmodelRenderInstance> brush_instances,
        std::vector<world_visibility::WorldVisibleSurfaceInput> world_surfaces,
        assets::WorldBounds bounds,
        WorldSceneRenderStatistics statistics,
        WorldSceneRendererResourceIdentity resource_identity,
        world_visibility::WorldVisibilitySceneIdentity
            visibility_scene_identity) noexcept;

    std::shared_ptr<const world_render::WorldRenderPackage> world_package_;
    world_spatial::WorldSpatialPackage spatial_package_;
    BrushSubmodelRenderLibrary brush_library_;
    std::vector<BrushSubmodelRenderInstance> brush_instances_;
    std::vector<world_visibility::WorldVisibleSurfaceInput> world_surfaces_;
    assets::WorldBounds bounds_{};
    WorldSceneRenderStatistics statistics_{};
    WorldSceneRendererResourceIdentity resource_identity_{};
    world_visibility::WorldVisibilitySceneIdentity visibility_scene_identity_{};
    WorldSceneRenderCompatibilityProfile compatibility_profile_{
        WorldSceneRenderCompatibilityProfile::
            goldsrc_static_world_visibility_and_brush_instances_v1};
    WorldSceneRenderEvidenceProfile evidence_profile_{
        WorldSceneRenderEvidenceProfile::
            canonical_bsp_m4_1_textures_m4_2_lightmaps_m4_3_and_m4_4_spatial};
};

struct WorldSceneRenderPackageBuildResult {
    std::optional<WorldSceneRenderPackage> package;
    std::optional<WorldSceneRenderError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return package.has_value();
    }
};

struct WorldSceneRenderPackageLimits {
    std::size_t maximum_world_surfaces{65'535U};
    std::size_t maximum_brush_models{400U};
    std::size_t maximum_brush_surfaces{65'535U};
    std::size_t maximum_brush_instances{4'096U};
    std::size_t maximum_instance_leaf_links{4'096U * 8'192U};
};

class WorldSceneRenderPackageBuilder final {
public:
    [[nodiscard]] WorldSceneRenderPackageBuildResult build(
        std::shared_ptr<const world_render::WorldRenderPackage> world_package,
        world_spatial::WorldSpatialPackage spatial_package,
        BrushSubmodelRenderLibrary brush_library = {},
        std::vector<BrushSubmodelRenderInstance> brush_instances = {},
        const WorldSceneRenderPackageLimits& limits = {}) const;
};

} // namespace hlclient::world_scene_render
