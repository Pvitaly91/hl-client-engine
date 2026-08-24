#include <hlclient/goldsrc/wad3/goldsrc_wad3_catalog.hpp>

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

namespace fixture = hlclient::tests;
namespace wad3 = hlclient::goldsrc::wad3;

[[nodiscard]] wad3::GoldSrcWad3CatalogParseResult parse(
    const fixture::SyntheticWad3Fixture& source,
    const wad3::GoldSrcWad3CatalogLimits& limits = {})
{
    return wad3::GoldSrcWad3CatalogParser::parse(source.bytes, limits);
}

TEST_CASE("WAD3 constants are clean-room wire values", "[goldsrc-wad3][catalog][wire]")
{
    STATIC_CHECK(wad3::kGoldSrcWad3HeaderWireSize == 12U);
    STATIC_CHECK(wad3::kGoldSrcWad3DirectoryEntryWireSize == 32U);
    STATIC_CHECK(wad3::kGoldSrcWad3EntryNameWireSize == 16U);

    // Pinned Valve evidence: wadlib.h defines TYP_LUMPY as 64; qlumpy.c
    // places miptex at command index 3 and writes TYP_LUMPY + index.
    STATIC_CHECK(wad3::kGoldSrcWad3MiptexType == 0x43U);
    STATIC_CHECK(wad3::kGoldSrcWad3NoCompression == 0U);
}

TEST_CASE("WAD3 header and directory decode exact owning metadata",
    "[goldsrc-wad3][catalog][valid]")
{
    const auto source = fixture::synthetic_valid_wad3("Stone01");
    const auto result = parse(source);
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    REQUIRE(result.catalog->entry_count() == 1U);
    CHECK(result.catalog->source_byte_count() == source.bytes.size());

    const auto& entry = result.catalog->entries().front();
    CHECK(entry.directory_ordinal == 0U);
    CHECK(entry.source_name == "Stone01");
    CHECK(entry.normalized_name == "STONE01");
    CHECK(entry.file_offset == source.payload_offsets.front());
    CHECK(entry.disk_size == source.directory_offset - source.payload_offsets.front());
    CHECK(entry.disk_size == entry.uncompressed_size);
    CHECK(entry.type == 0x43U);
    CHECK(entry.compression == 0U);
    CHECK(entry.is_miptex());
    CHECK(entry.compatibility_profile ==
        wad3::GoldSrcWad3CompatibilityProfile::wad3_uncompressed_miptex_v1);
    CHECK(entry.evidence_profile ==
        wad3::GoldSrcWad3EvidenceProfile::pinned_valve_tools_and_synthetic_fixture);
}

TEST_CASE("WAD3 header grammar fails closed", "[goldsrc-wad3][catalog][header]")
{
    SECTION("every strict header truncation")
    {
        const auto complete = fixture::synthetic_valid_wad3().bytes;
        for (std::size_t size = 0U; size < wad3::kGoldSrcWad3HeaderWireSize; ++size) {
            INFO(size);
            const auto result = wad3::GoldSrcWad3CatalogParser::parse(
                std::span<const std::byte>{complete}.first(size));
            REQUIRE_FALSE(result);
            CHECK(result.error->code == wad3::GoldSrcWad3CatalogErrorCode::source_too_small);
        }
    }

    SECTION("WAD2 is not accepted as WAD3")
    {
        auto source = fixture::synthetic_valid_wad3();
        source.bytes[3U] = std::byte{'2'};
        const auto result = parse(source);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3CatalogErrorCode::invalid_identification);
    }

    SECTION("arbitrary identification is rejected")
    {
        auto source = fixture::synthetic_valid_wad3();
        source.bytes[0U] = std::byte{'X'};
        const auto result = parse(source);
        REQUIRE_FALSE(result);
        CHECK(result.error->byte_offset == 0U);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3CatalogErrorCode::invalid_identification);
    }

    SECTION("negative lump count")
    {
        auto source = fixture::synthetic_valid_wad3();
        fixture::synthetic_wad3_write_i32le(source.bytes, 4U, -1);
        const auto result = parse(source);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3CatalogErrorCode::negative_lump_count);
    }

    SECTION("zero lump count is outside the world-texture profile")
    {
        auto source = fixture::synthetic_valid_wad3();
        fixture::synthetic_wad3_write_i32le(source.bytes, 4U, 0);
        const auto result = parse(source);
        REQUIRE_FALSE(result);
        CHECK(result.error->code == wad3::GoldSrcWad3CatalogErrorCode::zero_lump_count);
    }

    SECTION("configured lump count exact limit and limit plus one")
    {
        auto second = fixture::SyntheticWad3Entry{};
        second.name = "SECOND";
        second.payload = fixture::synthetic_goldsrc_miptex(second.name);
        const auto one = fixture::synthetic_valid_wad3("FIRST");
        const auto two = fixture::synthetic_wad3(
            {fixture::SyntheticWad3Entry{
                 "FIRST", fixture::synthetic_goldsrc_miptex("FIRST")},
             std::move(second)});
        auto limits = wad3::GoldSrcWad3CatalogLimits{};
        limits.maximum_lump_count = 1U;
        CHECK(parse(one, limits));
        const auto result = parse(two, limits);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3CatalogErrorCode::lump_count_limit_exceeded);
    }

    SECTION("source byte exact limit and limit plus one")
    {
        const auto source = fixture::synthetic_valid_wad3();
        auto limits = wad3::GoldSrcWad3CatalogLimits{};
        limits.maximum_source_bytes = source.bytes.size();
        CHECK(parse(source, limits));
        --limits.maximum_source_bytes;
        const auto result = parse(source, limits);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3CatalogErrorCode::source_limit_exceeded);
    }

    SECTION("invalid limits are rejected before source interpretation")
    {
        const auto source = fixture::synthetic_valid_wad3();
        auto limits = wad3::GoldSrcWad3CatalogLimits{};
        limits.maximum_lump_count = 0U;
        const auto result = parse(source, limits);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3CatalogErrorCode::invalid_configuration);
    }
}

TEST_CASE("WAD3 directory range is signed bounded and disjoint",
    "[goldsrc-wad3][catalog][ranges]")
{
    SECTION("negative directory offset")
    {
        auto source = fixture::synthetic_valid_wad3();
        fixture::synthetic_wad3_write_i32le(source.bytes, 8U, -1);
        const auto result = parse(source);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3CatalogErrorCode::negative_directory_offset);
    }

    SECTION("directory overlaps header")
    {
        auto source = fixture::synthetic_valid_wad3();
        fixture::synthetic_wad3_write_i32le(source.bytes, 8U, 4);
        const auto result = parse(source);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3CatalogErrorCode::directory_overlaps_header);
    }

    SECTION("directory extends beyond source")
    {
        auto source = fixture::synthetic_valid_wad3();
        fixture::synthetic_wad3_write_i32le(
            source.bytes, 8U, static_cast<std::int32_t>(source.bytes.size() - 31U));
        const auto result = parse(source);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3CatalogErrorCode::directory_out_of_bounds);
    }

    SECTION("large checked directory range cannot wrap into the source")
    {
        auto source = fixture::synthetic_valid_wad3();
        fixture::synthetic_wad3_write_i32le(source.bytes, 8U, 0x7FFFFFF0);
        const auto result = parse(source);
        REQUIRE_FALSE(result);
        CHECK((result.error->code ==
                   wad3::GoldSrcWad3CatalogErrorCode::directory_range_overflow ||
               result.error->code ==
                   wad3::GoldSrcWad3CatalogErrorCode::directory_out_of_bounds));
    }
}

TEST_CASE("WAD3 entry signed ranges fail closed", "[goldsrc-wad3][catalog][entry-range]")
{
    const auto mutate_entry_i32 = [](const std::size_t field_offset, const std::int32_t value) {
        auto source = fixture::synthetic_valid_wad3();
        fixture::synthetic_wad3_write_i32le(
            source.bytes,
            source.directory_offset + field_offset,
            value);
        return source;
    };

    SECTION("negative file position")
    {
        const auto result = parse(mutate_entry_i32(0U, -1));
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3CatalogErrorCode::negative_file_position);
    }

    SECTION("negative disk size")
    {
        const auto result = parse(mutate_entry_i32(4U, -1));
        REQUIRE_FALSE(result);
        CHECK(result.error->code == wad3::GoldSrcWad3CatalogErrorCode::negative_disk_size);
    }

    SECTION("negative uncompressed size")
    {
        const auto result = parse(mutate_entry_i32(8U, -1));
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3CatalogErrorCode::negative_uncompressed_size);
    }

    SECTION("lump beyond source")
    {
        auto source = fixture::synthetic_valid_wad3();
        const auto entry_offset = source.directory_offset;
        fixture::synthetic_wad3_write_i32le(
            source.bytes,
            entry_offset,
            static_cast<std::int32_t>(source.bytes.size()));
        fixture::synthetic_wad3_write_i32le(source.bytes, entry_offset + 4U, 1);
        fixture::synthetic_wad3_write_i32le(source.bytes, entry_offset + 8U, 1);
        const auto result = parse(source);
        REQUIRE_FALSE(result);
        CHECK(result.error->code == wad3::GoldSrcWad3CatalogErrorCode::lump_out_of_bounds);
    }

    SECTION("lump overlaps header")
    {
        auto source = fixture::synthetic_valid_wad3();
        const auto entry_offset = source.directory_offset;
        fixture::synthetic_wad3_write_i32le(source.bytes, entry_offset, 0);
        fixture::synthetic_wad3_write_i32le(source.bytes, entry_offset + 4U, 1);
        fixture::synthetic_wad3_write_i32le(source.bytes, entry_offset + 8U, 1);
        const auto result = parse(source);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3CatalogErrorCode::lump_overlaps_header);
    }

    SECTION("lump overlaps directory")
    {
        auto source = fixture::synthetic_valid_wad3();
        const auto entry_offset = source.directory_offset;
        fixture::synthetic_wad3_write_i32le(
            source.bytes,
            entry_offset,
            static_cast<std::int32_t>(source.directory_offset));
        fixture::synthetic_wad3_write_i32le(source.bytes, entry_offset + 4U, 1);
        fixture::synthetic_wad3_write_i32le(source.bytes, entry_offset + 8U, 1);
        const auto result = parse(source);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3CatalogErrorCode::lump_overlaps_directory);
    }

    SECTION("two nonempty lump ranges overlap")
    {
        auto source = fixture::synthetic_wad3(
            {fixture::SyntheticWad3Entry{
                 "FIRST", fixture::synthetic_goldsrc_miptex("FIRST")},
             fixture::SyntheticWad3Entry{
                 "SECOND", fixture::synthetic_goldsrc_miptex("SECOND")}});
        const auto second_entry = source.directory_offset +
            fixture::kSyntheticWad3DirectoryEntrySize;
        fixture::synthetic_wad3_write_i32le(
            source.bytes,
            second_entry,
            static_cast<std::int32_t>(source.payload_offsets[0U] + 1U));
        const auto result = parse(source);
        REQUIRE_FALSE(result);
        CHECK(result.error->code == wad3::GoldSrcWad3CatalogErrorCode::lump_overlap);
    }
}

TEST_CASE("WAD3 entry profiles are strict", "[goldsrc-wad3][catalog][entry-profile]")
{
    SECTION("compression zero with equal sizes is accepted")
    {
        CHECK(parse(fixture::synthetic_valid_wad3()));
    }

    SECTION("unsupported compression is typed")
    {
        auto entry = fixture::SyntheticWad3Entry{};
        entry.compression = 1U;
        const auto result = parse(fixture::synthetic_wad3({std::move(entry)}));
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3CatalogErrorCode::unsupported_compression);
    }

    SECTION("uncompressed disk and logical sizes must match")
    {
        auto entry = fixture::SyntheticWad3Entry{};
        entry.uncompressed_size = static_cast<std::int32_t>(entry.payload.size() - 1U);
        const auto result = parse(fixture::synthetic_wad3({std::move(entry)}));
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3CatalogErrorCode::uncompressed_size_mismatch);
    }

    SECTION("both directory padding bytes are exact zero")
    {
        auto entry = fixture::SyntheticWad3Entry{};
        entry.padding1 = 1U;
        const auto result = parse(fixture::synthetic_wad3({std::move(entry)}));
        REQUIRE_FALSE(result);
        CHECK(result.error->code == wad3::GoldSrcWad3CatalogErrorCode::nonzero_padding);
    }

    SECTION("a full printable 16-byte entry name is accepted")
    {
        const auto source = fixture::synthetic_valid_wad3("ABCDEFGHIJKLMNOP");
        const auto result = parse(source);
        REQUIRE(result);
        CHECK(result.catalog->entries().front().source_name == "ABCDEFGHIJKLMNOP");
    }

    SECTION("empty and non-ASCII names are rejected")
    {
        auto source = fixture::synthetic_valid_wad3();
        const auto name_offset = source.directory_offset + 16U;
        for (std::size_t index = 0U; index < 16U; ++index) {
            source.bytes[name_offset + index] = std::byte{0};
        }
        auto result = parse(source);
        REQUIRE_FALSE(result);
        CHECK(result.error->code == wad3::GoldSrcWad3CatalogErrorCode::invalid_entry_name);

        auto non_ascii_source = fixture::synthetic_valid_wad3();
        non_ascii_source.bytes[non_ascii_source.directory_offset + 16U] =
            std::byte{0x80};
        const auto non_ascii_result = parse(non_ascii_source);
        REQUIRE_FALSE(non_ascii_result);
        CHECK(non_ascii_result.error->code ==
            wad3::GoldSrcWad3CatalogErrorCode::invalid_entry_name);
    }
}

TEST_CASE("WAD3 miptex lookup is deterministic and ambiguity fails catalog parse",
    "[goldsrc-wad3][catalog][lookup]")
{
    SECTION("exact and ASCII-insensitive lookup")
    {
        const auto result = parse(fixture::synthetic_valid_wad3("MiXeD"));
        REQUIRE(result);
        REQUIRE(result.catalog->find_exact_miptex("MiXeD") != nullptr);
        CHECK(result.catalog->find_exact_miptex("mixed") == nullptr);
        REQUIRE(result.catalog->find_miptex("mixed") != nullptr);
        CHECK(result.catalog->find_miptex("MIXED")->directory_ordinal == 0U);
        CHECK(result.catalog->find_miptex("") == nullptr);
        CHECK(result.catalog->find_miptex("CONTROL\n") == nullptr);
    }

    SECTION("duplicate normalized miptex names are rejected as ambiguous")
    {
        const auto source = fixture::synthetic_wad3(
            {fixture::SyntheticWad3Entry{
                 "DUPLICATE", fixture::synthetic_goldsrc_miptex("DUPLICATE")},
             fixture::SyntheticWad3Entry{
                 "duplicate", fixture::synthetic_goldsrc_miptex("duplicate")}});
        const auto result = parse(source);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            wad3::GoldSrcWad3CatalogErrorCode::ambiguous_texture_name);
        CHECK(result.error->directory_ordinal == 1U);
    }

    SECTION("non-miptex entries remain metadata but never satisfy texture lookup")
    {
        auto entry = fixture::SyntheticWad3Entry{};
        entry.name = "OTHER";
        entry.type = 0x42U;
        const auto result = parse(fixture::synthetic_wad3({std::move(entry)}));
        REQUIRE(result);
        REQUIRE(result.catalog->entry_count() == 1U);
        CHECK_FALSE(result.catalog->entries().front().is_miptex());
        CHECK(result.catalog->find_miptex("OTHER") == nullptr);
    }
}

TEST_CASE("WAD3 catalog owns only metadata and survives source destruction",
    "[goldsrc-wad3][catalog][owning]")
{
    std::optional<wad3::GoldSrcWad3Catalog> retained;
    std::size_t original_byte_count = 0U;
    {
        auto source = fixture::synthetic_valid_wad3("RETAINED");
        original_byte_count = source.bytes.size();
        auto result = parse(source);
        REQUIRE(result);
        retained.emplace(std::move(*result.catalog));
        std::ranges::fill(source.bytes, std::byte{0xCC});
        source.bytes.clear();
        source.bytes.shrink_to_fit();
    }

    REQUIRE(retained.has_value());
    CHECK(retained->source_byte_count() == original_byte_count);
    REQUIRE(retained->find_miptex("retained") != nullptr);
    CHECK(retained->find_miptex("retained")->source_name == "RETAINED");
    CHECK(retained->entries().front().disk_size > 0U);
}

TEST_CASE("WAD3 catalog error names are bounded and stable",
    "[goldsrc-wad3][catalog][diagnostics]")
{
    CHECK(wad3::to_string(wad3::GoldSrcWad3CatalogErrorCode::unsupported_compression) ==
        "unsupported_compression");
    CHECK(wad3::to_string(wad3::GoldSrcWad3CatalogErrorCode::ambiguous_texture_name) ==
        "ambiguous_texture_name");

    auto source = fixture::synthetic_valid_wad3();
    source.bytes[0U] = std::byte{'X'};
    const auto result = parse(source);
    REQUIRE_FALSE(result);
    REQUIRE(result.error.has_value());
    CHECK(result.error->context.size() <=
        wad3::kGoldSrcWad3MaximumDiagnosticContextBytes);
    CHECK(result.error->context.find("WAD_TEXTURE") == std::string::npos);
}

} // namespace
