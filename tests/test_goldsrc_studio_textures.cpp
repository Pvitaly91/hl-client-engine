#include <hlclient/goldsrc/studio/goldsrc_studio_parser.hpp>

#include "goldsrc_studio_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace {

namespace assets = hlclient::assets;
namespace studio = hlclient::goldsrc::studio;
namespace fixture = hlclient::tests;

TEST_CASE("Studio indexed texture owns exact palette RGB and opaque RGBA",
    "[goldsrc-studio][textures][indexed]")
{
    const auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
    const auto result = studio::GoldSrcStudioParser::parse(bundle);
    REQUIRE(result);
    const auto& texture = result.document->skeletal_model.textures[0U];
    REQUIRE(texture.width == 1U);
    REQUIRE(texture.height == 1U);
    REQUIRE(texture.indexed_pixels == std::vector<std::uint8_t>{0U});
    REQUIRE(texture.palette_rgb.size() == 256U);
    REQUIRE(texture.palette_rgb[0U] == std::array<std::uint8_t, 3U>{10U, 20U, 30U});
    REQUIRE(texture.rgba8_level_zero == std::vector<std::byte>{
        std::byte{10}, std::byte{20}, std::byte{30}, std::byte{255}});
    REQUIRE(texture.alpha_mode == assets::ModelTextureAlphaMode::opaque);
}

TEST_CASE("Studio opaque texture keeps palette index 255 opaque",
    "[goldsrc-studio][textures][indexed][opaque][regression]")
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    bytes[fixture::kSyntheticStudioTextureDataOffset] = std::byte{255};
    const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
    const auto result = studio::GoldSrcStudioParser::parse(bundle);
    REQUIRE(result);
    const auto& texture = result.document->skeletal_model.textures[0U];
    REQUIRE(texture.indexed_pixels == std::vector<std::uint8_t>{255U});
    REQUIRE(texture.rgba8_level_zero == std::vector<std::byte>{
        std::byte{40}, std::byte{50}, std::byte{60}, std::byte{255}});
    REQUIRE(texture.alpha_mode == assets::ModelTextureAlphaMode::opaque);
    REQUIRE((texture.source_flags & studio::kGoldSrcStudioTextureMasked) == 0U);
}

TEST_CASE("Studio masked index 255 retains RGB and derives only transparent alpha",
    "[goldsrc-studio][textures][masked]")
{
    const auto bytes = fixture::literal_minimal_goldsrc_studio_v10(true);
    const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
    const auto result = studio::GoldSrcStudioParser::parse(bundle);
    REQUIRE(result);
    const auto& texture = result.document->skeletal_model.textures[0U];
    REQUIRE(texture.indexed_pixels == std::vector<std::uint8_t>{255U});
    REQUIRE(texture.rgba8_level_zero == std::vector<std::byte>{
        std::byte{40}, std::byte{50}, std::byte{60}, std::byte{0}});
    REQUIRE(texture.alpha_mode ==
        assets::ModelTextureAlphaMode::masked_index_255);
    REQUIRE((texture.source_flags & studio::kGoldSrcStudioTextureMasked) != 0U);
}

TEST_CASE("Studio texture dimensions pixels palette and aggregate RGBA are bounded",
    "[goldsrc-studio][textures][limits]")
{
    SECTION("dimension limit")
    {
        auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
        fixture::studio_write_i32le(bytes, fixture::kSyntheticStudioTextureOffset + 68U,
            4097);
        const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
        REQUIRE_FALSE(studio::GoldSrcStudioParser::parse(bundle));
    }
    SECTION("truncated palette via data offset")
    {
        auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
        const auto data_offset = static_cast<std::int32_t>(bytes.size() - 768U);
        fixture::studio_write_i32le(bytes, fixture::kSyntheticStudioTextureOffset + 76U,
            data_offset);
        fixture::studio_write_i32le(bytes,
            studio::kGoldSrcStudioHeaderTextureDataOffset, data_offset);
        const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
        const auto result = studio::GoldSrcStudioParser::parse(bundle);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == studio::GoldSrcStudioErrorCode::invalid_texture);
    }
    SECTION("truncated indexed pixels")
    {
        auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
        const auto data_offset = static_cast<std::int32_t>(bytes.size());
        fixture::studio_write_i32le(bytes,
            fixture::kSyntheticStudioTextureOffset + 76U, data_offset);
        fixture::studio_write_i32le(bytes,
            studio::kGoldSrcStudioHeaderTextureDataOffset, data_offset);
        const auto result = studio::GoldSrcStudioParser::parse(
            studio::GoldSrcStudioSourceBundleView{bytes, std::nullopt, {}});
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == studio::GoldSrcStudioErrorCode::invalid_texture);
    }
    SECTION("pixel range multiplication is checked")
    {
        auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
        fixture::studio_write_i32le(bytes,
            fixture::kSyntheticStudioTextureOffset + 68U,
            static_cast<std::int32_t>(
                studio::kGoldSrcStudioHardMaximumTextureDimension));
        fixture::studio_write_i32le(bytes,
            fixture::kSyntheticStudioTextureOffset + 72U,
            static_cast<std::int32_t>(
                studio::kGoldSrcStudioHardMaximumTextureDimension));
        const auto result = studio::GoldSrcStudioParser::parse(
            studio::GoldSrcStudioSourceBundleView{bytes, std::nullopt, {}});
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == studio::GoldSrcStudioErrorCode::invalid_texture);
    }
    SECTION("exact aggregate limit and limit plus one")
    {
        const auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
        auto limits = studio::GoldSrcStudioModelImportLimits{};
        limits.maximum_total_rgba_bytes = 4U;
        const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
        REQUIRE(studio::GoldSrcStudioParser::parse(bundle, limits));
        limits.maximum_total_rgba_bytes = 3U;
        REQUIRE_FALSE(studio::GoldSrcStudioParser::parse(bundle, limits));
    }
}

TEST_CASE("Studio texture data may not overlap descriptor or skin ranges",
    "[goldsrc-studio][textures][overlap]")
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    fixture::studio_write_i32le(bytes, fixture::kSyntheticStudioTextureOffset + 76U,
        static_cast<std::int32_t>(fixture::kSyntheticStudioSkinOffset));
    fixture::studio_write_i32le(bytes,
        studio::kGoldSrcStudioHeaderTextureDataOffset,
        static_cast<std::int32_t>(fixture::kSyntheticStudioSkinOffset));
    const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
    const auto result = studio::GoldSrcStudioParser::parse(bundle);
    REQUIRE_FALSE(result);
    REQUIRE(result.error->code == studio::GoldSrcStudioErrorCode::range_overlap);
}

TEST_CASE("Studio texturedataindex identifies the first texture payload",
    "[goldsrc-studio][textures][texturedataindex][mutation]")
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    fixture::studio_write_i32le(bytes,
        studio::kGoldSrcStudioHeaderTextureDataOffset,
        static_cast<std::int32_t>(fixture::kSyntheticStudioTextureDataOffset + 1U));
    const auto result = studio::GoldSrcStudioParser::inspect_dependencies(bytes);
    REQUIRE_FALSE(result);
    REQUIRE(result.error->code == studio::GoldSrcStudioErrorCode::invalid_texture);

    auto split = fixture::synthetic_split_texture_main();
    fixture::studio_write_i32le(split,
        studio::kGoldSrcStudioHeaderTextureDataOffset, 244);
    const auto invalid_split = studio::GoldSrcStudioParser::inspect_dependencies(split);
    REQUIRE_FALSE(invalid_split);
    REQUIRE(invalid_split.error->code ==
        studio::GoldSrcStudioErrorCode::invalid_texture);
}

TEST_CASE("Studio texture companions reject every unexpected model directory profile",
    "[goldsrc-studio][textures][companion][profile][mutation]")
{
    const auto main = fixture::synthetic_split_texture_main();
    for (const auto header_field : std::array<std::size_t, 12U>{
             76U, 136U, 140U, 148U, 156U, 164U,
             172U, 204U, 212U, 220U, 228U, 236U}) {
        INFO(header_field);
        auto companion = fixture::synthetic_texture_companion();
        fixture::studio_write_i32le(companion, header_field, 1);
        const auto result = studio::GoldSrcStudioParser::parse(
            studio::GoldSrcStudioSourceBundleView{main, companion, {}});
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code ==
            studio::GoldSrcStudioErrorCode::invalid_texture_companion);
    }
}

TEST_CASE("Studio texture descriptor and dimension limits are exact",
    "[goldsrc-studio][textures][exact-limit]")
{
    const auto baseline = fixture::literal_minimal_goldsrc_studio_v10();
    auto limits = studio::GoldSrcStudioModelImportLimits{};
    limits.maximum_textures = 1U;
    limits.maximum_texture_dimension = 1U;
    const studio::GoldSrcStudioSourceBundleView baseline_bundle{
        baseline, std::nullopt, {}};
    REQUIRE(studio::GoldSrcStudioParser::parse(baseline_bundle, limits));

    auto too_many = baseline;
    fixture::studio_write_i32le(too_many, 180U, 2);
    const studio::GoldSrcStudioSourceBundleView too_many_bundle{
        too_many, std::nullopt, {}};
    REQUIRE_FALSE(studio::GoldSrcStudioParser::parse(too_many_bundle, limits));

    auto too_wide = baseline;
    fixture::studio_write_i32le(too_wide,
        fixture::kSyntheticStudioTextureOffset + 68U, 2);
    const studio::GoldSrcStudioSourceBundleView too_wide_bundle{
        too_wide, std::nullopt, {}};
    REQUIRE_FALSE(studio::GoldSrcStudioParser::parse(too_wide_bundle, limits));
}

TEST_CASE("Studio retains raw texture flags and emits no mip or gamma-derived data",
    "[goldsrc-studio][textures][flags]")
{
    auto bytes = fixture::literal_minimal_goldsrc_studio_v10();
    fixture::studio_write_i32le(bytes,
        fixture::kSyntheticStudioTextureOffset + 64U,
        static_cast<std::int32_t>(studio::kGoldSrcStudioKnownTextureFlags));
    const studio::GoldSrcStudioSourceBundleView bundle{bytes, std::nullopt, {}};
    const auto result = studio::GoldSrcStudioParser::parse(bundle);
    REQUIRE(result);
    const auto& texture = result.document->skeletal_model.textures[0U];
    REQUIRE(texture.source_flags == studio::kGoldSrcStudioKnownTextureFlags);
    REQUIRE(texture.indexed_pixels.size() == 1U);
    REQUIRE(texture.rgba8_level_zero.size() == 4U);
}

} // namespace
