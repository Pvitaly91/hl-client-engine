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

    // A server disconnect must not be treated as a harmless skipped control.
    const std::vector<std::byte> disconnect{
        std::byte{2U}, std::byte{0U}, std::byte{11U}, std::byte{48U}};
    const auto disconnected = decoder.decode(decompressed(disconnect));
    check_error(disconnected,
                goldsrc::ServiceMessageErrorCode::unsupported_service_opcode,
                disconnect.size());
    CHECK(disconnected.error->wire_opcode == 2U);

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

TEST_CASE("Command-text and no-op controls are inert before server info",
          "[goldsrc][signon][service][security]")
{
    constexpr std::string_view command{"exec config.cfg; quit\n"};
    auto command_bytes = text_message(command);
    command_bytes.front() = std::byte{9U};
    std::vector<std::byte> fixture{std::byte{1U}};
    append(fixture, command_bytes);
    append(fixture, text_message("welcome"));
    fixture.push_back(std::byte{11U});
    fixture.push_back(std::byte{48U});
    const goldsrc::ServiceMessageStreamDecoder decoder;
    const auto decoded = decoder.decode(decompressed(fixture));
    REQUIRE(decoded);
    REQUIRE(decoded.stream->messages.size() == 3U);
    REQUIRE(decoded.stream->boundary.has_value());
    const auto& nop = decoded.stream->messages[0];
    CHECK(nop.kind == goldsrc::ServiceMessageKind::no_operation);
    CHECK(nop.byte_count == 1U);
    CHECK(std::holds_alternative<std::monostate>(nop.body));
    const auto& ignored = decoded.stream->messages[1];
    CHECK(ignored.opcode == goldsrc::ServiceMessageOpcode::command_text);
    CHECK(ignored.kind == goldsrc::ServiceMessageKind::ignored_command_text);
    CHECK(ignored.byte_offset == 1U);
    CHECK(ignored.byte_count == command.size() + 2U);
    CHECK(std::get<goldsrc::ServiceIgnoredCommandText>(ignored.body).string_length ==
          command.size());
    CHECK_FALSE(std::holds_alternative<goldsrc::ServiceTextControl>(ignored.body));
    CHECK(text_body(decoded.stream->messages[2]).text == "welcome");
    CHECK(decoded.stream->boundary->byte_offset == fixture.size() - 2U);
    CHECK(decoded.stream->required_event_count == 4U);
}

TEST_CASE("Command-only initial payload is accepted without inventing server info",
          "[goldsrc][signon][service][security]")
{
    const goldsrc::ServiceMessageStreamDecoder decoder;
    const auto decoded = decoder.decode(decompressed({
        std::byte{9U}, std::byte{0U}, std::byte{1U}}));
    REQUIRE(decoded);
    CHECK_FALSE(decoded.stream->boundary.has_value());
    REQUIRE(decoded.stream->messages.size() == 2U);
    CHECK(decoded.stream->bytes_consumed == 3U);
    CHECK(std::get<goldsrc::ServiceIgnoredCommandText>(
              decoded.stream->messages[0].body).string_length == 0U);
}

TEST_CASE("Command framing obeys string and message caps without resynchronization",
          "[goldsrc][signon][service][security][limits]")
{
    auto limits = goldsrc::ServiceMessageLimits{};
    limits.maximum_string_length = 3U;
    limits.maximum_messages_per_payload = 2U;
    const goldsrc::ServiceMessageStreamDecoder decoder{limits};
    auto command = text_message("abc");
    command.front() = std::byte{9U};
    REQUIRE(decoder.decode(decompressed(command)));
    for (std::size_t size = 1U; size < command.size(); ++size) {
        auto prefix = command;
        prefix.resize(size);
        const auto result = decoder.decode(decompressed(prefix));
        check_error(result, goldsrc::ServiceMessageErrorCode::unterminated_string,
                    prefix.size());
        CHECK(result.error->wire_opcode == 9U);
    }
    auto too_long = text_message("abcd");
    too_long.front() = std::byte{9U};
    check_error(decoder.decode(decompressed(too_long)),
                goldsrc::ServiceMessageErrorCode::service_string_too_long,
                too_long.size());
    const std::vector<std::byte> embedded_boundary{
        std::byte{9U}, std::byte{'x'}, std::byte{11U}};
    check_error(decoder.decode(decompressed(embedded_boundary)),
                goldsrc::ServiceMessageErrorCode::unterminated_string,
                embedded_boundary.size());
    const std::vector<std::byte> too_many{
        std::byte{1U}, std::byte{9U}, std::byte{0U},
        std::byte{11U}, std::byte{48U}};
    check_error(decoder.decode(decompressed(too_many)),
                goldsrc::ServiceMessageErrorCode::message_limit_exceeded,
                too_many.size());
}

TEST_CASE("Initial cvar queries retain only bounded inert metadata",
          "[goldsrc][signon][service][security][cvar]")
{
    const goldsrc::ServiceMessageStreamDecoder decoder;
    auto plain = text_message("sensitivity");
    plain.front() = std::byte{57U};
    std::vector<std::byte> identified{
        std::byte{58U}, std::byte{0xfeU}, std::byte{0xffU},
        std::byte{0xffU}, std::byte{0xffU}};
    const std::string_view name{"cl_filterstuffcmd"};
    for (const auto value : name) {
        identified.push_back(static_cast<std::byte>(value));
    }
    identified.push_back(std::byte{0U});
    auto fixture = plain;
    append(fixture, identified);
    fixture.push_back(std::byte{11U});
    fixture.push_back(std::byte{48U});
    const auto result = decoder.decode(decompressed(fixture));
    REQUIRE(result);
    REQUIRE(result.stream->messages.size() == 2U);
    REQUIRE(result.stream->boundary.has_value());
    CHECK(result.stream->required_event_count == 3U);
    const auto& first = result.stream->messages[0U];
    CHECK(first.opcode == goldsrc::ServiceMessageOpcode::cvar_query);
    CHECK(first.kind == goldsrc::ServiceMessageKind::ignored_cvar_query);
    CHECK(first.byte_count == plain.size());
    const auto& first_body = std::get<goldsrc::ServiceIgnoredCvarQuery>(first.body);
    CHECK(first_body.string_length == 11U);
    CHECK_FALSE(first_body.request_id.has_value());
    const auto& second = result.stream->messages[1U];
    CHECK(second.opcode == goldsrc::ServiceMessageOpcode::cvar_query_with_id);
    CHECK(second.byte_count == identified.size());
    CHECK(second.byte_offset == plain.size());
    const auto& second_body = std::get<goldsrc::ServiceIgnoredCvarQuery>(second.body);
    CHECK(second_body.string_length == name.size());
    CHECK(second_body.request_id == -2);
    CHECK_FALSE(std::holds_alternative<goldsrc::ServiceTextControl>(second.body));
    CHECK(result.stream->boundary->byte_offset == plain.size() + identified.size());
    const auto only_query = decoder.decode(decompressed(identified));
    REQUIRE(only_query);
    CHECK_FALSE(only_query.stream->boundary.has_value());
}

TEST_CASE("Initial cvar queries reject truncated identifiers and bounded strings",
          "[goldsrc][signon][service][security][cvar][truncation]")
{
    const goldsrc::ServiceMessageStreamDecoder decoder;
    std::vector<std::byte> fixture{
        std::byte{58U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
        std::byte{0x80U}, std::byte{'x'}, std::byte{0U}};
    for (std::size_t size = 1U; size < fixture.size(); ++size) {
        INFO("query prefix " << size);
        auto prefix = fixture;
        prefix.resize(size);
        check_error(decoder.decode(decompressed(prefix)),
                    size < 5U ? goldsrc::ServiceMessageErrorCode::truncated_control_body
                              : goldsrc::ServiceMessageErrorCode::unterminated_string,
                    size);
    }
    const auto complete = decoder.decode(decompressed(fixture));
    REQUIRE(complete);
    CHECK(std::get<goldsrc::ServiceIgnoredCvarQuery>(
        complete.stream->messages[0U].body).request_id ==
        (std::numeric_limits<std::int32_t>::min)());

    auto limits = goldsrc::ServiceMessageLimits{};
    limits.maximum_string_length = 3U;
    const goldsrc::ServiceMessageStreamDecoder limited{limits};
    for (const auto opcode : {std::byte{57U}, std::byte{58U}}) {
        auto bytes = text_message("abc");
        bytes.front() = opcode;
        if (opcode == std::byte{58U}) {
            bytes.insert(bytes.begin() + 1, 4U, std::byte{0U});
        }
        REQUIRE(limited.decode(decompressed(bytes)));
        bytes.insert(bytes.end() - 1, std::byte{'d'});
        check_error(limited.decode(decompressed(bytes)),
                    goldsrc::ServiceMessageErrorCode::service_string_too_long,
                    bytes.size());
    }
    const std::vector<std::byte> embedded_boundary{
        std::byte{57U}, std::byte{'x'}, std::byte{11U}};
    check_error(decoder.decode(decompressed(embedded_boundary)),
                goldsrc::ServiceMessageErrorCode::unterminated_string,
                embedded_boundary.size());
}

TEST_CASE("Early director messages consume an exact bounded opaque body before a real boundary",
          "[goldsrc][signon][service][director-signon][security]")
{
    const goldsrc::ServiceMessageStreamDecoder decoder;
    for(const std::size_t body_size:{0U,1U,255U}) {
        CAPTURE(body_size);
        std::vector<std::byte> fixture{std::byte{51U},static_cast<std::byte>(body_size)};
        // Literal message/opcode-looking bytes are opaque Director data, not
        // recursive service records, commands, query names or a sign-on boundary.
        const std::vector<std::byte> opaque{
            std::byte{11U},std::byte{14U},std::byte{43U},std::byte{9U},
            std::byte{'e'},std::byte{'x'},std::byte{'e'},std::byte{'c'},
            std::byte{' '},std::byte{'x'},std::byte{0U},std::byte{57U},
            std::byte{'p'},std::byte{'w'},std::byte{0U},std::byte{58U},
            std::byte{255U},std::byte{255U},std::byte{255U},std::byte{255U},std::byte{0U}};
        for(std::size_t i=0;i<body_size;++i)fixture.push_back(opaque[i%opaque.size()]);
        append(fixture,text_message("real text"));
        fixture.insert(fixture.end(),{std::byte{11U},std::byte{0xa5U},std::byte{0x5aU}});
        const auto decoded=decoder.decode(decompressed(fixture));
        REQUIRE(decoded);REQUIRE(decoded.stream->messages.size()==2U);
        const auto& director=decoded.stream->messages.front();
        CHECK(static_cast<unsigned>(director.opcode)==51U);
        CHECK(director.kind==goldsrc::ServiceMessageKind::ignored_director);
        CHECK(std::get<goldsrc::ServiceIgnoredDirector>(director.body).body_size==body_size);
        CHECK_FALSE(std::holds_alternative<goldsrc::ServiceTextControl>(director.body));
        CHECK(director.byte_offset==0U);CHECK(director.byte_count==body_size+2U);
        CHECK(text_body(decoded.stream->messages[1]).text=="real text");
        CHECK(decoded.stream->messages[1].byte_offset==body_size+2U);
        REQUIRE(decoded.stream->boundary);
        CHECK(decoded.stream->boundary->byte_offset==fixture.size()-3U);
        CHECK(decoded.stream->boundary->remaining_byte_count==2U);
        CHECK(decoded.stream->bytes_consumed==fixture.size()-2U);
        CHECK(decoded.stream->required_event_count==3U);
        CHECK(decoded.stream->payload.bytes==fixture);
        CHECK(decoded.stream->payload.source_sequence==31U);
        CHECK(decoded.stream->payload.source_acknowledgement==17U);
        CHECK(decoded.stream->payload.source_reliable);
        CHECK(decoded.stream->payload.acknowledgement_reliable);

        fixture.resize(body_size+2U);
        const auto standalone=decoder.decode(decompressed(fixture));
        REQUIRE(standalone);CHECK_FALSE(standalone.stream->boundary);
        CHECK(standalone.stream->messages.size()==1U);
        CHECK(standalone.stream->bytes_consumed==fixture.size());
    }
}

TEST_CASE("Early director truncations never scan for an embedded boundary or publish partial messages",
          "[goldsrc][signon][service][director-signon][truncation]")
{
    const goldsrc::ServiceMessageStreamDecoder decoder;
    std::vector<std::byte> complete{std::byte{1U},std::byte{51U},std::byte{255U}};
    complete.resize(258U,std::byte{11U});
    for(std::size_t size=2U;size<complete.size();++size) {
        CAPTURE(size);
        const std::vector<std::byte> prefix{complete.begin(),complete.begin()+static_cast<std::ptrdiff_t>(size)};
        const auto decoded=decoder.decode(decompressed(prefix));
        check_error(decoded,goldsrc::ServiceMessageErrorCode::truncated_control_body,size);
        CHECK(decoded.error->wire_opcode==51U);CHECK(decoded.error->byte_offset==size);
    }
    REQUIRE(decoder.decode(decompressed(complete)));
    complete.insert(complete.end(),{std::byte{254U},std::byte{11U},std::byte{48U}});
    const auto unknown=decoder.decode(decompressed(complete));
    check_error(unknown,goldsrc::ServiceMessageErrorCode::unsupported_service_opcode,complete.size());
    CHECK(unknown.error->wire_opcode==254U);CHECK(unknown.error->byte_offset==258U);
}

TEST_CASE("Early director records remain subject to message and owning payload bounds",
          "[goldsrc][signon][service][director-signon][limits]")
{
    auto limits=goldsrc::ServiceMessageLimits{};
    limits.maximum_messages_per_payload=1U;limits.maximum_string_length=1U;
    const goldsrc::ServiceMessageStreamDecoder decoder{limits};
    std::vector<std::byte> fixture{std::byte{51U},std::byte{255U}};
    fixture.resize(257U,std::byte{9U});
    REQUIRE(decoder.decode(decompressed(fixture))); // Binary body is not a string.
    fixture.push_back(std::byte{1U});
    check_error(decoder.decode(decompressed(fixture)),
        goldsrc::ServiceMessageErrorCode::message_limit_exceeded,fixture.size());
    limits.maximum_payload_size=256U;
    const goldsrc::ServiceMessageStreamDecoder bounded{limits};
    fixture.pop_back();
    check_error(bounded.decode(decompressed(fixture)),
        goldsrc::ServiceMessageErrorCode::payload_too_large,fixture.size());
}

} // namespace
