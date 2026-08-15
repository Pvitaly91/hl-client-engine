#pragma once

#include <hlclient/goldsrc/netchan_packet.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

// Confirmed stock Protocol 48 profile. Neither direction carries qport or a
// checksum after the two 32-bit header words. The project-only first ACK uses
// the observed eight-byte padding body and does not contain a sign-on command.
inline constexpr bool kStockProtocol48NetchanHasQport = false;
inline constexpr std::size_t kStockProtocol48MinimumDecodedPayloadSize = 8U;
inline constexpr std::byte kStockProtocol48NetchanPaddingByte{0x01};
inline constexpr std::size_t kDefaultMaximumUnfragmentedReliablePayload =
    kDefaultNetchanDatagramSize - kNetchanHeaderSize;
inline constexpr std::size_t kMaximumPendingReliablePayload =
    kMaximumNetchanDatagramSize - kNetchanHeaderSize;
// Project safety bounds for the deterministic outgoing normal-fragment mirror.
// They are not claims about a stock engine maximum. Fresh stock capture confirms
// the 1,024-byte non-final chunk and per-fragment stop-and-wait behavior.
inline constexpr std::size_t kMaximumOutgoingNormalFragmentTransferSize =
    kMaximumPendingReliablePayload;
inline constexpr std::size_t kMaximumOutgoingNormalFragments =
    (kMaximumOutgoingNormalFragmentTransferSize +
     kStockProtocol48NormalFragmentChunkSize - 1U) /
    kStockProtocol48NormalFragmentChunkSize;

struct NetchanSequenceState {
    NetchanSequence next_outgoing_sequence;
    NetchanSequence last_outgoing_sequence;
    NetchanSequence incoming_sequence;
    NetchanSequence peer_acknowledgement;
    bool incoming_reliable_acknowledgement{false};
    bool peer_reliable_acknowledgement{false};

    [[nodiscard]] static NetchanSequenceState stock_protocol48() noexcept;
};

struct NetchanSessionLimits {
    std::size_t maximum_datagram_size{kDefaultNetchanDatagramSize};
    std::size_t maximum_unfragmented_reliable_payload{
        kDefaultMaximumUnfragmentedReliablePayload};
    std::size_t maximum_pending_reliable_payload{
        kMaximumPendingReliablePayload};
};

enum class NetchanIncomingSequenceDisposition {
    newer,
    duplicate,
    older,
    half_range_ambiguous,
};

enum class NetchanAcknowledgementDisposition {
    advanced,
    duplicate,
    stale,
};

// A fragmented newer packet must be classified only after its strict
// descriptor/range has been transactionally prepared by the reassembler.
// Exact fragment retransmissions advance numeric channel observations without
// admitting/toggling the same reliable unit twice.
enum class NetchanIncomingReliableUnitClassification {
    unfragmented,
    new_fragment_unit,
    exact_fragment_retransmission,
};

struct NetchanAcknowledgementObservation {
    NetchanSequence sequence;
    bool reliable{false};
    NetchanAcknowledgementDisposition disposition{
        NetchanAcknowledgementDisposition::duplicate};
};

enum class ReliableTransmitDecision {
    none,
    send_new,
    retransmit,
    blocked_waiting_for_ack,
    send_new_fragment,
    retransmit_fragment,
    // Project wrap policy: consume a numeric sequence whose fragment flags
    // would collide with a connectionless/split classifier using an ordinary
    // padded ACK-only datagram, then retry the unchanged fragment work.
    advance_fragment_sequence,
};

struct NetchanReliableTransmitState {
    bool has_pending_payload{false};
    bool has_in_flight_payload{false};
    bool retransmission_requested{false};
    bool pending_requires_fragmentation{false};
};

// This helper is intentionally policy-neutral: the receive path sets
// retransmission_requested only after the stock ACK-gap condition is met.
[[nodiscard]] ReliableTransmitDecision decide_reliable_transmit(
    const NetchanReliableTransmitState& state) noexcept;

struct InFlightReliablePayload {
    std::vector<std::byte> bytes;
    bool toggle{false};
    NetchanSequence first_sent_sequence;
    NetchanSequence most_recent_sent_sequence;
    std::uint64_t send_count{0U};
    bool retransmission_requested{false};
};

class NetchanOutgoingFragmentTransferId final {
public:
    constexpr NetchanOutgoingFragmentTransferId() noexcept = default;

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return value_ != 0U;
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept
    {
        return value_;
    }

    friend constexpr bool operator==(
        const NetchanOutgoingFragmentTransferId& left,
        const NetchanOutgoingFragmentTransferId& right) noexcept = default;

private:
    friend class NetchanSession;

    explicit constexpr NetchanOutgoingFragmentTransferId(
        const std::uint64_t value) noexcept
        : value_{value}
    {
    }

    std::uint64_t value_{0U};
};

struct NetchanFragmentBuildPlan {
    NetchanOutgoingFragmentTransferId transfer_id;
    std::uint16_t fragment_index{0U};
    std::uint16_t fragment_count{0U};
    std::size_t canonical_offset{0U};
    std::size_t canonical_length{0U};
    bool retransmission{false};
};

struct NetchanOutgoingFragmentTransfer {
    NetchanOutgoingFragmentTransferId transfer_id;
    std::vector<std::byte> canonical_bytes;
    std::uint16_t fragment_count{0U};
    std::uint16_t current_fragment_index{0U};
};

enum class NetchanSessionErrorCode {
    invalid_configuration,
    future_acknowledgement,
    acknowledgement_half_range_ambiguous,
    incoming_not_newer,
    stale_incoming_inspection,
    first_incoming_already_committed,
    first_acknowledgement_before_incoming,
    first_acknowledgement_already_prepared,
    stale_first_acknowledgement_transaction,
    first_acknowledgement_transaction_mismatch,
    reliable_queue_overflow,
    unreliable_payload_does_not_fit,
    combined_payload_does_not_fit,
    stale_outgoing_transaction,
    foreign_outgoing_transaction,
    outgoing_transaction_mismatch,
    reliable_send_count_overflow,
    session_revision_overflow,
    foreign_incoming_inspection,
    fragment_reliable_classification_required,
    fragment_reliable_classification_mismatch,
    fragment_reliable_flag_required,
    outgoing_fragment_transfer_too_large,
    outgoing_fragment_count_overflow,
    outgoing_fragment_datagram_does_not_fit,
    outgoing_fragment_transfer_id_overflow,
};

struct NetchanSessionError {
    NetchanSessionErrorCode code{NetchanSessionErrorCode::invalid_configuration};
};

struct NetchanSessionOperationResult {
    std::optional<NetchanSessionError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

class NetchanSessionIdentity;

class NetchanIncomingInspection final {
public:
    NetchanIncomingInspection(const NetchanIncomingInspection&) = delete;
    NetchanIncomingInspection& operator=(const NetchanIncomingInspection&) = delete;
    NetchanIncomingInspection(NetchanIncomingInspection&& other) noexcept;
    NetchanIncomingInspection& operator=(NetchanIncomingInspection&& other) noexcept;

    [[nodiscard]] NetchanIncomingSequenceDisposition disposition() const noexcept
    {
        return disposition_;
    }

    [[nodiscard]] const NetchanHeader& header() const noexcept
    {
        return header_;
    }

    [[nodiscard]] std::uint32_t skipped_sequences() const noexcept
    {
        return skipped_sequences_;
    }

    [[nodiscard]] const std::optional<NetchanAcknowledgementObservation>&
    acknowledgement() const noexcept
    {
        return acknowledgement_;
    }

    [[nodiscard]] bool should_commit() const noexcept
    {
        return disposition_ == NetchanIncomingSequenceDisposition::newer;
    }

    // Sequence bit 31 is reliable-byte presence. A committed newer packet
    // toggles the receiver's independent reliable ACK generation only when a
    // validated caller classification admits it as a new reliable unit; an
    // exact fragment retransmission preserves the current generation.
    [[nodiscard]] bool contains_new_reliable_data() const noexcept
    {
        return should_commit() && contains_new_reliable_data_;
    }

    [[nodiscard]] NetchanIncomingReliableUnitClassification
    reliable_unit_classification() const noexcept
    {
        return reliable_unit_classification_;
    }

    [[nodiscard]] bool incoming_reliable_acknowledgement_after_commit() const noexcept
    {
        return incoming_reliable_acknowledgement_after_commit_;
    }

private:
    friend class NetchanSession;

    NetchanIncomingInspection(
        NetchanHeader header,
        NetchanIncomingSequenceDisposition disposition,
        std::uint32_t skipped_sequences,
        std::optional<NetchanAcknowledgementObservation> acknowledgement,
        NetchanIncomingReliableUnitClassification reliable_unit_classification,
        bool contains_new_reliable_data,
        bool incoming_reliable_acknowledgement_after_commit,
        std::shared_ptr<const NetchanSessionIdentity> session_identity,
        std::uint64_t session_revision) noexcept;

    NetchanHeader header_;
    NetchanIncomingSequenceDisposition disposition_;
    std::uint32_t skipped_sequences_{0U};
    std::optional<NetchanAcknowledgementObservation> acknowledgement_;
    NetchanIncomingReliableUnitClassification reliable_unit_classification_{
        NetchanIncomingReliableUnitClassification::unfragmented};
    bool contains_new_reliable_data_{false};
    bool incoming_reliable_acknowledgement_after_commit_{false};
    std::shared_ptr<const NetchanSessionIdentity> session_identity_;
    std::uint64_t session_revision_{0U};
    bool consumable_{true};
};

struct NetchanIncomingInspectResult {
    std::optional<NetchanIncomingInspection> inspection;
    std::optional<NetchanSessionError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return inspection.has_value();
    }
};

class NetchanFirstAcknowledgementTransaction final {
public:
    NetchanFirstAcknowledgementTransaction(
        const NetchanFirstAcknowledgementTransaction&) = delete;
    NetchanFirstAcknowledgementTransaction& operator=(
        const NetchanFirstAcknowledgementTransaction&) = delete;
    NetchanFirstAcknowledgementTransaction(
        NetchanFirstAcknowledgementTransaction&& other) noexcept;
    NetchanFirstAcknowledgementTransaction& operator=(
        NetchanFirstAcknowledgementTransaction&& other) noexcept;

    [[nodiscard]] const ClientToServerNetchanPacket& packet() const noexcept
    {
        return packet_;
    }

private:
    friend class NetchanSession;

    NetchanFirstAcknowledgementTransaction(
        ClientToServerNetchanPacket packet,
        std::shared_ptr<const NetchanSessionIdentity> session_identity,
        std::uint64_t session_revision) noexcept;

    ClientToServerNetchanPacket packet_;
    std::shared_ptr<const NetchanSessionIdentity> session_identity_;
    std::uint64_t session_revision_{0U};
    bool consumable_{true};
};

struct NetchanFirstAcknowledgementPrepareResult {
    std::optional<NetchanFirstAcknowledgementTransaction> transaction;
    std::optional<NetchanSessionError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return transaction.has_value();
    }
};

class NetchanTransmitPlan final {
public:
    NetchanTransmitPlan(const NetchanTransmitPlan&) = delete;
    NetchanTransmitPlan& operator=(const NetchanTransmitPlan&) = delete;
    NetchanTransmitPlan(NetchanTransmitPlan&& other) noexcept;
    NetchanTransmitPlan& operator=(NetchanTransmitPlan&& other) noexcept;
    ~NetchanTransmitPlan() = default;

    [[nodiscard]] const ClientToServerNetchanPacket& packet() const noexcept
    {
        return packet_;
    }

    [[nodiscard]] ReliableTransmitDecision reliable_decision() const noexcept
    {
        return reliable_decision_;
    }

    [[nodiscard]] std::size_t reliable_payload_size() const noexcept
    {
        return reliable_payload_size_;
    }

    [[nodiscard]] std::size_t unreliable_payload_size() const noexcept
    {
        return unreliable_payload_size_;
    }

    [[nodiscard]] const std::optional<NetchanFragmentBuildPlan>&
    fragment_plan() const noexcept
    {
        return fragment_plan_;
    }

private:
    friend class NetchanSession;

    NetchanTransmitPlan(
        ClientToServerNetchanPacket packet,
        ReliableTransmitDecision reliable_decision,
        std::size_t reliable_payload_size,
        std::size_t unreliable_payload_size,
        bool planned_reliable_toggle,
        std::optional<NetchanFragmentBuildPlan> fragment_plan,
        std::vector<std::byte> planned_reliable_bytes,
        std::shared_ptr<const NetchanSessionIdentity> session_identity,
        std::uint64_t session_revision) noexcept;

    ClientToServerNetchanPacket packet_;
    ReliableTransmitDecision reliable_decision_{ReliableTransmitDecision::none};
    std::size_t reliable_payload_size_{0U};
    std::size_t unreliable_payload_size_{0U};
    bool planned_reliable_toggle_{false};
    std::optional<NetchanFragmentBuildPlan> fragment_plan_;
    // Owning canonical range reserved during prepare so fragment send commit
    // remains allocation-free and noexcept.
    std::vector<std::byte> planned_reliable_bytes_;
    std::shared_ptr<const NetchanSessionIdentity> session_identity_;
    std::uint64_t session_revision_{0U};
    bool consumable_{true};
};

struct NetchanTransmitPrepareResult {
    std::optional<NetchanTransmitPlan> plan;
    std::optional<NetchanSessionError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return plan.has_value();
    }
};

// Persistent, transport-independent state on the confirmed Protocol 48 base
// wire profile. Callers decode and validate a datagram first, then use
// inspect/commit for atomic receive state. Outgoing prepare is read-only;
// callers encode/send the returned packet and commit only after send succeeds.
// Reliable-prefix/unreliable-suffix composition is a deterministic project
// policy pending direct stock isolation of that otherwise opaque boundary.
class NetchanSession final {
public:
    explicit NetchanSession(
        NetchanSequenceState initial_state =
            NetchanSequenceState::stock_protocol48(),
        NetchanSessionLimits limits = {});
    explicit NetchanSession(NetchanSessionLimits limits);

    NetchanSession(const NetchanSession&) = delete;
    NetchanSession& operator=(const NetchanSession&) = delete;
    NetchanSession(NetchanSession&&) = delete;
    NetchanSession& operator=(NetchanSession&&) = delete;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const NetchanSessionLimits& limits() const noexcept;

    [[nodiscard]] NetchanIncomingInspectResult inspect_incoming(
        const NetchanHeader& header,
        NetchanIncomingReliableUnitClassification classification =
            NetchanIncomingReliableUnitClassification::unfragmented) const noexcept;

    [[nodiscard]] NetchanSessionOperationResult commit_incoming(
        NetchanIncomingInspection&& inspection) noexcept;

    [[nodiscard]] NetchanSessionOperationResult queue_reliable(
        std::span<const std::byte> payload);

    [[nodiscard]] NetchanTransmitPrepareResult prepare_outgoing_packet(
        std::span<const std::byte> unreliable_payload = {}) const;

    [[nodiscard]] NetchanSessionOperationResult commit_outgoing_send(
        NetchanTransmitPlan&& plan) noexcept;

    [[nodiscard]] NetchanSessionOperationResult abandon_outgoing_packet(
        NetchanTransmitPlan&& plan) const noexcept;

    void clear_reliable_state() noexcept;

    // M2.3.1 compatibility path for its byte-exact padded first transport ACK.
    [[nodiscard]] NetchanFirstAcknowledgementPrepareResult
    prepare_first_acknowledgement();

    [[nodiscard]] NetchanSessionOperationResult commit_first_acknowledgement(
        NetchanFirstAcknowledgementTransaction&& transaction) noexcept;

    [[nodiscard]] const NetchanSequenceState& state() const noexcept;
    [[nodiscard]] const std::vector<std::byte>& pending_reliable_payload() const noexcept;
    [[nodiscard]] const std::optional<InFlightReliablePayload>&
    in_flight_reliable_payload() const noexcept;
    [[nodiscard]] bool outgoing_reliable_toggle() const noexcept;
    [[nodiscard]] const std::optional<NetchanOutgoingFragmentTransfer>&
    outgoing_fragment_transfer() const noexcept;
    [[nodiscard]] bool has_outgoing_fragment_send_work() const noexcept;
    [[nodiscard]] bool first_incoming_committed() const noexcept;
    [[nodiscard]] bool first_acknowledgement_prepared() const noexcept;
    [[nodiscard]] bool first_acknowledgement_sent() const noexcept;

private:
    [[nodiscard]] NetchanSessionOperationResult validate_outgoing_plan(
        NetchanTransmitPlan& plan) const noexcept;

    NetchanSequenceState state_;
    NetchanSessionLimits limits_;
    std::vector<std::byte> pending_reliable_payload_;
    std::optional<InFlightReliablePayload> in_flight_reliable_payload_;
    std::optional<NetchanOutgoingFragmentTransfer> outgoing_fragment_transfer_;
    std::shared_ptr<const NetchanSessionIdentity> identity_;
    bool outgoing_reliable_toggle_{false};
    bool valid_configuration_{false};
    bool first_incoming_committed_{false};
    bool first_acknowledgement_prepared_{false};
    bool first_acknowledgement_sent_{false};
    std::uint64_t revision_{0U};
    std::uint64_t next_outgoing_fragment_transfer_id_{1U};
};

[[nodiscard]] constexpr std::string_view to_string(
    const ReliableTransmitDecision decision) noexcept
{
    switch (decision) {
    case ReliableTransmitDecision::none:
        return "none";
    case ReliableTransmitDecision::send_new:
        return "send_new";
    case ReliableTransmitDecision::retransmit:
        return "retransmit";
    case ReliableTransmitDecision::blocked_waiting_for_ack:
        return "blocked_waiting_for_ack";
    case ReliableTransmitDecision::send_new_fragment:
        return "send_new_fragment";
    case ReliableTransmitDecision::retransmit_fragment:
        return "retransmit_fragment";
    case ReliableTransmitDecision::advance_fragment_sequence:
        return "advance_fragment_sequence";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const NetchanSessionErrorCode code) noexcept
{
    switch (code) {
    case NetchanSessionErrorCode::invalid_configuration:
        return "invalid_configuration";
    case NetchanSessionErrorCode::future_acknowledgement:
        return "future_acknowledgement";
    case NetchanSessionErrorCode::acknowledgement_half_range_ambiguous:
        return "acknowledgement_half_range_ambiguous";
    case NetchanSessionErrorCode::incoming_not_newer:
        return "incoming_not_newer";
    case NetchanSessionErrorCode::stale_incoming_inspection:
        return "stale_incoming_inspection";
    case NetchanSessionErrorCode::first_incoming_already_committed:
        return "first_incoming_already_committed";
    case NetchanSessionErrorCode::first_acknowledgement_before_incoming:
        return "first_acknowledgement_before_incoming";
    case NetchanSessionErrorCode::first_acknowledgement_already_prepared:
        return "first_acknowledgement_already_prepared";
    case NetchanSessionErrorCode::stale_first_acknowledgement_transaction:
        return "stale_first_acknowledgement_transaction";
    case NetchanSessionErrorCode::first_acknowledgement_transaction_mismatch:
        return "first_acknowledgement_transaction_mismatch";
    case NetchanSessionErrorCode::reliable_queue_overflow:
        return "reliable_queue_overflow";
    case NetchanSessionErrorCode::unreliable_payload_does_not_fit:
        return "unreliable_payload_does_not_fit";
    case NetchanSessionErrorCode::combined_payload_does_not_fit:
        return "combined_payload_does_not_fit";
    case NetchanSessionErrorCode::stale_outgoing_transaction:
        return "stale_outgoing_transaction";
    case NetchanSessionErrorCode::foreign_outgoing_transaction:
        return "foreign_outgoing_transaction";
    case NetchanSessionErrorCode::outgoing_transaction_mismatch:
        return "outgoing_transaction_mismatch";
    case NetchanSessionErrorCode::reliable_send_count_overflow:
        return "reliable_send_count_overflow";
    case NetchanSessionErrorCode::session_revision_overflow:
        return "session_revision_overflow";
    case NetchanSessionErrorCode::foreign_incoming_inspection:
        return "foreign_incoming_inspection";
    case NetchanSessionErrorCode::fragment_reliable_classification_required:
        return "fragment_reliable_classification_required";
    case NetchanSessionErrorCode::fragment_reliable_classification_mismatch:
        return "fragment_reliable_classification_mismatch";
    case NetchanSessionErrorCode::fragment_reliable_flag_required:
        return "fragment_reliable_flag_required";
    case NetchanSessionErrorCode::outgoing_fragment_transfer_too_large:
        return "outgoing_fragment_transfer_too_large";
    case NetchanSessionErrorCode::outgoing_fragment_count_overflow:
        return "outgoing_fragment_count_overflow";
    case NetchanSessionErrorCode::outgoing_fragment_datagram_does_not_fit:
        return "outgoing_fragment_datagram_does_not_fit";
    case NetchanSessionErrorCode::outgoing_fragment_transfer_id_overflow:
        return "outgoing_fragment_transfer_id_overflow";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
