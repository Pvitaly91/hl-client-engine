#include <hlclient/goldsrc/sprite/goldsrc_sprite_parser.hpp>

#include "goldsrc_sprite_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

namespace fixture = hlclient::tests::sprite_fixture;
namespace sprite = hlclient::goldsrc::sprite;

[[nodiscard]] sprite::GoldSrcSpriteParseResult parse(
    const std::vector<std::byte>& bytes,
    const sprite::GoldSrcSpriteImportLimits& limits = {})
{
    return sprite::GoldSrcSpriteParser::parse(bytes, limits);
}

TEST_CASE("GoldSrc sprite wire constants and exact IDSP v2 header decode are explicit",
    "[goldsrc-sprite][header]")
{
    STATIC_CHECK(sprite::kGoldSrcSpriteHeaderWireSize == 40U);
    STATIC_CHECK(sprite::kGoldSrcSpriteIdentifier == 0x50534449U);
    STATIC_CHECK(sprite::kGoldSrcSpriteVersion == 2);

    const auto result = parse(fixture::literal_single_sprite());
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    const auto& source = result.document->source_data;
    CHECK(source.source_version == 2);
    CHECK(source.orientation == hlclient::assets::SpriteOrientation::view_parallel);
    CHECK(source.texture_format == hlclient::assets::SpriteTextureFormat::normal);
    CHECK(source.sync_type == hlclient::assets::SpriteSyncType::synchronized);
    CHECK(source.bounding_radius == 8.0F);
    CHECK(source.maximum_width == 2U);
    CHECK(source.maximum_height == 2U);
    CHECK(source.beam_length == 0.0F);
}

TEST_CASE("Every truncation of the exact 40-byte sprite header fails before decode",
    "[goldsrc-sprite][header][truncation]")
{
    const auto complete = fixture::literal_single_sprite();
    for (std::size_t size = 0U; size < sprite::kGoldSrcSpriteHeaderWireSize; ++size) {
        INFO(size);
        const std::vector<std::byte> truncated(
            complete.begin(), complete.begin() + static_cast<std::ptrdiff_t>(size));
        const auto result = parse(truncated);
        REQUIRE_FALSE(result);
        CHECK(result.error->code == sprite::GoldSrcSpriteErrorCode::source_too_small);
    }
}

TEST_CASE("Sprite identifier and version are signature-first exact values",
    "[goldsrc-sprite][header][profile]")
{
    SECTION("wrong identifier")
    {
        auto bytes = fixture::literal_single_sprite();
        fixture::write_u32_le(bytes, 0U, 0x54534449U);
        const auto result = parse(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code == sprite::GoldSrcSpriteErrorCode::invalid_identifier);
    }
    SECTION("unsupported version")
    {
        auto bytes = fixture::literal_single_sprite();
        fixture::write_i32_le(bytes, 4U, 1);
        const auto result = parse(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code == sprite::GoldSrcSpriteErrorCode::unsupported_version);
    }
}

TEST_CASE("Sprite orientation format and synchronization categories fail closed",
    "[goldsrc-sprite][header][profile][mutation]")
{
    SECTION("all supported orientation categories retain their meaning")
    {
        const std::array expected{
            hlclient::assets::SpriteOrientation::view_parallel_upright,
            hlclient::assets::SpriteOrientation::facing_upright,
            hlclient::assets::SpriteOrientation::view_parallel,
            hlclient::assets::SpriteOrientation::oriented,
            hlclient::assets::SpriteOrientation::view_parallel_oriented,
        };
        for (std::size_t value = 0U; value < expected.size(); ++value) {
            INFO(value);
            auto bytes = fixture::literal_single_sprite();
            fixture::write_i32_le(bytes, 8U, static_cast<std::int32_t>(value));
            const auto result = parse(bytes);
            REQUIRE(result);
            CHECK(result.document->source_data.orientation == expected[value]);
        }
    }
    SECTION("unknown orientation")
    {
        auto bytes = fixture::literal_single_sprite();
        fixture::write_i32_le(bytes, 8U, 5);
        const auto result = parse(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            sprite::GoldSrcSpriteErrorCode::unsupported_orientation);
    }
    SECTION("unknown texture format")
    {
        auto bytes = fixture::literal_single_sprite();
        fixture::write_i32_le(bytes, 12U, -1);
        const auto result = parse(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            sprite::GoldSrcSpriteErrorCode::unsupported_texture_format);
    }
    SECTION("unknown synchronization type")
    {
        auto bytes = fixture::literal_single_sprite();
        fixture::write_i32_le(bytes, 36U, 2);
        const auto result = parse(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            sprite::GoldSrcSpriteErrorCode::unsupported_sync_type);
    }
}

TEST_CASE("Sprite header floats reject non-finite radius and beam metadata",
    "[goldsrc-sprite][header][float][mutation]")
{
    for (const auto value : {std::numeric_limits<float>::quiet_NaN(),
             std::numeric_limits<float>::infinity(),
             -std::numeric_limits<float>::infinity()}) {
        DYNAMIC_SECTION("radius bits " << std::bit_cast<std::uint32_t>(value))
        {
            auto bytes = fixture::literal_single_sprite();
            fixture::write_f32_le(bytes, 16U, value);
            const auto result = parse(bytes);
            REQUIRE_FALSE(result);
            CHECK(result.error->code ==
                sprite::GoldSrcSpriteErrorCode::invalid_bounding_radius);
        }
        DYNAMIC_SECTION("beam bits " << std::bit_cast<std::uint32_t>(value))
        {
            auto bytes = fixture::literal_single_sprite();
            fixture::write_f32_le(bytes, 32U, value);
            const auto result = parse(bytes);
            REQUIRE_FALSE(result);
            CHECK(result.error->code ==
                sprite::GoldSrcSpriteErrorCode::invalid_beam_length);
        }
    }
    auto negative = fixture::literal_single_sprite();
    fixture::write_f32_le(negative, 16U, -0.25F);
    const auto result = parse(negative);
    REQUIRE_FALSE(result);
    CHECK(result.error->code ==
        sprite::GoldSrcSpriteErrorCode::invalid_bounding_radius);
}

TEST_CASE("Sprite header dimensions and top-level counts are bounded",
    "[goldsrc-sprite][header][limits][mutation]")
{
    SECTION("nonpositive maximum dimensions")
    {
        for (const auto offset : {20U, 24U}) {
            auto bytes = fixture::literal_single_sprite();
            fixture::write_i32_le(bytes, offset, -1);
            const auto result = parse(bytes);
            REQUIRE_FALSE(result);
            CHECK(result.error->code ==
                sprite::GoldSrcSpriteErrorCode::invalid_header_dimensions);
        }
    }
    SECTION("negative frame count")
    {
        auto bytes = fixture::literal_single_sprite();
        fixture::write_i32_le(bytes, 28U, -1);
        const auto result = parse(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            sprite::GoldSrcSpriteErrorCode::invalid_top_level_count);
    }
    SECTION("count exact limit and limit plus one")
    {
        auto limits = sprite::GoldSrcSpriteImportLimits{};
        limits.maximum_top_level_entries = 1U;
        REQUIRE(parse(fixture::literal_single_sprite(), limits));
        auto bytes = fixture::literal_single_sprite();
        fixture::write_i32_le(bytes, 28U, 2);
        const auto result = parse(bytes, limits);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            sprite::GoldSrcSpriteErrorCode::invalid_top_level_count);
    }
}

TEST_CASE("Sprite source and configuration limits have exact fail-closed edges",
    "[goldsrc-sprite][header][source-limit]")
{
    const auto bytes = fixture::literal_single_sprite();
    auto limits = sprite::GoldSrcSpriteImportLimits{};
    limits.maximum_source_bytes = bytes.size();
    REQUIRE(parse(bytes, limits));
    --limits.maximum_source_bytes;
    const auto over = parse(bytes, limits);
    REQUIRE_FALSE(over);
    CHECK(over.error->code ==
        sprite::GoldSrcSpriteErrorCode::source_limit_exceeded);

    limits = {};
    limits.maximum_width = 0U;
    const auto invalid = parse(bytes, limits);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error->code ==
        sprite::GoldSrcSpriteErrorCode::invalid_configuration);
}

TEST_CASE("Sprite header dimension and metadata limits have exact fail-closed edges",
    "[goldsrc-sprite][header][exact-limit]")
{
    const auto bytes = fixture::literal_single_sprite();
    constexpr auto exact_metadata_bytes = sprite::kGoldSrcSpriteHeaderWireSize +
                                          sprite::kGoldSrcSpritePaletteCountWireSize +
                                          sprite::kGoldSrcSpritePaletteRgbByteCount +
                                          sprite::kGoldSrcSpriteFrameTypeWireSize +
                                          sprite::kGoldSrcSpriteFrameHeaderWireSize;
    auto limits = sprite::GoldSrcSpriteImportLimits{};
    limits.maximum_width = 2U;
    limits.maximum_height = 2U;
    limits.maximum_metadata_bytes = exact_metadata_bytes;
    REQUIRE(parse(bytes, limits));

    SECTION("header width is limit plus one")
    {
        --limits.maximum_width;
        const auto result = parse(bytes, limits);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            sprite::GoldSrcSpriteErrorCode::invalid_header_dimensions);
    }
    SECTION("header height is limit plus one")
    {
        --limits.maximum_height;
        const auto result = parse(bytes, limits);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            sprite::GoldSrcSpriteErrorCode::invalid_header_dimensions);
    }
    SECTION("metadata is limit plus one")
    {
        --limits.maximum_metadata_bytes;
        const auto result = parse(bytes, limits);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            sprite::GoldSrcSpriteErrorCode::metadata_limit_exceeded);
    }
}

} // namespace
