#pragma once

#include <hlclient/gameplay_camera/first_person_camera.hpp>
#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/goldsrc/movement/goldsrc_local_movement.hpp>
#include <hlclient/goldsrc/usercmd_input_adapter.hpp>
#include <hlclient/goldsrc/usercmd_scheduler.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::local_player {

struct LocalPlayerMovementControllerConfig {
    goldsrc::GoldSrcUserCmdSchedulerConfig scheduler{};
    goldsrc::GoldSrcUserCmdMovementSpeedConfig movement_speeds{};
    goldsrc::movement::GoldSrcLocalMovementConfig movement{};
    gameplay_camera::FirstPersonCameraConfig camera{
        gameplay_camera::FirstPersonCameraConfig::project_default_v1()};
};

enum class LocalPlayerMovementControllerErrorCode : std::uint8_t {
    invalid_configuration,
    invalid_initial_state,
    camera_update_failed,
    scheduler_failed,
    command_build_failed,
    movement_simulation_failed,
    camera_revision_exhausted,
    statistics_overflow,
    stale_prepared_update,
    prepared_update_already_consumed,
    invalid_reconciled_state,
    allocation_failed,
};

[[nodiscard]] std::string_view to_string(
    LocalPlayerMovementControllerErrorCode code) noexcept;

struct LocalPlayerMovementControllerError {
    LocalPlayerMovementControllerErrorCode code{
        LocalPlayerMovementControllerErrorCode::invalid_configuration};
    std::optional<goldsrc::GoldSrcUserCmdSchedulerError> scheduler_error;
    std::optional<goldsrc::GoldSrcUserCmdInputAdapterError> command_error;
    std::optional<goldsrc::movement::LocalMovementSimulationError>
        movement_error;
    std::optional<gameplay_camera::GameplayCameraError> camera_error;
    std::string_view context;
};

// Optional exact-match filter for caller-owned diagnostics.  The controller
// evaluates it only against touches returned by successfully simulated
// commands.  Speculative direct/step traces that the movement kernel discards
// never reach this boundary.
struct LocalPlayerMovementCommittedTouchFilter final {
    movement::PlayerMovementHitIdentity hit{};
    movement::PlayerMovementPlane plane{};
};

struct LocalPlayerMovementControllerUpdateResult {
    std::optional<movement::LocalPlayerMovementState> player_state;
    std::optional<gameplay_camera::GameplayCameraState> camera;
    movement::PlayerMovementStatistics statistics{};
    std::optional<LocalPlayerMovementControllerError> error;
    std::size_t generated_command_count{0U};
    std::uint64_t final_state_signature{0U};
    std::uint64_t committed_touch_match_count{0U};
    bool player_state_changed{false};
    bool camera_revision_changed{false};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return player_state.has_value() && camera.has_value() &&
            !error.has_value();
    }
};

struct LocalPlayerMovementPreparedTouchSummary final {
    std::size_t touch_count{0U};
    std::optional<movement::PlayerMovementHitKind> first_hit_kind;
    std::optional<movement::PlayerMovementHitKind> last_hit_kind;
    std::uint64_t deterministic_signature{0U};
};

struct LocalPlayerMovementPreparedCommand final {
    std::shared_ptr<const goldsrc::GoldSrcUserCmdState> command;
    std::shared_ptr<const movement::LocalPlayerMovementState> pre_state;
    std::shared_ptr<const movement::LocalPlayerMovementState> post_state;
    movement::PlayerMovementStatistics statistics{};
    LocalPlayerMovementPreparedTouchSummary touch_summary{};
};

class LocalPlayerMovementPreparedUpdate final {
public:
    LocalPlayerMovementPreparedUpdate(
        const LocalPlayerMovementPreparedUpdate&) = delete;
    LocalPlayerMovementPreparedUpdate& operator=(
        const LocalPlayerMovementPreparedUpdate&) = delete;
    LocalPlayerMovementPreparedUpdate(
        LocalPlayerMovementPreparedUpdate&& other) noexcept;
    LocalPlayerMovementPreparedUpdate& operator=(
        LocalPlayerMovementPreparedUpdate&& other) noexcept;
    ~LocalPlayerMovementPreparedUpdate() = default;

    [[nodiscard]] std::uint64_t base_controller_revision() const noexcept;
    [[nodiscard]] std::uint64_t plan_identity() const noexcept;
    [[nodiscard]] std::span<const LocalPlayerMovementPreparedCommand> commands()
        const noexcept;
    [[nodiscard]] const movement::LocalPlayerMovementState& final_player_state()
        const noexcept;
    [[nodiscard]] const gameplay_camera::GameplayCameraState& final_camera()
        const noexcept;
    [[nodiscard]] const movement::PlayerMovementStatistics& statistics()
        const noexcept;
    [[nodiscard]] std::uint64_t committed_touch_match_count() const noexcept;
    [[nodiscard]] bool consumable() const noexcept;

private:
    friend class LocalPlayerMovementController;

    LocalPlayerMovementPreparedUpdate(
        const void* owner,
        std::uint64_t base_controller_revision,
        std::uint64_t plan_identity,
        std::vector<LocalPlayerMovementPreparedCommand> commands,
        movement::LocalPlayerMovementState final_player_state,
        gameplay_camera::GameplayCameraState final_camera,
        goldsrc::GoldSrcUserCmdScheduler staged_scheduler,
        gameplay_input::GameplayButtonMask staged_pending_one_shots,
        movement::PlayerMovementStatistics statistics,
        std::uint64_t committed_touch_match_count,
        bool camera_revision_changed) noexcept;

    void invalidate_after_move() noexcept;

    const void* owner_{nullptr};
    std::uint64_t base_controller_revision_{0U};
    std::uint64_t plan_identity_{0U};
    std::vector<LocalPlayerMovementPreparedCommand> commands_;
    std::optional<movement::LocalPlayerMovementState> final_player_state_;
    std::optional<gameplay_camera::GameplayCameraState> final_camera_;
    goldsrc::GoldSrcUserCmdScheduler staged_scheduler_{};
    gameplay_input::GameplayButtonMask staged_pending_one_shots_{0U};
    movement::PlayerMovementStatistics statistics_{};
    std::uint64_t committed_touch_match_count_{0U};
    bool camera_revision_changed_{false};
    bool consumable_{true};
};

struct LocalPlayerMovementPrepareResult {
    std::optional<LocalPlayerMovementPreparedUpdate> prepared_update;
    LocalPlayerMovementControllerUpdateResult preview;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return prepared_update.has_value() && preview &&
            !preview.error.has_value();
    }
};

// Local orchestration only. The controller samples synthetic commands at a
// caller-supplied monotonic fixed cadence and never publishes them to network
// history or a transport.
class LocalPlayerMovementController final {
public:
    LocalPlayerMovementController(
        movement::LocalPlayerMovementState initial_state,
        goldsrc::movement::GoldSrcMovementEnvironment environment,
        LocalPlayerMovementControllerConfig config = {}) noexcept;

    LocalPlayerMovementController(const LocalPlayerMovementController&) = delete;
    LocalPlayerMovementController& operator=(
        const LocalPlayerMovementController&) = delete;
    LocalPlayerMovementController(LocalPlayerMovementController&&) noexcept =
        default;
    LocalPlayerMovementController& operator=(
        LocalPlayerMovementController&&) = delete;
    ~LocalPlayerMovementController() = default;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const movement::LocalPlayerMovementState& player_state()
        const noexcept;
    [[nodiscard]] const gameplay_camera::GameplayCameraState& camera()
        const noexcept;
    [[nodiscard]] const goldsrc::movement::GoldSrcMovementEnvironment&
    environment() const noexcept;
    [[nodiscard]] const LocalPlayerMovementControllerConfig& config()
        const noexcept;
    [[nodiscard]] gameplay_input::GameplayButtonMask pending_one_shots()
        const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    // Accepts caller-owned edge input before a transactional prepare.  The
    // accepted edge remains pending until a prepared command commits, so an
    // abandoned plan cannot lose jump/impulse/button presses.
    void capture_pending_input(
        const gameplay_input::GameplayInputIntent& intent) noexcept;
    void discard_pending_input() noexcept;

    [[nodiscard]] LocalPlayerMovementPrepareResult prepare_update(
        std::int64_t monotonic_time_nanoseconds,
        const gameplay_input::GameplayInputIntent& intent,
        const goldsrc::movement::ILocalMovementCollision& collision,
        goldsrc::movement::GoldSrcLocalMovementScratch& scratch,
        const LocalPlayerMovementCommittedTouchFilter*
            committed_touch_filter = nullptr) const;

    [[nodiscard]] std::optional<LocalPlayerMovementControllerError>
    preflight_prepared_update(
        const LocalPlayerMovementPreparedUpdate& prepared) const noexcept;

    [[nodiscard]] LocalPlayerMovementControllerUpdateResult
    commit_prepared_update(LocalPlayerMovementPreparedUpdate&& prepared)
        noexcept;

    void abandon_prepared_update(
        LocalPlayerMovementPreparedUpdate& prepared) const noexcept;

    [[nodiscard]] LocalPlayerMovementControllerUpdateResult
    replace_simulation_state(
        std::uint64_t expected_controller_revision,
        movement::LocalPlayerMovementState corrected_state,
        bool reset_command_stream = false) noexcept;

    [[nodiscard]] LocalPlayerMovementControllerUpdateResult update(
        std::int64_t monotonic_time_nanoseconds,
        const gameplay_input::GameplayInputIntent& intent,
        const goldsrc::movement::ILocalMovementCollision& collision,
        goldsrc::movement::GoldSrcLocalMovementScratch& scratch,
        const LocalPlayerMovementCommittedTouchFilter*
            committed_touch_filter = nullptr);

private:
    [[nodiscard]] std::optional<gameplay_camera::GameplayCameraState>
    make_player_camera(
        const movement::LocalPlayerMovementState& state,
        double yaw_degrees,
        double pitch_degrees,
        std::uint64_t previous_revision,
        bool& revision_changed,
        std::optional<gameplay_camera::GameplayCameraError>& error) const
        noexcept;

    std::optional<movement::LocalPlayerMovementState> player_state_;
    std::optional<goldsrc::movement::GoldSrcMovementEnvironment> environment_;
    std::optional<gameplay_camera::GameplayCameraState> camera_;
    LocalPlayerMovementControllerConfig config_{};
    goldsrc::GoldSrcUserCmdScheduler scheduler_{};
    goldsrc::GoldSrcUserCmdInputAdapter input_adapter_{};
    gameplay_input::GameplayButtonMask pending_one_shots_{0U};
    std::uint64_t revision_{1U};
    bool valid_configuration_{false};
};

} // namespace hlclient::local_player
