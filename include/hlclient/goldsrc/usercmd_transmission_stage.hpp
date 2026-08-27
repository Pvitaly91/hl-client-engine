#pragma once

#include <hlclient/goldsrc/netchan_driver.hpp>
#include <hlclient/goldsrc/usercmd_input_adapter.hpp>
#include <hlclient/goldsrc/usercmd_packet_planner.hpp>
#include <hlclient/goldsrc/usercmd_scheduler.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

enum class GoldSrcUserCmdSessionPrerequisiteProfile : std::uint8_t {
    synthetic_runtime_ready_v1,
    stock_runtime_ready_evidence_pending,
};

struct GoldSrcUserCmdSessionPrerequisite {
    GoldSrcUserCmdSessionPrerequisiteProfile profile{
        GoldSrcUserCmdSessionPrerequisiteProfile::
            stock_runtime_ready_evidence_pending};
    bool runtime_ready{false};
};

enum class GoldSrcUserCmdTransmissionState : std::uint8_t {
    idle,
    waiting_for_runtime_ready,
    sampling_commands,
    command_history_ready,
    waiting_for_packet_context,
    planning_move_packet,
    encoding_move_packet,
    waiting_for_unreliable_submission,
    move_packet_submitted,
    waiting_for_next_sample,
    history_backpressure,
    unreliable_backpressure,
    event_backpressure,
    checksum_evidence_pending,
    signon_evidence_pending,
    cancelled,
    timed_out,
    network_error,
    protocol_error,
    closed,
};

enum class GoldSrcUserCmdTransmissionEventType : std::uint8_t {
    usercmd_sampled,
    usercmd_history_inserted,
    move_plan_prepared,
    move_context_stale,
    move_packet_encoded,
    move_packet_submitted,
    backup_commands_included,
    new_commands_included,
    history_evicted,
    history_backpressure,
    unreliable_backpressure,
    checksum_pending,
    signon_pending,
    cancelled,
    timed_out,
    network_error,
    protocol_error,
};

// Metadata only: intentionally no command values, button masks, view angles,
// packet bytes, authentication material, or player identity.
struct GoldSrcUserCmdTransmissionEvent {
    GoldSrcUserCmdTransmissionEventType type{
        GoldSrcUserCmdTransmissionEventType::protocol_error};
    std::optional<std::uint32_t> command_sequence;
    std::optional<std::uint64_t> input_sequence;
    std::size_t new_command_count{0U};
    std::size_t backup_command_count{0U};
    std::size_t encoded_bytes{0U};
    std::size_t encoded_bits{0U};
    std::size_t changed_field_count{0U};
    std::optional<std::uint32_t> outgoing_netchan_sequence;
    std::size_t history_size{0U};
};

struct GoldSrcUserCmdTransmissionConfig {
    GoldSrcUserCmdLimits limits{};
    GoldSrcUserCmdSchedulerConfig scheduler{};
    GoldSrcUserCmdPacketPlannerConfig planner{};
    GoldSrcUserCmdHistoryConfig history{};
    GoldSrcUserCmdMovementSpeedConfig movement_speeds{};
    std::uint16_t lerp_msec{0U};
    std::uint8_t light_level{0U};
    // Maximum time without a newly sampled command or a committed move send.
    // This is a stalled-work deadline, not an absolute session lifetime.
    std::chrono::milliseconds timeout{30'000};
    std::size_t maximum_events{128U};
    // Preparing/encoding and committing/sending are separate bounded phases.
    // Two preserves the ordinary single-update path; one lets an event-loop
    // yield at the owning outgoing-context boundary without copying plans.
    std::size_t maximum_transmission_phases_per_update{2U};
};

enum class GoldSrcUserCmdTransmissionErrorCode : std::uint8_t {
    invalid_configuration,
    not_active,
    runtime_signon_evidence_pending,
    scheduler_failed,
    adapter_failed,
    history_failed,
    packet_plan_failed,
    packet_context_failed,
    packet_context_stale,
    packet_too_large,
    driver_submission_failed,
    driver_send_failed,
    one_shot_pending,
    event_backpressure,
    counter_exhausted,
    timed_out,
    cancelled,
};

struct GoldSrcUserCmdTransmissionError {
    GoldSrcUserCmdTransmissionErrorCode code{
        GoldSrcUserCmdTransmissionErrorCode::invalid_configuration};
    std::optional<GoldSrcUserCmdSchedulerErrorCode> scheduler_code;
    std::optional<GoldSrcUserCmdInputAdapterErrorCode> adapter_code;
    std::optional<GoldSrcUserCmdHistoryErrorCode> history_code;
    std::optional<GoldSrcUserCmdPacketPlannerErrorCode> planner_code;
    std::optional<NetchanDriverErrorCode> driver_code;
    std::string_view context;
};

struct GoldSrcUserCmdTransmissionOperationResult {
    std::optional<GoldSrcUserCmdTransmissionError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

class GoldSrcUserCmdTransmissionStage final {
public:
    GoldSrcUserCmdTransmissionStage(
        NetchanDriver& driver,
        const GoldSrcUserCmdSchemaBinding& binding,
        GoldSrcUserCmdSessionPrerequisite prerequisite,
        GoldSrcUserCmdTransmissionConfig config = {});

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] GoldSrcUserCmdTransmissionState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] std::size_t sampled_command_count() const noexcept;
    [[nodiscard]] std::size_t transmitted_packet_count() const noexcept;
    [[nodiscard]] GoldSrcUserCmdHistoryState history() const;
    [[nodiscard]] const std::optional<GoldSrcUserCmdTransmissionError>&
    last_error() const noexcept;
    [[nodiscard]] std::optional<GoldSrcUserCmdTransmissionEvent> poll_event();

    [[nodiscard]] GoldSrcUserCmdTransmissionOperationResult queue_impulse(
        std::uint8_t impulse) noexcept;
    [[nodiscard]] GoldSrcUserCmdTransmissionOperationResult update(
        NetchanDriverTimePoint now,
        const gameplay_input::GameplayInputIntent& intent,
        const gameplay_camera::GameplayCameraState& camera);
    void cancel(NetchanDriverTimePoint now) noexcept;
    void close(NetchanDriverTimePoint now) noexcept;

private:
    friend class detail::GoldSrcUserCmdTransactionalTestAccess;

    struct PreparedMove final {
        NetchanOutgoingContextPlan context;
        GoldSrcUserCmdPacketPlan packet;
        std::uint64_t context_identity{0U};
        std::size_t encoded_bytes{0U};
        std::size_t encoded_bits{0U};
        std::size_t changed_field_count{0U};
        std::uint32_t outgoing_sequence{0U};
    };

    [[nodiscard]] bool push_event(GoldSrcUserCmdTransmissionEvent event) noexcept;
    [[nodiscard]] GoldSrcUserCmdTransmissionOperationResult commit_prepared_move(
        NetchanDriverTimePoint now);
    void abandon_prepared_move() noexcept;
    [[nodiscard]] GoldSrcUserCmdTransmissionOperationResult fail(
        GoldSrcUserCmdTransmissionState state,
        GoldSrcUserCmdTransmissionError error,
        GoldSrcUserCmdTransmissionEventType event,
        NetchanDriverTimePoint now,
        bool discard_pending_input) noexcept;

    NetchanDriver& driver_;
    GoldSrcUserCmdSchemaBinding binding_;
    GoldSrcUserCmdSessionPrerequisite prerequisite_;
    GoldSrcUserCmdTransmissionConfig config_;
    GoldSrcUserCmdTransmissionState state_{
        GoldSrcUserCmdTransmissionState::idle};
    GoldSrcUserCmdScheduler scheduler_;
    GoldSrcUserCmdHistoryBuilder history_;
    GoldSrcUserCmdPacketPlanner planner_;
    GoldSrcUserCmdInputAdapter adapter_;
    bool valid_configuration_{false};
    gameplay_input::GameplayButtonMask pending_pressed_buttons_{0U};
    std::optional<std::uint8_t> pending_impulse_;
    std::optional<PreparedMove> prepared_move_;
    std::optional<NetchanDriverTimePoint> last_progress_at_;
    std::optional<NetchanDriverTimePoint> last_update_;
    std::optional<GoldSrcUserCmdTransmissionError> last_error_;
    std::vector<GoldSrcUserCmdTransmissionEvent> events_;
    std::size_t next_event_index_{0U};
    std::size_t sampled_command_count_{0U};
    std::size_t transmitted_packet_count_{0U};
};

} // namespace hlclient::goldsrc
