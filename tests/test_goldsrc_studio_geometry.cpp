#include <hlclient/goldsrc/studio/goldsrc_studio_geometry.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_parser.hpp>

#include "goldsrc_studio_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace studio = hlclient::goldsrc::studio;
namespace fixture = hlclient::tests;

[[nodiscard]] std::vector<std::byte> commands(
    const std::int16_t count,
    const std::size_t vertex_count,
    const bool terminator = true)
{
    std::vector<std::byte> bytes(2U + vertex_count * 8U + (terminator ? 2U : 0U));
    fixture::studio_write_i16le(bytes, 0U, count);
    for (std::size_t index = 0U; index < vertex_count; ++index) {
        const auto offset = 2U + index * 8U;
        fixture::studio_write_i16le(bytes, offset, static_cast<std::int16_t>(index));
        fixture::studio_write_i16le(bytes, offset + 2U,
            static_cast<std::int16_t>(index));
        fixture::studio_write_i16le(bytes, offset + 4U,
            static_cast<std::int16_t>(index * 10U));
        fixture::studio_write_i16le(bytes, offset + 6U,
            static_cast<std::int16_t>(index * -5));
    }
    return bytes;
}

[[nodiscard]] studio::GoldSrcStudioMeshCommandResult decode(
    const std::vector<std::byte>& bytes,
    const std::span<const assets::AssetVector3> positions,
    const std::size_t triangles)
{
    static constexpr std::array normals{
        assets::AssetVector3{0.0F, 0.0F, 1.0F},
        assets::AssetVector3{0.0F, 0.0F, 1.0F},
        assets::AssetVector3{0.0F, 0.0F, 1.0F},
        assets::AssetVector3{0.0F, 0.0F, 1.0F},
    };
    static constexpr std::array<std::uint8_t, 4U> bones{0U, 0U, 0U, 0U};
    return studio::decode_goldsrc_studio_mesh_commands(
        studio::GoldSrcStudioMeshCommandInput{
            bytes,
            0U,
            positions,
            std::span{normals}.first(positions.size()),
            std::span{bones}.first(positions.size()),
            std::span{bones}.first(positions.size()),
            1U,
            triangles,
        });
}

TEST_CASE("Studio one-triangle command retains raw S and T",
    "[goldsrc-studio][geometry][triangle]")
{
    static constexpr std::array positions{
        assets::AssetVector3{0.0F, 0.0F, 0.0F},
        assets::AssetVector3{1.0F, 0.0F, 0.0F},
        assets::AssetVector3{0.0F, 1.0F, 0.0F},
    };
    const auto result = decode(commands(3, 3U), positions, 1U);
    REQUIRE(result);
    REQUIRE(result.output->indices == std::vector<std::uint32_t>{0U, 1U, 2U});
    REQUIRE(result.output->vertices[1U].raw_texture_s == 10);
    REQUIRE(result.output->vertices[1U].raw_texture_t == -5);
    REQUIRE(result.output->source_command_count == 1U);
    REQUIRE(result.output->retained_degenerate_triangle_count == 0U);
    REQUIRE(result.output->consumed_byte_count == 28U);
}

TEST_CASE("Studio strips alternate parity and fans retain their anchor",
    "[goldsrc-studio][geometry][strip][fan]")
{
    static constexpr std::array positions{
        assets::AssetVector3{0.0F, 0.0F, 0.0F},
        assets::AssetVector3{1.0F, 0.0F, 0.0F},
        assets::AssetVector3{0.0F, 1.0F, 0.0F},
        assets::AssetVector3{1.0F, 1.0F, 0.0F},
    };
    const auto strip = decode(commands(4, 4U), positions, 2U);
    REQUIRE(strip);
    REQUIRE(strip.output->indices ==
        std::vector<std::uint32_t>{0U, 1U, 2U, 2U, 1U, 3U});

    const auto fan = decode(commands(-4, 4U), positions, 2U);
    REQUIRE(fan);
    REQUIRE(fan.output->indices ==
        std::vector<std::uint32_t>{0U, 1U, 2U, 0U, 2U, 3U});
}

TEST_CASE("Studio command streams require bounded counts and exact terminators",
    "[goldsrc-studio][geometry][invalid]")
{
    static constexpr std::array positions{
        assets::AssetVector3{0.0F, 0.0F, 0.0F},
        assets::AssetVector3{1.0F, 0.0F, 0.0F},
        assets::AssetVector3{0.0F, 1.0F, 0.0F},
    };
    const auto missing = decode(commands(3, 3U, false), positions, 1U);
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error->code == studio::GoldSrcStudioGeometryErrorCode::truncated_command);

    auto minimum = commands(3, 3U);
    fixture::studio_write_i16le(minimum, 0U,
        std::numeric_limits<std::int16_t>::min());
    const auto invalid = decode(minimum, positions, 1U);
    REQUIRE_FALSE(invalid);
    REQUIRE(invalid.error->code ==
        studio::GoldSrcStudioGeometryErrorCode::invalid_command_count);

    auto reference = commands(3, 3U);
    fixture::studio_write_i16le(reference, 2U, 3);
    const auto out_of_range = decode(reference, positions, 1U);
    REQUIRE_FALSE(out_of_range);
    REQUIRE(out_of_range.error->code ==
        studio::GoldSrcStudioGeometryErrorCode::invalid_vertex_reference);
}

TEST_CASE("Studio mesh triangle cross-check retains evidenced source degenerates",
    "[goldsrc-studio][geometry][triangles]")
{
    static constexpr std::array positions{
        assets::AssetVector3{0.0F, 0.0F, 0.0F},
        assets::AssetVector3{1.0F, 0.0F, 0.0F},
        assets::AssetVector3{0.0F, 1.0F, 0.0F},
    };
    const auto mismatch = decode(commands(3, 3U), positions, 2U);
    REQUIRE_FALSE(mismatch);
    REQUIRE(mismatch.error->code ==
        studio::GoldSrcStudioGeometryErrorCode::triangle_count_mismatch);

    auto degenerate = commands(3, 3U);
    fixture::studio_write_i16le(degenerate, 2U + 2U * 8U, 1);
    const auto retained = decode(degenerate, positions, 1U);
    REQUIRE(retained);
    REQUIRE(retained.output->indices ==
        std::vector<std::uint32_t>{0U, 1U, 2U});
    REQUIRE(retained.output->retained_degenerate_triangle_count == 1U);
    fixture::studio_write_i16le(degenerate, 0U, -3);
    const auto retained_fan = decode(degenerate, positions, 1U);
    REQUIRE(retained_fan);
    REQUIRE(retained_fan.output->retained_degenerate_triangle_count == 1U);

    static constexpr std::array collinear_positions{
        assets::AssetVector3{0.0F, 0.0F, 0.0F},
        assets::AssetVector3{1.0F, 0.0F, 0.0F},
        assets::AssetVector3{2.0F, 0.0F, 0.0F},
    };
    const auto area_only = decode(commands(-3, 3U), collinear_positions, 1U);
    REQUIRE(area_only);
    REQUIRE(area_only.output->retained_degenerate_triangle_count == 1U);
    const auto area_only_strip = decode(
        commands(3, 3U), collinear_positions, 1U);
    REQUIRE(area_only_strip);
    REQUIRE(area_only_strip.output->retained_degenerate_triangle_count == 1U);

    auto parser_source = fixture::literal_minimal_goldsrc_studio_v10();
    fixture::studio_write_i16le(parser_source,
        fixture::kSyntheticStudioCommandsOffset + 2U + 2U * 8U, 1);
    const auto parsed = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{parser_source, std::nullopt, {}});
    REQUIRE(parsed);
    REQUIRE(parsed.document->skeletal_model.submodels[0U]
                .meshes[0U]
                .retained_degenerate_triangle_count == 1U);
    REQUIRE(parsed.document->skeletal_model.statistics
                .retained_degenerate_triangle_count == 1U);
}

TEST_CASE("Normalized Studio UV helper is pure and does not flip or wrap",
    "[goldsrc-studio][geometry][uv]")
{
    const auto uv = studio::goldsrc_studio_normalized_uv(16, -8, 32U, 16U);
    REQUIRE(uv);
    REQUIRE(uv->x == 0.5F);
    REQUIRE(uv->y == -0.5F);
    REQUIRE_FALSE(studio::goldsrc_studio_normalized_uv(0, 0, 0U, 1U));
}

TEST_CASE("Studio multiple command streams and command limits are deterministic",
    "[goldsrc-studio][geometry][commands][exact-limit]")
{
    static constexpr std::array positions{
        assets::AssetVector3{0.0F, 0.0F, 0.0F},
        assets::AssetVector3{1.0F, 0.0F, 0.0F},
        assets::AssetVector3{0.0F, 1.0F, 0.0F},
    };
    auto bytes = commands(3, 3U, false);
    const auto second = commands(-3, 3U, true);
    bytes.insert(bytes.end(), second.begin(), second.end());
    const auto parsed = decode(bytes, positions, 2U);
    REQUIRE(parsed);
    REQUIRE(parsed.output->source_command_count == 2U);
    REQUIRE(parsed.output->indices.size() == 6U);

    static constexpr std::array normals{
        assets::AssetVector3{0.0F, 0.0F, 1.0F},
        assets::AssetVector3{0.0F, 0.0F, 1.0F},
        assets::AssetVector3{0.0F, 0.0F, 1.0F},
    };
    static constexpr std::array<std::uint8_t, 3U> bones{0U, 0U, 0U};
    const studio::GoldSrcStudioMeshCommandInput input{
        bytes, 0U, positions, normals, bones, bones, 1U, 2U};
    REQUIRE_FALSE(studio::decode_goldsrc_studio_mesh_commands(input,
        studio::GoldSrcStudioMeshCommandLimits{1U, 16U, 18U}));
    REQUIRE(studio::decode_goldsrc_studio_mesh_commands(input,
        studio::GoldSrcStudioMeshCommandLimits{2U, 16U, 18U}));
}

TEST_CASE("Studio command normal and source-bone references are independent",
    "[goldsrc-studio][geometry][references]")
{
    static constexpr std::array positions{
        assets::AssetVector3{0.0F, 0.0F, 0.0F},
        assets::AssetVector3{1.0F, 0.0F, 0.0F},
        assets::AssetVector3{0.0F, 1.0F, 0.0F},
    };
    auto bad_normal = commands(3, 3U);
    fixture::studio_write_i16le(bad_normal, 2U + 2U, 3);
    const auto normal_result = decode(bad_normal, positions, 1U);
    REQUIRE_FALSE(normal_result);
    REQUIRE(normal_result.error->code ==
        studio::GoldSrcStudioGeometryErrorCode::invalid_normal_reference);

    auto zero_normals = fixture::literal_minimal_goldsrc_studio_v10();
    fixture::studio_write_i16le(zero_normals,
        fixture::kSyntheticStudioCommandsOffset + 2U + 8U + 2U, 0);
    fixture::studio_write_i16le(zero_normals,
        fixture::kSyntheticStudioCommandsOffset + 2U + 16U + 2U, 0);
    fixture::studio_write_vector3(zero_normals,
        fixture::kSyntheticStudioNormalsOffset + 24U, 0.0F, 0.0F, 0.0F);
    const auto unused_result = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{zero_normals, std::nullopt, {}});
    REQUIRE(unused_result);

    fixture::studio_write_i16le(zero_normals,
        fixture::kSyntheticStudioCommandsOffset + 2U + 16U + 2U, 2);
    const auto referenced_result = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{zero_normals, std::nullopt, {}});
    REQUIRE(referenced_result);
    const auto& retained_normal = referenced_result.document->skeletal_model
                                      .submodels[0U]
                                      .vertices[2U]
                                      .source_normal;
    REQUIRE(retained_normal.x == 0.0F);
    REQUIRE(retained_normal.y == 0.0F);
    REQUIRE(retained_normal.z == 0.0F);

    auto nonfinite_normal = fixture::literal_minimal_goldsrc_studio_v10();
    fixture::studio_write_f32le(nonfinite_normal,
        fixture::kSyntheticStudioNormalsOffset,
        std::numeric_limits<float>::quiet_NaN());
    const auto nonfinite_result = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{
            nonfinite_normal, std::nullopt, {}});
    REQUIRE_FALSE(nonfinite_result);
    REQUIRE(nonfinite_result.error->code ==
        studio::GoldSrcStudioErrorCode::invalid_reference);

    static constexpr std::array normals{
        assets::AssetVector3{0.0F, 0.0F, 1.0F},
        assets::AssetVector3{0.0F, 0.0F, 1.0F},
        assets::AssetVector3{0.0F, 0.0F, 1.0F},
    };
    static constexpr std::array<std::uint8_t, 3U> invalid_position_bones{1U, 0U, 0U};
    static constexpr std::array<std::uint8_t, 3U> valid_normal_bones{0U, 0U, 0U};
    const auto source = commands(3, 3U);
    const auto bone_result = studio::decode_goldsrc_studio_mesh_commands(
        studio::GoldSrcStudioMeshCommandInput{
            source,
            0U,
            positions,
            normals,
            invalid_position_bones,
            valid_normal_bones,
            1U,
            1U,
        });
    REQUIRE_FALSE(bone_result);
    REQUIRE(bone_result.error->code ==
        studio::GoldSrcStudioGeometryErrorCode::invalid_bone_reference);
}

TEST_CASE("Studio vertex normal triangle and emitted-output limits are exact",
    "[goldsrc-studio][geometry][parser][exact-limit]")
{
    const auto baseline = fixture::literal_minimal_goldsrc_studio_v10();
    const studio::GoldSrcStudioSourceBundleView bundle{baseline, std::nullopt, {}};
    auto limits = studio::GoldSrcStudioModelImportLimits{};
    limits.maximum_vertices_per_submodel = 3U;
    limits.maximum_normals_per_submodel = 3U;
    limits.maximum_triangles_per_submodel = 1U;
    limits.maximum_total_triangles = 1U;
    limits.maximum_triangle_commands = 1U;
    limits.maximum_output_vertices = 3U;
    limits.maximum_output_indices = 3U;
    REQUIRE(studio::GoldSrcStudioParser::parse(bundle, limits));

    auto vertices = baseline;
    fixture::studio_write_i32le(vertices,
        fixture::kSyntheticStudioSubmodelOffset + 80U, 4);
    REQUIRE_FALSE(studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{vertices, std::nullopt, {}}, limits));

    auto normals = baseline;
    fixture::studio_write_i32le(normals,
        fixture::kSyntheticStudioSubmodelOffset + 92U, 4);
    REQUIRE_FALSE(studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{normals, std::nullopt, {}}, limits));

    auto triangles = baseline;
    fixture::studio_write_i32le(triangles,
        fixture::kSyntheticStudioMeshOffset, 2);
    REQUIRE_FALSE(studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{triangles, std::nullopt, {}}, limits));

    auto output_limit = limits;
    output_limit.maximum_output_vertices = 2U;
    REQUIRE_FALSE(studio::GoldSrcStudioParser::parse(bundle, output_limit));
    output_limit = limits;
    output_limit.maximum_output_indices = 2U;
    REQUIRE_FALSE(studio::GoldSrcStudioParser::parse(bundle, output_limit));
}

TEST_CASE("Studio triangle limit is accumulated across every mesh in a submodel",
    "[goldsrc-studio][geometry][triangles][aggregate]")
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    const auto mesh_offset = bytes.size();
    const auto first_commands = mesh_offset + 2U * studio::kGoldSrcStudioMeshWireSize;
    const auto second_commands = first_commands + 28U;
    bytes.resize(second_commands + 28U, std::byte{0});
    for (std::size_t mesh_index = 0U; mesh_index < 2U; ++mesh_index) {
        std::copy_n(bytes.begin() +
                static_cast<std::ptrdiff_t>(fixture::kSyntheticStudioMeshOffset),
            studio::kGoldSrcStudioMeshWireSize,
            bytes.begin() + static_cast<std::ptrdiff_t>(
                mesh_offset + mesh_index * studio::kGoldSrcStudioMeshWireSize));
    }
    std::copy_n(bytes.begin() +
            static_cast<std::ptrdiff_t>(fixture::kSyntheticStudioCommandsOffset),
        28U, bytes.begin() + static_cast<std::ptrdiff_t>(first_commands));
    std::copy_n(bytes.begin() +
            static_cast<std::ptrdiff_t>(fixture::kSyntheticStudioCommandsOffset),
        28U, bytes.begin() + static_cast<std::ptrdiff_t>(second_commands));
    fixture::studio_write_i32le(bytes, mesh_offset + 4U,
        static_cast<std::int32_t>(first_commands));
    fixture::studio_write_i32le(bytes,
        mesh_offset + studio::kGoldSrcStudioMeshWireSize + 4U,
        static_cast<std::int32_t>(second_commands));
    fixture::studio_write_i32le(bytes,
        mesh_offset + studio::kGoldSrcStudioMeshWireSize + 12U, 0);
    fixture::studio_write_i32le(bytes, 72U,
        static_cast<std::int32_t>(bytes.size()));
    fixture::studio_write_i32le(bytes,
        fixture::kSyntheticStudioSubmodelOffset + 72U, 2);
    fixture::studio_write_i32le(bytes,
        fixture::kSyntheticStudioSubmodelOffset + 76U,
        static_cast<std::int32_t>(mesh_offset));

    const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
    auto limits = studio::GoldSrcStudioModelImportLimits{};
    limits.maximum_meshes = 2U;
    limits.maximum_triangles_per_submodel = 2U;
    REQUIRE(studio::GoldSrcStudioParser::parse(bundle, limits));
    limits.maximum_triangles_per_submodel = 1U;
    const auto rejected = studio::GoldSrcStudioParser::parse(bundle, limits);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error->code ==
        studio::GoldSrcStudioErrorCode::count_limit_exceeded);
}

TEST_CASE("Studio command streams cannot start inside a retained texture payload",
    "[goldsrc-studio][geometry][range-overlap]")
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    fixture::studio_write_i32le(bytes, fixture::kSyntheticStudioMeshOffset + 4U,
        static_cast<std::int32_t>(fixture::kSyntheticStudioTextureDataOffset));
    const auto result = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{bytes, std::nullopt, {}});
    REQUIRE_FALSE(result);
    REQUIRE(result.error->code == studio::GoldSrcStudioErrorCode::range_overlap);
}

TEST_CASE("Studio mesh normal partition uses the exact Valve zero marker",
    "[goldsrc-studio][geometry][mesh-normal-marker][mutation]")
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    fixture::studio_write_i32le(bytes,
        fixture::kSyntheticStudioMeshOffset + 16U, 1);
    const auto result = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{bytes, std::nullopt, {}});
    REQUIRE_FALSE(result);
    REQUIRE(result.error->code == studio::GoldSrcStudioErrorCode::invalid_mesh);
}

} // namespace
