#pragma once

#include <hlclient/goldsrc/netchan_driver.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/network/network_address.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

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
    kDefaultNetchanDriverOpaquePayloadSize;
inline constexpr std::size_t kMaximumNetchanBootstrapOpaquePayloadSize =
    kMaximumNetchanDriverOpaquePayloadSize;
inline constexpr std::size_t kMinimumNetchanBootstrapDatagramSize =
    kNetchanHeaderSize + kStockProtocol48MinimumDecodedPayloadSize;
inline constexpr std::size_t kNetchanBootstrapDiagnosticTextLimit = 256U;

struct NetchanBootstrapConfig {
    std::chrono::milliseconds first_packet_timeout{kDefaultNetchanFirstPacketTimeout};
    std::chrono::milliseconds fragment_transfer_timeout{
        kDefaultNetchanFragmentTransferTimeout};
    std::size_t maximum_datagram_size{kDefaultNetchanDatagramSize};
    std::size_t maximum_fragment_datagram_size{
        kDefaultNetchanFragmentDatagramSize};
    std::size_t maximum_fragment_payload_size{
        kDefaultMaximumNetchanFragmentPayloadSize};
    std::size_t maximum_normal_transfer_size{
        kDefaultMaximumNetchanNormalTransferSize};
    std::size_t maximum_fragments_per_transfer{
        kDefaultMaximumNetchanFragmentsPerTransfer};
    std::size_t maximum_active_normal_transfers{
        kDefaultMaximumActiveNormalTransfers};
    std::size_t maximum_fragment_ranges{
        kDefaultMaximumNetchanFragmentRanges};
    std::size_t maximum_datagrams_per_update{kDefaultNetchanBootstrapDatagramsPerUpdate};
    std::size_t maximum_outgoing_packets_per_update{
        kDefaultNetchanBootstrapOutgoingPacketsPerUpdate};
    std::size_t maximum_opaque_payload_size{kDefaultNetchanBootstrapOpaquePayloadSize};
    std::size_t maximum_events{kDefaultNetchanDriverEvents};
};

enum class NetchanBootstrapState {
    idle,
    waiting_first,
    processing,
    ack_pending,
    complete,
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
    fragment_reassembly_failed,
    secondary_stream_pending_m3,
    fragment_transfer_timed_out,
    channel_inactivity_timed_out,
    event_backpressure,
};

struct NetchanBootstrapError {
    NetchanBootstrapErrorCode code{NetchanBootstrapErrorCode::invalid_configuration};
    std::optional<NetchanPacketErrorCode> packet_code;
    std::optional<NetchanSessionErrorCode> session_code;
    std::string context;
    std::optional<NetchanReassemblyErrorCode> reassembly_code;
};

struct NetchanBootstrapResult {
    OwnedNetchanPayload payload;
};

enum class NetchanBootstrapTraceClassification {
    bootstrap_started,
    receive_would_block,
    wrong_endpoint_ignored,
    sequenced_packet_received,
    fragment_received,
    normal_transfer_completed,
    duplicate_sequence_ignored,
    older_sequence_ignored,
    payload_ready,
    acknowledgement_sent,
    bootstrap_complete,
    datagram_truncated,
    normal_transfer_timed_out,
    secondary_stream_pending_m3,
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
    std::optional<NetchanFragmentStream> fragment_stream;
    std::optional<std::uint64_t> local_transfer_id;
    std::size_t fragment_offset{0U};
    std::size_t fragment_length{0U};
    std::size_t covered_size{0U};
    std::size_t transfer_size{0U};
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
    // no datagram. The driver ACKs admitted transport units and the facade
    // completes on the first owning unfragmented or reassembled normal payload.
    [[nodiscard]] bool start(
        NetchanBootstrapTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime = {});
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
    // Narrow compatibility seam retained for existing library consumers. The
    // exact driver session becomes mutable only after the first owning payload
    // and its required acknowledgement have committed successfully.
    [[nodiscard]] NetchanSession* persistent_session() noexcept;
    [[nodiscard]] const NetchanSession* persistent_session() const noexcept;

private:
    [[nodiscard]] NetchanDriverConfig driver_config() const noexcept;
    void synchronize_from_driver(NetchanBootstrapTimePoint now);
    void fail_from_driver(NetchanBootstrapTimePoint now);
    void handle_driver_trace(const NetchanDriverTraceEvent& event);
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
    NetchanSession idle_session_;
    std::unique_ptr<NetchanDriver> driver_;
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
