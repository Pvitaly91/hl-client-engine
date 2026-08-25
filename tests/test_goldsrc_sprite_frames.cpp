#include <hlclient/goldsrc/sprite/goldsrc_sprite_parser.hpp>

#include "goldsrc_sprite_test_fixture.hpp"

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

TEST_CASE("GoldSrc single sprite frame retains origin dimensions and indexed pixels",
    "[goldsrc-sprite][frame][single]")
{
    const auto result = sprite::GoldSrcSpriteParser::parse(
        fixture::literal_single_sprite());
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    const auto& source = result.document->source_data;
    REQUIRE(source.indexed_frames.size() == 1U);
    const auto& frame = source.indexed_frames.front();
    CHECK(frame.origin.x == -1);
    CHECK(frame.origin.y == 2);
    CHECK(frame.width == 2U);
    CHECK(frame.height == 2U);
    CHECK(frame.indexed_pixels == (std::vector<std::byte>{
        std::byte{0}, std::byte{1}, std::byte{255}, std::byte{2}}));
    CHECK(frame.source_top_level_entry == 0U);
    CHECK_FALSE(frame.source_group_ordinal.has_value());
    CHECK_FALSE(frame.source_group_frame_ordinal.has_value());

    REQUIRE(source.top_level_entries.size() == 1U);
    CHECK(source.top_level_entries[0U].kind ==
        assets::SpriteTopLevelEntryKind::single);
    CHECK(source.top_level_entries[0U].first_flattened_frame == 0U);
    CHECK(source.top_level_entries[0U].flattened_frame_count == 1U);
    CHECK(source.statistics.top_level_entry_count == 1U);
    CHECK(source.statistics.flattened_frame_count == 1U);
    CHECK(source.statistics.indexed_pixel_byte_count == 4U);
}

TEST_CASE("Sprite frame header and indexed pixels have independent truncation errors",
    "[goldsrc-sprite][frame][truncation]")
{
    const auto complete = fixture::literal_single_sprite();
    constexpr auto frame_header_offset = fixture::kFirstTopLevelEntryOffset + 4U;
    for (std::size_t header_bytes = 0U;
         header_bytes < sprite::kGoldSrcSpriteFrameHeaderWireSize;
         ++header_bytes) {
        INFO(header_bytes);
        const auto size = frame_header_offset + header_bytes;
        const std::vector<std::byte> truncated(
            complete.begin(), complete.begin() + static_cast<std::ptrdiff_t>(size));
        const auto result = sprite::GoldSrcSpriteParser::parse(truncated);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            sprite::GoldSrcSpriteErrorCode::truncated_frame_header);
    }

    constexpr auto pixel_offset = frame_header_offset +
        sprite::kGoldSrcSpriteFrameHeaderWireSize;
    for (std::size_t pixel_bytes = 0U; pixel_bytes < 4U; ++pixel_bytes) {
        INFO(pixel_bytes);
        const auto size = pixel_offset + pixel_bytes;
        const std::vector<std::byte> truncated(
            complete.begin(), complete.begin() + static_cast<std::ptrdiff_t>(size));
        const auto result = sprite::GoldSrcSpriteParser::parse(truncated);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            sprite::GoldSrcSpriteErrorCode::truncated_frame_pixels);
    }
}

TEST_CASE("Sprite frame dimensions origins and areas fail closed under mutations",
    "[goldsrc-sprite][frame][limits][mutation]")
{
    constexpr auto frame_offset = fixture::kFirstTopLevelEntryOffset + 4U;
    SECTION("zero and negative sizes")
    {
        for (const auto offset : {frame_offset + 8U, frame_offset + 12U}) {
            for (const auto value : {0, -1}) {
                auto bytes = fixture::literal_single_sprite();
                fixture::write_i32_le(bytes, offset, value);
                const auto result = sprite::GoldSrcSpriteParser::parse(bytes);
                REQUIRE_FALSE(result);
                CHECK(result.error->code ==
                    sprite::GoldSrcSpriteErrorCode::invalid_frame_dimensions);
            }
        }
    }
    SECTION("overflow-like signed dimensions are rejected before multiplication")
    {
        auto bytes = fixture::literal_single_sprite();
        fixture::write_i32_le(
            bytes, frame_offset + 8U, std::numeric_limits<std::int32_t>::max());
        fixture::write_i32_le(
            bytes, frame_offset + 12U, std::numeric_limits<std::int32_t>::max());
        const auto result = sprite::GoldSrcSpriteParser::parse(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            sprite::GoldSrcSpriteErrorCode::invalid_frame_dimensions);
    }
    SECTION("signed origin magnitude is bounded without abs INT_MIN overflow")
    {
        auto bytes = fixture::literal_single_sprite();
        fixture::write_i32_le(bytes,
            frame_offset,
            std::numeric_limits<std::int32_t>::min());
        const auto result = sprite::GoldSrcSpriteParser::parse(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            sprite::GoldSrcSpriteErrorCode::invalid_frame_origin);
    }
    SECTION("header maximum must cover every frame")
    {
        auto bytes = fixture::literal_single_sprite();
        fixture::write_i32_le(bytes, 20U, 1);
        const auto result = sprite::GoldSrcSpriteParser::parse(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            sprite::GoldSrcSpriteErrorCode::header_frame_dimensions_too_small);
    }
    SECTION("per-frame pixel exact limit and limit plus one")
    {
        auto limits = sprite::GoldSrcSpriteImportLimits{};
        limits.maximum_pixels_per_frame = 4U;
        REQUIRE(sprite::GoldSrcSpriteParser::parse(
            fixture::literal_single_sprite(), limits));
        limits.maximum_pixels_per_frame = 3U;
        const auto result = sprite::GoldSrcSpriteParser::parse(
            fixture::literal_single_sprite(), limits);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            sprite::GoldSrcSpriteErrorCode::frame_pixel_limit_exceeded);
    }
}

TEST_CASE("Multiple top-level sprite entries remain distinct from flat frames",
    "[goldsrc-sprite][frame][top-level]")
{
    const auto result = sprite::GoldSrcSpriteParser::parse(
        fixture::literal_two_single_sprite());
    REQUIRE(result);
    const auto& source = result.document->source_data;
    REQUIRE(source.top_level_entries.size() == 2U);
    REQUIRE(source.indexed_frames.size() == 2U);
    CHECK(source.top_level_entries[0U].first_flattened_frame == 0U);
    CHECK(source.top_level_entries[1U].first_flattened_frame == 1U);
    CHECK(source.indexed_frames[0U].indexed_pixels[0U] == std::byte{7});
    CHECK(source.indexed_frames[1U].indexed_pixels[0U] == std::byte{8});
}

TEST_CASE("Sprite aggregate pixel limits and trailing-byte policy are exact",
    "[goldsrc-sprite][frame][aggregate][trailing]")
{
    auto limits = sprite::GoldSrcSpriteImportLimits{};
    limits.maximum_total_indexed_bytes = 4U;
    // Four pixels produce one authoritative 16-byte RGBA payload and one
    // additive 16-byte compatibility copy. The configured memory cap accounts
    // for both owning buffers while the public statistic remains 16 bytes.
    limits.maximum_total_rgba_bytes = 32U;
    const auto exact = sprite::GoldSrcSpriteParser::parse(
        fixture::literal_single_sprite(), limits);
    REQUIRE(exact);
    CHECK(exact.document->source_data.statistics.derived_rgba_byte_count == 16U);

    limits.maximum_total_indexed_bytes = 3U;
    auto result = sprite::GoldSrcSpriteParser::parse(
        fixture::literal_single_sprite(), limits);
    REQUIRE_FALSE(result);
    CHECK(result.error->code ==
        sprite::GoldSrcSpriteErrorCode::total_indexed_limit_exceeded);

    limits = {};
    limits.maximum_total_rgba_bytes = 31U;
    result = sprite::GoldSrcSpriteParser::parse(
        fixture::literal_single_sprite(), limits);
    REQUIRE_FALSE(result);
    CHECK(result.error->code ==
        sprite::GoldSrcSpriteErrorCode::total_rgba_limit_exceeded);

    auto trailing = fixture::literal_single_sprite();
    trailing.push_back(std::byte{0});
    result = sprite::GoldSrcSpriteParser::parse(trailing);
    REQUIRE_FALSE(result);
    CHECK(result.error->code ==
        sprite::GoldSrcSpriteErrorCode::unexpected_trailing_data);
}

} // namespace
