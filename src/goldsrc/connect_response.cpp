#include <hlclient/goldsrc/connect_response.hpp>

#include <hlclient/goldsrc/connectionless_packet.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <system_error>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] ConnectResponseParseResult failure(
    const ConnectResponseErrorCode code,
    const std::size_t byte_offset,
    std::string context)
{
    return ConnectResponseParseResult{
        std::nullopt,
        ConnectResponseError{code, byte_offset, std::move(context)},
    };
}

[[nodiscard]] ConnectResponseParseResult payload_failure(
    const ConnectResponseErrorCode code,
    const std::size_t payload_offset,
    std::string context)
{
    return failure(
        code,
        kConnectionlessPacketHeaderSize + payload_offset,
        std::move(context));
}

[[nodiscard]] std::string_view as_text(const std::span<const std::byte> bytes) noexcept
{
    return std::string_view{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size(),
    };
}

template<class Integer>
struct DecimalFieldResult {
    Integer value{};
    std::size_t next{0U};
    bool present{false};
    bool valid{false};
    bool overflow{false};
};

template<class Integer>
[[nodiscard]] DecimalFieldResult<Integer> parse_decimal_field(
    const std::string_view text,
    const std::size_t begin) noexcept
{
    DecimalFieldResult<Integer> result;
    result.next = begin;
    if (begin >= text.size() || text[begin] < '0' || text[begin] > '9') {
        return result;
    }
    result.present = true;

    auto end = begin;
    while (end < text.size() && text[end] >= '0' && text[end] <= '9') {
        ++end;
    }
    result.next = end;
    if (end - begin > 1U && text[begin] == '0') {
        return result;
    }

    const auto converted = std::from_chars(
        text.data() + begin,
        text.data() + end,
        result.value,
        10);
    result.overflow = converted.ec == std::errc::result_out_of_range;
    result.valid = converted.ec == std::errc{} && converted.ptr == text.data() + end;
    return result;
}

[[nodiscard]] bool parse_bounded_decimal(
    const std::string_view text,
    const unsigned int maximum,
    const bool allow_zero,
    unsigned int& value) noexcept
{
    if (text.empty() || (text.size() > 1U && text.front() == '0')) {
        return false;
    }
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
    }

    unsigned int parsed = 0U;
    const auto converted = std::from_chars(
        text.data(), text.data() + text.size(), parsed, 10);
    if (converted.ec != std::errc{} || converted.ptr != text.data() + text.size() ||
        parsed > maximum || (!allow_zero && parsed == 0U)) {
        return false;
    }
    value = parsed;
    return true;
}

[[nodiscard]] std::optional<network::NetworkAddress> parse_canonical_ipv4_endpoint(
    const std::string_view endpoint) noexcept
{
    std::array<unsigned int, 4U> octets{};
    std::size_t begin = 0U;
    for (std::size_t index = 0U; index < octets.size(); ++index) {
        const char delimiter = index + 1U == octets.size() ? ':' : '.';
        const auto end = endpoint.find(delimiter, begin);
        if (end == std::string_view::npos ||
            !parse_bounded_decimal(endpoint.substr(begin, end - begin), 255U, true, octets[index])) {
            return std::nullopt;
        }
        begin = end + 1U;
    }

    unsigned int port = 0U;
    if (!parse_bounded_decimal(endpoint.substr(begin), 65'535U, false, port)) {
        return std::nullopt;
    }

    const auto address = (octets[0] << 24U) | (octets[1] << 16U) |
                         (octets[2] << 8U) | octets[3];
    return network::NetworkAddress{address, static_cast<std::uint16_t>(port)};
}

[[nodiscard]] ConnectResponseParseResult parse_accepted(const std::string_view payload)
{
    std::size_t offset = 1U;
    if (offset >= payload.size() || payload[offset] != ' ') {
        return payload_failure(
            ConnectResponseErrorCode::invalid_separator,
            offset,
            "Accepted response class must be followed by one ASCII space");
    }

    ++offset;
    const auto user_id = parse_decimal_field<std::uint32_t>(payload, offset);
    if (!user_id.present) {
        if (offset < payload.size() && payload[offset] != ' ' && payload[offset] != '\0') {
            return payload_failure(
                ConnectResponseErrorCode::invalid_user_id,
                offset,
                "Accepted response user ID is not a canonical unsigned decimal");
        }
        return payload_failure(
            ConnectResponseErrorCode::missing_user_id,
            offset,
            "Accepted response user ID is missing");
    }
    if (user_id.overflow) {
        return payload_failure(
            ConnectResponseErrorCode::user_id_overflow,
            offset,
            "Accepted response user ID exceeds the uint32 range");
    }
    if (!user_id.valid) {
        return payload_failure(
            ConnectResponseErrorCode::invalid_user_id,
            offset,
            "Accepted response user ID is not a canonical unsigned decimal");
    }
    offset = user_id.next;
    if (offset >= payload.size() || payload[offset] != ' ') {
        return payload_failure(
            ConnectResponseErrorCode::invalid_separator,
            offset,
            "Accepted response user ID must be followed by one ASCII space");
    }

    ++offset;
    if (offset >= payload.size() || payload[offset] != '"') {
        return payload_failure(
            ConnectResponseErrorCode::invalid_quote,
            offset,
            "Accepted response client endpoint must begin with an ASCII quote");
    }
    const auto endpoint_begin = ++offset;
    const auto endpoint_end = payload.find('"', endpoint_begin);
    if (endpoint_end == std::string_view::npos) {
        return payload_failure(
            ConnectResponseErrorCode::invalid_quote,
            endpoint_begin,
            "Accepted response client endpoint has no closing ASCII quote");
    }
    const auto client_endpoint = parse_canonical_ipv4_endpoint(
        payload.substr(endpoint_begin, endpoint_end - endpoint_begin));
    if (!client_endpoint) {
        return payload_failure(
            ConnectResponseErrorCode::invalid_client_endpoint,
            endpoint_begin,
            "Accepted response client endpoint is not canonical numeric IPv4 with a nonzero port");
    }
    offset = endpoint_end + 1U;
    if (offset >= payload.size() || payload[offset] != ' ') {
        return payload_failure(
            ConnectResponseErrorCode::invalid_separator,
            offset,
            "Accepted response client endpoint must be followed by one ASCII space");
    }

    ++offset;
    const auto secure = parse_decimal_field<std::uint32_t>(payload, offset);
    if (!secure.present || !secure.valid || secure.overflow || secure.value > 1U) {
        return payload_failure(
            ConnectResponseErrorCode::invalid_secure_flag,
            offset,
            "Accepted response secure flag must be canonical decimal 0 or 1");
    }
    offset = secure.next;
    if (offset >= payload.size() || payload[offset] != ' ') {
        return payload_failure(
            ConnectResponseErrorCode::invalid_separator,
            offset,
            "Accepted response secure flag must be followed by one ASCII space");
    }

    ++offset;
    const auto server_build = parse_decimal_field<std::uint32_t>(payload, offset);
    if (!server_build.present) {
        if (offset < payload.size() && payload[offset] != '\0') {
            return payload_failure(
                ConnectResponseErrorCode::invalid_server_build,
                offset,
                "Accepted response server build is not a canonical unsigned decimal");
        }
        return payload_failure(
            ConnectResponseErrorCode::missing_server_build,
            offset,
            "Accepted response server build is missing");
    }
    if (server_build.overflow) {
        return payload_failure(
            ConnectResponseErrorCode::server_build_overflow,
            offset,
            "Accepted response server build exceeds the uint32 range");
    }
    if (!server_build.valid) {
        return payload_failure(
            ConnectResponseErrorCode::invalid_server_build,
            offset,
            "Accepted response server build is not a canonical unsigned decimal");
    }
    offset = server_build.next;
    if (offset >= payload.size() || payload[offset] != '\0') {
        return payload_failure(
            ConnectResponseErrorCode::invalid_terminator,
            offset,
            "Accepted response must end with exactly one NUL byte and no LF");
    }
    ++offset;
    if (offset != payload.size()) {
        return payload_failure(
            ConnectResponseErrorCode::unexpected_trailing_data,
            offset,
            "Accepted response contains bytes after its NUL terminator");
    }

    ConnectResponse response{ConnectAccepted{
        user_id.value,
        *client_endpoint,
        secure.value != 0U,
        server_build.value,
    }};
    return ConnectResponseParseResult{std::move(response), std::nullopt};
}

[[nodiscard]] ConnectResponseParseResult parse_rejected(const std::string_view payload)
{
    const auto terminator = payload.find('\n', 1U);
    if (terminator == std::string_view::npos) {
        if (payload.size() > 1U + kMaximumConnectRejectMessageSize) {
            return payload_failure(
                ConnectResponseErrorCode::rejection_message_too_large,
                1U + kMaximumConnectRejectMessageSize,
                "Rejected response message exceeds the 512-byte bound");
        }
        return payload_failure(
            ConnectResponseErrorCode::invalid_terminator,
            payload.size(),
            "Rejected response must end with LF followed by NUL");
    }
    if (terminator == 1U) {
        return payload_failure(
            ConnectResponseErrorCode::empty_rejection_message,
            1U,
            "Rejected response message is empty");
    }
    const auto message_size = terminator - 1U;
    if (message_size > kMaximumConnectRejectMessageSize) {
        return payload_failure(
            ConnectResponseErrorCode::rejection_message_too_large,
            1U + kMaximumConnectRejectMessageSize,
            "Rejected response message exceeds the 512-byte bound");
    }
    if (terminator + 1U >= payload.size() || payload[terminator + 1U] != '\0') {
        return payload_failure(
            ConnectResponseErrorCode::invalid_terminator,
            terminator,
            "Rejected response LF must be followed by NUL");
    }
    if (terminator + 2U != payload.size()) {
        return payload_failure(
            ConnectResponseErrorCode::unexpected_trailing_data,
            terminator + 2U,
            "Rejected response contains bytes after its LF-NUL terminator");
    }

    const auto message = payload.substr(1U, message_size);
    for (std::size_t index = 0U; index < message.size(); ++index) {
        const auto value = static_cast<unsigned char>(message[index]);
        if (value == 0U || value > 0x7fU) {
            return payload_failure(
                ConnectResponseErrorCode::invalid_rejection_message,
                1U + index,
                "Rejected response message must contain bounded non-NUL ASCII bytes");
        }
    }

    ConnectResponse response{ConnectRejected{std::string{message}}};
    return ConnectResponseParseResult{std::move(response), std::nullopt};
}

[[nodiscard]] std::size_t escaped_size(const unsigned char value) noexcept
{
    if (value == '\\' || value == '\n' || value == '\r' || value == '\t') {
        return 2U;
    }
    if (value >= 0x20U && value <= 0x7eU) {
        return 1U;
    }
    return 4U;
}

void append_escaped(std::string& output, const unsigned char value)
{
    switch (value) {
    case '\\':
        output += "\\\\";
        return;
    case '\n':
        output += "\\n";
        return;
    case '\r':
        output += "\\r";
        return;
    case '\t':
        output += "\\t";
        return;
    default:
        break;
    }
    if (value >= 0x20U && value <= 0x7eU) {
        output.push_back(static_cast<char>(value));
        return;
    }

    constexpr std::string_view digits{"0123456789ABCDEF"};
    output += "\\x";
    output.push_back(digits[(value >> 4U) & 0x0fU]);
    output.push_back(digits[value & 0x0fU]);
}

} // namespace

ConnectResponseParseResult parse_connect_response(
    const std::span<const std::byte> datagram)
{
    if (datagram.size() > kMaximumConnectResponseDatagramSize) {
        return failure(
            ConnectResponseErrorCode::payload_too_large,
            kMaximumConnectResponseDatagramSize,
            "Connect response exceeds the 1024-byte project bound");
    }
    if (datagram.size() < kConnectionlessPacketHeaderSize) {
        return failure(
            ConnectResponseErrorCode::packet_too_short,
            datagram.size(),
            "Connect response is shorter than the connectionless header");
    }

    const auto envelope = parse_connectionless_packet(
        datagram,
        kMaximumConnectResponseDatagramSize);
    if (!envelope) {
        if (!envelope.error) {
            return failure(
                ConnectResponseErrorCode::packet_too_short,
                0U,
                "Connectionless parser returned no packet or diagnostic");
        }
        switch (envelope.error->code) {
        case ConnectionlessPacketErrorCode::invalid_header:
            return failure(
                ConnectResponseErrorCode::invalid_header,
                envelope.error->byte_offset,
                envelope.error->context);
        case ConnectionlessPacketErrorCode::datagram_too_large:
        case ConnectionlessPacketErrorCode::payload_too_large:
            return failure(
                ConnectResponseErrorCode::payload_too_large,
                envelope.error->byte_offset,
                envelope.error->context);
        case ConnectionlessPacketErrorCode::truncated_header:
        case ConnectionlessPacketErrorCode::empty_payload:
            return failure(
                ConnectResponseErrorCode::packet_too_short,
                envelope.error->byte_offset,
                envelope.error->context);
        }
        return failure(
            ConnectResponseErrorCode::packet_too_short,
            envelope.error->byte_offset,
            "Connectionless parser returned an unknown error code");
    }

    const auto payload = as_text(envelope.packet->payload);
    const auto response_class = envelope.packet->payload.front();
    if (response_class == kConnectAcceptedResponseClass) {
        return parse_accepted(payload);
    }
    if (response_class == kConnectRejectedResponseClass) {
        return parse_rejected(payload);
    }
    return payload_failure(
        ConnectResponseErrorCode::unknown_response_class,
        0U,
        "Connectionless packet is not a stock connect accept or reject response");
}

std::string sanitize_connect_rejection_for_presentation(
    const std::string_view message,
    const std::size_t maximum_output_size)
{
    const auto output_limit = std::min(
        maximum_output_size,
        kMaximumConnectRejectPresentationSize);

    std::size_t complete_size = 0U;
    bool truncated = false;
    for (const char character : message) {
        const auto token_size = escaped_size(static_cast<unsigned char>(character));
        if (token_size > output_limit || complete_size > output_limit - token_size) {
            truncated = true;
            break;
        }
        complete_size += token_size;
    }

    std::string output;
    if (!truncated) {
        output.reserve(complete_size);
        for (const char character : message) {
            append_escaped(output, static_cast<unsigned char>(character));
        }
        return output;
    }

    const auto ellipsis_size = std::min<std::size_t>(3U, output_limit);
    const auto content_limit = output_limit - ellipsis_size;
    output.reserve(output_limit);
    for (const char character : message) {
        const auto value = static_cast<unsigned char>(character);
        const auto token_size = escaped_size(value);
        if (token_size > content_limit || output.size() > content_limit - token_size) {
            break;
        }
        append_escaped(output, value);
    }
    output.append(ellipsis_size, '.');
    return output;
}

} // namespace hlclient::goldsrc
