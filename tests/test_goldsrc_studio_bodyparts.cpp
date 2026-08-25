#include <hlclient/goldsrc/studio/goldsrc_studio_parser.hpp>

#include "goldsrc_studio_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace {

namespace studio = hlclient::goldsrc::studio;
namespace fixture = hlclient::tests;

TEST_CASE("Studio bodypart submodel mesh and skin-slot metadata remain ordered",
    "[goldsrc-studio][bodyparts][mesh]")
{
    const auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
    const auto result = studio::GoldSrcStudioParser::parse(bundle);
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    const auto& model = result.document->skeletal_model;
    REQUIRE(model.bodyparts.size() == 1U);
    REQUIRE(model.bodyparts[0U].name == "body");
    REQUIRE(model.bodyparts[0U].base == 1);
    REQUIRE(model.bodyparts[0U].submodel_indices == std::vector<std::uint32_t>{0U});
    REQUIRE(model.submodels.size() == 1U);
    REQUIRE(model.submodels[0U].meshes.size() == 1U);
    REQUIRE(model.submodels[0U].meshes[0U].skin_reference_slot == 0U);
    REQUIRE(model.submodels[0U].meshes[0U].source_triangle_count == 1U);
    REQUIRE(model.skin_families.size() == 1U);
    REQUIRE(model.skin_families[0U].texture_indices ==
        std::vector<std::uint16_t>{0U});
}

TEST_CASE("Studio mesh skinref addresses a slot and invalid slots fail",
    "[goldsrc-studio][bodyparts][skinref]")
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    fixture::studio_write_i32le(bytes, fixture::kSyntheticStudioMeshOffset + 8U, 1);
    const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
    const auto result = studio::GoldSrcStudioParser::parse(bundle);
    REQUIRE_FALSE(result);
    REQUIRE(result.error->code == studio::GoldSrcStudioErrorCode::invalid_mesh);
}

TEST_CASE("Studio skin families validate every texture index transactionally",
    "[goldsrc-studio][bodyparts][skins]")
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    fixture::studio_write_i16le(bytes, fixture::kSyntheticStudioSkinOffset, 1);
    const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
    const auto result = studio::GoldSrcStudioParser::parse(bundle);
    REQUIRE_FALSE(result);
    REQUIRE(result.error->code == studio::GoldSrcStudioErrorCode::invalid_skin_table);
    REQUIRE_FALSE(result.document.has_value());
}

TEST_CASE("Studio split texture dependency is typed and complete bundles succeed",
    "[goldsrc-studio][bodyparts][split]")
{
    const auto main = fixture::synthetic_split_texture_main();
    const auto plan = studio::GoldSrcStudioParser::inspect_dependencies(main);
    REQUIRE(plan);
    REQUIRE(plan.plan->texture_companion_required);
    REQUIRE(plan.plan->expected_source_count == 2U);

    const studio::GoldSrcStudioSourceBundleView incomplete{main, std::nullopt, {}};
    const auto missing = studio::GoldSrcStudioParser::parse(incomplete);
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error->code ==
        studio::GoldSrcStudioErrorCode::external_dependency_required);

    const auto texture = fixture::synthetic_texture_companion();
    const studio::GoldSrcStudioSourceBundleView complete{main, texture, {}};
    const auto imported = studio::GoldSrcStudioParser::parse(complete);
    INFO((imported.error ? imported.error->context : std::string{}));
    REQUIRE(imported);
    REQUIRE(imported.document->skeletal_model.textures.size() == 1U);
    REQUIRE(imported.document->skeletal_model.statistics.source_count == 2U);
}

TEST_CASE("Studio preserves multiple bodyparts submodels and skin families without selection",
    "[goldsrc-studio][bodyparts][multiple]")
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    const auto bodyparts_offset = bytes.size();
    bytes.resize(bytes.size() + 2U * studio::kGoldSrcStudioBodyPartWireSize);
    std::copy_n(bytes.begin() +
            static_cast<std::ptrdiff_t>(fixture::kSyntheticStudioBodyPartOffset),
        studio::kGoldSrcStudioBodyPartWireSize,
        bytes.begin() + static_cast<std::ptrdiff_t>(bodyparts_offset));
    std::copy_n(bytes.begin() +
            static_cast<std::ptrdiff_t>(fixture::kSyntheticStudioBodyPartOffset),
        studio::kGoldSrcStudioBodyPartWireSize,
        bytes.begin() + static_cast<std::ptrdiff_t>(
            bodyparts_offset + studio::kGoldSrcStudioBodyPartWireSize));
    fixture::studio_write_fixed_string(bytes, bodyparts_offset, 64U, "body-a");
    fixture::studio_write_fixed_string(bytes,
        bodyparts_offset + studio::kGoldSrcStudioBodyPartWireSize, 64U, "body-b");
    const auto skins_offset = bytes.size();
    bytes.resize(bytes.size() + 4U);
    fixture::studio_write_i16le(bytes, skins_offset, 0);
    fixture::studio_write_i16le(bytes, skins_offset + 2U, 0);

    const auto second_model = bytes.size();
    const auto second_mesh = second_model + studio::kGoldSrcStudioSubmodelWireSize;
    const auto second_vertex_bones =
        second_mesh + studio::kGoldSrcStudioMeshWireSize;
    const auto second_normal_bones = second_vertex_bones + 3U;
    const auto second_vertices = second_normal_bones + 3U;
    const auto second_normals = second_vertices + 36U;
    const auto second_commands = second_normals + 36U;
    bytes.resize(second_commands + 28U, std::byte{0});
    std::copy_n(bytes.begin() +
            static_cast<std::ptrdiff_t>(fixture::kSyntheticStudioSubmodelOffset),
        studio::kGoldSrcStudioSubmodelWireSize,
        bytes.begin() + static_cast<std::ptrdiff_t>(second_model));
    std::copy_n(bytes.begin() +
            static_cast<std::ptrdiff_t>(fixture::kSyntheticStudioMeshOffset),
        studio::kGoldSrcStudioMeshWireSize,
        bytes.begin() + static_cast<std::ptrdiff_t>(second_mesh));
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(
            fixture::kSyntheticStudioVertexBonesOffset),
        3U, bytes.begin() + static_cast<std::ptrdiff_t>(second_vertex_bones));
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(
            fixture::kSyntheticStudioNormalBonesOffset),
        3U, bytes.begin() + static_cast<std::ptrdiff_t>(second_normal_bones));
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(
            fixture::kSyntheticStudioVerticesOffset),
        36U, bytes.begin() + static_cast<std::ptrdiff_t>(second_vertices));
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(
            fixture::kSyntheticStudioNormalsOffset),
        36U, bytes.begin() + static_cast<std::ptrdiff_t>(second_normals));
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(
            fixture::kSyntheticStudioCommandsOffset),
        28U, bytes.begin() + static_cast<std::ptrdiff_t>(second_commands));
    fixture::studio_write_i32le(bytes, second_model + 76U,
        static_cast<std::int32_t>(second_mesh));
    fixture::studio_write_i32le(bytes, second_model + 84U,
        static_cast<std::int32_t>(second_vertex_bones));
    fixture::studio_write_i32le(bytes, second_model + 88U,
        static_cast<std::int32_t>(second_vertices));
    fixture::studio_write_i32le(bytes, second_model + 96U,
        static_cast<std::int32_t>(second_normal_bones));
    fixture::studio_write_i32le(bytes, second_model + 100U,
        static_cast<std::int32_t>(second_normals));
    fixture::studio_write_i32le(bytes, second_mesh + 4U,
        static_cast<std::int32_t>(second_commands));
    fixture::studio_write_i32le(bytes,
        bodyparts_offset + studio::kGoldSrcStudioBodyPartWireSize + 72U,
        static_cast<std::int32_t>(second_model));
    fixture::studio_write_i32le(bytes, 72U, static_cast<std::int32_t>(bytes.size()));
    fixture::studio_write_i32le(bytes, 196U, 2);
    fixture::studio_write_i32le(bytes, 200U, static_cast<std::int32_t>(skins_offset));
    fixture::studio_write_i32le(bytes, 204U, 2);
    fixture::studio_write_i32le(bytes, 208U,
        static_cast<std::int32_t>(bodyparts_offset));

    const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
    const auto result = studio::GoldSrcStudioParser::parse(bundle);
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    REQUIRE(result.document->skeletal_model.bodyparts.size() == 2U);
    REQUIRE(result.document->skeletal_model.submodels.size() == 2U);
    REQUIRE(result.document->skeletal_model.skin_families.size() == 2U);
    REQUIRE(result.document->skeletal_model.skin_families[0U].texture_indices ==
        result.document->skeletal_model.skin_families[1U].texture_indices);

    auto limits = studio::GoldSrcStudioModelImportLimits{};
    limits.maximum_bodyparts = 1U;
    const auto rejected = studio::GoldSrcStudioParser::parse(bundle, limits);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error->code ==
        studio::GoldSrcStudioErrorCode::count_limit_exceeded);
}

TEST_CASE("Studio mesh and submodel limits reject limit plus one before allocation",
    "[goldsrc-studio][bodyparts][exact-limit]")
{
    const auto baseline = fixture::literal_minimal_goldsrc_studio_v10();
    auto limits = studio::GoldSrcStudioModelImportLimits{};
    limits.maximum_meshes = 1U;
    limits.maximum_submodels = 1U;
    limits.maximum_models_per_bodypart = 1U;
    const studio::GoldSrcStudioSourceBundleView baseline_bundle{
        baseline, std::nullopt, {}};
    REQUIRE(studio::GoldSrcStudioParser::parse(baseline_bundle, limits));

    auto too_many_meshes = baseline;
    fixture::studio_write_i32le(too_many_meshes,
        fixture::kSyntheticStudioSubmodelOffset + 72U, 2);
    const studio::GoldSrcStudioSourceBundleView mesh_bundle{
        too_many_meshes, std::nullopt, {}};
    REQUIRE_FALSE(studio::GoldSrcStudioParser::parse(mesh_bundle, limits));

    auto too_many_models = baseline;
    fixture::studio_write_i32le(too_many_models,
        fixture::kSyntheticStudioBodyPartOffset + 64U, 2);
    const studio::GoldSrcStudioSourceBundleView model_bundle{
        too_many_models, std::nullopt, {}};
    const auto per_bodypart = studio::GoldSrcStudioParser::parse(
        model_bundle, limits);
    REQUIRE_FALSE(per_bodypart);
    REQUIRE(per_bodypart.error->code ==
        studio::GoldSrcStudioErrorCode::count_limit_exceeded);

    limits.maximum_models_per_bodypart = 2U;
    const auto aggregate = studio::GoldSrcStudioParser::parse(
        model_bundle, limits);
    REQUIRE_FALSE(aggregate);
    REQUIRE(aggregate.error->code ==
        studio::GoldSrcStudioErrorCode::count_limit_exceeded);
}

TEST_CASE("Studio unsupported deformation-group zero-count offsets stay bounded",
    "[goldsrc-studio][bodyparts][groups][mutation]")
{
    for (const auto offset : std::array<std::int32_t, 2U>{
             -1, static_cast<std::int32_t>(
                     fixture::kSyntheticStudioSourceSize + 1U)}) {
        INFO(offset);
        auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
        fixture::studio_write_i32le(bytes,
            fixture::kSyntheticStudioSubmodelOffset + 108U, offset);
        const auto result = studio::GoldSrcStudioParser::parse(
            studio::GoldSrcStudioSourceBundleView{bytes, std::nullopt, {}});
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code ==
            studio::GoldSrcStudioErrorCode::invalid_submodel);
    }
}

} // namespace
