#pragma once

#include <hlclient/goldsrc/brush_models/goldsrc_brush_render_library.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_world_scene_builder.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/lightmaps/goldsrc_world_lightmap_import.hpp>
#include <hlclient/goldsrc/world_textures/world_texture_import_stage.hpp>
#include <hlclient/world_render/world_render_package_builder.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::goldsrc {

inline constexpr std::size_t kDefaultMaximumWorldRenderPackageStageEvents =
    2'048U;
inline constexpr std::size_t
    kWorldRenderPackageSuccessfulPublicationEventCount = 5U;
// Capacities below the five-event success publication are deliberately valid:
// they provide a bounded deterministic backpressure profile and terminate
// before lightmap import instead of allocating or dropping progress events.
inline constexpr std::size_t kMinimumWorldRenderPackageStageEvents = 1U;
inline constexpr std::size_t kMaximumWorldRenderPackageStageEvents = 8'192U;
inline constexpr std::size_t kWorldRenderPackageStageDiagnosticTextLimit = 256U;

using WorldRenderPackageStageTimePoint = WorldTextureImportStageTimePoint;

struct WorldRenderPackageStageConfig {
    WorldTextureImportStageConfig world_textures;
    lightmaps::GoldSrcWorldLightmapImportLimits lightmaps;
    world_render::WorldRenderPackageLimits render_package;
    // Opt-in M4.4 continuation. The historical M4.3 package boundary remains
    // byte-for-byte compatible when this is false.
    bool build_world_spatial_scene{false};
    bsp::GoldSrcBspImportLimits bsp;
    brush_models::GoldSrcBrushRenderLibraryLimits brush_library;
    brush_models::GoldSrcWorldSceneBuildConfig world_scene;
    brush_models::GoldSrcWorldSceneBuildLimits world_scene_limits;
    std::size_t maximum_stage_events{
        kDefaultMaximumWorldRenderPackageStageEvents};
};

[[nodiscard]] bool valid_world_render_package_stage_configuration(
    const WorldRenderPackageStageConfig& config) noexcept;

enum class WorldRenderPackageStageState {
    idle,
    waiting_for_world_textures,
    importing_lightmaps,
    packing_lightmap_atlases,
    building_render_package,
    world_render_package_ready,
    world_textures_incomplete,
    lightmap_import_failed,
    render_package_failed,
    timed_out,
    cancelled,
    backpressure,
    network_error,
    protocol_error,
};

enum class WorldRenderPackageStageErrorCode {
    invalid_configuration,
    world_texture_start_failed,
    world_texture_failed,
    world_textures_incomplete,
    retained_world_missing,
    retained_bsp_source_missing,
    lightmap_import_failed,
    render_package_build_failed,
    world_scene_bsp_parse_failed,
    brush_render_library_build_failed,
    world_scene_build_failed,
    event_backpressure,
    unable_to_retain_package,
};

struct WorldRenderPackageStageError {
    WorldRenderPackageStageErrorCode code{
        WorldRenderPackageStageErrorCode::invalid_configuration};
    std::optional<WorldTextureImportStageErrorCode> world_texture_code;
    std::optional<lightmaps::GoldSrcWorldLightmapImportErrorCode>
        lightmap_code;
    std::optional<world_render::WorldRenderPackageErrorCode>
        render_package_code;
    std::optional<bsp::GoldSrcBspErrorCode> bsp_code;
    std::optional<brush_models::GoldSrcBrushRenderLibraryErrorCode>
        brush_library_code;
    std::optional<brush_models::GoldSrcWorldSceneBuildErrorCode>
        world_scene_code;
    std::string context;
};

enum class WorldRenderPackageStageEventType {
    world_textures_ready,
    lightmap_import_started,
    lightmap_atlases_ready,
    render_package_build_started,
    world_render_package_ready,
    world_textures_incomplete,
    lightmap_import_failed,
    render_package_failed,
    timeout,
    cancelled,
    backpressure,
    network_error,
    protocol_error,
};

// Bounded metadata only: no paths, source bytes, pixels, native handles, or
// network payloads cross the stage event boundary.
struct WorldRenderPackageStageEvent {
    WorldRenderPackageStageEventType type{
        WorldRenderPackageStageEventType::protocol_error};
    std::size_t surface_count{0U};
    std::size_t atlas_page_count{0U};
    std::size_t vertex_count{0U};
    std::size_t index_count{0U};
    std::size_t batch_count{0U};
    WorldRenderPackageStageTimePoint occurred_at{};
};

enum class WorldRenderPackageTraceClassification {
    stage_started,
    world_textures_ready,
    lightmap_import_started,
    lightmap_atlases_ready,
    render_package_build_started,
    world_render_package_ready,
    world_textures_incomplete,
    lightmap_import_failed,
    render_package_failed,
    stage_timed_out,
    stage_cancelled,
    backpressure,
    network_failure,
    protocol_failure,
};

struct WorldRenderPackageTraceEvent {
    WorldRenderPackageTraceClassification classification{
        WorldRenderPackageTraceClassification::stage_started};
    WorldRenderPackageStageState state{WorldRenderPackageStageState::idle};
    network::NetworkAddress endpoint;
    std::size_t surface_count{0U};
    std::size_t atlas_page_count{0U};
    std::size_t vertex_count{0U};
    std::size_t index_count{0U};
    std::size_t batch_count{0U};
    std::size_t transmitted_packet_count{0U};
};

using WorldRenderPackageTraceCallback =
    std::function<void(const WorldRenderPackageTraceEvent&)>;

// Continues the complete texture-import chain using its retained connection
// lifetime. All post-texture work is caller-driven CPU work. Publication
// finalizes the retained network/auth boundary before a consumer may preview.
class WorldRenderPackageStage final {
public:
    WorldRenderPackageStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        const assets::AssetImporterRegistries& importer_registries,
        WorldRenderPackageStageConfig config = {},
        resource_consistency::IResourceConsistencyProvider*
            consistency_provider = nullptr,
        InitialSignonTraceCallback initial_trace_callback = {},
        PreResourceSignonTraceCallback pre_resource_trace_callback = {},
        DeltaDescriptionTraceCallback delta_trace_callback = {},
        MovementEnvironmentTraceCallback movement_trace_callback = {},
        UserInfoSignonTraceCallback user_info_trace_callback = {},
        ResourceTransitionTraceCallback transition_trace_callback = {},
        ResourceListTraceCallback resource_list_trace_callback = {},
        ResourceClientResponseTraceCallback response_trace_callback = {},
        PrecacheManifestTraceCallback manifest_trace_callback = {},
        PrecacheAssetDispatchTraceCallback asset_dispatch_trace_callback = {},
        WorldTextureImportTraceCallback world_texture_trace_callback = {},
        WorldRenderPackageTraceCallback trace_callback = {});
    ~WorldRenderPackageStage();

    WorldRenderPackageStage(const WorldRenderPackageStage&) = delete;
    WorldRenderPackageStage& operator=(const WorldRenderPackageStage&) = delete;
    WorldRenderPackageStage(WorldRenderPackageStage&&) = delete;
    WorldRenderPackageStage& operator=(WorldRenderPackageStage&&) = delete;

    [[nodiscard]] bool start(
        WorldRenderPackageStageTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime = {});
    void update(WorldRenderPackageStageTimePoint now);
    void cancel(WorldRenderPackageStageTimePoint now);

    [[nodiscard]] std::optional<WorldRenderPackageStageEvent> poll_event();
    [[nodiscard]] WorldRenderPackageStageState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] const std::shared_ptr<
        const world_render::WorldRenderPackage>& result() const noexcept;
    [[nodiscard]] const std::shared_ptr<const
        world_scene_render::WorldSceneRenderPackage>& scene_result()
        const noexcept;
    [[nodiscard]] const std::optional<
        brush_models::GoldSrcSpawnCameraExtractionResult>&
    spawn_camera_result() const noexcept;
    [[nodiscard]] const std::optional<WorldRenderPackageStageError>& error()
        const noexcept;
    [[nodiscard]] const network::NetworkAddress& remote_endpoint()
        const noexcept;
    [[nodiscard]] const std::optional<network::NetworkAddress>& local_endpoint()
        const noexcept;
    [[nodiscard]] std::size_t pending_event_count() const noexcept;
    [[nodiscard]] std::size_t transmitted_packet_count() const noexcept;
    [[nodiscard]] std::optional<std::size_t>
    transmitted_packet_count_at_manifest_publication() const noexcept;
    [[nodiscard]] std::size_t cleanup_count() const noexcept;
    [[nodiscard]] std::size_t manifest_publication_count() const noexcept;
    [[nodiscard]] std::size_t bsp_source_open_attempt_count() const noexcept;
    [[nodiscard]] std::size_t importer_dispatch_count() const noexcept;
    [[nodiscard]] std::size_t wad_source_open_attempt_count() const noexcept;
    [[nodiscard]] std::size_t texture_set_publication_count() const noexcept;
    [[nodiscard]] std::size_t lightmap_import_count() const noexcept;
    [[nodiscard]] std::size_t lightmap_set_publication_count() const noexcept;
    [[nodiscard]] std::size_t render_package_publication_count() const noexcept;
    [[nodiscard]] std::size_t bsp_scene_parse_count() const noexcept;
    [[nodiscard]] std::size_t brush_library_build_count() const noexcept;
    [[nodiscard]] std::size_t world_scene_publication_count() const noexcept;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const WorldRenderPackageStageErrorCode code) noexcept
{
    switch (code) {
    case WorldRenderPackageStageErrorCode::invalid_configuration:
        return "invalid_configuration";
    case WorldRenderPackageStageErrorCode::world_texture_start_failed:
        return "world_texture_start_failed";
    case WorldRenderPackageStageErrorCode::world_texture_failed:
        return "world_texture_failed";
    case WorldRenderPackageStageErrorCode::world_textures_incomplete:
        return "world_textures_incomplete";
    case WorldRenderPackageStageErrorCode::retained_world_missing:
        return "retained_world_missing";
    case WorldRenderPackageStageErrorCode::retained_bsp_source_missing:
        return "retained_bsp_source_missing";
    case WorldRenderPackageStageErrorCode::lightmap_import_failed:
        return "lightmap_import_failed";
    case WorldRenderPackageStageErrorCode::render_package_build_failed:
        return "render_package_build_failed";
    case WorldRenderPackageStageErrorCode::world_scene_bsp_parse_failed:
        return "world_scene_bsp_parse_failed";
    case WorldRenderPackageStageErrorCode::brush_render_library_build_failed:
        return "brush_render_library_build_failed";
    case WorldRenderPackageStageErrorCode::world_scene_build_failed:
        return "world_scene_build_failed";
    case WorldRenderPackageStageErrorCode::event_backpressure:
        return "event_backpressure";
    case WorldRenderPackageStageErrorCode::unable_to_retain_package:
        return "unable_to_retain_package";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
