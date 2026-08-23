#include <hlclient/goldsrc/precache_manifest_stage.hpp>

#include <algorithm>
#include <new>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool terminal_state(
    const PrecacheManifestStageState state) noexcept
{
    switch (state) {
    case PrecacheManifestStageState::precache_manifest_ready:
    case PrecacheManifestStageState::local_resources_incomplete:
    case PrecacheManifestStageState::unsafe_local_resources:
    case PrecacheManifestStageState::unsupported_local_profile:
    case PrecacheManifestStageState::local_resource_io_error:
    case PrecacheManifestStageState::timed_out:
    case PrecacheManifestStageState::cancelled:
    case PrecacheManifestStageState::backpressure:
    case PrecacheManifestStageState::secondary_stream_pending:
    case PrecacheManifestStageState::network_error:
    case PrecacheManifestStageState::protocol_error:
        return true;
    case PrecacheManifestStageState::idle:
    case PrecacheManifestStageState::waiting_for_resource_response_boundary:
    case PrecacheManifestStageState::building_local_inventory:
    case PrecacheManifestStageState::building_precache_manifest:
        return false;
    }
    return true;
}

[[nodiscard]] PrecacheManifestStageEventType terminal_event(
    const PrecacheManifestStageState state) noexcept
{
    switch (state) {
    case PrecacheManifestStageState::precache_manifest_ready:
        return PrecacheManifestStageEventType::precache_manifest_ready;
    case PrecacheManifestStageState::local_resources_incomplete:
        return PrecacheManifestStageEventType::local_resources_incomplete;
    case PrecacheManifestStageState::unsafe_local_resources:
        return PrecacheManifestStageEventType::unsafe_local_resources;
    case PrecacheManifestStageState::unsupported_local_profile:
        return PrecacheManifestStageEventType::unsupported_local_profile;
    case PrecacheManifestStageState::local_resource_io_error:
        return PrecacheManifestStageEventType::local_resource_io_error;
    case PrecacheManifestStageState::timed_out:
        return PrecacheManifestStageEventType::timeout;
    case PrecacheManifestStageState::cancelled:
        return PrecacheManifestStageEventType::cancelled;
    case PrecacheManifestStageState::backpressure:
        return PrecacheManifestStageEventType::backpressure;
    case PrecacheManifestStageState::secondary_stream_pending:
        return PrecacheManifestStageEventType::secondary_stream_pending;
    case PrecacheManifestStageState::network_error:
        return PrecacheManifestStageEventType::network_error;
    case PrecacheManifestStageState::idle:
    case PrecacheManifestStageState::waiting_for_resource_response_boundary:
    case PrecacheManifestStageState::building_local_inventory:
    case PrecacheManifestStageState::building_precache_manifest:
    case PrecacheManifestStageState::protocol_error:
        return PrecacheManifestStageEventType::protocol_error;
    }
    return PrecacheManifestStageEventType::protocol_error;
}

[[nodiscard]] PrecacheManifestTraceClassification terminal_trace(
    const PrecacheManifestStageState state) noexcept
{
    switch (state) {
    case PrecacheManifestStageState::precache_manifest_ready:
        return PrecacheManifestTraceClassification::precache_manifest_ready;
    case PrecacheManifestStageState::local_resources_incomplete:
        return PrecacheManifestTraceClassification::local_resources_incomplete;
    case PrecacheManifestStageState::unsafe_local_resources:
        return PrecacheManifestTraceClassification::unsafe_local_resources;
    case PrecacheManifestStageState::unsupported_local_profile:
        return PrecacheManifestTraceClassification::unsupported_local_profile;
    case PrecacheManifestStageState::local_resource_io_error:
        return PrecacheManifestTraceClassification::local_resource_io_error;
    case PrecacheManifestStageState::timed_out:
        return PrecacheManifestTraceClassification::stage_timed_out;
    case PrecacheManifestStageState::cancelled:
        return PrecacheManifestTraceClassification::stage_cancelled;
    case PrecacheManifestStageState::backpressure:
        return PrecacheManifestTraceClassification::backpressure;
    case PrecacheManifestStageState::secondary_stream_pending:
        return PrecacheManifestTraceClassification::secondary_stream_pending;
    case PrecacheManifestStageState::network_error:
        return PrecacheManifestTraceClassification::network_failure;
    case PrecacheManifestStageState::idle:
    case PrecacheManifestStageState::waiting_for_resource_response_boundary:
    case PrecacheManifestStageState::building_local_inventory:
    case PrecacheManifestStageState::building_precache_manifest:
    case PrecacheManifestStageState::protocol_error:
        return PrecacheManifestTraceClassification::protocol_failure;
    }
    return PrecacheManifestTraceClassification::protocol_failure;
}

[[nodiscard]] const ServerInfoState& server_info_from(
    const ResourceClientResponseSignonState& response) noexcept
{
    return response.resource_list()
        .transition()
        .user_info()
        .movement_environment()
        .delta_description()
        .pre_resource()
        .server_info();
}

[[nodiscard]] PrecacheManifestStageState response_failure_state(
    const ResourceClientResponseStageState state) noexcept
{
    switch (state) {
    case ResourceClientResponseStageState::timed_out:
        return PrecacheManifestStageState::timed_out;
    case ResourceClientResponseStageState::cancelled:
        return PrecacheManifestStageState::cancelled;
    case ResourceClientResponseStageState::backpressure:
        return PrecacheManifestStageState::backpressure;
    case ResourceClientResponseStageState::secondary_stream_pending:
        return PrecacheManifestStageState::secondary_stream_pending;
    case ResourceClientResponseStageState::network_error:
        return PrecacheManifestStageState::network_error;
    case ResourceClientResponseStageState::unsupported_response_profile:
        return PrecacheManifestStageState::unsupported_local_profile;
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
    case ResourceClientResponseStageState::consistency_provider_required:
    case ResourceClientResponseStageState::protocol_error:
        return PrecacheManifestStageState::protocol_error;
    }
    return PrecacheManifestStageState::protocol_error;
}

[[nodiscard]] PrecacheManifestStageState manifest_terminal_state(
    const PrecacheManifestCompleteness completeness) noexcept
{
    switch (completeness) {
    case PrecacheManifestCompleteness::complete_for_supported_local_profile:
        return PrecacheManifestStageState::precache_manifest_ready;
    case PrecacheManifestCompleteness::world_ready_but_incomplete:
    case PrecacheManifestCompleteness::incomplete_missing_resources:
        return PrecacheManifestStageState::local_resources_incomplete;
    case PrecacheManifestCompleteness::blocked_unsafe_resources:
        return PrecacheManifestStageState::unsafe_local_resources;
    case PrecacheManifestCompleteness::unsupported_profile:
        return PrecacheManifestStageState::unsupported_local_profile;
    case PrecacheManifestCompleteness::local_io_failure:
        return PrecacheManifestStageState::local_resource_io_error;
    case PrecacheManifestCompleteness::invalid_server_resource_correlation:
        return PrecacheManifestStageState::protocol_error;
    }
    return PrecacheManifestStageState::protocol_error;
}

} // namespace

bool valid_precache_manifest_stage_configuration(
    const PrecacheManifestStageConfig& config) noexcept
{
    return valid_resource_client_response_stage_configuration(config.response) &&
           valid_local_resource_inventory_limits(config.inventory) &&
           valid_precache_manifest_limits(config.manifest);
}

class PrecacheManifestSignonState::Implementation final {
public:
    Implementation(
        ResourceClientResponseSignonState response,
        LocalResourceInventoryState inventory,
        PrecacheManifestState manifest,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment) noexcept
        : response_{std::move(response)},
          inventory_{std::move(inventory)},
          manifest_{std::move(manifest)},
          environment_{std::move(environment)}
    {
    }

    ResourceClientResponseSignonState response_;
    LocalResourceInventoryState inventory_;
    PrecacheManifestState manifest_;
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment_;
};

PrecacheManifestSignonState::PrecacheManifestSignonState(
    std::unique_ptr<Implementation> implementation) noexcept
    : implementation_{std::move(implementation)}
{
}

PrecacheManifestSignonState::~PrecacheManifestSignonState() = default;
PrecacheManifestSignonState::PrecacheManifestSignonState(
    PrecacheManifestSignonState&&) noexcept = default;
PrecacheManifestSignonState& PrecacheManifestSignonState::operator=(
    PrecacheManifestSignonState&&) noexcept = default;

const ResourceClientResponseSignonState&
PrecacheManifestSignonState::response() const noexcept
{
    return implementation_->response_;
}

const LocalResourceInventoryState&
PrecacheManifestSignonState::inventory() const noexcept
{
    return implementation_->inventory_;
}

const PrecacheManifestState&
PrecacheManifestSignonState::manifest() const noexcept
{
    return implementation_->manifest_;
}

const std::shared_ptr<const local_resources::LocalResourceEnvironment>&
PrecacheManifestSignonState::environment() const noexcept
{
    return implementation_->environment_;
}

class PrecacheManifestStage::Implementation final {
public:
    Implementation(
        network::IDatagramTransport& transport,
        const network::NetworkAddress remote_endpoint,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        PrecacheManifestStageConfig config,
        resource_consistency::IResourceConsistencyProvider*
            consistency_provider,
        InitialSignonTraceCallback initial_trace_callback,
        PreResourceSignonTraceCallback pre_resource_trace_callback,
        DeltaDescriptionTraceCallback delta_trace_callback,
        MovementEnvironmentTraceCallback movement_trace_callback,
        UserInfoSignonTraceCallback user_info_trace_callback,
        ResourceTransitionTraceCallback transition_trace_callback,
        ResourceListTraceCallback resource_list_trace_callback,
        ResourceClientResponseTraceCallback response_trace_callback,
        PrecacheManifestTraceCallback trace_callback,
        const bool retain_connection_at_boundary)
        : config_{std::move(config)},
          environment_{std::move(environment)},
          trace_callback_{std::move(trace_callback)},
          retain_connection_at_boundary_{retain_connection_at_boundary},
          configuration_valid_{
              valid_precache_manifest_stage_configuration(config_) &&
              environment_ != nullptr && environment_->root_count() > 0U},
          response_stage_{
              transport,
              remote_endpoint,
              config_.response,
              consistency_provider,
              std::move(initial_trace_callback),
              std::move(pre_resource_trace_callback),
              std::move(delta_trace_callback),
              std::move(movement_trace_callback),
              std::move(user_info_trace_callback),
              std::move(transition_trace_callback),
              std::move(resource_list_trace_callback),
              std::move(response_trace_callback),
              ResourceClientResponseStage::RetainConnectionAtBoundary{}},
          event_slots_(
              configuration_valid_
                  ? config_.manifest.maximum_manifest_events
                  : 0U)
    {
    }

    ~Implementation()
    {
        if (state_ != PrecacheManifestStageState::idle &&
            !terminal_state(state_)) {
            cancel(last_update_.value_or(PrecacheManifestStageTimePoint{}));
        } else if (retain_connection_at_boundary_ && !cleanup_done_ &&
                   result_) {
            cleanup(last_update_.value_or(PrecacheManifestStageTimePoint{}));
        }
    }

    [[nodiscard]] bool start(
        const PrecacheManifestStageTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime)
    {
        if (trace_callback_active_ ||
            state_ != PrecacheManifestStageState::idle) {
            return false;
        }
        error_.reset();
        if (!configuration_valid_) {
            set_start_error(
                PrecacheManifestStageErrorCode::invalid_configuration,
                "Precache-manifest stage configuration or local environment is invalid");
            emit_trace(PrecacheManifestTraceClassification::protocol_failure);
            return false;
        }

        bool started = false;
        try {
            started = response_stage_.start(
                now,
                expected_local_endpoint,
                std::move(connection_lifetime));
        } catch (...) {
            try { response_stage_.cancel(now); } catch (...) {}
            fail(
                PrecacheManifestStageErrorCode::response_stage_start_failed,
                PrecacheManifestStageState::protocol_error,
                "Nested resource-response stage threw during start",
                now);
            return false;
        }
        drain_response_events();
        if (!started) {
            fail_from_response(now, true);
            return false;
        }

        last_update_ = now;
        state_ =
            PrecacheManifestStageState::waiting_for_resource_response_boundary;
        emit_trace(PrecacheManifestTraceClassification::stage_started);
        return true;
    }

    void update(const PrecacheManifestStageTimePoint now)
    {
        if (trace_callback_active_ ||
            state_ == PrecacheManifestStageState::idle ||
            terminal_state(state_)) {
            return;
        }
        if (!last_update_ || now < *last_update_) {
            fail(
                PrecacheManifestStageErrorCode::time_moved_backwards,
                PrecacheManifestStageState::protocol_error,
                "Precache-manifest stage update time moved backwards",
                now);
            return;
        }
        last_update_ = now;

        if (state_ == PrecacheManifestStageState::
                          waiting_for_resource_response_boundary) {
            drain_response_events();
            try {
                response_stage_.update(now);
            } catch (...) {
                try { response_stage_.cancel(now); } catch (...) {}
                fail(
                    PrecacheManifestStageErrorCode::response_stage_failed,
                    PrecacheManifestStageState::protocol_error,
                    "Nested resource-response stage threw during update",
                    now);
                return;
            }
            drain_response_events();
            synchronize_from_response(now);
            return;
        }
        if (state_ ==
            PrecacheManifestStageState::building_local_inventory) {
            build_local_inventory(now);
            return;
        }
        if (state_ ==
            PrecacheManifestStageState::building_precache_manifest) {
            build_precache_manifest(now);
        }
    }

    void cancel(const PrecacheManifestStageTimePoint now)
    {
        if (trace_callback_active_ ||
            state_ == PrecacheManifestStageState::idle ||
            terminal_state(state_)) {
            return;
        }
        if (state_ == PrecacheManifestStageState::
                          waiting_for_resource_response_boundary) {
            try { response_stage_.cancel(now); } catch (...) {}
        }
        state_ = PrecacheManifestStageState::cancelled;
        result_.reset();
        inventory_.reset();
        error_.reset();
        if (can_push_events()) {
            push_event(PrecacheManifestStageEvent{
                .type = PrecacheManifestStageEventType::cancelled,
                .occurred_at = now,
            });
        }
        cleanup(now);
        emit_trace(PrecacheManifestTraceClassification::stage_cancelled);
    }

    [[nodiscard]] std::optional<PrecacheManifestStageEvent> poll_event()
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

    [[nodiscard]] bool can_push_events(
        const std::size_t count = 1U) const noexcept
    {
        return count <= event_slots_.size() - event_size_;
    }

    void push_event(PrecacheManifestStageEvent event) noexcept
    {
        const auto index = (event_head_ + event_size_) % event_slots_.size();
        event_slots_[index].emplace(std::move(event));
        ++event_size_;
    }

    void drain_response_events() noexcept
    {
        while (response_stage_.poll_event()) {
        }
    }

    void synchronize_from_response(const PrecacheManifestStageTimePoint now)
    {
        if (response_stage_.state() ==
            ResourceClientResponseStageState::next_server_boundary_reached) {
            if (!response_stage_.result() ||
                response_stage_.retained_driver() == nullptr) {
                fail(
                    PrecacheManifestStageErrorCode::retained_driver_missing,
                    PrecacheManifestStageState::protocol_error,
                    "Resource-response completion has no owning result or retained driver",
                    now);
                return;
            }
            if (!can_push_events()) {
                fail(
                    PrecacheManifestStageErrorCode::event_backpressure,
                    PrecacheManifestStageState::backpressure,
                    "No bounded event slot remains for the response boundary",
                    now);
                return;
            }
            state_ = PrecacheManifestStageState::building_local_inventory;
            push_event(PrecacheManifestStageEvent{
                .type = PrecacheManifestStageEventType::
                    resource_response_boundary_reached,
                .entry_count = response_stage_.result()
                                   ->resource_list()
                                   .resource_list()
                                   .resource_count(),
                .occurred_at = now,
            });
            emit_trace(
                PrecacheManifestTraceClassification::
                    resource_response_boundary_reached);
            return;
        }
        if (response_stage_.terminal() || response_stage_.error()) {
            fail_from_response(now, false);
        }
    }

    void build_local_inventory(const PrecacheManifestStageTimePoint now)
    {
        if (!response_stage_.result() || !environment_ ||
            response_stage_.retained_driver() == nullptr) {
            fail(
                PrecacheManifestStageErrorCode::retained_driver_missing,
                PrecacheManifestStageState::protocol_error,
                "Local inventory build lost its retained prerequisites",
                now);
            return;
        }

        std::optional<LocalResourceInventoryBuildResult> built;
        try {
            built.emplace(
                LocalResourceInventoryBuilder{config_.inventory}.build(
                    response_stage_.result()->resource_list().resource_list(),
                    mapper_,
                    environment_->resolver()));
        } catch (...) {
            fail(
                PrecacheManifestStageErrorCode::inventory_build_failed,
                PrecacheManifestStageState::protocol_error,
                "Bounded local inventory builder threw",
                now);
            return;
        }
        if (!built || !*built || !built->state) {
            fail(
                PrecacheManifestStageErrorCode::inventory_build_failed,
                PrecacheManifestStageState::protocol_error,
                built && built->error
                    ? std::string_view{built->error->context}
                    : std::string_view{
                          "Local inventory builder returned no state"},
                now,
                std::nullopt,
                built && built->error
                    ? std::optional{built->error->code}
                    : std::nullopt);
            return;
        }
        if (!can_push_events()) {
            fail(
                PrecacheManifestStageErrorCode::event_backpressure,
                PrecacheManifestStageState::backpressure,
                "No bounded event slot remains for local inventory metadata",
                now);
            return;
        }

        const auto& summary = built->state->summary();
        const auto unsupported =
            summary.count(
                LocalResourceInventoryStatus::unsupported_name_encoding) +
            summary.count(LocalResourceInventoryStatus::unsupported_mapping);
        const auto event = PrecacheManifestStageEvent{
            .type = PrecacheManifestStageEventType::local_inventory_ready,
            .entry_count = summary.total_entry_count(),
            .ready_count = summary.count(LocalResourceInventoryStatus::resolved),
            .missing_count = summary.count(LocalResourceInventoryStatus::missing),
            .unsafe_count = summary.count(LocalResourceInventoryStatus::unsafe_name),
            .unsupported_count = unsupported,
            .ambiguous_count = summary.count(LocalResourceInventoryStatus::ambiguous),
            .io_error_count = summary.count(LocalResourceInventoryStatus::io_error),
            .occurred_at = now,
        };
        inventory_.emplace(std::move(*built->state));
        state_ = PrecacheManifestStageState::building_precache_manifest;
        push_event(event);
        emit_trace(
            PrecacheManifestTraceClassification::local_inventory_ready,
            event);
    }

    void build_precache_manifest(const PrecacheManifestStageTimePoint now)
    {
        if (!response_stage_.result() || !inventory_ || !environment_ ||
            response_stage_.retained_driver() == nullptr) {
            fail(
                PrecacheManifestStageErrorCode::retained_driver_missing,
                PrecacheManifestStageState::protocol_error,
                "Precache-manifest build lost its retained prerequisites",
                now);
            return;
        }

        std::optional<PrecacheManifestBuildResult> built;
        try {
            const auto& response = *response_stage_.result();
            built.emplace(
                PrecacheManifestBuilder{config_.manifest}.build(
                    response.resource_list().resource_list(),
                    *inventory_,
                    server_info_from(response),
                    mapper_,
                    *environment_));
        } catch (...) {
            fail(
                PrecacheManifestStageErrorCode::manifest_build_failed,
                PrecacheManifestStageState::protocol_error,
                "Bounded precache-manifest builder threw",
                now);
            return;
        }
        if (!built || !*built || !built->state) {
            fail(
                PrecacheManifestStageErrorCode::manifest_build_failed,
                PrecacheManifestStageState::protocol_error,
                built && built->error
                    ? std::string_view{built->error->context}
                    : std::string_view{
                          "Precache-manifest builder returned no state"},
                now,
                std::nullopt,
                std::nullopt,
                built && built->error
                    ? std::optional{built->error->code}
                    : std::nullopt);
            return;
        }

        const auto terminal = manifest_terminal_state(
            built->state->completeness());
        if (terminal == PrecacheManifestStageState::protocol_error) {
            fail(
                PrecacheManifestStageErrorCode::manifest_build_failed,
                PrecacheManifestStageState::protocol_error,
                "Manifest builder published invalid server-resource correlation",
                now);
            return;
        }
        if (!can_push_events()) {
            fail(
                PrecacheManifestStageErrorCode::event_backpressure,
                PrecacheManifestStageState::backpressure,
                "No bounded event slot remains for manifest publication",
                now);
            return;
        }

        const auto& summary = built->state->readiness_summary();
        const auto event = PrecacheManifestStageEvent{
            .type = terminal_event(terminal),
            .entry_count = summary.total_entry_count(),
            .ready_count = summary.resolved_mapped_file_count(),
            .metadata_only_count = summary.metadata_only_count(),
            .missing_count = summary.missing_count(),
            .unsafe_count = summary.security_blocked_count(),
            .unsupported_count = summary.unsupported_count(),
            .ambiguous_count = summary.ambiguous_count(),
            .io_error_count = summary.io_failure_count(),
            .occurred_at = now,
        };

        try {
            auto owned = std::make_unique<
                PrecacheManifestSignonState::Implementation>(
                *response_stage_.result(),
                std::move(*inventory_),
                std::move(*built->state),
                environment_);
            result_.emplace(PrecacheManifestSignonState{std::move(owned)});
        } catch (...) {
            fail(
                PrecacheManifestStageErrorCode::manifest_build_failed,
                PrecacheManifestStageState::protocol_error,
                "Unable to retain the owning precache-manifest publication",
                now);
            return;
        }

        inventory_.reset();
        ++manifest_publication_count_;
        state_ = terminal;
        push_event(event);
        if (!retain_connection_at_boundary_) {
            cleanup(now);
        }
        emit_trace(terminal_trace(terminal), event);
    }

    void fail_from_response(
        const PrecacheManifestStageTimePoint now,
        const bool start_failure) noexcept
    {
        const auto& nested_error = response_stage_.error();
        const auto mapped_state = response_failure_state(response_stage_.state());
        fail(
            start_failure
                ? PrecacheManifestStageErrorCode::response_stage_start_failed
                : PrecacheManifestStageErrorCode::response_stage_failed,
            mapped_state,
            nested_error
                ? std::string_view{nested_error->context}
                : std::string_view{"Nested resource-response stage failed"},
            now,
            nested_error ? std::optional{nested_error->code} : std::nullopt);
    }

    void fail(
        const PrecacheManifestStageErrorCode code,
        const PrecacheManifestStageState state,
        const std::string_view context,
        const PrecacheManifestStageTimePoint now,
        const std::optional<ResourceClientResponseStageErrorCode>
            response_code = std::nullopt,
        const std::optional<LocalResourceInventoryErrorCode>
            inventory_code = std::nullopt,
        const std::optional<PrecacheManifestErrorCode>
            manifest_code = std::nullopt) noexcept
    {
        if (terminal_state(state_)) {
            return;
        }
        state_ = state;
        result_.reset();
        inventory_.reset();
        error_.reset();
        try {
            error_.emplace();
            error_->code = code;
            error_->response_code = response_code;
            error_->inventory_code = inventory_code;
            error_->manifest_code = manifest_code;
            const auto bounded = context.substr(
                0U,
                (std::min)(
                    context.size(),
                    kPrecacheManifestStageDiagnosticTextLimit));
            error_->context.assign(bounded.data(), bounded.size());
        } catch (...) {
        }
        if (can_push_events()) {
            push_event(PrecacheManifestStageEvent{
                .type = terminal_event(state),
                .occurred_at = now,
            });
        }
        cleanup(now);
        emit_trace(terminal_trace(state));
    }

    void set_start_error(
        const PrecacheManifestStageErrorCode code,
        const std::string_view context) noexcept
    {
        error_.reset();
        try {
            error_.emplace();
            error_->code = code;
            const auto bounded = context.substr(
                0U,
                (std::min)(
                    context.size(),
                    kPrecacheManifestStageDiagnosticTextLimit));
            error_->context.assign(bounded.data(), bounded.size());
        } catch (...) {
        }
    }

    void cleanup(const PrecacheManifestStageTimePoint now) noexcept
    {
        if (cleanup_done_) {
            return;
        }
        cleanup_done_ = true;
        response_stage_.finalize_retained_boundary(now);
    }

    void emit_trace(
        const PrecacheManifestTraceClassification classification,
        const PrecacheManifestStageEvent metadata = {}) noexcept
    {
        if (!trace_callback_ || trace_callback_active_) {
            return;
        }
        PrecacheManifestTraceEvent event;
        event.classification = classification;
        event.state = state_;
        event.endpoint = response_stage_.remote_endpoint();
        event.entry_count = metadata.entry_count;
        event.ready_count = metadata.ready_count;
        event.metadata_only_count = metadata.metadata_only_count;
        event.missing_count = metadata.missing_count;
        event.unsafe_count = metadata.unsafe_count;
        event.unsupported_count = metadata.unsupported_count;
        event.ambiguous_count = metadata.ambiguous_count;
        event.io_error_count = metadata.io_error_count;
        event.transmitted_packet_count =
            response_stage_.transmitted_packet_count();
        trace_callback_active_ = true;
        try { trace_callback_(event); } catch (...) {}
        trace_callback_active_ = false;
    }

    PrecacheManifestStageConfig config_;
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment_;
    GoldSrcResourceNameMapper mapper_;
    PrecacheManifestTraceCallback trace_callback_;
    bool trace_callback_active_{false};
    bool retain_connection_at_boundary_{false};
    bool configuration_valid_{false};
    ResourceClientResponseStage response_stage_;
    std::vector<std::optional<PrecacheManifestStageEvent>> event_slots_;
    std::size_t event_head_{0U};
    std::size_t event_size_{0U};
    PrecacheManifestStageState state_{PrecacheManifestStageState::idle};
    std::optional<PrecacheManifestSignonState> result_;
    std::optional<PrecacheManifestStageError> error_;
    std::optional<LocalResourceInventoryState> inventory_;
    std::optional<PrecacheManifestStageTimePoint> last_update_;
    std::size_t manifest_publication_count_{0U};
    bool cleanup_done_{false};
};

PrecacheManifestStage::PrecacheManifestStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment,
    PrecacheManifestStageConfig config,
    resource_consistency::IResourceConsistencyProvider* consistency_provider,
    InitialSignonTraceCallback initial_trace_callback,
    PreResourceSignonTraceCallback pre_resource_trace_callback,
    DeltaDescriptionTraceCallback delta_trace_callback,
    MovementEnvironmentTraceCallback movement_trace_callback,
    UserInfoSignonTraceCallback user_info_trace_callback,
    ResourceTransitionTraceCallback transition_trace_callback,
    ResourceListTraceCallback resource_list_trace_callback,
    ResourceClientResponseTraceCallback response_trace_callback,
    PrecacheManifestTraceCallback trace_callback)
    : implementation_{std::make_unique<Implementation>(
          transport,
          remote_endpoint,
          std::move(environment),
          std::move(config),
          consistency_provider,
          std::move(initial_trace_callback),
          std::move(pre_resource_trace_callback),
          std::move(delta_trace_callback),
          std::move(movement_trace_callback),
          std::move(user_info_trace_callback),
          std::move(transition_trace_callback),
          std::move(resource_list_trace_callback),
          std::move(response_trace_callback),
          std::move(trace_callback),
          false)}
{
}

PrecacheManifestStage::PrecacheManifestStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment,
    PrecacheManifestStageConfig config,
    resource_consistency::IResourceConsistencyProvider* consistency_provider,
    InitialSignonTraceCallback initial_trace_callback,
    PreResourceSignonTraceCallback pre_resource_trace_callback,
    DeltaDescriptionTraceCallback delta_trace_callback,
    MovementEnvironmentTraceCallback movement_trace_callback,
    UserInfoSignonTraceCallback user_info_trace_callback,
    ResourceTransitionTraceCallback transition_trace_callback,
    ResourceListTraceCallback resource_list_trace_callback,
    ResourceClientResponseTraceCallback response_trace_callback,
    PrecacheManifestTraceCallback trace_callback,
    RetainConnectionAtBoundary)
    : implementation_{std::make_unique<Implementation>(
          transport,
          remote_endpoint,
          std::move(environment),
          std::move(config),
          consistency_provider,
          std::move(initial_trace_callback),
          std::move(pre_resource_trace_callback),
          std::move(delta_trace_callback),
          std::move(movement_trace_callback),
          std::move(user_info_trace_callback),
          std::move(transition_trace_callback),
          std::move(resource_list_trace_callback),
          std::move(response_trace_callback),
          std::move(trace_callback),
          true)}
{
}

PrecacheManifestStage::~PrecacheManifestStage() = default;

bool PrecacheManifestStage::start(
    const PrecacheManifestStageTimePoint now,
    const network::NetworkAddress& expected_local_endpoint,
    std::unique_ptr<INetchanDriverLifetime> connection_lifetime)
{
    return implementation_->start(
        now,
        expected_local_endpoint,
        std::move(connection_lifetime));
}

void PrecacheManifestStage::update(const PrecacheManifestStageTimePoint now)
{
    implementation_->update(now);
}

void PrecacheManifestStage::cancel(const PrecacheManifestStageTimePoint now)
{
    implementation_->cancel(now);
}

std::optional<PrecacheManifestStageEvent>
PrecacheManifestStage::poll_event()
{
    return implementation_->poll_event();
}

PrecacheManifestStageState PrecacheManifestStage::state() const noexcept
{
    return implementation_->state_;
}

bool PrecacheManifestStage::terminal() const noexcept
{
    return terminal_state(implementation_->state_);
}

const std::optional<PrecacheManifestSignonState>&
PrecacheManifestStage::result() const noexcept
{
    return implementation_->result_;
}

const std::optional<PrecacheManifestStageError>&
PrecacheManifestStage::error() const noexcept
{
    return implementation_->error_;
}

const network::NetworkAddress&
PrecacheManifestStage::remote_endpoint() const noexcept
{
    return implementation_->response_stage_.remote_endpoint();
}

const std::optional<network::NetworkAddress>&
PrecacheManifestStage::local_endpoint() const noexcept
{
    return implementation_->response_stage_.local_endpoint();
}

std::size_t PrecacheManifestStage::pending_event_count() const noexcept
{
    return implementation_->event_size_;
}

std::size_t PrecacheManifestStage::transmitted_packet_count() const noexcept
{
    return implementation_->response_stage_.transmitted_packet_count();
}

std::size_t PrecacheManifestStage::cleanup_count() const noexcept
{
    return implementation_->response_stage_.cleanup_count();
}

std::size_t PrecacheManifestStage::initial_request_queue_count() const noexcept
{
    return implementation_->response_stage_.initial_request_queue_count();
}

std::size_t
PrecacheManifestStage::transition_request_queue_count() const noexcept
{
    return implementation_->response_stage_.transition_request_queue_count();
}

std::size_t PrecacheManifestStage::response_queue_count() const noexcept
{
    return implementation_->response_stage_.response_queue_count();
}

std::size_t PrecacheManifestStage::manifest_publication_count() const noexcept
{
    return implementation_->manifest_publication_count_;
}

NetchanDriver* PrecacheManifestStage::retained_driver() noexcept
{
    if (!implementation_->retain_connection_at_boundary_ ||
        !implementation_->result_ || implementation_->cleanup_done_) {
        return nullptr;
    }
    return implementation_->response_stage_.retained_driver();
}

void PrecacheManifestStage::finalize_retained_boundary(
    const PrecacheManifestStageTimePoint now) noexcept
{
    if (!implementation_->retain_connection_at_boundary_ ||
        !implementation_->result_) {
        return;
    }
    implementation_->cleanup(now);
}

} // namespace hlclient::goldsrc
