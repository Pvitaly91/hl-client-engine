#pragma once

#include <hlclient/local_player/local_player_movement_controller.hpp>
#include <hlclient/prediction/prediction_reconciliation.hpp>
#include <hlclient/prediction/prediction_visual_correction.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace hlclient::local_player {

enum class LocalPlayerPredictionControllerStatus : std::uint8_t {
    idle,
    predicting,
    awaiting_authority,
    reconciling,
    replay_complete,
    visual_correction_active,
    history_backpressure,
    authority_pending,
    authority_stale,
    authority_conflict,
    replay_failed,
    hard_reset,
    stock_evidence_pending,
    cancelled,
    failed,
};

enum class LocalPlayerPredictionEventType : std::uint8_t {
    none,
    predicted_command_appended,
    prediction_history_advanced,
    authoritative_update_received,
    authoritative_update_ignored_stale,
    acknowledgement_accepted,
    misprediction_measured,
    command_replay_started,
    command_replayed,
    command_replay_completed,
    history_trimmed,
    visual_correction_started,
    visual_correction_active,
    visual_correction_constrained,
    visual_correction_completed,
    hard_reset_applied,
    prediction_backpressure,
    reconciliation_failed,
};

struct LocalPlayerPredictionEvent {
    LocalPlayerPredictionEventType type{LocalPlayerPredictionEventType::none};
    std::optional<goldsrc::GoldSrcUserCmdSequence> command_sequence;
    std::uint64_t authority_update_ordinal{0U};
    std::size_t command_count{0U};
    prediction::PredictionCorrectionClass correction_class{
        prediction::PredictionCorrectionClass::exact};
    std::uint64_t history_revision{0U};
};

// A controller operation cannot replay more than the hard replay-command
// limit. The remaining slots cover every non-command lifecycle event emitted
// by the same operation, so event publication is allocation-free and bounded.
class LocalPlayerPredictionEventBatch final {
public:
    static constexpr std::size_t maximum_event_count =
        prediction::kHardMaximumPredictionReplayCommands + 16U;

    [[nodiscard]] bool append(LocalPlayerPredictionEvent event) noexcept
    {
        if (size_ >= events_.size()) {
            return false;
        }
        events_[size_] = event;
        ++size_;
        return true;
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0U; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr std::size_t capacity() const noexcept
    {
        return events_.size();
    }
    [[nodiscard]] constexpr const LocalPlayerPredictionEvent* data()
        const noexcept
    {
        return events_.data();
    }
    [[nodiscard]] constexpr const LocalPlayerPredictionEvent* begin()
        const noexcept
    {
        return data();
    }
    [[nodiscard]] constexpr const LocalPlayerPredictionEvent* end()
        const noexcept
    {
        return data() + size_;
    }
    [[nodiscard]] constexpr const LocalPlayerPredictionEvent& operator[](
        const std::size_t index) const noexcept
    {
        return events_[index];
    }

private:
    std::array<LocalPlayerPredictionEvent, maximum_event_count> events_{};
    std::size_t size_{0U};
};

struct LocalPlayerPredictionControllerConfig {
    prediction::LocalPredictionHistoryLimits history{};
    prediction::PredictionReconciliationConfig reconciliation{};
    prediction::PredictionVisualCorrectionConfig visual{};
};

struct LocalPlayerPredictionOperationResult {
    std::shared_ptr<const movement::LocalPlayerMovementState> player_state;
    std::optional<gameplay_camera::GameplayCameraState> camera;
    std::shared_ptr<const prediction::LocalPredictionHistoryState> history;
    std::optional<prediction::PredictionError> prediction_error;
    std::optional<LocalPlayerMovementControllerError> movement_error;
    LocalPlayerPredictionEventBatch events;
    // Compatibility alias for callers that consumed the former single-event
    // API. It remains the operation's principal/terminal event.
    std::optional<LocalPlayerPredictionEvent> event;
    LocalPlayerPredictionControllerStatus status{
        LocalPlayerPredictionControllerStatus::idle};
    std::size_t command_count{0U};
    std::size_t replay_depth{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return player_state != nullptr && camera.has_value() &&
            history != nullptr && !prediction_error.has_value() &&
            !movement_error.has_value();
    }
};

class LocalPlayerPredictionController final {
public:
    struct CreateResult;

    LocalPlayerPredictionController(const LocalPlayerPredictionController&) =
        delete;
    LocalPlayerPredictionController& operator=(
        const LocalPlayerPredictionController&) = delete;
    LocalPlayerPredictionController(
        LocalPlayerPredictionController&&) noexcept = default;
    LocalPlayerPredictionController& operator=(
        LocalPlayerPredictionController&&) = delete;
    ~LocalPlayerPredictionController() = default;

    [[nodiscard]] static CreateResult create(
        LocalPlayerMovementController movement_controller,
        prediction::PredictionSessionIdentity session,
        LocalPlayerPredictionControllerConfig config = {});

    [[nodiscard]] const prediction::PredictionSessionIdentity& session()
        const noexcept;
    [[nodiscard]] const LocalPlayerMovementController& movement_controller()
        const noexcept;
    [[nodiscard]] const std::shared_ptr<
        const prediction::LocalPredictionHistoryState>&
    history() const noexcept;
    [[nodiscard]] LocalPlayerPredictionControllerStatus status() const noexcept;
    [[nodiscard]] const prediction::PredictionVisualCorrectionState&
    visual_correction() const noexcept;
    [[nodiscard]] const prediction::LocalPredictionStatistics& statistics()
        const noexcept;

    [[nodiscard]] LocalPlayerPredictionOperationResult update_local_input(
        std::int64_t monotonic_time_nanoseconds,
        const gameplay_input::GameplayInputIntent& intent,
        const goldsrc::movement::ILocalMovementCollision& collision,
        goldsrc::movement::GoldSrcLocalMovementScratch& scratch);

    [[nodiscard]] LocalPlayerPredictionOperationResult
    apply_authoritative_state(
        const prediction::AuthoritativePlayerState& authoritative,
        double monotonic_time_seconds,
        const goldsrc::movement::ILocalMovementCollision& collision,
        goldsrc::movement::GoldSrcLocalMovementScratch& scratch);

    [[nodiscard]] LocalPlayerPredictionOperationResult sample_camera(
        double monotonic_time_seconds,
        const collision::CollisionWorldQuery* collision_query,
        collision::CollisionQueryScratch& scratch,
        const collision::CollisionQueryLimits& query_limits = {});

    void cancel() noexcept;

private:
    LocalPlayerPredictionController(
        LocalPlayerMovementController movement_controller,
        prediction::PredictionSessionIdentity session,
        std::shared_ptr<const prediction::LocalPredictionHistoryState> history,
        LocalPlayerPredictionControllerConfig config) noexcept;

    [[nodiscard]] LocalPlayerPredictionOperationResult current_result(
        std::optional<prediction::PredictionError> prediction_error =
            std::nullopt,
        std::optional<LocalPlayerMovementControllerError> movement_error =
            std::nullopt,
        std::optional<LocalPlayerPredictionEvent> event = std::nullopt,
        LocalPlayerPredictionEventBatch events = {});

    LocalPlayerMovementController movement_controller_;
    prediction::PredictionSessionIdentity session_{};
    std::shared_ptr<const prediction::LocalPredictionHistoryState> history_;
    LocalPlayerPredictionControllerConfig config_{};
    std::optional<prediction::PredictionVisualCorrectionState>
        visual_correction_{
            prediction::PredictionVisualCorrectionState::inactive()};
    prediction::LocalPredictionStatistics statistics_{};
    LocalPlayerPredictionControllerStatus status_{
        LocalPlayerPredictionControllerStatus::idle};
    bool cancelled_{false};
};

struct LocalPlayerPredictionController::CreateResult {
    std::unique_ptr<LocalPlayerPredictionController> controller;
    std::optional<prediction::PredictionError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return controller != nullptr && !error.has_value();
    }
};

[[nodiscard]] std::string_view to_string(
    LocalPlayerPredictionControllerStatus status) noexcept;
[[nodiscard]] std::string_view to_string(
    LocalPlayerPredictionEventType type) noexcept;

} // namespace hlclient::local_player
