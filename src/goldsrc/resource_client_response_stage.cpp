#include <hlclient/goldsrc/resource_client_response_stage.hpp>

#include <algorithm>
#include <ranges>
#include <type_traits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

inline constexpr std::string_view kSupportedResponseWireName{"tempdecal.wad"};

[[nodiscard]] bool terminal_state(
    const ResourceClientResponseStageState state) noexcept
{
    switch (state) {
    case ResourceClientResponseStageState::next_server_boundary_reached:
    case ResourceClientResponseStageState::consistency_provider_required:
    case ResourceClientResponseStageState::unsupported_response_profile:
    case ResourceClientResponseStageState::timed_out:
    case ResourceClientResponseStageState::cancelled:
    case ResourceClientResponseStageState::backpressure:
    case ResourceClientResponseStageState::secondary_stream_pending:
    case ResourceClientResponseStageState::network_error:
    case ResourceClientResponseStageState::protocol_error:
        return true;
    case ResourceClientResponseStageState::idle:
    case ResourceClientResponseStageState::waiting_for_resource_list:
    case ResourceClientResponseStageState::preparing_response:
    case ResourceClientResponseStageState::waiting_for_consistency_provider:
    case ResourceClientResponseStageState::response_ready:
    case ResourceClientResponseStageState::waiting_for_response_transmit:
    case ResourceClientResponseStageState::waiting_for_response_ack:
    case ResourceClientResponseStageState::waiting_for_server_continuation:
    case ResourceClientResponseStageState::decoding_server_continuation:
        return false;
    }
    return true;
}

[[nodiscard]] ResourceClientResponseStageEventType terminal_event(
    const ResourceClientResponseStageState state) noexcept
{
    switch (state) {
    case ResourceClientResponseStageState::consistency_provider_required:
        return ResourceClientResponseStageEventType::consistency_provider_required;
    case ResourceClientResponseStageState::unsupported_response_profile:
        return ResourceClientResponseStageEventType::unsupported_response_profile;
    case ResourceClientResponseStageState::timed_out:
        return ResourceClientResponseStageEventType::timeout;
    case ResourceClientResponseStageState::cancelled:
        return ResourceClientResponseStageEventType::cancelled;
    case ResourceClientResponseStageState::backpressure:
        return ResourceClientResponseStageEventType::backpressure;
    case ResourceClientResponseStageState::secondary_stream_pending:
        return ResourceClientResponseStageEventType::secondary_stream_pending;
    case ResourceClientResponseStageState::network_error:
        return ResourceClientResponseStageEventType::network_error;
    case ResourceClientResponseStageState::idle:
    case ResourceClientResponseStageState::waiting_for_resource_list:
    case ResourceClientResponseStageState::preparing_response:
    case ResourceClientResponseStageState::waiting_for_consistency_provider:
    case ResourceClientResponseStageState::response_ready:
    case ResourceClientResponseStageState::waiting_for_response_transmit:
    case ResourceClientResponseStageState::waiting_for_response_ack:
    case ResourceClientResponseStageState::waiting_for_server_continuation:
    case ResourceClientResponseStageState::decoding_server_continuation:
    case ResourceClientResponseStageState::next_server_boundary_reached:
    case ResourceClientResponseStageState::protocol_error:
        return ResourceClientResponseStageEventType::protocol_error;
    }
    return ResourceClientResponseStageEventType::protocol_error;
}

[[nodiscard]] ResourceClientResponseTraceClassification terminal_trace(
    const ResourceClientResponseStageState state) noexcept
{
    switch (state) {
    case ResourceClientResponseStageState::next_server_boundary_reached:
        return ResourceClientResponseTraceClassification::
            next_server_boundary_reached;
    case ResourceClientResponseStageState::consistency_provider_required:
        return ResourceClientResponseTraceClassification::
            consistency_provider_required;
    case ResourceClientResponseStageState::unsupported_response_profile:
        return ResourceClientResponseTraceClassification::
            unsupported_response_profile;
    case ResourceClientResponseStageState::timed_out:
        return ResourceClientResponseTraceClassification::stage_timed_out;
    case ResourceClientResponseStageState::cancelled:
        return ResourceClientResponseTraceClassification::stage_cancelled;
    case ResourceClientResponseStageState::backpressure:
        return ResourceClientResponseTraceClassification::backpressure;
    case ResourceClientResponseStageState::secondary_stream_pending:
        return ResourceClientResponseTraceClassification::
            secondary_stream_pending;
    case ResourceClientResponseStageState::network_error:
        return ResourceClientResponseTraceClassification::network_failure;
    case ResourceClientResponseStageState::idle:
    case ResourceClientResponseStageState::waiting_for_resource_list:
    case ResourceClientResponseStageState::preparing_response:
    case ResourceClientResponseStageState::waiting_for_consistency_provider:
    case ResourceClientResponseStageState::response_ready:
    case ResourceClientResponseStageState::waiting_for_response_transmit:
    case ResourceClientResponseStageState::waiting_for_response_ack:
    case ResourceClientResponseStageState::waiting_for_server_continuation:
    case ResourceClientResponseStageState::decoding_server_continuation:
    case ResourceClientResponseStageState::protocol_error:
        return ResourceClientResponseTraceClassification::protocol_failure;
    }
    return ResourceClientResponseTraceClassification::protocol_failure;
}

[[nodiscard]] bool driver_network_error(
    const NetchanDriverErrorCode code) noexcept
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

[[nodiscard]] const NetchanDriverConfig& response_driver_config(
    const ResourceClientResponseStageConfig& config) noexcept
{
    return config.resource_list.transition.user_info.movement_environment.delta
        .pre_resource.initial_signon.driver;
}

[[nodiscard]] bool fixed_deadline_elapsed(
    const ResourceClientResponseStageTimePoint now,
    const ResourceClientResponseStageTimePoint started_at,
    const std::chrono::milliseconds timeout) noexcept
{
    const auto clock_timeout = std::chrono::duration_cast<
        ResourceClientResponseStageClock::duration>(timeout);
    if (started_at > ResourceClientResponseStageTimePoint::max() -
                         clock_timeout) {
        return true;
    }
    return now >= started_at + clock_timeout;
}

} // namespace

ResourceListStageConfig
default_resource_client_response_resource_list_stage_config()
{
    ResourceListStageConfig config;
    config.transition.user_info.movement_environment.delta.pre_resource
        .initial_signon.driver.maximum_unfragmented_reliable_payload =
        kOpcode5ResourceResponseSemanticSize - 1U;
    return config;
}

bool valid_resource_client_response_stage_configuration(
    const ResourceClientResponseStageConfig& config) noexcept
{
    const auto& driver = response_driver_config(config);
    return valid_resource_list_stage_configuration(config.resource_list) &&
           valid_resource_client_response_limits(config.response) &&
           config.consistency_provider_timeout.count() > 0 &&
           config.consistency_provider_timeout <=
               kMaximumResourceConsistencyProviderTimeout &&
           config.response_acknowledgement_timeout.count() > 0 &&
           config.response_acknowledgement_timeout <=
               kMaximumResourceResponseAcknowledgementTimeout &&
           config.post_ack_boundary_timeout.count() > 0 &&
           config.post_ack_boundary_timeout <=
               kMaximumPostResourceResponseBoundaryTimeout &&
           config.maximum_driver_events_per_update > 0U &&
           config.maximum_driver_events_per_update <=
               kMaximumResourceResponseDriverEventsPerUpdate &&
           driver.maximum_unfragmented_reliable_payload.has_value() &&
           *driver.maximum_unfragmented_reliable_payload ==
               kOpcode5ResourceResponseSemanticSize - 1U;
}

ResourceResponseReliableLifecycle::ResourceResponseReliableLifecycle(
    const std::uint64_t reliable_generation,
    const bool fragmented,
    const std::uint16_t fragment_count,
    const bool reliable_toggle,
    const std::uint32_t first_transmit_sequence,
    const std::uint32_t most_recent_transmit_sequence,
    const std::uint64_t transmit_count,
    NetchanAcknowledgementObservation acknowledgement) noexcept
    : reliable_generation_{reliable_generation},
      fragmented_{fragmented},
      fragment_count_{fragment_count},
      reliable_toggle_{reliable_toggle},
      first_transmit_sequence_{first_transmit_sequence},
      most_recent_transmit_sequence_{most_recent_transmit_sequence},
      transmit_count_{transmit_count},
      acknowledgement_{acknowledgement}
{
}

std::uint64_t
ResourceResponseReliableLifecycle::reliable_generation() const noexcept
{
    return reliable_generation_;
}

bool ResourceResponseReliableLifecycle::fragmented() const noexcept
{
    return fragmented_;
}

std::uint16_t ResourceResponseReliableLifecycle::fragment_count() const noexcept
{
    return fragment_count_;
}

bool ResourceResponseReliableLifecycle::reliable_toggle() const noexcept
{
    return reliable_toggle_;
}

std::uint32_t
ResourceResponseReliableLifecycle::first_transmit_sequence() const noexcept
{
    return first_transmit_sequence_;
}

std::uint32_t
ResourceResponseReliableLifecycle::most_recent_transmit_sequence() const noexcept
{
    return most_recent_transmit_sequence_;
}

std::uint64_t ResourceResponseReliableLifecycle::transmit_count() const noexcept
{
    return transmit_count_;
}

const NetchanAcknowledgementObservation&
ResourceResponseReliableLifecycle::acknowledgement() const noexcept
{
    return acknowledgement_;
}

ResourceClientResponseSignonState::ResourceClientResponseSignonState(
    ResourceListSignonState resource_list,
    Opcode5ResourceResponse response,
    std::optional<ResourceResponseCarrierGeometry> source_carrier_geometry,
    std::optional<ResourceResponseConcurrentTail> concurrent_tail,
    ResourceResponseReliableLifecycle reliable_lifecycle,
    PostResourceResponseBoundary boundary) noexcept
    : resource_list_{std::move(resource_list)},
      response_{std::move(response)},
      source_carrier_geometry_{std::move(source_carrier_geometry)},
      concurrent_tail_{std::move(concurrent_tail)},
      reliable_lifecycle_{std::move(reliable_lifecycle)},
      boundary_{std::move(boundary)}
{
}

const ResourceListSignonState&
ResourceClientResponseSignonState::resource_list() const noexcept
{
    return resource_list_;
}

const Opcode5ResourceResponse&
ResourceClientResponseSignonState::response() const noexcept
{
    return response_;
}

const std::optional<ResourceResponseCarrierGeometry>&
ResourceClientResponseSignonState::source_carrier_geometry() const noexcept
{
    return source_carrier_geometry_;
}

const std::optional<ResourceResponseConcurrentTail>&
ResourceClientResponseSignonState::concurrent_tail() const noexcept
{
    return concurrent_tail_;
}

const ResourceResponseReliableLifecycle&
ResourceClientResponseSignonState::reliable_lifecycle() const noexcept
{
    return reliable_lifecycle_;
}

const PostResourceResponseBoundary&
ResourceClientResponseSignonState::boundary() const noexcept
{
    return boundary_;
}

ResourceClientResponseCompatibilityProfile
ResourceClientResponseSignonState::compatibility_profile() const noexcept
{
    return response_.compatibility_profile();
}

ResourceClientResponseEvidenceProfile
ResourceClientResponseSignonState::evidence_profile() const noexcept
{
    return response_.evidence_profile();
}

ResourceClientResponseStage::ResourceClientResponseStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    ResourceClientResponseStageConfig config,
    resource_consistency::IResourceConsistencyProvider* consistency_provider,
    InitialSignonTraceCallback initial_trace_callback,
    PreResourceSignonTraceCallback pre_resource_trace_callback,
    DeltaDescriptionTraceCallback delta_trace_callback,
    MovementEnvironmentTraceCallback movement_trace_callback,
    UserInfoSignonTraceCallback user_info_trace_callback,
    ResourceTransitionTraceCallback transition_trace_callback,
    ResourceListTraceCallback resource_list_trace_callback,
    ResourceClientResponseTraceCallback trace_callback)
    : ResourceClientResponseStage{
          transport,
          remote_endpoint,
          std::move(config),
          consistency_provider,
          std::move(initial_trace_callback),
          std::move(pre_resource_trace_callback),
          std::move(delta_trace_callback),
          std::move(movement_trace_callback),
          std::move(user_info_trace_callback),
          std::move(transition_trace_callback),
          std::move(resource_list_trace_callback),
          std::move(trace_callback),
          false}
{
}

ResourceClientResponseStage::ResourceClientResponseStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    ResourceClientResponseStageConfig config,
    resource_consistency::IResourceConsistencyProvider* consistency_provider,
    InitialSignonTraceCallback initial_trace_callback,
    PreResourceSignonTraceCallback pre_resource_trace_callback,
    DeltaDescriptionTraceCallback delta_trace_callback,
    MovementEnvironmentTraceCallback movement_trace_callback,
    UserInfoSignonTraceCallback user_info_trace_callback,
    ResourceTransitionTraceCallback transition_trace_callback,
    ResourceListTraceCallback resource_list_trace_callback,
    ResourceClientResponseTraceCallback trace_callback,
    RetainConnectionAtBoundary)
    : ResourceClientResponseStage{
          transport,
          remote_endpoint,
          std::move(config),
          consistency_provider,
          std::move(initial_trace_callback),
          std::move(pre_resource_trace_callback),
          std::move(delta_trace_callback),
          std::move(movement_trace_callback),
          std::move(user_info_trace_callback),
          std::move(transition_trace_callback),
          std::move(resource_list_trace_callback),
          std::move(trace_callback),
          true}
{
}

ResourceClientResponseStage::ResourceClientResponseStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    ResourceClientResponseStageConfig config,
    resource_consistency::IResourceConsistencyProvider* consistency_provider,
    InitialSignonTraceCallback initial_trace_callback,
    PreResourceSignonTraceCallback pre_resource_trace_callback,
    DeltaDescriptionTraceCallback delta_trace_callback,
    MovementEnvironmentTraceCallback movement_trace_callback,
    UserInfoSignonTraceCallback user_info_trace_callback,
    ResourceTransitionTraceCallback transition_trace_callback,
    ResourceListTraceCallback resource_list_trace_callback,
    ResourceClientResponseTraceCallback trace_callback,
    const bool retain_connection_at_boundary)
    : config_{std::move(config)},
      consistency_provider_{consistency_provider},
      trace_callback_{std::move(trace_callback)},
      configuration_valid_{
          valid_resource_client_response_stage_configuration(config_)},
      retain_connection_at_boundary_{retain_connection_at_boundary},
      resource_list_stage_{
          transport,
          remote_endpoint,
          config_.resource_list,
          std::move(initial_trace_callback),
          std::move(pre_resource_trace_callback),
          std::move(delta_trace_callback),
          std::move(movement_trace_callback),
          std::move(user_info_trace_callback),
          std::move(transition_trace_callback),
          std::move(resource_list_trace_callback),
          ResourceListStage::RetainConnectionAtBoundary{}},
      event_slots_(
          configuration_valid_
              ? config_.response.maximum_response_stage_events
              : 0U)
{
}

ResourceClientResponseStage::~ResourceClientResponseStage()
{
    if (consistency_operation_) {
        consistency_operation_->cancel();
        consistency_operation_.reset();
    }
    consistency_session_.reset();
}

bool ResourceClientResponseStage::start(
    const ResourceClientResponseStageTimePoint now,
    const network::NetworkAddress& expected_local_endpoint,
    std::unique_ptr<INetchanDriverLifetime> connection_lifetime)
{
    if (trace_callback_active_ ||
        state_ != ResourceClientResponseStageState::idle) {
        return false;
    }
    error_.reset();
    if (!configuration_valid_) {
        fail(
            ResourceClientResponseStageErrorCode::invalid_configuration,
            ResourceClientResponseStageState::protocol_error,
            "Resource-client-response stage configuration is outside project bounds",
            now);
        return false;
    }

    bool started = false;
    try {
        started = resource_list_stage_.start(
            now,
            expected_local_endpoint,
            std::move(connection_lifetime));
    } catch (...) {
        try { resource_list_stage_.cancel(now); } catch (...) {}
        fail(
            ResourceClientResponseStageErrorCode::
                resource_list_stage_start_failed,
            ResourceClientResponseStageState::protocol_error,
            "Nested resource-list stage threw during start",
            now);
        return false;
    }
    drain_resource_list_events();
    if (!started) {
        fail_from_resource_list(now);
        if (error_) {
            error_->code = ResourceClientResponseStageErrorCode::
                resource_list_stage_start_failed;
        }
        return false;
    }

    last_update_ = now;
    state_ = ResourceClientResponseStageState::waiting_for_resource_list;
    emit_trace(ResourceClientResponseTraceClassification::stage_started);
    return true;
}

void ResourceClientResponseStage::update(
    const ResourceClientResponseStageTimePoint now)
{
    if (trace_callback_active_ ||
        state_ == ResourceClientResponseStageState::idle ||
        terminal_state(state_)) {
        return;
    }
    if (!last_update_ || now < *last_update_) {
        fail(
            ResourceClientResponseStageErrorCode::time_moved_backwards,
            ResourceClientResponseStageState::protocol_error,
            "Resource-client-response stage update time moved backwards",
            now);
        return;
    }
    last_update_ = now;

    if (state_ ==
        ResourceClientResponseStageState::waiting_for_resource_list) {
        drain_resource_list_events();
        try {
            resource_list_stage_.update(now);
        } catch (...) {
            try { resource_list_stage_.cancel(now); } catch (...) {}
            fail(
                ResourceClientResponseStageErrorCode::resource_list_stage_failed,
                ResourceClientResponseStageState::protocol_error,
                "Nested resource-list stage threw during update",
                now);
            return;
        }
        drain_resource_list_events();
        synchronize_from_resource_list(now);
        if (terminal_state(state_) ||
            state_ ==
                ResourceClientResponseStageState::waiting_for_resource_list) {
            return;
        }
    }

    if (state_ == ResourceClientResponseStageState::
                      waiting_for_consistency_provider) {
        if (!consistency_provider_started_at_ ||
            fixed_deadline_elapsed(
                now,
                *consistency_provider_started_at_,
                config_.consistency_provider_timeout)) {
            fail(
                ResourceClientResponseStageErrorCode::
                    consistency_provider_failed,
                ResourceClientResponseStageState::timed_out,
                "Resource-consistency provider exceeded its bounded deadline",
                now,
                std::nullopt,
                resource_consistency::ResourceConsistencyErrorCode::timed_out);
            return;
        }
        poll_consistency_provider(now);
        if (terminal_state(state_)) {
            return;
        }
    }

    if (response_queue_count_ == 1U && !response_acknowledged_ &&
        (!response_acknowledgement_started_at_ ||
         fixed_deadline_elapsed(
             now,
             *response_acknowledgement_started_at_,
             config_.response_acknowledgement_timeout))) {
        fail(
            ResourceClientResponseStageErrorCode::
                response_acknowledgement_timed_out,
            ResourceClientResponseStageState::timed_out,
            "Queued resource response was not acknowledged before its fixed deadline",
            now);
        return;
    }
    if (response_acknowledged_ &&
        (!post_ack_boundary_started_at_ ||
         fixed_deadline_elapsed(
             now,
             *post_ack_boundary_started_at_,
             config_.post_ack_boundary_timeout))) {
        fail(
            ResourceClientResponseStageErrorCode::
                post_response_boundary_timed_out,
            ResourceClientResponseStageState::timed_out,
            "Post-response server boundary did not arrive before its fixed deadline",
            now);
        return;
    }

    drive_transport(now);
}

void ResourceClientResponseStage::cancel(
    const ResourceClientResponseStageTimePoint now)
{
    if (trace_callback_active_ ||
        state_ == ResourceClientResponseStageState::idle ||
        terminal_state(state_)) {
        return;
    }

    if (state_ ==
        ResourceClientResponseStageState::waiting_for_resource_list) {
        try {
            resource_list_stage_.cancel(now);
        } catch (...) {
            fail(
                ResourceClientResponseStageErrorCode::resource_list_stage_failed,
                ResourceClientResponseStageState::protocol_error,
                "Nested resource-list stage threw during cancellation",
                now);
            return;
        }
    } else if (auto* const driver = resource_list_stage_.retained_driver();
               driver != nullptr && !driver->terminal()) {
        driver->cancel(now);
    }

    state_ = ResourceClientResponseStageState::cancelled;
    result_.reset();
    error_.reset();
    if (can_push_events()) {
        push_event(ResourceClientResponseStageEvent{
            .type = ResourceClientResponseStageEventType::cancelled,
            .occurred_at = now,
        });
    }
    cleanup(now);
    emit_trace(ResourceClientResponseTraceClassification::stage_cancelled);
}

std::optional<ResourceClientResponseStageEvent>
ResourceClientResponseStage::poll_event()
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

ResourceClientResponseStageState
ResourceClientResponseStage::state() const noexcept
{
    return state_;
}

bool ResourceClientResponseStage::terminal() const noexcept
{
    return terminal_state(state_);
}

const std::optional<ResourceClientResponseSignonState>&
ResourceClientResponseStage::result() const noexcept
{
    return result_;
}

const std::optional<ResourceClientResponseStageError>&
ResourceClientResponseStage::error() const noexcept
{
    return error_;
}

const network::NetworkAddress&
ResourceClientResponseStage::remote_endpoint() const noexcept
{
    return resource_list_stage_.remote_endpoint();
}

const std::optional<network::NetworkAddress>&
ResourceClientResponseStage::local_endpoint() const noexcept
{
    return resource_list_stage_.local_endpoint();
}

std::size_t ResourceClientResponseStage::pending_event_count() const noexcept
{
    return event_size_;
}

std::size_t
ResourceClientResponseStage::transmitted_packet_count() const noexcept
{
    return resource_list_stage_.transmitted_packet_count();
}

std::size_t ResourceClientResponseStage::cleanup_count() const noexcept
{
    return resource_list_stage_.cleanup_count();
}

std::size_t
ResourceClientResponseStage::initial_request_queue_count() const noexcept
{
    return resource_list_stage_.initial_request_queue_count();
}

std::size_t
ResourceClientResponseStage::transition_request_queue_count() const noexcept
{
    return resource_list_stage_.transition_request_queue_count();
}

std::size_t
ResourceClientResponseStage::response_queue_count() const noexcept
{
    return response_queue_count_;
}

std::size_t
ResourceClientResponseStage::response_build_count() const noexcept
{
    return response_build_count_;
}

std::size_t
ResourceClientResponseStage::requirements_derivation_count() const noexcept
{
    return requirements_derivation_count_;
}

std::size_t ResourceClientResponseStage::provider_begin_count() const noexcept
{
    return provider_begin_count_;
}

bool ResourceClientResponseStage::response_transmitted() const noexcept
{
    return response_transmitted_;
}

bool ResourceClientResponseStage::response_acknowledged() const noexcept
{
    return response_acknowledged_;
}

NetchanDriver* ResourceClientResponseStage::retained_driver() noexcept
{
    if (!retain_connection_at_boundary_ ||
        state_ !=
            ResourceClientResponseStageState::next_server_boundary_reached ||
        cleanup_done_) {
        return nullptr;
    }
    return resource_list_stage_.retained_driver();
}

void ResourceClientResponseStage::finalize_retained_boundary(
    const ResourceClientResponseStageTimePoint now) noexcept
{
    if (!retain_connection_at_boundary_ ||
        state_ !=
            ResourceClientResponseStageState::next_server_boundary_reached) {
        return;
    }
    cleanup(now);
}

bool ResourceClientResponseStage::can_push_events(
    const std::size_t count) const noexcept
{
    return count <= event_slots_.size() - event_size_;
}

void ResourceClientResponseStage::push_event(
    ResourceClientResponseStageEvent event) noexcept
{
    const auto index = (event_head_ + event_size_) % event_slots_.size();
    event_slots_[index].emplace(std::move(event));
    ++event_size_;
}

void ResourceClientResponseStage::drain_resource_list_events() noexcept
{
    while (resource_list_stage_.poll_event()) {
    }
}

void ResourceClientResponseStage::synchronize_from_resource_list(
    const ResourceClientResponseStageTimePoint now)
{
    if (resource_list_stage_.state() ==
        ResourceListStageState::client_response_required) {
        if (!resource_list_stage_.result() ||
            resource_list_stage_.retained_driver() == nullptr) {
            fail(
                ResourceClientResponseStageErrorCode::retained_driver_missing,
                ResourceClientResponseStageState::protocol_error,
                "Resource-list completion has no owning result or retained driver",
                now);
            return;
        }
        emit_trace(
            ResourceClientResponseTraceClassification::resource_list_ready);
        determine_requirements(now);
        return;
    }
    if (resource_list_stage_.terminal() || resource_list_stage_.error()) {
        fail_from_resource_list(now);
    }
}

void ResourceClientResponseStage::determine_requirements(
    const ResourceClientResponseStageTimePoint now)
{
    if (requirements_derivation_count_ != 0U || requirements_ ||
        consistency_operation_ || response_encoding_ ||
        response_queue_count_ != 0U) {
        fail(
            ResourceClientResponseStageErrorCode::response_requirements_failed,
            ResourceClientResponseStageState::protocol_error,
            "Resource-response requirements were derived more than once",
            now);
        return;
    }
    if (!can_push_events(2U)) {
        fail(
            ResourceClientResponseStageErrorCode::event_backpressure,
            ResourceClientResponseStageState::backpressure,
            "Resource-response requirement publication exceeds bounded event capacity",
            now);
        return;
    }

    state_ = ResourceClientResponseStageState::preparing_response;
    ++requirements_derivation_count_;
    auto requirements = resource_consistency::ResourceConsistencyRequirements::
        stock_opcode5_single_resource();
    if (!requirements || requirements->material_count() != 1U ||
        requirements->opaque_bytes_per_material() !=
            kOpcode5ResourceResponseOpaqueSize) {
        fail(
            ResourceClientResponseStageErrorCode::response_requirements_failed,
            ResourceClientResponseStageState::unsupported_response_profile,
            "Supported opcode-5 response requirements are unavailable",
            now);
        return;
    }
    requirements_.emplace(*requirements);
    push_event(ResourceClientResponseStageEvent{
        .type = ResourceClientResponseStageEventType::
            resource_response_requirements_ready,
        .semantic_byte_count = kOpcode5ResourceResponseSemanticSize,
        .opcode = kOpcode5ResourceResponseOpcode,
        .occurred_at = now,
    });
    emit_trace(
        ResourceClientResponseTraceClassification::
            resource_response_requirements_ready,
        0U,
        0U,
        kOpcode5ResourceResponseOpcode);

    if (consistency_provider_ == nullptr) {
        fail(
            ResourceClientResponseStageErrorCode::provider_required,
            ResourceClientResponseStageState::consistency_provider_required,
            "The supported response profile requires a path-free consistency provider",
            now,
            std::nullopt,
            resource_consistency::ResourceConsistencyErrorCode::unavailable);
        return;
    }

    resource_consistency::ResourceConsistencyBeginResult begun;
    ++provider_begin_count_;
    try {
        begun = consistency_provider_->begin(*requirements_);
    } catch (...) {
        fail(
            ResourceClientResponseStageErrorCode::
                consistency_provider_begin_failed,
            ResourceClientResponseStageState::protocol_error,
            "Resource-consistency provider threw during begin",
            now,
            std::nullopt,
            resource_consistency::ResourceConsistencyErrorCode::provider_error);
        return;
    }
    if (!begun.operation || begun.error) {
        if (begun.operation) {
            begun.operation->cancel();
        }
        const auto provider_code = begun.error
            ? begun.error->code
            : resource_consistency::ResourceConsistencyErrorCode::provider_error;
        const auto provider_required =
            provider_code ==
            resource_consistency::ResourceConsistencyErrorCode::unavailable;
        fail(
            provider_required
                ? ResourceClientResponseStageErrorCode::provider_required
                : ResourceClientResponseStageErrorCode::
                      consistency_provider_begin_failed,
            provider_required
                ? ResourceClientResponseStageState::
                      consistency_provider_required
                : ResourceClientResponseStageState::protocol_error,
            begun.error
                ? std::string_view{
                      "Resource-consistency provider rejected the bounded request"}
                : std::string_view{
                      "Resource-consistency provider returned no operation"},
            now,
            std::nullopt,
            provider_code);
        return;
    }

    consistency_operation_ = std::move(begun.operation);
    consistency_provider_started_at_ = now;
    state_ = ResourceClientResponseStageState::
        waiting_for_consistency_provider;
}

void ResourceClientResponseStage::poll_consistency_provider(
    const ResourceClientResponseStageTimePoint now)
{
    if (!consistency_operation_) {
        fail(
            ResourceClientResponseStageErrorCode::
                consistency_provider_result_invalid,
            ResourceClientResponseStageState::protocol_error,
            "Consistency-provider wait state has no owning operation",
            now);
        return;
    }

    std::optional<resource_consistency::ResourceConsistencyUpdateResult>
        updated;
    try {
        updated.emplace(consistency_operation_->update());
    } catch (...) {
        fail(
            ResourceClientResponseStageErrorCode::consistency_provider_failed,
            ResourceClientResponseStageState::protocol_error,
            "Resource-consistency provider threw during update",
            now,
            std::nullopt,
            resource_consistency::ResourceConsistencyErrorCode::provider_error);
        return;
    }

    switch (updated->state) {
    case resource_consistency::ResourceConsistencyUpdateState::pending:
        if (updated->session || updated->error) {
            fail(
                ResourceClientResponseStageErrorCode::
                    consistency_provider_result_invalid,
                ResourceClientResponseStageState::protocol_error,
                "Pending consistency-provider result contains terminal data",
                now);
        }
        return;
    case resource_consistency::ResourceConsistencyUpdateState::failed: {
        if (updated->session || !updated->error) {
            fail(
                ResourceClientResponseStageErrorCode::
                    consistency_provider_result_invalid,
                ResourceClientResponseStageState::protocol_error,
                "Failed consistency-provider result is structurally invalid",
                now);
            return;
        }
        const auto provider_code = updated->error->code;
        consistency_operation_.reset();
        auto failure_state = ResourceClientResponseStageState::protocol_error;
        if (provider_code ==
            resource_consistency::ResourceConsistencyErrorCode::unavailable) {
            failure_state = ResourceClientResponseStageState::
                consistency_provider_required;
        } else if (provider_code ==
                   resource_consistency::ResourceConsistencyErrorCode::
                       timed_out) {
            failure_state = ResourceClientResponseStageState::timed_out;
        } else if (provider_code ==
                   resource_consistency::ResourceConsistencyErrorCode::
                       cancelled) {
            failure_state = ResourceClientResponseStageState::cancelled;
        }
        fail(
            failure_state == ResourceClientResponseStageState::
                                 consistency_provider_required
                ? ResourceClientResponseStageErrorCode::provider_required
                : ResourceClientResponseStageErrorCode::
                      consistency_provider_failed,
            failure_state,
            "Resource-consistency provider reported a typed failure",
            now,
            std::nullopt,
            provider_code);
        return;
    }
    case resource_consistency::ResourceConsistencyUpdateState::succeeded:
        break;
    default:
        fail(
            ResourceClientResponseStageErrorCode::
                consistency_provider_result_invalid,
            ResourceClientResponseStageState::protocol_error,
            "Consistency provider returned an unknown update state",
            now);
        return;
    }

    if (!updated->session || updated->error ||
        !updated->session->has_material() ||
        updated->session->opaque_byte_count() !=
            kOpcode5ResourceResponseOpaqueSize) {
        fail(
            ResourceClientResponseStageErrorCode::consistency_material_invalid,
            ResourceClientResponseStageState::protocol_error,
            "Successful consistency-provider result has invalid bounded material",
            now,
            std::nullopt,
            resource_consistency::ResourceConsistencyErrorCode::invalid_material);
        return;
    }

    consistency_session_.emplace(std::move(*updated->session));
    consistency_operation_.reset();
    consistency_provider_started_at_.reset();
    auto material = consistency_session_->take_material();
    if (!material || material->opaque_byte_count() !=
                         kOpcode5ResourceResponseOpaqueSize) {
        fail(
            ResourceClientResponseStageErrorCode::consistency_material_invalid,
            ResourceClientResponseStageState::protocol_error,
            "Consistency session did not transfer one exact-width material",
            now,
            std::nullopt,
            resource_consistency::ResourceConsistencyErrorCode::invalid_material);
        return;
    }
    build_and_queue_response(std::move(*material), now);
}

void ResourceClientResponseStage::build_and_queue_response(
    resource_consistency::ResourceConsistencyMaterial material,
    const ResourceClientResponseStageTimePoint now)
{
    auto* const driver = resource_list_stage_.retained_driver();
    if (!resource_list_stage_.result() || driver == nullptr) {
        fail(
            ResourceClientResponseStageErrorCode::retained_driver_missing,
            ResourceClientResponseStageState::protocol_error,
            "Response preparation lost its retained persistent driver",
            now);
        return;
    }
    if (response_build_count_ != 0U || response_queue_count_ != 0U ||
        response_encoding_) {
        fail(
            ResourceClientResponseStageErrorCode::response_build_failed,
            ResourceClientResponseStageState::protocol_error,
            "Resource response build or semantic queue operation was duplicated",
            now);
        return;
    }
    // Two semantic events are published below; keep one additional slot for a
    // terminal failure if a later queue/transport invariant rejects the work.
    if (!can_push_events(3U)) {
        fail(
            ResourceClientResponseStageErrorCode::event_backpressure,
            ResourceClientResponseStageState::backpressure,
            "No bounded event slots remain for response build and queue",
            now);
        return;
    }

    std::optional<Opcode5ResourceResponseBuildResult> built;
    ++response_build_count_;
    try {
        const Opcode5ResourceResponseBuilder builder{config_.response};
        built.emplace(builder.build(ResourceClientResponseInput{
            std::string{kSupportedResponseWireName},
            kOpcode5ResourceResponseFieldType,
            kOpcode5ResourceResponseFieldIndex,
            kOpcode5ResourceResponseFieldFlags,
            std::move(material)}));
    } catch (...) {
        fail(
            ResourceClientResponseStageErrorCode::response_build_failed,
            ResourceClientResponseStageState::protocol_error,
            "Typed opcode-5 response builder threw",
            now);
        return;
    }
    if (!*built || !built->encoding ||
        built->encoding->semantic_bytes().size() !=
            kOpcode5ResourceResponseSemanticSize) {
        fail(
            ResourceClientResponseStageErrorCode::response_build_failed,
            ResourceClientResponseStageState::protocol_error,
            built->error
                ? std::string_view{built->error->context}
                : std::string_view{
                      "Typed opcode-5 response builder returned no exact encoding"},
            now,
            std::nullopt,
            std::nullopt,
            built->error ? std::optional{built->error->code} : std::nullopt);
        return;
    }

    response_encoding_.emplace(std::move(*built->encoding));
    state_ = ResourceClientResponseStageState::response_ready;
    push_event(ResourceClientResponseStageEvent{
        .type = ResourceClientResponseStageEventType::resource_response_ready,
        .semantic_byte_count = response_encoding_->semantic_bytes().size(),
        .opcode = response_encoding_->response().opcode(),
        .occurred_at = now,
    });
    emit_trace(
        ResourceClientResponseTraceClassification::resource_response_ready,
        0U,
        0U,
        response_encoding_->response().opcode());

    NetchanDriverOperationResult queued;
    try {
        queued = driver->queue_reliable(response_encoding_->semantic_bytes());
    } catch (...) {
        fail(
            ResourceClientResponseStageErrorCode::response_queue_failed,
            ResourceClientResponseStageState::protocol_error,
            "Persistent driver threw while queueing the typed response",
            now);
        return;
    }
    if (!queued) {
        fail(
            ResourceClientResponseStageErrorCode::response_queue_failed,
            ResourceClientResponseStageState::protocol_error,
            queued.error
                ? std::string_view{queued.error->context}
                : std::string_view{
                      "Persistent driver rejected the typed resource response"},
            now,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            queued.error ? std::optional{queued.error->code} : std::nullopt);
        return;
    }

    ++response_queue_count_;
    response_acknowledgement_started_at_ = now;
    push_event(ResourceClientResponseStageEvent{
        .type = ResourceClientResponseStageEventType::resource_response_queued,
        .semantic_byte_count = response_encoding_->semantic_bytes().size(),
        .opcode = response_encoding_->response().opcode(),
        .occurred_at = now,
    });
    emit_trace(
        ResourceClientResponseTraceClassification::resource_response_queued,
        0U,
        0U,
        response_encoding_->response().opcode());
    state_ = ResourceClientResponseStageState::
        waiting_for_response_transmit;
}

void ResourceClientResponseStage::drive_transport(
    const ResourceClientResponseStageTimePoint now)
{
    auto* const driver = resource_list_stage_.retained_driver();
    if (driver == nullptr) {
        fail(
            ResourceClientResponseStageErrorCode::retained_driver_missing,
            ResourceClientResponseStageState::protocol_error,
            "Resource response lost its retained persistent driver",
            now);
        return;
    }

    if (pending_decode_payload_ && response_acknowledged_) {
        decode_pending_server_payload(now);
        if (terminal_state(state_) || pending_decode_payload_) {
            return;
        }
    }

    observe_response_transmit(now);
    if (terminal_state(state_)) {
        return;
    }
    std::size_t processed_events = 0U;
    drain_driver_events(now, processed_events);
    if (terminal_state(state_)) {
        return;
    }
    if (pending_decode_payload_ && response_acknowledged_) {
        decode_pending_server_payload(now);
        if (terminal_state(state_)) {
            return;
        }
    }
    if (processed_events >= config_.maximum_driver_events_per_update) {
        return;
    }

    // One update can publish transmit, ACK, continuation, and boundary events.
    // Preflight their maximum bounded fan-out before transport side effects.
    const auto required_capacity = response_queue_count_ == 0U ? 1U : 4U;
    if (!can_push_events(required_capacity + 1U)) {
        fail(
            ResourceClientResponseStageErrorCode::event_backpressure,
            ResourceClientResponseStageState::backpressure,
            "No bounded event capacity remains for one response transport update",
            now);
        return;
    }

    server_payloads_admissible_ = response_transmitted_;
    try {
        driver->update(now);
    } catch (...) {
        fail(
            ResourceClientResponseStageErrorCode::driver_failed,
            ResourceClientResponseStageState::protocol_error,
            "Persistent driver threw during resource-response update",
            now);
        return;
    }
    observe_response_transmit(now);
    if (terminal_state(state_)) {
        return;
    }
    drain_driver_events(now, processed_events);
    if (terminal_state(state_)) {
        return;
    }
    if (pending_decode_payload_ && response_acknowledged_) {
        decode_pending_server_payload(now);
        if (terminal_state(state_)) {
            return;
        }
    }
    if (driver->terminal()) {
        fail_from_driver(now);
    }
}

void ResourceClientResponseStage::observe_response_transmit(
    const ResourceClientResponseStageTimePoint now)
{
    if (response_queue_count_ == 0U) {
        return;
    }
    auto* const driver = resource_list_stage_.retained_driver();
    if (driver == nullptr || !response_encoding_) {
        return;
    }
    const auto& transfer = driver->session().outgoing_fragment_transfer();
    const auto& in_flight = driver->session().in_flight_reliable_payload();
    if (!transfer || !in_flight) {
        return;
    }
    if (!std::ranges::equal(
            transfer->canonical_bytes,
            response_encoding_->semantic_bytes()) ||
        transfer->fragment_count != 1U ||
        transfer->current_fragment_index != 1U ||
        !transfer->transfer_id.valid()) {
        fail(
            ResourceClientResponseStageErrorCode::response_transmit_mismatch,
            ResourceClientResponseStageState::unsupported_response_profile,
            "In-flight response contradicts the exact one-fragment canonical profile",
            now);
        return;
    }

    if (response_transmitted_) {
        if (reliable_generation_ != transfer->transfer_id.value() ||
            response_fragment_count_ != transfer->fragment_count ||
            response_reliable_toggle_ != in_flight->toggle ||
            !first_transmit_sequence_ ||
            *first_transmit_sequence_ != in_flight->first_sent_sequence ||
            in_flight->send_count < response_transmit_count_) {
            fail(
                ResourceClientResponseStageErrorCode::response_transmit_mismatch,
                ResourceClientResponseStageState::protocol_error,
                "Reliable response generation changed outside driver retransmission",
                now);
            return;
        }
        most_recent_transmit_sequence_ =
            in_flight->most_recent_sent_sequence;
        response_transmit_count_ = in_flight->send_count;
        return;
    }

    if (!can_push_events()) {
        fail(
            ResourceClientResponseStageErrorCode::event_backpressure,
            ResourceClientResponseStageState::backpressure,
            "No bounded event slot remains for response transmission",
            now);
        return;
    }
    response_transmitted_ = true;
    reliable_generation_ = transfer->transfer_id.value();
    response_fragment_count_ = transfer->fragment_count;
    response_reliable_toggle_ = in_flight->toggle;
    first_transmit_sequence_ = in_flight->first_sent_sequence;
    most_recent_transmit_sequence_ = in_flight->most_recent_sent_sequence;
    response_transmit_count_ = in_flight->send_count;
    state_ = ResourceClientResponseStageState::waiting_for_response_ack;
    push_event(ResourceClientResponseStageEvent{
        .type = ResourceClientResponseStageEventType::
            resource_response_transmitted,
        .semantic_byte_count = response_encoding_->semantic_bytes().size(),
        .opcode = response_encoding_->response().opcode(),
        .reliable_generation = reliable_generation_,
        .transmit_sequence = most_recent_transmit_sequence_->value(),
        .transmit_count = response_transmit_count_,
        .reliable = true,
        .fragmented = true,
        .occurred_at = now,
    });
    emit_trace(
        ResourceClientResponseTraceClassification::
            resource_response_transmitted,
        0U,
        0U,
        response_encoding_->response().opcode(),
        most_recent_transmit_sequence_->value());
}

void ResourceClientResponseStage::drain_driver_events(
    const ResourceClientResponseStageTimePoint now,
    std::size_t& processed_events)
{
    auto* const driver = resource_list_stage_.retained_driver();
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

void ResourceClientResponseStage::handle_driver_event(
    NetchanDriverEvent event,
    const ResourceClientResponseStageTimePoint now)
{
    switch (event.type) {
    case NetchanDriverEventType::payload_ready:
        if (!event.payload) {
            fail(
                ResourceClientResponseStageErrorCode::driver_failed,
                ResourceClientResponseStageState::protocol_error,
                "Driver payload event has no owning payload",
                now);
            return;
        }
        handle_server_payload(std::move(*event.payload), now);
        return;
    case NetchanDriverEventType::reliable_payload_acknowledged:
        handle_response_acknowledgement(event, now);
        return;
    case NetchanDriverEventType::normal_transfer_started:
    case NetchanDriverEventType::normal_transfer_completed:
        return;
    case NetchanDriverEventType::normal_transfer_timed_out:
    case NetchanDriverEventType::channel_timed_out:
        fail(
            ResourceClientResponseStageErrorCode::driver_failed,
            ResourceClientResponseStageState::timed_out,
            "Post-response transfer or channel timed out",
            now,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            event.type == NetchanDriverEventType::normal_transfer_timed_out
                ? std::optional{
                      NetchanDriverErrorCode::fragment_transfer_timed_out}
                : std::optional{
                      NetchanDriverErrorCode::channel_inactivity_timed_out});
        return;
    case NetchanDriverEventType::secondary_stream_pending_m3:
        fail(
            ResourceClientResponseStageErrorCode::driver_failed,
            ResourceClientResponseStageState::secondary_stream_pending,
            "Secondary netchan fragment stream remains outside this milestone",
            now,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            NetchanDriverErrorCode::secondary_stream_pending_m3);
        return;
    case NetchanDriverEventType::cancelled:
        state_ = ResourceClientResponseStageState::cancelled;
        result_.reset();
        error_.reset();
        if (can_push_events()) {
            push_event(ResourceClientResponseStageEvent{
                .type = ResourceClientResponseStageEventType::cancelled,
                .occurred_at = now,
            });
        }
        cleanup(now);
        emit_trace(ResourceClientResponseTraceClassification::stage_cancelled);
        return;
    case NetchanDriverEventType::network_error:
    case NetchanDriverEventType::protocol_error:
        fail_from_driver(now);
        return;
    }
}

void ResourceClientResponseStage::handle_response_acknowledgement(
    const NetchanDriverEvent& event,
    const ResourceClientResponseStageTimePoint now)
{
    auto* const driver = resource_list_stage_.retained_driver();
    if (!driver || !response_transmitted_ || response_acknowledged_ ||
        !first_transmit_sequence_ || !most_recent_transmit_sequence_ ||
        !event.acknowledgement || !event.completed_reliable_generation) {
        fail(
            ResourceClientResponseStageErrorCode::
                response_acknowledgement_invalid,
            ResourceClientResponseStageState::protocol_error,
            "Unexpected or metadata-free reliable acknowledgement",
            now);
        return;
    }
    const auto& observation = *event.acknowledgement;
    const auto comparison = compare_sequences(
        observation.sequence,
        *most_recent_transmit_sequence_);
    const auto& session = driver->session();
    if (observation.disposition !=
            NetchanAcknowledgementDisposition::advanced ||
        *event.completed_reliable_generation != reliable_generation_ ||
        observation.reliable != response_reliable_toggle_ ||
        (comparison != NetchanSequenceComparison::equal &&
         comparison != NetchanSequenceComparison::newer) ||
        !session.pending_reliable_payload().empty() ||
        session.in_flight_reliable_payload() ||
        session.outgoing_fragment_transfer()) {
        fail(
            ResourceClientResponseStageErrorCode::
                response_acknowledgement_invalid,
            ResourceClientResponseStageState::protocol_error,
            "Reliable ACK did not exactly cover and release the response generation",
            now);
        return;
    }
    if (!can_push_events()) {
        fail(
            ResourceClientResponseStageErrorCode::event_backpressure,
            ResourceClientResponseStageState::backpressure,
            "No bounded event slot remains for response acknowledgement",
            now);
        return;
    }

    acknowledgement_ = observation;
    response_acknowledged_ = true;
    response_acknowledgement_started_at_.reset();
    post_ack_boundary_started_at_ = now;
    state_ = ResourceClientResponseStageState::
        waiting_for_server_continuation;
    push_event(ResourceClientResponseStageEvent{
        .type = ResourceClientResponseStageEventType::
            resource_response_acknowledged,
        .semantic_byte_count = response_encoding_
            ? response_encoding_->semantic_bytes().size()
            : kOpcode5ResourceResponseSemanticSize,
        .opcode = kOpcode5ResourceResponseOpcode,
        .reliable_generation = reliable_generation_,
        .transmit_sequence = most_recent_transmit_sequence_->value(),
        .acknowledgement_sequence = observation.sequence.value(),
        .transmit_count = response_transmit_count_,
        .reliable = observation.reliable,
        .fragmented = true,
        .occurred_at = now,
    });
    emit_trace(
        ResourceClientResponseTraceClassification::
            resource_response_acknowledged,
        0U,
        0U,
        kOpcode5ResourceResponseOpcode,
        most_recent_transmit_sequence_->value(),
        observation.sequence.value());

    if (pre_ack_payload_) {
        pending_decode_payload_.emplace(std::move(*pre_ack_payload_));
        pre_ack_payload_.reset();
        state_ = ResourceClientResponseStageState::
            decoding_server_continuation;
    }
}

void ResourceClientResponseStage::handle_server_payload(
    OwnedNetchanPayload payload,
    const ResourceClientResponseStageTimePoint now)
{
    // Header-only ACK packets advance transport state but are not the first
    // following service payload.
    if (payload.bytes.empty()) {
        return;
    }
    if (!server_payloads_admissible_) {
        fail(
            ResourceClientResponseStageErrorCode::
                server_payload_before_response_transmit,
            ResourceClientResponseStageState::protocol_error,
            "Server continuation arrived before a completed response transmit update",
            now);
        return;
    }
    if (response_queue_count_ != 1U) {
        fail(
            ResourceClientResponseStageErrorCode::
                post_response_payload_overflow,
            ResourceClientResponseStageState::protocol_error,
            "Server payload arrived before the semantic response was queued",
            now);
        return;
    }
    if (!first_transmit_sequence_) {
        fail(
            ResourceClientResponseStageErrorCode::
                server_payload_before_response_transmit,
            ResourceClientResponseStageState::protocol_error,
            "Server continuation has no first response transmission anchor",
            now);
        return;
    }
    const auto source_ack_comparison = compare_sequences(
        payload.source_acknowledgement,
        *first_transmit_sequence_);
    if (source_ack_comparison != NetchanSequenceComparison::equal &&
        source_ack_comparison != NetchanSequenceComparison::newer) {
        fail(
            ResourceClientResponseStageErrorCode::
                server_payload_acknowledgement_invalid,
            ResourceClientResponseStageState::protocol_error,
            "Server continuation numeric ACK predates the first response transmission",
            now);
        return;
    }
    if (!can_push_events()) {
        fail(
            ResourceClientResponseStageErrorCode::event_backpressure,
            ResourceClientResponseStageState::backpressure,
            "No bounded event slot remains for server continuation",
            now);
        return;
    }

    const auto payload_size = payload.bytes.size();
    const auto payload_reliable = payload.sequence_flags.reliable;
    const auto payload_fragmented = payload.sequence_flags.fragmented;
    push_event(ResourceClientResponseStageEvent{
        .type = ResourceClientResponseStageEventType::
            server_continuation_received,
        .payload_byte_count = payload_size,
        .reliable = payload_reliable,
        .fragmented = payload_fragmented,
        .occurred_at = now,
    });
    emit_trace(
        ResourceClientResponseTraceClassification::
            server_continuation_received,
        payload_size);

    if (!response_acknowledged_) {
        if (pre_ack_payload_ ||
            config_.response.maximum_pre_ack_server_payloads != 1U) {
            fail(
                ResourceClientResponseStageErrorCode::
                    pre_ack_server_payload_overflow,
                ResourceClientResponseStageState::protocol_error,
                "More than one complete server payload arrived before response ACK",
                now);
            return;
        }
        pre_ack_payload_.emplace(std::move(payload));
        return;
    }
    if (pending_decode_payload_) {
        fail(
            ResourceClientResponseStageErrorCode::
                post_response_payload_overflow,
            ResourceClientResponseStageState::backpressure,
            "Another server payload arrived before bounded boundary decode",
            now);
        return;
    }
    pending_decode_payload_.emplace(std::move(payload));
    state_ = ResourceClientResponseStageState::
        decoding_server_continuation;
}

void ResourceClientResponseStage::decode_pending_server_payload(
    const ResourceClientResponseStageTimePoint now)
{
    if (!pending_decode_payload_ || terminal_state(state_)) {
        return;
    }

    std::optional<ServicePayloadEnvelopeDecodeResult> decoded;
    try {
        const ServicePayloadEnvelopeDecoder decoder{
            ServicePayloadEnvelopeLimits{
                config_.response.maximum_post_response_payload_size}};
        decoded.emplace(decoder.decode(std::move(*pending_decode_payload_)));
    } catch (...) {
        pending_decode_payload_.reset();
        fail(
            ResourceClientResponseStageErrorCode::
                post_response_envelope_decode_failed,
            ResourceClientResponseStageState::protocol_error,
            "Bounded post-response service envelope decoder threw",
            now);
        return;
    }
    pending_decode_payload_.reset();
    if (!*decoded || !decoded->envelope) {
        fail(
            ResourceClientResponseStageErrorCode::
                post_response_envelope_decode_failed,
            ResourceClientResponseStageState::protocol_error,
            decoded->error
                ? std::string_view{decoded->error->context}
                : std::string_view{
                      "Post-response service envelope returned no payload"},
            now,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            decoded->error ? std::optional{decoded->error->code}
                           : std::nullopt);
        return;
    }

    auto& envelope = *decoded->envelope;
    if (envelope.decompressed_byte_count >
            config_.response.maximum_post_response_payload_size ||
        envelope.payload.bytes.size() != envelope.decompressed_byte_count) {
        fail(
            ResourceClientResponseStageErrorCode::
                post_response_envelope_decode_failed,
            ResourceClientResponseStageState::protocol_error,
            "Post-response payload exceeds or contradicts its bounded metadata",
            now);
        return;
    }

    const auto source_metadata = PostResourceResponseSourcePayloadMetadata{
        envelope.payload.direction,
        envelope.payload.source_sequence,
        envelope.payload.source_reliable,
        envelope.payload.reassembled,
        envelope.payload.decompressed,
        envelope.payload.bytes.size(),
    };
    std::optional<PostResourceResponseBoundaryParseResult> parsed;
    try {
        const PostResourceResponseBoundaryParser parser{config_.response};
        parsed.emplace(parser.parse(
            envelope.payload.bytes,
            source_metadata));
    } catch (...) {
        fail(
            ResourceClientResponseStageErrorCode::
                post_response_boundary_decode_failed,
            ResourceClientResponseStageState::protocol_error,
            "Strict post-response boundary parser threw",
            now);
        return;
    }
    if (!*parsed || !parsed->boundary) {
        fail(
            ResourceClientResponseStageErrorCode::
                post_response_boundary_decode_failed,
            ResourceClientResponseStageState::protocol_error,
            parsed->error
                ? std::string_view{parsed->error->context}
                : std::string_view{
                      "Post-response boundary parser returned no boundary"},
            now,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            parsed->error ? std::optional{parsed->error->code}
                          : std::nullopt);
        return;
    }
    if (!resource_list_stage_.result() || !response_encoding_ ||
        !first_transmit_sequence_ || !most_recent_transmit_sequence_ ||
        !acknowledgement_ || !response_acknowledged_) {
        fail(
            ResourceClientResponseStageErrorCode::
                post_response_boundary_decode_failed,
            ResourceClientResponseStageState::protocol_error,
            "Post-response boundary lost prerequisite owning lifecycle state",
            now);
        return;
    }
    if (!can_push_events()) {
        fail(
            ResourceClientResponseStageErrorCode::event_backpressure,
            ResourceClientResponseStageState::backpressure,
            "No bounded event slot remains for the next server boundary",
            now);
        return;
    }

    const auto opcode = parsed->boundary->opcode();
    const auto remaining = parsed->boundary->remaining_byte_count();
    const auto payload_size = envelope.payload.bytes.size();
    std::optional<ResourceClientResponseSignonState> candidate;
    try {
        auto built_result = ResourceClientResponseSignonState{
            *resource_list_stage_.result(),
            response_encoding_->response(),
            std::nullopt,
            std::nullopt,
            ResourceResponseReliableLifecycle{
                reliable_generation_,
                true,
                response_fragment_count_,
                response_reliable_toggle_,
                first_transmit_sequence_->value(),
                most_recent_transmit_sequence_->value(),
                response_transmit_count_,
                *acknowledgement_},
            std::move(*parsed->boundary)};
        candidate.emplace(std::move(built_result));
    } catch (...) {
        fail(
            ResourceClientResponseStageErrorCode::
                post_response_boundary_decode_failed,
            ResourceClientResponseStageState::protocol_error,
            "Unable to publish bounded owning post-response state",
            now);
        return;
    }

    result_.emplace(std::move(*candidate));
    state_ = ResourceClientResponseStageState::next_server_boundary_reached;
    push_event(ResourceClientResponseStageEvent{
        .type = ResourceClientResponseStageEventType::
            next_server_boundary_reached,
        .payload_byte_count = payload_size,
        .remaining_byte_count = remaining,
        .opcode = opcode,
        .reliable_generation = reliable_generation_,
        .transmit_sequence = most_recent_transmit_sequence_->value(),
        .acknowledgement_sequence = acknowledgement_->sequence.value(),
        .transmit_count = response_transmit_count_,
        .reliable = envelope.payload.source_reliable,
        .fragmented = envelope.payload.reassembled,
        .occurred_at = now,
    });
    if (!retain_connection_at_boundary_) {
        cleanup(now);
    }
    emit_trace(
        ResourceClientResponseTraceClassification::
            next_server_boundary_reached,
        payload_size,
        remaining,
        opcode,
        most_recent_transmit_sequence_->value(),
        acknowledgement_->sequence.value());
}

void ResourceClientResponseStage::fail_from_resource_list(
    const ResourceClientResponseStageTimePoint now) noexcept
{
    const auto& nested_error = resource_list_stage_.error();
    auto mapped_state = ResourceClientResponseStageState::protocol_error;
    switch (resource_list_stage_.state()) {
    case ResourceListStageState::timed_out:
        mapped_state = ResourceClientResponseStageState::timed_out;
        break;
    case ResourceListStageState::cancelled:
        mapped_state = ResourceClientResponseStageState::cancelled;
        break;
    case ResourceListStageState::unsupported_resource_profile:
        mapped_state =
            ResourceClientResponseStageState::unsupported_response_profile;
        break;
    case ResourceListStageState::backpressure:
        mapped_state = ResourceClientResponseStageState::backpressure;
        break;
    case ResourceListStageState::secondary_stream_pending:
        mapped_state =
            ResourceClientResponseStageState::secondary_stream_pending;
        break;
    case ResourceListStageState::network_error:
        mapped_state = ResourceClientResponseStageState::network_error;
        break;
    case ResourceListStageState::idle:
    case ResourceListStageState::waiting_for_transition_state:
    case ResourceListStageState::decoding_resource_list:
    case ResourceListStageState::resource_list_ready:
    case ResourceListStageState::decoding_post_list_messages:
    case ResourceListStageState::post_list_boundary_reached:
    case ResourceListStageState::client_response_required:
    case ResourceListStageState::protocol_error:
        mapped_state = ResourceClientResponseStageState::protocol_error;
        break;
    }
    fail(
        ResourceClientResponseStageErrorCode::resource_list_stage_failed,
        mapped_state,
        nested_error
            ? std::string_view{nested_error->context}
            : std::string_view{"Nested resource-list stage terminated"},
        now,
        nested_error ? std::optional{nested_error->code} : std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        nested_error ? nested_error->driver_code : std::nullopt);
}

void ResourceClientResponseStage::fail_from_driver(
    const ResourceClientResponseStageTimePoint now) noexcept
{
    auto* const driver = resource_list_stage_.retained_driver();
    const std::optional<NetchanDriverError>* nested_error =
        driver != nullptr ? &driver->last_error() : nullptr;
    const auto code = nested_error && *nested_error
        ? (*nested_error)->code
        : NetchanDriverErrorCode::not_active;
    auto mapped_state = ResourceClientResponseStageState::protocol_error;
    if (driver_network_error(code)) {
        mapped_state = ResourceClientResponseStageState::network_error;
    } else if (code == NetchanDriverErrorCode::channel_inactivity_timed_out ||
               code == NetchanDriverErrorCode::fragment_transfer_timed_out) {
        mapped_state = ResourceClientResponseStageState::timed_out;
    } else if (code == NetchanDriverErrorCode::event_backpressure) {
        mapped_state = ResourceClientResponseStageState::backpressure;
    } else if (code ==
               NetchanDriverErrorCode::secondary_stream_pending_m3) {
        mapped_state =
            ResourceClientResponseStageState::secondary_stream_pending;
    }
    fail(
        ResourceClientResponseStageErrorCode::driver_failed,
        mapped_state,
        nested_error && *nested_error
            ? std::string_view{(*nested_error)->context}
            : std::string_view{"Persistent driver terminated unexpectedly"},
        now,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        code);
}

void ResourceClientResponseStage::fail(
    const ResourceClientResponseStageErrorCode code,
    const ResourceClientResponseStageState state,
    const std::string_view context,
    const ResourceClientResponseStageTimePoint now,
    const std::optional<ResourceListStageErrorCode> resource_list_code,
    const std::optional<
        resource_consistency::ResourceConsistencyErrorCode>
        consistency_code,
    const std::optional<Opcode5ResourceResponseErrorCode> response_code,
    const std::optional<ServicePayloadEnvelopeErrorCode> envelope_code,
    const std::optional<PostResourceResponseBoundaryErrorCode> boundary_code,
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
        error_->resource_list_code = resource_list_code;
        error_->consistency_code = consistency_code;
        error_->response_code = response_code;
        error_->envelope_code = envelope_code;
        error_->boundary_code = boundary_code;
        error_->driver_code = driver_code;
        const auto bounded = context.substr(
            0U,
            (std::min)(
                context.size(),
                kResourceClientResponseStageDiagnosticTextLimit));
        error_->context.assign(bounded.data(), bounded.size());
    } catch (...) {
    }
    if (can_push_events()) {
        push_event(ResourceClientResponseStageEvent{
            .type = terminal_event(state_),
            .occurred_at = now,
        });
    }
    cleanup(now);
    emit_trace(terminal_trace(state_));
}

void ResourceClientResponseStage::cleanup(
    const ResourceClientResponseStageTimePoint now) noexcept
{
    if (cleanup_done_) {
        return;
    }
    cleanup_done_ = true;
    if (consistency_operation_) {
        consistency_operation_->cancel();
        consistency_operation_.reset();
    }
    consistency_provider_started_at_.reset();
    response_acknowledgement_started_at_.reset();
    post_ack_boundary_started_at_.reset();
    consistency_session_.reset();
    pre_ack_payload_.reset();
    pending_decode_payload_.reset();
    resource_list_stage_.finalize_retained_boundary(now);
}

void ResourceClientResponseStage::emit_trace(
    const ResourceClientResponseTraceClassification classification,
    const std::size_t payload_byte_count,
    const std::size_t remaining_byte_count,
    const std::optional<std::uint8_t> opcode,
    const std::optional<std::uint32_t> transmit_sequence,
    const std::optional<std::uint32_t> acknowledgement_sequence) noexcept
{
    if (!trace_callback_ || trace_callback_active_) {
        return;
    }
    ResourceClientResponseTraceEvent event;
    event.classification = classification;
    event.state = state_;
    event.endpoint = remote_endpoint();
    event.semantic_byte_count = response_encoding_
        ? response_encoding_->semantic_bytes().size()
        : 0U;
    event.payload_byte_count = payload_byte_count;
    event.remaining_byte_count = remaining_byte_count;
    event.opcode = opcode;
    event.reliable_generation = response_transmitted_
        ? std::optional{reliable_generation_}
        : std::nullopt;
    event.transmit_sequence = transmit_sequence;
    event.acknowledgement_sequence = acknowledgement_sequence;
    event.transmit_count = response_transmit_count_;
    event.reliable = response_transmitted_;
    event.fragmented = response_transmitted_ && response_fragment_count_ != 0U;
    event.transmitted_packet_count = transmitted_packet_count();
    trace_callback_active_ = true;
    try {
        trace_callback_(event);
    } catch (...) {
    }
    trace_callback_active_ = false;
}

static_assert(
    std::is_nothrow_move_constructible_v<ResourceClientResponseStageEvent>);
static_assert(
    std::is_nothrow_move_constructible_v<ResourceClientResponseSignonState>);

} // namespace hlclient::goldsrc
