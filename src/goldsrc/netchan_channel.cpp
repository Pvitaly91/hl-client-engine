#include <hlclient/goldsrc/netchan_channel.hpp>

#include <algorithm>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] NetchanSequence sequence(const std::uint32_t value) noexcept
{
    return *NetchanSequence::from_numeric(value & kNetchanSequenceMask);
}

[[nodiscard]] NetchanSequence next_sequence(const NetchanSequence current) noexcept
{
    return sequence((current.value() + 1U) & kNetchanSequenceMask);
}

[[nodiscard]] NetchanChannelOperationResult operation_failure(
    const NetchanChannelErrorCode code) noexcept
{
    return NetchanChannelOperationResult{NetchanChannelError{code}};
}

[[nodiscard]] NetchanOutgoingPrepareResult prepare_failure(
    const NetchanChannelErrorCode code) noexcept
{
    return NetchanOutgoingPrepareResult{
        std::nullopt,
        NetchanChannelError{code},
    };
}

} // namespace

NetchanInitialState NetchanInitialState::stock_protocol48() noexcept
{
    return NetchanInitialState{
        sequence(1U),
        sequence(0U),
        sequence(0U),
        sequence(0U),
        false,
        false,
        false,
    };
}

NetchanOutgoingTransaction::NetchanOutgoingTransaction(
    ClientToServerNetchanPacket packet,
    const std::size_t reliable_payload_size,
    const std::size_t unreliable_payload_size,
    const std::size_t padding_size,
    const bool reliable_retransmission,
    const bool expected_reliable_acknowledgement,
    const std::uint64_t channel_revision,
    const std::uint64_t reliable_generation) noexcept
    : packet_{std::move(packet)},
      reliable_payload_size_{reliable_payload_size},
      unreliable_payload_size_{unreliable_payload_size},
      padding_size_{padding_size},
      reliable_retransmission_{reliable_retransmission},
      expected_reliable_acknowledgement_{expected_reliable_acknowledgement},
      channel_revision_{channel_revision},
      reliable_generation_{reliable_generation}
{
}

NetchanIncomingInspection::NetchanIncomingInspection(
    NetchanHeader header,
    const NetchanIncomingSequenceDisposition disposition,
    const std::uint32_t skipped_sequences,
    std::optional<NetchanAcknowledgementDisposition> acknowledgement,
    const bool advance_acknowledgement,
    const bool clear_reliable,
    const std::uint64_t channel_revision) noexcept
    : header_{std::move(header)},
      disposition_{disposition},
      skipped_sequences_{skipped_sequences},
      acknowledgement_{acknowledgement},
      advance_acknowledgement_{advance_acknowledgement},
      clear_reliable_{clear_reliable},
      channel_revision_{channel_revision}
{
}

NetchanIncomingInspection::NetchanIncomingInspection(
    NetchanIncomingInspection&& other) noexcept
    : header_{std::move(other.header_)},
      disposition_{other.disposition_},
      skipped_sequences_{other.skipped_sequences_},
      acknowledgement_{other.acknowledgement_},
      advance_acknowledgement_{other.advance_acknowledgement_},
      clear_reliable_{other.clear_reliable_},
      consumable_{other.consumable_},
      channel_revision_{other.channel_revision_}
{
    other.consumable_ = false;
}

NetchanIncomingInspection& NetchanIncomingInspection::operator=(
    NetchanIncomingInspection&& other) noexcept
{
    if (this != &other) {
        header_ = std::move(other.header_);
        disposition_ = other.disposition_;
        skipped_sequences_ = other.skipped_sequences_;
        acknowledgement_ = other.acknowledgement_;
        advance_acknowledgement_ = other.advance_acknowledgement_;
        clear_reliable_ = other.clear_reliable_;
        consumable_ = other.consumable_;
        channel_revision_ = other.channel_revision_;
        other.consumable_ = false;
    }
    return *this;
}

NetchanChannel::NetchanChannel(
    const NetchanChannelLimits limits,
    const NetchanInitialState initial_state)
    : limits_{limits},
      next_outgoing_sequence_{initial_state.next_outgoing_sequence},
      last_outgoing_sequence_{initial_state.last_outgoing_sequence},
      incoming_sequence_{initial_state.incoming_sequence},
      peer_acknowledgement_{initial_state.peer_acknowledgement},
      outgoing_reliable_toggle_{initial_state.outgoing_reliable_toggle},
      incoming_reliable_toggle_{initial_state.incoming_reliable_toggle},
      peer_reliable_acknowledgement_{initial_state.peer_reliable_acknowledgement}
{
    const auto peer_ack_comparison = compare_sequences(
        peer_acknowledgement_,
        last_outgoing_sequence_);
    valid_configuration_ =
        limits_.maximum_packet_payload_size >=
            kStockProtocol48MinimumDecodedPayloadSize &&
        limits_.maximum_packet_payload_size <= kMaximumNetchanChannelPayloadSize &&
        limits_.maximum_reliable_payload_size <= limits_.maximum_packet_payload_size &&
        next_outgoing_sequence_ == next_sequence(last_outgoing_sequence_) &&
        peer_ack_comparison != NetchanSequenceComparison::newer &&
        peer_ack_comparison != NetchanSequenceComparison::half_range_ambiguous &&
        peer_reliable_acknowledgement_ == outgoing_reliable_toggle_;
}

bool NetchanChannel::valid_configuration() const noexcept
{
    return valid_configuration_;
}

NetchanChannelOperationResult NetchanChannel::queue_reliable_payload(
    const std::span<const std::byte> payload)
{
    if (!valid_configuration_) {
        return operation_failure(NetchanChannelErrorCode::invalid_configuration);
    }
    if (reliable_) {
        return operation_failure(NetchanChannelErrorCode::reliable_slot_occupied);
    }
    if (payload.empty()) {
        return operation_failure(NetchanChannelErrorCode::empty_reliable_payload);
    }
    if (payload.size() > limits_.maximum_reliable_payload_size) {
        return operation_failure(NetchanChannelErrorCode::reliable_payload_too_large);
    }

    ReliablePayloadState state;
    state.bytes.assign(payload.begin(), payload.end());
    state.acknowledgement_toggle = !outgoing_reliable_toggle_;
    state.generation = next_reliable_generation_++;
    reliable_ = std::move(state);
    ++revision_;
    return NetchanChannelOperationResult{};
}

bool NetchanChannel::has_reliable_payload() const noexcept
{
    return reliable_.has_value();
}

bool NetchanChannel::has_reliable_in_flight() const noexcept
{
    return reliable_ && reliable_->first_sent_sequence.has_value();
}

std::size_t NetchanChannel::reliable_payload_size() const noexcept
{
    return reliable_ ? reliable_->bytes.size() : 0U;
}

NetchanOutgoingPrepareResult NetchanChannel::prepare_outgoing(
    const std::span<const std::byte> unreliable_payload) const
{
    if (!valid_configuration_) {
        return prepare_failure(NetchanChannelErrorCode::invalid_configuration);
    }

    const auto reliable_size = reliable_ ? reliable_->bytes.size() : 0U;
    if (reliable_size > limits_.maximum_packet_payload_size ||
        unreliable_payload.size() > limits_.maximum_packet_payload_size - reliable_size) {
        return prepare_failure(NetchanChannelErrorCode::outgoing_payload_too_large);
    }

    const auto unpadded_size = reliable_size + unreliable_payload.size();
    const auto padding_size = unpadded_size < kStockProtocol48MinimumDecodedPayloadSize
                                  ? kStockProtocol48MinimumDecodedPayloadSize - unpadded_size
                                  : 0U;
    std::vector<std::byte> payload;
    payload.reserve(unpadded_size + padding_size);
    if (reliable_) {
        payload.insert(payload.end(), reliable_->bytes.begin(), reliable_->bytes.end());
    }
    payload.insert(payload.end(), unreliable_payload.begin(), unreliable_payload.end());
    payload.insert(payload.end(), padding_size, kStockProtocol48NetchanPaddingByte);

    const auto has_reliable = reliable_.has_value();
    ClientToServerNetchanPacket packet{
        NetchanHeader{
            NetchanSequenceWord{
                next_outgoing_sequence_,
                NetchanSequenceFlags{has_reliable, false},
            },
            NetchanAcknowledgementWord{
                incoming_sequence_,
                incoming_reliable_toggle_,
            },
        },
        {},
        std::move(payload),
    };

    const auto reliable_retransmission =
        reliable_ && reliable_->first_sent_sequence.has_value();
    const auto expected_acknowledgement =
        reliable_ ? reliable_->acknowledgement_toggle : outgoing_reliable_toggle_;
    const auto generation = reliable_ ? reliable_->generation : 0U;
    return NetchanOutgoingPrepareResult{
        NetchanOutgoingTransaction{
            std::move(packet),
            reliable_size,
            unreliable_payload.size(),
            padding_size,
            reliable_retransmission,
            expected_acknowledgement,
            revision_,
            generation,
        },
        std::nullopt,
    };
}

NetchanChannelOperationResult NetchanChannel::commit_outgoing(
    const NetchanOutgoingTransaction& transaction)
{
    if (!valid_configuration_) {
        return operation_failure(NetchanChannelErrorCode::invalid_configuration);
    }
    if (transaction.channel_revision_ != revision_) {
        return operation_failure(NetchanChannelErrorCode::stale_outgoing_transaction);
    }

    const auto& packet = transaction.packet_;
    const auto expected_reliable_size = reliable_ ? reliable_->bytes.size() : 0U;
    const auto expected_generation = reliable_ ? reliable_->generation : 0U;
    const auto expected_acknowledgement =
        reliable_ ? reliable_->acknowledgement_toggle : outgoing_reliable_toggle_;
    const auto expected_payload_size = transaction.reliable_payload_size_ +
                                       transaction.unreliable_payload_size_ +
                                       transaction.padding_size_;
    bool payload_matches = packet.payload.size() == expected_payload_size;
    if (payload_matches && reliable_) {
        payload_matches = std::equal(
            reliable_->bytes.begin(),
            reliable_->bytes.end(),
            packet.payload.begin());
    }
    if (payload_matches && transaction.padding_size_ > 0U) {
        payload_matches = std::all_of(
            packet.payload.end() - static_cast<std::ptrdiff_t>(transaction.padding_size_),
            packet.payload.end(),
            [](const std::byte value) {
                return value == kStockProtocol48NetchanPaddingByte;
            });
    }

    if (packet.header.sequence.sequence != next_outgoing_sequence_ ||
        packet.header.sequence.flags.reliable != reliable_.has_value() ||
        packet.header.sequence.flags.fragmented ||
        packet.header.acknowledgement.sequence != incoming_sequence_ ||
        packet.header.acknowledgement.reliable != incoming_reliable_toggle_ ||
        transaction.reliable_payload_size_ != expected_reliable_size ||
        transaction.reliable_generation_ != expected_generation ||
        transaction.expected_reliable_acknowledgement_ != expected_acknowledgement ||
        transaction.reliable_retransmission_ != has_reliable_in_flight() ||
        !payload_matches) {
        return operation_failure(NetchanChannelErrorCode::outgoing_transaction_mismatch);
    }

    const auto committed_sequence = next_outgoing_sequence_;
    last_outgoing_sequence_ = committed_sequence;
    next_outgoing_sequence_ = next_sequence(committed_sequence);
    if (reliable_) {
        if (!reliable_->first_sent_sequence) {
            outgoing_reliable_toggle_ = reliable_->acknowledgement_toggle;
            reliable_->first_sent_sequence = committed_sequence;
        }
        reliable_->last_sent_sequence = committed_sequence;
    }
    ++revision_;
    return NetchanChannelOperationResult{};
}

NetchanIncomingInspectResult NetchanChannel::inspect_incoming(
    const NetchanHeader& header) const noexcept
{
    const auto comparison = compare_sequences(
        header.sequence.sequence,
        incoming_sequence_);
    NetchanIncomingSequenceDisposition disposition{
        NetchanIncomingSequenceDisposition::duplicate};
    std::uint32_t skipped_sequences = 0U;
    switch (comparison) {
    case NetchanSequenceComparison::equal:
        disposition = NetchanIncomingSequenceDisposition::duplicate;
        break;
    case NetchanSequenceComparison::newer:
        disposition = NetchanIncomingSequenceDisposition::newer;
        skipped_sequences = sequence_distance(
                                header.sequence.sequence,
                                incoming_sequence_) -
                            1U;
        break;
    case NetchanSequenceComparison::older:
        disposition = NetchanIncomingSequenceDisposition::older;
        break;
    case NetchanSequenceComparison::half_range_ambiguous:
        disposition = NetchanIncomingSequenceDisposition::half_range_ambiguous;
        break;
    }
    if (disposition != NetchanIncomingSequenceDisposition::newer) {
        return NetchanIncomingInspectResult{
            NetchanIncomingInspection{
                header,
                disposition,
                skipped_sequences,
                std::nullopt,
                false,
                false,
                revision_,
            },
            std::nullopt,
        };
    }

    const auto acknowledgement = evaluate_acknowledgement(header.acknowledgement);
    if (acknowledgement.error) {
        return NetchanIncomingInspectResult{
            std::nullopt,
            acknowledgement.error,
        };
    }
    return NetchanIncomingInspectResult{
        NetchanIncomingInspection{
            header,
            disposition,
            skipped_sequences,
            acknowledgement.disposition,
            acknowledgement.advance,
            acknowledgement.clear_reliable,
            revision_,
        },
        std::nullopt,
    };
}

NetchanChannel::AcknowledgementEvaluation NetchanChannel::evaluate_acknowledgement(
    const NetchanAcknowledgementWord& acknowledgement) const noexcept
{
    const auto versus_last_sent = compare_sequences(
        acknowledgement.sequence,
        last_outgoing_sequence_);
    if (versus_last_sent == NetchanSequenceComparison::newer) {
        return AcknowledgementEvaluation{
            std::nullopt,
            NetchanChannelError{NetchanChannelErrorCode::future_acknowledgement},
        };
    }
    if (versus_last_sent == NetchanSequenceComparison::half_range_ambiguous) {
        return AcknowledgementEvaluation{
            std::nullopt,
            NetchanChannelError{
                NetchanChannelErrorCode::acknowledgement_half_range_ambiguous},
        };
    }

    const auto versus_previous = compare_sequences(
        acknowledgement.sequence,
        peer_acknowledgement_);
    if (versus_previous == NetchanSequenceComparison::half_range_ambiguous) {
        return AcknowledgementEvaluation{
            std::nullopt,
            NetchanChannelError{
                NetchanChannelErrorCode::acknowledgement_half_range_ambiguous},
        };
    }
    if (versus_previous == NetchanSequenceComparison::older) {
        return AcknowledgementEvaluation{
            NetchanAcknowledgementDisposition::stale,
            std::nullopt,
        };
    }
    if (versus_previous == NetchanSequenceComparison::equal) {
        if (acknowledgement.reliable == peer_reliable_acknowledgement_) {
            return AcknowledgementEvaluation{
                NetchanAcknowledgementDisposition::duplicate,
                std::nullopt,
            };
        }
        if (reliable_ && reliable_->first_sent_sequence) {
            const auto versus_first_reliable = compare_sequences(
                acknowledgement.sequence,
                *reliable_->first_sent_sequence);
            const auto covers_reliable =
                versus_first_reliable == NetchanSequenceComparison::equal ||
                versus_first_reliable == NetchanSequenceComparison::newer;
            if (covers_reliable &&
                acknowledgement.reliable == reliable_->acknowledgement_toggle) {
                return AcknowledgementEvaluation{
                    NetchanAcknowledgementDisposition::advanced_reliable_confirmed,
                    std::nullopt,
                    true,
                    true,
                };
            }
        }
        return AcknowledgementEvaluation{
            std::nullopt,
            NetchanChannelError{
                NetchanChannelErrorCode::invalid_reliable_acknowledgement_toggle},
        };
    }

    if (!reliable_ || !reliable_->first_sent_sequence) {
        if (acknowledgement.reliable != outgoing_reliable_toggle_) {
            return AcknowledgementEvaluation{
                std::nullopt,
                NetchanChannelError{
                    NetchanChannelErrorCode::invalid_reliable_acknowledgement_toggle},
            };
        }
        return AcknowledgementEvaluation{
            NetchanAcknowledgementDisposition::advanced,
            std::nullopt,
            true,
            false,
        };
    }

    const auto versus_first_reliable = compare_sequences(
        acknowledgement.sequence,
        *reliable_->first_sent_sequence);
    if (versus_first_reliable == NetchanSequenceComparison::half_range_ambiguous) {
        return AcknowledgementEvaluation{
            std::nullopt,
            NetchanChannelError{
                NetchanChannelErrorCode::acknowledgement_half_range_ambiguous},
        };
    }
    const auto acknowledges_reliable_sequence =
        versus_first_reliable == NetchanSequenceComparison::equal ||
        versus_first_reliable == NetchanSequenceComparison::newer;
    const auto reliable_matches =
        acknowledgement.reliable == reliable_->acknowledgement_toggle;
    if (acknowledges_reliable_sequence && reliable_matches) {
        return AcknowledgementEvaluation{
            NetchanAcknowledgementDisposition::advanced_reliable_confirmed,
            std::nullopt,
            true,
            true,
        };
    }
    return AcknowledgementEvaluation{
        NetchanAcknowledgementDisposition::advanced_reliable_unconfirmed,
        std::nullopt,
        true,
        false,
    };
}

void NetchanChannel::commit_incoming(
    NetchanIncomingInspection&& inspection,
    const NetchanIncomingReliableUnit reliable_unit) noexcept
{
    if (!inspection.consumable_) {
        return;
    }
    inspection.consumable_ = false;
    // An ignored token requires neither reassembly nor a commit. Treat an
    // accidental call as a harmless no-op in release builds.
    if (!inspection.should_commit()) {
        return;
    }
    const auto contains_reliable = inspection.header_.sequence.flags.reliable;
    const auto reliable_resolution_matches =
        (contains_reliable && reliable_unit != NetchanIncomingReliableUnit::none) ||
        (!contains_reliable && reliable_unit == NetchanIncomingReliableUnit::none);
    if (inspection.channel_revision_ != revision_ || !reliable_resolution_matches) {
        return;
    }

    if (inspection.advance_acknowledgement_) {
        peer_acknowledgement_ = inspection.header_.acknowledgement.sequence;
        peer_reliable_acknowledgement_ = inspection.header_.acknowledgement.reliable;
    }
    if (inspection.clear_reliable_) {
        reliable_.reset();
    }

    incoming_sequence_ = inspection.header_.sequence.sequence;
    if (contains_reliable && reliable_unit == NetchanIncomingReliableUnit::new_unit) {
        incoming_reliable_toggle_ = !incoming_reliable_toggle_;
    }
    ++revision_;
}

NetchanSequence NetchanChannel::next_outgoing_sequence() const noexcept
{
    return next_outgoing_sequence_;
}

NetchanSequence NetchanChannel::last_outgoing_sequence() const noexcept
{
    return last_outgoing_sequence_;
}

NetchanSequence NetchanChannel::incoming_sequence() const noexcept
{
    return incoming_sequence_;
}

NetchanSequence NetchanChannel::peer_acknowledgement() const noexcept
{
    return peer_acknowledgement_;
}

bool NetchanChannel::outgoing_reliable_toggle() const noexcept
{
    return outgoing_reliable_toggle_;
}

bool NetchanChannel::incoming_reliable_toggle() const noexcept
{
    return incoming_reliable_toggle_;
}

bool NetchanChannel::peer_reliable_acknowledgement() const noexcept
{
    return peer_reliable_acknowledgement_;
}

} // namespace hlclient::goldsrc
