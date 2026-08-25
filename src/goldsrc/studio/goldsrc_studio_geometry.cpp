#include <hlclient/goldsrc/studio/goldsrc_studio_geometry.hpp>

#include <hlclient/goldsrc/studio/goldsrc_studio_format.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace hlclient::goldsrc::studio {
namespace {

[[nodiscard]] GoldSrcStudioMeshCommandResult failure(
    const GoldSrcStudioGeometryErrorCode code,
    const std::size_t offset,
    const std::optional<std::size_t> command = std::nullopt)
{
    return GoldSrcStudioMeshCommandResult{
        std::nullopt, GoldSrcStudioGeometryError{code, offset, command}};
}

[[nodiscard]] bool finite_vector(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

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

[[nodiscard]] std::optional<std::int16_t> read_i16_le(
    const std::span<const std::byte> source,
    const std::size_t offset) noexcept
{
    if (offset > source.size() || source.size() - offset < 2U) {
        return std::nullopt;
    }
    const auto bits = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(source[offset])) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(
                std::to_integer<std::uint8_t>(source[offset + 1U]))
            << 8U));
    return std::bit_cast<std::int16_t>(bits);
}

[[nodiscard]] double squared_area(
    const assets::AssetVector3& a,
    const assets::AssetVector3& b,
    const assets::AssetVector3& c) noexcept
{
    const auto abx = static_cast<double>(b.x) - static_cast<double>(a.x);
    const auto aby = static_cast<double>(b.y) - static_cast<double>(a.y);
    const auto abz = static_cast<double>(b.z) - static_cast<double>(a.z);
    const auto acx = static_cast<double>(c.x) - static_cast<double>(a.x);
    const auto acy = static_cast<double>(c.y) - static_cast<double>(a.y);
    const auto acz = static_cast<double>(c.z) - static_cast<double>(a.z);
    const auto x = aby * acz - abz * acy;
    const auto y = abz * acx - abx * acz;
    const auto z = abx * acy - aby * acx;
    return x * x + y * y + z * z;
}

} // namespace

std::string_view to_string(const GoldSrcStudioGeometryErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcStudioGeometryErrorCode::invalid_configuration:
        return "invalid_configuration";
    case GoldSrcStudioGeometryErrorCode::command_offset_out_of_bounds:
        return "command_offset_out_of_bounds";
    case GoldSrcStudioGeometryErrorCode::truncated_command:
        return "truncated_command";
    case GoldSrcStudioGeometryErrorCode::missing_terminator:
        return "missing_terminator";
    case GoldSrcStudioGeometryErrorCode::invalid_command_count:
        return "invalid_command_count";
    case GoldSrcStudioGeometryErrorCode::command_limit_exceeded:
        return "command_limit_exceeded";
    case GoldSrcStudioGeometryErrorCode::invalid_vertex_reference:
        return "invalid_vertex_reference";
    case GoldSrcStudioGeometryErrorCode::invalid_normal_reference:
        return "invalid_normal_reference";
    case GoldSrcStudioGeometryErrorCode::invalid_bone_reference:
        return "invalid_bone_reference";
    case GoldSrcStudioGeometryErrorCode::invalid_source_vector:
        return "invalid_source_vector";
    case GoldSrcStudioGeometryErrorCode::degenerate_triangle:
        return "degenerate_triangle";
    case GoldSrcStudioGeometryErrorCode::triangle_count_mismatch:
        return "triangle_count_mismatch";
    case GoldSrcStudioGeometryErrorCode::output_limit_exceeded:
        return "output_limit_exceeded";
    case GoldSrcStudioGeometryErrorCode::unable_to_retain_geometry:
        return "unable_to_retain_geometry";
    }
    return "unknown";
}

GoldSrcStudioMeshCommandResult decode_goldsrc_studio_mesh_commands(
    const GoldSrcStudioMeshCommandInput& input,
    const GoldSrcStudioMeshCommandLimits& limits)
{
    if (limits.maximum_commands == 0U || limits.maximum_output_vertices == 0U ||
        limits.maximum_output_indices == 0U ||
        input.source_positions.size() != input.position_bones.size() ||
        input.source_normals.size() != input.normal_bones.size() ||
        input.skeleton_bone_count == 0U) {
        return failure(GoldSrcStudioGeometryErrorCode::invalid_configuration,
            input.command_stream_offset);
    }
    if (input.command_stream_offset > input.source.size()) {
        return failure(GoldSrcStudioGeometryErrorCode::command_offset_out_of_bounds,
            input.command_stream_offset);
    }
    for (std::size_t index = 0U; index < input.source_positions.size(); ++index) {
        if (!finite_vector(input.source_positions[index])) {
            return failure(GoldSrcStudioGeometryErrorCode::invalid_source_vector,
                input.command_stream_offset, index);
        }
        if (static_cast<std::size_t>(input.position_bones[index]) >=
            input.skeleton_bone_count) {
            return failure(GoldSrcStudioGeometryErrorCode::invalid_bone_reference,
                input.command_stream_offset, index);
        }
    }
    for (std::size_t index = 0U; index < input.source_normals.size(); ++index) {
        const auto& normal = input.source_normals[index];
        if (!finite_vector(normal)) {
            return failure(GoldSrcStudioGeometryErrorCode::invalid_source_vector,
                input.command_stream_offset, index);
        }
        if (static_cast<std::size_t>(input.normal_bones[index]) >=
            input.skeleton_bone_count) {
            return failure(GoldSrcStudioGeometryErrorCode::invalid_bone_reference,
                input.command_stream_offset, index);
        }
    }

    try {
        GoldSrcStudioMeshCommandOutput output;
        std::size_t cursor = input.command_stream_offset;
        std::size_t triangle_count = 0U;
        for (;;) {
            const auto count = read_i16_le(input.source, cursor);
            if (!count) {
                return failure(output.source_command_count == 0U
                                   ? GoldSrcStudioGeometryErrorCode::missing_terminator
                                   : GoldSrcStudioGeometryErrorCode::truncated_command,
                    cursor, output.source_command_count);
            }
            cursor += 2U;
            if (*count == 0) {
                output.consumed_byte_count = cursor - input.command_stream_offset;
                break;
            }
            if (*count == std::numeric_limits<std::int16_t>::min()) {
                return failure(GoldSrcStudioGeometryErrorCode::invalid_command_count,
                    cursor - 2U, output.source_command_count);
            }
            if (output.source_command_count >= limits.maximum_commands) {
                return failure(GoldSrcStudioGeometryErrorCode::command_limit_exceeded,
                    cursor - 2U, output.source_command_count);
            }
            const auto signed_count = static_cast<std::int32_t>(*count);
            const auto vertex_count = static_cast<std::size_t>(
                signed_count < 0 ? -signed_count : signed_count);
            if (vertex_count < 3U) {
                return failure(GoldSrcStudioGeometryErrorCode::invalid_command_count,
                    cursor - 2U, output.source_command_count);
            }
            std::size_t command_bytes = 0U;
            if (vertex_count > std::numeric_limits<std::size_t>::max() /
                                   kGoldSrcStudioTriangleCommandVertexWireSize) {
                return failure(GoldSrcStudioGeometryErrorCode::output_limit_exceeded,
                    cursor - 2U, output.source_command_count);
            }
            command_bytes = vertex_count * kGoldSrcStudioTriangleCommandVertexWireSize;
            std::size_t command_end = 0U;
            if (!checked_add(cursor, command_bytes, command_end) ||
                command_end > input.source.size()) {
                return failure(GoldSrcStudioGeometryErrorCode::truncated_command,
                    cursor, output.source_command_count);
            }
            if (vertex_count > limits.maximum_output_vertices -
                                   std::min(output.vertices.size(),
                                       limits.maximum_output_vertices)) {
                return failure(GoldSrcStudioGeometryErrorCode::output_limit_exceeded,
                    cursor, output.source_command_count);
            }
            const auto triangles_in_command = vertex_count - 2U;
            if (triangles_in_command >
                    (limits.maximum_output_indices -
                        std::min(output.indices.size(), limits.maximum_output_indices)) /
                        3U ||
                triangle_count > std::numeric_limits<std::size_t>::max() -
                                     triangles_in_command) {
                return failure(GoldSrcStudioGeometryErrorCode::output_limit_exceeded,
                    cursor, output.source_command_count);
            }

            const auto first_vertex = output.vertices.size();
            std::vector<std::size_t> source_vertex_indices;
            source_vertex_indices.reserve(vertex_count);
            for (std::size_t vertex_ordinal = 0U; vertex_ordinal < vertex_count;
                 ++vertex_ordinal) {
                const auto wire = GoldSrcStudioWireDecoder::triangle_command_vertex(
                    input.source,
                    cursor + vertex_ordinal *
                                 kGoldSrcStudioTriangleCommandVertexWireSize);
                if (!wire) {
                    return failure(GoldSrcStudioGeometryErrorCode::truncated_command,
                        cursor, output.source_command_count);
                }
                if (wire->vertex_index < 0 ||
                    static_cast<std::size_t>(wire->vertex_index) >=
                        input.source_positions.size()) {
                    return failure(GoldSrcStudioGeometryErrorCode::invalid_vertex_reference,
                        cursor, output.source_command_count);
                }
                if (wire->normal_index < 0 ||
                    static_cast<std::size_t>(wire->normal_index) >=
                        input.source_normals.size()) {
                    return failure(GoldSrcStudioGeometryErrorCode::invalid_normal_reference,
                        cursor, output.source_command_count);
                }
                const auto position_index = static_cast<std::size_t>(wire->vertex_index);
                const auto normal_index = static_cast<std::size_t>(wire->normal_index);
                source_vertex_indices.push_back(position_index);
                output.vertices.push_back(assets::ModelSkinnedVertex{
                    input.source_positions[position_index],
                    input.source_normals[normal_index],
                    wire->raw_s,
                    wire->raw_t,
                    input.position_bones[position_index],
                    input.normal_bones[normal_index],
                });
            }

            const auto append_triangle = [&](const std::size_t a,
                                             const std::size_t b,
                                             const std::size_t c) {
                const bool degenerate =
                    source_vertex_indices[a] == source_vertex_indices[b] ||
                    source_vertex_indices[b] == source_vertex_indices[c] ||
                    source_vertex_indices[a] == source_vertex_indices[c] ||
                    squared_area(input.source_positions[source_vertex_indices[a]],
                        input.source_positions[source_vertex_indices[b]],
                        input.source_positions[source_vertex_indices[c]]) == 0.0;
                if (degenerate) {
                    ++output.retained_degenerate_triangle_count;
                }
                output.indices.push_back(static_cast<std::uint32_t>(first_vertex + a));
                output.indices.push_back(static_cast<std::uint32_t>(first_vertex + b));
                output.indices.push_back(static_cast<std::uint32_t>(first_vertex + c));
            };
            for (std::size_t index = 2U; index < vertex_count; ++index) {
                if (*count > 0) {
                    if ((index & 1U) == 0U) {
                        append_triangle(index - 2U, index - 1U, index);
                    } else {
                        append_triangle(index - 1U, index - 2U, index);
                    }
                } else {
                    append_triangle(0U, index - 1U, index);
                }
            }
            triangle_count += triangles_in_command;
            cursor = command_end;
            ++output.source_command_count;
        }
        if (triangle_count != input.expected_triangle_count) {
            return failure(GoldSrcStudioGeometryErrorCode::triangle_count_mismatch,
                input.command_stream_offset);
        }
        return GoldSrcStudioMeshCommandResult{std::move(output), std::nullopt};
    } catch (const std::bad_alloc&) {
        return failure(GoldSrcStudioGeometryErrorCode::unable_to_retain_geometry,
            input.command_stream_offset);
    } catch (const std::length_error&) {
        return failure(GoldSrcStudioGeometryErrorCode::unable_to_retain_geometry,
            input.command_stream_offset);
    }
}

std::optional<assets::AssetVector2> goldsrc_studio_normalized_uv(
    const std::int16_t raw_s,
    const std::int16_t raw_t,
    const std::uint32_t texture_width,
    const std::uint32_t texture_height) noexcept
{
    if (texture_width == 0U || texture_height == 0U) {
        return std::nullopt;
    }
    return assets::AssetVector2{
        static_cast<float>(raw_s) / static_cast<float>(texture_width),
        static_cast<float>(raw_t) / static_cast<float>(texture_height),
    };
}

} // namespace hlclient::goldsrc::studio
