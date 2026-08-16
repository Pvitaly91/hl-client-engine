#pragma once

#include <hlclient/goldsrc/netchan_driver.hpp>
#include <hlclient/goldsrc/server_info.hpp>

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

// Opcode values 54 and 14 are intentionally not assigned third-party semantic
// names. Differential stock captures confirm only their exact local layouts
// and order at this M2.4.2 boundary.
inline constexpr std::uint8_t kPreResourceSimpleControlOpcode = 54U;
inline constexpr std::uint8_t kPreResourceComplexBoundaryOpcode = 14U;

enum class ResourcePhaseBoundaryDirection {
    server_message,
    client_request_required,
};

enum class ResourcePhaseEvidenceStatus {
    // The boundary and message order are stock-confirmed under category C.
    // The opcode's semantic name and its body remain pending and untouched.
    confirmed_pre_resource_boundary_body_pending,
};

class ResourcePhaseBoundary final {
public:
    ResourcePhaseBoundary(const ResourcePhaseBoundary&) = default;
    ResourcePhaseBoundary& operator=(const ResourcePhaseBoundary&) = default;
    ResourcePhaseBoundary(ResourcePhaseBoundary&&) noexcept = default;
    ResourcePhaseBoundary& operator=(ResourcePhaseBoundary&&) noexcept = default;
    ~ResourcePhaseBoundary() = default;

    [[nodiscard]] std::uint8_t opcode() const noexcept;
    [[nodiscard]] std::size_t byte_offset() const noexcept;
    [[nodiscard]] std::size_t remaining_byte_count() const noexcept;
    [[nodiscard]] ResourcePhaseBoundaryDirection direction() const noexcept;
    [[nodiscard]] ResourcePhaseEvidenceStatus evidence_status() const noexcept;

private:
    friend class ServiceMessageStreamDecoder;

    ResourcePhaseBoundary(
        std::uint8_t opcode,
        std::size_t byte_offset,
        std::size_t remaining_byte_count,
        ResourcePhaseBoundaryDirection direction,
        ResourcePhaseEvidenceStatus evidence_status) noexcept;

    std::uint8_t opcode_{0U};
    std::size_t byte_offset_{0U};
    std::size_t remaining_byte_count_{0U};
    ResourcePhaseBoundaryDirection direction_{
        ResourcePhaseBoundaryDirection::server_message};
    ResourcePhaseEvidenceStatus evidence_status_{
        ResourcePhaseEvidenceStatus::
            confirmed_pre_resource_boundary_body_pending};
};

class PreResourceControl final {
public:
    PreResourceControl(const PreResourceControl&) = default;
    PreResourceControl& operator=(const PreResourceControl&) = default;
    PreResourceControl(PreResourceControl&&) noexcept = default;
    PreResourceControl& operator=(PreResourceControl&&) noexcept = default;
    ~PreResourceControl() = default;

    [[nodiscard]] std::uint8_t opcode() const noexcept;
    [[nodiscard]] std::size_t byte_offset() const noexcept;
    [[nodiscard]] std::size_t byte_count() const noexcept;
    [[nodiscard]] std::size_t string_length() const noexcept;
    [[nodiscard]] std::uint8_t control_value() const noexcept;

private:
    friend class ServiceMessageStreamDecoder;

    PreResourceControl(
        std::uint8_t opcode,
        std::size_t byte_offset,
        std::size_t byte_count,
        std::size_t string_length,
        std::uint8_t control_value) noexcept;

    std::uint8_t opcode_{0U};
    std::size_t byte_offset_{0U};
    std::size_t byte_count_{0U};
    std::size_t string_length_{0U};
    std::uint8_t control_value_{0U};
};

class PreResourceSourcePayloadMetadata final {
public:
    PreResourceSourcePayloadMetadata(
        std::size_t payload_size,
        std::uint32_t source_sequence,
        std::uint32_t source_acknowledgement,
        bool source_reliable,
        bool reassembled,
        bool decompressed,
        bool acknowledgement_reliable,
        NetchanDirection direction,
        NetchanDriverTimePoint received_at,
        std::size_t initial_boundary_offset,
        std::size_t server_info_body_offset,
        std::size_t server_info_body_size) noexcept;

    [[nodiscard]] std::size_t payload_size() const noexcept;
    [[nodiscard]] std::uint32_t source_sequence() const noexcept;
    [[nodiscard]] std::uint32_t source_acknowledgement() const noexcept;
    [[nodiscard]] bool source_reliable() const noexcept;
    [[nodiscard]] bool reassembled() const noexcept;
    [[nodiscard]] bool decompressed() const noexcept;
    [[nodiscard]] bool acknowledgement_reliable() const noexcept;
    [[nodiscard]] NetchanDirection direction() const noexcept;
    [[nodiscard]] NetchanDriverTimePoint received_at() const noexcept;
    [[nodiscard]] std::size_t initial_boundary_offset() const noexcept;
    [[nodiscard]] std::size_t server_info_body_offset() const noexcept;
    [[nodiscard]] std::size_t server_info_body_size() const noexcept;

private:
    std::size_t payload_size_{0U};
    std::uint32_t source_sequence_{0U};
    std::uint32_t source_acknowledgement_{0U};
    bool source_reliable_{false};
    bool reassembled_{false};
    bool decompressed_{false};
    bool acknowledgement_reliable_{false};
    NetchanDirection direction_{NetchanDirection::server_to_client};
    NetchanDriverTimePoint received_at_{};
    std::size_t initial_boundary_offset_{0U};
    std::size_t server_info_body_offset_{0U};
    std::size_t server_info_body_size_{0U};
};

// Fully owning, immutable semantic result. It stores no raw payload pointer,
// socket, filesystem handle, renderer object, or resource data.
class PreResourceSignonState final {
public:
    PreResourceSignonState(const PreResourceSignonState&) = default;
    PreResourceSignonState& operator=(const PreResourceSignonState&) = delete;
    PreResourceSignonState(PreResourceSignonState&&) noexcept = default;
    PreResourceSignonState& operator=(PreResourceSignonState&&) noexcept = delete;
    ~PreResourceSignonState() = default;

    [[nodiscard]] const ServerInfoState& server_info() const noexcept;
    [[nodiscard]] const std::vector<PreResourceControl>& controls() const noexcept;
    [[nodiscard]] const ResourcePhaseBoundary& boundary() const noexcept;
    [[nodiscard]] const PreResourceSourcePayloadMetadata& source_payload() const noexcept;

private:
    friend class ServiceMessageStreamDecoder;

    PreResourceSignonState(
        ServerInfoState server_info,
        std::vector<PreResourceControl> controls,
        ResourcePhaseBoundary boundary,
        PreResourceSourcePayloadMetadata source_payload) noexcept;

    ServerInfoState server_info_;
    std::vector<PreResourceControl> controls_;
    ResourcePhaseBoundary boundary_;
    PreResourceSourcePayloadMetadata source_payload_;
};

enum class PreResourceServiceErrorCode {
    invalid_configuration,
    payload_not_decompressed,
    payload_too_large,
    wrong_initial_boundary_opcode,
    invalid_initial_boundary_geometry,
    server_info_decode_failed,
    missing_post_server_info_control,
    truncated_post_server_info_control,
    invalid_post_server_info_control,
    missing_pre_resource_boundary,
    duplicate_server_info,
    unsupported_post_server_info_opcode,
    message_limit_exceeded,
    boundary_body_missing,
    size_overflow,
};

struct PreResourceServiceError {
    PreResourceServiceErrorCode code{
        PreResourceServiceErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::optional<std::uint8_t> wire_opcode;
    std::optional<ServerInfoErrorCode> server_info_code;
    std::string context;
};

struct PreResourceServiceDecodeResult {
    std::optional<PreResourceSignonState> state;
    std::optional<PreResourceServiceError> error;
    // One server-info-ready event, one event per confirmed simple control,
    // and one boundary event. Zero on failure.
    std::size_t required_event_count{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

class ServiceMessageStreamDecoder final {
public:
    explicit ServiceMessageStreamDecoder(ServiceMessageLimits limits = {}) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const ServiceMessageLimits& limits() const noexcept;
    [[nodiscard]] ServiceMessageDecodeResult decode(OwnedServicePayload payload) const;

    // Continues at the exact owning M2.4.1 boundary. It does not repeat
    // envelope decoding or opcode-8 parsing, never scans for an opcode, and
    // leaves the first complex post-control message body untouched.
    [[nodiscard]] PreResourceServiceDecodeResult continue_to_pre_resource(
        const OwnedServicePayload& payload,
        const ServiceMessageBoundary& initial_boundary) const;

private:
    ServiceMessageLimits limits_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const ResourcePhaseBoundaryDirection direction) noexcept
{
    switch (direction) {
    case ResourcePhaseBoundaryDirection::server_message:
        return "server_message";
    case ResourcePhaseBoundaryDirection::client_request_required:
        return "client_request_required";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const ResourcePhaseEvidenceStatus status) noexcept
{
    switch (status) {
    case ResourcePhaseEvidenceStatus::
        confirmed_pre_resource_boundary_body_pending:
        return "confirmed_pre_resource_boundary_body_pending";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const PreResourceServiceErrorCode code) noexcept
{
    switch (code) {
    case PreResourceServiceErrorCode::invalid_configuration:
        return "invalid_configuration";
    case PreResourceServiceErrorCode::payload_not_decompressed:
        return "payload_not_decompressed";
    case PreResourceServiceErrorCode::payload_too_large:
        return "payload_too_large";
    case PreResourceServiceErrorCode::wrong_initial_boundary_opcode:
        return "wrong_initial_boundary_opcode";
    case PreResourceServiceErrorCode::invalid_initial_boundary_geometry:
        return "invalid_initial_boundary_geometry";
    case PreResourceServiceErrorCode::server_info_decode_failed:
        return "server_info_decode_failed";
    case PreResourceServiceErrorCode::missing_post_server_info_control:
        return "missing_post_server_info_control";
    case PreResourceServiceErrorCode::truncated_post_server_info_control:
        return "truncated_post_server_info_control";
    case PreResourceServiceErrorCode::invalid_post_server_info_control:
        return "invalid_post_server_info_control";
    case PreResourceServiceErrorCode::missing_pre_resource_boundary:
        return "missing_pre_resource_boundary";
    case PreResourceServiceErrorCode::duplicate_server_info:
        return "duplicate_server_info";
    case PreResourceServiceErrorCode::unsupported_post_server_info_opcode:
        return "unsupported_post_server_info_opcode";
    case PreResourceServiceErrorCode::message_limit_exceeded:
        return "message_limit_exceeded";
    case PreResourceServiceErrorCode::boundary_body_missing:
        return "boundary_body_missing";
    case PreResourceServiceErrorCode::size_overflow:
        return "size_overflow";
    }
    return "unknown";
}

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
