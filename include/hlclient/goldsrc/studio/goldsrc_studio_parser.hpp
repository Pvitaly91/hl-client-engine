#pragma once

#include <hlclient/assets/model_asset_types.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_format.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc::studio {

inline constexpr std::size_t kGoldSrcStudioMaximumDiagnosticContextBytes = 192U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumMainSourceBytes =
    16U * 1024U * 1024U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumCompanionSourceBytes =
    16U * 1024U * 1024U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumTotalBundleBytes =
    32U * 1024U * 1024U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumBones = 128U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumBoneControllers = 8U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumHitboxes = 512U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumAttachments = 512U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumBodyParts = 32U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumModelsPerBodyPart = 32U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumSubmodels = 1'024U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumTextures = 100U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumSkinFamilies = 256U;
inline constexpr std::uint32_t kGoldSrcStudioHardMaximumTextureDimension = 4'096U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumTotalRgbaBytes =
    64U * 1024U * 1024U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumSequenceGroups = 16U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumSequences = 2'048U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumEvents = 1'024U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumPivots = 256U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumAnimationBlends = 2'048U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumAnimationTracks =
    kGoldSrcStudioHardMaximumAnimationBlends * kGoldSrcStudioHardMaximumBones;
inline constexpr std::size_t kGoldSrcStudioHardMaximumAnimationRuns = 1'048'576U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumAnimationValueBytes =
    16U * 1024U * 1024U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumMeshes = 256U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumVerticesPerSubmodel = 2'048U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumTrianglesPerSubmodel = 20'000U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumTotalTriangles = 262'144U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumTriangleCommands = 1'048'576U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumOutputVertices = 1'048'576U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumOutputIndices = 3'145'728U;
inline constexpr std::size_t kGoldSrcStudioHardMaximumStringBytes = 64U;

struct GoldSrcStudioModelImportLimits {
    std::size_t maximum_main_source_bytes{kGoldSrcStudioHardMaximumMainSourceBytes};
    std::size_t maximum_companion_source_bytes{
        kGoldSrcStudioHardMaximumCompanionSourceBytes};
    std::size_t maximum_total_bundle_bytes{kGoldSrcStudioHardMaximumTotalBundleBytes};
    std::size_t maximum_bones{kGoldSrcStudioHardMaximumBones};
    std::size_t maximum_bone_controllers{kGoldSrcStudioHardMaximumBoneControllers};
    std::size_t maximum_hitboxes{kGoldSrcStudioHardMaximumHitboxes};
    std::size_t maximum_attachments{kGoldSrcStudioHardMaximumAttachments};
    std::size_t maximum_sequences{kGoldSrcStudioHardMaximumSequences};
    std::size_t maximum_sequence_groups{kGoldSrcStudioHardMaximumSequenceGroups};
    std::size_t maximum_events{kGoldSrcStudioHardMaximumEvents};
    std::size_t maximum_pivots{kGoldSrcStudioHardMaximumPivots};
    std::size_t maximum_animation_blends{
        kGoldSrcStudioHardMaximumAnimationBlends};
    std::size_t maximum_animation_tracks{
        kGoldSrcStudioHardMaximumAnimationTracks};
    std::size_t maximum_animation_runs{kGoldSrcStudioHardMaximumAnimationRuns};
    std::size_t maximum_animation_value_bytes{
        kGoldSrcStudioHardMaximumAnimationValueBytes};
    std::size_t maximum_bodyparts{kGoldSrcStudioHardMaximumBodyParts};
    std::size_t maximum_models_per_bodypart{
        kGoldSrcStudioHardMaximumModelsPerBodyPart};
    std::size_t maximum_submodels{kGoldSrcStudioHardMaximumSubmodels};
    std::size_t maximum_meshes{kGoldSrcStudioHardMaximumMeshes};
    std::size_t maximum_vertices_per_submodel{
        kGoldSrcStudioHardMaximumVerticesPerSubmodel};
    std::size_t maximum_normals_per_submodel{
        kGoldSrcStudioHardMaximumVerticesPerSubmodel};
    std::size_t maximum_triangles_per_submodel{
        kGoldSrcStudioHardMaximumTrianglesPerSubmodel};
    std::size_t maximum_total_triangles{kGoldSrcStudioHardMaximumTotalTriangles};
    std::size_t maximum_triangle_commands{kGoldSrcStudioHardMaximumTriangleCommands};
    std::size_t maximum_output_vertices{kGoldSrcStudioHardMaximumOutputVertices};
    std::size_t maximum_output_indices{kGoldSrcStudioHardMaximumOutputIndices};
    std::size_t maximum_textures{kGoldSrcStudioHardMaximumTextures};
    std::size_t maximum_skin_references{kGoldSrcStudioHardMaximumTextures};
    std::size_t maximum_skin_families{kGoldSrcStudioHardMaximumSkinFamilies};
    std::uint32_t maximum_texture_dimension{
        kGoldSrcStudioHardMaximumTextureDimension};
    std::size_t maximum_total_rgba_bytes{kGoldSrcStudioHardMaximumTotalRgbaBytes};
    std::size_t maximum_string_bytes{kGoldSrcStudioHardMaximumStringBytes};
};

[[nodiscard]] bool valid_goldsrc_studio_model_import_limits(
    const GoldSrcStudioModelImportLimits& limits) noexcept;

enum class GoldSrcStudioErrorCode {
    invalid_configuration,
    source_too_small,
    source_limit_exceeded,
    unsupported_identifier,
    unsupported_version,
    invalid_declared_length,
    unexplained_trailing_data,
    negative_count_or_offset,
    count_limit_exceeded,
    range_overflow,
    range_out_of_bounds,
    range_overlaps_header,
    range_overlap,
    invalid_string,
    invalid_float,
    invalid_bounds,
    invalid_reference,
    invalid_skeleton,
    invalid_controller,
    invalid_hitbox,
    invalid_attachment,
    invalid_bodypart,
    invalid_submodel,
    invalid_mesh,
    invalid_geometry,
    invalid_texture,
    invalid_skin_table,
    invalid_sequence,
    invalid_animation,
    unsupported_sound_group,
    invalid_transition_table,
    external_dependency_required,
    missing_texture_companion,
    invalid_texture_companion,
    missing_sequence_group,
    invalid_sequence_group,
    duplicate_sequence_group,
    total_bundle_limit_exceeded,
    unable_to_retain_model,
};

[[nodiscard]] std::string_view to_string(GoldSrcStudioErrorCode code) noexcept;

struct GoldSrcStudioError {
    GoldSrcStudioErrorCode code{GoldSrcStudioErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::optional<std::size_t> element_index;
    std::optional<std::uint32_t> source_group_ordinal;
    std::string context;
};

struct GoldSrcStudioModelDependencyPlan {
    bool texture_companion_required{false};
    std::vector<std::uint32_t> required_sequence_group_ordinals;
    std::size_t expected_source_count{1U};
};

struct GoldSrcStudioDependencyPlanResult {
    std::optional<GoldSrcStudioModelDependencyPlan> plan;
    std::optional<GoldSrcStudioError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return plan.has_value();
    }
};

struct GoldSrcStudioSequenceGroupSourceView {
    std::uint32_t ordinal{0U};
    std::span<const std::byte> bytes;
};

struct GoldSrcStudioSourceBundleView {
    std::span<const std::byte> main_source;
    std::optional<std::span<const std::byte>> texture_source;
    std::span<const GoldSrcStudioSequenceGroupSourceView> sequence_groups;
};

struct GoldSrcStudioParsedDocument {
    GoldSrcStudioHeader main_header;
    GoldSrcStudioModelDependencyPlan dependency_plan;
    assets::SkeletalModelAssetData skeletal_model;
};

struct GoldSrcStudioParseResult {
    std::optional<GoldSrcStudioParsedDocument> document;
    std::optional<GoldSrcStudioError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return document.has_value();
    }
};

class GoldSrcStudioParser final {
public:
    [[nodiscard]] static GoldSrcStudioDependencyPlanResult inspect_dependencies(
        std::span<const std::byte> main_source,
        const GoldSrcStudioModelImportLimits& limits = {});

    [[nodiscard]] static GoldSrcStudioParseResult parse(
        const GoldSrcStudioSourceBundleView& sources,
        const GoldSrcStudioModelImportLimits& limits = {});
};

using GoldSrcStudioModelParser = GoldSrcStudioParser;

} // namespace hlclient::goldsrc::studio
