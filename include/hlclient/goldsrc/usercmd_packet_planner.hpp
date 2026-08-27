#pragma once

#include <hlclient/goldsrc/client_move_message.hpp>
#include <hlclient/goldsrc/usercmd_history.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

enum class GoldSrcUserCmdPacketPlannerProfile : std::uint8_t {
    synthetic_backup_v1,
    stock_protocol_48_evidence_pending,
};

struct GoldSrcUserCmdPacketPlannerConfig {
    std::size_t desired_backup_commands{2U};
    std::size_t maximum_backup_commands{7U};
    std::size_t maximum_new_commands{8U};
    std::size_t maximum_commands_per_packet{16U};
    std::size_t maximum_packet_bytes{1'024U};
    std::size_t maximum_packet_bits{8'192U};
    GoldSrcUserCmdPacketPlannerProfile profile{
        GoldSrcUserCmdPacketPlannerProfile::synthetic_backup_v1};
};

[[nodiscard]] bool valid_goldsrc_usercmd_packet_planner_config(
    const GoldSrcUserCmdPacketPlannerConfig& config) noexcept;

class GoldSrcUserCmdPacketPlannerIdentity;

class GoldSrcUserCmdPacketPlan final {
public:
    GoldSrcUserCmdPacketPlan(const GoldSrcUserCmdPacketPlan&) = delete;
    GoldSrcUserCmdPacketPlan& operator=(const GoldSrcUserCmdPacketPlan&) = delete;
    GoldSrcUserCmdPacketPlan(GoldSrcUserCmdPacketPlan&&) noexcept = default;
    GoldSrcUserCmdPacketPlan& operator=(GoldSrcUserCmdPacketPlan&&) noexcept =
        default;
    ~GoldSrcUserCmdPacketPlan() = default;

    [[nodiscard]] const std::vector<GoldSrcUserCmdSequence>&
    ordered_sequences() const noexcept;
    [[nodiscard]] const std::vector<std::shared_ptr<const GoldSrcUserCmdState>>&
    ordered_commands() const noexcept;
    [[nodiscard]] std::size_t backup_command_count() const noexcept;
    [[nodiscard]] std::size_t new_command_count() const noexcept;
    [[nodiscard]] std::uint64_t history_revision() const noexcept;
    [[nodiscard]] std::uint64_t planner_revision() const noexcept;
    [[nodiscard]] std::uint64_t plan_identity() const noexcept;
    [[nodiscard]] std::uint32_t outgoing_netchan_sequence() const noexcept;
    [[nodiscard]] std::size_t expected_encoded_bits() const noexcept;
    [[nodiscard]] std::size_t expected_encoded_bytes() const noexcept;
    [[nodiscard]] const GoldSrcClientMoveMessage& encoded_message() const noexcept;

private:
    friend class GoldSrcUserCmdPacketPlanner;

    GoldSrcUserCmdPacketPlan(
        std::vector<GoldSrcUserCmdSequence> ordered_sequences,
        std::vector<std::shared_ptr<const GoldSrcUserCmdState>> ordered_commands,
        std::size_t backup_command_count,
        std::size_t new_command_count,
        std::uint64_t history_revision,
        std::uint64_t planner_revision,
        std::uint64_t plan_identity,
        std::uint32_t outgoing_netchan_sequence,
        GoldSrcClientMoveMessage encoded_message,
        std::shared_ptr<const GoldSrcUserCmdPacketPlannerIdentity> owner) noexcept;

    std::vector<GoldSrcUserCmdSequence> ordered_sequences_;
    std::vector<std::shared_ptr<const GoldSrcUserCmdState>> ordered_commands_;
    std::size_t backup_command_count_{0U};
    std::size_t new_command_count_{0U};
    std::uint64_t history_revision_{0U};
    std::uint64_t planner_revision_{0U};
    std::uint64_t plan_identity_{0U};
    std::uint32_t outgoing_netchan_sequence_{0U};
    std::optional<GoldSrcClientMoveMessage> encoded_message_;
    std::shared_ptr<const GoldSrcUserCmdPacketPlannerIdentity> owner_;
    bool consumable_{true};
};

enum class GoldSrcUserCmdPacketPlannerErrorCode : std::uint8_t {
    invalid_configuration,
    stock_evidence_pending,
    no_new_commands,
    command_limit_exceeded,
    packet_budget_exceeded,
    encode_failed,
    identity_overflow,
    revision_overflow,
    stale_plan,
    foreign_plan,
    consumed_plan,
    history_commit_failed,
};

struct GoldSrcUserCmdPacketPlannerError {
    GoldSrcUserCmdPacketPlannerErrorCode code{
        GoldSrcUserCmdPacketPlannerErrorCode::invalid_configuration};
    std::optional<GoldSrcClientMoveErrorCode> move_code;
    std::optional<GoldSrcUserCmdHistoryErrorCode> history_code;
    std::string_view context;
};

struct GoldSrcUserCmdPacketPlanResult {
    std::optional<GoldSrcUserCmdPacketPlan> plan;
    std::optional<GoldSrcUserCmdPacketPlannerError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return plan.has_value();
    }
};

struct GoldSrcUserCmdPacketPlannerOperationResult {
    std::optional<GoldSrcUserCmdPacketPlannerError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

class GoldSrcUserCmdPacketPlanner final {
public:
    explicit GoldSrcUserCmdPacketPlanner(
        GoldSrcUserCmdPacketPlannerConfig config = {});
    GoldSrcUserCmdPacketPlanner(const GoldSrcUserCmdPacketPlanner&) = delete;
    GoldSrcUserCmdPacketPlanner& operator=(
        const GoldSrcUserCmdPacketPlanner&) = delete;
    GoldSrcUserCmdPacketPlanner(GoldSrcUserCmdPacketPlanner&&) = delete;
    GoldSrcUserCmdPacketPlanner& operator=(
        GoldSrcUserCmdPacketPlanner&&) = delete;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] GoldSrcUserCmdPacketPlanResult prepare(
        const GoldSrcUserCmdHistoryState& history,
        const GoldSrcUserCmdSchemaBinding& binding,
        std::uint32_t outgoing_netchan_sequence);
    [[nodiscard]] GoldSrcUserCmdPacketPlannerOperationResult commit(
        GoldSrcUserCmdHistoryBuilder& history,
        GoldSrcUserCmdPacketPlan&& plan) noexcept;
    [[nodiscard]] GoldSrcUserCmdPacketPlannerOperationResult abandon(
        GoldSrcUserCmdPacketPlan&& plan) const noexcept;

private:
    friend class detail::GoldSrcUserCmdTransactionalTestAccess;

    [[nodiscard]] GoldSrcUserCmdPacketPlannerOperationResult validate(
        GoldSrcUserCmdPacketPlan& plan) const noexcept;

    GoldSrcUserCmdPacketPlannerConfig config_;
    bool valid_configuration_{false};
    std::shared_ptr<const GoldSrcUserCmdPacketPlannerIdentity> identity_;
    std::uint64_t revision_{0U};
    std::uint64_t next_plan_identity_{1U};
};

} // namespace hlclient::goldsrc
