#include <hlclient/goldsrc/world_render/world_render_package_stage.hpp>

#include <algorithm>
#include <new>
#include <utility>
#include <vector>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool terminal_state(
    const WorldRenderPackageStageState state) noexcept
{
    switch (state) {
    case WorldRenderPackageStageState::world_render_package_ready:
    case WorldRenderPackageStageState::world_textures_incomplete:
    case WorldRenderPackageStageState::lightmap_import_failed:
    case WorldRenderPackageStageState::render_package_failed:
    case WorldRenderPackageStageState::timed_out:
    case WorldRenderPackageStageState::cancelled:
    case WorldRenderPackageStageState::backpressure:
    case WorldRenderPackageStageState::network_error:
    case WorldRenderPackageStageState::protocol_error:
        return true;
    case WorldRenderPackageStageState::idle:
    case WorldRenderPackageStageState::waiting_for_world_textures:
    case WorldRenderPackageStageState::importing_lightmaps:
    case WorldRenderPackageStageState::packing_lightmap_atlases:
    case WorldRenderPackageStageState::building_render_package:
        return false;
    }
    return true;
}

[[nodiscard]] bool valid_render_package_limits(
    const world_render::WorldRenderPackageLimits& limits) noexcept
{
    return limits.maximum_vertices > 0U && limits.maximum_indices > 0U &&
        limits.maximum_materials > 0U && limits.maximum_batches > 0U &&
        limits.maximum_base_texture_bytes > 0U &&
        limits.maximum_lightmap_bytes > 0U &&
        limits.maximum_total_cpu_render_bytes > 0U;
}

[[nodiscard]] WorldRenderPackageStageEventType terminal_event_type(
    const WorldRenderPackageStageState state) noexcept
{
    switch (state) {
    case WorldRenderPackageStageState::world_render_package_ready:
        return WorldRenderPackageStageEventType::world_render_package_ready;
    case WorldRenderPackageStageState::world_textures_incomplete:
        return WorldRenderPackageStageEventType::world_textures_incomplete;
    case WorldRenderPackageStageState::lightmap_import_failed:
        return WorldRenderPackageStageEventType::lightmap_import_failed;
    case WorldRenderPackageStageState::render_package_failed:
        return WorldRenderPackageStageEventType::render_package_failed;
    case WorldRenderPackageStageState::timed_out:
        return WorldRenderPackageStageEventType::timeout;
    case WorldRenderPackageStageState::cancelled:
        return WorldRenderPackageStageEventType::cancelled;
    case WorldRenderPackageStageState::backpressure:
        return WorldRenderPackageStageEventType::backpressure;
    case WorldRenderPackageStageState::network_error:
        return WorldRenderPackageStageEventType::network_error;
    case WorldRenderPackageStageState::idle:
    case WorldRenderPackageStageState::waiting_for_world_textures:
    case WorldRenderPackageStageState::importing_lightmaps:
    case WorldRenderPackageStageState::packing_lightmap_atlases:
    case WorldRenderPackageStageState::building_render_package:
    case WorldRenderPackageStageState::protocol_error:
        return WorldRenderPackageStageEventType::protocol_error;
    }
    return WorldRenderPackageStageEventType::protocol_error;
}

[[nodiscard]] WorldRenderPackageTraceClassification terminal_trace(
    const WorldRenderPackageStageState state) noexcept
{
    switch (state) {
    case WorldRenderPackageStageState::world_render_package_ready:
        return WorldRenderPackageTraceClassification::world_render_package_ready;
    case WorldRenderPackageStageState::world_textures_incomplete:
        return WorldRenderPackageTraceClassification::world_textures_incomplete;
    case WorldRenderPackageStageState::lightmap_import_failed:
        return WorldRenderPackageTraceClassification::lightmap_import_failed;
    case WorldRenderPackageStageState::render_package_failed:
        return WorldRenderPackageTraceClassification::render_package_failed;
    case WorldRenderPackageStageState::timed_out:
        return WorldRenderPackageTraceClassification::stage_timed_out;
    case WorldRenderPackageStageState::cancelled:
        return WorldRenderPackageTraceClassification::stage_cancelled;
    case WorldRenderPackageStageState::backpressure:
        return WorldRenderPackageTraceClassification::backpressure;
    case WorldRenderPackageStageState::network_error:
        return WorldRenderPackageTraceClassification::network_failure;
    case WorldRenderPackageStageState::idle:
    case WorldRenderPackageStageState::waiting_for_world_textures:
    case WorldRenderPackageStageState::importing_lightmaps:
    case WorldRenderPackageStageState::packing_lightmap_atlases:
    case WorldRenderPackageStageState::building_render_package:
    case WorldRenderPackageStageState::protocol_error:
        return WorldRenderPackageTraceClassification::protocol_failure;
    }
    return WorldRenderPackageTraceClassification::protocol_failure;
}

} // namespace

bool valid_world_render_package_stage_configuration(
    const WorldRenderPackageStageConfig& config) noexcept
{
    const bool scene_configuration_valid =
        !config.build_world_spatial_scene ||
        (bsp::valid_goldsrc_bsp_import_limits(config.bsp) &&
            brush_models::valid_goldsrc_brush_render_library_limits(
                config.brush_library) &&
            brush_models::valid_goldsrc_world_scene_build_config(
                config.world_scene) &&
            brush_models::valid_goldsrc_world_scene_build_limits(
                config.world_scene_limits));
    return scene_configuration_valid &&
        valid_world_texture_import_stage_configuration(
               config.world_textures) &&
        lightmaps::valid_goldsrc_world_lightmap_import_limits(
            config.lightmaps) &&
        valid_render_package_limits(config.render_package) &&
        config.maximum_stage_events >=
            kMinimumWorldRenderPackageStageEvents &&
        config.maximum_stage_events <=
            kMaximumWorldRenderPackageStageEvents;
}

class WorldRenderPackageStage::Implementation final {
public:
    Implementation(
        network::IDatagramTransport& transport,
        const network::NetworkAddress remote_endpoint,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        const assets::AssetImporterRegistries& importer_registries,
        WorldRenderPackageStageConfig config,
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
        WorldTextureImportTraceCallback world_texture_trace_callback,
        WorldRenderPackageTraceCallback trace_callback)
        : config_{std::move(config)},
          trace_callback_{std::move(trace_callback)},
          environment_{environment},
          configuration_valid_{
              valid_world_render_package_stage_configuration(config_) &&
              environment != nullptr && environment->root_count() > 0U},
          world_texture_stage_{
              transport,
              remote_endpoint,
              std::move(environment),
              importer_registries,
              config_.world_textures,
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
              std::move(world_texture_trace_callback),
              WorldTextureImportStage::RetainConnectionAtBoundary{}},
          event_slots_(configuration_valid_ ? config_.maximum_stage_events : 0U)
    {
    }

    ~Implementation()
    {
        if (state_ != WorldRenderPackageStageState::idle &&
            !network_cleanup_done_) {
            finalize_nested(last_update_.value_or(
                WorldRenderPackageStageTimePoint{}));
        }
    }

    [[nodiscard]] bool start(
        const WorldRenderPackageStageTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime)
    {
        if (trace_callback_active_ ||
            state_ != WorldRenderPackageStageState::idle) {
            return false;
        }
        last_update_ = now;
        if (!configuration_valid_) {
            fail(WorldRenderPackageStageErrorCode::invalid_configuration,
                WorldRenderPackageStageState::protocol_error,
                "World-render-package stage configuration is invalid", now);
            return false;
        }
        bool started = false;
        try {
            started = world_texture_stage_.start(
                now, expected_local_endpoint, std::move(connection_lifetime));
        } catch (...) {
            fail(WorldRenderPackageStageErrorCode::world_texture_start_failed,
                WorldRenderPackageStageState::protocol_error,
                "Nested world-texture stage threw during start", now);
            return false;
        }
        drain_world_texture_events();
        if (!started) {
            finish_from_world_textures(now, true);
            return false;
        }
        state_ = WorldRenderPackageStageState::waiting_for_world_textures;
        emit_trace(WorldRenderPackageTraceClassification::stage_started);
        return true;
    }

    void update(const WorldRenderPackageStageTimePoint now)
    {
        if (trace_callback_active_ ||
            state_ == WorldRenderPackageStageState::idle ||
            terminal_state(state_)) {
            return;
        }
        if (last_update_ && now < *last_update_) {
            fail(WorldRenderPackageStageErrorCode::world_texture_failed,
                WorldRenderPackageStageState::protocol_error,
                "World-render-package stage time moved backwards", now);
            return;
        }
        last_update_ = now;
        try {
            world_texture_stage_.update(now);
        } catch (...) {
            fail(WorldRenderPackageStageErrorCode::world_texture_failed,
                WorldRenderPackageStageState::protocol_error,
                "Nested world-texture stage threw during update", now);
            return;
        }
        drain_world_texture_events();
        if (world_texture_stage_.terminal()) {
            finish_from_world_textures(now, false);
        }
    }

    void cancel(const WorldRenderPackageStageTimePoint now) noexcept
    {
        if (trace_callback_active_ ||
            state_ == WorldRenderPackageStageState::idle ||
            terminal_state(state_)) {
            return;
        }
        last_update_ = now;
        try {
            world_texture_stage_.cancel(now);
        } catch (...) {
        }
        drain_world_texture_events();
        state_ = WorldRenderPackageStageState::cancelled;
        result_.reset();
        scene_result_.reset();
        spawn_camera_result_.reset();
        error_.reset();
        WorldRenderPackageStageEvent event;
        event.type = WorldRenderPackageStageEventType::cancelled;
        event.occurred_at = now;
        if (can_push_events()) {
            push_event(event);
        }
        finalize_nested(now);
        emit_trace(WorldRenderPackageTraceClassification::stage_cancelled, event);
    }

    [[nodiscard]] std::optional<WorldRenderPackageStageEvent> poll_event()
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

    void finish_from_world_textures(
        const WorldRenderPackageStageTimePoint now,
        const bool start_failure)
    {
        if (!world_texture_stage_.terminal()) {
            fail(start_failure
                    ? WorldRenderPackageStageErrorCode::world_texture_start_failed
                    : WorldRenderPackageStageErrorCode::world_texture_failed,
                WorldRenderPackageStageState::protocol_error,
                "Nested world-texture stage did not reach a terminal state", now);
            return;
        }
        if (world_texture_stage_.state() !=
            WorldTextureImportStageState::world_textures_ready) {
            fail_from_world_textures(now, start_failure);
            return;
        }
        if (!can_push_events(
                kWorldRenderPackageSuccessfulPublicationEventCount)) {
            fail(WorldRenderPackageStageErrorCode::event_backpressure,
                WorldRenderPackageStageState::backpressure,
                "No bounded event capacity remains for package publication", now);
            return;
        }

        WorldRenderPackageStageEvent texture_event;
        texture_event.type = WorldRenderPackageStageEventType::world_textures_ready;
        texture_event.occurred_at = now;
        push_event(texture_event);
        emit_trace(WorldRenderPackageTraceClassification::world_textures_ready,
            texture_event);

        auto retained = world_texture_stage_.take_result();
        if (!retained) {
            fail(WorldRenderPackageStageErrorCode::retained_world_missing,
                WorldRenderPackageStageState::protocol_error,
                "Nested world-texture publication could not be transferred", now);
            return;
        }
        const auto source_bytes =
            retained->dispatch_state().source().source().bytes();
        if (source_bytes.empty()) {
            fail(WorldRenderPackageStageErrorCode::retained_bsp_source_missing,
                WorldRenderPackageStageState::protocol_error,
                "Approved retained BSP source is empty", now);
            return;
        }

        state_ = WorldRenderPackageStageState::importing_lightmaps;
        WorldRenderPackageStageEvent import_event;
        import_event.type =
            WorldRenderPackageStageEventType::lightmap_import_started;
        import_event.surface_count = retained->world().surfaces.size();
        import_event.occurred_at = now;
        push_event(import_event);
        emit_trace(WorldRenderPackageTraceClassification::lightmap_import_started,
            import_event);

        ++lightmap_import_count_;
        auto lightmap_result = lightmaps::GoldSrcWorldLightmapImporter::import(
            retained->world(), source_bytes, config_.lightmaps);
        if (!lightmap_result) {
            const auto code = lightmap_result.error
                                  ? std::optional{lightmap_result.error->code}
                                  : std::nullopt;
            fail(WorldRenderPackageStageErrorCode::lightmap_import_failed,
                WorldRenderPackageStageState::lightmap_import_failed,
                lightmap_result.error
                    ? std::string_view{lightmap_result.error->context}
                    : std::string_view{"Lightmap importer returned no result"},
                now, std::nullopt, code);
            return;
        }
        ++lightmap_set_publication_count_;
        state_ = WorldRenderPackageStageState::packing_lightmap_atlases;
        WorldRenderPackageStageEvent atlas_event;
        atlas_event.type =
            WorldRenderPackageStageEventType::lightmap_atlases_ready;
        atlas_event.surface_count =
            lightmap_result.lightmap_set->binding_count();
        atlas_event.atlas_page_count =
            lightmap_result.lightmap_set->page_count();
        atlas_event.occurred_at = now;
        push_event(atlas_event);
        emit_trace(WorldRenderPackageTraceClassification::lightmap_atlases_ready,
            atlas_event);

        auto textured_world = retained->take_textured_world();
        if (!textured_world) {
            fail(WorldRenderPackageStageErrorCode::retained_world_missing,
                WorldRenderPackageStageState::protocol_error,
                "Textured-world ownership transfer failed", now);
            return;
        }
        state_ = WorldRenderPackageStageState::building_render_package;
        WorldRenderPackageStageEvent build_event;
        build_event.type =
            WorldRenderPackageStageEventType::render_package_build_started;
        build_event.surface_count = textured_world->world.surfaces.size();
        build_event.atlas_page_count =
            lightmap_result.lightmap_set->page_count();
        build_event.occurred_at = now;
        push_event(build_event);
        emit_trace(
            WorldRenderPackageTraceClassification::render_package_build_started,
            build_event);

        const world_render::WorldRenderPackageBuilder builder;
        auto package_result = builder.build(
            std::move(*textured_world),
            std::move(*lightmap_result.lightmap_set),
            config_.render_package);
        if (!package_result) {
            const auto code = package_result.error
                                  ? std::optional{package_result.error->code}
                                  : std::nullopt;
            fail(WorldRenderPackageStageErrorCode::render_package_build_failed,
                WorldRenderPackageStageState::render_package_failed,
                package_result.error
                    ? std::string_view{package_result.error->context}
                    : std::string_view{"Render-package builder returned no package"},
                now, code);
            return;
        }

        try {
            result_ = std::make_shared<world_render::WorldRenderPackage>(
                std::move(*package_result.package));
        } catch (...) {
            fail(WorldRenderPackageStageErrorCode::unable_to_retain_package,
                WorldRenderPackageStageState::render_package_failed,
                "Unable to retain immutable render package", now);
            return;
        }

        if (config_.build_world_spatial_scene) {
            ++bsp_scene_parse_count_;
            auto parsed = bsp::GoldSrcBspParser::parse(
                source_bytes,
                config_.bsp,
                bsp::GoldSrcBspParseOptions{
                    config_.world_scene.brushes ==
                    brush_models::GoldSrcWorldSceneBrushMode::static_initial});
            if (!parsed || !parsed.document) {
                const auto code = parsed.error
                    ? std::optional{parsed.error->code}
                    : std::nullopt;
                if (config_.world_scene.brushes ==
                        brush_models::GoldSrcWorldSceneBrushMode::static_initial &&
                    parsed.error && parsed.error->source_model_index) {
                    ++brush_library_build_count_;
                    const auto brush_code = parsed.error->code ==
                            bsp::GoldSrcBspErrorCode::geometry_limit_exceeded
                        ? brush_models::GoldSrcBrushRenderLibraryErrorCode::
                              aggregate_limit_exceeded
                        : brush_models::GoldSrcBrushRenderLibraryErrorCode::
                              invalid_model_geometry;
                    fail(
                        WorldRenderPackageStageErrorCode::
                            brush_render_library_build_failed,
                        WorldRenderPackageStageState::render_package_failed,
                        parsed.error->context,
                        now,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        brush_code);
                    return;
                }
                fail(
                    WorldRenderPackageStageErrorCode::
                        world_scene_bsp_parse_failed,
                    WorldRenderPackageStageState::render_package_failed,
                    parsed.error
                        ? std::string_view{parsed.error->context}
                        : std::string_view{
                              "Canonical BSP parser returned no document"},
                    now,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    code);
                return;
            }

            std::optional<world_scene_render::BrushSubmodelRenderLibrary>
                brush_library;
            if (config_.world_scene.brushes ==
                brush_models::GoldSrcWorldSceneBrushMode::static_initial) {
                ++brush_library_build_count_;
                auto built_library =
                    brush_models::GoldSrcBrushRenderLibraryBuilder::build(
                        *parsed.document,
                        source_bytes,
                        environment_,
                        config_.brush_library);
                if (!built_library || !built_library.library) {
                    const auto code = built_library.error
                        ? std::optional{built_library.error->code}
                        : std::nullopt;
                    fail(
                        WorldRenderPackageStageErrorCode::
                            brush_render_library_build_failed,
                        WorldRenderPackageStageState::render_package_failed,
                        "Brush render-library construction failed",
                        now,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        code);
                    return;
                }
                brush_library.emplace(std::move(*built_library.library));
            }

            auto built_scene = brush_models::GoldSrcWorldSceneBuilder::build(
                *parsed.document,
                result_,
                std::move(brush_library),
                config_.world_scene,
                config_.world_scene_limits);
            if (!built_scene || !built_scene.scene_package) {
                const auto code = built_scene.error
                    ? std::optional{built_scene.error->code}
                    : std::nullopt;
                fail(
                    WorldRenderPackageStageErrorCode::world_scene_build_failed,
                    WorldRenderPackageStageState::render_package_failed,
                    "World spatial-scene construction failed",
                    now,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    code);
                return;
            }
            try {
                scene_result_ = std::make_shared<
                    world_scene_render::WorldSceneRenderPackage>(
                    std::move(*built_scene.scene_package));
                spawn_camera_result_ = std::move(built_scene.spawn_camera);
            } catch (...) {
                fail(
                    WorldRenderPackageStageErrorCode::unable_to_retain_package,
                    WorldRenderPackageStageState::render_package_failed,
                    "Unable to retain immutable world spatial scene",
                    now);
                return;
            }
            ++world_scene_publication_count_;
        }
        state_ = WorldRenderPackageStageState::world_render_package_ready;
        ++render_package_publication_count_;
        const auto& statistics = result_->statistics();
        WorldRenderPackageStageEvent ready_event;
        ready_event.type =
            WorldRenderPackageStageEventType::world_render_package_ready;
        ready_event.surface_count = statistics.source_surface_count;
        ready_event.atlas_page_count = result_->lightmaps().page_count();
        ready_event.vertex_count = statistics.vertex_count;
        ready_event.index_count = statistics.index_count;
        ready_event.batch_count = statistics.batch_count;
        ready_event.occurred_at = now;
        push_event(ready_event);
        finalize_nested(now);
        emit_trace(WorldRenderPackageTraceClassification::world_render_package_ready,
            ready_event);
    }

    void fail_from_world_textures(
        const WorldRenderPackageStageTimePoint now,
        const bool start_failure) noexcept
    {
        auto terminal = WorldRenderPackageStageState::protocol_error;
        auto code = start_failure
                        ? WorldRenderPackageStageErrorCode::world_texture_start_failed
                        : WorldRenderPackageStageErrorCode::world_texture_failed;
        switch (world_texture_stage_.state()) {
        case WorldTextureImportStageState::world_textures_incomplete:
            terminal = WorldRenderPackageStageState::world_textures_incomplete;
            code = WorldRenderPackageStageErrorCode::world_textures_incomplete;
            break;
        case WorldTextureImportStageState::timed_out:
            terminal = WorldRenderPackageStageState::timed_out;
            break;
        case WorldTextureImportStageState::cancelled:
            terminal = WorldRenderPackageStageState::cancelled;
            break;
        case WorldTextureImportStageState::backpressure:
            terminal = WorldRenderPackageStageState::backpressure;
            break;
        case WorldTextureImportStageState::network_error:
            terminal = WorldRenderPackageStageState::network_error;
            break;
        case WorldTextureImportStageState::idle:
        case WorldTextureImportStageState::waiting_for_world_geometry:
        case WorldTextureImportStageState::parsing_texture_sources:
        case WorldTextureImportStageState::decoding_embedded_textures:
        case WorldTextureImportStageState::resolving_wad_archives:
        case WorldTextureImportStageState::decoding_external_textures:
        case WorldTextureImportStageState::building_texture_set:
        case WorldTextureImportStageState::world_textures_ready:
        case WorldTextureImportStageState::world_geometry_unavailable:
        case WorldTextureImportStageState::worldspawn_parse_failed:
        case WorldTextureImportStageState::wad_reference_invalid:
        case WorldTextureImportStageState::wad_source_unavailable:
        case WorldTextureImportStageState::wad_source_open_failed:
        case WorldTextureImportStageState::wad_catalog_failed:
        case WorldTextureImportStageState::texture_decode_failed:
        case WorldTextureImportStageState::protocol_error:
            break;
        }
        const auto& nested = world_texture_stage_.error();
        fail(code, terminal,
            nested ? std::string_view{nested->context}
                   : std::string_view{"Nested world-texture stage failed"},
            now, std::nullopt, std::nullopt,
            nested ? std::optional{nested->code} : std::nullopt);
    }

    void fail(
        const WorldRenderPackageStageErrorCode code,
        const WorldRenderPackageStageState terminal,
        const std::string_view context,
        const WorldRenderPackageStageTimePoint now,
        const std::optional<world_render::WorldRenderPackageErrorCode>
            package_code = std::nullopt,
        const std::optional<lightmaps::GoldSrcWorldLightmapImportErrorCode>
            lightmap_code = std::nullopt,
        const std::optional<WorldTextureImportStageErrorCode>
            texture_code = std::nullopt,
        const std::optional<bsp::GoldSrcBspErrorCode>
            bsp_code = std::nullopt,
        const std::optional<
            brush_models::GoldSrcBrushRenderLibraryErrorCode>
            brush_library_code = std::nullopt,
        const std::optional<brush_models::GoldSrcWorldSceneBuildErrorCode>
            world_scene_code = std::nullopt) noexcept
    {
        if (terminal_state(state_)) {
            return;
        }
        std::string retained_context;
        try {
            const auto bounded = context.substr(0U,
                (std::min)(context.size(),
                    kWorldRenderPackageStageDiagnosticTextLimit));
            retained_context.assign(bounded.data(), bounded.size());
        } catch (...) {
        }
        state_ = terminal;
        result_.reset();
        scene_result_.reset();
        spawn_camera_result_.reset();
        error_.reset();
        try {
            error_.emplace();
            error_->code = code;
            error_->world_texture_code = texture_code;
            error_->lightmap_code = lightmap_code;
            error_->render_package_code = package_code;
            error_->bsp_code = bsp_code;
            error_->brush_library_code = brush_library_code;
            error_->world_scene_code = world_scene_code;
            error_->context = std::move(retained_context);
        } catch (...) {
        }
        WorldRenderPackageStageEvent event;
        event.type = terminal_event_type(terminal);
        event.occurred_at = now;
        if (can_push_events()) {
            push_event(event);
        }
        finalize_nested(now);
        emit_trace(terminal_trace(terminal), event);
    }

    void finalize_nested(const WorldRenderPackageStageTimePoint now) noexcept
    {
        if (network_cleanup_done_) {
            return;
        }
        if (!world_texture_stage_.terminal()) {
            try {
                world_texture_stage_.cancel(now);
            } catch (...) {
            }
        }
        world_texture_stage_.finalize_retained_boundary(now);
        network_cleanup_done_ = true;
    }

    void drain_world_texture_events()
    {
        while (world_texture_stage_.poll_event()) {
        }
    }

    [[nodiscard]] bool can_push_events(
        const std::size_t count = 1U) const noexcept
    {
        return count <= event_slots_.size() - event_size_;
    }

    void push_event(WorldRenderPackageStageEvent event) noexcept
    {
        if (!can_push_events()) {
            return;
        }
        const auto index = (event_head_ + event_size_) % event_slots_.size();
        event_slots_[index].emplace(std::move(event));
        ++event_size_;
    }

    void emit_trace(
        const WorldRenderPackageTraceClassification classification,
        const WorldRenderPackageStageEvent& metadata = {}) noexcept
    {
        if (!trace_callback_ || trace_callback_active_) {
            return;
        }
        WorldRenderPackageTraceEvent event;
        event.classification = classification;
        event.state = state_;
        event.endpoint = world_texture_stage_.remote_endpoint();
        event.surface_count = metadata.surface_count;
        event.atlas_page_count = metadata.atlas_page_count;
        event.vertex_count = metadata.vertex_count;
        event.index_count = metadata.index_count;
        event.batch_count = metadata.batch_count;
        event.transmitted_packet_count =
            world_texture_stage_.transmitted_packet_count();
        trace_callback_active_ = true;
        try {
            trace_callback_(event);
        } catch (...) {
        }
        trace_callback_active_ = false;
    }

    WorldRenderPackageStageConfig config_;
    WorldRenderPackageTraceCallback trace_callback_;
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment_;
    bool trace_callback_active_{false};
    bool configuration_valid_{false};
    WorldTextureImportStage world_texture_stage_;
    std::vector<std::optional<WorldRenderPackageStageEvent>> event_slots_;
    std::size_t event_head_{0U};
    std::size_t event_size_{0U};
    WorldRenderPackageStageState state_{WorldRenderPackageStageState::idle};
    std::shared_ptr<const world_render::WorldRenderPackage> result_;
    std::shared_ptr<const world_scene_render::WorldSceneRenderPackage>
        scene_result_;
    std::optional<brush_models::GoldSrcSpawnCameraExtractionResult>
        spawn_camera_result_;
    std::optional<WorldRenderPackageStageError> error_;
    std::optional<WorldRenderPackageStageTimePoint> last_update_;
    std::size_t lightmap_import_count_{0U};
    std::size_t lightmap_set_publication_count_{0U};
    std::size_t render_package_publication_count_{0U};
    std::size_t bsp_scene_parse_count_{0U};
    std::size_t brush_library_build_count_{0U};
    std::size_t world_scene_publication_count_{0U};
    bool network_cleanup_done_{false};
};

WorldRenderPackageStage::WorldRenderPackageStage(
    network::IDatagramTransport& transport,
    const network::NetworkAddress remote_endpoint,
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment,
    const assets::AssetImporterRegistries& importer_registries,
    WorldRenderPackageStageConfig config,
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
    WorldTextureImportTraceCallback world_texture_trace_callback,
    WorldRenderPackageTraceCallback trace_callback)
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
          std::move(world_texture_trace_callback),
          std::move(trace_callback))}
{
}

WorldRenderPackageStage::~WorldRenderPackageStage() = default;

bool WorldRenderPackageStage::start(
    const WorldRenderPackageStageTimePoint now,
    const network::NetworkAddress& expected_local_endpoint,
    std::unique_ptr<INetchanDriverLifetime> connection_lifetime)
{
    return implementation_->start(
        now, expected_local_endpoint, std::move(connection_lifetime));
}

void WorldRenderPackageStage::update(const WorldRenderPackageStageTimePoint now)
{
    implementation_->update(now);
}

void WorldRenderPackageStage::cancel(const WorldRenderPackageStageTimePoint now)
{
    implementation_->cancel(now);
}

std::optional<WorldRenderPackageStageEvent>
WorldRenderPackageStage::poll_event()
{
    return implementation_->poll_event();
}

WorldRenderPackageStageState WorldRenderPackageStage::state() const noexcept
{
    return implementation_->state_;
}

bool WorldRenderPackageStage::terminal() const noexcept
{
    return terminal_state(implementation_->state_);
}

const std::shared_ptr<const world_render::WorldRenderPackage>&
WorldRenderPackageStage::result() const noexcept
{
    return implementation_->result_;
}

const std::shared_ptr<const world_scene_render::WorldSceneRenderPackage>&
WorldRenderPackageStage::scene_result() const noexcept
{
    return implementation_->scene_result_;
}

const std::optional<brush_models::GoldSrcSpawnCameraExtractionResult>&
WorldRenderPackageStage::spawn_camera_result() const noexcept
{
    return implementation_->spawn_camera_result_;
}

const std::optional<WorldRenderPackageStageError>&
WorldRenderPackageStage::error() const noexcept
{
    return implementation_->error_;
}

const network::NetworkAddress& WorldRenderPackageStage::remote_endpoint()
    const noexcept
{
    return implementation_->world_texture_stage_.remote_endpoint();
}

const std::optional<network::NetworkAddress>&
WorldRenderPackageStage::local_endpoint() const noexcept
{
    return implementation_->world_texture_stage_.local_endpoint();
}

std::size_t WorldRenderPackageStage::pending_event_count() const noexcept
{
    return implementation_->event_size_;
}

std::size_t WorldRenderPackageStage::transmitted_packet_count() const noexcept
{
    return implementation_->world_texture_stage_.transmitted_packet_count();
}

std::optional<std::size_t>
WorldRenderPackageStage::transmitted_packet_count_at_manifest_publication()
    const noexcept
{
    return implementation_->world_texture_stage_
        .transmitted_packet_count_at_manifest_publication();
}

std::size_t WorldRenderPackageStage::cleanup_count() const noexcept
{
    return implementation_->world_texture_stage_.cleanup_count();
}

std::size_t WorldRenderPackageStage::manifest_publication_count() const noexcept
{
    return implementation_->world_texture_stage_.manifest_publication_count();
}

std::size_t WorldRenderPackageStage::bsp_source_open_attempt_count()
    const noexcept
{
    return implementation_->world_texture_stage_.bsp_source_open_attempt_count();
}

std::size_t WorldRenderPackageStage::importer_dispatch_count() const noexcept
{
    return implementation_->world_texture_stage_.importer_dispatch_count();
}

std::size_t WorldRenderPackageStage::wad_source_open_attempt_count()
    const noexcept
{
    return implementation_->world_texture_stage_.wad_source_open_attempt_count();
}

std::size_t WorldRenderPackageStage::texture_set_publication_count()
    const noexcept
{
    return implementation_->world_texture_stage_.texture_set_publication_count();
}

std::size_t WorldRenderPackageStage::lightmap_import_count() const noexcept
{
    return implementation_->lightmap_import_count_;
}

std::size_t WorldRenderPackageStage::lightmap_set_publication_count()
    const noexcept
{
    return implementation_->lightmap_set_publication_count_;
}

std::size_t WorldRenderPackageStage::render_package_publication_count()
    const noexcept
{
    return implementation_->render_package_publication_count_;
}

std::size_t WorldRenderPackageStage::bsp_scene_parse_count() const noexcept
{
    return implementation_->bsp_scene_parse_count_;
}

std::size_t WorldRenderPackageStage::brush_library_build_count() const noexcept
{
    return implementation_->brush_library_build_count_;
}

std::size_t WorldRenderPackageStage::world_scene_publication_count()
    const noexcept
{
    return implementation_->world_scene_publication_count_;
}

} // namespace hlclient::goldsrc
