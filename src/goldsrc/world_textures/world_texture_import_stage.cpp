#include <hlclient/goldsrc/world_textures/world_texture_import_stage.hpp>

#include <algorithm>
#include <new>
#include <utility>
#include <variant>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool terminal_state(
    const WorldTextureImportStageState state) noexcept
{
    switch (state) {
    case WorldTextureImportStageState::world_textures_ready:
    case WorldTextureImportStageState::world_textures_incomplete:
    case WorldTextureImportStageState::world_geometry_unavailable:
    case WorldTextureImportStageState::worldspawn_parse_failed:
    case WorldTextureImportStageState::wad_reference_invalid:
    case WorldTextureImportStageState::wad_source_unavailable:
    case WorldTextureImportStageState::wad_source_open_failed:
    case WorldTextureImportStageState::wad_catalog_failed:
    case WorldTextureImportStageState::texture_decode_failed:
    case WorldTextureImportStageState::timed_out:
    case WorldTextureImportStageState::cancelled:
    case WorldTextureImportStageState::backpressure:
    case WorldTextureImportStageState::network_error:
    case WorldTextureImportStageState::protocol_error:
        return true;
    case WorldTextureImportStageState::idle:
    case WorldTextureImportStageState::waiting_for_world_geometry:
    case WorldTextureImportStageState::parsing_texture_sources:
    case WorldTextureImportStageState::decoding_embedded_textures:
    case WorldTextureImportStageState::resolving_wad_archives:
    case WorldTextureImportStageState::decoding_external_textures:
    case WorldTextureImportStageState::building_texture_set:
        return false;
    }
    return true;
}

[[nodiscard]] WorldTextureImportStageState map_operation_state(
    const WorldTextureImportState state) noexcept
{
    switch (state) {
    case WorldTextureImportState::idle:
    case WorldTextureImportState::parsing_bsp_texture_sources:
        return WorldTextureImportStageState::parsing_texture_sources;
    case WorldTextureImportState::decoding_embedded_textures:
        return WorldTextureImportStageState::decoding_embedded_textures;
    case WorldTextureImportState::parsing_worldspawn:
    case WorldTextureImportState::resolving_wad_references:
    case WorldTextureImportState::opening_wad:
    case WorldTextureImportState::parsing_wad_catalog:
        return WorldTextureImportStageState::resolving_wad_archives;
    case WorldTextureImportState::resolving_external_textures:
        return WorldTextureImportStageState::decoding_external_textures;
    case WorldTextureImportState::building_texture_set:
        return WorldTextureImportStageState::building_texture_set;
    case WorldTextureImportState::textures_ready:
        return WorldTextureImportStageState::world_textures_ready;
    case WorldTextureImportState::textures_incomplete:
        return WorldTextureImportStageState::world_textures_incomplete;
    case WorldTextureImportState::cancelled:
        return WorldTextureImportStageState::cancelled;
    case WorldTextureImportState::timed_out:
        return WorldTextureImportStageState::timed_out;
    case WorldTextureImportState::failed:
        return WorldTextureImportStageState::texture_decode_failed;
    }
    return WorldTextureImportStageState::protocol_error;
}

[[nodiscard]] WorldTextureImportStageState map_operation_error_state(
    const WorldTextureImportErrorCode code) noexcept
{
    switch (code) {
    case WorldTextureImportErrorCode::worldspawn_parse_failed:
        return WorldTextureImportStageState::worldspawn_parse_failed;
    case WorldTextureImportErrorCode::wad_reference_invalid:
        return WorldTextureImportStageState::wad_reference_invalid;
    case WorldTextureImportErrorCode::wad_source_resolution_failed:
        return WorldTextureImportStageState::wad_source_unavailable;
    case WorldTextureImportErrorCode::wad_source_open_failed:
        return WorldTextureImportStageState::wad_source_open_failed;
    case WorldTextureImportErrorCode::wad_catalog_failed:
        return WorldTextureImportStageState::wad_catalog_failed;
    case WorldTextureImportErrorCode::embedded_texture_decode_failed:
    case WorldTextureImportErrorCode::wad_texture_decode_failed:
    case WorldTextureImportErrorCode::bsp_texture_source_parse_failed:
        return WorldTextureImportStageState::texture_decode_failed;
    case WorldTextureImportErrorCode::timed_out:
        return WorldTextureImportStageState::timed_out;
    case WorldTextureImportErrorCode::cancelled:
        return WorldTextureImportStageState::cancelled;
    case WorldTextureImportErrorCode::bsp_source_missing:
    case WorldTextureImportErrorCode::invalid_world_asset:
        return WorldTextureImportStageState::world_geometry_unavailable;
    case WorldTextureImportErrorCode::invalid_configuration:
    case WorldTextureImportErrorCode::texture_set_build_failed:
    case WorldTextureImportErrorCode::time_moved_backwards:
    case WorldTextureImportErrorCode::unable_to_retain_state:
        return WorldTextureImportStageState::protocol_error;
    }
    return WorldTextureImportStageState::protocol_error;
}

[[nodiscard]] WorldTextureImportStageEventType terminal_event_type(
    const WorldTextureImportStageState state) noexcept
{
    switch (state) {
    case WorldTextureImportStageState::world_textures_ready:
        return WorldTextureImportStageEventType::world_textures_ready;
    case WorldTextureImportStageState::world_textures_incomplete:
        return WorldTextureImportStageEventType::world_textures_incomplete;
    case WorldTextureImportStageState::world_geometry_unavailable:
        return WorldTextureImportStageEventType::world_geometry_unavailable;
    case WorldTextureImportStageState::worldspawn_parse_failed:
        return WorldTextureImportStageEventType::worldspawn_parse_failed;
    case WorldTextureImportStageState::wad_reference_invalid:
        return WorldTextureImportStageEventType::wad_reference_invalid;
    case WorldTextureImportStageState::wad_source_unavailable:
        return WorldTextureImportStageEventType::wad_source_unavailable;
    case WorldTextureImportStageState::wad_source_open_failed:
        return WorldTextureImportStageEventType::wad_source_open_failed;
    case WorldTextureImportStageState::wad_catalog_failed:
        return WorldTextureImportStageEventType::wad_catalog_failed;
    case WorldTextureImportStageState::texture_decode_failed:
        return WorldTextureImportStageEventType::texture_decode_failed;
    case WorldTextureImportStageState::timed_out:
        return WorldTextureImportStageEventType::timeout;
    case WorldTextureImportStageState::cancelled:
        return WorldTextureImportStageEventType::cancelled;
    case WorldTextureImportStageState::backpressure:
        return WorldTextureImportStageEventType::backpressure;
    case WorldTextureImportStageState::network_error:
        return WorldTextureImportStageEventType::network_error;
    case WorldTextureImportStageState::idle:
    case WorldTextureImportStageState::waiting_for_world_geometry:
    case WorldTextureImportStageState::parsing_texture_sources:
    case WorldTextureImportStageState::decoding_embedded_textures:
    case WorldTextureImportStageState::resolving_wad_archives:
    case WorldTextureImportStageState::decoding_external_textures:
    case WorldTextureImportStageState::building_texture_set:
    case WorldTextureImportStageState::protocol_error:
        return WorldTextureImportStageEventType::protocol_error;
    }
    return WorldTextureImportStageEventType::protocol_error;
}

[[nodiscard]] WorldTextureImportTraceClassification terminal_trace(
    const WorldTextureImportStageState state) noexcept
{
    switch (state) {
    case WorldTextureImportStageState::world_textures_ready:
        return WorldTextureImportTraceClassification::world_textures_ready;
    case WorldTextureImportStageState::world_textures_incomplete:
        return WorldTextureImportTraceClassification::world_textures_incomplete;
    case WorldTextureImportStageState::world_geometry_unavailable:
        return WorldTextureImportTraceClassification::world_geometry_unavailable;
    case WorldTextureImportStageState::worldspawn_parse_failed:
        return WorldTextureImportTraceClassification::worldspawn_parse_failed;
    case WorldTextureImportStageState::wad_reference_invalid:
        return WorldTextureImportTraceClassification::wad_reference_invalid;
    case WorldTextureImportStageState::wad_source_unavailable:
        return WorldTextureImportTraceClassification::wad_source_unavailable;
    case WorldTextureImportStageState::wad_source_open_failed:
        return WorldTextureImportTraceClassification::wad_source_open_failed;
    case WorldTextureImportStageState::wad_catalog_failed:
        return WorldTextureImportTraceClassification::wad_catalog_failed;
    case WorldTextureImportStageState::texture_decode_failed:
        return WorldTextureImportTraceClassification::texture_decode_failed;
    case WorldTextureImportStageState::timed_out:
        return WorldTextureImportTraceClassification::stage_timed_out;
    case WorldTextureImportStageState::cancelled:
        return WorldTextureImportTraceClassification::stage_cancelled;
    case WorldTextureImportStageState::backpressure:
        return WorldTextureImportTraceClassification::backpressure;
    case WorldTextureImportStageState::network_error:
        return WorldTextureImportTraceClassification::network_failure;
    case WorldTextureImportStageState::idle:
    case WorldTextureImportStageState::waiting_for_world_geometry:
    case WorldTextureImportStageState::parsing_texture_sources:
    case WorldTextureImportStageState::decoding_embedded_textures:
    case WorldTextureImportStageState::resolving_wad_archives:
    case WorldTextureImportStageState::decoding_external_textures:
    case WorldTextureImportStageState::building_texture_set:
    case WorldTextureImportStageState::protocol_error:
        return WorldTextureImportTraceClassification::protocol_failure;
    }
    return WorldTextureImportTraceClassification::protocol_failure;
}

} // namespace

bool valid_world_texture_import_stage_configuration(
    const WorldTextureImportStageConfig& config) noexcept
{
    return valid_precache_asset_dispatch_stage_configuration(
               config.asset_dispatch) &&
        valid_goldsrc_world_texture_import_limits(config.texture_import) &&
        config.maximum_stage_events >=
            kMinimumWorldTextureImportStageEvents &&
        config.maximum_stage_events <=
            kMaximumWorldTextureImportStageEvents;
}

class TexturedWorldAssetState::Implementation final {
public:
    Implementation(
        ApprovedAssetDispatchState dispatch_state,
        assets::WorldTextureSet textures) noexcept
        : dispatch_state_{std::move(dispatch_state)},
          textures_{std::move(textures)}
    {
    }

    ApprovedAssetDispatchState dispatch_state_;
    assets::WorldTextureSet textures_;
};

TexturedWorldAssetState::TexturedWorldAssetState(
    std::unique_ptr<Implementation> implementation) noexcept
    : implementation_{std::move(implementation)}
{
}

TexturedWorldAssetState::TexturedWorldAssetState(
    TexturedWorldAssetState&&) noexcept = default;
TexturedWorldAssetState& TexturedWorldAssetState::operator=(
    TexturedWorldAssetState&&) noexcept = default;
TexturedWorldAssetState::~TexturedWorldAssetState() = default;

const ApprovedAssetDispatchState& TexturedWorldAssetState::dispatch_state()
    const noexcept
{
    return implementation_->dispatch_state_;
}

const assets::WorldAsset& TexturedWorldAssetState::world() const noexcept
{
    return *std::get_if<assets::WorldAsset>(
        &*implementation_->dispatch_state_.imported_asset());
}

const assets::WorldTextureSet& TexturedWorldAssetState::textures()
    const noexcept
{
    return implementation_->textures_;
}

const std::shared_ptr<const local_resources::LocalResourceEnvironment>&
TexturedWorldAssetState::environment() const noexcept
{
    return implementation_->dispatch_state_.environment();
}

class WorldTextureImportStage::Implementation final {
public:
    Implementation(
        network::IDatagramTransport& transport,
        const network::NetworkAddress remote_endpoint,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        const assets::AssetImporterRegistries& importer_registries,
        WorldTextureImportStageConfig config,
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
        PrecacheAssetDispatchTraceCallback asset_dispatch_trace_callback,
        WorldTextureImportTraceCallback trace_callback)
        : config_{std::move(config)},
          trace_callback_{std::move(trace_callback)},
          configuration_valid_{
              valid_world_texture_import_stage_configuration(config_) &&
              environment != nullptr && environment->root_count() > 0U},
          asset_stage_{
              transport,
              remote_endpoint,
              std::move(environment),
              importer_registries,
              config_.asset_dispatch,
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
              std::move(asset_dispatch_trace_callback),
              PrecacheAssetDispatchStage::RetainConnectionAtBoundary{}},
          event_slots_(configuration_valid_ ? config_.maximum_stage_events : 0U)
    {
    }

    ~Implementation()
    {
        if (state_ != WorldTextureImportStageState::idle && !cleanup_done_) {
            cleanup(last_update_.value_or(WorldTextureImportStageTimePoint{}));
        }
    }

    [[nodiscard]] bool start(
        const WorldTextureImportStageTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime)
    {
        if (trace_callback_active_ ||
            state_ != WorldTextureImportStageState::idle) {
            return false;
        }
        error_.reset();
        if (!configuration_valid_) {
            fail(WorldTextureImportStageErrorCode::invalid_configuration,
                WorldTextureImportStageState::protocol_error,
                "World-texture stage configuration is invalid", now);
            return false;
        }
        bool started = false;
        try {
            started = asset_stage_.start(
                now, expected_local_endpoint, std::move(connection_lifetime));
        } catch (...) {
            fail(WorldTextureImportStageErrorCode::asset_dispatch_start_failed,
                WorldTextureImportStageState::protocol_error,
                "Nested asset-dispatch stage threw during start", now);
            return false;
        }
        drain_asset_events();
        if (!started) {
            fail_from_asset_stage(now, true);
            return false;
        }
        state_ = WorldTextureImportStageState::waiting_for_world_geometry;
        last_update_ = now;
        emit_trace(WorldTextureImportTraceClassification::stage_started);
        return true;
    }

    void update(const WorldTextureImportStageTimePoint now)
    {
        if (trace_callback_active_ || state_ == WorldTextureImportStageState::idle ||
            terminal_state(state_)) {
            return;
        }
        if (last_update_ && now < *last_update_) {
            fail(WorldTextureImportStageErrorCode::texture_import_failed,
                WorldTextureImportStageState::protocol_error,
                "World-texture stage time moved backwards", now,
                WorldTextureImportErrorCode::time_moved_backwards);
            return;
        }
        last_update_ = now;
        if (state_ == WorldTextureImportStageState::waiting_for_world_geometry) {
            try {
                asset_stage_.update(now);
            } catch (...) {
                fail(WorldTextureImportStageErrorCode::asset_dispatch_failed,
                    WorldTextureImportStageState::protocol_error,
                    "Nested asset-dispatch stage threw during update", now);
                return;
            }
            drain_asset_events();
            if (asset_stage_.terminal()) {
                begin_texture_import(now);
            }
            return;
        }
        update_texture_import(now);
    }

    void cancel(const WorldTextureImportStageTimePoint now) noexcept
    {
        if (trace_callback_active_ || state_ == WorldTextureImportStageState::idle ||
            terminal_state(state_)) {
            return;
        }
        if (operation_) {
            operation_->cancel();
        }
        state_ = WorldTextureImportStageState::cancelled;
        result_.reset();
        dispatch_state_.reset();
        error_.reset();
        WorldTextureImportStageEvent event;
        event.type = WorldTextureImportStageEventType::cancelled;
        event.occurred_at = now;
        if (can_push_events()) {
            push_event(event);
        }
        cleanup(now);
        emit_trace(WorldTextureImportTraceClassification::stage_cancelled, event);
    }

    [[nodiscard]] std::optional<WorldTextureImportStageEvent> poll_event()
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

private:
    void begin_texture_import(const WorldTextureImportStageTimePoint now)
    {
        if (asset_stage_.state() !=
                PrecacheAssetDispatchStageState::asset_imported ||
            !asset_stage_.result()) {
            fail_from_asset_stage(now, false);
            return;
        }
        if (!can_push_events(2U)) {
            fail_backpressure(now);
            return;
        }

        auto owned_dispatch = asset_stage_.take_result();
        if (!owned_dispatch || !owned_dispatch->imported_asset()) {
            fail(WorldTextureImportStageErrorCode::unable_to_retain_result,
                WorldTextureImportStageState::protocol_error,
                "Unable to take the owning imported-world prerequisite", now);
            return;
        }
        const auto* world = std::get_if<assets::WorldAsset>(
            &*owned_dispatch->imported_asset());
        if (world == nullptr || world->vertices.empty() || world->indices.empty() ||
            world->materials.empty()) {
            fail(WorldTextureImportStageErrorCode::imported_world_missing,
                WorldTextureImportStageState::world_geometry_unavailable,
                "Imported asset is not a non-empty CPU world", now);
            return;
        }
        const auto source_bytes = owned_dispatch->source().source().bytes();
        if (source_bytes.empty() || !owned_dispatch->environment()) {
            fail(WorldTextureImportStageErrorCode::retained_bsp_source_missing,
                WorldTextureImportStageState::world_geometry_unavailable,
                "Retained approved BSP source or local environment is missing",
                now);
            return;
        }

        auto begin = WorldTextureImportOperation::begin(
            *world,
            source_bytes,
            owned_dispatch->environment(),
            config_.texture_import);
        if (!begin || !begin.operation) {
            const auto nested_code = begin.error
                                         ? std::optional{
                                               begin.error->code}
                                         : std::nullopt;
            fail(WorldTextureImportStageErrorCode::texture_import_begin_failed,
                begin.error
                    ? map_operation_error_state(begin.error->code)
                    : WorldTextureImportStageState::protocol_error,
                begin.error ? std::string_view{begin.error->context}
                            : std::string_view{
                                  "Texture import operation could not begin"},
                now,
                nested_code);
            return;
        }

        const auto material_count = world->materials.size();
        dispatch_state_.emplace(std::move(*owned_dispatch));
        operation_.emplace(std::move(*begin.operation));
        state_ = map_operation_state(operation_->state());
        last_progress_ = operation_->progress();

        WorldTextureImportStageEvent geometry_event;
        geometry_event.type = WorldTextureImportStageEventType::world_geometry_ready;
        geometry_event.material_count = material_count;
        geometry_event.occurred_at = now;
        push_event(geometry_event);
        emit_trace(WorldTextureImportTraceClassification::world_geometry_ready,
            geometry_event);

        auto started_event = geometry_event;
        started_event.type = WorldTextureImportStageEventType::texture_import_started;
        push_event(started_event);
        emit_trace(WorldTextureImportTraceClassification::texture_import_started,
            started_event);
    }

    void update_texture_import(const WorldTextureImportStageTimePoint now)
    {
        if (!operation_ || !dispatch_state_) {
            fail(WorldTextureImportStageErrorCode::texture_import_failed,
                WorldTextureImportStageState::protocol_error,
                "Texture import lost its owning local prerequisite", now);
            return;
        }
        // Resolving a declaration may both start and complete a small WAD
        // source in one caller update. Reserve both observable transitions
        // before advancing any local state so backpressure remains
        // transactional.
        const auto required_event_capacity =
            operation_->state() ==
                    WorldTextureImportState::resolving_wad_references
                ? 2U
                : 1U;
        if (!can_push_events(required_event_capacity)) {
            fail_backpressure(now);
            return;
        }
        const auto previous_state = operation_->state();
        const auto previous_progress = operation_->progress();
        try {
            operation_->update(now);
        } catch (...) {
            fail(WorldTextureImportStageErrorCode::texture_import_failed,
                WorldTextureImportStageState::protocol_error,
                "Texture import operation threw during update", now);
            return;
        }
        const auto current_progress = operation_->progress();
        last_progress_ = current_progress;

        const bool wad_open_started =
            current_progress.wad_source_open_attempts !=
            previous_progress.wad_source_open_attempts;
        const bool wad_source_ready =
            (previous_state == WorldTextureImportState::opening_wad ||
                wad_open_started) &&
            operation_->state() ==
                WorldTextureImportState::parsing_wad_catalog;
        if (wad_open_started) {
            WorldTextureImportStageEvent event;
            event.type =
                WorldTextureImportStageEventType::wad_source_open_started;
            event.archive_count =
                current_progress.wad_declarations_considered;
            event.occurred_at = now;
            push_event(event);
            emit_trace(
                WorldTextureImportTraceClassification::wad_source_open_started,
                event);
        }
        if (wad_source_ready) {
            WorldTextureImportStageEvent event;
            event.type = WorldTextureImportStageEventType::wad_source_ready;
            event.archive_count =
                current_progress.wad_declarations_considered;
            event.occurred_at = now;
            push_event(event);
            emit_trace(WorldTextureImportTraceClassification::wad_source_ready,
                event);
        }

        if (!operation_->terminal()) {
            state_ = map_operation_state(operation_->state());
            if (current_progress.pixel_conversion_bytes !=
                    previous_progress.pixel_conversion_bytes ||
                current_progress.materials_considered !=
                    previous_progress.materials_considered ||
                (!wad_open_started && !wad_source_ready &&
                    current_progress.wad_declarations_considered !=
                        previous_progress.wad_declarations_considered)) {
                WorldTextureImportStageEvent event;
                event.type = WorldTextureImportStageEventType::texture_import_progress;
                event.material_count = current_progress.materials_considered;
                event.archive_count =
                    current_progress.wad_declarations_considered;
                event.pixel_conversion_bytes =
                    current_progress.pixel_conversion_bytes;
                event.occurred_at = now;
                push_event(event);
                emit_trace(
                    WorldTextureImportTraceClassification::texture_import_progress,
                    event);
            }
            return;
        }

        if (operation_->state() == WorldTextureImportState::textures_ready ||
            operation_->state() == WorldTextureImportState::textures_incomplete) {
            publish(now);
            return;
        }
        const auto* nested_error = operation_->error();
        const auto nested_code = nested_error
                                     ? nested_error->code
                                     : (operation_->state() ==
                                                WorldTextureImportState::timed_out
                                            ? WorldTextureImportErrorCode::timed_out
                                            : WorldTextureImportErrorCode::
                                                  unable_to_retain_state);
        fail(WorldTextureImportStageErrorCode::texture_import_failed,
            map_operation_error_state(nested_code),
            nested_error ? std::string_view{nested_error->context}
                         : std::string_view{"Texture import terminated without a result"},
            now,
            nested_code);
    }

    void publish(const WorldTextureImportStageTimePoint now)
    {
        if (!operation_ || !dispatch_state_ || !post_manifest_transmit_unchanged()) {
            fail(WorldTextureImportStageErrorCode::post_manifest_transmit_changed,
                WorldTextureImportStageState::protocol_error,
                "Texture publication lost a prerequisite or post-manifest transmit count changed",
                now);
            return;
        }
        auto texture_set = operation_->take_result();
        if (!texture_set) {
            fail(WorldTextureImportStageErrorCode::unable_to_retain_result,
                WorldTextureImportStageState::protocol_error,
                "Texture operation did not retain its terminal texture set", now);
            return;
        }
        const bool complete = texture_set->complete_for_world_materials();
        const auto statistics = texture_set->statistics();
        try {
            auto owned = std::make_unique<TexturedWorldAssetState::Implementation>(
                std::move(*dispatch_state_), std::move(*texture_set));
            result_.emplace(TexturedWorldAssetState{std::move(owned)});
        } catch (...) {
            fail(WorldTextureImportStageErrorCode::unable_to_retain_result,
                WorldTextureImportStageState::protocol_error,
                "Unable to retain the owning textured-world publication", now);
            return;
        }
        dispatch_state_.reset();
        operation_.reset();
        state_ = complete
                     ? WorldTextureImportStageState::world_textures_ready
                     : WorldTextureImportStageState::world_textures_incomplete;
        ++texture_set_publication_count_;

        WorldTextureImportStageEvent event;
        event.type = terminal_event_type(state_);
        event.material_count = statistics.material_binding_count;
        event.texture_count = statistics.decoded_texture_count;
        event.binding_count = statistics.material_binding_count;
        event.archive_count = statistics.wad_declaration_count;
        event.unresolved_binding_count = statistics.unresolved_material_count;
        event.pixel_conversion_bytes = statistics.total_rgba_byte_count;
        event.occurred_at = now;
        push_event(event);
        cleanup(now);
        emit_trace(terminal_trace(state_), event);
    }

    void fail_from_asset_stage(
        const WorldTextureImportStageTimePoint now,
        const bool start_failure) noexcept
    {
        const auto& nested = asset_stage_.error();
        auto terminal = WorldTextureImportStageState::protocol_error;
        if (asset_stage_.state() == PrecacheAssetDispatchStageState::network_error) {
            terminal = WorldTextureImportStageState::network_error;
        } else if (asset_stage_.state() ==
                       PrecacheAssetDispatchStageState::world_source_unavailable ||
                   asset_stage_.state() ==
                       PrecacheAssetDispatchStageState::importer_boundary_reached ||
                   asset_stage_.state() ==
                       PrecacheAssetDispatchStageState::import_failed) {
            terminal = WorldTextureImportStageState::world_geometry_unavailable;
        } else if (asset_stage_.state() ==
                   PrecacheAssetDispatchStageState::timed_out) {
            terminal = WorldTextureImportStageState::timed_out;
        } else if (asset_stage_.state() ==
                   PrecacheAssetDispatchStageState::cancelled) {
            terminal = WorldTextureImportStageState::cancelled;
        } else if (asset_stage_.state() ==
                   PrecacheAssetDispatchStageState::backpressure) {
            terminal = WorldTextureImportStageState::backpressure;
        }
        fail(start_failure
                 ? WorldTextureImportStageErrorCode::asset_dispatch_start_failed
                 : WorldTextureImportStageErrorCode::asset_dispatch_failed,
            terminal,
            nested ? std::string_view{nested->context}
                   : std::string_view{"Nested asset-dispatch stage failed"},
            now,
            std::nullopt,
            nested ? std::optional{nested->code} : std::nullopt);
    }

    void fail_backpressure(const WorldTextureImportStageTimePoint now) noexcept
    {
        fail(WorldTextureImportStageErrorCode::event_backpressure,
            WorldTextureImportStageState::backpressure,
            "World-texture stage event queue is full", now);
    }

    void fail(
        const WorldTextureImportStageErrorCode code,
        const WorldTextureImportStageState terminal,
        const std::string_view context,
        const WorldTextureImportStageTimePoint now,
        const std::optional<WorldTextureImportErrorCode> texture_code =
            std::nullopt,
        const std::optional<PrecacheAssetDispatchStageErrorCode>
            dispatch_code = std::nullopt) noexcept
    {
        if (terminal_state(state_)) {
            return;
        }
        std::string retained_context;
        try {
            const auto bounded = context.substr(0U,
                (std::min)(context.size(),
                    kWorldTextureImportStageDiagnosticTextLimit));
            retained_context.assign(bounded.data(), bounded.size());
        } catch (...) {
        }
        if (operation_) {
            operation_->cancel();
        }
        state_ = terminal;
        result_.reset();
        dispatch_state_.reset();
        operation_.reset();
        error_.reset();
        try {
            error_.emplace();
            error_->code = code;
            error_->asset_dispatch_code = dispatch_code;
            error_->texture_import_code = texture_code;
            error_->context = std::move(retained_context);
        } catch (...) {
        }

        WorldTextureImportStageEvent event;
        event.type = terminal_event_type(terminal);
        event.occurred_at = now;
        if (can_push_events()) {
            push_event(event);
        }
        cleanup(now);
        emit_trace(terminal_trace(terminal), event);
    }

    void cleanup(const WorldTextureImportStageTimePoint now) noexcept
    {
        if (cleanup_done_) {
            return;
        }
        cleanup_done_ = true;
        if (!asset_stage_.terminal()) {
            try {
                asset_stage_.cancel(now);
            } catch (...) {
            }
        }
        asset_stage_.finalize_retained_boundary(now);
    }

    void drain_asset_events()
    {
        while (asset_stage_.poll_event()) {
        }
    }

    [[nodiscard]] bool can_push_events(
        const std::size_t count = 1U) const noexcept
    {
        return count <= event_slots_.size() - event_size_;
    }

    void push_event(WorldTextureImportStageEvent event) noexcept
    {
        if (!can_push_events()) {
            return;
        }
        const auto index = (event_head_ + event_size_) % event_slots_.size();
        event_slots_[index].emplace(std::move(event));
        ++event_size_;
    }

    [[nodiscard]] bool post_manifest_transmit_unchanged() const noexcept
    {
        const auto published =
            asset_stage_.transmitted_packet_count_at_manifest_publication();
        return published &&
            asset_stage_.transmitted_packet_count() == *published;
    }

    void emit_trace(
        const WorldTextureImportTraceClassification classification,
        const WorldTextureImportStageEvent& metadata = {}) noexcept
    {
        if (!trace_callback_ || trace_callback_active_) {
            return;
        }
        WorldTextureImportTraceEvent event;
        event.classification = classification;
        event.state = state_;
        event.endpoint = asset_stage_.remote_endpoint();
        event.material_count = metadata.material_count;
        event.texture_count = metadata.texture_count;
        event.binding_count = metadata.binding_count;
        event.archive_count = metadata.archive_count;
        event.unresolved_binding_count = metadata.unresolved_binding_count;
        event.pixel_conversion_bytes = metadata.pixel_conversion_bytes;
        event.transmitted_packet_count =
            asset_stage_.transmitted_packet_count();
        trace_callback_active_ = true;
        try {
            trace_callback_(event);
        } catch (...) {
        }
        trace_callback_active_ = false;
    }

public:
    WorldTextureImportStageConfig config_;
    WorldTextureImportTraceCallback trace_callback_;
    bool trace_callback_active_{false};
    bool configuration_valid_{false};
    PrecacheAssetDispatchStage asset_stage_;
    std::vector<std::optional<WorldTextureImportStageEvent>> event_slots_;
    std::size_t event_head_{0U};
    std::size_t event_size_{0U};
    WorldTextureImportStageState state_{WorldTextureImportStageState::idle};
    std::optional<ApprovedAssetDispatchState> dispatch_state_;
    std::optional<WorldTextureImportOperation> operation_;
    std::optional<TexturedWorldAssetState> result_;
    std::optional<WorldTextureImportStageError> error_;
    std::optional<WorldTextureImportStageTimePoint> last_update_;
    WorldTextureImportProgress last_progress_{};
    std::size_t texture_set_publication_count_{0U};
    bool cleanup_done_{false};
};

WorldTextureImportStage::WorldTextureImportStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment,
    const assets::AssetImporterRegistries& importer_registries,
    WorldTextureImportStageConfig config,
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
    PrecacheAssetDispatchTraceCallback asset_dispatch_trace_callback,
    WorldTextureImportTraceCallback trace_callback)
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
          std::move(asset_dispatch_trace_callback),
          std::move(trace_callback))}
{
}

WorldTextureImportStage::~WorldTextureImportStage() = default;

bool WorldTextureImportStage::start(
    const WorldTextureImportStageTimePoint now,
    const network::NetworkAddress& expected_local_endpoint,
    std::unique_ptr<INetchanDriverLifetime> connection_lifetime)
{
    return implementation_->start(
        now, expected_local_endpoint, std::move(connection_lifetime));
}

void WorldTextureImportStage::update(const WorldTextureImportStageTimePoint now)
{
    implementation_->update(now);
}

void WorldTextureImportStage::cancel(const WorldTextureImportStageTimePoint now)
{
    implementation_->cancel(now);
}

std::optional<WorldTextureImportStageEvent>
WorldTextureImportStage::poll_event()
{
    return implementation_->poll_event();
}

WorldTextureImportStageState WorldTextureImportStage::state() const noexcept
{
    return implementation_->state_;
}

bool WorldTextureImportStage::terminal() const noexcept
{
    return terminal_state(implementation_->state_);
}

const std::optional<TexturedWorldAssetState>& WorldTextureImportStage::result()
    const noexcept
{
    return implementation_->result_;
}

const std::optional<WorldTextureImportStageError>& WorldTextureImportStage::error()
    const noexcept
{
    return implementation_->error_;
}

const network::NetworkAddress& WorldTextureImportStage::remote_endpoint()
    const noexcept
{
    return implementation_->asset_stage_.remote_endpoint();
}

const std::optional<network::NetworkAddress>&
WorldTextureImportStage::local_endpoint() const noexcept
{
    return implementation_->asset_stage_.local_endpoint();
}

std::size_t WorldTextureImportStage::pending_event_count() const noexcept
{
    return implementation_->event_size_;
}

std::size_t WorldTextureImportStage::transmitted_packet_count() const noexcept
{
    return implementation_->asset_stage_.transmitted_packet_count();
}

std::optional<std::size_t>
WorldTextureImportStage::transmitted_packet_count_at_manifest_publication()
    const noexcept
{
    return implementation_->asset_stage_
        .transmitted_packet_count_at_manifest_publication();
}

std::size_t WorldTextureImportStage::cleanup_count() const noexcept
{
    return implementation_->asset_stage_.cleanup_count();
}

std::size_t WorldTextureImportStage::manifest_publication_count() const noexcept
{
    return implementation_->asset_stage_.manifest_publication_count();
}

std::size_t WorldTextureImportStage::bsp_source_open_attempt_count()
    const noexcept
{
    return implementation_->asset_stage_.source_open_attempt_count();
}

std::size_t WorldTextureImportStage::importer_dispatch_count() const noexcept
{
    return implementation_->asset_stage_.importer_dispatch_count();
}

std::size_t WorldTextureImportStage::wad_source_open_attempt_count()
    const noexcept
{
    return implementation_->operation_
               ? implementation_->operation_->progress()
                     .wad_source_open_attempts
               : implementation_->last_progress_.wad_source_open_attempts;
}

std::size_t WorldTextureImportStage::texture_set_publication_count()
    const noexcept
{
    return implementation_->texture_set_publication_count_;
}

} // namespace hlclient::goldsrc
