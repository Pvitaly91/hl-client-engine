#include <hlclient/goldsrc/usercmd_packet_planner.hpp>

#include <algorithm>
#include <limits>
#include <bit>

namespace hlclient::goldsrc {

class GoldSrcUserCmdPacketPlannerIdentity final {};

namespace {

[[nodiscard]] GoldSrcUserCmdPacketPlanResult prepare_failure(
    const GoldSrcUserCmdPacketPlannerErrorCode code,
    const std::string_view context,
    const std::optional<GoldSrcClientMoveErrorCode> move_code = std::nullopt,
    const std::optional<GoldSrcUserCmdHistoryErrorCode> history_code =
        std::nullopt)
{
    return {std::nullopt,
            GoldSrcUserCmdPacketPlannerError{
                code, move_code, history_code, context}};
}

[[nodiscard]] GoldSrcUserCmdPacketPlannerOperationResult operation_failure(
    const GoldSrcUserCmdPacketPlannerErrorCode code,
    const std::string_view context,
    const std::optional<GoldSrcUserCmdHistoryErrorCode> history_code =
        std::nullopt) noexcept
{
    return {GoldSrcUserCmdPacketPlannerError{
        code, std::nullopt, history_code, context}};
}

} // namespace

bool valid_goldsrc_usercmd_packet_planner_config(
    const GoldSrcUserCmdPacketPlannerConfig& config) noexcept
{
    const bool reference = config.profile == GoldSrcUserCmdPacketPlannerProfile::reference_backup_v1;
    return (reference || config.profile ==
               GoldSrcUserCmdPacketPlannerProfile::synthetic_backup_v1) &&
           config.desired_backup_commands <= config.maximum_backup_commands &&
           config.maximum_backup_commands <= (reference ? 61U : 15U) &&
           config.maximum_new_commands > 0U &&
           config.maximum_new_commands <= (reference ? 62U : 15U) &&
           config.maximum_commands_per_packet > 0U &&
           config.maximum_commands_per_packet <= (reference ? 62U : 32U) &&
           config.maximum_backup_commands + config.maximum_new_commands <=
               config.maximum_commands_per_packet &&
           config.maximum_packet_bytes >= 4U &&
           config.maximum_packet_bytes <=
               kGoldSrcUserCmdHardLimits.maximum_encoded_bytes &&
           config.maximum_packet_bits >= 32U &&
           config.maximum_packet_bits <=
               kGoldSrcUserCmdHardLimits.maximum_encoded_bits &&
           config.maximum_packet_bits <= config.maximum_packet_bytes * 8U;
}

GoldSrcUserCmdPacketPlan::GoldSrcUserCmdPacketPlan(
    std::vector<GoldSrcUserCmdSequence> ordered_sequences,
    std::vector<std::shared_ptr<const GoldSrcUserCmdState>> ordered_commands,
    const std::size_t backup_command_count,
    const std::size_t new_command_count,
    const std::uint64_t history_revision,
    const std::uint64_t planner_revision,
    const std::uint64_t plan_identity,
    const std::uint32_t outgoing_netchan_sequence,
    GoldSrcClientMoveMessage encoded_message,
    std::shared_ptr<const GoldSrcUserCmdPacketPlannerIdentity> owner) noexcept
    : ordered_sequences_{std::move(ordered_sequences)},
      ordered_commands_{std::move(ordered_commands)},
      backup_command_count_{backup_command_count},
      new_command_count_{new_command_count},
      history_revision_{history_revision},
      planner_revision_{planner_revision},
      plan_identity_{plan_identity},
      outgoing_netchan_sequence_{outgoing_netchan_sequence},
      encoded_message_{std::move(encoded_message)},
      owner_{std::move(owner)}
{
}

const std::vector<GoldSrcUserCmdSequence>&
GoldSrcUserCmdPacketPlan::ordered_sequences() const noexcept
{
    return ordered_sequences_;
}
const std::vector<std::shared_ptr<const GoldSrcUserCmdState>>&
GoldSrcUserCmdPacketPlan::ordered_commands() const noexcept
{
    return ordered_commands_;
}
std::size_t GoldSrcUserCmdPacketPlan::backup_command_count() const noexcept
{
    return backup_command_count_;
}
std::size_t GoldSrcUserCmdPacketPlan::new_command_count() const noexcept
{
    return new_command_count_;
}
std::uint64_t GoldSrcUserCmdPacketPlan::history_revision() const noexcept
{
    return history_revision_;
}
std::uint64_t GoldSrcUserCmdPacketPlan::planner_revision() const noexcept
{
    return planner_revision_;
}
std::uint64_t GoldSrcUserCmdPacketPlan::plan_identity() const noexcept
{
    return plan_identity_;
}
std::uint32_t GoldSrcUserCmdPacketPlan::outgoing_netchan_sequence() const noexcept
{
    return outgoing_netchan_sequence_;
}
std::size_t GoldSrcUserCmdPacketPlan::expected_encoded_bits() const noexcept
{
    return reference_message_ ? reference_message_->bytes.size() * 8 : encoded_message_ ? encoded_message_->bit_length() : 0U;
}
std::size_t GoldSrcUserCmdPacketPlan::expected_encoded_bytes() const noexcept
{
    return encoded_bytes().size();
}
std::span<const std::byte> GoldSrcUserCmdPacketPlan::encoded_bytes() const noexcept {
    return reference_message_ ? std::span<const std::byte>{reference_message_->bytes}
        : encoded_message_ ? encoded_message_->bytes() : std::span<const std::byte>{};
}
std::size_t GoldSrcUserCmdPacketPlan::changed_field_count() const noexcept {
    if (!reference_message_) return encoded_message_ ? encoded_message_->changed_field_count() : 0;
    std::size_t count = 0;
    for (const auto& c : reference_message_->commands) count += std::popcount(c.field_mask);
    return count;
}
GoldSrcUserCmdPacketPlan::GoldSrcUserCmdPacketPlan(
    std::vector<GoldSrcUserCmdSequence> sequences, std::uint64_t history_revision,
    std::uint64_t planner_revision, std::uint64_t identity, ReferenceClientMoveMessage message,
    std::shared_ptr<const GoldSrcUserCmdPacketPlannerIdentity> owner) noexcept
    : ordered_sequences_{std::move(sequences)}, backup_command_count_{message.backup_count},
      new_command_count_{message.new_count}, history_revision_{history_revision},
      planner_revision_{planner_revision}, plan_identity_{identity},
      outgoing_netchan_sequence_{message.source.sequence}, owner_{std::move(owner)},
      reference_message_{std::move(message)} {}
const GoldSrcClientMoveMessage&
GoldSrcUserCmdPacketPlan::encoded_message() const noexcept
{
    return *encoded_message_;
}

GoldSrcUserCmdPacketPlanner::GoldSrcUserCmdPacketPlanner(
    GoldSrcUserCmdPacketPlannerConfig config)
    : config_{config},
      valid_configuration_{valid_goldsrc_usercmd_packet_planner_config(config_)},
      identity_{std::make_shared<const GoldSrcUserCmdPacketPlannerIdentity>()}
{
}

bool GoldSrcUserCmdPacketPlanner::valid_configuration() const noexcept
{
    return valid_configuration_;
}

GoldSrcUserCmdPacketPlanResult GoldSrcUserCmdPacketPlanner::prepare(
    const GoldSrcUserCmdHistoryState& history,
    const GoldSrcUserCmdSchemaBinding& binding,
    const std::uint32_t outgoing_netchan_sequence)
{
    if (config_.profile == GoldSrcUserCmdPacketPlannerProfile::reference_backup_v1)
        return prepare_failure(GoldSrcUserCmdPacketPlannerErrorCode::invalid_configuration,
            "Reference planning requires a retained driver context");
    return prepare_bounded(history, binding, outgoing_netchan_sequence, config_.maximum_packet_bytes);
}

GoldSrcUserCmdPacketPlanResult GoldSrcUserCmdPacketPlanner::prepare(
    const GoldSrcUserCmdHistoryState& history, const GoldSrcUserCmdSchemaBinding& binding,
    const NetchanOutgoingContextPlan& context)
{
    return prepare_bounded(history, binding, context.next_outgoing_sequence().value(),
        context.maximum_unreliable_payload_size());
}

GoldSrcUserCmdPacketPlanResult GoldSrcUserCmdPacketPlanner::prepare_bounded(
    const GoldSrcUserCmdHistoryState& history, const GoldSrcUserCmdSchemaBinding& binding,
    std::uint32_t outgoing_netchan_sequence, std::size_t capacity)
{
    if (!valid_configuration_) {
        return prepare_failure(
            config_.profile ==
                    GoldSrcUserCmdPacketPlannerProfile::
                        stock_protocol_48_evidence_pending
                ? GoldSrcUserCmdPacketPlannerErrorCode::stock_evidence_pending
                : GoldSrcUserCmdPacketPlannerErrorCode::invalid_configuration,
            "Only the bounded synthetic usercmd packet planner is executable");
    }
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return prepare_failure(
            GoldSrcUserCmdPacketPlannerErrorCode::revision_overflow,
            "Usercmd packet planner revision is exhausted");
    }
    if (next_plan_identity_ == 0U ||
        next_plan_identity_ == std::numeric_limits<std::uint64_t>::max()) {
        return prepare_failure(
            GoldSrcUserCmdPacketPlannerErrorCode::identity_overflow,
            "Usercmd packet plan identity domain is exhausted");
    }
    const auto& entries = history.entries();
    const bool reference = config_.profile == GoldSrcUserCmdPacketPlannerProfile::reference_backup_v1;
    if (reference != (history.profile() == GoldSrcUserCmdHistoryProfile::reference_wire_v1))
        return prepare_failure(GoldSrcUserCmdPacketPlannerErrorCode::invalid_configuration,
            "History and planner profiles differ");
    const auto first_unsent = std::ranges::find_if(entries, [](const auto& entry) {
        return entry.has_command() && entry.new_transmission_count == 0U;
    });
    if (first_unsent == entries.end()) {
        return prepare_failure(
            GoldSrcUserCmdPacketPlannerErrorCode::no_new_commands,
            "Usercmd packet planning requires at least one unsent command");
    }
    const auto first_unsent_index = static_cast<std::size_t>(
        std::distance(entries.begin(), first_unsent));
    const auto available_new = entries.size() - first_unsent_index;
    const auto new_count = std::min(
        available_new, config_.maximum_new_commands);

    std::vector<std::size_t> backup_indexes;
    backup_indexes.reserve(config_.desired_backup_commands);
    auto candidate = first_unsent_index;
    while (candidate > 0U &&
           backup_indexes.size() < config_.desired_backup_commands) {
        --candidate;
        if (entries[candidate].has_command() &&
            entries[candidate].new_transmission_count != 0U) {
            backup_indexes.push_back(candidate);
        }
    }
    std::ranges::reverse(backup_indexes);
    const auto backup_count = backup_indexes.size();
    if (backup_count + new_count > config_.maximum_commands_per_packet) {
        return prepare_failure(
            GoldSrcUserCmdPacketPlannerErrorCode::command_limit_exceeded,
            "Selected backup/new split exceeds the packet command bound");
    }

    if (reference) {
        ReferenceMoveLimits limits;
        limits.maximum_commands = config_.maximum_commands_per_packet;
        limits.maximum_message_bytes = std::min({capacity, config_.maximum_packet_bytes, config_.maximum_packet_bits / 8});
        if (limits.maximum_message_bytes < 7)
            return prepare_failure(GoldSrcUserCmdPacketPlannerErrorCode::packet_budget_exceeded, "No minimum move fits suffix capacity");
        const GoldSrcReferenceClientMoveCodec codec{binding, history.generation(), limits};
        if (!codec.valid_configuration())
            return prepare_failure(GoldSrcUserCmdPacketPlannerErrorCode::invalid_configuration, "Reference codec binding or limits invalid");
        auto backups = backup_count;
        auto news = new_count;
        while (news != 0) {
            std::vector<GoldSrcWireUserCmd> values;
            std::vector<GoldSrcUserCmdSequence> ids;
            values.reserve(backups + news); ids.reserve(backups + news);
            const auto append = [&](std::size_t index) {
                const auto& e = entries[index];
                if (!e.reference_command || e.command) return false;
                values.push_back(*e.reference_command); ids.push_back(e.sequence()); return true;
            };
            for (std::size_t b = backup_count - backups; b < backup_count; ++b)
                if (!append(backup_indexes[b])) return prepare_failure(GoldSrcUserCmdPacketPlannerErrorCode::invalid_configuration, "Non-reference history entry");
            for (std::size_t n = 0; n < news; ++n)
                if (!append(first_unsent_index + n)) return prepare_failure(GoldSrcUserCmdPacketPlannerErrorCode::invalid_configuration, "Non-reference history entry");
            ReferenceMoveSource source; source.generation = history.generation(); source.sequence = outgoing_netchan_sequence;
            auto encoded = codec.encode(values, backups, 0, source);
            if (encoded) {
                const auto checked = history.preflight_submission(history.revision(), ids, backups);
                if (!checked) return prepare_failure(GoldSrcUserCmdPacketPlannerErrorCode::history_commit_failed, "Reference history preflight failed", {}, checked.error->code);
                GoldSrcUserCmdPacketPlan plan{std::move(ids), history.revision(), revision_, next_plan_identity_++, std::move(*encoded.message), identity_};
                plan.history_owner_ = history.owner_;
                return {std::move(plan), {}};
            }
            if (encoded.error->code != ReferenceMoveErrorCode::byte_limit)
                return prepare_failure(GoldSrcUserCmdPacketPlannerErrorCode::encode_failed, "Reference encode failed");
            if (backups != 0) --backups; else --news;
        }
        return prepare_failure(GoldSrcUserCmdPacketPlannerErrorCode::packet_budget_exceeded, "Oldest unsent command cannot fit suffix capacity");
    }

    std::vector<std::shared_ptr<const GoldSrcUserCmdState>> commands;
    commands.reserve(backup_count + new_count);
    for (const auto index : backup_indexes) {
        commands.push_back(entries[index].command);
    }
    for (std::size_t index = 0U; index < new_count; ++index) {
        commands.push_back(entries[first_unsent_index + index].command);
    }

    std::vector<GoldSrcUserCmdSequence> sequences;
    sequences.reserve(commands.size());
    for (const auto& command : commands) {
        sequences.push_back(command->command_sequence());
    }
    const auto history_preflight = history.preflight_submission(
        history.revision(), sequences, backup_count);
    if (!history_preflight) {
        return prepare_failure(
            GoldSrcUserCmdPacketPlannerErrorCode::history_commit_failed,
            "Usercmd history would reject this packet submission",
            std::nullopt,
            history_preflight.error
                ? std::optional{history_preflight.error->code}
                : std::nullopt);
    }

    auto limits = GoldSrcUserCmdLimits{};
    limits.maximum_backup_commands = config_.maximum_backup_commands;
    limits.maximum_new_commands = config_.maximum_new_commands;
    limits.maximum_commands_per_packet = config_.maximum_commands_per_packet;
    limits.maximum_encoded_bytes = config_.maximum_packet_bytes;
    limits.maximum_encoded_bits = config_.maximum_packet_bits;
    const auto encoded = GoldSrcClientMoveMessageCodec{
        limits,
        GoldSrcClientMoveCompatibilityProfile::synthetic_client_move_v1}
                             .encode(
                                 commands,
                                 binding,
                                 GoldSrcClientMoveEncodeContext{
                                     outgoing_netchan_sequence,
                                     0U,
                                     backup_count,
                                     new_count});
    if (!encoded || !encoded.message) {
        return prepare_failure(
            encoded.error &&
                    encoded.error->code ==
                        GoldSrcClientMoveErrorCode::packet_size_exceeded
                ? GoldSrcUserCmdPacketPlannerErrorCode::packet_budget_exceeded
                : GoldSrcUserCmdPacketPlannerErrorCode::encode_failed,
            "Selected usercmd packet plan could not be encoded transactionally",
            encoded.error ? std::optional{encoded.error->code} : std::nullopt);
    }

    const auto identity = next_plan_identity_++;
    return {GoldSrcUserCmdPacketPlan{
                std::move(sequences),
                std::move(commands),
                backup_count,
                new_count,
                history.revision(),
                revision_,
                identity,
                outgoing_netchan_sequence,
                std::move(*encoded.message),
                identity_},
            std::nullopt};
}

GoldSrcUserCmdPacketPlannerOperationResult
GoldSrcUserCmdPacketPlanner::validate(GoldSrcUserCmdPacketPlan& plan) const noexcept
{
    if (!plan.consumable_) {
        return operation_failure(
            GoldSrcUserCmdPacketPlannerErrorCode::consumed_plan,
            "Usercmd packet plan was already consumed");
    }
    if (plan.owner_ != identity_) {
        return operation_failure(
            GoldSrcUserCmdPacketPlannerErrorCode::foreign_plan,
            "Usercmd packet plan belongs to another planner");
    }
    plan.consumable_ = false;
    if (plan.planner_revision_ != revision_) {
        return operation_failure(
            GoldSrcUserCmdPacketPlannerErrorCode::stale_plan,
            "Usercmd packet plan no longer matches planner revision");
    }
    return {};
}

GoldSrcUserCmdPacketPlannerOperationResult GoldSrcUserCmdPacketPlanner::commit(
    GoldSrcUserCmdHistoryBuilder& history,
    GoldSrcUserCmdPacketPlan&& plan) noexcept
{
    const auto checked = preflight(history, plan);
    if (!checked) {
        if (plan.owner_ == identity_ && checked.error->code != GoldSrcUserCmdPacketPlannerErrorCode::foreign_plan)
            plan.consumable_ = false;
        return checked;
    }
    const auto validated = validate(plan);
    if (!validated) {
        return validated;
    }
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return operation_failure(
            GoldSrcUserCmdPacketPlannerErrorCode::revision_overflow,
            "Usercmd packet planner revision is exhausted");
    }
    const auto committed = history.commit_submission(
        plan.history_revision_,
        plan.ordered_sequences_,
        plan.backup_command_count_,
        plan.outgoing_netchan_sequence_);
    if (!committed) {
        return operation_failure(
            committed.error &&
                    committed.error->code ==
                        GoldSrcUserCmdHistoryErrorCode::stale_submission
                ? GoldSrcUserCmdPacketPlannerErrorCode::stale_plan
                : GoldSrcUserCmdPacketPlannerErrorCode::history_commit_failed,
            "Usercmd history rejected a prepared packet submission",
            committed.error ? std::optional{committed.error->code}
                            : std::nullopt);
    }
    ++revision_;
    return {};
}

GoldSrcUserCmdPacketPlannerOperationResult GoldSrcUserCmdPacketPlanner::preflight(
    const GoldSrcUserCmdHistoryBuilder& history, const GoldSrcUserCmdPacketPlan& plan) const noexcept
{
    if (!plan.consumable_) return operation_failure(GoldSrcUserCmdPacketPlannerErrorCode::consumed_plan, "Plan consumed");
    if (plan.owner_ != identity_ || (plan.history_owner_ && plan.history_owner_ != history.owner_))
        return operation_failure(GoldSrcUserCmdPacketPlannerErrorCode::foreign_plan, "Foreign planner/history owner");
    if (plan.planner_revision_ != revision_) return operation_failure(GoldSrcUserCmdPacketPlannerErrorCode::stale_plan, "Planner revision changed");
    if (revision_ == UINT64_MAX) return operation_failure(GoldSrcUserCmdPacketPlannerErrorCode::revision_overflow, "Planner revision exhausted");
    const auto checked = history.preflight_submission(plan.history_revision_, plan.ordered_sequences_, plan.backup_command_count_);
    if (!checked) return operation_failure(checked.error->code == GoldSrcUserCmdHistoryErrorCode::stale_submission ? GoldSrcUserCmdPacketPlannerErrorCode::stale_plan : GoldSrcUserCmdPacketPlannerErrorCode::history_commit_failed, "History preflight failed", checked.error->code);
    return {};
}

GoldSrcUserCmdPacketPlannerOperationResult GoldSrcUserCmdPacketPlanner::abandon(
    GoldSrcUserCmdPacketPlan&& plan) const noexcept
{
    return validate(plan);
}

} // namespace hlclient::goldsrc
