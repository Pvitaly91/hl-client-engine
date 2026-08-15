#pragma once

#include <cstdint>
#include <optional>

namespace hlclient::goldsrc {

// Confirmed stock Protocol 48 server/client capture profile. The low 30 bits
// carry the numeric sequence while bits 31 and 30 are independent flags.
inline constexpr std::uint32_t kNetchanSequenceMask = 0x3fff'ffffU;
inline constexpr std::uint32_t kNetchanReliableSequenceFlag = 0x8000'0000U;
inline constexpr std::uint32_t kNetchanFragmentSequenceFlag = 0x4000'0000U;
inline constexpr std::uint32_t kNetchanSequenceModulus = kNetchanSequenceMask + 1U;
inline constexpr std::uint32_t kNetchanSequenceHalfRange = kNetchanSequenceModulus / 2U;

class NetchanSequence final {
public:
    [[nodiscard]] static std::optional<NetchanSequence> from_numeric(
        std::uint32_t value) noexcept;

    [[nodiscard]] constexpr std::uint32_t value() const noexcept
    {
        return value_;
    }

    [[nodiscard]] friend constexpr bool operator==(
        const NetchanSequence& left,
        const NetchanSequence& right) noexcept = default;

private:
    explicit constexpr NetchanSequence(const std::uint32_t value) noexcept : value_{value} {}

    std::uint32_t value_{0U};
};

struct NetchanSequenceFlags {
    bool reliable{false};
    bool fragmented{false};

    [[nodiscard]] friend constexpr bool operator==(
        const NetchanSequenceFlags& left,
        const NetchanSequenceFlags& right) noexcept = default;
};

struct NetchanSequenceWord {
    NetchanSequence sequence;
    NetchanSequenceFlags flags;
};

enum class NetchanSequenceComparison {
    equal,
    newer,
    older,
    half_range_ambiguous,
};

// Returns the forward modular distance from reference to candidate.
[[nodiscard]] std::uint32_t sequence_distance(
    NetchanSequence candidate,
    NetchanSequence reference) noexcept;

[[nodiscard]] NetchanSequenceComparison compare_sequences(
    NetchanSequence candidate,
    NetchanSequence reference) noexcept;

[[nodiscard]] bool is_sequence_newer(
    NetchanSequence candidate,
    NetchanSequence reference) noexcept;

[[nodiscard]] NetchanSequenceWord decode_netchan_sequence_word(
    std::uint32_t wire_value) noexcept;

[[nodiscard]] std::uint32_t encode_netchan_sequence_word(
    NetchanSequence sequence,
    NetchanSequenceFlags flags) noexcept;

} // namespace hlclient::goldsrc
