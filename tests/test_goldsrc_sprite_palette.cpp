#include <hlclient/goldsrc/sprite/goldsrc_sprite_parser.hpp>

#include "goldsrc_sprite_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace fixture = hlclient::tests::sprite_fixture;
namespace sprite = hlclient::goldsrc::sprite;

TEST_CASE("GoldSrc sprite owns the exact 256-entry RGB palette in source order",
    "[goldsrc-sprite][palette]")
{
    const auto result = sprite::GoldSrcSpriteParser::parse(
        fixture::literal_single_sprite());
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    const auto& palette = result.document->source_data.palette;
    CHECK(palette.colors.size() == 256U);
    CHECK(palette.colors[0U] == (assets::SpritePaletteColor{0U, 255U, 0U}));
    CHECK(palette.colors[1U] == (assets::SpritePaletteColor{1U, 254U, 3U}));
    CHECK(palette.colors[127U] ==
        (assets::SpritePaletteColor{127U, 128U, 125U}));
    CHECK(palette.colors[255U] ==
        (assets::SpritePaletteColor{255U, 0U, 253U}));
}

TEST_CASE("Sprite palette count accepts exactly 256 and no external palette profile",
    "[goldsrc-sprite][palette][count][mutation]")
{
    for (const std::uint16_t count :
         std::array<std::uint16_t, 3U>{0U, 255U, 257U}) {
        INFO(count);
        auto bytes = fixture::literal_single_sprite();
        bytes[fixture::kPaletteCountOffset] =
            std::byte{static_cast<std::uint8_t>(count & 0xFFU)};
        bytes[fixture::kPaletteCountOffset + 1U] =
            std::byte{static_cast<std::uint8_t>((count >> 8U) & 0xFFU)};
        const auto result = sprite::GoldSrcSpriteParser::parse(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            sprite::GoldSrcSpriteErrorCode::unsupported_palette_count);
    }

    auto missing = fixture::literal_single_sprite();
    missing.resize(sprite::kGoldSrcSpriteHeaderWireSize);
    const auto result = sprite::GoldSrcSpriteParser::parse(missing);
    REQUIRE_FALSE(result);
    CHECK(result.error->code ==
        sprite::GoldSrcSpriteErrorCode::missing_palette_count);
}

TEST_CASE("Every palette truncation is rejected at the palette boundary",
    "[goldsrc-sprite][palette][truncation]")
{
    const auto complete = fixture::literal_single_sprite();
    for (std::size_t palette_bytes = 0U;
         palette_bytes < sprite::kGoldSrcSpritePaletteRgbByteCount;
         ++palette_bytes) {
        INFO(palette_bytes);
        const auto size = fixture::kPaletteOffset + palette_bytes;
        const std::vector<std::byte> truncated(
            complete.begin(), complete.begin() + static_cast<std::ptrdiff_t>(size));
        const auto result = sprite::GoldSrcSpriteParser::parse(truncated);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            sprite::GoldSrcSpriteErrorCode::truncated_palette);
    }
}

TEST_CASE("Sprite RGB values are neither gamma adjusted nor premultiplied",
    "[goldsrc-sprite][palette][rgba]")
{
    const auto result = sprite::GoldSrcSpriteParser::parse(
        fixture::literal_single_sprite());
    REQUIRE(result);
    const auto& frame = result.document->source_data.indexed_frames.front();
    REQUIRE(frame.derived_rgba8.size() == 16U);
    // Index 1 maps literally to (1, 254, 3) with alpha 255.
    CHECK(frame.derived_rgba8[4U] == std::byte{1});
    CHECK(frame.derived_rgba8[5U] == std::byte{254});
    CHECK(frame.derived_rgba8[6U] == std::byte{3});
    CHECK(frame.derived_rgba8[7U] == std::byte{255});
}

} // namespace
