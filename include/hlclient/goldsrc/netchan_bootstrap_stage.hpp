#pragma once

#include <hlclient/goldsrc/netchan_session.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/network/network_address.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace hlclient::goldsrc {

using NetchanBootstrapClock = std::chrono::steady_clock;
using NetchanBootstrapTimePoint = NetchanBootstrapClock::time_point;

inline constexpr std::chrono::milliseconds kDefaultNetchanFirstPacketTimeout{5'000};
inline constexpr std::chrono::milliseconds kMaximumNetchanBootstrapTimeout{30'000};
inline constexpr std::size_t kDefaultNetchanBootstrapDatagramsPerUpdate = 8U;
inline constexpr std::size_t kMaximumNetchanBootstrapDatagramsPerUpdate = 64U;
inline constexpr std::size_t kDefaultNetchanBootstrapOutgoingPacketsPerUpdate = 1U;
inline constexpr std::size_t kMaximumNetchanBootstrapOutgoingPacketsPerUpdate = 8U;
inline constexpr std::size_t kDefaultNetchanBootstrapOpaquePayloadSize =
    kDefaultNetchanDatagramSize - kNetchanHeaderSize;
inline constexpr std::size_t kMaximumNetchanBootstrapOpaquePayloadSize =
    kMaximumNetchanDatagramSize - kNetchanHeaderSize;
inline constexpr std::size_t kMinimumNetchanBootstrapDatagramSize =
    kNetchanHeaderSize + kStockProtocol48MinimumDecodedPayloadSize;
inline constexpr std::size_t kNetchanBootstrapDiagnosticTextLimit = 256U;

struct NetchanBootstrapConfig {
    std::chrono::milliseconds first_packet_timeout{kDefaultNetchanFirstPacketTimeout};
    std::size_t maximum_datagram_size{kDefaultNetchanDatagramSize};
    std::size_t maximum_datagrams_per_update{kDefaultNetchanBootstrapDatagramsPerUpdate};
    std::size_t maximum_outgoing_packets_per_update{
        kDefaultNetchanBootstrapOutgoingPacketsPerUpdate};
    std::size_t maximum_opaque_payload_size{kDefaultNetchanBootstrapOpaquePayloadSize};
};

enum class NetchanBootstrapState {
    idle,
    waiting_first,
    processing,
    ack_pending,
    complete,
    fragmented_payload_pending_m2_3_3,
    timed_out,
    cancelled,
    network_error,
    protocol_error,
};

enum class NetchanBootstrapErrorCode {
    invalid_configuration,
    time_moved_backwards,
    local_endpoint_unavailable,
    local_endpoint_changed,
    packet_encode_failed,
    send_failed,
    receive_failed,
    inconsistent_receive_result,
    datagram_truncated,
    unexpected_connectionless_packet,
    unsupported_special_packet,
    malformed_packet,
    invalid_sequence,
    invalid_acknowledgement,
    opaque_payload_too_large,
    fragmented_payload_pending_m2_3_3,
};

struct NetchanBootstrapError {
    NetchanBootstrapErrorCode code{NetchanBootstrapErrorCode::invalid_configuration};
    std::optional<NetchanPacketErrorCode> packet_code;
    std::optional<NetchanSessionErrorCode> session_code;
    std::string context;
};

struct OwnedNetchanPayload {
    std::vector<std::byte> bytes;
    NetchanSequence source_sequence;
    NetchanSequence source_acknowledgement;
    NetchanSequenceFlags sequence_flags;
    bool acknowledgement_reliable{false};
    NetchanDirection direction{NetchanDirection::server_to_client};
    NetchanBootstrapTimePoint received_at{};
};

struct NetchanBootstrapResult {
    OwnedNetchanPayload payload;
};

enum class NetchanBootstrapTraceClassification {
    bootstrap_started,
    receive_would_block,
    wrong_endpoint_ignored,
    sequenced_packet_received,
    duplicate_sequence_ignored,
    older_sequence_ignored,
    payload_ready,
    acknowledgement_sent,
    bootstrap_complete,
    datagram_truncated,
    fragmented_payload_pending_m2_3_3,
    bootstrap_timed_out,
    bootstrap_cancelled,
    network_failure,
    protocol_failure,
};

// Metadata only: the event deliberately contains neither datagram nor payload
// bytes, authentication data, nor decoded sign-on fields.
struct NetchanBootstrapTraceEvent {
    NetchanBootstrapTraceClassification classification{
        NetchanBootstrapTraceClassification::bootstrap_started};
    NetchanBootstrapState state{NetchanBootstrapState::idle};
    std::chrono::milliseconds elapsed{0};
    network::NetworkAddress endpoint;
    std::size_t datagram_size{0U};
    std::size_t payload_size{0U};
    std::optional<std::uint32_t> sequence;
    std::optional<std::uint32_t> acknowledgement;
    bool reliable{false};
    bool fragmented{false};
    bool reliable_acknowledgement{false};
    std::size_t transmitted_packet_count{0U};
};

using NetchanBootstrapTraceCallback =
    std::function<void(const NetchanBootstrapTraceEvent&)>;

class NetchanBootstrapStage final {
public:
    NetchanBootstrapStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        NetchanBootstrapConfig config = {},
        NetchanBootstrapTraceCallback trace_callback = {});

    NetchanBootstrapStage(const NetchanBootstrapStage&) = delete;
    NetchanBootstrapStage& operator=(const NetchanBootstrapStage&) = delete;
    NetchanBootstrapStage(NetchanBootstrapStage&&) = delete;
    NetchanBootstrapStage& operator=(NetchanBootstrapStage&&) = delete;

    // start() only validates configuration and same-socket continuity. It sends
    // no datagram; the sole M2.3.1 transmission is the ACK after one admitted
    // unfragmented server packet.
    [[nodiscard]] bool start(
        NetchanBootstrapTimePoint now,
        const network::NetworkAddress& expected_local_endpoint);
    void update(NetchanBootstrapTimePoint now);
    void cancel(NetchanBootstrapTimePoint now);

    [[nodiscard]] NetchanBootstrapState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] const network::NetworkAddress& remote_endpoint() const noexcept;
    [[nodiscard]] const std::optional<network::NetworkAddress>& local_endpoint() const noexcept;
    [[nodiscard]] const std::optional<NetchanBootstrapResult>& result() const noexcept;
    [[nodiscard]] const std::optional<NetchanBootstrapError>& error() const noexcept;
    [[nodiscard]] const std::optional<NetchanBootstrapTimePoint>&
    first_packet_deadline() const noexcept;
    [[nodiscard]] std::size_t transmitted_packet_count() const noexcept;
    [[nodiscard]] const NetchanSession& session() const noexcept;
    // Library-only continuation seam for M2.3.2. The exact bootstrap session
    // becomes mutable only after the first payload and its byte-exact ACK have
    // both committed successfully. Runtime CLI handling remains terminal at
    // the netchan-bootstrap stop point.
    [[nodiscard]] NetchanSession* persistent_session() noexcept;
    [[nodiscard]] const NetchanSession* persistent_session() const noexcept;

private:
    [[nodiscard]] bool validate_start(
        NetchanBootstrapTimePoint now,
        const network::NetworkAddress& expected_local_endpoint);
    [[nodiscard]] bool validate_local_continuity(NetchanBootstrapTimePoint now);
    [[nodiscard]] bool process_receive_result(
        network::DatagramTransportReceiveResult received,
        NetchanBootstrapTimePoint now,
        std::size_t& outgoing_packets_this_update);
    [[nodiscard]] bool process_target_datagram(
        std::vector<std::byte> datagram,
        NetchanBootstrapTimePoint now,
        std::size_t& outgoing_packets_this_update);
    [[nodiscard]] bool send_first_acknowledgement(
        NetchanBootstrapTimePoint now,
        std::size_t& outgoing_packets_this_update,
        const NetchanHeader& incoming_header,
        std::size_t payload_size);
    void fail(
        NetchanBootstrapState state,
        NetchanBootstrapErrorCode code,
        std::optional<NetchanPacketErrorCode> packet_code,
        std::optional<NetchanSessionErrorCode> session_code,
        std::string context,
        NetchanBootstrapTimePoint now,
        NetchanBootstrapTraceClassification classification,
        network::NetworkAddress endpoint,
        std::size_t datagram_size = 0U,
        const NetchanHeader* header = nullptr,
        std::size_t payload_size = 0U);
    void emit_trace(
        NetchanBootstrapTraceClassification classification,
        NetchanBootstrapTimePoint now,
        network::NetworkAddress endpoint,
        std::size_t datagram_size = 0U,
        const NetchanHeader* header = nullptr,
        std::size_t payload_size = 0U);
    [[nodiscard]] std::chrono::milliseconds elapsed(
        NetchanBootstrapTimePoint now) const noexcept;

    network::IDatagramTransport& transport_;
    network::NetworkAddress remote_endpoint_;
    NetchanBootstrapConfig config_;
    NetchanBootstrapTraceCallback trace_callback_;
    bool trace_callback_active_{false};
    NetchanSession session_;
    NetchanBootstrapState state_{NetchanBootstrapState::idle};
    std::optional<network::NetworkAddress> local_endpoint_;
    std::optional<NetchanBootstrapResult> result_;
    std::optional<NetchanBootstrapError> error_;
    std::optional<NetchanBootstrapTimePoint> started_at_;
    std::optional<NetchanBootstrapTimePoint> last_update_;
    std::optional<NetchanBootstrapTimePoint> first_packet_deadline_;
    std::size_t transmitted_packet_count_{0U};
};

} // namespace hlclient::goldsrc
