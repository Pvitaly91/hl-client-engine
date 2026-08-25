#include <hlclient/goldsrc/studio/goldsrc_studio_format.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_model_importer.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_parser.hpp>

#include "goldsrc_studio_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace {

namespace studio = hlclient::goldsrc::studio;
namespace fixture = hlclient::tests;

TEST_CASE("Studio v10 wire sizes are exact named constants",
    "[goldsrc-studio][header][wire]")
{
    STATIC_REQUIRE(studio::kGoldSrcStudioHeaderWireSize == 244U);
    STATIC_REQUIRE(studio::kGoldSrcStudioSequenceHeaderWireSize == 76U);
    STATIC_REQUIRE(studio::kGoldSrcStudioVersion == 10);
    REQUIRE(fixture::literal_minimal_goldsrc_studio_v10().size() ==
        fixture::kSyntheticStudioSourceSize);
}

TEST_CASE("Studio allocation hard profile accepts exact caps and rejects cap plus one",
    "[goldsrc-studio][header][configuration][hard-limit]")
{
    using Limits = studio::GoldSrcStudioModelImportLimits;
    using SizeMember = std::size_t Limits::*;
    struct Field {
        SizeMember member;
        std::size_t hard_limit;
    };
    const std::array fields{
        Field{&Limits::maximum_main_source_bytes,
            studio::kGoldSrcStudioHardMaximumMainSourceBytes},
        Field{&Limits::maximum_companion_source_bytes,
            studio::kGoldSrcStudioHardMaximumCompanionSourceBytes},
        Field{&Limits::maximum_total_bundle_bytes,
            studio::kGoldSrcStudioHardMaximumTotalBundleBytes},
        Field{&Limits::maximum_hitboxes,
            studio::kGoldSrcStudioHardMaximumHitboxes},
        Field{&Limits::maximum_attachments,
            studio::kGoldSrcStudioHardMaximumAttachments},
        Field{&Limits::maximum_events,
            studio::kGoldSrcStudioHardMaximumEvents},
        Field{&Limits::maximum_pivots,
            studio::kGoldSrcStudioHardMaximumPivots},
        Field{&Limits::maximum_animation_blends,
            studio::kGoldSrcStudioHardMaximumAnimationBlends},
        Field{&Limits::maximum_animation_tracks,
            studio::kGoldSrcStudioHardMaximumAnimationTracks},
        Field{&Limits::maximum_animation_runs,
            studio::kGoldSrcStudioHardMaximumAnimationRuns},
        Field{&Limits::maximum_animation_value_bytes,
            studio::kGoldSrcStudioHardMaximumAnimationValueBytes},
        Field{&Limits::maximum_submodels,
            studio::kGoldSrcStudioHardMaximumSubmodels},
        Field{&Limits::maximum_total_triangles,
            studio::kGoldSrcStudioHardMaximumTotalTriangles},
        Field{&Limits::maximum_triangle_commands,
            studio::kGoldSrcStudioHardMaximumTriangleCommands},
        Field{&Limits::maximum_output_vertices,
            studio::kGoldSrcStudioHardMaximumOutputVertices},
        Field{&Limits::maximum_output_indices,
            studio::kGoldSrcStudioHardMaximumOutputIndices},
        Field{&Limits::maximum_skin_families,
            studio::kGoldSrcStudioHardMaximumSkinFamilies},
        Field{&Limits::maximum_total_rgba_bytes,
            studio::kGoldSrcStudioHardMaximumTotalRgbaBytes},
    };
    for (const auto& field : fields) {
        INFO(field.hard_limit);
        auto exact = Limits{};
        exact.*(field.member) = field.hard_limit;
        REQUIRE(studio::valid_goldsrc_studio_model_import_limits(exact));
        auto over = exact;
        over.*(field.member) = field.hard_limit + 1U;
        REQUIRE_FALSE(studio::valid_goldsrc_studio_model_import_limits(over));
    }

    auto dimension = Limits{};
    dimension.maximum_texture_dimension =
        studio::kGoldSrcStudioHardMaximumTextureDimension;
    REQUIRE(studio::valid_goldsrc_studio_model_import_limits(dimension));
    ++dimension.maximum_texture_dimension;
    REQUIRE_FALSE(studio::valid_goldsrc_studio_model_import_limits(dimension));

    auto inconsistent = Limits{};
    inconsistent.maximum_main_source_bytes = studio::kGoldSrcStudioHeaderWireSize;
    inconsistent.maximum_total_bundle_bytes =
        inconsistent.maximum_main_source_bytes - 1U;
    REQUIRE_FALSE(studio::valid_goldsrc_studio_model_import_limits(inconsistent));
}

TEST_CASE("The independent literal Studio header and model parse completely",
    "[goldsrc-studio][header][literal]")
{
    const auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    const auto header = studio::GoldSrcStudioWireDecoder::header(bytes);
    REQUIRE(header);
    REQUIRE(header->declared_length == static_cast<std::int32_t>(bytes.size()));
    REQUIRE(header->bones.count == 1);
    REQUIRE(header->bodyparts.count == 1);
    REQUIRE(header->textures.count == 1);

    const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
    const auto parsed = studio::GoldSrcStudioParser::parse(bundle);
    INFO((parsed.error ? parsed.error->context : std::string{}));
    REQUIRE(parsed);
    REQUIRE(parsed.document->skeletal_model.statistics.bone_count == 1U);
    REQUIRE(parsed.document->skeletal_model.statistics.emitted_triangle_count == 1U);
}

TEST_CASE("Studio sound metadata fields must all be zero",
    "[goldsrc-studio][header][sound-group][mutation]")
{
    const auto baseline = fixture::literal_minimal_goldsrc_studio_v10();
    REQUIRE(studio::GoldSrcStudioParser::inspect_dependencies(baseline));

    for (const auto field : std::array{
             studio::kGoldSrcStudioHeaderSoundTableOffset,
             studio::kGoldSrcStudioHeaderSoundIndexOffset,
             studio::kGoldSrcStudioHeaderSoundGroupsOffset,
             studio::kGoldSrcStudioHeaderSoundGroupOffset}) {
        INFO(field);
        auto positive = baseline;
        fixture::studio_write_i32le(positive, field, 1);
        const auto positive_result =
            studio::GoldSrcStudioParser::inspect_dependencies(positive);
        REQUIRE_FALSE(positive_result);
        REQUIRE_FALSE(positive_result.plan);
        REQUIRE(positive_result.error->code ==
            studio::GoldSrcStudioErrorCode::unsupported_sound_group);

        auto negative = baseline;
        fixture::studio_write_i32le(negative, field, -1);
        const auto negative_result =
            studio::GoldSrcStudioParser::inspect_dependencies(negative);
        REQUIRE_FALSE(negative_result);
        REQUIRE_FALSE(negative_result.plan);
        REQUIRE(negative_result.error->code ==
            studio::GoldSrcStudioErrorCode::negative_count_or_offset);
    }
}

TEST_CASE("Every strict Studio header truncation is rejected without publication",
    "[goldsrc-studio][header][truncation]")
{
    const auto baseline = fixture::literal_minimal_goldsrc_studio_v10();
    for (std::size_t size = 0U; size < studio::kGoldSrcStudioHeaderWireSize; ++size) {
        INFO(size);
        const std::vector<std::byte> truncated(baseline.begin(),
            baseline.begin() + static_cast<std::ptrdiff_t>(size));
        const auto result = studio::GoldSrcStudioParser::inspect_dependencies(truncated);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        REQUIRE((result.error->code == studio::GoldSrcStudioErrorCode::source_too_small ||
            result.error->code == studio::GoldSrcStudioErrorCode::unsupported_identifier));
    }
}

TEST_CASE("Studio identifier version and exact declared length fail closed",
    "[goldsrc-studio][header][mutation]")
{
    SECTION("wrong identifier")
    {
        auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
        bytes[0U] = std::byte{0};
        const auto result = studio::GoldSrcStudioParser::inspect_dependencies(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == studio::GoldSrcStudioErrorCode::unsupported_identifier);
    }
    SECTION("wrong version")
    {
        auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
        fixture::studio_write_i32le(bytes, 4U, 11);
        const auto result = studio::GoldSrcStudioParser::inspect_dependencies(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == studio::GoldSrcStudioErrorCode::unsupported_version);
    }
    SECTION("negative length")
    {
        auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
        fixture::studio_write_i32le(bytes, 72U, -1);
        const auto result = studio::GoldSrcStudioParser::inspect_dependencies(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code ==
            studio::GoldSrcStudioErrorCode::invalid_declared_length);
    }
    SECTION("length below header")
    {
        auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
        fixture::studio_write_i32le(bytes, 72U, 243);
        const auto result = studio::GoldSrcStudioParser::inspect_dependencies(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code ==
            studio::GoldSrcStudioErrorCode::invalid_declared_length);
    }
    SECTION("length beyond source")
    {
        auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
        fixture::studio_write_i32le(bytes, 72U,
            static_cast<std::int32_t>(bytes.size() + 1U));
        const auto result = studio::GoldSrcStudioParser::inspect_dependencies(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code ==
            studio::GoldSrcStudioErrorCode::invalid_declared_length);
    }
    SECTION("unexplained suffix")
    {
        auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
        bytes.push_back(std::byte{0});
        const auto result = studio::GoldSrcStudioParser::inspect_dependencies(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code ==
            studio::GoldSrcStudioErrorCode::unexplained_trailing_data);
    }
}

TEST_CASE("Studio fixed strings accept the exact configured limit and reject limit plus one",
    "[goldsrc-studio][header][string][exact-limit]")
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    fixture::studio_write_fixed_string(bytes, 8U, 64U, std::string(64U, 'x'));
    const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
    auto limits = studio::GoldSrcStudioModelImportLimits{};
    limits.maximum_string_bytes = 64U;
    REQUIRE(studio::GoldSrcStudioParser::parse(bundle, limits));
    limits.maximum_string_bytes = 63U;
    const auto rejected = studio::GoldSrcStudioParser::parse(bundle, limits);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error->code == studio::GoldSrcStudioErrorCode::invalid_string);
}

TEST_CASE("Studio one-record directory limits reject deterministic limit plus one counts",
    "[goldsrc-studio][header][directories][exact-limit]")
{
    const auto baseline = fixture::literal_minimal_goldsrc_studio_v10();
    auto limits = studio::GoldSrcStudioModelImportLimits{};
    limits.maximum_hitboxes = 1U;
    limits.maximum_attachments = 1U;
    limits.maximum_sequence_groups = 1U;
    limits.maximum_skin_references = 1U;
    limits.maximum_skin_families = 1U;
    REQUIRE(studio::GoldSrcStudioParser::inspect_dependencies(baseline, limits));

    for (const auto count_offset :
        std::array<std::size_t, 5U>{156U, 172U, 192U, 196U, 212U}) {
        INFO(count_offset);
        auto bytes = baseline;
        fixture::studio_write_i32le(bytes, count_offset, 2);
        const auto rejected =
            studio::GoldSrcStudioParser::inspect_dependencies(bytes, limits);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error->code ==
            studio::GoldSrcStudioErrorCode::count_limit_exceeded);
    }
}

TEST_CASE("Studio source and signed directory limits are checked exactly",
    "[goldsrc-studio][header][limits]")
{
    const auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    auto limits = studio::GoldSrcStudioModelImportLimits{};
    limits.maximum_main_source_bytes = bytes.size();
    REQUIRE(studio::GoldSrcStudioParser::inspect_dependencies(bytes, limits));
    limits.maximum_main_source_bytes = bytes.size() - 1U;
    const auto over = studio::GoldSrcStudioParser::inspect_dependencies(bytes, limits);
    REQUIRE_FALSE(over);
    REQUIRE(over.error->code == studio::GoldSrcStudioErrorCode::source_limit_exceeded);

    auto negative = bytes;
    fixture::studio_write_i32le(negative, 140U, -1);
    const auto invalid = studio::GoldSrcStudioParser::inspect_dependencies(negative);
    REQUIRE_FALSE(invalid);
    REQUIRE(invalid.error->code ==
        studio::GoldSrcStudioErrorCode::negative_count_or_offset);
}

TEST_CASE("Studio importer probes signature first and publishes an owning skeletal model",
    "[goldsrc-studio][importer][probe]")
{
    auto created = hlclient::assets::AssetSource::create(
        "extensionless", fixture::literal_minimal_goldsrc_studio_v10());
    REQUIRE(created);
    studio::GoldSrcStudioModelImporter importer;
    REQUIRE(importer.id() == studio::kGoldSrcStudioModelImporterId);
    REQUIRE(importer.probe(hlclient::assets::make_asset_probe(*created.source)) >=
        studio::kGoldSrcStudioDirectoryProbeConfidence);
    const auto imported = importer.import(*created.source);
    REQUIRE(imported);
    REQUIRE(imported.value().skeletal_data);
    REQUIRE(imported.value().skeletal_data->statistics.emitted_triangle_count == 1U);

    auto sequence = hlclient::assets::AssetSource::create(
        "models/not-top-level.mdl", fixture::synthetic_sequence_group_01());
    REQUIRE(sequence);
    REQUIRE(importer.probe(hlclient::assets::make_asset_probe(*sequence.source)) ==
        hlclient::assets::kAssetProbeNoMatch);
}

TEST_CASE("Studio header range overflow and limit plus one are rejected",
    "[goldsrc-studio][header][range][exact-limit]")
{
    const auto baseline = fixture::literal_minimal_goldsrc_studio_v10();
    auto limits = studio::GoldSrcStudioModelImportLimits{};
    limits.maximum_bones = 1U;
    REQUIRE(studio::GoldSrcStudioParser::inspect_dependencies(baseline, limits));

    auto too_many = baseline;
    fixture::studio_write_i32le(too_many, 140U, 2);
    const auto over_limit = studio::GoldSrcStudioParser::inspect_dependencies(
        too_many, limits);
    REQUIRE_FALSE(over_limit);
    REQUIRE(over_limit.error->code ==
        studio::GoldSrcStudioErrorCode::count_limit_exceeded);

    auto negative_offset = baseline;
    fixture::studio_write_i32le(negative_offset, 144U, -1);
    REQUIRE_FALSE(studio::GoldSrcStudioParser::inspect_dependencies(negative_offset));

    auto out_of_bounds = baseline;
    fixture::studio_write_i32le(out_of_bounds, 144U,
        std::numeric_limits<std::int32_t>::max());
    const auto invalid_range = studio::GoldSrcStudioParser::inspect_dependencies(
        out_of_bounds);
    REQUIRE_FALSE(invalid_range);
    REQUIRE((invalid_range.error->code ==
                 studio::GoldSrcStudioErrorCode::range_out_of_bounds ||
        invalid_range.error->code == studio::GoldSrcStudioErrorCode::range_overflow));
}

TEST_CASE("Deterministic Studio structural mutation corpus never partially publishes",
    "[goldsrc-studio][mutation-corpus]")
{
    for (std::size_t mutation = 0U; mutation < 7U; ++mutation) {
        INFO(mutation);
        auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
        switch (mutation) {
        case 0U:
            fixture::studio_write_i32le(bytes, 144U, 1);
            break;
        case 1U:
            fixture::studio_write_i32le(
                bytes, fixture::kSyntheticStudioBoneOffset + 32U, 4);
            break;
        case 2U:
            fixture::studio_write_i32le(
                bytes, fixture::kSyntheticStudioMeshOffset + 4U, 0);
            break;
        case 3U:
            fixture::studio_write_i16le(bytes, fixture::kSyntheticStudioSkinOffset, 4);
            break;
        case 4U:
            fixture::studio_write_i32le(
                bytes, fixture::kSyntheticStudioTextureOffset + 76U, 0);
            break;
        case 5U:
            fixture::studio_write_i32le(
                bytes, fixture::kSyntheticStudioSequenceOffset + 56U, 0);
            break;
        case 6U:
            bytes[fixture::kSyntheticStudioAnimationStreamOffset + 1U] = std::byte{0};
            break;
        default: break;
        }
        const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
        const auto result = studio::GoldSrcStudioParser::parse(bundle);
        REQUIRE_FALSE(result);
        REQUIRE_FALSE(result.document.has_value());
        REQUIRE(result.error);
    }
}

} // namespace
