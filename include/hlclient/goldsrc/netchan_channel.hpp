#pragma once

#include <hlclient/goldsrc/netchan_packet.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

// Stock Protocol 48 capture has no qport/checksum after the two 32-bit words.
// A decoded packet shorter than 16 bytes is padded after the 8-byte header with
// capture-confirmed 0x01 bytes. M2.3 assigns no message-level meaning to them.
inline constexpr bool kStockProtocol48NetchanHasQport = false;
inline constexpr std::size_t kStockProtocol48MinimumDecodedPayloadSize = 8U;
inline constexpr std::byte kStockProtocol48NetchanPaddingByte{0x01};
inline constexpr std::size_t kDefaultNetchanChannelPayloadSize =
    kDefaultNetchanDatagramSize - kNetchanHeaderSize;
inline constexpr std::size_t kMaximumNetchanChannelPayloadSize =
    kMaximumNetchanDatagramSize - kNetchanHeaderSize;

struct NetchanInitialState {
    NetchanSequence next_outgoing_sequence;
    NetchanSequence last_outgoing_sequence;
    NetchanSequence incoming_sequence;
    NetchanSequence peer_acknowledgement;
    bool outgoing_reliable_toggle{false};
    bool incoming_reliable_toggle{false};
    bool peer_reliable_acknowledgement{false};

    [[nodiscard]] static NetchanInitialState stock_protocol48() noexcept;
};

struct NetchanChannelLimits {
    std::size_t maximum_reliable_payload_size{kDefaultNetchanChannelPayloadSize};
    std::size_t maximum_packet_payload_size{kDefaultNetchanChannelPayloadSize};
};

enum class NetchanChannelErrorCode {
    invalid_configuration,
    empty_reliable_payload,
    reliable_payload_too_large,
    reliable_slot_occupied,
    outgoing_payload_too_large,
    stale_outgoing_transaction,
    outgoing_transaction_mismatch,
    future_acknowledgement,
    acknowledgement_half_range_ambiguous,
    invalid_reliable_acknowledgement_toggle,
};

struct NetchanChannelError {
    NetchanChannelErrorCode code{NetchanChannelErrorCode::invalid_configuration};
};

struct NetchanChannelOperationResult {
    std::optional<NetchanChannelError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

enum class NetchanAcknowledgementDisposition {
    advanced,
    advanced_reliable_confirmed,
    advanced_reliable_unconfirmed,
    duplicate,
    stale,
};

enum class NetchanIncomingSequenceDisposition {
    newer,
    duplicate,
    older,
    half_range_ambiguous,
};

// Fragment/reliable ownership is decided above this pure channel. A fragment
// reassembler can identify a new reliable unit versus a byte-identical retry
// sent under a fresh numeric sequence without exposing sign-on semantics here.
enum class NetchanIncomingReliableUnit {
    none,
    new_unit,
    exact_retransmission,
};

class NetchanOutgoingTransaction final {
public:
    [[nodiscard]] const ClientToServerNetchanPacket& packet() const noexcept
    {
        return packet_;
    }

    [[nodiscard]] std::size_t reliable_payload_size() const noexcept
    {
        return reliable_payload_size_;
    }

    [[nodiscard]] std::size_t unreliable_payload_size() const noexcept
    {
        return unreliable_payload_size_;
    }

    [[nodiscard]] std::size_t padding_size() const noexcept
    {
        return padding_size_;
    }

    [[nodiscard]] bool is_reliable_retransmission() const noexcept
    {
        return reliable_retransmission_;
    }

    [[nodiscard]] bool expected_reliable_acknowledgement() const noexcept
    {
        return expected_reliable_acknowledgement_;
    }

private:
    friend class NetchanChannel;

    NetchanOutgoingTransaction(
        ClientToServerNetchanPacket packet,
        std::size_t reliable_payload_size,
        std::size_t unreliable_payload_size,
        std::size_t padding_size,
        bool reliable_retransmission,
        bool expected_reliable_acknowledgement,
        std::uint64_t channel_revision,
        std::uint64_t reliable_generation) noexcept;

    ClientToServerNetchanPacket packet_;
    std::size_t reliable_payload_size_{0U};
    std::size_t unreliable_payload_size_{0U};
    std::size_t padding_size_{0U};
    bool reliable_retransmission_{false};
    bool expected_reliable_acknowledgement_{false};
    std::uint64_t channel_revision_{0U};
    std::uint64_t reliable_generation_{0U};
};

struct NetchanOutgoingPrepareResult {
    std::optional<NetchanOutgoingTransaction> transaction;
    std::optional<NetchanChannelError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return transaction.has_value();
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

    [[nodiscard]] std::optional<NetchanAcknowledgementDisposition>
    acknowledgement() const noexcept
    {
        return acknowledgement_;
    }

    [[nodiscard]] bool should_commit() const noexcept
    {
        return disposition_ == NetchanIncomingSequenceDisposition::newer;
    }

    [[nodiscard]] bool reliable_resolution_required() const noexcept
    {
        return should_commit() && header_.sequence.flags.reliable;
    }

private:
    friend class NetchanChannel;

    NetchanIncomingInspection(
        NetchanHeader header,
        NetchanIncomingSequenceDisposition disposition,
        std::uint32_t skipped_sequences,
        std::optional<NetchanAcknowledgementDisposition> acknowledgement,
        bool advance_acknowledgement,
        bool clear_reliable,
        std::uint64_t channel_revision) noexcept;

    NetchanHeader header_;
    NetchanIncomingSequenceDisposition disposition_;
    std::uint32_t skipped_sequences_{0U};
    std::optional<NetchanAcknowledgementDisposition> acknowledgement_;
    bool advance_acknowledgement_{false};
    bool clear_reliable_{false};
    bool consumable_{true};
    std::uint64_t channel_revision_{0U};
};

struct NetchanIncomingInspectResult {
    std::optional<NetchanIncomingInspection> inspection;
    std::optional<NetchanChannelError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return inspection.has_value();
    }
};

class NetchanChannel final {
public:
    explicit NetchanChannel(
        NetchanChannelLimits limits = {},
        NetchanInitialState initial_state = NetchanInitialState::stock_protocol48());

    [[nodiscard]] bool valid_configuration() const noexcept;

    [[nodiscard]] NetchanChannelOperationResult queue_reliable_payload(
        std::span<const std::byte> payload);

    [[nodiscard]] bool has_reliable_payload() const noexcept;
    [[nodiscard]] bool has_reliable_in_flight() const noexcept;
    [[nodiscard]] std::size_t reliable_payload_size() const noexcept;

    [[nodiscard]] NetchanOutgoingPrepareResult prepare_outgoing(
        std::span<const std::byte> unreliable_payload = {}) const;

    // Commit only after codec and transport success. Discarding a prepared
    // transaction leaves sequence and reliable state unchanged.
    [[nodiscard]] NetchanChannelOperationResult commit_outgoing(
        const NetchanOutgoingTransaction& transaction);

    [[nodiscard]] NetchanIncomingInspectResult inspect_incoming(
        const NetchanHeader& header) const noexcept;

    // Inspect validates sequence and acknowledgement without mutation. The
    // caller skips codec/reassembly when should_commit() is false. After all
    // downstream work succeeds this operation only applies the prepared token;
    // it has no late validation/failure path. No scheduler lives here: retries
    // happen only when the caller explicitly prepares and commits another packet.
    void commit_incoming(
        NetchanIncomingInspection&& inspection,
        NetchanIncomingReliableUnit reliable_unit) noexcept;

    [[nodiscard]] NetchanSequence next_outgoing_sequence() const noexcept;
    [[nodiscard]] NetchanSequence last_outgoing_sequence() const noexcept;
    [[nodiscard]] NetchanSequence incoming_sequence() const noexcept;
    [[nodiscard]] NetchanSequence peer_acknowledgement() const noexcept;
    [[nodiscard]] bool outgoing_reliable_toggle() const noexcept;
    [[nodiscard]] bool incoming_reliable_toggle() const noexcept;
    [[nodiscard]] bool peer_reliable_acknowledgement() const noexcept;

private:
    struct ReliablePayloadState {
        std::vector<std::byte> bytes;
        bool acknowledgement_toggle{false};
        std::optional<NetchanSequence> first_sent_sequence;
        std::optional<NetchanSequence> last_sent_sequence;
        std::uint64_t generation{0U};
    };

    struct AcknowledgementEvaluation {
        std::optional<NetchanAcknowledgementDisposition> disposition;
        std::optional<NetchanChannelError> error;
        bool advance{false};
        bool clear_reliable{false};
    };

    [[nodiscard]] AcknowledgementEvaluation evaluate_acknowledgement(
        const NetchanAcknowledgementWord& acknowledgement) const noexcept;

    NetchanChannelLimits limits_;
    bool valid_configuration_{false};
    NetchanSequence next_outgoing_sequence_;
    NetchanSequence last_outgoing_sequence_;
    NetchanSequence incoming_sequence_;
    NetchanSequence peer_acknowledgement_;
    bool outgoing_reliable_toggle_{false};
    bool incoming_reliable_toggle_{false};
    bool peer_reliable_acknowledgement_{false};
    std::optional<ReliablePayloadState> reliable_;
    std::uint64_t revision_{0U};
    std::uint64_t next_reliable_generation_{1U};
};

[[nodiscard]] constexpr std::string_view to_string(
    const NetchanChannelErrorCode code) noexcept
{
    switch (code) {
    case NetchanChannelErrorCode::invalid_configuration:
        return "invalid_configuration";
    case NetchanChannelErrorCode::empty_reliable_payload:
        return "empty_reliable_payload";
    case NetchanChannelErrorCode::reliable_payload_too_large:
        return "reliable_payload_too_large";
    case NetchanChannelErrorCode::reliable_slot_occupied:
        return "reliable_slot_occupied";
    case NetchanChannelErrorCode::outgoing_payload_too_large:
        return "outgoing_payload_too_large";
    case NetchanChannelErrorCode::stale_outgoing_transaction:
        return "stale_outgoing_transaction";
    case NetchanChannelErrorCode::outgoing_transaction_mismatch:
        return "outgoing_transaction_mismatch";
    case NetchanChannelErrorCode::future_acknowledgement:
        return "future_acknowledgement";
    case NetchanChannelErrorCode::acknowledgement_half_range_ambiguous:
        return "acknowledgement_half_range_ambiguous";
    case NetchanChannelErrorCode::invalid_reliable_acknowledgement_toggle:
        return "invalid_reliable_acknowledgement_toggle";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
