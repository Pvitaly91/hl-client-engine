#pragma once

#include <hlclient/auth/authentication_provider.hpp>
#include <hlclient/goldsrc/challenge_exchange.hpp>
#include <hlclient/goldsrc/connect_request.hpp>
#include <hlclient/goldsrc/connect_response_wait.hpp>
#include <hlclient/goldsrc/initial_signon_stage.hpp>
#include <hlclient/goldsrc/netchan_bootstrap_stage.hpp>
#include <hlclient/goldsrc/pre_resource_signon_stage.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/network/network_address.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

enum class HandshakeStopPoint {
    challenge,
    connect_request,
    connect_response,
    netchan_bootstrap,
    signon_boundary,
    pre_resource,
};

enum class ConnectRequestStageState {
    idle,
    building_request,
    request_ready,
    sending_request,
    request_sent,
    cancelled,
    configuration_error,
    network_error,
    protocol_error,
};

enum class ConnectRequestStageErrorCategory {
    configuration,
    network,
    protocol,
};

struct ConnectRequestStageError {
    ConnectRequestStageErrorCategory category{ConnectRequestStageErrorCategory::protocol};
    std::optional<ConnectRequestErrorCode> protocol_code;
    std::string context;
};

struct ConnectRequestTraceEvent {
    ConnectRequestStageState state{ConnectRequestStageState::idle};
    network::NetworkAddress endpoint;
    std::size_t datagram_size{0U};
    std::uint32_t protocol{kGoldSrcProtocolVersion};
    ChallengeToken challenge{0U};
    std::vector<std::string> protocol_info_field_names;
    std::vector<std::string> user_info_field_names;
    std::size_t protocol_info_size{0U};
    std::size_t user_info_size{0U};
    std::size_t authentication_size{0U};
    std::string context;
};

// Connect trace events expose metadata only. They intentionally contain no raw
// datagram, field values, authentication bytes, or escaped packet preview.
using ConnectRequestTraceCallback = std::function<void(const ConnectRequestTraceEvent&)>;

class ConnectRequestStage final {
public:
    ConnectRequestStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        PreparedConnectRequest prepared_request,
        ConnectRequestTraceCallback trace_callback = {});

    ConnectRequestStage(const ConnectRequestStage&) = delete;
    ConnectRequestStage& operator=(const ConnectRequestStage&) = delete;
    ConnectRequestStage(ConnectRequestStage&&) = delete;
    ConnectRequestStage& operator=(ConnectRequestStage&&) = delete;

    [[nodiscard]] bool start(
        const ChallengeResponse& challenge,
        const network::NetworkAddress& expected_local_endpoint);
    void cancel();

    [[nodiscard]] ConnectRequestStageState state() const noexcept;
    [[nodiscard]] const network::NetworkAddress& remote_endpoint() const noexcept;
    [[nodiscard]] const std::optional<network::NetworkAddress>& local_endpoint() const noexcept;
    [[nodiscard]] std::size_t send_attempts() const noexcept;
    [[nodiscard]] const std::optional<ConnectRequestStageError>& error() const noexcept;

private:
    void fail(
        ConnectRequestStageState state,
        ConnectRequestStageErrorCategory category,
        std::optional<ConnectRequestErrorCode> protocol_code,
        std::string context);
    void emit_trace(
        ChallengeToken challenge,
        std::size_t datagram_size,
        std::string context = {});

    network::IDatagramTransport& transport_;
    network::NetworkAddress remote_endpoint_;
    std::optional<PreparedConnectRequest> prepared_request_;
    ConnectRequestTraceCallback trace_callback_;
    bool trace_callback_active_{false};
    ConnectRequestStageState state_{ConnectRequestStageState::idle};
    std::optional<network::NetworkAddress> local_endpoint_;
    std::size_t send_attempts_{0U};
    std::optional<ConnectRequestStageError> error_;
};

enum class GoldSrcHandshakeState {
    idle,
    waiting_for_challenge,
    challenge_received,
    building_request,
    request_ready,
    sending_request,
    request_sent,
    waiting_for_connect_response,
    accepted,
    rejected,
    connect_response_timed_out,
    waiting_for_netchan,
    netchan_bootstrap_complete,
    netchan_timed_out,
    waiting_for_signon,
    signon_boundary_reached,
    signon_timed_out,
    signon_unsupported_service,
    signon_backpressure,
    signon_secondary_stream_pending_m3,
    waiting_for_pre_resource,
    pre_resource_boundary_reached,
    pre_resource_timed_out,
    pre_resource_unsupported_message,
    pre_resource_backpressure,
    pre_resource_secondary_stream_pending_m3,
    timed_out,
    cancelled,
    configuration_error,
    network_error,
    protocol_error,
};

class GoldSrcHandshakeCoordinator final {
public:
    GoldSrcHandshakeCoordinator(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        HandshakeStopPoint stop_point,
        std::optional<PreparedConnectRequest> prepared_request,
        ChallengeExchangeConfig challenge_config = {},
        ChallengeTraceCallback challenge_trace_callback = {},
        ConnectRequestTraceCallback connect_trace_callback = {},
        ConnectResponseWaitConfig response_config = {},
        ConnectResponseTraceCallback response_trace_callback = {},
        std::optional<auth::AuthenticationSession> authentication_session = std::nullopt,
        NetchanBootstrapConfig netchan_config = {},
        NetchanBootstrapTraceCallback netchan_trace_callback = {},
        InitialSignonConfig signon_config = {},
        InitialSignonTraceCallback signon_trace_callback = {},
        PreResourceSignonConfig pre_resource_config = {},
        PreResourceSignonTraceCallback pre_resource_trace_callback = {});

    GoldSrcHandshakeCoordinator(const GoldSrcHandshakeCoordinator&) = delete;
    GoldSrcHandshakeCoordinator& operator=(const GoldSrcHandshakeCoordinator&) = delete;
    GoldSrcHandshakeCoordinator(GoldSrcHandshakeCoordinator&&) = delete;
    GoldSrcHandshakeCoordinator& operator=(GoldSrcHandshakeCoordinator&&) = delete;

    [[nodiscard]] bool start(ChallengeExchangeTimePoint now);
    void update(ChallengeExchangeTimePoint now);
    void cancel(ChallengeExchangeTimePoint now);

    [[nodiscard]] GoldSrcHandshakeState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] HandshakeStopPoint stop_point() const noexcept;
    [[nodiscard]] const std::optional<ChallengeResponse>& challenge() const noexcept;
    [[nodiscard]] const std::optional<ConnectResponse>& connect_response() const noexcept;
    [[nodiscard]] const std::optional<NetchanBootstrapResult>&
    netchan_bootstrap_result() const noexcept;
    [[nodiscard]] const std::optional<InitialSignonResult>&
    initial_signon_result() const noexcept;
    [[nodiscard]] const std::optional<InitialSignonError>&
    initial_signon_error() const noexcept;
    [[nodiscard]] const std::optional<PreResourceSignonState>&
    pre_resource_result() const noexcept;
    [[nodiscard]] const std::optional<PreResourceSignonError>&
    pre_resource_error() const noexcept;
    // Non-null only after a successful netchan bootstrap. The returned object
    // is the same session that committed the M2.3.3 bootstrap ACKs; callers must use
    // the coordinator's original externally-owned datagram transport.
    [[nodiscard]] NetchanSession* netchan_session() noexcept;
    [[nodiscard]] const NetchanSession* netchan_session() const noexcept;
    [[nodiscard]] const std::optional<network::NetworkAddress>& local_endpoint() const noexcept;
    [[nodiscard]] std::size_t connect_send_attempts() const noexcept;
    [[nodiscard]] std::string_view error_context() const noexcept;

private:
    void synchronize_from_challenge(ChallengeExchangeTimePoint now);
    void synchronize_from_response(ChallengeExchangeTimePoint now);
    void synchronize_from_netchan();
    void synchronize_from_signon();
    void synchronize_from_pre_resource();
    void release_authentication_session_if_terminal();

    HandshakeStopPoint stop_point_;
    ChallengeExchange challenge_exchange_;
    std::optional<ConnectRequestStage> connect_stage_;
    std::optional<ConnectResponseWaitStage> response_stage_;
    std::optional<NetchanBootstrapStage> netchan_stage_;
    std::optional<InitialSignonStage> signon_stage_;
    std::optional<PreResourceSignonStage> pre_resource_stage_;
    std::optional<auth::AuthenticationSession> authentication_session_;
    GoldSrcHandshakeState state_{GoldSrcHandshakeState::idle};
    std::string configuration_error_;
};

} // namespace hlclient::goldsrc
