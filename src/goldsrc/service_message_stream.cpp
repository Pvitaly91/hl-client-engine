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

[[nodiscard]] PreResourceServiceDecodeResult pre_resource_failure(
    const PreResourceServiceErrorCode code,
    const std::size_t byte_offset,
    const std::optional<std::uint8_t> wire_opcode,
    const std::optional<ServerInfoErrorCode> server_info_code,
    std::string context)
{
    return PreResourceServiceDecodeResult{
        std::nullopt,
        PreResourceServiceError{
            code,
            byte_offset,
            wire_opcode,
            server_info_code,
            std::move(context),
        },
        0U,
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

ResourcePhaseBoundary::ResourcePhaseBoundary(
    const std::uint8_t opcode,
    const std::size_t byte_offset,
    const std::size_t remaining_byte_count,
    const ResourcePhaseBoundaryDirection direction,
    const ResourcePhaseEvidenceStatus evidence_status) noexcept
    : opcode_{opcode},
      byte_offset_{byte_offset},
      remaining_byte_count_{remaining_byte_count},
      direction_{direction},
      evidence_status_{evidence_status}
{
}

std::uint8_t ResourcePhaseBoundary::opcode() const noexcept
{
    return opcode_;
}

std::size_t ResourcePhaseBoundary::byte_offset() const noexcept
{
    return byte_offset_;
}

std::size_t ResourcePhaseBoundary::remaining_byte_count() const noexcept
{
    return remaining_byte_count_;
}

ResourcePhaseBoundaryDirection ResourcePhaseBoundary::direction() const noexcept
{
    return direction_;
}

ResourcePhaseEvidenceStatus ResourcePhaseBoundary::evidence_status() const noexcept
{
    return evidence_status_;
}

PreResourceControl::PreResourceControl(
    const std::uint8_t opcode,
    const std::size_t byte_offset,
    const std::size_t byte_count,
    const std::size_t string_length,
    const std::uint8_t control_value) noexcept
    : opcode_{opcode},
      byte_offset_{byte_offset},
      byte_count_{byte_count},
      string_length_{string_length},
      control_value_{control_value}
{
}

std::uint8_t PreResourceControl::opcode() const noexcept
{
    return opcode_;
}

std::size_t PreResourceControl::byte_offset() const noexcept
{
    return byte_offset_;
}

std::size_t PreResourceControl::byte_count() const noexcept
{
    return byte_count_;
}

std::size_t PreResourceControl::string_length() const noexcept
{
    return string_length_;
}

std::uint8_t PreResourceControl::control_value() const noexcept
{
    return control_value_;
}

PreResourceSourcePayloadMetadata::PreResourceSourcePayloadMetadata(
    const std::size_t payload_size,
    const std::uint32_t source_sequence,
    const std::uint32_t source_acknowledgement,
    const bool source_reliable,
    const bool reassembled,
    const bool decompressed,
    const bool acknowledgement_reliable,
    const NetchanDirection direction,
    const NetchanDriverTimePoint received_at,
    const std::size_t initial_boundary_offset,
    const std::size_t server_info_body_offset,
    const std::size_t server_info_body_size) noexcept
    : payload_size_{payload_size},
      source_sequence_{source_sequence},
      source_acknowledgement_{source_acknowledgement},
      source_reliable_{source_reliable},
      reassembled_{reassembled},
      decompressed_{decompressed},
      acknowledgement_reliable_{acknowledgement_reliable},
      direction_{direction},
      received_at_{received_at},
      initial_boundary_offset_{initial_boundary_offset},
      server_info_body_offset_{server_info_body_offset},
      server_info_body_size_{server_info_body_size}
{
}

std::size_t PreResourceSourcePayloadMetadata::payload_size() const noexcept
{
    return payload_size_;
}

std::uint32_t PreResourceSourcePayloadMetadata::source_sequence() const noexcept
{
    return source_sequence_;
}

std::uint32_t PreResourceSourcePayloadMetadata::source_acknowledgement() const noexcept
{
    return source_acknowledgement_;
}

bool PreResourceSourcePayloadMetadata::source_reliable() const noexcept
{
    return source_reliable_;
}

bool PreResourceSourcePayloadMetadata::reassembled() const noexcept
{
    return reassembled_;
}

bool PreResourceSourcePayloadMetadata::decompressed() const noexcept
{
    return decompressed_;
}

bool PreResourceSourcePayloadMetadata::acknowledgement_reliable() const noexcept
{
    return acknowledgement_reliable_;
}

NetchanDirection PreResourceSourcePayloadMetadata::direction() const noexcept
{
    return direction_;
}

NetchanDriverTimePoint PreResourceSourcePayloadMetadata::received_at() const noexcept
{
    return received_at_;
}

std::size_t PreResourceSourcePayloadMetadata::initial_boundary_offset() const noexcept
{
    return initial_boundary_offset_;
}

std::size_t PreResourceSourcePayloadMetadata::server_info_body_offset() const noexcept
{
    return server_info_body_offset_;
}

std::size_t PreResourceSourcePayloadMetadata::server_info_body_size() const noexcept
{
    return server_info_body_size_;
}

PreResourceSignonState::PreResourceSignonState(
    ServerInfoState server_info,
    std::vector<PreResourceControl> controls,
    ResourcePhaseBoundary boundary,
    PreResourceSourcePayloadMetadata source_payload) noexcept
    : server_info_{std::move(server_info)},
      controls_{std::move(controls)},
      boundary_{std::move(boundary)},
      source_payload_{source_payload}
{
}

const ServerInfoState& PreResourceSignonState::server_info() const noexcept
{
    return server_info_;
}

const std::vector<PreResourceControl>& PreResourceSignonState::controls() const noexcept
{
    return controls_;
}

const ResourcePhaseBoundary& PreResourceSignonState::boundary() const noexcept
{
    return boundary_;
}

const PreResourceSourcePayloadMetadata& PreResourceSignonState::source_payload() const noexcept
{
    return source_payload_;
}

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

PreResourceServiceDecodeResult ServiceMessageStreamDecoder::continue_to_pre_resource(
    const OwnedServicePayload& payload,
    const ServiceMessageBoundary& initial_boundary) const
{
    if (!valid_configuration()) {
        return pre_resource_failure(
            PreResourceServiceErrorCode::invalid_configuration,
            0U,
            std::nullopt,
            std::nullopt,
            "Service-message limits are outside project hard caps");
    }
    if (!payload.decompressed) {
        return pre_resource_failure(
            PreResourceServiceErrorCode::payload_not_decompressed,
            0U,
            std::nullopt,
            std::nullopt,
            "Pre-resource continuation requires a decompressed owning payload");
    }
    if (payload.bytes.size() > limits_.maximum_payload_size) {
        return pre_resource_failure(
            PreResourceServiceErrorCode::payload_too_large,
            limits_.maximum_payload_size,
            std::nullopt,
            std::nullopt,
            "Pre-resource service payload exceeds the configured bound");
    }
    if (initial_boundary.opcode != ServiceMessageOpcode::complex_signon_boundary) {
        return pre_resource_failure(
            PreResourceServiceErrorCode::wrong_initial_boundary_opcode,
            initial_boundary.byte_offset,
            static_cast<std::uint8_t>(initial_boundary.opcode),
            std::nullopt,
            "Continuation input is not the M2.4.1 opcode-11 boundary");
    }
    if (payload.direction != NetchanDirection::server_to_client ||
        initial_boundary.byte_offset >= payload.bytes.size()) {
        return pre_resource_failure(
            PreResourceServiceErrorCode::invalid_initial_boundary_geometry,
            initial_boundary.byte_offset,
            std::nullopt,
            std::nullopt,
            "Initial service boundary is outside the owning server payload");
    }

    const auto wire_boundary_opcode = std::to_integer<std::uint8_t>(
        payload.bytes[initial_boundary.byte_offset]);
    if (wire_boundary_opcode !=
        static_cast<std::uint8_t>(ServiceMessageOpcode::complex_signon_boundary)) {
        return pre_resource_failure(
            PreResourceServiceErrorCode::invalid_initial_boundary_geometry,
            initial_boundary.byte_offset,
            wire_boundary_opcode,
            std::nullopt,
            "Owning payload does not contain opcode 11 at the supplied boundary");
    }

    const auto body_offset = initial_boundary.byte_offset + 1U;
    const auto expected_remaining = payload.bytes.size() - body_offset;
    if (initial_boundary.remaining_byte_count != expected_remaining) {
        return pre_resource_failure(
            PreResourceServiceErrorCode::invalid_initial_boundary_geometry,
            body_offset,
            wire_boundary_opcode,
            std::nullopt,
            "Initial boundary remaining-byte count does not match the owning payload");
    }

    const ServerInfoParser parser{{limits_.maximum_string_length}};
    auto parsed = parser.parse(
        std::span<const std::byte>{payload.bytes}.subspan(body_offset));
    if (!parsed) {
        const auto relative_error = parsed.error ? parsed.error->byte_offset : 0U;
        if (relative_error > expected_remaining) {
            return pre_resource_failure(
                PreResourceServiceErrorCode::size_overflow,
                body_offset,
                wire_boundary_opcode,
                parsed.error ? std::optional{parsed.error->code} : std::nullopt,
                "Server-info parser returned an out-of-range diagnostic offset");
        }
        return pre_resource_failure(
            PreResourceServiceErrorCode::server_info_decode_failed,
            body_offset + relative_error,
            wire_boundary_opcode,
            parsed.error ? std::optional{parsed.error->code} : std::nullopt,
            parsed.error ? parsed.error->context
                         : "Server-info parser returned no state or diagnostic");
    }
    if (!parsed.state || parsed.bytes_consumed == 0U ||
        parsed.bytes_consumed > expected_remaining) {
        return pre_resource_failure(
            PreResourceServiceErrorCode::size_overflow,
            body_offset,
            wire_boundary_opcode,
            std::nullopt,
            "Server-info parser returned invalid consumption metadata");
    }

    if (limits_.maximum_messages_per_payload < 3U) {
        return pre_resource_failure(
            PreResourceServiceErrorCode::message_limit_exceeded,
            body_offset + parsed.bytes_consumed,
            std::nullopt,
            std::nullopt,
            "Pre-resource publication exceeds the configured message bound");
    }

    auto offset = body_offset + parsed.bytes_consumed;
    if (offset >= payload.bytes.size()) {
        return pre_resource_failure(
            PreResourceServiceErrorCode::missing_post_server_info_control,
            offset,
            std::nullopt,
            std::nullopt,
            "Server-info is not followed by the confirmed simple control");
    }

    const auto control_opcode = std::to_integer<std::uint8_t>(payload.bytes[offset]);
    if (control_opcode != kPreResourceSimpleControlOpcode) {
        const auto code = control_opcode ==
                                  static_cast<std::uint8_t>(
                                      ServiceMessageOpcode::complex_signon_boundary)
                              ? PreResourceServiceErrorCode::duplicate_server_info
                              : PreResourceServiceErrorCode::
                                    unsupported_post_server_info_opcode;
        return pre_resource_failure(
            code,
            offset,
            control_opcode,
            std::nullopt,
            "Unexpected service opcode follows the decoded server-info body");
    }
    if (payload.bytes.size() - offset < 3U) {
        return pre_resource_failure(
            PreResourceServiceErrorCode::truncated_post_server_info_control,
            payload.bytes.size(),
            control_opcode,
            std::nullopt,
            "Confirmed opcode-54 control body is truncated");
    }
    if (payload.bytes[offset + 1U] != std::byte{0U} ||
        payload.bytes[offset + 2U] != std::byte{0U}) {
        return pre_resource_failure(
            PreResourceServiceErrorCode::invalid_post_server_info_control,
            offset + 1U,
            control_opcode,
            std::nullopt,
            "Opcode-54 control must contain the captured empty NUL string and zero byte");
    }

    std::vector<PreResourceControl> controls;
    controls.reserve(1U);
    controls.push_back(PreResourceControl{
        control_opcode,
        offset,
        3U,
        0U,
        0U,
    });
    offset += 3U;

    if (offset >= payload.bytes.size()) {
        return pre_resource_failure(
            PreResourceServiceErrorCode::missing_pre_resource_boundary,
            offset,
            std::nullopt,
            std::nullopt,
            "Confirmed simple control is not followed by a complex boundary opcode");
    }
    const auto complex_opcode = std::to_integer<std::uint8_t>(payload.bytes[offset]);
    if (complex_opcode != kPreResourceComplexBoundaryOpcode) {
        const auto code = complex_opcode ==
                                  static_cast<std::uint8_t>(
                                      ServiceMessageOpcode::complex_signon_boundary)
                              ? PreResourceServiceErrorCode::duplicate_server_info
                              : PreResourceServiceErrorCode::
                                    unsupported_post_server_info_opcode;
        return pre_resource_failure(
            code,
            offset,
            complex_opcode,
            std::nullopt,
            "Unexpected service opcode follows the confirmed opcode-54 control");
    }

    const auto complex_offset = offset;
    ++offset;
    if (offset == payload.bytes.size()) {
        return pre_resource_failure(
            PreResourceServiceErrorCode::boundary_body_missing,
            offset,
            complex_opcode,
            std::nullopt,
            "Complex pre-resource boundary opcode has no body byte to preserve");
    }

    auto boundary = ResourcePhaseBoundary{
        complex_opcode,
        complex_offset,
        payload.bytes.size() - offset,
        ResourcePhaseBoundaryDirection::server_message,
        ResourcePhaseEvidenceStatus::
            confirmed_pre_resource_boundary_body_pending,
    };
    auto source_payload = PreResourceSourcePayloadMetadata{
        payload.bytes.size(),
        payload.source_sequence,
        payload.source_acknowledgement,
        payload.source_reliable,
        payload.reassembled,
        payload.decompressed,
        payload.acknowledgement_reliable,
        payload.direction,
        payload.received_at,
        initial_boundary.byte_offset,
        body_offset,
        parsed.bytes_consumed,
    };

    return PreResourceServiceDecodeResult{
        PreResourceSignonState{
            std::move(*parsed.state),
            std::move(controls),
            std::move(boundary),
            source_payload,
        },
        std::nullopt,
        3U,
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
