#pragma once

#include <hlclient/goldsrc/netchan_channel.hpp>
#include <hlclient/goldsrc/netchan_reassembly.hpp>
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
inline constexpr std::chrono::milliseconds kDefaultNetchanFragmentTimeout{5'000};
inline constexpr std::chrono::milliseconds kMaximumNetchanBootstrapTimeout{30'000};
inline constexpr std::size_t kDefaultNetchanBootstrapDatagramsPerUpdate = 8U;
inline constexpr std::size_t kMaximumNetchanBootstrapDatagramsPerUpdate = 64U;
inline constexpr std::size_t kDefaultNetchanBootstrapOutgoingPacketsPerUpdate = 1U;
inline constexpr std::size_t kMaximumNetchanBootstrapOutgoingPacketsPerUpdate = 8U;
inline constexpr std::size_t kNetchanBootstrapDiagnosticTextLimit = 256U;

struct NetchanBootstrapConfig {
    std::chrono::milliseconds first_packet_timeout{kDefaultNetchanFirstPacketTimeout};
    std::chrono::milliseconds fragment_completion_timeout{kDefaultNetchanFragmentTimeout};
    std::size_t maximum_datagrams_per_update{kDefaultNetchanBootstrapDatagramsPerUpdate};
    std::size_t maximum_datagram_size{kDefaultNetchanDatagramSize};
    std::size_t maximum_outgoing_packets_per_update{
        kDefaultNetchanBootstrapOutgoingPacketsPerUpdate};
    NetchanChannelLimits channel_limits{};
    NetchanNormalReassemblyLimits reassembly_limits{};
    NetchanInitialState initial_state{NetchanInitialState::stock_protocol48()};

    // The production bootstrap currently has no message-level bytes to send.
    // Tests and future upper layers may provide a bounded opaque reliable unit;
    // this stage never interprets its contents as sign-on messages.
    std::vector<std::byte> initial_reliable_payload;
};

enum class NetchanBootstrapState {
    idle,
    sending_initial_packet,
    waiting_first,
    processing,
    waiting_fragments,
    ack_pending,
    complete,
    timed_out,
    cancelled,
    network_error,
    protocol_error,
    secondary_stream_pending_m3,
};

enum class NetchanBootstrapErrorCode {
    invalid_configuration,
    time_moved_backwards,
    local_endpoint_unavailable,
    local_endpoint_changed,
    initial_reliable_payload_rejected,
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
    invalid_fragment_layout,
    fragment_reassembly_failed,
    secondary_stream_pending_m3,
};

struct NetchanBootstrapError {
    NetchanBootstrapErrorCode code{NetchanBootstrapErrorCode::invalid_configuration};
    std::optional<NetchanPacketErrorCode> packet_code;
    std::optional<NetchanChannelErrorCode> channel_code;
    std::optional<NetchanNormalReassemblyErrorCode> reassembly_code;
    std::string context;
};

struct OwnedNetchanPayload {
    std::vector<std::byte> bytes;
    NetchanSequence source_sequence;
    bool reliable{false};
    bool reassembled{false};
    std::uint16_t fragment_count{0U};
    NetchanBootstrapTimePoint received_at{};
};

struct NetchanBootstrapResult {
    OwnedNetchanPayload payload;
};

enum class NetchanBootstrapTraceClassification {
    bootstrap_started,
    initial_packet_sent,
    receive_would_block,
    wrong_endpoint_ignored,
    sequenced_packet_received,
    duplicate_sequence_ignored,
    older_sequence_ignored,
    fragment_accepted,
    fragment_duplicate_ignored,
    payload_ready,
    acknowledgement_sent,
    bootstrap_complete,
    datagram_truncated,
    secondary_stream_pending_m3,
    bootstrap_timed_out,
    bootstrap_cancelled,
    network_failure,
    protocol_failure,
};

// Metadata only: the event deliberately contains neither datagram nor payload
// bytes, authentication data, filenames, nor any decoded sign-on fields.
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
    std::size_t received_fragment_count{0U};
    std::optional<std::uint16_t> fragment_count;
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

    // start() validates same-socket continuity and sends exactly one initial
    // project probe. Receive polling starts only on a later update().
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
    [[nodiscard]] const std::optional<NetchanBootstrapTimePoint>&
    fragment_deadline() const noexcept;
    [[nodiscard]] std::size_t transmitted_packet_count() const noexcept;
    [[nodiscard]] const NetchanChannel& channel() const noexcept;

private:
    [[nodiscard]] bool validate_start(
        NetchanBootstrapTimePoint now,
        const network::NetworkAddress& expected_local_endpoint);
    [[nodiscard]] bool validate_local_continuity(NetchanBootstrapTimePoint now);
    [[nodiscard]] bool send_prepared_packet(
        const NetchanOutgoingTransaction& transaction,
        NetchanBootstrapTimePoint now,
        bool initial_packet);
    [[nodiscard]] bool process_receive_result(
        network::DatagramTransportReceiveResult received,
        NetchanBootstrapTimePoint now,
        std::size_t& outgoing_packets_this_update);
    [[nodiscard]] bool process_target_datagram(
        std::vector<std::byte> datagram,
        NetchanBootstrapTimePoint now,
        std::size_t& outgoing_packets_this_update);
    [[nodiscard]] bool send_acknowledgement(
        NetchanBootstrapTimePoint now,
        std::size_t& outgoing_packets_this_update);
    void fail(
        NetchanBootstrapState state,
        NetchanBootstrapErrorCode code,
        std::optional<NetchanPacketErrorCode> packet_code,
        std::optional<NetchanChannelErrorCode> channel_code,
        std::optional<NetchanNormalReassemblyErrorCode> reassembly_code,
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
        std::size_t payload_size = 0U,
        std::optional<std::uint16_t> fragment_count = std::nullopt);
    [[nodiscard]] std::chrono::milliseconds elapsed(
        NetchanBootstrapTimePoint now) const noexcept;

    network::IDatagramTransport& transport_;
    network::NetworkAddress remote_endpoint_;
    NetchanBootstrapConfig config_;
    NetchanBootstrapTraceCallback trace_callback_;
    bool trace_callback_active_{false};
    NetchanChannel channel_;
    NetchanNormalReassembly reassembly_;
    NetchanBootstrapState state_{NetchanBootstrapState::idle};
    std::optional<network::NetworkAddress> local_endpoint_;
    std::optional<NetchanBootstrapResult> result_;
    std::optional<NetchanBootstrapError> error_;
    std::optional<NetchanBootstrapTimePoint> started_at_;
    std::optional<NetchanBootstrapTimePoint> last_update_;
    std::optional<NetchanBootstrapTimePoint> first_packet_deadline_;
    std::optional<NetchanBootstrapTimePoint> fragment_deadline_;
    std::size_t transmitted_packet_count_{0U};
};

} // namespace hlclient::goldsrc
