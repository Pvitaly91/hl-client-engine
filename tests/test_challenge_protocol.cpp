#include <hlclient/goldsrc/challenge_protocol.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] std::vector<std::byte> make_datagram(const std::string_view body)
{
    std::vector<std::byte> result{
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
    const auto bytes = std::as_bytes(std::span{body.data(), body.size()});
    result.insert(result.end(), bytes.begin(), bytes.end());
    return result;
}

[[nodiscard]] std::vector<std::byte> make_response(std::string fields)
{
    fields.push_back('\n');
    fields.push_back('\0');
    return make_datagram(fields);
}

TEST_CASE("Protocol 48 getchallenge request uses exact observed bytes", "[goldsrc][challenge]")
{
    const std::array expected{
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{'g'},  std::byte{'e'},  std::byte{'t'},  std::byte{'c'},
        std::byte{'h'},  std::byte{'a'},  std::byte{'l'},  std::byte{'l'},
        std::byte{'e'},  std::byte{'n'},  std::byte{'g'},  std::byte{'e'},
        std::byte{' '},  std::byte{'s'},  std::byte{'t'},  std::byte{'e'},
        std::byte{'a'},  std::byte{'m'},  std::byte{'\n'},
    };

    const auto request = goldsrc::build_getchallenge_request();
    REQUIRE(request);
    CHECK(request.datagram->size() == 23U);
    CHECK(std::ranges::equal(*request.datagram, expected));
}

TEST_CASE("Parser accepts the exact captured original HLDS response", "[goldsrc][challenge]")
{
    const std::array captured{
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{'A'},  std::byte{'0'},  std::byte{'0'},  std::byte{'0'},
        std::byte{'0'},  std::byte{'0'},  std::byte{'0'},  std::byte{'0'},
        std::byte{'0'},  std::byte{' '},  std::byte{'3'},  std::byte{'6'},
        std::byte{'4'},  std::byte{'3'},  std::byte{'3'},  std::byte{'7'},
        std::byte{'8'},  std::byte{'8'},  std::byte{'7'},  std::byte{' '},
        std::byte{'3'},  std::byte{' '},  std::byte{'7'},  std::byte{'2'},
        std::byte{'0'},  std::byte{'5'},  std::byte{'7'},  std::byte{'5'},
        std::byte{'9'},  std::byte{'4'},  std::byte{'0'},  std::byte{'3'},
        std::byte{'7'},  std::byte{'9'},  std::byte{'2'},  std::byte{'7'},
        std::byte{'9'},  std::byte{'3'},  std::byte{'6'},  std::byte{' '},
        std::byte{'0'},  std::byte{'\n'}, std::byte{0},
    };

    const auto result = goldsrc::parse_challenge_response(captured);

    CHECK(captured.size() == 47U);
    REQUIRE(result);
    CHECK(result.response->challenge == 364'337'887);
    CHECK(result.response->profile_parameter_1 == 3U);
    CHECK(result.response->profile_parameter_2 == 72'057'594'037'927'936ULL);
    CHECK(result.response->profile_parameter_3 == 0U);
}

TEST_CASE("Challenge parser accepts non-negative int32 boundaries", "[goldsrc][challenge]")
{
    const auto minimum = goldsrc::parse_challenge_response(
        make_response("A00000000 0 3 72057594037927936 0"));
    const auto maximum = goldsrc::parse_challenge_response(
        make_response("A00000000 2147483647 3 72057594037927936 0"));

    REQUIRE(minimum);
    CHECK(minimum.response->challenge == 0);
    REQUIRE(maximum);
    CHECK(maximum.response->challenge == std::numeric_limits<std::int32_t>::max());
    CHECK(maximum.response->profile_parameter_2 == 72'057'594'037'927'936ULL);
    CHECK(maximum.response->profile_parameter_3 == 0U);
}

TEST_CASE("Challenge parser rejects each wrong connectionless header byte", "[goldsrc][challenge]")
{
    const auto valid = make_response("A00000000 1 3 72057594037927936 0");
    for (std::size_t index = 0; index < goldsrc::kConnectionlessPacketHeaderSize; ++index) {
        auto wrong = valid;
        wrong[index] = std::byte{0xfe};
        const auto result = goldsrc::parse_challenge_response(wrong);
        INFO("header byte " << index);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == goldsrc::ChallengeProtocolErrorCode::invalid_header);
    }
}

TEST_CASE("Challenge parser rejects header-only and oversized packets", "[goldsrc][challenge]")
{
    const std::array header{
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
    const auto short_result = goldsrc::parse_challenge_response(header);
    REQUIRE_FALSE(short_result);
    REQUIRE(short_result.error);
    CHECK(short_result.error->code == goldsrc::ChallengeProtocolErrorCode::packet_too_short);

    std::vector<std::byte> oversized(
        goldsrc::kMaximumConnectionlessChallengeDatagramSize + 1U, std::byte{0xff});
    const auto oversized_result = goldsrc::parse_challenge_response(oversized);
    REQUIRE_FALSE(oversized_result);
    REQUIRE(oversized_result.error);
    CHECK(oversized_result.error->code ==
          goldsrc::ChallengeProtocolErrorCode::payload_too_large);
}

TEST_CASE("Challenge parser rejects missing invalid overflow and negative tokens", "[goldsrc][challenge]")
{
    const auto missing = goldsrc::parse_challenge_response(
        make_response("A00000000 "));
    REQUIRE_FALSE(missing);
    CHECK(missing.error->code == goldsrc::ChallengeProtocolErrorCode::missing_challenge);

    const auto empty = goldsrc::parse_challenge_response(
        make_response("A00000000  3 0 0"));
    REQUIRE_FALSE(empty);
    CHECK(empty.error->code == goldsrc::ChallengeProtocolErrorCode::missing_challenge);

    const auto invalid = goldsrc::parse_challenge_response(
        make_response("A00000000 12x 3 0 0"));
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error->code == goldsrc::ChallengeProtocolErrorCode::invalid_challenge);

    const auto overflow = goldsrc::parse_challenge_response(
        make_response("A00000000 2147483648 3 0 0"));
    REQUIRE_FALSE(overflow);
    CHECK(overflow.error->code == goldsrc::ChallengeProtocolErrorCode::challenge_overflow);

    const auto negative = goldsrc::parse_challenge_response(
        make_response("A00000000 -1 3 0 0"));
    REQUIRE_FALSE(negative);
    CHECK(negative.error->code == goldsrc::ChallengeProtocolErrorCode::invalid_challenge);

    const auto leading_zero = goldsrc::parse_challenge_response(
        make_response("A00000000 01 3 72057594037927936 0"));
    REQUIRE_FALSE(leading_zero);
    CHECK(leading_zero.error->code == goldsrc::ChallengeProtocolErrorCode::invalid_challenge);
}

TEST_CASE("Challenge parser rejects wrong response and profile variants", "[goldsrc][challenge]")
{
    const auto wrong_type = goldsrc::parse_challenge_response(
        make_response("B00000000 1 3 0 0"));
    REQUIRE_FALSE(wrong_type);
    CHECK(wrong_type.error->code ==
          goldsrc::ChallengeProtocolErrorCode::unexpected_response_type);

    const auto leading_whitespace = goldsrc::parse_challenge_response(
        make_response(" A00000000 1 3 0 0"));
    REQUIRE_FALSE(leading_whitespace);
    CHECK(leading_whitespace.error->code ==
          goldsrc::ChallengeProtocolErrorCode::unexpected_response_type);

    const auto unsupported_parameter_1 = goldsrc::parse_challenge_response(
        make_response("A00000000 1 2 1 0"));
    REQUIRE_FALSE(unsupported_parameter_1);
    CHECK(unsupported_parameter_1.error->code ==
          goldsrc::ChallengeProtocolErrorCode::unsupported_variant);

    const auto unsupported_parameter_2 = goldsrc::parse_challenge_response(
        make_response("A00000000 1 3 72057594037927937 0"));
    REQUIRE_FALSE(unsupported_parameter_2);
    CHECK(unsupported_parameter_2.error->code ==
          goldsrc::ChallengeProtocolErrorCode::unsupported_variant);

    const auto unsupported_parameter_3 = goldsrc::parse_challenge_response(
        make_response("A00000000 1 3 72057594037927936 1"));
    REQUIRE_FALSE(unsupported_parameter_3);
    CHECK(unsupported_parameter_3.error->code ==
          goldsrc::ChallengeProtocolErrorCode::unsupported_variant);
}

TEST_CASE("Challenge parser rejects a valid body with the connectionless header absent",
          "[goldsrc][challenge]")
{
    std::string body{"A00000000 1 3 72057594037927936 0\n"};
    body.push_back('\0');
    const auto bytes = std::as_bytes(std::span{body.data(), body.size()});
    const auto result = goldsrc::parse_challenge_response(bytes);

    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == goldsrc::ChallengeProtocolErrorCode::invalid_header);
}

TEST_CASE("Challenge parser requires exactly LF then NUL", "[goldsrc][challenge]")
{
    const auto missing = goldsrc::parse_challenge_response(
        make_datagram("A00000000 1 3 72057594037927936 0"));
    REQUIRE_FALSE(missing);
    CHECK(missing.error->code == goldsrc::ChallengeProtocolErrorCode::invalid_terminator);

    auto duplicate = make_response("A00000000 1 3 72057594037927936 0");
    duplicate.push_back(std::byte{'\n'});
    duplicate.push_back(std::byte{0});
    const auto duplicate_result = goldsrc::parse_challenge_response(duplicate);
    REQUIRE_FALSE(duplicate_result);
    CHECK(duplicate_result.error->code ==
          goldsrc::ChallengeProtocolErrorCode::unexpected_trailing_data);

    std::string reversed{"A00000000 1 3 72057594037927936 0"};
    reversed.push_back('\0');
    reversed.push_back('\n');
    const auto reversed_result = goldsrc::parse_challenge_response(make_datagram(reversed));
    REQUIRE_FALSE(reversed_result);
    CHECK(reversed_result.error->code ==
          goldsrc::ChallengeProtocolErrorCode::invalid_terminator);

    std::string trailing{"A00000000 1 3 72057594037927936 0"};
    trailing.push_back('\n');
    trailing.push_back('\0');
    trailing.push_back('x');
    const auto trailing_result = goldsrc::parse_challenge_response(make_datagram(trailing));
    REQUIRE_FALSE(trailing_result);
    CHECK(trailing_result.error->code ==
          goldsrc::ChallengeProtocolErrorCode::unexpected_trailing_data);
}

TEST_CASE("Challenge parser rejects embedded line and NUL delimiters", "[goldsrc][challenge]")
{
    std::string embedded_nul{"A00000000 1"};
    embedded_nul.push_back('\0');
    embedded_nul.append(" 3 72057594037927936 0\n");
    embedded_nul.push_back('\0');
    const auto nul_result = goldsrc::parse_challenge_response(make_datagram(embedded_nul));
    CHECK_FALSE(nul_result);

    std::string embedded_line{"A00000000 1\n 3 72057594037927936 0\n"};
    embedded_line.push_back('\0');
    const auto line_result = goldsrc::parse_challenge_response(make_datagram(embedded_line));
    CHECK_FALSE(line_result);
}

TEST_CASE("Every truncation of a valid challenge response fails closed", "[goldsrc][challenge]")
{
    const auto valid = make_response("A00000000 364337887 3 72057594037927936 0");
    REQUIRE(goldsrc::parse_challenge_response(valid));

    for (std::size_t size = 0; size < valid.size(); ++size) {
        const auto result = goldsrc::parse_challenge_response(
            std::span<const std::byte>{valid}.first(size));
        INFO("truncated size " << size);
        CHECK_FALSE(result);
    }
}

} // namespace
