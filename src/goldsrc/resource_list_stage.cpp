#include <hlclient/goldsrc/resource_list_stage.hpp>

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool terminal_state(const ResourceListStageState state) noexcept
{
    switch (state) {
    case ResourceListStageState::client_response_required:
    case ResourceListStageState::unsupported_resource_profile:
    case ResourceListStageState::timed_out:
    case ResourceListStageState::cancelled:
    case ResourceListStageState::backpressure:
    case ResourceListStageState::secondary_stream_pending:
    case ResourceListStageState::network_error:
    case ResourceListStageState::protocol_error:
        return true;
    case ResourceListStageState::idle:
    case ResourceListStageState::waiting_for_transition_state:
    case ResourceListStageState::decoding_resource_list:
    case ResourceListStageState::resource_list_ready:
    case ResourceListStageState::decoding_post_list_messages:
    case ResourceListStageState::post_list_boundary_reached:
        return false;
    }
    return true;
}

[[nodiscard]] ResourceListTraceClassification terminal_trace(
    const ResourceListStageState state) noexcept
{
    switch (state) {
    case ResourceListStageState::client_response_required:
        return ResourceListTraceClassification::client_response_required;
    case ResourceListStageState::unsupported_resource_profile:
        return ResourceListTraceClassification::unsupported_resource_profile;
    case ResourceListStageState::timed_out:
        return ResourceListTraceClassification::stage_timed_out;
    case ResourceListStageState::cancelled:
        return ResourceListTraceClassification::stage_cancelled;
    case ResourceListStageState::backpressure:
        return ResourceListTraceClassification::backpressure;
    case ResourceListStageState::secondary_stream_pending:
        return ResourceListTraceClassification::secondary_stream_pending;
    case ResourceListStageState::network_error:
        return ResourceListTraceClassification::network_failure;
    case ResourceListStageState::idle:
    case ResourceListStageState::waiting_for_transition_state:
    case ResourceListStageState::decoding_resource_list:
    case ResourceListStageState::resource_list_ready:
    case ResourceListStageState::decoding_post_list_messages:
    case ResourceListStageState::post_list_boundary_reached:
    case ResourceListStageState::protocol_error:
        return ResourceListTraceClassification::protocol_failure;
    }
    return ResourceListTraceClassification::protocol_failure;
}

[[nodiscard]] ResourceListStageEventType terminal_event(
    const ResourceListStageState state) noexcept
{
    switch (state) {
    case ResourceListStageState::unsupported_resource_profile:
        return ResourceListStageEventType::unsupported_resource_profile;
    case ResourceListStageState::timed_out:
        return ResourceListStageEventType::timeout;
    case ResourceListStageState::cancelled:
        return ResourceListStageEventType::cancelled;
    case ResourceListStageState::backpressure:
        return ResourceListStageEventType::backpressure;
    case ResourceListStageState::secondary_stream_pending:
        return ResourceListStageEventType::secondary_stream_pending;
    case ResourceListStageState::network_error:
        return ResourceListStageEventType::network_error;
    case ResourceListStageState::idle:
    case ResourceListStageState::waiting_for_transition_state:
    case ResourceListStageState::decoding_resource_list:
    case ResourceListStageState::resource_list_ready:
    case ResourceListStageState::decoding_post_list_messages:
    case ResourceListStageState::post_list_boundary_reached:
    case ResourceListStageState::client_response_required:
    case ResourceListStageState::protocol_error:
        return ResourceListStageEventType::protocol_error;
    }
    return ResourceListStageEventType::protocol_error;
}

} // namespace

bool valid_resource_list_stage_configuration(
    const ResourceListStageConfig& config) noexcept
{
    return valid_resource_transition_stage_configuration(config.transition) &&
           valid_resource_list_limits(config.resource_list) &&
           config.maximum_stage_events > 0U &&
           config.maximum_stage_events <= kMaximumResourceListEvents;
}

ResourceListSignonState::ResourceListSignonState(
    ResourceTransitionState transition,
    ResourceListState resource_list,
    PostResourceListStreamState post_list) noexcept
    : transition_{std::move(transition)},
      resource_list_{std::move(resource_list)},
      post_list_{std::move(post_list)}
{
}

const ResourceTransitionState&
ResourceListSignonState::transition() const noexcept
{
    return transition_;
}

const ResourceListState&
ResourceListSignonState::resource_list() const noexcept
{
    return resource_list_;
}

const PostResourceListStreamState&
ResourceListSignonState::post_list() const noexcept
{
    return post_list_;
}

const PostResourceListBoundary&
ResourceListSignonState::boundary() const noexcept
{
    return post_list_.boundary();
}

const ResourceClientResponseBoundary&
ResourceListSignonState::client_response() const noexcept
{
    return post_list_.client_response();
}

ResourceListStage::ResourceListStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    ResourceListStageConfig config,
    InitialSignonTraceCallback initial_trace_callback,
    PreResourceSignonTraceCallback pre_resource_trace_callback,
    DeltaDescriptionTraceCallback delta_trace_callback,
    MovementEnvironmentTraceCallback movement_trace_callback,
    UserInfoSignonTraceCallback user_info_trace_callback,
    ResourceTransitionTraceCallback transition_trace_callback,
    ResourceListTraceCallback trace_callback)
    : config_{std::move(config)},
      trace_callback_{std::move(trace_callback)},
      configuration_valid_{valid_resource_list_stage_configuration(config_)},
      transition_stage_{
          transport,
          remote_endpoint,
          config_.transition,
          [this, callback = std::move(initial_trace_callback)](
              const InitialSignonTraceEvent& event) {
              if (!callback || trace_callback_active_) {
                  return;
              }
              trace_callback_active_ = true;
              try { callback(event); } catch (...) {}
              trace_callback_active_ = false;
          },
          [this, callback = std::move(pre_resource_trace_callback)](
              const PreResourceSignonTraceEvent& event) {
              if (!callback || trace_callback_active_) {
                  return;
              }
              trace_callback_active_ = true;
              try { callback(event); } catch (...) {}
              trace_callback_active_ = false;
          },
          [this, callback = std::move(delta_trace_callback)](
              const DeltaDescriptionTraceEvent& event) {
              if (!callback || trace_callback_active_) {
                  return;
              }
              trace_callback_active_ = true;
              try { callback(event); } catch (...) {}
              trace_callback_active_ = false;
          },
          [this, callback = std::move(movement_trace_callback)](
              const MovementEnvironmentTraceEvent& event) {
              if (!callback || trace_callback_active_) {
                  return;
              }
              trace_callback_active_ = true;
              try { callback(event); } catch (...) {}
              trace_callback_active_ = false;
          },
          [this, callback = std::move(user_info_trace_callback)](
              const UserInfoSignonTraceEvent& event) {
              if (!callback || trace_callback_active_) {
                  return;
              }
              trace_callback_active_ = true;
              try { callback(event); } catch (...) {}
              trace_callback_active_ = false;
          },
          [this, callback = std::move(transition_trace_callback)](
              const ResourceTransitionTraceEvent& event) {
              if (!callback || trace_callback_active_) {
                  return;
              }
              trace_callback_active_ = true;
              try { callback(event); } catch (...) {}
              trace_callback_active_ = false;
          },
          ResourceTransitionStage::RetainConnectionAtBoundary{}},
      event_slots_(configuration_valid_ ? config_.maximum_stage_events : 0U)
{
}

ResourceListStage::~ResourceListStage() = default;

bool ResourceListStage::start(
    const ResourceListStageTimePoint now,
    const network::NetworkAddress& expected_local_endpoint,
    std::unique_ptr<INetchanDriverLifetime> connection_lifetime)
{
    if (trace_callback_active_ || state_ != ResourceListStageState::idle) {
        return false;
    }
    error_.reset();
    if (!configuration_valid_) {
        set_error(
            ResourceListStageErrorCode::invalid_configuration,
            ResourceListStageState::protocol_error,
            "Resource-list stage configuration is outside project bounds");
        emit_trace(ResourceListTraceClassification::protocol_failure);
        return false;
    }

    bool started = false;
    try {
        started = transition_stage_.start(
            now,
            expected_local_endpoint,
            std::move(connection_lifetime));
    } catch (...) {
        try { transition_stage_.cancel(now); } catch (...) {}
        set_error(
            ResourceListStageErrorCode::transition_stage_start_failed,
            ResourceListStageState::protocol_error,
            "Nested resource-transition stage threw during start");
        if (can_push_events()) {
            push_event(ResourceListStageEvent{
                .type = ResourceListStageEventType::protocol_error,
                .occurred_at = now,
            });
        }
        transition_stage_.finalize_retained_boundary(now);
        emit_trace(ResourceListTraceClassification::protocol_failure);
        return false;
    }
    drain_transition_events();
    if (!started) {
        fail_from_transition(now);
        if (error_) {
            error_->code =
                ResourceListStageErrorCode::transition_stage_start_failed;
        }
        return false;
    }

    last_update_ = now;
    state_ = ResourceListStageState::waiting_for_transition_state;
    emit_trace(ResourceListTraceClassification::stage_started);
    return true;
}

void ResourceListStage::update(const ResourceListStageTimePoint now)
{
    if (trace_callback_active_ || state_ == ResourceListStageState::idle ||
        terminal_state(state_)) {
        return;
    }
    if (!last_update_ || now < *last_update_) {
        fail_after_transition(
            ResourceListStageErrorCode::time_moved_backwards,
            ResourceListStageState::protocol_error,
            "Resource-list stage update time moved backwards",
            now);
        return;
    }
    last_update_ = now;

    if (state_ == ResourceListStageState::waiting_for_transition_state) {
        drain_transition_events();
        try {
            transition_stage_.update(now);
        } catch (...) {
            try { transition_stage_.cancel(now); } catch (...) {}
            fail_after_transition(
                ResourceListStageErrorCode::transition_stage_failed,
                ResourceListStageState::protocol_error,
                "Nested resource-transition stage threw during update",
                now);
            return;
        }
        drain_transition_events();
        synchronize_from_transition(now);
    }
}

void ResourceListStage::cancel(const ResourceListStageTimePoint now)
{
    if (trace_callback_active_ || state_ == ResourceListStageState::idle ||
        terminal_state(state_)) {
        return;
    }
    try {
        transition_stage_.cancel(now);
    } catch (...) {
        fail_after_transition(
            ResourceListStageErrorCode::transition_stage_failed,
            ResourceListStageState::protocol_error,
            "Nested resource-transition stage threw during cancellation",
            now);
        return;
    }
    result_.reset();
    error_.reset();
    state_ = ResourceListStageState::cancelled;
    if (can_push_events()) {
        push_event(ResourceListStageEvent{
            .type = ResourceListStageEventType::cancelled,
            .occurred_at = now,
        });
    }
    transition_stage_.finalize_retained_boundary(now);
    emit_trace(ResourceListTraceClassification::stage_cancelled);
}

std::optional<ResourceListStageEvent> ResourceListStage::poll_event()
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

ResourceListStageState ResourceListStage::state() const noexcept
{
    return state_;
}

bool ResourceListStage::terminal() const noexcept
{
    return terminal_state(state_);
}

const std::optional<ResourceListSignonState>&
ResourceListStage::result() const noexcept
{
    return result_;
}

const std::optional<ResourceListStageError>&
ResourceListStage::error() const noexcept
{
    return error_;
}

const network::NetworkAddress& ResourceListStage::remote_endpoint() const noexcept
{
    return transition_stage_.remote_endpoint();
}

const std::optional<network::NetworkAddress>&
ResourceListStage::local_endpoint() const noexcept
{
    return transition_stage_.local_endpoint();
}

std::size_t ResourceListStage::pending_event_count() const noexcept
{
    return event_size_;
}

std::size_t ResourceListStage::transmitted_packet_count() const noexcept
{
    return transition_stage_.transmitted_packet_count();
}

std::size_t ResourceListStage::cleanup_count() const noexcept
{
    return transition_stage_.cleanup_count();
}

std::size_t ResourceListStage::initial_request_queue_count() const noexcept
{
    return transition_stage_.initial_request_queue_count();
}

std::size_t ResourceListStage::transition_request_queue_count() const noexcept
{
    return transition_stage_.transition_request_queue_count();
}

bool ResourceListStage::can_push_events(const std::size_t count) const noexcept
{
    return count <= event_slots_.size() - event_size_;
}

void ResourceListStage::push_event(ResourceListStageEvent event) noexcept
{
    const auto index = (event_head_ + event_size_) % event_slots_.size();
    event_slots_[index].emplace(std::move(event));
    ++event_size_;
}

void ResourceListStage::drain_transition_events() noexcept
{
    while (transition_stage_.poll_event()) {
    }
}

void ResourceListStage::synchronize_from_transition(
    const ResourceListStageTimePoint now)
{
    if (transition_stage_.state() ==
        ResourceTransitionStageState::neutral_opcode43_boundary_reached) {
        emit_trace(ResourceListTraceClassification::transition_boundary_reached);
        decode_retained_resource_list(now);
        return;
    }
    if (transition_stage_.terminal() || transition_stage_.error()) {
        fail_from_transition(now);
    }
}

void ResourceListStage::decode_retained_resource_list(
    const ResourceListStageTimePoint now)
{
    const auto* const payload = transition_stage_.retained_source_payload();
    auto* const driver = transition_stage_.retained_driver();
    if (!transition_stage_.result() || payload == nullptr) {
        fail_after_transition(
            ResourceListStageErrorCode::retained_payload_missing,
            ResourceListStageState::protocol_error,
            "Resource-list continuation lost its owning decompressed payload",
            now);
        return;
    }
    if (driver == nullptr) {
        fail_after_transition(
            ResourceListStageErrorCode::retained_driver_missing,
            ResourceListStageState::protocol_error,
            "Resource-list continuation lost its retained persistent driver",
            now);
        return;
    }

    const auto& transition = *transition_stage_.result();
    const auto& boundary = transition.boundary();
    if (boundary.opcode() != kResourceListOpcode ||
        boundary.source_payload_size() != payload->bytes.size() ||
        boundary.byte_offset() >= payload->bytes.size() ||
        boundary.remaining_byte_count() !=
            payload->bytes.size() - boundary.byte_offset() - 1U ||
        std::to_integer<std::uint8_t>(
            payload->bytes[boundary.byte_offset()]) != kResourceListOpcode) {
        fail_after_transition(
            ResourceListStageErrorCode::transition_boundary_mismatch,
            ResourceListStageState::protocol_error,
            "Retained payload contradicts the exact opcode-43 transition boundary",
            now);
        return;
    }
    if (payload->bytes.size() >
        (std::numeric_limits<std::size_t>::max)() / 8U) {
        fail_after_transition(
            ResourceListStageErrorCode::transition_boundary_mismatch,
            ResourceListStageState::protocol_error,
            "Retained resource-list payload bit geometry overflowed",
            now);
        return;
    }
    const auto payload_bit_length = payload->bytes.size() * 8U;

    state_ = ResourceListStageState::decoding_resource_list;
    std::optional<ResourceListParseResult> parsed;
    try {
        const ResourceListParser parser{config_.resource_list};
        parsed.emplace(parser.parse(
            payload->bytes,
            boundary.byte_offset(),
            payload_bit_length));
    } catch (...) {
        fail_after_transition(
            ResourceListStageErrorCode::resource_list_decode_failed,
            ResourceListStageState::protocol_error,
            "Strict resource-list parser threw",
            now);
        return;
    }
    if (!*parsed || !parsed->state) {
        const auto unsupported = parsed->error &&
            parsed->error->code ==
                ResourceListErrorCode::unsupported_resource_profile;
        fail_after_transition(
            ResourceListStageErrorCode::resource_list_decode_failed,
            unsupported
                ? ResourceListStageState::unsupported_resource_profile
                : ResourceListStageState::protocol_error,
            parsed->error
                ? std::string_view{parsed->error->context}
                : std::string_view{"Resource-list parser returned no state"},
            now,
            parsed->error
                ? std::optional{parsed->error->code}
                : std::nullopt);
        return;
    }

    state_ = ResourceListStageState::decoding_post_list_messages;
    std::optional<PostResourceListStreamDecodeResult> post_decoded;
    try {
        const PostResourceListStreamDecoder decoder;
        post_decoded.emplace(decoder.decode(
            payload->bytes,
            *parsed->state,
            payload_bit_length));
    } catch (...) {
        fail_after_transition(
            ResourceListStageErrorCode::post_resource_stream_decode_failed,
            ResourceListStageState::protocol_error,
            "Strict post-resource-list decoder threw",
            now);
        return;
    }
    if (!*post_decoded || !post_decoded->state) {
        fail_after_transition(
            ResourceListStageErrorCode::post_resource_stream_decode_failed,
            ResourceListStageState::protocol_error,
            post_decoded->error
                ? std::string_view{post_decoded->error->context}
                : std::string_view{"Post-resource-list decoder returned no state"},
            now,
            std::nullopt,
            post_decoded->error
                ? std::optional{post_decoded->error->code}
                : std::nullopt);
        return;
    }

    const auto resource_count = parsed->state->resource_count();
    if (post_decoded->required_event_count >
            (std::numeric_limits<std::size_t>::max)() - 1U ||
        resource_count >
            (std::numeric_limits<std::size_t>::max)() - 1U -
                post_decoded->required_event_count) {
        fail_after_transition(
            ResourceListStageErrorCode::event_backpressure,
            ResourceListStageState::backpressure,
            "Resource-list event-count preflight overflowed",
            now);
        return;
    }
    const auto required_event_count =
        1U + resource_count + post_decoded->required_event_count;
    if (!can_push_events(required_event_count)) {
        fail_after_transition(
            ResourceListStageErrorCode::event_backpressure,
            ResourceListStageState::backpressure,
            "Resource-list publication exceeds bounded event capacity",
            now);
        return;
    }

    std::vector<ResourceListStageEvent> candidate_events;
    std::optional<ResourceListSignonState> candidate_result;
    try {
        candidate_events.reserve(required_event_count);
        candidate_events.push_back(ResourceListStageEvent{
            .type = ResourceListStageEventType::resource_list_ready,
            .resource_count = resource_count,
            .byte_offset = parsed->state->source_opcode_byte_offset(),
            .byte_count = parsed->state->bytes_consumed(),
            .bit_count = parsed->state->bits_consumed(),
            .opcode = kResourceListOpcode,
            .occurred_at = now,
        });
        for (const auto& entry : parsed->state->entries()) {
            const auto entry_bits =
                entry.source_end_bit_offset() - entry.source_start_bit_offset();
            candidate_events.push_back(ResourceListStageEvent{
                .type = ResourceListStageEventType::resource_entry_metadata,
                .resource_count = resource_count,
                .entry_ordinal = entry.wire_ordinal(),
                .resource_type = entry.type(),
                .resource_index = entry.index().value(),
                .resource_size_code = entry.declared_size().raw_code(),
                .resource_flags = entry.flags().wire_value(),
                .resource_name_byte_count = entry.name().byte_length(),
                .byte_offset = entry.source_start_bit_offset() / 8U,
                .bit_offset = entry.source_start_bit_offset() & 7U,
                .byte_count = entry_bits / 8U +
                    ((entry_bits & 7U) != 0U ? 1U : 0U),
                .bit_count = entry_bits,
                .occurred_at = now,
            });
        }
        const auto& post = *post_decoded->state;
        candidate_events.push_back(ResourceListStageEvent{
            .type = ResourceListStageEventType::post_resource_boundary,
            .resource_count = resource_count,
            .byte_offset = post.boundary().byte_offset(),
            .bit_offset = post.boundary().bit_offset(),
            .occurred_at = now,
        });
        candidate_events.push_back(ResourceListStageEvent{
            .type = ResourceListStageEventType::client_response_required,
            .resource_count = resource_count,
            .byte_offset = post.client_response().trigger_byte_offset(),
            .bit_offset = post.client_response().trigger_bit_offset(),
            .occurred_at = now,
        });
        if (candidate_events.size() != required_event_count) {
            fail_after_transition(
                ResourceListStageErrorCode::post_resource_stream_decode_failed,
                ResourceListStageState::protocol_error,
                "Post-resource-list event contract is inconsistent",
                now);
            return;
        }
        auto built_result = ResourceListSignonState{
            transition,
            std::move(*parsed->state),
            std::move(*post_decoded->state)};
        candidate_result.emplace(std::move(built_result));
    } catch (...) {
        fail_after_transition(
            ResourceListStageErrorCode::resource_list_decode_failed,
            ResourceListStageState::protocol_error,
            "Unable to allocate bounded owning resource-list publication",
            now);
        return;
    }

    result_.emplace(std::move(*candidate_result));
    for (auto& event : candidate_events) {
        push_event(std::move(event));
    }

    state_ = ResourceListStageState::resource_list_ready;
    emit_trace(
        ResourceListTraceClassification::resource_list_decoded,
        result_->resource_list().resource_count(),
        0U,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        0U,
        result_->resource_list().source_opcode_byte_offset(),
        0U,
        kResourceListOpcode);
    for (const auto& entry : result_->resource_list().entries()) {
        emit_trace(
            ResourceListTraceClassification::resource_entry_metadata,
            result_->resource_list().resource_count(),
            entry.wire_ordinal(),
            entry.type(),
            entry.index().value(),
            entry.declared_size().raw_code(),
            entry.flags().wire_value(),
            entry.name().byte_length(),
            entry.source_start_bit_offset() / 8U,
            entry.source_start_bit_offset() & 7U);
    }
    state_ = ResourceListStageState::post_list_boundary_reached;
    emit_trace(
        ResourceListTraceClassification::post_resource_boundary_reached,
        result_->resource_list().resource_count(),
        0U,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        0U,
        result_->boundary().byte_offset(),
        result_->boundary().bit_offset());
    state_ = ResourceListStageState::client_response_required;
    transition_stage_.finalize_retained_boundary(now);
    emit_trace(
        ResourceListTraceClassification::client_response_required,
        result_->resource_list().resource_count(),
        0U,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        0U,
        result_->client_response().trigger_byte_offset(),
        result_->client_response().trigger_bit_offset());
}

void ResourceListStage::fail_from_transition(
    const ResourceListStageTimePoint now) noexcept
{
    const auto& nested_error = transition_stage_.error();
    auto mapped_state = ResourceListStageState::protocol_error;
    switch (transition_stage_.state()) {
    case ResourceTransitionStageState::timed_out:
        mapped_state = ResourceListStageState::timed_out;
        break;
    case ResourceTransitionStageState::cancelled:
        mapped_state = ResourceListStageState::cancelled;
        break;
    case ResourceTransitionStageState::backpressure:
        mapped_state = ResourceListStageState::backpressure;
        break;
    case ResourceTransitionStageState::secondary_stream_pending:
        mapped_state = ResourceListStageState::secondary_stream_pending;
        break;
    case ResourceTransitionStageState::network_error:
        mapped_state = ResourceListStageState::network_error;
        break;
    case ResourceTransitionStageState::idle:
    case ResourceTransitionStageState::waiting_for_user_info_state:
    case ResourceTransitionStageState::request_ready:
    case ResourceTransitionStageState::waiting_for_request_transmit:
    case ResourceTransitionStageState::waiting_for_request_ack:
    case ResourceTransitionStageState::waiting_for_server_transfer:
    case ResourceTransitionStageState::decoding_transition_control:
    case ResourceTransitionStageState::neutral_opcode43_boundary_reached:
    case ResourceTransitionStageState::unsupported_message:
    case ResourceTransitionStageState::protocol_error:
        mapped_state = ResourceListStageState::protocol_error;
        break;
    }
    set_error(
        ResourceListStageErrorCode::transition_stage_failed,
        mapped_state,
        nested_error
            ? std::string_view{nested_error->context}
            : std::string_view{"Nested resource-transition stage terminated"},
        nested_error
            ? std::optional{nested_error->code}
            : std::nullopt,
        std::nullopt,
        std::nullopt,
        nested_error ? nested_error->driver_code : std::nullopt);
    if (can_push_events()) {
        push_event(ResourceListStageEvent{
            .type = terminal_event(mapped_state),
            .occurred_at = now,
        });
    }
    transition_stage_.finalize_retained_boundary(now);
    emit_trace(terminal_trace(mapped_state));
}

void ResourceListStage::fail_after_transition(
    const ResourceListStageErrorCode code,
    const ResourceListStageState state,
    const std::string_view context,
    const ResourceListStageTimePoint now,
    const std::optional<ResourceListErrorCode> resource_list_code,
    const std::optional<PostResourceListStreamErrorCode> post_stream_code,
    const std::optional<NetchanDriverErrorCode> driver_code) noexcept
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
        resource_list_code,
        post_stream_code,
        driver_code);
    if (can_push_events()) {
        push_event(ResourceListStageEvent{
            .type = terminal_event(state),
            .occurred_at = now,
        });
    }
    transition_stage_.finalize_retained_boundary(now);
    emit_trace(terminal_trace(state));
}

void ResourceListStage::set_error(
    const ResourceListStageErrorCode code,
    const ResourceListStageState state,
    const std::string_view context,
    const std::optional<ResourceTransitionStageErrorCode> transition_code,
    const std::optional<ResourceListErrorCode> resource_list_code,
    const std::optional<PostResourceListStreamErrorCode> post_stream_code,
    const std::optional<NetchanDriverErrorCode> driver_code) noexcept
{
    state_ = state;
    result_.reset();
    error_.reset();
    try {
        error_.emplace();
        error_->code = code;
        error_->transition_code = transition_code;
        error_->resource_list_code = resource_list_code;
        error_->post_stream_code = post_stream_code;
        error_->driver_code = driver_code;
        const auto bounded = context.substr(
            0U,
            (std::min)(context.size(), kResourceListStageDiagnosticTextLimit));
        error_->context.assign(bounded.data(), bounded.size());
    } catch (...) {
    }
}

void ResourceListStage::emit_trace(
    const ResourceListTraceClassification classification,
    const std::size_t resource_count,
    const std::size_t entry_ordinal,
    const std::optional<ResourceType> resource_type,
    const std::optional<std::uint16_t> resource_index,
    const std::optional<std::uint32_t> resource_size_code,
    const std::optional<std::uint8_t> resource_flags,
    const std::size_t resource_name_byte_count,
    const std::size_t byte_offset,
    const std::size_t bit_offset,
    const std::optional<std::uint8_t> opcode) noexcept
{
    if (!trace_callback_ || trace_callback_active_) {
        return;
    }
    ResourceListTraceEvent event;
    event.classification = classification;
    event.state = state_;
    event.endpoint = remote_endpoint();
    event.resource_count = resource_count;
    event.entry_ordinal = entry_ordinal;
    event.resource_type = resource_type;
    event.resource_index = resource_index;
    event.resource_size_code = resource_size_code;
    event.resource_flags = resource_flags;
    event.resource_name_byte_count = resource_name_byte_count;
    event.byte_offset = byte_offset;
    event.bit_offset = bit_offset;
    event.opcode = opcode;
    event.transmitted_packet_count = transmitted_packet_count();
    trace_callback_active_ = true;
    try { trace_callback_(event); } catch (...) {}
    trace_callback_active_ = false;
}

static_assert(std::is_nothrow_move_constructible_v<ResourceListStageEvent>);
static_assert(std::is_nothrow_move_constructible_v<ResourceListSignonState>);

} // namespace hlclient::goldsrc
