#include <hlclient/goldsrc/studio/goldsrc_studio_animation.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_parser.hpp>

#include "goldsrc_studio_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace studio = hlclient::goldsrc::studio;
namespace fixture = hlclient::tests;

[[nodiscard]] studio::GoldSrcStudioAnimationChannelParseResult parse_channel(
    const std::vector<std::byte>& bytes,
    const std::uint32_t frames)
{
    return studio::parse_goldsrc_studio_animation_channel(
        studio::GoldSrcStudioAnimationChannelParseInput{
            bytes,
            0U,
            2U,
            frames,
            assets::ModelAnimationChannelSemantic::translation_x,
            10.0F,
            0.5F,
            0U,
        });
}

[[nodiscard]] std::vector<std::byte> two_zero_offset_sequence_model()
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    std::array<std::byte, studio::kGoldSrcStudioSequenceWireSize> sequence{};
    std::copy_n(
        bytes.begin() + static_cast<std::ptrdiff_t>(
                            fixture::kSyntheticStudioSequenceOffset),
        sequence.size(), sequence.begin());

    const auto sequence_offset = bytes.size();
    const auto animation_offset =
        sequence_offset + 2U * studio::kGoldSrcStudioSequenceWireSize;
    bytes.resize(animation_offset +
                     2U * studio::kGoldSrcStudioAnimationOffsetWireSize,
        std::byte{0});
    for (std::size_t index = 0U; index < 2U; ++index) {
        const auto descriptor_offset =
            sequence_offset + index * studio::kGoldSrcStudioSequenceWireSize;
        std::copy(sequence.begin(), sequence.end(),
            bytes.begin() + static_cast<std::ptrdiff_t>(descriptor_offset));
        fixture::studio_write_i32le(bytes, descriptor_offset + 124U,
            static_cast<std::int32_t>(
                animation_offset +
                index * studio::kGoldSrcStudioAnimationOffsetWireSize));
    }
    fixture::studio_write_i32le(
        bytes, 72U, static_cast<std::int32_t>(bytes.size()));
    fixture::studio_write_i32le(bytes, 164U, 2);
    fixture::studio_write_i32le(
        bytes, 168U, static_cast<std::int32_t>(sequence_offset));
    return bytes;
}

[[nodiscard]] std::vector<std::byte> many_animation_stream_model(
    const std::size_t sequence_count)
{
    constexpr auto header_offset = studio::kGoldSrcStudioHeaderWireSize;
    constexpr auto bone_offset = header_offset;
    constexpr auto sequence_offset =
        bone_offset + studio::kGoldSrcStudioBoneWireSize;
    const auto group_offset =
        sequence_offset + sequence_count * studio::kGoldSrcStudioSequenceWireSize;
    const auto animation_offset =
        group_offset + studio::kGoldSrcStudioSequenceGroupWireSize;
    const auto stream_offset = animation_offset +
        sequence_count * studio::kGoldSrcStudioAnimationOffsetWireSize;
    std::vector<std::byte> bytes(
        stream_offset + sequence_count * 6U * 4U, std::byte{0});

    bytes[0U] = std::byte{0x49};
    bytes[1U] = std::byte{0x44};
    bytes[2U] = std::byte{0x53};
    bytes[3U] = std::byte{0x54};
    fixture::studio_write_i32le(bytes, 4U, 10);
    fixture::studio_write_fixed_string(bytes, 8U, 64U, "many-streams.mdl");
    fixture::studio_write_i32le(
        bytes, 72U, static_cast<std::int32_t>(bytes.size()));
    fixture::studio_write_i32le(bytes, 140U, 1);
    fixture::studio_write_i32le(
        bytes, 144U, static_cast<std::int32_t>(bone_offset));
    fixture::studio_write_i32le(
        bytes, 164U, static_cast<std::int32_t>(sequence_count));
    fixture::studio_write_i32le(
        bytes, 168U, static_cast<std::int32_t>(sequence_offset));
    fixture::studio_write_i32le(bytes, 172U, 1);
    fixture::studio_write_i32le(
        bytes, 176U, static_cast<std::int32_t>(group_offset));

    fixture::studio_write_fixed_string(bytes, bone_offset, 32U, "root");
    fixture::studio_write_i32le(bytes, bone_offset + 32U, -1);
    for (std::size_t channel = 0U; channel < 6U; ++channel) {
        fixture::studio_write_i32le(
            bytes, bone_offset + 40U + channel * 4U, -1);
    }
    fixture::studio_write_fixed_string(bytes, group_offset, 32U, "default");

    for (std::size_t sequence = 0U; sequence < sequence_count; ++sequence) {
        const auto descriptor =
            sequence_offset + sequence * studio::kGoldSrcStudioSequenceWireSize;
        const auto record = animation_offset +
            sequence * studio::kGoldSrcStudioAnimationOffsetWireSize;
        fixture::studio_write_fixed_string(bytes, descriptor, 32U, "idle");
        fixture::studio_write_f32le(bytes, descriptor + 32U, 30.0F);
        fixture::studio_write_i32le(bytes, descriptor + 56U, 1);
        fixture::studio_write_i32le(bytes, descriptor + 72U, 0);
        fixture::studio_write_i32le(bytes, descriptor + 120U, 1);
        fixture::studio_write_i32le(
            bytes, descriptor + 124U, static_cast<std::int32_t>(record));
        fixture::studio_write_i32le(bytes, descriptor + 152U, -1);

        for (std::size_t channel = 0U; channel < 6U; ++channel) {
            const auto stream =
                stream_offset + (sequence * 6U + channel) * 4U;
            const auto relative = stream - record;
            REQUIRE(relative <= std::numeric_limits<std::uint16_t>::max());
            fixture::studio_write_u16le(bytes, record + channel * 2U,
                static_cast<std::uint16_t>(relative));
            bytes[stream] = std::byte{1};
            bytes[stream + 1U] = std::byte{1};
            fixture::studio_write_i16le(bytes, stream + 2U,
                static_cast<std::int16_t>(channel));
        }
    }
    return bytes;
}

TEST_CASE("Studio zero-offset animation channels use neutral quantized zero",
    "[goldsrc-studio][animation][zero]")
{
    const std::vector<std::byte> bytes;
    const auto parsed = studio::parse_goldsrc_studio_animation_channel(
        studio::GoldSrcStudioAnimationChannelParseInput{
            bytes,
            0U,
            0U,
            3U,
            assets::ModelAnimationChannelSemantic::rotation_z,
            1.0F,
            2.0F,
            0U,
        });
    REQUIRE(parsed);
    REQUIRE(parsed.channel->runs.empty());
    REQUIRE(studio::StudioAnimationChannelSampler::sample_quantized(
        *parsed.channel, 2U) == 0);
    REQUIRE(studio::StudioAnimationChannelSampler::sample_default_scaled(
        *parsed.channel, 2U) == 1.0F);
}

TEST_CASE("Studio sequence group zero owns its compressed bytes in the main IDST",
    "[goldsrc-studio][animation][group-zero]")
{
    const auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    const auto result = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{bytes, std::nullopt, {}});
    REQUIRE(result);
    const auto& channel = result.document->skeletal_model.sequences[0U]
                              .animation_blends[0U]
                              .bone_tracks[0U]
                              .channels[0U];
    REQUIRE(channel.source_sequence_group_ordinal == 0U);
    REQUIRE(studio::StudioAnimationChannelSampler::sample_quantized(channel, 0U) == 2);
}

TEST_CASE("Studio sequence group zero data base is added to sequence animindex",
    "[goldsrc-studio][animation][group-zero][data-base]")
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    fixture::studio_write_i32le(bytes,
        fixture::kSyntheticStudioSequenceGroupOffset + 100U,
        static_cast<std::int32_t>(fixture::kSyntheticStudioAnimationOffset));
    fixture::studio_write_i32le(bytes,
        fixture::kSyntheticStudioSequenceOffset + 124U, 0);
    const auto result = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{bytes, std::nullopt, {}});
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    const auto& channel = result.document->skeletal_model.sequences[0U]
                              .animation_blends[0U]
                              .bone_tracks[0U]
                              .channels[0U];
    REQUIRE(studio::StudioAnimationChannelSampler::sample_quantized(channel, 0U) == 2);
}

TEST_CASE("Studio sequence group zero data base mutations fail transactionally",
    "[goldsrc-studio][animation][group-zero][data-base][mutation]")
{
    SECTION("negative base")
    {
        auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
        fixture::studio_write_i32le(bytes,
            fixture::kSyntheticStudioSequenceGroupOffset + 100U, -1);
        const auto result = studio::GoldSrcStudioParser::parse(
            studio::GoldSrcStudioSourceBundleView{bytes, std::nullopt, {}});
        REQUIRE_FALSE(result);
        REQUIRE_FALSE(result.document);
        REQUIRE(result.error->code ==
            studio::GoldSrcStudioErrorCode::invalid_sequence_group);
    }
    SECTION("resolved range exceeds the source")
    {
        auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
        fixture::studio_write_i32le(bytes,
            fixture::kSyntheticStudioSequenceGroupOffset + 100U,
            static_cast<std::int32_t>(bytes.size()));
        fixture::studio_write_i32le(bytes,
            fixture::kSyntheticStudioSequenceOffset + 124U, 1);
        const auto result = studio::GoldSrcStudioParser::parse(
            studio::GoldSrcStudioSourceBundleView{bytes, std::nullopt, {}});
        REQUIRE_FALSE(result);
        REQUIRE_FALSE(result.document);
        REQUIRE(result.error->code ==
            studio::GoldSrcStudioErrorCode::invalid_animation);
    }
    SECTION("resolved range overlaps a fixed table")
    {
        auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
        fixture::studio_write_i32le(bytes,
            fixture::kSyntheticStudioSequenceGroupOffset + 100U, 16);
        const auto result = studio::GoldSrcStudioParser::parse(
            studio::GoldSrcStudioSourceBundleView{bytes, std::nullopt, {}});
        REQUIRE_FALSE(result);
        REQUIRE_FALSE(result.document);
        REQUIRE(result.error->code == studio::GoldSrcStudioErrorCode::range_overlap);
    }
}

TEST_CASE("Studio ordered animation range registry handles many distinct streams",
    "[goldsrc-studio][animation][range-registry][stress]")
{
    constexpr std::size_t sequence_count = 1'024U;
    const auto bytes = many_animation_stream_model(sequence_count);
    const auto result = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{bytes, std::nullopt, {}});
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    REQUIRE(result.document->skeletal_model.sequences.size() == sequence_count);
    REQUIRE(result.document->skeletal_model.statistics.animation_run_count ==
        sequence_count * 6U);
}

TEST_CASE("Studio animation stream-anchor cap is tied to the run budget",
    "[goldsrc-studio][animation][range-registry][anchor-limit]")
{
    constexpr std::size_t sequence_count = 3U;
    constexpr std::size_t stream_count = sequence_count * 6U;
    const auto bytes = many_animation_stream_model(sequence_count);
    const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
    auto limits = studio::GoldSrcStudioModelImportLimits{};
    limits.maximum_sequences = sequence_count;
    limits.maximum_animation_blends = sequence_count;
    limits.maximum_animation_tracks = sequence_count;
    limits.maximum_animation_runs = stream_count;
    limits.maximum_animation_value_bytes = stream_count * 2U;
    REQUIRE(studio::GoldSrcStudioParser::parse(bundle, limits));

    --limits.maximum_animation_runs;
    const auto over = studio::GoldSrcStudioParser::parse(bundle, limits);
    REQUIRE_FALSE(over);
    REQUIRE_FALSE(over.document);
    REQUIRE(over.error->code ==
        studio::GoldSrcStudioErrorCode::count_limit_exceeded);
    REQUIRE(over.error->context.find("stream-anchor") != std::string::npos);
}

TEST_CASE("Studio zero FPS is limited to the evidenced one-frame static profile",
    "[goldsrc-studio][animation][sequence][fps-zero]")
{
    auto static_sequence = fixture::literal_minimal_goldsrc_studio_v10();
    fixture::studio_write_f32le(static_sequence,
        fixture::kSyntheticStudioSequenceOffset + 32U, 0.0F);
    const auto accepted = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{
            static_sequence, std::nullopt, {}});
    REQUIRE(accepted);
    REQUIRE(accepted.document->skeletal_model.sequences[0U]
                .frames_per_second == 0.0F);

    auto negative = static_sequence;
    fixture::studio_write_f32le(negative,
        fixture::kSyntheticStudioSequenceOffset + 32U, -1.0F);
    const auto rejected_negative = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{negative, std::nullopt, {}});
    REQUIRE_FALSE(rejected_negative);
    REQUIRE(rejected_negative.error->code ==
        studio::GoldSrcStudioErrorCode::invalid_sequence);

    auto multiple_frames = static_sequence;
    fixture::studio_write_i32le(multiple_frames,
        fixture::kSyntheticStudioSequenceOffset + 56U, 2);
    const auto rejected_multiple = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{
            multiple_frames, std::nullopt, {}});
    REQUIRE_FALSE(rejected_multiple);
    REQUIRE(rejected_multiple.error->code ==
        studio::GoldSrcStudioErrorCode::invalid_sequence);

    auto motion = static_sequence;
    fixture::studio_write_i32le(motion,
        fixture::kSyntheticStudioSequenceOffset + 68U, 1);
    const auto rejected_motion = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{motion, std::nullopt, {}});
    REQUIRE_FALSE(rejected_motion);
    REQUIRE(rejected_motion.error->code ==
        studio::GoldSrcStudioErrorCode::invalid_sequence);

    auto movement = static_sequence;
    fixture::studio_write_f32le(movement,
        fixture::kSyntheticStudioSequenceOffset + 76U, 1.0F);
    const auto rejected_movement = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{movement, std::nullopt, {}});
    REQUIRE_FALSE(rejected_movement);
    REQUIRE(rejected_movement.error->code ==
        studio::GoldSrcStudioErrorCode::invalid_sequence);

    auto automatic_movement = static_sequence;
    fixture::studio_write_i32le(automatic_movement,
        fixture::kSyntheticStudioSequenceOffset + 88U, 1);
    const auto rejected_automatic = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{
            automatic_movement, std::nullopt, {}});
    REQUIRE_FALSE(rejected_automatic);
    REQUIRE(rejected_automatic.error->code ==
        studio::GoldSrcStudioErrorCode::invalid_sequence);
}

TEST_CASE("Studio animation valid-total runs sample values and repeated spans exactly",
    "[goldsrc-studio][animation][rle]")
{
    std::vector<std::byte> bytes(12U, std::byte{0});
    bytes[2U] = std::byte{2};
    bytes[3U] = std::byte{4};
    fixture::studio_write_i16le(bytes, 4U, 3);
    fixture::studio_write_i16le(bytes, 6U, 5);
    bytes[8U] = std::byte{1};
    bytes[9U] = std::byte{1};
    fixture::studio_write_i16le(bytes, 10U, -2);
    const auto parsed = parse_channel(bytes, 5U);
    REQUIRE(parsed);
    REQUIRE(parsed.channel->runs.size() == 2U);
    REQUIRE(studio::StudioAnimationChannelSampler::sample_quantized(
        *parsed.channel, 0U) == 3);
    REQUIRE(studio::StudioAnimationChannelSampler::sample_quantized(
        *parsed.channel, 1U) == 5);
    REQUIRE(studio::StudioAnimationChannelSampler::sample_quantized(
        *parsed.channel, 2U) == 5);
    REQUIRE(studio::StudioAnimationChannelSampler::sample_quantized(
        *parsed.channel, 3U) == 5);
    REQUIRE(studio::StudioAnimationChannelSampler::sample_quantized(
        *parsed.channel, 4U) == -2);
    REQUIRE(studio::StudioAnimationChannelSampler::sample_default_scaled(
        *parsed.channel, 4U) == 9.0F);
    REQUIRE_FALSE(studio::StudioAnimationChannelSampler::sample_quantized(
        *parsed.channel, 5U));
}

TEST_CASE("Studio animation malformed runs and coverage fail closed",
    "[goldsrc-studio][animation][invalid]")
{
    SECTION("total zero")
    {
        std::vector<std::byte> bytes(4U, std::byte{0});
        bytes[2U] = std::byte{1};
        const auto result = parse_channel(bytes, 1U);
        REQUIRE_FALSE(result);
        REQUIRE(result.error == studio::GoldSrcStudioAnimationErrorCode::invalid_run);
    }
    SECTION("valid greater than total")
    {
        std::vector<std::byte> bytes(8U, std::byte{0});
        bytes[2U] = std::byte{2};
        bytes[3U] = std::byte{1};
        const auto result = parse_channel(bytes, 1U);
        REQUIRE_FALSE(result);
        REQUIRE(result.error == studio::GoldSrcStudioAnimationErrorCode::invalid_run);
    }
    SECTION("truncated values")
    {
        std::vector<std::byte> bytes(5U, std::byte{0});
        bytes[2U] = std::byte{1};
        bytes[3U] = std::byte{1};
        const auto result = parse_channel(bytes, 1U);
        REQUIRE_FALSE(result);
        REQUIRE(result.error == studio::GoldSrcStudioAnimationErrorCode::truncated_run);
    }
    SECTION("coverage overshoots")
    {
        std::vector<std::byte> bytes(6U, std::byte{0});
        bytes[2U] = std::byte{1};
        bytes[3U] = std::byte{2};
        const auto result = parse_channel(bytes, 1U);
        REQUIRE_FALSE(result);
        REQUIRE(result.error ==
            studio::GoldSrcStudioAnimationErrorCode::frame_coverage_mismatch);
    }
}

TEST_CASE("Studio external IDSQ sequence group is bounded and ignores header paths",
    "[goldsrc-studio][animation][idsq]")
{
    const auto main = fixture::synthetic_external_sequence_main();
    const auto plan = studio::GoldSrcStudioParser::inspect_dependencies(main);
    REQUIRE(plan);
    REQUIRE(plan.plan->required_sequence_group_ordinals ==
        std::vector<std::uint32_t>{1U});
    const auto group = fixture::synthetic_sequence_group_01();
    const std::array groups{
        studio::GoldSrcStudioSequenceGroupSourceView{1U, group}};
    const studio::GoldSrcStudioSourceBundleView bundle{
        main, std::nullopt, groups};
    const auto result = studio::GoldSrcStudioParser::parse(bundle);
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    REQUIRE(result.document->skeletal_model.statistics.source_count == 2U);
    const auto& channel = result.document->skeletal_model.sequences[0U]
                              .animation_blends[0U]
                              .bone_tracks[0U]
                              .channels[0U];
    REQUIRE(channel.source_sequence_group_ordinal == 1U);
    REQUIRE(studio::StudioAnimationChannelSampler::sample_quantized(channel, 0U) == 3);
}

TEST_CASE("Studio external group wrong ID and duplicate ordinals fail transactionally",
    "[goldsrc-studio][animation][idsq][invalid]")
{
    const auto main = fixture::synthetic_external_sequence_main();
    auto bad_group = fixture::synthetic_sequence_group_01();
    bad_group[3U] = std::byte{0};
    const std::array bad{
        studio::GoldSrcStudioSequenceGroupSourceView{1U, bad_group}};
    REQUIRE_FALSE(studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{main, std::nullopt, bad}));

    const auto good_group = fixture::synthetic_sequence_group_01();
    const std::array duplicate{
        studio::GoldSrcStudioSequenceGroupSourceView{1U, good_group},
        studio::GoldSrcStudioSequenceGroupSourceView{1U, good_group},
    };
    const auto repeated = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{main, std::nullopt, duplicate});
    REQUIRE_FALSE(repeated);
    REQUIRE(repeated.error->code ==
        studio::GoldSrcStudioErrorCode::duplicate_sequence_group);

    auto long_name = fixture::synthetic_sequence_group_01();
    fixture::studio_write_fixed_string(long_name, 8U, 64U,
        std::string(64U, 'g'));
    const std::array overlong{
        studio::GoldSrcStudioSequenceGroupSourceView{1U, long_name}};
    auto limits = studio::GoldSrcStudioModelImportLimits{};
    limits.maximum_string_bytes = 63U;
    const auto rejected_name = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{main, std::nullopt, overlong}, limits);
    REQUIRE_FALSE(rejected_name);
    REQUIRE(rejected_name.error->code ==
        studio::GoldSrcStudioErrorCode::invalid_string);
}

TEST_CASE("Studio animation run and value-byte limits are exact",
    "[goldsrc-studio][animation][exact-limit]")
{
    std::vector<std::byte> one_run(6U, std::byte{0});
    one_run[2U] = std::byte{1};
    one_run[3U] = std::byte{1};
    fixture::studio_write_i16le(one_run, 4U, 7);
    const studio::GoldSrcStudioAnimationChannelParseInput input{
        one_run,
        0U,
        2U,
        1U,
        assets::ModelAnimationChannelSemantic::translation_x,
        0.0F,
        1.0F,
        0U,
    };
    REQUIRE(studio::parse_goldsrc_studio_animation_channel(input,
        studio::GoldSrcStudioAnimationChannelLimits{1U, 2U}));
    REQUIRE_FALSE(studio::parse_goldsrc_studio_animation_channel(input,
        studio::GoldSrcStudioAnimationChannelLimits{1U, 1U}));

    std::vector<std::byte> two_runs(10U, std::byte{0});
    two_runs[2U] = std::byte{1};
    two_runs[3U] = std::byte{1};
    fixture::studio_write_i16le(two_runs, 4U, 1);
    two_runs[6U] = std::byte{1};
    two_runs[7U] = std::byte{1};
    fixture::studio_write_i16le(two_runs, 8U, 2);
    auto two_input = input;
    two_input.source = two_runs;
    two_input.sequence_frame_count = 2U;
    REQUIRE_FALSE(studio::parse_goldsrc_studio_animation_channel(two_input,
        studio::GoldSrcStudioAnimationChannelLimits{1U, 4U}));
    REQUIRE(studio::parse_goldsrc_studio_animation_channel(two_input,
        studio::GoldSrcStudioAnimationChannelLimits{2U, 4U}));
}

TEST_CASE("Studio aggregate animation budget is applied before each channel allocation",
    "[goldsrc-studio][animation][aggregate][exact-limit]")
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    const auto animation_offset = bytes.size();
    const auto first_stream = animation_offset +
                              studio::kGoldSrcStudioAnimationOffsetWireSize;
    const auto second_stream = first_stream + 4U;
    bytes.resize(second_stream + 4U, std::byte{0});
    fixture::studio_write_u16le(bytes, animation_offset, 12U);
    fixture::studio_write_u16le(bytes, animation_offset + 2U, 16U);
    bytes[first_stream] = std::byte{1};
    bytes[first_stream + 1U] = std::byte{1};
    fixture::studio_write_i16le(bytes, first_stream + 2U, 7);
    bytes[second_stream] = std::byte{1};
    bytes[second_stream + 1U] = std::byte{1};
    fixture::studio_write_i16le(bytes, second_stream + 2U, 8);
    fixture::studio_write_i32le(bytes, 72U,
        static_cast<std::int32_t>(bytes.size()));
    fixture::studio_write_i32le(bytes,
        fixture::kSyntheticStudioSequenceOffset + 124U,
        static_cast<std::int32_t>(animation_offset));

    const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
    auto limits = studio::GoldSrcStudioModelImportLimits{};
    limits.maximum_animation_runs = 2U;
    limits.maximum_animation_value_bytes = 4U;
    REQUIRE(studio::GoldSrcStudioParser::parse(bundle, limits));

    limits.maximum_animation_runs = 1U;
    const auto run_limit = studio::GoldSrcStudioParser::parse(bundle, limits);
    REQUIRE_FALSE(run_limit);
    REQUIRE(run_limit.error->code ==
        studio::GoldSrcStudioErrorCode::count_limit_exceeded);
    limits.maximum_animation_runs = 2U;
    limits.maximum_animation_value_bytes = 3U;
    const auto byte_limit = studio::GoldSrcStudioParser::parse(bundle, limits);
    REQUIRE_FALSE(byte_limit);
    REQUIRE(byte_limit.error->code ==
        studio::GoldSrcStudioErrorCode::invalid_animation);
}

TEST_CASE("Studio aggregate zero-offset blends and tracks are bounded before retention",
    "[goldsrc-studio][animation][aggregate][track-limit]")
{
    const auto bytes = two_zero_offset_sequence_model();
    const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
    auto limits = studio::GoldSrcStudioModelImportLimits{};
    limits.maximum_animation_blends = 2U;
    limits.maximum_animation_tracks = 2U;
    const auto exact = studio::GoldSrcStudioParser::parse(bundle, limits);
    INFO((exact.error ? exact.error->context : std::string{}));
    REQUIRE(exact);
    REQUIRE(exact.document->skeletal_model.sequences.size() == 2U);
    REQUIRE(exact.document->skeletal_model.sequences[0U]
                .animation_blends[0U]
                .bone_tracks.size() == 1U);
    REQUIRE(exact.document->skeletal_model.sequences[1U]
                .animation_blends[0U]
                .bone_tracks.size() == 1U);

    limits.maximum_animation_blends = 1U;
    const auto blend_limit = studio::GoldSrcStudioParser::parse(bundle, limits);
    REQUIRE_FALSE(blend_limit);
    REQUIRE(blend_limit.error->code ==
        studio::GoldSrcStudioErrorCode::count_limit_exceeded);

    limits.maximum_animation_blends = 2U;
    limits.maximum_animation_tracks = 1U;
    const auto track_limit = studio::GoldSrcStudioParser::parse(bundle, limits);
    REQUIRE_FALSE(track_limit);
    REQUIRE(track_limit.error->code ==
        studio::GoldSrcStudioErrorCode::count_limit_exceeded);
}

TEST_CASE("Studio animation offsets and compressed ownership stay bounded",
    "[goldsrc-studio][animation][offset][compressed]")
{
    const std::vector<std::byte> source(8U, std::byte{0});
    const auto overflow = studio::parse_goldsrc_studio_animation_channel(
        studio::GoldSrcStudioAnimationChannelParseInput{
            source,
            std::numeric_limits<std::size_t>::max(),
            2U,
            1U,
            assets::ModelAnimationChannelSemantic::translation_x,
            0.0F,
            1.0F,
            0U,
        });
    REQUIRE_FALSE(overflow);
    REQUIRE(overflow.error == studio::GoldSrcStudioAnimationErrorCode::offset_overflow);

    std::vector<std::byte> compressed(6U, std::byte{0});
    compressed[2U] = std::byte{1};
    compressed[3U] = std::byte{255};
    fixture::studio_write_i16le(compressed, 4U, 9);
    const auto parsed = parse_channel(compressed, 255U);
    REQUIRE(parsed);
    REQUIRE(parsed.channel->runs.size() == 1U);
    REQUIRE(parsed.channel->runs[0U].quantized_values.size() == 1U);
    REQUIRE(studio::StudioAnimationChannelSampler::sample_quantized(
        *parsed.channel, 254U) == 9);
}

TEST_CASE("Studio sequence count limit is exact before animation allocation",
    "[goldsrc-studio][animation][sequence-limit]")
{
    const auto baseline = fixture::literal_minimal_goldsrc_studio_v10();
    auto limits = studio::GoldSrcStudioModelImportLimits{};
    limits.maximum_sequences = 1U;
    const studio::GoldSrcStudioSourceBundleView baseline_bundle{
        baseline, std::nullopt, {}};
    REQUIRE(studio::GoldSrcStudioParser::parse(baseline_bundle, limits));
    auto too_many = baseline;
    fixture::studio_write_i32le(too_many, 164U, 2);
    const studio::GoldSrcStudioSourceBundleView over_bundle{
        too_many, std::nullopt, {}};
    REQUIRE_FALSE(studio::GoldSrcStudioParser::parse(over_bundle, limits));
}

TEST_CASE("Studio event and pivot aggregate limits accept exact one and reject two",
    "[goldsrc-studio][animation][metadata][exact-limit]")
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    const auto event_offset = bytes.size();
    const auto pivot_offset = event_offset + studio::kGoldSrcStudioSequenceEventWireSize;
    bytes.resize(pivot_offset + studio::kGoldSrcStudioPivotWireSize,
        std::byte{0});
    fixture::studio_write_i32le(bytes, 72U,
        static_cast<std::int32_t>(bytes.size()));
    fixture::studio_write_i32le(bytes,
        fixture::kSyntheticStudioSequenceOffset + 48U, 1);
    fixture::studio_write_i32le(bytes,
        fixture::kSyntheticStudioSequenceOffset + 52U,
        static_cast<std::int32_t>(event_offset));
    fixture::studio_write_i32le(bytes,
        fixture::kSyntheticStudioSequenceOffset + 60U, 1);
    fixture::studio_write_i32le(bytes,
        fixture::kSyntheticStudioSequenceOffset + 64U,
        static_cast<std::int32_t>(pivot_offset));

    auto limits = studio::GoldSrcStudioModelImportLimits{};
    limits.maximum_events = 1U;
    limits.maximum_pivots = 1U;
    const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
    REQUIRE(studio::GoldSrcStudioParser::parse(bundle, limits));

    auto events = bytes;
    fixture::studio_write_i32le(events,
        fixture::kSyntheticStudioSequenceOffset + 48U, 2);
    REQUIRE_FALSE(studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{events, std::nullopt, {}}, limits));
    auto pivots = bytes;
    fixture::studio_write_i32le(pivots,
        fixture::kSyntheticStudioSequenceOffset + 60U, 2);
    REQUIRE_FALSE(studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{pivots, std::nullopt, {}}, limits));
}

TEST_CASE("Studio animation records and streams cannot overlap fixed tables",
    "[goldsrc-studio][animation][range-overlap]")
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    fixture::studio_write_i32le(bytes,
        fixture::kSyntheticStudioSequenceOffset + 124U,
        static_cast<std::int32_t>(fixture::kSyntheticStudioBoneOffset));
    const auto result = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{bytes, std::nullopt, {}});
    REQUIRE_FALSE(result);
    REQUIRE(result.error->code == studio::GoldSrcStudioErrorCode::range_overlap);
}

TEST_CASE("Studio dependency planning and parsing retain multiple required IDSQ groups",
    "[goldsrc-studio][animation][idsq][multiple]")
{
    auto main = fixture::literal_minimal_goldsrc_studio_v10();
    const auto sequence_offset = main.size();
    const auto group_offset = sequence_offset +
                              2U * studio::kGoldSrcStudioSequenceWireSize;
    main.resize(group_offset + 3U * studio::kGoldSrcStudioSequenceGroupWireSize,
        std::byte{0});
    for (std::size_t index = 0U; index < 2U; ++index) {
        std::copy_n(main.begin() + static_cast<std::ptrdiff_t>(
                fixture::kSyntheticStudioSequenceOffset),
            studio::kGoldSrcStudioSequenceWireSize,
            main.begin() + static_cast<std::ptrdiff_t>(
                sequence_offset + index * studio::kGoldSrcStudioSequenceWireSize));
        fixture::studio_write_i32le(main,
            sequence_offset + index * studio::kGoldSrcStudioSequenceWireSize + 124U,
            76);
        fixture::studio_write_i32le(main,
            sequence_offset + index * studio::kGoldSrcStudioSequenceWireSize + 156U,
            static_cast<std::int32_t>(index + 1U));
    }
    for (std::size_t index = 0U; index < 3U; ++index) {
        std::copy_n(main.begin() + static_cast<std::ptrdiff_t>(
                fixture::kSyntheticStudioSequenceGroupOffset),
            studio::kGoldSrcStudioSequenceGroupWireSize,
            main.begin() + static_cast<std::ptrdiff_t>(
                group_offset + index * studio::kGoldSrcStudioSequenceGroupWireSize));
    }
    fixture::studio_write_i32le(main, 72U,
        static_cast<std::int32_t>(main.size()));
    fixture::studio_write_i32le(main, 164U, 2);
    fixture::studio_write_i32le(main, 168U,
        static_cast<std::int32_t>(sequence_offset));
    fixture::studio_write_i32le(main, 172U, 3);
    fixture::studio_write_i32le(main, 176U,
        static_cast<std::int32_t>(group_offset));

    const auto plan = studio::GoldSrcStudioParser::inspect_dependencies(main);
    REQUIRE(plan);
    REQUIRE(plan.plan->required_sequence_group_ordinals ==
        std::vector<std::uint32_t>{1U, 2U});
    const auto group01 = fixture::synthetic_sequence_group_01();
    const auto group02 = fixture::synthetic_sequence_group_01();
    const std::array groups{
        studio::GoldSrcStudioSequenceGroupSourceView{1U, group01},
        studio::GoldSrcStudioSequenceGroupSourceView{2U, group02},
    };
    const auto result = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{main, std::nullopt, groups});
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    REQUIRE(result.document->skeletal_model.sequences.size() == 2U);
    REQUIRE(result.document->skeletal_model.statistics.source_count == 3U);
}

TEST_CASE("Studio transition nodes use bounded one-based identifiers",
    "[goldsrc-studio][animation][transitions][mutation]")
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    const auto transition_offset = bytes.size();
    bytes.push_back(std::byte{1});
    fixture::studio_write_i32le(bytes, 72U,
        static_cast<std::int32_t>(bytes.size()));
    fixture::studio_write_i32le(bytes, 236U, 1);
    fixture::studio_write_i32le(bytes, 240U,
        static_cast<std::int32_t>(transition_offset));
    fixture::studio_write_i32le(bytes,
        fixture::kSyntheticStudioSequenceOffset + 160U, 1);
    fixture::studio_write_i32le(bytes,
        fixture::kSyntheticStudioSequenceOffset + 164U, 1);
    const studio::GoldSrcStudioSourceBundleView valid{bytes, std::nullopt, {}};
    REQUIRE(studio::GoldSrcStudioParser::parse(valid));

    auto entry = bytes;
    fixture::studio_write_i32le(entry,
        fixture::kSyntheticStudioSequenceOffset + 160U, 2);
    const auto bad_entry = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{entry, std::nullopt, {}});
    REQUIRE_FALSE(bad_entry);
    REQUIRE(bad_entry.error->code ==
        studio::GoldSrcStudioErrorCode::invalid_sequence);

    auto table = bytes;
    table[transition_offset] = std::byte{2};
    const auto bad_table = studio::GoldSrcStudioParser::parse(
        studio::GoldSrcStudioSourceBundleView{table, std::nullopt, {}});
    REQUIRE_FALSE(bad_table);
    REQUIRE(bad_table.error->code ==
        studio::GoldSrcStudioErrorCode::invalid_transition_table);
}

} // namespace
