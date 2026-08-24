#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace hlclient::goldsrc::bsp {

inline constexpr std::int32_t kGoldSrcBspVersion = 30;
inline constexpr std::size_t kGoldSrcBspLumpCount = 15U;
inline constexpr std::size_t kGoldSrcBspLumpDescriptorWireSize = 8U;
inline constexpr std::size_t kGoldSrcBspHeaderWireSize =
    4U + kGoldSrcBspLumpCount * kGoldSrcBspLumpDescriptorWireSize;

enum class GoldSrcBspLumpId : std::uint8_t {
    entities = 0U,
    planes = 1U,
    textures = 2U,
    vertices = 3U,
    visibility = 4U,
    nodes = 5U,
    texinfo = 6U,
    faces = 7U,
    lighting = 8U,
    clipnodes = 9U,
    leaves = 10U,
    marksurfaces = 11U,
    edges = 12U,
    surfedges = 13U,
    models = 14U,
};

inline constexpr std::array<GoldSrcBspLumpId, kGoldSrcBspLumpCount>
    kGoldSrcBspLumpIds{
        GoldSrcBspLumpId::entities,
        GoldSrcBspLumpId::planes,
        GoldSrcBspLumpId::textures,
        GoldSrcBspLumpId::vertices,
        GoldSrcBspLumpId::visibility,
        GoldSrcBspLumpId::nodes,
        GoldSrcBspLumpId::texinfo,
        GoldSrcBspLumpId::faces,
        GoldSrcBspLumpId::lighting,
        GoldSrcBspLumpId::clipnodes,
        GoldSrcBspLumpId::leaves,
        GoldSrcBspLumpId::marksurfaces,
        GoldSrcBspLumpId::edges,
        GoldSrcBspLumpId::surfedges,
        GoldSrcBspLumpId::models,
    };

[[nodiscard]] constexpr std::size_t goldsrc_bsp_lump_index(
    const GoldSrcBspLumpId id) noexcept
{
    return static_cast<std::size_t>(id);
}

[[nodiscard]] constexpr std::string_view to_string(
    const GoldSrcBspLumpId id) noexcept
{
    switch (id) {
    case GoldSrcBspLumpId::entities: return "entities";
    case GoldSrcBspLumpId::planes: return "planes";
    case GoldSrcBspLumpId::textures: return "textures";
    case GoldSrcBspLumpId::vertices: return "vertices";
    case GoldSrcBspLumpId::visibility: return "visibility";
    case GoldSrcBspLumpId::nodes: return "nodes";
    case GoldSrcBspLumpId::texinfo: return "texinfo";
    case GoldSrcBspLumpId::faces: return "faces";
    case GoldSrcBspLumpId::lighting: return "lighting";
    case GoldSrcBspLumpId::clipnodes: return "clipnodes";
    case GoldSrcBspLumpId::leaves: return "leaves";
    case GoldSrcBspLumpId::marksurfaces: return "marksurfaces";
    case GoldSrcBspLumpId::edges: return "edges";
    case GoldSrcBspLumpId::surfedges: return "surfedges";
    case GoldSrcBspLumpId::models: return "models";
    }
    return "unknown";
}

inline constexpr std::size_t kGoldSrcBspPlaneWireSize = 20U;
inline constexpr std::size_t kGoldSrcBspVertexWireSize = 12U;
inline constexpr std::size_t kGoldSrcBspNodeWireSize = 24U;
inline constexpr std::size_t kGoldSrcBspTexinfoWireSize = 40U;
inline constexpr std::size_t kGoldSrcBspFaceWireSize = 20U;
inline constexpr std::size_t kGoldSrcBspClipnodeWireSize = 8U;
inline constexpr std::size_t kGoldSrcBspLeafWireSize = 28U;
inline constexpr std::size_t kGoldSrcBspMarksurfaceWireSize = 2U;
inline constexpr std::size_t kGoldSrcBspEdgeWireSize = 4U;
inline constexpr std::size_t kGoldSrcBspSurfedgeWireSize = 4U;
inline constexpr std::size_t kGoldSrcBspModelWireSize = 64U;
inline constexpr std::size_t kGoldSrcBspMiptexHeaderWireSize = 40U;
inline constexpr std::size_t kGoldSrcBspTextureNameWireSize = 16U;
inline constexpr std::uint32_t kGoldSrcBspTextureDimensionGranularity = 16U;

inline constexpr std::uint32_t kGoldSrcBspTexSpecialFlag = 1U;
inline constexpr std::int32_t kGoldSrcBspMinimumContentsValue = -15;
inline constexpr std::int32_t kGoldSrcBspMaximumContentsValue = -1;

// These are the clean-room supported-profile ceilings. The record-count
// values cross-check the public Valve GoldSrc BSP v30 design limits; they are
// parser compatibility limits, not native SDK types or ABI declarations.
inline constexpr std::size_t kGoldSrcBspDefaultMaximumSourceBytes =
    32U * 1024U * 1024U;
inline constexpr std::size_t kGoldSrcBspHardMaximumSourceBytes = 64U * 1024U * 1024U;
inline constexpr std::size_t kGoldSrcBspHardMaximumModels = 400U;
inline constexpr std::size_t kGoldSrcBspHardMaximumPlanes = 32'767U;
inline constexpr std::size_t kGoldSrcBspHardMaximumNodes = 32'767U;
inline constexpr std::size_t kGoldSrcBspHardMaximumClipnodes = 32'767U;
inline constexpr std::size_t kGoldSrcBspHardMaximumLeaves = 8'192U;
inline constexpr std::size_t kGoldSrcBspHardMaximumVertices = 65'535U;
inline constexpr std::size_t kGoldSrcBspHardMaximumFaces = 65'535U;
inline constexpr std::size_t kGoldSrcBspHardMaximumMarksurfaces = 65'535U;
inline constexpr std::size_t kGoldSrcBspHardMaximumTexinfo = 8'192U;
inline constexpr std::size_t kGoldSrcBspHardMaximumEdges = 256'000U;
inline constexpr std::size_t kGoldSrcBspHardMaximumSurfedges = 512'000U;
inline constexpr std::size_t kGoldSrcBspHardMaximumTextures = 512U;
inline constexpr std::size_t kGoldSrcBspHardMaximumFaceEdges = 32'767U;
inline constexpr std::size_t kGoldSrcBspHardMaximumPolygonEdgePairTests =
    536'788'994U;
inline constexpr std::size_t kGoldSrcBspHardMaximumOutputVertices = 1'048'576U;
inline constexpr std::size_t kGoldSrcBspHardMaximumOutputIndices = 3'145'728U;
inline constexpr std::size_t kGoldSrcBspHardMaximumOutputSurfaces = 65'535U;
inline constexpr std::size_t kGoldSrcBspHardMaximumOutputMaterials = 8'192U;
inline constexpr std::uint32_t kGoldSrcBspHardMaximumTextureDimension = 65'535U;
inline constexpr std::uint64_t kGoldSrcBspHardMaximumTextureTexels =
    static_cast<std::uint64_t>(kGoldSrcBspHardMaximumTextureDimension) *
    kGoldSrcBspHardMaximumTextureDimension;

inline constexpr float kGoldSrcBspPlaneUnitTolerance = 0.01F;
inline constexpr float kGoldSrcBspPlanarityTolerance = 0.02F;
inline constexpr double kGoldSrcBspGeometryEpsilon = 1.0e-6;

} // namespace hlclient::goldsrc::bsp
