#include <hlclient/goldsrc/netchan_session.hpp>

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

[[nodiscard]] NetchanSessionOperationResult operation_failure(
    const NetchanSessionErrorCode code) noexcept
{
    return NetchanSessionOperationResult{NetchanSessionError{code}};
}

[[nodiscard]] NetchanIncomingInspectResult inspection_failure(
    const NetchanSessionErrorCode code) noexcept
{
    return NetchanIncomingInspectResult{
        std::nullopt,
        NetchanSessionError{code},
    };
}

[[nodiscard]] NetchanFirstAcknowledgementPrepareResult prepare_failure(
    const NetchanSessionErrorCode code) noexcept
{
    return NetchanFirstAcknowledgementPrepareResult{
        std::nullopt,
        NetchanSessionError{code},
    };
}

} // namespace

NetchanSequenceState NetchanSequenceState::stock_protocol48() noexcept
{
    return NetchanSequenceState{
        sequence(1U),
        sequence(0U),
        sequence(0U),
        sequence(0U),
        false,
        false,
    };
}

NetchanIncomingInspection::NetchanIncomingInspection(
    NetchanHeader header,
    const NetchanIncomingSequenceDisposition disposition,
    const std::uint32_t skipped_sequences,
    std::optional<NetchanAcknowledgementObservation> acknowledgement,
    const std::uint64_t session_revision) noexcept
    : header_{std::move(header)},
      disposition_{disposition},
      skipped_sequences_{skipped_sequences},
      acknowledgement_{std::move(acknowledgement)},
      session_revision_{session_revision}
{
}

NetchanIncomingInspection::NetchanIncomingInspection(
    NetchanIncomingInspection&& other) noexcept
    : header_{std::move(other.header_)},
      disposition_{other.disposition_},
      skipped_sequences_{other.skipped_sequences_},
      acknowledgement_{std::move(other.acknowledgement_)},
      session_revision_{other.session_revision_},
      consumable_{other.consumable_}
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
        acknowledgement_ = std::move(other.acknowledgement_);
        session_revision_ = other.session_revision_;
        consumable_ = other.consumable_;
        other.consumable_ = false;
    }
    return *this;
}

NetchanFirstAcknowledgementTransaction::NetchanFirstAcknowledgementTransaction(
    ClientToServerNetchanPacket packet,
    const std::uint64_t session_revision) noexcept
    : packet_{std::move(packet)}, session_revision_{session_revision}
{
}

NetchanFirstAcknowledgementTransaction::NetchanFirstAcknowledgementTransaction(
    NetchanFirstAcknowledgementTransaction&& other) noexcept
    : packet_{std::move(other.packet_)},
      session_revision_{other.session_revision_},
      consumable_{other.consumable_}
{
    other.consumable_ = false;
}

NetchanFirstAcknowledgementTransaction&
NetchanFirstAcknowledgementTransaction::operator=(
    NetchanFirstAcknowledgementTransaction&& other) noexcept
{
    if (this != &other) {
        packet_ = std::move(other.packet_);
        session_revision_ = other.session_revision_;
        consumable_ = other.consumable_;
        other.consumable_ = false;
    }
    return *this;
}

NetchanSession::NetchanSession(const NetchanSequenceState initial_state) noexcept
    : state_{initial_state}
{
    const auto acknowledgement_comparison = compare_sequences(
        state_.peer_acknowledgement,
        state_.last_outgoing_sequence);
    valid_configuration_ =
        state_.next_outgoing_sequence == next_sequence(state_.last_outgoing_sequence) &&
        acknowledgement_comparison != NetchanSequenceComparison::newer &&
        acknowledgement_comparison != NetchanSequenceComparison::half_range_ambiguous;
}

bool NetchanSession::valid_configuration() const noexcept
{
    return valid_configuration_;
}

NetchanIncomingInspectResult NetchanSession::inspect_incoming(
    const NetchanHeader& header) const noexcept
{
    if (!valid_configuration_) {
        return inspection_failure(NetchanSessionErrorCode::invalid_configuration);
    }

    const auto comparison = compare_sequences(
        header.sequence.sequence,
        state_.incoming_sequence);
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
                                state_.incoming_sequence) -
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
                revision_,
            },
            std::nullopt,
        };
    }
    if (first_incoming_committed_) {
        return inspection_failure(
            NetchanSessionErrorCode::first_incoming_already_committed);
    }

    const auto versus_last_outgoing = compare_sequences(
        header.acknowledgement.sequence,
        state_.last_outgoing_sequence);
    if (versus_last_outgoing == NetchanSequenceComparison::newer) {
        return inspection_failure(NetchanSessionErrorCode::future_acknowledgement);
    }
    if (versus_last_outgoing == NetchanSequenceComparison::half_range_ambiguous) {
        return inspection_failure(
            NetchanSessionErrorCode::acknowledgement_half_range_ambiguous);
    }

    const auto versus_previous = compare_sequences(
        header.acknowledgement.sequence,
        state_.peer_acknowledgement);
    if (versus_previous == NetchanSequenceComparison::half_range_ambiguous) {
        return inspection_failure(
            NetchanSessionErrorCode::acknowledgement_half_range_ambiguous);
    }

    NetchanAcknowledgementDisposition acknowledgement_disposition{
        NetchanAcknowledgementDisposition::duplicate};
    if (versus_previous == NetchanSequenceComparison::newer) {
        acknowledgement_disposition = NetchanAcknowledgementDisposition::advanced;
    } else if (versus_previous == NetchanSequenceComparison::older) {
        acknowledgement_disposition = NetchanAcknowledgementDisposition::stale;
    }

    return NetchanIncomingInspectResult{
        NetchanIncomingInspection{
            header,
            disposition,
            skipped_sequences,
            NetchanAcknowledgementObservation{
                header.acknowledgement.sequence,
                header.acknowledgement.reliable,
                acknowledgement_disposition,
            },
            revision_,
        },
        std::nullopt,
    };
}

NetchanSessionOperationResult NetchanSession::commit_incoming(
    NetchanIncomingInspection&& inspection) noexcept
{
    if (!inspection.consumable_ || inspection.session_revision_ != revision_) {
        inspection.consumable_ = false;
        return operation_failure(NetchanSessionErrorCode::stale_incoming_inspection);
    }
    inspection.consumable_ = false;
    if (!valid_configuration_) {
        return operation_failure(NetchanSessionErrorCode::invalid_configuration);
    }
    if (first_incoming_committed_) {
        return operation_failure(
            NetchanSessionErrorCode::first_incoming_already_committed);
    }
    if (!inspection.should_commit() || !inspection.acknowledgement_) {
        return operation_failure(NetchanSessionErrorCode::incoming_not_newer);
    }

    state_.incoming_sequence = inspection.header_.sequence.sequence;
    if (inspection.header_.sequence.flags.reliable) {
        state_.incoming_reliable_acknowledgement =
            !state_.incoming_reliable_acknowledgement;
    }

    const auto& acknowledgement = *inspection.acknowledgement_;
    if (acknowledgement.disposition != NetchanAcknowledgementDisposition::stale) {
        state_.peer_acknowledgement = acknowledgement.sequence;
        state_.peer_reliable_acknowledgement = acknowledgement.reliable;
    }

    first_incoming_committed_ = true;
    ++revision_;
    return NetchanSessionOperationResult{};
}

NetchanFirstAcknowledgementPrepareResult
NetchanSession::prepare_first_acknowledgement()
{
    if (!valid_configuration_) {
        return prepare_failure(NetchanSessionErrorCode::invalid_configuration);
    }
    if (!first_incoming_committed_) {
        return prepare_failure(
            NetchanSessionErrorCode::first_acknowledgement_before_incoming);
    }
    if (first_acknowledgement_prepared_ || first_acknowledgement_sent_) {
        return prepare_failure(
            NetchanSessionErrorCode::first_acknowledgement_already_prepared);
    }

    ClientToServerNetchanPacket packet{
        NetchanHeader{
            NetchanSequenceWord{
                state_.next_outgoing_sequence,
                NetchanSequenceFlags{false, false},
            },
            NetchanAcknowledgementWord{
                state_.incoming_sequence,
                state_.incoming_reliable_acknowledgement,
            },
        },
        {},
        std::vector<std::byte>(
            kStockProtocol48MinimumDecodedPayloadSize,
            kStockProtocol48NetchanPaddingByte),
    };

    first_acknowledgement_prepared_ = true;
    ++revision_;
    return NetchanFirstAcknowledgementPrepareResult{
        NetchanFirstAcknowledgementTransaction{std::move(packet), revision_},
        std::nullopt,
    };
}

NetchanSessionOperationResult NetchanSession::commit_first_acknowledgement(
    NetchanFirstAcknowledgementTransaction&& transaction) noexcept
{
    if (!transaction.consumable_ || transaction.session_revision_ != revision_) {
        transaction.consumable_ = false;
        return operation_failure(
            NetchanSessionErrorCode::stale_first_acknowledgement_transaction);
    }
    transaction.consumable_ = false;
    if (!valid_configuration_) {
        return operation_failure(NetchanSessionErrorCode::invalid_configuration);
    }
    if (!first_acknowledgement_prepared_ || first_acknowledgement_sent_) {
        return operation_failure(
            NetchanSessionErrorCode::first_acknowledgement_transaction_mismatch);
    }

    const auto& packet = transaction.packet_;
    const bool packet_matches =
        packet.header.sequence.sequence == state_.next_outgoing_sequence &&
        !packet.header.sequence.flags.reliable &&
        !packet.header.sequence.flags.fragmented &&
        packet.header.acknowledgement.sequence == state_.incoming_sequence &&
        packet.header.acknowledgement.reliable ==
            state_.incoming_reliable_acknowledgement &&
        std::ranges::none_of(
            packet.fragments,
            [](const auto& descriptor) { return descriptor.has_value(); }) &&
        packet.payload.size() == kStockProtocol48MinimumDecodedPayloadSize &&
        std::ranges::all_of(packet.payload, [](const std::byte value) {
            return value == kStockProtocol48NetchanPaddingByte;
        });
    if (!packet_matches) {
        return operation_failure(
            NetchanSessionErrorCode::first_acknowledgement_transaction_mismatch);
    }

    state_.last_outgoing_sequence = state_.next_outgoing_sequence;
    state_.next_outgoing_sequence = next_sequence(state_.next_outgoing_sequence);
    first_acknowledgement_sent_ = true;
    ++revision_;
    return NetchanSessionOperationResult{};
}

const NetchanSequenceState& NetchanSession::state() const noexcept
{
    return state_;
}

bool NetchanSession::first_incoming_committed() const noexcept
{
    return first_incoming_committed_;
}

bool NetchanSession::first_acknowledgement_prepared() const noexcept
{
    return first_acknowledgement_prepared_;
}

bool NetchanSession::first_acknowledgement_sent() const noexcept
{
    return first_acknowledgement_sent_;
}

} // namespace hlclient::goldsrc
