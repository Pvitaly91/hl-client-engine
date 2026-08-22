#pragma once

#include <hlclient/goldsrc/resource_transition_control.hpp>
#include <hlclient/goldsrc/resource_transition_request.hpp>
#include <hlclient/goldsrc/service_payload_envelope.hpp>
#include <hlclient/goldsrc/user_info_signon_stage.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

class ResourceListStage;

using ResourceTransitionStageClock = UserInfoSignonStageClock;
using ResourceTransitionStageTimePoint = UserInfoSignonStageTimePoint;

// Project safety limits, not stock engine maxima.
inline constexpr std::size_t kDefaultMaximumSecondServicePayloadSize = 65'536U;
inline constexpr std::size_t kMaximumSecondServicePayloadSize = 1'048'576U;
inline constexpr std::size_t kDefaultMaximumResourceTransitionStageEvents = 64U;
inline constexpr std::size_t kMaximumResourceTransitionStageEvents = 256U;
inline constexpr std::size_t kDefaultMaximumResourceTransitionDriverEventsPerUpdate = 32U;
inline constexpr std::size_t kMaximumResourceTransitionDriverEventsPerUpdate = 256U;
inline constexpr std::size_t kResourceTransitionStageDiagnosticTextLimit = 256U;

struct ResourceTransitionStageConfig {
    UserInfoSignonStageConfig user_info;
    ResourceTransitionRequestLimits request;
    ResourceTransitionControlLimits control;
    std::size_t maximum_second_service_payload_size{
        kDefaultMaximumSecondServicePayloadSize};
    std::size_t maximum_stage_events{
        kDefaultMaximumResourceTransitionStageEvents};
    std::size_t maximum_driver_events_per_update{
        kDefaultMaximumResourceTransitionDriverEventsPerUpdate};
};

[[nodiscard]] bool valid_resource_transition_stage_configuration(
    const ResourceTransitionStageConfig& config) noexcept;

enum class ResourceTransitionStageState {
    idle,
    waiting_for_user_info_state,
    request_ready,
    waiting_for_request_transmit,
    waiting_for_request_ack,
    waiting_for_server_transfer,
    decoding_transition_control,
    neutral_opcode43_boundary_reached,
    unsupported_message,
    timed_out,
    cancelled,
    backpressure,
    secondary_stream_pending,
    network_error,
    protocol_error,
};

enum class ResourceTransitionStageErrorCode {
    invalid_configuration,
    user_info_stage_start_failed,
    user_info_stage_failed,
    retained_driver_missing,
    request_build_failed,
    request_queue_failed,
    time_moved_backwards,
    service_payload_before_ack_overflow,
    second_payload_envelope_decode_failed,
    transition_control_decode_failed,
    event_backpressure,
    driver_failed,
};

struct ResourceTransitionStageError {
    ResourceTransitionStageErrorCode code{
        ResourceTransitionStageErrorCode::invalid_configuration};
    std::optional<UserInfoSignonStageErrorCode> user_info_code;
    std::optional<ResourceTransitionRequestErrorCode> request_code;
    std::optional<ServicePayloadEnvelopeErrorCode> envelope_code;
    std::optional<ResourceTransitionControlErrorCode> control_code;
    std::optional<NetchanDriverErrorCode> driver_code;
    std::string context;
};

class ResourceTransitionSourcePayloadMetadata final {
public:
    [[nodiscard]] std::size_t compressed_byte_count() const noexcept;
    [[nodiscard]] std::size_t decompressed_byte_count() const noexcept;
    [[nodiscard]] std::uint32_t source_sequence() const noexcept;
    [[nodiscard]] std::uint32_t source_acknowledgement() const noexcept;
    [[nodiscard]] bool source_reliable() const noexcept;
    [[nodiscard]] bool reassembled() const noexcept;
    [[nodiscard]] bool decompressed() const noexcept;
    [[nodiscard]] bool acknowledgement_reliable() const noexcept;
    [[nodiscard]] NetchanDirection direction() const noexcept;
    [[nodiscard]] NetchanDriverTimePoint received_at() const noexcept;

private:
    friend class ResourceTransitionStage;

    ResourceTransitionSourcePayloadMetadata(
        std::size_t compressed_byte_count,
        std::size_t decompressed_byte_count,
        std::uint32_t source_sequence,
        std::uint32_t source_acknowledgement,
        bool source_reliable,
        bool reassembled,
        bool decompressed,
        bool acknowledgement_reliable,
        NetchanDirection direction,
        NetchanDriverTimePoint received_at) noexcept;

    std::size_t compressed_byte_count_{0U};
    std::size_t decompressed_byte_count_{0U};
    std::uint32_t source_sequence_{0U};
    std::uint32_t source_acknowledgement_{0U};
    bool source_reliable_{false};
    bool reassembled_{false};
    bool decompressed_{false};
    bool acknowledgement_reliable_{false};
    NetchanDirection direction_{NetchanDirection::server_to_client};
    NetchanDriverTimePoint received_at_{};
};

enum class ResourceTransitionEvidenceProfile {
    bounded_stock_transition_with_neutral_opcode43_boundary,
};

// Immutable owning result. It intentionally contains no resource count,
// resource entry, path, file, download, cache, renderer, or raw payload.
class ResourceTransitionState final {
public:
    ResourceTransitionState(const ResourceTransitionState&) = default;
    ResourceTransitionState& operator=(const ResourceTransitionState&) = delete;
    ResourceTransitionState(ResourceTransitionState&&) noexcept = default;
    ResourceTransitionState& operator=(ResourceTransitionState&&) noexcept = delete;
    ~ResourceTransitionState() = default;

    [[nodiscard]] const UserInfoSignonState& user_info() const noexcept;
    [[nodiscard]] const ResourceTransitionRequest& request() const noexcept;
    [[nodiscard]] const ResourceTransitionControlState& control() const noexcept;
    [[nodiscard]] const Opcode43Boundary& boundary() const noexcept;
    [[nodiscard]] const ResourceTransitionSourcePayloadMetadata&
    source_payload() const noexcept;
    [[nodiscard]] ResourceTransitionEvidenceProfile evidence_profile() const noexcept;

private:
    friend class ResourceTransitionStage;

    ResourceTransitionState(
        UserInfoSignonState user_info,
        ResourceTransitionRequest request,
        ResourceTransitionControlState control,
        Opcode43Boundary boundary,
        ResourceTransitionSourcePayloadMetadata source_payload) noexcept;

    UserInfoSignonState user_info_;
    ResourceTransitionRequest request_;
    ResourceTransitionControlState control_;
    Opcode43Boundary boundary_;
    ResourceTransitionSourcePayloadMetadata source_payload_;
};

enum class ResourceTransitionStageEventType {
    transition_request_queued,
    transition_request_transmitted,
    transition_request_acknowledged,
    second_service_transfer_received,
    transition_control_decoded,
    neutral_opcode43_boundary,
};

struct ResourceTransitionStageEvent {
    ResourceTransitionStageEventType type{
        ResourceTransitionStageEventType::transition_request_queued};
    std::size_t byte_offset{0U};
    std::size_t byte_count{0U};
    std::optional<std::uint8_t> opcode;
    ResourceTransitionStageTimePoint occurred_at{};
};

enum class ResourceTransitionTraceClassification {
    stage_started,
    user_info_ready,
    transition_request_queued,
    transition_request_transmitted,
    transition_request_acknowledged,
    second_service_transfer_received,
    transition_control_decoded,
    neutral_opcode43_boundary_reached,
    stage_cancelled,
    stage_timed_out,
    unsupported_message,
    backpressure,
    secondary_stream_pending,
    network_failure,
    protocol_failure,
};

// Metadata only. No request command string, user-info value, opaque control
// field, raw payload, authentication bytes, or resource bytes are exposed.
struct ResourceTransitionTraceEvent {
    ResourceTransitionTraceClassification classification{
        ResourceTransitionTraceClassification::stage_started};
    ResourceTransitionStageState state{ResourceTransitionStageState::idle};
    network::NetworkAddress endpoint;
    std::size_t request_size{0U};
    std::size_t byte_offset{0U};
    std::size_t byte_count{0U};
    std::optional<std::uint8_t> opcode;
    std::size_t transmitted_packet_count{0U};
};

using ResourceTransitionTraceCallback =
    std::function<void(const ResourceTransitionTraceEvent&)>;

class ResourceTransitionStage final {
public:
    ResourceTransitionStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        ResourceTransitionStageConfig config = {},
        InitialSignonTraceCallback initial_trace_callback = {},
        PreResourceSignonTraceCallback pre_resource_trace_callback = {},
        DeltaDescriptionTraceCallback delta_trace_callback = {},
        MovementEnvironmentTraceCallback movement_trace_callback = {},
        UserInfoSignonTraceCallback user_info_trace_callback = {},
        ResourceTransitionTraceCallback trace_callback = {});
    ~ResourceTransitionStage();

    ResourceTransitionStage(const ResourceTransitionStage&) = delete;
    ResourceTransitionStage& operator=(const ResourceTransitionStage&) = delete;
    ResourceTransitionStage(ResourceTransitionStage&&) = delete;
    ResourceTransitionStage& operator=(ResourceTransitionStage&&) = delete;

    [[nodiscard]] bool start(
        ResourceTransitionStageTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime = {});
    void update(ResourceTransitionStageTimePoint now);
    void cancel(ResourceTransitionStageTimePoint now);

    [[nodiscard]] std::optional<ResourceTransitionStageEvent> poll_event();
    [[nodiscard]] ResourceTransitionStageState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] const std::optional<ResourceTransitionState>& result() const noexcept;
    [[nodiscard]] const std::optional<ResourceTransitionStageError>& error() const noexcept;
    [[nodiscard]] const network::NetworkAddress& remote_endpoint() const noexcept;
    [[nodiscard]] const std::optional<network::NetworkAddress>& local_endpoint() const noexcept;
    [[nodiscard]] std::size_t pending_event_count() const noexcept;
    [[nodiscard]] std::size_t transmitted_packet_count() const noexcept;
    [[nodiscard]] std::size_t cleanup_count() const noexcept;
    [[nodiscard]] std::size_t initial_request_queue_count() const noexcept;
    [[nodiscard]] std::size_t transition_request_queue_count() const noexcept;
    [[nodiscard]] bool transition_request_transmitted() const noexcept;
    [[nodiscard]] bool transition_request_acknowledged() const noexcept;

private:
    friend class ResourceListStage;

    struct RetainConnectionAtBoundary final {};

    ResourceTransitionStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        ResourceTransitionStageConfig config,
        InitialSignonTraceCallback initial_trace_callback,
        PreResourceSignonTraceCallback pre_resource_trace_callback,
        DeltaDescriptionTraceCallback delta_trace_callback,
        MovementEnvironmentTraceCallback movement_trace_callback,
        UserInfoSignonTraceCallback user_info_trace_callback,
        ResourceTransitionTraceCallback trace_callback,
        RetainConnectionAtBoundary);
    ResourceTransitionStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        ResourceTransitionStageConfig config,
        InitialSignonTraceCallback initial_trace_callback,
        PreResourceSignonTraceCallback pre_resource_trace_callback,
        DeltaDescriptionTraceCallback delta_trace_callback,
        MovementEnvironmentTraceCallback movement_trace_callback,
        UserInfoSignonTraceCallback user_info_trace_callback,
        ResourceTransitionTraceCallback trace_callback,
        bool retain_connection_at_boundary);

    [[nodiscard]] const OwnedServicePayload* retained_source_payload() const noexcept;
    [[nodiscard]] NetchanDriver* retained_driver() noexcept;
    void finalize_retained_boundary(ResourceTransitionStageTimePoint now) noexcept;
    [[nodiscard]] bool can_push_events(std::size_t count = 1U) const noexcept;
    void push_event(ResourceTransitionStageEvent event) noexcept;
    void drain_user_info_events() noexcept;
    void synchronize_from_user_info(ResourceTransitionStageTimePoint now);
    void queue_transition_request(ResourceTransitionStageTimePoint now);
    void observe_request_transmit(ResourceTransitionStageTimePoint now);
    void drain_driver_events(
        ResourceTransitionStageTimePoint now,
        std::size_t& processed_events);
    void handle_driver_event(
        NetchanDriverEvent event,
        ResourceTransitionStageTimePoint now);
    void handle_request_acknowledgement(ResourceTransitionStageTimePoint now);
    void handle_payload(
        OwnedNetchanPayload payload,
        ResourceTransitionStageTimePoint now);
    void decode_pending_payload(ResourceTransitionStageTimePoint now);
    void fail_from_user_info() noexcept;
    void fail_from_driver(ResourceTransitionStageTimePoint now) noexcept;
    void fail(
        ResourceTransitionStageErrorCode code,
        ResourceTransitionStageState state,
        std::string_view context,
        ResourceTransitionStageTimePoint now,
        std::optional<UserInfoSignonStageErrorCode> user_info_code = std::nullopt,
        std::optional<ResourceTransitionRequestErrorCode> request_code = std::nullopt,
        std::optional<ServicePayloadEnvelopeErrorCode> envelope_code = std::nullopt,
        std::optional<ResourceTransitionControlErrorCode> control_code = std::nullopt,
        std::optional<NetchanDriverErrorCode> driver_code = std::nullopt) noexcept;
    void cleanup(ResourceTransitionStageTimePoint now) noexcept;
    void emit_trace(
        ResourceTransitionTraceClassification classification,
        std::size_t byte_offset = 0U,
        std::size_t byte_count = 0U,
        std::optional<std::uint8_t> opcode = std::nullopt) noexcept;

    ResourceTransitionStageConfig config_;
    ResourceTransitionTraceCallback trace_callback_;
    bool trace_callback_active_{false};
    bool configuration_valid_{false};
    bool retain_connection_at_boundary_{false};
    UserInfoSignonStage user_info_stage_;
    std::vector<std::optional<ResourceTransitionStageEvent>> event_slots_;
    std::size_t event_head_{0U};
    std::size_t event_size_{0U};
    ResourceTransitionStageState state_{ResourceTransitionStageState::idle};
    std::optional<ResourceTransitionState> result_;
    std::optional<ResourceTransitionRequest> request_;
    std::optional<ResourceTransitionStageError> error_;
    std::optional<OwnedNetchanPayload> pre_ack_payload_;
    std::optional<OwnedNetchanPayload> pending_decode_payload_;
    std::optional<OwnedServicePayload> retained_source_payload_;
    std::optional<ResourceTransitionStageTimePoint> last_update_;
    std::size_t transition_request_queue_count_{0U};
    bool request_transmitted_{false};
    bool request_acknowledged_{false};
    bool cleanup_done_{false};
};

[[nodiscard]] constexpr std::string_view to_string(
    const ResourceTransitionStageErrorCode code) noexcept
{
    switch (code) {
    case ResourceTransitionStageErrorCode::invalid_configuration:
        return "invalid_configuration";
    case ResourceTransitionStageErrorCode::user_info_stage_start_failed:
        return "user_info_stage_start_failed";
    case ResourceTransitionStageErrorCode::user_info_stage_failed:
        return "user_info_stage_failed";
    case ResourceTransitionStageErrorCode::retained_driver_missing:
        return "retained_driver_missing";
    case ResourceTransitionStageErrorCode::request_build_failed:
        return "request_build_failed";
    case ResourceTransitionStageErrorCode::request_queue_failed:
        return "request_queue_failed";
    case ResourceTransitionStageErrorCode::time_moved_backwards:
        return "time_moved_backwards";
    case ResourceTransitionStageErrorCode::service_payload_before_ack_overflow:
        return "service_payload_before_ack_overflow";
    case ResourceTransitionStageErrorCode::second_payload_envelope_decode_failed:
        return "second_payload_envelope_decode_failed";
    case ResourceTransitionStageErrorCode::transition_control_decode_failed:
        return "transition_control_decode_failed";
    case ResourceTransitionStageErrorCode::event_backpressure:
        return "event_backpressure";
    case ResourceTransitionStageErrorCode::driver_failed:
        return "driver_failed";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
