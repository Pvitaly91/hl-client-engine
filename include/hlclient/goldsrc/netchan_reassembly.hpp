#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::size_t kObservedMaximumNormalFragmentPayloadSize = 1'024U;
inline constexpr std::size_t kMaximumNormalReassembledMessageSize = 1U << 20U;
inline constexpr std::size_t kMaximumNormalFragmentsPerMessage = 1'024U;
inline constexpr std::size_t kMaximumNormalFragmentPayloadSize = 16U << 10U;
inline constexpr std::size_t kMaximumActiveNormalFragmentTransfers = 1U;
inline constexpr std::size_t kNetchanReassemblyDiagnosticTextLimit = 256U;

struct NetchanNormalReassemblyLimits {
    std::size_t maximum_reassembled_size{kMaximumNormalReassembledMessageSize};
    std::size_t maximum_fragment_count{kMaximumNormalFragmentsPerMessage};
    std::size_t maximum_fragment_size{kMaximumNormalFragmentPayloadSize};
};

struct ReassembledNormalPayload {
    std::vector<std::byte> bytes;
    std::uint16_t fragment_count{0U};
};

enum class NetchanNormalReassemblyStatus {
    failed,
    fragment_accepted,
    duplicate_ignored,
    complete,
};

enum class NetchanNormalReassemblyErrorCode {
    invalid_configuration,
    zero_fragment_count,
    fragment_count_too_large,
    zero_fragment_index,
    fragment_index_out_of_range,
    empty_fragment,
    fragment_too_large,
    active_fragment_count_mismatch,
    conflicting_duplicate,
    message_size_overflow,
    message_too_large,
    internal_invariant,
};

struct NetchanNormalReassemblyError {
    NetchanNormalReassemblyErrorCode code{
        NetchanNormalReassemblyErrorCode::invalid_configuration};
    std::uint16_t fragment_index{0U};
    std::uint16_t fragment_count{0U};
    std::string context;
};

struct NetchanNormalReassemblyResult {
    NetchanNormalReassemblyStatus status{
        NetchanNormalReassemblyStatus::fragment_accepted};
    std::optional<ReassembledNormalPayload> payload;
    std::optional<NetchanNormalReassemblyError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

// Owns at most one bounded normal-stream transfer. Fragment indices are the
// captured one-based high 16 bits of frag_id; fragment_count is its low 16
// bits. The datagram codec is responsible for validating/slicing the captured
// offset and length fields before this copying boundary is called.
class NetchanNormalReassembly final {
public:
    explicit NetchanNormalReassembly(NetchanNormalReassemblyLimits limits = {});

    NetchanNormalReassembly(const NetchanNormalReassembly&) = delete;
    NetchanNormalReassembly& operator=(const NetchanNormalReassembly&) = delete;
    NetchanNormalReassembly(NetchanNormalReassembly&&) noexcept = default;
    NetchanNormalReassembly& operator=(NetchanNormalReassembly&&) noexcept = default;

    [[nodiscard]] NetchanNormalReassemblyResult add_fragment(
        std::uint16_t one_based_index,
        std::uint16_t fragment_count,
        std::span<const std::byte> fragment_bytes);

    void reset() noexcept;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::optional<std::uint16_t> expected_fragment_count() const noexcept;
    [[nodiscard]] std::size_t received_fragment_count() const noexcept;
    [[nodiscard]] std::size_t accumulated_size() const noexcept;
    [[nodiscard]] const NetchanNormalReassemblyLimits& limits() const noexcept;

private:
    [[nodiscard]] bool valid_configuration() const noexcept;

    NetchanNormalReassemblyLimits limits_;
    std::optional<std::uint16_t> expected_fragment_count_;
    std::vector<std::optional<std::vector<std::byte>>> fragments_;
    std::size_t received_fragment_count_{0U};
    std::size_t accumulated_size_{0U};
};

// Capture confirms slot zero as the normal stream. Slot one remains only an
// opaque secondary-stream boundary for M3; this classification carries no
// filename, path, or bytes and performs no persistence.
enum class NetchanFragmentStreamDisposition {
    normal,
    secondary_stream_pending_m3,
    invalid_slot,
};

[[nodiscard]] constexpr NetchanFragmentStreamDisposition
classify_netchan_fragment_stream(const std::uint8_t slot_index) noexcept
{
    if (slot_index == 0U) {
        return NetchanFragmentStreamDisposition::normal;
    }
    if (slot_index == 1U) {
        return NetchanFragmentStreamDisposition::secondary_stream_pending_m3;
    }
    return NetchanFragmentStreamDisposition::invalid_slot;
}

[[nodiscard]] constexpr std::string_view to_string(
    const NetchanNormalReassemblyErrorCode code) noexcept
{
    switch (code) {
    case NetchanNormalReassemblyErrorCode::invalid_configuration:
        return "invalid_configuration";
    case NetchanNormalReassemblyErrorCode::zero_fragment_count:
        return "zero_fragment_count";
    case NetchanNormalReassemblyErrorCode::fragment_count_too_large:
        return "fragment_count_too_large";
    case NetchanNormalReassemblyErrorCode::zero_fragment_index:
        return "zero_fragment_index";
    case NetchanNormalReassemblyErrorCode::fragment_index_out_of_range:
        return "fragment_index_out_of_range";
    case NetchanNormalReassemblyErrorCode::empty_fragment:
        return "empty_fragment";
    case NetchanNormalReassemblyErrorCode::fragment_too_large:
        return "fragment_too_large";
    case NetchanNormalReassemblyErrorCode::active_fragment_count_mismatch:
        return "active_fragment_count_mismatch";
    case NetchanNormalReassemblyErrorCode::conflicting_duplicate:
        return "conflicting_duplicate";
    case NetchanNormalReassemblyErrorCode::message_size_overflow:
        return "message_size_overflow";
    case NetchanNormalReassemblyErrorCode::message_too_large:
        return "message_too_large";
    case NetchanNormalReassemblyErrorCode::internal_invariant:
        return "internal_invariant";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
