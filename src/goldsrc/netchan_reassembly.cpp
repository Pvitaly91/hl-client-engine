#include <hlclient/goldsrc/netchan_reassembly.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace hlclient::goldsrc {

class NetchanNormalReassemblerIdentity final {
};

namespace {

[[nodiscard]] bool valid_limits(const NetchanReassemblyLimits& limits) noexcept
{
    if (limits.maximum_fragment_payload_size == 0U ||
        limits.maximum_fragment_payload_size > kMaximumNetchanFragmentPayloadSize ||
        limits.maximum_normal_transfer_size == 0U ||
        limits.maximum_normal_transfer_size > kMaximumNetchanNormalTransferSize ||
        limits.maximum_fragments_per_transfer == 0U ||
        limits.maximum_fragments_per_transfer >
            kMaximumNetchanFragmentsPerTransfer ||
        limits.maximum_active_normal_transfers == 0U ||
        limits.maximum_active_normal_transfers >
            kMaximumActiveNormalTransfers ||
        limits.maximum_fragment_ranges == 0U ||
        limits.maximum_fragment_ranges > kMaximumNetchanFragmentRanges ||
        limits.maximum_fragment_ranges < limits.maximum_fragments_per_transfer ||
        limits.fragment_timeout <= std::chrono::milliseconds::zero() ||
        limits.fragment_timeout > kMaximumNetchanFragmentTimeout) {
        return false;
    }

    if (limits.maximum_fragments_per_transfer >
        std::numeric_limits<std::size_t>::max() /
            kStockProtocol48NormalFragmentChunkSize) {
        return false;
    }
    const auto representable_transfer_size =
        limits.maximum_fragments_per_transfer *
        kStockProtocol48NormalFragmentChunkSize;
    return limits.maximum_normal_transfer_size <= representable_transfer_size;
}

[[nodiscard]] NetchanReassemblyOperationResult operation_failure(
    const NetchanReassemblyErrorCode code) noexcept
{
    return NetchanReassemblyOperationResult{NetchanReassemblyError{code}};
}

[[nodiscard]] NetchanFragmentInsertPrepareResult prepare_failure(
    const NetchanReassemblyErrorCode code) noexcept
{
    return NetchanFragmentInsertPrepareResult{
        std::nullopt,
        NetchanReassemblyError{code},
    };
}

[[nodiscard]] NetchanFragmentInsertCommitResult commit_failure(
    const NetchanReassemblyErrorCode code) noexcept
{
    return NetchanFragmentInsertCommitResult{
        std::nullopt,
        std::nullopt,
        NetchanReassemblyError{code},
    };
}

[[nodiscard]] NetchanFragmentExpiryResult expiry_failure(
    const NetchanReassemblyErrorCode code) noexcept
{
    return NetchanFragmentExpiryResult{
        std::nullopt,
        NetchanReassemblyError{code},
    };
}

[[nodiscard]] bool can_add_timeout(
    const NetchanFragmentTimePoint now,
    const std::chrono::milliseconds timeout) noexcept
{
    const auto duration =
        std::chrono::duration_cast<NetchanFragmentClock::duration>(timeout);
    return duration > NetchanFragmentClock::duration::zero() &&
           now <= NetchanFragmentTimePoint::max() - duration;
}

[[nodiscard]] std::size_t transfer_offset(
    const std::uint16_t fragment_index) noexcept
{
    return (static_cast<std::size_t>(fragment_index) - 1U) *
           kStockProtocol48NormalFragmentChunkSize;
}

[[nodiscard]] bool ranges_overlap(
    const NetchanFragmentRange& left,
    const NetchanFragmentRange& right) noexcept
{
    return std::max(left.transfer_offset, right.transfer_offset) <
           std::min(
               left.transfer_offset + left.length,
               right.transfer_offset + right.length);
}

} // namespace

NetchanFragmentInsertPlan::NetchanFragmentInsertPlan(
    NetchanFragmentReceipt receipt,
    std::optional<NetchanNormalTransferState> candidate_state,
    std::vector<std::byte> candidate_bytes,
    std::vector<NetchanFragmentRange> candidate_ranges,
    std::optional<NetchanTransferCompletion> completion,
    std::optional<std::uint16_t> candidate_completed_fragment_count,
    const std::uint64_t candidate_next_transfer_id,
    const bool state_unchanged,
    std::shared_ptr<const NetchanNormalReassemblerIdentity> identity,
    const std::uint64_t revision) noexcept
    : receipt_{std::move(receipt)},
      candidate_state_{std::move(candidate_state)},
      candidate_bytes_{std::move(candidate_bytes)},
      candidate_ranges_{std::move(candidate_ranges)},
      completion_{std::move(completion)},
      candidate_completed_fragment_count_{
          candidate_completed_fragment_count},
      candidate_next_transfer_id_{candidate_next_transfer_id},
      state_unchanged_{state_unchanged},
      identity_{std::move(identity)},
      revision_{revision}
{
}

NetchanFragmentInsertPlan::NetchanFragmentInsertPlan(
    NetchanFragmentInsertPlan&& other) noexcept
    : receipt_{std::move(other.receipt_)},
      candidate_state_{std::move(other.candidate_state_)},
      candidate_bytes_{std::move(other.candidate_bytes_)},
      candidate_ranges_{std::move(other.candidate_ranges_)},
      completion_{std::move(other.completion_)},
      candidate_completed_fragment_count_{
          other.candidate_completed_fragment_count_},
      candidate_next_transfer_id_{other.candidate_next_transfer_id_},
      state_unchanged_{other.state_unchanged_},
      identity_{std::move(other.identity_)},
      revision_{other.revision_},
      consumable_{other.consumable_}
{
    other.consumable_ = false;
}

NetchanFragmentInsertPlan& NetchanFragmentInsertPlan::operator=(
    NetchanFragmentInsertPlan&& other) noexcept
{
    if (this != &other) {
        receipt_ = std::move(other.receipt_);
        candidate_state_ = std::move(other.candidate_state_);
        candidate_bytes_ = std::move(other.candidate_bytes_);
        candidate_ranges_ = std::move(other.candidate_ranges_);
        completion_ = std::move(other.completion_);
        candidate_completed_fragment_count_ =
            other.candidate_completed_fragment_count_;
        candidate_next_transfer_id_ = other.candidate_next_transfer_id_;
        state_unchanged_ = other.state_unchanged_;
        identity_ = std::move(other.identity_);
        revision_ = other.revision_;
        consumable_ = other.consumable_;
        other.consumable_ = false;
    }
    return *this;
}

NetchanNormalReassembler::NetchanNormalReassembler(
    const NetchanReassemblyLimits limits)
    : limits_{limits},
      identity_{std::make_shared<const NetchanNormalReassemblerIdentity>()},
      valid_configuration_{valid_limits(limits_)}
{
}

bool NetchanNormalReassembler::valid_configuration() const noexcept
{
    return valid_configuration_;
}

const NetchanReassemblyLimits& NetchanNormalReassembler::limits() const noexcept
{
    return limits_;
}

NetchanFragmentInsertPrepareResult NetchanNormalReassembler::prepare_insert(
    const NetchanFragmentDescriptor& descriptor,
    const std::span<const std::byte> fragment_payload,
    const NetchanFragmentTimePoint now) const
{
    if (!valid_configuration_) {
        return prepare_failure(NetchanReassemblyErrorCode::invalid_configuration);
    }
    const auto stream = descriptor.stream();
    if (!stream) {
        return prepare_failure(NetchanReassemblyErrorCode::unsupported_fragment_stream);
    }
    if (*stream != StockProtocol48FragmentProfile::normal_stream) {
        // Slot 1/file semantics remain capture-pending. Do not call it a file
        // stream or retain any of its bytes without that evidence.
        return prepare_failure(
            NetchanReassemblyErrorCode::secondary_stream_pending_m3);
    }
    if (descriptor.offset != 0U || descriptor.payload_offset != 0U) {
        return prepare_failure(NetchanReassemblyErrorCode::invalid_fragment_offset);
    }

    const auto packed_id = descriptor.packed_id();
    if (!packed_id) {
        return prepare_failure(NetchanReassemblyErrorCode::invalid_packed_fragment_id);
    }
    const auto fragment_index = packed_id->fragment_index();
    const auto fragment_count = packed_id->fragment_count();
    if (fragment_count == 0U) {
        return prepare_failure(NetchanReassemblyErrorCode::invalid_fragment_count);
    }
    if (fragment_index == 0U || fragment_index > fragment_count) {
        return prepare_failure(NetchanReassemblyErrorCode::invalid_fragment_index);
    }
    if (static_cast<std::size_t>(fragment_count) >
        limits_.maximum_fragments_per_transfer) {
        return prepare_failure(NetchanReassemblyErrorCode::too_many_fragments);
    }
    if (static_cast<std::size_t>(fragment_count) >
        std::numeric_limits<std::size_t>::max() /
            kStockProtocol48NormalFragmentChunkSize) {
        return prepare_failure(NetchanReassemblyErrorCode::fragment_range_overflow);
    }
    const auto declared_extent =
        static_cast<std::size_t>(fragment_count) *
        kStockProtocol48NormalFragmentChunkSize;
    const auto minimum_transfer_extent =
        declared_extent - kStockProtocol48NormalFragmentChunkSize + 1U;
    if (minimum_transfer_extent > limits_.maximum_normal_transfer_size) {
        return prepare_failure(
            NetchanReassemblyErrorCode::normal_transfer_too_large);
    }
    if (descriptor.length == 0U ||
        static_cast<std::size_t>(descriptor.length) >
            limits_.maximum_fragment_payload_size) {
        return prepare_failure(NetchanReassemblyErrorCode::invalid_fragment_length);
    }
    if (fragment_payload.size() != static_cast<std::size_t>(descriptor.length)) {
        return prepare_failure(
            NetchanReassemblyErrorCode::fragment_payload_size_mismatch);
    }

    const bool final_fragment = fragment_index == fragment_count;
    if (!final_fragment &&
        descriptor.length != kStockProtocol48NormalFragmentChunkSize) {
        return prepare_failure(NetchanReassemblyErrorCode::invalid_fragment_length);
    }
    const auto range_offset = transfer_offset(fragment_index);
    const auto range_length = static_cast<std::size_t>(descriptor.length);
    if (range_offset > std::numeric_limits<std::size_t>::max() - range_length) {
        return prepare_failure(NetchanReassemblyErrorCode::fragment_range_overflow);
    }
    const auto range_end = range_offset + range_length;
    if (range_end > limits_.maximum_normal_transfer_size) {
        return prepare_failure(NetchanReassemblyErrorCode::normal_transfer_too_large);
    }

    const NetchanFragmentRange incoming_range{
        range_offset,
        range_length,
        fragment_index,
    };

    if (active_transfer_) {
        if (now < active_transfer_->last_fragment_at) {
            return prepare_failure(NetchanReassemblyErrorCode::time_moved_backwards);
        }
        if (active_transfer_->declared_fragment_count != fragment_count) {
            return prepare_failure(
                NetchanReassemblyErrorCode::transfer_replacement_rejected);
        }
        if (now >= active_transfer_->deadline) {
            return prepare_failure(
                NetchanReassemblyErrorCode::transfer_deadline_expired);
        }

        const auto existing = std::ranges::find_if(
            ranges_,
            [fragment_index](const NetchanFragmentRange& range) {
                return range.fragment_index == fragment_index;
            });
        if (existing != ranges_.end()) {
            if (existing->transfer_offset != incoming_range.transfer_offset ||
                existing->length != incoming_range.length) {
                return prepare_failure(
                    NetchanReassemblyErrorCode::
                        complete_overlap_with_different_boundaries);
            }
            const auto existing_payload = std::span<const std::byte>{bytes_}.subspan(
                existing->transfer_offset,
                existing->length);
            if (!std::ranges::equal(existing_payload, fragment_payload)) {
                return prepare_failure(
                    NetchanReassemblyErrorCode::conflicting_duplicate);
            }
            return NetchanFragmentInsertPrepareResult{
                NetchanFragmentInsertPlan{
                    NetchanFragmentReceipt{
                        NetchanFragmentInsertDisposition::exact_duplicate,
                        active_transfer_->transfer_id,
                        incoming_range,
                        active_transfer_->covered_size,
                        false,
                        0U,
                        std::nullopt,
                    },
                    std::nullopt,
                    {},
                    {},
                    std::nullopt,
                    completed_fragment_count_,
                    next_transfer_id_,
                    true,
                    identity_,
                    revision_,
                },
                std::nullopt,
            };
        }

        if (ranges_.size() >= limits_.maximum_fragment_ranges) {
            return prepare_failure(NetchanReassemblyErrorCode::too_many_ranges);
        }
        for (const auto& range : ranges_) {
            if (ranges_overlap(range, incoming_range)) {
                return prepare_failure(NetchanReassemblyErrorCode::partial_overlap);
            }
        }
    } else if (completed_fragment_count_ && fragment_index != 1U) {
        // The packed field carries only ordinal/count, not a stable transfer
        // identifier. Stock starts each observed lifecycle at ordinal 1. A
        // later ordinal after completion is therefore fail-closed as an old
        // fragment; accepting it as a new out-of-order transfer would be
        // indistinguishable from replay.
        return prepare_failure(
            NetchanReassemblyErrorCode::old_fragment_after_completion);
    }

    // Exact duplicates above have a zero-mutation plan and remain idempotent
    // even at the terminal revision value. Only a state-changing candidate
    // needs room for the commit revision increment.
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return prepare_failure(NetchanReassemblyErrorCode::revision_overflow);
    }

    const bool starts_transfer = !active_transfer_.has_value();
    if (starts_transfer && !can_add_timeout(now, limits_.fragment_timeout)) {
        return prepare_failure(NetchanReassemblyErrorCode::deadline_overflow);
    }

    auto candidate_state = active_transfer_;
    auto candidate_bytes = bytes_;
    auto candidate_ranges = ranges_;
    auto candidate_completed_fragment_count = completed_fragment_count_;
    auto candidate_next_transfer_id = next_transfer_id_;
    if (starts_transfer) {
        if (next_transfer_id_ == std::numeric_limits<std::uint64_t>::max()) {
            return prepare_failure(NetchanReassemblyErrorCode::transfer_id_overflow);
        }
        // Before the final ordinal arrives its exact length is unknown. Reserve
        // only the configured owning bound, not the count's 1,024-byte worst
        // case, so an otherwise valid near-limit transfer remains independent
        // of arrival order. Every inserted range is still checked against this
        // bound before it can address the candidate image.
        const auto allocation_size = final_fragment
                                         ? range_end
                                         : std::min(
                                               declared_extent,
                                               limits_.maximum_normal_transfer_size);
        candidate_bytes.assign(allocation_size, std::byte{0});
        candidate_ranges.clear();
        candidate_ranges.reserve(std::min(
            static_cast<std::size_t>(fragment_count),
            limits_.maximum_fragment_ranges));
        const auto transfer_id = NetchanNormalTransferId{next_transfer_id_};
        candidate_state = NetchanNormalTransferState{
            transfer_id,
            fragment_count,
            0U,
            0U,
            std::nullopt,
            now,
            now,
            now + std::chrono::duration_cast<NetchanFragmentClock::duration>(
                      limits_.fragment_timeout),
        };
        candidate_next_transfer_id = next_transfer_id_ + 1U;
        candidate_completed_fragment_count.reset();
    }

    if (!candidate_state || range_end > candidate_bytes.size()) {
        return prepare_failure(
            NetchanReassemblyErrorCode::fragment_range_out_of_bounds);
    }
    std::ranges::copy(
        fragment_payload,
        candidate_bytes.begin() + static_cast<std::ptrdiff_t>(range_offset));
    candidate_ranges.push_back(incoming_range);
    std::ranges::sort(
        candidate_ranges,
        {},
        &NetchanFragmentRange::transfer_offset);
    ++candidate_state->received_fragment_count;
    if (candidate_state->covered_size >
        limits_.maximum_normal_transfer_size - range_length) {
        return prepare_failure(NetchanReassemblyErrorCode::normal_transfer_too_large);
    }
    candidate_state->covered_size += range_length;
    candidate_state->last_fragment_at = now;
    // The deadline is fixed at creation. Fresh fragments update diagnostics,
    // but neither ordered progress nor attacker-controlled trickle extends the
    // transfer lifetime.
    if (final_fragment) {
        candidate_state->total_size = range_end;
    }

    std::optional<NetchanTransferCompletion> completion;
    auto disposition = starts_transfer
                           ? NetchanFragmentInsertDisposition::transfer_started
                           : NetchanFragmentInsertDisposition::inserted;
    std::size_t required_event_count = starts_transfer ? 1U : 0U;
    std::optional<std::size_t> completion_size;
    const auto receipt_transfer_id = candidate_state->transfer_id;
    const auto receipt_covered_size = candidate_state->covered_size;
    if (candidate_state->received_fragment_count ==
        candidate_state->declared_fragment_count) {
        if (!candidate_state->total_size || candidate_ranges.empty()) {
            return prepare_failure(
                NetchanReassemblyErrorCode::invalid_final_boundary);
        }
        std::size_t cursor = 0U;
        for (const auto& range : candidate_ranges) {
            if (cursor > *candidate_state->total_size ||
                range.transfer_offset != cursor ||
                range.length > *candidate_state->total_size - cursor) {
                return prepare_failure(
                    NetchanReassemblyErrorCode::invalid_final_boundary);
            }
            cursor += range.length;
        }
        if (cursor != *candidate_state->total_size) {
            return prepare_failure(
                NetchanReassemblyErrorCode::invalid_final_boundary);
        }

        candidate_bytes.resize(*candidate_state->total_size);
        completion_size = candidate_bytes.size();
        completion = NetchanTransferCompletion{
            candidate_state->transfer_id,
            std::move(candidate_bytes),
        };
        candidate_state.reset();
        candidate_ranges.clear();
        candidate_completed_fragment_count = fragment_count;
        disposition = NetchanFragmentInsertDisposition::completed;
        required_event_count += 1U;
    }

    return NetchanFragmentInsertPrepareResult{
        NetchanFragmentInsertPlan{
            NetchanFragmentReceipt{
                disposition,
                receipt_transfer_id,
                incoming_range,
                receipt_covered_size,
                starts_transfer,
                required_event_count,
                completion_size,
            },
            std::move(candidate_state),
            std::move(candidate_bytes),
            std::move(candidate_ranges),
            std::move(completion),
            candidate_completed_fragment_count,
            candidate_next_transfer_id,
            false,
            identity_,
            revision_,
        },
        std::nullopt,
    };
}

NetchanFragmentInsertCommitResult NetchanNormalReassembler::commit_insert(
    NetchanFragmentInsertPlan&& plan) noexcept
{
    if (plan.identity_ != identity_) {
        return commit_failure(NetchanReassemblyErrorCode::foreign_insert_plan);
    }
    if (!plan.consumable_ || plan.revision_ != revision_) {
        plan.consumable_ = false;
        return commit_failure(NetchanReassemblyErrorCode::stale_insert_plan);
    }
    if (!valid_configuration_) {
        plan.consumable_ = false;
        return commit_failure(NetchanReassemblyErrorCode::invalid_configuration);
    }
    const bool duplicate_shape =
        plan.receipt_.disposition ==
            NetchanFragmentInsertDisposition::exact_duplicate &&
        plan.state_unchanged_ && !plan.completion_;
    const bool completed_shape =
        plan.receipt_.disposition == NetchanFragmentInsertDisposition::completed &&
        !plan.state_unchanged_ && !plan.candidate_state_ && plan.completion_;
    const bool inserted_shape =
        (plan.receipt_.disposition ==
             NetchanFragmentInsertDisposition::transfer_started ||
         plan.receipt_.disposition == NetchanFragmentInsertDisposition::inserted) &&
        !plan.state_unchanged_ && plan.candidate_state_ && !plan.completion_;
    if ((!duplicate_shape && !completed_shape && !inserted_shape) ||
        !plan.receipt_.transfer_id.valid()) {
        plan.consumable_ = false;
        return commit_failure(NetchanReassemblyErrorCode::insert_plan_mismatch);
    }
    if (!plan.state_unchanged_ &&
        revision_ == std::numeric_limits<std::uint64_t>::max()) {
        plan.consumable_ = false;
        return commit_failure(NetchanReassemblyErrorCode::revision_overflow);
    }

    plan.consumable_ = false;
    if (!plan.state_unchanged_) {
        active_transfer_.swap(plan.candidate_state_);
        bytes_.swap(plan.candidate_bytes_);
        ranges_.swap(plan.candidate_ranges_);
        completed_fragment_count_ =
            plan.candidate_completed_fragment_count_;
        next_transfer_id_ = plan.candidate_next_transfer_id_;
    }
    if (!plan.state_unchanged_) {
        ++revision_;
    }
    return NetchanFragmentInsertCommitResult{
        std::move(plan.receipt_),
        std::move(plan.completion_),
        std::nullopt,
    };
}

NetchanReassemblyOperationResult NetchanNormalReassembler::abandon_insert(
    NetchanFragmentInsertPlan&& plan) const noexcept
{
    if (plan.identity_ != identity_) {
        return operation_failure(NetchanReassemblyErrorCode::foreign_insert_plan);
    }
    if (!plan.consumable_ || plan.revision_ != revision_) {
        plan.consumable_ = false;
        return operation_failure(NetchanReassemblyErrorCode::stale_insert_plan);
    }
    plan.consumable_ = false;
    return NetchanReassemblyOperationResult{};
}

NetchanFragmentExpiryResult NetchanNormalReassembler::expire(
    const NetchanFragmentTimePoint now) noexcept
{
    if (!valid_configuration_) {
        return expiry_failure(NetchanReassemblyErrorCode::invalid_configuration);
    }
    if (!active_transfer_) {
        return NetchanFragmentExpiryResult{};
    }
    if (now < active_transfer_->last_fragment_at) {
        return expiry_failure(NetchanReassemblyErrorCode::time_moved_backwards);
    }
    if (now < active_transfer_->deadline) {
        return NetchanFragmentExpiryResult{};
    }
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return expiry_failure(NetchanReassemblyErrorCode::revision_overflow);
    }

    const auto timed_out_transfer = active_transfer_->transfer_id;
    active_transfer_.reset();
    std::vector<std::byte>{}.swap(bytes_);
    std::vector<NetchanFragmentRange>{}.swap(ranges_);
    ++revision_;
    return NetchanFragmentExpiryResult{
        timed_out_transfer,
        std::nullopt,
    };
}

void NetchanNormalReassembler::clear() noexcept
{
    active_transfer_.reset();
    std::vector<std::byte>{}.swap(bytes_);
    std::vector<NetchanFragmentRange>{}.swap(ranges_);
    completed_fragment_count_.reset();
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        valid_configuration_ = false;
        return;
    }
    ++revision_;
}

const std::optional<NetchanNormalTransferState>&
NetchanNormalReassembler::active_transfer() const noexcept
{
    return active_transfer_;
}

const std::vector<NetchanFragmentRange>&
NetchanNormalReassembler::ranges() const noexcept
{
    return ranges_;
}

} // namespace hlclient::goldsrc
