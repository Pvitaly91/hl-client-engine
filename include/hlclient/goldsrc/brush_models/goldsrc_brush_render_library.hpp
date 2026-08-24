#pragma once

#include <hlclient/goldsrc/brush_models/goldsrc_brush_transform.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/lightmaps/goldsrc_world_lightmap_import.hpp>
#include <hlclient/goldsrc/world_textures/world_texture_import.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/world_render/world_render_package_builder.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace hlclient::goldsrc::brush_models {

inline constexpr std::size_t kDefaultMaximumBrushRenderModels = 400U;
inline constexpr std::size_t kHardMaximumBrushRenderModels = 400U;
inline constexpr std::size_t kDefaultMaximumBrushRenderVertices = 1'048'576U;
inline constexpr std::size_t kHardMaximumBrushRenderVertices = 4'194'304U;
inline constexpr std::size_t kDefaultMaximumBrushRenderIndices = 3'145'728U;
inline constexpr std::size_t kHardMaximumBrushRenderIndices = 12'582'912U;
inline constexpr std::size_t kDefaultMaximumBrushRenderSurfaces = 65'535U;
inline constexpr std::size_t kHardMaximumBrushRenderSurfaces = 65'535U;
inline constexpr std::size_t kDefaultMaximumBrushRenderMaterials = 8'192U;
inline constexpr std::size_t kHardMaximumBrushRenderMaterials = 8'192U;
inline constexpr std::size_t kDefaultMaximumBrushTextureImportUpdates = 65'536U;
inline constexpr std::size_t kHardMaximumBrushTextureImportUpdates = 1'048'576U;

enum class GoldSrcBrushRenderCompatibilityProfile {
    aggregate_static_opaque_world_render_package_v1,
};

enum class GoldSrcBrushRenderEvidenceProfile {
    canonical_bsp_geometry_shared_texture_and_lightmap_codecs,
};

struct GoldSrcBrushRenderLibraryLimits {
    std::size_t maximum_models{kDefaultMaximumBrushRenderModels};
    std::size_t maximum_vertices{kDefaultMaximumBrushRenderVertices};
    std::size_t maximum_indices{kDefaultMaximumBrushRenderIndices};
    std::size_t maximum_surfaces{kDefaultMaximumBrushRenderSurfaces};
    std::size_t maximum_materials{kDefaultMaximumBrushRenderMaterials};
    std::size_t maximum_texture_import_updates{
        kDefaultMaximumBrushTextureImportUpdates};
    GoldSrcWorldTextureImportLimits textures{};
    lightmaps::GoldSrcWorldLightmapImportLimits lightmaps{};
    world_render::WorldRenderPackageLimits render_package{};
};

[[nodiscard]] bool valid_goldsrc_brush_render_library_limits(
    const GoldSrcBrushRenderLibraryLimits& limits) noexcept;

struct GoldSrcBrushRenderLibraryStatistics {
    std::uint64_t source_model_count{0U};
    std::uint64_t aggregate_vertex_count{0U};
    std::uint64_t aggregate_index_count{0U};
    std::uint64_t aggregate_surface_count{0U};
    std::uint64_t input_material_reference_count{0U};
    std::uint64_t unique_material_reference_count{0U};
    std::uint64_t deduplicated_material_reference_count{0U};
    std::uint64_t decoded_texture_count{0U};
    std::uint64_t lightmap_atlas_page_count{0U};
    std::uint64_t texture_import_update_count{0U};
};

enum class GoldSrcBrushRenderLibraryErrorCode {
    invalid_configuration,
    source_document_mismatch,
    invalid_model_order,
    invalid_model_geometry,
    aggregate_limit_exceeded,
    texture_import_begin_failed,
    texture_import_update_limit_exceeded,
    textures_incomplete,
    texture_import_failed,
    lightmap_import_failed,
    render_package_failed,
    surface_range_mismatch,
    unable_to_retain_library,
};

[[nodiscard]] std::string_view to_string(
    GoldSrcBrushRenderLibraryErrorCode code) noexcept;

// Deterministic first unresolved aggregate-material binding. When the binding
// identifies a WAD declaration, or the binding reports that a declared archive
// is missing, the matching archive status is retained as well.
struct GoldSrcBrushTextureIncompleteReason {
    std::size_t material_index{0U};
    assets::WorldMaterialTextureBindingStatus binding_status{
        assets::WorldMaterialTextureBindingStatus::
            missing_bsp_texture_reference};
    std::optional<std::uint32_t> source_bsp_texture_index;
    std::optional<std::uint32_t> source_archive_ordinal;
    std::optional<assets::WorldTextureArchiveStatus> archive_status;
};

struct GoldSrcBrushRenderLibraryError {
    GoldSrcBrushRenderLibraryErrorCode code{
        GoldSrcBrushRenderLibraryErrorCode::invalid_configuration};
    std::optional<std::size_t> source_model_table_index;
    std::optional<std::uint32_t> source_model_index;
    std::optional<WorldTextureImportErrorCode> texture_error;
    std::optional<lightmaps::GoldSrcWorldLightmapImportErrorCode> lightmap_error;
    std::optional<world_render::WorldRenderPackageErrorCode> render_package_error;
    std::optional<GoldSrcBrushTextureIncompleteReason>
        texture_incomplete_reason;
};

struct GoldSrcBrushRenderLibraryBuildResult {
    std::optional<world_scene_render::BrushSubmodelRenderLibrary> library;
    std::optional<GoldSrcBrushRenderLibraryError> error;
    GoldSrcBrushRenderLibraryStatistics statistics{};
    BrushSubmodelCoordinateProfile coordinate_profile{
        BrushSubmodelCoordinateProfile::qcsg_entity_origin_relative_v1};
    GoldSrcBrushRenderCompatibilityProfile compatibility_profile{
        GoldSrcBrushRenderCompatibilityProfile::
            aggregate_static_opaque_world_render_package_v1};
    GoldSrcBrushRenderEvidenceProfile evidence_profile{
        GoldSrcBrushRenderEvidenceProfile::
            canonical_bsp_geometry_shared_texture_and_lightmap_codecs};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return library.has_value();
    }
};

class GoldSrcBrushRenderLibraryBuilder final {
public:
    // Drives the existing incremental texture operation using a deterministic
    // synthetic clock. It performs no sleeps, network work, or renderer work.
    [[nodiscard]] static GoldSrcBrushRenderLibraryBuildResult build(
        const bsp::GoldSrcBspParsedDocument& document,
        std::span<const std::byte> retained_bsp_source,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        const GoldSrcBrushRenderLibraryLimits& limits = {});
};

} // namespace hlclient::goldsrc::brush_models
