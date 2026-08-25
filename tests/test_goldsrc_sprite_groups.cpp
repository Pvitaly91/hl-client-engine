#include <hlclient/goldsrc/sprite/goldsrc_sprite_parser.hpp>

#include "goldsrc_sprite_test_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace fixture = hlclient::tests::sprite_fixture;
namespace sprite = hlclient::goldsrc::sprite;
using Catch::Approx;

inline constexpr std::size_t kGroupCountOffset =
    fixture::kFirstTopLevelEntryOffset + 4U;
inline constexpr std::size_t kFirstIntervalOffset = kGroupCountOffset + 4U;

TEST_CASE("GoldSrc sprite groups retain cumulative and derived frame timing",
    "[goldsrc-sprite][group]")
{
    const auto result = sprite::GoldSrcSpriteParser::parse(
        fixture::literal_group_sprite());
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    const auto& source = result.document->source_data;
    REQUIRE(source.top_level_entries.size() == 1U);
    CHECK(source.top_level_entries[0U].kind ==
        assets::SpriteTopLevelEntryKind::group);
    CHECK(source.top_level_entries[0U].flattened_frame_count == 2U);
    CHECK(source.top_level_entries[0U].group_ordinal == 0U);

    REQUIRE(source.groups.size() == 1U);
    const auto& group = source.groups[0U];
    REQUIRE(group.cumulative_intervals_seconds.size() == 2U);
    REQUIRE(group.frame_durations_seconds.size() == 2U);
    CHECK(group.cumulative_intervals_seconds[0U] == Approx(0.10F));
    CHECK(group.cumulative_intervals_seconds[1U] == Approx(0.35F));
    CHECK(group.frame_durations_seconds[0U] == Approx(0.10F));
    CHECK(group.frame_durations_seconds[1U] == Approx(0.25F));
    CHECK(group.flattened_frame_indices ==
        (std::vector<std::uint32_t>{0U, 1U}));

    REQUIRE(source.indexed_frames.size() == 2U);
    CHECK(source.indexed_frames[0U].source_group_ordinal == 0U);
    CHECK(source.indexed_frames[0U].source_group_frame_ordinal == 0U);
    CHECK(source.indexed_frames[1U].source_group_frame_ordinal == 1U);
    REQUIRE(result.document->compatibility_frames.size() == 2U);
    CHECK(result.document->compatibility_frames[0U].duration_seconds ==
        Approx(0.10F));
    CHECK(result.document->compatibility_frames[1U].duration_seconds ==
        Approx(0.25F));
    CHECK(source.statistics.top_level_entry_count == 1U);
    CHECK(source.statistics.flattened_frame_count == 2U);
    CHECK(source.statistics.group_count == 1U);
}

TEST_CASE("Sprite cumulative group intervals reject every invalid numeric class",
    "[goldsrc-sprite][group][interval][mutation]")
{
    for (const auto value : {0.0F,
             -0.1F,
             std::numeric_limits<float>::quiet_NaN(),
             std::numeric_limits<float>::infinity()}) {
        INFO(value);
        auto bytes = fixture::literal_group_sprite();
        fixture::write_f32_le(bytes, kFirstIntervalOffset, value);
        const auto result = sprite::GoldSrcSpriteParser::parse(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            sprite::GoldSrcSpriteErrorCode::invalid_group_interval);
    }

    auto non_increasing = fixture::literal_group_sprite();
    fixture::write_f32_le(non_increasing, kFirstIntervalOffset + 4U, 0.10F);
    const auto result = sprite::GoldSrcSpriteParser::parse(non_increasing);
    REQUIRE_FALSE(result);
    CHECK(result.error->code ==
        sprite::GoldSrcSpriteErrorCode::invalid_group_interval);
}

TEST_CASE("Sprite group count and interval arrays are bounded before frame parsing",
    "[goldsrc-sprite][group][limits][truncation]")
{
    SECTION("zero and negative group counts")
    {
        for (const auto value : {0, -1}) {
            auto bytes = fixture::literal_group_sprite();
            fixture::write_i32_le(bytes, kGroupCountOffset, value);
            const auto result = sprite::GoldSrcSpriteParser::parse(bytes);
            REQUIRE_FALSE(result);
            CHECK(result.error->code ==
                sprite::GoldSrcSpriteErrorCode::invalid_group_frame_count);
        }
    }
    SECTION("group count exact limit and limit plus one")
    {
        auto limits = sprite::GoldSrcSpriteImportLimits{};
        limits.maximum_group_frames = 2U;
        REQUIRE(sprite::GoldSrcSpriteParser::parse(
            fixture::literal_group_sprite(), limits));
        limits.maximum_group_frames = 1U;
        const auto result = sprite::GoldSrcSpriteParser::parse(
            fixture::literal_group_sprite(), limits);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            sprite::GoldSrcSpriteErrorCode::invalid_group_frame_count);
    }
    SECTION("truncated interval array")
    {
        const auto complete = fixture::literal_group_sprite();
        for (std::size_t interval_bytes = 0U; interval_bytes < 8U; ++interval_bytes) {
            INFO(interval_bytes);
            const auto size = kFirstIntervalOffset + interval_bytes;
            const std::vector<std::byte> truncated(
                complete.begin(), complete.begin() + static_cast<std::ptrdiff_t>(size));
            const auto result = sprite::GoldSrcSpriteParser::parse(truncated);
            REQUIRE_FALSE(result);
            CHECK(result.error->code ==
                sprite::GoldSrcSpriteErrorCode::truncated_group_intervals);
        }
    }
}

TEST_CASE("Sprite duration and flattened-frame aggregate limits have exact edges",
    "[goldsrc-sprite][group][duration][flattened]")
{
    auto limits = sprite::GoldSrcSpriteImportLimits{};
    limits.maximum_duration_seconds = 0.35F;
    REQUIRE(sprite::GoldSrcSpriteParser::parse(
        fixture::literal_group_sprite(), limits));
    limits.maximum_duration_seconds = 0.34F;
    auto result = sprite::GoldSrcSpriteParser::parse(
        fixture::literal_group_sprite(), limits);
    REQUIRE_FALSE(result);
    CHECK(result.error->code ==
        sprite::GoldSrcSpriteErrorCode::duration_limit_exceeded);

    auto aggregate = fixture::literal_header_and_palette(2);
    const std::vector<float> intervals{0.10F, 0.35F};
    const std::vector<fixture::FrameSpec> frames{
        fixture::FrameSpec{-1, 1, 2, 1, {std::byte{3}, std::byte{4}}},
        fixture::FrameSpec{0, 2, 1, 2, {std::byte{5}, std::byte{6}}},
    };
    fixture::append_group_entry(aggregate, intervals, frames);
    fixture::append_group_entry(aggregate, intervals, frames);

    limits = {};
    limits.maximum_top_level_entries = 2U;
    limits.maximum_group_frames = 2U;
    limits.maximum_flattened_frames = 4U;
    REQUIRE(sprite::GoldSrcSpriteParser::parse(aggregate, limits));
    --limits.maximum_flattened_frames;
    result = sprite::GoldSrcSpriteParser::parse(aggregate, limits);
    REQUIRE_FALSE(result);
    CHECK(result.error->code ==
        sprite::GoldSrcSpriteErrorCode::flattened_frame_limit_exceeded);
}

TEST_CASE("Sprite grammar rejects unknown top-level frame types and never nests groups",
    "[goldsrc-sprite][group][frame-type]")
{
    for (const auto value : {-1, 2, 99}) {
        auto bytes = fixture::literal_single_sprite();
        fixture::write_i32_le(bytes, fixture::kFirstTopLevelEntryOffset, value);
        const auto result = sprite::GoldSrcSpriteParser::parse(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            sprite::GoldSrcSpriteErrorCode::invalid_frame_type);
    }
}

} // namespace
