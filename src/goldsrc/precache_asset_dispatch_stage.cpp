#include <hlclient/goldsrc/precache_asset_dispatch_stage.hpp>

#include <algorithm>
#include <new>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool terminal_state(
    const PrecacheAssetDispatchStageState state) noexcept
{
    switch (state) {
    case PrecacheAssetDispatchStageState::asset_imported:
    case PrecacheAssetDispatchStageState::importer_boundary_reached:
    case PrecacheAssetDispatchStageState::world_source_unavailable:
    case PrecacheAssetDispatchStageState::source_open_failed:
    case PrecacheAssetDispatchStageState::ambiguous_importer:
    case PrecacheAssetDispatchStageState::import_failed:
    case PrecacheAssetDispatchStageState::timed_out:
    case PrecacheAssetDispatchStageState::cancelled:
    case PrecacheAssetDispatchStageState::backpressure:
    case PrecacheAssetDispatchStageState::network_error:
    case PrecacheAssetDispatchStageState::protocol_error:
        return true;
    case PrecacheAssetDispatchStageState::idle:
    case PrecacheAssetDispatchStageState::waiting_for_precache_manifest:
    case PrecacheAssetDispatchStageState::selecting_world_entry:
    case PrecacheAssetDispatchStageState::opening_asset_source:
    case PrecacheAssetDispatchStageState::asset_source_ready:
    case PrecacheAssetDispatchStageState::probing_importers:
    case PrecacheAssetDispatchStageState::importing_asset:
        return false;
    }
    return true;
}

[[nodiscard]] PrecacheAssetDispatchStageState manifest_failure_state(
    const PrecacheManifestStageState state) noexcept
{
    switch (state) {
    case PrecacheManifestStageState::timed_out:
        return PrecacheAssetDispatchStageState::timed_out;
    case PrecacheManifestStageState::cancelled:
        return PrecacheAssetDispatchStageState::cancelled;
    case PrecacheManifestStageState::backpressure:
        return PrecacheAssetDispatchStageState::backpressure;
    case PrecacheManifestStageState::network_error:
        return PrecacheAssetDispatchStageState::network_error;
    case PrecacheManifestStageState::idle:
    case PrecacheManifestStageState::waiting_for_resource_response_boundary:
    case PrecacheManifestStageState::building_local_inventory:
    case PrecacheManifestStageState::building_precache_manifest:
    case PrecacheManifestStageState::precache_manifest_ready:
    case PrecacheManifestStageState::local_resources_incomplete:
    case PrecacheManifestStageState::unsafe_local_resources:
    case PrecacheManifestStageState::unsupported_local_profile:
    case PrecacheManifestStageState::local_resource_io_error:
    case PrecacheManifestStageState::secondary_stream_pending:
    case PrecacheManifestStageState::protocol_error:
        return PrecacheAssetDispatchStageState::protocol_error;
    }
    return PrecacheAssetDispatchStageState::protocol_error;
}

[[nodiscard]] PrecacheAssetDispatchStageEventType terminal_event(
    const PrecacheAssetDispatchStageState state) noexcept
{
    switch (state) {
    case PrecacheAssetDispatchStageState::asset_imported:
        return PrecacheAssetDispatchStageEventType::asset_imported;
    case PrecacheAssetDispatchStageState::importer_boundary_reached:
        return PrecacheAssetDispatchStageEventType::importer_boundary_reached;
    case PrecacheAssetDispatchStageState::world_source_unavailable:
        return PrecacheAssetDispatchStageEventType::world_source_unavailable;
    case PrecacheAssetDispatchStageState::source_open_failed:
        return PrecacheAssetDispatchStageEventType::source_open_failed;
    case PrecacheAssetDispatchStageState::ambiguous_importer:
        return PrecacheAssetDispatchStageEventType::ambiguous_importer;
    case PrecacheAssetDispatchStageState::import_failed:
        return PrecacheAssetDispatchStageEventType::import_failed;
    case PrecacheAssetDispatchStageState::timed_out:
        return PrecacheAssetDispatchStageEventType::timeout;
    case PrecacheAssetDispatchStageState::cancelled:
        return PrecacheAssetDispatchStageEventType::cancelled;
    case PrecacheAssetDispatchStageState::backpressure:
        return PrecacheAssetDispatchStageEventType::backpressure;
    case PrecacheAssetDispatchStageState::network_error:
        return PrecacheAssetDispatchStageEventType::network_error;
    case PrecacheAssetDispatchStageState::idle:
    case PrecacheAssetDispatchStageState::waiting_for_precache_manifest:
    case PrecacheAssetDispatchStageState::selecting_world_entry:
    case PrecacheAssetDispatchStageState::opening_asset_source:
    case PrecacheAssetDispatchStageState::asset_source_ready:
    case PrecacheAssetDispatchStageState::probing_importers:
    case PrecacheAssetDispatchStageState::importing_asset:
    case PrecacheAssetDispatchStageState::protocol_error:
        return PrecacheAssetDispatchStageEventType::protocol_error;
    }
    return PrecacheAssetDispatchStageEventType::protocol_error;
}

[[nodiscard]] PrecacheAssetDispatchTraceClassification terminal_trace(
    const PrecacheAssetDispatchStageState state) noexcept
{
    switch (state) {
    case PrecacheAssetDispatchStageState::asset_imported:
        return PrecacheAssetDispatchTraceClassification::asset_imported;
    case PrecacheAssetDispatchStageState::importer_boundary_reached:
        return PrecacheAssetDispatchTraceClassification::
            importer_boundary_reached;
    case PrecacheAssetDispatchStageState::world_source_unavailable:
        return PrecacheAssetDispatchTraceClassification::
            world_source_unavailable;
    case PrecacheAssetDispatchStageState::source_open_failed:
        return PrecacheAssetDispatchTraceClassification::source_open_failed;
    case PrecacheAssetDispatchStageState::ambiguous_importer:
        return PrecacheAssetDispatchTraceClassification::ambiguous_importer;
    case PrecacheAssetDispatchStageState::import_failed:
        return PrecacheAssetDispatchTraceClassification::import_failed;
    case PrecacheAssetDispatchStageState::timed_out:
        return PrecacheAssetDispatchTraceClassification::stage_timed_out;
    case PrecacheAssetDispatchStageState::cancelled:
        return PrecacheAssetDispatchTraceClassification::stage_cancelled;
    case PrecacheAssetDispatchStageState::backpressure:
        return PrecacheAssetDispatchTraceClassification::backpressure;
    case PrecacheAssetDispatchStageState::network_error:
        return PrecacheAssetDispatchTraceClassification::network_failure;
    case PrecacheAssetDispatchStageState::idle:
    case PrecacheAssetDispatchStageState::waiting_for_precache_manifest:
    case PrecacheAssetDispatchStageState::selecting_world_entry:
    case PrecacheAssetDispatchStageState::opening_asset_source:
    case PrecacheAssetDispatchStageState::asset_source_ready:
    case PrecacheAssetDispatchStageState::probing_importers:
    case PrecacheAssetDispatchStageState::importing_asset:
    case PrecacheAssetDispatchStageState::protocol_error:
        return PrecacheAssetDispatchTraceClassification::protocol_failure;
    }
    return PrecacheAssetDispatchTraceClassification::protocol_failure;
}

[[nodiscard]] std::string bounded_importer_id(
    const std::string_view importer_id)
{
    const auto bounded = importer_id.substr(
        0U,
        (std::min)(importer_id.size(),
                   assets::kMaximumAssetDispatchImporterIdBytes));
    return std::string{bounded};
}

} // namespace

bool valid_precache_asset_dispatch_stage_configuration(
    const PrecacheAssetDispatchStageConfig& config) noexcept
{
    return valid_precache_manifest_stage_configuration(config.manifest) &&
           local_assets::valid_local_asset_source_open_limits(
               config.source_open) &&
           config.maximum_stage_events >=
               kMinimumPrecacheAssetDispatchEvents &&
           config.maximum_stage_events <=
               kMaximumPrecacheAssetDispatchEvents;
}

class ApprovedAssetDispatchState::Implementation final {
public:
    Implementation(
        PrecacheManifestState manifest,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        AssetDispatchPlan plan,
        ApprovedAssetSource source,
        assets::AssetDispatchResult dispatch_result) noexcept
        : manifest_{std::move(manifest)},
          environment_{std::move(environment)},
          plan_{std::move(plan)},
          source_{std::move(source)},
          dispatch_result_{std::move(dispatch_result)}
    {
    }

    PrecacheManifestState manifest_;
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment_;
    AssetDispatchPlan plan_;
    ApprovedAssetSource source_;
    assets::AssetDispatchResult dispatch_result_;
};

ApprovedAssetDispatchState::ApprovedAssetDispatchState(
    std::unique_ptr<Implementation> implementation) noexcept
    : implementation_{std::move(implementation)}
{
}

ApprovedAssetDispatchState::~ApprovedAssetDispatchState() = default;
ApprovedAssetDispatchState::ApprovedAssetDispatchState(
    ApprovedAssetDispatchState&&) noexcept = default;
ApprovedAssetDispatchState& ApprovedAssetDispatchState::operator=(
    ApprovedAssetDispatchState&&) noexcept = default;

const PrecacheManifestState& ApprovedAssetDispatchState::manifest()
    const noexcept
{
    return implementation_->manifest_;
}

const std::shared_ptr<const local_resources::LocalResourceEnvironment>&
ApprovedAssetDispatchState::environment() const noexcept
{
    return implementation_->environment_;
}

const AssetDispatchPlan& ApprovedAssetDispatchState::plan() const noexcept
{
    return implementation_->plan_;
}

const ApprovedAssetSource& ApprovedAssetDispatchState::source() const noexcept
{
    return implementation_->source_;
}

const assets::AssetDispatchResult&
ApprovedAssetDispatchState::dispatch_result() const noexcept
{
    return implementation_->dispatch_result_;
}

const std::optional<assets::ImportedAsset>&
ApprovedAssetDispatchState::imported_asset() const noexcept
{
    return implementation_->dispatch_result_.asset;
}

std::uint64_t ApprovedAssetDispatchState::source_byte_count() const noexcept
{
    return implementation_->source_.byte_count();
}

class PrecacheAssetDispatchStage::Implementation final {
public:
    Implementation(
        network::IDatagramTransport& transport,
        const network::NetworkAddress remote_endpoint,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        const assets::AssetImporterRegistries& importer_registries,
        PrecacheAssetDispatchStageConfig config,
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
        PrecacheManifestTraceCallback manifest_trace_callback,
        PrecacheAssetDispatchTraceCallback trace_callback)
        : config_{std::move(config)},
          environment_{std::move(environment)},
          dispatcher_{importer_registries},
          trace_callback_{std::move(trace_callback)},
          configuration_valid_{
              valid_precache_asset_dispatch_stage_configuration(config_) &&
              environment_ != nullptr && environment_->root_count() > 0U},
          manifest_stage_{
              transport,
              remote_endpoint,
              environment_,
              config_.manifest,
              consistency_provider,
              std::move(initial_trace_callback),
              std::move(pre_resource_trace_callback),
              std::move(delta_trace_callback),
              std::move(movement_trace_callback),
              std::move(user_info_trace_callback),
              std::move(transition_trace_callback),
              std::move(resource_list_trace_callback),
              std::move(response_trace_callback),
              std::move(manifest_trace_callback),
              PrecacheManifestStage::RetainConnectionAtBoundary{}},
          event_slots_(
              configuration_valid_ ? config_.maximum_stage_events : 0U)
    {
    }

    ~Implementation()
    {
        if (state_ != PrecacheAssetDispatchStageState::idle &&
            !cleanup_done_) {
            cleanup(last_update_.value_or(
                PrecacheAssetDispatchStageTimePoint{}));
        }
    }

    [[nodiscard]] bool start(
        const PrecacheAssetDispatchStageTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime)
    {
        if (trace_callback_active_ || importer_callback_active_ ||
            state_ != PrecacheAssetDispatchStageState::idle) {
            return false;
        }
        error_.reset();
        if (!configuration_valid_) {
            fail(
                PrecacheAssetDispatchStageErrorCode::invalid_configuration,
                PrecacheAssetDispatchStageState::protocol_error,
                "Asset-dispatch stage configuration is invalid",
                now);
            return false;
        }

        bool started = false;
        try {
            started = manifest_stage_.start(
                now,
                expected_local_endpoint,
                std::move(connection_lifetime));
        } catch (...) {
            fail(
                PrecacheAssetDispatchStageErrorCode::
                    manifest_stage_start_failed,
                PrecacheAssetDispatchStageState::protocol_error,
                "Nested precache-manifest stage threw during start",
                now);
            return false;
        }
        drain_manifest_events();
        if (!started) {
            fail_from_manifest(now, true);
            return false;
        }

        last_update_ = now;
        state_ =
            PrecacheAssetDispatchStageState::waiting_for_precache_manifest;
        emit_trace(PrecacheAssetDispatchTraceClassification::stage_started);
        return true;
    }

    void update(const PrecacheAssetDispatchStageTimePoint now)
    {
        if (trace_callback_active_ || importer_callback_active_ ||
            state_ == PrecacheAssetDispatchStageState::idle ||
            terminal_state(state_)) {
            return;
        }
        if (!last_update_ || now < *last_update_) {
            fail(
                PrecacheAssetDispatchStageErrorCode::time_moved_backwards,
                PrecacheAssetDispatchStageState::protocol_error,
                "Asset-dispatch stage update time moved backwards",
                now);
            return;
        }
        last_update_ = now;

        if (!post_manifest_transmit_unchanged()) {
            fail(
                PrecacheAssetDispatchStageErrorCode::
                    post_manifest_transmit_changed,
                PrecacheAssetDispatchStageState::protocol_error,
                "Retained transport transmitted after manifest publication",
                now);
            return;
        }

        switch (state_) {
        case PrecacheAssetDispatchStageState::waiting_for_precache_manifest:
            drain_manifest_events();
            try {
                manifest_stage_.update(now);
            } catch (...) {
                fail(
                    PrecacheAssetDispatchStageErrorCode::manifest_stage_failed,
                    PrecacheAssetDispatchStageState::protocol_error,
                    "Nested precache-manifest stage threw during update",
                    now);
                return;
            }
            drain_manifest_events();
            synchronize_from_manifest(now);
            return;
        case PrecacheAssetDispatchStageState::selecting_world_entry:
            select_world(now);
            return;
        case PrecacheAssetDispatchStageState::opening_asset_source:
            update_source_open(now);
            return;
        case PrecacheAssetDispatchStageState::asset_source_ready:
            state_ = PrecacheAssetDispatchStageState::probing_importers;
            return;
        case PrecacheAssetDispatchStageState::probing_importers:
            dispatch_asset(now);
            return;
        case PrecacheAssetDispatchStageState::importing_asset:
            fail(
                PrecacheAssetDispatchStageErrorCode::import_failed,
                PrecacheAssetDispatchStageState::protocol_error,
                "Asset dispatcher returned without a terminal result",
                now);
            return;
        case PrecacheAssetDispatchStageState::idle:
        case PrecacheAssetDispatchStageState::asset_imported:
        case PrecacheAssetDispatchStageState::importer_boundary_reached:
        case PrecacheAssetDispatchStageState::world_source_unavailable:
        case PrecacheAssetDispatchStageState::source_open_failed:
        case PrecacheAssetDispatchStageState::ambiguous_importer:
        case PrecacheAssetDispatchStageState::import_failed:
        case PrecacheAssetDispatchStageState::timed_out:
        case PrecacheAssetDispatchStageState::cancelled:
        case PrecacheAssetDispatchStageState::backpressure:
        case PrecacheAssetDispatchStageState::network_error:
        case PrecacheAssetDispatchStageState::protocol_error:
            return;
        }
    }

    void cancel(const PrecacheAssetDispatchStageTimePoint now) noexcept
    {
        if (trace_callback_active_ || importer_callback_active_ ||
            state_ == PrecacheAssetDispatchStageState::idle ||
            terminal_state(state_)) {
            return;
        }
        if (open_operation_) {
            open_operation_->cancel();
        }
        state_ = PrecacheAssetDispatchStageState::cancelled;
        result_.reset();
        dispatch_plan_.reset();
        approved_source_.reset();
        open_operation_.reset();
        error_.reset();
        PrecacheAssetDispatchStageEvent event;
        event.type = PrecacheAssetDispatchStageEventType::cancelled;
        event.occurred_at = now;
        if (can_push_events()) {
            push_event(event);
        }
        cleanup(now);
        emit_trace(
            PrecacheAssetDispatchTraceClassification::stage_cancelled,
            event);
    }

    [[nodiscard]] std::optional<PrecacheAssetDispatchStageEvent> poll_event()
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

    void push_event(const PrecacheAssetDispatchStageEvent& event) noexcept
    {
        const auto index = (event_head_ + event_size_) % event_slots_.size();
        try {
            event_slots_[index].emplace(event);
            ++event_size_;
        } catch (...) {
            event_slots_[index].reset();
        }
    }

    void drain_manifest_events() noexcept
    {
        while (manifest_stage_.poll_event()) {
        }
    }

    void synchronize_from_manifest(
        const PrecacheAssetDispatchStageTimePoint now)
    {
        if (manifest_stage_.result()) {
            if (manifest_stage_.retained_driver() == nullptr) {
                fail(
                    PrecacheAssetDispatchStageErrorCode::
                        retained_driver_missing,
                    PrecacheAssetDispatchStageState::protocol_error,
                    "Manifest publication lost its retained continuation",
                    now);
                return;
            }
            if (!can_push_events()) {
                fail_backpressure(
                    now,
                    "No bounded event slot remains for manifest publication");
                return;
            }
            transmitted_at_manifest_publication_ =
                manifest_stage_.transmitted_packet_count();
            state_ = PrecacheAssetDispatchStageState::selecting_world_entry;
            PrecacheAssetDispatchStageEvent event;
            event.type =
                PrecacheAssetDispatchStageEventType::precache_manifest_ready;
            if (const auto* world =
                    manifest_stage_.result()->manifest().world_entry()) {
                populate_entry_metadata(event, *world);
            }
            event.occurred_at = now;
            push_event(event);
            emit_trace(
                PrecacheAssetDispatchTraceClassification::
                    precache_manifest_ready,
                event);
            return;
        }
        if (manifest_stage_.terminal() || manifest_stage_.error()) {
            fail_from_manifest(now, false);
        }
    }

    void select_world(const PrecacheAssetDispatchStageTimePoint now)
    {
        if (!manifest_stage_.result() || !environment_) {
            fail(
                PrecacheAssetDispatchStageErrorCode::manifest_stage_failed,
                PrecacheAssetDispatchStageState::protocol_error,
                "Manifest prerequisite disappeared before world selection",
                now);
            return;
        }
        const auto& manifest = manifest_stage_.result()->manifest();
        const auto* world = manifest.world_entry();
        if (!manifest.world_geometry_ready() || world == nullptr ||
            world->readiness_status() !=
                LocalResourceReadinessStatus::ready_local_file ||
            !world->locator()) {
            fail(
                PrecacheAssetDispatchStageErrorCode::
                    world_source_unavailable,
                PrecacheAssetDispatchStageState::world_source_unavailable,
                "Selected world has no approved ready local source",
                now,
                PrecacheAssetDispatchStageEventType::world_source_unavailable,
                world);
            return;
        }

        auto built = AssetDispatchPlanBuilder{}.build(manifest, *world);
        if (!built || !built.plan || !built.plan->selected_world() ||
            built.plan->role() != assets::AssetDispatchRole::world) {
            fail(
                PrecacheAssetDispatchStageErrorCode::dispatch_plan_failed,
                PrecacheAssetDispatchStageState::protocol_error,
                "Exact selected world did not produce a world-only dispatch plan",
                now,
                PrecacheAssetDispatchStageEventType::protocol_error,
                world,
                std::nullopt,
                built.error ? std::optional{built.error->code}
                            : std::nullopt);
            return;
        }
        if (!can_push_events()) {
            fail_backpressure(
                now,
                "No bounded event slot remains for selected-world metadata");
            return;
        }
        dispatch_plan_.emplace(std::move(*built.plan));
        state_ = PrecacheAssetDispatchStageState::opening_asset_source;
        PrecacheAssetDispatchStageEvent event;
        event.type =
            PrecacheAssetDispatchStageEventType::world_entry_selected;
        populate_entry_metadata(event, *world);
        event.role = dispatch_plan_->role();
        event.occurred_at = now;
        push_event(event);
        emit_trace(
            PrecacheAssetDispatchTraceClassification::world_entry_selected,
            event);
    }

    void update_source_open(const PrecacheAssetDispatchStageTimePoint now)
    {
        const auto* world = selected_world_entry();
        if (world == nullptr || !dispatch_plan_ || !environment_) {
            fail(
                PrecacheAssetDispatchStageErrorCode::source_open_begin_failed,
                PrecacheAssetDispatchStageState::protocol_error,
                "Approved source opening lost its selected-world prerequisites",
                now);
            return;
        }

        if (!open_operation_) {
            if (!can_push_events()) {
                fail_backpressure(
                    now,
                    "No bounded event slot remains for source-open start");
                return;
            }
            ++source_open_attempt_count_;
            std::optional<ApprovedAssetSourceOpenBeginResult> begun;
            try {
                begun.emplace(opener_.begin(
                    *dispatch_plan_,
                    environment_,
                    config_.source_open));
            } catch (...) {
                fail(
                    PrecacheAssetDispatchStageErrorCode::
                        source_open_begin_failed,
                    PrecacheAssetDispatchStageState::source_open_failed,
                    "Approved source opener threw while beginning",
                    now,
                    PrecacheAssetDispatchStageEventType::source_open_failed,
                    world);
                return;
            }
            if (!*begun || !begun->operation) {
                fail_source_open(
                    now,
                    begun->error ? &*begun->error : nullptr,
                    true);
                return;
            }
            open_operation_.emplace(std::move(*begun->operation));
            last_progress_bytes_ = 0U;
            PrecacheAssetDispatchStageEvent event;
            event.type = PrecacheAssetDispatchStageEventType::
                asset_source_open_started;
            populate_entry_metadata(event, *world);
            event.role = dispatch_plan_->role();
            event.byte_count = world->local_file_size().value_or(0U);
            event.occurred_at = now;
            push_event(event);
            emit_trace(
                PrecacheAssetDispatchTraceClassification::
                    asset_source_open_started,
                event);
            return;
        }

        open_operation_->update(now);
        const auto progress = open_operation_->progress_bytes();
        if (progress != last_progress_bytes_) {
            if (!can_push_events()) {
                open_operation_->cancel();
                fail_backpressure(
                    now,
                    "No bounded event slot remains for source-read progress");
                return;
            }
            last_progress_bytes_ = progress;
            PrecacheAssetDispatchStageEvent event;
            event.type =
                PrecacheAssetDispatchStageEventType::asset_source_progress;
            populate_entry_metadata(event, *world);
            event.role = dispatch_plan_->role();
            event.byte_count = world->local_file_size().value_or(0U);
            event.progress_bytes = progress;
            event.occurred_at = now;
            push_event(event);
            emit_trace(
                PrecacheAssetDispatchTraceClassification::
                    asset_source_progress,
                event);
        }

        switch (open_operation_->state()) {
        case ApprovedAssetSourceOpenState::opening:
        case ApprovedAssetSourceOpenState::reading:
        case ApprovedAssetSourceOpenState::validating:
            return;
        case ApprovedAssetSourceOpenState::source_ready: {
            if (!can_push_events()) {
                fail_backpressure(
                    now,
                    "No bounded event slot remains for approved source publication");
                return;
            }
            auto source = open_operation_->take_result();
            if (!source) {
                fail_source_open(now, open_operation_->error(), false);
                return;
            }
            const auto byte_count = source->byte_count();
            approved_source_.emplace(std::move(*source));
            open_operation_.reset();
            state_ = PrecacheAssetDispatchStageState::asset_source_ready;
            PrecacheAssetDispatchStageEvent event;
            event.type =
                PrecacheAssetDispatchStageEventType::asset_source_ready;
            populate_entry_metadata(event, *world);
            event.role = dispatch_plan_->role();
            event.byte_count = byte_count;
            event.progress_bytes = byte_count;
            event.occurred_at = now;
            push_event(event);
            emit_trace(
                PrecacheAssetDispatchTraceClassification::asset_source_ready,
                event);
            return;
        }
        case ApprovedAssetSourceOpenState::cancelled:
        case ApprovedAssetSourceOpenState::timed_out:
        case ApprovedAssetSourceOpenState::failed:
            fail_source_open(now, open_operation_->error(), false);
            return;
        case ApprovedAssetSourceOpenState::idle:
            fail_source_open(now, open_operation_->error(), false);
            return;
        }
    }

    void dispatch_asset(const PrecacheAssetDispatchStageTimePoint now)
    {
        const auto* world = selected_world_entry();
        if (world == nullptr || !dispatch_plan_ || !approved_source_) {
            fail(
                PrecacheAssetDispatchStageErrorCode::import_failed,
                PrecacheAssetDispatchStageState::protocol_error,
                "Importer dispatch lost its approved source prerequisites",
                now);
            return;
        }
        if (!can_push_events(3U)) {
            fail_backpressure(
                now,
                "Insufficient bounded event capacity for importer dispatch");
            return;
        }
        if (!post_manifest_transmit_unchanged()) {
            fail(
                PrecacheAssetDispatchStageErrorCode::
                    post_manifest_transmit_changed,
                PrecacheAssetDispatchStageState::protocol_error,
                "Retained transport changed before importer dispatch",
                now);
            return;
        }

        state_ = PrecacheAssetDispatchStageState::importing_asset;
        ++importer_dispatch_count_;
        assets::AssetDispatchResult dispatched;
        importer_callback_active_ = true;
        try {
            dispatched = dispatcher_.dispatch(
                *approved_source_, *dispatch_plan_);
        } catch (...) {
            importer_callback_active_ = false;
            fail(
                PrecacheAssetDispatchStageErrorCode::import_failed,
                PrecacheAssetDispatchStageState::import_failed,
                "Asset importer dispatcher threw",
                now,
                PrecacheAssetDispatchStageEventType::import_failed,
                world);
            return;
        }
        importer_callback_active_ = false;

        if (state_ != PrecacheAssetDispatchStageState::importing_asset ||
            !dispatch_plan_ || !approved_source_ ||
            selected_world_entry() != world) {
            fail(
                PrecacheAssetDispatchStageErrorCode::import_failed,
                PrecacheAssetDispatchStageState::protocol_error,
                "Importer callback changed retained dispatch prerequisites",
                now);
            return;
        }

        try {
            PrecacheAssetDispatchStageEvent probe_event;
            probe_event.type = PrecacheAssetDispatchStageEventType::
                importer_probe_completed;
            populate_entry_metadata(probe_event, *world);
            probe_event.role = dispatch_plan_->role();
            probe_event.byte_count = approved_source_->byte_count();
            probe_event.importer_category = dispatched.selected_category;
            probe_event.dispatch_state = dispatched.state;
            if (!dispatched.top_candidates.empty()) {
                probe_event.importer_id = bounded_importer_id(
                    dispatched.top_candidates.front().importer_id);
            }
            probe_event.occurred_at = now;
            push_event(probe_event);
            emit_trace(
                PrecacheAssetDispatchTraceClassification::
                    importer_probe_completed,
                probe_event);

            if (!dispatched.selected_importer_id.empty()) {
                PrecacheAssetDispatchStageEvent selected_event = probe_event;
                selected_event.type =
                    PrecacheAssetDispatchStageEventType::importer_selected;
                selected_event.importer_id = bounded_importer_id(
                    dispatched.selected_importer_id);
                push_event(selected_event);
                emit_trace(
                    PrecacheAssetDispatchTraceClassification::
                        importer_selected,
                    selected_event);
            }

            switch (dispatched.state) {
        case assets::AssetDispatchState::imported:
            if (!dispatched.imported()) {
                fail_dispatch_result(
                    now,
                    std::move(dispatched),
                    PrecacheAssetDispatchStageState::import_failed,
                    PrecacheAssetDispatchStageErrorCode::import_failed,
                    "Asset dispatcher reported import success without an asset");
                return;
            }
            publish_dispatch_result(
                now,
                std::move(dispatched),
                PrecacheAssetDispatchStageState::asset_imported,
                PrecacheAssetDispatchStageEventType::asset_imported,
                PrecacheAssetDispatchTraceClassification::asset_imported);
            return;
        case assets::AssetDispatchState::importer_not_registered:
            publish_dispatch_result(
                now,
                std::move(dispatched),
                PrecacheAssetDispatchStageState::importer_boundary_reached,
                PrecacheAssetDispatchStageEventType::
                    importer_boundary_reached,
                PrecacheAssetDispatchTraceClassification::
                    importer_boundary_reached);
            return;
        case assets::AssetDispatchState::ambiguous_importer:
            fail_dispatch_result(
                now,
                std::move(dispatched),
                PrecacheAssetDispatchStageState::ambiguous_importer,
                PrecacheAssetDispatchStageErrorCode::ambiguous_importer,
                "World importer selection is ambiguous");
            return;
        case assets::AssetDispatchState::unsupported_asset_role:
        case assets::AssetDispatchState::metadata_only_resource:
        case assets::AssetDispatchState::source_invalid:
        case assets::AssetDispatchState::import_failed:
            fail_dispatch_result(
                now,
                std::move(dispatched),
                PrecacheAssetDispatchStageState::import_failed,
                PrecacheAssetDispatchStageErrorCode::import_failed,
                "Selected world importer dispatch failed");
            return;
        }
        } catch (...) {
            fail(
                PrecacheAssetDispatchStageErrorCode::unable_to_retain_result,
                PrecacheAssetDispatchStageState::protocol_error,
                "Unable to retain importer dispatch metadata",
                now);
        }
    }

    void publish_dispatch_result(
        const PrecacheAssetDispatchStageTimePoint now,
        assets::AssetDispatchResult dispatched,
        const PrecacheAssetDispatchStageState terminal,
        const PrecacheAssetDispatchStageEventType event_type,
        const PrecacheAssetDispatchTraceClassification trace)
    {
        const auto* world = selected_world_entry();
        if (world == nullptr || !manifest_stage_.result() ||
            !dispatch_plan_ || !approved_source_ ||
            !post_manifest_transmit_unchanged()) {
            fail(
                PrecacheAssetDispatchStageErrorCode::unable_to_retain_result,
                PrecacheAssetDispatchStageState::protocol_error,
                "Dispatch publication lost an immutable prerequisite",
                now);
            return;
        }

        const auto dispatch_state = dispatched.state;
        const auto selected_category = dispatched.selected_category;
        const auto byte_count = approved_source_->byte_count();
        std::string selected_importer_id;
        try {
            selected_importer_id =
                bounded_importer_id(dispatched.selected_importer_id);
            auto implementation =
                std::make_unique<ApprovedAssetDispatchState::Implementation>(
                    manifest_stage_.result()->manifest(),
                    environment_,
                    *dispatch_plan_,
                    std::move(*approved_source_),
                    std::move(dispatched));
            result_.emplace(
                ApprovedAssetDispatchState{std::move(implementation)});
        } catch (...) {
            fail(
                PrecacheAssetDispatchStageErrorCode::unable_to_retain_result,
                PrecacheAssetDispatchStageState::protocol_error,
                "Unable to retain the owning asset-dispatch publication",
                now);
            return;
        }

        dispatch_plan_.reset();
        approved_source_.reset();
        open_operation_.reset();
        state_ = terminal;
        PrecacheAssetDispatchStageEvent event;
        event.type = event_type;
        populate_entry_metadata(event, *world);
        event.role = assets::AssetDispatchRole::world;
        event.byte_count = byte_count;
        event.progress_bytes = byte_count;
        event.importer_category = selected_category;
        event.importer_id = std::move(selected_importer_id);
        event.dispatch_state = dispatch_state;
        event.occurred_at = now;
        push_event(event);
        cleanup(now);
        emit_trace(trace, event);
    }

    void fail_dispatch_result(
        const PrecacheAssetDispatchStageTimePoint now,
        assets::AssetDispatchResult dispatched,
        const PrecacheAssetDispatchStageState state,
        const PrecacheAssetDispatchStageErrorCode code,
        const std::string_view context) noexcept
    {
        const auto* world = selected_world_entry();
        const auto dispatch_state = dispatched.state;
        const auto asset_code =
            dispatched.error
                ? std::optional{dispatched.error->code}
                : std::nullopt;
        std::string importer_id;
        try {
            importer_id = bounded_importer_id(
                dispatched.selected_importer_id);
        } catch (...) {
        }
        const auto category = dispatched.selected_category;
        fail(
            code,
            state,
            context,
            now,
            terminal_event(state),
            world,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            dispatch_state,
            asset_code,
            category,
            importer_id);
    }

    void fail_source_open(
        const PrecacheAssetDispatchStageTimePoint now,
        const ApprovedAssetSourceOpenError* error,
        const bool begin_failure) noexcept
    {
        auto state = PrecacheAssetDispatchStageState::source_open_failed;
        if (error &&
            error->code == ApprovedAssetSourceOpenErrorCode::timed_out) {
            state = PrecacheAssetDispatchStageState::timed_out;
        } else if (error &&
                   error->code ==
                       ApprovedAssetSourceOpenErrorCode::cancelled) {
            state = PrecacheAssetDispatchStageState::cancelled;
        }
        fail(
            begin_failure
                ? PrecacheAssetDispatchStageErrorCode::source_open_begin_failed
                : PrecacheAssetDispatchStageErrorCode::source_open_failed,
            state,
            begin_failure ? "Approved source opening could not begin"
                          : "Approved source opening failed",
            now,
            terminal_event(state),
            selected_world_entry(),
            std::nullopt,
            std::nullopt,
            error ? error->approved_source_code : std::nullopt);
        if (error != nullptr && error_) {
            error_->source_open_code = error->code;
            error_->local_source_open_code = error->local_source_code;
            error_->locator_reopen_code = error->locator_reopen_code;
            error_->read_code = error->read_code;
        }
    }

    void fail_from_manifest(
        const PrecacheAssetDispatchStageTimePoint now,
        const bool start_failure) noexcept
    {
        const auto& nested_error = manifest_stage_.error();
        fail(
            start_failure
                ? PrecacheAssetDispatchStageErrorCode::
                      manifest_stage_start_failed
                : PrecacheAssetDispatchStageErrorCode::manifest_stage_failed,
            manifest_failure_state(manifest_stage_.state()),
            start_failure ? "Nested precache-manifest stage could not start"
                          : "Nested precache-manifest stage failed",
            now,
            terminal_event(manifest_failure_state(manifest_stage_.state())),
            nullptr,
            nested_error ? std::optional{nested_error->code} : std::nullopt);
    }

    void fail_backpressure(
        const PrecacheAssetDispatchStageTimePoint now,
        const std::string_view context) noexcept
    {
        fail(
            PrecacheAssetDispatchStageErrorCode::event_backpressure,
            PrecacheAssetDispatchStageState::backpressure,
            context,
            now,
            PrecacheAssetDispatchStageEventType::backpressure,
            selected_world_entry());
    }

    void fail(
        const PrecacheAssetDispatchStageErrorCode code,
        const PrecacheAssetDispatchStageState state,
        const std::string_view context,
        const PrecacheAssetDispatchStageTimePoint now,
        const PrecacheAssetDispatchStageEventType event_type =
            PrecacheAssetDispatchStageEventType::protocol_error,
        const PrecacheManifestEntry* entry = nullptr,
        const std::optional<PrecacheManifestStageErrorCode> manifest_code =
            std::nullopt,
        const std::optional<AssetDispatchPlanErrorCode> plan_code =
            std::nullopt,
        const std::optional<ApprovedAssetSourceErrorCode>
            approved_source_code = std::nullopt,
        const std::optional<assets::AssetDispatchState> dispatch_state =
            std::nullopt,
        const std::optional<assets::AssetErrorCode> asset_code = std::nullopt,
        const assets::AssetImporterCategory importer_category =
            assets::AssetImporterCategory::none,
        const std::string_view importer_id = {}) noexcept
    {
        if (terminal_state(state_)) {
            return;
        }
        if (open_operation_) {
            open_operation_->cancel();
        }
        const auto role = dispatch_plan_
                              ? dispatch_plan_->role()
                              : assets::AssetDispatchRole::unsupported;
        state_ = state;
        result_.reset();
        dispatch_plan_.reset();
        approved_source_.reset();
        open_operation_.reset();
        error_.reset();
        try {
            error_.emplace();
            error_->code = code;
            error_->manifest_code = manifest_code;
            error_->plan_code = plan_code;
            error_->approved_source_code = approved_source_code;
            error_->dispatch_state = dispatch_state;
            error_->asset_code = asset_code;
            const auto bounded = context.substr(
                0U,
                (std::min)(context.size(),
                           kPrecacheAssetDispatchStageDiagnosticTextLimit));
            error_->context.assign(bounded.data(), bounded.size());
        } catch (...) {
        }

        PrecacheAssetDispatchStageEvent event;
        event.type = event_type;
        if (entry != nullptr) {
            populate_entry_metadata(event, *entry);
        }
        event.role = role;
        event.importer_category = importer_category;
        try {
            event.importer_id = bounded_importer_id(importer_id);
        } catch (...) {
        }
        event.dispatch_state = dispatch_state;
        event.occurred_at = now;
        if (can_push_events()) {
            push_event(event);
        }
        cleanup(now);
        emit_trace(terminal_trace(state), event);
    }

    void cleanup(const PrecacheAssetDispatchStageTimePoint now) noexcept
    {
        if (cleanup_done_) {
            return;
        }
        cleanup_done_ = true;
        if (open_operation_) {
            open_operation_->cancel();
        }
        if (!manifest_stage_.terminal()) {
            try {
                manifest_stage_.cancel(now);
            } catch (...) {
            }
        }
        manifest_stage_.finalize_retained_boundary(now);
    }

    [[nodiscard]] bool post_manifest_transmit_unchanged() const noexcept
    {
        return !transmitted_at_manifest_publication_ ||
               manifest_stage_.transmitted_packet_count() ==
                   *transmitted_at_manifest_publication_;
    }

    [[nodiscard]] const PrecacheManifestEntry* selected_world_entry()
        const noexcept
    {
        return manifest_stage_.result()
                   ? manifest_stage_.result()->manifest().world_entry()
                   : nullptr;
    }

    static void populate_entry_metadata(
        PrecacheAssetDispatchStageEvent& event,
        const PrecacheManifestEntry& entry) noexcept
    {
        event.resource_type = entry.resource_type();
        event.resource_index = entry.resource_index();
        event.wire_ordinal = entry.wire_ordinal();
        event.byte_count = entry.local_file_size().value_or(0U);
    }

    void emit_trace(
        const PrecacheAssetDispatchTraceClassification classification,
        const PrecacheAssetDispatchStageEvent& metadata = {}) noexcept
    {
        if (!trace_callback_ || trace_callback_active_) {
            return;
        }
        PrecacheAssetDispatchTraceEvent event;
        event.classification = classification;
        event.state = state_;
        event.endpoint = manifest_stage_.remote_endpoint();
        event.resource_type = metadata.resource_type;
        event.resource_index = metadata.resource_index;
        event.wire_ordinal = metadata.wire_ordinal;
        event.role = metadata.role;
        event.byte_count = metadata.byte_count;
        event.progress_bytes = metadata.progress_bytes;
        event.importer_category = metadata.importer_category;
        try {
            event.importer_id = bounded_importer_id(metadata.importer_id);
        } catch (...) {
        }
        event.dispatch_state = metadata.dispatch_state;
        event.transmitted_packet_count =
            manifest_stage_.transmitted_packet_count();
        trace_callback_active_ = true;
        try {
            trace_callback_(event);
        } catch (...) {
        }
        trace_callback_active_ = false;
    }

    PrecacheAssetDispatchStageConfig config_;
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment_;
    ApprovedAssetSourceOpener opener_;
    ApprovedAssetImporterDispatcher dispatcher_;
    PrecacheAssetDispatchTraceCallback trace_callback_;
    bool trace_callback_active_{false};
    bool importer_callback_active_{false};
    bool configuration_valid_{false};
    PrecacheManifestStage manifest_stage_;
    std::vector<std::optional<PrecacheAssetDispatchStageEvent>> event_slots_;
    std::size_t event_head_{0U};
    std::size_t event_size_{0U};
    PrecacheAssetDispatchStageState state_{
        PrecacheAssetDispatchStageState::idle};
    std::optional<ApprovedAssetDispatchState> result_;
    std::optional<PrecacheAssetDispatchStageError> error_;
    std::optional<AssetDispatchPlan> dispatch_plan_;
    std::optional<ApprovedAssetSourceOpenOperation> open_operation_;
    std::optional<ApprovedAssetSource> approved_source_;
    std::optional<PrecacheAssetDispatchStageTimePoint> last_update_;
    std::optional<std::size_t> transmitted_at_manifest_publication_;
    std::uint64_t last_progress_bytes_{0U};
    std::size_t source_open_attempt_count_{0U};
    std::size_t importer_dispatch_count_{0U};
    bool cleanup_done_{false};
};

PrecacheAssetDispatchStage::PrecacheAssetDispatchStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment,
    const assets::AssetImporterRegistries& importer_registries,
    PrecacheAssetDispatchStageConfig config,
    resource_consistency::IResourceConsistencyProvider* consistency_provider,
    InitialSignonTraceCallback initial_trace_callback,
    PreResourceSignonTraceCallback pre_resource_trace_callback,
    DeltaDescriptionTraceCallback delta_trace_callback,
    MovementEnvironmentTraceCallback movement_trace_callback,
    UserInfoSignonTraceCallback user_info_trace_callback,
    ResourceTransitionTraceCallback transition_trace_callback,
    ResourceListTraceCallback resource_list_trace_callback,
    ResourceClientResponseTraceCallback response_trace_callback,
    PrecacheManifestTraceCallback manifest_trace_callback,
    PrecacheAssetDispatchTraceCallback trace_callback)
    : implementation_{std::make_unique<Implementation>(
          transport,
          remote_endpoint,
          std::move(environment),
          importer_registries,
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
          std::move(manifest_trace_callback),
          std::move(trace_callback))}
{
}

PrecacheAssetDispatchStage::~PrecacheAssetDispatchStage() = default;

bool PrecacheAssetDispatchStage::start(
    const PrecacheAssetDispatchStageTimePoint now,
    const network::NetworkAddress& expected_local_endpoint,
    std::unique_ptr<INetchanDriverLifetime> connection_lifetime)
{
    return implementation_->start(
        now, expected_local_endpoint, std::move(connection_lifetime));
}

void PrecacheAssetDispatchStage::update(
    const PrecacheAssetDispatchStageTimePoint now)
{
    implementation_->update(now);
}

void PrecacheAssetDispatchStage::cancel(
    const PrecacheAssetDispatchStageTimePoint now)
{
    implementation_->cancel(now);
}

std::optional<PrecacheAssetDispatchStageEvent>
PrecacheAssetDispatchStage::poll_event()
{
    return implementation_->poll_event();
}

PrecacheAssetDispatchStageState PrecacheAssetDispatchStage::state()
    const noexcept
{
    return implementation_->state_;
}

bool PrecacheAssetDispatchStage::terminal() const noexcept
{
    return terminal_state(implementation_->state_);
}

const std::optional<ApprovedAssetDispatchState>&
PrecacheAssetDispatchStage::result() const noexcept
{
    return implementation_->result_;
}

const std::optional<PrecacheAssetDispatchStageError>&
PrecacheAssetDispatchStage::error() const noexcept
{
    return implementation_->error_;
}

const network::NetworkAddress& PrecacheAssetDispatchStage::remote_endpoint()
    const noexcept
{
    return implementation_->manifest_stage_.remote_endpoint();
}

const std::optional<network::NetworkAddress>&
PrecacheAssetDispatchStage::local_endpoint() const noexcept
{
    return implementation_->manifest_stage_.local_endpoint();
}

std::size_t PrecacheAssetDispatchStage::pending_event_count() const noexcept
{
    return implementation_->event_size_;
}

std::size_t PrecacheAssetDispatchStage::transmitted_packet_count()
    const noexcept
{
    return implementation_->manifest_stage_.transmitted_packet_count();
}

std::optional<std::size_t>
PrecacheAssetDispatchStage::transmitted_packet_count_at_manifest_publication()
    const noexcept
{
    return implementation_->transmitted_at_manifest_publication_;
}

std::size_t PrecacheAssetDispatchStage::cleanup_count() const noexcept
{
    return implementation_->manifest_stage_.cleanup_count();
}

std::size_t PrecacheAssetDispatchStage::manifest_publication_count()
    const noexcept
{
    return implementation_->manifest_stage_.manifest_publication_count();
}

std::size_t PrecacheAssetDispatchStage::source_open_attempt_count()
    const noexcept
{
    return implementation_->source_open_attempt_count_;
}

std::size_t PrecacheAssetDispatchStage::importer_dispatch_count()
    const noexcept
{
    return implementation_->importer_dispatch_count_;
}

} // namespace hlclient::goldsrc
