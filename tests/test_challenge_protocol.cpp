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

TEST_CASE("Challenge parser preserves the full unsigned 32-bit token", "[goldsrc][challenge]")
{
    const auto minimum = goldsrc::parse_challenge_response(
        make_response("A00000000 0 3 72057594037927936 0"));
    const auto maximum = goldsrc::parse_challenge_response(
        make_response("A00000000 4294967295 3 72057594037927936 0"));

    REQUIRE(minimum);
    CHECK(minimum.response->challenge == 0);
    REQUIRE(maximum);
    CHECK(maximum.response->challenge == std::numeric_limits<std::uint32_t>::max());
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
        make_response("A00000000 4294967296 3 72057594037927936 0"));
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

    const auto alternate_server_identity = goldsrc::parse_challenge_response(
        make_response("A00000000 1 3 72057594037927937 0"));
    REQUIRE(alternate_server_identity);
    CHECK(alternate_server_identity.response->profile_parameter_2 ==
          72'057'594'037'927'937ULL);

    const auto secure_server = goldsrc::parse_challenge_response(
        make_response("A00000000 1 3 72057594037927936 1"));
    REQUIRE(secure_server);
    CHECK(secure_server.response->profile_parameter_3 == 1U);

    const auto unsupported_parameter_2 = goldsrc::parse_challenge_response(
        make_response("A00000000 1 3 0 0"));
    REQUIRE_FALSE(unsupported_parameter_2);
    CHECK(unsupported_parameter_2.error->code ==
          goldsrc::ChallengeProtocolErrorCode::unsupported_variant);

    const auto unsupported_parameter_3 = goldsrc::parse_challenge_response(
        make_response("A00000000 1 3 72057594037927936 2"));
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

TEST_CASE("E31 observed m suffix preserves plain challenge fields and explicit security",
          "[e31][goldsrc][challenge]")
{
    // Project-owned values with the observed field widths, NOT a retained
    // challenge/server identity from the user's packet. The suffix is opaque;
    // it is not a security flag and does not assign the server an engine type.
    for (const auto secure : {0U, 1U}) {
        const auto plain = goldsrc::parse_challenge_response(make_response(
            "A00000000 123456789 3 72057594037927936 " + std::to_string(secure)));
        const auto suffixed_bytes = make_response(
            "A00000000 123456789 3 72057594037927936m " + std::to_string(secure));
        CHECK(suffixed_bytes.size() == 48U);
        CHECK(suffixed_bytes[43U] == std::byte{'m'});
        const auto suffixed = goldsrc::parse_challenge_response(suffixed_bytes);
        REQUIRE(plain);
        REQUIRE(suffixed);
        CHECK(suffixed.response->challenge == plain.response->challenge);
        CHECK(suffixed.response->profile_parameter_1 == plain.response->profile_parameter_1);
        CHECK(suffixed.response->profile_parameter_2 == plain.response->profile_parameter_2);
        CHECK(suffixed.response->profile_parameter_3 == plain.response->profile_parameter_3);
        CHECK(suffixed.response->profile_parameter_3 == secure);
    }
}

TEST_CASE("E31 m suffix retains uint32 and uint64 boundaries without defaults",
          "[e31][goldsrc][challenge]")
{
    for (const auto challenge : {std::uint32_t{0}, std::numeric_limits<std::uint32_t>::max()}) {
        for (const auto identity : {std::uint64_t{1}, std::numeric_limits<std::uint64_t>::max()}) {
            for (const auto secure : {0U, 1U}) {
                for (const bool suffix : {false, true}) {
                    const auto fields = "A00000000 " + std::to_string(challenge) + " 3 " +
                        std::to_string(identity) + (suffix ? "m " : " ") + std::to_string(secure);
                    INFO("suffix=" << suffix << " secure=" << secure);
                    const auto result = goldsrc::parse_challenge_response(make_response(fields));
                    REQUIRE(result);
                    CHECK(result.response->challenge == challenge);
                    CHECK(result.response->profile_parameter_1 == 3U);
                    CHECK(result.response->profile_parameter_2 == identity);
                    CHECK(result.response->profile_parameter_3 == secure);
                }
            }
        }
    }
}

TEST_CASE("E31 m suffix accepts no alternate nondigit byte or wildcard whitespace",
          "[e31][goldsrc][challenge]")
{
    const auto valid = make_response("A00000000 123456789 3 72057594037927936m 0");
    REQUIRE(goldsrc::parse_challenge_response(valid));
    for (unsigned byte = 0; byte <= 255U; ++byte) {
        if (byte == static_cast<unsigned>('m') ||
            (byte >= static_cast<unsigned>('0') && byte <= static_cast<unsigned>('9'))) continue;
        auto replaced = valid;
        replaced[43U] = static_cast<std::byte>(byte);
        INFO("unsupported suffix byte=" << byte);
        CHECK_FALSE(goldsrc::parse_challenge_response(replaced));
    }
    // Decimal digits at this location legitimately extend the ID; they must
    // not be dropped as though they were a marker or globally rejected.
    for (unsigned digit = 0; digit <= 9U; ++digit) {
        auto extended = valid;
        extended[43U] = static_cast<std::byte>(static_cast<unsigned>('0') + digit);
        const auto result = goldsrc::parse_challenge_response(extended);
        REQUIRE(result);
        CHECK(result.response->profile_parameter_2 == 72'057'594'037'927'936ULL * 10ULL + digit);
        CHECK(result.response->challenge == 123'456'789U);
        CHECK(result.response->profile_parameter_3 == 0U);
    }
}

TEST_CASE("E31 m suffix is accepted exactly once only after the server identity",
          "[e31][goldsrc][challenge]")
{
    for (const auto fields : {
        "mA00000000 123456789 3 72057594037927936 0",
        "A00000000m 123456789 3 72057594037927936 0",
        "A00000000 123456789m 3 72057594037927936 0",
        "A00000000 123456789 3m 72057594037927936 0",
        "A00000000 123456789 3 m72057594037927936 0",
        "A00000000 123456789 3 72057594m037927936 0",
        "A00000000 123456789 3 72057594037927936mm 0",
        "A00000000 123456789 3 72057594037927936 m 0",
        "A00000000 123456789 3 72057594037927936 m0",
        "A00000000 123456789 3 72057594037927936 0m",
        "A00000000 123456789 2 72057594037927936m 0",
        "A00000000 -1 3 72057594037927936m 0",
        "A00000000 0123456789 3 72057594037927936m 0",
        "A00000000 4294967296 3 72057594037927936m 0"}) {
        INFO(fields);
        CHECK_FALSE(goldsrc::parse_challenge_response(make_response(fields)));
    }
}

TEST_CASE("E31 m suffix does not legalize missing zero noncanonical or overflowing identity",
          "[e31][goldsrc][challenge]")
{
    for (const auto identity : {"", "0", "00", "01", "-1", "+1",
        "18446744073709551616", "184467440737095516150"}) {
        INFO(identity);
        const auto result = goldsrc::parse_challenge_response(make_response(
            std::string{"A00000000 123456789 3 "} + identity + "m 0"));
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == goldsrc::ChallengeProtocolErrorCode::unsupported_variant);
        CHECK(result.error->field == goldsrc::ChallengeProtocolField::server_identity);
        CHECK(result.error->byte_offset == 26U);
    }
}

TEST_CASE("E31 m suffix requires the existing explicit security and separator grammar",
          "[e31][goldsrc][challenge]")
{
    for (const auto tail : {"", " ", "  ", " 2", " -1", " +0", " 00", "0",
        "\t0", "\r0", "\n0", " 0 ", " 0m", " 0 0"}) {
        INFO("tail length=" << std::string_view{tail}.size());
        CHECK_FALSE(goldsrc::parse_challenge_response(make_response(
            std::string{"A00000000 123456789 3 72057594037927936m"} + tail)));
    }
    auto embedded_nul = make_response("A00000000 123456789 3 72057594037927936m 0");
    embedded_nul[44U] = std::byte{0};
    CHECK_FALSE(goldsrc::parse_challenge_response(embedded_nul));
}

TEST_CASE("E31 suffixed challenge failures retain original datagram offsets",
          "[e31][goldsrc][challenge]")
{
    const auto valid = make_response("A00000000 123456789 3 72057594037927936m 0");
    const auto expect_failure = [](const std::vector<std::byte>& bytes,
        goldsrc::ChallengeProtocolErrorCode code, goldsrc::ChallengeProtocolField field,
        std::size_t offset) {
        const auto result = goldsrc::parse_challenge_response(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == code);
        CHECK(result.error->field == field);
        CHECK(result.error->byte_offset == offset);
    };
    auto wrong_marker = valid;
    wrong_marker[43U] = std::byte{'x'};
    expect_failure(wrong_marker, goldsrc::ChallengeProtocolErrorCode::unsupported_variant,
        goldsrc::ChallengeProtocolField::server_identity, 43U);
    auto missing_space = valid;
    missing_space.erase(missing_space.begin() + 44);
    expect_failure(missing_space, goldsrc::ChallengeProtocolErrorCode::unsupported_variant,
        goldsrc::ChallengeProtocolField::server_identity, 44U);
    auto repeated_marker = valid;
    repeated_marker.insert(repeated_marker.begin() + 44, std::byte{'m'});
    expect_failure(repeated_marker, goldsrc::ChallengeProtocolErrorCode::unsupported_variant,
        goldsrc::ChallengeProtocolField::server_identity, 44U);
    auto missing_security = valid;
    missing_security.erase(missing_security.begin() + 45);
    expect_failure(missing_security, goldsrc::ChallengeProtocolErrorCode::unsupported_variant,
        goldsrc::ChallengeProtocolField::security_flag, 45U);
    auto invalid_security = valid;
    invalid_security[45U] = std::byte{'2'};
    expect_failure(invalid_security, goldsrc::ChallengeProtocolErrorCode::unsupported_variant,
        goldsrc::ChallengeProtocolField::security_flag, 45U);
    for (const std::size_t index : {46U, 47U}) {
        auto invalid_terminator = valid;
        invalid_terminator[index] = std::byte{'x'};
        expect_failure(invalid_terminator, goldsrc::ChallengeProtocolErrorCode::invalid_terminator,
            goldsrc::ChallengeProtocolField::terminator, 46U);
    }
    auto trailing = valid;
    trailing.push_back(std::byte{0});
    expect_failure(trailing, goldsrc::ChallengeProtocolErrorCode::unexpected_trailing_data,
        goldsrc::ChallengeProtocolField::trailing_data, 48U);
}

TEST_CASE("E31 every suffixed response truncation and extra terminator fails closed",
          "[e31][goldsrc][challenge]")
{
    const auto valid = make_response("A00000000 123456789 3 72057594037927936m 0");
    REQUIRE(goldsrc::parse_challenge_response(valid));
    for (std::size_t size = 0; size < valid.size(); ++size) {
        INFO("suffixed truncated size=" << size);
        CHECK_FALSE(goldsrc::parse_challenge_response(std::span<const std::byte>{valid}.first(size)));
    }
    auto duplicate = valid;
    duplicate.push_back(std::byte{'\n'});
    duplicate.push_back(std::byte{0});
    CHECK_FALSE(goldsrc::parse_challenge_response(duplicate));
    auto reversed = valid;
    std::swap(reversed[46U], reversed[47U]);
    CHECK_FALSE(goldsrc::parse_challenge_response(reversed));
    auto oversized = valid;
    oversized.resize(goldsrc::kMaximumConnectionlessChallengeDatagramSize + 1U, std::byte{'x'});
    const auto rejected = goldsrc::parse_challenge_response(oversized);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code == goldsrc::ChallengeProtocolErrorCode::payload_too_large);
}

} // namespace
