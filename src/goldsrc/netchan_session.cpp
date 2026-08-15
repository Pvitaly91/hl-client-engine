#include <hlclient/goldsrc/netchan_session.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace hlclient::goldsrc {

class NetchanSessionIdentity final {
};

namespace {

[[nodiscard]] NetchanSequence sequence(const std::uint32_t value) noexcept
{
    return *NetchanSequence::from_numeric(value & kNetchanSequenceMask);
}

[[nodiscard]] NetchanSequence next_sequence(const NetchanSequence current) noexcept
{
    return sequence((current.value() + 1U) & kNetchanSequenceMask);
}

[[nodiscard]] bool valid_limits(const NetchanSessionLimits& limits) noexcept
{
    if (limits.maximum_datagram_size <
            kNetchanHeaderSize + kStockProtocol48MinimumDecodedPayloadSize ||
        limits.maximum_datagram_size > kMaximumNetchanDatagramSize) {
        return false;
    }

    const auto maximum_body_size =
        limits.maximum_datagram_size - kNetchanHeaderSize;
    return limits.maximum_unfragmented_reliable_payload > 0U &&
           limits.maximum_unfragmented_reliable_payload <= maximum_body_size &&
           limits.maximum_pending_reliable_payload >=
               limits.maximum_unfragmented_reliable_payload &&
           limits.maximum_pending_reliable_payload <=
               kMaximumPendingReliablePayload;
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

[[nodiscard]] NetchanFirstAcknowledgementPrepareResult first_prepare_failure(
    const NetchanSessionErrorCode code) noexcept
{
    return NetchanFirstAcknowledgementPrepareResult{
        std::nullopt,
        NetchanSessionError{code},
    };
}

[[nodiscard]] NetchanTransmitPrepareResult transmit_prepare_failure(
    const NetchanSessionErrorCode code) noexcept
{
    return NetchanTransmitPrepareResult{
        std::nullopt,
        NetchanSessionError{code},
    };
}

[[nodiscard]] bool no_fragments(const NetchanFragmentSlots& fragments) noexcept
{
    return std::ranges::none_of(
        fragments,
        [](const auto& descriptor) { return descriptor.has_value(); });
}

[[nodiscard]] std::size_t padded_body_size(const std::size_t payload_size) noexcept
{
    return std::max(payload_size, kStockProtocol48MinimumDecodedPayloadSize);
}

} // namespace

ReliableTransmitDecision decide_reliable_transmit(
    const NetchanReliableTransmitState& state) noexcept
{
    if (state.has_in_flight_payload) {
        return state.retransmission_requested
                   ? ReliableTransmitDecision::retransmit
                   : ReliableTransmitDecision::blocked_waiting_for_ack;
    }
    if (!state.has_pending_payload) {
        return ReliableTransmitDecision::none;
    }
    if (state.pending_requires_fragmentation) {
        return ReliableTransmitDecision::requires_fragmentation_pending_m2_3_3;
    }
    return ReliableTransmitDecision::send_new;
}

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
    const bool incoming_reliable_acknowledgement_after_commit,
    std::shared_ptr<const NetchanSessionIdentity> session_identity,
    const std::uint64_t session_revision) noexcept
    : header_{std::move(header)},
      disposition_{disposition},
      skipped_sequences_{skipped_sequences},
      acknowledgement_{std::move(acknowledgement)},
      incoming_reliable_acknowledgement_after_commit_{
          incoming_reliable_acknowledgement_after_commit},
      session_identity_{std::move(session_identity)},
      session_revision_{session_revision}
{
}

NetchanIncomingInspection::NetchanIncomingInspection(
    NetchanIncomingInspection&& other) noexcept
    : header_{std::move(other.header_)},
      disposition_{other.disposition_},
      skipped_sequences_{other.skipped_sequences_},
      acknowledgement_{std::move(other.acknowledgement_)},
      incoming_reliable_acknowledgement_after_commit_{
          other.incoming_reliable_acknowledgement_after_commit_},
      session_identity_{std::move(other.session_identity_)},
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
        incoming_reliable_acknowledgement_after_commit_ =
            other.incoming_reliable_acknowledgement_after_commit_;
        session_identity_ = std::move(other.session_identity_);
        session_revision_ = other.session_revision_;
        consumable_ = other.consumable_;
        other.consumable_ = false;
    }
    return *this;
}

NetchanFirstAcknowledgementTransaction::NetchanFirstAcknowledgementTransaction(
    ClientToServerNetchanPacket packet,
    std::shared_ptr<const NetchanSessionIdentity> session_identity,
    const std::uint64_t session_revision) noexcept
    : packet_{std::move(packet)},
      session_identity_{std::move(session_identity)},
      session_revision_{session_revision}
{
}

NetchanFirstAcknowledgementTransaction::NetchanFirstAcknowledgementTransaction(
    NetchanFirstAcknowledgementTransaction&& other) noexcept
    : packet_{std::move(other.packet_)},
      session_identity_{std::move(other.session_identity_)},
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
        session_identity_ = std::move(other.session_identity_);
        session_revision_ = other.session_revision_;
        consumable_ = other.consumable_;
        other.consumable_ = false;
    }
    return *this;
}

NetchanTransmitPlan::NetchanTransmitPlan(
    ClientToServerNetchanPacket packet,
    const ReliableTransmitDecision reliable_decision,
    const std::size_t reliable_payload_size,
    const std::size_t unreliable_payload_size,
    const bool planned_reliable_toggle,
    std::shared_ptr<const NetchanSessionIdentity> session_identity,
    const std::uint64_t session_revision) noexcept
    : packet_{std::move(packet)},
      reliable_decision_{reliable_decision},
      reliable_payload_size_{reliable_payload_size},
      unreliable_payload_size_{unreliable_payload_size},
      planned_reliable_toggle_{planned_reliable_toggle},
      session_identity_{std::move(session_identity)},
      session_revision_{session_revision}
{
}

NetchanTransmitPlan::NetchanTransmitPlan(NetchanTransmitPlan&& other) noexcept
    : packet_{std::move(other.packet_)},
      reliable_decision_{other.reliable_decision_},
      reliable_payload_size_{other.reliable_payload_size_},
      unreliable_payload_size_{other.unreliable_payload_size_},
      planned_reliable_toggle_{other.planned_reliable_toggle_},
      session_identity_{std::move(other.session_identity_)},
      session_revision_{other.session_revision_},
      consumable_{other.consumable_}
{
    other.consumable_ = false;
}

NetchanTransmitPlan& NetchanTransmitPlan::operator=(
    NetchanTransmitPlan&& other) noexcept
{
    if (this != &other) {
        packet_ = std::move(other.packet_);
        reliable_decision_ = other.reliable_decision_;
        reliable_payload_size_ = other.reliable_payload_size_;
        unreliable_payload_size_ = other.unreliable_payload_size_;
        planned_reliable_toggle_ = other.planned_reliable_toggle_;
        session_identity_ = std::move(other.session_identity_);
        session_revision_ = other.session_revision_;
        consumable_ = other.consumable_;
        other.consumable_ = false;
    }
    return *this;
}

NetchanSession::NetchanSession(
    const NetchanSequenceState initial_state,
    const NetchanSessionLimits limits)
    : state_{initial_state},
      limits_{limits},
      identity_{std::make_shared<const NetchanSessionIdentity>()},
      outgoing_reliable_toggle_{initial_state.peer_reliable_acknowledgement}
{
    const auto acknowledgement_comparison = compare_sequences(
        state_.peer_acknowledgement,
        state_.last_outgoing_sequence);
    valid_configuration_ =
        valid_limits(limits_) &&
        state_.next_outgoing_sequence == next_sequence(state_.last_outgoing_sequence) &&
        acknowledgement_comparison != NetchanSequenceComparison::newer &&
        acknowledgement_comparison != NetchanSequenceComparison::half_range_ambiguous;
}

NetchanSession::NetchanSession(const NetchanSessionLimits limits)
    : NetchanSession{NetchanSequenceState::stock_protocol48(), limits}
{
}

bool NetchanSession::valid_configuration() const noexcept
{
    return valid_configuration_;
}

const NetchanSessionLimits& NetchanSession::limits() const noexcept
{
    return limits_;
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

    const bool reliable_ack_after_commit =
        disposition == NetchanIncomingSequenceDisposition::newer &&
                header.sequence.flags.reliable
            ? !state_.incoming_reliable_acknowledgement
            : state_.incoming_reliable_acknowledgement;
    if (disposition != NetchanIncomingSequenceDisposition::newer) {
        return NetchanIncomingInspectResult{
            NetchanIncomingInspection{
                header,
                disposition,
                skipped_sequences,
                std::nullopt,
                reliable_ack_after_commit,
                identity_,
                revision_,
            },
            std::nullopt,
        };
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
    if (in_flight_reliable_payload_ &&
        compare_sequences(
            header.acknowledgement.sequence,
            in_flight_reliable_payload_->most_recent_sent_sequence) ==
            NetchanSequenceComparison::half_range_ambiguous) {
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
            reliable_ack_after_commit,
            identity_,
            revision_,
        },
        std::nullopt,
    };
}

NetchanSessionOperationResult NetchanSession::commit_incoming(
    NetchanIncomingInspection&& inspection) noexcept
{
    if (inspection.session_identity_ != identity_) {
        return operation_failure(
            NetchanSessionErrorCode::foreign_incoming_inspection);
    }
    if (!inspection.consumable_ || inspection.session_revision_ != revision_) {
        inspection.consumable_ = false;
        return operation_failure(NetchanSessionErrorCode::stale_incoming_inspection);
    }
    inspection.consumable_ = false;
    if (!valid_configuration_) {
        return operation_failure(NetchanSessionErrorCode::invalid_configuration);
    }
    if (!inspection.should_commit() || !inspection.acknowledgement_) {
        return operation_failure(NetchanSessionErrorCode::incoming_not_newer);
    }
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return operation_failure(NetchanSessionErrorCode::session_revision_overflow);
    }
    if (inspection.header_.sequence.flags.fragmented) {
        return operation_failure(
            NetchanSessionErrorCode::fragmented_payload_pending_m2_3_3);
    }

    const auto& acknowledgement = *inspection.acknowledgement_;
    const bool acknowledgement_advanced =
        acknowledgement.disposition == NetchanAcknowledgementDisposition::advanced;

    // All decisions are made against committed state before any field mutates.
    bool clear_in_flight = false;
    bool request_retransmission = false;
    if (in_flight_reliable_payload_ && acknowledgement_advanced) {
        // Repeated stock drop-first and drop-first-two captures show that an
        // old-generation ACK must advance strictly past the most recent send
        // before another copy is emitted. Matching-generation clears are
        // fail-closed to ACK coverage of that same most-recent sequence.
        const auto versus_most_recent_send = compare_sequences(
            acknowledgement.sequence,
            in_flight_reliable_payload_->most_recent_sent_sequence);
        const bool covers_most_recent_send =
            versus_most_recent_send == NetchanSequenceComparison::equal ||
            versus_most_recent_send == NetchanSequenceComparison::newer;
        if (covers_most_recent_send &&
            acknowledgement.reliable == in_flight_reliable_payload_->toggle) {
            clear_in_flight = true;
        } else if (
            versus_most_recent_send == NetchanSequenceComparison::newer &&
            acknowledgement.reliable != in_flight_reliable_payload_->toggle) {
            request_retransmission = true;
        }
    }

    state_.incoming_sequence = inspection.header_.sequence.sequence;
    state_.incoming_reliable_acknowledgement =
        inspection.incoming_reliable_acknowledgement_after_commit_;
    if (acknowledgement.disposition != NetchanAcknowledgementDisposition::stale) {
        state_.peer_acknowledgement = acknowledgement.sequence;
        state_.peer_reliable_acknowledgement = acknowledgement.reliable;
    }

    if (clear_in_flight) {
        in_flight_reliable_payload_.reset();
    } else if (request_retransmission && in_flight_reliable_payload_) {
        in_flight_reliable_payload_->retransmission_requested = true;
    }

    first_incoming_committed_ = true;
    ++revision_;
    return NetchanSessionOperationResult{};
}

NetchanSessionOperationResult NetchanSession::queue_reliable(
    const std::span<const std::byte> payload)
{
    if (!valid_configuration_) {
        return operation_failure(NetchanSessionErrorCode::invalid_configuration);
    }
    if (payload.empty()) {
        return NetchanSessionOperationResult{};
    }
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return operation_failure(NetchanSessionErrorCode::session_revision_overflow);
    }
    if (pending_reliable_payload_.size() >
            limits_.maximum_pending_reliable_payload ||
        payload.size() > limits_.maximum_pending_reliable_payload -
                             pending_reliable_payload_.size()) {
        return operation_failure(NetchanSessionErrorCode::reliable_queue_overflow);
    }

    std::vector<std::byte> candidate = pending_reliable_payload_;
    candidate.reserve(candidate.size() + payload.size());
    candidate.insert(candidate.end(), payload.begin(), payload.end());
    pending_reliable_payload_.swap(candidate);
    ++revision_;
    return NetchanSessionOperationResult{};
}

NetchanTransmitPrepareResult NetchanSession::prepare_outgoing_packet(
    const std::span<const std::byte> unreliable_payload) const
{
    if (!valid_configuration_) {
        return transmit_prepare_failure(
            NetchanSessionErrorCode::invalid_configuration);
    }
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return transmit_prepare_failure(
            NetchanSessionErrorCode::session_revision_overflow);
    }
    if (first_acknowledgement_prepared_ && !first_acknowledgement_sent_) {
        return transmit_prepare_failure(
            NetchanSessionErrorCode::outgoing_transaction_mismatch);
    }

    const bool pending_requires_fragmentation =
        pending_reliable_payload_.size() >
        limits_.maximum_unfragmented_reliable_payload;
    const auto decision = decide_reliable_transmit(NetchanReliableTransmitState{
        !pending_reliable_payload_.empty(),
        in_flight_reliable_payload_.has_value(),
        in_flight_reliable_payload_ &&
            in_flight_reliable_payload_->retransmission_requested,
        pending_requires_fragmentation,
    });
    if (decision ==
        ReliableTransmitDecision::requires_fragmentation_pending_m2_3_3) {
        return transmit_prepare_failure(NetchanSessionErrorCode::
                                            reliable_payload_requires_fragmentation_pending_m2_3_3);
    }
    if (decision == ReliableTransmitDecision::retransmit &&
        in_flight_reliable_payload_->send_count ==
            std::numeric_limits<std::uint64_t>::max()) {
        return transmit_prepare_failure(
            NetchanSessionErrorCode::reliable_send_count_overflow);
    }

    std::span<const std::byte> reliable_payload;
    bool planned_reliable_toggle = outgoing_reliable_toggle_;
    if (decision == ReliableTransmitDecision::send_new) {
        reliable_payload = pending_reliable_payload_;
        planned_reliable_toggle = !outgoing_reliable_toggle_;
    } else if (decision == ReliableTransmitDecision::retransmit) {
        reliable_payload = in_flight_reliable_payload_->bytes;
        planned_reliable_toggle = in_flight_reliable_payload_->toggle;
    }

    const auto maximum_body_size =
        limits_.maximum_datagram_size - kNetchanHeaderSize;
    if (unreliable_payload.size() > maximum_body_size) {
        return transmit_prepare_failure(
            NetchanSessionErrorCode::unreliable_payload_does_not_fit);
    }
    if (reliable_payload.size() > maximum_body_size - unreliable_payload.size()) {
        return transmit_prepare_failure(
            NetchanSessionErrorCode::combined_payload_does_not_fit);
    }

    const auto composed_size = reliable_payload.size() + unreliable_payload.size();
    std::vector<std::byte> body(
        padded_body_size(composed_size),
        kStockProtocol48NetchanPaddingByte);
    // The reliable prefix is canonical and retained; the unreliable suffix is
    // one-shot. This deterministic composition order is secondary-reference
    // informed but has not yet been isolated in byte-preserving stock capture.
    auto output = body.begin();
    output = std::ranges::copy(reliable_payload, output).out;
    std::ranges::copy(unreliable_payload, output);

    ClientToServerNetchanPacket packet{
        NetchanHeader{
            NetchanSequenceWord{
                state_.next_outgoing_sequence,
                NetchanSequenceFlags{!reliable_payload.empty(), false},
            },
            NetchanAcknowledgementWord{
                state_.incoming_sequence,
                state_.incoming_reliable_acknowledgement,
            },
        },
        {},
        std::move(body),
    };

    return NetchanTransmitPrepareResult{
        NetchanTransmitPlan{
            std::move(packet),
            decision,
            reliable_payload.size(),
            unreliable_payload.size(),
            planned_reliable_toggle,
            identity_,
            revision_,
        },
        std::nullopt,
    };
}

NetchanSessionOperationResult NetchanSession::validate_outgoing_plan(
    NetchanTransmitPlan& plan) const noexcept
{
    if (plan.session_identity_ != identity_) {
        return operation_failure(
            NetchanSessionErrorCode::foreign_outgoing_transaction);
    }
    if (!valid_configuration_) {
        plan.consumable_ = false;
        return operation_failure(NetchanSessionErrorCode::invalid_configuration);
    }
    if (!plan.consumable_ || plan.session_revision_ != revision_) {
        plan.consumable_ = false;
        return operation_failure(NetchanSessionErrorCode::stale_outgoing_transaction);
    }

    const auto current_decision = decide_reliable_transmit(
        NetchanReliableTransmitState{
            !pending_reliable_payload_.empty(),
            in_flight_reliable_payload_.has_value(),
            in_flight_reliable_payload_ &&
                in_flight_reliable_payload_->retransmission_requested,
            pending_reliable_payload_.size() >
                limits_.maximum_unfragmented_reliable_payload,
        });
    const bool reliable_decision =
        plan.reliable_decision_ == ReliableTransmitDecision::send_new ||
        plan.reliable_decision_ == ReliableTransmitDecision::retransmit;
    const auto& packet = plan.packet_;
    const auto composed_size =
        plan.reliable_payload_size_ + plan.unreliable_payload_size_;
    const bool header_matches =
        packet.header.sequence.sequence == state_.next_outgoing_sequence &&
        packet.header.sequence.flags.reliable == reliable_decision &&
        !packet.header.sequence.flags.fragmented &&
        packet.header.acknowledgement.sequence == state_.incoming_sequence &&
        packet.header.acknowledgement.reliable ==
            state_.incoming_reliable_acknowledgement &&
        no_fragments(packet.fragments);
    const bool sizes_match =
        plan.reliable_payload_size_ <= packet.payload.size() &&
        plan.unreliable_payload_size_ <=
            packet.payload.size() - plan.reliable_payload_size_ &&
        packet.payload.size() == padded_body_size(composed_size) &&
        packet.payload.size() <= limits_.maximum_datagram_size - kNetchanHeaderSize;
    bool reliable_bytes_match = true;
    bool toggle_matches = plan.planned_reliable_toggle_ == outgoing_reliable_toggle_;
    if (plan.reliable_decision_ == ReliableTransmitDecision::send_new) {
        reliable_bytes_match =
            plan.reliable_payload_size_ == pending_reliable_payload_.size() &&
            std::ranges::equal(
                pending_reliable_payload_,
                std::span<const std::byte>{packet.payload}.first(
                    plan.reliable_payload_size_));
        toggle_matches =
            plan.planned_reliable_toggle_ == !outgoing_reliable_toggle_;
    } else if (plan.reliable_decision_ == ReliableTransmitDecision::retransmit) {
        reliable_bytes_match = in_flight_reliable_payload_ &&
                               plan.reliable_payload_size_ ==
                                   in_flight_reliable_payload_->bytes.size() &&
                               std::ranges::equal(
                                   in_flight_reliable_payload_->bytes,
                                   std::span<const std::byte>{packet.payload}.first(
                                       plan.reliable_payload_size_));
        toggle_matches = in_flight_reliable_payload_ &&
                         plan.planned_reliable_toggle_ ==
                             in_flight_reliable_payload_->toggle;
    }
    const bool padding_matches = sizes_match && std::ranges::all_of(
        packet.payload.begin() + static_cast<std::ptrdiff_t>(composed_size),
        packet.payload.end(),
        [](const std::byte value) {
            return value == kStockProtocol48NetchanPaddingByte;
        });
    if (plan.reliable_decision_ == ReliableTransmitDecision::retransmit &&
        in_flight_reliable_payload_ &&
        in_flight_reliable_payload_->send_count ==
            std::numeric_limits<std::uint64_t>::max()) {
        plan.consumable_ = false;
        return operation_failure(
            NetchanSessionErrorCode::reliable_send_count_overflow);
    }
    if (plan.reliable_decision_ != current_decision || !header_matches ||
        !sizes_match || !reliable_bytes_match || !toggle_matches ||
        !padding_matches) {
        plan.consumable_ = false;
        return operation_failure(
            NetchanSessionErrorCode::outgoing_transaction_mismatch);
    }
    return NetchanSessionOperationResult{};
}

NetchanSessionOperationResult NetchanSession::commit_outgoing_send(
    NetchanTransmitPlan&& plan) noexcept
{
    const auto validated = validate_outgoing_plan(plan);
    if (!validated) {
        return validated;
    }
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        plan.consumable_ = false;
        return operation_failure(NetchanSessionErrorCode::session_revision_overflow);
    }
    plan.consumable_ = false;

    const auto sent_sequence = state_.next_outgoing_sequence;
    if (plan.reliable_decision_ == ReliableTransmitDecision::send_new) {
        outgoing_reliable_toggle_ = plan.planned_reliable_toggle_;
        in_flight_reliable_payload_.emplace(InFlightReliablePayload{
            std::move(pending_reliable_payload_),
            outgoing_reliable_toggle_,
            sent_sequence,
            sent_sequence,
            1U,
            false,
        });
        pending_reliable_payload_.clear();
    } else if (plan.reliable_decision_ == ReliableTransmitDecision::retransmit) {
        in_flight_reliable_payload_->most_recent_sent_sequence = sent_sequence;
        ++in_flight_reliable_payload_->send_count;
        in_flight_reliable_payload_->retransmission_requested = false;
    }

    state_.last_outgoing_sequence = sent_sequence;
    state_.next_outgoing_sequence = next_sequence(sent_sequence);
    ++revision_;
    return NetchanSessionOperationResult{};
}

NetchanSessionOperationResult NetchanSession::abandon_outgoing_packet(
    NetchanTransmitPlan&& plan) const noexcept
{
    if (plan.session_identity_ != identity_) {
        return operation_failure(
            NetchanSessionErrorCode::foreign_outgoing_transaction);
    }
    if (!plan.consumable_ || plan.session_revision_ != revision_) {
        plan.consumable_ = false;
        return operation_failure(NetchanSessionErrorCode::stale_outgoing_transaction);
    }
    plan.consumable_ = false;
    return NetchanSessionOperationResult{};
}

void NetchanSession::clear_reliable_state() noexcept
{
    std::vector<std::byte>{}.swap(pending_reliable_payload_);
    in_flight_reliable_payload_.reset();
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        valid_configuration_ = false;
        return;
    }
    ++revision_;
}

NetchanFirstAcknowledgementPrepareResult
NetchanSession::prepare_first_acknowledgement()
{
    if (!valid_configuration_) {
        return first_prepare_failure(NetchanSessionErrorCode::invalid_configuration);
    }
    if (!first_incoming_committed_) {
        return first_prepare_failure(
            NetchanSessionErrorCode::first_acknowledgement_before_incoming);
    }
    if (first_acknowledgement_prepared_ || first_acknowledgement_sent_) {
        return first_prepare_failure(
            NetchanSessionErrorCode::first_acknowledgement_already_prepared);
    }
    // Preparing the special first ACK consumes one revision and committing it
    // consumes another. Reject before either step if both increments cannot be
    // represented, so a caller never sends a transaction that cannot commit.
    if (revision_ >= std::numeric_limits<std::uint64_t>::max() - 1U) {
        return first_prepare_failure(
            NetchanSessionErrorCode::session_revision_overflow);
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
        NetchanFirstAcknowledgementTransaction{
            std::move(packet),
            identity_,
            revision_,
        },
        std::nullopt,
    };
}

NetchanSessionOperationResult NetchanSession::commit_first_acknowledgement(
    NetchanFirstAcknowledgementTransaction&& transaction) noexcept
{
    if (transaction.session_identity_ != identity_ || !transaction.consumable_ ||
        transaction.session_revision_ != revision_) {
        transaction.consumable_ = false;
        return operation_failure(
            NetchanSessionErrorCode::stale_first_acknowledgement_transaction);
    }
    transaction.consumable_ = false;
    if (!valid_configuration_) {
        return operation_failure(NetchanSessionErrorCode::invalid_configuration);
    }
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return operation_failure(NetchanSessionErrorCode::session_revision_overflow);
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
        no_fragments(packet.fragments) &&
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

const std::vector<std::byte>& NetchanSession::pending_reliable_payload() const noexcept
{
    return pending_reliable_payload_;
}

const std::optional<InFlightReliablePayload>&
NetchanSession::in_flight_reliable_payload() const noexcept
{
    return in_flight_reliable_payload_;
}

bool NetchanSession::outgoing_reliable_toggle() const noexcept
{
    return outgoing_reliable_toggle_;
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
