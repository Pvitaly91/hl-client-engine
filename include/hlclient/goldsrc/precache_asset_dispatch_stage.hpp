#pragma once

#include <hlclient/assets/asset_importer_dispatcher.hpp>
#include <hlclient/goldsrc/precache_asset_dispatch.hpp>
#include <hlclient/goldsrc/precache_manifest_stage.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

using PrecacheAssetDispatchStageClock = PrecacheManifestStageClock;
using PrecacheAssetDispatchStageTimePoint = PrecacheManifestStageTimePoint;

// A default-limit 16 MiB source read in 64 KiB increments can publish 256
// progress events before the bounded lifecycle/dispatch events are counted.
// Keep the default queue large enough for that valid no-poll execution path.
inline constexpr std::size_t kDefaultMaximumPrecacheAssetDispatchEvents = 512U;
// One dispatch update can atomically publish probe, selection, and terminal
// events. Smaller queues could never complete even when drained every update.
inline constexpr std::size_t kMinimumPrecacheAssetDispatchEvents = 3U;
inline constexpr std::size_t kMaximumPrecacheAssetDispatchEvents = 8'192U;
inline constexpr std::size_t kPrecacheAssetDispatchStageDiagnosticTextLimit =
    256U;

struct PrecacheAssetDispatchStageConfig {
    PrecacheManifestStageConfig manifest;
    local_assets::LocalAssetSourceOpenLimits source_open;
    std::size_t maximum_stage_events{
        kDefaultMaximumPrecacheAssetDispatchEvents};
};

[[nodiscard]] bool valid_precache_asset_dispatch_stage_configuration(
    const PrecacheAssetDispatchStageConfig& config) noexcept;

enum class PrecacheAssetDispatchStageState {
    idle,
    waiting_for_precache_manifest,
    selecting_world_entry,
    opening_asset_source,
    asset_source_ready,
    probing_importers,
    importing_asset,
    asset_imported,
    importer_boundary_reached,
    world_source_unavailable,
    source_open_failed,
    ambiguous_importer,
    import_failed,
    timed_out,
    cancelled,
    backpressure,
    network_error,
    protocol_error,
};

enum class PrecacheAssetDispatchStageErrorCode {
    invalid_configuration,
    manifest_stage_start_failed,
    manifest_stage_failed,
    retained_driver_missing,
    world_source_unavailable,
    dispatch_plan_failed,
    source_open_begin_failed,
    source_open_failed,
    approved_source_failed,
    ambiguous_importer,
    import_failed,
    event_backpressure,
    time_moved_backwards,
    post_manifest_transmit_changed,
    unable_to_retain_result,
};

struct PrecacheAssetDispatchStageError {
    PrecacheAssetDispatchStageErrorCode code{
        PrecacheAssetDispatchStageErrorCode::invalid_configuration};
    std::optional<PrecacheManifestStageErrorCode> manifest_code;
    std::optional<AssetDispatchPlanErrorCode> plan_code;
    std::optional<ApprovedAssetSourceOpenErrorCode> source_open_code;
    std::optional<local_assets::LocalAssetSourceOpenErrorCode>
        local_source_open_code;
    std::optional<local_resources::LocalResourceLocatorReopenErrorCode>
        locator_reopen_code;
    std::optional<local_resources::LocalReadOnlyFileErrorCode> read_code;
    std::optional<ApprovedAssetSourceErrorCode> approved_source_code;
    std::optional<assets::AssetDispatchState> dispatch_state;
    std::optional<assets::AssetErrorCode> asset_code;
    std::string context;
};

enum class PrecacheAssetDispatchStageEventType {
    precache_manifest_ready,
    world_entry_selected,
    asset_source_open_started,
    asset_source_progress,
    asset_source_ready,
    importer_probe_completed,
    importer_selected,
    asset_imported,
    importer_boundary_reached,
    world_source_unavailable,
    source_open_failed,
    ambiguous_importer,
    import_failed,
    timeout,
    cancelled,
    backpressure,
    network_error,
    protocol_error,
};

// Bounded local metadata only. No path, source bytes, stable identity, network
// payload, or parser input is present in stage events.
struct PrecacheAssetDispatchStageEvent {
    PrecacheAssetDispatchStageEventType type{
        PrecacheAssetDispatchStageEventType::protocol_error};
    ResourceType resource_type{ResourceType::model};
    std::uint16_t resource_index{0U};
    std::size_t wire_ordinal{0U};
    assets::AssetDispatchRole role{assets::AssetDispatchRole::unsupported};
    std::uint64_t byte_count{0U};
    std::uint64_t progress_bytes{0U};
    assets::AssetImporterCategory importer_category{
        assets::AssetImporterCategory::none};
    std::string importer_id;
    std::optional<assets::AssetDispatchState> dispatch_state;
    PrecacheAssetDispatchStageTimePoint occurred_at{};
};

enum class PrecacheAssetDispatchTraceClassification {
    stage_started,
    precache_manifest_ready,
    world_entry_selected,
    asset_source_open_started,
    asset_source_progress,
    asset_source_ready,
    importer_probe_completed,
    importer_selected,
    asset_imported,
    importer_boundary_reached,
    world_source_unavailable,
    source_open_failed,
    ambiguous_importer,
    import_failed,
    stage_timed_out,
    stage_cancelled,
    backpressure,
    network_failure,
    protocol_failure,
};

struct PrecacheAssetDispatchTraceEvent {
    PrecacheAssetDispatchTraceClassification classification{
        PrecacheAssetDispatchTraceClassification::stage_started};
    PrecacheAssetDispatchStageState state{
        PrecacheAssetDispatchStageState::idle};
    network::NetworkAddress endpoint;
    ResourceType resource_type{ResourceType::model};
    std::uint16_t resource_index{0U};
    std::size_t wire_ordinal{0U};
    assets::AssetDispatchRole role{assets::AssetDispatchRole::unsupported};
    std::uint64_t byte_count{0U};
    std::uint64_t progress_bytes{0U};
    assets::AssetImporterCategory importer_category{
        assets::AssetImporterCategory::none};
    std::string importer_id;
    std::optional<assets::AssetDispatchState> dispatch_state;
    std::size_t transmitted_packet_count{0U};
};

using PrecacheAssetDispatchTraceCallback =
    std::function<void(const PrecacheAssetDispatchTraceEvent&)>;

// Owns the source and importer outcome together with a bounded copy of the
// exact manifest prerequisite. The retained network driver is never exposed.
class ApprovedAssetDispatchState final {
public:
    ApprovedAssetDispatchState(ApprovedAssetDispatchState&&) noexcept;
    ApprovedAssetDispatchState& operator=(ApprovedAssetDispatchState&&) noexcept;
    ApprovedAssetDispatchState(const ApprovedAssetDispatchState&) = delete;
    ApprovedAssetDispatchState& operator=(const ApprovedAssetDispatchState&) =
        delete;
    ~ApprovedAssetDispatchState();

    [[nodiscard]] const PrecacheManifestState& manifest() const noexcept;
    [[nodiscard]] const std::shared_ptr<
        const local_resources::LocalResourceEnvironment>&
    environment() const noexcept;
    [[nodiscard]] const AssetDispatchPlan& plan() const noexcept;
    [[nodiscard]] const ApprovedAssetSource& source() const noexcept;
    [[nodiscard]] const assets::AssetDispatchResult& dispatch_result()
        const noexcept;
    [[nodiscard]] const std::optional<assets::ImportedAsset>& imported_asset()
        const noexcept;
    [[nodiscard]] std::uint64_t source_byte_count() const noexcept;

private:
    friend class PrecacheAssetDispatchStage;
    class Implementation;

    explicit ApprovedAssetDispatchState(
        std::unique_ptr<Implementation> implementation) noexcept;

    std::unique_ptr<Implementation> implementation_;
};

// Continues one privately retained PrecacheManifestStage. Once the manifest is
// published, only bounded local source and importer work is performed; the
// retained NetchanDriver is neither updated nor queued.
class PrecacheAssetDispatchStage final {
public:
    // importer_registries is non-owning. It must outlive the stage and remain
    // structurally immutable until the stage is destroyed.
    PrecacheAssetDispatchStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        const assets::AssetImporterRegistries& importer_registries,
        PrecacheAssetDispatchStageConfig config = {},
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
        PrecacheAssetDispatchTraceCallback trace_callback = {});
    ~PrecacheAssetDispatchStage();

    PrecacheAssetDispatchStage(const PrecacheAssetDispatchStage&) = delete;
    PrecacheAssetDispatchStage& operator=(const PrecacheAssetDispatchStage&) =
        delete;
    PrecacheAssetDispatchStage(PrecacheAssetDispatchStage&&) = delete;
    PrecacheAssetDispatchStage& operator=(PrecacheAssetDispatchStage&&) =
        delete;

    [[nodiscard]] bool start(
        PrecacheAssetDispatchStageTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime = {});
    void update(PrecacheAssetDispatchStageTimePoint now);
    void cancel(PrecacheAssetDispatchStageTimePoint now);

    [[nodiscard]] std::optional<PrecacheAssetDispatchStageEvent> poll_event();
    [[nodiscard]] PrecacheAssetDispatchStageState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] const std::optional<ApprovedAssetDispatchState>& result()
        const noexcept;
    [[nodiscard]] const std::optional<PrecacheAssetDispatchStageError>& error()
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
    [[nodiscard]] std::size_t source_open_attempt_count() const noexcept;
    [[nodiscard]] std::size_t importer_dispatch_count() const noexcept;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const PrecacheAssetDispatchStageErrorCode code) noexcept
{
    switch (code) {
    case PrecacheAssetDispatchStageErrorCode::invalid_configuration:
        return "invalid_configuration";
    case PrecacheAssetDispatchStageErrorCode::manifest_stage_start_failed:
        return "manifest_stage_start_failed";
    case PrecacheAssetDispatchStageErrorCode::manifest_stage_failed:
        return "manifest_stage_failed";
    case PrecacheAssetDispatchStageErrorCode::retained_driver_missing:
        return "retained_driver_missing";
    case PrecacheAssetDispatchStageErrorCode::world_source_unavailable:
        return "world_source_unavailable";
    case PrecacheAssetDispatchStageErrorCode::dispatch_plan_failed:
        return "dispatch_plan_failed";
    case PrecacheAssetDispatchStageErrorCode::source_open_begin_failed:
        return "source_open_begin_failed";
    case PrecacheAssetDispatchStageErrorCode::source_open_failed:
        return "source_open_failed";
    case PrecacheAssetDispatchStageErrorCode::approved_source_failed:
        return "approved_source_failed";
    case PrecacheAssetDispatchStageErrorCode::ambiguous_importer:
        return "ambiguous_importer";
    case PrecacheAssetDispatchStageErrorCode::import_failed:
        return "import_failed";
    case PrecacheAssetDispatchStageErrorCode::event_backpressure:
        return "event_backpressure";
    case PrecacheAssetDispatchStageErrorCode::time_moved_backwards:
        return "time_moved_backwards";
    case PrecacheAssetDispatchStageErrorCode::post_manifest_transmit_changed:
        return "post_manifest_transmit_changed";
    case PrecacheAssetDispatchStageErrorCode::unable_to_retain_result:
        return "unable_to_retain_result";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
