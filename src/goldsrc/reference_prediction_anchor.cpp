#include <hlclient/goldsrc/reference_prediction_anchor.hpp>

#include <hlclient/goldsrc/netchan_sequence.hpp>
#include <hlclient/client/runtime_observation.hpp>

#include <algorithm>
#include <cmath>
#include <new>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] std::optional<NetchanSequence> sequence(
    const std::uint32_t value) noexcept {
    return NetchanSequence::from_numeric(value);
}

// Unfragmented packets do not encode a reliable-prefix length. This is an
// explicit current-state profile, NOT inferred byte membership. Require the
// fully committed decoder's ordered time/clientdata/entity observations; a
// newer time after retained/old clientdata cannot turn it into a current frame.
[[nodiscard]] std::optional<double> coherent_frame_time(
    const client::RuntimeClientObservationState& observation,
    const std::uint8_t slot) noexcept {
    using client::RuntimeObservationFreshness;
    using client::RuntimeObservationCompleteness;
    if (!observation.generation || !observation.publication_revision ||
        !observation.receiving_client || !observation.server_time_seconds ||
        !std::isfinite(*observation.server_time_seconds) ||
        *observation.server_time_seconds < 0.0)
        return std::nullopt;
    for (const auto* metadata : {&observation.server_time_metadata,
             &observation.client_metadata, &observation.entity_metadata}) {
        if (metadata->generation != observation.generation ||
            metadata->freshness != RuntimeObservationFreshness::observed_in_record ||
            metadata->completeness != RuntimeObservationCompleteness::complete_reconstruction ||
            !metadata->source || !metadata->source->record_identity ||
            metadata->source->start_bit_offset >= metadata->source->end_bit_offset)
            return std::nullopt;
    }
    const auto& time = *observation.server_time_metadata.source;
    const auto& data = *observation.client_metadata.source;
    const auto& entities = *observation.entity_metadata.source;
    const auto same_carrier = [&data](const client::RuntimeObservationSource& source) {
        return source.record_identity == data.record_identity &&
            source.record_ordinal == data.record_ordinal &&
            source.source_transport_sequence == data.source_transport_sequence &&
            source.carrier_acknowledgement == data.carrier_acknowledgement &&
            source.source_reliable == data.source_reliable &&
            source.reassembled == data.reassembled &&
            source.contemporaneous_fragment_suffix == data.contemporaneous_fragment_suffix;
    };
    if (!same_carrier(time) || !same_carrier(entities) || data.reassembled ||
        !data.carrier_acknowledgement ||
        time.end_bit_offset > data.start_bit_offset ||
        data.end_bit_offset > entities.start_bit_offset)
        return std::nullopt;
    const auto player_number = static_cast<std::uint32_t>(slot) + 1U;
    const auto player = std::find_if(observation.packet_entities.begin(),
        observation.packet_entities.end(), [player_number](const auto& entity) {
            return entity.entity_number == player_number && entity.player_movement_schema;
        });
    if (player == observation.packet_entities.end()) return std::nullopt;
    return observation.server_time_seconds;
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
    return bind_clientdata_impl(record, false);
}

ReferencePredictionAnchorResult ReferencePredictionCarrierLedger::bind_observation(
    const client::RuntimeClientObservationState& observation,
    const std::uint8_t receiving_client_slot,
    const bool coherent_unfragmented_movement_frames) noexcept {
    if (!observation.client_metadata.source ||
        observation.client_metadata.generation != observation.generation)
        return {ReferencePredictionAnchorStatus::invalid_record};
    const auto& source = *observation.client_metadata.source;
    const ReferenceClientdataCarrier record{
        observation.generation, source.record_identity,
        source.source_transport_sequence, source.carrier_acknowledgement,
        observation.client_metadata.freshness == client::RuntimeObservationFreshness::observed_in_record,
        source.source_reliable, source.reassembled, source.contemporaneous_fragment_suffix};
    if (!generation_ || record.generation != *generation_)
        return {ReferencePredictionAnchorStatus::generation_mismatch};
    const auto frame_time = coherent_frame_time(observation, receiving_client_slot);
    const bool compatible = coherent_unfragmented_movement_frames && record.source_reliable &&
        !record.reassembled && !record.contemporaneous_fragment_suffix;
    if (compatible) {
        if (!frame_time) return {ReferencePredictionAnchorStatus::incoherent_movement_frame};
        if (last_bound_server_time_ && *frame_time <= *last_bound_server_time_)
            return {ReferencePredictionAnchorStatus::stale_movement_frame};
    }
    auto result = bind_clientdata_impl(record, compatible);
    if (result.bound() && frame_time)
        last_bound_server_time_ = std::max(last_bound_server_time_.value_or(*frame_time), *frame_time);
    return result;
}

ReferencePredictionAnchorResult ReferencePredictionCarrierLedger::bind_clientdata_impl(
    const ReferenceClientdataCarrier& record, const bool coherent_reliable_frame) noexcept {
    if (record.generation == 0U || record.record_identity == 0U ||
        !record.fresh_clientdata || !record.acknowledgement ||
        !sequence(record.source_sequence) || !sequence(*record.acknowledgement))
        return {ReferencePredictionAnchorStatus::invalid_record};
    if (!generation_ || record.generation != *generation_)
        return {ReferencePredictionAnchorStatus::generation_mismatch};
    // A completed reliable body can be older than its completion header. It
    // cannot reserve the sequence of a distinct contemporary suffix in that
    // same packet. Ordinary reliable-marked frames need the separate explicit
    // coherent-state profile; a newer svc_time alone never establishes it.
    if (record.reassembled ||
        (record.source_reliable && !record.contemporaneous_fragment_suffix && !coherent_reliable_frame))
        return {ReferencePredictionAnchorStatus::old_body_or_reassembly};
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
    const auto command_boundary = found->commands.back().identity.value();
    if (last_bound_command_ && command_boundary < *last_bound_command_)
        return {ReferencePredictionAnchorStatus::stale_command_boundary};
    last_bound_command_ = command_boundary;
    return {ReferencePredictionAnchorStatus::bound,
        found->commands.back().identity, found->commands.back().value,
        found->outgoing_sequence,
        found->history_revision, record.generation,
        record.record_identity, record.source_sequence, coherent_reliable_frame};
}

} // namespace hlclient::goldsrc
