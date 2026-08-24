#include <hlclient/goldsrc/bsp/goldsrc_bsp_format.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>

#include "synthetic_goldsrc_bsp_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace bsp = hlclient::goldsrc::bsp;
namespace fixture = hlclient::tests;

[[nodiscard]] bsp::GoldSrcBspParseResult parse(const std::vector<std::byte>& bytes)
{
    return bsp::GoldSrcBspParser::parse(bytes);
}

TEST_CASE("The independent literal BSP has the exact v30 header and lump profile",
    "[goldsrc-bsp][header][literal]")
{
    STATIC_REQUIRE(bsp::kGoldSrcBspHeaderWireSize == 124U);
    STATIC_REQUIRE(bsp::kGoldSrcBspLumpCount == 15U);
    STATIC_REQUIRE(bsp::kGoldSrcBspVersion == 30);

    const auto bytes = fixture::literal_minimal_goldsrc_bsp_v30();
    REQUIRE(bytes.size() == 482U);
    REQUIRE(fixture::synthetic_read_i32le(bytes, 0U) == 30);

    const auto result = parse(bytes);
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    REQUIRE(result.document->lump_element_counts[bsp::goldsrc_bsp_lump_index(
                bsp::GoldSrcBspLumpId::planes)] == 1U);
    REQUIRE(result.document->lump_element_counts[bsp::goldsrc_bsp_lump_index(
                bsp::GoldSrcBspLumpId::vertices)] == 4U);
    REQUIRE(result.document->lump_element_counts[bsp::goldsrc_bsp_lump_index(
                bsp::GoldSrcBspLumpId::faces)] == 1U);
    REQUIRE(result.document->lump_element_counts[bsp::goldsrc_bsp_lump_index(
                bsp::GoldSrcBspLumpId::models)] == 1U);
}

TEST_CASE("Every strict header truncation prefix is rejected without publication",
    "[goldsrc-bsp][header][mutation]")
{
    const auto baseline = fixture::literal_minimal_goldsrc_bsp_v30();
    for (std::size_t size = 0U; size < bsp::kGoldSrcBspHeaderWireSize; ++size) {
        INFO(size);
        const std::vector<std::byte> truncated(baseline.begin(),
            baseline.begin() + static_cast<std::ptrdiff_t>(size));
        const auto result = parse(truncated);
        REQUIRE_FALSE(result);
        REQUIRE_FALSE(result.document.has_value());
        REQUIRE(result.error.has_value());
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::source_too_small);
        REQUIRE(result.error->context.size() <=
            bsp::kGoldSrcBspMaximumDiagnosticContextBytes);
    }
}

TEST_CASE("Only BSP version 30 is accepted", "[goldsrc-bsp][header][version]")
{
    for (const auto version : {29, 31}) {
        INFO(version);
        const auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                               .set_version(version)
                               .take();
        const auto result = parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error.has_value());
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::unsupported_version);
        REQUIRE_FALSE(result.error->lump_id.has_value());
    }
}

TEST_CASE("Signed lump ranges and checked ends fail closed", "[goldsrc-bsp][header][range]")
{
    SECTION("negative offset")
    {
        const auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                               .set_lump_offset(fixture::SyntheticBspLumpId::planes, -1)
                               .take();
        const auto result = parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::negative_lump_range);
        REQUIRE(result.error->lump_id == bsp::GoldSrcBspLumpId::planes);
    }

    SECTION("negative length")
    {
        const auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                               .set_lump_length(fixture::SyntheticBspLumpId::planes, -1)
                               .take();
        const auto result = parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::negative_lump_range);
        REQUIRE(result.error->lump_id == bsp::GoldSrcBspLumpId::planes);
    }

    SECTION("signed maximum offset and length cannot wrap into a valid range")
    {
        const auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                               .set_lump_descriptor(fixture::SyntheticBspLumpId::planes,
                                   std::numeric_limits<std::int32_t>::max(),
                                   std::numeric_limits<std::int32_t>::max())
                               .take();
        const auto result = parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE((result.error->code == bsp::GoldSrcBspErrorCode::lump_range_overflow ||
            result.error->code == bsp::GoldSrcBspErrorCode::lump_out_of_bounds));
    }

    SECTION("range beyond source")
    {
        const auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                               .set_lump_offset(fixture::SyntheticBspLumpId::planes, 480)
                               .take();
        const auto result = parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::lump_out_of_bounds);
    }
}

TEST_CASE("Non-empty lumps neither overlap the header nor one another",
    "[goldsrc-bsp][header][overlap]")
{
    SECTION("header overlap")
    {
        const auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                               .set_lump_offset(fixture::SyntheticBspLumpId::planes, 120)
                               .take();
        const auto result = parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::lump_overlaps_header);
    }

    SECTION("lump overlap")
    {
        auto corruptor = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()};
        const auto plane_offset = static_cast<std::int32_t>(
            corruptor.lump_offset(fixture::SyntheticBspLumpId::planes));
        const auto bytes = std::move(corruptor)
                               .set_lump_descriptor(
                                   fixture::SyntheticBspLumpId::faces, plane_offset, 20)
                               .take();
        const auto result = parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::lump_overlap);
    }

    SECTION("zero-length lump may use offset zero")
    {
        auto bytes = fixture::literal_minimal_goldsrc_bsp_v30();
        const auto descriptor = fixture::synthetic_lump_descriptor_offset(
            fixture::SyntheticBspLumpId::entities);
        REQUIRE(fixture::synthetic_read_i32le(bytes, descriptor) == 0);
        REQUIRE(fixture::synthetic_read_i32le(bytes, descriptor + 4U) == 0);
        REQUIRE(parse(bytes));
    }
}

TEST_CASE("Lumps may be unordered and byte-unaligned", "[goldsrc-bsp][header][wire]")
{
    SECTION("physically unordered non-overlapping lumps")
    {
        auto bytes = fixture::literal_minimal_goldsrc_bsp_v30();
        constexpr std::size_t plane_offset = 124U;
        constexpr std::size_t face_offset = 304U;
        constexpr std::size_t record_size = 20U;
        std::array<std::byte, record_size> plane{};
        std::array<std::byte, record_size> face{};
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(plane_offset), record_size,
            plane.begin());
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(face_offset), record_size,
            face.begin());
        std::copy(face.begin(), face.end(),
            bytes.begin() + static_cast<std::ptrdiff_t>(plane_offset));
        std::copy(plane.begin(), plane.end(),
            bytes.begin() + static_cast<std::ptrdiff_t>(face_offset));
        fixture::synthetic_write_i32le(bytes,
            fixture::synthetic_lump_descriptor_offset(fixture::SyntheticBspLumpId::planes),
            static_cast<std::int32_t>(face_offset));
        fixture::synthetic_write_i32le(bytes,
            fixture::synthetic_lump_descriptor_offset(fixture::SyntheticBspLumpId::faces),
            static_cast<std::int32_t>(plane_offset));
        REQUIRE(parse(bytes));
    }

    SECTION("all non-empty lumps shifted to unaligned file offsets")
    {
        auto bytes = fixture::literal_minimal_goldsrc_bsp_v30();
        bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(bsp::kGoldSrcBspHeaderWireSize),
            std::byte{0xA5});
        for (std::size_t index = 0U; index < fixture::kSyntheticBspLumpCount; ++index) {
            const auto descriptor = 4U + (index * 8U);
            const auto length = fixture::synthetic_read_i32le(bytes, descriptor + 4U);
            if (length != 0) {
                const auto offset = fixture::synthetic_read_i32le(bytes, descriptor);
                fixture::synthetic_write_i32le(bytes, descriptor, offset + 1);
            }
        }
        REQUIRE(parse(bytes));
    }
}

TEST_CASE("Fixed record alignment and configured counts are exact",
    "[goldsrc-bsp][header][limits]")
{
    SECTION("source size equals configured limit")
    {
        const auto bytes = fixture::literal_minimal_goldsrc_bsp_v30();
        auto limits = bsp::GoldSrcBspImportLimits{};
        limits.maximum_source_bytes = bytes.size();
        REQUIRE(bsp::GoldSrcBspParser::parse(bytes, limits));
    }

    SECTION("source size is configured limit plus one")
    {
        const auto bytes = fixture::literal_minimal_goldsrc_bsp_v30();
        auto limits = bsp::GoldSrcBspImportLimits{};
        limits.maximum_source_bytes = bytes.size() - 1U;
        const auto result = bsp::GoldSrcBspParser::parse(bytes, limits);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code ==
            bsp::GoldSrcBspErrorCode::count_limit_exceeded);
        REQUIRE_FALSE(result.document.has_value());
    }

    SECTION("wrong fixed-record multiple")
    {
        const auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                               .set_lump_length(fixture::SyntheticBspLumpId::planes, 19)
                               .take();
        const auto result = parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code ==
            bsp::GoldSrcBspErrorCode::misaligned_fixed_lump_size);
        REQUIRE(result.error->lump_id == bsp::GoldSrcBspLumpId::planes);
    }

    SECTION("count equals configured limit")
    {
        auto limits = bsp::GoldSrcBspImportLimits{};
        limits.maximum_planes = 1U;
        REQUIRE(bsp::GoldSrcBspParser::parse(
            fixture::literal_minimal_goldsrc_bsp_v30(), limits));
    }

    SECTION("count is configured limit plus one")
    {
        const std::array planes{
            fixture::SyntheticBspPlane{}, fixture::SyntheticBspPlane{}};
        fixture::SyntheticBspBuilder builder;
        const auto bytes = builder.set_planes(planes).build();
        auto limits = bsp::GoldSrcBspImportLimits{};
        limits.maximum_planes = 1U;
        const auto result = bsp::GoldSrcBspParser::parse(bytes, limits);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::count_limit_exceeded);
        REQUIRE(result.error->lump_id == bsp::GoldSrcBspLumpId::planes);
    }
}

} // namespace
