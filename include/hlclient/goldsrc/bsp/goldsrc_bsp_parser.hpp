#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_collision_source.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_format.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_face_geometry_builder.hpp>
#include <hlclient/goldsrc/spatial/goldsrc_spatial_package_builder.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc::bsp {

inline constexpr std::size_t kGoldSrcBspMaximumDiagnosticContextBytes = 192U;

struct GoldSrcBspImportLimits {
    std::size_t maximum_source_bytes{kGoldSrcBspDefaultMaximumSourceBytes};
    std::size_t maximum_planes{kGoldSrcBspHardMaximumPlanes};
    std::size_t maximum_vertices{kGoldSrcBspHardMaximumVertices};
    std::size_t maximum_nodes{kGoldSrcBspHardMaximumNodes};
    std::size_t maximum_texinfo{kGoldSrcBspHardMaximumTexinfo};
    std::size_t maximum_faces{kGoldSrcBspHardMaximumFaces};
    std::size_t maximum_clipnodes{kGoldSrcBspHardMaximumClipnodes};
    std::size_t maximum_leaves{kGoldSrcBspHardMaximumLeaves};
    std::size_t maximum_marksurfaces{kGoldSrcBspHardMaximumMarksurfaces};
    std::size_t maximum_edges{kGoldSrcBspHardMaximumEdges};
    std::size_t maximum_surfedges{kGoldSrcBspHardMaximumSurfedges};
    std::size_t maximum_models{kGoldSrcBspHardMaximumModels};
    std::size_t maximum_textures{kGoldSrcBspHardMaximumTextures};
    std::size_t maximum_face_edges{4'096U};
    std::size_t maximum_polygon_edge_pair_tests{16'777'216U};
    std::size_t maximum_output_vertices{512'000U};
    std::size_t maximum_output_indices{1'536'000U};
    std::size_t maximum_output_surfaces{kGoldSrcBspHardMaximumOutputSurfaces};
    std::size_t maximum_output_materials{kGoldSrcBspHardMaximumOutputMaterials};
    std::size_t maximum_texture_name_bytes{kGoldSrcBspTextureNameWireSize};
    std::uint32_t maximum_texture_dimension{16'384U};
    std::uint64_t maximum_texture_texels{268'435'456ULL};
    std::size_t maximum_collision_validation_steps{
        kGoldSrcBspDefaultMaximumCollisionValidationSteps};
};

[[nodiscard]] bool valid_goldsrc_bsp_import_limits(
    const GoldSrcBspImportLimits& limits) noexcept;

struct GoldSrcBspParseOptions {
    // Historical M4.1-M4.3 importers retain only model 0. M4.4 opts in when a
    // brush render library is requested; all fixed records and references are
    // still decoded and validated by the same canonical parser.
    bool materialize_brush_submodels{true};
};

[[nodiscard]] assets::AssetSourceFingerprint goldsrc_bsp_source_fingerprint(
    std::span<const std::byte> source) noexcept;

enum class GoldSrcBspErrorCode {
    invalid_configuration,
    source_too_small,
    unsupported_version,
    invalid_lump_directory,
    negative_lump_range,
    lump_range_overflow,
    lump_out_of_bounds,
    lump_overlaps_header,
    lump_overlap,
    misaligned_fixed_lump_size,
    count_limit_exceeded,
    invalid_float,
    invalid_plane,
    invalid_texture_directory,
    invalid_texture_metadata,
    invalid_vertex_reference,
    invalid_edge_reference,
    invalid_surfedge_reference,
    invalid_texinfo_reference,
    invalid_face_reference,
    invalid_model_reference,
    invalid_node_reference,
    invalid_leaf_reference,
    invalid_marksurface_reference,
    invalid_clipnode_reference,
    node_cycle,
    clipnode_cycle,
    collision_validation_limit_exceeded,
    invalid_light_offset,
    broken_face_edge_loop,
    degenerate_face,
    nonplanar_face,
    invalid_face_winding,
    geometry_limit_exceeded,
    unable_to_retain_collision_source,
    unable_to_retain_world,
};

[[nodiscard]] std::string_view to_string(GoldSrcBspErrorCode code) noexcept;

struct GoldSrcBspError {
    GoldSrcBspErrorCode code{GoldSrcBspErrorCode::invalid_configuration};
    std::optional<GoldSrcBspLumpId> lump_id;
    std::size_t byte_offset{0U};
    std::optional<std::size_t> element_index;
    std::string context;
    // Present for every face-geometry materialization failure, including
    // model zero. Non-geometry structural failures remain untagged.
    std::optional<std::uint32_t> source_model_index;
    std::optional<GoldSrcFaceGeometryDiagnostic> face_geometry_diagnostic;
};

// Owning handoff of the one canonical BSP validation/decode pass.  Spatial
// package construction consumes these records and the public WorldAsset; it
// never reparses node/leaf/model wire layouts independently.
struct GoldSrcBspSpatialSource {
    std::vector<spatial::GoldSrcSpatialSourcePlane> planes;
    std::vector<spatial::GoldSrcSpatialSourceNode> nodes;
    std::vector<spatial::GoldSrcSpatialSourceLeaf> leaves;
    std::vector<std::uint32_t> marksurface_face_ordinals;
    // Exact validated ownership set for render faces in BSP models[1..N].
    // Spatial membership uses it to distinguish submodel-only faces from
    // malformed marksurface references to otherwise unowned face records.
    std::vector<std::uint32_t> submodel_face_ordinals;
    std::vector<std::byte> visibility_bytes;
    spatial::GoldSrcSpatialSourceModel world_model{};
    std::uint32_t source_face_count{0U};
};

// One renderer-neutral owning geometry asset per BSP render submodel. Source
// model metadata stays separate from entity instances: the parser performs no
// entity association and applies no transform policy.
struct GoldSrcBspBrushSubmodelAsset {
    std::uint32_t source_model_index{0U};
    assets::AssetVector3 source_model_origin{};
    assets::WorldBounds source_model_bounds{};
    std::int32_t render_headnode{0};
    assets::WorldAsset geometry;
};

// Sanitized aggregate-only geometry evidence. It intentionally retains no
// face-local arrays, source names, entity values, or filesystem information.
struct GoldSrcBspGeometryStatistics {
    std::uint64_t source_model_count{0U};
    std::uint64_t world_face_count{0U};
    std::uint64_t brush_face_count{0U};
    std::uint64_t side_zero_face_count{0U};
    std::uint64_t side_one_face_count{0U};
    std::uint64_t positive_surfedge_count{0U};
    std::uint64_t negative_surfedge_count{0U};
    std::uint64_t faces_with_negative_surfedges{0U};
    std::uint64_t faces_with_mixed_surfedge_signs{0U};
    std::uint64_t negative_wire_area_dot_count{0U};
    std::uint64_t positive_wire_area_dot_count{0U};
    std::uint64_t ambiguous_wire_area_dot_count{0U};
    std::uint64_t already_canonical_face_count{0U};
    std::uint64_t canonicalized_face_count{0U};
    std::uint64_t collinear_canonicalized_face_count{0U};
    std::uint64_t removed_collinear_corner_count{0U};
    std::uint64_t rejected_ambiguous_face_count{0U};
    std::uint64_t brush_canonicalized_face_count{0U};
    double minimum_accepted_winding_margin{0.0};
    double maximum_planarity_deviation{0.0};
};

struct GoldSrcBspParsedDocument {
    assets::WorldAsset world_asset;
    GoldSrcBspSpatialSource spatial_source;
    GoldSrcBspCollisionSource collision_source;
    std::vector<GoldSrcBspBrushSubmodelAsset> brush_submodels;
    std::vector<std::byte> entity_lump_bytes;
    // Fixed-record lumps report record counts. Variable byte lumps report byte
    // counts, while the texture lump reports its directory entry count.
    std::array<std::size_t, kGoldSrcBspLumpCount> lump_element_counts{};
    GoldSrcBspGeometryStatistics geometry_statistics{};
};

struct GoldSrcBspParseResult {
    std::optional<GoldSrcBspParsedDocument> document;
    std::optional<GoldSrcBspError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return document.has_value();
    }
};

class GoldSrcBspParser final {
public:
    [[nodiscard]] static GoldSrcBspParseResult parse(
        std::span<const std::byte> source,
        const GoldSrcBspImportLimits& limits = {},
        const GoldSrcBspParseOptions& options = {});
};

} // namespace hlclient::goldsrc::bsp
