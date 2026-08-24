#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/goldsrc/spatial/goldsrc_pvs_decoder.hpp>
#include <hlclient/world_spatial/world_spatial_types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace hlclient::goldsrc::spatial {

inline constexpr std::size_t kGoldSrcSpatialHardMaximumPlanes = 32'767U;
inline constexpr std::size_t kGoldSrcSpatialHardMaximumNodes = 32'767U;
inline constexpr std::size_t kGoldSrcSpatialHardMaximumLeaves = 8'192U;
inline constexpr std::size_t kGoldSrcSpatialHardMaximumMarksurfaceLinks = 262'144U;
inline constexpr std::size_t kGoldSrcSpatialHardMaximumUniquePvsRows = 8'192U;
inline constexpr std::size_t kGoldSrcSpatialHardMaximumWorldSurfaces = 65'535U;
inline constexpr std::size_t kGoldSrcSpatialHardMaximumSourceFaces = 65'535U;

struct GoldSrcSpatialImportLimits {
    std::size_t maximum_planes{kGoldSrcSpatialHardMaximumPlanes};
    std::size_t maximum_nodes{kGoldSrcSpatialHardMaximumNodes};
    std::size_t maximum_leaves{kGoldSrcSpatialHardMaximumLeaves};
    std::size_t maximum_marksurface_links{
        kGoldSrcSpatialHardMaximumMarksurfaceLinks};
    std::size_t maximum_unique_pvs_rows{
        kGoldSrcSpatialHardMaximumUniquePvsRows};
    std::size_t maximum_pvs_row_bytes{kGoldSrcPvsDefaultMaximumRowBytes};
    std::size_t maximum_decompressed_pvs_bytes{
        kGoldSrcPvsDefaultMaximumDecompressedBytes};
    std::size_t maximum_world_surfaces{
        kGoldSrcSpatialHardMaximumWorldSurfaces};
    std::size_t maximum_source_faces{kGoldSrcSpatialHardMaximumSourceFaces};
};

[[nodiscard]] bool valid_goldsrc_spatial_import_limits(
    const GoldSrcSpatialImportLimits& limits) noexcept;

// These records are the already-decoded, canonical BSP parser handoff. They
// deliberately describe source semantics but contain no wire byte offsets or
// ABI-dependent SDK structures. GoldSrcSpatialPackageBuilder never parses a
// BSP header or fixed-record lump.
struct GoldSrcSpatialSourcePlane {
    assets::AssetVector3 normal{};
    double distance{0.0};
    std::int32_t source_type{0};
};

struct GoldSrcSpatialSourceNode {
    std::int32_t plane_index{0};
    std::array<std::int32_t, 2U> encoded_children{};
    assets::WorldBounds bounds{};
    std::optional<std::uint32_t> first_source_face;
    std::optional<std::uint32_t> source_face_count;
};

struct GoldSrcSpatialSourceLeaf {
    std::int32_t contents{-1};
    std::int32_t visibility_offset{-1};
    assets::WorldBounds bounds{};
    std::uint32_t first_marksurface{0U};
    std::uint32_t marksurface_count{0U};
};

struct GoldSrcSpatialSourceModel {
    assets::WorldBounds bounds{};
    std::int32_t render_headnode{0};
    std::int32_t visible_leaf_count{0};
};

struct GoldSrcSpatialBuildInput {
    std::span<const GoldSrcSpatialSourcePlane> planes;
    std::span<const GoldSrcSpatialSourceNode> nodes;
    std::span<const GoldSrcSpatialSourceLeaf> leaves;
    std::span<const std::uint32_t> marksurface_face_ordinals;
    std::span<const std::byte> visibility_bytes;
    GoldSrcSpatialSourceModel world_model{};
    std::uint32_t source_face_count{0U};
    std::span<const assets::WorldSurface> world_surfaces;
    // Canonical source-face ordinals owned by BSP models[1..N]. A face that
    // is absent from both this set and the model-0 WorldSurface table is
    // unowned and cannot be silently treated as a submodel face.
    std::span<const std::uint32_t> submodel_face_ordinals;
};

enum class GoldSrcSpatialImportErrorCode {
    invalid_configuration,
    count_limit_exceeded,
    invalid_plane,
    invalid_node,
    invalid_leaf,
    invalid_world_model,
    node_cycle,
    invalid_marksurface_reference,
    invalid_face_ownership,
    invalid_world_surface,
    ambiguous_world_surface_mapping,
    pvs_decode_failed,
    pvs_table_limit_exceeded,
    unable_to_retain_package,
};

[[nodiscard]] std::string_view to_string(
    GoldSrcSpatialImportErrorCode code) noexcept;

struct GoldSrcSpatialImportError {
    GoldSrcSpatialImportErrorCode code{
        GoldSrcSpatialImportErrorCode::invalid_configuration};
    std::optional<std::size_t> source_element_index;
    std::optional<GoldSrcPvsDecodeError> pvs_error;
};

struct GoldSrcSpatialBuildResult {
    std::optional<world_spatial::WorldSpatialPackage> package;
    std::optional<GoldSrcSpatialImportError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return package.has_value();
    }
};

class GoldSrcSpatialPackageBuilder final {
public:
    [[nodiscard]] static GoldSrcSpatialBuildResult build(
        const GoldSrcSpatialBuildInput& input,
        const GoldSrcSpatialImportLimits& limits = {});
};

} // namespace hlclient::goldsrc::spatial
