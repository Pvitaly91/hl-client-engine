#pragma once

#include <hlclient/prediction/authoritative_player_state.hpp>

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace hlclient::prediction {

class LocalPlayerPredictionReconciler;
class LocalPredictionHistoryState;
struct LocalPredictionAppendResult;
struct PredictedCommandAppend;

class PredictionHistoryAnchor final {
public:
    PredictionHistoryAnchor(const PredictionHistoryAnchor&) = default;
    PredictionHistoryAnchor(PredictionHistoryAnchor&&) noexcept = default;
    PredictionHistoryAnchor& operator=(const PredictionHistoryAnchor&) = delete;
    PredictionHistoryAnchor& operator=(PredictionHistoryAnchor&&) = delete;
    ~PredictionHistoryAnchor() = default;

    [[nodiscard]] const std::shared_ptr<
        const movement::LocalPlayerMovementState>&
    movement_state() const noexcept;
    [[nodiscard]] const AuthoritativeCommandAcknowledgement& acknowledgement()
        const noexcept;
    [[nodiscard]] const std::optional<AuthoritativePlayerUpdateIdentity>&
    authority_update_identity() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> authority_update_ordinal()
        const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> authority_state_signature()
        const noexcept;
    [[nodiscard]] std::uint64_t state_signature() const noexcept;
    [[nodiscard]] const PredictionSessionIdentity& session() const noexcept;

private:
    friend class LocalPredictionHistoryState;
    friend class LocalPlayerPredictionReconciler;
    friend LocalPredictionAppendResult append_local_prediction_commands(
        const LocalPredictionHistoryState&,
        std::span<const PredictedCommandAppend>);

    PredictionHistoryAnchor(
        std::shared_ptr<const movement::LocalPlayerMovementState> movement_state,
        std::optional<AuthoritativePlayerUpdateIdentity>
            authority_update_identity,
        std::optional<std::uint64_t> authority_state_signature,
        PredictionSessionIdentity session) noexcept;

    std::shared_ptr<const movement::LocalPlayerMovementState> movement_state_;
    std::optional<AuthoritativePlayerUpdateIdentity> authority_update_identity_;
    std::optional<std::uint64_t> authority_state_signature_;
    std::uint64_t state_signature_{0U};
    PredictionSessionIdentity session_{};
};

class PredictedCommandEntry final {
public:
    PredictedCommandEntry(const PredictedCommandEntry&) = default;
    PredictedCommandEntry(PredictedCommandEntry&&) noexcept = default;
    PredictedCommandEntry& operator=(const PredictedCommandEntry&) = delete;
    PredictedCommandEntry& operator=(PredictedCommandEntry&&) = delete;
    ~PredictedCommandEntry() = default;

    [[nodiscard]] const std::shared_ptr<const goldsrc::GoldSrcUserCmdState>&
    command() const noexcept;
    [[nodiscard]] goldsrc::GoldSrcUserCmdSequence command_sequence()
        const noexcept;
    [[nodiscard]] const std::shared_ptr<
        const movement::LocalPlayerMovementState>&
    pre_command_state() const noexcept;
    [[nodiscard]] const std::shared_ptr<
        const movement::LocalPlayerMovementState>&
    post_command_state() const noexcept;
    [[nodiscard]] std::uint64_t pre_state_signature() const noexcept;
    [[nodiscard]] std::uint64_t post_state_signature() const noexcept;
    [[nodiscard]] const movement::PlayerMovementStatistics&
    simulation_statistics() const noexcept;
    [[nodiscard]] const PredictionTouchSummary& touch_summary() const noexcept;
    [[nodiscard]] std::uint64_t prediction_generation() const noexcept;
    [[nodiscard]] std::uint64_t entry_ordinal() const noexcept;

private:
    friend class LocalPredictionHistoryState;
    friend class LocalPlayerPredictionReconciler;
    friend LocalPredictionAppendResult append_local_prediction_commands(
        const LocalPredictionHistoryState&,
        std::span<const PredictedCommandAppend>);

    PredictedCommandEntry(
        std::shared_ptr<const goldsrc::GoldSrcUserCmdState> command,
        std::shared_ptr<const movement::LocalPlayerMovementState>
            pre_command_state,
        std::shared_ptr<const movement::LocalPlayerMovementState>
            post_command_state,
        movement::PlayerMovementStatistics simulation_statistics,
        PredictionTouchSummary touch_summary,
        std::uint64_t prediction_generation,
        std::uint64_t entry_ordinal) noexcept;

    std::shared_ptr<const goldsrc::GoldSrcUserCmdState> command_;
    std::shared_ptr<const movement::LocalPlayerMovementState> pre_command_state_;
    std::shared_ptr<const movement::LocalPlayerMovementState> post_command_state_;
    std::uint64_t pre_state_signature_{0U};
    std::uint64_t post_state_signature_{0U};
    movement::PlayerMovementStatistics simulation_statistics_{};
    PredictionTouchSummary touch_summary_{};
    std::uint64_t prediction_generation_{0U};
    std::uint64_t entry_ordinal_{0U};
};

struct PredictedCommandAppend {
    std::shared_ptr<const goldsrc::GoldSrcUserCmdState> command;
    std::shared_ptr<const movement::LocalPlayerMovementState> pre_command_state;
    std::shared_ptr<const movement::LocalPlayerMovementState> post_command_state;
    movement::PlayerMovementStatistics simulation_statistics{};
    PredictionTouchSummary touch_summary{};
};

struct LocalPredictionHistoryStatistics {
    std::uint64_t total_appended_commands{0U};
    std::uint64_t total_acknowledged_commands{0U};
    std::uint64_t total_replayed_commands{0U};
    std::uint64_t publication_count{0U};
    std::size_t high_water_mark{0U};
};

class LocalPredictionHistoryState final {
public:
    struct CreateResult;

    LocalPredictionHistoryState(const LocalPredictionHistoryState&) = default;
    LocalPredictionHistoryState(LocalPredictionHistoryState&&) noexcept = default;
    LocalPredictionHistoryState& operator=(
        const LocalPredictionHistoryState&) = delete;
    LocalPredictionHistoryState& operator=(LocalPredictionHistoryState&&) = delete;
    ~LocalPredictionHistoryState() = default;

    [[nodiscard]] static CreateResult create_initial(
        movement::LocalPlayerMovementState initial_state,
        PredictionSessionIdentity session,
        const LocalPredictionHistoryLimits& limits = {});

    [[nodiscard]] const PredictionHistoryAnchor& anchor() const noexcept;
    [[nodiscard]] std::span<const PredictedCommandEntry> entries()
        const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const PredictedCommandEntry* find_exact(
        goldsrc::GoldSrcUserCmdSequence sequence) const noexcept;
    [[nodiscard]] const std::shared_ptr<
        const movement::LocalPlayerMovementState>&
    current_predicted_state() const noexcept;
    [[nodiscard]] std::optional<goldsrc::GoldSrcUserCmdSequence>
    oldest_command_sequence() const noexcept;
    [[nodiscard]] std::optional<goldsrc::GoldSrcUserCmdSequence>
    newest_command_sequence() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] std::size_t accounted_state_bytes() const noexcept;
    [[nodiscard]] std::size_t accounted_command_bytes() const noexcept;
    [[nodiscard]] std::size_t accounted_touch_summary_bytes() const noexcept;
    [[nodiscard]] const LocalPredictionHistoryLimits& limits() const noexcept;
    [[nodiscard]] const PredictionSessionIdentity& session() const noexcept;
    [[nodiscard]] const LocalPredictionHistoryStatistics& statistics()
        const noexcept;

private:
    friend class LocalPlayerPredictionReconciler;
    friend LocalPredictionAppendResult append_local_prediction_commands(
        const LocalPredictionHistoryState&,
        std::span<const PredictedCommandAppend>);

    LocalPredictionHistoryState(
        PredictionHistoryAnchor anchor,
        std::vector<PredictedCommandEntry> entries,
        std::shared_ptr<const movement::LocalPlayerMovementState> current_state,
        std::uint64_t revision,
        std::size_t accounted_state_bytes,
        std::size_t accounted_command_bytes,
        std::size_t accounted_touch_summary_bytes,
        LocalPredictionHistoryLimits limits,
        LocalPredictionHistoryStatistics statistics) noexcept;

    PredictionHistoryAnchor anchor_;
    std::vector<PredictedCommandEntry> entries_;
    std::shared_ptr<const movement::LocalPlayerMovementState> current_state_;
    std::uint64_t revision_{0U};
    std::size_t accounted_state_bytes_{0U};
    std::size_t accounted_command_bytes_{0U};
    std::size_t accounted_touch_summary_bytes_{0U};
    LocalPredictionHistoryLimits limits_{};
    LocalPredictionHistoryStatistics statistics_{};
};

struct LocalPredictionHistoryState::CreateResult {
    std::shared_ptr<const LocalPredictionHistoryState> history;
    std::optional<PredictionError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return history != nullptr && !error.has_value();
    }
};

struct LocalPredictionAppendResult {
    std::shared_ptr<const LocalPredictionHistoryState> history;
    std::shared_ptr<const movement::LocalPlayerMovementState>
        final_predicted_state;
    std::size_t appended_command_count{0U};
    std::size_t history_size{0U};
    std::uint64_t prediction_revision{0U};
    std::optional<PredictionError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return history != nullptr && final_predicted_state != nullptr &&
            !error.has_value();
    }
};

[[nodiscard]] LocalPredictionAppendResult append_local_prediction_commands(
    const LocalPredictionHistoryState& history,
    std::span<const PredictedCommandAppend> commands);

[[nodiscard]] PredictionTouchSummary summarize_prediction_touches(
    std::span<const movement::PlayerMovementTouch> touches,
    bool start_solid,
    bool all_solid) noexcept;

[[nodiscard]] std::uint64_t local_prediction_history_signature(
    const LocalPredictionHistoryState& history) noexcept;

} // namespace hlclient::prediction
