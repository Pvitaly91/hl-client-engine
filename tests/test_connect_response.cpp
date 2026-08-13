#include <hlclient/goldsrc/connect_response.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <locale>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] std::vector<std::byte> bytes_from_text(const std::string_view text)
{
    std::vector<std::byte> result;
    result.reserve(text.size());
    std::ranges::transform(text, std::back_inserter(result), [](const char value) {
        return static_cast<std::byte>(static_cast<unsigned char>(value));
    });
    return result;
}

[[nodiscard]] std::vector<std::byte> make_datagram(const std::string_view payload)
{
    std::vector<std::byte> result{
        std::byte{0xff},
        std::byte{0xff},
        std::byte{0xff},
        std::byte{0xff},
    };
    const auto payload_bytes = bytes_from_text(payload);
    result.insert(result.end(), payload_bytes.begin(), payload_bytes.end());
    return result;
}

[[nodiscard]] std::vector<std::byte> make_accept(
    const std::string_view fields = " 1 \"127.0.0.1:54456\" 0 10210")
{
    std::string payload{"B"};
    payload += fields;
    payload.push_back('\0');
    return make_datagram(payload);
}

[[nodiscard]] std::vector<std::byte> make_reject(const std::string_view message)
{
    std::string payload{"9"};
    payload += message;
    payload.push_back('\n');
    payload.push_back('\0');
    return make_datagram(payload);
}

void check_error(
    const std::span<const std::byte> datagram,
    const goldsrc::ConnectResponseErrorCode expected)
{
    const auto result = goldsrc::parse_connect_response(datagram);
    INFO("expected error " << goldsrc::to_string(expected));
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
    CHECK_FALSE(result.response);
    CHECK_FALSE(result.error->context.empty());
    CHECK(result.error->byte_offset <= datagram.size());
}

TEST_CASE("Parser accepts independent captured stock connect-accept fixtures",
          "[goldsrc][connect-response][capture]")
{
    struct Fixture {
        std::uint32_t user_id;
        std::uint16_t port;
    };
    constexpr std::array fixtures{
        Fixture{1U, 54'456U},
        Fixture{1U, 64'210U},
        Fixture{2U, 52'851U},
    };

    for (const auto fixture : fixtures) {
        std::string payload{"B "};
        payload += std::to_string(fixture.user_id);
        payload += " \"127.0.0.1:";
        payload += std::to_string(fixture.port);
        payload += "\" 0 10210";
        payload.push_back('\0');
        const auto datagram = make_datagram(payload);
        INFO("relay source port " << fixture.port);
        REQUIRE(datagram.size() == 34U);

        auto result = goldsrc::parse_connect_response(datagram);
        REQUIRE(result);
        REQUIRE(result.response);
        REQUIRE(std::holds_alternative<goldsrc::ConnectAccepted>(*result.response));
        const auto& accepted = std::get<goldsrc::ConnectAccepted>(*result.response);
        CHECK(accepted.user_id == fixture.user_id);
        CHECK(accepted.server_view_of_client ==
              hlclient::network::NetworkAddress::loopback(fixture.port));
        CHECK_FALSE(accepted.secure);
        CHECK(accepted.server_build == 10'210U);
        CHECK_FALSE(result.error);
    }
}

TEST_CASE("Parser accepts independent captured stock connect-reject fixtures",
          "[goldsrc][connect-response][capture]")
{
    constexpr std::array messages{
        std::string_view{"Invalid connection."},
        std::string_view{
            "This server is using a newer protocol ( 48 ) than your client ( 47 ).  "
            "You should check for updates to your client."},
        std::string_view{
            "This server is using an older protocol ( 48 ) than your client ( 49 ).  "
            "If you believe this server is outdated, you can contact the server administrator "
            "at (no email address specified)."},
    };
    constexpr std::array<std::size_t, messages.size()> expected_sizes{26U, 122U, 192U};

    for (std::size_t index = 0U; index < messages.size(); ++index) {
        const auto datagram = make_reject(messages[index]);
        INFO("fixture " << index);
        REQUIRE(datagram.size() == expected_sizes[index]);
        auto result = goldsrc::parse_connect_response(datagram);
        REQUIRE(result);
        REQUIRE(result.response);
        REQUIRE(std::holds_alternative<goldsrc::ConnectRejected>(*result.response));
        CHECK(std::get<goldsrc::ConnectRejected>(*result.response).message == messages[index]);
    }
}

TEST_CASE("Connect-response parser returns owning values", "[goldsrc][connect-response]")
{
    auto source = make_reject("Invalid connection.");
    auto result = goldsrc::parse_connect_response(source);
    REQUIRE(result);
    std::ranges::fill(source, std::byte{'X'});

    REQUIRE(result.response);
    REQUIRE(std::holds_alternative<goldsrc::ConnectRejected>(*result.response));
    CHECK(std::get<goldsrc::ConnectRejected>(*result.response).message == "Invalid connection.");
}

TEST_CASE("Connect-accept parser accepts exact typed boundaries",
          "[goldsrc][connect-response][boundary]")
{
    const auto maximum = make_accept(
        " 4294967295 \"255.255.255.255:65535\" 1 4294967295");
    const auto zero = make_accept(" 0 \"0.0.0.0:1\" 0 0");

    for (const auto* fixture : {&maximum, &zero}) {
        const auto result = goldsrc::parse_connect_response(*fixture);
        REQUIRE(result);
        REQUIRE(result.response);
        CHECK(std::holds_alternative<goldsrc::ConnectAccepted>(*result.response));
    }
}

TEST_CASE("Connect-accept parser enforces strict grammar",
          "[goldsrc][connect-response][matrix]")
{
    struct InvalidFixture {
        std::string fields;
        goldsrc::ConnectResponseErrorCode error;
    };
    const std::array fixtures{
        InvalidFixture{"1 \"127.0.0.1:54456\" 0 10210", goldsrc::ConnectResponseErrorCode::invalid_separator},
        InvalidFixture{"  \"127.0.0.1:54456\" 0 10210", goldsrc::ConnectResponseErrorCode::missing_user_id},
        InvalidFixture{" +1 \"127.0.0.1:54456\" 0 10210", goldsrc::ConnectResponseErrorCode::invalid_user_id},
        InvalidFixture{" 01 \"127.0.0.1:54456\" 0 10210", goldsrc::ConnectResponseErrorCode::invalid_user_id},
        InvalidFixture{" 4294967296 \"127.0.0.1:54456\" 0 10210", goldsrc::ConnectResponseErrorCode::user_id_overflow},
        InvalidFixture{" 1  \"127.0.0.1:54456\" 0 10210", goldsrc::ConnectResponseErrorCode::invalid_quote},
        InvalidFixture{" 1\t\"127.0.0.1:54456\" 0 10210", goldsrc::ConnectResponseErrorCode::invalid_separator},
        InvalidFixture{" 1 127.0.0.1:54456\" 0 10210", goldsrc::ConnectResponseErrorCode::invalid_quote},
        InvalidFixture{" 1 \"127.0.0.1:54456 0 10210", goldsrc::ConnectResponseErrorCode::invalid_quote},
        InvalidFixture{" 1 \"127.0.0.1:0\" 0 10210", goldsrc::ConnectResponseErrorCode::invalid_client_endpoint},
        InvalidFixture{" 1 \"127.0.0.1:65536\" 0 10210", goldsrc::ConnectResponseErrorCode::invalid_client_endpoint},
        InvalidFixture{" 1 \"127.0.00.1:54456\" 0 10210", goldsrc::ConnectResponseErrorCode::invalid_client_endpoint},
        InvalidFixture{" 1 \"256.0.0.1:54456\" 0 10210", goldsrc::ConnectResponseErrorCode::invalid_client_endpoint},
        InvalidFixture{" 1 \"localhost:54456\" 0 10210", goldsrc::ConnectResponseErrorCode::invalid_client_endpoint},
        InvalidFixture{" 1 \"127.0.0.1:54456\"  0 10210", goldsrc::ConnectResponseErrorCode::invalid_secure_flag},
        InvalidFixture{" 1 \"127.0.0.1:54456\"\t0 10210", goldsrc::ConnectResponseErrorCode::invalid_separator},
        InvalidFixture{" 1 \"127.0.0.1:54456\" 00 10210", goldsrc::ConnectResponseErrorCode::invalid_secure_flag},
        InvalidFixture{" 1 \"127.0.0.1:54456\" 2 10210", goldsrc::ConnectResponseErrorCode::invalid_secure_flag},
        InvalidFixture{" 1 \"127.0.0.1:54456\" 0  10210", goldsrc::ConnectResponseErrorCode::invalid_server_build},
        InvalidFixture{" 1 \"127.0.0.1:54456\" 0\t10210", goldsrc::ConnectResponseErrorCode::invalid_separator},
        InvalidFixture{" 1 \"127.0.0.1:54456\" 0 ", goldsrc::ConnectResponseErrorCode::missing_server_build},
        InvalidFixture{" 1 \"127.0.0.1:54456\" 0 +1", goldsrc::ConnectResponseErrorCode::invalid_server_build},
        InvalidFixture{" 1 \"127.0.0.1:54456\" 0 010210", goldsrc::ConnectResponseErrorCode::invalid_server_build},
        InvalidFixture{" 1 \"127.0.0.1:54456\" 0 4294967296", goldsrc::ConnectResponseErrorCode::server_build_overflow},
    };

    for (const auto& fixture : fixtures) {
        INFO(fixture.fields);
        check_error(make_accept(fixture.fields), fixture.error);
    }
}

TEST_CASE("Connect-accept parser requires exact NUL-only termination",
          "[goldsrc][connect-response]")
{
    check_error(
        make_datagram("B 1 \"127.0.0.1:54456\" 0 10210"),
        goldsrc::ConnectResponseErrorCode::invalid_terminator);

    std::string lf_nul{"B 1 \"127.0.0.1:54456\" 0 10210\n"};
    lf_nul.push_back('\0');
    check_error(make_datagram(lf_nul), goldsrc::ConnectResponseErrorCode::invalid_terminator);

    std::string trailing{"B 1 \"127.0.0.1:54456\" 0 10210"};
    trailing.push_back('\0');
    trailing.push_back('X');
    check_error(make_datagram(trailing), goldsrc::ConnectResponseErrorCode::unexpected_trailing_data);

    std::string duplicate_nul{"B 1 \"127.0.0.1:54456\" 0 10210"};
    duplicate_nul.push_back('\0');
    duplicate_nul.push_back('\0');
    check_error(
        make_datagram(duplicate_nul),
        goldsrc::ConnectResponseErrorCode::unexpected_trailing_data);
}

TEST_CASE("Connect-reject parser enforces message and terminator bounds",
          "[goldsrc][connect-response][boundary]")
{
    const std::string at_limit(goldsrc::kMaximumConnectRejectMessageSize, 'A');
    const auto accepted = goldsrc::parse_connect_response(make_reject(at_limit));
    REQUIRE(accepted);
    REQUIRE(accepted.response);
    CHECK(std::get<goldsrc::ConnectRejected>(*accepted.response).message.size() ==
          goldsrc::kMaximumConnectRejectMessageSize);

    check_error(
        make_reject(std::string(goldsrc::kMaximumConnectRejectMessageSize + 1U, 'A')),
        goldsrc::ConnectResponseErrorCode::rejection_message_too_large);
    check_error(make_reject(""), goldsrc::ConnectResponseErrorCode::empty_rejection_message);
    check_error(
        make_datagram("9Invalid connection."),
        goldsrc::ConnectResponseErrorCode::invalid_terminator);

    std::string nul_only{"9Invalid connection."};
    nul_only.push_back('\0');
    check_error(make_datagram(nul_only), goldsrc::ConnectResponseErrorCode::invalid_terminator);

    std::string lf_without_nul{"9Invalid connection.\n"};
    check_error(make_datagram(lf_without_nul), goldsrc::ConnectResponseErrorCode::invalid_terminator);

    std::string trailing{"9Invalid connection.\n"};
    trailing.push_back('\0');
    trailing.push_back('X');
    check_error(make_datagram(trailing), goldsrc::ConnectResponseErrorCode::unexpected_trailing_data);

    std::string embedded_nul{"9Invalid"};
    embedded_nul.push_back('\0');
    embedded_nul += " connection.\n";
    embedded_nul.push_back('\0');
    check_error(make_datagram(embedded_nul), goldsrc::ConnectResponseErrorCode::invalid_rejection_message);

    std::string non_ascii{"9Invalid "};
    non_ascii.push_back(static_cast<char>(0x80));
    non_ascii.push_back('\n');
    non_ascii.push_back('\0');
    check_error(make_datagram(non_ascii), goldsrc::ConnectResponseErrorCode::invalid_rejection_message);

    check_error(
        make_reject("first line\nsecond line"),
        goldsrc::ConnectResponseErrorCode::invalid_terminator);

    std::string controls{"Remote says"};
    controls.push_back('\r');
    controls.push_back('\x1b');
    controls += "stop";
    const auto controlled = goldsrc::parse_connect_response(make_reject(controls));
    REQUIRE(controlled);
    const auto& controlled_rejection =
        std::get<goldsrc::ConnectRejected>(*controlled.response);
    CHECK(controlled_rejection.message == controls);
    const auto presentation =
        goldsrc::sanitize_connect_rejection_for_presentation(controlled_rejection.message);
    CHECK(presentation.find('\r') == std::string::npos);
    CHECK(presentation.find('\x1b') == std::string::npos);
}

TEST_CASE("Connect-response parser rejects unknown classes and non-connectionless packets",
          "[goldsrc][connect-response]")
{
    std::string unknown{"Xunknown"};
    unknown.push_back('\0');
    check_error(make_datagram(unknown), goldsrc::ConnectResponseErrorCode::unknown_response_class);

    auto wrong_header = make_accept();
    wrong_header[0] = std::byte{1};
    check_error(wrong_header, goldsrc::ConnectResponseErrorCode::invalid_header);

    const std::array sequenced{
        std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{'B'}};
    check_error(sequenced, goldsrc::ConnectResponseErrorCode::invalid_header);
}

TEST_CASE("Connect-response parser applies the hard packet cap before parsing",
          "[goldsrc][connect-response][boundary]")
{
    std::vector<std::byte> exact_limit(
        goldsrc::kMaximumConnectResponseDatagramSize,
        std::byte{'X'});
    std::fill_n(exact_limit.begin(), 4U, std::byte{0xff});
    check_error(exact_limit, goldsrc::ConnectResponseErrorCode::unknown_response_class);

    std::vector<std::byte> oversized(
        goldsrc::kMaximumConnectResponseDatagramSize + 1U,
        std::byte{0xff});
    check_error(oversized, goldsrc::ConnectResponseErrorCode::payload_too_large);
}

TEST_CASE("Every truncation of valid accept and reject responses fails closed",
          "[goldsrc][connect-response][truncation]")
{
    const std::array fixtures{
        make_accept(),
        make_reject("Invalid connection."),
        make_reject(
            "This server is using a newer protocol ( 48 ) than your client ( 47 ).  "
            "You should check for updates to your client."),
    };
    for (std::size_t fixture_index = 0U; fixture_index < fixtures.size(); ++fixture_index) {
        REQUIRE(goldsrc::parse_connect_response(fixtures[fixture_index]));
        for (std::size_t size = 0U; size < fixtures[fixture_index].size(); ++size) {
            INFO("fixture " << fixture_index << ", truncation " << size);
            CHECK_FALSE(goldsrc::parse_connect_response(
                std::span<const std::byte>{fixtures[fixture_index]}.first(size)));
        }
    }
}

TEST_CASE("Connect-response decimal grammar is independent of the global locale",
          "[goldsrc][connect-response][locale]")
{
    struct CommaPunctuation final : std::numpunct<char> {
        [[nodiscard]] char do_decimal_point() const override
        {
            return ',';
        }

        [[nodiscard]] char do_thousands_sep() const override
        {
            return '.';
        }

        [[nodiscard]] std::string do_grouping() const override
        {
            return "\3";
        }
    };

    const auto previous = std::locale();
    struct LocaleRestorer final {
        explicit LocaleRestorer(std::locale original) : original_{std::move(original)} {}
        ~LocaleRestorer() { std::locale::global(original_); }
        std::locale original_;
    } restorer{previous};
    std::locale::global(std::locale{previous, new CommaPunctuation});

    CHECK(goldsrc::parse_connect_response(make_accept()));
    check_error(
        make_accept(" 1.000 \"127.0.0.1:54456\" 0 10.210"),
        goldsrc::ConnectResponseErrorCode::invalid_separator);
}

TEST_CASE("Reject presentation sanitizer escapes controls and is tightly bounded",
          "[goldsrc][connect-response][sanitizer]")
{
    std::string untrusted{"visible\nline\rreturn\ttab\\slash"};
    untrusted.push_back('\x1b');
    untrusted.push_back('\0');
    untrusted.push_back(static_cast<char>(0xff));
    const auto sanitized = goldsrc::sanitize_connect_rejection_for_presentation(untrusted);

    CHECK(sanitized ==
          "visible\\nline\\rreturn\\ttab\\\\slash\\x1B\\x00\\xFF");
    CHECK(sanitized.find('\n') == std::string::npos);
    CHECK(sanitized.find('\r') == std::string::npos);
    CHECK(sanitized.find('\x1b') == std::string::npos);

    const auto truncated = goldsrc::sanitize_connect_rejection_for_presentation(
        std::string(300U, 'A'),
        16U);
    CHECK(truncated == "AAAAAAAAAAAAA...");
    CHECK(truncated.size() == 16U);

    const auto tiny = goldsrc::sanitize_connect_rejection_for_presentation("A\nB", 2U);
    CHECK(tiny == "..");
    CHECK(goldsrc::sanitize_connect_rejection_for_presentation("A", 0U).empty());

    const auto clamped = goldsrc::sanitize_connect_rejection_for_presentation(
        std::string(400U, 'A'),
        static_cast<std::size_t>(-1));
    CHECK(clamped.size() == goldsrc::kMaximumConnectRejectPresentationSize);
    CHECK(clamped.ends_with("..."));
}

} // namespace
