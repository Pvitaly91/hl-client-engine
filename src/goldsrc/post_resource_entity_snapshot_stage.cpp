#include <hlclient/goldsrc/post_resource_entity_snapshot_stage.hpp>
#include <hlclient/goldsrc/service_payload_envelope.hpp>

#include <algorithm>
#include <array>
#include <ranges>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool terminal_state(
    const PostResourceEntitySnapshotStageState state) noexcept
{
    switch (state) {
    case PostResourceEntitySnapshotStageState::baseline_registry_ready:
    case PostResourceEntitySnapshotStageState::full_snapshot_ready:
    case PostResourceEntitySnapshotStageState::entity_snapshot_ready:
    case PostResourceEntitySnapshotStageState::unsupported_message:
    case PostResourceEntitySnapshotStageState::missing_delta_base:
    case PostResourceEntitySnapshotStageState::timed_out:
    case PostResourceEntitySnapshotStageState::cancelled:
    case PostResourceEntitySnapshotStageState::backpressure:
    case PostResourceEntitySnapshotStageState::secondary_stream_pending:
    case PostResourceEntitySnapshotStageState::network_error:
    case PostResourceEntitySnapshotStageState::protocol_error:
        return true;
    case PostResourceEntitySnapshotStageState::idle:
    case PostResourceEntitySnapshotStageState::waiting_for_resource_response:
    case PostResourceEntitySnapshotStageState::decoding_post_resource_messages:
    case PostResourceEntitySnapshotStageState::client_request_ready:
    case PostResourceEntitySnapshotStageState::
        waiting_for_client_request_transmit:
    case PostResourceEntitySnapshotStageState::
        waiting_for_client_request_ack:
    case PostResourceEntitySnapshotStageState::waiting_for_server_signon:
    case PostResourceEntitySnapshotStageState::decoding_baselines:
    case PostResourceEntitySnapshotStageState::waiting_for_full_snapshot:
    case PostResourceEntitySnapshotStageState::waiting_for_delta_snapshot:
        return false;
    }
    return true;
}

[[nodiscard]] PostResourceEntitySnapshotStageEventType terminal_event(
    const PostResourceEntitySnapshotStageState state) noexcept
{
    switch (state) {
    case PostResourceEntitySnapshotStageState::unsupported_message:
        return PostResourceEntitySnapshotStageEventType::unsupported_message;
    case PostResourceEntitySnapshotStageState::missing_delta_base:
        return PostResourceEntitySnapshotStageEventType::missing_delta_base;
    case PostResourceEntitySnapshotStageState::timed_out:
        return PostResourceEntitySnapshotStageEventType::timeout;
    case PostResourceEntitySnapshotStageState::cancelled:
        return PostResourceEntitySnapshotStageEventType::cancelled;
    case PostResourceEntitySnapshotStageState::backpressure:
        return PostResourceEntitySnapshotStageEventType::backpressure;
    case PostResourceEntitySnapshotStageState::secondary_stream_pending:
        return PostResourceEntitySnapshotStageEventType::
            secondary_stream_pending;
    case PostResourceEntitySnapshotStageState::network_error:
        return PostResourceEntitySnapshotStageEventType::network_error;
    default:
        return PostResourceEntitySnapshotStageEventType::protocol_error;
    }
}

[[nodiscard]] const DeltaSchemaRegistryState& delta_registry_from(
    const ResourceClientResponseSignonState& response) noexcept
{
    return response.resource_list()
        .transition()
        .user_info()
        .movement_environment()
        .delta_description()
        .registry();
}

[[nodiscard]] bool valid_stop_condition(
    const EntitySnapshotStageStopCondition stop_condition) noexcept
{
    switch (stop_condition) {
    case EntitySnapshotStageStopCondition::server_baselines:
    case EntitySnapshotStageStopCondition::first_full_snapshot:
    case EntitySnapshotStageStopCondition::first_applied_delta:
        return true;
    }
    return false;
}

[[nodiscard]] std::optional<std::vector<DeltaScalarValue>>
synthetic_object_values(
    const DeltaSchema& schema,
    const bool changed)
{
    std::vector<DeltaScalarValue> values;
    values.reserve(schema.field_count());
    for (const auto& field : schema.fields()) {
        switch (field.type_flags().base_type()) {
        case DeltaFieldBaseType::byte_value:
        case DeltaFieldBaseType::short_value:
        case DeltaFieldBaseType::integer_value:
            if (field.type_flags().signed_value()) {
                const auto value = changed && field.significant_bits() >= 2U
                    ? std::int32_t{1}
                    : std::int32_t{0};
                values.emplace_back(value);
            } else {
                values.emplace_back(
                    changed ? std::uint32_t{1U} : std::uint32_t{0U});
            }
            break;
        case DeltaFieldBaseType::float_value:
        case DeltaFieldBaseType::angle:
            values.emplace_back(changed ? 1.0 : 0.0);
            break;
        case DeltaFieldBaseType::string:
            values.emplace_back(changed ? std::string{"neutral-v1"}
                                        : std::string{});
            break;
        case DeltaFieldBaseType::time_window_8:
        case DeltaFieldBaseType::time_window_big:
            return std::nullopt;
        }
    }
    return values;
}

} // namespace

bool valid_post_resource_entity_snapshot_stage_configuration(
    const PostResourceEntitySnapshotStageConfig& config) noexcept
{
    return valid_resource_client_response_stage_configuration(
               config.resource_response) &&
           valid_post_resource_signon_limits(config.post_resource) &&
           valid_post_resource_signon_profile(config.profile) &&
           valid_entity_snapshot_limits(config.entity_snapshots) &&
           valid_stop_condition(config.stop_condition) &&
           (config.stop_condition !=
                EntitySnapshotStageStopCondition::first_applied_delta ||
            config.entity_snapshots.maximum_snapshot_history >= 2U) &&
           config.maximum_stage_events != 0U &&
           config.maximum_stage_events <= kMaximumPostResourceStageEvents &&
           config.maximum_driver_events_per_update != 0U &&
           config.maximum_driver_events_per_update <=
               kMaximumPostResourceDriverEventsPerUpdate &&
           config.timeout > std::chrono::milliseconds::zero() &&
           config.timeout <= kMaximumPostResourceSignonTimeout;
}

PostResourceSignonState::PostResourceSignonState(
    ResourceClientResponseSignonState resource_response,
    DeltaSchemaRegistryState delta_registry,
    PostResourceSignonBoundaryState boundary_state,
    const PostResourceSignonCompatibilityProfile profile,
    std::optional<EntityBaselineRegistryState> baseline_registry,
    std::optional<EntitySnapshotHistoryState> snapshot_history) noexcept
    : resource_response_{std::move(resource_response)},
      delta_registry_{std::move(delta_registry)},
      boundary_state_{std::move(boundary_state)},
      baseline_registry_{std::move(baseline_registry)},
      snapshot_history_{std::move(snapshot_history)},
      profile_{profile}
{
}

const ResourceClientResponseSignonState&
PostResourceSignonState::resource_response() const noexcept
{
    return resource_response_;
}

const DeltaSchemaRegistryState&
PostResourceSignonState::delta_registry() const noexcept
{
    return delta_registry_;
}

const PostResourceSignonBoundaryState&
PostResourceSignonState::boundary_state() const noexcept
{
    return boundary_state_;
}

const std::optional<EntityBaselineRegistryState>&
PostResourceSignonState::baseline_registry() const noexcept
{
    return baseline_registry_;
}

const std::optional<EntitySnapshotHistoryState>&
PostResourceSignonState::snapshot_history() const noexcept
{
    return snapshot_history_;
}

const EntitySnapshotState*
PostResourceSignonState::latest_snapshot() const noexcept
{
    if (!snapshot_history_ || snapshot_history_->snapshots().empty()) {
        return nullptr;
    }
    return &snapshot_history_->snapshots().back();
}

PostResourceSignonCompatibilityProfile
PostResourceSignonState::profile() const noexcept
{
    return profile_;
}

std::optional<PostResourceSignonBoundaryState>
PostResourceEntitySnapshotStage::aggregate_transcript(
    const PostResourceSignonBoundaryState& latest,
    const std::span<const PostResourceMessageMetadata> server_messages,
    const std::span<const PostResourceClientRequestMetadata> client_requests)
{
    return latest.with_transcript(server_messages, client_requests);
}

std::optional<PostResourceSignonBoundaryState>
PostResourceEntitySnapshotStage::complete_synthetic_sequence(
    const PostResourceSignonBoundaryState& latest)
{
    return latest.with_completed_synthetic_sequence();
}

std::optional<PostResourceSignonBoundaryState>
PostResourceEntitySnapshotStage::apply_synthetic_publication(
    const PostResourceSignonBoundaryState& latest,
    const PostResourceSignonProgress published_progress)
{
    return latest.with_applied_synthetic_publication(published_progress);
}

class PostResourceEntitySnapshotStage::Implementation final {
public:
    Implementation(
        network::IDatagramTransport& transport,
        const network::NetworkAddress remote_endpoint,
        PostResourceEntitySnapshotStageConfig config,
        resource_consistency::IResourceConsistencyProvider*
            consistency_provider,
        PostResourceEntitySnapshotTraceCallback trace_callback)
        : config_{std::move(config)},
          trace_callback_{std::move(trace_callback)},
          configuration_valid_{
              valid_post_resource_entity_snapshot_stage_configuration(
                  config_)},
          response_stage_{
              transport,
              remote_endpoint,
              config_.resource_response,
              consistency_provider,
              {}, {}, {}, {}, {}, {}, {}, {},
              ResourceClientResponseStage::
                  RetainPostResourcePayloadAtBoundary{}},
          event_slots_(
              configuration_valid_ ? config_.maximum_stage_events : 0U)
    {
    }

    ~Implementation()
    {
        if (state_ != PostResourceEntitySnapshotStageState::idle &&
            !terminal_state(state_)) {
            cancel(last_update_.value_or(
                PostResourceEntitySnapshotStageTimePoint{}));
        } else if (!cleanup_done_ && result_) {
            cleanup(last_update_.value_or(
                PostResourceEntitySnapshotStageTimePoint{}));
        }
    }

    [[nodiscard]] bool start(
        const PostResourceEntitySnapshotStageTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime)
    {
        if (trace_callback_active_ ||
            state_ != PostResourceEntitySnapshotStageState::idle) {
            return false;
        }
        error_.reset();
        if (!configuration_valid_) {
            set_start_error(
                PostResourceEntitySnapshotStageErrorCode::invalid_configuration,
                "Post-resource stage limits are outside project hard caps");
            state_ = PostResourceEntitySnapshotStageState::protocol_error;
            return false;
        }
        bool started = false;
        try {
            started = response_stage_.start(
                now, expected_local_endpoint, std::move(connection_lifetime));
        } catch (...) {
            try { response_stage_.cancel(now); } catch (...) {}
            set_start_error(
                PostResourceEntitySnapshotStageErrorCode::
                    response_stage_start_failed,
                "Nested resource-response stage threw during start");
            state_ = PostResourceEntitySnapshotStageState::protocol_error;
            return false;
        }
        if (!started) {
            fail_from_response(now, true);
            return false;
        }
        last_update_ = now;
        state_ =
            PostResourceEntitySnapshotStageState::waiting_for_resource_response;
        return true;
    }

    void update(const PostResourceEntitySnapshotStageTimePoint now)
    {
        if (state_ == PostResourceEntitySnapshotStageState::idle ||
            terminal_state(state_) || trace_callback_active_) {
            return;
        }
        if (last_update_ && now < *last_update_) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::time_moved_backwards,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Post-resource stage time moved backwards",
                now);
            return;
        }
        last_update_ = now;

        if (state_ == PostResourceEntitySnapshotStageState::
                          waiting_for_resource_response) {
            try {
                response_stage_.update(now);
            } catch (...) {
                fail(
                    PostResourceEntitySnapshotStageErrorCode::
                        response_stage_failed,
                    PostResourceEntitySnapshotStageState::protocol_error,
                    "Nested resource-response stage threw during update",
                    now);
                return;
            }
            try {
                synchronize_from_response(now);
            } catch (...) {
                fail(
                    PostResourceEntitySnapshotStageErrorCode::
                        response_stage_failed,
                    PostResourceEntitySnapshotStageState::protocol_error,
                    "Post-resource boundary synchronization threw",
                    now);
            }
            return;
        }

        if (post_resource_started_at_ &&
            now - *post_resource_started_at_ >= config_.timeout) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::stage_timed_out,
                PostResourceEntitySnapshotStageState::timed_out,
                "Post-resource sign-on did not reach its configured stop",
                now);
            return;
        }

        auto* const driver = response_stage_.retained_driver();
        if (driver == nullptr) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::
                    retained_driver_missing,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Post-resource continuation lost its retained driver",
                now);
            return;
        }
        try {
            driver->update(now);
        } catch (...) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::driver_failed,
                PostResourceEntitySnapshotStageState::network_error,
                "Retained driver threw during post-resource update",
                now);
            return;
        }
        observe_request_transmit(now);
        std::size_t processed = 0U;
        try {
            while (processed < config_.maximum_driver_events_per_update &&
                   !terminal_state(state_)) {
                auto event = driver->poll_event();
                if (!event) {
                    break;
                }
                ++processed;
                handle_driver_event(std::move(*event), now);
            }
        } catch (...) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::driver_failed,
                PostResourceEntitySnapshotStageState::network_error,
                "Retained driver threw while publishing an event",
                now);
            return;
        }
        if (!terminal_state(state_) &&
            driver->pending_event_count() != 0U &&
            processed == config_.maximum_driver_events_per_update) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::event_backpressure,
                PostResourceEntitySnapshotStageState::backpressure,
                "Driver event budget was exhausted during one update",
                now);
        }
    }

    void cancel(const PostResourceEntitySnapshotStageTimePoint now)
    {
        if (state_ == PostResourceEntitySnapshotStageState::idle ||
            terminal_state(state_) || trace_callback_active_) {
            return;
        }
        if (state_ == PostResourceEntitySnapshotStageState::
                          waiting_for_resource_response) {
            try { response_stage_.cancel(now); } catch (...) {}
        } else if (auto* const driver = response_stage_.retained_driver();
                   driver != nullptr && !driver->terminal()) {
            try { driver->cancel(now); } catch (...) {}
        }
        state_ = PostResourceEntitySnapshotStageState::cancelled;
        result_.reset();
        error_.reset();
        const auto event = PostResourceEntitySnapshotStageEvent{
            .type = PostResourceEntitySnapshotStageEventType::cancelled,
            .occurred_at = now};
        if (can_push_events()) {
            push_event(event);
        }
        cleanup(now);
        emit_trace(event);
    }

    [[nodiscard]] std::optional<PostResourceEntitySnapshotStageEvent>
    poll_event()
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

    void synchronize_from_response(
        const PostResourceEntitySnapshotStageTimePoint now)
    {
        if (response_stage_.state() !=
                ResourceClientResponseStageState::
                    next_server_boundary_reached) {
            if (response_stage_.terminal() || response_stage_.error()) {
                fail_from_response(now, false);
            }
            return;
        }
        const auto* const payload = response_stage_.retained_source_payload();
        auto* const driver = response_stage_.retained_driver();
        if (!response_stage_.result() || payload == nullptr) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::
                    retained_payload_missing,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Resource response did not retain its owning source payload",
                now);
            return;
        }
        if (driver == nullptr) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::
                    retained_driver_missing,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Resource response did not retain its persistent driver",
                now);
            return;
        }
        const auto& registry = delta_registry_from(*response_stage_.result());
        if (registry.schema_count() == 0U) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::
                    delta_registry_missing,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Existing delta schema registry is empty",
                now);
            return;
        }
        state_ = PostResourceEntitySnapshotStageState::
            decoding_post_resource_messages;
        post_resource_started_at_ = now;
        std::optional<PostResourceSignonStreamDecodeResult> decoded;
        try {
            decoded.emplace(PostResourceSignonStreamDecoder{
                config_.post_resource, config_.profile}.decode(
                    *payload,
                    response_stage_.result()->boundary(),
                    registry));
        } catch (...) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::stream_decode_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Post-resource decoder threw while retaining typed state",
                now);
            return;
        }
        if (!*decoded || !decoded->state) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::stream_decode_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                decoded->error
                    ? std::string_view{decoded->error->context}
                    : std::string_view{
                          "Post-resource decoder returned no state"},
                now,
                std::nullopt,
                decoded->error ? std::optional{decoded->error->code}
                               : std::nullopt);
            return;
        }
        if (!can_push_events(
                decoded->state->unsupported_boundary() ? 2U : 3U)) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::event_backpressure,
                PostResourceEntitySnapshotStageState::backpressure,
                "Post-resource publication exceeds the event bound",
                now);
            return;
        }
        std::optional<PostResourceSignonState> candidate;
        try {
            auto built_candidate = PostResourceSignonState{
                *response_stage_.result(),
                registry,
                std::move(*decoded->state),
                config_.profile};
            candidate.emplace(std::move(built_candidate));
        } catch (...) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::stream_decode_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Unable to publish owning post-resource sign-on state",
                now);
            return;
        }
        const auto& boundary_state = candidate->boundary_state();
        if (boundary_state.transcript().server_messages().empty()) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::stream_decode_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Post-resource decoder published no server message metadata",
                now);
            return;
        }
        const auto& message =
            boundary_state.transcript().server_messages().front();
        const auto received_event = PostResourceEntitySnapshotStageEvent{
            .type = PostResourceEntitySnapshotStageEventType::
                post_resource_message_received,
            .opcode = message.opcode,
            .byte_offset = message.byte_start,
            .bit_offset = message.bit_start,
            .occurred_at = now};
        result_.emplace(std::move(*candidate));
        latest_boundary_state_.emplace(result_->boundary_state());
        transcript_server_messages_ =
            result_->boundary_state().transcript().server_messages();
        transcript_client_requests_ =
            result_->boundary_state().transcript().client_requests();
        response_stage_.release_retained_source_payload();
        push_event(received_event);
        emit_trace(received_event);

        if (result_->boundary_state().unsupported_boundary()) {
            state_ = PostResourceEntitySnapshotStageState::unsupported_message;
            PostResourceEntitySnapshotStageError typed_error;
            typed_error.code =
                PostResourceEntitySnapshotStageErrorCode::unsupported_message;
            typed_error.context =
                "Stock post-resource message grammar requires accepted evidence";
            error_.emplace(std::move(typed_error));
            const auto unsupported_event =
                PostResourceEntitySnapshotStageEvent{
                    .type = PostResourceEntitySnapshotStageEventType::
                        unsupported_message,
                    .opcode = message.opcode,
                    .byte_offset = message.byte_start,
                    .bit_offset = message.bit_start,
                    .occurred_at = now};
            push_event(unsupported_event);
            cleanup(now);
            emit_trace(unsupported_event);
            return;
        }

        std::optional<PostResourceClientRequestBuildResult> built;
        try {
            built.emplace(PostResourceClientRequestBuilder{
                config_.post_resource, config_.profile}.build_first());
        } catch (...) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::request_build_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Typed post-resource request builder threw",
                now);
            return;
        }
        if (!*built || !built->encoding) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::request_build_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                built->error
                    ? std::string_view{built->error->context}
                    : std::string_view{
                          "Typed post-resource request builder returned no bytes"},
                now,
                std::nullopt,
                std::nullopt,
                built->error ? std::optional{built->error->code}
                             : std::nullopt);
            return;
        }
        request_.emplace(std::move(*built->encoding));
        state_ = PostResourceEntitySnapshotStageState::client_request_ready;
        const auto ready_event = PostResourceEntitySnapshotStageEvent{
            .type = PostResourceEntitySnapshotStageEventType::
                client_signon_request_ready,
            .semantic_byte_count = request_->semantic_bytes().size(),
            .occurred_at = now};
        push_event(ready_event);
        emit_trace(ready_event);
        std::optional<NetchanDriverOperationResult> queued;
        try {
            queued.emplace(
                driver->queue_reliable(request_->semantic_bytes()));
        } catch (...) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::request_queue_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Persistent driver threw while queueing the typed request",
                now);
            return;
        }
        if (!*queued) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::request_queue_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                queued->error
                    ? std::string_view{queued->error->context}
                    : std::string_view{
                          "Persistent driver rejected the typed request"},
                now,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                queued->error ? std::optional{queued->error->code}
                              : std::nullopt);
            return;
        }
        ++request_queue_count_;
        state_ = PostResourceEntitySnapshotStageState::
            waiting_for_client_request_transmit;
        const auto queued_event = PostResourceEntitySnapshotStageEvent{
            .type = PostResourceEntitySnapshotStageEventType::
                client_signon_request_queued,
            .semantic_byte_count = request_->semantic_bytes().size(),
            .occurred_at = now};
        push_event(queued_event);
        emit_trace(queued_event);
    }

    void observe_request_transmit(
        const PostResourceEntitySnapshotStageTimePoint now)
    {
        if (request_transmitted_ || !request_ ||
            state_ != PostResourceEntitySnapshotStageState::
                          waiting_for_client_request_transmit) {
            return;
        }
        auto* const driver = response_stage_.retained_driver();
        if (driver == nullptr) {
            return;
        }
        const auto& in_flight =
            driver->session().in_flight_reliable_payload();
        if (!in_flight) {
            return;
        }
        if (!std::ranges::equal(
                in_flight->bytes, request_->semantic_bytes())) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::
                    request_transmit_mismatch,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Unexpected reliable payload became in-flight",
                now);
            return;
        }
        request_transmitted_ = true;
        state_ = PostResourceEntitySnapshotStageState::
            waiting_for_client_request_ack;
    }

    [[nodiscard]] bool publish_result_state(
        const PostResourceEntitySnapshotStageTimePoint now)
    {
        if (!result_ || !latest_boundary_state_) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::
                    entity_publication_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Post-resource publication lost its immutable prerequisites",
                now);
            return false;
        }
        try {
            auto candidate = PostResourceSignonState{
                result_->resource_response(),
                result_->delta_registry(),
                *latest_boundary_state_,
                config_.profile,
                baseline_registry_,
                snapshot_history_};
            result_.emplace(std::move(candidate));
        } catch (...) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::
                    entity_publication_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Unable to publish owning post-resource entity state",
                now);
            return false;
        }
        return true;
    }

    void publish_synthetic_baseline(
        const PostResourceEntitySnapshotStageTimePoint now)
    {
        state_ = PostResourceEntitySnapshotStageState::decoding_baselines;
        try {
            if (!result_ || result_->delta_registry().schemas().empty()) {
                fail(
                    PostResourceEntitySnapshotStageErrorCode::
                        delta_registry_missing,
                    PostResourceEntitySnapshotStageState::protocol_error,
                    "Synthetic baseline publication has no schema",
                    now);
                return;
            }
            const auto object_builder = DeltaObjectBuilder{
                {}, DeltaValueCompatibilityProfile::synthetic_neutral_v1};
            const auto* const schema =
                result_->delta_registry().find_exact("entity_state_t");
            if (schema == nullptr) {
                fail(
                    PostResourceEntitySnapshotStageErrorCode::
                        entity_publication_failed,
                    PostResourceEntitySnapshotStageState::protocol_error,
                    "Synthetic entity sequence requires exact schema entity_state_t",
                    now);
                return;
            }
            auto baseline_values = synthetic_object_values(*schema, false);
            auto changed_values = synthetic_object_values(*schema, true);
            if (!baseline_values || !changed_values) {
                fail(
                    PostResourceEntitySnapshotStageErrorCode::
                        entity_publication_failed,
                    PostResourceEntitySnapshotStageState::protocol_error,
                    "Synthetic entity_state_t contains an evidence-pending time-window field",
                    now,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    DeltaValueErrorCode::evidence_pending);
                return;
            }
            auto baseline_object = object_builder.build(
                *schema, *baseline_values);
            auto changed_object = object_builder.build(
                *schema, *changed_values);
            if (!baseline_object || !baseline_object.state ||
                !changed_object || !changed_object.state) {
                const auto code = baseline_object.error
                    ? std::optional{baseline_object.error->code}
                    : changed_object.error
                        ? std::optional{changed_object.error->code}
                        : std::nullopt;
                fail(
                    PostResourceEntitySnapshotStageErrorCode::
                        entity_publication_failed,
                    PostResourceEntitySnapshotStageState::protocol_error,
                    "Synthetic entity_state_t values could not be published",
                    now,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    code);
                return;
            }

            EntityBaselineRegistryBuilder baseline_builder{
                result_->delta_registry(),
                config_.entity_snapshots,
                EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
            for (std::uint32_t entity_number = 1U;
                 entity_number <= 3U;
                 ++entity_number) {
                const auto inserted = baseline_builder.insert(
                    EntityBaselineKey::for_entity(entity_number),
                    EntitySchemaCategory::ordinary_entity,
                    *baseline_object.state,
                    EntitySourceGeometry{1U, 3U, 0U, 24U});
                if (!inserted) {
                    fail(
                        PostResourceEntitySnapshotStageErrorCode::
                            entity_publication_failed,
                        PostResourceEntitySnapshotStageState::protocol_error,
                        inserted.error
                            ? std::string_view{inserted.error->context}
                            : std::string_view{
                                  "Synthetic baseline registry rejected its record"},
                        now,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        inserted.error
                            ? std::optional{inserted.error->code}
                            : std::nullopt);
                    return;
                }
            }
            auto published = std::move(baseline_builder).publish();
            if (!published || !published.state) {
                fail(
                    PostResourceEntitySnapshotStageErrorCode::
                        entity_publication_failed,
                    PostResourceEntitySnapshotStageState::protocol_error,
                    published.error
                        ? std::string_view{published.error->context}
                        : std::string_view{
                              "Synthetic baseline registry was not published"},
                    now,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    published.error ? std::optional{published.error->code}
                                    : std::nullopt);
                return;
            }
            changed_object_.emplace(std::move(*changed_object.state));
            baseline_registry_.emplace(std::move(*published.state));
            auto published_boundary =
                PostResourceEntitySnapshotStage::apply_synthetic_publication(
                    *latest_boundary_state_,
                    PostResourceSignonProgress::baseline_registry_ready);
            if (!published_boundary) {
                fail(
                    PostResourceEntitySnapshotStageErrorCode::
                        entity_publication_failed,
                    PostResourceEntitySnapshotStageState::protocol_error,
                    "Synthetic baseline readiness requires its ordered publication trigger",
                    now);
                return;
            }
            latest_boundary_state_.emplace(std::move(*published_boundary));
            if (!publish_result_state(now)) {
                return;
            }
        } catch (...) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::
                    entity_publication_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Synthetic baseline publication threw",
                now);
            return;
        }

        const auto decoded_event = PostResourceEntitySnapshotStageEvent{
            .type = PostResourceEntitySnapshotStageEventType::baseline_decoded,
            .baseline_count = baseline_registry_->baseline_count(),
            .occurred_at = now};
        push_event(decoded_event);
        emit_trace(decoded_event);
        const auto ready_event = PostResourceEntitySnapshotStageEvent{
            .type = PostResourceEntitySnapshotStageEventType::
                baseline_registry_ready,
            .baseline_count = baseline_registry_->baseline_count(),
            .occurred_at = now};
        state_ =
            PostResourceEntitySnapshotStageState::baseline_registry_ready;
        push_event(ready_event);
        emit_trace(ready_event);
        if (config_.stop_condition ==
            EntitySnapshotStageStopCondition::server_baselines) {
            cleanup(now);
            return;
        }
        state_ =
            PostResourceEntitySnapshotStageState::waiting_for_full_snapshot;
    }

    void publish_synthetic_full_snapshot(
        const PostResourceEntitySnapshotStageTimePoint now)
    {
        try {
            if (!baseline_registry_) {
                fail(
                    PostResourceEntitySnapshotStageErrorCode::
                        entity_publication_failed,
                    PostResourceEntitySnapshotStageState::protocol_error,
                    "Synthetic full snapshot arrived before baselines",
                    now);
                return;
            }
            const std::array entities{
                EntitySnapshotEntityInput::from_baseline(
                    1U, EntityBaselineKey::for_entity(1U)),
                EntitySnapshotEntityInput::from_baseline(
                    2U, EntityBaselineKey::for_entity(2U)),
            };
            auto built = EntityFullSnapshotBuilder{
                config_.entity_snapshots,
                EntitySnapshotCompatibilityProfile::synthetic_neutral_v1}
                             .build(
                                 EntitySnapshotReference::synthetic(1U),
                                 EntityServerTime::synthetic_raw(100),
                                 *baseline_registry_,
                                 entities,
                                 EntitySourceGeometry{2U, 3U, 0U, 24U});
            if (!built || !built.state) {
                fail(
                    PostResourceEntitySnapshotStageErrorCode::
                        entity_publication_failed,
                    PostResourceEntitySnapshotStageState::protocol_error,
                    built.error
                        ? std::string_view{built.error->context}
                        : std::string_view{
                              "Synthetic full snapshot was not published"},
                    now,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    built.error ? std::optional{built.error->code}
                                : std::nullopt);
                return;
            }
            history_builder_.emplace(
                config_.entity_snapshots,
                EntitySnapshotCompatibilityProfile::synthetic_neutral_v1);
            const auto inserted = history_builder_->insert(*built.state);
            if (!inserted) {
                fail(
                    PostResourceEntitySnapshotStageErrorCode::
                        entity_publication_failed,
                    PostResourceEntitySnapshotStageState::protocol_error,
                    inserted.error
                        ? std::string_view{inserted.error->context}
                        : std::string_view{
                              "Synthetic full snapshot history insert failed"},
                    now,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    inserted.error ? std::optional{inserted.error->code}
                                   : std::nullopt);
                return;
            }
            auto history = history_builder_->publish();
            if (!history || !history.state) {
                fail(
                    PostResourceEntitySnapshotStageErrorCode::
                        entity_publication_failed,
                    PostResourceEntitySnapshotStageState::protocol_error,
                    history.error
                        ? std::string_view{history.error->context}
                        : std::string_view{
                              "Synthetic full snapshot history was not published"},
                    now,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    history.error ? std::optional{history.error->code}
                                  : std::nullopt);
                return;
            }
            snapshot_history_.emplace(std::move(*history.state));
            auto published_boundary =
                PostResourceEntitySnapshotStage::apply_synthetic_publication(
                    *latest_boundary_state_,
                    PostResourceSignonProgress::full_snapshot_ready);
            if (!published_boundary) {
                fail(
                    PostResourceEntitySnapshotStageErrorCode::
                        entity_publication_failed,
                    PostResourceEntitySnapshotStageState::protocol_error,
                    "Synthetic full-snapshot readiness requires its ordered publication trigger",
                    now);
                return;
            }
            latest_boundary_state_.emplace(std::move(*published_boundary));
            if (!publish_result_state(now)) {
                return;
            }
        } catch (...) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::
                    entity_publication_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Synthetic full snapshot publication threw",
                now);
            return;
        }

        const auto* const snapshot = result_->latest_snapshot();
        const auto ready_event = PostResourceEntitySnapshotStageEvent{
            .type = PostResourceEntitySnapshotStageEventType::
                full_entity_snapshot_ready,
            .entity_count = snapshot ? snapshot->entity_count() : 0U,
            .added_count = snapshot
                ? snapshot->statistics().added_count
                : 0U,
            .snapshot_reference = 1U,
            .occurred_at = now};
        state_ = PostResourceEntitySnapshotStageState::full_snapshot_ready;
        push_event(ready_event);
        emit_trace(ready_event);
        const auto history_event = PostResourceEntitySnapshotStageEvent{
            .type = PostResourceEntitySnapshotStageEventType::
                snapshot_history_updated,
            .history_count = snapshot_history_->snapshot_count(),
            .snapshot_reference = 1U,
            .occurred_at = now};
        push_event(history_event);
        emit_trace(history_event);
        if (config_.stop_condition ==
            EntitySnapshotStageStopCondition::first_full_snapshot) {
            cleanup(now);
            return;
        }
        state_ =
            PostResourceEntitySnapshotStageState::waiting_for_delta_snapshot;
    }

    void publish_synthetic_delta_snapshot(
        const PostResourceEntitySnapshotStageTimePoint now)
    {
        try {
            if (!baseline_registry_ || !snapshot_history_ ||
                !history_builder_ || !changed_object_ ||
                !latest_boundary_state_) {
                fail(
                    PostResourceEntitySnapshotStageErrorCode::
                        entity_publication_failed,
                    PostResourceEntitySnapshotStageState::missing_delta_base,
                    "Synthetic delta snapshot has no retained exact base",
                    now);
                return;
            }
            const std::array updates{
                EntitySnapshotEntityInput::with_decoded_state(
                    1U,
                    EntityBaselineKey::for_entity(1U),
                    *changed_object_),
                EntitySnapshotEntityInput::from_baseline(
                    3U, EntityBaselineKey::for_entity(3U)),
            };
            constexpr std::array<std::uint32_t, 1U> removals{2U};
            auto built = EntityDeltaSnapshotBuilder{
                config_.entity_snapshots,
                EntitySnapshotCompatibilityProfile::synthetic_neutral_v1}
                             .build(
                                 EntitySnapshotReference::synthetic(2U),
                                 EntityServerTime::synthetic_raw(101),
                                 EntitySnapshotReference::synthetic(1U),
                                 *snapshot_history_,
                                 *baseline_registry_,
                                 updates,
                                 removals,
                                 EntitySourceGeometry{3U, 3U, 0U, 24U});
            if (!built || !built.state) {
                const auto missing_base =
                    built.error &&
                    (built.error->code == EntitySnapshotErrorCode::
                                              missing_delta_snapshot_base ||
                     built.error->code == EntitySnapshotErrorCode::
                                              evicted_delta_snapshot_base ||
                     built.error->code == EntitySnapshotErrorCode::
                                              future_delta_snapshot_base ||
                     built.error->code == EntitySnapshotErrorCode::
                                              wrong_delta_snapshot_base);
                fail(
                    PostResourceEntitySnapshotStageErrorCode::
                        entity_publication_failed,
                    missing_base
                        ? PostResourceEntitySnapshotStageState::
                              missing_delta_base
                        : PostResourceEntitySnapshotStageState::protocol_error,
                    built.error
                        ? std::string_view{built.error->context}
                        : std::string_view{
                              "Synthetic delta snapshot was not published"},
                    now,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    built.error ? std::optional{built.error->code}
                                : std::nullopt);
                return;
            }
            const auto inserted = history_builder_->insert(*built.state);
            if (!inserted) {
                fail(
                    PostResourceEntitySnapshotStageErrorCode::
                        entity_publication_failed,
                    PostResourceEntitySnapshotStageState::protocol_error,
                    inserted.error
                        ? std::string_view{inserted.error->context}
                        : std::string_view{
                              "Synthetic delta snapshot history insert failed"},
                    now,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    inserted.error ? std::optional{inserted.error->code}
                                   : std::nullopt);
                return;
            }
            auto history = history_builder_->publish();
            if (!history || !history.state) {
                fail(
                    PostResourceEntitySnapshotStageErrorCode::
                        entity_publication_failed,
                    PostResourceEntitySnapshotStageState::protocol_error,
                    history.error
                        ? std::string_view{history.error->context}
                        : std::string_view{
                              "Synthetic delta snapshot history was not published"},
                    now,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    history.error ? std::optional{history.error->code}
                                  : std::nullopt);
                return;
            }
            snapshot_history_.emplace(std::move(*history.state));
            auto completed_boundary =
                PostResourceEntitySnapshotStage::complete_synthetic_sequence(
                    *latest_boundary_state_);
            if (!completed_boundary) {
                fail(
                    PostResourceEntitySnapshotStageErrorCode::
                        entity_publication_failed,
                    PostResourceEntitySnapshotStageState::protocol_error,
                    "Synthetic completion requires the ordered delta publication boundary",
                    now);
                return;
            }
            latest_boundary_state_.emplace(
                std::move(*completed_boundary));
            if (!publish_result_state(now)) {
                return;
            }
        } catch (...) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::
                    entity_publication_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Synthetic delta snapshot publication threw",
                now);
            return;
        }

        const auto* const snapshot = result_->latest_snapshot();
        const auto ready_event = PostResourceEntitySnapshotStageEvent{
            .type = PostResourceEntitySnapshotStageEventType::
                delta_entity_snapshot_ready,
            .entity_count = snapshot ? snapshot->entity_count() : 0U,
            .changed_count = snapshot
                ? snapshot->statistics().changed_count
                : 0U,
            .added_count = snapshot
                ? snapshot->statistics().added_count
                : 0U,
            .removed_count = snapshot
                ? snapshot->statistics().removed_count
                : 0U,
            .snapshot_reference = 2U,
            .occurred_at = now};
        state_ = PostResourceEntitySnapshotStageState::entity_snapshot_ready;
        push_event(ready_event);
        emit_trace(ready_event);
        const auto removed_event = PostResourceEntitySnapshotStageEvent{
            .type = PostResourceEntitySnapshotStageEventType::entity_removed,
            .removed_count = 1U,
            .snapshot_reference = 2U,
            .occurred_at = now};
        push_event(removed_event);
        emit_trace(removed_event);
        const auto history_event = PostResourceEntitySnapshotStageEvent{
            .type = PostResourceEntitySnapshotStageEventType::
                snapshot_history_updated,
            .history_count = snapshot_history_->snapshot_count(),
            .snapshot_reference = 2U,
            .occurred_at = now};
        push_event(history_event);
        emit_trace(history_event);
        cleanup(now);
    }

    void handle_post_resource_payload(
        OwnedNetchanPayload payload,
        const PostResourceEntitySnapshotStageTimePoint now)
    {
        if (config_.profile !=
            PostResourceSignonCompatibilityProfile::synthetic_neutral_v1) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::unsupported_message,
                PostResourceEntitySnapshotStageState::unsupported_message,
                "Stock post-resource bodies remain unconsumed without accepted evidence",
                now);
            return;
        }
        if (state_ !=
                PostResourceEntitySnapshotStageState::waiting_for_server_signon &&
            state_ !=
                PostResourceEntitySnapshotStageState::waiting_for_full_snapshot &&
            state_ !=
                PostResourceEntitySnapshotStageState::waiting_for_delta_snapshot) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::unsupported_message,
                PostResourceEntitySnapshotStageState::unsupported_message,
                "Synthetic server frame arrived outside its exact stage boundary",
                now);
            return;
        }

        std::optional<ServicePayloadEnvelopeDecodeResult> envelope;
        try {
            envelope.emplace(ServicePayloadEnvelopeDecoder{
                ServicePayloadEnvelopeLimits{
                    config_.post_resource.maximum_post_resource_payload_bytes}}
                                 .decode(std::move(payload)));
        } catch (...) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::stream_decode_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Post-resource service envelope decoder threw",
                now);
            return;
        }
        if (!*envelope || !envelope->envelope) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::stream_decode_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                envelope->error
                    ? std::string_view{envelope->error->context}
                    : std::string_view{
                          "Post-resource service envelope was not decoded"},
                now);
            return;
        }
        auto& service_payload = envelope->envelope->payload;
        const auto source = PostResourceResponseSourcePayloadMetadata{
            service_payload.direction,
            service_payload.source_sequence,
            service_payload.source_reliable,
            service_payload.reassembled,
            service_payload.decompressed,
            service_payload.bytes.size()};
        std::optional<PostResourceResponseBoundaryParseResult> parsed;
        try {
            parsed.emplace(PostResourceResponseBoundaryParser{
                config_.resource_response.response}.parse(
                    service_payload.bytes, source));
        } catch (...) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::stream_decode_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Post-resource boundary parser threw",
                now);
            return;
        }
        if (!*parsed || !parsed->boundary) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::stream_decode_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                parsed->error
                    ? std::string_view{parsed->error->context}
                    : std::string_view{
                          "Post-resource boundary parser returned no boundary"},
                now);
            return;
        }
        std::optional<PostResourceSignonStreamDecodeResult> decoded;
        try {
            decoded.emplace(PostResourceSignonStreamDecoder{
                config_.post_resource, config_.profile}.decode(
                    service_payload,
                    *parsed->boundary,
                    result_->delta_registry()));
        } catch (...) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::stream_decode_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Post-resource stream decoder threw",
                now);
            return;
        }
        if (!*decoded || !decoded->state ||
            decoded->state->unsupported_boundary() ||
            decoded->state->transcript().server_messages().size() != 1U ||
            !decoded->state->transcript().client_requests().empty()) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::unsupported_message,
                PostResourceEntitySnapshotStageState::unsupported_message,
                decoded->error
                    ? std::string_view{decoded->error->context}
                    : std::string_view{
                          "Post-resource frame is outside neutral v1"},
                now,
                std::nullopt,
                decoded->error ? std::optional{decoded->error->code}
                               : std::nullopt);
            return;
        }

        const auto progress = decoded->state->progress().progress();
        const auto expected =
            (state_ == PostResourceEntitySnapshotStageState::
                           waiting_for_server_signon &&
             progress == PostResourceSignonProgress::
                             synthetic_baseline_publication_observed) ||
            (state_ == PostResourceEntitySnapshotStageState::
                           waiting_for_full_snapshot &&
             progress == PostResourceSignonProgress::
                             synthetic_full_snapshot_publication_observed) ||
            (state_ == PostResourceEntitySnapshotStageState::
                           waiting_for_delta_snapshot &&
             progress == PostResourceSignonProgress::
                             synthetic_delta_snapshot_publication_observed);
        if (!expected) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::unsupported_message,
                PostResourceEntitySnapshotStageState::unsupported_message,
                "Synthetic post-resource frame order is invalid",
                now);
            return;
        }
        if (transcript_server_messages_.size() >=
            config_.post_resource.maximum_post_resource_messages) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::stream_decode_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Aggregate post-resource message count exceeds its bound",
                now,
                std::nullopt,
                PostResourceSignonStreamErrorCode::message_limit_exceeded);
            return;
        }
        const std::size_t required_events =
            progress == PostResourceSignonProgress::
                            synthetic_baseline_publication_observed
                ? 4U
                : progress == PostResourceSignonProgress::
                                  synthetic_full_snapshot_publication_observed
                    ? 4U
                    : 5U;
        if (!can_push_events(required_events)) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::event_backpressure,
                PostResourceEntitySnapshotStageState::backpressure,
                "Synthetic entity publication exceeds the event bound",
                now);
            return;
        }

        auto message = decoded->state->transcript().server_messages().front();
        message.ordinal = transcript_server_messages_.size();
        message.decompressed_payload_ordinal = message.ordinal;
        transcript_server_messages_.push_back(message);
        for (const auto& request :
             decoded->state->transcript().client_requests()) {
            transcript_client_requests_.push_back(request);
        }
        auto aggregate = PostResourceEntitySnapshotStage::aggregate_transcript(
            *decoded->state,
            transcript_server_messages_,
            transcript_client_requests_);
        if (!aggregate) {
            fail(
                PostResourceEntitySnapshotStageErrorCode::stream_decode_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Aggregate post-resource transcript exceeds hard caps",
                now,
                std::nullopt,
                PostResourceSignonStreamErrorCode::message_limit_exceeded);
            return;
        }
        latest_boundary_state_.emplace(std::move(*aggregate));
        const auto received_event = PostResourceEntitySnapshotStageEvent{
            .type = PostResourceEntitySnapshotStageEventType::
                post_resource_message_received,
            .opcode = message.opcode,
            .byte_offset = message.byte_start,
            .bit_offset = message.bit_start,
            .semantic_byte_count = service_payload.bytes.size(),
            .occurred_at = now};
        push_event(received_event);
        emit_trace(received_event);
        const auto progress_event = PostResourceEntitySnapshotStageEvent{
            .type = PostResourceEntitySnapshotStageEventType::
                server_signon_progress,
            .opcode = message.opcode,
            .byte_offset = message.byte_end,
            .bit_offset = message.bit_end,
            .semantic_byte_count = service_payload.bytes.size(),
            .occurred_at = now};
        push_event(progress_event);
        emit_trace(progress_event);

        switch (progress) {
        case PostResourceSignonProgress::
            synthetic_baseline_publication_observed:
            publish_synthetic_baseline(now);
            return;
        case PostResourceSignonProgress::
            synthetic_full_snapshot_publication_observed:
            publish_synthetic_full_snapshot(now);
            return;
        case PostResourceSignonProgress::
            synthetic_delta_snapshot_publication_observed:
            publish_synthetic_delta_snapshot(now);
            return;
        default:
            fail(
                PostResourceEntitySnapshotStageErrorCode::unsupported_message,
                PostResourceEntitySnapshotStageState::unsupported_message,
                "Synthetic post-resource progress is unsupported",
                now);
            return;
        }
    }

    void handle_driver_event(
        NetchanDriverEvent event,
        const PostResourceEntitySnapshotStageTimePoint now)
    {
        switch (event.type) {
        case NetchanDriverEventType::payload_ready:
            if (event.payload && event.payload->bytes.empty()) {
                return;
            }
            if (!event.payload) {
                fail(
                    PostResourceEntitySnapshotStageErrorCode::driver_failed,
                    PostResourceEntitySnapshotStageState::protocol_error,
                    "Driver published payload_ready without an owning payload",
                    now);
                return;
            }
            handle_post_resource_payload(std::move(*event.payload), now);
            return;
        case NetchanDriverEventType::reliable_payload_acknowledged:
            if (!request_transmitted_ || request_acknowledged_) {
                fail(
                    PostResourceEntitySnapshotStageErrorCode::
                        unexpected_acknowledgement,
                    PostResourceEntitySnapshotStageState::protocol_error,
                    "Unexpected reliable acknowledgement",
                    now);
                return;
            }
            if (!can_push_events()) {
                fail(
                    PostResourceEntitySnapshotStageErrorCode::
                        event_backpressure,
                    PostResourceEntitySnapshotStageState::backpressure,
                    "Reliable acknowledgement exceeds the stage event bound",
                    now);
                return;
            }
            request_acknowledged_ = true;
            state_ = PostResourceEntitySnapshotStageState::
                waiting_for_server_signon;
            {
                const auto ack_event =
                    PostResourceEntitySnapshotStageEvent{
                        .type = PostResourceEntitySnapshotStageEventType::
                            client_signon_request_acknowledged,
                        .semantic_byte_count = request_
                            ? request_->semantic_bytes().size()
                            : 0U,
                        .occurred_at = now};
                push_event(ack_event);
                emit_trace(ack_event);
            }
            return;
        case NetchanDriverEventType::normal_transfer_started:
        case NetchanDriverEventType::normal_transfer_completed:
            return;
        case NetchanDriverEventType::secondary_stream_pending_m3:
            fail(
                PostResourceEntitySnapshotStageErrorCode::driver_failed,
                PostResourceEntitySnapshotStageState::secondary_stream_pending,
                "Secondary fragment stream remains unsupported",
                now,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                NetchanDriverErrorCode::secondary_stream_pending_m3);
            return;
        case NetchanDriverEventType::normal_transfer_timed_out:
        case NetchanDriverEventType::channel_timed_out:
            fail(
                PostResourceEntitySnapshotStageErrorCode::driver_failed,
                PostResourceEntitySnapshotStageState::timed_out,
                "Post-resource transfer or channel timed out",
                now);
            return;
        case NetchanDriverEventType::cancelled:
            state_ = PostResourceEntitySnapshotStageState::cancelled;
            result_.reset();
            error_.reset();
            cleanup(now);
            {
                const auto cancelled_event =
                    PostResourceEntitySnapshotStageEvent{
                        .type = PostResourceEntitySnapshotStageEventType::
                            cancelled,
                        .occurred_at = now};
                if (can_push_events()) {
                    push_event(cancelled_event);
                }
                emit_trace(cancelled_event);
            }
            return;
        case NetchanDriverEventType::network_error:
            fail(
                PostResourceEntitySnapshotStageErrorCode::driver_failed,
                PostResourceEntitySnapshotStageState::network_error,
                "Retained driver reported a network failure",
                now);
            return;
        case NetchanDriverEventType::protocol_error:
            fail(
                PostResourceEntitySnapshotStageErrorCode::driver_failed,
                PostResourceEntitySnapshotStageState::protocol_error,
                "Retained driver reported a protocol failure",
                now);
            return;
        }
    }

    void fail_from_response(
        const PostResourceEntitySnapshotStageTimePoint now,
        const bool start_failure)
    {
        fail(
            start_failure
                ? PostResourceEntitySnapshotStageErrorCode::
                      response_stage_start_failed
                : PostResourceEntitySnapshotStageErrorCode::
                      response_stage_failed,
            PostResourceEntitySnapshotStageState::protocol_error,
            response_stage_.error()
                ? std::string_view{response_stage_.error()->context}
                : std::string_view{"Nested resource-response stage failed"},
            now,
            response_stage_.error()
                ? std::optional{response_stage_.error()->code}
                : std::nullopt);
    }

    void fail(
        const PostResourceEntitySnapshotStageErrorCode code,
        const PostResourceEntitySnapshotStageState state,
        const std::string_view context,
        const PostResourceEntitySnapshotStageTimePoint now,
        const std::optional<ResourceClientResponseStageErrorCode> response_code =
            std::nullopt,
        const std::optional<PostResourceSignonStreamErrorCode> stream_code =
            std::nullopt,
        const std::optional<PostResourceClientRequestErrorCode> request_code =
            std::nullopt,
        const std::optional<NetchanDriverErrorCode> driver_code = std::nullopt,
        const std::optional<DeltaValueErrorCode> delta_value_code =
            std::nullopt,
        const std::optional<EntityBaselineErrorCode> baseline_code =
            std::nullopt,
        const std::optional<EntitySnapshotErrorCode> snapshot_code =
            std::nullopt,
        const std::optional<EntitySnapshotHistoryErrorCode> history_code =
            std::nullopt)
    {
        if (terminal_state(state_)) {
            return;
        }
        state_ = state;
        result_.reset();
        error_.reset();
        try {
            PostResourceEntitySnapshotStageError error;
            error.code = code;
            error.response_code = response_code;
            error.stream_code = stream_code;
            error.request_code = request_code;
            error.driver_code = driver_code;
            error.delta_value_code = delta_value_code;
            error.baseline_code = baseline_code;
            error.snapshot_code = snapshot_code;
            error.history_code = history_code;
            const auto bounded = context.substr(
                0U,
                (std::min)(
                    context.size(), kResourceClientResponseStageDiagnosticTextLimit));
            error.context.assign(bounded.data(), bounded.size());
            error_.emplace(std::move(error));
        } catch (...) {
        }
        if (can_push_events()) {
            const auto event = PostResourceEntitySnapshotStageEvent{
                .type = terminal_event(state), .occurred_at = now};
            push_event(event);
            emit_trace(event);
        }
        cleanup(now);
    }

    void set_start_error(
        const PostResourceEntitySnapshotStageErrorCode code,
        const std::string_view context) noexcept
    {
        error_.reset();
        try {
            PostResourceEntitySnapshotStageError error;
            error.code = code;
            const auto bounded = context.substr(
                0U,
                (std::min)(
                    context.size(),
                    kResourceClientResponseStageDiagnosticTextLimit));
            error.context.assign(bounded.data(), bounded.size());
            error_.emplace(std::move(error));
        } catch (...) {
        }
    }

    [[nodiscard]] bool can_push_events(
        const std::size_t count = 1U) const noexcept
    {
        return count <= event_slots_.size() - event_size_;
    }

    void push_event(PostResourceEntitySnapshotStageEvent event)
    {
        if (!can_push_events()) {
            return;
        }
        const auto index = (event_head_ + event_size_) % event_slots_.size();
        event_slots_[index].emplace(std::move(event));
        ++event_size_;
    }

    void cleanup(const PostResourceEntitySnapshotStageTimePoint now) noexcept
    {
        if (cleanup_done_) {
            return;
        }
        cleanup_done_ = true;
        request_.reset();
        latest_boundary_state_.reset();
        baseline_registry_.reset();
        history_builder_.reset();
        snapshot_history_.reset();
        changed_object_.reset();
        transcript_server_messages_.clear();
        transcript_client_requests_.clear();
        if (!response_stage_.terminal()) {
            try {
                response_stage_.cancel(now);
            } catch (...) {
            }
        }
        response_stage_.finalize_retained_boundary(now);
    }

    void emit_trace(const PostResourceEntitySnapshotStageEvent& metadata)
        noexcept
    {
        if (!trace_callback_ || trace_callback_active_) {
            return;
        }
        trace_callback_active_ = true;
        try {
            trace_callback_(PostResourceEntitySnapshotTraceEvent{
                state_, response_stage_.remote_endpoint(), metadata,
                response_stage_.transmitted_packet_count()});
        } catch (...) {
        }
        trace_callback_active_ = false;
    }

    PostResourceEntitySnapshotStageConfig config_;
    PostResourceEntitySnapshotTraceCallback trace_callback_;
    bool trace_callback_active_{false};
    bool configuration_valid_{false};
    ResourceClientResponseStage response_stage_;
    std::vector<std::optional<PostResourceEntitySnapshotStageEvent>> event_slots_;
    std::size_t event_head_{0U};
    std::size_t event_size_{0U};
    PostResourceEntitySnapshotStageState state_{
        PostResourceEntitySnapshotStageState::idle};
    std::optional<PostResourceSignonState> result_;
    std::optional<PostResourceEntitySnapshotStageError> error_;
    std::optional<EncodedPostResourceClientRequest> request_;
    std::optional<PostResourceSignonBoundaryState> latest_boundary_state_;
    std::optional<EntityBaselineRegistryState> baseline_registry_;
    std::optional<EntitySnapshotHistoryBuilder> history_builder_;
    std::optional<EntitySnapshotHistoryState> snapshot_history_;
    std::optional<DeltaObjectState> changed_object_;
    std::vector<PostResourceMessageMetadata> transcript_server_messages_;
    std::vector<PostResourceClientRequestMetadata> transcript_client_requests_;
    std::optional<PostResourceEntitySnapshotStageTimePoint> last_update_;
    std::optional<PostResourceEntitySnapshotStageTimePoint>
        post_resource_started_at_;
    std::size_t request_queue_count_{0U};
    bool request_transmitted_{false};
    bool request_acknowledged_{false};
    bool cleanup_done_{false};
};

PostResourceEntitySnapshotStage::PostResourceEntitySnapshotStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    PostResourceEntitySnapshotStageConfig config,
    resource_consistency::IResourceConsistencyProvider* consistency_provider,
    PostResourceEntitySnapshotTraceCallback trace_callback)
    : implementation_{std::make_unique<Implementation>(
          transport,
          remote_endpoint,
          std::move(config),
          consistency_provider,
          std::move(trace_callback))}
{
}

PostResourceEntitySnapshotStage::~PostResourceEntitySnapshotStage() = default;

bool PostResourceEntitySnapshotStage::start(
    const PostResourceEntitySnapshotStageTimePoint now,
    const network::NetworkAddress& expected_local_endpoint,
    std::unique_ptr<INetchanDriverLifetime> connection_lifetime)
{
    return implementation_->start(
        now, expected_local_endpoint, std::move(connection_lifetime));
}

void PostResourceEntitySnapshotStage::update(
    const PostResourceEntitySnapshotStageTimePoint now)
{
    implementation_->update(now);
}

void PostResourceEntitySnapshotStage::cancel(
    const PostResourceEntitySnapshotStageTimePoint now)
{
    implementation_->cancel(now);
}

std::optional<PostResourceEntitySnapshotStageEvent>
PostResourceEntitySnapshotStage::poll_event()
{
    return implementation_->poll_event();
}

PostResourceEntitySnapshotStageState
PostResourceEntitySnapshotStage::state() const noexcept
{
    return implementation_->state_;
}

bool PostResourceEntitySnapshotStage::terminal() const noexcept
{
    return terminal_state(implementation_->state_);
}

const std::optional<PostResourceSignonState>&
PostResourceEntitySnapshotStage::result() const noexcept
{
    return implementation_->result_;
}

const std::optional<PostResourceEntitySnapshotStageError>&
PostResourceEntitySnapshotStage::error() const noexcept
{
    return implementation_->error_;
}

const network::NetworkAddress&
PostResourceEntitySnapshotStage::remote_endpoint() const noexcept
{
    return implementation_->response_stage_.remote_endpoint();
}

const std::optional<network::NetworkAddress>&
PostResourceEntitySnapshotStage::local_endpoint() const noexcept
{
    return implementation_->response_stage_.local_endpoint();
}

std::size_t PostResourceEntitySnapshotStage::pending_event_count() const noexcept
{
    return implementation_->event_size_;
}

std::size_t
PostResourceEntitySnapshotStage::transmitted_packet_count() const noexcept
{
    return implementation_->response_stage_.transmitted_packet_count();
}

std::size_t PostResourceEntitySnapshotStage::cleanup_count() const noexcept
{
    return implementation_->response_stage_.cleanup_count();
}

std::size_t PostResourceEntitySnapshotStage::request_queue_count() const noexcept
{
    return implementation_->request_queue_count_;
}

bool PostResourceEntitySnapshotStage::request_transmitted() const noexcept
{
    return implementation_->request_transmitted_;
}

bool PostResourceEntitySnapshotStage::request_acknowledged() const noexcept
{
    return implementation_->request_acknowledged_;
}

} // namespace hlclient::goldsrc
