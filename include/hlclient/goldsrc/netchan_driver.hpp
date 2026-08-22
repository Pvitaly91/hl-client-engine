#pragma once

#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/netchan_reassembly.hpp>
#include <hlclient/goldsrc/netchan_session.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/network/network_address.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

using NetchanDriverClock = std::chrono::steady_clock;
using NetchanDriverTimePoint = NetchanDriverClock::time_point;

// These are project safety limits, not claims about stock engine maxima.
inline constexpr std::chrono::milliseconds kDefaultNetchanChannelInactivityTimeout{30'000};
inline constexpr std::chrono::milliseconds kMaximumNetchanChannelInactivityTimeout{300'000};
inline constexpr auto kDefaultNetchanFragmentTransferTimeout =
    kDefaultNetchanFragmentTimeout;
inline constexpr auto kMaximumNetchanFragmentTransferTimeout =
    kMaximumNetchanFragmentTimeout;
inline constexpr std::size_t kDefaultNetchanDriverDatagramsPerUpdate = 8U;
inline constexpr std::size_t kMaximumNetchanDriverDatagramsPerUpdate = 64U;
inline constexpr std::size_t kDefaultNetchanDriverOutgoingPacketsPerUpdate = 1U;
inline constexpr std::size_t kMaximumNetchanDriverOutgoingPacketsPerUpdate = 8U;
inline constexpr std::size_t kDefaultNetchanDriverEvents = 16U;
inline constexpr std::size_t kMaximumNetchanDriverEvents = 256U;
inline constexpr std::size_t kMinimumNetchanDriverEvents = 5U;
inline constexpr std::size_t kDefaultNetchanFragmentDatagramSize =
    kDefaultNetchanDatagramSize;
inline constexpr std::size_t kMaximumNetchanFragmentDatagramSize =
    kMaximumNetchanDatagramSize;
inline constexpr std::size_t kDefaultNetchanDriverOpaquePayloadSize =
    kDefaultMaximumNetchanNormalTransferSize;
inline constexpr std::size_t kMaximumNetchanDriverOpaquePayloadSize =
    kMaximumNetchanNormalTransferSize;
inline constexpr std::size_t kNetchanDriverDiagnosticTextLimit = 256U;

struct NetchanDriverConfig {
    std::chrono::milliseconds channel_inactivity_timeout{
        kDefaultNetchanChannelInactivityTimeout};
    std::chrono::milliseconds fragment_transfer_timeout{
        kDefaultNetchanFragmentTransferTimeout};
    std::size_t maximum_datagram_size{kDefaultNetchanDatagramSize};
    // Optional session overrides keep existing callers' datagram-derived
    // unfragmented limit and project hard-cap pending limit by default.
    // Constrained embedders can lower both without changing datagram or
    // fragment geometry.
    std::optional<std::size_t> maximum_unfragmented_reliable_payload;
    std::optional<std::size_t> maximum_pending_reliable_payload;
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
    std::size_t maximum_opaque_payload_size{kDefaultNetchanDriverOpaquePayloadSize};
    std::size_t maximum_unreliable_payload_size{
        kDefaultNetchanDatagramSize - kNetchanHeaderSize};
    std::size_t maximum_datagrams_per_update{kDefaultNetchanDriverDatagramsPerUpdate};
    std::size_t maximum_outgoing_packets_per_update{
        kDefaultNetchanDriverOutgoingPacketsPerUpdate};
    std::size_t maximum_events{kDefaultNetchanDriverEvents};
    // Compatibility façades can request an immediate cooperative yield after
    // an owning payload. Persistent drivers leave this false and consume the
    // remaining bounded RX/TX/event budget.
    bool yield_after_owning_payload{false};
};

[[nodiscard]] bool valid_configuration(const NetchanDriverConfig& config) noexcept;

enum class NetchanDriverState {
    idle,
    active,
    cancelled,
    timed_out,
    network_error,
    protocol_error,
    backpressure,
    closed,
};

enum class NetchanDriverErrorCode {
    invalid_configuration,
    not_active,
    reentrant_operation,
    time_moved_backwards,
    local_endpoint_unavailable,
    local_endpoint_changed,
    receive_failed,
    inconsistent_receive_result,
    datagram_truncated,
    unexpected_connectionless_packet,
    unsupported_special_packet,
    malformed_packet,
    invalid_sequence,
    invalid_acknowledgement,
    opaque_payload_too_large,
    packet_encode_failed,
    send_failed,
    reliable_queue_failed,
    unreliable_payload_too_large,
    unreliable_payload_pending,
    fragment_reassembly_failed,
    secondary_stream_pending_m3,
    fragment_transfer_timed_out,
    channel_inactivity_timed_out,
    event_backpressure,
};

struct NetchanDriverError {
    NetchanDriverErrorCode code{NetchanDriverErrorCode::invalid_configuration};
    std::optional<NetchanPacketErrorCode> packet_code;
    std::optional<NetchanSessionErrorCode> session_code;
    std::string context;
    std::optional<NetchanReassemblyErrorCode> reassembly_code;
};

struct NetchanDriverOperationResult {
    std::optional<NetchanDriverError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

struct OwnedNetchanPayload {
    std::vector<std::byte> bytes;
    NetchanSequence source_sequence;
    NetchanSequence source_acknowledgement;
    NetchanSequenceFlags sequence_flags;
    bool acknowledgement_reliable{false};
    NetchanDirection direction{NetchanDirection::server_to_client};
    NetchanDriverTimePoint received_at{};
};

enum class NetchanDriverEventType {
    payload_ready,
    reliable_payload_acknowledged,
    normal_transfer_started,
    normal_transfer_completed,
    normal_transfer_timed_out,
    secondary_stream_pending_m3,
    channel_timed_out,
    cancelled,
    network_error,
    protocol_error,
};

struct NetchanDriverEvent {
    NetchanDriverEventType type{NetchanDriverEventType::protocol_error};
    std::optional<OwnedNetchanPayload> payload;
    std::size_t transfer_size{0U};
    std::size_t fragment_offset{0U};
    std::size_t fragment_length{0U};
    std::size_t covered_size{0U};
    NetchanDriverTimePoint occurred_at{};
};

enum class NetchanDriverTraceClassification {
    driver_started,
    receive_would_block,
    wrong_endpoint_ignored,
    duplicate_sequence_ignored,
    older_sequence_ignored,
    sequenced_packet_received,
    fragment_received,
    normal_transfer_completed,
    normal_transfer_timed_out,
    secondary_stream_pending_m3,
    payload_ready,
    packet_sent,
    channel_timed_out,
    driver_cancelled,
    driver_closed,
    network_failure,
    protocol_failure,
};

// Metadata only. This deliberately contains no datagram, payload, file bytes,
// authentication material, or decoded sign-on data.
struct NetchanDriverTraceEvent {
    NetchanDriverTraceClassification classification{
        NetchanDriverTraceClassification::driver_started};
    NetchanDriverState state{NetchanDriverState::idle};
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

using NetchanDriverTraceCallback =
    std::function<void(const NetchanDriverTraceEvent&)>;

// A driver can own an authentication/provider lifetime without depending on
// authentication types or exposing authentication bytes to the netchan layer.
class INetchanDriverLifetime {
public:
    virtual ~INetchanDriverLifetime() = default;

    INetchanDriverLifetime(const INetchanDriverLifetime&) = delete;
    INetchanDriverLifetime& operator=(const INetchanDriverLifetime&) = delete;
    INetchanDriverLifetime(INetchanDriverLifetime&&) = delete;
    INetchanDriverLifetime& operator=(INetchanDriverLifetime&&) = delete;

protected:
    INetchanDriverLifetime() = default;
};

// The externally owned transport must outlive the driver. This reference is
// intentional: the driver must continue on the same socket that performed the
// connectionless handshake and ACCEPT exchange.
class NetchanDriver final {
public:
    NetchanDriver(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        NetchanDriverConfig config = {},
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime = {},
        NetchanDriverTraceCallback trace_callback = {});
    ~NetchanDriver();

    NetchanDriver(const NetchanDriver&) = delete;
    NetchanDriver& operator=(const NetchanDriver&) = delete;
    NetchanDriver(NetchanDriver&&) = delete;
    NetchanDriver& operator=(NetchanDriver&&) = delete;

    [[nodiscard]] bool start(
        NetchanDriverTimePoint now,
        const network::NetworkAddress& expected_local_endpoint);
    void update(NetchanDriverTimePoint now);
    void cancel(NetchanDriverTimePoint now);
    void close(NetchanDriverTimePoint now);

    [[nodiscard]] NetchanDriverOperationResult queue_reliable(
        std::span<const std::byte> payload);
    [[nodiscard]] NetchanDriverOperationResult submit_unreliable(
        std::span<const std::byte> payload);
    [[nodiscard]] std::optional<NetchanDriverEvent> poll_event();

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] NetchanDriverState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] const std::optional<NetchanDriverError>& last_error() const noexcept;
    [[nodiscard]] const network::NetworkAddress& remote_endpoint() const noexcept;
    [[nodiscard]] const std::optional<network::NetworkAddress>&
    local_endpoint() const noexcept;
    [[nodiscard]] const std::optional<NetchanDriverTimePoint>&
    last_valid_packet_time() const noexcept;
    [[nodiscard]] std::size_t pending_event_count() const noexcept;
    [[nodiscard]] std::size_t transmitted_packet_count() const noexcept;
    [[nodiscard]] std::size_t cleanup_count() const noexcept;
    [[nodiscard]] const NetchanSession& session() const noexcept;
    [[nodiscard]] const NetchanNormalReassembler& normal_reassembler() const noexcept;

private:
    friend class NetchanBootstrapStage;

    // Narrow compatibility seam for the legacy bootstrap facade. Persistent
    // driver consumers intentionally receive only the const session view.
    [[nodiscard]] NetchanSession& compatibility_session() noexcept;

    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const NetchanDriverErrorCode code) noexcept
{
    switch (code) {
    case NetchanDriverErrorCode::invalid_configuration:
        return "invalid_configuration";
    case NetchanDriverErrorCode::not_active:
        return "not_active";
    case NetchanDriverErrorCode::reentrant_operation:
        return "reentrant_operation";
    case NetchanDriverErrorCode::time_moved_backwards:
        return "time_moved_backwards";
    case NetchanDriverErrorCode::local_endpoint_unavailable:
        return "local_endpoint_unavailable";
    case NetchanDriverErrorCode::local_endpoint_changed:
        return "local_endpoint_changed";
    case NetchanDriverErrorCode::receive_failed:
        return "receive_failed";
    case NetchanDriverErrorCode::inconsistent_receive_result:
        return "inconsistent_receive_result";
    case NetchanDriverErrorCode::datagram_truncated:
        return "datagram_truncated";
    case NetchanDriverErrorCode::unexpected_connectionless_packet:
        return "unexpected_connectionless_packet";
    case NetchanDriverErrorCode::unsupported_special_packet:
        return "unsupported_special_packet";
    case NetchanDriverErrorCode::malformed_packet:
        return "malformed_packet";
    case NetchanDriverErrorCode::invalid_sequence:
        return "invalid_sequence";
    case NetchanDriverErrorCode::invalid_acknowledgement:
        return "invalid_acknowledgement";
    case NetchanDriverErrorCode::opaque_payload_too_large:
        return "opaque_payload_too_large";
    case NetchanDriverErrorCode::packet_encode_failed:
        return "packet_encode_failed";
    case NetchanDriverErrorCode::send_failed:
        return "send_failed";
    case NetchanDriverErrorCode::reliable_queue_failed:
        return "reliable_queue_failed";
    case NetchanDriverErrorCode::unreliable_payload_too_large:
        return "unreliable_payload_too_large";
    case NetchanDriverErrorCode::unreliable_payload_pending:
        return "unreliable_payload_pending";
    case NetchanDriverErrorCode::fragment_reassembly_failed:
        return "fragment_reassembly_failed";
    case NetchanDriverErrorCode::secondary_stream_pending_m3:
        return "secondary_stream_pending_m3";
    case NetchanDriverErrorCode::fragment_transfer_timed_out:
        return "fragment_transfer_timed_out";
    case NetchanDriverErrorCode::channel_inactivity_timed_out:
        return "channel_inactivity_timed_out";
    case NetchanDriverErrorCode::event_backpressure:
        return "event_backpressure";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
