#include <hlclient/goldsrc/studio/goldsrc_studio_parser.hpp>

#include "goldsrc_studio_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace {

namespace studio = hlclient::goldsrc::studio;
namespace fixture = hlclient::tests;

[[nodiscard]] std::vector<std::byte> two_bone_fixture(
    const std::int32_t first_parent,
    const std::int32_t second_parent)
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    const auto bone_offset = bytes.size();
    bytes.resize(bytes.size() + 2U * studio::kGoldSrcStudioBoneWireSize);
    std::copy_n(bytes.begin() +
            static_cast<std::ptrdiff_t>(fixture::kSyntheticStudioBoneOffset),
        studio::kGoldSrcStudioBoneWireSize,
        bytes.begin() + static_cast<std::ptrdiff_t>(bone_offset));
    std::copy_n(bytes.begin() +
            static_cast<std::ptrdiff_t>(fixture::kSyntheticStudioBoneOffset),
        studio::kGoldSrcStudioBoneWireSize,
        bytes.begin() + static_cast<std::ptrdiff_t>(
            bone_offset + studio::kGoldSrcStudioBoneWireSize));
    fixture::studio_write_fixed_string(bytes, bone_offset, 32U, "root-a");
    fixture::studio_write_fixed_string(bytes,
        bone_offset + studio::kGoldSrcStudioBoneWireSize, 32U, "root-b");
    fixture::studio_write_i32le(bytes, bone_offset + 32U, first_parent);
    fixture::studio_write_i32le(bytes,
        bone_offset + studio::kGoldSrcStudioBoneWireSize + 32U, second_parent);

    const auto animation_offset = bytes.size();
    bytes.resize(bytes.size() + 2U * studio::kGoldSrcStudioAnimationOffsetWireSize + 4U);
    fixture::studio_write_u16le(bytes, animation_offset, 24U);
    bytes[animation_offset + 24U] = std::byte{1};
    bytes[animation_offset + 25U] = std::byte{1};
    fixture::studio_write_i16le(bytes, animation_offset + 26U, 2);
    fixture::studio_write_i32le(bytes, 72U, static_cast<std::int32_t>(bytes.size()));
    fixture::studio_write_i32le(bytes, 140U, 2);
    fixture::studio_write_i32le(bytes, 144U, static_cast<std::int32_t>(bone_offset));
    fixture::studio_write_i32le(bytes,
        fixture::kSyntheticStudioSequenceOffset + 124U,
        static_cast<std::int32_t>(animation_offset));
    return bytes;
}

TEST_CASE("Studio skeleton hitbox and attachment are owning neutral metadata",
    "[goldsrc-studio][skeleton][owning]")
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
    auto parsed = studio::GoldSrcStudioParser::parse(bundle);
    REQUIRE(parsed);
    bytes.clear();
    bytes.shrink_to_fit();
    REQUIRE(parsed.document->skeletal_model.bones.size() == 1U);
    REQUIRE(parsed.document->skeletal_model.bones[0U].name == "root");
    REQUIRE(parsed.document->skeletal_model.bones[0U].parent_index == -1);
    REQUIRE(parsed.document->skeletal_model.hitboxes.size() == 1U);
    REQUIRE(parsed.document->skeletal_model.attachments.size() == 1U);
    REQUIRE(parsed.document->skeletal_model.attachments[0U].name == "tag");
}

TEST_CASE("Studio skeleton rejects self parents invalid parents and cycles",
    "[goldsrc-studio][skeleton][graph]")
{
    SECTION("self parent")
    {
        auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
        fixture::studio_write_i32le(bytes, fixture::kSyntheticStudioBoneOffset + 32U, 0);
        const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
        const auto result = studio::GoldSrcStudioParser::parse(bundle);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == studio::GoldSrcStudioErrorCode::invalid_skeleton);
    }
    SECTION("invalid parent")
    {
        auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
        fixture::studio_write_i32le(bytes, fixture::kSyntheticStudioBoneOffset + 32U, 1);
        const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
        const auto result = studio::GoldSrcStudioParser::parse(bundle);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == studio::GoldSrcStudioErrorCode::invalid_skeleton);
    }
}

TEST_CASE("Studio hitbox and attachment references fail transactionally",
    "[goldsrc-studio][skeleton][references]")
{
    auto hitbox = fixture::literal_minimal_goldsrc_studio_v10();
    fixture::studio_write_i32le(hitbox, fixture::kSyntheticStudioHitboxOffset, 1);
    const studio::GoldSrcStudioSourceBundleView hitbox_bundle{hitbox, std::nullopt, {}};
    REQUIRE_FALSE(studio::GoldSrcStudioParser::parse(hitbox_bundle));

    auto attachment = fixture::literal_minimal_goldsrc_studio_v10();
    fixture::studio_write_i32le(attachment,
        fixture::kSyntheticStudioAttachmentOffset + 36U, 1);
    const studio::GoldSrcStudioSourceBundleView attachment_bundle{
        attachment, std::nullopt, {}};
    REQUIRE_FALSE(studio::GoldSrcStudioParser::parse(attachment_bundle));

    auto bounds = fixture::literal_minimal_goldsrc_studio_v10();
    fixture::studio_write_f32le(bounds,
        fixture::kSyntheticStudioHitboxOffset + 8U, 2.0F);
    const studio::GoldSrcStudioSourceBundleView bounds_bundle{
        bounds, std::nullopt, {}};
    const auto invalid_bounds = studio::GoldSrcStudioParser::parse(bounds_bundle);
    REQUIRE_FALSE(invalid_bounds);
    REQUIRE(invalid_bounds.error->code ==
        studio::GoldSrcStudioErrorCode::invalid_hitbox);
}

TEST_CASE("Studio skeleton supports multiple roots and parent chains but rejects cycles",
    "[goldsrc-studio][skeleton][multi-root][cycle]")
{
    SECTION("multiple roots")
    {
        const auto bytes = two_bone_fixture(-1, -1);
        const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
        const auto result = studio::GoldSrcStudioParser::parse(bundle);
        REQUIRE(result);
        REQUIRE(result.document->skeletal_model.bones.size() == 2U);
        REQUIRE(result.document->skeletal_model.bones[1U].parent_index == -1);
    }
    SECTION("parent chain")
    {
        const auto bytes = two_bone_fixture(-1, 0);
        const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
        const auto result = studio::GoldSrcStudioParser::parse(bundle);
        REQUIRE(result);
        REQUIRE(result.document->skeletal_model.bones[1U].parent_index == 0);
    }
    SECTION("cycle")
    {
        const auto bytes = two_bone_fixture(1, 0);
        const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
        const auto result = studio::GoldSrcStudioParser::parse(bundle);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == studio::GoldSrcStudioErrorCode::invalid_skeleton);
    }
}

TEST_CASE("Studio bone controllers validate references and supported type bits",
    "[goldsrc-studio][skeleton][controller]")
{
    auto make_controller = [] {
        auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
        const auto offset = bytes.size();
        bytes.resize(bytes.size() + studio::kGoldSrcStudioBoneControllerWireSize);
        fixture::studio_write_i32le(bytes, 72U,
            static_cast<std::int32_t>(bytes.size()));
        fixture::studio_write_i32le(bytes, 148U, 1);
        fixture::studio_write_i32le(bytes, 152U, static_cast<std::int32_t>(offset));
        fixture::studio_write_i32le(bytes,
            fixture::kSyntheticStudioBoneOffset + 40U, 0);
        fixture::studio_write_i32le(bytes, offset, 0);
        fixture::studio_write_i32le(bytes, offset + 4U, 1);
        fixture::studio_write_f32le(bytes, offset + 8U, -1.0F);
        fixture::studio_write_f32le(bytes, offset + 12U, 1.0F);
        fixture::studio_write_i32le(bytes, offset + 16U, 127);
        fixture::studio_write_i32le(bytes, offset + 20U, 0);
        return bytes;
    };

    auto valid = make_controller();
    const studio::GoldSrcStudioSourceBundleView valid_bundle{valid, std::nullopt, {}};
    auto limits = studio::GoldSrcStudioModelImportLimits{};
    limits.maximum_bone_controllers = 1U;
    const auto parsed = studio::GoldSrcStudioParser::parse(valid_bundle, limits);
    REQUIRE(parsed);
    REQUIRE(parsed.document->skeletal_model.bone_controllers.size() == 1U);

    auto too_many = valid;
    fixture::studio_write_i32le(too_many, 148U, 2);
    const studio::GoldSrcStudioSourceBundleView too_many_bundle{
        too_many, std::nullopt, {}};
    const auto over_limit = studio::GoldSrcStudioParser::parse(
        too_many_bundle, limits);
    REQUIRE_FALSE(over_limit);
    REQUIRE(over_limit.error->code ==
        studio::GoldSrcStudioErrorCode::count_limit_exceeded);

    auto invalid = make_controller();
    const auto controller_offset = invalid.size() -
                                   studio::kGoldSrcStudioBoneControllerWireSize;
    fixture::studio_write_i32le(invalid, controller_offset + 4U, 0x40000000);
    const studio::GoldSrcStudioSourceBundleView invalid_bundle{
        invalid, std::nullopt, {}};
    const auto rejected = studio::GoldSrcStudioParser::parse(invalid_bundle);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error->code == studio::GoldSrcStudioErrorCode::invalid_controller);

    auto wrong_bone = make_controller();
    const auto wrong_bone_offset = wrong_bone.size() -
                                   studio::kGoldSrcStudioBoneControllerWireSize;
    fixture::studio_write_i32le(wrong_bone, wrong_bone_offset, -1);
    const auto mismatched_bone = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{wrong_bone, std::nullopt, {}});
    REQUIRE_FALSE(mismatched_bone);
    REQUIRE(mismatched_bone.error->code ==
        studio::GoldSrcStudioErrorCode::invalid_controller);

    auto wrong_axis = make_controller();
    const auto wrong_axis_offset = wrong_axis.size() -
                                   studio::kGoldSrcStudioBoneControllerWireSize;
    fixture::studio_write_i32le(wrong_axis, wrong_axis_offset + 4U, 2);
    const auto mismatched_axis = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{wrong_axis, std::nullopt, {}});
    REQUIRE_FALSE(mismatched_axis);
    REQUIRE(mismatched_axis.error->code ==
        studio::GoldSrcStudioErrorCode::invalid_controller);
}

} // namespace
