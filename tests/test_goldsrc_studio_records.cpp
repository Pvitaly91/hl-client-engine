#include <hlclient/goldsrc/studio/goldsrc_studio_format.hpp>

#include "goldsrc_studio_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

namespace studio = hlclient::goldsrc::studio;
namespace fixture = hlclient::tests;

TEST_CASE("Every Studio fixed record has the public Valve v10 wire size",
    "[goldsrc-studio][records][wire]")
{
    STATIC_REQUIRE(studio::kGoldSrcStudioBoneWireSize == 112U);
    STATIC_REQUIRE(studio::kGoldSrcStudioBoneControllerWireSize == 24U);
    STATIC_REQUIRE(studio::kGoldSrcStudioHitboxWireSize == 32U);
    STATIC_REQUIRE(studio::kGoldSrcStudioSequenceGroupWireSize == 104U);
    STATIC_REQUIRE(studio::kGoldSrcStudioSequenceWireSize == 176U);
    STATIC_REQUIRE(studio::kGoldSrcStudioSequenceEventWireSize == 76U);
    STATIC_REQUIRE(studio::kGoldSrcStudioPivotWireSize == 20U);
    STATIC_REQUIRE(studio::kGoldSrcStudioAttachmentWireSize == 88U);
    STATIC_REQUIRE(studio::kGoldSrcStudioAnimationOffsetWireSize == 12U);
    STATIC_REQUIRE(studio::kGoldSrcStudioBodyPartWireSize == 76U);
    STATIC_REQUIRE(studio::kGoldSrcStudioTextureWireSize == 80U);
    STATIC_REQUIRE(studio::kGoldSrcStudioSubmodelWireSize == 112U);
    STATIC_REQUIRE(studio::kGoldSrcStudioMeshWireSize == 20U);
    STATIC_REQUIRE(studio::kGoldSrcStudioTriangleCommandVertexWireSize == 8U);
    STATIC_REQUIRE(studio::kGoldSrcStudioSkinReferenceWireSize == 2U);
    STATIC_REQUIRE(studio::kGoldSrcStudioSourceVertexWireSize == 12U);
    STATIC_REQUIRE(studio::kGoldSrcStudioSourceNormalWireSize == 12U);
}

TEST_CASE("Studio fixed records decode independently without ABI casts",
    "[goldsrc-studio][records][decode]")
{
    const auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    REQUIRE(studio::GoldSrcStudioWireDecoder::bone(
        bytes, fixture::kSyntheticStudioBoneOffset)->name == "root");
    REQUIRE(studio::GoldSrcStudioWireDecoder::hitbox(
        bytes, fixture::kSyntheticStudioHitboxOffset)->bone == 0);
    REQUIRE(studio::GoldSrcStudioWireDecoder::attachment(
        bytes, fixture::kSyntheticStudioAttachmentOffset)->name == "tag");
    REQUIRE(studio::GoldSrcStudioWireDecoder::animation_offset(
        bytes, fixture::kSyntheticStudioAnimationOffset)->channel_offsets[0U] == 12U);
    REQUIRE(studio::GoldSrcStudioWireDecoder::sequence(
        bytes, fixture::kSyntheticStudioSequenceOffset)->label == "idle");
    REQUIRE(studio::GoldSrcStudioWireDecoder::sequence_group(
        bytes, fixture::kSyntheticStudioSequenceGroupOffset)->label == "default");
    REQUIRE(studio::GoldSrcStudioWireDecoder::bodypart(
        bytes, fixture::kSyntheticStudioBodyPartOffset)->model_count == 1);
    REQUIRE(studio::GoldSrcStudioWireDecoder::submodel(
        bytes, fixture::kSyntheticStudioSubmodelOffset)->vertex_count == 3);
    REQUIRE(studio::GoldSrcStudioWireDecoder::mesh(
        bytes, fixture::kSyntheticStudioMeshOffset)->triangle_count == 1);
    REQUIRE(studio::GoldSrcStudioWireDecoder::texture(
        bytes, fixture::kSyntheticStudioTextureOffset)->width == 1);
}

TEST_CASE("Fixed record decoders reject truncation and non-finite floats",
    "[goldsrc-studio][records][invalid]")
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    REQUIRE_FALSE(studio::GoldSrcStudioWireDecoder::bone(
        std::span<const std::byte>{bytes}.first(
            fixture::kSyntheticStudioBoneOffset + studio::kGoldSrcStudioBoneWireSize - 1U),
        fixture::kSyntheticStudioBoneOffset));

    fixture::studio_write_u32le(bytes, fixture::kSyntheticStudioBoneOffset + 64U,
        std::bit_cast<std::uint32_t>(std::numeric_limits<float>::infinity()));
    REQUIRE_FALSE(studio::GoldSrcStudioWireDecoder::bone(
        bytes, fixture::kSyntheticStudioBoneOffset));

    bytes = fixture::literal_minimal_goldsrc_studio_v10();
    fixture::studio_write_u32le(bytes, fixture::kSyntheticStudioAttachmentOffset + 40U,
        std::bit_cast<std::uint32_t>(
            std::numeric_limits<float>::quiet_NaN()));
    REQUIRE_FALSE(studio::GoldSrcStudioWireDecoder::attachment(
        bytes, fixture::kSyntheticStudioAttachmentOffset));
}

TEST_CASE("Controller event and pivot records decode at literal wire offsets",
    "[goldsrc-studio][records][independent]")
{
    std::vector<std::byte> controller(studio::kGoldSrcStudioBoneControllerWireSize);
    fixture::studio_write_i32le(controller, 0U, 0);
    fixture::studio_write_i32le(controller, 4U, 1);
    fixture::studio_write_f32le(controller, 8U, -2.0F);
    fixture::studio_write_f32le(controller, 12U, 2.0F);
    fixture::studio_write_i32le(controller, 16U, 127);
    fixture::studio_write_i32le(controller, 20U, 0);
    const auto decoded_controller =
        studio::GoldSrcStudioWireDecoder::bone_controller(controller, 0U);
    REQUIRE(decoded_controller);
    REQUIRE(decoded_controller->start == -2.0F);
    REQUIRE(decoded_controller->end == 2.0F);

    std::vector<std::byte> event(studio::kGoldSrcStudioSequenceEventWireSize);
    fixture::studio_write_i32le(event, 0U, 3);
    fixture::studio_write_i32le(event, 4U, 5001);
    fixture::studio_write_i32le(event, 8U, 1);
    event[12U] = std::byte{0x41};
    const auto decoded_event = studio::GoldSrcStudioWireDecoder::sequence_event(
        event, 0U);
    REQUIRE(decoded_event);
    REQUIRE(decoded_event->frame == 3);
    REQUIRE(decoded_event->options[0U] == std::byte{0x41});

    std::vector<std::byte> pivot(studio::kGoldSrcStudioPivotWireSize);
    fixture::studio_write_vector3(pivot, 0U, 1.0F, 2.0F, 3.0F);
    fixture::studio_write_i32le(pivot, 12U, 4);
    fixture::studio_write_i32le(pivot, 16U, 5);
    const auto decoded_pivot = studio::GoldSrcStudioWireDecoder::pivot(pivot, 0U);
    REQUIRE(decoded_pivot);
    REQUIRE(decoded_pivot->origin.z == 3.0F);
    REQUIRE(decoded_pivot->end == 5);
}

TEST_CASE("Wrong-endian Studio leading fields do not decode as v10",
    "[goldsrc-studio][records][endian]")
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    bytes[4U] = std::byte{0};
    bytes[5U] = std::byte{0};
    bytes[6U] = std::byte{0};
    bytes[7U] = std::byte{10};
    REQUIRE_FALSE(studio::GoldSrcStudioWireDecoder::header(bytes));
}

} // namespace
