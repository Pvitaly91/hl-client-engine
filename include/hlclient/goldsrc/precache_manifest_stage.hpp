#pragma once

#include <hlclient/goldsrc/local_resource_inventory.hpp>
#include <hlclient/goldsrc/precache_manifest.hpp>
#include <hlclient/goldsrc/resource_client_response_stage.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::goldsrc {

class GoldSrcHandshakeCoordinator;

using PrecacheManifestStageClock = ResourceClientResponseStageClock;
using PrecacheManifestStageTimePoint = ResourceClientResponseStageTimePoint;

inline constexpr std::size_t kPrecacheManifestStageDiagnosticTextLimit = 256U;

struct PrecacheManifestStageConfig {
    ResourceClientResponseStageConfig response;
    LocalResourceInventoryLimits inventory;
    PrecacheManifestLimits manifest;
};

[[nodiscard]] bool valid_precache_manifest_stage_configuration(
    const PrecacheManifestStageConfig& config) noexcept;

enum class PrecacheManifestStageState {
    idle,
    waiting_for_resource_response_boundary,
    building_local_inventory,
    building_precache_manifest,
    precache_manifest_ready,
    local_resources_incomplete,
    unsafe_local_resources,
    unsupported_local_profile,
    local_resource_io_error,
    timed_out,
    cancelled,
    backpressure,
    secondary_stream_pending,
    network_error,
    protocol_error,
};

enum class PrecacheManifestStageErrorCode {
    invalid_configuration,
    response_stage_start_failed,
    response_stage_failed,
    retained_driver_missing,
    inventory_build_failed,
    manifest_build_failed,
    event_backpressure,
    time_moved_backwards,
};

struct PrecacheManifestStageError {
    PrecacheManifestStageErrorCode code{
        PrecacheManifestStageErrorCode::invalid_configuration};
    std::optional<ResourceClientResponseStageErrorCode> response_code;
    std::optional<LocalResourceInventoryErrorCode> inventory_code;
    std::optional<PrecacheManifestErrorCode> manifest_code;
    std::string context;
};

// Owns every protocol and local-metadata prerequisite needed at the M3.2.2
// boundary. The shared environment keeps locator root IDs usable without
// placing native paths or handles in the immutable manifest itself.
class PrecacheManifestSignonState final {
public:
    ~PrecacheManifestSignonState();
    PrecacheManifestSignonState(PrecacheManifestSignonState&&) noexcept;
    PrecacheManifestSignonState& operator=(
        PrecacheManifestSignonState&&) noexcept;
    PrecacheManifestSignonState(const PrecacheManifestSignonState&) = delete;
    PrecacheManifestSignonState& operator=(
        const PrecacheManifestSignonState&) = delete;

    [[nodiscard]] const ResourceClientResponseSignonState& response()
        const noexcept;
    [[nodiscard]] const LocalResourceInventoryState& inventory()
        const noexcept;
    [[nodiscard]] const PrecacheManifestState& manifest() const noexcept;
    [[nodiscard]] const std::shared_ptr<
        const local_resources::LocalResourceEnvironment>&
    environment() const noexcept;

private:
    friend class PrecacheManifestStage;
    class Implementation;

    explicit PrecacheManifestSignonState(
        std::unique_ptr<Implementation> implementation) noexcept;

    std::unique_ptr<Implementation> implementation_;
};

enum class PrecacheManifestStageEventType {
    resource_response_boundary_reached,
    local_inventory_ready,
    precache_manifest_ready,
    local_resources_incomplete,
    unsafe_local_resources,
    unsupported_local_profile,
    local_resource_io_error,
    timeout,
    cancelled,
    backpressure,
    secondary_stream_pending,
    network_error,
    protocol_error,
};

struct PrecacheManifestStageEvent {
    PrecacheManifestStageEventType type{
        PrecacheManifestStageEventType::protocol_error};
    std::size_t entry_count{0U};
    std::size_t ready_count{0U};
    std::size_t metadata_only_count{0U};
    std::size_t missing_count{0U};
    std::size_t unsafe_count{0U};
    std::size_t unsupported_count{0U};
    std::size_t ambiguous_count{0U};
    std::size_t io_error_count{0U};
    PrecacheManifestStageTimePoint occurred_at{};
};

enum class PrecacheManifestTraceClassification {
    stage_started,
    resource_response_boundary_reached,
    local_inventory_ready,
    precache_manifest_ready,
    local_resources_incomplete,
    unsafe_local_resources,
    unsupported_local_profile,
    local_resource_io_error,
    stage_timed_out,
    stage_cancelled,
    backpressure,
    secondary_stream_pending,
    network_failure,
    protocol_failure,
};

struct PrecacheManifestTraceEvent {
    PrecacheManifestTraceClassification classification{
        PrecacheManifestTraceClassification::stage_started};
    PrecacheManifestStageState state{PrecacheManifestStageState::idle};
    network::NetworkAddress endpoint;
    std::size_t entry_count{0U};
    std::size_t ready_count{0U};
    std::size_t metadata_only_count{0U};
    std::size_t missing_count{0U};
    std::size_t unsafe_count{0U};
    std::size_t unsupported_count{0U};
    std::size_t ambiguous_count{0U};
    std::size_t io_error_count{0U};
    std::size_t transmitted_packet_count{0U};
};

using PrecacheManifestTraceCallback =
    std::function<void(const PrecacheManifestTraceEvent&)>;

// Continues the exact retained response stage but performs local metadata work
// only. Once the response boundary is reached this stage never drives or queues
// the retained NetchanDriver; it publishes a manifest and closes the lifetime.
class PrecacheManifestStage final {
public:
    PrecacheManifestStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        PrecacheManifestStageConfig config = {},
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
        PrecacheManifestTraceCallback trace_callback = {});
    ~PrecacheManifestStage();

    PrecacheManifestStage(const PrecacheManifestStage&) = delete;
    PrecacheManifestStage& operator=(const PrecacheManifestStage&) = delete;
    PrecacheManifestStage(PrecacheManifestStage&&) = delete;
    PrecacheManifestStage& operator=(PrecacheManifestStage&&) = delete;

    [[nodiscard]] bool start(
        PrecacheManifestStageTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime = {});
    void update(PrecacheManifestStageTimePoint now);
    void cancel(PrecacheManifestStageTimePoint now);

    [[nodiscard]] std::optional<PrecacheManifestStageEvent> poll_event();
    [[nodiscard]] PrecacheManifestStageState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] const std::optional<PrecacheManifestSignonState>& result()
        const noexcept;
    [[nodiscard]] const std::optional<PrecacheManifestStageError>& error()
        const noexcept;
    [[nodiscard]] const network::NetworkAddress& remote_endpoint()
        const noexcept;
    [[nodiscard]] const std::optional<network::NetworkAddress>& local_endpoint()
        const noexcept;
    [[nodiscard]] std::size_t pending_event_count() const noexcept;
    [[nodiscard]] std::size_t transmitted_packet_count() const noexcept;
    [[nodiscard]] std::size_t cleanup_count() const noexcept;
    [[nodiscard]] std::size_t initial_request_queue_count() const noexcept;
    [[nodiscard]] std::size_t transition_request_queue_count() const noexcept;
    [[nodiscard]] std::size_t response_queue_count() const noexcept;
    [[nodiscard]] std::size_t manifest_publication_count() const noexcept;

private:
    friend class GoldSrcHandshakeCoordinator;

    // A future post-manifest coordinator branch may own this stage and keep
    // the exact driver/auth lifetime alive. The public manifest API never
    // exposes the driver, and today's terminal stop explicitly finalizes it.
    struct RetainConnectionAtBoundary final {};

    PrecacheManifestStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
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
        RetainConnectionAtBoundary);
    [[nodiscard]] NetchanDriver* retained_driver() noexcept;
    void finalize_retained_boundary(
        PrecacheManifestStageTimePoint now) noexcept;

    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const PrecacheManifestStageErrorCode code) noexcept
{
    switch (code) {
    case PrecacheManifestStageErrorCode::invalid_configuration:
        return "invalid_configuration";
    case PrecacheManifestStageErrorCode::response_stage_start_failed:
        return "response_stage_start_failed";
    case PrecacheManifestStageErrorCode::response_stage_failed:
        return "response_stage_failed";
    case PrecacheManifestStageErrorCode::retained_driver_missing:
        return "retained_driver_missing";
    case PrecacheManifestStageErrorCode::inventory_build_failed:
        return "inventory_build_failed";
    case PrecacheManifestStageErrorCode::manifest_build_failed:
        return "manifest_build_failed";
    case PrecacheManifestStageErrorCode::event_backpressure:
        return "event_backpressure";
    case PrecacheManifestStageErrorCode::time_moved_backwards:
        return "time_moved_backwards";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
