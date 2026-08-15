#pragma once

#include <hlclient/goldsrc/netchan_packet.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::goldsrc {

// Confirmed stock Protocol 48 profile. Neither direction carries qport or a
// checksum after the two 32-bit header words. The project-only first ACK uses
// the observed eight-byte padding body and does not contain a sign-on command.
inline constexpr bool kStockProtocol48NetchanHasQport = false;
inline constexpr std::size_t kStockProtocol48MinimumDecodedPayloadSize = 8U;
inline constexpr std::byte kStockProtocol48NetchanPaddingByte{0x01};

struct NetchanSequenceState {
    NetchanSequence next_outgoing_sequence;
    NetchanSequence last_outgoing_sequence;
    NetchanSequence incoming_sequence;
    NetchanSequence peer_acknowledgement;
    bool incoming_reliable_acknowledgement{false};
    bool peer_reliable_acknowledgement{false};

    [[nodiscard]] static NetchanSequenceState stock_protocol48() noexcept;
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

// M2.3.1 observes acknowledgement metadata but deliberately owns no reliable
// send buffer and performs no reliable-buffer clearing.
struct NetchanAcknowledgementObservation {
    NetchanSequence sequence;
    bool reliable{false};
    NetchanAcknowledgementDisposition disposition{
        NetchanAcknowledgementDisposition::duplicate};
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

private:
    friend class NetchanSession;

    NetchanIncomingInspection(
        NetchanHeader header,
        NetchanIncomingSequenceDisposition disposition,
        std::uint32_t skipped_sequences,
        std::optional<NetchanAcknowledgementObservation> acknowledgement,
        std::uint64_t session_revision) noexcept;

    NetchanHeader header_;
    NetchanIncomingSequenceDisposition disposition_;
    std::uint32_t skipped_sequences_{0U};
    std::optional<NetchanAcknowledgementObservation> acknowledgement_;
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
        std::uint64_t session_revision) noexcept;

    ClientToServerNetchanPacket packet_;
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

// A deliberately one-shot M2.3.1 session. It admits one first server packet,
// observes its sequence/acknowledgement metadata, and prepares exactly one
// transport ACK. Reliable queues, retransmission, and fragmentation state are
// intentionally deferred to M2.3.2/M2.3.3.
class NetchanSession final {
public:
    explicit NetchanSession(
        NetchanSequenceState initial_state =
            NetchanSequenceState::stock_protocol48()) noexcept;

    NetchanSession(const NetchanSession&) = delete;
    NetchanSession& operator=(const NetchanSession&) = delete;
    NetchanSession(NetchanSession&&) = delete;
    NetchanSession& operator=(NetchanSession&&) = delete;

    [[nodiscard]] bool valid_configuration() const noexcept;

    [[nodiscard]] NetchanIncomingInspectResult inspect_incoming(
        const NetchanHeader& header) const noexcept;

    [[nodiscard]] NetchanSessionOperationResult commit_incoming(
        NetchanIncomingInspection&& inspection) noexcept;

    [[nodiscard]] NetchanFirstAcknowledgementPrepareResult
    prepare_first_acknowledgement();

    [[nodiscard]] NetchanSessionOperationResult commit_first_acknowledgement(
        NetchanFirstAcknowledgementTransaction&& transaction) noexcept;

    [[nodiscard]] const NetchanSequenceState& state() const noexcept;
    [[nodiscard]] bool first_incoming_committed() const noexcept;
    [[nodiscard]] bool first_acknowledgement_prepared() const noexcept;
    [[nodiscard]] bool first_acknowledgement_sent() const noexcept;

private:
    NetchanSequenceState state_;
    bool valid_configuration_{false};
    bool first_incoming_committed_{false};
    bool first_acknowledgement_prepared_{false};
    bool first_acknowledgement_sent_{false};
    std::uint64_t revision_{0U};
};

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
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
