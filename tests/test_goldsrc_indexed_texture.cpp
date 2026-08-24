#include <hlclient/goldsrc/indexed_texture/goldsrc_indexed_texture_decoder.hpp>

#include "synthetic_goldsrc_wad3_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace indexed = hlclient::goldsrc::indexed_texture;
namespace fixture = hlclient::tests;

[[nodiscard]] indexed::GoldSrcMiptexParseResult parse_wad(
    const std::vector<std::byte>& bytes,
    const indexed::GoldSrcIndexedTextureLimits& limits = {})
{
    return indexed::GoldSrcMiptexParser::parse(
        bytes, indexed::GoldSrcMiptexSourceProfile::wad3_lump, limits);
}

TEST_CASE("Shared GoldSrc miptex parser decodes the exact bounded profile",
    "[goldsrc-indexed-texture][parser]")
{
    const auto bytes = fixture::synthetic_goldsrc_miptex("TEXTURE16");
    const auto result = parse_wad(bytes);
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    const auto& texture = *result.texture;
    CHECK(texture.name == "TEXTURE16");
    CHECK(texture.normalized_name == "TEXTURE16");
    CHECK(texture.width == 16U);
    CHECK(texture.height == 16U);
    CHECK(texture.storage_profile ==
        indexed::GoldSrcMiptexStorageProfile::indexed_pixels);
    REQUIRE(texture.palette_byte_offset.has_value());
    CHECK(texture.palette_color_count == 256U);
    CHECK(texture.bytes_consumed == bytes.size());
    const std::array<std::uint32_t, 4U> widths{16U, 8U, 4U, 2U};
    const std::array<std::size_t, 4U> byte_counts{256U, 64U, 16U, 4U};
    for (std::size_t level = 0U; level < 4U; ++level) {
        CHECK(texture.mip_levels[level].width == widths[level]);
        CHECK(texture.mip_levels[level].height == widths[level]);
        CHECK(texture.mip_levels[level].byte_count == byte_counts[level]);
    }
}

TEST_CASE("Shared miptex parser distinguishes exact external references",
    "[goldsrc-indexed-texture][external]")
{
    auto bytes = fixture::synthetic_goldsrc_miptex("EXTERNAL");
    bytes.resize(indexed::kGoldSrcMiptexHeaderWireSize);
    for (std::size_t level = 0U; level < 4U; ++level) {
        fixture::synthetic_wad3_write_u32le(bytes, 24U + level * 4U, 0U);
    }
    const auto bsp = indexed::GoldSrcMiptexParser::parse(
        bytes, indexed::GoldSrcMiptexSourceProfile::bsp_embedded);
    REQUIRE(bsp);
    CHECK(bsp.texture->storage_profile ==
        indexed::GoldSrcMiptexStorageProfile::external_reference);
    CHECK_FALSE(bsp.texture->palette_byte_offset.has_value());
    CHECK(bsp.texture->bytes_consumed == 40U);

    const auto wad = parse_wad(bytes);
    REQUIRE_FALSE(wad);
    CHECK(wad.error->code ==
        indexed::GoldSrcMiptexErrorCode::external_reference_not_allowed);
}

TEST_CASE("Every truncated miptex header is rejected without reading beyond input",
    "[goldsrc-indexed-texture][truncation]")
{
    const auto complete = fixture::synthetic_goldsrc_miptex();
    for (std::size_t size = 0U; size < indexed::kGoldSrcMiptexHeaderWireSize; ++size) {
        INFO(size);
        const std::vector<std::byte> truncated(
            complete.begin(), complete.begin() + static_cast<std::ptrdiff_t>(size));
        const auto result = parse_wad(truncated);
        REQUIRE_FALSE(result);
        CHECK(result.error->code == indexed::GoldSrcMiptexErrorCode::source_too_small);
    }
}

TEST_CASE("Miptex dimensions and configured limits fail closed",
    "[goldsrc-indexed-texture][dimensions][limits]")
{
    const auto check_dimension_error = [](const std::size_t offset,
                                           const std::uint32_t value) {
        auto bytes = fixture::synthetic_goldsrc_miptex();
        fixture::synthetic_wad3_write_u32le(bytes, offset, value);
        const auto result = parse_wad(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            indexed::GoldSrcMiptexErrorCode::invalid_dimensions);
    };
    SECTION("zero width") { check_dimension_error(16U, 0U); }
    SECTION("zero height") { check_dimension_error(20U, 0U); }
    SECTION("width is not a multiple of sixteen")
    {
        check_dimension_error(16U, 17U);
    }
    SECTION("height is not a multiple of sixteen")
    {
        check_dimension_error(20U, 31U);
    }
    SECTION("configured dimension exact limit and limit plus one")
    {
        auto limits = indexed::GoldSrcIndexedTextureLimits{};
        limits.maximum_dimension = 16U;
        REQUIRE(parse_wad(fixture::synthetic_goldsrc_miptex(), limits));
        const auto over = parse_wad(
            fixture::synthetic_goldsrc_miptex("WIDE", 32U, 16U), limits);
        REQUIRE_FALSE(over);
        CHECK(over.error->code ==
            indexed::GoldSrcMiptexErrorCode::texture_limit_exceeded);
    }
    SECTION("invalid limit")
    {
        auto limits = indexed::GoldSrcIndexedTextureLimits{};
        limits.maximum_dimension = 0U;
        const auto result = parse_wad(fixture::synthetic_goldsrc_miptex(), limits);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            indexed::GoldSrcMiptexErrorCode::invalid_configuration);
    }
}

TEST_CASE("Mip offsets are ordered, non-overlapping, and record bounded",
    "[goldsrc-indexed-texture][mips][mutation]")
{
    SECTION("offset below header")
    {
        auto bytes = fixture::synthetic_goldsrc_miptex();
        fixture::synthetic_wad3_write_u32le(bytes, 24U, 39U);
        const auto result = parse_wad(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            indexed::GoldSrcMiptexErrorCode::mip_offset_before_header);
    }
    SECTION("offset outside record")
    {
        auto bytes = fixture::synthetic_goldsrc_miptex();
        fixture::synthetic_wad3_write_u32le(
            bytes, 36U, 999'999U);
        const auto result = parse_wad(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            indexed::GoldSrcMiptexErrorCode::mip_range_out_of_bounds);
    }
    SECTION("overlapping levels")
    {
        auto bytes = fixture::synthetic_goldsrc_miptex();
        fixture::synthetic_wad3_write_u32le(bytes, 28U, 40U);
        const auto result = parse_wad(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            indexed::GoldSrcMiptexErrorCode::overlapping_mip_ranges);
    }
    SECTION("mixed zero and nonzero offsets")
    {
        auto bytes = fixture::synthetic_goldsrc_miptex();
        fixture::synthetic_wad3_write_u32le(bytes, 32U, 0U);
        const auto result = parse_wad(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            indexed::GoldSrcMiptexErrorCode::mixed_mip_offsets);
    }
}

TEST_CASE("Miptex palette grammar and zero-fill suffix are exact",
    "[goldsrc-indexed-texture][palette][trailing]")
{
    constexpr std::size_t palette_count_offset = 40U + 256U + 64U + 16U + 4U;
    SECTION("palette count missing")
    {
        auto bytes = fixture::synthetic_goldsrc_miptex();
        bytes.resize(palette_count_offset);
        const auto result = parse_wad(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            indexed::GoldSrcMiptexErrorCode::missing_palette_count);
    }
    SECTION("palette count must be 256")
    {
        auto bytes = fixture::synthetic_goldsrc_miptex();
        bytes[palette_count_offset] = std::byte{0xFF};
        bytes[palette_count_offset + 1U] = std::byte{0};
        const auto result = parse_wad(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            indexed::GoldSrcMiptexErrorCode::unsupported_palette_count);
    }
    SECTION("palette bytes must fit")
    {
        auto bytes = fixture::synthetic_goldsrc_miptex();
        bytes.pop_back();
        const auto result = parse_wad(bytes);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            indexed::GoldSrcMiptexErrorCode::palette_range_out_of_bounds);
    }
    SECTION("public-tool zero alignment fill is bounded")
    {
        auto bytes = fixture::synthetic_goldsrc_miptex();
        bytes.insert(bytes.end(), 2U, std::byte{0});
        const auto result = parse_wad(bytes);
        REQUIRE(result);
        CHECK(result.texture->bytes_consumed == bytes.size());
    }
    SECTION("nonzero or excessive suffix is rejected")
    {
        auto nonzero = fixture::synthetic_goldsrc_miptex();
        nonzero.push_back(std::byte{1});
        auto result = parse_wad(nonzero);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            indexed::GoldSrcMiptexErrorCode::unexpected_trailing_data);

        auto excessive = fixture::synthetic_goldsrc_miptex();
        excessive.insert(excessive.end(), 4U, std::byte{0});
        result = parse_wad(excessive);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            indexed::GoldSrcMiptexErrorCode::unexpected_trailing_data);
    }
}

TEST_CASE("Indexed pixels convert to owning unmodified RGBA mip levels",
    "[goldsrc-indexed-texture][rgba]")
{
    auto bytes = fixture::synthetic_goldsrc_miptex("OPAQUE");
    auto result = indexed::GoldSrcIndexedTextureDecoder::decode(bytes,
        indexed::GoldSrcMiptexSourceProfile::wad3_lump,
        assets::WorldTextureSourceKind::external_wad3,
        7U,
        2U);
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    auto retained = std::move(*result.texture);
    bytes.clear();
    bytes.shrink_to_fit();

    CHECK(retained.source_bsp_texture_index == 7U);
    CHECK(retained.source_archive_ordinal == 2U);
    CHECK(retained.alpha_mode == assets::WorldTextureAlphaMode::opaque);
    REQUIRE(retained.mip_levels.size() == 4U);
    CHECK(retained.mip_levels[0U].rgba_pixels.size() == 16U * 16U * 4U);
    const auto& rgba = retained.mip_levels[0U].rgba_pixels;
    CHECK(rgba[0U] == std::byte{0});
    CHECK(rgba[1U] == std::byte{0xFF});
    CHECK(rgba[2U] == std::byte{0x5A});
    CHECK(rgba[3U] == std::byte{0xFF});
    CHECK(rgba[255U * 4U + 3U] == std::byte{0xFF});
}

TEST_CASE("Masked alpha is evidence-gated by the opening brace and index 255",
    "[goldsrc-indexed-texture][alpha]")
{
    auto masked = indexed::GoldSrcIndexedTextureDecoder::decode(
        fixture::synthetic_goldsrc_miptex("{MASKED"),
        indexed::GoldSrcMiptexSourceProfile::wad3_lump,
        assets::WorldTextureSourceKind::external_wad3);
    REQUIRE(masked);
    CHECK(masked.texture->alpha_mode ==
        assets::WorldTextureAlphaMode::masked_index_255);
    const auto& pixel = masked.texture->mip_levels[0U].rgba_pixels;
    CHECK(pixel[255U * 4U] == std::byte{0xFF});
    CHECK(pixel[255U * 4U + 1U] == std::byte{0});
    CHECK(pixel[255U * 4U + 2U] == std::byte{0xA5});
    CHECK(pixel[255U * 4U + 3U] == std::byte{0});
    CHECK(pixel[0U * 4U + 3U] == std::byte{0xFF});

    const auto opaque = indexed::GoldSrcIndexedTextureDecoder::decode(
        fixture::synthetic_goldsrc_miptex("OPAQUE"),
        indexed::GoldSrcMiptexSourceProfile::wad3_lump,
        assets::WorldTextureSourceKind::external_wad3);
    REQUIRE(opaque);
    CHECK(opaque.texture->mip_levels[0U].rgba_pixels[255U * 4U + 3U] ==
        std::byte{0xFF});
}

TEST_CASE("Incremental decoder obeys the caller pixel-conversion budget",
    "[goldsrc-indexed-texture][incremental]")
{
    const auto bytes = fixture::synthetic_goldsrc_miptex();
    auto started = indexed::GoldSrcIndexedTextureDecodeOperation::begin(bytes,
        indexed::GoldSrcMiptexSourceProfile::wad3_lump,
        assets::WorldTextureSourceKind::external_wad3);
    REQUIRE(started);
    auto operation = std::move(*started.operation);
    CHECK(operation.update(4U) ==
        indexed::GoldSrcIndexedTextureDecodeState::decoding);
    CHECK(operation.converted_rgba_byte_count() == 4U);
    CHECK(operation.texture() == nullptr);
    CHECK(operation.update(operation.total_rgba_byte_count()) ==
        indexed::GoldSrcIndexedTextureDecodeState::complete);
    REQUIRE(operation.texture() != nullptr);
    CHECK(operation.converted_rgba_byte_count() ==
        operation.total_rgba_byte_count());

    auto second = indexed::GoldSrcIndexedTextureDecodeOperation::begin(bytes,
        indexed::GoldSrcMiptexSourceProfile::wad3_lump,
        assets::WorldTextureSourceKind::external_wad3);
    REQUIRE(second);
    CHECK(second.operation->update(3U) ==
        indexed::GoldSrcIndexedTextureDecodeState::failed);
    REQUIRE(second.operation->error() != nullptr);
    CHECK(second.operation->error()->code ==
        indexed::GoldSrcMiptexErrorCode::invalid_update_budget);
}

TEST_CASE("Repeated shared decode is deterministic", "[goldsrc-indexed-texture][determinism]")
{
    const auto bytes = fixture::synthetic_goldsrc_miptex("DETERMINISTIC");
    const auto first = indexed::GoldSrcIndexedTextureDecoder::decode(bytes,
        indexed::GoldSrcMiptexSourceProfile::wad3_lump,
        assets::WorldTextureSourceKind::external_wad3);
    const auto second = indexed::GoldSrcIndexedTextureDecoder::decode(bytes,
        indexed::GoldSrcMiptexSourceProfile::wad3_lump,
        assets::WorldTextureSourceKind::external_wad3);
    REQUIRE(first);
    REQUIRE(second);
    for (std::size_t level = 0U; level < 4U; ++level) {
        CHECK(first.texture->mip_levels[level].rgba_pixels ==
            second.texture->mip_levels[level].rgba_pixels);
    }
}

} // namespace
