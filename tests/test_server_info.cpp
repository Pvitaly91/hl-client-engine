#include <hlclient/goldsrc/server_info.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

// Independent sanitized literal with the exact stock-confirmed field order.
// It deliberately uses synthetic metadata and synthetic opaque bytes.
constexpr std::array kExactServerInfoBody{
    std::byte{0x30U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x78U}, std::byte{0x56U}, std::byte{0x34U}, std::byte{0x12U},
    std::byte{0xefU}, std::byte{0xbeU}, std::byte{0xadU}, std::byte{0xdeU},
    std::byte{0x00U}, std::byte{0x11U}, std::byte{0x22U}, std::byte{0x33U},
    std::byte{0x44U}, std::byte{0x55U}, std::byte{0x66U}, std::byte{0x77U},
    std::byte{0x88U}, std::byte{0x99U}, std::byte{0xaaU}, std::byte{0xbbU},
    std::byte{0xccU}, std::byte{0xddU}, std::byte{0xeeU}, std::byte{0xffU},
    std::byte{0x08U}, std::byte{0x00U}, std::byte{0x01U},
    std::byte{'s'}, std::byte{'a'}, std::byte{'m'}, std::byte{'p'},
    std::byte{'l'}, std::byte{'e'}, std::byte{0x00U},
    std::byte{'L'}, std::byte{'o'}, std::byte{'c'}, std::byte{'a'},
    std::byte{'l'}, std::byte{' '}, std::byte{'T'}, std::byte{'e'},
    std::byte{'s'}, std::byte{'t'}, std::byte{0x00U},
    std::byte{'m'}, std::byte{'a'}, std::byte{'p'}, std::byte{'s'},
    std::byte{'/'}, std::byte{'t'}, std::byte{'e'}, std::byte{'s'},
    std::byte{'t'}, std::byte{'_'}, std::byte{'a'}, std::byte{'l'},
    std::byte{'p'}, std::byte{'h'}, std::byte{'a'}, std::byte{'.'},
    std::byte{'b'}, std::byte{'s'}, std::byte{'p'}, std::byte{0x00U},
    std::byte{'a'}, std::byte{'l'}, std::byte{'p'}, std::byte{'h'},
    std::byte{'a'}, std::byte{' '}, std::byte{'b'}, std::byte{'e'},
    std::byte{'t'}, std::byte{'a'}, std::byte{0x00U},
    std::byte{0x00U},
};

static_assert(kExactServerInfoBody.size() == 81U);
inline constexpr std::size_t kMaximumClientsOffset = 28U;
inline constexpr std::size_t kOpaqueSlotCandidateOffset = 29U;
inline constexpr std::size_t kProfileFlagOffset = 30U;
inline constexpr std::size_t kGameDirectoryOffset = 31U;
inline constexpr std::size_t kServerLabelOffset = 38U;
inline constexpr std::size_t kMapPathOffset = 49U;
inline constexpr std::size_t kMapPathLength = 19U;
inline constexpr std::size_t kReservedOffset = 80U;

[[nodiscard]] std::vector<std::byte> exact_body()
{
    return {kExactServerInfoBody.begin(), kExactServerInfoBody.end()};
}

void overwrite_ascii(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::string_view value)
{
    REQUIRE(offset + value.size() <= bytes.size());
    const auto source = std::as_bytes(std::span{value.data(), value.size()});
    std::ranges::copy(source, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

void check_error(
    const goldsrc::ServerInfoParseResult& result,
    const goldsrc::ServerInfoErrorCode expected,
    const std::size_t input_size)
{
    INFO("expected " << goldsrc::to_string(expected));
    REQUIRE_FALSE(result);
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == expected);
    CHECK(result.error->byte_offset <= input_size);
    CHECK_FALSE(result.error->context.empty());
    CHECK(result.error->context.size() <= goldsrc::kServerInfoDiagnosticTextLimit);
    CHECK_FALSE(result.state.has_value());
    CHECK(result.bytes_consumed == 0U);
}

void check_public_state(
    const goldsrc::ServerInfoState& state,
    const std::uint8_t maximum_clients,
    const std::string_view map_path)
{
    CHECK(state.protocol_version() == goldsrc::ProtocolVersion::goldsrc_48);
    CHECK(state.maximum_clients().value() == maximum_clients);
    CHECK(state.multi_client_mode() == (maximum_clients > 1U));
    CHECK(state.game_directory() == "sample");
    CHECK(state.server_label() == "Local Test");
    CHECK(state.map_file_path() == map_path);
    CHECK(
        state.compatibility_profile() ==
        goldsrc::ServerInfoCompatibilityProfile::
            valve_half_life_protocol_48_build_10210);
    CHECK(
        state.evidence_profile() ==
        goldsrc::ServerInfoEvidenceProfile::differential_stock_capture);
}

TEST_CASE("Server-info parser decodes the exact sanitized literal transactionally",
          "[goldsrc][signon][server-info][fixture]")
{
    const goldsrc::ServerInfoParser parser;
    const auto result = parser.parse(kExactServerInfoBody);

    REQUIRE(result);
    REQUIRE(result.state.has_value());
    CHECK_FALSE(result.error.has_value());
    CHECK(result.bytes_consumed == kExactServerInfoBody.size());
    check_public_state(*result.state, 8U, "maps/test_alpha.bsp");

    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<goldsrc::ServerInfoState>);
    STATIC_REQUIRE_FALSE(std::is_aggregate_v<goldsrc::ServerInfoState>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<goldsrc::ServerInfoState>);
    STATIC_REQUIRE_FALSE(std::is_move_assignable_v<goldsrc::ServerInfoState>);
    STATIC_REQUIRE(std::is_same_v<
                   decltype(std::declval<const goldsrc::ServerInfoState&>().map_file_path()),
                   const std::string&>);
}

TEST_CASE("Every truncated server-info body prefix fails without cursor publication",
          "[goldsrc][signon][server-info][truncation]")
{
    const goldsrc::ServerInfoParser parser;
    for (std::size_t size = 0U; size < kExactServerInfoBody.size(); ++size) {
        INFO("prefix size " << size);
        const auto result = parser.parse(std::span{kExactServerInfoBody}.first(size));
        REQUIRE_FALSE(result);
        REQUIRE(result.error.has_value());
        CHECK(result.error->byte_offset <= size);
        CHECK_FALSE(result.state.has_value());
        CHECK(result.bytes_consumed == 0U);
    }
}

TEST_CASE("Server-info parser accepts only the Protocol 48 compatibility profile",
          "[goldsrc][signon][server-info][protocol]")
{
    const goldsrc::ServerInfoParser parser;
    for (const std::array wrong_protocol{
             std::uint32_t{0U},
             std::uint32_t{47U},
             std::uint32_t{49U},
             (std::numeric_limits<std::uint32_t>::max)(),
         };
         const auto protocol : wrong_protocol) {
        auto body = exact_body();
        body[0U] = static_cast<std::byte>(protocol & 0xffU);
        body[1U] = static_cast<std::byte>((protocol >> 8U) & 0xffU);
        body[2U] = static_cast<std::byte>((protocol >> 16U) & 0xffU);
        body[3U] = static_cast<std::byte>((protocol >> 24U) & 0xffU);
        check_error(
            parser.parse(body),
            goldsrc::ServerInfoErrorCode::unsupported_protocol,
            body.size());
    }

    auto wrong_endianness = exact_body();
    wrong_endianness[0U] = std::byte{0x00U};
    wrong_endianness[1U] = std::byte{0x00U};
    wrong_endianness[2U] = std::byte{0x00U};
    wrong_endianness[3U] = std::byte{0x30U};
    check_error(
        parser.parse(wrong_endianness),
        goldsrc::ServerInfoErrorCode::unsupported_protocol,
        wrong_endianness.size());
}

TEST_CASE("Server-info maximum-clients field is strong, nonzero, and capped at 32",
          "[goldsrc][signon][server-info][max-clients]")
{
    const goldsrc::ServerInfoParser parser;
    for (const auto accepted : std::array<std::uint8_t, 4U>{1U, 2U, 8U, 32U}) {
        auto body = exact_body();
        body[kMaximumClientsOffset] = static_cast<std::byte>(accepted);
        body[kProfileFlagOffset] = accepted == 1U ? std::byte{0U} : std::byte{1U};
        const auto result = parser.parse(body);
        REQUIRE(result);
        CHECK(result.state->maximum_clients().value() == accepted);
    }

    for (const auto rejected : std::array<std::uint8_t, 2U>{0U, 33U}) {
        auto body = exact_body();
        body[kMaximumClientsOffset] = static_cast<std::byte>(rejected);
        check_error(
            parser.parse(body),
            goldsrc::ServerInfoErrorCode::invalid_maximum_clients,
            body.size());
    }
}

TEST_CASE("Server-info ordinal candidate remains opaque and unvalidated",
          "[goldsrc][signon][server-info][opaque][ordinal]")
{
    const goldsrc::ServerInfoParser parser;
    auto body = exact_body();
    std::ranges::fill(body.begin() + 4, body.begin() + 8, std::byte{0U});
    const auto zero = parser.parse(body);
    REQUIRE(zero);
    check_public_state(*zero.state, 8U, "maps/test_alpha.bsp");

    body = exact_body();
    body[4U] = std::byte{2U};
    body[5U] = std::byte{0U};
    body[6U] = std::byte{0U};
    body[7U] = std::byte{0U};
    const auto second = parser.parse(body);
    REQUIRE(second);
    check_public_state(*second.state, 8U, "maps/test_alpha.bsp");
}

TEST_CASE("Server-info strings are bounded and require an in-bound NUL terminator",
          "[goldsrc][signon][server-info][string][limits]")
{
    goldsrc::ServerInfoLimits limits;
    limits.maximum_string_length = kMapPathLength;
    const goldsrc::ServerInfoParser parser{limits};
    REQUIRE(parser.parse(kExactServerInfoBody));

    auto too_long = exact_body();
    too_long.insert(
        too_long.begin() + static_cast<std::ptrdiff_t>(kMapPathOffset + kMapPathLength),
        std::byte{'x'});
    check_error(
        parser.parse(too_long),
        goldsrc::ServerInfoErrorCode::string_field_too_long,
        too_long.size());

    auto unterminated = exact_body();
    unterminated.resize(kMapPathOffset + kMapPathLength);
    check_error(
        parser.parse(unterminated),
        goldsrc::ServerInfoErrorCode::unterminated_string_field,
        unterminated.size());
}

TEST_CASE("Server-info fixed binary field width and final reserved value are strict",
          "[goldsrc][signon][server-info][fixed][security]")
{
    const goldsrc::ServerInfoParser parser;

    auto shortened_binary = exact_body();
    shortened_binary.erase(shortened_binary.begin() + 27);
    const auto shortened = parser.parse(shortened_binary);
    REQUIRE_FALSE(shortened);
    REQUIRE(shortened.error.has_value());
    CHECK_FALSE(shortened.state.has_value());
    CHECK(shortened.bytes_consumed == 0U);

    auto invalid_flag = exact_body();
    invalid_flag[kProfileFlagOffset] = std::byte{2U};
    check_error(
        parser.parse(invalid_flag),
        goldsrc::ServerInfoErrorCode::invalid_profile_flag,
        invalid_flag.size());

    auto inconsistent_single = exact_body();
    inconsistent_single[kMaximumClientsOffset] = std::byte{1U};
    inconsistent_single[kProfileFlagOffset] = std::byte{1U};
    check_error(
        parser.parse(inconsistent_single),
        goldsrc::ServerInfoErrorCode::inconsistent_multi_client_mode,
        inconsistent_single.size());

    auto inconsistent_multiple = exact_body();
    inconsistent_multiple[kMaximumClientsOffset] = std::byte{8U};
    inconsistent_multiple[kProfileFlagOffset] = std::byte{0U};
    check_error(
        parser.parse(inconsistent_multiple),
        goldsrc::ServerInfoErrorCode::inconsistent_multi_client_mode,
        inconsistent_multiple.size());

    auto invalid_reserved = exact_body();
    invalid_reserved[kReservedOffset] = std::byte{1U};
    check_error(
        parser.parse(invalid_reserved),
        goldsrc::ServerInfoErrorCode::invalid_reserved_value,
        invalid_reserved.size());
}

TEST_CASE("Only differential-confirmed server-info fields enter the public state",
          "[goldsrc][signon][server-info][differential][evidence]")
{
    const goldsrc::ServerInfoParser parser;
    const auto baseline = parser.parse(kExactServerInfoBody);
    REQUIRE(baseline);

    auto opaque_variants = exact_body();
    opaque_variants[8U] = std::byte{0x5aU};
    opaque_variants[12U] = std::byte{0xffU};
    opaque_variants[kOpaqueSlotCandidateOffset] = std::byte{0xffU};
    const auto opaque = parser.parse(opaque_variants);
    REQUIRE(opaque);
    check_public_state(*opaque.state, 8U, "maps/test_alpha.bsp");

    auto different_text_metadata = exact_body();
    overwrite_ascii(different_text_metadata, kGameDirectoryOffset, "module");
    overwrite_ascii(different_text_metadata, kServerLabelOffset, "Other Test");
    const auto text_metadata = parser.parse(different_text_metadata);
    REQUIRE(text_metadata);
    CHECK(text_metadata.state->game_directory() == "module");
    CHECK(text_metadata.state->server_label() == "Other Test");
    CHECK(text_metadata.state->map_file_path() == baseline.state->map_file_path());

    auto different_map = exact_body();
    overwrite_ascii(different_map, kMapPathOffset, "maps/test_bravo.bsp");
    const auto map = parser.parse(different_map);
    REQUIRE(map);
    CHECK(map.state->map_file_path() == "maps/test_bravo.bsp");
    CHECK(map.state->maximum_clients() == baseline.state->maximum_clients());

    auto different_maximum = exact_body();
    different_maximum[kMaximumClientsOffset] = std::byte{1U};
    different_maximum[kProfileFlagOffset] = std::byte{0U};
    const auto maximum = parser.parse(different_maximum);
    REQUIRE(maximum);
    CHECK(maximum.state->maximum_clients().value() == 1U);
    CHECK(maximum.state->map_file_path() == baseline.state->map_file_path());
}

TEST_CASE("Server-info parser leaves following service bytes exactly unconsumed",
          "[goldsrc][signon][server-info][cursor]")
{
    auto with_following_message = exact_body();
    with_following_message.push_back(std::byte{54U});
    with_following_message.push_back(std::byte{0U});
    with_following_message.push_back(std::byte{0U});
    with_following_message.push_back(std::byte{14U});

    const goldsrc::ServerInfoParser parser;
    const auto result = parser.parse(with_following_message);
    REQUIRE(result);
    CHECK(result.bytes_consumed == kExactServerInfoBody.size());
    CHECK(with_following_message[result.bytes_consumed] == std::byte{54U});
}

TEST_CASE("Server-info parser configuration is positive and hard capped",
          "[goldsrc][signon][server-info][configuration]")
{
    CHECK(goldsrc::valid_server_info_limits({}));
    CHECK(goldsrc::valid_server_info_limits({goldsrc::kMaximumServerInfoStringLength}));
    CHECK_FALSE(goldsrc::valid_server_info_limits({0U}));
    CHECK_FALSE(goldsrc::valid_server_info_limits({
        goldsrc::kMaximumServerInfoStringLength + 1U,
    }));

    const goldsrc::ServerInfoParser invalid{{0U}};
    check_error(
        invalid.parse(kExactServerInfoBody),
        goldsrc::ServerInfoErrorCode::invalid_configuration,
        kExactServerInfoBody.size());
}

TEST_CASE("Server-info state remains owning after source storage expires",
          "[goldsrc][signon][server-info][ownership]")
{
    std::optional<goldsrc::ServerInfoParseResult> result;
    {
        auto body = exact_body();
        const goldsrc::ServerInfoParser parser;
        result.emplace(parser.parse(body));
        std::ranges::fill(body, std::byte{0U});
    }

    REQUIRE(*result);
    REQUIRE(result->state.has_value());
    check_public_state(*result->state, 8U, "maps/test_alpha.bsp");
}

} // namespace
