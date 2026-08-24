#pragma once

#include <hlclient/goldsrc/precache_asset_dispatch_stage.hpp>
#include <hlclient/goldsrc/world_textures/world_texture_import.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

class WorldRenderPackageStage;

inline constexpr std::size_t kDefaultMaximumWorldTextureImportStageEvents =
    2'048U;
inline constexpr std::size_t kMinimumWorldTextureImportStageEvents = 3U;
inline constexpr std::size_t kMaximumWorldTextureImportStageEvents = 8'192U;
inline constexpr std::size_t kWorldTextureImportStageDiagnosticTextLimit = 256U;

using WorldTextureImportStageTimePoint = PrecacheAssetDispatchStageTimePoint;

struct WorldTextureImportStageConfig {
    PrecacheAssetDispatchStageConfig asset_dispatch;
    GoldSrcWorldTextureImportLimits texture_import;
    std::size_t maximum_stage_events{
        kDefaultMaximumWorldTextureImportStageEvents};
};

[[nodiscard]] bool valid_world_texture_import_stage_configuration(
    const WorldTextureImportStageConfig& config) noexcept;

enum class WorldTextureImportStageState {
    idle,
    waiting_for_world_geometry,
    parsing_texture_sources,
    decoding_embedded_textures,
    resolving_wad_archives,
    decoding_external_textures,
    building_texture_set,
    world_textures_ready,
    world_textures_incomplete,
    world_geometry_unavailable,
    worldspawn_parse_failed,
    wad_reference_invalid,
    wad_source_unavailable,
    wad_source_open_failed,
    wad_catalog_failed,
    texture_decode_failed,
    timed_out,
    cancelled,
    backpressure,
    network_error,
    protocol_error,
};

enum class WorldTextureImportStageErrorCode {
    invalid_configuration,
    asset_dispatch_start_failed,
    asset_dispatch_failed,
    imported_world_missing,
    retained_bsp_source_missing,
    texture_import_begin_failed,
    texture_import_failed,
    event_backpressure,
    post_manifest_transmit_changed,
    unable_to_retain_result,
};

struct WorldTextureImportStageError {
    WorldTextureImportStageErrorCode code{
        WorldTextureImportStageErrorCode::invalid_configuration};
    std::optional<PrecacheAssetDispatchStageErrorCode> asset_dispatch_code;
    std::optional<WorldTextureImportErrorCode> texture_import_code;
    std::string context;
};

enum class WorldTextureImportStageEventType {
    world_geometry_ready,
    texture_import_started,
    texture_import_progress,
    wad_source_open_started,
    wad_source_ready,
    world_textures_ready,
    world_textures_incomplete,
    world_geometry_unavailable,
    worldspawn_parse_failed,
    wad_reference_invalid,
    wad_source_unavailable,
    wad_source_open_failed,
    wad_catalog_failed,
    texture_decode_failed,
    timeout,
    cancelled,
    backpressure,
    network_error,
    protocol_error,
};

// Bounded metadata only. No texture/archive names, paths, source bytes, pixel
// bytes, palettes, native handles, or network payloads are exposed.
struct WorldTextureImportStageEvent {
    WorldTextureImportStageEventType type{
        WorldTextureImportStageEventType::protocol_error};
    std::size_t material_count{0U};
    std::size_t texture_count{0U};
    std::size_t binding_count{0U};
    std::size_t archive_count{0U};
    std::size_t unresolved_binding_count{0U};
    std::size_t pixel_conversion_bytes{0U};
    WorldTextureImportStageTimePoint occurred_at{};
};

enum class WorldTextureImportTraceClassification {
    stage_started,
    world_geometry_ready,
    texture_import_started,
    texture_import_progress,
    wad_source_open_started,
    wad_source_ready,
    world_textures_ready,
    world_textures_incomplete,
    world_geometry_unavailable,
    worldspawn_parse_failed,
    wad_reference_invalid,
    wad_source_unavailable,
    wad_source_open_failed,
    wad_catalog_failed,
    texture_decode_failed,
    stage_timed_out,
    stage_cancelled,
    backpressure,
    network_failure,
    protocol_failure,
};

struct WorldTextureImportTraceEvent {
    WorldTextureImportTraceClassification classification{
        WorldTextureImportTraceClassification::stage_started};
    WorldTextureImportStageState state{WorldTextureImportStageState::idle};
    network::NetworkAddress endpoint;
    std::size_t material_count{0U};
    std::size_t texture_count{0U};
    std::size_t binding_count{0U};
    std::size_t archive_count{0U};
    std::size_t unresolved_binding_count{0U};
    std::size_t pixel_conversion_bytes{0U};
    std::size_t transmitted_packet_count{0U};
};

using WorldTextureImportTraceCallback =
    std::function<void(const WorldTextureImportTraceEvent&)>;

class TexturedWorldAssetState final {
public:
    TexturedWorldAssetState(TexturedWorldAssetState&&) noexcept;
    TexturedWorldAssetState& operator=(TexturedWorldAssetState&&) noexcept;
    TexturedWorldAssetState(const TexturedWorldAssetState&) = delete;
    TexturedWorldAssetState& operator=(const TexturedWorldAssetState&) = delete;
    ~TexturedWorldAssetState();

    [[nodiscard]] const ApprovedAssetDispatchState& dispatch_state()
        const noexcept;
    [[nodiscard]] const assets::WorldAsset& world() const noexcept;
    [[nodiscard]] const assets::WorldTextureSet& textures() const noexcept;
    [[nodiscard]] const std::shared_ptr<
        const local_resources::LocalResourceEnvironment>&
    environment() const noexcept;
private:
    friend class WorldTextureImportStage;
    friend class WorldRenderPackageStage;
    // Single-use ownership transfer for the later renderer-neutral package
    // boundary. Keeping this private prevents public observers from calling
    // world() after the transfer has consumed the retained asset.
    [[nodiscard]] std::optional<assets::TexturedWorldAsset>
    take_textured_world() noexcept;
    class Implementation;
    explicit TexturedWorldAssetState(
        std::unique_ptr<Implementation> implementation) noexcept;
    std::unique_ptr<Implementation> implementation_;
};

// Owns the complete pre-texture stage chain. After the nested asset stage
// publishes world geometry, only local caller-driven work is performed and the
// retained NetchanDriver is never updated or queued again.
class WorldTextureImportStage final {
public:
    WorldTextureImportStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        const assets::AssetImporterRegistries& importer_registries,
        WorldTextureImportStageConfig config = {},
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
        WorldTextureImportTraceCallback trace_callback = {});
    ~WorldTextureImportStage();

    WorldTextureImportStage(const WorldTextureImportStage&) = delete;
    WorldTextureImportStage& operator=(const WorldTextureImportStage&) = delete;
    WorldTextureImportStage(WorldTextureImportStage&&) = delete;
    WorldTextureImportStage& operator=(WorldTextureImportStage&&) = delete;

    [[nodiscard]] bool start(
        WorldTextureImportStageTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime = {});
    void update(WorldTextureImportStageTimePoint now);
    void cancel(WorldTextureImportStageTimePoint now);

    [[nodiscard]] std::optional<WorldTextureImportStageEvent> poll_event();
    [[nodiscard]] WorldTextureImportStageState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] const std::optional<TexturedWorldAssetState>& result()
        const noexcept;
    [[nodiscard]] const std::optional<WorldTextureImportStageError>& error()
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

private:
    friend class WorldRenderPackageStage;

    struct RetainConnectionAtBoundary final {};

    WorldTextureImportStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
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
        WorldTextureImportTraceCallback trace_callback,
        RetainConnectionAtBoundary);
    [[nodiscard]] std::optional<TexturedWorldAssetState> take_result() noexcept;
    void finalize_retained_boundary(
        WorldTextureImportStageTimePoint now) noexcept;

    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const WorldTextureImportStageErrorCode code) noexcept
{
    switch (code) {
    case WorldTextureImportStageErrorCode::invalid_configuration:
        return "invalid_configuration";
    case WorldTextureImportStageErrorCode::asset_dispatch_start_failed:
        return "asset_dispatch_start_failed";
    case WorldTextureImportStageErrorCode::asset_dispatch_failed:
        return "asset_dispatch_failed";
    case WorldTextureImportStageErrorCode::imported_world_missing:
        return "imported_world_missing";
    case WorldTextureImportStageErrorCode::retained_bsp_source_missing:
        return "retained_bsp_source_missing";
    case WorldTextureImportStageErrorCode::texture_import_begin_failed:
        return "texture_import_begin_failed";
    case WorldTextureImportStageErrorCode::texture_import_failed:
        return "texture_import_failed";
    case WorldTextureImportStageErrorCode::event_backpressure:
        return "event_backpressure";
    case WorldTextureImportStageErrorCode::post_manifest_transmit_changed:
        return "post_manifest_transmit_changed";
    case WorldTextureImportStageErrorCode::unable_to_retain_result:
        return "unable_to_retain_result";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
