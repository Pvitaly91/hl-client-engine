#pragma once

#include <hlclient/goldsrc/brush_models/brush_submodel_instances.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_spawn_camera.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/spatial/goldsrc_spatial_package_builder.hpp>
#include <hlclient/world_render/world_render_types.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace hlclient::goldsrc::brush_models {

enum class GoldSrcWorldSceneBrushMode {
    off,
    static_initial,
};

[[nodiscard]] std::string_view to_string(
    GoldSrcWorldSceneBrushMode mode) noexcept;

struct GoldSrcWorldSceneBuildConfig {
    GoldSrcWorldSceneBrushMode brushes{GoldSrcWorldSceneBrushMode::off};
    bool extract_spawn{false};
};

[[nodiscard]] bool valid_goldsrc_world_scene_build_config(
    const GoldSrcWorldSceneBuildConfig& config) noexcept;

struct GoldSrcWorldSceneBuildLimits {
    spatial::GoldSrcSpatialImportLimits spatial{};
    bsp::GoldSrcEntityDocumentLimits entities{};
    BrushSubmodelInstanceBuildLimits instances{};
    world_scene_render::WorldSceneRenderPackageLimits scene{};
};

[[nodiscard]] bool valid_goldsrc_world_scene_build_limits(
    const GoldSrcWorldSceneBuildLimits& limits) noexcept;

enum class GoldSrcWorldSceneCompatibilityProfile {
    canonical_world_spatial_optional_static_initial_brushes_v1,
};

enum class GoldSrcWorldSceneEvidenceProfile {
    validated_bsp_inert_entities_and_renderer_neutral_scene_v1,
};

struct GoldSrcWorldSceneBuildStatistics {
    std::uint64_t source_world_surface_count{0U};
    std::uint64_t source_brush_model_count{0U};
    std::uint64_t spatial_node_count{0U};
    std::uint64_t spatial_leaf_count{0U};
    // Zero when neither static brushes nor spawn extraction requested parsing;
    // exactly one otherwise.
    std::uint64_t entity_document_parse_count{0U};
    std::uint64_t parsed_entity_count{0U};
    std::uint64_t parsed_entity_pair_count{0U};
    std::uint64_t retained_brush_render_model_count{0U};
    std::uint64_t brush_instance_count{0U};
    std::uint64_t supported_brush_instance_count{0U};
    std::uint64_t unsupported_brush_instance_count{0U};
    std::uint64_t brush_instance_leaf_link_count{0U};
};

enum class GoldSrcWorldSceneBuildErrorCode {
    invalid_configuration,
    invalid_world_package,
    world_package_document_mismatch,
    spatial_package_build_failed,
    entity_document_parse_failed,
    invalid_brush_model_table,
    brush_library_document_mismatch,
    brush_instance_set_build_failed,
    invalid_brush_instance_conversion,
    scene_package_build_failed,
    unable_to_retain_scene,
};

[[nodiscard]] std::string_view to_string(
    GoldSrcWorldSceneBuildErrorCode code) noexcept;

struct GoldSrcWorldSceneBuildError {
    GoldSrcWorldSceneBuildErrorCode code{
        GoldSrcWorldSceneBuildErrorCode::invalid_configuration};
    std::optional<spatial::GoldSrcSpatialImportError> spatial_error;
    std::optional<bsp::GoldSrcEntityDocumentError> entity_error;
    std::optional<BrushSubmodelInstanceBuildError> instance_error;
    std::optional<world_scene_render::WorldSceneRenderError> scene_error;
    std::optional<std::size_t> source_instance_index;
};

struct GoldSrcWorldSceneBuildResult {
    std::optional<world_scene_render::WorldSceneRenderPackage> scene_package;
    // Disengaged means extraction was not requested. An engaged result retains
    // the extractor's selected/no-candidate status without retaining entities.
    std::optional<GoldSrcSpawnCameraExtractionResult> spawn_camera;
    std::optional<GoldSrcWorldSceneBuildError> error;
    GoldSrcWorldSceneBuildStatistics statistics{};
    GoldSrcWorldSceneCompatibilityProfile compatibility_profile{
        GoldSrcWorldSceneCompatibilityProfile::
            canonical_world_spatial_optional_static_initial_brushes_v1};
    GoldSrcWorldSceneEvidenceProfile evidence_profile{
        GoldSrcWorldSceneEvidenceProfile::
            validated_bsp_inert_entities_and_renderer_neutral_scene_v1};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return scene_package.has_value();
    }
};

[[nodiscard]] world_scene_render::BrushSubmodelRenderSupportStatus
to_world_scene_render_support_status(
    BrushSubmodelInstanceStatus status) noexcept;

class GoldSrcWorldSceneBuilder final {
public:
    // Pure CPU transactional composition. The entity document is parsed at
    // most once and remains local to this call. Neither it nor BSP source bytes
    // are retained by the returned renderer-neutral scene package.
    // Unsupported instances whose typed source result has no transform/bounds
    // use neutral identity/zero payloads; their support status remains
    // authoritative and prevents rendering. Touched-leaf sets are sorted for
    // the neutral scene contract while preserving their exact membership.
    [[nodiscard]] static GoldSrcWorldSceneBuildResult build(
        const bsp::GoldSrcBspParsedDocument& document,
        std::shared_ptr<const world_render::WorldRenderPackage> world_package,
        std::optional<world_scene_render::BrushSubmodelRenderLibrary>
            brush_library = std::nullopt,
        const GoldSrcWorldSceneBuildConfig& config = {},
        const GoldSrcWorldSceneBuildLimits& limits = {});
};

} // namespace hlclient::goldsrc::brush_models
