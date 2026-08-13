#pragma once

#include <hlclient/goldsrc/challenge_protocol.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/network/network_address.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace hlclient::goldsrc {

using ChallengeExchangeClock = std::chrono::steady_clock;
using ChallengeExchangeTimePoint = ChallengeExchangeClock::time_point;

inline constexpr std::size_t kChallengeTracePreviewByteLimit = 128U;
inline constexpr std::size_t kChallengeDiagnosticTextLimit = 256U;
inline constexpr std::chrono::milliseconds kMaximumChallengeRetryInterval{60'000};
inline constexpr std::chrono::milliseconds kMaximumChallengeTimeout{120'000};
inline constexpr std::uint32_t kMaximumChallengeAttempts = 16U;
inline constexpr std::size_t kMaximumChallengeDatagramsPerUpdate = 64U;

enum class ChallengeExchangeState {
    idle,
    sending_request,
    waiting_for_response,
    challenge_received,
    timed_out,
    cancelled,
    network_error,
    protocol_error,
};

enum class ChallengeExchangeErrorCategory {
    invalid_configuration,
    network,
    protocol,
};

struct ChallengeExchangeError {
    ChallengeExchangeErrorCategory category{ChallengeExchangeErrorCategory::protocol};
    std::optional<ChallengeProtocolErrorCode> protocol_code;
    std::string context;
};

struct ChallengeExchangeConfig {
    std::chrono::milliseconds retry_interval{1'000};
    std::chrono::milliseconds timeout{5'000};
    std::uint32_t maximum_attempts{3U};
    std::size_t maximum_datagrams_per_update{8U};
    std::size_t maximum_datagram_size{kMaximumConnectionlessChallengeDatagramSize};
};

enum class ChallengeTraceClassification {
    exchange_started,
    request_send_started,
    request_sent,
    receive_would_block,
    wrong_endpoint_ignored,
    response_truncated,
    response_rejected,
    challenge_accepted,
    exchange_timed_out,
    exchange_cancelled,
    network_failure,
    protocol_failure,
};

struct ChallengeTraceEvent {
    ChallengeTraceClassification classification{ChallengeTraceClassification::exchange_started};
    ChallengeExchangeState state{ChallengeExchangeState::idle};
    std::chrono::milliseconds elapsed{0};
    network::NetworkAddress endpoint;
    std::size_t datagram_size{0U};
    std::uint32_t attempt{0U};
    std::string escaped_preview;
    std::string context;
};

// Trace callbacks are diagnostic observers. Exceptions and reentrant attempts to
// mutate the exchange are isolated so logging cannot alter protocol state.
using ChallengeTraceCallback = std::function<void(const ChallengeTraceEvent&)>;

[[nodiscard]] std::string escape_challenge_datagram_preview(
    std::span<const std::byte> datagram);

class ChallengeExchange final {
public:
    ChallengeExchange(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        ChallengeExchangeConfig config = {},
        ChallengeTraceCallback trace_callback = {});

    ChallengeExchange(const ChallengeExchange&) = delete;
    ChallengeExchange& operator=(const ChallengeExchange&) = delete;
    ChallengeExchange(ChallengeExchange&&) = delete;
    ChallengeExchange& operator=(ChallengeExchange&&) = delete;

    [[nodiscard]] bool start(ChallengeExchangeTimePoint now);
    void update(ChallengeExchangeTimePoint now);
    void cancel(ChallengeExchangeTimePoint now);

    [[nodiscard]] ChallengeExchangeState state() const noexcept;
    [[nodiscard]] const network::NetworkAddress& remote_endpoint() const noexcept;
    [[nodiscard]] const std::optional<network::NetworkAddress>& local_endpoint() const noexcept;
    [[nodiscard]] std::uint32_t attempts() const noexcept;
    [[nodiscard]] const std::optional<ChallengeResponse>& challenge() const noexcept;
    [[nodiscard]] const std::optional<ChallengeExchangeError>& error() const noexcept;
    [[nodiscard]] const std::optional<ChallengeExchangeTimePoint>& next_retry() const noexcept;
    [[nodiscard]] const std::optional<ChallengeExchangeTimePoint>& deadline() const noexcept;

private:
    [[nodiscard]] bool validate_start(ChallengeExchangeTimePoint now);
    [[nodiscard]] bool send_request(ChallengeExchangeTimePoint now);
    [[nodiscard]] bool process_received(
        const network::DatagramTransportReceiveResult& received,
        ChallengeExchangeTimePoint now);
    void fail(
        ChallengeExchangeState state,
        ChallengeExchangeErrorCategory category,
        std::optional<ChallengeProtocolErrorCode> protocol_code,
        std::string context,
        ChallengeExchangeTimePoint now,
        ChallengeTraceClassification classification,
        network::NetworkAddress endpoint,
        std::size_t datagram_size,
        std::span<const std::byte> preview);
    void emit_trace(
        ChallengeTraceClassification classification,
        ChallengeExchangeTimePoint now,
        network::NetworkAddress endpoint,
        std::size_t datagram_size = 0U,
        std::span<const std::byte> preview = {},
        std::string context = {});
    [[nodiscard]] std::chrono::milliseconds elapsed(ChallengeExchangeTimePoint now) const noexcept;

    network::IDatagramTransport& transport_;
    network::NetworkAddress remote_endpoint_;
    ChallengeExchangeConfig config_;
    ChallengeTraceCallback trace_callback_;
    bool trace_callback_active_{false};
    ChallengeExchangeState state_{ChallengeExchangeState::idle};
    std::optional<network::NetworkAddress> local_endpoint_;
    std::uint32_t attempts_{0U};
    std::optional<ChallengeResponse> challenge_;
    std::optional<ChallengeExchangeError> error_;
    std::optional<ChallengeExchangeTimePoint> started_at_;
    std::optional<ChallengeExchangeTimePoint> last_update_;
    std::optional<ChallengeExchangeTimePoint> next_retry_;
    std::optional<ChallengeExchangeTimePoint> deadline_;
};

} // namespace hlclient::goldsrc
