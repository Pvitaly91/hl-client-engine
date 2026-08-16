#include <hlclient/goldsrc/client_message.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <span>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

template<std::size_t Size>
[[nodiscard]] std::vector<std::byte> bytes(
    const std::array<std::uint8_t, Size>& values)
{
    std::vector<std::byte> output;
    output.reserve(values.size());
    std::ranges::transform(
        values,
        std::back_inserter(output),
        [](const std::uint8_t value) { return std::byte{value}; });
    return output;
}

void check_error(
    const goldsrc::ClientMessageParseResult& result,
    const goldsrc::ClientMessageErrorCode expected,
    const std::size_t input_size)
{
    INFO("expected " << goldsrc::to_string(expected));
    REQUIRE_FALSE(result);
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == expected);
    CHECK(result.error->byte_offset <= input_size);
    CHECK_FALSE(result.error->context.empty());
    CHECK(result.error->context.size() <= goldsrc::kClientMessageDiagnosticTextLimit);
    CHECK_FALSE(result.request.has_value());
}

TEST_CASE("Initial sign-on builder matches the independent stock semantic fixture",
          "[goldsrc][signon][client-message][capture]")
{
    const auto fixture = bytes(std::array<std::uint8_t, 5U>{
        0x03U, 0x6eU, 0x65U, 0x77U, 0x00U});

    const auto built = goldsrc::InitialSignonRequestBuilder::build();
    REQUIRE(built);
    REQUIRE(built.bytes.has_value());
    CHECK(*built.bytes == fixture);
    CHECK_FALSE(built.error.has_value());
    CHECK(built.bytes->size() == goldsrc::kInitialSignonRequestSize);
    CHECK(std::to_integer<std::uint8_t>((*built.bytes)[0]) == 3U);
    CHECK(std::to_integer<std::uint8_t>((*built.bytes)[4]) == 0U);

    const auto parsed = goldsrc::parse_initial_signon_request(fixture);
    REQUIRE(parsed);
    REQUIRE(parsed.request.has_value());
    CHECK(parsed.request->opcode() == goldsrc::ClientMessageOpcode::string_command);
    CHECK(parsed.request->command() == "new");
    CHECK_FALSE(parsed.error.has_value());
}

TEST_CASE("Initial sign-on parser rejects every truncation prefix",
          "[goldsrc][signon][client-message][bounds]")
{
    const auto fixture = bytes(std::array<std::uint8_t, 5U>{
        0x03U, 0x6eU, 0x65U, 0x77U, 0x00U});

    for (std::size_t size = 0U; size < fixture.size(); ++size) {
        INFO("prefix size " << size);
        const auto result = goldsrc::parse_initial_signon_request(
            std::span<const std::byte>{fixture}.first(size));
        REQUIRE_FALSE(result);
        REQUIRE(result.error.has_value());
        CHECK(result.error->byte_offset <= size);
        CHECK_FALSE(result.request.has_value());
    }
}

TEST_CASE("Initial sign-on parser rejects wrong opcode and unsupported command variants",
          "[goldsrc][signon][client-message][strict]")
{
    auto wrong_opcode = bytes(std::array<std::uint8_t, 5U>{
        0x02U, 0x6eU, 0x65U, 0x77U, 0x00U});
    check_error(
        goldsrc::parse_initial_signon_request(wrong_opcode),
        goldsrc::ClientMessageErrorCode::wrong_opcode,
        wrong_opcode.size());

    auto wrong_case = bytes(std::array<std::uint8_t, 5U>{
        0x03U, 0x4eU, 0x65U, 0x77U, 0x00U});
    check_error(
        goldsrc::parse_initial_signon_request(wrong_case),
        goldsrc::ClientMessageErrorCode::unsupported_command_variant,
        wrong_case.size());

    auto empty = bytes(std::array<std::uint8_t, 2U>{0x03U, 0x00U});
    check_error(
        goldsrc::parse_initial_signon_request(empty),
        goldsrc::ClientMessageErrorCode::empty_required_command,
        empty.size());
}

TEST_CASE("Initial sign-on parser rejects malformed terminators and transport padding",
          "[goldsrc][signon][client-message][terminator]")
{
    auto missing = bytes(std::array<std::uint8_t, 4U>{
        0x03U, 0x6eU, 0x65U, 0x77U});
    check_error(
        goldsrc::parse_initial_signon_request(missing),
        goldsrc::ClientMessageErrorCode::missing_terminator,
        missing.size());

    auto embedded = bytes(std::array<std::uint8_t, 6U>{
        0x03U, 0x6eU, 0x00U, 0x65U, 0x77U, 0x00U});
    check_error(
        goldsrc::parse_initial_signon_request(embedded),
        goldsrc::ClientMessageErrorCode::embedded_nul,
        embedded.size());

    auto duplicate = bytes(std::array<std::uint8_t, 6U>{
        0x03U, 0x6eU, 0x65U, 0x77U, 0x00U, 0x00U});
    check_error(
        goldsrc::parse_initial_signon_request(duplicate),
        goldsrc::ClientMessageErrorCode::unexpected_trailing_data,
        duplicate.size());

    auto stock_netchan_body = bytes(std::array<std::uint8_t, 8U>{
        0x03U, 0x6eU, 0x65U, 0x77U, 0x00U, 0x01U, 0x01U, 0x01U});
    check_error(
        goldsrc::parse_initial_signon_request(stock_netchan_body),
        goldsrc::ClientMessageErrorCode::unexpected_trailing_data,
        stock_netchan_body.size());
}

TEST_CASE("Initial sign-on parser rejects CR and LF without repair",
          "[goldsrc][signon][client-message][security]")
{
    for (const auto forbidden : {0x0dU, 0x0aU}) {
        auto fixture = bytes(std::array<std::uint8_t, 6U>{
            0x03U, 0x6eU, static_cast<std::uint8_t>(forbidden), 0x65U, 0x77U, 0x00U});
        INFO("forbidden byte " << forbidden);
        check_error(
            goldsrc::parse_initial_signon_request(fixture),
            goldsrc::ClientMessageErrorCode::invalid_command_character,
            fixture.size());
    }
}

TEST_CASE("Initial sign-on command and message limits are exact",
          "[goldsrc][signon][client-message][limits]")
{
    STATIC_CHECK(goldsrc::kMaximumInitialSignonCommandLength > 0U);
    STATIC_CHECK(
        goldsrc::kMaximumInitialSignonMessageSize ==
        1U + goldsrc::kMaximumInitialSignonCommandLength + 1U);

    std::vector<std::byte> at_limit(
        1U + goldsrc::kMaximumInitialSignonCommandLength,
        std::byte{'x'});
    at_limit.front() = std::byte{3U};
    check_error(
        goldsrc::parse_initial_signon_request(at_limit),
        goldsrc::ClientMessageErrorCode::missing_terminator,
        at_limit.size());

    at_limit.push_back(std::byte{0U});
    check_error(
        goldsrc::parse_initial_signon_request(at_limit),
        goldsrc::ClientMessageErrorCode::unsupported_command_variant,
        at_limit.size());

    std::vector<std::byte> limit_plus_one(
        2U + goldsrc::kMaximumInitialSignonCommandLength,
        std::byte{'x'});
    limit_plus_one.front() = std::byte{3U};
    check_error(
        goldsrc::parse_initial_signon_request(limit_plus_one),
        goldsrc::ClientMessageErrorCode::command_too_large,
        limit_plus_one.size());

    limit_plus_one.push_back(std::byte{0U});
    check_error(
        goldsrc::parse_initial_signon_request(limit_plus_one),
        goldsrc::ClientMessageErrorCode::message_too_large,
        limit_plus_one.size());
}

} // namespace
