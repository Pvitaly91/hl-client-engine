#include <hlclient/goldsrc/connect_request_stage.hpp>

#include <utility>

namespace hlclient::goldsrc {
namespace {

class AuthenticationDriverLifetime final : public INetchanDriverLifetime {
public:
    explicit AuthenticationDriverLifetime(
        auth::AuthenticationSession&& session) noexcept
        : session_{std::move(session)}
    {
    }

private:
    auth::AuthenticationSession session_;
};

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

[[nodiscard]] GoldSrcHandshakeState map_response_state(
    const ConnectResponseWaitState state) noexcept
{
    switch (state) {
    case ConnectResponseWaitState::idle:
    case ConnectResponseWaitState::waiting:
        return GoldSrcHandshakeState::waiting_for_connect_response;
    case ConnectResponseWaitState::accepted:
        return GoldSrcHandshakeState::accepted;
    case ConnectResponseWaitState::rejected:
        return GoldSrcHandshakeState::rejected;
    case ConnectResponseWaitState::timed_out:
        return GoldSrcHandshakeState::connect_response_timed_out;
    case ConnectResponseWaitState::cancelled:
        return GoldSrcHandshakeState::cancelled;
    case ConnectResponseWaitState::network_error:
        return GoldSrcHandshakeState::network_error;
    case ConnectResponseWaitState::protocol_error:
        return GoldSrcHandshakeState::protocol_error;
    }
    return GoldSrcHandshakeState::protocol_error;
}

[[nodiscard]] GoldSrcHandshakeState map_netchan_state(
    const NetchanBootstrapState state) noexcept
{
    switch (state) {
    case NetchanBootstrapState::idle:
    case NetchanBootstrapState::waiting_first:
    case NetchanBootstrapState::processing:
    case NetchanBootstrapState::ack_pending:
        return GoldSrcHandshakeState::waiting_for_netchan;
    case NetchanBootstrapState::complete:
        return GoldSrcHandshakeState::netchan_bootstrap_complete;
    case NetchanBootstrapState::timed_out:
        return GoldSrcHandshakeState::netchan_timed_out;
    case NetchanBootstrapState::cancelled:
        return GoldSrcHandshakeState::cancelled;
    case NetchanBootstrapState::network_error:
        return GoldSrcHandshakeState::network_error;
    case NetchanBootstrapState::protocol_error:
        return GoldSrcHandshakeState::protocol_error;
    }
    return GoldSrcHandshakeState::protocol_error;
}

[[nodiscard]] GoldSrcHandshakeState map_signon_state(
    const InitialSignonState state) noexcept
{
    switch (state) {
    case InitialSignonState::idle:
    case InitialSignonState::waiting_for_request_transmit:
    case InitialSignonState::waiting_for_request_ack:
    case InitialSignonState::waiting_for_server_payload:
    case InitialSignonState::decoding_service_stream:
        return GoldSrcHandshakeState::waiting_for_signon;
    case InitialSignonState::signon_boundary_reached:
        return GoldSrcHandshakeState::signon_boundary_reached;
    case InitialSignonState::timed_out:
        return GoldSrcHandshakeState::signon_timed_out;
    case InitialSignonState::cancelled:
        return GoldSrcHandshakeState::cancelled;
    case InitialSignonState::network_error:
        return GoldSrcHandshakeState::network_error;
    case InitialSignonState::protocol_error:
        return GoldSrcHandshakeState::protocol_error;
    case InitialSignonState::unsupported_service_message:
        return GoldSrcHandshakeState::signon_unsupported_service;
    case InitialSignonState::backpressure:
        return GoldSrcHandshakeState::signon_backpressure;
    case InitialSignonState::secondary_stream_pending_m3:
        return GoldSrcHandshakeState::signon_secondary_stream_pending_m3;
    }
    return GoldSrcHandshakeState::protocol_error;
}

[[nodiscard]] GoldSrcHandshakeState map_pre_resource_state(
    const PreResourceSignonStageState state) noexcept
{
    switch (state) {
    case PreResourceSignonStageState::idle:
    case PreResourceSignonStageState::waiting_for_initial_boundary:
    case PreResourceSignonStageState::decoding_server_info:
    case PreResourceSignonStageState::server_info_ready:
    case PreResourceSignonStageState::decoding_pre_resource_messages:
        return GoldSrcHandshakeState::waiting_for_pre_resource;
    case PreResourceSignonStageState::pre_resource_boundary_reached:
        return GoldSrcHandshakeState::pre_resource_boundary_reached;
    case PreResourceSignonStageState::unsupported_message:
        return GoldSrcHandshakeState::pre_resource_unsupported_message;
    case PreResourceSignonStageState::timed_out:
        return GoldSrcHandshakeState::pre_resource_timed_out;
    case PreResourceSignonStageState::cancelled:
        return GoldSrcHandshakeState::cancelled;
    case PreResourceSignonStageState::backpressure:
        return GoldSrcHandshakeState::pre_resource_backpressure;
    case PreResourceSignonStageState::secondary_stream_pending_m3:
        return GoldSrcHandshakeState::pre_resource_secondary_stream_pending_m3;
    case PreResourceSignonStageState::network_error:
        return GoldSrcHandshakeState::network_error;
    case PreResourceSignonStageState::protocol_error:
        return GoldSrcHandshakeState::protocol_error;
    }
    return GoldSrcHandshakeState::protocol_error;
}

[[nodiscard]] GoldSrcHandshakeState map_delta_description_state(
    const DeltaDescriptionStageState state) noexcept
{
    switch (state) {
    case DeltaDescriptionStageState::idle:
    case DeltaDescriptionStageState::waiting_for_pre_resource_state:
    case DeltaDescriptionStageState::decoding_delta_stream:
    case DeltaDescriptionStageState::delta_registry_ready:
        return GoldSrcHandshakeState::waiting_for_delta_schemas;
    case DeltaDescriptionStageState::post_delta_boundary_reached:
        return GoldSrcHandshakeState::delta_schemas_ready;
    case DeltaDescriptionStageState::unsupported_message:
        return GoldSrcHandshakeState::delta_unsupported_message;
    case DeltaDescriptionStageState::timed_out:
        return GoldSrcHandshakeState::delta_timed_out;
    case DeltaDescriptionStageState::cancelled:
        return GoldSrcHandshakeState::cancelled;
    case DeltaDescriptionStageState::backpressure:
        return GoldSrcHandshakeState::delta_backpressure;
    case DeltaDescriptionStageState::secondary_stream_pending_m3:
        return GoldSrcHandshakeState::delta_secondary_stream_pending_m3;
    case DeltaDescriptionStageState::network_error:
        return GoldSrcHandshakeState::network_error;
    case DeltaDescriptionStageState::protocol_error:
        return GoldSrcHandshakeState::protocol_error;
    }
    return GoldSrcHandshakeState::protocol_error;
}

[[nodiscard]] GoldSrcHandshakeState map_movement_environment_state(
    const MovementEnvironmentStageState state) noexcept
{
    switch (state) {
    case MovementEnvironmentStageState::idle:
    case MovementEnvironmentStageState::waiting_for_delta_state:
    case MovementEnvironmentStageState::decoding_move_vars:
    case MovementEnvironmentStageState::environment_state_ready:
    case MovementEnvironmentStageState::decoding_post_environment_messages:
        return GoldSrcHandshakeState::waiting_for_movevars;
    case MovementEnvironmentStageState::post_environment_boundary_reached:
        return GoldSrcHandshakeState::movement_environment_boundary_reached;
    case MovementEnvironmentStageState::unsupported_message:
        return GoldSrcHandshakeState::movevars_unsupported_message;
    case MovementEnvironmentStageState::timed_out:
        return GoldSrcHandshakeState::movevars_timed_out;
    case MovementEnvironmentStageState::cancelled:
        return GoldSrcHandshakeState::cancelled;
    case MovementEnvironmentStageState::backpressure:
        return GoldSrcHandshakeState::movevars_backpressure;
    case MovementEnvironmentStageState::secondary_stream_pending_m3:
        return GoldSrcHandshakeState::movevars_secondary_stream_pending_m3;
    case MovementEnvironmentStageState::network_error:
        return GoldSrcHandshakeState::network_error;
    case MovementEnvironmentStageState::protocol_error:
        return GoldSrcHandshakeState::protocol_error;
    }
    return GoldSrcHandshakeState::protocol_error;
}

[[nodiscard]] GoldSrcHandshakeState map_user_info_state(
    const UserInfoSignonStageState state) noexcept
{
    switch (state) {
    case UserInfoSignonStageState::idle:
    case UserInfoSignonStageState::waiting_for_movevars_state:
    case UserInfoSignonStageState::decoding_user_info:
    case UserInfoSignonStageState::user_info_ready:
        return GoldSrcHandshakeState::waiting_for_user_info;
    case UserInfoSignonStageState::first_batch_complete:
        return GoldSrcHandshakeState::user_info_complete;
    case UserInfoSignonStageState::unsupported_message:
        return GoldSrcHandshakeState::user_info_unsupported_message;
    case UserInfoSignonStageState::timed_out:
        return GoldSrcHandshakeState::user_info_timed_out;
    case UserInfoSignonStageState::cancelled:
        return GoldSrcHandshakeState::cancelled;
    case UserInfoSignonStageState::backpressure:
        return GoldSrcHandshakeState::user_info_backpressure;
    case UserInfoSignonStageState::secondary_stream_pending:
        return GoldSrcHandshakeState::user_info_secondary_stream_pending;
    case UserInfoSignonStageState::network_error:
        return GoldSrcHandshakeState::network_error;
    case UserInfoSignonStageState::protocol_error:
        return GoldSrcHandshakeState::protocol_error;
    }
    return GoldSrcHandshakeState::protocol_error;
}

[[nodiscard]] GoldSrcHandshakeState map_resource_transition_state(
    const ResourceTransitionStageState state) noexcept
{
    switch (state) {
    case ResourceTransitionStageState::idle:
    case ResourceTransitionStageState::waiting_for_user_info_state:
    case ResourceTransitionStageState::request_ready:
    case ResourceTransitionStageState::waiting_for_request_transmit:
    case ResourceTransitionStageState::waiting_for_request_ack:
    case ResourceTransitionStageState::waiting_for_server_transfer:
    case ResourceTransitionStageState::decoding_transition_control:
        return GoldSrcHandshakeState::waiting_for_resource_transition;
    case ResourceTransitionStageState::neutral_opcode43_boundary_reached:
        return GoldSrcHandshakeState::resource_transition_boundary_reached;
    case ResourceTransitionStageState::unsupported_message:
        return GoldSrcHandshakeState::resource_transition_unsupported_message;
    case ResourceTransitionStageState::timed_out:
        return GoldSrcHandshakeState::resource_transition_timed_out;
    case ResourceTransitionStageState::cancelled:
        return GoldSrcHandshakeState::cancelled;
    case ResourceTransitionStageState::backpressure:
        return GoldSrcHandshakeState::resource_transition_backpressure;
    case ResourceTransitionStageState::secondary_stream_pending:
        return GoldSrcHandshakeState::resource_transition_secondary_stream_pending;
    case ResourceTransitionStageState::network_error:
        return GoldSrcHandshakeState::network_error;
    case ResourceTransitionStageState::protocol_error:
        return GoldSrcHandshakeState::protocol_error;
    }
    return GoldSrcHandshakeState::protocol_error;
}

[[nodiscard]] GoldSrcHandshakeState map_resource_list_state(
    const ResourceListStageState state) noexcept
{
    switch (state) {
    case ResourceListStageState::idle:
    case ResourceListStageState::waiting_for_transition_state:
    case ResourceListStageState::decoding_resource_list:
    case ResourceListStageState::resource_list_ready:
    case ResourceListStageState::decoding_post_list_messages:
    case ResourceListStageState::post_list_boundary_reached:
        return GoldSrcHandshakeState::waiting_for_resource_list;
    case ResourceListStageState::client_response_required:
        return GoldSrcHandshakeState::resource_list_client_response_required;
    case ResourceListStageState::unsupported_resource_profile:
        return GoldSrcHandshakeState::resource_list_unsupported_profile;
    case ResourceListStageState::timed_out:
        return GoldSrcHandshakeState::resource_list_timed_out;
    case ResourceListStageState::cancelled:
        return GoldSrcHandshakeState::cancelled;
    case ResourceListStageState::backpressure:
        return GoldSrcHandshakeState::resource_list_backpressure;
    case ResourceListStageState::secondary_stream_pending:
        return GoldSrcHandshakeState::resource_list_secondary_stream_pending;
    case ResourceListStageState::network_error:
        return GoldSrcHandshakeState::network_error;
    case ResourceListStageState::protocol_error:
        return GoldSrcHandshakeState::protocol_error;
    }
    return GoldSrcHandshakeState::protocol_error;
}

[[nodiscard]] GoldSrcHandshakeState map_resource_client_response_state(
    const ResourceClientResponseStageState state) noexcept
{
    switch (state) {
    case ResourceClientResponseStageState::idle:
    case ResourceClientResponseStageState::waiting_for_resource_list:
    case ResourceClientResponseStageState::preparing_response:
    case ResourceClientResponseStageState::waiting_for_consistency_provider:
    case ResourceClientResponseStageState::response_ready:
    case ResourceClientResponseStageState::waiting_for_response_transmit:
    case ResourceClientResponseStageState::waiting_for_response_ack:
    case ResourceClientResponseStageState::waiting_for_server_continuation:
    case ResourceClientResponseStageState::decoding_server_continuation:
        return GoldSrcHandshakeState::waiting_for_resource_response;
    case ResourceClientResponseStageState::next_server_boundary_reached:
        return GoldSrcHandshakeState::resource_response_boundary_reached;
    case ResourceClientResponseStageState::consistency_provider_required:
        return GoldSrcHandshakeState::resource_response_provider_required;
    case ResourceClientResponseStageState::unsupported_response_profile:
        return GoldSrcHandshakeState::resource_response_unsupported_profile;
    case ResourceClientResponseStageState::timed_out:
        return GoldSrcHandshakeState::resource_response_timed_out;
    case ResourceClientResponseStageState::cancelled:
        return GoldSrcHandshakeState::cancelled;
    case ResourceClientResponseStageState::backpressure:
        return GoldSrcHandshakeState::resource_response_backpressure;
    case ResourceClientResponseStageState::secondary_stream_pending:
        return GoldSrcHandshakeState::resource_response_secondary_stream_pending;
    case ResourceClientResponseStageState::network_error:
        return GoldSrcHandshakeState::network_error;
    case ResourceClientResponseStageState::protocol_error:
        return GoldSrcHandshakeState::protocol_error;
    }
    return GoldSrcHandshakeState::protocol_error;
}

[[nodiscard]] bool signon_start_network_failure(
    const std::optional<NetchanDriverErrorCode> driver_code) noexcept
{
    if (!driver_code) {
        return false;
    }
    switch (*driver_code) {
    case NetchanDriverErrorCode::receive_failed:
    case NetchanDriverErrorCode::inconsistent_receive_result:
    case NetchanDriverErrorCode::local_endpoint_unavailable:
    case NetchanDriverErrorCode::local_endpoint_changed:
    case NetchanDriverErrorCode::send_failed:
        return true;
    case NetchanDriverErrorCode::invalid_configuration:
    case NetchanDriverErrorCode::not_active:
    case NetchanDriverErrorCode::reentrant_operation:
    case NetchanDriverErrorCode::time_moved_backwards:
    case NetchanDriverErrorCode::event_backpressure:
    case NetchanDriverErrorCode::datagram_truncated:
    case NetchanDriverErrorCode::unexpected_connectionless_packet:
    case NetchanDriverErrorCode::unsupported_special_packet:
    case NetchanDriverErrorCode::malformed_packet:
    case NetchanDriverErrorCode::invalid_sequence:
    case NetchanDriverErrorCode::invalid_acknowledgement:
    case NetchanDriverErrorCode::opaque_payload_too_large:
    case NetchanDriverErrorCode::packet_encode_failed:
    case NetchanDriverErrorCode::reliable_queue_failed:
    case NetchanDriverErrorCode::unreliable_payload_too_large:
    case NetchanDriverErrorCode::unreliable_payload_pending:
    case NetchanDriverErrorCode::fragment_reassembly_failed:
    case NetchanDriverErrorCode::secondary_stream_pending_m3:
    case NetchanDriverErrorCode::channel_inactivity_timed_out:
    case NetchanDriverErrorCode::fragment_transfer_timed_out:
        return false;
    }
    return false;
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
    ConnectRequestTraceCallback connect_trace_callback,
    ConnectResponseWaitConfig response_config,
    ConnectResponseTraceCallback response_trace_callback,
    std::optional<auth::AuthenticationSession> authentication_session,
    NetchanBootstrapConfig netchan_config,
    NetchanBootstrapTraceCallback netchan_trace_callback,
    InitialSignonConfig signon_config,
    InitialSignonTraceCallback signon_trace_callback,
    PreResourceSignonConfig pre_resource_config,
    PreResourceSignonTraceCallback pre_resource_trace_callback,
    DeltaDescriptionStageConfig delta_config,
    DeltaDescriptionTraceCallback delta_trace_callback,
    MovementEnvironmentStageConfig movement_environment_config,
    MovementEnvironmentTraceCallback movement_environment_trace_callback,
    UserInfoSignonStageConfig user_info_config,
    UserInfoSignonTraceCallback user_info_trace_callback,
    ResourceTransitionStageConfig resource_transition_config,
    ResourceTransitionTraceCallback resource_transition_trace_callback,
    ResourceListStageConfig resource_list_config,
    ResourceListTraceCallback resource_list_trace_callback,
    ResourceClientResponseStageConfig resource_response_config,
    resource_consistency::IResourceConsistencyProvider*
        resource_consistency_provider,
    ResourceClientResponseTraceCallback resource_response_trace_callback)
    : stop_point_{stop_point},
      challenge_exchange_{
          transport,
          remote_endpoint,
          challenge_config,
          std::move(challenge_trace_callback)},
      authentication_session_{std::move(authentication_session)}
{
    if (stop_point_ != HandshakeStopPoint::challenge) {
        if (!prepared_request) {
            configuration_error_ =
                "Connect request/response mode requires prepared authentication and user info";
            state_ = GoldSrcHandshakeState::configuration_error;
        } else {
            connect_stage_.emplace(
                transport,
                remote_endpoint,
                std::move(*prepared_request),
                std::move(connect_trace_callback));
            if (stop_point_ == HandshakeStopPoint::connect_response ||
                stop_point_ == HandshakeStopPoint::netchan_bootstrap ||
                stop_point_ == HandshakeStopPoint::signon_boundary ||
                stop_point_ == HandshakeStopPoint::pre_resource ||
                stop_point_ == HandshakeStopPoint::delta_schemas ||
                stop_point_ == HandshakeStopPoint::movevars ||
                stop_point_ == HandshakeStopPoint::user_info ||
                stop_point_ == HandshakeStopPoint::resource_list_boundary ||
                stop_point_ == HandshakeStopPoint::resource_list ||
                stop_point_ == HandshakeStopPoint::resource_response_boundary) {
                response_stage_.emplace(
                    transport,
                    remote_endpoint,
                    response_config,
                    std::move(response_trace_callback));
            }
            if (stop_point_ == HandshakeStopPoint::netchan_bootstrap) {
                netchan_stage_.emplace(
                    transport,
                    remote_endpoint,
                    std::move(netchan_config),
                    std::move(netchan_trace_callback));
            }
            if (stop_point_ == HandshakeStopPoint::signon_boundary) {
                signon_stage_.emplace(
                    transport,
                    remote_endpoint,
                    std::move(signon_config),
                    std::move(signon_trace_callback));
            }
            if (stop_point_ == HandshakeStopPoint::pre_resource) {
                pre_resource_stage_.emplace(
                    transport,
                    remote_endpoint,
                    std::move(pre_resource_config),
                    std::move(signon_trace_callback),
                    std::move(pre_resource_trace_callback));
            }
            if (stop_point_ == HandshakeStopPoint::delta_schemas) {
                delta_description_stage_.emplace(
                    transport,
                    remote_endpoint,
                    std::move(delta_config),
                    std::move(signon_trace_callback),
                    std::move(pre_resource_trace_callback),
                    std::move(delta_trace_callback));
            }
            if (stop_point_ == HandshakeStopPoint::movevars) {
                movement_environment_stage_.emplace(
                    transport,
                    remote_endpoint,
                    std::move(movement_environment_config),
                    std::move(signon_trace_callback),
                    std::move(pre_resource_trace_callback),
                    std::move(delta_trace_callback),
                    std::move(movement_environment_trace_callback));
            }
            if (stop_point_ == HandshakeStopPoint::user_info) {
                user_info_stage_.emplace(
                    transport,
                    remote_endpoint,
                    std::move(user_info_config),
                    std::move(signon_trace_callback),
                    std::move(pre_resource_trace_callback),
                    std::move(delta_trace_callback),
                    std::move(movement_environment_trace_callback),
                    std::move(user_info_trace_callback));
            }
            if (stop_point_ == HandshakeStopPoint::resource_list_boundary) {
                resource_transition_stage_.emplace(
                    transport,
                    remote_endpoint,
                    std::move(resource_transition_config),
                    std::move(signon_trace_callback),
                    std::move(pre_resource_trace_callback),
                    std::move(delta_trace_callback),
                    std::move(movement_environment_trace_callback),
                    std::move(user_info_trace_callback),
                    std::move(resource_transition_trace_callback));
            }
            if (stop_point_ == HandshakeStopPoint::resource_list) {
                resource_list_stage_.emplace(
                    transport,
                    remote_endpoint,
                    std::move(resource_list_config),
                    std::move(signon_trace_callback),
                    std::move(pre_resource_trace_callback),
                    std::move(delta_trace_callback),
                    std::move(movement_environment_trace_callback),
                    std::move(user_info_trace_callback),
                    std::move(resource_transition_trace_callback),
                    std::move(resource_list_trace_callback));
            }
            if (stop_point_ == HandshakeStopPoint::resource_response_boundary) {
                resource_client_response_stage_.emplace(
                    transport,
                    remote_endpoint,
                    std::move(resource_response_config),
                    resource_consistency_provider,
                    std::move(signon_trace_callback),
                    std::move(pre_resource_trace_callback),
                    std::move(delta_trace_callback),
                    std::move(movement_environment_trace_callback),
                    std::move(user_info_trace_callback),
                    std::move(resource_transition_trace_callback),
                    std::move(resource_list_trace_callback),
                    std::move(resource_response_trace_callback));
            }
        }
    } else if (authentication_session_) {
        configuration_error_ =
            "Challenge-only mode does not accept an authentication session";
        state_ = GoldSrcHandshakeState::configuration_error;
    }
    release_authentication_session_if_terminal();
}

bool GoldSrcHandshakeCoordinator::start(const ChallengeExchangeTimePoint now)
{
    if (state_ != GoldSrcHandshakeState::idle) {
        return false;
    }
    const auto started = challenge_exchange_.start(now);
    synchronize_from_challenge(now);
    release_authentication_session_if_terminal();
    return started;
}

void GoldSrcHandshakeCoordinator::update(const ChallengeExchangeTimePoint now)
{
    if (terminal()) {
        return;
    }
    if (resource_client_response_stage_ &&
        state_ == GoldSrcHandshakeState::waiting_for_resource_response) {
        resource_client_response_stage_->update(now);
        synchronize_from_resource_client_response();
        release_authentication_session_if_terminal();
        return;
    }
    if (resource_list_stage_ &&
        state_ == GoldSrcHandshakeState::waiting_for_resource_list) {
        resource_list_stage_->update(now);
        synchronize_from_resource_list();
        release_authentication_session_if_terminal();
        return;
    }
    if (resource_transition_stage_ &&
        state_ == GoldSrcHandshakeState::waiting_for_resource_transition) {
        resource_transition_stage_->update(now);
        synchronize_from_resource_transition();
        release_authentication_session_if_terminal();
        return;
    }
    if (user_info_stage_ &&
        state_ == GoldSrcHandshakeState::waiting_for_user_info) {
        user_info_stage_->update(now);
        synchronize_from_user_info();
        release_authentication_session_if_terminal();
        return;
    }
    if (movement_environment_stage_ &&
        state_ == GoldSrcHandshakeState::waiting_for_movevars) {
        movement_environment_stage_->update(now);
        synchronize_from_movement_environment();
        release_authentication_session_if_terminal();
        return;
    }
    if (delta_description_stage_ &&
        state_ == GoldSrcHandshakeState::waiting_for_delta_schemas) {
        delta_description_stage_->update(now);
        synchronize_from_delta_description();
        release_authentication_session_if_terminal();
        return;
    }
    if (pre_resource_stage_ &&
        state_ == GoldSrcHandshakeState::waiting_for_pre_resource) {
        pre_resource_stage_->update(now);
        synchronize_from_pre_resource();
        release_authentication_session_if_terminal();
        return;
    }
    if (signon_stage_ && state_ == GoldSrcHandshakeState::waiting_for_signon) {
        signon_stage_->update(now);
        synchronize_from_signon();
        release_authentication_session_if_terminal();
        return;
    }
    if (netchan_stage_ && state_ == GoldSrcHandshakeState::waiting_for_netchan) {
        netchan_stage_->update(now);
        synchronize_from_netchan();
        release_authentication_session_if_terminal();
        return;
    }
    if (response_stage_ &&
        state_ == GoldSrcHandshakeState::waiting_for_connect_response) {
        response_stage_->update(now);
        synchronize_from_response(now);
        release_authentication_session_if_terminal();
        return;
    }
    challenge_exchange_.update(now);
    synchronize_from_challenge(now);
    release_authentication_session_if_terminal();
}

void GoldSrcHandshakeCoordinator::cancel(const ChallengeExchangeTimePoint now)
{
    if (terminal()) {
        return;
    }
    if (resource_client_response_stage_ &&
        state_ == GoldSrcHandshakeState::waiting_for_resource_response) {
        resource_client_response_stage_->cancel(now);
        synchronize_from_resource_client_response();
        release_authentication_session_if_terminal();
        return;
    }
    if (resource_list_stage_ &&
        state_ == GoldSrcHandshakeState::waiting_for_resource_list) {
        resource_list_stage_->cancel(now);
        synchronize_from_resource_list();
        release_authentication_session_if_terminal();
        return;
    }
    if (resource_transition_stage_ &&
        state_ == GoldSrcHandshakeState::waiting_for_resource_transition) {
        resource_transition_stage_->cancel(now);
        synchronize_from_resource_transition();
        release_authentication_session_if_terminal();
        return;
    }
    if (user_info_stage_ &&
        state_ == GoldSrcHandshakeState::waiting_for_user_info) {
        user_info_stage_->cancel(now);
        synchronize_from_user_info();
        release_authentication_session_if_terminal();
        return;
    }
    if (movement_environment_stage_ &&
        state_ == GoldSrcHandshakeState::waiting_for_movevars) {
        movement_environment_stage_->cancel(now);
        synchronize_from_movement_environment();
        release_authentication_session_if_terminal();
        return;
    }
    if (delta_description_stage_ &&
        state_ == GoldSrcHandshakeState::waiting_for_delta_schemas) {
        delta_description_stage_->cancel(now);
        synchronize_from_delta_description();
        release_authentication_session_if_terminal();
        return;
    }
    if (pre_resource_stage_ &&
        state_ == GoldSrcHandshakeState::waiting_for_pre_resource) {
        pre_resource_stage_->cancel(now);
        synchronize_from_pre_resource();
        release_authentication_session_if_terminal();
        return;
    }
    if (signon_stage_ && state_ == GoldSrcHandshakeState::waiting_for_signon) {
        signon_stage_->cancel(now);
        synchronize_from_signon();
        release_authentication_session_if_terminal();
        return;
    }
    if (netchan_stage_ && state_ == GoldSrcHandshakeState::waiting_for_netchan) {
        netchan_stage_->cancel(now);
        synchronize_from_netchan();
        release_authentication_session_if_terminal();
        return;
    }
    if (response_stage_ &&
        state_ == GoldSrcHandshakeState::waiting_for_connect_response) {
        response_stage_->cancel(now);
        synchronize_from_response(now);
        release_authentication_session_if_terminal();
        return;
    }
    if (connect_stage_ && connect_stage_->state() != ConnectRequestStageState::idle) {
        connect_stage_->cancel();
        state_ = map_connect_state(connect_stage_->state());
        release_authentication_session_if_terminal();
        return;
    }
    challenge_exchange_.cancel(now);
    synchronize_from_challenge(now);
    release_authentication_session_if_terminal();
}

GoldSrcHandshakeState GoldSrcHandshakeCoordinator::state() const noexcept { return state_; }

bool GoldSrcHandshakeCoordinator::terminal() const noexcept
{
    switch (state_) {
    case GoldSrcHandshakeState::challenge_received:
        return stop_point_ == HandshakeStopPoint::challenge;
    case GoldSrcHandshakeState::request_sent:
        return stop_point_ == HandshakeStopPoint::connect_request;
    case GoldSrcHandshakeState::accepted:
        return stop_point_ == HandshakeStopPoint::connect_response;
    case GoldSrcHandshakeState::rejected:
    case GoldSrcHandshakeState::connect_response_timed_out:
    case GoldSrcHandshakeState::netchan_bootstrap_complete:
    case GoldSrcHandshakeState::netchan_timed_out:
    case GoldSrcHandshakeState::signon_boundary_reached:
    case GoldSrcHandshakeState::signon_timed_out:
    case GoldSrcHandshakeState::signon_unsupported_service:
    case GoldSrcHandshakeState::signon_backpressure:
    case GoldSrcHandshakeState::signon_secondary_stream_pending_m3:
    case GoldSrcHandshakeState::pre_resource_boundary_reached:
    case GoldSrcHandshakeState::pre_resource_timed_out:
    case GoldSrcHandshakeState::pre_resource_unsupported_message:
    case GoldSrcHandshakeState::pre_resource_backpressure:
    case GoldSrcHandshakeState::pre_resource_secondary_stream_pending_m3:
    case GoldSrcHandshakeState::delta_schemas_ready:
    case GoldSrcHandshakeState::delta_timed_out:
    case GoldSrcHandshakeState::delta_unsupported_message:
    case GoldSrcHandshakeState::delta_backpressure:
    case GoldSrcHandshakeState::delta_secondary_stream_pending_m3:
    case GoldSrcHandshakeState::movement_environment_boundary_reached:
    case GoldSrcHandshakeState::movevars_timed_out:
    case GoldSrcHandshakeState::movevars_unsupported_message:
    case GoldSrcHandshakeState::movevars_backpressure:
    case GoldSrcHandshakeState::movevars_secondary_stream_pending_m3:
    case GoldSrcHandshakeState::user_info_complete:
    case GoldSrcHandshakeState::user_info_timed_out:
    case GoldSrcHandshakeState::user_info_unsupported_message:
    case GoldSrcHandshakeState::user_info_backpressure:
    case GoldSrcHandshakeState::user_info_secondary_stream_pending:
    case GoldSrcHandshakeState::resource_transition_boundary_reached:
    case GoldSrcHandshakeState::resource_transition_timed_out:
    case GoldSrcHandshakeState::resource_transition_unsupported_message:
    case GoldSrcHandshakeState::resource_transition_backpressure:
    case GoldSrcHandshakeState::resource_transition_secondary_stream_pending:
    case GoldSrcHandshakeState::resource_list_client_response_required:
    case GoldSrcHandshakeState::resource_list_unsupported_profile:
    case GoldSrcHandshakeState::resource_list_timed_out:
    case GoldSrcHandshakeState::resource_list_backpressure:
    case GoldSrcHandshakeState::resource_list_secondary_stream_pending:
    case GoldSrcHandshakeState::resource_response_boundary_reached:
    case GoldSrcHandshakeState::resource_response_provider_required:
    case GoldSrcHandshakeState::resource_response_unsupported_profile:
    case GoldSrcHandshakeState::resource_response_timed_out:
    case GoldSrcHandshakeState::resource_response_backpressure:
    case GoldSrcHandshakeState::resource_response_secondary_stream_pending:
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
    case GoldSrcHandshakeState::waiting_for_connect_response:
    case GoldSrcHandshakeState::waiting_for_netchan:
    case GoldSrcHandshakeState::waiting_for_signon:
    case GoldSrcHandshakeState::waiting_for_pre_resource:
    case GoldSrcHandshakeState::waiting_for_delta_schemas:
    case GoldSrcHandshakeState::waiting_for_movevars:
    case GoldSrcHandshakeState::waiting_for_user_info:
    case GoldSrcHandshakeState::waiting_for_resource_transition:
    case GoldSrcHandshakeState::waiting_for_resource_list:
    case GoldSrcHandshakeState::waiting_for_resource_response:
        return false;
    }
    return true;
}

HandshakeStopPoint GoldSrcHandshakeCoordinator::stop_point() const noexcept { return stop_point_; }
const std::optional<ChallengeResponse>& GoldSrcHandshakeCoordinator::challenge() const noexcept
{
    return challenge_exchange_.challenge();
}
const std::optional<ConnectResponse>&
GoldSrcHandshakeCoordinator::connect_response() const noexcept
{
    static const std::optional<ConnectResponse> empty;
    return response_stage_ ? response_stage_->response() : empty;
}
const std::optional<NetchanBootstrapResult>&
GoldSrcHandshakeCoordinator::netchan_bootstrap_result() const noexcept
{
    static const std::optional<NetchanBootstrapResult> empty;
    return netchan_stage_ ? netchan_stage_->result() : empty;
}
const std::optional<InitialSignonResult>&
GoldSrcHandshakeCoordinator::initial_signon_result() const noexcept
{
    static const std::optional<InitialSignonResult> empty;
    return signon_stage_ ? signon_stage_->result() : empty;
}
const std::optional<InitialSignonError>&
GoldSrcHandshakeCoordinator::initial_signon_error() const noexcept
{
    static const std::optional<InitialSignonError> empty;
    return signon_stage_ ? signon_stage_->error() : empty;
}
const std::optional<PreResourceSignonState>&
GoldSrcHandshakeCoordinator::pre_resource_result() const noexcept
{
    static const std::optional<PreResourceSignonState> empty;
    return pre_resource_stage_ ? pre_resource_stage_->result() : empty;
}
const std::optional<PreResourceSignonError>&
GoldSrcHandshakeCoordinator::pre_resource_error() const noexcept
{
    static const std::optional<PreResourceSignonError> empty;
    return pre_resource_stage_ ? pre_resource_stage_->error() : empty;
}
const std::optional<DeltaDescriptionSignonState>&
GoldSrcHandshakeCoordinator::delta_description_result() const noexcept
{
    static const std::optional<DeltaDescriptionSignonState> empty;
    return delta_description_stage_ ? delta_description_stage_->result() : empty;
}
const std::optional<DeltaDescriptionStageError>&
GoldSrcHandshakeCoordinator::delta_description_error() const noexcept
{
    static const std::optional<DeltaDescriptionStageError> empty;
    return delta_description_stage_ ? delta_description_stage_->error() : empty;
}
const std::optional<MovementEnvironmentSignonState>&
GoldSrcHandshakeCoordinator::movement_environment_result() const noexcept
{
    static const std::optional<MovementEnvironmentSignonState> empty;
    return movement_environment_stage_ ? movement_environment_stage_->result() : empty;
}
const std::optional<MovementEnvironmentStageError>&
GoldSrcHandshakeCoordinator::movement_environment_error() const noexcept
{
    static const std::optional<MovementEnvironmentStageError> empty;
    return movement_environment_stage_ ? movement_environment_stage_->error() : empty;
}
const std::optional<UserInfoSignonState>&
GoldSrcHandshakeCoordinator::user_info_result() const noexcept
{
    static const std::optional<UserInfoSignonState> empty;
    return user_info_stage_ ? user_info_stage_->result() : empty;
}
const std::optional<UserInfoSignonStageError>&
GoldSrcHandshakeCoordinator::user_info_error() const noexcept
{
    static const std::optional<UserInfoSignonStageError> empty;
    return user_info_stage_ ? user_info_stage_->error() : empty;
}
const std::optional<ResourceTransitionState>&
GoldSrcHandshakeCoordinator::resource_transition_result() const noexcept
{
    static const std::optional<ResourceTransitionState> empty;
    return resource_transition_stage_ ? resource_transition_stage_->result() : empty;
}
const std::optional<ResourceTransitionStageError>&
GoldSrcHandshakeCoordinator::resource_transition_error() const noexcept
{
    static const std::optional<ResourceTransitionStageError> empty;
    return resource_transition_stage_ ? resource_transition_stage_->error() : empty;
}
const std::optional<ResourceListSignonState>&
GoldSrcHandshakeCoordinator::resource_list_result() const noexcept
{
    static const std::optional<ResourceListSignonState> empty;
    return resource_list_stage_ ? resource_list_stage_->result() : empty;
}
const std::optional<ResourceListStageError>&
GoldSrcHandshakeCoordinator::resource_list_error() const noexcept
{
    static const std::optional<ResourceListStageError> empty;
    return resource_list_stage_ ? resource_list_stage_->error() : empty;
}
const std::optional<ResourceClientResponseSignonState>&
GoldSrcHandshakeCoordinator::resource_client_response_result() const noexcept
{
    static const std::optional<ResourceClientResponseSignonState> empty;
    return resource_client_response_stage_
               ? resource_client_response_stage_->result()
               : empty;
}
const std::optional<ResourceClientResponseStageError>&
GoldSrcHandshakeCoordinator::resource_client_response_error() const noexcept
{
    static const std::optional<ResourceClientResponseStageError> empty;
    return resource_client_response_stage_
               ? resource_client_response_stage_->error()
               : empty;
}
NetchanSession* GoldSrcHandshakeCoordinator::netchan_session() noexcept
{
    return netchan_stage_ ? netchan_stage_->persistent_session() : nullptr;
}
const NetchanSession* GoldSrcHandshakeCoordinator::netchan_session() const noexcept
{
    return netchan_stage_ ? netchan_stage_->persistent_session() : nullptr;
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
    if (response_stage_ && response_stage_->error()) {
        return response_stage_->error()->context;
    }
    if (netchan_stage_ && netchan_stage_->error()) {
        return netchan_stage_->error()->context;
    }
    if (signon_stage_ && signon_stage_->error()) {
        return signon_stage_->error()->context;
    }
    if (pre_resource_stage_ && pre_resource_stage_->error()) {
        return pre_resource_stage_->error()->context;
    }
    if (delta_description_stage_ && delta_description_stage_->error()) {
        return delta_description_stage_->error()->context;
    }
    if (movement_environment_stage_ && movement_environment_stage_->error()) {
        return movement_environment_stage_->error()->context;
    }
    if (user_info_stage_ && user_info_stage_->error()) {
        return user_info_stage_->error()->context;
    }
    if (resource_transition_stage_ && resource_transition_stage_->error()) {
        return resource_transition_stage_->error()->context;
    }
    if (resource_list_stage_ && resource_list_stage_->error()) {
        return resource_list_stage_->error()->context;
    }
    if (resource_client_response_stage_ &&
        resource_client_response_stage_->error()) {
        return resource_client_response_stage_->error()->context;
    }
    if (challenge_exchange_.error()) {
        return challenge_exchange_.error()->context;
    }
    return {};
}

void GoldSrcHandshakeCoordinator::synchronize_from_challenge(
    const ChallengeExchangeTimePoint now)
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
    if (state_ != GoldSrcHandshakeState::request_sent ||
        (stop_point_ != HandshakeStopPoint::connect_response &&
         stop_point_ != HandshakeStopPoint::netchan_bootstrap &&
         stop_point_ != HandshakeStopPoint::signon_boundary &&
         stop_point_ != HandshakeStopPoint::pre_resource &&
         stop_point_ != HandshakeStopPoint::delta_schemas &&
         stop_point_ != HandshakeStopPoint::movevars &&
         stop_point_ != HandshakeStopPoint::user_info &&
         stop_point_ != HandshakeStopPoint::resource_list_boundary &&
         stop_point_ != HandshakeStopPoint::resource_list &&
         stop_point_ != HandshakeStopPoint::resource_response_boundary)) {
        return;
    }
    if (!response_stage_) {
        state_ = GoldSrcHandshakeState::configuration_error;
        configuration_error_ = "Connect-response mode has no response wait stage";
        return;
    }
    static_cast<void>(response_stage_->start(now, *challenge_exchange_.local_endpoint()));
    synchronize_from_response(now);
}

void GoldSrcHandshakeCoordinator::synchronize_from_response(
    const ChallengeExchangeTimePoint now)
{
    if (!response_stage_) {
        state_ = GoldSrcHandshakeState::configuration_error;
        configuration_error_ = "Connect-response mode has no response wait stage";
        return;
    }
    state_ = map_response_state(response_stage_->state());
    if (response_stage_->error() &&
        response_stage_->error()->code ==
            ConnectResponseWaitErrorCode::invalid_configuration) {
        state_ = GoldSrcHandshakeState::configuration_error;
    }
    if (state_ != GoldSrcHandshakeState::accepted ||
        (stop_point_ != HandshakeStopPoint::netchan_bootstrap &&
         stop_point_ != HandshakeStopPoint::signon_boundary &&
         stop_point_ != HandshakeStopPoint::pre_resource &&
         stop_point_ != HandshakeStopPoint::delta_schemas &&
         stop_point_ != HandshakeStopPoint::movevars &&
          stop_point_ != HandshakeStopPoint::user_info &&
          stop_point_ != HandshakeStopPoint::resource_list_boundary &&
          stop_point_ != HandshakeStopPoint::resource_list &&
          stop_point_ != HandshakeStopPoint::resource_response_boundary)) {
        return;
    }
    if (!challenge_exchange_.local_endpoint() ||
        (stop_point_ == HandshakeStopPoint::netchan_bootstrap && !netchan_stage_) ||
        (stop_point_ == HandshakeStopPoint::signon_boundary && !signon_stage_) ||
        (stop_point_ == HandshakeStopPoint::pre_resource && !pre_resource_stage_) ||
        (stop_point_ == HandshakeStopPoint::delta_schemas &&
         !delta_description_stage_) ||
        (stop_point_ == HandshakeStopPoint::movevars &&
         !movement_environment_stage_) ||
        (stop_point_ == HandshakeStopPoint::user_info && !user_info_stage_) ||
        (stop_point_ == HandshakeStopPoint::resource_list_boundary &&
         !resource_transition_stage_) ||
         (stop_point_ == HandshakeStopPoint::resource_list &&
          !resource_list_stage_) ||
         (stop_point_ == HandshakeStopPoint::resource_response_boundary &&
          !resource_client_response_stage_)) {
        state_ = GoldSrcHandshakeState::configuration_error;
        configuration_error_ =
            "Post-ACCEPT mode has no transport stage or stable local endpoint";
        return;
    }
    std::unique_ptr<INetchanDriverLifetime> driver_lifetime;
    if (authentication_session_) {
        driver_lifetime = std::make_unique<AuthenticationDriverLifetime>(
            std::move(*authentication_session_));
        // From ACCEPT onward the driver is the sole lifetime owner. Resetting
        // the moved-from optional cannot release the provider guard.
        authentication_session_.reset();
    }
    if (stop_point_ == HandshakeStopPoint::netchan_bootstrap) {
        static_cast<void>(netchan_stage_->start(
            now,
            *challenge_exchange_.local_endpoint(),
            std::move(driver_lifetime)));
        synchronize_from_netchan();
        return;
    }
    if (stop_point_ == HandshakeStopPoint::pre_resource) {
        const bool pre_resource_started = pre_resource_stage_->start(
            now,
            *challenge_exchange_.local_endpoint(),
            std::move(driver_lifetime));
        synchronize_from_pre_resource();
        if (!pre_resource_started &&
            state_ == GoldSrcHandshakeState::waiting_for_pre_resource) {
            // start() is transactional and intentionally leaves the stage idle
            // on failure. Never turn that idle/error pair into a nonterminal wait.
            state_ = GoldSrcHandshakeState::protocol_error;
        }
        return;
    }
    if (stop_point_ == HandshakeStopPoint::delta_schemas) {
        const bool delta_started = delta_description_stage_->start(
            now,
            *challenge_exchange_.local_endpoint(),
            std::move(driver_lifetime));
        synchronize_from_delta_description();
        if (!delta_started &&
            state_ == GoldSrcHandshakeState::waiting_for_delta_schemas) {
            state_ = GoldSrcHandshakeState::protocol_error;
        }
        return;
    }
    if (stop_point_ == HandshakeStopPoint::movevars) {
        const bool movement_started = movement_environment_stage_->start(
            now,
            *challenge_exchange_.local_endpoint(),
            std::move(driver_lifetime));
        synchronize_from_movement_environment();
        if (!movement_started &&
            state_ == GoldSrcHandshakeState::waiting_for_movevars) {
            state_ = GoldSrcHandshakeState::protocol_error;
        }
        return;
    }
    if (stop_point_ == HandshakeStopPoint::user_info) {
        const bool user_info_started = user_info_stage_->start(
            now,
            *challenge_exchange_.local_endpoint(),
            std::move(driver_lifetime));
        synchronize_from_user_info();
        if (!user_info_started &&
            state_ == GoldSrcHandshakeState::waiting_for_user_info) {
            state_ = GoldSrcHandshakeState::protocol_error;
        }
        return;
    }
    if (stop_point_ == HandshakeStopPoint::resource_list_boundary) {
        const bool transition_started = resource_transition_stage_->start(
            now,
            *challenge_exchange_.local_endpoint(),
            std::move(driver_lifetime));
        synchronize_from_resource_transition();
        if (!transition_started &&
            state_ == GoldSrcHandshakeState::waiting_for_resource_transition) {
            state_ = GoldSrcHandshakeState::protocol_error;
        }
        return;
    }
    if (stop_point_ == HandshakeStopPoint::resource_list) {
        const bool resource_list_started = resource_list_stage_->start(
            now,
            *challenge_exchange_.local_endpoint(),
            std::move(driver_lifetime));
        synchronize_from_resource_list();
        if (!resource_list_started &&
            state_ == GoldSrcHandshakeState::waiting_for_resource_list) {
            state_ = GoldSrcHandshakeState::protocol_error;
        }
        return;
    }
    if (stop_point_ == HandshakeStopPoint::resource_response_boundary) {
        const bool resource_response_started =
            resource_client_response_stage_->start(
                now,
                *challenge_exchange_.local_endpoint(),
                std::move(driver_lifetime));
        synchronize_from_resource_client_response();
        if (!resource_response_started &&
            state_ == GoldSrcHandshakeState::waiting_for_resource_response) {
            state_ = GoldSrcHandshakeState::protocol_error;
        }
        return;
    }
    const bool signon_started = signon_stage_->start(
        now,
        *challenge_exchange_.local_endpoint(),
        std::move(driver_lifetime));
    synchronize_from_signon();
    if (!signon_started && state_ == GoldSrcHandshakeState::waiting_for_signon) {
        // start() is transactional and intentionally leaves the stage idle on
        // failure. Never turn that idle/error pair into a nonterminal wait.
        state_ = GoldSrcHandshakeState::protocol_error;
    }
}

void GoldSrcHandshakeCoordinator::synchronize_from_netchan()
{
    if (!netchan_stage_) {
        state_ = GoldSrcHandshakeState::configuration_error;
        configuration_error_ = "Netchan mode has no bootstrap stage";
        return;
    }
    state_ = map_netchan_state(netchan_stage_->state());
    if (netchan_stage_->error() &&
        netchan_stage_->error()->code ==
            NetchanBootstrapErrorCode::invalid_configuration) {
        state_ = GoldSrcHandshakeState::configuration_error;
    }
}

void GoldSrcHandshakeCoordinator::synchronize_from_signon()
{
    if (!signon_stage_) {
        state_ = GoldSrcHandshakeState::configuration_error;
        configuration_error_ = "Sign-on mode has no initial sign-on stage";
        return;
    }
    const auto& signon_error = signon_stage_->error();
    if (signon_stage_->state() == InitialSignonState::idle && signon_error) {
        if (signon_error->code == InitialSignonErrorCode::invalid_configuration ||
            signon_error->driver_code ==
                NetchanDriverErrorCode::invalid_configuration) {
            state_ = GoldSrcHandshakeState::configuration_error;
        } else if (signon_start_network_failure(signon_error->driver_code)) {
            state_ = GoldSrcHandshakeState::network_error;
        } else {
            state_ = GoldSrcHandshakeState::protocol_error;
        }
        return;
    }
    state_ = map_signon_state(signon_stage_->state());
    if (signon_error &&
        signon_error->code == InitialSignonErrorCode::invalid_configuration) {
        state_ = GoldSrcHandshakeState::configuration_error;
    }
}

void GoldSrcHandshakeCoordinator::synchronize_from_pre_resource()
{
    if (!pre_resource_stage_) {
        state_ = GoldSrcHandshakeState::configuration_error;
        configuration_error_ =
            "Pre-resource mode has no pre-resource sign-on stage";
        return;
    }
    const auto& pre_resource_error = pre_resource_stage_->error();
    if (pre_resource_stage_->state() == PreResourceSignonStageState::idle &&
        pre_resource_error) {
        if (pre_resource_error->code ==
                PreResourceSignonErrorCode::invalid_configuration ||
            pre_resource_error->driver_code ==
                NetchanDriverErrorCode::invalid_configuration) {
            state_ = GoldSrcHandshakeState::configuration_error;
        } else if (signon_start_network_failure(
                       pre_resource_error->driver_code)) {
            state_ = GoldSrcHandshakeState::network_error;
        } else {
            state_ = GoldSrcHandshakeState::protocol_error;
        }
        return;
    }
    state_ = map_pre_resource_state(pre_resource_stage_->state());
    if (pre_resource_error &&
        pre_resource_error->code ==
            PreResourceSignonErrorCode::invalid_configuration) {
        state_ = GoldSrcHandshakeState::configuration_error;
    }
}

void GoldSrcHandshakeCoordinator::synchronize_from_delta_description()
{
    if (!delta_description_stage_) {
        state_ = GoldSrcHandshakeState::configuration_error;
        configuration_error_ =
            "Delta-schema mode has no delta-description stage";
        return;
    }
    const auto& delta_error = delta_description_stage_->error();
    if (delta_description_stage_->state() == DeltaDescriptionStageState::idle &&
        delta_error) {
        if (delta_error->code ==
                DeltaDescriptionStageErrorCode::invalid_configuration ||
            delta_error->driver_code ==
                NetchanDriverErrorCode::invalid_configuration) {
            state_ = GoldSrcHandshakeState::configuration_error;
        } else if (signon_start_network_failure(delta_error->driver_code)) {
            state_ = GoldSrcHandshakeState::network_error;
        } else {
            state_ = GoldSrcHandshakeState::protocol_error;
        }
        return;
    }
    state_ = map_delta_description_state(delta_description_stage_->state());
    if (delta_error &&
        delta_error->code ==
            DeltaDescriptionStageErrorCode::invalid_configuration) {
        state_ = GoldSrcHandshakeState::configuration_error;
    }
}

void GoldSrcHandshakeCoordinator::synchronize_from_movement_environment()
{
    if (!movement_environment_stage_) {
        state_ = GoldSrcHandshakeState::configuration_error;
        configuration_error_ =
            "Movevars mode has no movement/environment stage";
        return;
    }
    const auto& movement_error = movement_environment_stage_->error();
    if (movement_environment_stage_->state() ==
            MovementEnvironmentStageState::idle &&
        movement_error) {
        if (movement_error->code ==
                MovementEnvironmentStageErrorCode::invalid_configuration ||
            movement_error->driver_code ==
                NetchanDriverErrorCode::invalid_configuration) {
            state_ = GoldSrcHandshakeState::configuration_error;
        } else if (signon_start_network_failure(movement_error->driver_code)) {
            state_ = GoldSrcHandshakeState::network_error;
        } else {
            state_ = GoldSrcHandshakeState::protocol_error;
        }
        return;
    }
    state_ = map_movement_environment_state(
        movement_environment_stage_->state());
    if (movement_error &&
        movement_error->code ==
            MovementEnvironmentStageErrorCode::invalid_configuration) {
        state_ = GoldSrcHandshakeState::configuration_error;
    }
}

void GoldSrcHandshakeCoordinator::synchronize_from_user_info()
{
    if (!user_info_stage_) {
        state_ = GoldSrcHandshakeState::configuration_error;
        configuration_error_ = "User-info mode has no user-info sign-on stage";
        return;
    }
    const auto& stage_error = user_info_stage_->error();
    if (user_info_stage_->state() == UserInfoSignonStageState::idle &&
        stage_error) {
        if (stage_error->code ==
                UserInfoSignonStageErrorCode::invalid_configuration ||
            stage_error->driver_code ==
                NetchanDriverErrorCode::invalid_configuration) {
            state_ = GoldSrcHandshakeState::configuration_error;
        } else if (signon_start_network_failure(stage_error->driver_code)) {
            state_ = GoldSrcHandshakeState::network_error;
        } else {
            state_ = GoldSrcHandshakeState::protocol_error;
        }
        return;
    }
    state_ = map_user_info_state(user_info_stage_->state());
    if (stage_error &&
        stage_error->code == UserInfoSignonStageErrorCode::invalid_configuration) {
        state_ = GoldSrcHandshakeState::configuration_error;
    }
}

void GoldSrcHandshakeCoordinator::synchronize_from_resource_transition()
{
    if (!resource_transition_stage_) {
        state_ = GoldSrcHandshakeState::configuration_error;
        configuration_error_ =
            "Resource-transition mode has no resource-transition stage";
        return;
    }
    const auto& stage_error = resource_transition_stage_->error();
    if (resource_transition_stage_->state() ==
            ResourceTransitionStageState::idle &&
        stage_error) {
        if (stage_error->code ==
                ResourceTransitionStageErrorCode::invalid_configuration ||
            stage_error->driver_code ==
                NetchanDriverErrorCode::invalid_configuration) {
            state_ = GoldSrcHandshakeState::configuration_error;
        } else if (signon_start_network_failure(stage_error->driver_code)) {
            state_ = GoldSrcHandshakeState::network_error;
        } else {
            state_ = GoldSrcHandshakeState::protocol_error;
        }
        return;
    }
    state_ = map_resource_transition_state(resource_transition_stage_->state());
    if (stage_error &&
        stage_error->code ==
            ResourceTransitionStageErrorCode::invalid_configuration) {
        state_ = GoldSrcHandshakeState::configuration_error;
    }
}

void GoldSrcHandshakeCoordinator::synchronize_from_resource_list()
{
    if (!resource_list_stage_) {
        state_ = GoldSrcHandshakeState::configuration_error;
        configuration_error_ =
            "Resource-list mode has no resource-list stage";
        return;
    }
    const auto& stage_error = resource_list_stage_->error();
    if (resource_list_stage_->state() == ResourceListStageState::idle &&
        stage_error) {
        if (stage_error->code ==
                ResourceListStageErrorCode::invalid_configuration ||
            stage_error->driver_code ==
                NetchanDriverErrorCode::invalid_configuration) {
            state_ = GoldSrcHandshakeState::configuration_error;
        } else if (signon_start_network_failure(stage_error->driver_code)) {
            state_ = GoldSrcHandshakeState::network_error;
        } else {
            state_ = GoldSrcHandshakeState::protocol_error;
        }
        return;
    }
    state_ = map_resource_list_state(resource_list_stage_->state());
    if (stage_error &&
        stage_error->code == ResourceListStageErrorCode::invalid_configuration) {
        state_ = GoldSrcHandshakeState::configuration_error;
    }
}

void GoldSrcHandshakeCoordinator::synchronize_from_resource_client_response()
{
    if (!resource_client_response_stage_) {
        state_ = GoldSrcHandshakeState::configuration_error;
        configuration_error_ =
            "Resource-response mode has no resource-client-response stage";
        return;
    }
    const auto& stage_error = resource_client_response_stage_->error();
    if (resource_client_response_stage_->state() ==
            ResourceClientResponseStageState::idle &&
        stage_error) {
        if (stage_error->code ==
                ResourceClientResponseStageErrorCode::invalid_configuration ||
            stage_error->driver_code ==
                NetchanDriverErrorCode::invalid_configuration) {
            state_ = GoldSrcHandshakeState::configuration_error;
        } else if (signon_start_network_failure(stage_error->driver_code)) {
            state_ = GoldSrcHandshakeState::network_error;
        } else {
            state_ = GoldSrcHandshakeState::protocol_error;
        }
        return;
    }
    state_ = map_resource_client_response_state(
        resource_client_response_stage_->state());
    if (stage_error &&
        stage_error->code ==
            ResourceClientResponseStageErrorCode::invalid_configuration) {
        state_ = GoldSrcHandshakeState::configuration_error;
    }
}

void GoldSrcHandshakeCoordinator::release_authentication_session_if_terminal()
{
    if (terminal()) {
        authentication_session_.reset();
    }
}

} // namespace hlclient::goldsrc
