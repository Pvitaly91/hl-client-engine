#pragma once

#include <hlclient/goldsrc/netchan_driver.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hlclient::goldsrc {

// Project safety limits, not claims about stock engine maxima.
inline constexpr std::size_t kDefaultMaximumServiceStringLength = 1'024U;
inline constexpr std::size_t kMaximumServiceStringLength = 4'096U;
inline constexpr std::size_t kDefaultMaximumServiceMessagesPerPayload = 64U;
inline constexpr std::size_t kMaximumServiceMessagesPerPayload = 256U;
inline constexpr std::size_t kDefaultMaximumServicePayloadSize = 65'536U;
inline constexpr std::size_t kMaximumServicePayloadSize = 1'048'576U;
inline constexpr std::size_t kMaximumServiceTextPresentationSize = 256U;
inline constexpr std::size_t kServiceMessageDiagnosticTextLimit = 256U;

struct ServiceMessageLimits {
    std::size_t maximum_string_length{kDefaultMaximumServiceStringLength};
    std::size_t maximum_messages_per_payload{
        kDefaultMaximumServiceMessagesPerPayload};
    std::size_t maximum_payload_size{kDefaultMaximumServicePayloadSize};
};

[[nodiscard]] bool valid_service_message_limits(
    const ServiceMessageLimits& limits) noexcept;

enum class ServiceMessageOpcode : std::uint8_t {
    // Neutral names intentionally describe only the stock-confirmed wire
    // layout needed by this milestone.
    text_control = 8U,
    complex_signon_boundary = 11U,
};

enum class ServiceMessageKind {
    text_control,
};

struct ServiceTextControl {
    std::string text;
};

using ServiceMessageBody = std::variant<
    std::monostate,
    ServiceTextControl>;

struct DecodedServiceMessage {
    ServiceMessageOpcode opcode{ServiceMessageOpcode::text_control};
    ServiceMessageKind kind{ServiceMessageKind::text_control};
    std::size_t byte_offset{0U};
    std::size_t byte_count{0U};
    ServiceMessageBody body;
};

struct ServiceMessageCursor {
    std::size_t byte_offset{0U};
    std::size_t remaining_byte_count{0U};
};

struct ServiceMessageBoundary {
    ServiceMessageOpcode opcode{ServiceMessageOpcode::complex_signon_boundary};
    std::size_t byte_offset{0U};
    // The opcode is consumed. This count begins at the unconsumed body.
    std::size_t remaining_byte_count{0U};
};

// This is a sign-on-layer copy/move boundary. No field refers into a driver
// receive buffer, fragment reassembler, or temporary decoder storage.
struct OwnedServicePayload {
    std::vector<std::byte> bytes;
    std::uint32_t source_sequence{0U};
    std::uint32_t source_acknowledgement{0U};
    bool source_reliable{false};
    bool reassembled{false};
    bool decompressed{false};
    bool acknowledgement_reliable{false};
    NetchanDirection direction{NetchanDirection::server_to_client};
    NetchanDriverTimePoint received_at{};
};

[[nodiscard]] OwnedServicePayload make_owned_service_payload(
    OwnedNetchanPayload&& payload) noexcept;

struct DecodedServiceStream {
    OwnedServicePayload payload;
    std::vector<DecodedServiceMessage> messages;
    std::optional<ServiceMessageBoundary> boundary;
    // On a boundary this points immediately after its opcode, before its
    // unconsumed body. Otherwise it equals the owning payload size.
    std::size_t bytes_consumed{0U};
    // One event per simple message and, when present, one boundary event.
    std::size_t required_event_count{0U};
};

enum class ServiceMessageErrorCode {
    invalid_configuration,
    payload_not_decompressed,
    empty_payload,
    payload_too_large,
    message_limit_exceeded,
    truncated_opcode,
    unsupported_service_opcode,
    unterminated_string,
    service_string_too_long,
    invalid_service_value,
    boundary_body_missing,
    size_overflow,
};

struct ServiceMessageError {
    ServiceMessageErrorCode code{ServiceMessageErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::optional<std::uint8_t> wire_opcode;
    std::string context;
};

struct ServiceMessageDecodeResult {
    std::optional<DecodedServiceStream> stream;
    std::optional<ServiceMessageError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return stream.has_value();
    }
};

class ServiceMessageStreamDecoder final {
public:
    explicit ServiceMessageStreamDecoder(ServiceMessageLimits limits = {}) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const ServiceMessageLimits& limits() const noexcept;
    [[nodiscard]] ServiceMessageDecodeResult decode(OwnedServicePayload payload) const;

private:
    ServiceMessageLimits limits_;
};

// Produces one terminal-safe line. ESC and every other non-printable byte are
// escaped, and no escape token is split at the requested presentation bound.
[[nodiscard]] std::string sanitize_service_text_for_presentation(
    std::string_view text,
    std::size_t maximum_output_size = kMaximumServiceTextPresentationSize);

[[nodiscard]] constexpr std::string_view to_string(
    const ServiceMessageOpcode opcode) noexcept
{
    switch (opcode) {
    case ServiceMessageOpcode::text_control:
        return "text_control";
    case ServiceMessageOpcode::complex_signon_boundary:
        return "complex_signon_boundary";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const ServiceMessageErrorCode code) noexcept
{
    switch (code) {
    case ServiceMessageErrorCode::invalid_configuration:
        return "invalid_configuration";
    case ServiceMessageErrorCode::payload_not_decompressed:
        return "payload_not_decompressed";
    case ServiceMessageErrorCode::empty_payload:
        return "empty_payload";
    case ServiceMessageErrorCode::payload_too_large:
        return "payload_too_large";
    case ServiceMessageErrorCode::message_limit_exceeded:
        return "message_limit_exceeded";
    case ServiceMessageErrorCode::truncated_opcode:
        return "truncated_opcode";
    case ServiceMessageErrorCode::unsupported_service_opcode:
        return "unsupported_service_opcode";
    case ServiceMessageErrorCode::unterminated_string:
        return "unterminated_string";
    case ServiceMessageErrorCode::service_string_too_long:
        return "service_string_too_long";
    case ServiceMessageErrorCode::invalid_service_value:
        return "invalid_service_value";
    case ServiceMessageErrorCode::boundary_body_missing:
        return "boundary_body_missing";
    case ServiceMessageErrorCode::size_overflow:
        return "size_overflow";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
