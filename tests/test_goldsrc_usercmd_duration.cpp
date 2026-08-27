#include <hlclient/goldsrc/usercmd_duration.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

void require_error(
    const goldsrc::GoldSrcUserCmdDurationResult& result,
    const goldsrc::GoldSrcUserCmdDurationErrorCode expected)
{
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
}

} // namespace

TEST_CASE("GoldSrc usercmd duration carries sub-millisecond remainder exactly",
          "[goldsrc][usercmd][duration][remainder]")
{
    const auto first = goldsrc::GoldSrcUserCmdDurationQuantizer::quantize(
        0.0004);
    REQUIRE(first);
    REQUIRE(first.quantization);
    CHECK(first.quantization->command_msec.empty());
    CHECK(first.quantization->requested_nanoseconds == 400'000U);
    CHECK(first.quantization->represented_milliseconds == 0U);
    CHECK(first.quantization->remainder_nanoseconds == 400'000);

    const auto second = goldsrc::GoldSrcUserCmdDurationQuantizer::quantize(
        0.0007, first.quantization->remainder_nanoseconds);
    REQUIRE(second);
    REQUIRE(second.quantization);
    CHECK(second.quantization->command_msec ==
          std::vector<std::uint8_t>{1U});
    CHECK(second.quantization->requested_nanoseconds == 700'000U);
    CHECK(second.quantization->represented_milliseconds == 1U);
    CHECK(second.quantization->remainder_nanoseconds == 100'000);

    const auto rounded = goldsrc::GoldSrcUserCmdDurationQuantizer::quantize(
        0.000'000'000'6);
    REQUIRE(rounded);
    REQUIRE(rounded.quantization);
    CHECK(rounded.quantization->requested_nanoseconds == 1U);
    CHECK(rounded.quantization->remainder_nanoseconds == 1);
}

TEST_CASE("GoldSrc usercmd duration splits oldest-first into bounded wire bytes",
          "[goldsrc][usercmd][duration][split]")
{
    const auto split = goldsrc::GoldSrcUserCmdDurationQuantizer::quantize(
        0.600, 0, 255U, 3U);
    REQUIRE(split);
    REQUIRE(split.quantization);
    CHECK(split.quantization->command_msec ==
          std::vector<std::uint8_t>{255U, 255U, 90U});
    CHECK(split.quantization->represented_milliseconds == 600U);
    CHECK(split.quantization->requested_nanoseconds == 600'000'000U);
    CHECK(split.quantization->remainder_nanoseconds == 0);

    const auto exact = goldsrc::GoldSrcUserCmdDurationQuantizer::quantize(
        0.510, 0, 255U, 2U);
    REQUIRE(exact);
    REQUIRE(exact.quantization);
    CHECK(exact.quantization->command_msec ==
          std::vector<std::uint8_t>{255U, 255U});

    const auto zero = goldsrc::GoldSrcUserCmdDurationQuantizer::quantize(0.0);
    REQUIRE(zero);
    REQUIRE(zero.quantization);
    CHECK(zero.quantization->command_msec.empty());
    CHECK(zero.quantization->requested_nanoseconds == 0U);
    CHECK(zero.quantization->remainder_nanoseconds == 0);
}

TEST_CASE("GoldSrc usercmd duration rejects invalid configuration and numerics",
          "[goldsrc][usercmd][duration][bounds]")
{
    require_error(
        goldsrc::GoldSrcUserCmdDurationQuantizer::quantize(0.01, 0, 0U, 1U),
        goldsrc::GoldSrcUserCmdDurationErrorCode::invalid_configuration);
    require_error(
        goldsrc::GoldSrcUserCmdDurationQuantizer::quantize(0.01, 0, 1U, 0U),
        goldsrc::GoldSrcUserCmdDurationErrorCode::invalid_configuration);
    require_error(
        goldsrc::GoldSrcUserCmdDurationQuantizer::quantize(
            0.01,
            0,
            1U,
            goldsrc::kMaximumUserCmdDurationSegments + 1U),
        goldsrc::GoldSrcUserCmdDurationErrorCode::invalid_configuration);
    require_error(
        goldsrc::GoldSrcUserCmdDurationQuantizer::quantize(0.01, -1),
        goldsrc::GoldSrcUserCmdDurationErrorCode::invalid_configuration);
    require_error(
        goldsrc::GoldSrcUserCmdDurationQuantizer::quantize(0.01, 1'000'000),
        goldsrc::GoldSrcUserCmdDurationErrorCode::invalid_configuration);

    require_error(
        goldsrc::GoldSrcUserCmdDurationQuantizer::quantize(
            std::numeric_limits<double>::quiet_NaN()),
        goldsrc::GoldSrcUserCmdDurationErrorCode::non_finite_duration);
    require_error(
        goldsrc::GoldSrcUserCmdDurationQuantizer::quantize(
            std::numeric_limits<double>::infinity()),
        goldsrc::GoldSrcUserCmdDurationErrorCode::non_finite_duration);
    require_error(
        goldsrc::GoldSrcUserCmdDurationQuantizer::quantize(-0.001),
        goldsrc::GoldSrcUserCmdDurationErrorCode::negative_duration);
    require_error(
        goldsrc::GoldSrcUserCmdDurationQuantizer::quantize(
            std::numeric_limits<double>::max()),
        goldsrc::GoldSrcUserCmdDurationErrorCode::duration_overflow);

    require_error(
        goldsrc::GoldSrcUserCmdDurationQuantizer::quantize(
            0.511, 0, 255U, 2U),
        goldsrc::GoldSrcUserCmdDurationErrorCode::segment_limit_exceeded);
    require_error(
        goldsrc::GoldSrcUserCmdDurationQuantizer::quantize(
            5'000'000.0, 0, 255U, 256U),
        goldsrc::GoldSrcUserCmdDurationErrorCode::segment_limit_exceeded);
}
