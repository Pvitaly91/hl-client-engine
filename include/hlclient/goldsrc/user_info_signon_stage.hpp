#pragma once

#include <hlclient/goldsrc/movement_environment_stage.hpp>
#include <hlclient/goldsrc/user_info_update.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

class ResourceTransitionStage;

using UserInfoSignonStageClock = MovementEnvironmentStageClock;
using UserInfoSignonStageTimePoint = MovementEnvironmentStageTimePoint;

// Project safety limits, not stock engine maxima.
inline constexpr std::size_t kDefaultMaximumUserInfoStageEvents = 64U;
inline constexpr std::size_t kMaximumUserInfoStageEvents = 256U;
inline constexpr std::size_t kUserInfoSignonStageDiagnosticTextLimit = 256U;

struct UserInfoSignonStageConfig {
    MovementEnvironmentStageConfig movement_environment;
    UserInfoUpdateLimits user_info;
    std::size_t maximum_stage_events{kDefaultMaximumUserInfoStageEvents};
};

[[nodiscard]] bool valid_user_info_signon_stage_configuration(
    const UserInfoSignonStageConfig& config) noexcept;

enum class UserInfoSignonStageState {
    idle,
    waiting_for_movevars_state,
    decoding_user_info,
    user_info_ready,
    first_batch_complete,
    unsupported_message,
    timed_out,
    cancelled,
    backpressure,
    secondary_stream_pending,
    network_error,
    protocol_error,
};

enum class UserInfoSignonStageErrorCode {
    invalid_configuration,
    movement_stage_start_failed,
    movement_stage_failed,
    retained_payload_missing,
    user_info_stream_decode_failed,
    first_batch_not_complete,
    event_backpressure,
};

struct UserInfoSignonStageError {
    UserInfoSignonStageErrorCode code{
        UserInfoSignonStageErrorCode::invalid_configuration};
    std::optional<MovementEnvironmentStageErrorCode> movement_code;
    std::optional<UserInfoUpdateStreamErrorCode> stream_code;
    std::optional<UserInfoUpdateErrorCode> parser_code;
    std::optional<NetchanDriverErrorCode> driver_code;
    std::string context;
};

// Fully owning immutable initial user/session metadata. Raw payload, private
// user IDs, info values, and the opaque identity suffix are absent
// from this public surface.
class UserInfoSignonState final {
public:
    UserInfoSignonState(const UserInfoSignonState&) = default;
    UserInfoSignonState& operator=(const UserInfoSignonState&) = delete;
    UserInfoSignonState(UserInfoSignonState&&) noexcept = default;
    UserInfoSignonState& operator=(UserInfoSignonState&&) noexcept = delete;
    ~UserInfoSignonState() = default;

    [[nodiscard]] const MovementEnvironmentSignonState&
    movement_environment() const noexcept;
    [[nodiscard]] const UserInfoUpdateStreamState& stream() const noexcept;
    [[nodiscard]] const std::vector<UserInfoUpdateState>& messages() const noexcept;
    [[nodiscard]] std::size_t message_count() const noexcept;
    [[nodiscard]] const UserInfoFirstBatchCompletion& completion() const noexcept;
    [[nodiscard]] UserInfoUpdateEvidenceProfile evidence_profile() const noexcept;
    [[nodiscard]] const PreResourceSourcePayloadMetadata&
    source_payload() const noexcept;

private:
    friend class UserInfoSignonStage;

    UserInfoSignonState(
        MovementEnvironmentSignonState movement_environment,
        UserInfoUpdateStreamState stream) noexcept;

    MovementEnvironmentSignonState movement_environment_;
    UserInfoUpdateStreamState stream_;
};

enum class UserInfoSignonStageEventType {
    user_info_message_decoded,
    first_batch_complete,
};

struct UserInfoSignonStageEvent {
    UserInfoSignonStageEventType type{
        UserInfoSignonStageEventType::user_info_message_decoded};
    std::size_t message_index{0U};
    std::size_t byte_offset{0U};
    std::size_t byte_count{0U};
    std::size_t info_string_length{0U};
    std::size_t info_entry_count{0U};
    std::optional<std::size_t> player_name_length;
    std::optional<std::size_t> player_model_length;
    UserInfoSignonStageTimePoint occurred_at{};
};

enum class UserInfoSignonTraceClassification {
    stage_started,
    movevars_boundary_reached,
    user_info_message_decoded,
    first_batch_complete,
    stage_cancelled,
    stage_timed_out,
    unsupported_message,
    backpressure,
    secondary_stream_pending,
    network_failure,
    protocol_failure,
};

// Metadata only: no raw info string, values, private user ID, or
// opaque identity bytes are available to logging callbacks.
struct UserInfoSignonTraceEvent {
    UserInfoSignonTraceClassification classification{
        UserInfoSignonTraceClassification::stage_started};
    UserInfoSignonStageState state{UserInfoSignonStageState::idle};
    network::NetworkAddress endpoint;
    std::size_t message_index{0U};
    std::size_t message_count{0U};
    std::size_t byte_offset{0U};
    std::size_t byte_count{0U};
    std::size_t info_string_length{0U};
    std::size_t info_entry_count{0U};
    std::optional<std::size_t> player_name_length;
    std::optional<std::size_t> player_model_length;
    std::size_t transmitted_packet_count{0U};
};

using UserInfoSignonTraceCallback =
    std::function<void(const UserInfoSignonTraceEvent&)>;

// Owns the ACCEPT -> exact first-batch completion path. It continues over the
// retained decompressed payload and never sends the resource-transition
// request. Only ResourceTransitionStage may retain the same driver afterward.
class UserInfoSignonStage final {
public:
    UserInfoSignonStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        UserInfoSignonStageConfig config = {},
        InitialSignonTraceCallback initial_trace_callback = {},
        PreResourceSignonTraceCallback pre_resource_trace_callback = {},
        DeltaDescriptionTraceCallback delta_trace_callback = {},
        MovementEnvironmentTraceCallback movement_trace_callback = {},
        UserInfoSignonTraceCallback trace_callback = {});
    ~UserInfoSignonStage();

    UserInfoSignonStage(const UserInfoSignonStage&) = delete;
    UserInfoSignonStage& operator=(const UserInfoSignonStage&) = delete;
    UserInfoSignonStage(UserInfoSignonStage&&) = delete;
    UserInfoSignonStage& operator=(UserInfoSignonStage&&) = delete;

    [[nodiscard]] bool start(
        UserInfoSignonStageTimePoint now,
        const network::NetworkAddress& expected_local_endpoint,
        std::unique_ptr<INetchanDriverLifetime> connection_lifetime = {});
    void update(UserInfoSignonStageTimePoint now);
    void cancel(UserInfoSignonStageTimePoint now);

    [[nodiscard]] std::optional<UserInfoSignonStageEvent> poll_event();
    [[nodiscard]] UserInfoSignonStageState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] const std::optional<UserInfoSignonState>& result() const noexcept;
    [[nodiscard]] const std::optional<UserInfoSignonStageError>& error() const noexcept;
    [[nodiscard]] const network::NetworkAddress& remote_endpoint() const noexcept;
    [[nodiscard]] const std::optional<network::NetworkAddress>& local_endpoint() const noexcept;
    [[nodiscard]] std::size_t pending_event_count() const noexcept;
    [[nodiscard]] std::size_t transmitted_packet_count() const noexcept;
    [[nodiscard]] std::size_t cleanup_count() const noexcept;
    [[nodiscard]] std::size_t request_queue_count() const noexcept;

private:
    friend class ResourceTransitionStage;

    struct RetainConnectionAtBoundary final {};

    UserInfoSignonStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        UserInfoSignonStageConfig config,
        InitialSignonTraceCallback initial_trace_callback,
        PreResourceSignonTraceCallback pre_resource_trace_callback,
        DeltaDescriptionTraceCallback delta_trace_callback,
        MovementEnvironmentTraceCallback movement_trace_callback,
        UserInfoSignonTraceCallback trace_callback,
        RetainConnectionAtBoundary);
    UserInfoSignonStage(
        network::IDatagramTransport& transport,
        network::NetworkAddress remote_endpoint,
        UserInfoSignonStageConfig config,
        InitialSignonTraceCallback initial_trace_callback,
        PreResourceSignonTraceCallback pre_resource_trace_callback,
        DeltaDescriptionTraceCallback delta_trace_callback,
        MovementEnvironmentTraceCallback movement_trace_callback,
        UserInfoSignonTraceCallback trace_callback,
        bool retain_connection_at_boundary);

    [[nodiscard]] NetchanDriver* retained_driver() noexcept;
    void finalize_retained_boundary(UserInfoSignonStageTimePoint now) noexcept;
    [[nodiscard]] bool can_push_events(std::size_t count) const noexcept;
    void push_event(UserInfoSignonStageEvent event) noexcept;
    void drain_movement_events() noexcept;
    void synchronize_from_movement(UserInfoSignonStageTimePoint now);
    void decode_retained_user_info(UserInfoSignonStageTimePoint now);
    void fail_from_movement() noexcept;
    void fail_after_retained_boundary(
        UserInfoSignonStageErrorCode code,
        UserInfoSignonStageState state,
        std::string_view context,
        UserInfoSignonStageTimePoint now,
        std::optional<UserInfoUpdateStreamErrorCode> stream_code = std::nullopt,
        std::optional<UserInfoUpdateErrorCode> parser_code = std::nullopt) noexcept;
    void set_error(
        UserInfoSignonStageErrorCode code,
        UserInfoSignonStageState state,
        std::string_view context,
        std::optional<MovementEnvironmentStageErrorCode> movement_code = std::nullopt,
        std::optional<UserInfoUpdateStreamErrorCode> stream_code = std::nullopt,
        std::optional<UserInfoUpdateErrorCode> parser_code = std::nullopt,
        std::optional<NetchanDriverErrorCode> driver_code = std::nullopt) noexcept;
    void emit_trace(
        UserInfoSignonTraceClassification classification,
        std::size_t message_index = 0U,
        std::size_t message_count = 0U,
        std::size_t byte_offset = 0U,
        std::size_t byte_count = 0U,
        std::size_t info_string_length = 0U,
        std::size_t info_entry_count = 0U,
        std::optional<std::size_t> player_name_length = std::nullopt,
        std::optional<std::size_t> player_model_length = std::nullopt) noexcept;

    UserInfoSignonStageConfig config_;
    UserInfoSignonTraceCallback trace_callback_;
    bool trace_callback_active_{false};
    bool configuration_valid_{false};
    bool retain_connection_at_boundary_{false};
    MovementEnvironmentStage movement_stage_;
    std::vector<std::optional<UserInfoSignonStageEvent>> event_slots_;
    std::size_t event_head_{0U};
    std::size_t event_size_{0U};
    UserInfoSignonStageState state_{UserInfoSignonStageState::idle};
    std::optional<UserInfoSignonState> result_;
    std::optional<UserInfoSignonStageError> error_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const UserInfoSignonStageErrorCode code) noexcept
{
    switch (code) {
    case UserInfoSignonStageErrorCode::invalid_configuration:
        return "invalid_configuration";
    case UserInfoSignonStageErrorCode::movement_stage_start_failed:
        return "movement_stage_start_failed";
    case UserInfoSignonStageErrorCode::movement_stage_failed:
        return "movement_stage_failed";
    case UserInfoSignonStageErrorCode::retained_payload_missing:
        return "retained_payload_missing";
    case UserInfoSignonStageErrorCode::user_info_stream_decode_failed:
        return "user_info_stream_decode_failed";
    case UserInfoSignonStageErrorCode::first_batch_not_complete:
        return "first_batch_not_complete";
    case UserInfoSignonStageErrorCode::event_backpressure:
        return "event_backpressure";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
