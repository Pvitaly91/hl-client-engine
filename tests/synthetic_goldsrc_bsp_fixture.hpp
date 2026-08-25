#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace hlclient::tests {

enum class SyntheticBspLumpId : std::size_t {
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

inline constexpr std::size_t kSyntheticBspLumpCount = 15U;
inline constexpr std::size_t kSyntheticBspHeaderSize = 124U;

// Independent, literal GoldSrc BSP v30 fixture. It contains a single 64x64
// Valve-clockwise wire quad on the Z=0 plane, one external texture reference,
// one world model, and the minimal node/leaf/marksurface structure used by the
// supported profile. Canonical import output is counter-clockwise. This byte
// string is deliberately not produced by the test-only builder below.
inline constexpr char kLiteralMinimalGoldSrcBspV30[] =
    "\x1e\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x7c\x00\x00\x00"
    "\x14\x00\x00\x00\x90\x00\x00\x00\x30\x00\x00\x00\xc0\x00\x00\x00"
    "\x30\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xf0\x00\x00\x00"
    "\x18\x00\x00\x00\x08\x01\x00\x00\x28\x00\x00\x00\x30\x01\x00\x00"
    "\x14\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x44\x01\x00\x00\x38\x00\x00\x00\x7c\x01\x00\x00"
    "\x02\x00\x00\x00\x7e\x01\x00\x00\x14\x00\x00\x00\x92\x01\x00\x00"
    "\x10\x00\x00\x00\xa2\x01\x00\x00\x40\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x80\x3f\x00\x00\x00\x00\x02\x00\x00\x00"
    "\x01\x00\x00\x00\x08\x00\x00\x00\x54\x45\x53\x54\x5f\x51\x55\x41"
    "\x44\x00\x00\x00\x00\x00\x00\x00\x40\x00\x00\x00\x40\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x42"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x42\x00\x00\x80\x42"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x42\x00\x00\x00\x00"
    "\x00\x00\x00\x00\xff\xff\xfe\xff\xff\xff\xff\xff\xff\xff\x41\x00"
    "\x41\x00\x01\x00\x00\x00\x01\x00\x00\x00\x80\x3f\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3f"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x04\x00\x00\x00\xff\xff\xff\xff"
    "\xff\xff\xff\xff\xfe\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
    "\xff\xff\x41\x00\x41\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\x41\x00"
    "\x41\x00\x01\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x01\x00\x01\x00\x02\x00\x02\x00\x03\x00\x03\x00"
    "\x00\x00\xfc\xff\xff\xff\xfd\xff\xff\xff\xfe\xff\xff\xff\xff\xff"
    "\xff\xff\x00\x00\x80\xbf\x00\x00\x80\xbf\x00\x00\x80\xbf\x00\x00"
    "\x82\x42\x00\x00\x82\x42\x00\x00\x80\x3f\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff"
    "\xff\xff\xff\xff\xff\xff\x01\x00\x00\x00\x00\x00\x00\x00\x01\x00"
    "\x00\x00";

static_assert(sizeof(kLiteralMinimalGoldSrcBspV30) - 1U == 482U);

[[nodiscard]] inline std::vector<std::byte> literal_minimal_goldsrc_bsp_v30()
{
    std::vector<std::byte> bytes;
    bytes.reserve(sizeof(kLiteralMinimalGoldSrcBspV30) - 1U);
    for (std::size_t index = 0U; index < sizeof(kLiteralMinimalGoldSrcBspV30) - 1U; ++index) {
        bytes.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(kLiteralMinimalGoldSrcBspV30[index])));
    }
    return bytes;
}

[[nodiscard]] constexpr std::size_t synthetic_lump_descriptor_offset(
    const SyntheticBspLumpId lump) noexcept
{
    return 4U + (static_cast<std::size_t>(lump) * 8U);
}

[[nodiscard]] inline std::uint16_t synthetic_read_u16le(
    const std::span<const std::byte> bytes,
    const std::size_t offset)
{
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1U])) << 8U));
}

[[nodiscard]] inline std::uint32_t synthetic_read_u32le(
    const std::span<const std::byte> bytes,
    const std::size_t offset)
{
    return std::to_integer<std::uint8_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1U])) << 8U) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2U])) << 16U) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] inline std::int32_t synthetic_read_i32le(
    const std::span<const std::byte> bytes,
    const std::size_t offset)
{
    return std::bit_cast<std::int32_t>(synthetic_read_u32le(bytes, offset));
}

inline void synthetic_write_u16le(
    const std::span<std::byte> bytes,
    const std::size_t offset,
    const std::uint16_t value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

inline void synthetic_write_i16le(
    const std::span<std::byte> bytes,
    const std::size_t offset,
    const std::int16_t value)
{
    synthetic_write_u16le(bytes, offset, std::bit_cast<std::uint16_t>(value));
}

inline void synthetic_write_u32le(
    const std::span<std::byte> bytes,
    const std::size_t offset,
    const std::uint32_t value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xFFU);
    bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xFFU);
}

inline void synthetic_write_i32le(
    const std::span<std::byte> bytes,
    const std::size_t offset,
    const std::int32_t value)
{
    synthetic_write_u32le(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

inline void synthetic_write_f32le(
    const std::span<std::byte> bytes,
    const std::size_t offset,
    const float value)
{
    synthetic_write_u32le(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

inline void synthetic_append_u8(std::vector<std::byte>& bytes, const std::uint8_t value)
{
    bytes.push_back(static_cast<std::byte>(value));
}

inline void synthetic_append_u16le(std::vector<std::byte>& bytes, const std::uint16_t value)
{
    synthetic_append_u8(bytes, static_cast<std::uint8_t>(value & 0xFFU));
    synthetic_append_u8(bytes, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

inline void synthetic_append_i16le(std::vector<std::byte>& bytes, const std::int16_t value)
{
    synthetic_append_u16le(bytes, std::bit_cast<std::uint16_t>(value));
}

inline void synthetic_append_u32le(std::vector<std::byte>& bytes, const std::uint32_t value)
{
    synthetic_append_u8(bytes, static_cast<std::uint8_t>(value & 0xFFU));
    synthetic_append_u8(bytes, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    synthetic_append_u8(bytes, static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    synthetic_append_u8(bytes, static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

inline void synthetic_append_i32le(std::vector<std::byte>& bytes, const std::int32_t value)
{
    synthetic_append_u32le(bytes, std::bit_cast<std::uint32_t>(value));
}

inline void synthetic_append_f32le(std::vector<std::byte>& bytes, const float value)
{
    synthetic_append_u32le(bytes, std::bit_cast<std::uint32_t>(value));
}

struct SyntheticBspVector3 {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

struct SyntheticBspPlane {
    SyntheticBspVector3 normal{0.0F, 0.0F, 1.0F};
    float distance{0.0F};
    std::int32_t type{2};
};

struct SyntheticBspTexinfo {
    std::array<float, 4U> s_vector{1.0F, 0.0F, 0.0F, 0.0F};
    std::array<float, 4U> t_vector{0.0F, 1.0F, 0.0F, 0.0F};
    std::int32_t miptex_index{0};
    std::int32_t flags{0};
};

struct SyntheticBspFace {
    std::int16_t plane_index{0};
    std::int16_t side{0};
    std::int32_t first_surfedge{0};
    std::int16_t surfedge_count{4};
    std::int16_t texinfo_index{0};
    std::array<std::uint8_t, 4U> light_styles{0xFFU, 0xFFU, 0xFFU, 0xFFU};
    std::int32_t light_offset{-1};
};

struct SyntheticBspEdge {
    std::uint16_t first_vertex{0U};
    std::uint16_t second_vertex{0U};
};

struct SyntheticBspNode {
    std::int32_t plane_index{0};
    std::array<std::int16_t, 2U> children{-1, -2};
    std::array<std::int16_t, 3U> minimum{-1, -1, -1};
    std::array<std::int16_t, 3U> maximum{65, 65, 1};
    std::uint16_t first_face{0U};
    std::uint16_t face_count{1U};
};

struct SyntheticBspLeaf {
    std::int32_t contents{-1};
    std::int32_t visibility_offset{-1};
    std::array<std::int16_t, 3U> minimum{-1, -1, -1};
    std::array<std::int16_t, 3U> maximum{65, 65, 1};
    std::uint16_t first_marksurface{0U};
    std::uint16_t marksurface_count{1U};
    std::array<std::uint8_t, 4U> ambient_levels{};
};

struct SyntheticBspClipnode {
    std::int32_t plane_index{0};
    std::array<std::int16_t, 2U> children{-1, -2};
};

struct SyntheticBspModel {
    SyntheticBspVector3 minimum{-1.0F, -1.0F, -1.0F};
    SyntheticBspVector3 maximum{65.0F, 65.0F, 1.0F};
    SyntheticBspVector3 origin{};
    std::array<std::int32_t, 4U> headnodes{0, -1, -1, -1};
    std::int32_t visibility_leaf_count{1};
    std::int32_t first_face{0};
    std::int32_t face_count{1};
};

struct SyntheticBspMipTexture {
    std::array<char, 16U> name{};
    std::uint32_t width{64U};
    std::uint32_t height{64U};
    std::array<std::uint32_t, 4U> mip_offsets{};
    std::size_t trailing_byte_count{0U};
};

[[nodiscard]] inline SyntheticBspMipTexture synthetic_external_texture(
    const std::string_view name = "TEST_QUAD")
{
    SyntheticBspMipTexture texture;
    const auto copy_count = std::min(name.size(), texture.name.size());
    std::copy_n(name.begin(), copy_count, texture.name.begin());
    return texture;
}

[[nodiscard]] inline SyntheticBspMipTexture synthetic_embedded_texture(
    const std::string_view name = "EMBEDDED",
    const std::uint32_t width = 16U,
    const std::uint32_t height = 16U)
{
    SyntheticBspMipTexture texture = synthetic_external_texture(name);
    texture.width = width;
    texture.height = height;

    const auto first_level = static_cast<std::uint64_t>(width) * height;
    const auto second_level = first_level / 4U;
    const auto third_level = second_level / 4U;
    const auto fourth_level = third_level / 4U;
    texture.mip_offsets = {
        40U,
        static_cast<std::uint32_t>(40U + first_level),
        static_cast<std::uint32_t>(40U + first_level + second_level),
        static_cast<std::uint32_t>(40U + first_level + second_level + third_level),
    };
    texture.trailing_byte_count = static_cast<std::size_t>(
        first_level + second_level + third_level + fourth_level);
    return texture;
}

class SyntheticBspBuilder final {
public:
    SyntheticBspBuilder()
    {
        const auto literal = literal_minimal_goldsrc_bsp_v30();
        for (std::size_t index = 0U; index < kSyntheticBspLumpCount; ++index) {
            const auto descriptor = 4U + (index * 8U);
            const auto offset = synthetic_read_i32le(literal, descriptor);
            const auto length = synthetic_read_i32le(literal, descriptor + 4U);
            if (length > 0) {
                const auto begin = static_cast<std::size_t>(offset);
                const auto end = begin + static_cast<std::size_t>(length);
                lumps_[index].assign(literal.begin() + static_cast<std::ptrdiff_t>(begin),
                    literal.begin() + static_cast<std::ptrdiff_t>(end));
            }
        }
    }

    [[nodiscard]] std::int32_t version() const noexcept
    {
        return version_;
    }

    SyntheticBspBuilder& set_version(const std::int32_t version) noexcept
    {
        version_ = version;
        return *this;
    }

    [[nodiscard]] std::vector<std::byte>& lump(const SyntheticBspLumpId id) noexcept
    {
        return lumps_[static_cast<std::size_t>(id)];
    }

    [[nodiscard]] const std::vector<std::byte>& lump(const SyntheticBspLumpId id) const noexcept
    {
        return lumps_[static_cast<std::size_t>(id)];
    }

    SyntheticBspBuilder& clear_lump(const SyntheticBspLumpId id)
    {
        lump(id).clear();
        return *this;
    }

    SyntheticBspBuilder& set_planes(const std::span<const SyntheticBspPlane> planes)
    {
        auto& bytes = lump(SyntheticBspLumpId::planes);
        bytes.clear();
        for (const auto& plane : planes) {
            synthetic_append_f32le(bytes, plane.normal.x);
            synthetic_append_f32le(bytes, plane.normal.y);
            synthetic_append_f32le(bytes, plane.normal.z);
            synthetic_append_f32le(bytes, plane.distance);
            synthetic_append_i32le(bytes, plane.type);
        }
        return *this;
    }

    SyntheticBspBuilder& set_vertices(const std::span<const SyntheticBspVector3> vertices)
    {
        auto& bytes = lump(SyntheticBspLumpId::vertices);
        bytes.clear();
        for (const auto& vertex : vertices) {
            synthetic_append_f32le(bytes, vertex.x);
            synthetic_append_f32le(bytes, vertex.y);
            synthetic_append_f32le(bytes, vertex.z);
        }
        return *this;
    }

    SyntheticBspBuilder& set_texinfo(const std::span<const SyntheticBspTexinfo> texinfo)
    {
        auto& bytes = lump(SyntheticBspLumpId::texinfo);
        bytes.clear();
        for (const auto& entry : texinfo) {
            for (const auto component : entry.s_vector) {
                synthetic_append_f32le(bytes, component);
            }
            for (const auto component : entry.t_vector) {
                synthetic_append_f32le(bytes, component);
            }
            synthetic_append_i32le(bytes, entry.miptex_index);
            synthetic_append_i32le(bytes, entry.flags);
        }
        return *this;
    }

    SyntheticBspBuilder& set_faces(const std::span<const SyntheticBspFace> faces)
    {
        auto& bytes = lump(SyntheticBspLumpId::faces);
        bytes.clear();
        for (const auto& face : faces) {
            synthetic_append_i16le(bytes, face.plane_index);
            synthetic_append_i16le(bytes, face.side);
            synthetic_append_i32le(bytes, face.first_surfedge);
            synthetic_append_i16le(bytes, face.surfedge_count);
            synthetic_append_i16le(bytes, face.texinfo_index);
            for (const auto style : face.light_styles) {
                synthetic_append_u8(bytes, style);
            }
            synthetic_append_i32le(bytes, face.light_offset);
        }
        return *this;
    }

    SyntheticBspBuilder& set_edges(const std::span<const SyntheticBspEdge> edges)
    {
        auto& bytes = lump(SyntheticBspLumpId::edges);
        bytes.clear();
        for (const auto& edge : edges) {
            synthetic_append_u16le(bytes, edge.first_vertex);
            synthetic_append_u16le(bytes, edge.second_vertex);
        }
        return *this;
    }

    SyntheticBspBuilder& set_surfedges(const std::span<const std::int32_t> surfedges)
    {
        auto& bytes = lump(SyntheticBspLumpId::surfedges);
        bytes.clear();
        for (const auto surfedge : surfedges) {
            synthetic_append_i32le(bytes, surfedge);
        }
        return *this;
    }

    SyntheticBspBuilder& set_nodes(const std::span<const SyntheticBspNode> nodes)
    {
        auto& bytes = lump(SyntheticBspLumpId::nodes);
        bytes.clear();
        for (const auto& node : nodes) {
            synthetic_append_i32le(bytes, node.plane_index);
            for (const auto child : node.children) {
                synthetic_append_i16le(bytes, child);
            }
            for (const auto coordinate : node.minimum) {
                synthetic_append_i16le(bytes, coordinate);
            }
            for (const auto coordinate : node.maximum) {
                synthetic_append_i16le(bytes, coordinate);
            }
            synthetic_append_u16le(bytes, node.first_face);
            synthetic_append_u16le(bytes, node.face_count);
        }
        return *this;
    }

    SyntheticBspBuilder& set_leaves(const std::span<const SyntheticBspLeaf> leaves)
    {
        auto& bytes = lump(SyntheticBspLumpId::leaves);
        bytes.clear();
        for (const auto& leaf : leaves) {
            synthetic_append_i32le(bytes, leaf.contents);
            synthetic_append_i32le(bytes, leaf.visibility_offset);
            for (const auto coordinate : leaf.minimum) {
                synthetic_append_i16le(bytes, coordinate);
            }
            for (const auto coordinate : leaf.maximum) {
                synthetic_append_i16le(bytes, coordinate);
            }
            synthetic_append_u16le(bytes, leaf.first_marksurface);
            synthetic_append_u16le(bytes, leaf.marksurface_count);
            for (const auto level : leaf.ambient_levels) {
                synthetic_append_u8(bytes, level);
            }
        }
        return *this;
    }

    SyntheticBspBuilder& set_marksurfaces(const std::span<const std::uint16_t> marksurfaces)
    {
        auto& bytes = lump(SyntheticBspLumpId::marksurfaces);
        bytes.clear();
        for (const auto marksurface : marksurfaces) {
            synthetic_append_u16le(bytes, marksurface);
        }
        return *this;
    }

    SyntheticBspBuilder& set_clipnodes(const std::span<const SyntheticBspClipnode> clipnodes)
    {
        auto& bytes = lump(SyntheticBspLumpId::clipnodes);
        bytes.clear();
        for (const auto& clipnode : clipnodes) {
            synthetic_append_i32le(bytes, clipnode.plane_index);
            for (const auto child : clipnode.children) {
                synthetic_append_i16le(bytes, child);
            }
        }
        return *this;
    }

    SyntheticBspBuilder& set_models(const std::span<const SyntheticBspModel> models)
    {
        auto& bytes = lump(SyntheticBspLumpId::models);
        bytes.clear();
        for (const auto& model : models) {
            for (const auto value : {model.minimum.x, model.minimum.y, model.minimum.z,
                     model.maximum.x, model.maximum.y, model.maximum.z,
                     model.origin.x, model.origin.y, model.origin.z}) {
                synthetic_append_f32le(bytes, value);
            }
            for (const auto headnode : model.headnodes) {
                synthetic_append_i32le(bytes, headnode);
            }
            synthetic_append_i32le(bytes, model.visibility_leaf_count);
            synthetic_append_i32le(bytes, model.first_face);
            synthetic_append_i32le(bytes, model.face_count);
        }
        return *this;
    }

    SyntheticBspBuilder& set_texture_directory(
        const std::span<const std::optional<SyntheticBspMipTexture>> textures)
    {
        auto& bytes = lump(SyntheticBspLumpId::textures);
        bytes.clear();
        synthetic_append_i32le(bytes, static_cast<std::int32_t>(textures.size()));

        const auto table_offset = bytes.size();
        bytes.resize(bytes.size() + (textures.size() * 4U));
        for (std::size_t index = 0U; index < textures.size(); ++index) {
            if (!textures[index].has_value()) {
                synthetic_write_i32le(bytes, table_offset + (index * 4U), -1);
                continue;
            }

            synthetic_write_i32le(bytes, table_offset + (index * 4U),
                static_cast<std::int32_t>(bytes.size()));
            const auto& texture = *textures[index];
            for (const auto character : texture.name) {
                synthetic_append_u8(bytes, static_cast<std::uint8_t>(character));
            }
            synthetic_append_u32le(bytes, texture.width);
            synthetic_append_u32le(bytes, texture.height);
            for (const auto offset : texture.mip_offsets) {
                synthetic_append_u32le(bytes, offset);
            }
            bytes.resize(bytes.size() + texture.trailing_byte_count);
        }
        return *this;
    }

    SyntheticBspBuilder& set_convex_polygon(
        const std::span<const SyntheticBspVector3> vertices)
    {
        set_vertices(vertices);

        // Callers provide renderer-canonical CCW positions. Valve qbsp wire
        // loops use the opposite direction; retain corner zero so the parser's
        // canonical conversion keeps historical output ordering byte-stable.
        std::vector<std::uint16_t> wire_order;
        wire_order.reserve(vertices.size());
        wire_order.push_back(0U);
        for (std::size_t index = vertices.size(); index > 1U; --index) {
            wire_order.push_back(static_cast<std::uint16_t>(index - 1U));
        }

        std::vector<SyntheticBspEdge> edges;
        edges.reserve(vertices.size() + 1U);
        edges.push_back({0U, 0U});
        for (std::size_t index = 0U; index < vertices.size(); ++index) {
            edges.push_back({wire_order[index],
                wire_order[(index + 1U) % wire_order.size()]});
        }
        set_edges(edges);

        std::vector<std::int32_t> surfedges;
        surfedges.reserve(vertices.size());
        for (std::size_t index = 0U; index < vertices.size(); ++index) {
            surfedges.push_back(static_cast<std::int32_t>(index + 1U));
        }
        set_surfedges(surfedges);

        SyntheticBspFace face;
        face.surfedge_count = static_cast<std::int16_t>(vertices.size());
        set_faces(std::span{&face, 1U});
        return *this;
    }

    [[nodiscard]] std::vector<std::byte> build() const
    {
        std::vector<std::byte> bytes(kSyntheticBspHeaderSize);
        synthetic_write_i32le(bytes, 0U, version_);

        for (std::size_t index = 0U; index < kSyntheticBspLumpCount; ++index) {
            const auto descriptor = 4U + (index * 8U);
            if (lumps_[index].empty()) {
                synthetic_write_i32le(bytes, descriptor, 0);
                synthetic_write_i32le(bytes, descriptor + 4U, 0);
                continue;
            }

            synthetic_write_i32le(bytes, descriptor, static_cast<std::int32_t>(bytes.size()));
            synthetic_write_i32le(bytes, descriptor + 4U,
                static_cast<std::int32_t>(lumps_[index].size()));
            bytes.insert(bytes.end(), lumps_[index].begin(), lumps_[index].end());
        }
        return bytes;
    }

private:
    std::int32_t version_{30};
    std::array<std::vector<std::byte>, kSyntheticBspLumpCount> lumps_{};
};

class SyntheticBspCorruptor final {
public:
    explicit SyntheticBspCorruptor(std::vector<std::byte> bytes)
        : bytes_{std::move(bytes)}
    {
    }

    SyntheticBspCorruptor& truncate(const std::size_t size)
    {
        bytes_.resize(std::min(size, bytes_.size()));
        return *this;
    }

    SyntheticBspCorruptor& set_version(const std::int32_t version)
    {
        synthetic_write_i32le(bytes_, 0U, version);
        return *this;
    }

    SyntheticBspCorruptor& set_lump_descriptor(
        const SyntheticBspLumpId id,
        const std::int32_t offset,
        const std::int32_t length)
    {
        const auto descriptor = synthetic_lump_descriptor_offset(id);
        synthetic_write_i32le(bytes_, descriptor, offset);
        synthetic_write_i32le(bytes_, descriptor + 4U, length);
        return *this;
    }

    SyntheticBspCorruptor& set_lump_offset(
        const SyntheticBspLumpId id,
        const std::int32_t offset)
    {
        synthetic_write_i32le(bytes_, synthetic_lump_descriptor_offset(id), offset);
        return *this;
    }

    SyntheticBspCorruptor& set_lump_length(
        const SyntheticBspLumpId id,
        const std::int32_t length)
    {
        synthetic_write_i32le(bytes_, synthetic_lump_descriptor_offset(id) + 4U, length);
        return *this;
    }

    [[nodiscard]] std::size_t lump_offset(const SyntheticBspLumpId id) const
    {
        return static_cast<std::size_t>(synthetic_read_i32le(
            bytes_, synthetic_lump_descriptor_offset(id)));
    }

    SyntheticBspCorruptor& write_i16(
        const SyntheticBspLumpId id,
        const std::size_t relative_offset,
        const std::int16_t value)
    {
        synthetic_write_i16le(bytes_, lump_offset(id) + relative_offset, value);
        return *this;
    }

    SyntheticBspCorruptor& write_u16(
        const SyntheticBspLumpId id,
        const std::size_t relative_offset,
        const std::uint16_t value)
    {
        synthetic_write_u16le(bytes_, lump_offset(id) + relative_offset, value);
        return *this;
    }

    SyntheticBspCorruptor& write_i32(
        const SyntheticBspLumpId id,
        const std::size_t relative_offset,
        const std::int32_t value)
    {
        synthetic_write_i32le(bytes_, lump_offset(id) + relative_offset, value);
        return *this;
    }

    SyntheticBspCorruptor& write_u32(
        const SyntheticBspLumpId id,
        const std::size_t relative_offset,
        const std::uint32_t value)
    {
        synthetic_write_u32le(bytes_, lump_offset(id) + relative_offset, value);
        return *this;
    }

    SyntheticBspCorruptor& write_f32(
        const SyntheticBspLumpId id,
        const std::size_t relative_offset,
        const float value)
    {
        synthetic_write_f32le(bytes_, lump_offset(id) + relative_offset, value);
        return *this;
    }

    SyntheticBspCorruptor& xor_byte(const std::size_t offset, const std::uint8_t mask)
    {
        bytes_[offset] ^= static_cast<std::byte>(mask);
        return *this;
    }

    [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept
    {
        return bytes_;
    }

    [[nodiscard]] std::vector<std::byte> take() noexcept
    {
        return std::move(bytes_);
    }

private:
    std::vector<std::byte> bytes_;
};

[[nodiscard]] inline constexpr std::array<SyntheticBspVector3, 3U>
    synthetic_triangle_vertices() noexcept
{
    return {{{0.0F, 0.0F, 0.0F}, {64.0F, 0.0F, 0.0F}, {0.0F, 64.0F, 0.0F}}};
}

[[nodiscard]] inline constexpr std::array<SyntheticBspVector3, 4U>
    synthetic_quad_vertices() noexcept
{
    return {{{0.0F, 0.0F, 0.0F},
        {64.0F, 0.0F, 0.0F},
        {64.0F, 64.0F, 0.0F},
        {0.0F, 64.0F, 0.0F}}};
}

[[nodiscard]] inline constexpr std::array<SyntheticBspVector3, 5U>
    synthetic_pentagon_vertices() noexcept
{
    return {{{0.0F, 0.0F, 0.0F},
        {64.0F, 0.0F, 0.0F},
        {80.0F, 32.0F, 0.0F},
        {32.0F, 80.0F, 0.0F},
        {0.0F, 64.0F, 0.0F}}};
}

} // namespace hlclient::tests
