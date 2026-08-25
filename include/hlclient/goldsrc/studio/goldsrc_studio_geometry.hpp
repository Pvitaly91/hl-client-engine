#pragma once

#include <hlclient/assets/model_asset_types.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc::studio {

enum class GoldSrcStudioGeometryErrorCode {
    invalid_configuration,
    command_offset_out_of_bounds,
    truncated_command,
    missing_terminator,
    invalid_command_count,
    command_limit_exceeded,
    invalid_vertex_reference,
    invalid_normal_reference,
    invalid_bone_reference,
    invalid_source_vector,
    degenerate_triangle,
    triangle_count_mismatch,
    output_limit_exceeded,
    unable_to_retain_geometry,
};

[[nodiscard]] std::string_view to_string(
    GoldSrcStudioGeometryErrorCode code) noexcept;

struct GoldSrcStudioGeometryError {
    GoldSrcStudioGeometryErrorCode code{
        GoldSrcStudioGeometryErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::optional<std::size_t> command_ordinal;
};

struct GoldSrcStudioMeshCommandLimits {
    std::size_t maximum_commands{1'048'576U};
    std::size_t maximum_output_vertices{1'048'576U};
    std::size_t maximum_output_indices{3'145'728U};
};

struct GoldSrcStudioMeshCommandInput {
    std::span<const std::byte> source;
    std::size_t command_stream_offset{0U};
    std::span<const assets::AssetVector3> source_positions;
    std::span<const assets::AssetVector3> source_normals;
    std::span<const std::uint8_t> position_bones;
    std::span<const std::uint8_t> normal_bones;
    std::size_t skeleton_bone_count{0U};
    std::size_t expected_triangle_count{0U};
};

struct GoldSrcStudioMeshCommandOutput {
    std::vector<assets::ModelSkinnedVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::size_t source_command_count{0U};
    std::size_t retained_degenerate_triangle_count{0U};
    std::size_t consumed_byte_count{0U};
};

struct GoldSrcStudioMeshCommandResult {
    std::optional<GoldSrcStudioMeshCommandOutput> output;
    std::optional<GoldSrcStudioGeometryError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return output.has_value();
    }
};

[[nodiscard]] GoldSrcStudioMeshCommandResult decode_goldsrc_studio_mesh_commands(
    const GoldSrcStudioMeshCommandInput& input,
    const GoldSrcStudioMeshCommandLimits& limits = {});

[[nodiscard]] std::optional<assets::AssetVector2> goldsrc_studio_normalized_uv(
    std::int16_t raw_s,
    std::int16_t raw_t,
    std::uint32_t texture_width,
    std::uint32_t texture_height) noexcept;

} // namespace hlclient::goldsrc::studio
