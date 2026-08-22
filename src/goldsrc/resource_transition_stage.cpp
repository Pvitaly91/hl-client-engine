#include <hlclient/goldsrc/resource_transition_stage.hpp>

#include <algorithm>
#include <ranges>
#include <type_traits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool terminal_state(
    const ResourceTransitionStageState state) noexcept
{
    switch (state) {
    case ResourceTransitionStageState::neutral_opcode43_boundary_reached:
    case ResourceTransitionStageState::unsupported_message:
    case ResourceTransitionStageState::timed_out:
    case ResourceTransitionStageState::cancelled:
    case ResourceTransitionStageState::backpressure:
    case ResourceTransitionStageState::secondary_stream_pending:
    case ResourceTransitionStageState::network_error:
    case ResourceTransitionStageState::protocol_error:
        return true;
    case ResourceTransitionStageState::idle:
    case ResourceTransitionStageState::waiting_for_user_info_state:
    case ResourceTransitionStageState::request_ready:
    case ResourceTransitionStageState::waiting_for_request_transmit:
    case ResourceTransitionStageState::waiting_for_request_ack:
    case ResourceTransitionStageState::waiting_for_server_transfer:
    case ResourceTransitionStageState::decoding_transition_control:
        return false;
    }
    return true;
}

[[nodiscard]] ResourceTransitionTraceClassification terminal_trace(
    const ResourceTransitionStageState state) noexcept
{
    switch (state) {
    case ResourceTransitionStageState::neutral_opcode43_boundary_reached:
        return ResourceTransitionTraceClassification::
            neutral_opcode43_boundary_reached;
    case ResourceTransitionStageState::unsupported_message:
        return ResourceTransitionTraceClassification::unsupported_message;
    case ResourceTransitionStageState::timed_out:
        return ResourceTransitionTraceClassification::stage_timed_out;
    case ResourceTransitionStageState::cancelled:
        return ResourceTransitionTraceClassification::stage_cancelled;
    case ResourceTransitionStageState::backpressure:
        return ResourceTransitionTraceClassification::backpressure;
    case ResourceTransitionStageState::secondary_stream_pending:
        return ResourceTransitionTraceClassification::secondary_stream_pending;
    case ResourceTransitionStageState::network_error:
        return ResourceTransitionTraceClassification::network_failure;
    case ResourceTransitionStageState::idle:
    case ResourceTransitionStageState::waiting_for_user_info_state:
    case ResourceTransitionStageState::request_ready:
    case ResourceTransitionStageState::waiting_for_request_transmit:
    case ResourceTransitionStageState::waiting_for_request_ack:
    case ResourceTransitionStageState::waiting_for_server_transfer:
    case ResourceTransitionStageState::decoding_transition_control:
    case ResourceTransitionStageState::protocol_error:
        return ResourceTransitionTraceClassification::protocol_failure;
    }
    return ResourceTransitionTraceClassification::protocol_failure;
}

[[nodiscard]] bool driver_network_error(const NetchanDriverErrorCode code) noexcept
{
    switch (code) {
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

} // namespace

bool valid_resource_transition_stage_configuration(
    const ResourceTransitionStageConfig& config) noexcept
{
    return valid_user_info_signon_stage_configuration(config.user_info) &&
           valid_resource_transition_request_limits(config.request) &&
           valid_resource_transition_control_limits(config.control) &&
           config.maximum_second_service_payload_size > 0U &&
           config.maximum_second_service_payload_size <=
               kMaximumSecondServicePayloadSize &&
           config.maximum_stage_events > 0U &&
           config.maximum_stage_events <=
               kMaximumResourceTransitionStageEvents &&
           config.maximum_driver_events_per_update > 0U &&
           config.maximum_driver_events_per_update <=
               kMaximumResourceTransitionDriverEventsPerUpdate;
}

ResourceTransitionSourcePayloadMetadata::
ResourceTransitionSourcePayloadMetadata(
    const std::size_t compressed_byte_count,
    const std::size_t decompressed_byte_count,
    const std::uint32_t source_sequence,
    const std::uint32_t source_acknowledgement,
    const bool source_reliable,
    const bool reassembled,
    const bool decompressed,
    const bool acknowledgement_reliable,
    const NetchanDirection direction,
    const NetchanDriverTimePoint received_at) noexcept
    : compressed_byte_count_{compressed_byte_count},
      decompressed_byte_count_{decompressed_byte_count},
      source_sequence_{source_sequence},
      source_acknowledgement_{source_acknowledgement},
      source_reliable_{source_reliable},
      reassembled_{reassembled},
      decompressed_{decompressed},
      acknowledgement_reliable_{acknowledgement_reliable},
      direction_{direction},
      received_at_{received_at}
{
}

std::size_t ResourceTransitionSourcePayloadMetadata::compressed_byte_count() const noexcept
{
    return compressed_byte_count_;
}

std::size_t ResourceTransitionSourcePayloadMetadata::decompressed_byte_count() const noexcept
{
    return decompressed_byte_count_;
}

std::uint32_t ResourceTransitionSourcePayloadMetadata::source_sequence() const noexcept
{
    return source_sequence_;
}

std::uint32_t ResourceTransitionSourcePayloadMetadata::source_acknowledgement() const noexcept
{
    return source_acknowledgement_;
}

bool ResourceTransitionSourcePayloadMetadata::source_reliable() const noexcept
{
    return source_reliable_;
}

bool ResourceTransitionSourcePayloadMetadata::reassembled() const noexcept
{
    return reassembled_;
}

bool ResourceTransitionSourcePayloadMetadata::decompressed() const noexcept
{
    return decompressed_;
}

bool ResourceTransitionSourcePayloadMetadata::acknowledgement_reliable() const noexcept
{
    return acknowledgement_reliable_;
}

NetchanDirection ResourceTransitionSourcePayloadMetadata::direction() const noexcept
{
    return direction_;
}

NetchanDriverTimePoint ResourceTransitionSourcePayloadMetadata::received_at() const noexcept
{
    return received_at_;
}

ResourceTransitionState::ResourceTransitionState(
    UserInfoSignonState user_info,
    ResourceTransitionRequest request,
    ResourceTransitionControlState control,
    Opcode43Boundary boundary,
    ResourceTransitionSourcePayloadMetadata source_payload) noexcept
    : user_info_{std::move(user_info)},
      request_{std::move(request)},
      control_{std::move(control)},
      boundary_{std::move(boundary)},
      source_payload_{std::move(source_payload)}
{
}

const UserInfoSignonState& ResourceTransitionState::user_info() const noexcept
{
    return user_info_;
}

const ResourceTransitionRequest& ResourceTransitionState::request() const noexcept
{
    return request_;
}

const ResourceTransitionControlState& ResourceTransitionState::control() const noexcept
{
    return control_;
}

const Opcode43Boundary& ResourceTransitionState::boundary() const noexcept
{
    return boundary_;
}

const ResourceTransitionSourcePayloadMetadata&
ResourceTransitionState::source_payload() const noexcept
{
    return source_payload_;
}

ResourceTransitionEvidenceProfile ResourceTransitionState::evidence_profile() const noexcept
{
    return ResourceTransitionEvidenceProfile::
        bounded_stock_transition_with_neutral_opcode43_boundary;
}

ResourceTransitionStage::ResourceTransitionStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    ResourceTransitionStageConfig config,
    InitialSignonTraceCallback initial_trace_callback,
    PreResourceSignonTraceCallback pre_resource_trace_callback,
    DeltaDescriptionTraceCallback delta_trace_callback,
    MovementEnvironmentTraceCallback movement_trace_callback,
    UserInfoSignonTraceCallback user_info_trace_callback,
    ResourceTransitionTraceCallback trace_callback)
    : config_{std::move(config)},
      trace_callback_{std::move(trace_callback)},
      configuration_valid_{valid_resource_transition_stage_configuration(config_)},
      user_info_stage_{
          transport,
          remote_endpoint,
          config_.user_info,
          std::move(initial_trace_callback),
          std::move(pre_resource_trace_callback),
          std::move(delta_trace_callback),
          std::move(movement_trace_callback),
          std::move(user_info_trace_callback),
          UserInfoSignonStage::RetainConnectionAtBoundary{}},
      event_slots_(configuration_valid_ ? config_.maximum_stage_events : 0U)
{
}

ResourceTransitionStage::~ResourceTransitionStage() = default;

bool ResourceTransitionStage::start(
    const ResourceTransitionStageTimePoint now,
    const network::NetworkAddress& expected_local_endpoint,
    std::unique_ptr<INetchanDriverLifetime> connection_lifetime)
{
    if (trace_callback_active_ || state_ != ResourceTransitionStageState::idle) {
        return false;
    }
    error_.reset();
    if (!configuration_valid_) {
        fail(
            ResourceTransitionStageErrorCode::invalid_configuration,
            ResourceTransitionStageState::protocol_error,
            "Resource-transition configuration is outside project bounds",
            now);
        return false;
    }
    bool started = false;
    try {
        started = user_info_stage_.start(
            now,
            expected_local_endpoint,
            std::move(connection_lifetime));
    } catch (...) {
        try {
            user_info_stage_.cancel(now);
        } catch (...) {
        }
        fail(
            ResourceTransitionStageErrorCode::user_info_stage_start_failed,
            ResourceTransitionStageState::protocol_error,
            "Nested user-info stage threw during start",
            now);
        return false;
    }
    drain_user_info_events();
    if (!started) {
        fail_from_user_info();
        if (error_) {
            error_->code =
                ResourceTransitionStageErrorCode::user_info_stage_start_failed;
        }
        return false;
    }
    last_update_ = now;
    state_ = ResourceTransitionStageState::waiting_for_user_info_state;
    emit_trace(ResourceTransitionTraceClassification::stage_started);
    return true;
}

void ResourceTransitionStage::update(const ResourceTransitionStageTimePoint now)
{
    if (trace_callback_active_ || state_ == ResourceTransitionStageState::idle ||
        terminal_state(state_)) {
        return;
    }
    if (!last_update_ || now < *last_update_) {
        fail(
            ResourceTransitionStageErrorCode::time_moved_backwards,
            ResourceTransitionStageState::protocol_error,
            "Resource-transition update time moved backwards",
            now);
        return;
    }
    last_update_ = now;

    if (state_ == ResourceTransitionStageState::waiting_for_user_info_state) {
        drain_user_info_events();
        try {
            user_info_stage_.update(now);
        } catch (...) {
            try {
                user_info_stage_.cancel(now);
            } catch (...) {
            }
            fail(
                ResourceTransitionStageErrorCode::user_info_stage_failed,
                ResourceTransitionStageState::protocol_error,
                "Nested user-info stage threw during update",
                now);
            return;
        }
        drain_user_info_events();
        synchronize_from_user_info(now);
        return;
    }

    auto* const driver = user_info_stage_.retained_driver();
    if (driver == nullptr) {
        fail(
            ResourceTransitionStageErrorCode::retained_driver_missing,
            ResourceTransitionStageState::protocol_error,
            "Resource transition lost its retained persistent driver",
            now);
        return;
    }

    if (pending_decode_payload_) {
        decode_pending_payload(now);
        if (terminal_state(state_) || pending_decode_payload_) {
            return;
        }
    }

    std::size_t processed_events = 0U;
    drain_driver_events(now, processed_events);
    if (terminal_state(state_) || pending_decode_payload_ ||
        processed_events >= config_.maximum_driver_events_per_update) {
        if (pending_decode_payload_) {
            decode_pending_payload(now);
        }
        return;
    }

    if (!request_transmitted_ && !can_push_events()) {
        fail(
            ResourceTransitionStageErrorCode::event_backpressure,
            ResourceTransitionStageState::backpressure,
            "No bounded event slot remains for transition request transmission",
            now);
        return;
    }

    driver->update(now);
    observe_request_transmit(now);
    if (terminal_state(state_)) {
        return;
    }
    drain_driver_events(now, processed_events);
    if (terminal_state(state_)) {
        return;
    }
    if (pending_decode_payload_) {
        decode_pending_payload(now);
        if (terminal_state(state_)) {
            return;
        }
    }
    if (driver->terminal()) {
        fail_from_driver(now);
    }
}

void ResourceTransitionStage::cancel(const ResourceTransitionStageTimePoint now)
{
    if (trace_callback_active_ || state_ == ResourceTransitionStageState::idle ||
        terminal_state(state_)) {
        return;
    }
    if (state_ == ResourceTransitionStageState::waiting_for_user_info_state) {
        try {
            user_info_stage_.cancel(now);
        } catch (...) {
            fail(
                ResourceTransitionStageErrorCode::user_info_stage_failed,
                ResourceTransitionStageState::protocol_error,
                "Nested user-info stage threw during cancellation",
                now);
            return;
        }
    } else if (auto* const driver = user_info_stage_.retained_driver();
               driver != nullptr && !driver->terminal()) {
        driver->cancel(now);
    }
    state_ = ResourceTransitionStageState::cancelled;
    cleanup(now);
    emit_trace(ResourceTransitionTraceClassification::stage_cancelled);
}

std::optional<ResourceTransitionStageEvent> ResourceTransitionStage::poll_event()
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

ResourceTransitionStageState ResourceTransitionStage::state() const noexcept
{
    return state_;
}

bool ResourceTransitionStage::terminal() const noexcept
{
    return terminal_state(state_);
}

const std::optional<ResourceTransitionState>&
ResourceTransitionStage::result() const noexcept
{
    return result_;
}

const std::optional<ResourceTransitionStageError>&
ResourceTransitionStage::error() const noexcept
{
    return error_;
}

const network::NetworkAddress& ResourceTransitionStage::remote_endpoint() const noexcept
{
    return user_info_stage_.remote_endpoint();
}

const std::optional<network::NetworkAddress>&
ResourceTransitionStage::local_endpoint() const noexcept
{
    return user_info_stage_.local_endpoint();
}

std::size_t ResourceTransitionStage::pending_event_count() const noexcept
{
    return event_size_;
}

std::size_t ResourceTransitionStage::transmitted_packet_count() const noexcept
{
    return user_info_stage_.transmitted_packet_count();
}

std::size_t ResourceTransitionStage::cleanup_count() const noexcept
{
    return user_info_stage_.cleanup_count();
}

std::size_t ResourceTransitionStage::initial_request_queue_count() const noexcept
{
    return user_info_stage_.request_queue_count();
}

std::size_t ResourceTransitionStage::transition_request_queue_count() const noexcept
{
    return transition_request_queue_count_;
}

bool ResourceTransitionStage::transition_request_transmitted() const noexcept
{
    return request_transmitted_;
}

bool ResourceTransitionStage::transition_request_acknowledged() const noexcept
{
    return request_acknowledged_;
}

bool ResourceTransitionStage::can_push_events(const std::size_t count) const noexcept
{
    return count <= event_slots_.size() - event_size_;
}

void ResourceTransitionStage::push_event(ResourceTransitionStageEvent event) noexcept
{
    const auto index = (event_head_ + event_size_) % event_slots_.size();
    event_slots_[index].emplace(std::move(event));
    ++event_size_;
}

void ResourceTransitionStage::drain_user_info_events() noexcept
{
    while (user_info_stage_.poll_event()) {
    }
}

void ResourceTransitionStage::synchronize_from_user_info(
    const ResourceTransitionStageTimePoint now)
{
    if (user_info_stage_.state() == UserInfoSignonStageState::first_batch_complete) {
        emit_trace(ResourceTransitionTraceClassification::user_info_ready);
        queue_transition_request(now);
        return;
    }
    if (user_info_stage_.terminal() || user_info_stage_.error()) {
        fail_from_user_info();
    }
}

void ResourceTransitionStage::queue_transition_request(
    const ResourceTransitionStageTimePoint now)
{
    auto* const driver = user_info_stage_.retained_driver();
    if (!user_info_stage_.result() || driver == nullptr) {
        fail(
            ResourceTransitionStageErrorCode::retained_driver_missing,
            ResourceTransitionStageState::protocol_error,
            "User-info completion has no retained persistent driver",
            now);
        return;
    }
    if (transition_request_queue_count_ != 0U || request_) {
        fail(
            ResourceTransitionStageErrorCode::request_queue_failed,
            ResourceTransitionStageState::protocol_error,
            "Transition request semantic queue operation was duplicated",
            now);
        return;
    }
    if (!can_push_events()) {
        fail(
            ResourceTransitionStageErrorCode::event_backpressure,
            ResourceTransitionStageState::backpressure,
            "No bounded event slot remains for transition request queueing",
            now);
        return;
    }

    std::optional<ResourceTransitionRequestBuildResult> built;
    try {
        const ResourceTransitionRequestBuilder builder{config_.request};
        built.emplace(builder.build());
    } catch (...) {
        fail(
            ResourceTransitionStageErrorCode::request_build_failed,
            ResourceTransitionStageState::protocol_error,
            "Typed transition request builder threw",
            now);
        return;
    }
    if (!*built || !built->request) {
        fail(
            ResourceTransitionStageErrorCode::request_build_failed,
            ResourceTransitionStageState::protocol_error,
            built->error
                ? std::string_view{built->error->context}
                : std::string_view{"Typed transition request builder returned no request"},
            now,
            std::nullopt,
            built->error ? std::optional{built->error->code} : std::nullopt);
        return;
    }

    request_.emplace(std::move(*built->request));
    NetchanDriverOperationResult queued;
    try {
        queued = driver->queue_reliable(request_->bytes());
    } catch (...) {
        fail(
            ResourceTransitionStageErrorCode::request_queue_failed,
            ResourceTransitionStageState::protocol_error,
            "Persistent driver threw while queueing the typed transition request",
            now);
        return;
    }
    if (!queued) {
        fail(
            ResourceTransitionStageErrorCode::request_queue_failed,
            ResourceTransitionStageState::protocol_error,
            queued.error
                ? std::string_view{queued.error->context}
                : std::string_view{"Persistent driver rejected the transition request"},
            now,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            queued.error ? std::optional{queued.error->code} : std::nullopt);
        return;
    }

    ++transition_request_queue_count_;
    state_ = ResourceTransitionStageState::request_ready;
    push_event(ResourceTransitionStageEvent{
        ResourceTransitionStageEventType::transition_request_queued,
        0U,
        request_->message_bytes(),
        static_cast<std::uint8_t>(request_->opcode()),
        now,
    });
    emit_trace(
        ResourceTransitionTraceClassification::transition_request_queued,
        0U,
        request_->message_bytes(),
        static_cast<std::uint8_t>(request_->opcode()));
    state_ = ResourceTransitionStageState::waiting_for_request_transmit;
}

void ResourceTransitionStage::observe_request_transmit(
    const ResourceTransitionStageTimePoint now)
{
    if (request_transmitted_) {
        return;
    }
    auto* const driver = user_info_stage_.retained_driver();
    if (driver == nullptr || !request_) {
        return;
    }
    const auto& in_flight = driver->session().in_flight_reliable_payload();
    if (!in_flight) {
        return;
    }
    if (!std::ranges::equal(in_flight->bytes, request_->bytes())) {
        fail(
            ResourceTransitionStageErrorCode::request_queue_failed,
            ResourceTransitionStageState::protocol_error,
            "Unexpected reliable payload became in-flight during transition",
            now);
        return;
    }
    if (!can_push_events()) {
        fail(
            ResourceTransitionStageErrorCode::event_backpressure,
            ResourceTransitionStageState::backpressure,
            "No bounded event slot remains for transition request transmission",
            now);
        return;
    }
    request_transmitted_ = true;
    state_ = ResourceTransitionStageState::waiting_for_request_ack;
    push_event(ResourceTransitionStageEvent{
        ResourceTransitionStageEventType::transition_request_transmitted,
        0U,
        request_->message_bytes(),
        static_cast<std::uint8_t>(request_->opcode()),
        now,
    });
    emit_trace(
        ResourceTransitionTraceClassification::transition_request_transmitted,
        0U,
        request_->message_bytes(),
        static_cast<std::uint8_t>(request_->opcode()));
}

void ResourceTransitionStage::drain_driver_events(
    const ResourceTransitionStageTimePoint now,
    std::size_t& processed_events)
{
    auto* const driver = user_info_stage_.retained_driver();
    while (driver != nullptr &&
           processed_events < config_.maximum_driver_events_per_update &&
           !terminal_state(state_) && !pending_decode_payload_) {
        auto event = driver->poll_event();
        if (!event) {
            break;
        }
        ++processed_events;
        handle_driver_event(std::move(*event), now);
    }
}

void ResourceTransitionStage::handle_driver_event(
    NetchanDriverEvent event,
    const ResourceTransitionStageTimePoint now)
{
    switch (event.type) {
    case NetchanDriverEventType::payload_ready:
        if (!event.payload) {
            fail(
                ResourceTransitionStageErrorCode::driver_failed,
                ResourceTransitionStageState::protocol_error,
                "Driver payload event has no owning payload",
                now);
            return;
        }
        handle_payload(std::move(*event.payload), now);
        return;
    case NetchanDriverEventType::reliable_payload_acknowledged:
        handle_request_acknowledgement(now);
        return;
    case NetchanDriverEventType::normal_transfer_started:
    case NetchanDriverEventType::normal_transfer_completed:
        return;
    case NetchanDriverEventType::normal_transfer_timed_out:
    case NetchanDriverEventType::channel_timed_out:
        fail(
            ResourceTransitionStageErrorCode::driver_failed,
            ResourceTransitionStageState::timed_out,
            "Resource-transition transfer or channel timed out",
            now,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            event.type == NetchanDriverEventType::normal_transfer_timed_out
                ? std::optional{NetchanDriverErrorCode::fragment_transfer_timed_out}
                : std::optional{NetchanDriverErrorCode::channel_inactivity_timed_out});
        return;
    case NetchanDriverEventType::secondary_stream_pending_m3:
        fail(
            ResourceTransitionStageErrorCode::driver_failed,
            ResourceTransitionStageState::secondary_stream_pending,
            "Secondary netchan fragment stream remains outside this milestone",
            now,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            NetchanDriverErrorCode::secondary_stream_pending_m3);
        return;
    case NetchanDriverEventType::cancelled:
        state_ = ResourceTransitionStageState::cancelled;
        cleanup(now);
        emit_trace(ResourceTransitionTraceClassification::stage_cancelled);
        return;
    case NetchanDriverEventType::network_error:
    case NetchanDriverEventType::protocol_error:
        fail_from_driver(now);
        return;
    }
}

void ResourceTransitionStage::handle_request_acknowledgement(
    const ResourceTransitionStageTimePoint now)
{
    if (!request_transmitted_ || request_acknowledged_) {
        fail(
            ResourceTransitionStageErrorCode::driver_failed,
            ResourceTransitionStageState::protocol_error,
            "Unexpected reliable acknowledgement in transition stage",
            now);
        return;
    }
    if (!can_push_events()) {
        fail(
            ResourceTransitionStageErrorCode::event_backpressure,
            ResourceTransitionStageState::backpressure,
            "No bounded event slot remains for transition acknowledgement",
            now);
        return;
    }
    request_acknowledged_ = true;
    state_ = ResourceTransitionStageState::waiting_for_server_transfer;
    push_event(ResourceTransitionStageEvent{
        ResourceTransitionStageEventType::transition_request_acknowledged,
        0U,
        request_ ? request_->message_bytes() : 0U,
        request_ ? std::optional{static_cast<std::uint8_t>(request_->opcode())}
                 : std::nullopt,
        now,
    });
    emit_trace(
        ResourceTransitionTraceClassification::transition_request_acknowledged,
        0U,
        request_ ? request_->message_bytes() : 0U,
        request_ ? std::optional{static_cast<std::uint8_t>(request_->opcode())}
                 : std::nullopt);
    if (pre_ack_payload_) {
        pending_decode_payload_.emplace(std::move(*pre_ack_payload_));
        pre_ack_payload_.reset();
        state_ = ResourceTransitionStageState::decoding_transition_control;
    }
}

void ResourceTransitionStage::handle_payload(
    OwnedNetchanPayload payload,
    const ResourceTransitionStageTimePoint now)
{
    // A sequenced header-only server acknowledgement is surfaced by the
    // persistent driver as an owning event with zero payload bytes. It advances
    // only transport state and is not a second service transfer.
    if (payload.bytes.empty()) {
        return;
    }
    if (!request_acknowledged_) {
        if (pre_ack_payload_) {
            fail(
                ResourceTransitionStageErrorCode::service_payload_before_ack_overflow,
                ResourceTransitionStageState::protocol_error,
                "More than one second service payload arrived before request ACK",
                now);
            return;
        }
        pre_ack_payload_.emplace(std::move(payload));
        return;
    }
    if (pending_decode_payload_) {
        fail(
            ResourceTransitionStageErrorCode::event_backpressure,
            ResourceTransitionStageState::backpressure,
            "A second payload arrived before bounded transition decode commit",
            now);
        return;
    }
    pending_decode_payload_.emplace(std::move(payload));
    state_ = ResourceTransitionStageState::decoding_transition_control;
}

void ResourceTransitionStage::decode_pending_payload(
    const ResourceTransitionStageTimePoint now)
{
    if (!pending_decode_payload_ || terminal_state(state_)) {
        return;
    }
    std::optional<ServicePayloadEnvelopeDecodeResult> decoded;
    try {
        const ServicePayloadEnvelopeDecoder decoder{
            ServicePayloadEnvelopeLimits{config_.maximum_second_service_payload_size}};
        decoded.emplace(decoder.decode(std::move(*pending_decode_payload_)));
    } catch (...) {
        pending_decode_payload_.reset();
        fail(
            ResourceTransitionStageErrorCode::second_payload_envelope_decode_failed,
            ResourceTransitionStageState::protocol_error,
            "Bounded second service envelope decoder threw",
            now);
        return;
    }
    pending_decode_payload_.reset();
    if (!*decoded || !decoded->envelope) {
        fail(
            ResourceTransitionStageErrorCode::second_payload_envelope_decode_failed,
            ResourceTransitionStageState::protocol_error,
            decoded->error
                ? std::string_view{decoded->error->context}
                : std::string_view{"Second service envelope returned no payload"},
            now,
            std::nullopt,
            std::nullopt,
            decoded->error ? std::optional{decoded->error->code} : std::nullopt);
        return;
    }

    auto& envelope = *decoded->envelope;
    if (envelope.decompressed_byte_count >
            config_.maximum_second_service_payload_size ||
        envelope.payload.bytes.size() != envelope.decompressed_byte_count) {
        fail(
            ResourceTransitionStageErrorCode::second_payload_envelope_decode_failed,
            ResourceTransitionStageState::protocol_error,
            "Second service payload exceeds or contradicts its bounded metadata",
            now);
        return;
    }

    std::optional<ResourceTransitionControlParseResult> parsed;
    try {
        const ResourceTransitionControlParser parser{config_.control};
        parsed.emplace(parser.parse(envelope.payload.bytes));
    } catch (...) {
        fail(
            ResourceTransitionStageErrorCode::transition_control_decode_failed,
            ResourceTransitionStageState::protocol_error,
            "Strict transition-control parser threw",
            now);
        return;
    }
    if (!*parsed || !parsed->state || !parsed->boundary) {
        fail(
            ResourceTransitionStageErrorCode::transition_control_decode_failed,
            parsed->error &&
                    parsed->error->code ==
                        ResourceTransitionControlErrorCode::wrong_opcode
                ? ResourceTransitionStageState::unsupported_message
                : ResourceTransitionStageState::protocol_error,
            parsed->error
                ? std::string_view{parsed->error->context}
                : std::string_view{"Transition-control parser returned no state"},
            now,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            parsed->error ? std::optional{parsed->error->code} : std::nullopt);
        return;
    }
    if (!user_info_stage_.result() || !request_) {
        fail(
            ResourceTransitionStageErrorCode::transition_control_decode_failed,
            ResourceTransitionStageState::protocol_error,
            "Transition decode lost prerequisite owning state",
            now);
        return;
    }
    if (!can_push_events(3U)) {
        fail(
            ResourceTransitionStageErrorCode::event_backpressure,
            ResourceTransitionStageState::backpressure,
            "Transition result exceeds bounded event capacity",
            now);
        return;
    }

    const auto source_metadata = ResourceTransitionSourcePayloadMetadata{
        envelope.compressed_byte_count,
        envelope.decompressed_byte_count,
        envelope.payload.source_sequence,
        envelope.payload.source_acknowledgement,
        envelope.payload.source_reliable,
        envelope.payload.reassembled,
        envelope.payload.decompressed,
        envelope.payload.acknowledgement_reliable,
        envelope.payload.direction,
        envelope.payload.received_at};

    std::optional<ResourceTransitionState> candidate_result;
    try {
        auto built_result = ResourceTransitionState{
            *user_info_stage_.result(),
            *request_,
            std::move(*parsed->state),
            std::move(*parsed->boundary),
            source_metadata};
        candidate_result.emplace(std::move(built_result));
    } catch (...) {
        fail(
            ResourceTransitionStageErrorCode::transition_control_decode_failed,
            ResourceTransitionStageState::protocol_error,
            "Unable to allocate bounded owning transition result",
            now);
        return;
    }

    result_.emplace(std::move(*candidate_result));
    push_event(ResourceTransitionStageEvent{
        ResourceTransitionStageEventType::second_service_transfer_received,
        0U,
        result_->source_payload().decompressed_byte_count(),
        std::nullopt,
        now,
    });
    push_event(ResourceTransitionStageEvent{
        ResourceTransitionStageEventType::transition_control_decoded,
        result_->control().source_message_offset(),
        result_->control().message_bytes(),
        result_->control().opcode(),
        now,
    });
    push_event(ResourceTransitionStageEvent{
        ResourceTransitionStageEventType::neutral_opcode43_boundary,
        result_->boundary().byte_offset(),
        result_->boundary().remaining_byte_count(),
        result_->boundary().opcode(),
        now,
    });
    emit_trace(
        ResourceTransitionTraceClassification::second_service_transfer_received,
        0U,
        result_->source_payload().decompressed_byte_count());
    emit_trace(
        ResourceTransitionTraceClassification::transition_control_decoded,
        result_->control().source_message_offset(),
        result_->control().message_bytes(),
        result_->control().opcode());
    state_ = ResourceTransitionStageState::neutral_opcode43_boundary_reached;
    cleanup(now);
    emit_trace(
        ResourceTransitionTraceClassification::neutral_opcode43_boundary_reached,
        result_->boundary().byte_offset(),
        result_->boundary().remaining_byte_count(),
        result_->boundary().opcode());
}

void ResourceTransitionStage::fail_from_user_info() noexcept
{
    const auto& nested_error = user_info_stage_.error();
    const auto driver_code = nested_error ? nested_error->driver_code : std::nullopt;
    switch (user_info_stage_.state()) {
    case UserInfoSignonStageState::timed_out:
        state_ = ResourceTransitionStageState::timed_out;
        break;
    case UserInfoSignonStageState::cancelled:
        state_ = ResourceTransitionStageState::cancelled;
        break;
    case UserInfoSignonStageState::unsupported_message:
        state_ = ResourceTransitionStageState::unsupported_message;
        break;
    case UserInfoSignonStageState::backpressure:
        state_ = ResourceTransitionStageState::backpressure;
        break;
    case UserInfoSignonStageState::secondary_stream_pending:
        state_ = ResourceTransitionStageState::secondary_stream_pending;
        break;
    case UserInfoSignonStageState::network_error:
        state_ = ResourceTransitionStageState::network_error;
        break;
    case UserInfoSignonStageState::idle:
    case UserInfoSignonStageState::waiting_for_movevars_state:
    case UserInfoSignonStageState::decoding_user_info:
    case UserInfoSignonStageState::user_info_ready:
    case UserInfoSignonStageState::first_batch_complete:
    case UserInfoSignonStageState::protocol_error:
        state_ = ResourceTransitionStageState::protocol_error;
        break;
    }
    error_.reset();
    try {
        error_.emplace();
        error_->code = ResourceTransitionStageErrorCode::user_info_stage_failed;
        error_->user_info_code = nested_error
            ? std::optional{nested_error->code}
            : std::nullopt;
        error_->driver_code = driver_code;
        if (nested_error) {
            const auto bounded = std::string_view{nested_error->context}.substr(
                0U,
                kResourceTransitionStageDiagnosticTextLimit);
            error_->context.assign(bounded.data(), bounded.size());
        }
    } catch (...) {
    }
    emit_trace(terminal_trace(state_));
}

void ResourceTransitionStage::fail_from_driver(
    const ResourceTransitionStageTimePoint now) noexcept
{
    auto* const driver = user_info_stage_.retained_driver();
    const std::optional<NetchanDriverError>* nested_error =
        driver != nullptr ? &driver->last_error() : nullptr;
    const auto code = nested_error
        && *nested_error
        ? (*nested_error)->code
        : NetchanDriverErrorCode::not_active;
    auto state = ResourceTransitionStageState::protocol_error;
    if (driver_network_error(code)) {
        state = ResourceTransitionStageState::network_error;
    } else if (code == NetchanDriverErrorCode::channel_inactivity_timed_out ||
               code == NetchanDriverErrorCode::fragment_transfer_timed_out) {
        state = ResourceTransitionStageState::timed_out;
    } else if (code == NetchanDriverErrorCode::event_backpressure) {
        state = ResourceTransitionStageState::backpressure;
    } else if (code == NetchanDriverErrorCode::secondary_stream_pending_m3) {
        state = ResourceTransitionStageState::secondary_stream_pending;
    }
    fail(
        ResourceTransitionStageErrorCode::driver_failed,
        state,
        nested_error && *nested_error
            ? std::string_view{(*nested_error)->context}
            : std::string_view{"Persistent driver terminated unexpectedly"},
        now,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        code);
}

void ResourceTransitionStage::fail(
    const ResourceTransitionStageErrorCode code,
    const ResourceTransitionStageState state,
    const std::string_view context,
    const ResourceTransitionStageTimePoint now,
    const std::optional<UserInfoSignonStageErrorCode> user_info_code,
    const std::optional<ResourceTransitionRequestErrorCode> request_code,
    const std::optional<ServicePayloadEnvelopeErrorCode> envelope_code,
    const std::optional<ResourceTransitionControlErrorCode> control_code,
    const std::optional<NetchanDriverErrorCode> driver_code) noexcept
{
    if (terminal_state(state_)) {
        return;
    }
    state_ = state;
    result_.reset();
    error_.reset();
    try {
        error_.emplace();
        error_->code = code;
        error_->user_info_code = user_info_code;
        error_->request_code = request_code;
        error_->envelope_code = envelope_code;
        error_->control_code = control_code;
        error_->driver_code = driver_code;
        const auto bounded = context.substr(
            0U,
            (std::min)(context.size(), kResourceTransitionStageDiagnosticTextLimit));
        error_->context.assign(bounded.data(), bounded.size());
    } catch (...) {
    }
    cleanup(now);
    emit_trace(terminal_trace(state_));
}

void ResourceTransitionStage::cleanup(
    const ResourceTransitionStageTimePoint now) noexcept
{
    if (cleanup_done_) {
        return;
    }
    cleanup_done_ = true;
    user_info_stage_.finalize_retained_boundary(now);
    pre_ack_payload_.reset();
    pending_decode_payload_.reset();
}

void ResourceTransitionStage::emit_trace(
    const ResourceTransitionTraceClassification classification,
    const std::size_t byte_offset,
    const std::size_t byte_count,
    const std::optional<std::uint8_t> opcode) noexcept
{
    if (!trace_callback_ || trace_callback_active_) {
        return;
    }
    ResourceTransitionTraceEvent event;
    event.classification = classification;
    event.state = state_;
    event.endpoint = remote_endpoint();
    event.request_size = request_ ? request_->message_bytes() : 0U;
    event.byte_offset = byte_offset;
    event.byte_count = byte_count;
    event.opcode = opcode;
    event.transmitted_packet_count = transmitted_packet_count();
    trace_callback_active_ = true;
    try {
        trace_callback_(event);
    } catch (...) {
    }
    trace_callback_active_ = false;
}

static_assert(std::is_nothrow_move_constructible_v<ResourceTransitionStageEvent>);
static_assert(std::is_nothrow_move_constructible_v<ResourceTransitionState>);

} // namespace hlclient::goldsrc
