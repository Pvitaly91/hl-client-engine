#pragma once

#include <hlclient/goldsrc/entity_snapshot.hpp>
#include <hlclient/goldsrc/post_resource_signon.hpp>
#include <hlclient/goldsrc/resource_client_response_stage.hpp>

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

using PostResourceEntitySnapshotStageClock = ResourceClientResponseStageClock;
using PostResourceEntitySnapshotStageTimePoint =
    ResourceClientResponseStageTimePoint;

inline constexpr std::size_t kDefaultMaximumPostResourceStageEvents = 128U;
inline constexpr std::size_t kMaximumPostResourceStageEvents = 1'024U;
inline constexpr std::size_t
    kDefaultMaximumPostResourceDriverEventsPerUpdate = 32U;
inline constexpr std::size_t kMaximumPostResourceDriverEventsPerUpdate = 256U;
inline constexpr auto kDefaultPostResourceSignonTimeout =
    std::chrono::seconds{5};
inline constexpr auto kMaximumPostResourceSignonTimeout =
    std::chrono::seconds{60};

enum class EntitySnapshotStageStopCondition {
    server_baselines,
    first_full_snapshot,
    first_applied_delta,
};

struct PostResourceEntitySnapshotStageConfig {
    ResourceClientResponseStageConfig resource_response;
    PostResourceSignonLimits post_resource;
    EntitySnapshotLimits entity_snapshots;
    PostResourceSignonCompatibilityProfile profile{
        PostResourceSignonCompatibilityProfile::
            stock_protocol_48_build_10210_evidence_pending};
    EntitySnapshotStageStopCondition stop_condition{
        EntitySnapshotStageStopCondition::first_applied_delta};
    std::chrono::milliseconds timeout{kDefaultPostResourceSignonTimeout};
    std::size_t maximum_stage_events{kDefaultMaximumPostResourceStageEvents};
    std::size_t maximum_driver_events_per_update{
        kDefaultMaximumPostResourceDriverEventsPerUpdate};
};

[[nodiscard]] bool valid_post_resource_entity_snapshot_stage_configuration(
    const PostResourceEntitySnapshotStageConfig& config) noexcept;

enum class PostResourceEntitySnapshotStageState {
    idle,
    waiting_for_resource_response,
    decoding_post_resource_messages,
    client_request_ready,
    waiting_for_client_request_transmit,
    waiting_for_client_request_ack,
    waiting_for_server_signon,
    decoding_baselines,
    baseline_registry_ready,
    waiting_for_full_snapshot,
    full_snapshot_ready,
    waiting_for_delta_snapshot,
    entity_snapshot_ready,
    unsupported_message,
    missing_delta_base,
    timed_out,
    cancelled,
    backpressure,
    secondary_stream_pending,
    network_error,
    protocol_error,
};

enum class PostResourceEntitySnapshotStageErrorCode {
    invalid_configuration,
    response_stage_start_failed,
    response_stage_failed,
    retained_driver_missing,
    retained_payload_missing,
    delta_registry_missing,
    stream_decode_failed,
    request_build_failed,
    request_queue_failed,
    request_transmit_mismatch,
    unexpected_acknowledgement,
    unsupported_message,
    entity_publication_failed,
    stage_timed_out,
    event_backpressure,
    driver_failed,
    time_moved_backwards,
};

struct PostResourceEntitySnapshotStageError {
    PostResourceEntitySnapshotStageErrorCode code{
        PostResourceEntitySnapshotStageErrorCode::invalid_configuration};
    std::optional<ResourceClientResponseStageErrorCode> response_code;
    std::optional<PostResourceSignonStreamErrorCode> stream_code;
    std::optional<PostResourceClientRequestErrorCode> request_code;
    std::optional<NetchanDriverErrorCode> driver_code;
    std::optional<DeltaValueErrorCode> delta_value_code;
    std::optional<EntityBaselineErrorCode> baseline_code;
    std::optional<EntitySnapshotErrorCode> snapshot_code;
    std::optional<EntitySnapshotHistoryErrorCode> history_code;
    std::string context;
};

// Immutable metadata publication for the exact boundary. It deliberately has
// no socket, raw payload, authentication material, filesystem or renderer.
class PostResourceSignonState final {
public:
    PostResourceSignonState(const PostResourceSignonState&) = default;
    PostResourceSignonState& operator=(const PostResourceSignonState&) = delete;
    PostResourceSignonState(PostResourceSignonState&&) noexcept = default;
    PostResourceSignonState& operator=(PostResourceSignonState&&) noexcept =
        delete;
    ~PostResourceSignonState() = default;

    [[nodiscard]] const ResourceClientResponseSignonState& resource_response()
        const noexcept;
    [[nodiscard]] const DeltaSchemaRegistryState& delta_registry()
        const noexcept;
    [[nodiscard]] const PostResourceSignonBoundaryState& boundary_state()
        const noexcept;
    [[nodiscard]] const std::optional<EntityBaselineRegistryState>&
    baseline_registry() const noexcept;
    [[nodiscard]] const std::optional<EntitySnapshotHistoryState>&
    snapshot_history() const noexcept;
    [[nodiscard]] const EntitySnapshotState* latest_snapshot() const noexcept;
    [[nodiscard]] PostResourceSignonCompatibilityProfile profile()
        const noexcept;

private:
    friend class PostResourceEntitySnapshotStage;

    PostResourceSignonState(
        ResourceClientResponseSignonState resource_response,
        DeltaSchemaRegistryState delta_registry,
        PostResourceSignonBoundaryState boundary_state,
        PostResourceSignonCompatibilityProfile profile,
        std::optional<EntityBaselineRegistryState> baseline_registry =
            std::nullopt,
        std::optional<EntitySnapshotHistoryState> snapshot_history =
            std::nullopt) noexcept;

    ResourceClientResponseSignonState resource_response_;
    DeltaSchemaRegistryState delta_registry_;
    PostResourceSignonBoundaryState boundary_state_;
    std::optional<EntityBaselineRegistryState> baseline_registry_;
    std::optional<EntitySnapshotHistoryState> snapshot_history_;
    PostResourceSignonCompatibilityProfile profile_{
        PostResourceSignonCompatibilityProfile::
            stock_protocol_48_build_10210_evidence_pending};
};

enum class PostResourceEntitySnapshotStageEventType {
    post_resource_message_received,
    client_signon_request_ready,
    client_signon_request_queued,
    client_signon_request_acknowledged,
    server_signon_progress,
    baseline_decoded,
    baseline_registry_ready,
    full_entity_snapshot_ready,
    delta_entity_snapshot_ready,
    entity_removed,
    snapshot_history_updated,
    unsupported_message,
    missing_delta_base,
    timeout,
    cancelled,
    backpressure,
    secondary_stream_pending,
    network_error,
    protocol_error,
};

struct PostResourceEntitySnapshotStageEvent {
    PostResourceEntitySnapshotStageEventType type{
        PostResourceEntitySnapshotStageEventType::protocol_error};
    std::optional<std::uint8_t> opcode;
    std::size_t byte_offset{0U};
    std::size_t bit_offset{0U};
    std::size_t semantic_byte_count{0U};
    std::size_t baseline_count{0U};
    std::size_t entity_count{0U};
    std::size_t changed_count{0U};
    std::size_t added_count{0U};
    std::size_t removed_count{0U};
    std::size_t history_count{0U};
    std::optional<std::uint32_t> snapshot_reference;
    PostResourceEntitySnapshotStageTimePoint occurred_at{};
};

struct PostResourceEntitySnapshotTraceEvent {
    PostResourceEntitySnapshotStageState state{
        PostResourceEntitySnapshotStageState::idle};
    network::NetworkAddress endpoint;
    PostResourceEntitySnapshotStageEvent metadata;
    std::size_t transmitted_packet_count{0U};
};

using PostResourceEntitySnapshotTraceCallback =
    std::function<void(const PostResourceEntitySnapshotTraceEvent&)>;

// This stage owns the complete existing response ladder and is the sole active
// consumer of its retained driver. The stock evidence-pending profile reaches
// a typed unsupported boundary without consuming its body or queueing bytes.
class PostResourceEntitySnapshotStage final {
public:
    PostResourceEntitySnapshotStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        PostResourceEntitySnapshotStageConfig config = {},
        resource_consistency::IResourceConsistencyProvider*
            consistency_provider = nullptr,
        PostResourceEntitySnapshotTraceCallback trace_callback = {});
    ~PostResourceEntitySnapshotStage();

    PostResourceEntitySnapshotStage(
        const PostResourceEntitySnapshotStage&) = delete;
    PostResourceEntitySnapshotStage& operator=(
        const PostResourceEntitySnapshotStage&) = delete;
    PostResourceEntitySnapshotStage(PostResourceEntitySnapshotStage&&) =
        delete;
    PostResourceEntitySnapshotStage& operator=(
        PostResourceEntitySnapshotStage&&) = delete;

    [[nodiscard]] bool start(
        PostResourceEntitySnapshotStageTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime = {});
    void update(PostResourceEntitySnapshotStageTimePoint now);
    void cancel(PostResourceEntitySnapshotStageTimePoint now);

    [[nodiscard]] std::optional<PostResourceEntitySnapshotStageEvent>
    poll_event();
    [[nodiscard]] PostResourceEntitySnapshotStageState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] const std::optional<PostResourceSignonState>& result()
        const noexcept;
    [[nodiscard]] const std::optional<
        PostResourceEntitySnapshotStageError>& error() const noexcept;
    [[nodiscard]] const network::NetworkAddress& remote_endpoint()
        const noexcept;
    [[nodiscard]] const std::optional<network::NetworkAddress>& local_endpoint()
        const noexcept;
    [[nodiscard]] std::size_t pending_event_count() const noexcept;
    [[nodiscard]] std::size_t transmitted_packet_count() const noexcept;
    [[nodiscard]] std::size_t cleanup_count() const noexcept;
    [[nodiscard]] std::size_t request_queue_count() const noexcept;
    [[nodiscard]] bool request_transmitted() const noexcept;
    [[nodiscard]] bool request_acknowledged() const noexcept;

private:
    [[nodiscard]] static std::optional<PostResourceSignonBoundaryState>
    aggregate_transcript(
        const PostResourceSignonBoundaryState& latest,
        std::span<const PostResourceMessageMetadata> server_messages,
        std::span<const PostResourceClientRequestMetadata> client_requests);
    [[nodiscard]] static std::optional<PostResourceSignonBoundaryState>
    complete_synthetic_sequence(
        const PostResourceSignonBoundaryState& latest);
    [[nodiscard]] static std::optional<PostResourceSignonBoundaryState>
    apply_synthetic_publication(
        const PostResourceSignonBoundaryState& latest,
        PostResourceSignonProgress published_progress);

    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

[[nodiscard]] constexpr std::string_view to_string(
    PostResourceEntitySnapshotStageErrorCode code) noexcept
{
    switch (code) {
    case PostResourceEntitySnapshotStageErrorCode::invalid_configuration:
        return "invalid_configuration";
    case PostResourceEntitySnapshotStageErrorCode::response_stage_start_failed:
        return "response_stage_start_failed";
    case PostResourceEntitySnapshotStageErrorCode::response_stage_failed:
        return "response_stage_failed";
    case PostResourceEntitySnapshotStageErrorCode::retained_driver_missing:
        return "retained_driver_missing";
    case PostResourceEntitySnapshotStageErrorCode::retained_payload_missing:
        return "retained_payload_missing";
    case PostResourceEntitySnapshotStageErrorCode::delta_registry_missing:
        return "delta_registry_missing";
    case PostResourceEntitySnapshotStageErrorCode::stream_decode_failed:
        return "stream_decode_failed";
    case PostResourceEntitySnapshotStageErrorCode::request_build_failed:
        return "request_build_failed";
    case PostResourceEntitySnapshotStageErrorCode::request_queue_failed:
        return "request_queue_failed";
    case PostResourceEntitySnapshotStageErrorCode::request_transmit_mismatch:
        return "request_transmit_mismatch";
    case PostResourceEntitySnapshotStageErrorCode::unexpected_acknowledgement:
        return "unexpected_acknowledgement";
    case PostResourceEntitySnapshotStageErrorCode::unsupported_message:
        return "unsupported_message";
    case PostResourceEntitySnapshotStageErrorCode::entity_publication_failed:
        return "entity_publication_failed";
    case PostResourceEntitySnapshotStageErrorCode::stage_timed_out:
        return "stage_timed_out";
    case PostResourceEntitySnapshotStageErrorCode::event_backpressure:
        return "event_backpressure";
    case PostResourceEntitySnapshotStageErrorCode::driver_failed:
        return "driver_failed";
    case PostResourceEntitySnapshotStageErrorCode::time_moved_backwards:
        return "time_moved_backwards";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
