#include <hlclient/goldsrc/move_checksum.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

TEST_CASE("Synthetic move checksum matches independent CRC8 literals",
          "[goldsrc][usercmd][move-checksum][literal]")
{
    const goldsrc::GoldSrcMoveChecksum checksum;
    REQUIRE(checksum.valid_configuration());
    CHECK(checksum.profile() ==
          goldsrc::GoldSrcMoveChecksumProfile::synthetic_crc8_v1);

    SECTION("empty body")
    {
        const auto result = checksum.compute({0U, 0U}, {});
        REQUIRE(result);
        REQUIRE(result.checksum);
        CHECK(*result.checksum == 0xdaU);
        CHECK(result.covered_bytes == 0U);
        CHECK(result.padding_bits_participate);
        CHECK_FALSE(result.error);
    }

    SECTION("three full bytes and little-endian sequence")
    {
        constexpr std::array body{
            std::byte{0x01U}, std::byte{0x02U}, std::byte{0x03U}};
        const auto result = checksum.compute({0x1122'3344U, 24U}, body);
        REQUIRE(result);
        REQUIRE(result.checksum);
        CHECK(*result.checksum == 0xeaU);
        CHECK(result.covered_bytes == body.size());
        CHECK(result.padding_bits_participate);
    }

    SECTION("partial final byte is covered with its meaningful-bit count")
    {
        constexpr std::array body{std::byte{0xa5U}};
        const auto three_bits = checksum.compute({7U, 3U}, body);
        const auto four_bits = checksum.compute({7U, 4U}, body);
        REQUIRE(three_bits);
        REQUIRE(three_bits.checksum);
        REQUIRE(four_bits);
        REQUIRE(four_bits.checksum);
        CHECK(*three_bits.checksum == 0x87U);
        CHECK(*four_bits.checksum == 0x92U);
    }
}

TEST_CASE("Synthetic move checksum is sensitive to body sequence and bit count",
          "[goldsrc][usercmd][move-checksum][sensitivity]")
{
    const goldsrc::GoldSrcMoveChecksum checksum;
    constexpr std::array body{
        std::byte{0x01U}, std::byte{0x02U}, std::byte{0x03U}};
    constexpr std::array changed_body{
        std::byte{0x01U}, std::byte{0x02U}, std::byte{0x02U}};

    const auto reference = checksum.compute({0x1122'3344U, 24U}, body);
    const auto next_sequence = checksum.compute({0x1122'3345U, 24U}, body);
    const auto changed = checksum.compute({0x1122'3344U, 24U}, changed_body);
    REQUIRE(reference);
    REQUIRE(reference.checksum);
    REQUIRE(next_sequence);
    REQUIRE(next_sequence.checksum);
    REQUIRE(changed);
    REQUIRE(changed.checksum);
    CHECK(*reference.checksum == 0xeaU);
    CHECK(*next_sequence.checksum == 0xf9U);
    CHECK(*changed.checksum == 0xffU);
    CHECK(*reference.checksum != *next_sequence.checksum);
    CHECK(*reference.checksum != *changed.checksum);
}

TEST_CASE("Move checksum validates exact byte geometry and coverage bounds",
          "[goldsrc][usercmd][move-checksum][limit][malformed]")
{
    SECTION("configuration bounds")
    {
        const goldsrc::GoldSrcMoveChecksum zero_limit{
            goldsrc::GoldSrcMoveChecksumProfile::synthetic_crc8_v1, 0U};
        REQUIRE_FALSE(zero_limit.valid_configuration());
        const auto zero_result = zero_limit.compute({}, {});
        REQUIRE_FALSE(zero_result);
        REQUIRE(zero_result.error);
        CHECK(zero_result.error->code ==
              goldsrc::GoldSrcMoveChecksumErrorCode::invalid_configuration);

        const goldsrc::GoldSrcMoveChecksum excessive_limit{
            goldsrc::GoldSrcMoveChecksumProfile::synthetic_crc8_v1,
            goldsrc::kMaximumSyntheticMoveChecksumCoverageBytes + 1U};
        REQUIRE_FALSE(excessive_limit.valid_configuration());
        const auto excessive_result = excessive_limit.compute({}, {});
        REQUIRE_FALSE(excessive_result);
        REQUIRE(excessive_result.error);
        CHECK(excessive_result.error->code ==
              goldsrc::GoldSrcMoveChecksumErrorCode::invalid_configuration);

        const goldsrc::GoldSrcMoveChecksum unknown_profile{
            static_cast<goldsrc::GoldSrcMoveChecksumProfile>(0xffU)};
        REQUIRE_FALSE(unknown_profile.valid_configuration());
        const auto unknown_result = unknown_profile.compute({}, {});
        REQUIRE_FALSE(unknown_result);
        REQUIRE(unknown_result.error);
        CHECK(unknown_result.error->code ==
              goldsrc::GoldSrcMoveChecksumErrorCode::invalid_configuration);
    }

    SECTION("default coverage boundary")
    {
        const goldsrc::GoldSrcMoveChecksum checksum;
        const std::vector<std::byte> at_limit(
            goldsrc::kDefaultSyntheticMoveChecksumCoverageBytes,
            std::byte{0U});
        const auto accepted = checksum.compute(
            {std::numeric_limits<std::uint32_t>::max(),
             at_limit.size() * 8U},
            at_limit);
        REQUIRE(accepted);
        REQUIRE(accepted.checksum);
        CHECK(*accepted.checksum == 0x91U);
        CHECK(accepted.covered_bytes == at_limit.size());

        auto over_limit = at_limit;
        over_limit.push_back(std::byte{0U});
        const auto rejected = checksum.compute(
            {0U, over_limit.size() * 8U}, over_limit);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code == goldsrc::
              GoldSrcMoveChecksumErrorCode::coverage_limit_exceeded);
        CHECK(rejected.covered_bytes == 0U);
    }

    SECTION("owning-byte geometry")
    {
        const goldsrc::GoldSrcMoveChecksum checksum;
        constexpr std::array two_bytes{
            std::byte{0U}, std::byte{0U}};
        const auto too_short = checksum.compute({0U, 8U}, two_bytes);
        REQUIRE_FALSE(too_short);
        REQUIRE(too_short.error);
        CHECK(too_short.error->code ==
              goldsrc::GoldSrcMoveChecksumErrorCode::invalid_geometry);

        const auto accepted_partial = checksum.compute({7U, 9U}, two_bytes);
        REQUIRE(accepted_partial);
        REQUIRE(accepted_partial.checksum);
        CHECK(*accepted_partial.checksum == 0x2cU);

        constexpr std::array one_byte{std::byte{0U}};
        const auto too_long = checksum.compute({0U, 9U}, one_byte);
        REQUIRE_FALSE(too_long);
        REQUIRE(too_long.error);
        CHECK(too_long.error->code ==
              goldsrc::GoldSrcMoveChecksumErrorCode::invalid_geometry);

        const auto nonempty_empty_bits = checksum.compute({0U, 0U}, one_byte);
        REQUIRE_FALSE(nonempty_empty_bits);
        REQUIRE(nonempty_empty_bits.error);
        CHECK(nonempty_empty_bits.error->code ==
              goldsrc::GoldSrcMoveChecksumErrorCode::invalid_geometry);

        const auto empty_nonzero_bits = checksum.compute({0U, 1U}, {});
        REQUIRE_FALSE(empty_nonzero_bits);
        REQUIRE(empty_nonzero_bits.error);
        CHECK(empty_nonzero_bits.error->code ==
              goldsrc::GoldSrcMoveChecksumErrorCode::invalid_geometry);
    }
}

TEST_CASE("Stock move checksum gate precedes body inspection",
          "[goldsrc][usercmd][move-checksum][evidence]")
{
    const goldsrc::GoldSrcMoveChecksum checksum{
        goldsrc::GoldSrcMoveChecksumProfile::
            stock_protocol_48_build_10210_evidence_pending};
    constexpr std::array body{std::byte{0xffU}};
    const auto result = checksum.compute({0U, 99U}, body);
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code ==
          goldsrc::GoldSrcMoveChecksumErrorCode::stock_evidence_pending);
    CHECK_FALSE(result.checksum);
    CHECK(result.covered_bytes == 0U);
}

} // namespace
