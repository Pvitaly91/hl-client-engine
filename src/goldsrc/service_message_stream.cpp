#include <hlclient/goldsrc/service_message_stream.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] ServiceMessageDecodeResult failure(
    const ServiceMessageErrorCode code,
    const std::size_t byte_offset,
    const std::optional<std::uint8_t> wire_opcode,
    std::string context)
{
    return ServiceMessageDecodeResult{
        std::nullopt,
        ServiceMessageError{code, byte_offset, wire_opcode, std::move(context)},
    };
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

bool valid_service_message_limits(const ServiceMessageLimits& limits) noexcept
{
    return limits.maximum_string_length > 0U &&
           limits.maximum_string_length <= kMaximumServiceStringLength &&
           limits.maximum_messages_per_payload > 0U &&
           limits.maximum_messages_per_payload <=
               kMaximumServiceMessagesPerPayload &&
           limits.maximum_payload_size > 0U &&
           limits.maximum_payload_size <= kMaximumServicePayloadSize;
}

OwnedServicePayload make_owned_service_payload(OwnedNetchanPayload&& payload) noexcept
{
    return OwnedServicePayload{
        std::move(payload.bytes),
        payload.source_sequence.value(),
        payload.source_acknowledgement.value(),
        payload.sequence_flags.reliable,
        payload.sequence_flags.fragmented,
        false,
        payload.acknowledgement_reliable,
        payload.direction,
        payload.received_at,
    };
}

ServiceMessageStreamDecoder::ServiceMessageStreamDecoder(
    ServiceMessageLimits limits) noexcept
    : limits_{limits}
{
}

bool ServiceMessageStreamDecoder::valid_configuration() const noexcept
{
    return valid_service_message_limits(limits_);
}

const ServiceMessageLimits& ServiceMessageStreamDecoder::limits() const noexcept
{
    return limits_;
}

ServiceMessageDecodeResult ServiceMessageStreamDecoder::decode(
    OwnedServicePayload payload) const
{
    if (!valid_configuration()) {
        return failure(
            ServiceMessageErrorCode::invalid_configuration,
            0U,
            std::nullopt,
            "Service-message limits are outside project hard caps");
    }
    if (!payload.decompressed) {
        return failure(
            ServiceMessageErrorCode::payload_not_decompressed,
            0U,
            std::nullopt,
            "Service-message decoder requires an owning decompressed payload");
    }
    if (payload.bytes.empty()) {
        return failure(
            ServiceMessageErrorCode::empty_payload,
            0U,
            std::nullopt,
            "Service-message payload is empty");
    }
    if (payload.bytes.size() > limits_.maximum_payload_size) {
        return failure(
            ServiceMessageErrorCode::payload_too_large,
            limits_.maximum_payload_size,
            std::nullopt,
            "Decompressed service-message payload exceeds the configured bound");
    }

    std::vector<DecodedServiceMessage> messages;
    messages.reserve(std::min(
        limits_.maximum_messages_per_payload,
        payload.bytes.size()));
    std::size_t offset = 0U;
    std::size_t message_count = 0U;

    while (offset < payload.bytes.size()) {
        if (message_count >= limits_.maximum_messages_per_payload) {
            return failure(
                ServiceMessageErrorCode::message_limit_exceeded,
                offset,
                std::nullopt,
                "Service-message count exceeds the configured per-payload bound");
        }

        const auto opcode_offset = offset;
        const auto wire_opcode = std::to_integer<std::uint8_t>(payload.bytes[offset]);
        ++offset;

        if (wire_opcode ==
            static_cast<std::uint8_t>(ServiceMessageOpcode::complex_signon_boundary)) {
            if (offset == payload.bytes.size()) {
                return failure(
                    ServiceMessageErrorCode::boundary_body_missing,
                    offset,
                    wire_opcode,
                    "Complex sign-on boundary opcode has no body byte to preserve");
            }
            if (messages.size() == std::numeric_limits<std::size_t>::max()) {
                return failure(
                    ServiceMessageErrorCode::size_overflow,
                    opcode_offset,
                    wire_opcode,
                    "Service-message event count overflowed");
            }

            const auto required_event_count = messages.size() + 1U;
            const auto remaining_byte_count = payload.bytes.size() - offset;
            return ServiceMessageDecodeResult{
                DecodedServiceStream{
                    std::move(payload),
                    std::move(messages),
                    ServiceMessageBoundary{
                        ServiceMessageOpcode::complex_signon_boundary,
                        opcode_offset,
                        remaining_byte_count,
                    },
                    offset,
                    required_event_count,
                },
                std::nullopt,
            };
        }

        if (wire_opcode !=
            static_cast<std::uint8_t>(ServiceMessageOpcode::text_control)) {
            return failure(
                ServiceMessageErrorCode::unsupported_service_opcode,
                opcode_offset,
                wire_opcode,
                "Unsupported service opcode appears before the confirmed boundary");
        }

        const auto string_offset = offset;
        const auto available = payload.bytes.size() - string_offset;
        const auto scan_count = std::min(
            available,
            limits_.maximum_string_length + 1U);
        std::optional<std::size_t> terminator_offset;
        for (std::size_t index = 0U; index < scan_count; ++index) {
            if (payload.bytes[string_offset + index] == std::byte{0U}) {
                terminator_offset = string_offset + index;
                break;
            }
        }

        if (!terminator_offset) {
            if (available > limits_.maximum_string_length) {
                return failure(
                    ServiceMessageErrorCode::service_string_too_long,
                    string_offset + limits_.maximum_string_length,
                    wire_opcode,
                    "Service text exceeds the configured bounded-string length");
            }
            return failure(
                ServiceMessageErrorCode::unterminated_string,
                payload.bytes.size(),
                wire_opcode,
                "Service text has no NUL terminator within the owning payload");
        }

        const auto string_size = *terminator_offset - string_offset;
        if (string_size > limits_.maximum_string_length) {
            return failure(
                ServiceMessageErrorCode::service_string_too_long,
                string_offset + limits_.maximum_string_length,
                wire_opcode,
                "Service text exceeds the configured bounded-string length");
        }

        const auto* string_data = reinterpret_cast<const char*>(
            payload.bytes.data() + string_offset);
        const auto message_end = *terminator_offset + 1U;
        messages.push_back(DecodedServiceMessage{
            ServiceMessageOpcode::text_control,
            ServiceMessageKind::text_control,
            opcode_offset,
            message_end - opcode_offset,
            ServiceTextControl{std::string{string_data, string_size}},
        });
        ++message_count;
        offset = message_end;
    }

    return ServiceMessageDecodeResult{
        DecodedServiceStream{
            std::move(payload),
            std::move(messages),
            std::nullopt,
            offset,
            message_count,
        },
        std::nullopt,
    };
}

std::string sanitize_service_text_for_presentation(
    const std::string_view text,
    const std::size_t maximum_output_size)
{
    const auto output_limit = std::min(
        maximum_output_size,
        kMaximumServiceTextPresentationSize);

    std::size_t complete_size = 0U;
    bool truncated = false;
    for (const char character : text) {
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
        for (const char character : text) {
            append_escaped(output, static_cast<unsigned char>(character));
        }
        return output;
    }

    const auto ellipsis_size = std::min<std::size_t>(3U, output_limit);
    const auto content_limit = output_limit - ellipsis_size;
    output.reserve(output_limit);
    for (const char character : text) {
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
