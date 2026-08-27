#pragma once

#include <hlclient/goldsrc/usercmd_transmission_stage.hpp>

#include <cstdint>

namespace hlclient::goldsrc::detail {

// A narrow exhaustion-state seam for transaction-boundary tests. Production
// code cannot mutate these monotonic counters directly.
class GoldSrcUserCmdTransactionalTestAccess final {
public:
    static void set_history_revision(
        GoldSrcUserCmdHistoryBuilder& history,
        const std::uint64_t revision) noexcept
    {
        history.revision_ = revision;
    }

    [[nodiscard]] static bool set_transmission_counts(
        GoldSrcUserCmdHistoryBuilder& history,
        const GoldSrcUserCmdSequence sequence,
        const std::uint32_t new_count,
        const std::uint32_t backup_count) noexcept
    {
        for (auto& entry : history.entries_) {
            if (entry.command &&
                entry.command->command_sequence() == sequence) {
                entry.new_transmission_count = new_count;
                entry.backup_transmission_count = backup_count;
                return true;
            }
        }
        return false;
    }

    static void set_planner_revision(
        GoldSrcUserCmdPacketPlanner& planner,
        const std::uint64_t revision) noexcept
    {
        planner.revision_ = revision;
    }

    [[nodiscard]] static GoldSrcUserCmdHistoryBuilder& history(
        GoldSrcUserCmdTransmissionStage& stage) noexcept
    {
        return stage.history_;
    }

    [[nodiscard]] static GoldSrcUserCmdPacketPlanner& planner(
        GoldSrcUserCmdTransmissionStage& stage) noexcept
    {
        return stage.planner_;
    }

    [[nodiscard]] static std::optional<std::uint8_t> pending_impulse(
        const GoldSrcUserCmdTransmissionStage& stage) noexcept
    {
        return stage.pending_impulse_;
    }
};

} // namespace hlclient::goldsrc::detail
