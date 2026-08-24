#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_texture_source_parser.hpp>
#include <hlclient/goldsrc/indexed_texture/goldsrc_indexed_texture_decoder.hpp>

#include "synthetic_goldsrc_bsp_fixture.hpp"
#include "synthetic_goldsrc_wad3_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace bsp = hlclient::goldsrc::bsp;
namespace indexed = hlclient::goldsrc::indexed_texture;
namespace fixture = hlclient::tests;

[[nodiscard]] std::vector<std::byte> external_miptex(
    const std::string_view name,
    const std::uint32_t width = 16U,
    const std::uint32_t height = 16U)
{
    auto bytes = fixture::synthetic_goldsrc_miptex(name, width, height);
    bytes.resize(40U);
    for (std::size_t level = 0U; level < 4U; ++level) {
        fixture::synthetic_wad3_write_u32le(bytes, 24U + level * 4U, 0U);
    }
    return bytes;
}

[[nodiscard]] std::vector<std::byte> sequential_texture_lump(
    const std::span<const std::optional<std::vector<std::byte>>> records)
{
    std::vector<std::byte> lump(4U + records.size() * 4U, std::byte{0});
    fixture::synthetic_write_i32le(lump, 0U,
        static_cast<std::int32_t>(records.size()));
    for (std::size_t index = 0U; index < records.size(); ++index) {
        if (!records[index]) {
            fixture::synthetic_write_i32le(lump, 4U + index * 4U, -1);
            continue;
        }
        fixture::synthetic_write_i32le(lump, 4U + index * 4U,
            static_cast<std::int32_t>(lump.size()));
        lump.insert(lump.end(), records[index]->begin(), records[index]->end());
    }
    return lump;
}

[[nodiscard]] std::vector<std::byte> make_bsp(
    std::vector<std::byte> texture_lump,
    const std::int32_t texture_index = 0)
{
    fixture::SyntheticBspBuilder builder;
    builder.lump(fixture::SyntheticBspLumpId::textures) =
        std::move(texture_lump);
    auto texinfo = fixture::SyntheticBspTexinfo{};
    texinfo.miptex_index = texture_index;
    builder.set_texinfo(std::span{&texinfo, 1U});
    return builder.build();
}

[[nodiscard]] bsp::GoldSrcBspParsedDocument import_world(
    const std::vector<std::byte>& bytes)
{
    auto parsed = bsp::GoldSrcBspParser::parse(bytes);
    INFO((parsed.error ? parsed.error->context : std::string{}));
    REQUIRE(parsed);
    return std::move(*parsed.document);
}

TEST_CASE("BSP texture source parser extracts one used embedded texture",
    "[goldsrc-bsp][embedded-textures][source]")
{
    const std::array<std::optional<std::vector<std::byte>>, 1U> records{
        fixture::synthetic_goldsrc_miptex("EMBEDDED")};
    const auto bytes = make_bsp(sequential_texture_lump(records));
    const auto imported = import_world(bytes);
    REQUIRE(imported.world_asset.materials.size() == 1U);
    CHECK(imported.world_asset.materials[0U].source_texture_index == 0U);

    const auto sources = bsp::GoldSrcBspTextureSourceParser::parse(
        bytes, imported.world_asset.materials);
    INFO((sources.error ? sources.error->context : std::string{}));
    REQUIRE(sources);
    REQUIRE(sources.document->sources().size() == 1U);
    const auto& source = sources.document->sources()[0U];
    CHECK(source.canonical_source_texture_index == 0U);
    CHECK(source.source_texture_indices == std::vector<std::uint32_t>{0U});
    CHECK(source.storage == bsp::GoldSrcBspTextureSourceStorage::embedded);
    REQUIRE(source.source_record_offset.has_value());
    REQUIRE(source.source_record_byte_count.has_value());
    REQUIRE(source.miptex.has_value());
    CHECK(source.miptex->name == "EMBEDDED");

    const auto record = std::span<const std::byte>{bytes}.subspan(
        *source.source_record_offset, *source.source_record_byte_count);
    const auto decoded = indexed::GoldSrcIndexedTextureDecoder::decode(record,
        indexed::GoldSrcMiptexSourceProfile::bsp_embedded,
        assets::WorldTextureSourceKind::embedded_bsp,
        source.canonical_source_texture_index);
    REQUIRE(decoded);
    CHECK(decoded.texture->source_kind ==
        assets::WorldTextureSourceKind::embedded_bsp);
    CHECK(decoded.texture->source_bsp_texture_index == 0U);
    CHECK(decoded.texture->mip_levels.size() == 4U);
}

TEST_CASE("Unused embedded pixel records are not parsed or decoded",
    "[goldsrc-bsp][embedded-textures][used-only]")
{
    auto unused = fixture::synthetic_goldsrc_miptex("UNUSED");
    constexpr std::size_t palette_count_offset = 40U + 256U + 64U + 16U + 4U;
    unused[palette_count_offset] = std::byte{0xFF};
    unused[palette_count_offset + 1U] = std::byte{0};
    const std::array<std::optional<std::vector<std::byte>>, 2U> records{
        fixture::synthetic_goldsrc_miptex("USED"), std::move(unused)};
    const auto bytes = make_bsp(sequential_texture_lump(records), 0);
    const auto imported = import_world(bytes);
    const auto sources = bsp::GoldSrcBspTextureSourceParser::parse(
        bytes, imported.world_asset.materials);
    REQUIRE(sources);
    REQUIRE(sources.document->sources().size() == 1U);
    CHECK(sources.document->sources()[0U].miptex->name == "USED");
    CHECK(sources.document->source_for_texture_index(1U) == nullptr);
}

TEST_CASE("Multiple materials sharing one BSP texture retain one source",
    "[goldsrc-bsp][embedded-textures][deduplication]")
{
    const std::array<std::optional<std::vector<std::byte>>, 1U> records{
        fixture::synthetic_goldsrc_miptex("SHARED")};
    const auto bytes = make_bsp(sequential_texture_lump(records));
    auto imported = import_world(bytes);
    imported.world_asset.materials.push_back(imported.world_asset.materials.front());
    const auto sources = bsp::GoldSrcBspTextureSourceParser::parse(
        bytes, imported.world_asset.materials);
    REQUIRE(sources);
    CHECK(sources.document->sources().size() == 1U);
}

TEST_CASE("Duplicate BSP directory offsets alias one physical source deterministically",
    "[goldsrc-bsp][embedded-textures][duplicate-offset]")
{
    const auto record = fixture::synthetic_goldsrc_miptex("ALIASED");
    std::vector<std::byte> lump(12U, std::byte{0});
    fixture::synthetic_write_i32le(lump, 0U, 2);
    fixture::synthetic_write_i32le(lump, 4U, 12);
    fixture::synthetic_write_i32le(lump, 8U, 12);
    lump.insert(lump.end(), record.begin(), record.end());
    const auto bytes = make_bsp(std::move(lump), 1);
    const auto imported = import_world(bytes);
    REQUIRE(imported.world_asset.materials[0U].source_texture_index == 1U);
    const auto sources = bsp::GoldSrcBspTextureSourceParser::parse(
        bytes, imported.world_asset.materials);
    REQUIRE(sources);
    REQUIRE(sources.document->sources().size() == 1U);
    const auto& source = sources.document->sources()[0U];
    CHECK(source.canonical_source_texture_index == 0U);
    CHECK(source.source_texture_indices ==
        std::vector<std::uint32_t>{0U, 1U});
    CHECK(sources.document->source_for_texture_index(0U) == &source);
    CHECK(sources.document->source_for_texture_index(1U) == &source);
}

TEST_CASE("Missing and external BSP sources retain typed storage",
    "[goldsrc-bsp][embedded-textures][storage]")
{
    SECTION("missing directory entry")
    {
        const std::array<std::optional<std::vector<std::byte>>, 1U> records{
            std::nullopt};
        const auto bytes = make_bsp(sequential_texture_lump(records));
        const auto imported = import_world(bytes);
        CHECK(imported.world_asset.materials[0U].source_texture_index == 0U);
        const auto sources = bsp::GoldSrcBspTextureSourceParser::parse(
            bytes, imported.world_asset.materials);
        REQUIRE(sources);
        REQUIRE(sources.document->sources().size() == 1U);
        CHECK(sources.document->sources()[0U].storage ==
            bsp::GoldSrcBspTextureSourceStorage::missing);
        CHECK_FALSE(sources.document->sources()[0U].source_record_offset);
    }
    SECTION("all-zero offsets are an external reference")
    {
        const std::array<std::optional<std::vector<std::byte>>, 1U> records{
            external_miptex("EXTERNAL")};
        const auto bytes = make_bsp(sequential_texture_lump(records));
        const auto imported = import_world(bytes);
        const auto sources = bsp::GoldSrcBspTextureSourceParser::parse(
            bytes, imported.world_asset.materials);
        REQUIRE(sources);
        REQUIRE(sources.document->sources().size() == 1U);
        CHECK(sources.document->sources()[0U].storage ==
            bsp::GoldSrcBspTextureSourceStorage::external_reference);
    }
}

TEST_CASE("Used mixed-offset embedded source is rejected transactionally",
    "[goldsrc-bsp][embedded-textures][malformed]")
{
    auto mixed = fixture::synthetic_goldsrc_miptex("MIXED");
    fixture::synthetic_wad3_write_u32le(mixed, 32U, 0U);
    const std::array<std::optional<std::vector<std::byte>>, 1U> records{
        std::move(mixed)};
    const auto bytes = make_bsp(sequential_texture_lump(records));
    const auto imported = bsp::GoldSrcBspParser::parse(bytes);
    REQUIRE_FALSE(imported);
    CHECK(imported.error->code == bsp::GoldSrcBspErrorCode::invalid_texture_metadata);
}

TEST_CASE("BSP embedded record boundaries follow physical offsets, not directory order",
    "[goldsrc-bsp][embedded-textures][record-boundary]")
{
    const auto physically_first = fixture::synthetic_goldsrc_miptex("FIRST");
    const auto physically_last = fixture::synthetic_goldsrc_miptex("LAST");
    std::vector<std::byte> lump(12U, std::byte{0});
    fixture::synthetic_write_i32le(lump, 0U, 2);
    const auto first_offset = lump.size();
    lump.insert(lump.end(), physically_first.begin(), physically_first.end());
    const auto last_offset = lump.size();
    lump.insert(lump.end(), physically_last.begin(), physically_last.end());
    // Directory ordinal zero is physically last.
    fixture::synthetic_write_i32le(lump, 4U,
        static_cast<std::int32_t>(last_offset));
    fixture::synthetic_write_i32le(lump, 8U,
        static_cast<std::int32_t>(first_offset));

    const auto bytes = make_bsp(std::move(lump), 1);
    const auto imported = import_world(bytes);
    const auto sources = bsp::GoldSrcBspTextureSourceParser::parse(
        bytes, imported.world_asset.materials);
    REQUIRE(sources);
    REQUIRE(sources.document->sources().size() == 1U);
    CHECK(sources.document->sources()[0U].source_record_byte_count ==
        physically_first.size());

    const auto last_bytes = make_bsp(
        sequential_texture_lump(std::array<std::optional<std::vector<std::byte>>, 1U>{
            physically_last}));
    const auto last_world = import_world(last_bytes);
    const auto last_sources = bsp::GoldSrcBspTextureSourceParser::parse(
        last_bytes, last_world.world_asset.materials);
    REQUIRE(last_sources);
    CHECK(last_sources.document->sources()[0U].source_record_byte_count ==
        physically_last.size());
}

TEST_CASE("Embedded palettes and masked pixels decode without changing M4.1 geometry",
    "[goldsrc-bsp][embedded-textures][rgba][geometry-regression]")
{
    const std::array<std::optional<std::vector<std::byte>>, 1U> records{
        fixture::synthetic_goldsrc_miptex("{MASKED")};
    const auto bytes = make_bsp(sequential_texture_lump(records));
    const auto snapshot = bytes;
    const auto imported = import_world(bytes);
    CHECK(imported.world_asset.vertices.size() == 4U);
    CHECK(imported.world_asset.indices ==
        std::vector<std::uint32_t>{0U, 1U, 2U, 0U, 2U, 3U});
    CHECK(imported.world_asset.statistics.emitted_triangle_count == 2U);

    const auto sources = bsp::GoldSrcBspTextureSourceParser::parse(
        bytes, imported.world_asset.materials);
    REQUIRE(sources);
    const auto& source = sources.document->sources()[0U];
    const auto record = std::span<const std::byte>{bytes}.subspan(
        *source.source_record_offset, *source.source_record_byte_count);
    const auto decoded = indexed::GoldSrcIndexedTextureDecoder::decode(record,
        indexed::GoldSrcMiptexSourceProfile::bsp_embedded,
        assets::WorldTextureSourceKind::embedded_bsp,
        source.canonical_source_texture_index);
    REQUIRE(decoded);
    CHECK(decoded.texture->alpha_mode ==
        assets::WorldTextureAlphaMode::masked_index_255);
    CHECK(decoded.texture->mip_levels[0U].rgba_pixels[255U * 4U + 3U] ==
        std::byte{0});
    CHECK(bytes == snapshot);
}

} // namespace
