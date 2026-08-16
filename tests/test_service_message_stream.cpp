#include <hlclient/goldsrc/service_message_stream.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] goldsrc::OwnedServicePayload decompressed(
    std::vector<std::byte> bytes)
{
    goldsrc::OwnedServicePayload payload;
    payload.bytes = std::move(bytes);
    payload.source_sequence = 31U;
    payload.source_acknowledgement = 17U;
    payload.source_reliable = true;
    payload.reassembled = true;
    payload.decompressed = true;
    payload.acknowledgement_reliable = true;
    return payload;
}

[[nodiscard]] std::vector<std::byte> text_message(const std::string_view text)
{
    std::vector<std::byte> message;
    message.reserve(1U + text.size() + 1U);
    message.push_back(std::byte{8U});
    const auto text_bytes = std::as_bytes(std::span{text.data(), text.size()});
    message.insert(message.end(), text_bytes.begin(), text_bytes.end());
    message.push_back(std::byte{0U});
    return message;
}

void append(std::vector<std::byte>& destination, const std::vector<std::byte>& source)
{
    destination.insert(destination.end(), source.begin(), source.end());
}

void check_error(
    const goldsrc::ServiceMessageDecodeResult& result,
    const goldsrc::ServiceMessageErrorCode expected,
    const std::size_t input_size)
{
    INFO("expected " << goldsrc::to_string(expected));
    REQUIRE_FALSE(result);
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == expected);
    CHECK(result.error->byte_offset <= input_size);
    CHECK_FALSE(result.error->context.empty());
    CHECK(result.error->context.size() <= goldsrc::kServiceMessageDiagnosticTextLimit);
    CHECK_FALSE(result.stream.has_value());
}

[[nodiscard]] const goldsrc::ServiceTextControl& text_body(
    const goldsrc::DecodedServiceMessage& message)
{
    return std::get<goldsrc::ServiceTextControl>(message.body);
}

TEST_CASE("Service decoder configuration is positive and hard capped",
          "[goldsrc][signon][service][limits]")
{
    CHECK(goldsrc::valid_service_message_limits({}));
    CHECK(goldsrc::valid_service_message_limits({
        goldsrc::kMaximumServiceStringLength,
        goldsrc::kMaximumServiceMessagesPerPayload,
        goldsrc::kMaximumServicePayloadSize,
    }));

    auto invalid = goldsrc::ServiceMessageLimits{};
    invalid.maximum_string_length = 0U;
    CHECK_FALSE(goldsrc::valid_service_message_limits(invalid));
    invalid = {};
    invalid.maximum_messages_per_payload = 0U;
    CHECK_FALSE(goldsrc::valid_service_message_limits(invalid));
    invalid = {};
    invalid.maximum_payload_size = 0U;
    CHECK_FALSE(goldsrc::valid_service_message_limits(invalid));
    invalid = {};
    invalid.maximum_string_length = goldsrc::kMaximumServiceStringLength + 1U;
    CHECK_FALSE(goldsrc::valid_service_message_limits(invalid));
    invalid = {};
    invalid.maximum_messages_per_payload =
        goldsrc::kMaximumServiceMessagesPerPayload + 1U;
    CHECK_FALSE(goldsrc::valid_service_message_limits(invalid));
    invalid = {};
    invalid.maximum_payload_size = goldsrc::kMaximumServicePayloadSize + 1U;
    CHECK_FALSE(goldsrc::valid_service_message_limits(invalid));

    const goldsrc::ServiceMessageStreamDecoder decoder{invalid};
    check_error(
        decoder.decode(decompressed(text_message("x"))),
        goldsrc::ServiceMessageErrorCode::invalid_configuration,
        3U);
}

TEST_CASE("Service decoder requires a nonempty owning decompressed payload",
          "[goldsrc][signon][service][ownership]")
{
    const goldsrc::ServiceMessageStreamDecoder decoder;

    auto compressed = decompressed(text_message("x"));
    compressed.decompressed = false;
    check_error(
        decoder.decode(std::move(compressed)),
        goldsrc::ServiceMessageErrorCode::payload_not_decompressed,
        3U);

    check_error(
        decoder.decode(decompressed({})),
        goldsrc::ServiceMessageErrorCode::empty_payload,
        0U);
}

TEST_CASE("Service decoder consumes one or multiple confirmed text controls",
          "[goldsrc][signon][service][text]")
{
    const goldsrc::ServiceMessageStreamDecoder decoder;

    const auto one = decoder.decode(decompressed(text_message("hello")));
    REQUIRE(one);
    REQUIRE(one.stream.has_value());
    REQUIRE(one.stream->messages.size() == 1U);
    CHECK_FALSE(one.stream->boundary.has_value());
    CHECK(one.stream->bytes_consumed == 7U);
    CHECK(one.stream->required_event_count == 1U);
    CHECK(one.stream->messages[0].opcode == goldsrc::ServiceMessageOpcode::text_control);
    CHECK(one.stream->messages[0].kind == goldsrc::ServiceMessageKind::text_control);
    CHECK(one.stream->messages[0].byte_offset == 0U);
    CHECK(one.stream->messages[0].byte_count == 7U);
    CHECK(text_body(one.stream->messages[0]).text == "hello");

    auto multiple_bytes = text_message("first");
    append(multiple_bytes, text_message("second"));
    const auto multiple = decoder.decode(decompressed(std::move(multiple_bytes)));
    REQUIRE(multiple);
    REQUIRE(multiple.stream->messages.size() == 2U);
    CHECK_FALSE(multiple.stream->boundary.has_value());
    CHECK(text_body(multiple.stream->messages[0]).text == "first");
    CHECK(text_body(multiple.stream->messages[1]).text == "second");
    CHECK(multiple.stream->messages[1].byte_offset == 7U);
    CHECK(multiple.stream->bytes_consumed == 15U);
    CHECK(multiple.stream->required_event_count == 2U);
}

TEST_CASE("Service decoder reaches the complex boundary without consuming its body",
          "[goldsrc][signon][service][boundary][capture]")
{
    const goldsrc::ServiceMessageStreamDecoder decoder;

    const std::vector<std::byte> first{
        std::byte{11U}, std::byte{0xaaU}, std::byte{0xbbU}};
    const auto boundary_first = decoder.decode(decompressed(first));
    REQUIRE(boundary_first);
    REQUIRE(boundary_first.stream->boundary.has_value());
    CHECK(boundary_first.stream->messages.empty());
    CHECK(
        boundary_first.stream->boundary->opcode ==
        goldsrc::ServiceMessageOpcode::complex_signon_boundary);
    CHECK(boundary_first.stream->boundary->byte_offset == 0U);
    CHECK(boundary_first.stream->boundary->remaining_byte_count == 2U);
    CHECK(boundary_first.stream->bytes_consumed == 1U);
    CHECK(boundary_first.stream->required_event_count == 1U);
    CHECK(boundary_first.stream->payload.bytes == first);

    constexpr std::string_view synthetic_text{
        "SYNTHETIC_STOCK_TEXT_CONTROL_40_BYTES___"};
    STATIC_REQUIRE(synthetic_text.size() == 40U);
    auto captured_shape = text_message(synthetic_text);
    REQUIRE(captured_shape.size() == 42U);
    captured_shape.push_back(std::byte{11U});
    captured_shape.resize(7'480U, std::byte{0xa5U});

    const auto captured = decoder.decode(decompressed(captured_shape));
    REQUIRE(captured);
    REQUIRE(captured.stream->messages.size() == 1U);
    REQUIRE(captured.stream->boundary.has_value());
    CHECK(captured.stream->messages[0].byte_offset == 0U);
    CHECK(captured.stream->messages[0].byte_count == 42U);
    CHECK(text_body(captured.stream->messages[0]).text.size() == 40U);
    CHECK(captured.stream->boundary->byte_offset == 42U);
    CHECK(captured.stream->boundary->remaining_byte_count == 7'437U);
    CHECK(captured.stream->bytes_consumed == 43U);
    CHECK(captured.stream->required_event_count == 2U);
    CHECK(captured.stream->payload.bytes.size() == 7'480U);
    CHECK(captured.stream->payload.bytes[43U] == std::byte{0xa5U});
}

TEST_CASE("Service boundary opcode without a body fails closed",
          "[goldsrc][signon][service][boundary][truncated]")
{
    const goldsrc::ServiceMessageStreamDecoder decoder;
    const std::vector<std::byte> truncated{std::byte{11U}};
    check_error(
        decoder.decode(decompressed(truncated)),
        goldsrc::ServiceMessageErrorCode::boundary_body_missing,
        truncated.size());
}

TEST_CASE("Unknown service opcode before the boundary is not skipped or resynchronized",
          "[goldsrc][signon][service][strict]")
{
    const goldsrc::ServiceMessageStreamDecoder decoder;

    const std::vector<std::byte> unknown{
        std::byte{0xfeU}, std::byte{11U}, std::byte{0xaaU}};
    const auto result = decoder.decode(decompressed(unknown));
    check_error(
        result,
        goldsrc::ServiceMessageErrorCode::unsupported_service_opcode,
        unknown.size());
    REQUIRE(result.error->wire_opcode.has_value());
    CHECK(*result.error->wire_opcode == 0xfeU);

    const std::vector<std::byte> unterminated_with_boundary_byte{
        std::byte{8U}, std::byte{'x'}, std::byte{11U}};
    check_error(
        decoder.decode(decompressed(unterminated_with_boundary_byte)),
        goldsrc::ServiceMessageErrorCode::unterminated_string,
        unterminated_with_boundary_byte.size());
}

TEST_CASE("Service string bound accepts the limit and rejects limit plus one",
          "[goldsrc][signon][service][string][limits]")
{
    auto limits = goldsrc::ServiceMessageLimits{};
    limits.maximum_string_length = 3U;
    const goldsrc::ServiceMessageStreamDecoder decoder{limits};

    const auto accepted = decoder.decode(decompressed(text_message("abc")));
    REQUIRE(accepted);
    REQUIRE(accepted.stream->messages.size() == 1U);
    CHECK(text_body(accepted.stream->messages[0]).text == "abc");

    const auto oversized = text_message("abcd");
    check_error(
        decoder.decode(decompressed(oversized)),
        goldsrc::ServiceMessageErrorCode::service_string_too_long,
        oversized.size());

    const std::vector<std::byte> unterminated{
        std::byte{8U}, std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    check_error(
        decoder.decode(decompressed(unterminated)),
        goldsrc::ServiceMessageErrorCode::unterminated_string,
        unterminated.size());
}

TEST_CASE("Service message count bound includes the boundary opcode",
          "[goldsrc][signon][service][message-count][limits]")
{
    auto limits = goldsrc::ServiceMessageLimits{};
    limits.maximum_messages_per_payload = 2U;
    const goldsrc::ServiceMessageStreamDecoder decoder{limits};

    auto two = text_message("a");
    append(two, text_message("b"));
    const auto accepted = decoder.decode(decompressed(two));
    REQUIRE(accepted);
    CHECK(accepted.stream->messages.size() == 2U);

    auto three = two;
    append(three, text_message("c"));
    check_error(
        decoder.decode(decompressed(three)),
        goldsrc::ServiceMessageErrorCode::message_limit_exceeded,
        three.size());

    auto text_then_boundary = text_message("a");
    text_then_boundary.push_back(std::byte{11U});
    text_then_boundary.push_back(std::byte{0xaaU});
    const auto boundary = decoder.decode(decompressed(text_then_boundary));
    REQUIRE(boundary);
    REQUIRE(boundary.stream->boundary.has_value());
    CHECK(boundary.stream->required_event_count == 2U);
}

TEST_CASE("Service payload bound accepts the limit and rejects limit plus one",
          "[goldsrc][signon][service][payload][limits]")
{
    auto limits = goldsrc::ServiceMessageLimits{};
    limits.maximum_payload_size = 3U;
    const goldsrc::ServiceMessageStreamDecoder decoder{limits};

    const auto accepted = decoder.decode(decompressed(text_message("x")));
    REQUIRE(accepted);
    CHECK(accepted.stream->bytes_consumed == 3U);

    const auto oversized = text_message("xy");
    check_error(
        decoder.decode(decompressed(oversized)),
        goldsrc::ServiceMessageErrorCode::payload_too_large,
        oversized.size());
}

TEST_CASE("Every prefix of a valid service fixture is bounded",
          "[goldsrc][signon][service][truncation]")
{
    auto fixture = text_message("abc");
    fixture.push_back(std::byte{11U});
    fixture.push_back(std::byte{0xaaU});
    const goldsrc::ServiceMessageStreamDecoder decoder;

    for (std::size_t size = 0U; size <= fixture.size(); ++size) {
        INFO("prefix size " << size);
        auto prefix = fixture;
        prefix.resize(size);
        const auto result = decoder.decode(decompressed(std::move(prefix)));
        if (size == 5U || size == fixture.size()) {
            REQUIRE(result);
            if (size == 5U) {
                CHECK_FALSE(result.stream->boundary.has_value());
            } else {
                CHECK(result.stream->boundary.has_value());
            }
        } else {
            REQUIRE_FALSE(result);
            REQUIRE(result.error.has_value());
            CHECK(result.error->byte_offset <= size);
        }
    }
}

TEST_CASE("Decoded service strings and payload remain owning after input lifetime ends",
          "[goldsrc][signon][service][ownership]")
{
    std::optional<goldsrc::ServiceMessageDecodeResult> result;
    {
        auto fixture = text_message("owned");
        fixture.push_back(std::byte{11U});
        fixture.push_back(std::byte{0x7fU});
        const goldsrc::ServiceMessageStreamDecoder decoder;
        result.emplace(decoder.decode(decompressed(std::move(fixture))));
    }

    REQUIRE(*result);
    REQUIRE(result->stream.has_value());
    CHECK(text_body(result->stream->messages[0]).text == "owned");
    CHECK(result->stream->payload.bytes.back() == std::byte{0x7fU});
}

TEST_CASE("Service text presentation escapes terminal controls and truncates atomically",
          "[goldsrc][signon][service][text][security]")
{
    std::string untrusted{"safe"};
    untrusted.push_back('\x1b');
    untrusted += "[31m\r\n\t\\";
    untrusted.push_back('\x01');

    const auto sanitized = goldsrc::sanitize_service_text_for_presentation(untrusted);
    CHECK(sanitized == "safe\\x1B[31m\\r\\n\\t\\\\\\x01");
    CHECK(sanitized.find('\x1b') == std::string::npos);
    CHECK(sanitized.find('\r') == std::string::npos);
    CHECK(sanitized.find('\n') == std::string::npos);

    CHECK(goldsrc::sanitize_service_text_for_presentation("abcdef", 6U) == "abcdef");
    CHECK(goldsrc::sanitize_service_text_for_presentation("abcdefg", 6U) == "abc...");
    CHECK(goldsrc::sanitize_service_text_for_presentation("\x1b", 4U) == "\\x1B");
    CHECK(goldsrc::sanitize_service_text_for_presentation("\x1bX", 4U) == "...");
    CHECK(goldsrc::sanitize_service_text_for_presentation("abc", 0U).empty());
}

TEST_CASE("Unconfirmed command-text opcode is data-rejected and cannot execute",
          "[goldsrc][signon][service][security]")
{
    bool command_dispatch_called = false;
    bool shell_called = false;
    bool filesystem_called = false;
    bool renderer_called = false;

    const std::vector<std::byte> unconfirmed_command{
        std::byte{9U}, std::byte{'x'}, std::byte{0U}};
    const goldsrc::ServiceMessageStreamDecoder decoder;
    check_error(
        decoder.decode(decompressed(unconfirmed_command)),
        goldsrc::ServiceMessageErrorCode::unsupported_service_opcode,
        unconfirmed_command.size());

    CHECK_FALSE(command_dispatch_called);
    CHECK_FALSE(shell_called);
    CHECK_FALSE(filesystem_called);
    CHECK_FALSE(renderer_called);
}

} // namespace
