#include <hlclient/goldsrc/pre_resource_signon_stage.hpp>

#include <limits>
#include <type_traits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool terminal_state(
    const PreResourceSignonStageState state) noexcept
{
    switch (state) {
    case PreResourceSignonStageState::pre_resource_boundary_reached:
    case PreResourceSignonStageState::unsupported_message:
    case PreResourceSignonStageState::timed_out:
    case PreResourceSignonStageState::cancelled:
    case PreResourceSignonStageState::backpressure:
    case PreResourceSignonStageState::secondary_stream_pending_m3:
    case PreResourceSignonStageState::network_error:
    case PreResourceSignonStageState::protocol_error:
        return true;
    case PreResourceSignonStageState::idle:
    case PreResourceSignonStageState::waiting_for_initial_boundary:
    case PreResourceSignonStageState::decoding_server_info:
    case PreResourceSignonStageState::server_info_ready:
    case PreResourceSignonStageState::decoding_pre_resource_messages:
        return false;
    }
    return true;
}

[[nodiscard]] bool initial_driver_network_error(
    const std::optional<NetchanDriverErrorCode> code) noexcept
{
    if (!code) {
        return false;
    }
    switch (*code) {
    case NetchanDriverErrorCode::local_endpoint_unavailable:
    case NetchanDriverErrorCode::local_endpoint_changed:
    case NetchanDriverErrorCode::receive_failed:
    case NetchanDriverErrorCode::inconsistent_receive_result:
    case NetchanDriverErrorCode::send_failed:
        return true;
    case NetchanDriverErrorCode::invalid_configuration:
    case NetchanDriverErrorCode::not_active:
    case NetchanDriverErrorCode::reentrant_operation:
    case NetchanDriverErrorCode::time_moved_backwards:
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
    case NetchanDriverErrorCode::fragment_transfer_timed_out:
    case NetchanDriverErrorCode::channel_inactivity_timed_out:
    case NetchanDriverErrorCode::event_backpressure:
        return false;
    }
    return false;
}

[[nodiscard]] bool unsupported_continuation(
    const PreResourceServiceErrorCode code) noexcept
{
    return code == PreResourceServiceErrorCode::duplicate_server_info ||
           code ==
               PreResourceServiceErrorCode::unsupported_post_server_info_opcode;
}

[[nodiscard]] PreResourceSignonTraceClassification trace_for_terminal_state(
    const PreResourceSignonStageState state) noexcept
{
    switch (state) {
    case PreResourceSignonStageState::timed_out:
        return PreResourceSignonTraceClassification::stage_timed_out;
    case PreResourceSignonStageState::cancelled:
        return PreResourceSignonTraceClassification::stage_cancelled;
    case PreResourceSignonStageState::backpressure:
        return PreResourceSignonTraceClassification::backpressure;
    case PreResourceSignonStageState::secondary_stream_pending_m3:
        return PreResourceSignonTraceClassification::secondary_stream_pending_m3;
    case PreResourceSignonStageState::network_error:
        return PreResourceSignonTraceClassification::network_failure;
    case PreResourceSignonStageState::unsupported_message:
        return PreResourceSignonTraceClassification::unsupported_message;
    case PreResourceSignonStageState::protocol_error:
        return PreResourceSignonTraceClassification::protocol_failure;
    case PreResourceSignonStageState::pre_resource_boundary_reached:
        return PreResourceSignonTraceClassification::pre_resource_boundary_reached;
    case PreResourceSignonStageState::idle:
    case PreResourceSignonStageState::waiting_for_initial_boundary:
    case PreResourceSignonStageState::decoding_server_info:
    case PreResourceSignonStageState::server_info_ready:
    case PreResourceSignonStageState::decoding_pre_resource_messages:
        return PreResourceSignonTraceClassification::protocol_failure;
    }
    return PreResourceSignonTraceClassification::protocol_failure;
}

} // namespace

bool valid_pre_resource_signon_configuration(
    const PreResourceSignonConfig& config) noexcept
{
    return valid_initial_signon_configuration(config.initial_signon) &&
           config.maximum_events > 0U &&
           config.maximum_events <= kMaximumPreResourceSignonEvents;
}

PreResourceSignonStage::PreResourceSignonStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    PreResourceSignonConfig config,
    InitialSignonTraceCallback initial_trace_callback,
    PreResourceSignonTraceCallback trace_callback)
    : PreResourceSignonStage{
          transport,
          remote_endpoint,
          std::move(config),
          std::move(initial_trace_callback),
          std::move(trace_callback),
          false}
{
}

PreResourceSignonStage::PreResourceSignonStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    PreResourceSignonConfig config,
    InitialSignonTraceCallback initial_trace_callback,
    PreResourceSignonTraceCallback trace_callback,
    RetainConnectionAtBoundary)
    : PreResourceSignonStage{
          transport,
          remote_endpoint,
          std::move(config),
          std::move(initial_trace_callback),
          std::move(trace_callback),
          true}
{
}

PreResourceSignonStage::PreResourceSignonStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    PreResourceSignonConfig config,
    InitialSignonTraceCallback initial_trace_callback,
    PreResourceSignonTraceCallback trace_callback,
    const bool retain_connection_at_boundary)
    : config_{std::move(config)},
      trace_callback_{std::move(trace_callback)},
      configuration_valid_{valid_pre_resource_signon_configuration(config_)},
      retain_connection_at_boundary_{retain_connection_at_boundary},
      initial_stage_{
          transport,
          remote_endpoint,
          config_.initial_signon,
          [this, callback = std::move(initial_trace_callback)](
              const InitialSignonTraceEvent& event) {
              if (!callback || trace_callback_active_) {
                  return;
              }
              trace_callback_active_ = true;
              try {
                  callback(event);
              } catch (...) {
              }
              trace_callback_active_ = false;
          },
          InitialSignonStage::RetainConnectionAtBoundary{}},
      event_slots_(configuration_valid_ ? config_.maximum_events : 0U)
{
}

PreResourceSignonStage::~PreResourceSignonStage() = default;

bool PreResourceSignonStage::start(
    const PreResourceSignonTimePoint now,
    const network::NetworkAddress& expected_local_endpoint,
    std::unique_ptr<INetchanDriverLifetime> connection_lifetime)
{
    if (trace_callback_active_ || state_ != PreResourceSignonStageState::idle) {
        return false;
    }
    error_.reset();
    if (!configuration_valid_) {
        set_error(
            PreResourceSignonErrorCode::invalid_configuration,
            PreResourceSignonStageState::protocol_error,
            "Pre-resource sign-on configuration is outside project bounds");
        emit_trace(PreResourceSignonTraceClassification::protocol_failure);
        return false;
    }

    bool started = false;
    try {
        started = initial_stage_.start(
            now,
            expected_local_endpoint,
            std::move(connection_lifetime));
    } catch (...) {
        try {
            initial_stage_.cancel(now);
        } catch (...) {
        }
        initial_stage_.finalize_retained_boundary(now);
        set_error(
            PreResourceSignonErrorCode::initial_signon_start_failed,
            PreResourceSignonStageState::protocol_error,
            "Nested initial sign-on stage threw during start");
        emit_trace(PreResourceSignonTraceClassification::protocol_failure);
        return false;
    }
    drain_initial_events();
    if (!started) {
        fail_from_initial();
        if (error_) {
            error_->code =
                PreResourceSignonErrorCode::initial_signon_start_failed;
        } else {
            set_error(
                PreResourceSignonErrorCode::initial_signon_start_failed,
                PreResourceSignonStageState::protocol_error,
                "Nested initial sign-on stage rejected start");
            emit_trace(PreResourceSignonTraceClassification::protocol_failure);
        }
        return false;
    }

    state_ = PreResourceSignonStageState::waiting_for_initial_boundary;
    emit_trace(PreResourceSignonTraceClassification::stage_started);
    return true;
}

void PreResourceSignonStage::update(const PreResourceSignonTimePoint now)
{
    if (trace_callback_active_ || state_ == PreResourceSignonStageState::idle ||
        terminal_state(state_)) {
        return;
    }
    if (state_ != PreResourceSignonStageState::waiting_for_initial_boundary) {
        return;
    }

    drain_initial_events();
    try {
        initial_stage_.update(now);
    } catch (...) {
        try {
            initial_stage_.cancel(now);
        } catch (...) {
        }
        initial_stage_.finalize_retained_boundary(now);
        set_error(
            PreResourceSignonErrorCode::initial_signon_failed,
            PreResourceSignonStageState::protocol_error,
            "Nested initial sign-on stage threw during update");
        emit_trace(PreResourceSignonTraceClassification::protocol_failure);
        return;
    }
    drain_initial_events();
    synchronize_from_initial(now);
}

void PreResourceSignonStage::cancel(const PreResourceSignonTimePoint now)
{
    if (trace_callback_active_ || state_ == PreResourceSignonStageState::idle ||
        terminal_state(state_)) {
        return;
    }
    try {
        initial_stage_.cancel(now);
    } catch (...) {
        initial_stage_.finalize_retained_boundary(now);
        set_error(
            PreResourceSignonErrorCode::initial_signon_failed,
            PreResourceSignonStageState::protocol_error,
            "Nested initial sign-on stage threw during cancellation");
        emit_trace(PreResourceSignonTraceClassification::protocol_failure);
        return;
    }
    drain_initial_events();
    state_ = PreResourceSignonStageState::cancelled;
    emit_trace(PreResourceSignonTraceClassification::stage_cancelled);
}

std::optional<PreResourceSignonEvent> PreResourceSignonStage::poll_event()
{
    if (event_size_ == 0U) {
        return std::nullopt;
    }
    auto event = std::move(event_slots_[event_head_]);
    event_slots_[event_head_].reset();
    event_head_ = (event_head_ + 1U) % event_slots_.size();
    --event_size_;
    return event;
}

PreResourceSignonStageState PreResourceSignonStage::state() const noexcept
{
    return state_;
}

bool PreResourceSignonStage::terminal() const noexcept
{
    return terminal_state(state_);
}

const std::optional<PreResourceSignonState>&
PreResourceSignonStage::result() const noexcept
{
    return result_;
}

const std::optional<PreResourceSignonError>&
PreResourceSignonStage::error() const noexcept
{
    return error_;
}

const network::NetworkAddress&
PreResourceSignonStage::remote_endpoint() const noexcept
{
    return initial_stage_.remote_endpoint();
}

const std::optional<network::NetworkAddress>&
PreResourceSignonStage::local_endpoint() const noexcept
{
    return initial_stage_.local_endpoint();
}

std::size_t PreResourceSignonStage::pending_event_count() const noexcept
{
    return event_size_;
}

std::size_t PreResourceSignonStage::transmitted_packet_count() const noexcept
{
    return initial_stage_.transmitted_packet_count();
}

std::size_t PreResourceSignonStage::cleanup_count() const noexcept
{
    return initial_stage_.cleanup_count();
}

std::size_t PreResourceSignonStage::request_queue_count() const noexcept
{
    return initial_stage_.request_queue_count();
}

bool PreResourceSignonStage::can_push_events(const std::size_t count) const noexcept
{
    return count <= event_slots_.size() - event_size_;
}

void PreResourceSignonStage::push_event(PreResourceSignonEvent event) noexcept
{
    const auto index = (event_head_ + event_size_) % event_slots_.size();
    event_slots_[index].emplace(std::move(event));
    ++event_size_;
}

void PreResourceSignonStage::drain_initial_events() noexcept
{
    // The composite owns the nested stage, and its M2.4.1 events are internal
    // progress signals here. Draining between bounded updates prevents the
    // nested ring from becoming a second externally visible event queue.
    while (initial_stage_.poll_event()) {
    }
}

void PreResourceSignonStage::synchronize_from_initial(
    const PreResourceSignonTimePoint now)
{
    if (initial_stage_.state() == InitialSignonState::signon_boundary_reached) {
        const auto& initial_result = initial_stage_.result();
        if (!initial_result) {
            fail_after_retained_boundary(
                PreResourceSignonErrorCode::initial_signon_failed,
                PreResourceSignonStageState::protocol_error,
                "Nested sign-on boundary has no owning result",
                now);
            return;
        }
        emit_trace(
            PreResourceSignonTraceClassification::initial_boundary_reached,
            static_cast<std::uint8_t>(initial_result->boundary.opcode),
            initial_result->boundary.byte_offset,
            initial_result->boundary.remaining_byte_count);
        decode_retained_boundary(now);
        return;
    }
    if (initial_stage_.terminal() || initial_stage_.error()) {
        fail_from_initial();
    }
}

void PreResourceSignonStage::decode_retained_boundary(
    const PreResourceSignonTimePoint now)
{
    const auto& initial_result = initial_stage_.result();
    if (!initial_result) {
        fail_after_retained_boundary(
            PreResourceSignonErrorCode::initial_signon_failed,
            PreResourceSignonStageState::protocol_error,
            "Nested sign-on result disappeared before continuation",
            now);
        return;
    }

    state_ = PreResourceSignonStageState::decoding_server_info;
    std::optional<PreResourceServiceDecodeResult> decoded;
    try {
        const ServiceMessageStreamDecoder decoder{
            config_.initial_signon.service_messages};
        decoded.emplace(decoder.continue_to_pre_resource(
            initial_result->boundary_payload,
            initial_result->boundary));
    } catch (...) {
        fail_after_retained_boundary(
            PreResourceSignonErrorCode::pre_resource_decode_failed,
            PreResourceSignonStageState::protocol_error,
            "Bounded pre-resource service continuation threw",
            now);
        return;
    }

    if (!*decoded || !decoded->state) {
        const auto service_code = decoded->error
            ? std::optional{decoded->error->code}
            : std::nullopt;
        const auto server_info_code = decoded->error
            ? decoded->error->server_info_code
            : std::nullopt;
        const bool unsupported = service_code &&
            unsupported_continuation(*service_code);
        fail_after_retained_boundary(
            unsupported ? PreResourceSignonErrorCode::unsupported_message
                        : PreResourceSignonErrorCode::pre_resource_decode_failed,
            unsupported ? PreResourceSignonStageState::unsupported_message
                        : PreResourceSignonStageState::protocol_error,
            decoded->error
                ? std::string_view{decoded->error->context}
                : "Pre-resource continuation returned no owning state",
            now,
            service_code,
            server_info_code);
        return;
    }

    const auto& candidate = *decoded->state;
    if (candidate.controls().size() >
        (std::numeric_limits<std::size_t>::max)() - 2U ||
        decoded->required_event_count != candidate.controls().size() + 2U) {
        fail_after_retained_boundary(
            PreResourceSignonErrorCode::pre_resource_decode_failed,
            PreResourceSignonStageState::protocol_error,
            "Pre-resource decoder returned inconsistent event metadata",
            now);
        return;
    }
    if (decoded->required_event_count > config_.maximum_events ||
        !can_push_events(decoded->required_event_count)) {
        fail_after_retained_boundary(
            PreResourceSignonErrorCode::event_backpressure,
            PreResourceSignonStageState::backpressure,
            "Decoded pre-resource state exceeds bounded event capacity",
            now);
        return;
    }
    if (candidate.boundary().direction() !=
        ResourcePhaseBoundaryDirection::server_message) {
        fail_after_retained_boundary(
            PreResourceSignonErrorCode::pre_resource_decode_failed,
            PreResourceSignonStageState::protocol_error,
            "Captured pre-resource profile requires a server-message boundary",
            now);
        return;
    }

    const auto& source = candidate.source_payload();
    push_event(PreResourceSignonEvent{
        PreResourceSignonEventType::server_info_ready,
        static_cast<std::uint8_t>(ServiceMessageOpcode::complex_signon_boundary),
        source.server_info_body_offset(),
        source.server_info_body_size(),
        0U,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        now,
    });
    for (const auto& control : candidate.controls()) {
        push_event(PreResourceSignonEvent{
            PreResourceSignonEventType::pre_resource_control,
            control.opcode(),
            control.byte_offset(),
            control.byte_count(),
            control.string_length(),
            control.control_value(),
            std::nullopt,
            std::nullopt,
            now,
        });
    }
    const auto& boundary = candidate.boundary();
    push_event(PreResourceSignonEvent{
        PreResourceSignonEventType::resource_phase_boundary,
        boundary.opcode(),
        boundary.byte_offset(),
        boundary.remaining_byte_count(),
        0U,
        std::nullopt,
        boundary.direction(),
        boundary.evidence_status(),
        now,
    });

    state_ = PreResourceSignonStageState::server_info_ready;
    result_.emplace(std::move(*decoded->state));
    state_ = PreResourceSignonStageState::pre_resource_boundary_reached;
    if (!retain_connection_at_boundary_) {
        initial_stage_.finalize_retained_boundary(now);
    }

    const auto& committed_source = result_->source_payload();
    emit_trace(
        PreResourceSignonTraceClassification::server_info_ready,
        static_cast<std::uint8_t>(ServiceMessageOpcode::complex_signon_boundary),
        committed_source.server_info_body_offset(),
        committed_source.server_info_body_size(),
        0U,
        &result_->server_info());
    for (const auto& control : result_->controls()) {
        emit_trace(
            PreResourceSignonTraceClassification::pre_resource_control,
            control.opcode(),
            control.byte_offset(),
            control.byte_count(),
            control.string_length());
    }
    emit_trace(
        PreResourceSignonTraceClassification::pre_resource_boundary_reached,
        result_->boundary().opcode(),
        result_->boundary().byte_offset(),
        result_->boundary().remaining_byte_count(),
        0U,
        nullptr,
        &result_->boundary());
}

const OwnedServicePayload*
PreResourceSignonStage::retained_source_payload() const noexcept
{
    if (!retain_connection_at_boundary_) {
        return nullptr;
    }
    const auto& initial_result = initial_stage_.result();
    return initial_result ? &initial_result->boundary_payload : nullptr;
}

void PreResourceSignonStage::finalize_retained_boundary(
    const PreResourceSignonTimePoint now) noexcept
{
    initial_stage_.finalize_retained_boundary(now);
}

void PreResourceSignonStage::fail_from_initial() noexcept
{
    const auto& initial_error = initial_stage_.error();
    const auto initial_code = initial_error
        ? std::optional{initial_error->code}
        : std::nullopt;
    const auto driver_code = initial_error
        ? initial_error->driver_code
        : std::nullopt;

    switch (initial_stage_.state()) {
    case InitialSignonState::timed_out:
        state_ = PreResourceSignonStageState::timed_out;
        break;
    case InitialSignonState::cancelled:
        state_ = PreResourceSignonStageState::cancelled;
        break;
    case InitialSignonState::network_error:
        state_ = PreResourceSignonStageState::network_error;
        break;
    case InitialSignonState::unsupported_service_message:
        state_ = PreResourceSignonStageState::unsupported_message;
        break;
    case InitialSignonState::backpressure:
        state_ = PreResourceSignonStageState::backpressure;
        break;
    case InitialSignonState::secondary_stream_pending_m3:
        state_ = PreResourceSignonStageState::secondary_stream_pending_m3;
        break;
    case InitialSignonState::idle:
        state_ = initial_driver_network_error(driver_code)
            ? PreResourceSignonStageState::network_error
            : PreResourceSignonStageState::protocol_error;
        break;
    case InitialSignonState::protocol_error:
    case InitialSignonState::signon_boundary_reached:
    case InitialSignonState::waiting_for_request_transmit:
    case InitialSignonState::waiting_for_request_ack:
    case InitialSignonState::waiting_for_server_payload:
    case InitialSignonState::decoding_service_stream:
        state_ = PreResourceSignonStageState::protocol_error;
        break;
    }

    set_error(
        PreResourceSignonErrorCode::initial_signon_failed,
        state_,
        initial_error
            ? std::string_view{initial_error->context}
            : std::string_view{
                  "Nested initial sign-on stage terminated without an error"},
        initial_code,
        std::nullopt,
        std::nullopt,
        driver_code);
    emit_trace(trace_for_terminal_state(state_));
}

void PreResourceSignonStage::fail_after_retained_boundary(
    const PreResourceSignonErrorCode code,
    const PreResourceSignonStageState state,
    const std::string_view context,
    const PreResourceSignonTimePoint now,
    const std::optional<PreResourceServiceErrorCode> service_code,
    const std::optional<ServerInfoErrorCode> server_info_code) noexcept
{
    if (terminal_state(state_)) {
        return;
    }
    result_.reset();
    set_error(
        code,
        state,
        context,
        std::nullopt,
        service_code,
        server_info_code,
        std::nullopt);
    initial_stage_.finalize_retained_boundary(now);
    emit_trace(trace_for_terminal_state(state_));
}

void PreResourceSignonStage::set_error(
    const PreResourceSignonErrorCode code,
    const PreResourceSignonStageState state,
    const std::string_view context,
    const std::optional<InitialSignonErrorCode> initial_signon_code,
    const std::optional<PreResourceServiceErrorCode> service_code,
    const std::optional<ServerInfoErrorCode> server_info_code,
    const std::optional<NetchanDriverErrorCode> driver_code) noexcept
{
    state_ = state;
    error_.reset();
    try {
        error_.emplace();
    } catch (...) {
        return;
    }

    error_->code = code;
    error_->initial_signon_code = initial_signon_code;
    error_->service_code = service_code;
    error_->server_info_code = server_info_code;
    error_->driver_code = driver_code;
    const auto bounded = context.substr(
        0U,
        (std::min)(context.size(), kPreResourceSignonDiagnosticTextLimit));
    try {
        error_->context.assign(bounded.data(), bounded.size());
    } catch (...) {
        error_->context.clear();
    }
}

void PreResourceSignonStage::emit_trace(
    const PreResourceSignonTraceClassification classification,
    const std::optional<std::uint8_t> opcode,
    const std::size_t byte_offset,
    const std::size_t byte_count,
    const std::size_t string_length,
    const ServerInfoState* const server_info,
    const ResourcePhaseBoundary* const boundary) noexcept
{
    if (!trace_callback_ || trace_callback_active_) {
        return;
    }

    PreResourceSignonTraceEvent event;
    event.classification = classification;
    event.state = state_;
    event.endpoint = remote_endpoint();
    event.opcode = opcode;
    event.byte_offset = byte_offset;
    event.byte_count = byte_count;
    event.string_length = string_length;
    if (server_info != nullptr) {
        event.protocol_version =
            static_cast<std::uint32_t>(server_info->protocol_version());
        event.maximum_clients = server_info->maximum_clients().value();
        event.multi_client_mode = server_info->multi_client_mode();
    }
    if (boundary != nullptr) {
        event.boundary_direction = boundary->direction();
        event.evidence_status = boundary->evidence_status();
    }
    event.transmitted_packet_count = transmitted_packet_count();

    trace_callback_active_ = true;
    try {
        trace_callback_(event);
    } catch (...) {
    }
    trace_callback_active_ = false;
}

static_assert(std::is_nothrow_move_constructible_v<PreResourceSignonEvent>);

} // namespace hlclient::goldsrc
