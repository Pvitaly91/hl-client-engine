#pragma once

#include <hlclient/goldsrc/connect_response.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/network/network_address.hpp>

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>

namespace hlclient::goldsrc {

using ConnectResponseWaitClock = std::chrono::steady_clock;
using ConnectResponseWaitTimePoint = ConnectResponseWaitClock::time_point;

inline constexpr std::chrono::milliseconds kDefaultConnectResponseWaitTimeout{5'000};
inline constexpr std::chrono::milliseconds kMaximumConnectResponseWaitTimeout{30'000};
inline constexpr std::size_t kDefaultConnectResponseDatagramsPerUpdate = 8U;
inline constexpr std::size_t kMaximumConnectResponseDatagramsPerUpdate = 64U;
inline constexpr std::size_t kMaximumConnectResponseWaitDatagramSize =
    kMaximumConnectResponseDatagramSize;
inline constexpr std::size_t kConnectResponseWaitDiagnosticTextLimit = 256U;

struct ConnectResponseWaitConfig {
    std::chrono::milliseconds timeout{kDefaultConnectResponseWaitTimeout};
    std::size_t maximum_datagrams_per_update{kDefaultConnectResponseDatagramsPerUpdate};
    std::size_t maximum_datagram_size{kMaximumConnectResponseWaitDatagramSize};
};

enum class ConnectResponseWaitState {
    idle,
    waiting,
    accepted,
    rejected,
    timed_out,
    cancelled,
    network_error,
    protocol_error,
};

enum class ConnectResponseWaitErrorCode {
    invalid_configuration,
    time_moved_backwards,
    local_endpoint_unavailable,
    local_endpoint_changed,
    receive_failed,
    inconsistent_receive_result,
    response_truncated,
    malformed_connect_response,
    unexpected_sequenced_packet_pending_m2_3,
};

struct ConnectResponseWaitError {
    ConnectResponseWaitErrorCode code{ConnectResponseWaitErrorCode::malformed_connect_response};
    std::optional<ConnectResponseErrorCode> protocol_code;
    std::string context;
};

enum class ConnectResponseTraceClassification {
    wait_started,
    receive_would_block,
    wrong_endpoint_ignored,
    unrelated_connectionless_ignored,
    connect_accepted,
    connect_rejected,
    response_truncated,
    unexpected_sequenced_packet_pending_m2_3,
    wait_timed_out,
    wait_cancelled,
    network_failure,
    protocol_failure,
};

// Connect-response trace events contain metadata only. In particular, they do
// not own packet bytes or expose a rejection message.
struct ConnectResponseTraceEvent {
    ConnectResponseTraceClassification classification{
        ConnectResponseTraceClassification::wait_started};
    ConnectResponseWaitState state{ConnectResponseWaitState::idle};
    std::chrono::milliseconds elapsed{0};
    network::NetworkAddress endpoint;
    std::size_t datagram_size{0U};
};

using ConnectResponseTraceCallback = std::function<void(const ConnectResponseTraceEvent&)>;

class ConnectResponseWaitStage final {
public:
    ConnectResponseWaitStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        ConnectResponseWaitConfig config = {},
        ConnectResponseTraceCallback trace_callback = {});

    ConnectResponseWaitStage(const ConnectResponseWaitStage&) = delete;
    ConnectResponseWaitStage& operator=(const ConnectResponseWaitStage&) = delete;
    ConnectResponseWaitStage(ConnectResponseWaitStage&&) = delete;
    ConnectResponseWaitStage& operator=(ConnectResponseWaitStage&&) = delete;

    [[nodiscard]] bool start(
        ConnectResponseWaitTimePoint now,
        const network::NetworkAddress& expected_local_endpoint);
    void update(ConnectResponseWaitTimePoint now);
    void cancel(ConnectResponseWaitTimePoint now);

    [[nodiscard]] ConnectResponseWaitState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] const network::NetworkAddress& remote_endpoint() const noexcept;
    [[nodiscard]] const std::optional<network::NetworkAddress>& local_endpoint() const noexcept;
    [[nodiscard]] const std::optional<ConnectResponse>& response() const noexcept;
    [[nodiscard]] const std::optional<ConnectResponseWaitError>& error() const noexcept;
    [[nodiscard]] const std::optional<ConnectResponseWaitTimePoint>& deadline() const noexcept;

private:
    [[nodiscard]] bool validate_start(
        ConnectResponseWaitTimePoint now,
        const network::NetworkAddress& expected_local_endpoint);
    [[nodiscard]] bool process_received(
        const network::DatagramTransportReceiveResult& received,
        ConnectResponseWaitTimePoint now);
    void fail(
        ConnectResponseWaitState state,
        ConnectResponseWaitErrorCode code,
        std::optional<ConnectResponseErrorCode> protocol_code,
        std::string context,
        ConnectResponseWaitTimePoint now,
        ConnectResponseTraceClassification classification,
        network::NetworkAddress endpoint,
        std::size_t datagram_size);
    void emit_trace(
        ConnectResponseTraceClassification classification,
        ConnectResponseWaitTimePoint now,
        network::NetworkAddress endpoint,
        std::size_t datagram_size = 0U);
    [[nodiscard]] std::chrono::milliseconds elapsed(
        ConnectResponseWaitTimePoint now) const noexcept;

    network::IDatagramTransport& transport_;
    network::NetworkAddress remote_endpoint_;
    ConnectResponseWaitConfig config_;
    ConnectResponseTraceCallback trace_callback_;
    bool trace_callback_active_{false};
    ConnectResponseWaitState state_{ConnectResponseWaitState::idle};
    std::optional<network::NetworkAddress> local_endpoint_;
    std::optional<ConnectResponse> response_;
    std::optional<ConnectResponseWaitError> error_;
    std::optional<ConnectResponseWaitTimePoint> started_at_;
    std::optional<ConnectResponseWaitTimePoint> last_update_;
    std::optional<ConnectResponseWaitTimePoint> deadline_;
};

} // namespace hlclient::goldsrc
