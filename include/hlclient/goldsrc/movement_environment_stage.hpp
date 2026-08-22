#pragma once

#include <hlclient/goldsrc/delta_description_stage.hpp>
#include <hlclient/goldsrc/move_vars.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

using MovementEnvironmentStageClock = DeltaDescriptionStageClock;
using MovementEnvironmentStageTimePoint = DeltaDescriptionStageTimePoint;

// Project safety limits, not stock engine maxima.
inline constexpr std::size_t kDefaultMaximumMovementEnvironmentStageEvents = 64U;
inline constexpr std::size_t kMaximumMovementEnvironmentStageEvents = 256U;
inline constexpr std::size_t kMovementEnvironmentStageDiagnosticTextLimit = 256U;

struct MovementEnvironmentStageConfig {
    DeltaDescriptionStageConfig delta;
    MoveVarsLimits move_vars;
    std::size_t maximum_events{kDefaultMaximumMovementEnvironmentStageEvents};
};

[[nodiscard]] bool valid_movement_environment_stage_configuration(
    const MovementEnvironmentStageConfig& config) noexcept;

enum class MovementEnvironmentStageState {
    idle,
    waiting_for_delta_state,
    decoding_move_vars,
    environment_state_ready,
    decoding_post_environment_messages,
    post_environment_boundary_reached,
    unsupported_message,
    timed_out,
    cancelled,
    backpressure,
    secondary_stream_pending_m3,
    network_error,
    protocol_error,
};

enum class MovementEnvironmentStageErrorCode {
    invalid_configuration,
    delta_start_failed,
    delta_failed,
    retained_payload_missing,
    move_vars_stream_decode_failed,
    event_backpressure,
};

struct MovementEnvironmentStageError {
    MovementEnvironmentStageErrorCode code{
        MovementEnvironmentStageErrorCode::invalid_configuration};
    std::optional<DeltaDescriptionStageErrorCode> delta_code;
    std::optional<MoveVarsStreamErrorCode> stream_code;
    std::optional<MoveVarsErrorCode> parser_code;
    std::optional<NetchanDriverErrorCode> driver_code;
    std::string context;
};

// Fully owning immutable sign-on metadata. It contains no payload span, driver,
// socket, filesystem object, renderer object, or runtime movement state.
class MovementEnvironmentSignonState final {
public:
    MovementEnvironmentSignonState(const MovementEnvironmentSignonState&) = default;
    MovementEnvironmentSignonState& operator=(const MovementEnvironmentSignonState&) = delete;
    MovementEnvironmentSignonState(MovementEnvironmentSignonState&&) noexcept = default;
    MovementEnvironmentSignonState& operator=(MovementEnvironmentSignonState&&) noexcept = delete;
    ~MovementEnvironmentSignonState() = default;

    [[nodiscard]] const DeltaDescriptionSignonState& delta_description() const noexcept;
    [[nodiscard]] const MoveVarsStreamState& stream() const noexcept;
    [[nodiscard]] const MoveVarsState& move_vars() const noexcept;
    [[nodiscard]] const PostMoveVarsBoundary& boundary() const noexcept;
    [[nodiscard]] std::size_t control_count() const noexcept;
    [[nodiscard]] std::size_t bytes_consumed() const noexcept;

private:
    friend class MovementEnvironmentStage;

    MovementEnvironmentSignonState(
        DeltaDescriptionSignonState delta_description,
        MoveVarsStreamState stream) noexcept;

    DeltaDescriptionSignonState delta_description_;
    MoveVarsStreamState stream_;
};

enum class MovementEnvironmentStageEventType {
    movement_environment_ready,
    post_environment_control,
    post_environment_boundary,
};

// Owning bounded metadata only. Untrusted strings and raw payload bytes are
// deliberately absent.
struct MovementEnvironmentStageEvent {
    MovementEnvironmentStageEventType type{
        MovementEnvironmentStageEventType::movement_environment_ready};
    std::size_t control_index{0U};
    std::optional<std::uint8_t> opcode;
    std::size_t byte_offset{0U};
    std::size_t byte_count{0U};
    std::size_t string_length{0U};
    MovementEnvironmentStageTimePoint occurred_at{};
};

enum class MovementEnvironmentTraceClassification {
    stage_started,
    delta_boundary_reached,
    movement_environment_ready,
    post_environment_control,
    post_environment_boundary_reached,
    stage_cancelled,
    stage_timed_out,
    unsupported_message,
    backpressure,
    secondary_stream_pending_m3,
    network_failure,
    protocol_failure,
};

// Trace is synchronous and exposes only independently confirmed semantic
// fields plus bounded cursor metadata. sky_name refers to owning result storage
// for the duration of the callback only.
struct MovementEnvironmentTraceEvent {
    MovementEnvironmentTraceClassification classification{
        MovementEnvironmentTraceClassification::stage_started};
    MovementEnvironmentStageState state{MovementEnvironmentStageState::idle};
    network::NetworkAddress endpoint;
    std::size_t control_index{0U};
    std::optional<std::uint8_t> opcode;
    std::size_t byte_offset{0U};
    std::size_t byte_count{0U};
    std::size_t string_length{0U};
    std::size_t control_count{0U};
    std::optional<float> gravity;
    std::optional<float> maximum_speed;
    std::optional<float> acceleration;
    std::optional<float> air_acceleration;
    std::optional<float> friction;
    std::optional<float> step_size;
    std::optional<float> maximum_velocity;
    std::optional<bool> footsteps;
    std::string_view sky_name;
    std::size_t transmitted_packet_count{0U};
};

using MovementEnvironmentTraceCallback =
    std::function<void(const MovementEnvironmentTraceEvent&)>;

// Owns the complete ACCEPT -> movement/environment boundary sequence through
// one nested driver and one authentication lifetime. It consumes no opcode-13
// body, creates no resource response, and does not apply move variables.
class MovementEnvironmentStage final {
public:
    MovementEnvironmentStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        MovementEnvironmentStageConfig config = {},
        InitialSignonTraceCallback initial_trace_callback = {},
        PreResourceSignonTraceCallback pre_resource_trace_callback = {},
        DeltaDescriptionTraceCallback delta_trace_callback = {},
        MovementEnvironmentTraceCallback trace_callback = {});
    ~MovementEnvironmentStage();

    MovementEnvironmentStage(const MovementEnvironmentStage&) = delete;
    MovementEnvironmentStage& operator=(const MovementEnvironmentStage&) = delete;
    MovementEnvironmentStage(MovementEnvironmentStage&&) = delete;
    MovementEnvironmentStage& operator=(MovementEnvironmentStage&&) = delete;

    [[nodiscard]] bool start(
        MovementEnvironmentStageTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime = {});
    void update(MovementEnvironmentStageTimePoint now);
    void cancel(MovementEnvironmentStageTimePoint now);

    [[nodiscard]] std::optional<MovementEnvironmentStageEvent> poll_event();
    [[nodiscard]] MovementEnvironmentStageState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] const std::optional<MovementEnvironmentSignonState>& result() const noexcept;
    [[nodiscard]] const std::optional<MovementEnvironmentStageError>& error() const noexcept;
    [[nodiscard]] const network::NetworkAddress& remote_endpoint() const noexcept;
    [[nodiscard]] const std::optional<network::NetworkAddress>& local_endpoint() const noexcept;
    [[nodiscard]] std::size_t pending_event_count() const noexcept;
    [[nodiscard]] std::size_t transmitted_packet_count() const noexcept;
    [[nodiscard]] std::size_t cleanup_count() const noexcept;
    [[nodiscard]] std::size_t request_queue_count() const noexcept;

private:
    [[nodiscard]] bool can_push_events(std::size_t count) const noexcept;
    void push_event(MovementEnvironmentStageEvent event) noexcept;
    void drain_delta_events() noexcept;
    void synchronize_from_delta(MovementEnvironmentStageTimePoint now);
    void decode_retained_move_vars_stream(MovementEnvironmentStageTimePoint now);
    void fail_from_delta() noexcept;
    void fail_after_retained_boundary(
        MovementEnvironmentStageErrorCode code,
        MovementEnvironmentStageState state,
        std::string_view context,
        MovementEnvironmentStageTimePoint now,
        std::optional<MoveVarsStreamErrorCode> stream_code = std::nullopt,
        std::optional<MoveVarsErrorCode> parser_code = std::nullopt) noexcept;
    void set_error(
        MovementEnvironmentStageErrorCode code,
        MovementEnvironmentStageState state,
        std::string_view context,
        std::optional<DeltaDescriptionStageErrorCode> delta_code = std::nullopt,
        std::optional<MoveVarsStreamErrorCode> stream_code = std::nullopt,
        std::optional<MoveVarsErrorCode> parser_code = std::nullopt,
        std::optional<NetchanDriverErrorCode> driver_code = std::nullopt) noexcept;
    void emit_trace(
        MovementEnvironmentTraceClassification classification,
        std::size_t control_index = 0U,
        std::optional<std::uint8_t> opcode = std::nullopt,
        std::size_t byte_offset = 0U,
        std::size_t byte_count = 0U,
        std::size_t string_length = 0U,
        const MoveVarsState* move_vars = nullptr,
        std::size_t control_count = 0U) noexcept;

    MovementEnvironmentStageConfig config_;
    MovementEnvironmentTraceCallback trace_callback_;
    bool trace_callback_active_{false};
    bool configuration_valid_{false};
    DeltaDescriptionStage delta_stage_;
    std::vector<std::optional<MovementEnvironmentStageEvent>> event_slots_;
    std::size_t event_head_{0U};
    std::size_t event_size_{0U};
    MovementEnvironmentStageState state_{MovementEnvironmentStageState::idle};
    std::optional<MovementEnvironmentSignonState> result_;
    std::optional<MovementEnvironmentStageError> error_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const MovementEnvironmentStageErrorCode code) noexcept
{
    switch (code) {
    case MovementEnvironmentStageErrorCode::invalid_configuration:
        return "invalid_configuration";
    case MovementEnvironmentStageErrorCode::delta_start_failed:
        return "delta_start_failed";
    case MovementEnvironmentStageErrorCode::delta_failed:
        return "delta_failed";
    case MovementEnvironmentStageErrorCode::retained_payload_missing:
        return "retained_payload_missing";
    case MovementEnvironmentStageErrorCode::move_vars_stream_decode_failed:
        return "move_vars_stream_decode_failed";
    case MovementEnvironmentStageErrorCode::event_backpressure:
        return "event_backpressure";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
