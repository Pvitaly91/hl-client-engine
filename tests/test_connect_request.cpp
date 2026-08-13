#include <hlclient/goldsrc/connect_request.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

inline constexpr std::string_view kProtectedAuthentication{"TEST_AUTH_MATERIAL"};
inline constexpr std::string_view kBinaryAuthentication{"TEST_AUTH_MATERIAL"};
inline constexpr std::string_view kFixtureProtocolInfo{
    "\\prot\\3\\unique\\-1\\raw\\steam\\cdkey\\TEST_AUTH_MATERIAL"};
inline constexpr std::string_view kFixtureUserInfo{
    "\\bottomcolor\\6\\cl_autowepswitch\\1\\cl_dlmax\\1024\\cl_lc\\1\\cl_lw\\1"
    "\\cl_updaterate\\102\\hud_classautokill\\1\\model\\fixture_model"
    "\\name\\FixturePlayer\\topcolor\\30\\esevcmmx\\0\\_gm\\3154"
    "\\_vgui_menus\\0\\rate\\25000"};

static_assert(!std::same_as<goldsrc::ProtocolInfo, goldsrc::UserInfo>);
static_assert(!std::is_constructible_v<goldsrc::ProtocolInfo, goldsrc::InfoString>);
static_assert(!std::is_constructible_v<goldsrc::UserInfo, goldsrc::InfoString>);
static_assert(!std::is_copy_constructible_v<goldsrc::AuthenticationMaterial>);
static_assert(!std::is_copy_assignable_v<goldsrc::AuthenticationMaterial>);
static_assert(std::is_nothrow_move_constructible_v<goldsrc::AuthenticationMaterial>);
static_assert(std::is_nothrow_move_assignable_v<goldsrc::AuthenticationMaterial>);
static_assert(!std::is_convertible_v<goldsrc::AuthenticationMaterial, std::string>);
static_assert(!std::is_convertible_v<goldsrc::AuthenticationMaterial, std::string_view>);
static_assert(!std::is_copy_constructible_v<goldsrc::ConnectRequest>);
static_assert(std::is_nothrow_move_constructible_v<goldsrc::ConnectRequest>);

[[nodiscard]] std::vector<std::byte> text_bytes(const std::string_view text)
{
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    return std::vector<std::byte>{bytes.begin(), bytes.end()};
}

[[nodiscard]] std::string_view byte_text(const std::span<const std::byte> bytes) noexcept
{
    return std::string_view{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size(),
    };
}

[[nodiscard]] goldsrc::ConnectCompatibilityProfile fixture_profile() noexcept
{
    return goldsrc::ConnectCompatibilityProfile{
        goldsrc::kMaximumConnectDatagramSize,
        kProtectedAuthentication.size(),
        kBinaryAuthentication.size(),
        false,
    };
}

[[nodiscard]] goldsrc::ClientConnectionSettings fixture_settings()
{
    goldsrc::ClientConnectionSettings settings;
    settings.model = "fixture_model";
    settings.display_name = "FixturePlayer";
    return settings;
}

[[nodiscard]] goldsrc::AuthenticationMaterial make_authentication(
    const std::string_view protected_value = kProtectedAuthentication,
    const std::string_view suffix = kBinaryAuthentication)
{
    const auto protected_bytes = text_bytes(protected_value);
    const auto suffix_bytes = text_bytes(suffix);
    auto result = goldsrc::AuthenticationMaterial::create(protected_bytes, suffix_bytes);
    REQUIRE(result);
    REQUIRE(result.value);
    return std::move(*result.value);
}

[[nodiscard]] goldsrc::ConnectRequest make_request(
    const goldsrc::ChallengeToken challenge,
    const goldsrc::ConnectCompatibilityProfile& profile = fixture_profile(),
    const goldsrc::ClientConnectionSettings& settings = fixture_settings())
{
    auto prepared = goldsrc::prepare_connect_request(
        settings, make_authentication(), profile);
    REQUIRE(prepared);
    REQUIRE(prepared.value);
    return std::move(*prepared.value).make_request(challenge);
}

[[nodiscard]] std::vector<std::byte> build_fixture(
    const goldsrc::ChallengeToken challenge,
    const goldsrc::ConnectCompatibilityProfile& profile = fixture_profile(),
    const goldsrc::ClientConnectionSettings& settings = fixture_settings())
{
    auto request = make_request(challenge, profile, settings);
    auto built = goldsrc::ConnectRequestBuilder::build(request, profile);
    REQUIRE(built);
    REQUIRE(built.datagram);
    return std::move(*built.datagram);
}

[[nodiscard]] std::vector<std::byte> make_datagram(
    const std::string_view body,
    const std::span<const std::byte> suffix = {})
{
    std::vector<std::byte> result{
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
    const auto body_bytes = std::as_bytes(std::span{body.data(), body.size()});
    result.insert(result.end(), body_bytes.begin(), body_bytes.end());
    result.insert(result.end(), suffix.begin(), suffix.end());
    return result;
}

[[nodiscard]] std::vector<std::byte> make_connect_datagram(
    const std::string_view protocol,
    const std::string_view challenge,
    const std::string_view protocol_info,
    const std::string_view user_info,
    const std::span<const std::byte> suffix)
{
    std::string body;
    body.reserve(
        16U + protocol.size() + challenge.size() + protocol_info.size() + user_info.size());
    body += "connect ";
    body += protocol;
    body.push_back(' ');
    body += challenge;
    body += " \"";
    body += protocol_info;
    body += "\" \"";
    body += user_info;
    body.push_back('"');
    return make_datagram(body, suffix);
}

void check_parse_error(
    const std::span<const std::byte> datagram,
    const goldsrc::ConnectRequestErrorCode expected,
    const goldsrc::ConnectCompatibilityProfile& profile = fixture_profile())
{
    const auto result = goldsrc::parse_connect_request(datagram, profile);
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
}

TEST_CASE("Connect builder matches an independent sanitized captured-layout fixture",
          "[goldsrc][connect-request]")
{
    // This literal intentionally does not use the codec, its constants, or the parser.
    // Its two authentication regions are synthetic and contain no captured material.
    static constexpr char expected_characters[] =
        "\xff\xff\xff\xff"
        "connect 48 -2147483648 \""
        "\\prot\\3\\unique\\-1\\raw\\steam\\cdkey\\TEST_AUTH_MATERIAL"
        "\" \""
        "\\bottomcolor\\6\\cl_autowepswitch\\1\\cl_dlmax\\1024\\cl_lc\\1\\cl_lw\\1"
        "\\cl_updaterate\\102\\hud_classautokill\\1\\model\\fixture_model"
        "\\name\\FixturePlayer\\topcolor\\30\\esevcmmx\\0\\_gm\\3154"
        "\\_vgui_menus\\0\\rate\\25000"
        "\""
        "TEST_AUTH_MATERIAL";
    const auto expected = text_bytes(
        std::string_view{expected_characters, sizeof(expected_characters) - 1U});

    const auto actual = build_fixture(0x8000'0000U);
    const bool exact_byte_match = actual == expected;

    CHECK(exact_byte_match);
    REQUIRE_FALSE(actual.empty());
    CHECK(actual.back() == std::byte{'L'});
    CHECK(actual.size() == expected.size());
}

TEST_CASE("Connect codec preserves Protocol 48 and all ChallengeToken bit patterns",
          "[goldsrc][connect-request]")
{
    struct ChallengeExample {
        goldsrc::ChallengeToken token;
        std::string_view signed_text;
    };
    constexpr std::array examples{
        ChallengeExample{0U, "0"},
        ChallengeExample{0x7fff'ffffU, "2147483647"},
        ChallengeExample{0x8000'0000U, "-2147483648"},
        ChallengeExample{std::numeric_limits<std::uint32_t>::max(), "-1"},
    };

    for (const auto& example : examples) {
        const auto datagram = build_fixture(example.token);
        const std::string expected_prefix =
            "connect 48 " + std::string{example.signed_text} + " \"";
        CHECK(byte_text(datagram).substr(4U).starts_with(expected_prefix));

        const auto parsed = goldsrc::parse_connect_request(datagram, fixture_profile());
        REQUIRE(parsed);
        REQUIRE(parsed.request);
        CHECK(parsed.request->protocol() == goldsrc::ProtocolVersion::goldsrc_48);
        CHECK(parsed.request->challenge() == example.token);
    }
}

TEST_CASE("Connect builder rejects an unsupported typed protocol",
          "[goldsrc][connect-request]")
{
    const auto valid = build_fixture(7U);
    auto parsed = goldsrc::parse_connect_request(valid, fixture_profile());
    REQUIRE(parsed);
    REQUIRE(parsed.request);

    goldsrc::ConnectRequest invalid_request{
        static_cast<goldsrc::ProtocolVersion>(47U),
        7U,
        parsed.request->protocol_info(),
        parsed.request->user_info(),
        make_authentication(),
    };
    const auto result = goldsrc::ConnectRequestBuilder::build(
        invalid_request, fixture_profile());

    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == goldsrc::ConnectRequestErrorCode::invalid_protocol);
}

TEST_CASE("Connect parser rejects invalid protocols and noncanonical challenges",
          "[goldsrc][connect-request]")
{
    const auto suffix = text_bytes(kBinaryAuthentication);

    check_parse_error(
        make_connect_datagram("47", "1", kFixtureProtocolInfo, kFixtureUserInfo, suffix),
        goldsrc::ConnectRequestErrorCode::invalid_protocol);
    check_parse_error(
        make_connect_datagram("048", "1", kFixtureProtocolInfo, kFixtureUserInfo, suffix),
        goldsrc::ConnectRequestErrorCode::invalid_protocol);

    for (const auto challenge : {"+1", "01", "-0", "2147483648", "-2147483649",
                                 "4294967295", "1x"}) {
        check_parse_error(
            make_connect_datagram(
                "48", challenge, kFixtureProtocolInfo, kFixtureUserInfo, suffix),
            goldsrc::ConnectRequestErrorCode::invalid_challenge);
    }
}

TEST_CASE("Connect parser rejects missing protocol info and user info",
          "[goldsrc][connect-request]")
{
    check_parse_error(
        make_datagram("connect 48 1 "),
        goldsrc::ConnectRequestErrorCode::missing_protocol_info_argument);

    const std::string protocol_only =
        "connect 48 1 \"" + std::string{kFixtureProtocolInfo} + "\"";
    check_parse_error(
        make_datagram(protocol_only),
        goldsrc::ConnectRequestErrorCode::missing_user_info_argument);
}

TEST_CASE("Connect parser rejects missing scalar arguments and the wrong command",
          "[goldsrc][connect-request]")
{
    check_parse_error(
        make_datagram("connect "), goldsrc::ConnectRequestErrorCode::missing_protocol);
    check_parse_error(
        make_datagram("connect 48 "), goldsrc::ConnectRequestErrorCode::missing_challenge);

    auto wrong_command = build_fixture(1U);
    wrong_command[4U] = std::byte{'C'};
    check_parse_error(wrong_command, goldsrc::ConnectRequestErrorCode::unexpected_command);
}

TEST_CASE("Connect parser rejects malformed protocol and user quoting",
          "[goldsrc][connect-request]")
{
    const auto suffix = text_bytes(kBinaryAuthentication);
    const std::string unterminated_protocol =
        "connect 48 1 \"" + std::string{kFixtureProtocolInfo};
    check_parse_error(
        make_datagram(unterminated_protocol, suffix),
        goldsrc::ConnectRequestErrorCode::invalid_quote);

    const std::string unterminated_user =
        "connect 48 1 \"" + std::string{kFixtureProtocolInfo} + "\" \"" +
        std::string{kFixtureUserInfo};
    check_parse_error(
        make_datagram(unterminated_user, suffix),
        goldsrc::ConnectRequestErrorCode::invalid_quote);

    const std::string missing_protocol_quote =
        "connect 48 1 " + std::string{kFixtureProtocolInfo};
    check_parse_error(
        make_datagram(missing_protocol_quote, suffix),
        goldsrc::ConnectRequestErrorCode::missing_protocol_info_argument);
}

TEST_CASE("Connect parser rejects every wrong connectionless header byte",
          "[goldsrc][connect-request]")
{
    const auto valid = build_fixture(1U);
    for (std::size_t index = 0U; index < 4U; ++index) {
        auto invalid = valid;
        invalid[index] = std::byte{0xfe};
        INFO("header byte " << index);
        check_parse_error(
            invalid, goldsrc::ConnectRequestErrorCode::invalid_connectionless_header);
    }
}

TEST_CASE("Connect request has no text terminator and enforces its opaque suffix boundary",
          "[goldsrc][connect-request]")
{
    const auto valid = build_fixture(1U);
    REQUIRE(valid.size() > kBinaryAuthentication.size());
    CHECK(valid.back() != std::byte{'\n'});
    CHECK(valid.back() != std::byte{'\0'});

    auto missing_suffix_byte = valid;
    missing_suffix_byte.pop_back();
    check_parse_error(
        missing_suffix_byte, goldsrc::ConnectRequestErrorCode::invalid_terminator);

    auto duplicate_line_terminator = valid;
    duplicate_line_terminator.push_back(std::byte{'\n'});
    check_parse_error(
        duplicate_line_terminator,
        goldsrc::ConnectRequestErrorCode::unexpected_trailing_data);

    auto swapped_text_terminator = valid;
    swapped_text_terminator.push_back(std::byte{0});
    swapped_text_terminator.push_back(std::byte{'\n'});
    check_parse_error(
        swapped_text_terminator,
        goldsrc::ConnectRequestErrorCode::unexpected_trailing_data);
}

TEST_CASE("Connect parser rejects trailing bytes after the exact request",
          "[goldsrc][connect-request]")
{
    auto trailing = build_fixture(1U);
    trailing.push_back(std::byte{'x'});
    check_parse_error(
        trailing, goldsrc::ConnectRequestErrorCode::unexpected_trailing_data);
}

TEST_CASE("Protocol-info codec enforces its exact serialized boundary",
          "[goldsrc][connect-request][boundary]")
{
    std::vector<goldsrc::InfoStringEntry> entries{
        {"a", std::string(127U, 'a')},
        {"b", std::string(122U, 'b')},
    };
    const auto exact = goldsrc::build_info_string(
        entries, goldsrc::connect_protocol_info_limits());
    REQUIRE(exact);
    REQUIRE(exact.value);
    CHECK(exact.value->serialized_size() == goldsrc::kMaximumProtocolInfoSerializedSize);

    entries[1U].value.push_back('b');
    const auto oversized = goldsrc::build_info_string(
        entries, goldsrc::connect_protocol_info_limits());
    REQUIRE_FALSE(oversized);
    REQUIRE(oversized.error);
    CHECK(oversized.error->code == goldsrc::InfoStringErrorCode::serialized_size_exceeded);
}

TEST_CASE("Stock user-info enforces its exact serialized boundary",
          "[goldsrc][connect-request][boundary]")
{
    goldsrc::ClientConnectionSettings settings;
    settings.bottom_color = "x";
    const auto baseline = goldsrc::build_stock_user_info(settings);
    REQUIRE(baseline);
    REQUIRE(baseline.value);
    REQUIRE(baseline.value->value().serialized_size() <
            goldsrc::kMaximumUserInfoSerializedSize);

    const auto growth = goldsrc::kMaximumUserInfoSerializedSize -
                        baseline.value->value().serialized_size();
    REQUIRE(settings.bottom_color.size() + growth <=
            goldsrc::connect_user_info_limits().maximum_value_length);
    settings.bottom_color.append(growth, 'x');

    const auto exact = goldsrc::build_stock_user_info(settings);
    REQUIRE(exact);
    REQUIRE(exact.value);
    CHECK(exact.value->value().serialized_size() == goldsrc::kMaximumUserInfoSerializedSize);

    settings.bottom_color.push_back('x');
    const auto oversized = goldsrc::build_stock_user_info(settings);
    REQUIRE_FALSE(oversized);
    REQUIRE(oversized.error);
    CHECK(oversized.error->code == goldsrc::ConnectRequestErrorCode::invalid_user_info);
}

TEST_CASE("Authentication material enforces exact hard bounds and rejects plus one",
          "[goldsrc][connect-request][boundary]")
{
    const std::vector<std::byte> one_suffix{std::byte{'x'}};
    const std::vector<std::byte> maximum_protected(
        goldsrc::kMaximumConnectProtectedAuthenticationSize, std::byte{'A'});
    const auto protected_at_limit = goldsrc::AuthenticationMaterial::create(
        maximum_protected, one_suffix);
    CHECK(protected_at_limit);

    auto protected_over_limit = maximum_protected;
    protected_over_limit.push_back(std::byte{'A'});
    const auto protected_oversized = goldsrc::AuthenticationMaterial::create(
        protected_over_limit, one_suffix);
    REQUIRE_FALSE(protected_oversized);
    REQUIRE(protected_oversized.error);
    CHECK(protected_oversized.error->code ==
          goldsrc::ConnectRequestErrorCode::authentication_too_large);

    const std::vector<std::byte> one_protected{std::byte{'A'}};
    const std::vector<std::byte> maximum_suffix(
        goldsrc::kMaximumConnectAuthenticationSuffixSize, std::byte{'Z'});
    const auto suffix_at_limit = goldsrc::AuthenticationMaterial::create(
        one_protected, maximum_suffix);
    CHECK(suffix_at_limit);

    auto suffix_over_limit = maximum_suffix;
    suffix_over_limit.push_back(std::byte{'Z'});
    const auto suffix_oversized = goldsrc::AuthenticationMaterial::create(
        one_protected, suffix_over_limit);
    REQUIRE_FALSE(suffix_oversized);
    REQUIRE(suffix_oversized.error);
    CHECK(suffix_oversized.error->code ==
          goldsrc::ConnectRequestErrorCode::authentication_too_large);
}

TEST_CASE("Authentication material rejects missing and structural protected values",
          "[goldsrc][connect-request]")
{
    const std::vector<std::byte> empty;
    const auto suffix = text_bytes(kBinaryAuthentication);
    const auto protected_value = text_bytes(kProtectedAuthentication);

    const auto missing_protected = goldsrc::AuthenticationMaterial::create(empty, suffix);
    REQUIRE_FALSE(missing_protected);
    CHECK(missing_protected.error->code ==
          goldsrc::ConnectRequestErrorCode::missing_authentication);

    const auto missing_suffix = goldsrc::AuthenticationMaterial::create(protected_value, empty);
    REQUIRE_FALSE(missing_suffix);
    CHECK(missing_suffix.error->code ==
          goldsrc::ConnectRequestErrorCode::missing_authentication);

    const auto invalid = text_bytes("TEST;AUTH");
    const auto structural = goldsrc::AuthenticationMaterial::create(invalid, suffix);
    REQUIRE_FALSE(structural);
    CHECK(structural.error->code ==
          goldsrc::ConnectRequestErrorCode::invalid_authentication);
}

TEST_CASE("Connect packet accepts its exact total bound and rejects limit plus one",
          "[goldsrc][connect-request][boundary]")
{
    constexpr goldsrc::ChallengeToken challenge = 0x8000'0000U;
    auto request = make_request(challenge);
    const auto baseline = goldsrc::ConnectRequestBuilder::build(
        request, fixture_profile());
    REQUIRE(baseline);
    REQUIRE(baseline.datagram);

    auto exact_profile = fixture_profile();
    exact_profile.maximum_datagram_size = baseline.datagram->size();
    const auto exact = goldsrc::ConnectRequestBuilder::build(request, exact_profile);
    REQUIRE(exact);
    REQUIRE(exact.datagram);
    CHECK(exact.datagram->size() == exact_profile.maximum_datagram_size);

    auto one_byte_smaller_profile = exact_profile;
    --one_byte_smaller_profile.maximum_datagram_size;
    const auto limit_plus_one = goldsrc::ConnectRequestBuilder::build(
        request, one_byte_smaller_profile);
    REQUIRE_FALSE(limit_plus_one);
    REQUIRE(limit_plus_one.error);
    CHECK(limit_plus_one.error->code == goldsrc::ConnectRequestErrorCode::packet_too_large);
}

TEST_CASE("Connect parser enforces the hard packet bound before parsing",
          "[goldsrc][connect-request][boundary]")
{
    std::vector<std::byte> oversized(
        goldsrc::kMaximumConnectDatagramSize + 1U, std::byte{0xff});
    check_parse_error(
        oversized, goldsrc::ConnectRequestErrorCode::packet_too_large);
}

TEST_CASE("Connect fields retain the captured stable order",
          "[goldsrc][connect-request]")
{
    const auto protocol = goldsrc::build_stock_protocol_info();
    REQUIRE(protocol);
    REQUIRE(protocol.value);
    constexpr std::array protocol_keys{
        std::string_view{"prot"},
        std::string_view{"unique"},
        std::string_view{"raw"},
        std::string_view{"cdkey"},
    };
    REQUIRE(protocol.value->value().entries().size() == protocol_keys.size());
    for (std::size_t index = 0U; index < protocol_keys.size(); ++index) {
        CHECK(protocol.value->value().entries()[index].key == protocol_keys[index]);
    }

    const auto user = goldsrc::build_stock_user_info(fixture_settings());
    REQUIRE(user);
    REQUIRE(user.value);
    constexpr std::array user_keys{
        std::string_view{"bottomcolor"},
        std::string_view{"cl_autowepswitch"},
        std::string_view{"cl_dlmax"},
        std::string_view{"cl_lc"},
        std::string_view{"cl_lw"},
        std::string_view{"cl_updaterate"},
        std::string_view{"hud_classautokill"},
        std::string_view{"model"},
        std::string_view{"name"},
        std::string_view{"topcolor"},
        std::string_view{"esevcmmx"},
        std::string_view{"_gm"},
        std::string_view{"_vgui_menus"},
        std::string_view{"rate"},
    };
    REQUIRE(user.value->value().entries().size() == user_keys.size());
    for (std::size_t index = 0U; index < user_keys.size(); ++index) {
        CHECK(user.value->value().entries()[index].key == user_keys[index]);
    }
}

TEST_CASE("Authentication redaction exposes only a byte count",
          "[goldsrc][connect-request]")
{
    const auto redacted = goldsrc::format_authentication_redaction(37U);
    const bool protected_value_absent = redacted.find(kProtectedAuthentication) == std::string::npos;
    const bool binary_value_absent = redacted.find(kBinaryAuthentication) == std::string::npos;

    CHECK(redacted == "<redacted:37 bytes>");
    CHECK(protected_value_absent);
    CHECK(binary_value_absent);
}

TEST_CASE("Stock profile requires the captured 32-byte ASCII-hex protected region",
          "[goldsrc][connect-request][authentication]")
{
    const std::string synthetic_hex(32U, 'A');
    std::vector<std::byte> suffix(goldsrc::kObservedConnectAuthenticationSuffixSize);
    const auto marker = text_bytes(kBinaryAuthentication);
    for (std::size_t index = 0U; index < suffix.size(); ++index) {
        suffix[index] = marker[index % marker.size()];
    }

    auto accepted_authentication = goldsrc::AuthenticationMaterial::create(
        text_bytes(synthetic_hex), suffix);
    REQUIRE(accepted_authentication);
    auto accepted = goldsrc::prepare_connect_request(
        goldsrc::ClientConnectionSettings{},
        std::move(*accepted_authentication.value));
    REQUIRE(accepted);
    auto accepted_request = std::move(*accepted.value).make_request(17U);
    const auto built = goldsrc::ConnectRequestBuilder::build(accepted_request);
    REQUIRE(built);
    REQUIRE(built.datagram);
    CHECK(goldsrc::parse_connect_request(*built.datagram));

    constexpr std::string_view marker_protected =
        "TEST_AUTH_MATERIAL_TEST_AUTH_MAT";
    auto marker_authentication = goldsrc::AuthenticationMaterial::create(
        text_bytes(marker_protected), suffix);
    REQUIRE(marker_authentication);
    auto custom_profile = goldsrc::ConnectCompatibilityProfile{};
    custom_profile.protected_authentication_is_ascii_hex = false;
    auto marker_prepared = goldsrc::prepare_connect_request(
        goldsrc::ClientConnectionSettings{},
        std::move(*marker_authentication.value),
        custom_profile);
    REQUIRE(marker_prepared);
    auto marker_request = std::move(*marker_prepared.value).make_request(17U);

    const auto rejected_build = goldsrc::ConnectRequestBuilder::build(marker_request);
    REQUIRE_FALSE(rejected_build);
    REQUIRE(rejected_build.error);
    CHECK(rejected_build.error->code ==
          goldsrc::ConnectRequestErrorCode::invalid_authentication);

    const auto custom_datagram = goldsrc::ConnectRequestBuilder::build(
        marker_request, custom_profile);
    REQUIRE(custom_datagram);
    REQUIRE(custom_datagram.datagram);
    const auto rejected_parse = goldsrc::parse_connect_request(*custom_datagram.datagram);
    REQUIRE_FALSE(rejected_parse);
    REQUIRE(rejected_parse.error);
    CHECK(rejected_parse.error->code ==
          goldsrc::ConnectRequestErrorCode::invalid_authentication);
}

TEST_CASE("Connect parser and builder round-trip the synthetic fixture",
          "[goldsrc][connect-request]")
{
    const auto protected_bytes = text_bytes(kProtectedAuthentication);
    const auto suffix_bytes = text_bytes(kBinaryAuthentication);
    const auto fixture = build_fixture(0xffff'ffffU);
    auto parsed = goldsrc::parse_connect_request(fixture, fixture_profile());

    REQUIRE(parsed);
    REQUIRE(parsed.request);
    CHECK(parsed.request->challenge() == 0xffff'ffffU);
    CHECK(parsed.request->authentication_size() ==
          protected_bytes.size() + suffix_bytes.size());
    CHECK(parsed.request->authentication_suffix_size() == suffix_bytes.size());
    CHECK(parsed.request->authentication_matches(protected_bytes, suffix_bytes));
    CHECK(parsed.request->protocol_info().value().serialized().find(kProtectedAuthentication) ==
          std::string_view::npos);
    CHECK(std::ranges::none_of(
        parsed.request->protocol_info().value().entries(),
        [](const goldsrc::InfoStringEntry& entry) {
            return entry.value.find(kProtectedAuthentication) != std::string::npos;
        }));

    const auto rebuilt = goldsrc::ConnectRequestBuilder::build(
        *parsed.request, fixture_profile());
    REQUIRE(rebuilt);
    REQUIRE(rebuilt.datagram);
    const bool exact_round_trip = *rebuilt.datagram == fixture;
    CHECK(exact_round_trip);
}

TEST_CASE("Captured-size synthetic profile pins observed Protocol 48 lengths",
          "[goldsrc][connect-request][boundary]")
{
    constexpr std::string_view protected_marker =
        "TEST_AUTH_MATERIAL_TEST_AUTH_MAT";
    std::vector<std::byte> suffix(goldsrc::kObservedConnectAuthenticationSuffixSize);
    const auto marker = text_bytes(kBinaryAuthentication);
    for (std::size_t index = 0U; index < suffix.size(); ++index) {
        suffix[index] = marker[index % marker.size()];
    }
    auto authentication = goldsrc::AuthenticationMaterial::create(
        text_bytes(protected_marker), suffix);
    REQUIRE(authentication);

    auto profile = goldsrc::ConnectCompatibilityProfile{};
    profile.protected_authentication_is_ascii_hex = false;
    auto settings = goldsrc::ClientConnectionSettings{};
    settings.display_name = "TEST";
    auto prepared = goldsrc::prepare_connect_request(
        settings, std::move(*authentication.value), profile);
    REQUIRE(prepared);
    REQUIRE(prepared.value);
    CHECK(prepared.value->protocol_info_wire_size() ==
          goldsrc::kObservedConnectProtocolInfoSize);
    CHECK(prepared.value->user_info().value().serialized_size() ==
          goldsrc::kObservedConnectUserInfoSize);

    auto request = std::move(*prepared.value).make_request(0x8000'0000U);
    const auto built = goldsrc::ConnectRequestBuilder::build(request, profile);
    REQUIRE(built);
    REQUIRE(built.datagram);
    CHECK(built.datagram->size() == goldsrc::kObservedMaximumConnectDatagramSize);
}

TEST_CASE("Player name and model enforce the 31-byte project boundary",
          "[goldsrc][connect-request][boundary]")
{
    for (const bool testing_name : {true, false}) {
        auto at_limit = fixture_settings();
        (testing_name ? at_limit.display_name : at_limit.model).assign(
            goldsrc::kMaximumPlayerNameLength, 'A');
        auto accepted = goldsrc::prepare_connect_request(
            at_limit, make_authentication(), fixture_profile());
        INFO((testing_name ? "name at limit" : "model at limit"));
        REQUIRE(accepted);

        auto above_limit = fixture_settings();
        (testing_name ? above_limit.display_name : above_limit.model).assign(
            goldsrc::kMaximumPlayerNameLength + 1U, 'A');
        auto rejected = goldsrc::prepare_connect_request(
            above_limit, make_authentication(), fixture_profile());
        INFO((testing_name ? "name above limit" : "model above limit"));
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code == goldsrc::ConnectRequestErrorCode::invalid_user_info);
    }
}

TEST_CASE("Connect parser fails closed at every truncated fixture boundary",
          "[goldsrc][connect-request]")
{
    const auto valid = build_fixture(123U);
    REQUIRE(goldsrc::parse_connect_request(valid, fixture_profile()));

    for (std::size_t size = 0U; size < valid.size(); ++size) {
        const auto truncated = std::span<const std::byte>{valid}.first(size);
        const auto result = goldsrc::parse_connect_request(truncated, fixture_profile());
        INFO("truncated size " << size);
        CHECK_FALSE(result);
    }
}

TEST_CASE("Authentication material and connect values own their source storage",
          "[goldsrc][connect-request]")
{
    const auto expected_protected = text_bytes(kProtectedAuthentication);
    const auto expected_suffix = text_bytes(kBinaryAuthentication);
    auto source_protected = expected_protected;
    auto source_suffix = expected_suffix;
    auto authentication = goldsrc::AuthenticationMaterial::create(
        source_protected, source_suffix);
    REQUIRE(authentication);
    REQUIRE(authentication.value);

    std::ranges::fill(source_protected, std::byte{'X'});
    std::ranges::fill(source_suffix, std::byte{'Y'});
    CHECK(authentication.value->matches(expected_protected, expected_suffix));
    CHECK_FALSE(authentication.value->matches(source_protected, source_suffix));

    auto settings = fixture_settings();
    const std::string original_name = settings.display_name;
    auto prepared = goldsrc::prepare_connect_request(
        settings, std::move(*authentication.value), fixture_profile());
    REQUIRE(prepared);
    REQUIRE(prepared.value);
    settings.display_name.assign("MutatedAfterPrepare");

    auto request = std::move(*prepared.value).make_request(1U);
    const auto& entries = request.user_info().value().entries();
    const auto name_entry = std::ranges::find(entries, std::string_view{"name"},
                                               &goldsrc::InfoStringEntry::key);
    REQUIRE(name_entry != entries.end());
    CHECK(name_entry->value == original_name);
}

TEST_CASE("Connect size arithmetic reports integer overflow without wrapping",
          "[goldsrc][connect-request][boundary]")
{
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    std::size_t result = 0U;

    CHECK(goldsrc::checked_connect_request_size_add(maximum - 5U, 5U, result));
    CHECK(result == maximum);

    result = 123U;
    CHECK_FALSE(goldsrc::checked_connect_request_size_add(maximum - 5U, 6U, result));
    CHECK(result == 123U);

    CHECK(goldsrc::checked_connect_request_size_add(0U, 0U, result));
    CHECK(result == 0U);
}

} // namespace
