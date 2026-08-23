#pragma once

#include <hlclient/goldsrc/resource_client_response.hpp>
#include <hlclient/goldsrc/resource_list_stage.hpp>
#include <hlclient/goldsrc/service_payload_envelope.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

using ResourceClientResponseStageClock = ResourceListStageClock;
using ResourceClientResponseStageTimePoint = ResourceListStageTimePoint;

// Project safety limits, not stock engine maxima.
inline constexpr std::size_t
    kDefaultMaximumResourceResponseDriverEventsPerUpdate = 32U;
inline constexpr std::size_t
    kMaximumResourceResponseDriverEventsPerUpdate = 256U;
inline constexpr std::size_t kResourceClientResponseStageDiagnosticTextLimit =
    256U;
inline constexpr auto kDefaultResourceConsistencyProviderTimeout =
    std::chrono::seconds{5};
inline constexpr auto kMaximumResourceConsistencyProviderTimeout =
    std::chrono::seconds{60};
inline constexpr auto kDefaultResourceResponseAcknowledgementTimeout =
    std::chrono::seconds{5};
inline constexpr auto kMaximumResourceResponseAcknowledgementTimeout =
    std::chrono::seconds{60};
inline constexpr auto kDefaultPostResourceResponseBoundaryTimeout =
    std::chrono::seconds{5};
inline constexpr auto kMaximumPostResourceResponseBoundaryTimeout =
    std::chrono::seconds{60};

// The supported 41-byte response is intentionally admitted as one normal
// fragment (rather than an ordinary unfragmented reliable prefix), matching
// the evidence-gated carrier profile. Callers may further lower unrelated
// limits, but changing this cutoff is an unsupported response profile.
[[nodiscard]] ResourceListStageConfig
default_resource_client_response_resource_list_stage_config();

struct ResourceClientResponseStageConfig {
    ResourceListStageConfig resource_list{
        default_resource_client_response_resource_list_stage_config()};
    ResourceClientResponseLimits response;
    std::chrono::milliseconds consistency_provider_timeout{
        kDefaultResourceConsistencyProviderTimeout};
    std::chrono::milliseconds response_acknowledgement_timeout{
        kDefaultResourceResponseAcknowledgementTimeout};
    std::chrono::milliseconds post_ack_boundary_timeout{
        kDefaultPostResourceResponseBoundaryTimeout};
    std::size_t maximum_driver_events_per_update{
        kDefaultMaximumResourceResponseDriverEventsPerUpdate};
};

[[nodiscard]] bool valid_resource_client_response_stage_configuration(
    const ResourceClientResponseStageConfig& config) noexcept;

enum class ResourceClientResponseStageState {
    idle,
    waiting_for_resource_list,
    preparing_response,
    waiting_for_consistency_provider,
    response_ready,
    waiting_for_response_transmit,
    waiting_for_response_ack,
    waiting_for_server_continuation,
    decoding_server_continuation,
    next_server_boundary_reached,
    consistency_provider_required,
    unsupported_response_profile,
    timed_out,
    cancelled,
    backpressure,
    secondary_stream_pending,
    network_error,
    protocol_error,
};

enum class ResourceClientResponseStageErrorCode {
    invalid_configuration,
    resource_list_stage_start_failed,
    resource_list_stage_failed,
    retained_driver_missing,
    response_requirements_failed,
    provider_required,
    consistency_provider_begin_failed,
    consistency_provider_failed,
    consistency_provider_result_invalid,
    consistency_material_invalid,
    response_build_failed,
    response_queue_failed,
    response_transmit_mismatch,
    response_acknowledgement_invalid,
    response_acknowledgement_timed_out,
    server_payload_before_response_transmit,
    server_payload_acknowledgement_invalid,
    pre_ack_server_payload_overflow,
    post_response_payload_overflow,
    post_response_envelope_decode_failed,
    post_response_boundary_decode_failed,
    post_response_boundary_timed_out,
    event_backpressure,
    driver_failed,
    time_moved_backwards,
};

struct ResourceClientResponseStageError {
    ResourceClientResponseStageErrorCode code{
        ResourceClientResponseStageErrorCode::invalid_configuration};
    std::optional<ResourceListStageErrorCode> resource_list_code;
    std::optional<resource_consistency::ResourceConsistencyErrorCode>
        consistency_code;
    std::optional<Opcode5ResourceResponseErrorCode> response_code;
    std::optional<ServicePayloadEnvelopeErrorCode> envelope_code;
    std::optional<PostResourceResponseBoundaryErrorCode> boundary_code;
    std::optional<NetchanDriverErrorCode> driver_code;
    std::string context;
};

class ResourceResponseReliableLifecycle final {
public:
    [[nodiscard]] std::uint64_t reliable_generation() const noexcept;
    [[nodiscard]] bool fragmented() const noexcept;
    [[nodiscard]] std::uint16_t fragment_count() const noexcept;
    [[nodiscard]] bool reliable_toggle() const noexcept;
    [[nodiscard]] std::uint32_t first_transmit_sequence() const noexcept;
    [[nodiscard]] std::uint32_t most_recent_transmit_sequence() const noexcept;
    [[nodiscard]] std::uint64_t transmit_count() const noexcept;
    [[nodiscard]] const NetchanAcknowledgementObservation& acknowledgement()
        const noexcept;

private:
    friend class ResourceClientResponseStage;

    ResourceResponseReliableLifecycle(
        std::uint64_t reliable_generation,
        bool fragmented,
        std::uint16_t fragment_count,
        bool reliable_toggle,
        std::uint32_t first_transmit_sequence,
        std::uint32_t most_recent_transmit_sequence,
        std::uint64_t transmit_count,
        NetchanAcknowledgementObservation acknowledgement) noexcept;

    std::uint64_t reliable_generation_{0U};
    bool fragmented_{false};
    std::uint16_t fragment_count_{0U};
    bool reliable_toggle_{false};
    std::uint32_t first_transmit_sequence_{0U};
    std::uint32_t most_recent_transmit_sequence_{0U};
    std::uint64_t transmit_count_{0U};
    NetchanAcknowledgementObservation acknowledgement_;
};

// Immutable owning terminal publication. Carrier/tail evidence is optional
// because the production stage builds semantic bytes and delegates transport
// framing; it never fabricates or replays an observed stock carrier or tail.
class ResourceClientResponseSignonState final {
public:
    ResourceClientResponseSignonState(
        const ResourceClientResponseSignonState&) = default;
    ResourceClientResponseSignonState& operator=(
        const ResourceClientResponseSignonState&) = delete;
    ResourceClientResponseSignonState(
        ResourceClientResponseSignonState&&) noexcept = default;
    ResourceClientResponseSignonState& operator=(
        ResourceClientResponseSignonState&&) noexcept = delete;
    ~ResourceClientResponseSignonState() = default;

    [[nodiscard]] const ResourceListSignonState& resource_list() const noexcept;
    [[nodiscard]] const Opcode5ResourceResponse& response() const noexcept;
    [[nodiscard]] const std::optional<ResourceResponseCarrierGeometry>&
    source_carrier_geometry() const noexcept;
    [[nodiscard]] const std::optional<ResourceResponseConcurrentTail>&
    concurrent_tail() const noexcept;
    [[nodiscard]] const ResourceResponseReliableLifecycle& reliable_lifecycle()
        const noexcept;
    [[nodiscard]] const PostResourceResponseBoundary& boundary() const noexcept;
    [[nodiscard]] ResourceClientResponseCompatibilityProfile
    compatibility_profile() const noexcept;
    [[nodiscard]] ResourceClientResponseEvidenceProfile evidence_profile()
        const noexcept;

private:
    friend class ResourceClientResponseStage;

    ResourceClientResponseSignonState(
        ResourceListSignonState resource_list,
        Opcode5ResourceResponse response,
        std::optional<ResourceResponseCarrierGeometry> source_carrier_geometry,
        std::optional<ResourceResponseConcurrentTail> concurrent_tail,
        ResourceResponseReliableLifecycle reliable_lifecycle,
        PostResourceResponseBoundary boundary) noexcept;

    ResourceListSignonState resource_list_;
    Opcode5ResourceResponse response_;
    std::optional<ResourceResponseCarrierGeometry> source_carrier_geometry_;
    std::optional<ResourceResponseConcurrentTail> concurrent_tail_;
    ResourceResponseReliableLifecycle reliable_lifecycle_;
    PostResourceResponseBoundary boundary_;
};

enum class ResourceClientResponseStageEventType {
    resource_response_requirements_ready,
    consistency_provider_required,
    resource_response_ready,
    resource_response_queued,
    resource_response_transmitted,
    resource_response_acknowledged,
    concurrent_tail_observed,
    server_continuation_received,
    next_server_boundary_reached,
    unsupported_response_profile,
    timeout,
    cancelled,
    backpressure,
    secondary_stream_pending,
    network_error,
    protocol_error,
};

// Bounded metadata only. Response bytes, tail bytes, provider material,
// resource names, paths, authentication, and identity are deliberately absent.
struct ResourceClientResponseStageEvent {
    ResourceClientResponseStageEventType type{
        ResourceClientResponseStageEventType::protocol_error};
    std::size_t semantic_byte_count{0U};
    std::size_t payload_byte_count{0U};
    std::size_t remaining_byte_count{0U};
    std::optional<std::uint8_t> opcode;
    std::optional<std::uint64_t> reliable_generation;
    std::optional<std::uint32_t> transmit_sequence;
    std::optional<std::uint32_t> acknowledgement_sequence;
    std::uint64_t transmit_count{0U};
    bool reliable{false};
    bool fragmented{false};
    ResourceClientResponseStageTimePoint occurred_at{};
};

enum class ResourceClientResponseTraceClassification {
    stage_started,
    resource_list_ready,
    resource_response_requirements_ready,
    consistency_provider_required,
    resource_response_ready,
    resource_response_queued,
    resource_response_transmitted,
    resource_response_acknowledged,
    concurrent_tail_observed,
    server_continuation_received,
    next_server_boundary_reached,
    unsupported_response_profile,
    stage_timed_out,
    stage_cancelled,
    backpressure,
    secondary_stream_pending,
    network_failure,
    protocol_failure,
};

struct ResourceClientResponseTraceEvent {
    ResourceClientResponseTraceClassification classification{
        ResourceClientResponseTraceClassification::stage_started};
    ResourceClientResponseStageState state{
        ResourceClientResponseStageState::idle};
    network::NetworkAddress endpoint;
    std::size_t semantic_byte_count{0U};
    std::size_t payload_byte_count{0U};
    std::size_t remaining_byte_count{0U};
    std::optional<std::uint8_t> opcode;
    std::optional<std::uint64_t> reliable_generation;
    std::optional<std::uint32_t> transmit_sequence;
    std::optional<std::uint32_t> acknowledgement_sequence;
    std::uint64_t transmit_count{0U};
    bool reliable{false};
    bool fragmented{false};
    std::size_t transmitted_packet_count{0U};
};

using ResourceClientResponseTraceCallback =
    std::function<void(const ResourceClientResponseTraceEvent&)>;

class ResourceClientResponseStage final {
public:
    // consistency_provider is non-owning and must outlive this stage. Its
    // begin/update/cancel calls are made on the stage update path and must
    // satisfy IResourceConsistencyProvider's nonblocking contract.
    ResourceClientResponseStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        ResourceClientResponseStageConfig config = {},
        resource_consistency::IResourceConsistencyProvider*
            consistency_provider = nullptr,
        InitialSignonTraceCallback initial_trace_callback = {},
        PreResourceSignonTraceCallback pre_resource_trace_callback = {},
        DeltaDescriptionTraceCallback delta_trace_callback = {},
        MovementEnvironmentTraceCallback movement_trace_callback = {},
        UserInfoSignonTraceCallback user_info_trace_callback = {},
        ResourceTransitionTraceCallback transition_trace_callback = {},
        ResourceListTraceCallback resource_list_trace_callback = {},
        ResourceClientResponseTraceCallback trace_callback = {});
    ~ResourceClientResponseStage();

    ResourceClientResponseStage(const ResourceClientResponseStage&) = delete;
    ResourceClientResponseStage& operator=(
        const ResourceClientResponseStage&) = delete;
    ResourceClientResponseStage(ResourceClientResponseStage&&) = delete;
    ResourceClientResponseStage& operator=(
        ResourceClientResponseStage&&) = delete;

    [[nodiscard]] bool start(
        ResourceClientResponseStageTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime = {});
    void update(ResourceClientResponseStageTimePoint now);
    void cancel(ResourceClientResponseStageTimePoint now);

    [[nodiscard]] std::optional<ResourceClientResponseStageEvent> poll_event();
    [[nodiscard]] ResourceClientResponseStageState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] const std::optional<ResourceClientResponseSignonState>&
    result() const noexcept;
    [[nodiscard]] const std::optional<ResourceClientResponseStageError>&
    error() const noexcept;
    [[nodiscard]] const network::NetworkAddress& remote_endpoint() const noexcept;
    [[nodiscard]] const std::optional<network::NetworkAddress>& local_endpoint()
        const noexcept;
    [[nodiscard]] std::size_t pending_event_count() const noexcept;
    [[nodiscard]] std::size_t transmitted_packet_count() const noexcept;
    [[nodiscard]] std::size_t cleanup_count() const noexcept;
    [[nodiscard]] std::size_t initial_request_queue_count() const noexcept;
    [[nodiscard]] std::size_t transition_request_queue_count() const noexcept;
    [[nodiscard]] std::size_t response_queue_count() const noexcept;
    [[nodiscard]] std::size_t response_build_count() const noexcept;
    [[nodiscard]] std::size_t requirements_derivation_count() const noexcept;
    [[nodiscard]] std::size_t provider_begin_count() const noexcept;
    [[nodiscard]] bool response_transmitted() const noexcept;
    [[nodiscard]] bool response_acknowledged() const noexcept;

private:
    [[nodiscard]] bool can_push_events(std::size_t count = 1U) const noexcept;
    void push_event(ResourceClientResponseStageEvent event) noexcept;
    void drain_resource_list_events() noexcept;
    void synchronize_from_resource_list(
        ResourceClientResponseStageTimePoint now);
    void determine_requirements(ResourceClientResponseStageTimePoint now);
    void poll_consistency_provider(ResourceClientResponseStageTimePoint now);
    void build_and_queue_response(
        resource_consistency::ResourceConsistencyMaterial material,
        ResourceClientResponseStageTimePoint now);
    void drive_transport(ResourceClientResponseStageTimePoint now);
    void observe_response_transmit(ResourceClientResponseStageTimePoint now);
    void drain_driver_events(
        ResourceClientResponseStageTimePoint now,
        std::size_t& processed_events);
    void handle_driver_event(
        NetchanDriverEvent event,
        ResourceClientResponseStageTimePoint now);
    void handle_response_acknowledgement(
        const NetchanDriverEvent& event,
        ResourceClientResponseStageTimePoint now);
    void handle_server_payload(
        OwnedNetchanPayload payload,
        ResourceClientResponseStageTimePoint now);
    void decode_pending_server_payload(
        ResourceClientResponseStageTimePoint now);
    void fail_from_resource_list(ResourceClientResponseStageTimePoint now)
        noexcept;
    void fail_from_driver(ResourceClientResponseStageTimePoint now) noexcept;
    void fail(
        ResourceClientResponseStageErrorCode code,
        ResourceClientResponseStageState state,
        std::string_view context,
        ResourceClientResponseStageTimePoint now,
        std::optional<ResourceListStageErrorCode> resource_list_code =
            std::nullopt,
        std::optional<
            resource_consistency::ResourceConsistencyErrorCode>
            consistency_code = std::nullopt,
        std::optional<Opcode5ResourceResponseErrorCode> response_code =
            std::nullopt,
        std::optional<ServicePayloadEnvelopeErrorCode> envelope_code =
            std::nullopt,
        std::optional<PostResourceResponseBoundaryErrorCode> boundary_code =
            std::nullopt,
        std::optional<NetchanDriverErrorCode> driver_code = std::nullopt)
        noexcept;
    void cleanup(ResourceClientResponseStageTimePoint now) noexcept;
    void emit_trace(
        ResourceClientResponseTraceClassification classification,
        std::size_t payload_byte_count = 0U,
        std::size_t remaining_byte_count = 0U,
        std::optional<std::uint8_t> opcode = std::nullopt,
        std::optional<std::uint32_t> transmit_sequence = std::nullopt,
        std::optional<std::uint32_t> acknowledgement_sequence = std::nullopt)
        noexcept;

    ResourceClientResponseStageConfig config_;
    resource_consistency::IResourceConsistencyProvider*
        consistency_provider_{nullptr};
    ResourceClientResponseTraceCallback trace_callback_;
    bool trace_callback_active_{false};
    bool configuration_valid_{false};
    ResourceListStage resource_list_stage_;
    std::vector<std::optional<ResourceClientResponseStageEvent>> event_slots_;
    std::size_t event_head_{0U};
    std::size_t event_size_{0U};
    ResourceClientResponseStageState state_{
        ResourceClientResponseStageState::idle};
    std::optional<ResourceClientResponseSignonState> result_;
    std::optional<ResourceClientResponseStageError> error_;
    std::optional<resource_consistency::ResourceConsistencyRequirements>
        requirements_;
    std::unique_ptr<resource_consistency::ResourceConsistencyOperation>
        consistency_operation_;
    std::optional<resource_consistency::ResourceConsistencySession>
        consistency_session_;
    std::optional<EncodedOpcode5ResourceResponse> response_encoding_;
    std::optional<OwnedNetchanPayload> pre_ack_payload_;
    std::optional<OwnedNetchanPayload> pending_decode_payload_;
    std::optional<ResourceClientResponseStageTimePoint> last_update_;
    std::optional<ResourceClientResponseStageTimePoint>
        consistency_provider_started_at_;
    std::optional<ResourceClientResponseStageTimePoint>
        response_acknowledgement_started_at_;
    std::optional<ResourceClientResponseStageTimePoint>
        post_ack_boundary_started_at_;
    std::optional<NetchanSequence> first_transmit_sequence_;
    std::optional<NetchanSequence> most_recent_transmit_sequence_;
    std::optional<NetchanAcknowledgementObservation> acknowledgement_;
    std::uint64_t reliable_generation_{0U};
    std::uint64_t response_transmit_count_{0U};
    std::uint16_t response_fragment_count_{0U};
    bool response_reliable_toggle_{false};
    std::size_t response_queue_count_{0U};
    std::size_t response_build_count_{0U};
    std::size_t requirements_derivation_count_{0U};
    std::size_t provider_begin_count_{0U};
    bool response_transmitted_{false};
    bool response_acknowledged_{false};
    bool server_payloads_admissible_{false};
    bool cleanup_done_{false};
};

[[nodiscard]] constexpr std::string_view to_string(
    const ResourceClientResponseStageErrorCode code) noexcept
{
    switch (code) {
    case ResourceClientResponseStageErrorCode::invalid_configuration:
        return "invalid_configuration";
    case ResourceClientResponseStageErrorCode::resource_list_stage_start_failed:
        return "resource_list_stage_start_failed";
    case ResourceClientResponseStageErrorCode::resource_list_stage_failed:
        return "resource_list_stage_failed";
    case ResourceClientResponseStageErrorCode::retained_driver_missing:
        return "retained_driver_missing";
    case ResourceClientResponseStageErrorCode::response_requirements_failed:
        return "response_requirements_failed";
    case ResourceClientResponseStageErrorCode::provider_required:
        return "provider_required";
    case ResourceClientResponseStageErrorCode::consistency_provider_begin_failed:
        return "consistency_provider_begin_failed";
    case ResourceClientResponseStageErrorCode::consistency_provider_failed:
        return "consistency_provider_failed";
    case ResourceClientResponseStageErrorCode::consistency_provider_result_invalid:
        return "consistency_provider_result_invalid";
    case ResourceClientResponseStageErrorCode::consistency_material_invalid:
        return "consistency_material_invalid";
    case ResourceClientResponseStageErrorCode::response_build_failed:
        return "response_build_failed";
    case ResourceClientResponseStageErrorCode::response_queue_failed:
        return "response_queue_failed";
    case ResourceClientResponseStageErrorCode::response_transmit_mismatch:
        return "response_transmit_mismatch";
    case ResourceClientResponseStageErrorCode::response_acknowledgement_invalid:
        return "response_acknowledgement_invalid";
    case ResourceClientResponseStageErrorCode::response_acknowledgement_timed_out:
        return "response_acknowledgement_timed_out";
    case ResourceClientResponseStageErrorCode::server_payload_before_response_transmit:
        return "server_payload_before_response_transmit";
    case ResourceClientResponseStageErrorCode::server_payload_acknowledgement_invalid:
        return "server_payload_acknowledgement_invalid";
    case ResourceClientResponseStageErrorCode::pre_ack_server_payload_overflow:
        return "pre_ack_server_payload_overflow";
    case ResourceClientResponseStageErrorCode::post_response_payload_overflow:
        return "post_response_payload_overflow";
    case ResourceClientResponseStageErrorCode::post_response_envelope_decode_failed:
        return "post_response_envelope_decode_failed";
    case ResourceClientResponseStageErrorCode::post_response_boundary_decode_failed:
        return "post_response_boundary_decode_failed";
    case ResourceClientResponseStageErrorCode::post_response_boundary_timed_out:
        return "post_response_boundary_timed_out";
    case ResourceClientResponseStageErrorCode::event_backpressure:
        return "event_backpressure";
    case ResourceClientResponseStageErrorCode::driver_failed:
        return "driver_failed";
    case ResourceClientResponseStageErrorCode::time_moved_backwards:
        return "time_moved_backwards";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
