#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hlclient::goldsrc::bsp {
namespace {

class ByteReader final {
public:
    explicit ByteReader(const std::span<const std::byte> bytes) noexcept : bytes_{bytes} {}

    [[nodiscard]] std::size_t position() const noexcept { return position_; }

    [[nodiscard]] std::optional<std::span<const std::byte>> read_bytes(
        const std::size_t count) noexcept
    {
        if (count > bytes_.size() - position_) {
            return std::nullopt;
        }
        const auto result = bytes_.subspan(position_, count);
        position_ += count;
        return result;
    }

    [[nodiscard]] std::optional<std::uint8_t> read_u8() noexcept
    {
        const auto bytes = read_bytes(1U);
        return bytes ? std::optional{std::to_integer<std::uint8_t>((*bytes)[0U])}
                     : std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint16_t> read_u16_le() noexcept
    {
        const auto bytes = read_bytes(2U);
        if (!bytes) {
            return std::nullopt;
        }
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(std::to_integer<std::uint8_t>((*bytes)[0U])) |
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(std::to_integer<std::uint8_t>((*bytes)[1U]))
                << 8U));
    }

    [[nodiscard]] std::optional<std::int16_t> read_i16_le() noexcept
    {
        const auto value = read_u16_le();
        return value ? std::optional{std::bit_cast<std::int16_t>(*value)} : std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint32_t> read_u32_le() noexcept
    {
        const auto bytes = read_bytes(4U);
        if (!bytes) {
            return std::nullopt;
        }
        std::uint32_t value = 0U;
        for (std::size_t index = 0U; index < 4U; ++index) {
            value |= static_cast<std::uint32_t>(
                         std::to_integer<std::uint8_t>((*bytes)[index]))
                     << static_cast<unsigned int>(index * 8U);
        }
        return value;
    }

    [[nodiscard]] std::optional<std::int32_t> read_i32_le() noexcept
    {
        const auto value = read_u32_le();
        return value ? std::optional{std::bit_cast<std::int32_t>(*value)} : std::nullopt;
    }

    [[nodiscard]] std::optional<float> read_f32_le() noexcept
    {
        static_assert(sizeof(float) == sizeof(std::uint32_t));
        static_assert(std::numeric_limits<float>::is_iec559);
        const auto value = read_u32_le();
        return value ? std::optional{std::bit_cast<float>(*value)} : std::nullopt;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t position_{0U};
};

struct LumpRange {
    std::size_t offset{0U};
    std::size_t length{0U};
};

struct Plane {
    assets::AssetVector3 normal{};
    double distance{0.0};
    std::int32_t type{0};
};

using Edge = GoldSrcFaceGeometrySourceEdge;

struct Texinfo {
    std::array<float, 4U> s{};
    std::array<float, 4U> t{};
    std::int32_t miptex_index{0};
    std::int32_t flags{0};
};

struct Face {
    std::int16_t plane_index{0};
    std::int16_t side{0};
    std::int32_t first_surfedge{0};
    std::int16_t surfedge_count{0};
    std::int16_t texinfo_index{0};
    std::array<std::uint8_t, 4U> light_styles{};
    std::int32_t light_offset{-1};
};

struct Node {
    std::int32_t plane_index{0};
    std::array<std::int16_t, 2U> children{};
    std::array<std::int16_t, 3U> minimums{};
    std::array<std::int16_t, 3U> maximums{};
    std::uint16_t first_face{0U};
    std::uint16_t face_count{0U};
};

struct Leaf {
    std::int32_t contents{0};
    std::int32_t visibility_offset{-1};
    std::array<std::int16_t, 3U> minimums{};
    std::array<std::int16_t, 3U> maximums{};
    std::uint16_t first_marksurface{0U};
    std::uint16_t marksurface_count{0U};
    std::array<std::uint8_t, 4U> ambient_levels{};
};

struct Clipnode {
    std::int32_t plane_index{0};
    std::array<std::int16_t, 2U> children{};
};

struct Model {
    assets::AssetVector3 minimums{};
    assets::AssetVector3 maximums{};
    assets::AssetVector3 origin{};
    std::array<std::int32_t, 4U> headnodes{};
    std::int32_t visible_leaf_count{0};
    std::int32_t first_face{0};
    std::int32_t face_count{0};
};

struct TextureMetadata {
    assets::WorldTextureStorage storage{assets::WorldTextureStorage::missing};
    std::optional<std::string> name;
    std::optional<std::uint32_t> width;
    std::optional<std::uint32_t> height;
};

struct ModelGeometryOutputLimits {
    std::size_t maximum_vertices{0U};
    std::size_t maximum_indices{0U};
    std::size_t maximum_surfaces{0U};
    std::size_t maximum_materials{0U};
};

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool checked_multiply(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] bool checked_multiply_u64(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t& result) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] bool checked_add_u64(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t& result) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool finite_vector(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool supported_contents(const std::int32_t value) noexcept
{
    return value >= kGoldSrcBspMinimumContentsValue &&
           value <= kGoldSrcBspMaximumContentsValue;
}

[[nodiscard]] std::string bounded_context(const std::string_view context)
{
    return std::string{context.substr(0U, kGoldSrcBspMaximumDiagnosticContextBytes)};
}

[[nodiscard]] GoldSrcBspParseResult failure_result(GoldSrcBspError error)
{
    return GoldSrcBspParseResult{std::nullopt, std::move(error)};
}

[[nodiscard]] GoldSrcBspErrorCode parser_error_code(
    const GoldSrcFaceGeometryErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcFaceGeometryErrorCode::none:
    case GoldSrcFaceGeometryErrorCode::invalid_configuration:
        return GoldSrcBspErrorCode::invalid_configuration;
    case GoldSrcFaceGeometryErrorCode::invalid_plane:
    case GoldSrcFaceGeometryErrorCode::invalid_face_side:
        return GoldSrcBspErrorCode::invalid_face_reference;
    case GoldSrcFaceGeometryErrorCode::invalid_surfedge_range:
        return GoldSrcBspErrorCode::invalid_face_reference;
    case GoldSrcFaceGeometryErrorCode::invalid_surfedge_reference:
        return GoldSrcBspErrorCode::invalid_surfedge_reference;
    case GoldSrcFaceGeometryErrorCode::invalid_edge_reference:
        return GoldSrcBspErrorCode::invalid_edge_reference;
    case GoldSrcFaceGeometryErrorCode::invalid_vertex_reference:
        return GoldSrcBspErrorCode::invalid_vertex_reference;
    case GoldSrcFaceGeometryErrorCode::broken_face_edge_loop:
    case GoldSrcFaceGeometryErrorCode::duplicate_face_vertex:
        return GoldSrcBspErrorCode::broken_face_edge_loop;
    case GoldSrcFaceGeometryErrorCode::nonplanar_face:
        return GoldSrcBspErrorCode::nonplanar_face;
    case GoldSrcFaceGeometryErrorCode::self_intersecting_face:
    case GoldSrcFaceGeometryErrorCode::invalid_face_winding:
    case GoldSrcFaceGeometryErrorCode::concave_face:
        return GoldSrcBspErrorCode::invalid_face_winding;
    case GoldSrcFaceGeometryErrorCode::degenerate_face:
    case GoldSrcFaceGeometryErrorCode::collinear_canonicalization_failed:
        return GoldSrcBspErrorCode::degenerate_face;
    case GoldSrcFaceGeometryErrorCode::invalid_texture_coordinate:
        return GoldSrcBspErrorCode::invalid_float;
    case GoldSrcFaceGeometryErrorCode::geometry_limit_exceeded:
        return GoldSrcBspErrorCode::geometry_limit_exceeded;
    case GoldSrcFaceGeometryErrorCode::unable_to_retain_candidate:
        return GoldSrcBspErrorCode::unable_to_retain_world;
    }
    return GoldSrcBspErrorCode::unable_to_retain_world;
}

} // namespace

bool valid_goldsrc_bsp_import_limits(const GoldSrcBspImportLimits& limits) noexcept
{
    return limits.maximum_source_bytes >= kGoldSrcBspHeaderWireSize &&
           limits.maximum_source_bytes <= kGoldSrcBspHardMaximumSourceBytes &&
           limits.maximum_planes > 0U &&
           limits.maximum_planes <= kGoldSrcBspHardMaximumPlanes &&
           limits.maximum_vertices > 0U &&
           limits.maximum_vertices <= kGoldSrcBspHardMaximumVertices &&
           limits.maximum_nodes > 0U &&
           limits.maximum_nodes <= kGoldSrcBspHardMaximumNodes &&
           limits.maximum_texinfo > 0U &&
           limits.maximum_texinfo <= kGoldSrcBspHardMaximumTexinfo &&
           limits.maximum_faces > 0U &&
           limits.maximum_faces <= kGoldSrcBspHardMaximumFaces &&
           limits.maximum_clipnodes > 0U &&
           limits.maximum_clipnodes <= kGoldSrcBspHardMaximumClipnodes &&
           limits.maximum_leaves > 0U &&
           limits.maximum_leaves <= kGoldSrcBspHardMaximumLeaves &&
           limits.maximum_marksurfaces > 0U &&
           limits.maximum_marksurfaces <= kGoldSrcBspHardMaximumMarksurfaces &&
           limits.maximum_edges > 0U &&
           limits.maximum_edges <= kGoldSrcBspHardMaximumEdges &&
           limits.maximum_surfedges > 0U &&
           limits.maximum_surfedges <= kGoldSrcBspHardMaximumSurfedges &&
           limits.maximum_models > 0U &&
           limits.maximum_models <= kGoldSrcBspHardMaximumModels &&
           limits.maximum_textures > 0U &&
           limits.maximum_textures <= kGoldSrcBspHardMaximumTextures &&
           limits.maximum_face_edges >= 3U &&
           limits.maximum_face_edges <= kGoldSrcBspHardMaximumFaceEdges &&
           limits.maximum_polygon_edge_pair_tests > 0U &&
           limits.maximum_polygon_edge_pair_tests <=
               kGoldSrcBspHardMaximumPolygonEdgePairTests &&
           limits.maximum_output_vertices >= 3U &&
           limits.maximum_output_vertices <= kGoldSrcBspHardMaximumOutputVertices &&
           limits.maximum_output_indices >= 3U &&
           limits.maximum_output_indices <= kGoldSrcBspHardMaximumOutputIndices &&
           limits.maximum_output_surfaces > 0U &&
           limits.maximum_output_surfaces <= kGoldSrcBspHardMaximumOutputSurfaces &&
           limits.maximum_output_materials > 0U &&
           limits.maximum_output_materials <= kGoldSrcBspHardMaximumOutputMaterials &&
           limits.maximum_texture_name_bytes > 0U &&
           limits.maximum_texture_name_bytes <= kGoldSrcBspTextureNameWireSize &&
           limits.maximum_texture_dimension > 0U &&
           limits.maximum_texture_dimension <= kGoldSrcBspHardMaximumTextureDimension &&
           limits.maximum_texture_texels > 0U &&
           limits.maximum_texture_texels <= kGoldSrcBspHardMaximumTextureTexels;
}

assets::AssetSourceFingerprint goldsrc_bsp_source_fingerprint(
    const std::span<const std::byte> source) noexcept
{
    constexpr std::uint64_t first_offset = 14'695'981'039'346'656'037ULL;
    constexpr std::uint64_t first_prime = 1'099'511'628'211ULL;
    constexpr std::uint64_t second_offset = 7'806'984'959'868'165'187ULL;
    constexpr std::uint64_t second_prime = 14'029'467'366'897'019'727ULL;
    std::uint64_t first = first_offset;
    std::uint64_t second = second_offset;
    const auto add = [](std::uint64_t& hash,
                         const std::uint8_t value,
                         const std::uint64_t prime) noexcept {
        hash ^= value;
        hash *= prime;
    };
    for (const auto value : source) {
        const auto byte = std::to_integer<std::uint8_t>(value);
        add(first, byte, first_prime);
        add(second, byte, second_prime);
    }
    auto size = static_cast<std::uint64_t>(source.size());
    for (std::size_t index = 0U; index < sizeof(size); ++index) {
        const auto byte = static_cast<std::uint8_t>(size & 0xFFU);
        add(first, byte, first_prime);
        add(second, byte, second_prime);
        size >>= 8U;
    }
    return {first, second};
}

std::string_view to_string(const GoldSrcBspErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcBspErrorCode::invalid_configuration: return "invalid_configuration";
    case GoldSrcBspErrorCode::source_too_small: return "source_too_small";
    case GoldSrcBspErrorCode::unsupported_version: return "unsupported_version";
    case GoldSrcBspErrorCode::invalid_lump_directory: return "invalid_lump_directory";
    case GoldSrcBspErrorCode::negative_lump_range: return "negative_lump_range";
    case GoldSrcBspErrorCode::lump_range_overflow: return "lump_range_overflow";
    case GoldSrcBspErrorCode::lump_out_of_bounds: return "lump_out_of_bounds";
    case GoldSrcBspErrorCode::lump_overlaps_header: return "lump_overlaps_header";
    case GoldSrcBspErrorCode::lump_overlap: return "lump_overlap";
    case GoldSrcBspErrorCode::misaligned_fixed_lump_size:
        return "misaligned_fixed_lump_size";
    case GoldSrcBspErrorCode::count_limit_exceeded: return "count_limit_exceeded";
    case GoldSrcBspErrorCode::invalid_float: return "invalid_float";
    case GoldSrcBspErrorCode::invalid_plane: return "invalid_plane";
    case GoldSrcBspErrorCode::invalid_texture_directory:
        return "invalid_texture_directory";
    case GoldSrcBspErrorCode::invalid_texture_metadata:
        return "invalid_texture_metadata";
    case GoldSrcBspErrorCode::invalid_vertex_reference: return "invalid_vertex_reference";
    case GoldSrcBspErrorCode::invalid_edge_reference: return "invalid_edge_reference";
    case GoldSrcBspErrorCode::invalid_surfedge_reference:
        return "invalid_surfedge_reference";
    case GoldSrcBspErrorCode::invalid_texinfo_reference: return "invalid_texinfo_reference";
    case GoldSrcBspErrorCode::invalid_face_reference: return "invalid_face_reference";
    case GoldSrcBspErrorCode::invalid_model_reference: return "invalid_model_reference";
    case GoldSrcBspErrorCode::invalid_node_reference: return "invalid_node_reference";
    case GoldSrcBspErrorCode::invalid_leaf_reference: return "invalid_leaf_reference";
    case GoldSrcBspErrorCode::invalid_marksurface_reference:
        return "invalid_marksurface_reference";
    case GoldSrcBspErrorCode::invalid_clipnode_reference:
        return "invalid_clipnode_reference";
    case GoldSrcBspErrorCode::invalid_light_offset: return "invalid_light_offset";
    case GoldSrcBspErrorCode::broken_face_edge_loop: return "broken_face_edge_loop";
    case GoldSrcBspErrorCode::degenerate_face: return "degenerate_face";
    case GoldSrcBspErrorCode::nonplanar_face: return "nonplanar_face";
    case GoldSrcBspErrorCode::invalid_face_winding: return "invalid_face_winding";
    case GoldSrcBspErrorCode::geometry_limit_exceeded:
        return "geometry_limit_exceeded";
    case GoldSrcBspErrorCode::unable_to_retain_world: return "unable_to_retain_world";
    }
    return "unknown";
}

namespace {

class ParserState final {
public:
    ParserState(
        const std::span<const std::byte> source,
        const GoldSrcBspImportLimits& limits,
        const GoldSrcBspParseOptions& options) noexcept
        : source_{source}, limits_{limits},
          materialize_brush_submodels_{options.materialize_brush_submodels}
    {
    }

    [[nodiscard]] GoldSrcBspParseResult run()
    {
        if (!valid_goldsrc_bsp_import_limits(limits_)) {
            return fail_result(
                GoldSrcBspErrorCode::invalid_configuration,
                std::nullopt,
                0U,
                std::nullopt,
                "BSP import limits are outside the supported hard profile");
        }
        if (source_.size() > limits_.maximum_source_bytes) {
            return fail_result(
                GoldSrcBspErrorCode::count_limit_exceeded,
                std::nullopt,
                0U,
                std::nullopt,
                "BSP source exceeds the configured byte limit");
        }
        if (source_.size() < 4U) {
            return fail_result(
                GoldSrcBspErrorCode::source_too_small,
                std::nullopt,
                source_.size(),
                std::nullopt,
                "BSP source does not contain a complete version field");
        }

        ByteReader version_reader{source_.first(4U)};
        const auto version = version_reader.read_i32_le();
        if (!version) {
            return fail_result(
                GoldSrcBspErrorCode::source_too_small,
                std::nullopt,
                0U,
                std::nullopt,
                "BSP version could not be decoded");
        }
        if (*version != kGoldSrcBspVersion) {
            return fail_result(
                GoldSrcBspErrorCode::unsupported_version,
                std::nullopt,
                0U,
                std::nullopt,
                "Only Valve GoldSrc BSP version 30 is supported");
        }
        if (source_.size() < kGoldSrcBspHeaderWireSize) {
            return fail_result(
                GoldSrcBspErrorCode::source_too_small,
                std::nullopt,
                source_.size(),
                std::nullopt,
                "Version 30 BSP source has a truncated 124-byte header");
        }
        if (!decode_lump_directory() || !validate_fixed_lumps() ||
            !decode_planes() || !decode_vertices() || !decode_edges() ||
            !decode_surfedges() || !decode_textures() || !decode_texinfo() ||
            !decode_faces() || !decode_nodes() || !decode_leaves() ||
            !decode_marksurfaces() || !decode_clipnodes() || !decode_models() ||
            !validate_references()) {
            return GoldSrcBspParseResult{std::nullopt, std::move(error_)};
        }

        auto document = build_world();
        if (!document) {
            return GoldSrcBspParseResult{std::nullopt, std::move(error_)};
        }
        return GoldSrcBspParseResult{std::move(document), std::nullopt};
    }

private:
    [[nodiscard]] GoldSrcBspParseResult fail_result(
        const GoldSrcBspErrorCode code,
        const std::optional<GoldSrcBspLumpId> lump_id,
        const std::size_t byte_offset,
        const std::optional<std::size_t> element_index,
        const std::string_view context)
    {
        return failure_result(GoldSrcBspError{
            code,
            lump_id,
            byte_offset,
            element_index,
            bounded_context(context),
        });
    }

    [[nodiscard]] bool fail(
        const GoldSrcBspErrorCode code,
        const std::optional<GoldSrcBspLumpId> lump_id,
        const std::size_t byte_offset,
        const std::optional<std::size_t> element_index,
        const std::string_view context)
    {
        if (!error_) {
            error_ = GoldSrcBspError{
                code,
                lump_id,
                byte_offset,
                element_index,
                bounded_context(context),
            };
        }
        return false;
    }

    [[nodiscard]] bool fail_face_geometry(
        const GoldSrcFaceGeometryError& geometry_error,
        const std::size_t face_record_offset)
    {
        if (!error_) {
            std::string context{"Face geometry validation failed: "};
            context.append(to_string(geometry_error.code));
            error_ = GoldSrcBspError{
                parser_error_code(geometry_error.code),
                GoldSrcBspLumpId::faces,
                face_record_offset,
                geometry_error.diagnostic.source_face_ordinal,
                bounded_context(context),
                geometry_error.diagnostic.source_model_index,
                geometry_error.diagnostic,
            };
        }
        return false;
    }

    [[nodiscard]] std::span<const std::byte> lump_bytes(
        const GoldSrcBspLumpId id) const noexcept
    {
        const auto& range = lumps_[goldsrc_bsp_lump_index(id)];
        return source_.subspan(range.offset, range.length);
    }

    [[nodiscard]] std::size_t absolute_record_offset(
        const GoldSrcBspLumpId id,
        const std::size_t element_index,
        const std::size_t record_size) const noexcept
    {
        return lumps_[goldsrc_bsp_lump_index(id)].offset + element_index * record_size;
    }

    [[nodiscard]] bool decode_lump_directory()
    {
        ByteReader reader{source_};
        if (!reader.read_i32_le()) {
            return fail(
                GoldSrcBspErrorCode::invalid_lump_directory,
                std::nullopt,
                0U,
                std::nullopt,
                "BSP version field disappeared during header decoding");
        }

        for (const auto id : kGoldSrcBspLumpIds) {
            const auto descriptor_offset = reader.position();
            const auto signed_offset = reader.read_i32_le();
            const auto signed_length = reader.read_i32_le();
            if (!signed_offset || !signed_length) {
                return fail(
                    GoldSrcBspErrorCode::invalid_lump_directory,
                    id,
                    descriptor_offset,
                    std::nullopt,
                    "A BSP lump descriptor is truncated");
            }
            if (*signed_offset < 0 || *signed_length < 0) {
                return fail(
                    GoldSrcBspErrorCode::negative_lump_range,
                    id,
                    descriptor_offset,
                    std::nullopt,
                    "BSP lump offsets and lengths are signed and must be non-negative");
            }

            const auto offset = static_cast<std::size_t>(
                static_cast<std::uint32_t>(*signed_offset));
            const auto length = static_cast<std::size_t>(
                static_cast<std::uint32_t>(*signed_length));
            std::size_t end = 0U;
            if (!checked_add(offset, length, end)) {
                return fail(
                    GoldSrcBspErrorCode::lump_range_overflow,
                    id,
                    descriptor_offset,
                    std::nullopt,
                    "BSP lump offset plus length overflows the host size type");
            }
            if (end > source_.size()) {
                return fail(
                    GoldSrcBspErrorCode::lump_out_of_bounds,
                    id,
                    descriptor_offset,
                    std::nullopt,
                    "BSP lump range extends beyond the retained source");
            }
            if (length != 0U && offset < kGoldSrcBspHeaderWireSize) {
                return fail(
                    GoldSrcBspErrorCode::lump_overlaps_header,
                    id,
                    descriptor_offset,
                    std::nullopt,
                    "A non-empty BSP lump overlaps the 124-byte header");
            }
            lumps_[goldsrc_bsp_lump_index(id)] = LumpRange{offset, length};
        }

        for (std::size_t left = 0U; left < lumps_.size(); ++left) {
            if (lumps_[left].length == 0U) {
                continue;
            }
            std::size_t left_end = 0U;
            if (!checked_add(lumps_[left].offset, lumps_[left].length, left_end)) {
                return fail(
                    GoldSrcBspErrorCode::lump_range_overflow,
                    kGoldSrcBspLumpIds[left],
                    4U + left * kGoldSrcBspLumpDescriptorWireSize,
                    std::nullopt,
                    "Validated BSP lump end could not be retained for overlap checking");
            }
            for (std::size_t right = left + 1U; right < lumps_.size(); ++right) {
                if (lumps_[right].length == 0U) {
                    continue;
                }
                std::size_t right_end = 0U;
                if (!checked_add(lumps_[right].offset, lumps_[right].length, right_end)) {
                    return fail(
                        GoldSrcBspErrorCode::lump_range_overflow,
                        kGoldSrcBspLumpIds[right],
                        4U + right * kGoldSrcBspLumpDescriptorWireSize,
                        std::nullopt,
                        "Validated BSP lump end could not be retained for overlap checking");
                }
                if (lumps_[left].offset < right_end && lumps_[right].offset < left_end) {
                    return fail(
                        GoldSrcBspErrorCode::lump_overlap,
                        kGoldSrcBspLumpIds[right],
                        4U + right * kGoldSrcBspLumpDescriptorWireSize,
                        std::nullopt,
                        "Two non-empty BSP lumps overlap");
                }
            }
        }
        return true;
    }

    [[nodiscard]] std::size_t configured_count_limit(const GoldSrcBspLumpId id) const noexcept
    {
        switch (id) {
        case GoldSrcBspLumpId::planes: return limits_.maximum_planes;
        case GoldSrcBspLumpId::vertices: return limits_.maximum_vertices;
        case GoldSrcBspLumpId::nodes: return limits_.maximum_nodes;
        case GoldSrcBspLumpId::texinfo: return limits_.maximum_texinfo;
        case GoldSrcBspLumpId::faces: return limits_.maximum_faces;
        case GoldSrcBspLumpId::clipnodes: return limits_.maximum_clipnodes;
        case GoldSrcBspLumpId::leaves: return limits_.maximum_leaves;
        case GoldSrcBspLumpId::marksurfaces: return limits_.maximum_marksurfaces;
        case GoldSrcBspLumpId::edges: return limits_.maximum_edges;
        case GoldSrcBspLumpId::surfedges: return limits_.maximum_surfedges;
        case GoldSrcBspLumpId::models: return limits_.maximum_models;
        default: return std::numeric_limits<std::size_t>::max();
        }
    }

    [[nodiscard]] bool validate_fixed_lump(
        const GoldSrcBspLumpId id,
        const std::size_t record_size)
    {
        const auto& range = lumps_[goldsrc_bsp_lump_index(id)];
        if (range.length % record_size != 0U) {
            return fail(
                GoldSrcBspErrorCode::misaligned_fixed_lump_size,
                id,
                range.offset,
                std::nullopt,
                "Fixed-record BSP lump length is not an exact wire-size multiple");
        }
        const auto count = range.length / record_size;
        if (count > configured_count_limit(id)) {
            return fail(
                GoldSrcBspErrorCode::count_limit_exceeded,
                id,
                range.offset,
                count,
                "Fixed-record BSP lump exceeds its configured record-count limit");
        }
        lump_element_counts_[goldsrc_bsp_lump_index(id)] = count;
        return true;
    }

    [[nodiscard]] bool validate_fixed_lumps()
    {
        lump_element_counts_[goldsrc_bsp_lump_index(GoldSrcBspLumpId::entities)] =
            lumps_[goldsrc_bsp_lump_index(GoldSrcBspLumpId::entities)].length;
        lump_element_counts_[goldsrc_bsp_lump_index(GoldSrcBspLumpId::visibility)] =
            lumps_[goldsrc_bsp_lump_index(GoldSrcBspLumpId::visibility)].length;
        lump_element_counts_[goldsrc_bsp_lump_index(GoldSrcBspLumpId::lighting)] =
            lumps_[goldsrc_bsp_lump_index(GoldSrcBspLumpId::lighting)].length;

        return validate_fixed_lump(GoldSrcBspLumpId::planes, kGoldSrcBspPlaneWireSize) &&
               validate_fixed_lump(GoldSrcBspLumpId::vertices, kGoldSrcBspVertexWireSize) &&
               validate_fixed_lump(GoldSrcBspLumpId::nodes, kGoldSrcBspNodeWireSize) &&
               validate_fixed_lump(GoldSrcBspLumpId::texinfo, kGoldSrcBspTexinfoWireSize) &&
               validate_fixed_lump(GoldSrcBspLumpId::faces, kGoldSrcBspFaceWireSize) &&
               validate_fixed_lump(
                   GoldSrcBspLumpId::clipnodes,
                   kGoldSrcBspClipnodeWireSize) &&
               validate_fixed_lump(GoldSrcBspLumpId::leaves, kGoldSrcBspLeafWireSize) &&
               validate_fixed_lump(
                   GoldSrcBspLumpId::marksurfaces,
                   kGoldSrcBspMarksurfaceWireSize) &&
               validate_fixed_lump(GoldSrcBspLumpId::edges, kGoldSrcBspEdgeWireSize) &&
               validate_fixed_lump(
                   GoldSrcBspLumpId::surfedges,
                   kGoldSrcBspSurfedgeWireSize) &&
               validate_fixed_lump(GoldSrcBspLumpId::models, kGoldSrcBspModelWireSize);
    }

    [[nodiscard]] std::optional<assets::AssetVector3> read_vector3(ByteReader& reader) const
    {
        const auto x = reader.read_f32_le();
        const auto y = reader.read_f32_le();
        const auto z = reader.read_f32_le();
        if (!x || !y || !z) {
            return std::nullopt;
        }
        return assets::AssetVector3{*x, *y, *z};
    }

    [[nodiscard]] bool decode_planes()
    {
        const auto id = GoldSrcBspLumpId::planes;
        ByteReader reader{lump_bytes(id)};
        const auto count = lump_element_counts_[goldsrc_bsp_lump_index(id)];
        planes_.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            const auto record_offset = absolute_record_offset(id, index, kGoldSrcBspPlaneWireSize);
            const auto normal = read_vector3(reader);
            const auto distance = reader.read_f32_le();
            const auto type = reader.read_i32_le();
            if (!normal || !distance || !type || !finite_vector(*normal) ||
                !std::isfinite(*distance)) {
                return fail(
                    GoldSrcBspErrorCode::invalid_float,
                    id,
                    record_offset,
                    index,
                    "BSP plane contains a truncated or non-finite float");
            }
            const auto length = std::sqrt(
                static_cast<double>(normal->x) * static_cast<double>(normal->x) +
                static_cast<double>(normal->y) * static_cast<double>(normal->y) +
                static_cast<double>(normal->z) * static_cast<double>(normal->z));
            if (!(length > kGoldSrcBspGeometryEpsilon) ||
                std::abs(length - 1.0) > static_cast<double>(kGoldSrcBspPlaneUnitTolerance) ||
                *type < 0 || *type > 5) {
                return fail(
                    GoldSrcBspErrorCode::invalid_plane,
                    id,
                    record_offset,
                    index,
                    "BSP plane normal or axial-type metadata is outside the supported profile");
            }
            planes_.push_back(Plane{
                assets::AssetVector3{
                    static_cast<float>(static_cast<double>(normal->x) / length),
                    static_cast<float>(static_cast<double>(normal->y) / length),
                    static_cast<float>(static_cast<double>(normal->z) / length),
                },
                static_cast<double>(*distance) / length,
                *type,
            });
        }
        return true;
    }

    [[nodiscard]] bool decode_vertices()
    {
        const auto id = GoldSrcBspLumpId::vertices;
        ByteReader reader{lump_bytes(id)};
        const auto count = lump_element_counts_[goldsrc_bsp_lump_index(id)];
        vertices_.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            const auto position = read_vector3(reader);
            if (!position || !finite_vector(*position)) {
                return fail(
                    GoldSrcBspErrorCode::invalid_float,
                    id,
                    absolute_record_offset(id, index, kGoldSrcBspVertexWireSize),
                    index,
                    "BSP vertex contains a truncated or non-finite coordinate");
            }
            vertices_.push_back(*position);
        }
        return true;
    }

    [[nodiscard]] bool decode_edges()
    {
        const auto id = GoldSrcBspLumpId::edges;
        ByteReader reader{lump_bytes(id)};
        const auto count = lump_element_counts_[goldsrc_bsp_lump_index(id)];
        edges_.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            const auto first = reader.read_u16_le();
            const auto second = reader.read_u16_le();
            if (!first || !second) {
                return fail(
                    GoldSrcBspErrorCode::invalid_edge_reference,
                    id,
                    absolute_record_offset(id, index, kGoldSrcBspEdgeWireSize),
                    index,
                    "BSP edge record is truncated");
            }
            edges_.push_back(Edge{{*first, *second}});
        }
        return true;
    }

    [[nodiscard]] bool decode_surfedges()
    {
        const auto id = GoldSrcBspLumpId::surfedges;
        ByteReader reader{lump_bytes(id)};
        const auto count = lump_element_counts_[goldsrc_bsp_lump_index(id)];
        surfedges_.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            const auto value = reader.read_i32_le();
            if (!value) {
                return fail(
                    GoldSrcBspErrorCode::invalid_surfedge_reference,
                    id,
                    absolute_record_offset(id, index, kGoldSrcBspSurfedgeWireSize),
                    index,
                    "BSP surfedge record is truncated");
            }
            surfedges_.push_back(*value);
        }
        return true;
    }

    [[nodiscard]] bool decode_textures()
    {
        const auto id = GoldSrcBspLumpId::textures;
        const auto bytes = lump_bytes(id);
        const auto base_offset = lumps_[goldsrc_bsp_lump_index(id)].offset;
        if (bytes.size() < 4U) {
            return fail(
                GoldSrcBspErrorCode::invalid_texture_directory,
                id,
                base_offset,
                std::nullopt,
                "Texture lump does not contain its signed directory count");
        }

        ByteReader directory_reader{bytes};
        const auto signed_count = directory_reader.read_i32_le();
        if (!signed_count || *signed_count < 0) {
            return fail(
                GoldSrcBspErrorCode::invalid_texture_directory,
                id,
                base_offset,
                std::nullopt,
                "Texture directory count is truncated or negative");
        }
        const auto count = static_cast<std::size_t>(
            static_cast<std::uint32_t>(*signed_count));
        if (count > limits_.maximum_textures) {
            return fail(
                GoldSrcBspErrorCode::count_limit_exceeded,
                id,
                base_offset,
                count,
                "Texture directory exceeds the configured texture-count limit");
        }

        std::size_t offset_table_bytes = 0U;
        std::size_t directory_bytes = 0U;
        if (!checked_multiply(count, 4U, offset_table_bytes) ||
            !checked_add(4U, offset_table_bytes, directory_bytes) ||
            directory_bytes > bytes.size()) {
            return fail(
                GoldSrcBspErrorCode::invalid_texture_directory,
                id,
                base_offset,
                count,
                "Texture offset table does not fit inside the texture lump");
        }

        std::vector<std::int32_t> offsets;
        offsets.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            const auto offset = directory_reader.read_i32_le();
            if (!offset || *offset < -1) {
                return fail(
                    GoldSrcBspErrorCode::invalid_texture_directory,
                    id,
                    base_offset + 4U + index * 4U,
                    index,
                    "Texture directory entry must be -1 or a non-negative lump-relative offset");
            }
            offsets.push_back(*offset);
        }

        textures_.reserve(count);
        for (std::size_t index = 0U; index < offsets.size(); ++index) {
            const auto signed_offset = offsets[index];
            if (signed_offset == -1) {
                textures_.push_back(TextureMetadata{});
                continue;
            }

            const auto miptex_offset = static_cast<std::size_t>(
                static_cast<std::uint32_t>(signed_offset));
            std::size_t miptex_end = 0U;
            if (miptex_offset < directory_bytes ||
                !checked_add(miptex_offset, kGoldSrcBspMiptexHeaderWireSize, miptex_end) ||
                miptex_end > bytes.size()) {
                return fail(
                    GoldSrcBspErrorCode::invalid_texture_metadata,
                    id,
                    base_offset + 4U + index * 4U,
                    index,
                    "Miptex header overlaps the directory or extends beyond the texture lump");
            }

            ByteReader miptex_reader{bytes.subspan(miptex_offset)};
            const auto name_bytes = miptex_reader.read_bytes(kGoldSrcBspTextureNameWireSize);
            const auto width = miptex_reader.read_u32_le();
            const auto height = miptex_reader.read_u32_le();
            std::array<std::uint32_t, 4U> mip_offsets{};
            bool complete_offsets = true;
            for (auto& mip_offset : mip_offsets) {
                const auto value = miptex_reader.read_u32_le();
                if (!value) {
                    complete_offsets = false;
                    break;
                }
                mip_offset = *value;
            }
            if (!name_bytes || !width || !height || !complete_offsets) {
                return fail(
                    GoldSrcBspErrorCode::invalid_texture_metadata,
                    id,
                    base_offset + miptex_offset,
                    index,
                    "Miptex metadata header is truncated");
            }

            std::size_t name_length = 0U;
            while (name_length < name_bytes->size() &&
                   (*name_bytes)[name_length] != std::byte{0}) {
                ++name_length;
            }
            if (name_length > limits_.maximum_texture_name_bytes) {
                return fail(
                    GoldSrcBspErrorCode::invalid_texture_metadata,
                    id,
                    base_offset + miptex_offset,
                    index,
                    "Miptex name exceeds the configured bounded metadata length");
            }
            std::string name;
            name.reserve(name_length);
            for (std::size_t name_index = 0U; name_index < name_length; ++name_index) {
                name.push_back(static_cast<char>(
                    std::to_integer<unsigned char>((*name_bytes)[name_index])));
            }

            if (*width == 0U || *height == 0U ||
                *width > limits_.maximum_texture_dimension ||
                *height > limits_.maximum_texture_dimension ||
                *width % kGoldSrcBspTextureDimensionGranularity != 0U ||
                *height % kGoldSrcBspTextureDimensionGranularity != 0U) {
                return fail(
                    GoldSrcBspErrorCode::invalid_texture_metadata,
                    id,
                    base_offset + miptex_offset + kGoldSrcBspTextureNameWireSize,
                    index,
                    "Miptex dimensions must be bounded positive multiples of the stock 16-texel granularity");
            }
            std::uint64_t texel_count = 0U;
            if (!checked_multiply_u64(
                    static_cast<std::uint64_t>(*width),
                    static_cast<std::uint64_t>(*height),
                    texel_count) ||
                texel_count > limits_.maximum_texture_texels) {
                return fail(
                    GoldSrcBspErrorCode::invalid_texture_metadata,
                    id,
                    base_offset + miptex_offset + kGoldSrcBspTextureNameWireSize,
                    index,
                    "Miptex dimension product exceeds the configured metadata limit");
            }

            const auto zero_offset_count = static_cast<std::size_t>(std::count(
                mip_offsets.begin(),
                mip_offsets.end(),
                0U));
            assets::WorldTextureStorage storage = assets::WorldTextureStorage::missing;
            if (zero_offset_count == mip_offsets.size()) {
                storage = assets::WorldTextureStorage::external_reference;
            } else if (zero_offset_count == 0U) {
                storage = assets::WorldTextureStorage::embedded;
                std::uint64_t previous_relative_end = kGoldSrcBspMiptexHeaderWireSize;
                for (std::size_t level = 0U; level < mip_offsets.size(); ++level) {
                    const auto relative_offset = mip_offsets[level];
                    const auto level_width = std::max(1U, *width >> level);
                    const auto level_height = std::max(1U, *height >> level);
                    std::uint64_t required_level_bytes = 0U;
                    std::uint64_t relative_end = 0U;
                    std::uint64_t absolute_offset = 0U;
                    std::uint64_t absolute_end = 0U;
                    if (!checked_multiply_u64(
                            static_cast<std::uint64_t>(level_width),
                            static_cast<std::uint64_t>(level_height),
                            required_level_bytes) ||
                        !checked_add_u64(
                            static_cast<std::uint64_t>(relative_offset),
                            required_level_bytes,
                            relative_end) ||
                        !checked_add_u64(
                            static_cast<std::uint64_t>(miptex_offset),
                            static_cast<std::uint64_t>(relative_offset),
                            absolute_offset) ||
                        !checked_add_u64(
                            absolute_offset,
                            required_level_bytes,
                            absolute_end) ||
                        static_cast<std::uint64_t>(relative_offset) <
                            previous_relative_end ||
                        absolute_end > static_cast<std::uint64_t>(bytes.size())) {
                        return fail(
                            GoldSrcBspErrorCode::invalid_texture_metadata,
                            id,
                            base_offset + miptex_offset + 24U + level * 4U,
                            index,
                            "Embedded mip byte spans must be ordered, non-overlapping, and inside the texture lump");
                    }
                    previous_relative_end = relative_end;
                }
            } else {
                return fail(
                    GoldSrcBspErrorCode::invalid_texture_metadata,
                    id,
                    base_offset + miptex_offset + 24U,
                    index,
                    "Mixed zero and non-zero mip offsets are not valid texture metadata");
            }

            textures_.push_back(TextureMetadata{
                storage,
                std::move(name),
                *width,
                *height,
            });
        }
        lump_element_counts_[goldsrc_bsp_lump_index(id)] = textures_.size();
        return true;
    }

    [[nodiscard]] bool decode_texinfo()
    {
        const auto id = GoldSrcBspLumpId::texinfo;
        ByteReader reader{lump_bytes(id)};
        const auto count = lump_element_counts_[goldsrc_bsp_lump_index(id)];
        texinfo_.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            Texinfo candidate;
            bool complete = true;
            for (auto& value : candidate.s) {
                const auto decoded = reader.read_f32_le();
                if (!decoded) {
                    complete = false;
                    break;
                }
                value = *decoded;
            }
            if (complete) {
                for (auto& value : candidate.t) {
                    const auto decoded = reader.read_f32_le();
                    if (!decoded) {
                        complete = false;
                        break;
                    }
                    value = *decoded;
                }
            }
            const auto miptex = complete ? reader.read_i32_le() : std::nullopt;
            const auto flags = complete ? reader.read_i32_le() : std::nullopt;
            if (!complete || !miptex || !flags ||
                !std::ranges::all_of(candidate.s, [](const float value) {
                    return std::isfinite(value);
                }) ||
                !std::ranges::all_of(candidate.t, [](const float value) {
                    return std::isfinite(value);
                })) {
                return fail(
                    GoldSrcBspErrorCode::invalid_float,
                    id,
                    absolute_record_offset(id, index, kGoldSrcBspTexinfoWireSize),
                    index,
                    "BSP texinfo contains a truncated or non-finite texture vector");
            }
            candidate.miptex_index = *miptex;
            candidate.flags = *flags;
            texinfo_.push_back(candidate);
        }
        return true;
    }

    [[nodiscard]] bool decode_faces()
    {
        const auto id = GoldSrcBspLumpId::faces;
        ByteReader reader{lump_bytes(id)};
        const auto count = lump_element_counts_[goldsrc_bsp_lump_index(id)];
        faces_.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            Face candidate;
            const auto plane = reader.read_i16_le();
            const auto side = reader.read_i16_le();
            const auto first_surfedge = reader.read_i32_le();
            const auto surfedge_count = reader.read_i16_le();
            const auto texinfo_index = reader.read_i16_le();
            bool complete_styles = true;
            for (auto& style : candidate.light_styles) {
                const auto value = reader.read_u8();
                if (!value) {
                    complete_styles = false;
                    break;
                }
                style = *value;
            }
            const auto light_offset = complete_styles ? reader.read_i32_le() : std::nullopt;
            if (!plane || !side || !first_surfedge || !surfedge_count ||
                !texinfo_index || !complete_styles || !light_offset) {
                return fail(
                    GoldSrcBspErrorCode::invalid_face_reference,
                    id,
                    absolute_record_offset(id, index, kGoldSrcBspFaceWireSize),
                    index,
                    "BSP face record is truncated");
            }
            candidate.plane_index = *plane;
            candidate.side = *side;
            candidate.first_surfedge = *first_surfedge;
            candidate.surfedge_count = *surfedge_count;
            candidate.texinfo_index = *texinfo_index;
            candidate.light_offset = *light_offset;
            faces_.push_back(candidate);
        }
        return true;
    }

    [[nodiscard]] bool decode_nodes()
    {
        const auto id = GoldSrcBspLumpId::nodes;
        ByteReader reader{lump_bytes(id)};
        const auto count = lump_element_counts_[goldsrc_bsp_lump_index(id)];
        nodes_.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            Node candidate;
            const auto plane = reader.read_i32_le();
            bool complete = plane.has_value();
            if (complete) {
                for (auto& child : candidate.children) {
                    const auto value = reader.read_i16_le();
                    if (!value) {
                        complete = false;
                        break;
                    }
                    child = *value;
                }
            }
            if (complete) {
                for (auto& value : candidate.minimums) {
                    const auto decoded = reader.read_i16_le();
                    if (!decoded) {
                        complete = false;
                        break;
                    }
                    value = *decoded;
                }
            }
            if (complete) {
                for (auto& value : candidate.maximums) {
                    const auto decoded = reader.read_i16_le();
                    if (!decoded) {
                        complete = false;
                        break;
                    }
                    value = *decoded;
                }
            }
            const auto first_face = complete ? reader.read_u16_le() : std::nullopt;
            const auto face_count = first_face ? reader.read_u16_le() : std::nullopt;
            if (!plane || !complete || !first_face || !face_count) {
                return fail(
                    GoldSrcBspErrorCode::invalid_node_reference,
                    id,
                    absolute_record_offset(id, index, kGoldSrcBspNodeWireSize),
                    index,
                    "BSP node record is truncated");
            }
            candidate.plane_index = *plane;
            candidate.first_face = *first_face;
            candidate.face_count = *face_count;
            nodes_.push_back(candidate);
        }
        return true;
    }

    [[nodiscard]] bool decode_leaves()
    {
        const auto id = GoldSrcBspLumpId::leaves;
        ByteReader reader{lump_bytes(id)};
        const auto count = lump_element_counts_[goldsrc_bsp_lump_index(id)];
        leaves_.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            Leaf candidate;
            const auto contents = reader.read_i32_le();
            const auto visibility_offset = reader.read_i32_le();
            bool complete = contents.has_value() && visibility_offset.has_value();
            if (complete) {
                for (auto& value : candidate.minimums) {
                    const auto decoded = reader.read_i16_le();
                    if (!decoded) {
                        complete = false;
                        break;
                    }
                    value = *decoded;
                }
            }
            if (complete) {
                for (auto& value : candidate.maximums) {
                    const auto decoded = reader.read_i16_le();
                    if (!decoded) {
                        complete = false;
                        break;
                    }
                    value = *decoded;
                }
            }
            const auto first_marksurface = complete ? reader.read_u16_le() : std::nullopt;
            const auto marksurface_count = first_marksurface ? reader.read_u16_le() : std::nullopt;
            bool complete_ambient = marksurface_count.has_value();
            if (complete_ambient) {
                for (auto& level : candidate.ambient_levels) {
                    const auto decoded = reader.read_u8();
                    if (!decoded) {
                        complete_ambient = false;
                        break;
                    }
                    level = *decoded;
                }
            }
            if (!contents || !visibility_offset || !complete || !first_marksurface ||
                !marksurface_count || !complete_ambient) {
                return fail(
                    GoldSrcBspErrorCode::invalid_leaf_reference,
                    id,
                    absolute_record_offset(id, index, kGoldSrcBspLeafWireSize),
                    index,
                    "BSP leaf record is truncated");
            }
            candidate.contents = *contents;
            candidate.visibility_offset = *visibility_offset;
            candidate.first_marksurface = *first_marksurface;
            candidate.marksurface_count = *marksurface_count;
            leaves_.push_back(candidate);
        }
        return true;
    }

    [[nodiscard]] bool decode_marksurfaces()
    {
        const auto id = GoldSrcBspLumpId::marksurfaces;
        ByteReader reader{lump_bytes(id)};
        const auto count = lump_element_counts_[goldsrc_bsp_lump_index(id)];
        marksurfaces_.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            const auto value = reader.read_u16_le();
            if (!value) {
                return fail(
                    GoldSrcBspErrorCode::invalid_marksurface_reference,
                    id,
                    absolute_record_offset(id, index, kGoldSrcBspMarksurfaceWireSize),
                    index,
                    "BSP marksurface record is truncated");
            }
            marksurfaces_.push_back(*value);
        }
        return true;
    }

    [[nodiscard]] bool decode_clipnodes()
    {
        const auto id = GoldSrcBspLumpId::clipnodes;
        ByteReader reader{lump_bytes(id)};
        const auto count = lump_element_counts_[goldsrc_bsp_lump_index(id)];
        clipnodes_.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            Clipnode candidate;
            const auto plane = reader.read_i32_le();
            const auto first_child = reader.read_i16_le();
            const auto second_child = reader.read_i16_le();
            if (!plane || !first_child || !second_child) {
                return fail(
                    GoldSrcBspErrorCode::invalid_clipnode_reference,
                    id,
                    absolute_record_offset(id, index, kGoldSrcBspClipnodeWireSize),
                    index,
                    "BSP clipnode record is truncated");
            }
            candidate.plane_index = *plane;
            candidate.children = {*first_child, *second_child};
            clipnodes_.push_back(candidate);
        }
        return true;
    }

    [[nodiscard]] bool decode_models()
    {
        const auto id = GoldSrcBspLumpId::models;
        ByteReader reader{lump_bytes(id)};
        const auto count = lump_element_counts_[goldsrc_bsp_lump_index(id)];
        if (count == 0U) {
            return fail(
                GoldSrcBspErrorCode::invalid_model_reference,
                id,
                lumps_[goldsrc_bsp_lump_index(id)].offset,
                std::nullopt,
                "BSP models lump must contain world model 0");
        }
        models_.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            Model candidate;
            const auto minimums = read_vector3(reader);
            const auto maximums = read_vector3(reader);
            const auto origin = read_vector3(reader);
            bool complete = minimums.has_value() && maximums.has_value() && origin.has_value();
            if (complete) {
                for (auto& headnode : candidate.headnodes) {
                    const auto decoded = reader.read_i32_le();
                    if (!decoded) {
                        complete = false;
                        break;
                    }
                    headnode = *decoded;
                }
            }
            const auto visible_leaf_count = complete ? reader.read_i32_le() : std::nullopt;
            const auto first_face = visible_leaf_count ? reader.read_i32_le() : std::nullopt;
            const auto face_count = first_face ? reader.read_i32_le() : std::nullopt;
            if (!minimums || !maximums || !origin || !complete || !visible_leaf_count ||
                !first_face || !face_count || !finite_vector(*minimums) ||
                !finite_vector(*maximums) || !finite_vector(*origin)) {
                return fail(
                    GoldSrcBspErrorCode::invalid_float,
                    id,
                    absolute_record_offset(id, index, kGoldSrcBspModelWireSize),
                    index,
                    "BSP model contains a truncated or non-finite source field");
            }
            candidate.minimums = *minimums;
            candidate.maximums = *maximums;
            candidate.origin = *origin;
            candidate.visible_leaf_count = *visible_leaf_count;
            candidate.first_face = *first_face;
            candidate.face_count = *face_count;
            models_.push_back(candidate);
        }
        return true;
    }

    [[nodiscard]] bool valid_face_range(
        const std::size_t first,
        const std::size_t count) const noexcept
    {
        std::size_t end = 0U;
        return checked_add(first, count, end) && end <= faces_.size();
    }

    [[nodiscard]] bool validate_references()
    {
        for (std::size_t index = 0U; index < edges_.size(); ++index) {
            const auto& edge = edges_[index];
            if (static_cast<std::size_t>(edge.vertex_indices[0U]) >= vertices_.size() ||
                static_cast<std::size_t>(edge.vertex_indices[1U]) >= vertices_.size()) {
                return fail(
                    GoldSrcBspErrorCode::invalid_vertex_reference,
                    GoldSrcBspLumpId::edges,
                    absolute_record_offset(
                        GoldSrcBspLumpId::edges,
                        index,
                        kGoldSrcBspEdgeWireSize),
                    index,
                    "BSP edge references a vertex outside the vertex lump");
            }
        }

        for (std::size_t index = 0U; index < surfedges_.size(); ++index) {
            const auto value = surfedges_[index];
            if (value == 0 || value == std::numeric_limits<std::int32_t>::min()) {
                return fail(
                    GoldSrcBspErrorCode::invalid_surfedge_reference,
                    GoldSrcBspLumpId::surfedges,
                    absolute_record_offset(
                        GoldSrcBspLumpId::surfedges,
                        index,
                        kGoldSrcBspSurfedgeWireSize),
                    index,
                    "Surfedge zero is the stock edge-0 sentinel and INT32_MIN cannot be oriented");
            }
            const auto oriented_index = value < 0
                                            ? static_cast<std::uint64_t>(
                                                  -static_cast<std::int64_t>(value))
                                            : static_cast<std::uint64_t>(value);
            if (oriented_index >= static_cast<std::uint64_t>(edges_.size())) {
                return fail(
                    GoldSrcBspErrorCode::invalid_edge_reference,
                    GoldSrcBspLumpId::surfedges,
                    absolute_record_offset(
                        GoldSrcBspLumpId::surfedges,
                        index,
                        kGoldSrcBspSurfedgeWireSize),
                    index,
                    "BSP surfedge references an edge outside the edge lump");
            }
        }

        for (std::size_t index = 0U; index < texinfo_.size(); ++index) {
            const auto miptex = texinfo_[index].miptex_index;
            if (miptex < 0 ||
                static_cast<std::uint64_t>(miptex) >=
                    static_cast<std::uint64_t>(textures_.size())) {
                return fail(
                    GoldSrcBspErrorCode::invalid_texinfo_reference,
                    GoldSrcBspLumpId::texinfo,
                    absolute_record_offset(
                        GoldSrcBspLumpId::texinfo,
                        index,
                        kGoldSrcBspTexinfoWireSize) +
                        32U,
                    index,
                    "BSP texinfo references a texture-directory ordinal outside the lump");
            }
        }

        const auto lighting_size =
            lumps_[goldsrc_bsp_lump_index(GoldSrcBspLumpId::lighting)].length;
        for (std::size_t index = 0U; index < faces_.size(); ++index) {
            const auto& face = faces_[index];
            const auto record_offset = absolute_record_offset(
                GoldSrcBspLumpId::faces,
                index,
                kGoldSrcBspFaceWireSize);
            if (face.plane_index < 0 ||
                static_cast<std::size_t>(face.plane_index) >= planes_.size() ||
                (face.side != 0 && face.side != 1)) {
                return fail(
                    GoldSrcBspErrorCode::invalid_face_reference,
                    GoldSrcBspLumpId::faces,
                    record_offset,
                    index,
                    "BSP face plane index or side flag is invalid");
            }
            if (face.first_surfedge < 0 || face.surfedge_count < 3) {
                return fail(
                    GoldSrcBspErrorCode::invalid_face_reference,
                    GoldSrcBspLumpId::faces,
                    record_offset + 4U,
                    index,
                    "BSP face surfedge start is negative or has fewer than three edges");
            }
            if (static_cast<std::size_t>(face.surfedge_count) >
                limits_.maximum_face_edges) {
                return fail(
                    GoldSrcBspErrorCode::count_limit_exceeded,
                    GoldSrcBspLumpId::faces,
                    record_offset + 8U,
                    index,
                    "BSP face edge count exceeds the configured compatibility limit");
            }
            const auto first_surfedge = static_cast<std::size_t>(
                static_cast<std::uint32_t>(face.first_surfedge));
            const auto surfedge_count = static_cast<std::size_t>(face.surfedge_count);
            std::size_t surfedge_end = 0U;
            if (!checked_add(first_surfedge, surfedge_count, surfedge_end) ||
                surfedge_end > surfedges_.size()) {
                return fail(
                    GoldSrcBspErrorCode::invalid_face_reference,
                    GoldSrcBspLumpId::faces,
                    record_offset + 4U,
                    index,
                    "BSP face surfedge range extends outside the surfedge lump");
            }
            if (face.texinfo_index < 0 ||
                static_cast<std::size_t>(face.texinfo_index) >= texinfo_.size()) {
                return fail(
                    GoldSrcBspErrorCode::invalid_texinfo_reference,
                    GoldSrcBspLumpId::faces,
                    record_offset + 10U,
                    index,
                    "BSP face references texinfo outside the texinfo lump");
            }
            if (face.light_offset < -1 ||
                (face.light_offset >= 0 &&
                 static_cast<std::uint64_t>(face.light_offset) >=
                     static_cast<std::uint64_t>(lighting_size))) {
                return fail(
                    GoldSrcBspErrorCode::invalid_light_offset,
                    GoldSrcBspLumpId::faces,
                    record_offset + 16U,
                    index,
                    "BSP face light offset is neither -1 nor inside the lighting lump");
            }
        }

        for (std::size_t index = 0U; index < nodes_.size(); ++index) {
            const auto& node = nodes_[index];
            const auto record_offset = absolute_record_offset(
                GoldSrcBspLumpId::nodes,
                index,
                kGoldSrcBspNodeWireSize);
            if (node.plane_index < 0 ||
                static_cast<std::size_t>(node.plane_index) >= planes_.size()) {
                return fail(
                    GoldSrcBspErrorCode::invalid_node_reference,
                    GoldSrcBspLumpId::nodes,
                    record_offset,
                    index,
                    "BSP node references a plane outside the plane lump");
            }
            for (std::size_t axis = 0U; axis < 3U; ++axis) {
                if (node.minimums[axis] > node.maximums[axis]) {
                    return fail(
                        GoldSrcBspErrorCode::invalid_node_reference,
                        GoldSrcBspLumpId::nodes,
                        record_offset + 8U,
                        index,
                        "BSP node integer bounds are inverted");
                }
            }
            for (const auto child : node.children) {
                if (child >= 0) {
                    if (static_cast<std::size_t>(child) >= nodes_.size()) {
                        return fail(
                            GoldSrcBspErrorCode::invalid_node_reference,
                            GoldSrcBspLumpId::nodes,
                            record_offset + 4U,
                            index,
                            "BSP node child references a node outside the node lump");
                    }
                } else {
                    const auto leaf_index = static_cast<std::size_t>(
                        -static_cast<std::int32_t>(child) - 1);
                    if (leaf_index >= leaves_.size()) {
                        return fail(
                            GoldSrcBspErrorCode::invalid_node_reference,
                            GoldSrcBspLumpId::nodes,
                            record_offset + 4U,
                            index,
                            "BSP negative node child references a leaf outside the leaf lump");
                    }
                }
            }
            if (!valid_face_range(
                    static_cast<std::size_t>(node.first_face),
                    static_cast<std::size_t>(node.face_count))) {
                return fail(
                    GoldSrcBspErrorCode::invalid_face_reference,
                    GoldSrcBspLumpId::nodes,
                    record_offset + 20U,
                    index,
                    "BSP node face range extends outside the face lump");
            }
        }

        const auto visibility_size =
            lumps_[goldsrc_bsp_lump_index(GoldSrcBspLumpId::visibility)].length;
        for (std::size_t index = 0U; index < leaves_.size(); ++index) {
            const auto& leaf = leaves_[index];
            const auto record_offset = absolute_record_offset(
                GoldSrcBspLumpId::leaves,
                index,
                kGoldSrcBspLeafWireSize);
            if (!supported_contents(leaf.contents)) {
                return fail(
                    GoldSrcBspErrorCode::invalid_leaf_reference,
                    GoldSrcBspLumpId::leaves,
                    record_offset,
                    index,
                    "BSP leaf contents value is outside the supported GoldSrc domain");
            }
            for (std::size_t axis = 0U; axis < 3U; ++axis) {
                if (leaf.minimums[axis] > leaf.maximums[axis]) {
                    return fail(
                        GoldSrcBspErrorCode::invalid_leaf_reference,
                        GoldSrcBspLumpId::leaves,
                        record_offset + 8U,
                        index,
                        "BSP leaf integer bounds are inverted");
                }
            }
            if (leaf.visibility_offset < -1 ||
                (leaf.visibility_offset >= 0 &&
                 static_cast<std::uint64_t>(leaf.visibility_offset) >=
                     static_cast<std::uint64_t>(visibility_size))) {
                return fail(
                    GoldSrcBspErrorCode::invalid_leaf_reference,
                    GoldSrcBspLumpId::leaves,
                    record_offset + 4U,
                    index,
                    "BSP leaf visibility offset is neither -1 nor inside the visibility lump");
            }
            std::size_t marksurface_end = 0U;
            if (!checked_add(
                    static_cast<std::size_t>(leaf.first_marksurface),
                    static_cast<std::size_t>(leaf.marksurface_count),
                    marksurface_end) ||
                marksurface_end > marksurfaces_.size()) {
                return fail(
                    GoldSrcBspErrorCode::invalid_leaf_reference,
                    GoldSrcBspLumpId::leaves,
                    record_offset + 20U,
                    index,
                    "BSP leaf marksurface range extends outside the marksurface lump");
            }
        }

        for (std::size_t index = 0U; index < marksurfaces_.size(); ++index) {
            if (static_cast<std::size_t>(marksurfaces_[index]) >= faces_.size()) {
                return fail(
                    GoldSrcBspErrorCode::invalid_marksurface_reference,
                    GoldSrcBspLumpId::marksurfaces,
                    absolute_record_offset(
                        GoldSrcBspLumpId::marksurfaces,
                        index,
                        kGoldSrcBspMarksurfaceWireSize),
                    index,
                    "BSP marksurface references a face outside the face lump");
            }
        }

        for (std::size_t index = 0U; index < clipnodes_.size(); ++index) {
            const auto& clipnode = clipnodes_[index];
            const auto record_offset = absolute_record_offset(
                GoldSrcBspLumpId::clipnodes,
                index,
                kGoldSrcBspClipnodeWireSize);
            if (clipnode.plane_index < 0 ||
                static_cast<std::size_t>(clipnode.plane_index) >= planes_.size()) {
                return fail(
                    GoldSrcBspErrorCode::invalid_clipnode_reference,
                    GoldSrcBspLumpId::clipnodes,
                    record_offset,
                    index,
                    "BSP clipnode references a plane outside the plane lump");
            }
            for (const auto child : clipnode.children) {
                if (child >= 0) {
                    if (static_cast<std::size_t>(child) >= clipnodes_.size()) {
                        return fail(
                            GoldSrcBspErrorCode::invalid_clipnode_reference,
                            GoldSrcBspLumpId::clipnodes,
                            record_offset + 4U,
                            index,
                            "BSP clipnode child references a clipnode outside the lump");
                    }
                } else if (!supported_contents(static_cast<std::int32_t>(child))) {
                    return fail(
                        GoldSrcBspErrorCode::invalid_clipnode_reference,
                        GoldSrcBspLumpId::clipnodes,
                        record_offset + 4U,
                        index,
                        "BSP clipnode negative child is outside supported contents values");
                }
            }
        }

        for (std::size_t index = 0U; index < models_.size(); ++index) {
            const auto& model = models_[index];
            const auto record_offset = absolute_record_offset(
                GoldSrcBspLumpId::models,
                index,
                kGoldSrcBspModelWireSize);
            if (model.minimums.x > model.maximums.x ||
                model.minimums.y > model.maximums.y ||
                model.minimums.z > model.maximums.z ||
                model.first_face < 0 || model.face_count < 0 ||
                !valid_face_range(
                    static_cast<std::size_t>(static_cast<std::uint32_t>(model.first_face)),
                    static_cast<std::size_t>(static_cast<std::uint32_t>(model.face_count)))) {
                return fail(
                    GoldSrcBspErrorCode::invalid_model_reference,
                    GoldSrcBspLumpId::models,
                    record_offset,
                    index,
                    "BSP model bounds or face range is invalid");
            }
            const auto first_face = static_cast<std::size_t>(
                static_cast<std::uint32_t>(model.first_face));
            const auto face_count = static_cast<std::size_t>(
                static_cast<std::uint32_t>(model.face_count));
            const auto face_end = first_face + face_count;
            for (std::size_t previous_index = 0U;
                 previous_index < index;
                 ++previous_index) {
                const auto& previous = models_[previous_index];
                const auto previous_first = static_cast<std::size_t>(
                    static_cast<std::uint32_t>(previous.first_face));
                const auto previous_count = static_cast<std::size_t>(
                    static_cast<std::uint32_t>(previous.face_count));
                const auto previous_end = previous_first + previous_count;
                if (face_count != 0U && previous_count != 0U &&
                    first_face < previous_end && previous_first < face_end) {
                    return fail(
                        GoldSrcBspErrorCode::invalid_model_reference,
                        GoldSrcBspLumpId::models,
                        record_offset + 56U,
                        index,
                        "BSP model render-face range overlaps an earlier model");
                }
            }
            if (model.visible_leaf_count < 0 ||
                static_cast<std::uint64_t>(model.visible_leaf_count) >
                    static_cast<std::uint64_t>(leaves_.size())) {
                return fail(
                    GoldSrcBspErrorCode::invalid_model_reference,
                    GoldSrcBspLumpId::models,
                    record_offset + 52U,
                    index,
                    "BSP model visible-leaf count is outside the leaf profile");
            }

            const auto draw_headnode = model.headnodes[0U];
            if (draw_headnode < 0 ||
                static_cast<std::uint64_t>(draw_headnode) >=
                    static_cast<std::uint64_t>(nodes_.size())) {
                return fail(
                    GoldSrcBspErrorCode::invalid_model_reference,
                    GoldSrcBspLumpId::models,
                    record_offset + 36U,
                    index,
                    "BSP model draw headnode references outside the node lump");
            }
            for (std::size_t hull = 1U; hull < model.headnodes.size(); ++hull) {
                const auto headnode = model.headnodes[hull];
                if (headnode >= 0) {
                    if (static_cast<std::uint64_t>(headnode) >=
                        static_cast<std::uint64_t>(clipnodes_.size())) {
                        return fail(
                            GoldSrcBspErrorCode::invalid_model_reference,
                            GoldSrcBspLumpId::models,
                            record_offset + 36U + hull * 4U,
                            index,
                            "BSP model collision headnode references outside the clipnode lump");
                    }
                } else if (!supported_contents(headnode)) {
                    return fail(
                        GoldSrcBspErrorCode::invalid_model_reference,
                        GoldSrcBspLumpId::models,
                        record_offset + 36U + hull * 4U,
                        index,
                        "BSP model collision headnode has unsupported contents metadata");
                }
            }
        }
        return true;
    }

    [[nodiscard]] std::optional<assets::WorldAsset> build_model_geometry(
        const std::size_t model_index,
        std::size_t& polygon_edge_pair_tests,
        const ModelGeometryOutputLimits& output_limits)
    {
        const auto& source_model = models_[model_index];
        const auto first_face = static_cast<std::size_t>(
            static_cast<std::uint32_t>(source_model.first_face));
        const auto face_count = static_cast<std::size_t>(
            static_cast<std::uint32_t>(source_model.face_count));
        if (face_count == 0U) {
            static_cast<void>(fail(
                GoldSrcBspErrorCode::degenerate_face,
                GoldSrcBspLumpId::models,
                absolute_record_offset(
                    GoldSrcBspLumpId::models,
                    model_index,
                    kGoldSrcBspModelWireSize) + 60U,
                model_index,
                "BSP render model contains no source faces"));
            return std::nullopt;
        }
        if (face_count > output_limits.maximum_surfaces) {
            static_cast<void>(fail(
                GoldSrcBspErrorCode::geometry_limit_exceeded,
                GoldSrcBspLumpId::models,
                absolute_record_offset(
                    GoldSrcBspLumpId::models,
                    model_index,
                    kGoldSrcBspModelWireSize) + 60U,
                model_index,
                "BSP model surface count exceeds the configured output limit"));
            return std::nullopt;
        }

        std::vector<GoldSrcFaceGeometryCandidate> candidates;
        candidates.reserve(face_count);
        std::vector<std::uint8_t> used_texinfo(texinfo_.size(), 0U);
        std::size_t output_vertex_count = 0U;
        std::size_t output_index_count = 0U;
        std::size_t output_triangle_count = 0U;
        std::size_t material_count = 0U;
        std::uint64_t removed_collinear_corner_count = 0U;
        auto minimum_winding_margin = std::numeric_limits<double>::infinity();

        for (std::size_t local_face = 0U; local_face < face_count; ++local_face) {
            const auto source_face_index = first_face + local_face;
            const auto& face = faces_[source_face_index];
            const auto& plane =
                planes_[static_cast<std::size_t>(face.plane_index)];
            const auto& source_texinfo =
                texinfo_[static_cast<std::size_t>(face.texinfo_index)];
            const auto remaining_pair_tests =
                polygon_edge_pair_tests <=
                        limits_.maximum_polygon_edge_pair_tests
                    ? limits_.maximum_polygon_edge_pair_tests -
                          polygon_edge_pair_tests
                    : 0U;
            auto built = GoldSrcFaceGeometryBuilder::build(
                GoldSrcFaceGeometryBuildInput{
                    static_cast<std::uint32_t>(model_index),
                    static_cast<std::uint32_t>(source_face_index),
                    GoldSrcFaceGeometrySourcePlane{
                        plane.normal,
                        plane.distance,
                    },
                    GoldSrcFaceGeometrySourceFace{
                        static_cast<std::uint32_t>(face.plane_index),
                        face.side,
                        face.first_surfedge,
                        face.surfedge_count,
                    },
                    edges_,
                    surfedges_,
                    vertices_,
                    GoldSrcFaceGeometrySourceTexinfo{
                        source_texinfo.s,
                        source_texinfo.t,
                    },
                    GoldSrcFaceGeometryLimits{
                        limits_.maximum_face_edges,
                        remaining_pair_tests,
                    },
                    GoldSrcFaceOrientationCompatibilityProfile::
                        valve_qbsp_clockwise_wire_to_counter_clockwise_render,
                });
            if (built.polygon_edge_pair_test_count > remaining_pair_tests) {
                static_cast<void>(fail(
                    GoldSrcBspErrorCode::geometry_limit_exceeded,
                    GoldSrcBspLumpId::faces,
                    absolute_record_offset(
                        GoldSrcBspLumpId::faces,
                        source_face_index,
                        kGoldSrcBspFaceWireSize),
                    source_face_index,
                    "Face geometry exceeded its aggregate validation-work budget"));
                return std::nullopt;
            }
            polygon_edge_pair_tests += built.polygon_edge_pair_test_count;
            if (!built || !built.candidate) {
                if (built.error) {
                    static_cast<void>(fail_face_geometry(
                        *built.error,
                        absolute_record_offset(
                            GoldSrcBspLumpId::faces,
                            source_face_index,
                            kGoldSrcBspFaceWireSize)));
                } else {
                    static_cast<void>(fail(
                        GoldSrcBspErrorCode::unable_to_retain_world,
                        GoldSrcBspLumpId::faces,
                        absolute_record_offset(
                            GoldSrcBspLumpId::faces,
                            source_face_index,
                            kGoldSrcBspFaceWireSize),
                        source_face_index,
                        "Face geometry builder returned no candidate or error"));
                }
                return std::nullopt;
            }

            auto candidate = std::move(*built.candidate);
            std::size_t next_triangle_count = 0U;
            if (!checked_add(
                    output_vertex_count,
                    candidate.corners.size(),
                    output_vertex_count) ||
                !checked_add(
                    output_index_count,
                    candidate.local_triangle_indices.size(),
                    output_index_count) ||
                !checked_add(
                    output_triangle_count,
                    candidate.local_triangle_indices.size() / 3U,
                    next_triangle_count)) {
                static_cast<void>(fail(
                    GoldSrcBspErrorCode::geometry_limit_exceeded,
                    GoldSrcBspLumpId::faces,
                    absolute_record_offset(
                        GoldSrcBspLumpId::faces,
                        source_face_index,
                        kGoldSrcBspFaceWireSize),
                    source_face_index,
                    "Checked face geometry output arithmetic overflowed"));
                return std::nullopt;
            }
            output_triangle_count = next_triangle_count;
            const auto source_texinfo_index =
                static_cast<std::size_t>(face.texinfo_index);
            if (used_texinfo[source_texinfo_index] == 0U) {
                used_texinfo[source_texinfo_index] = 1U;
                ++material_count;
            }

            if (output_vertex_count > output_limits.maximum_vertices ||
                output_vertex_count > static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max()) ||
                output_index_count > output_limits.maximum_indices ||
                output_index_count > static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max()) ||
                material_count > output_limits.maximum_materials ||
                material_count > static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max())) {
                static_cast<void>(fail(
                    GoldSrcBspErrorCode::geometry_limit_exceeded,
                    GoldSrcBspLumpId::faces,
                    absolute_record_offset(
                        GoldSrcBspLumpId::faces,
                        source_face_index,
                        kGoldSrcBspFaceWireSize),
                    source_face_index,
                    "BSP model geometry or material output exceeds configured limits"));
                return std::nullopt;
            }

            const auto& diagnostic = candidate.diagnostic;
            const auto positive_surfedges =
                static_cast<std::uint64_t>(diagnostic.positive_surfedge_count);
            const auto negative_surfedges =
                static_cast<std::uint64_t>(diagnostic.negative_surfedge_count);
            geometry_statistics_.positive_surfedge_count += positive_surfedges;
            geometry_statistics_.negative_surfedge_count += negative_surfedges;
            if (negative_surfedges != 0U) {
                ++geometry_statistics_.faces_with_negative_surfedges;
            }
            if (negative_surfedges != 0U && positive_surfedges != 0U) {
                ++geometry_statistics_.faces_with_mixed_surfedge_signs;
            }
            if (face.side == 0) {
                ++geometry_statistics_.side_zero_face_count;
            } else {
                ++geometry_statistics_.side_one_face_count;
            }
            if (diagnostic.signed_area_normal_dot <
                -diagnostic.polygon_area_tolerance) {
                ++geometry_statistics_.negative_wire_area_dot_count;
            } else if (diagnostic.signed_area_normal_dot >
                       diagnostic.polygon_area_tolerance) {
                ++geometry_statistics_.positive_wire_area_dot_count;
            } else {
                ++geometry_statistics_.ambiguous_wire_area_dot_count;
            }
            ++geometry_statistics_.canonicalized_face_count;
            if (diagnostic.removed_collinear_corner_count != 0U) {
                ++geometry_statistics_.collinear_canonicalized_face_count;
            }
            geometry_statistics_.removed_collinear_corner_count +=
                diagnostic.removed_collinear_corner_count;
            if (model_index != 0U) {
                ++geometry_statistics_.brush_canonicalized_face_count;
            }
            geometry_statistics_.maximum_planarity_deviation = std::max(
                geometry_statistics_.maximum_planarity_deviation,
                diagnostic.maximum_planarity_deviation);
            minimum_winding_margin = std::min(
                minimum_winding_margin,
                diagnostic.minimum_triangle_winding_dot);
            geometry_statistics_.minimum_accepted_winding_margin =
                geometry_statistics_.minimum_accepted_winding_margin == 0.0
                    ? diagnostic.minimum_triangle_winding_dot
                    : std::min(
                          geometry_statistics_.minimum_accepted_winding_margin,
                          diagnostic.minimum_triangle_winding_dot);
            removed_collinear_corner_count +=
                diagnostic.removed_collinear_corner_count;
            candidates.push_back(std::move(candidate));
        }

        assets::WorldAsset world;
        world.coordinate_space =
            assets::WorldCoordinateSpace::source_native_goldsrc_z_up;
        world.texture_coordinate_space =
            assets::WorldTextureCoordinateSpace::texel_units;
        world.source_profile = assets::WorldGeometrySourceProfile::goldsrc_bsp_v30;
        world.source_model_bounds = assets::WorldBounds{
            source_model.minimums,
            source_model.maximums,
        };
        world.vertices.reserve(output_vertex_count);
        world.indices.reserve(output_index_count);
        world.surfaces.reserve(face_count);
        world.materials.reserve(material_count);

        constexpr auto unassigned_material =
            std::numeric_limits<std::size_t>::max();
        std::vector<std::size_t> material_by_texinfo(
            texinfo_.size(),
            unassigned_material);
        bool has_world_bounds = false;
        assets::WorldBounds world_bounds{};

        for (std::size_t local_face = 0U; local_face < face_count; ++local_face) {
            const auto source_face_index = first_face + local_face;
            const auto& face = faces_[source_face_index];
            const auto& candidate = candidates[local_face];
            const auto source_texinfo_index =
                static_cast<std::size_t>(face.texinfo_index);
            const auto& source_texinfo = texinfo_[source_texinfo_index];

            auto material_index = material_by_texinfo[source_texinfo_index];
            if (material_index == unassigned_material) {
                const auto texture_index =
                    static_cast<std::size_t>(source_texinfo.miptex_index);
                const auto& texture = textures_[texture_index];
                material_index = world.materials.size();
                material_by_texinfo[source_texinfo_index] = material_index;
                world.materials.push_back(assets::WorldMaterialReference{
                    texture.name,
                    texture.width,
                    texture.height,
                    texture.storage,
                    source_texinfo.flags,
                    static_cast<std::uint32_t>(source_texinfo_index),
                    static_cast<std::uint32_t>(texture_index),
                    assets::WorldMaterialCompatibilityProfile::
                        source_texture_reference_v1,
                    assets::WorldMaterialEvidenceProfile::
                        validated_source_metadata,
                });
            }

            const auto first_output_vertex = world.vertices.size();
            const auto first_output_index = world.indices.size();
            for (const auto& corner : candidate.corners) {
                world.vertices.push_back(assets::WorldVertex{
                    corner.position,
                    candidate.emitted_face_normal,
                    corner.texture_coordinate,
                });
                if (!has_world_bounds) {
                    world_bounds =
                        assets::WorldBounds{corner.position, corner.position};
                    has_world_bounds = true;
                } else {
                    world_bounds.minimum.x =
                        std::min(world_bounds.minimum.x, corner.position.x);
                    world_bounds.minimum.y =
                        std::min(world_bounds.minimum.y, corner.position.y);
                    world_bounds.minimum.z =
                        std::min(world_bounds.minimum.z, corner.position.z);
                    world_bounds.maximum.x =
                        std::max(world_bounds.maximum.x, corner.position.x);
                    world_bounds.maximum.y =
                        std::max(world_bounds.maximum.y, corner.position.y);
                    world_bounds.maximum.z =
                        std::max(world_bounds.maximum.z, corner.position.z);
                }
            }
            for (const auto local_index : candidate.local_triangle_indices) {
                world.indices.push_back(static_cast<std::uint32_t>(
                    first_output_vertex + local_index));
            }
            world.surfaces.push_back(assets::WorldSurface{
                static_cast<std::uint32_t>(first_output_index),
                static_cast<std::uint32_t>(
                    candidate.local_triangle_indices.size()),
                static_cast<std::uint32_t>(material_index),
                candidate.bounds,
                static_cast<std::uint32_t>(source_face_index),
                face.light_offset >= 0
                    ? std::optional{
                          static_cast<std::uint32_t>(face.light_offset)}
                    : std::nullopt,
                face.light_styles,
                (static_cast<std::uint32_t>(source_texinfo.flags) &
                 kGoldSrcBspTexSpecialFlag) != 0U,
                static_cast<std::uint32_t>(first_output_vertex),
                static_cast<std::uint32_t>(candidate.corners.size()),
            });
        }

        if (!has_world_bounds || world.vertices.size() != output_vertex_count ||
            world.indices.size() != output_index_count ||
            world.surfaces.size() != face_count ||
            world.materials.size() != material_count) {
            static_cast<void>(fail(
                GoldSrcBspErrorCode::unable_to_retain_world,
                GoldSrcBspLumpId::models,
                absolute_record_offset(
                    GoldSrcBspLumpId::models,
                    model_index,
                    kGoldSrcBspModelWireSize),
                model_index,
                "Transactional BSP model construction produced inconsistent owning counts"));
            return std::nullopt;
        }
        world.bounds = world_bounds;
        world.statistics = assets::WorldGeometryStatistics{
            kGoldSrcBspVersion,
            static_cast<std::uint64_t>(models_.size()),
            static_cast<std::uint64_t>(faces_.size()),
            model_index == 0U ? static_cast<std::uint64_t>(face_count) : 0U,
            static_cast<std::uint64_t>(faces_.size() - face_count),
            static_cast<std::uint64_t>(world.surfaces.size()),
            static_cast<std::uint64_t>(world.vertices.size()),
            static_cast<std::uint64_t>(output_triangle_count),
            static_cast<std::uint64_t>(world.materials.size()),
            0U,
            0U,
            0U,
            0U,
            static_cast<std::uint64_t>(face_count),
            0U,
            removed_collinear_corner_count,
            std::isfinite(minimum_winding_margin)
                ? minimum_winding_margin
                : 0.0,
            model_index == 0U ? 0U : static_cast<std::uint64_t>(face_count),
        };
        for (const auto& material : world.materials) {
            switch (material.texture_storage) {
            case assets::WorldTextureStorage::missing:
                ++world.statistics.missing_texture_reference_count;
                break;
            case assets::WorldTextureStorage::external_reference:
                ++world.statistics.external_texture_reference_count;
                break;
            case assets::WorldTextureStorage::embedded:
                ++world.statistics.embedded_texture_reference_count;
                break;
            }
        }
        return world;
    }
    [[nodiscard]] std::optional<GoldSrcBspParsedDocument> build_world()
    {
        const auto& world_model = models_.front();
        geometry_statistics_ = GoldSrcBspGeometryStatistics{};
        geometry_statistics_.source_model_count =
            static_cast<std::uint64_t>(models_.size());
        geometry_statistics_.world_face_count =
            static_cast<std::uint64_t>(world_model.face_count);
        for (std::size_t model_index = 1U;
             model_index < models_.size();
             ++model_index) {
            geometry_statistics_.brush_face_count += static_cast<std::uint64_t>(
                models_[model_index].face_count);
        }
        std::size_t polygon_edge_pair_tests = 0U;
        auto world = build_model_geometry(
            0U,
            polygon_edge_pair_tests,
            ModelGeometryOutputLimits{
                limits_.maximum_output_vertices,
                limits_.maximum_output_indices,
                limits_.maximum_output_surfaces,
                limits_.maximum_output_materials,
            });
        if (!world) {
            return std::nullopt;
        }
        const auto source_fingerprint = goldsrc_bsp_source_fingerprint(source_);
        world->source_content_fingerprint = source_fingerprint;

        std::size_t retained_vertex_count = world->vertices.size();
        std::size_t retained_index_count = world->indices.size();
        std::size_t retained_surface_count = world->surfaces.size();
        std::size_t retained_material_count = world->materials.size();
        std::vector<GoldSrcBspBrushSubmodelAsset> brush_submodels;
        if (materialize_brush_submodels_) {
            brush_submodels.reserve(models_.size() - 1U);
            for (std::size_t model_index = 1U;
                 model_index < models_.size();
                 ++model_index) {
                const auto remaining_vertices =
                    limits_.maximum_output_vertices - retained_vertex_count;
                const auto remaining_indices =
                    limits_.maximum_output_indices - retained_index_count;
                const auto remaining_surfaces =
                    limits_.maximum_output_surfaces - retained_surface_count;
                const auto remaining_materials =
                    limits_.maximum_output_materials - retained_material_count;
                auto geometry = build_model_geometry(
                    model_index,
                    polygon_edge_pair_tests,
                    ModelGeometryOutputLimits{
                        remaining_vertices,
                        remaining_indices,
                        remaining_surfaces,
                        remaining_materials,
                    });
                if (!geometry) {
                    if (error_) {
                        error_->source_model_index =
                            static_cast<std::uint32_t>(model_index);
                    }
                    return std::nullopt;
                }
                geometry->source_content_fingerprint = source_fingerprint;

                std::size_t next_vertex_count = 0U;
                std::size_t next_index_count = 0U;
                std::size_t next_surface_count = 0U;
                std::size_t next_material_count = 0U;
                if (!checked_add(
                        retained_vertex_count,
                        geometry->vertices.size(),
                        next_vertex_count) ||
                    !checked_add(
                        retained_index_count,
                        geometry->indices.size(),
                        next_index_count) ||
                    !checked_add(
                        retained_surface_count,
                        geometry->surfaces.size(),
                        next_surface_count) ||
                    !checked_add(
                        retained_material_count,
                        geometry->materials.size(),
                        next_material_count) ||
                    next_vertex_count > limits_.maximum_output_vertices ||
                    next_index_count > limits_.maximum_output_indices ||
                    next_surface_count > limits_.maximum_output_surfaces ||
                    next_material_count > limits_.maximum_output_materials) {
                    static_cast<void>(fail(
                        GoldSrcBspErrorCode::geometry_limit_exceeded,
                        GoldSrcBspLumpId::models,
                        absolute_record_offset(
                            GoldSrcBspLumpId::models,
                            model_index,
                            kGoldSrcBspModelWireSize) + 56U,
                        model_index,
                        "Aggregate world and brush-model geometry exceeds configured limits"));
                    if (error_) {
                        error_->source_model_index =
                            static_cast<std::uint32_t>(model_index);
                    }
                    return std::nullopt;
                }
                retained_vertex_count = next_vertex_count;
                retained_index_count = next_index_count;
                retained_surface_count = next_surface_count;
                retained_material_count = next_material_count;

                const auto& source_model = models_[model_index];
                brush_submodels.push_back(GoldSrcBspBrushSubmodelAsset{
                    static_cast<std::uint32_t>(model_index),
                    source_model.origin,
                    assets::WorldBounds{
                        source_model.minimums, source_model.maximums},
                    source_model.headnodes[0U],
                    std::move(*geometry),
                });
            }
        }

        GoldSrcBspSpatialSource spatial_source;
        spatial_source.submodel_face_ordinals.reserve(faces_.size());
        for (std::size_t model_index = 1U;
             model_index < models_.size();
             ++model_index) {
            const auto& model = models_[model_index];
            const auto first_face = static_cast<std::uint32_t>(model.first_face);
            const auto face_count = static_cast<std::uint32_t>(model.face_count);
            for (std::uint32_t face_offset = 0U;
                 face_offset < face_count;
                 ++face_offset) {
                spatial_source.submodel_face_ordinals.push_back(
                    first_face + face_offset);
            }
        }
        spatial_source.planes.reserve(planes_.size());
        for (const auto& plane : planes_) {
            spatial_source.planes.push_back(
                spatial::GoldSrcSpatialSourcePlane{
                    plane.normal,
                    plane.distance,
                    plane.type,
                });
        }
        spatial_source.nodes.reserve(nodes_.size());
        for (const auto& node : nodes_) {
            spatial_source.nodes.push_back(
                spatial::GoldSrcSpatialSourceNode{
                    node.plane_index,
                    {static_cast<std::int32_t>(node.children[0U]),
                        static_cast<std::int32_t>(node.children[1U])},
                    assets::WorldBounds{
                        assets::AssetVector3{
                            static_cast<float>(node.minimums[0U]),
                            static_cast<float>(node.minimums[1U]),
                            static_cast<float>(node.minimums[2U]),
                        },
                        assets::AssetVector3{
                            static_cast<float>(node.maximums[0U]),
                            static_cast<float>(node.maximums[1U]),
                            static_cast<float>(node.maximums[2U]),
                        },
                    },
                    static_cast<std::uint32_t>(node.first_face),
                    static_cast<std::uint32_t>(node.face_count),
                });
        }
        spatial_source.leaves.reserve(leaves_.size());
        for (const auto& leaf : leaves_) {
            spatial_source.leaves.push_back(
                spatial::GoldSrcSpatialSourceLeaf{
                    leaf.contents,
                    leaf.visibility_offset,
                    assets::WorldBounds{
                        assets::AssetVector3{
                            static_cast<float>(leaf.minimums[0U]),
                            static_cast<float>(leaf.minimums[1U]),
                            static_cast<float>(leaf.minimums[2U]),
                        },
                        assets::AssetVector3{
                            static_cast<float>(leaf.maximums[0U]),
                            static_cast<float>(leaf.maximums[1U]),
                            static_cast<float>(leaf.maximums[2U]),
                        },
                    },
                    static_cast<std::uint32_t>(leaf.first_marksurface),
                    static_cast<std::uint32_t>(leaf.marksurface_count),
                });
        }
        spatial_source.marksurface_face_ordinals.reserve(marksurfaces_.size());
        for (const auto source_face_ordinal : marksurfaces_) {
            spatial_source.marksurface_face_ordinals.push_back(
                static_cast<std::uint32_t>(source_face_ordinal));
        }
        const auto visibility = lump_bytes(GoldSrcBspLumpId::visibility);
        spatial_source.visibility_bytes.assign(
            visibility.begin(), visibility.end());
        spatial_source.world_model = spatial::GoldSrcSpatialSourceModel{
            assets::WorldBounds{world_model.minimums, world_model.maximums},
            world_model.headnodes[0U],
            world_model.visible_leaf_count,
        };
        spatial_source.source_face_count =
            static_cast<std::uint32_t>(faces_.size());

        const auto entities = lump_bytes(GoldSrcBspLumpId::entities);
        std::vector<std::byte> entity_lump_bytes;
        entity_lump_bytes.assign(entities.begin(), entities.end());

        return GoldSrcBspParsedDocument{
            std::move(*world),
            std::move(spatial_source),
            std::move(brush_submodels),
            std::move(entity_lump_bytes),
            lump_element_counts_,
            geometry_statistics_,
        };
    }

    std::span<const std::byte> source_;
    const GoldSrcBspImportLimits& limits_;
    bool materialize_brush_submodels_{true};
    std::array<LumpRange, kGoldSrcBspLumpCount> lumps_{};
    std::array<std::size_t, kGoldSrcBspLumpCount> lump_element_counts_{};
    std::vector<Plane> planes_;
    std::vector<assets::AssetVector3> vertices_;
    std::vector<Edge> edges_;
    std::vector<std::int32_t> surfedges_;
    std::vector<TextureMetadata> textures_;
    std::vector<Texinfo> texinfo_;
    std::vector<Face> faces_;
    std::vector<Node> nodes_;
    std::vector<Leaf> leaves_;
    std::vector<std::uint16_t> marksurfaces_;
    std::vector<Clipnode> clipnodes_;
    std::vector<Model> models_;
    GoldSrcBspGeometryStatistics geometry_statistics_{};
    std::optional<GoldSrcBspError> error_;
};

} // namespace

GoldSrcBspParseResult GoldSrcBspParser::parse(
    const std::span<const std::byte> source,
    const GoldSrcBspImportLimits& limits,
    const GoldSrcBspParseOptions& options)
{
    try {
        ParserState parser{source, limits, options};
        return parser.run();
    } catch (const std::bad_alloc&) {
        return failure_result(GoldSrcBspError{
            GoldSrcBspErrorCode::unable_to_retain_world,
            std::nullopt,
            0U,
            std::nullopt,
            "Unable to retain bounded owning BSP parser state",
        });
    } catch (...) {
        return failure_result(GoldSrcBspError{
            GoldSrcBspErrorCode::unable_to_retain_world,
            std::nullopt,
            0U,
            std::nullopt,
            "Unexpected failure while constructing transactional BSP world geometry",
        });
    }
}

} // namespace hlclient::goldsrc::bsp
