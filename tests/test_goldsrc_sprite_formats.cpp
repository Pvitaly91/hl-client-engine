#include <hlclient/goldsrc/sprite/goldsrc_sprite_parser.hpp>

#include "goldsrc_sprite_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace fixture = hlclient::tests::sprite_fixture;
namespace sprite = hlclient::goldsrc::sprite;

TEST_CASE("Normal sprite RGBA is an opaque literal palette derivation",
    "[goldsrc-sprite][format][normal]")
{
    const auto result = sprite::GoldSrcSpriteParser::parse(
        fixture::literal_single_sprite(0));
    REQUIRE(result);
    const auto& frame = result.document->source_data.indexed_frames[0U];
    CHECK(result.document->source_data.texture_format ==
        assets::SpriteTextureFormat::normal);
    CHECK(frame.rgba_evidence ==
        assets::SpriteRgbaEvidenceProfile::normal_opaque);
    REQUIRE(frame.derived_rgba8.size() == 16U);
    CHECK(frame.derived_rgba8[0U] == std::byte{0});
    CHECK(frame.derived_rgba8[1U] == std::byte{255});
    CHECK(frame.derived_rgba8[2U] == std::byte{0});
    CHECK(frame.derived_rgba8[3U] == std::byte{255});
    CHECK(frame.derived_rgba8[12U] == std::byte{2});
    CHECK(frame.derived_rgba8[13U] == std::byte{253});
    CHECK(frame.derived_rgba8[14U] == std::byte{6});
    CHECK(frame.derived_rgba8[15U] == std::byte{255});
}

TEST_CASE("Alpha-test sprite derives transparency only for palette index 255",
    "[goldsrc-sprite][format][alpha-test]")
{
    const auto result = sprite::GoldSrcSpriteParser::parse(
        fixture::literal_single_sprite(3));
    REQUIRE(result);
    const auto& frame = result.document->source_data.indexed_frames[0U];
    CHECK(result.document->source_data.texture_format ==
        assets::SpriteTextureFormat::alpha_test);
    CHECK(frame.rgba_evidence ==
        assets::SpriteRgbaEvidenceProfile::alpha_test_index_255);
    REQUIRE(frame.derived_rgba8.size() == 16U);
    CHECK(frame.derived_rgba8[3U] == std::byte{255});
    CHECK(frame.derived_rgba8[7U] == std::byte{255});
    CHECK(frame.derived_rgba8[11U] == std::byte{0});
    CHECK(frame.derived_rgba8[12U] == std::byte{2});
    CHECK(frame.derived_rgba8[13U] == std::byte{253});
    CHECK(frame.derived_rgba8[14U] == std::byte{6});
    CHECK(frame.derived_rgba8[15U] == std::byte{255});
}

TEST_CASE("Additive sprite retains format metadata and labels opaque RGBA as preview",
    "[goldsrc-sprite][format][additive]")
{
    const auto result = sprite::GoldSrcSpriteParser::parse(
        fixture::literal_single_sprite(1));
    REQUIRE(result);
    const auto& source = result.document->source_data;
    const auto& frame = source.indexed_frames[0U];
    CHECK(source.texture_format == assets::SpriteTextureFormat::additive);
    CHECK(frame.rgba_evidence ==
        assets::SpriteRgbaEvidenceProfile::
            additive_opaque_preview_not_blend_semantics);
    REQUIRE(frame.derived_rgba8.size() == 16U);
    for (std::size_t alpha = 3U; alpha < frame.derived_rgba8.size(); alpha += 4U) {
        CHECK(frame.derived_rgba8[alpha] == std::byte{255});
    }
}

TEST_CASE("Index-alpha sprite remains structurally complete without guessed RGBA",
    "[goldsrc-sprite][format][index-alpha][evidence-pending]")
{
    const auto result = sprite::GoldSrcSpriteParser::parse(
        fixture::literal_single_sprite(2));
    REQUIRE(result);
    const auto& source = result.document->source_data;
    const auto& frame = source.indexed_frames[0U];
    CHECK(source.texture_format == assets::SpriteTextureFormat::index_alpha);
    CHECK(frame.rgba_evidence ==
        assets::SpriteRgbaEvidenceProfile::
            index_alpha_conversion_evidence_pending);
    CHECK(frame.indexed_pixels == (std::vector<std::byte>{
        std::byte{0}, std::byte{1}, std::byte{255}, std::byte{2}}));
    CHECK(frame.derived_rgba8.empty());
    REQUIRE(result.document->compatibility_frames.size() == 1U);
    CHECK(result.document->compatibility_frames[0U].image.width == 2U);
    CHECK(result.document->compatibility_frames[0U].image.height == 2U);
    CHECK(result.document->compatibility_frames[0U].image.pixels.empty());
    CHECK(source.statistics.derived_rgba_byte_count == 0U);
}

TEST_CASE("Sprite orientation and random synchronization are inert owning metadata",
    "[goldsrc-sprite][format][metadata]")
{
    const auto result = sprite::GoldSrcSpriteParser::parse(
        fixture::literal_single_sprite(0, 3, 1));
    REQUIRE(result);
    const auto& source = result.document->source_data;
    CHECK(source.orientation == assets::SpriteOrientation::oriented);
    CHECK(source.sync_type == assets::SpriteSyncType::random);
    CHECK(source.compatibility_profile ==
        assets::SpriteSourceCompatibilityProfile::goldsrc_palette_sprite_v2);
    // No billboard, random playback, renderer, or blend state is represented.
}

TEST_CASE("Sprite document owns palette indexed pixels timing and derived previews",
    "[goldsrc-sprite][format][ownership]")
{
    auto bytes = fixture::literal_group_sprite();
    auto result = sprite::GoldSrcSpriteParser::parse(bytes);
    REQUIRE(result);
    auto document = std::move(*result.document);
    bytes.assign(1U, std::byte{0xEE});
    bytes.shrink_to_fit();

    CHECK(document.source_data.palette.colors[3U] ==
        (assets::SpritePaletteColor{3U, 252U, 9U}));
    REQUIRE(document.source_data.indexed_frames.size() == 2U);
    CHECK(document.source_data.indexed_frames[0U].indexed_pixels ==
        (std::vector<std::byte>{std::byte{3}, std::byte{4}}));
    CHECK(document.source_data.indexed_frames[1U].indexed_pixels ==
        (std::vector<std::byte>{std::byte{5}, std::byte{6}}));
    CHECK(document.source_data.groups[0U].cumulative_intervals_seconds.size() == 2U);
}

} // namespace
