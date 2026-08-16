#pragma once

#include <hlclient/goldsrc/client_message.hpp>
#include <hlclient/goldsrc/netchan_driver.hpp>
#include <hlclient/goldsrc/service_message_stream.hpp>
#include <hlclient/goldsrc/service_payload_envelope.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/network/network_address.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

class PreResourceSignonStage;

using InitialSignonClock = NetchanDriverClock;
using InitialSignonTimePoint = NetchanDriverTimePoint;

// Project safety limits. They are not claims about stock engine maxima.
inline constexpr std::size_t kDefaultMaximumInitialSignonEvents = 32U;
inline constexpr std::size_t kMaximumInitialSignonEvents = 256U;
inline constexpr std::size_t kDefaultInitialSignonDriverEventsPerUpdate = 32U;
inline constexpr std::size_t kMaximumInitialSignonDriverEventsPerUpdate = 64U;
inline constexpr std::size_t kInitialSignonDiagnosticTextLimit = 256U;

struct InitialSignonConfig {
    NetchanDriverConfig driver;
    ServicePayloadEnvelopeLimits service_payload_envelope;
    ServiceMessageLimits service_messages;
    std::size_t maximum_events{kDefaultMaximumInitialSignonEvents};
    std::size_t maximum_driver_events_per_update{
        kDefaultInitialSignonDriverEventsPerUpdate};
};

[[nodiscard]] bool valid_initial_signon_configuration(
    const InitialSignonConfig& config) noexcept;

enum class InitialSignonState {
    idle,
    waiting_for_request_transmit,
    waiting_for_request_ack,
    waiting_for_server_payload,
    decoding_service_stream,
    signon_boundary_reached,
    timed_out,
    cancelled,
    network_error,
    protocol_error,
    unsupported_service_message,
    backpressure,
    secondary_stream_pending_m3,
};

enum class InitialSignonErrorCode {
    invalid_configuration,
    driver_start_failed,
    initial_request_build_failed,
    initial_request_queue_failed,
    time_moved_backwards,
    service_payload_before_ack_overflow,
    service_payload_envelope_decode_failed,
    service_message_decode_failed,
    unsupported_service_opcode,
    service_event_backpressure,
    channel_inactivity_timed_out,
    fragment_transfer_timed_out,
    secondary_stream_pending_m3,
    driver_network_error,
    driver_protocol_error,
};

struct InitialSignonError {
    InitialSignonErrorCode code{InitialSignonErrorCode::invalid_configuration};
    std::optional<ClientMessageErrorCode> client_message_code;
    std::optional<ServiceMessageErrorCode> service_message_code;
    std::optional<NetchanDriverErrorCode> driver_code;
    std::string context;
    std::optional<ServicePayloadEnvelopeErrorCode> envelope_code;
};

struct InitialSignonResult {
    std::vector<DecodedServiceMessage> messages;
    OwnedServicePayload boundary_payload;
    ServiceMessageBoundary boundary;
    std::size_t service_payload_count{0U};
};

enum class InitialSignonEventType {
    initial_request_queued,
    initial_request_transmitted,
    initial_request_acknowledged,
    service_payload_received,
    service_message_decoded,
    signon_boundary_reached,
};

// Events deliberately carry metadata only. Server strings and the raw boundary
// remainder remain owned by InitialSignonResult and never reach a command
// dispatcher, renderer, filesystem, or terminal sink through this API.
struct InitialSignonEvent {
    InitialSignonEventType type{InitialSignonEventType::initial_request_queued};
    std::optional<ServiceMessageOpcode> opcode;
    std::size_t byte_offset{0U};
    std::size_t byte_count{0U};
    std::size_t payload_size{0U};
    InitialSignonTimePoint occurred_at{};
};

enum class InitialSignonTraceClassification {
    stage_started,
    initial_request_queued,
    initial_request_transmitted,
    initial_request_acknowledged,
    service_payload_received,
    service_message_decoded,
    signon_boundary_reached,
    stage_cancelled,
    stage_timed_out,
    secondary_stream_pending_m3,
    backpressure,
    network_failure,
    protocol_failure,
};

// Trace is also metadata-only; it never exposes request bytes, service text,
// authentication material, or the raw boundary remainder.
struct InitialSignonTraceEvent {
    InitialSignonTraceClassification classification{
        InitialSignonTraceClassification::stage_started};
    InitialSignonState state{InitialSignonState::idle};
    network::NetworkAddress endpoint;
    std::size_t request_size{0U};
    std::size_t payload_size{0U};
    std::optional<ServiceMessageOpcode> opcode;
    std::size_t byte_offset{0U};
    std::size_t byte_count{0U};
    std::size_t service_payload_count{0U};
    std::size_t transmitted_packet_count{0U};
};

using InitialSignonTraceCallback =
    std::function<void(const InitialSignonTraceEvent&)>;

// Owns the one persistent driver created immediately after connectionless
// ACCEPT. The externally owned transport must outlive the stage, which keeps
// all handshake and sign-on datagrams on the same socket and source endpoint.
class InitialSignonStage final {
public:
    InitialSignonStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        InitialSignonConfig config = {},
        InitialSignonTraceCallback trace_callback = {});
    ~InitialSignonStage();

    InitialSignonStage(const InitialSignonStage&) = delete;
    InitialSignonStage& operator=(const InitialSignonStage&) = delete;
    InitialSignonStage(InitialSignonStage&&) = delete;
    InitialSignonStage& operator=(InitialSignonStage&&) = delete;

    // start() transactionally creates and starts the same-socket driver, queues
    // the one exact typed request, then commits stage state. Retransmission is
    // exclusively a NetchanDriver/NetchanSession responsibility.
    [[nodiscard]] bool start(
        InitialSignonTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime = {});
    void update(InitialSignonTimePoint now);
    void cancel(InitialSignonTimePoint now);

    [[nodiscard]] std::optional<InitialSignonEvent> poll_event();
    [[nodiscard]] InitialSignonState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] const std::optional<InitialSignonResult>& result() const noexcept;
    [[nodiscard]] const std::optional<InitialSignonError>& error() const noexcept;
    [[nodiscard]] const network::NetworkAddress& remote_endpoint() const noexcept;
    [[nodiscard]] const std::optional<network::NetworkAddress>&
    local_endpoint() const noexcept;
    [[nodiscard]] std::size_t pending_event_count() const noexcept;
    [[nodiscard]] std::size_t transmitted_packet_count() const noexcept;
    [[nodiscard]] std::size_t cleanup_count() const noexcept;
    [[nodiscard]] std::size_t request_queue_count() const noexcept;
    [[nodiscard]] const NetchanDriver* driver() const noexcept;

private:
    friend class PreResourceSignonStage;

    // Only the owning pre-resource facade can keep the already-started driver
    // alive across the M2.4.1 semantic boundary. Ordinary callers always use
    // the public constructor above and retain the historical close-on-boundary
    // behavior.
    struct RetainConnectionAtBoundary final {};

    InitialSignonStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        InitialSignonConfig config,
        InitialSignonTraceCallback trace_callback,
        RetainConnectionAtBoundary);

    // Idempotently closes the one retained driver and its lifetime guard after
    // the friend-owned continuation reaches a terminal outcome.
    void finalize_retained_boundary(InitialSignonTimePoint now) noexcept;

    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const InitialSignonErrorCode code) noexcept
{
    switch (code) {
    case InitialSignonErrorCode::invalid_configuration:
        return "invalid_configuration";
    case InitialSignonErrorCode::driver_start_failed:
        return "driver_start_failed";
    case InitialSignonErrorCode::initial_request_build_failed:
        return "initial_request_build_failed";
    case InitialSignonErrorCode::initial_request_queue_failed:
        return "initial_request_queue_failed";
    case InitialSignonErrorCode::time_moved_backwards:
        return "time_moved_backwards";
    case InitialSignonErrorCode::service_payload_before_ack_overflow:
        return "service_payload_before_ack_overflow";
    case InitialSignonErrorCode::service_payload_envelope_decode_failed:
        return "service_payload_envelope_decode_failed";
    case InitialSignonErrorCode::service_message_decode_failed:
        return "service_message_decode_failed";
    case InitialSignonErrorCode::unsupported_service_opcode:
        return "unsupported_service_opcode";
    case InitialSignonErrorCode::service_event_backpressure:
        return "service_event_backpressure";
    case InitialSignonErrorCode::channel_inactivity_timed_out:
        return "channel_inactivity_timed_out";
    case InitialSignonErrorCode::fragment_transfer_timed_out:
        return "fragment_transfer_timed_out";
    case InitialSignonErrorCode::secondary_stream_pending_m3:
        return "secondary_stream_pending_m3";
    case InitialSignonErrorCode::driver_network_error:
        return "driver_network_error";
    case InitialSignonErrorCode::driver_protocol_error:
        return "driver_protocol_error";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
