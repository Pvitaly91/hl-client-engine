#include <hlclient/goldsrc/connect_request_stage.hpp>

#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool stage_terminal(const ConnectRequestStageState state) noexcept
{
    switch (state) {
    case ConnectRequestStageState::request_sent:
    case ConnectRequestStageState::cancelled:
    case ConnectRequestStageState::configuration_error:
    case ConnectRequestStageState::network_error:
    case ConnectRequestStageState::protocol_error:
        return true;
    case ConnectRequestStageState::idle:
    case ConnectRequestStageState::building_request:
    case ConnectRequestStageState::request_ready:
    case ConnectRequestStageState::sending_request:
        return false;
    }
    return true;
}

[[nodiscard]] std::vector<std::string> field_names(const InfoString& info)
{
    std::vector<std::string> names;
    names.reserve(info.entries().size());
    for (const auto& entry : info.entries()) {
        names.push_back(entry.key);
    }
    return names;
}

[[nodiscard]] GoldSrcHandshakeState map_challenge_state(
    const ChallengeExchangeState state) noexcept
{
    switch (state) {
    case ChallengeExchangeState::idle:
        return GoldSrcHandshakeState::idle;
    case ChallengeExchangeState::sending_request:
    case ChallengeExchangeState::waiting_for_response:
        return GoldSrcHandshakeState::waiting_for_challenge;
    case ChallengeExchangeState::challenge_received:
        return GoldSrcHandshakeState::challenge_received;
    case ChallengeExchangeState::timed_out:
        return GoldSrcHandshakeState::timed_out;
    case ChallengeExchangeState::cancelled:
        return GoldSrcHandshakeState::cancelled;
    case ChallengeExchangeState::network_error:
        return GoldSrcHandshakeState::network_error;
    case ChallengeExchangeState::protocol_error:
        return GoldSrcHandshakeState::protocol_error;
    }
    return GoldSrcHandshakeState::protocol_error;
}

[[nodiscard]] GoldSrcHandshakeState map_connect_state(
    const ConnectRequestStageState state) noexcept
{
    switch (state) {
    case ConnectRequestStageState::idle:
        return GoldSrcHandshakeState::challenge_received;
    case ConnectRequestStageState::building_request:
        return GoldSrcHandshakeState::building_request;
    case ConnectRequestStageState::request_ready:
        return GoldSrcHandshakeState::request_ready;
    case ConnectRequestStageState::sending_request:
        return GoldSrcHandshakeState::sending_request;
    case ConnectRequestStageState::request_sent:
        return GoldSrcHandshakeState::request_sent;
    case ConnectRequestStageState::cancelled:
        return GoldSrcHandshakeState::cancelled;
    case ConnectRequestStageState::configuration_error:
        return GoldSrcHandshakeState::configuration_error;
    case ConnectRequestStageState::network_error:
        return GoldSrcHandshakeState::network_error;
    case ConnectRequestStageState::protocol_error:
        return GoldSrcHandshakeState::protocol_error;
    }
    return GoldSrcHandshakeState::protocol_error;
}

} // namespace

ConnectRequestStage::ConnectRequestStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    PreparedConnectRequest prepared_request,
    ConnectRequestTraceCallback trace_callback)
    : transport_{transport},
      remote_endpoint_{remote_endpoint},
      prepared_request_{std::move(prepared_request)},
      trace_callback_{std::move(trace_callback)}
{
}

bool ConnectRequestStage::start(
    const ChallengeResponse& challenge,
    const network::NetworkAddress& expected_local_endpoint)
{
    if (trace_callback_active_ || state_ != ConnectRequestStageState::idle) {
        return false;
    }
    if (!prepared_request_ || remote_endpoint_.ipv4_host_order() == 0U ||
        remote_endpoint_.port() == 0U || expected_local_endpoint.port() == 0U) {
        fail(
            ConnectRequestStageState::configuration_error,
            ConnectRequestStageErrorCategory::configuration,
            std::nullopt,
            "Connect stage configuration or endpoint is invalid");
        return false;
    }

    auto local = transport_.local_address();
    if (!local || !local.address || *local.address != expected_local_endpoint) {
        fail(
            ConnectRequestStageState::network_error,
            ConnectRequestStageErrorCategory::network,
            std::nullopt,
            local.error.empty()
                ? "Datagram transport local endpoint changed after challenge"
                : local.error);
        return false;
    }
    local_endpoint_ = *local.address;

    const auto protocol_info_names = field_names(prepared_request_->protocol_info().value());
    const auto user_info_names = field_names(prepared_request_->user_info().value());
    const auto protocol_info_size = prepared_request_->protocol_info_wire_size();
    const auto user_info_size = prepared_request_->user_info().value().serialized_size();
    const auto authentication_size = prepared_request_->authentication_size();
    const auto profile = prepared_request_->profile();

    state_ = ConnectRequestStageState::building_request;
    emit_trace(challenge.challenge, 0U);
    auto request = std::move(*prepared_request_).make_request(challenge.challenge);
    prepared_request_.reset();
    auto built = ConnectRequestBuilder::build(request, profile);
    if (!built) {
        fail(
            ConnectRequestStageState::protocol_error,
            ConnectRequestStageErrorCategory::protocol,
            built.error ? std::optional{built.error->code} : std::nullopt,
            built.error ? built.error->context : "Unable to build connect request");
        return false;
    }

    state_ = ConnectRequestStageState::request_ready;
    emit_trace(challenge.challenge, built.datagram->size());
    state_ = ConnectRequestStageState::sending_request;
    emit_trace(challenge.challenge, built.datagram->size());

    ++send_attempts_;
    auto sent = transport_.send_to(remote_endpoint_, *built.datagram);
    if (!sent) {
        fail(
            ConnectRequestStageState::network_error,
            ConnectRequestStageErrorCategory::network,
            std::nullopt,
            sent.error.empty() ? "Unable to send the one-shot connect request" : sent.error);
        return false;
    }

    state_ = ConnectRequestStageState::request_sent;
    if (trace_callback_ && !trace_callback_active_) {
        ConnectRequestTraceEvent event{
            state_,
            remote_endpoint_,
            built.datagram->size(),
            kGoldSrcProtocolVersion,
            challenge.challenge,
            protocol_info_names,
            user_info_names,
            protocol_info_size,
            user_info_size,
            authentication_size,
            "Connect request sent exactly once; server acceptance was not evaluated",
        };
        trace_callback_active_ = true;
        try {
            trace_callback_(event);
        } catch (...) {
        }
        trace_callback_active_ = false;
    }
    return true;
}

void ConnectRequestStage::cancel()
{
    if (trace_callback_active_ || state_ == ConnectRequestStageState::idle ||
        stage_terminal(state_)) {
        return;
    }
    state_ = ConnectRequestStageState::cancelled;
}

ConnectRequestStageState ConnectRequestStage::state() const noexcept { return state_; }
const network::NetworkAddress& ConnectRequestStage::remote_endpoint() const noexcept
{
    return remote_endpoint_;
}
const std::optional<network::NetworkAddress>& ConnectRequestStage::local_endpoint() const noexcept
{
    return local_endpoint_;
}
std::size_t ConnectRequestStage::send_attempts() const noexcept { return send_attempts_; }
const std::optional<ConnectRequestStageError>& ConnectRequestStage::error() const noexcept
{
    return error_;
}

void ConnectRequestStage::fail(
    const ConnectRequestStageState state,
    const ConnectRequestStageErrorCategory category,
    const std::optional<ConnectRequestErrorCode> protocol_code,
    std::string context)
{
    state_ = state;
    if (context.size() > kChallengeDiagnosticTextLimit) {
        context.resize(kChallengeDiagnosticTextLimit);
    }
    error_ = ConnectRequestStageError{category, protocol_code, context};
    emit_trace(0U, 0U, std::move(context));
}

void ConnectRequestStage::emit_trace(
    const ChallengeToken challenge,
    const std::size_t datagram_size,
    std::string context)
{
    if (!trace_callback_ || trace_callback_active_) {
        return;
    }
    if (context.size() > kChallengeDiagnosticTextLimit) {
        context.resize(kChallengeDiagnosticTextLimit);
    }
    ConnectRequestTraceEvent event{
        state_, remote_endpoint_, datagram_size, kGoldSrcProtocolVersion, challenge,
        {}, {}, 0U, 0U, 0U, std::move(context)};
    trace_callback_active_ = true;
    try {
        trace_callback_(event);
    } catch (...) {
    }
    trace_callback_active_ = false;
}

GoldSrcHandshakeCoordinator::GoldSrcHandshakeCoordinator(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    const HandshakeStopPoint stop_point,
    std::optional<PreparedConnectRequest> prepared_request,
    ChallengeExchangeConfig challenge_config,
    ChallengeTraceCallback challenge_trace_callback,
    ConnectRequestTraceCallback connect_trace_callback)
    : stop_point_{stop_point},
      challenge_exchange_{
          transport,
          remote_endpoint,
          challenge_config,
          std::move(challenge_trace_callback)}
{
    if (stop_point_ == HandshakeStopPoint::connect_request) {
        if (!prepared_request) {
            configuration_error_ = "Connect-request mode requires prepared authentication and user info";
            state_ = GoldSrcHandshakeState::configuration_error;
        } else {
            connect_stage_.emplace(
                transport,
                remote_endpoint,
                std::move(*prepared_request),
                std::move(connect_trace_callback));
        }
    }
}

bool GoldSrcHandshakeCoordinator::start(const ChallengeExchangeTimePoint now)
{
    if (state_ != GoldSrcHandshakeState::idle) {
        return false;
    }
    const auto started = challenge_exchange_.start(now);
    synchronize_from_challenge();
    return started;
}

void GoldSrcHandshakeCoordinator::update(const ChallengeExchangeTimePoint now)
{
    if (terminal()) {
        return;
    }
    challenge_exchange_.update(now);
    synchronize_from_challenge();
}

void GoldSrcHandshakeCoordinator::cancel(const ChallengeExchangeTimePoint now)
{
    if (terminal()) {
        return;
    }
    if (connect_stage_ && connect_stage_->state() != ConnectRequestStageState::idle) {
        connect_stage_->cancel();
        state_ = map_connect_state(connect_stage_->state());
        return;
    }
    challenge_exchange_.cancel(now);
    synchronize_from_challenge();
}

GoldSrcHandshakeState GoldSrcHandshakeCoordinator::state() const noexcept { return state_; }

bool GoldSrcHandshakeCoordinator::terminal() const noexcept
{
    switch (state_) {
    case GoldSrcHandshakeState::challenge_received:
        return stop_point_ == HandshakeStopPoint::challenge;
    case GoldSrcHandshakeState::request_sent:
    case GoldSrcHandshakeState::timed_out:
    case GoldSrcHandshakeState::cancelled:
    case GoldSrcHandshakeState::configuration_error:
    case GoldSrcHandshakeState::network_error:
    case GoldSrcHandshakeState::protocol_error:
        return true;
    case GoldSrcHandshakeState::idle:
    case GoldSrcHandshakeState::waiting_for_challenge:
    case GoldSrcHandshakeState::building_request:
    case GoldSrcHandshakeState::request_ready:
    case GoldSrcHandshakeState::sending_request:
        return false;
    }
    return true;
}

HandshakeStopPoint GoldSrcHandshakeCoordinator::stop_point() const noexcept { return stop_point_; }
const std::optional<ChallengeResponse>& GoldSrcHandshakeCoordinator::challenge() const noexcept
{
    return challenge_exchange_.challenge();
}
const std::optional<network::NetworkAddress>& GoldSrcHandshakeCoordinator::local_endpoint() const noexcept
{
    return challenge_exchange_.local_endpoint();
}
std::size_t GoldSrcHandshakeCoordinator::connect_send_attempts() const noexcept
{
    return connect_stage_ ? connect_stage_->send_attempts() : 0U;
}
std::string_view GoldSrcHandshakeCoordinator::error_context() const noexcept
{
    if (!configuration_error_.empty()) {
        return configuration_error_;
    }
    if (connect_stage_ && connect_stage_->error()) {
        return connect_stage_->error()->context;
    }
    if (challenge_exchange_.error()) {
        return challenge_exchange_.error()->context;
    }
    return {};
}

void GoldSrcHandshakeCoordinator::synchronize_from_challenge()
{
    state_ = map_challenge_state(challenge_exchange_.state());
    if (state_ != GoldSrcHandshakeState::challenge_received ||
        stop_point_ == HandshakeStopPoint::challenge) {
        return;
    }
    if (!connect_stage_ || !challenge_exchange_.challenge() ||
        !challenge_exchange_.local_endpoint()) {
        state_ = GoldSrcHandshakeState::configuration_error;
        configuration_error_ = "Challenge completed without a prepared connect stage or endpoint";
        return;
    }
    static_cast<void>(connect_stage_->start(
        *challenge_exchange_.challenge(),
        *challenge_exchange_.local_endpoint()));
    state_ = map_connect_state(connect_stage_->state());
}

} // namespace hlclient::goldsrc
