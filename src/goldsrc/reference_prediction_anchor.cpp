#include <hlclient/goldsrc/reference_prediction_anchor.hpp>

#include <hlclient/goldsrc/netchan_sequence.hpp>

#include <algorithm>
#include <new>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] std::optional<NetchanSequence> sequence(
    const std::uint32_t value) noexcept {
    return NetchanSequence::from_numeric(value);
}

} // namespace

ReferencePredictionAnchorStatus ReferencePredictionCarrierLedger::record_sent(
    const GoldSrcUserCmdTransmissionEvent& event,
    const GoldSrcUserCmdHistoryState& history) {
    if (maximum_carriers_ == 0U || maximum_carriers_ > 256U ||
        history.profile() != GoldSrcUserCmdHistoryProfile::reference_wire_v1 ||
        history.generation() == 0U ||
        event.type != GoldSrcUserCmdTransmissionEventType::move_packet_submitted ||
        !event.outgoing_netchan_sequence ||
        !sequence(*event.outgoing_netchan_sequence) ||
        !event.first_new_command_sequence || !event.last_new_command_sequence ||
        event.new_command_count == 0U || event.new_command_count > 62U ||
        event.backup_command_count > 62U ||
        event.new_command_count + event.backup_command_count > 62U ||
        *event.first_new_command_sequence > *event.last_new_command_sequence ||
        static_cast<std::uint64_t>(*event.last_new_command_sequence) -
            *event.first_new_command_sequence + 1U != event.new_command_count) {
        return ReferencePredictionAnchorStatus::invalid_receipt;
    }
    if (generation_ && *generation_ != history.generation())
        return ReferencePredictionAnchorStatus::generation_mismatch;
    if (last_sent_sequence_) {
        const auto order = compare_sequences(
            *sequence(*event.outgoing_netchan_sequence),
            *sequence(*last_sent_sequence_));
        if (order == NetchanSequenceComparison::half_range_ambiguous)
            return ReferencePredictionAnchorStatus::ambiguous_sequence;
        if (order != NetchanSequenceComparison::newer)
            return ReferencePredictionAnchorStatus::duplicate_or_old_receipt;
    }

    ReferenceSentCarrier candidate;
    candidate.generation = history.generation();
    candidate.outgoing_sequence = *event.outgoing_netchan_sequence;
    candidate.history_revision = history.revision();
    candidate.backup_count = event.backup_command_count;
    candidate.new_count = event.new_command_count;
    try {
        candidate.commands.reserve(candidate.backup_count + candidate.new_count);
        for (const auto& entry : history.entries()) {
            if (entry.last_packet_sequence != candidate.outgoing_sequence)
                continue;
            if (!entry.reference_command || !entry.sequence().valid())
                return ReferencePredictionAnchorStatus::history_missing;
            const auto id = entry.sequence().value();
            const bool is_new = id >= *event.first_new_command_sequence &&
                id <= *event.last_new_command_sequence;
            candidate.commands.push_back(ReferenceSentCommand{
                entry.sequence(), *entry.reference_command, !is_new});
        }
    } catch (const std::bad_alloc&) {
        return ReferencePredictionAnchorStatus::history_missing;
    }
    if (candidate.commands.size() != candidate.backup_count + candidate.new_count)
        return ReferencePredictionAnchorStatus::history_missing;
    std::size_t backups = 0U, new_commands = 0U;
    std::uint32_t expected_new = *event.first_new_command_sequence;
    bool seen_new = false;
    for (const auto& command : candidate.commands) {
        if (command.backup) {
            if (seen_new) return ReferencePredictionAnchorStatus::invalid_receipt;
            ++backups;
        } else {
            seen_new = true;
            if (command.identity.value() != expected_new)
                return ReferencePredictionAnchorStatus::history_missing;
            ++new_commands;
            ++expected_new;
        }
    }
    if (backups != candidate.backup_count || new_commands != candidate.new_count)
        return ReferencePredictionAnchorStatus::history_missing;
    try {
        if (carriers_.size() == maximum_carriers_) carriers_.erase(carriers_.begin());
        carriers_.push_back(std::move(candidate));
    } catch (const std::bad_alloc&) {
        return ReferencePredictionAnchorStatus::history_missing;
    }
    last_sent_sequence_ = *event.outgoing_netchan_sequence;
    generation_ = history.generation();
    return ReferencePredictionAnchorStatus::bound;
}

ReferencePredictionAnchorResult ReferencePredictionCarrierLedger::bind_clientdata(
    const ReferenceClientdataCarrier& record) noexcept {
    if (record.generation == 0U || record.record_identity == 0U ||
        !record.fresh_clientdata || !record.acknowledgement ||
        !sequence(record.source_sequence) || !sequence(*record.acknowledgement))
        return {ReferencePredictionAnchorStatus::invalid_record};
    if (!generation_ || record.generation != *generation_)
        return {ReferencePredictionAnchorStatus::generation_mismatch};
    if (last_seen_server_sequence_) {
        const auto order = compare_sequences(*sequence(record.source_sequence),
            *sequence(*last_seen_server_sequence_));
        if (order == NetchanSequenceComparison::half_range_ambiguous)
            return {ReferencePredictionAnchorStatus::ambiguous_sequence};
        if (order != NetchanSequenceComparison::newer ||
            record.record_identity == last_seen_record_identity_)
            return {ReferencePredictionAnchorStatus::stale_or_duplicate_record};
    }
    last_seen_server_sequence_ = record.source_sequence;
    last_seen_record_identity_ = record.record_identity;
    if (record.source_reliable || record.reassembled)
        return {ReferencePredictionAnchorStatus::old_body_or_reassembly};
    const auto found = std::find_if(carriers_.begin(), carriers_.end(),
        [&record](const auto& carrier) {
            return carrier.generation == record.generation &&
                carrier.outgoing_sequence == *record.acknowledgement;
        });
    if (found == carriers_.end())
        return {ReferencePredictionAnchorStatus::sent_carrier_missing};
    if (found->new_count == 0U || found->commands.empty() ||
        found->commands.back().backup)
        return {ReferencePredictionAnchorStatus::carrier_without_new_move};
    return {ReferencePredictionAnchorStatus::bound,
        found->commands.back().identity, found->commands.back().value,
        found->outgoing_sequence,
        found->history_revision, record.generation,
        record.record_identity, record.source_sequence};
}

} // namespace hlclient::goldsrc
