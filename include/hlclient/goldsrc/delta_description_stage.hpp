#pragma once

#include <hlclient/goldsrc/delta_description.hpp>
#include <hlclient/goldsrc/pre_resource_signon_stage.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

class MovementEnvironmentStage;

using DeltaDescriptionStageClock = PreResourceSignonClock;
using DeltaDescriptionStageTimePoint = PreResourceSignonTimePoint;

inline constexpr std::size_t kDefaultMaximumDeltaDescriptionStageEvents = 32U;
inline constexpr std::size_t kMaximumDeltaDescriptionStageEvents = 512U;
inline constexpr std::size_t kDeltaDescriptionStageDiagnosticTextLimit = 256U;

struct DeltaDescriptionStageConfig {
    PreResourceSignonConfig pre_resource;
    DeltaDescriptionLimits delta;
    std::size_t maximum_events{kDefaultMaximumDeltaDescriptionStageEvents};
};

[[nodiscard]] bool valid_delta_description_stage_configuration(
    const DeltaDescriptionStageConfig& config) noexcept;

enum class DeltaDescriptionStageState {
    idle,
    waiting_for_pre_resource_state,
    decoding_delta_stream,
    delta_registry_ready,
    post_delta_boundary_reached,
    unsupported_message,
    timed_out,
    cancelled,
    backpressure,
    secondary_stream_pending_m3,
    network_error,
    protocol_error,
};

enum class DeltaDescriptionStageErrorCode {
    invalid_configuration,
    pre_resource_start_failed,
    pre_resource_failed,
    retained_payload_missing,
    delta_stream_decode_failed,
    event_backpressure,
};

struct DeltaDescriptionStageError {
    DeltaDescriptionStageErrorCode code{
        DeltaDescriptionStageErrorCode::invalid_configuration};
    std::optional<PreResourceSignonErrorCode> pre_resource_code;
    std::optional<DeltaDescriptionStreamErrorCode> stream_code;
    std::optional<DeltaDescriptionErrorCode> parser_code;
    std::optional<DeltaRegistryErrorCode> registry_code;
    std::optional<NetchanDriverErrorCode> driver_code;
    std::string context;
};

class DeltaDescriptionSignonState final {
public:
    DeltaDescriptionSignonState(const DeltaDescriptionSignonState&) = default;
    DeltaDescriptionSignonState& operator=(const DeltaDescriptionSignonState&) = delete;
    DeltaDescriptionSignonState(DeltaDescriptionSignonState&&) noexcept = default;
    DeltaDescriptionSignonState& operator=(DeltaDescriptionSignonState&&) noexcept = delete;
    ~DeltaDescriptionSignonState() = default;

    [[nodiscard]] const PreResourceSignonState& pre_resource() const noexcept;
    [[nodiscard]] const DeltaSchemaRegistryState& registry() const noexcept;
    [[nodiscard]] const PostDeltaBoundary& boundary() const noexcept;
    [[nodiscard]] std::size_t delta_message_count() const noexcept;
    [[nodiscard]] std::size_t bits_consumed() const noexcept;
    [[nodiscard]] std::size_t bytes_consumed() const noexcept;
    [[nodiscard]] DeltaCompatibilityProfile profile() const noexcept;

private:
    friend class DeltaDescriptionStage;

    DeltaDescriptionSignonState(
        PreResourceSignonState pre_resource,
        DeltaDescriptionStreamState delta_stream,
        DeltaCompatibilityProfile profile) noexcept;

    PreResourceSignonState pre_resource_;
    DeltaDescriptionStreamState delta_stream_;
    DeltaCompatibilityProfile profile_{
        DeltaCompatibilityProfile::stock_protocol_48_build_10210};
};

enum class DeltaDescriptionStageEventType {
    delta_schema_decoded,
    delta_registry_ready,
    post_delta_boundary,
};

// Owning bounded metadata only. No raw service/resource bytes or socket handle.
struct DeltaDescriptionStageEvent {
    DeltaDescriptionStageEventType type{
        DeltaDescriptionStageEventType::delta_schema_decoded};
    std::size_t schema_index{0U};
    std::string schema_name;
    std::size_t field_count{0U};
    std::size_t byte_offset{0U};
    std::size_t bits_consumed{0U};
    std::size_t bytes_consumed{0U};
    std::optional<std::uint8_t> boundary_opcode;
    std::optional<PostDeltaBoundaryCategory> boundary_category;
    DeltaDescriptionStageTimePoint occurred_at{};
};

enum class DeltaDescriptionTraceClassification {
    stage_started,
    pre_resource_boundary_reached,
    delta_schema_decoded,
    delta_registry_ready,
    post_delta_boundary_reached,
    stage_cancelled,
    stage_timed_out,
    unsupported_message,
    backpressure,
    secondary_stream_pending_m3,
    network_failure,
    protocol_failure,
};

struct DeltaDescriptionTraceEvent {
    DeltaDescriptionTraceClassification classification{
        DeltaDescriptionTraceClassification::stage_started};
    DeltaDescriptionStageState state{DeltaDescriptionStageState::idle};
    network::NetworkAddress endpoint;
    std::size_t schema_index{0U};
    std::string_view schema_name;
    std::size_t schema_name_length{0U};
    std::size_t field_count{0U};
    std::size_t byte_offset{0U};
    std::size_t bits_consumed{0U};
    std::size_t bytes_consumed{0U};
    std::optional<std::uint8_t> boundary_opcode;
    std::optional<PostDeltaBoundaryCategory> boundary_category;
    std::size_t transmitted_packet_count{0U};
};

using DeltaDescriptionTraceCallback =
    std::function<void(const DeltaDescriptionTraceEvent&)>;

// Owns the whole ACCEPT -> pre-resource -> delta sequence through exactly one
// nested driver and lifetime guard. It never parses the following post-delta
// body and never queues a resource request.
class DeltaDescriptionStage final {
public:
    DeltaDescriptionStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        DeltaDescriptionStageConfig config = {},
        InitialSignonTraceCallback initial_trace_callback = {},
        PreResourceSignonTraceCallback pre_resource_trace_callback = {},
        DeltaDescriptionTraceCallback trace_callback = {});
    ~DeltaDescriptionStage();

    DeltaDescriptionStage(const DeltaDescriptionStage&) = delete;
    DeltaDescriptionStage& operator=(const DeltaDescriptionStage&) = delete;
    DeltaDescriptionStage(DeltaDescriptionStage&&) = delete;
    DeltaDescriptionStage& operator=(DeltaDescriptionStage&&) = delete;

    [[nodiscard]] bool start(
        DeltaDescriptionStageTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime = {});
    void update(DeltaDescriptionStageTimePoint now);
    void cancel(DeltaDescriptionStageTimePoint now);

    [[nodiscard]] std::optional<DeltaDescriptionStageEvent> poll_event();
    [[nodiscard]] DeltaDescriptionStageState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] const std::optional<DeltaDescriptionSignonState>& result() const noexcept;
    [[nodiscard]] const std::optional<DeltaDescriptionStageError>& error() const noexcept;
    [[nodiscard]] const network::NetworkAddress& remote_endpoint() const noexcept;
    [[nodiscard]] const std::optional<network::NetworkAddress>& local_endpoint() const noexcept;
    [[nodiscard]] std::size_t pending_event_count() const noexcept;
    [[nodiscard]] std::size_t transmitted_packet_count() const noexcept;
    [[nodiscard]] std::size_t cleanup_count() const noexcept;
    [[nodiscard]] std::size_t request_queue_count() const noexcept;

private:
    friend class MovementEnvironmentStage;

    struct RetainConnectionAtBoundary final {};

    DeltaDescriptionStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        DeltaDescriptionStageConfig config,
        InitialSignonTraceCallback initial_trace_callback,
        PreResourceSignonTraceCallback pre_resource_trace_callback,
        DeltaDescriptionTraceCallback trace_callback,
        RetainConnectionAtBoundary);
    DeltaDescriptionStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        DeltaDescriptionStageConfig config,
        InitialSignonTraceCallback initial_trace_callback,
        PreResourceSignonTraceCallback pre_resource_trace_callback,
        DeltaDescriptionTraceCallback trace_callback,
        bool retain_connection_at_boundary);

    [[nodiscard]] const OwnedServicePayload* retained_source_payload() const noexcept;
    void finalize_retained_boundary(DeltaDescriptionStageTimePoint now) noexcept;
    [[nodiscard]] bool can_push_events(std::size_t count) const noexcept;
    void push_event(DeltaDescriptionStageEvent event) noexcept;
    void drain_pre_resource_events() noexcept;
    void synchronize_from_pre_resource(DeltaDescriptionStageTimePoint now);
    void decode_retained_delta_stream(DeltaDescriptionStageTimePoint now);
    void fail_from_pre_resource() noexcept;
    void fail_after_retained_boundary(
        DeltaDescriptionStageErrorCode code,
        DeltaDescriptionStageState state,
        std::string_view context,
        DeltaDescriptionStageTimePoint now,
        std::optional<DeltaDescriptionStreamErrorCode> stream_code = std::nullopt,
        std::optional<DeltaDescriptionErrorCode> parser_code = std::nullopt,
        std::optional<DeltaRegistryErrorCode> registry_code = std::nullopt) noexcept;
    void set_error(
        DeltaDescriptionStageErrorCode code,
        DeltaDescriptionStageState state,
        std::string_view context,
        std::optional<PreResourceSignonErrorCode> pre_resource_code = std::nullopt,
        std::optional<DeltaDescriptionStreamErrorCode> stream_code = std::nullopt,
        std::optional<DeltaDescriptionErrorCode> parser_code = std::nullopt,
        std::optional<DeltaRegistryErrorCode> registry_code = std::nullopt,
        std::optional<NetchanDriverErrorCode> driver_code = std::nullopt) noexcept;
    void emit_trace(
        DeltaDescriptionTraceClassification classification,
        std::size_t schema_index = 0U,
        std::string_view schema_name = {},
        std::size_t field_count = 0U,
        std::size_t byte_offset = 0U,
        std::size_t bits_consumed = 0U,
        std::size_t bytes_consumed = 0U,
        std::optional<std::uint8_t> boundary_opcode = std::nullopt,
        std::optional<PostDeltaBoundaryCategory> boundary_category = std::nullopt) noexcept;

    DeltaDescriptionStageConfig config_;
    DeltaDescriptionTraceCallback trace_callback_;
    bool trace_callback_active_{false};
    bool configuration_valid_{false};
    bool retain_connection_at_boundary_{false};
    PreResourceSignonStage pre_resource_stage_;
    std::vector<std::optional<DeltaDescriptionStageEvent>> event_slots_;
    std::size_t event_head_{0U};
    std::size_t event_size_{0U};
    DeltaDescriptionStageState state_{DeltaDescriptionStageState::idle};
    std::optional<DeltaDescriptionSignonState> result_;
    std::optional<DeltaDescriptionStageError> error_;
};

[[nodiscard]] constexpr std::string_view to_string(
    DeltaDescriptionStageErrorCode code) noexcept
{
    switch (code) {
    case DeltaDescriptionStageErrorCode::invalid_configuration:
        return "invalid_configuration";
    case DeltaDescriptionStageErrorCode::pre_resource_start_failed:
        return "pre_resource_start_failed";
    case DeltaDescriptionStageErrorCode::pre_resource_failed:
        return "pre_resource_failed";
    case DeltaDescriptionStageErrorCode::retained_payload_missing:
        return "retained_payload_missing";
    case DeltaDescriptionStageErrorCode::delta_stream_decode_failed:
        return "delta_stream_decode_failed";
    case DeltaDescriptionStageErrorCode::event_backpressure:
        return "event_backpressure";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
