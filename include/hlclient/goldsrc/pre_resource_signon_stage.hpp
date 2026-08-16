#pragma once

#include <hlclient/goldsrc/initial_signon_stage.hpp>
#include <hlclient/goldsrc/service_message_stream.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

using PreResourceSignonClock = InitialSignonClock;
using PreResourceSignonTimePoint = InitialSignonTimePoint;

// Project safety limits, not stock engine maxima.
inline constexpr std::size_t kDefaultMaximumPreResourceSignonEvents = 32U;
inline constexpr std::size_t kMaximumPreResourceSignonEvents = 256U;
inline constexpr std::size_t kPreResourceSignonDiagnosticTextLimit = 256U;

struct PreResourceSignonConfig {
    InitialSignonConfig initial_signon;
    std::size_t maximum_events{kDefaultMaximumPreResourceSignonEvents};
};

[[nodiscard]] bool valid_pre_resource_signon_configuration(
    const PreResourceSignonConfig& config) noexcept;

enum class PreResourceSignonStageState {
    idle,
    waiting_for_initial_boundary,
    decoding_server_info,
    server_info_ready,
    decoding_pre_resource_messages,
    pre_resource_boundary_reached,
    unsupported_message,
    timed_out,
    cancelled,
    backpressure,
    secondary_stream_pending_m3,
    network_error,
    protocol_error,
};

enum class PreResourceSignonErrorCode {
    invalid_configuration,
    initial_signon_start_failed,
    initial_signon_failed,
    pre_resource_decode_failed,
    unsupported_message,
    event_backpressure,
};

struct PreResourceSignonError {
    PreResourceSignonErrorCode code{
        PreResourceSignonErrorCode::invalid_configuration};
    std::optional<InitialSignonErrorCode> initial_signon_code;
    std::optional<PreResourceServiceErrorCode> service_code;
    std::optional<ServerInfoErrorCode> server_info_code;
    std::optional<NetchanDriverErrorCode> driver_code;
    std::string context;
};

enum class PreResourceSignonEventType {
    server_info_ready,
    pre_resource_control,
    resource_phase_boundary,
};

// Events are owning metadata only. They contain no payload span, server text,
// path, digest, authentication material, or command body.
struct PreResourceSignonEvent {
    PreResourceSignonEventType type{
        PreResourceSignonEventType::server_info_ready};
    std::optional<std::uint8_t> opcode;
    std::size_t byte_offset{0U};
    std::size_t byte_count{0U};
    std::size_t string_length{0U};
    std::optional<std::uint8_t> control_value;
    std::optional<ResourcePhaseBoundaryDirection> boundary_direction;
    std::optional<ResourcePhaseEvidenceStatus> evidence_status;
    PreResourceSignonTimePoint occurred_at{};
};

enum class PreResourceSignonTraceClassification {
    stage_started,
    initial_boundary_reached,
    server_info_ready,
    pre_resource_control,
    pre_resource_boundary_reached,
    stage_cancelled,
    stage_timed_out,
    secondary_stream_pending_m3,
    unsupported_message,
    backpressure,
    network_failure,
    protocol_failure,
};

// Trace exposes only confirmed numeric and cursor metadata. Untrusted strings,
// raw server-info bytes, opaque fields, digests, and command text never enter
// this structure.
struct PreResourceSignonTraceEvent {
    PreResourceSignonTraceClassification classification{
        PreResourceSignonTraceClassification::stage_started};
    PreResourceSignonStageState state{PreResourceSignonStageState::idle};
    network::NetworkAddress endpoint;
    std::optional<std::uint8_t> opcode;
    std::size_t byte_offset{0U};
    std::size_t byte_count{0U};
    std::size_t string_length{0U};
    std::optional<std::uint32_t> protocol_version;
    std::optional<std::uint8_t> maximum_clients;
    std::optional<bool> multi_client_mode;
    std::optional<ResourcePhaseBoundaryDirection> boundary_direction;
    std::optional<ResourcePhaseEvidenceStatus> evidence_status;
    std::size_t transmitted_packet_count{0U};
};

using PreResourceSignonTraceCallback =
    std::function<void(const PreResourceSignonTraceEvent&)>;

// Owns one InitialSignonStage and, transitively, its one persistent driver and
// authentication lifetime. The nested stage retains that connection only at
// the M2.4.1 boundary, while this facade performs a synchronous, exact-cursor
// continuation over the already-owned decompressed payload. No second driver,
// envelope decode, opcode-8 decode, or client continuation is created.
class PreResourceSignonStage final {
public:
    PreResourceSignonStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        PreResourceSignonConfig config = {},
        InitialSignonTraceCallback initial_trace_callback = {},
        PreResourceSignonTraceCallback trace_callback = {});
    ~PreResourceSignonStage();

    PreResourceSignonStage(const PreResourceSignonStage&) = delete;
    PreResourceSignonStage& operator=(const PreResourceSignonStage&) = delete;
    PreResourceSignonStage(PreResourceSignonStage&&) = delete;
    PreResourceSignonStage& operator=(PreResourceSignonStage&&) = delete;

    [[nodiscard]] bool start(
        PreResourceSignonTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime = {});
    void update(PreResourceSignonTimePoint now);
    void cancel(PreResourceSignonTimePoint now);

    [[nodiscard]] std::optional<PreResourceSignonEvent> poll_event();
    [[nodiscard]] PreResourceSignonStageState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] const std::optional<PreResourceSignonState>& result() const noexcept;
    [[nodiscard]] const std::optional<PreResourceSignonError>& error() const noexcept;
    [[nodiscard]] const network::NetworkAddress& remote_endpoint() const noexcept;
    [[nodiscard]] const std::optional<network::NetworkAddress>&
    local_endpoint() const noexcept;
    [[nodiscard]] std::size_t pending_event_count() const noexcept;
    [[nodiscard]] std::size_t transmitted_packet_count() const noexcept;
    [[nodiscard]] std::size_t cleanup_count() const noexcept;
    [[nodiscard]] std::size_t request_queue_count() const noexcept;

private:
    [[nodiscard]] bool can_push_events(std::size_t count = 1U) const noexcept;
    void push_event(PreResourceSignonEvent event) noexcept;
    void drain_initial_events() noexcept;
    void synchronize_from_initial(PreResourceSignonTimePoint now);
    void decode_retained_boundary(PreResourceSignonTimePoint now);
    void fail_from_initial() noexcept;
    void fail_after_retained_boundary(
        PreResourceSignonErrorCode code,
        PreResourceSignonStageState state,
        std::string_view context,
        PreResourceSignonTimePoint now,
        std::optional<PreResourceServiceErrorCode> service_code = std::nullopt,
        std::optional<ServerInfoErrorCode> server_info_code = std::nullopt) noexcept;
    void set_error(
        PreResourceSignonErrorCode code,
        PreResourceSignonStageState state,
        std::string_view context,
        std::optional<InitialSignonErrorCode> initial_signon_code = std::nullopt,
        std::optional<PreResourceServiceErrorCode> service_code = std::nullopt,
        std::optional<ServerInfoErrorCode> server_info_code = std::nullopt,
        std::optional<NetchanDriverErrorCode> driver_code = std::nullopt) noexcept;
    void emit_trace(
        PreResourceSignonTraceClassification classification,
        std::optional<std::uint8_t> opcode = std::nullopt,
        std::size_t byte_offset = 0U,
        std::size_t byte_count = 0U,
        std::size_t string_length = 0U,
        const ServerInfoState* server_info = nullptr,
        const ResourcePhaseBoundary* boundary = nullptr) noexcept;

    PreResourceSignonConfig config_;
    PreResourceSignonTraceCallback trace_callback_;
    bool trace_callback_active_{false};
    bool configuration_valid_{false};
    InitialSignonStage initial_stage_;
    std::vector<std::optional<PreResourceSignonEvent>> event_slots_;
    std::size_t event_head_{0U};
    std::size_t event_size_{0U};
    PreResourceSignonStageState state_{PreResourceSignonStageState::idle};
    std::optional<PreResourceSignonState> result_;
    std::optional<PreResourceSignonError> error_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const PreResourceSignonErrorCode code) noexcept
{
    switch (code) {
    case PreResourceSignonErrorCode::invalid_configuration:
        return "invalid_configuration";
    case PreResourceSignonErrorCode::initial_signon_start_failed:
        return "initial_signon_start_failed";
    case PreResourceSignonErrorCode::initial_signon_failed:
        return "initial_signon_failed";
    case PreResourceSignonErrorCode::pre_resource_decode_failed:
        return "pre_resource_decode_failed";
    case PreResourceSignonErrorCode::unsupported_message:
        return "unsupported_message";
    case PreResourceSignonErrorCode::event_backpressure:
        return "event_backpressure";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
