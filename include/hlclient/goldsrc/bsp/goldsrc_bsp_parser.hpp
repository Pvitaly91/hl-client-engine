#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_format.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

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
};

[[nodiscard]] bool valid_goldsrc_bsp_import_limits(
    const GoldSrcBspImportLimits& limits) noexcept;

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
    invalid_light_offset,
    broken_face_edge_loop,
    degenerate_face,
    nonplanar_face,
    invalid_face_winding,
    geometry_limit_exceeded,
    unable_to_retain_world,
};

[[nodiscard]] std::string_view to_string(GoldSrcBspErrorCode code) noexcept;

struct GoldSrcBspError {
    GoldSrcBspErrorCode code{GoldSrcBspErrorCode::invalid_configuration};
    std::optional<GoldSrcBspLumpId> lump_id;
    std::size_t byte_offset{0U};
    std::optional<std::size_t> element_index;
    std::string context;
};

struct GoldSrcBspParsedDocument {
    assets::WorldAsset world_asset;
    // Fixed-record lumps report record counts. Variable byte lumps report byte
    // counts, while the texture lump reports its directory entry count.
    std::array<std::size_t, kGoldSrcBspLumpCount> lump_element_counts{};
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
        const GoldSrcBspImportLimits& limits = {});
};

} // namespace hlclient::goldsrc::bsp
