#pragma once

#include <hlclient/goldsrc/reference_client_move.hpp>
#include <hlclient/goldsrc/usercmd_transmission_stage.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace hlclient::goldsrc {

// One actually transmitted packet, reconstructed immediately from the owning
// transmission event and its committed immutable history. No packet bytes.
struct ReferenceSentCommand final {
    GoldSrcUserCmdSequence identity;
    GoldSrcWireUserCmd value;
    bool backup{false};
};

struct ReferenceSentCarrier final {
    std::uint64_t generation{0U};
    std::uint32_t outgoing_sequence{0U};
    std::uint64_t history_revision{0U};
    std::vector<ReferenceSentCommand> commands;
    std::size_t backup_count{0U};
    std::size_t new_count{0U};
};

struct ReferenceClientdataCarrier final {
    std::uint64_t generation{0U};
    std::uint64_t record_identity{0U};
    std::uint32_t source_sequence{0U};
    std::optional<std::uint32_t> acknowledgement;
    bool fresh_clientdata{false};
    bool source_reliable{false};
    bool reassembled{false};
};

enum class ReferencePredictionAnchorStatus : std::uint8_t {
    bound,
    invalid_receipt,
    duplicate_or_old_receipt,
    history_missing,
    invalid_record,
    generation_mismatch,
    stale_or_duplicate_record,
    ambiguous_sequence,
    old_body_or_reassembly,
    sent_carrier_missing,
    carrier_without_new_move,
};

struct ReferencePredictionAnchorResult final {
    ReferencePredictionAnchorStatus status{
        ReferencePredictionAnchorStatus::invalid_record};
    std::optional<GoldSrcUserCmdSequence> last_new_command;
    std::optional<GoldSrcWireUserCmd> last_new_value;
    std::optional<std::uint32_t> outgoing_sequence;
    std::uint64_t history_revision{0U};
    std::uint64_t generation{0U};
    std::uint64_t source_record_identity{0U};
    std::uint32_t source_sequence{0U};
    // This is a reference-derived carrier/history boundary. It is never a
    // direct observation of the server's internal command execution.
    [[nodiscard]] bool bound() const noexcept {
        return status == ReferencePredictionAnchorStatus::bound &&
            last_new_command.has_value() && last_new_value.has_value() &&
            generation != 0U && source_record_identity != 0U;
    }
};

class ReferencePredictionCarrierLedger final {
public:
    explicit ReferencePredictionCarrierLedger(std::size_t maximum_carriers = 128U)
        : maximum_carriers_{maximum_carriers} {}

    [[nodiscard]] ReferencePredictionAnchorStatus record_sent(
        const GoldSrcUserCmdTransmissionEvent& event,
        const GoldSrcUserCmdHistoryState& history);
    [[nodiscard]] ReferencePredictionAnchorResult bind_clientdata(
        const ReferenceClientdataCarrier& record) noexcept;
    [[nodiscard]] const std::vector<ReferenceSentCarrier>& carriers() const noexcept {
        return carriers_;
    }

private:
    std::size_t maximum_carriers_{128U};
    std::vector<ReferenceSentCarrier> carriers_;
    std::optional<std::uint64_t> generation_;
    std::optional<std::uint32_t> last_sent_sequence_;
    std::optional<std::uint32_t> last_seen_server_sequence_;
    std::optional<std::uint64_t> last_seen_record_identity_;
};

} // namespace hlclient::goldsrc
