#include <hlclient/goldsrc/spatial/goldsrc_pvs_decoder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

namespace spatial = hlclient::goldsrc::spatial;

[[nodiscard]] std::vector<std::uint8_t> as_u8(
    const std::vector<std::byte>& bytes)
{
    std::vector<std::uint8_t> output;
    output.reserve(bytes.size());
    for (const auto value : bytes) {
        output.push_back(std::to_integer<std::uint8_t>(value));
    }
    return output;
}

TEST_CASE("GoldSrc PVS literals and zero runs decode to an exact row",
    "[goldsrc-pvs][decoder]")
{
    SECTION("one literal byte")
    {
        constexpr std::array source{std::byte{0xA5U}};
        const auto result = spatial::GoldSrcPvsDecoder::decode(source, 0U, 1U);
        REQUIRE(result);
        CHECK(as_u8(*result.row) == std::vector<std::uint8_t>{0xA5U});
        CHECK(result.source_bytes_consumed == 1U);
    }

    SECTION("multiple literal bytes")
    {
        constexpr std::array source{std::byte{0x11U}, std::byte{0x22U}};
        const auto result = spatial::GoldSrcPvsDecoder::decode(source, 0U, 2U);
        REQUIRE(result);
        CHECK(as_u8(*result.row) ==
            std::vector<std::uint8_t>{0x11U, 0x22U});
    }

    SECTION("one and multiple zero runs")
    {
        constexpr std::array one_run{std::byte{0U}, std::byte{3U}};
        const auto one = spatial::GoldSrcPvsDecoder::decode(one_run, 0U, 3U);
        REQUIRE(one);
        CHECK(as_u8(*one.row) ==
            std::vector<std::uint8_t>{0U, 0U, 0U});

        constexpr std::array two_runs{
            std::byte{0U}, std::byte{2U}, std::byte{0U}, std::byte{1U}};
        const auto two = spatial::GoldSrcPvsDecoder::decode(two_runs, 0U, 3U);
        REQUIRE(two);
        CHECK(as_u8(*two.row) ==
            std::vector<std::uint8_t>{0U, 0U, 0U});
    }

    SECTION("mixed literal and run stream")
    {
        constexpr std::array source{
            std::byte{0xAAU},
            std::byte{0U},
            std::byte{2U},
            std::byte{0x55U}};
        const auto result = spatial::GoldSrcPvsDecoder::decode(source, 0U, 4U);
        REQUIRE(result);
        CHECK(as_u8(*result.row) ==
            std::vector<std::uint8_t>{0xAAU, 0U, 0U, 0x55U});
    }

    SECTION("exact row completion does not consume the next encoded row")
    {
        constexpr std::array source{
            std::byte{0x12U}, std::byte{0x34U}, std::byte{0x56U}};
        const auto result = spatial::GoldSrcPvsDecoder::decode(source, 0U, 2U);
        REQUIRE(result);
        CHECK(result.source_bytes_consumed == 2U);
        CHECK(as_u8(*result.row) ==
            std::vector<std::uint8_t>{0x12U, 0x34U});
    }
}

TEST_CASE("GoldSrc PVS malformed rows fail without partial publication",
    "[goldsrc-pvs][decoder][errors]")
{
    const auto require_error = [](
                                   const std::vector<std::byte>& source,
                                   const std::size_t offset,
                                   const std::size_t row_bytes,
                                   const spatial::GoldSrcPvsDecodeErrorCode expected) {
        const auto result = spatial::GoldSrcPvsDecoder::decode(
            source,
            offset,
            row_bytes);
        REQUIRE_FALSE(result);
        CHECK_FALSE(result.row.has_value());
        REQUIRE(result.error);
        CHECK(result.error->code == expected);
    };

    SECTION("truncated literal stream")
    {
        require_error(
            {std::byte{0x7FU}},
            0U,
            2U,
            spatial::GoldSrcPvsDecodeErrorCode::truncated_stream);
    }
    SECTION("missing run count")
    {
        require_error(
            {std::byte{0U}},
            0U,
            1U,
            spatial::GoldSrcPvsDecodeErrorCode::missing_zero_run_count);
    }
    SECTION("zero run count")
    {
        require_error(
            {std::byte{0U}, std::byte{0U}},
            0U,
            1U,
            spatial::GoldSrcPvsDecodeErrorCode::zero_run_count);
    }
    SECTION("run exceeds exact output")
    {
        require_error(
            {std::byte{0U}, std::byte{3U}},
            0U,
            2U,
            spatial::GoldSrcPvsDecodeErrorCode::output_overflow);
    }
    SECTION("offset outside lump")
    {
        require_error(
            {std::byte{0x01U}},
            1U,
            1U,
            spatial::GoldSrcPvsDecodeErrorCode::offset_out_of_bounds);
    }
}

TEST_CASE("GoldSrc PVS decoder applies row and decompressed byte limits",
    "[goldsrc-pvs][decoder][limits]")
{
    constexpr std::array source{std::byte{0x01U}, std::byte{0x02U}};

    SECTION("row-byte exact limit and limit plus one")
    {
        spatial::GoldSrcPvsDecodeLimits limits;
        limits.maximum_row_bytes = 1U;
        REQUIRE(spatial::GoldSrcPvsDecoder::decode(source, 0U, 1U, limits));
        const auto over = spatial::GoldSrcPvsDecoder::decode(source, 0U, 2U, limits);
        REQUIRE_FALSE(over);
        REQUIRE(over.error);
        CHECK(over.error->code ==
            spatial::GoldSrcPvsDecodeErrorCode::row_byte_limit_exceeded);
    }

    SECTION("decompressed-byte exact limit and limit plus one")
    {
        spatial::GoldSrcPvsDecodeLimits limits;
        limits.maximum_decompressed_bytes = 1U;
        REQUIRE(spatial::GoldSrcPvsDecoder::decode(source, 0U, 1U, limits));
        const auto over = spatial::GoldSrcPvsDecoder::decode(source, 0U, 2U, limits);
        REQUIRE_FALSE(over);
        REQUIRE(over.error);
        CHECK(over.error->code == spatial::GoldSrcPvsDecodeErrorCode::
            decompressed_byte_limit_exceeded);
    }

    SECTION("invalid hard-bound configuration")
    {
        spatial::GoldSrcPvsDecodeLimits limits;
        limits.maximum_decompressed_bytes =
            spatial::kGoldSrcPvsHardMaximumDecompressedBytes + 1U;
        const auto result = spatial::GoldSrcPvsDecoder::decode(source, 0U, 1U, limits);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            spatial::GoldSrcPvsDecodeErrorCode::invalid_configuration);
    }

    SECTION("hard row capacity succeeds and capacity plus one fails before allocation")
    {
        std::vector<std::byte> compressed;
        for (std::size_t run = 0U; run < 4U; ++run) {
            compressed.push_back(std::byte{0U});
            compressed.push_back(std::byte{255U});
        }
        compressed.push_back(std::byte{0U});
        compressed.push_back(std::byte{4U});

        const auto exact = spatial::GoldSrcPvsDecoder::decode(
            compressed,
            0U,
            spatial::kGoldSrcPvsHardMaximumRowBytes);
        REQUIRE(exact);
        REQUIRE(exact.row);
        CHECK(exact.row->size() == spatial::kGoldSrcPvsHardMaximumRowBytes);

        const auto over = spatial::GoldSrcPvsDecoder::decode(
            compressed,
            0U,
            spatial::kGoldSrcPvsHardMaximumRowBytes + 1U);
        REQUIRE_FALSE(over);
        CHECK_FALSE(over.row);
        REQUIRE(over.error);
        CHECK(over.error->code ==
            spatial::GoldSrcPvsDecodeErrorCode::row_byte_limit_exceeded);
    }

    CHECK(spatial::to_string(
              spatial::GoldSrcPvsDecodeErrorCode::unable_to_retain_row) ==
        std::string_view{"unable_to_retain_row"});
}

TEST_CASE("GoldSrc PVS decoding is deterministic",
    "[goldsrc-pvs][decoder][determinism]")
{
    constexpr std::array source{
        std::byte{0x11U}, std::byte{0U}, std::byte{2U}, std::byte{0x22U}};
    const auto first = spatial::GoldSrcPvsDecoder::decode(source, 0U, 4U);
    const auto second = spatial::GoldSrcPvsDecoder::decode(source, 0U, 4U);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(*first.row == *second.row);
    CHECK(first.source_bytes_consumed == second.source_bytes_consumed);
}

} // namespace
