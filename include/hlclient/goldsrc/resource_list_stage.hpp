#pragma once

#include <hlclient/goldsrc/resource_list.hpp>
#include <hlclient/goldsrc/resource_transition_stage.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

using ResourceListStageClock = ResourceTransitionStageClock;
using ResourceListStageTimePoint = ResourceTransitionStageTimePoint;

inline constexpr std::size_t kResourceListStageDiagnosticTextLimit = 256U;

struct ResourceListStageConfig {
    ResourceTransitionStageConfig transition;
    ResourceListLimits resource_list;
    std::size_t maximum_stage_events{kDefaultMaximumResourceListEvents};
};

[[nodiscard]] bool valid_resource_list_stage_configuration(
    const ResourceListStageConfig& config) noexcept;

enum class ResourceListStageState {
    idle,
    waiting_for_transition_state,
    decoding_resource_list,
    resource_list_ready,
    decoding_post_list_messages,
    post_list_boundary_reached,
    client_response_required,
    unsupported_resource_profile,
    timed_out,
    cancelled,
    backpressure,
    secondary_stream_pending,
    network_error,
    protocol_error,
};

enum class ResourceListStageErrorCode {
    invalid_configuration,
    transition_stage_start_failed,
    transition_stage_failed,
    retained_payload_missing,
    retained_driver_missing,
    transition_boundary_mismatch,
    resource_list_decode_failed,
    post_resource_stream_decode_failed,
    event_backpressure,
    time_moved_backwards,
};

struct ResourceListStageError {
    ResourceListStageErrorCode code{
        ResourceListStageErrorCode::invalid_configuration};
    std::optional<ResourceTransitionStageErrorCode> transition_code;
    std::optional<ResourceListErrorCode> resource_list_code;
    std::optional<PostResourceListStreamErrorCode> post_stream_code;
    std::optional<NetchanDriverErrorCode> driver_code;
    std::string context;
};

// Immutable owning publication of the transition prerequisite, ordered list,
// exact end-of-payload boundary, and metadata-only response requirement.
class ResourceListSignonState final {
public:
    ResourceListSignonState(const ResourceListSignonState&) = default;
    ResourceListSignonState& operator=(const ResourceListSignonState&) = delete;
    ResourceListSignonState(ResourceListSignonState&&) noexcept = default;
    ResourceListSignonState& operator=(ResourceListSignonState&&) noexcept = delete;
    ~ResourceListSignonState() = default;

    [[nodiscard]] const ResourceTransitionState& transition() const noexcept;
    [[nodiscard]] const ResourceListState& resource_list() const noexcept;
    [[nodiscard]] const PostResourceListStreamState& post_list() const noexcept;
    [[nodiscard]] const PostResourceListBoundary& boundary() const noexcept;
    [[nodiscard]] const ResourceClientResponseBoundary& client_response()
        const noexcept;

private:
    friend class ResourceListStage;

    ResourceListSignonState(
        ResourceTransitionState transition,
        ResourceListState resource_list,
        PostResourceListStreamState post_list) noexcept;

    ResourceTransitionState transition_;
    ResourceListState resource_list_;
    PostResourceListStreamState post_list_;
};

enum class ResourceListStageEventType {
    resource_list_ready,
    resource_entry_metadata,
    post_resource_control,
    post_resource_boundary,
    client_response_required,
    unsupported_resource_profile,
    timeout,
    cancelled,
    backpressure,
    secondary_stream_pending,
    network_error,
    protocol_error,
};

// Bounded metadata only. Resource names, payload bytes, and optional opaque
// entry bytes are deliberately absent.
struct ResourceListStageEvent {
    ResourceListStageEventType type{
        ResourceListStageEventType::resource_list_ready};
    std::size_t resource_count{0U};
    std::size_t entry_ordinal{0U};
    std::optional<ResourceType> resource_type;
    std::optional<std::uint16_t> resource_index;
    std::optional<std::uint32_t> resource_size_code;
    std::optional<std::uint8_t> resource_flags;
    std::size_t resource_name_byte_count{0U};
    std::size_t byte_offset{0U};
    std::size_t bit_offset{0U};
    std::size_t byte_count{0U};
    std::size_t bit_count{0U};
    std::optional<std::uint8_t> opcode;
    ResourceListStageTimePoint occurred_at{};
};

enum class ResourceListTraceClassification {
    stage_started,
    transition_boundary_reached,
    resource_list_decoded,
    resource_entry_metadata,
    post_resource_control,
    post_resource_boundary_reached,
    client_response_required,
    unsupported_resource_profile,
    stage_timed_out,
    stage_cancelled,
    backpressure,
    secondary_stream_pending,
    network_failure,
    protocol_failure,
};

struct ResourceListTraceEvent {
    ResourceListTraceClassification classification{
        ResourceListTraceClassification::stage_started};
    ResourceListStageState state{ResourceListStageState::idle};
    network::NetworkAddress endpoint;
    std::size_t resource_count{0U};
    std::size_t entry_ordinal{0U};
    std::optional<ResourceType> resource_type;
    std::optional<std::uint16_t> resource_index;
    std::optional<std::uint32_t> resource_size_code;
    std::optional<std::uint8_t> resource_flags;
    std::size_t resource_name_byte_count{0U};
    std::size_t byte_offset{0U};
    std::size_t bit_offset{0U};
    std::optional<std::uint8_t> opcode;
    std::size_t transmitted_packet_count{0U};
};

using ResourceListTraceCallback =
    std::function<void(const ResourceListTraceEvent&)>;

// Owns the complete existing sign-on chain through opcode 43. It parses the
// retained decompressed transfer exactly once and stops before any response,
// filesystem lookup, download, cache, precache, or renderer work.
class ResourceListStage final {
public:
    ResourceListStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        ResourceListStageConfig config = {},
        InitialSignonTraceCallback initial_trace_callback = {},
        PreResourceSignonTraceCallback pre_resource_trace_callback = {},
        DeltaDescriptionTraceCallback delta_trace_callback = {},
        MovementEnvironmentTraceCallback movement_trace_callback = {},
        UserInfoSignonTraceCallback user_info_trace_callback = {},
        ResourceTransitionTraceCallback transition_trace_callback = {},
        ResourceListTraceCallback trace_callback = {});
    ~ResourceListStage();

    ResourceListStage(const ResourceListStage&) = delete;
    ResourceListStage& operator=(const ResourceListStage&) = delete;
    ResourceListStage(ResourceListStage&&) = delete;
    ResourceListStage& operator=(ResourceListStage&&) = delete;

    [[nodiscard]] bool start(
        ResourceListStageTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime = {});
    void update(ResourceListStageTimePoint now);
    void cancel(ResourceListStageTimePoint now);

    [[nodiscard]] std::optional<ResourceListStageEvent> poll_event();
    [[nodiscard]] ResourceListStageState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] const std::optional<ResourceListSignonState>& result()
        const noexcept;
    [[nodiscard]] const std::optional<ResourceListStageError>& error()
        const noexcept;
    [[nodiscard]] const network::NetworkAddress& remote_endpoint() const noexcept;
    [[nodiscard]] const std::optional<network::NetworkAddress>& local_endpoint()
        const noexcept;
    [[nodiscard]] std::size_t pending_event_count() const noexcept;
    [[nodiscard]] std::size_t transmitted_packet_count() const noexcept;
    [[nodiscard]] std::size_t cleanup_count() const noexcept;
    [[nodiscard]] std::size_t initial_request_queue_count() const noexcept;
    [[nodiscard]] std::size_t transition_request_queue_count() const noexcept;
    [[nodiscard]] constexpr std::size_t response_queue_count() const noexcept
    {
        return 0U;
    }

private:
    [[nodiscard]] bool can_push_events(std::size_t count = 1U) const noexcept;
    void push_event(ResourceListStageEvent event) noexcept;
    void drain_transition_events() noexcept;
    void synchronize_from_transition(ResourceListStageTimePoint now);
    void decode_retained_resource_list(ResourceListStageTimePoint now);
    void fail_from_transition(ResourceListStageTimePoint now) noexcept;
    void fail_after_transition(
        ResourceListStageErrorCode code,
        ResourceListStageState state,
        std::string_view context,
        ResourceListStageTimePoint now,
        std::optional<ResourceListErrorCode> resource_list_code = std::nullopt,
        std::optional<PostResourceListStreamErrorCode> post_stream_code =
            std::nullopt,
        std::optional<NetchanDriverErrorCode> driver_code = std::nullopt) noexcept;
    void set_error(
        ResourceListStageErrorCode code,
        ResourceListStageState state,
        std::string_view context,
        std::optional<ResourceTransitionStageErrorCode> transition_code =
            std::nullopt,
        std::optional<ResourceListErrorCode> resource_list_code = std::nullopt,
        std::optional<PostResourceListStreamErrorCode> post_stream_code =
            std::nullopt,
        std::optional<NetchanDriverErrorCode> driver_code = std::nullopt) noexcept;
    void emit_trace(
        ResourceListTraceClassification classification,
        std::size_t resource_count = 0U,
        std::size_t entry_ordinal = 0U,
        std::optional<ResourceType> resource_type = std::nullopt,
        std::optional<std::uint16_t> resource_index = std::nullopt,
        std::optional<std::uint32_t> resource_size_code = std::nullopt,
        std::optional<std::uint8_t> resource_flags = std::nullopt,
        std::size_t resource_name_byte_count = 0U,
        std::size_t byte_offset = 0U,
        std::size_t bit_offset = 0U,
        std::optional<std::uint8_t> opcode = std::nullopt) noexcept;

    ResourceListStageConfig config_;
    ResourceListTraceCallback trace_callback_;
    bool trace_callback_active_{false};
    bool configuration_valid_{false};
    ResourceTransitionStage transition_stage_;
    std::vector<std::optional<ResourceListStageEvent>> event_slots_;
    std::size_t event_head_{0U};
    std::size_t event_size_{0U};
    ResourceListStageState state_{ResourceListStageState::idle};
    std::optional<ResourceListSignonState> result_;
    std::optional<ResourceListStageError> error_;
    std::optional<ResourceListStageTimePoint> last_update_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const ResourceListStageErrorCode code) noexcept
{
    switch (code) {
    case ResourceListStageErrorCode::invalid_configuration:
        return "invalid_configuration";
    case ResourceListStageErrorCode::transition_stage_start_failed:
        return "transition_stage_start_failed";
    case ResourceListStageErrorCode::transition_stage_failed:
        return "transition_stage_failed";
    case ResourceListStageErrorCode::retained_payload_missing:
        return "retained_payload_missing";
    case ResourceListStageErrorCode::retained_driver_missing:
        return "retained_driver_missing";
    case ResourceListStageErrorCode::transition_boundary_mismatch:
        return "transition_boundary_mismatch";
    case ResourceListStageErrorCode::resource_list_decode_failed:
        return "resource_list_decode_failed";
    case ResourceListStageErrorCode::post_resource_stream_decode_failed:
        return "post_resource_stream_decode_failed";
    case ResourceListStageErrorCode::event_backpressure:
        return "event_backpressure";
    case ResourceListStageErrorCode::time_moved_backwards:
        return "time_moved_backwards";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
