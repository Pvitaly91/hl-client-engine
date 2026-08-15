#include <hlclient/goldsrc/netchan_reassembly.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] std::string bounded_text(std::string text)
{
    if (text.size() > kNetchanReassemblyDiagnosticTextLimit) {
        text.resize(kNetchanReassemblyDiagnosticTextLimit);
    }
    return text;
}

[[nodiscard]] NetchanNormalReassemblyResult failure(
    const NetchanNormalReassemblyErrorCode code,
    const std::uint16_t index,
    const std::uint16_t count,
    std::string context)
{
    return NetchanNormalReassemblyResult{
        NetchanNormalReassemblyStatus::failed,
        std::nullopt,
        NetchanNormalReassemblyError{
            code,
            index,
            count,
            bounded_text(std::move(context)),
        },
    };
}

[[nodiscard]] bool bytes_equal(
    const std::vector<std::byte>& stored,
    const std::span<const std::byte> candidate) noexcept
{
    return stored.size() == candidate.size() &&
           std::equal(stored.begin(), stored.end(), candidate.begin());
}

} // namespace

NetchanNormalReassembly::NetchanNormalReassembly(
    const NetchanNormalReassemblyLimits limits)
    : limits_{limits}
{
}

NetchanNormalReassemblyResult NetchanNormalReassembly::add_fragment(
    const std::uint16_t one_based_index,
    const std::uint16_t fragment_count,
    const std::span<const std::byte> fragment_bytes)
{
    if (!valid_configuration()) {
        return failure(
            NetchanNormalReassemblyErrorCode::invalid_configuration,
            one_based_index,
            fragment_count,
            "Invalid normal-fragment reassembly limits");
    }
    if (fragment_count == 0U) {
        return failure(
            NetchanNormalReassemblyErrorCode::zero_fragment_count,
            one_based_index,
            fragment_count,
            "Normal fragment count must be nonzero");
    }
    if (static_cast<std::size_t>(fragment_count) > limits_.maximum_fragment_count) {
        return failure(
            NetchanNormalReassemblyErrorCode::fragment_count_too_large,
            one_based_index,
            fragment_count,
            "Normal fragment count exceeds the project bound");
    }
    if (one_based_index == 0U) {
        return failure(
            NetchanNormalReassemblyErrorCode::zero_fragment_index,
            one_based_index,
            fragment_count,
            "Normal fragment index is one-based and must be nonzero");
    }
    if (one_based_index > fragment_count) {
        return failure(
            NetchanNormalReassemblyErrorCode::fragment_index_out_of_range,
            one_based_index,
            fragment_count,
            "Normal fragment index exceeds its declared count");
    }
    if (fragment_bytes.empty()) {
        return failure(
            NetchanNormalReassemblyErrorCode::empty_fragment,
            one_based_index,
            fragment_count,
            "Normal fragment payload must be nonempty");
    }
    if (fragment_bytes.size() > limits_.maximum_fragment_size) {
        return failure(
            NetchanNormalReassemblyErrorCode::fragment_too_large,
            one_based_index,
            fragment_count,
            "Normal fragment payload exceeds the configured project bound");
    }
    if (expected_fragment_count_ && *expected_fragment_count_ != fragment_count) {
        return failure(
            NetchanNormalReassemblyErrorCode::active_fragment_count_mismatch,
            one_based_index,
            fragment_count,
            "Normal fragment count conflicts with the active transfer");
    }

    const auto slot = static_cast<std::size_t>(one_based_index - 1U);
    if (expected_fragment_count_) {
        const auto& existing = fragments_[slot];
        if (existing) {
            if (bytes_equal(*existing, fragment_bytes)) {
                return NetchanNormalReassemblyResult{
                    NetchanNormalReassemblyStatus::duplicate_ignored,
                    std::nullopt,
                    std::nullopt,
                };
            }
            return failure(
                NetchanNormalReassemblyErrorCode::conflicting_duplicate,
                one_based_index,
                fragment_count,
                "Duplicate normal fragment carries conflicting bytes");
        }
    }

    if (accumulated_size_ >
        std::numeric_limits<std::size_t>::max() - fragment_bytes.size()) {
        return failure(
            NetchanNormalReassemblyErrorCode::message_size_overflow,
            one_based_index,
            fragment_count,
            "Normal reassembled size overflows the host size type");
    }
    const auto new_size = accumulated_size_ + fragment_bytes.size();
    if (new_size > limits_.maximum_reassembled_size) {
        return failure(
            NetchanNormalReassemblyErrorCode::message_too_large,
            one_based_index,
            fragment_count,
            "Normal reassembled payload exceeds the configured project bound");
    }

    std::vector<std::byte> owned_fragment{fragment_bytes.begin(), fragment_bytes.end()};
    if (!expected_fragment_count_) {
        // Allocation is based only on a count already checked against the
        // 1024-fragment hard ceiling. Fragment payload allocation is likewise
        // capped before this point.
        fragments_.resize(fragment_count);
        expected_fragment_count_ = fragment_count;
    }
    fragments_[slot] = std::move(owned_fragment);
    accumulated_size_ = new_size;
    ++received_fragment_count_;

    if (received_fragment_count_ != static_cast<std::size_t>(fragment_count)) {
        return NetchanNormalReassemblyResult{
            NetchanNormalReassemblyStatus::fragment_accepted,
            std::nullopt,
            std::nullopt,
        };
    }

    ReassembledNormalPayload complete;
    complete.fragment_count = fragment_count;
    complete.bytes.reserve(accumulated_size_);
    for (const auto& fragment : fragments_) {
        // received_fragment_count == expected count is sufficient because one
        // optional slot is populated per distinct, validated index.
        if (!fragment) {
            reset();
            return failure(
                NetchanNormalReassemblyErrorCode::internal_invariant,
                one_based_index,
                fragment_count,
                "Normal fragment accounting is internally inconsistent");
        }
        complete.bytes.insert(
            complete.bytes.end(),
            fragment->begin(),
            fragment->end());
    }

    reset();
    return NetchanNormalReassemblyResult{
        NetchanNormalReassemblyStatus::complete,
        std::move(complete),
        std::nullopt,
    };
}

void NetchanNormalReassembly::reset() noexcept
{
    fragments_.clear();
    expected_fragment_count_.reset();
    received_fragment_count_ = 0U;
    accumulated_size_ = 0U;
}

bool NetchanNormalReassembly::active() const noexcept
{
    return expected_fragment_count_.has_value();
}

std::optional<std::uint16_t>
NetchanNormalReassembly::expected_fragment_count() const noexcept
{
    return expected_fragment_count_;
}

std::size_t NetchanNormalReassembly::received_fragment_count() const noexcept
{
    return received_fragment_count_;
}

std::size_t NetchanNormalReassembly::accumulated_size() const noexcept
{
    return accumulated_size_;
}

const NetchanNormalReassemblyLimits& NetchanNormalReassembly::limits() const noexcept
{
    return limits_;
}

bool NetchanNormalReassembly::valid_configuration() const noexcept
{
    return limits_.maximum_reassembled_size > 0U &&
           limits_.maximum_reassembled_size <= kMaximumNormalReassembledMessageSize &&
           limits_.maximum_fragment_count > 0U &&
           limits_.maximum_fragment_count <= kMaximumNormalFragmentsPerMessage &&
           limits_.maximum_fragment_size > 0U &&
           limits_.maximum_fragment_size <= kMaximumNormalFragmentPayloadSize;
}

} // namespace hlclient::goldsrc
