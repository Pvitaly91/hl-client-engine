#pragma once

#include <hlclient/goldsrc/netchan_packet.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

using NetchanFragmentClock = std::chrono::steady_clock;
using NetchanFragmentTimePoint = NetchanFragmentClock::time_point;

// Project safety limits, not claims about stock engine maxima. The supported
// stock profile itself uses 1,024-byte non-final normal chunks.
inline constexpr std::size_t kDefaultMaximumNetchanFragmentPayloadSize =
    kStockProtocol48NormalFragmentChunkSize;
inline constexpr std::size_t kMaximumNetchanFragmentPayloadSize =
    kStockProtocol48NormalFragmentChunkSize;
inline constexpr std::size_t kDefaultMaximumNetchanNormalTransferSize = 65'536U;
inline constexpr std::size_t kMaximumNetchanNormalTransferSize = 1'048'576U;
inline constexpr std::size_t kDefaultMaximumNetchanFragmentsPerTransfer = 64U;
inline constexpr std::size_t kMaximumNetchanFragmentsPerTransfer = 1'024U;
inline constexpr std::size_t kDefaultMaximumActiveNormalTransfers = 1U;
inline constexpr std::size_t kMaximumActiveNormalTransfers = 1U;
inline constexpr std::size_t kDefaultMaximumNetchanFragmentRanges = 64U;
inline constexpr std::size_t kMaximumNetchanFragmentRanges = 1'024U;
inline constexpr std::chrono::milliseconds kDefaultNetchanFragmentTimeout{5'000};
inline constexpr std::chrono::milliseconds kMaximumNetchanFragmentTimeout{30'000};

struct NetchanReassemblyLimits {
    std::size_t maximum_fragment_payload_size{
        kDefaultMaximumNetchanFragmentPayloadSize};
    std::size_t maximum_normal_transfer_size{
        kDefaultMaximumNetchanNormalTransferSize};
    std::size_t maximum_fragments_per_transfer{
        kDefaultMaximumNetchanFragmentsPerTransfer};
    std::size_t maximum_active_normal_transfers{
        kDefaultMaximumActiveNormalTransfers};
    std::size_t maximum_fragment_ranges{kDefaultMaximumNetchanFragmentRanges};
    std::chrono::milliseconds fragment_timeout{kDefaultNetchanFragmentTimeout};
};

// A channel-local owning-state token. It is never serialized and deliberately
// does not pretend that the changing packed index/count field is a stable wire
// transfer identifier.
class NetchanNormalTransferId final {
public:
    constexpr NetchanNormalTransferId() noexcept = default;

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return value_ != 0U;
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept
    {
        return value_;
    }

    friend constexpr bool operator==(
        const NetchanNormalTransferId& left,
        const NetchanNormalTransferId& right) noexcept = default;

private:
    friend class NetchanNormalReassembler;
    friend class NetchanFragmentInsertPlan;

    explicit constexpr NetchanNormalTransferId(const std::uint64_t value) noexcept
        : value_{value}
    {
    }

    std::uint64_t value_{0U};
};

struct NetchanFragmentRange {
    std::size_t transfer_offset{0U};
    std::size_t length{0U};
    std::uint16_t fragment_index{0U};

    friend bool operator==(
        const NetchanFragmentRange& left,
        const NetchanFragmentRange& right) noexcept = default;
};

struct NetchanNormalTransferState {
    NetchanNormalTransferId transfer_id;
    std::uint16_t declared_fragment_count{0U};
    std::size_t received_fragment_count{0U};
    std::size_t covered_size{0U};
    std::optional<std::size_t> total_size;
    NetchanFragmentTimePoint created_at{};
    NetchanFragmentTimePoint last_fragment_at{};
    NetchanFragmentTimePoint deadline{};
};

enum class NetchanFragmentInsertDisposition {
    transfer_started,
    inserted,
    exact_duplicate,
    completed,
};

struct NetchanTransferCompletion {
    NetchanNormalTransferId transfer_id;
    std::vector<std::byte> payload;
};

struct NetchanFragmentReceipt {
    NetchanFragmentInsertDisposition disposition{
        NetchanFragmentInsertDisposition::inserted};
    NetchanNormalTransferId transfer_id;
    NetchanFragmentRange range;
    std::size_t covered_size{0U};
    bool started_transfer{false};
    std::size_t required_event_count{0U};
    std::optional<std::size_t> completion_size;
};

enum class NetchanReassemblyErrorCode {
    invalid_configuration,
    unsupported_fragment_stream,
    secondary_stream_pending_m3,
    invalid_packed_fragment_id,
    invalid_fragment_count,
    invalid_fragment_index,
    invalid_fragment_offset,
    invalid_fragment_length,
    fragment_range_overflow,
    fragment_range_out_of_bounds,
    fragment_payload_size_mismatch,
    normal_transfer_too_large,
    too_many_fragments,
    too_many_ranges,
    unexpected_fragment_order,
    conflicting_duplicate,
    partial_overlap,
    complete_overlap_with_different_boundaries,
    invalid_final_boundary,
    transfer_replacement_rejected,
    old_fragment_after_completion,
    unsupported_compression,
    time_moved_backwards,
    transfer_deadline_expired,
    deadline_overflow,
    transfer_id_overflow,
    revision_overflow,
    foreign_insert_plan,
    stale_insert_plan,
    insert_plan_mismatch,
};

struct NetchanReassemblyError {
    NetchanReassemblyErrorCode code{
        NetchanReassemblyErrorCode::invalid_configuration};
};

struct NetchanReassemblyOperationResult {
    std::optional<NetchanReassemblyError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

class NetchanNormalReassemblerIdentity;

class NetchanFragmentInsertPlan final {
public:
    NetchanFragmentInsertPlan(const NetchanFragmentInsertPlan&) = delete;
    NetchanFragmentInsertPlan& operator=(const NetchanFragmentInsertPlan&) = delete;
    NetchanFragmentInsertPlan(NetchanFragmentInsertPlan&& other) noexcept;
    NetchanFragmentInsertPlan& operator=(NetchanFragmentInsertPlan&& other) noexcept;
    ~NetchanFragmentInsertPlan() = default;

    [[nodiscard]] const NetchanFragmentReceipt& receipt() const noexcept
    {
        return receipt_;
    }

    [[nodiscard]] NetchanFragmentInsertDisposition disposition() const noexcept
    {
        return receipt_.disposition;
    }

    [[nodiscard]] bool exact_retransmission() const noexcept
    {
        return receipt_.disposition ==
               NetchanFragmentInsertDisposition::exact_duplicate;
    }

    [[nodiscard]] std::size_t required_event_count() const noexcept
    {
        return receipt_.required_event_count;
    }

    [[nodiscard]] const std::optional<std::size_t>& completion_size() const noexcept
    {
        return receipt_.completion_size;
    }

private:
    friend class NetchanNormalReassembler;

    NetchanFragmentInsertPlan(
        NetchanFragmentReceipt receipt,
        std::optional<NetchanNormalTransferState> candidate_state,
        std::vector<std::byte> candidate_bytes,
        std::vector<NetchanFragmentRange> candidate_ranges,
        std::optional<NetchanTransferCompletion> completion,
        std::optional<std::uint16_t> candidate_completed_fragment_count,
        std::uint64_t candidate_next_transfer_id,
        bool state_unchanged,
        std::shared_ptr<const NetchanNormalReassemblerIdentity> identity,
        std::uint64_t revision) noexcept;

    NetchanFragmentReceipt receipt_;
    std::optional<NetchanNormalTransferState> candidate_state_;
    std::vector<std::byte> candidate_bytes_;
    std::vector<NetchanFragmentRange> candidate_ranges_;
    std::optional<NetchanTransferCompletion> completion_;
    std::optional<std::uint16_t> candidate_completed_fragment_count_;
    std::uint64_t candidate_next_transfer_id_{1U};
    bool state_unchanged_{false};
    std::shared_ptr<const NetchanNormalReassemblerIdentity> identity_;
    std::uint64_t revision_{0U};
    bool consumable_{true};
};

struct NetchanFragmentInsertPrepareResult {
    std::optional<NetchanFragmentInsertPlan> plan;
    std::optional<NetchanReassemblyError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return plan.has_value();
    }
};

struct NetchanFragmentInsertCommitResult {
    std::optional<NetchanFragmentReceipt> receipt;
    std::optional<NetchanTransferCompletion> completion;
    std::optional<NetchanReassemblyError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return receipt.has_value() && !error.has_value();
    }
};

struct NetchanFragmentExpiryResult {
    std::optional<NetchanNormalTransferId> timed_out_transfer;
    std::optional<NetchanReassemblyError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

class NetchanNormalReassembler final {
public:
    explicit NetchanNormalReassembler(NetchanReassemblyLimits limits = {});

    NetchanNormalReassembler(const NetchanNormalReassembler&) = delete;
    NetchanNormalReassembler& operator=(const NetchanNormalReassembler&) = delete;
    NetchanNormalReassembler(NetchanNormalReassembler&&) = delete;
    NetchanNormalReassembler& operator=(NetchanNormalReassembler&&) = delete;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const NetchanReassemblyLimits& limits() const noexcept;

    // `fragment_payload` must be the exact descriptor-selected bytes, not the
    // complete post-descriptor packet payload and not its contemporaneous suffix.
    [[nodiscard]] NetchanFragmentInsertPrepareResult prepare_insert(
        const NetchanFragmentDescriptor& descriptor,
        std::span<const std::byte> fragment_payload,
        NetchanFragmentTimePoint now) const;

    [[nodiscard]] NetchanFragmentInsertCommitResult commit_insert(
        NetchanFragmentInsertPlan&& plan) noexcept;

    [[nodiscard]] NetchanReassemblyOperationResult abandon_insert(
        NetchanFragmentInsertPlan&& plan) const noexcept;

    [[nodiscard]] NetchanFragmentExpiryResult expire(
        NetchanFragmentTimePoint now) noexcept;

    void clear() noexcept;

    [[nodiscard]] const std::optional<NetchanNormalTransferState>&
    active_transfer() const noexcept;
    [[nodiscard]] const std::vector<NetchanFragmentRange>& ranges() const noexcept;

private:
    // A prepared state-changing insert owns at most one additional bounded
    // transfer image. Peak reassembly working memory is therefore bounded by
    // the active image plus one candidate image (2 * the configured transfer
    // limit). Exact-duplicate plans own neither bytes nor ranges.
    NetchanReassemblyLimits limits_;
    std::optional<NetchanNormalTransferState> active_transfer_;
    std::vector<std::byte> bytes_;
    std::vector<NetchanFragmentRange> ranges_;
    std::shared_ptr<const NetchanNormalReassemblerIdentity> identity_;
    // Metadata-only ambiguity tombstone. The wire has no stable transfer ID,
    // so after completion the supported fail-closed policy requires ordinal 1
    // to open the next lifecycle. No completed opaque bytes are retained.
    std::optional<std::uint16_t> completed_fragment_count_;
    std::uint64_t next_transfer_id_{1U};
    std::uint64_t revision_{0U};
    bool valid_configuration_{false};
};

[[nodiscard]] constexpr std::string_view to_string(
    const NetchanFragmentInsertDisposition disposition) noexcept
{
    switch (disposition) {
    case NetchanFragmentInsertDisposition::transfer_started:
        return "transfer_started";
    case NetchanFragmentInsertDisposition::inserted:
        return "inserted";
    case NetchanFragmentInsertDisposition::exact_duplicate:
        return "exact_duplicate";
    case NetchanFragmentInsertDisposition::completed:
        return "completed";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const NetchanReassemblyErrorCode code) noexcept
{
    switch (code) {
    case NetchanReassemblyErrorCode::invalid_configuration:
        return "invalid_configuration";
    case NetchanReassemblyErrorCode::unsupported_fragment_stream:
        return "unsupported_fragment_stream";
    case NetchanReassemblyErrorCode::secondary_stream_pending_m3:
        return "secondary_stream_pending_m3";
    case NetchanReassemblyErrorCode::invalid_packed_fragment_id:
        return "invalid_packed_fragment_id";
    case NetchanReassemblyErrorCode::invalid_fragment_count:
        return "invalid_fragment_count";
    case NetchanReassemblyErrorCode::invalid_fragment_index:
        return "invalid_fragment_index";
    case NetchanReassemblyErrorCode::invalid_fragment_offset:
        return "invalid_fragment_offset";
    case NetchanReassemblyErrorCode::invalid_fragment_length:
        return "invalid_fragment_length";
    case NetchanReassemblyErrorCode::fragment_range_overflow:
        return "fragment_range_overflow";
    case NetchanReassemblyErrorCode::fragment_range_out_of_bounds:
        return "fragment_range_out_of_bounds";
    case NetchanReassemblyErrorCode::fragment_payload_size_mismatch:
        return "fragment_payload_size_mismatch";
    case NetchanReassemblyErrorCode::normal_transfer_too_large:
        return "normal_transfer_too_large";
    case NetchanReassemblyErrorCode::too_many_fragments:
        return "too_many_fragments";
    case NetchanReassemblyErrorCode::too_many_ranges:
        return "too_many_ranges";
    case NetchanReassemblyErrorCode::unexpected_fragment_order:
        return "unexpected_fragment_order";
    case NetchanReassemblyErrorCode::conflicting_duplicate:
        return "conflicting_duplicate";
    case NetchanReassemblyErrorCode::partial_overlap:
        return "partial_overlap";
    case NetchanReassemblyErrorCode::complete_overlap_with_different_boundaries:
        return "complete_overlap_with_different_boundaries";
    case NetchanReassemblyErrorCode::invalid_final_boundary:
        return "invalid_final_boundary";
    case NetchanReassemblyErrorCode::transfer_replacement_rejected:
        return "transfer_replacement_rejected";
    case NetchanReassemblyErrorCode::old_fragment_after_completion:
        return "old_fragment_after_completion";
    case NetchanReassemblyErrorCode::unsupported_compression:
        return "unsupported_compression";
    case NetchanReassemblyErrorCode::time_moved_backwards:
        return "time_moved_backwards";
    case NetchanReassemblyErrorCode::transfer_deadline_expired:
        return "transfer_deadline_expired";
    case NetchanReassemblyErrorCode::deadline_overflow:
        return "deadline_overflow";
    case NetchanReassemblyErrorCode::transfer_id_overflow:
        return "transfer_id_overflow";
    case NetchanReassemblyErrorCode::revision_overflow:
        return "revision_overflow";
    case NetchanReassemblyErrorCode::foreign_insert_plan:
        return "foreign_insert_plan";
    case NetchanReassemblyErrorCode::stale_insert_plan:
        return "stale_insert_plan";
    case NetchanReassemblyErrorCode::insert_plan_mismatch:
        return "insert_plan_mismatch";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
