#include <hlclient/goldsrc/wad3/goldsrc_wad3_catalog.hpp>
#include <hlclient/goldsrc/wad3/goldsrc_wad3_texture.hpp>

#include "synthetic_goldsrc_wad3_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

namespace assets = hlclient::assets;
namespace fixture = hlclient::tests;
namespace indexed = hlclient::goldsrc::indexed_texture;
namespace wad3 = hlclient::goldsrc::wad3;

struct CatalogAndEntry {
    wad3::GoldSrcWad3Catalog catalog;
    const wad3::GoldSrcWad3Entry* entry{nullptr};
};

[[nodiscard]] CatalogAndEntry parse_catalog(
    const fixture::SyntheticWad3Fixture& source,
    const std::string_view texture_name)
{
    auto result = wad3::GoldSrcWad3CatalogParser::parse(source.bytes);
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    auto catalog = std::move(*result.catalog);
    const auto* entry = catalog.find_miptex(texture_name);
    REQUIRE(entry != nullptr);
    return CatalogAndEntry{std::move(catalog), entry};
}

TEST_CASE("WAD3 miptex decoder uses the exact evidence-confirmed type",
    "[goldsrc-wad3][texture][wire]")
{
    STATIC_CHECK(wad3::kGoldSrcWad3MiptexType == 0x43U);
    const auto source = fixture::synthetic_valid_wad3();
    const auto parsed = parse_catalog(source, "WAD_TEXTURE");
    CHECK(parsed.entry->type == 0x43U);
    CHECK(parsed.entry->is_miptex());
}

TEST_CASE("Valid WAD3 miptex decodes four owning RGBA levels",
    "[goldsrc-wad3][texture][valid]")
{
    auto source = fixture::synthetic_valid_wad3("WAD_TEXTURE");
    const auto parsed = parse_catalog(source, "wad_texture");
    const auto result = wad3::GoldSrcWad3TextureDecoder::decode(
        source.bytes,
        *parsed.entry,
        wad3::GoldSrcWad3TextureRequest{
            "WAD_TEXTURE",
            16U,
            16U,
            9U,
            7U,
        });
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);

    const auto& texture = *result.texture;
    CHECK(texture.name == "WAD_TEXTURE");
    CHECK(texture.width == 16U);
    CHECK(texture.height == 16U);
    CHECK(texture.source_kind == assets::WorldTextureSourceKind::external_wad3);
    CHECK(texture.alpha_mode == assets::WorldTextureAlphaMode::opaque);
    CHECK(texture.source_bsp_texture_index == 9U);
    CHECK(texture.source_archive_ordinal == 7U);

    constexpr std::array<std::uint32_t, 4U> dimensions{16U, 8U, 4U, 2U};
    constexpr std::array<std::size_t, 4U> rgba_sizes{1'024U, 256U, 64U, 16U};
    for (std::size_t level = 0U; level < texture.mip_levels.size(); ++level) {
        INFO(level);
        CHECK(texture.mip_levels[level].width == dimensions[level]);
        CHECK(texture.mip_levels[level].height == dimensions[level]);
        CHECK(texture.mip_levels[level].pixel_format ==
            assets::WorldTexturePixelFormat::rgba8);
        CHECK(texture.mip_levels[level].rgba_pixels.size() == rgba_sizes[level]);
    }

    const auto& level_zero = texture.mip_levels[0U].rgba_pixels;
    REQUIRE(level_zero.size() == 1'024U);
    CHECK(level_zero[0U] == std::byte{0x00});
    CHECK(level_zero[1U] == std::byte{0xFF});
    CHECK(level_zero[2U] == std::byte{0x5A});
    CHECK(level_zero[3U] == std::byte{0xFF});
    const auto last = 255U * 4U;
    CHECK(level_zero[last] == std::byte{0xFF});
    CHECK(level_zero[last + 1U] == std::byte{0x00});
    CHECK(level_zero[last + 2U] == std::byte{0xA5});
    CHECK(level_zero[last + 3U] == std::byte{0xFF});

    std::ranges::fill(source.bytes, std::byte{0xCD});
    source.bytes.clear();
    source.bytes.shrink_to_fit();
    CHECK(texture.mip_levels[0U].rgba_pixels[2U] == std::byte{0x5A});
    CHECK(texture.mip_levels[3U].rgba_pixels.size() == 16U);
}

TEST_CASE("WAD3 masked miptex preserves RGB and gates only index 255 alpha",
    "[goldsrc-wad3][texture][masked]")
{
    const auto source = fixture::synthetic_valid_wad3("{MASKED");
    const auto parsed = parse_catalog(source, "{masked");
    const auto result = wad3::GoldSrcWad3TextureDecoder::decode(
        source.bytes, *parsed.entry);
    REQUIRE(result);
    const auto& texture = *result.texture;
    CHECK(texture.alpha_mode == assets::WorldTextureAlphaMode::masked_index_255);
    const auto& rgba = texture.mip_levels[0U].rgba_pixels;
    REQUIRE(rgba.size() == 1'024U);
    CHECK(rgba[3U] == std::byte{0xFF});
    const auto transparent = 255U * 4U;
    CHECK(rgba[transparent] == std::byte{0xFF});
    CHECK(rgba[transparent + 1U] == std::byte{0x00});
    CHECK(rgba[transparent + 2U] == std::byte{0xA5});
    CHECK(rgba[transparent + 3U] == std::byte{0x00});
}

TEST_CASE("WAD3 texture adapter rejects unsupported entry profiles",
    "[goldsrc-wad3][texture][entry]")
{
    const auto source = fixture::synthetic_valid_wad3();
    const auto parsed = parse_catalog(source, "WAD_TEXTURE");

    SECTION("invalid type")
    {
        auto entry = *parsed.entry;
        entry.type = 0x42U;
        const auto result = wad3::GoldSrcWad3TextureDecoder::decode(source.bytes, entry);
        REQUIRE_FALSE(result);
        CHECK(result.error->code == wad3::GoldSrcWad3TextureErrorCode::invalid_entry_type);
    }

    SECTION("unsupported compression")
    {
        auto entry = *parsed.entry;
        entry.compression = 1U;
        const auto result = wad3::GoldSrcWad3TextureDecoder::decode(source.bytes, entry);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3TextureErrorCode::unsupported_compression);
    }

    SECTION("uncompressed size mismatch")
    {
        auto entry = *parsed.entry;
        --entry.uncompressed_size;
        const auto result = wad3::GoldSrcWad3TextureDecoder::decode(source.bytes, entry);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3TextureErrorCode::entry_size_mismatch);
    }

    SECTION("entry range must fit the supplied archive")
    {
        auto entry = *parsed.entry;
        entry.file_offset = source.bytes.size();
        entry.disk_size = 1U;
        entry.uncompressed_size = 1U;
        const auto result = wad3::GoldSrcWad3TextureDecoder::decode(source.bytes, entry);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3TextureErrorCode::source_range_out_of_bounds);
    }
}

TEST_CASE("WAD3 texture adapter maps shared miptex grammar failures",
    "[goldsrc-wad3][texture][malformed]")
{
    SECTION("truncated lump")
    {
        auto entry = fixture::SyntheticWad3Entry{};
        entry.payload.resize(39U);
        const auto source = fixture::synthetic_wad3({std::move(entry)});
        const auto parsed = parse_catalog(source, "WAD_TEXTURE");
        const auto result = wad3::GoldSrcWad3TextureDecoder::decode(
            source.bytes, *parsed.entry);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code ==
            wad3::GoldSrcWad3TextureErrorCode::miptex_parse_failed);
        REQUIRE(result.error->miptex_error.has_value());
        CHECK(result.error->miptex_error->code ==
            indexed::GoldSrcMiptexErrorCode::source_too_small);
    }

    SECTION("invalid palette count")
    {
        auto entry = fixture::SyntheticWad3Entry{};
        entry.payload = fixture::synthetic_goldsrc_miptex();
        fixture::synthetic_wad3_write_u8(entry.payload, 380U, 0xFFU);
        fixture::synthetic_wad3_write_u8(entry.payload, 381U, 0x00U);
        const auto source = fixture::synthetic_wad3({std::move(entry)});
        const auto parsed = parse_catalog(source, "WAD_TEXTURE");
        const auto result = wad3::GoldSrcWad3TextureDecoder::decode(
            source.bytes, *parsed.entry);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->miptex_error.has_value());
        CHECK(result.error->miptex_error->code ==
            indexed::GoldSrcMiptexErrorCode::unsupported_palette_count);
    }

    SECTION("invalid dimensions")
    {
        auto entry = fixture::SyntheticWad3Entry{};
        entry.payload = fixture::synthetic_goldsrc_miptex();
        fixture::synthetic_wad3_write_u32le(entry.payload, 16U, 15U);
        const auto source = fixture::synthetic_wad3({std::move(entry)});
        const auto parsed = parse_catalog(source, "WAD_TEXTURE");
        const auto result = wad3::GoldSrcWad3TextureDecoder::decode(
            source.bytes, *parsed.entry);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->miptex_error.has_value());
        CHECK(result.error->miptex_error->code ==
            indexed::GoldSrcMiptexErrorCode::invalid_dimensions);
    }
}

TEST_CASE("WAD3 texture adapter cross-checks directory and BSP metadata",
    "[goldsrc-wad3][texture][cross-check]")
{
    SECTION("BSP dimensions mismatch")
    {
        const auto source = fixture::synthetic_valid_wad3("MATCH");
        const auto parsed = parse_catalog(source, "MATCH");
        const auto result = wad3::GoldSrcWad3TextureDecoder::decode(
            source.bytes,
            *parsed.entry,
            wad3::GoldSrcWad3TextureRequest{"MATCH", 32U, 16U});
        REQUIRE_FALSE(result);
        CHECK(result.error->code == wad3::GoldSrcWad3TextureErrorCode::dimension_mismatch);
    }

    SECTION("directory and miptex names mismatch")
    {
        auto entry = fixture::SyntheticWad3Entry{};
        entry.name = "DIRECTORY";
        entry.payload = fixture::synthetic_goldsrc_miptex("PAYLOAD");
        const auto source = fixture::synthetic_wad3({std::move(entry)});
        const auto parsed = parse_catalog(source, "DIRECTORY");
        const auto result = wad3::GoldSrcWad3TextureDecoder::decode(
            source.bytes, *parsed.entry);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3TextureErrorCode::directory_name_mismatch);
    }

    SECTION("requested BSP name mismatch")
    {
        const auto source = fixture::synthetic_valid_wad3("DIRECTORY");
        const auto parsed = parse_catalog(source, "DIRECTORY");
        const auto result = wad3::GoldSrcWad3TextureDecoder::decode(
            source.bytes,
            *parsed.entry,
            wad3::GoldSrcWad3TextureRequest{"OTHER", 16U, 16U});
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3TextureErrorCode::expected_texture_name_mismatch);
    }

    SECTION("width and height expectations are a pair")
    {
        const auto source = fixture::synthetic_valid_wad3();
        const auto parsed = parse_catalog(source, "WAD_TEXTURE");
        auto request = wad3::GoldSrcWad3TextureRequest{};
        request.expected_width = 16U;
        const auto result = wad3::GoldSrcWad3TextureDecoder::decode(
            source.bytes, *parsed.entry, request);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3TextureErrorCode::invalid_configuration);
    }
}

TEST_CASE("WAD3 prepared texture metadata is incremental and source-free",
    "[goldsrc-wad3][texture][prepare]")
{
    const auto source = fixture::synthetic_valid_wad3("PREPARED");
    const auto parsed = parse_catalog(source, "PREPARED");
    const auto prepared = wad3::GoldSrcWad3TextureParser::parse(
        source.bytes,
        *parsed.entry,
        wad3::GoldSrcWad3TextureRequest{
            "prepared",
            16U,
            16U,
            3U,
            11U,
        });
    REQUIRE(prepared);
    CHECK(prepared.texture->record_byte_offset == source.payload_offsets.front());
    CHECK(prepared.texture->record_byte_count == parsed.entry->disk_size);
    CHECK(prepared.texture->directory_ordinal == 0U);
    CHECK(prepared.texture->source_bsp_texture_index == 3U);
    CHECK(prepared.texture->source_archive_ordinal == 11U);
    CHECK(prepared.texture->miptex.name == "PREPARED");
    CHECK(prepared.texture->miptex.mip_levels.size() == 4U);
}

TEST_CASE("WAD3 texture diagnostics retain no names or source bytes",
    "[goldsrc-wad3][texture][diagnostics]")
{
    auto entry = fixture::SyntheticWad3Entry{};
    entry.name = "PRIVATE_NAME";
    entry.payload.resize(39U);
    const auto source = fixture::synthetic_wad3({std::move(entry)});
    const auto parsed = parse_catalog(source, "PRIVATE_NAME");
    const auto result = wad3::GoldSrcWad3TextureDecoder::decode(
        source.bytes, *parsed.entry);
    REQUIRE_FALSE(result);
    CHECK(result.error->context.find("PRIVATE_NAME") == std::string::npos);
    CHECK(result.error->context.size() <=
        wad3::kGoldSrcWad3MaximumDiagnosticContextBytes);
    CHECK(wad3::to_string(result.error->code) == "miptex_parse_failed");
}

} // namespace
