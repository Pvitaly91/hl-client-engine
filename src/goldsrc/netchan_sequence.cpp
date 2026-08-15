#include <hlclient/goldsrc/netchan_sequence.hpp>

namespace hlclient::goldsrc {

std::optional<NetchanSequence> NetchanSequence::from_numeric(
    const std::uint32_t value) noexcept
{
    if (value > kNetchanSequenceMask) {
        return std::nullopt;
    }
    return NetchanSequence{value};
}

std::uint32_t sequence_distance(
    const NetchanSequence candidate,
    const NetchanSequence reference) noexcept
{
    return (candidate.value() - reference.value()) & kNetchanSequenceMask;
}

NetchanSequenceComparison compare_sequences(
    const NetchanSequence candidate,
    const NetchanSequence reference) noexcept
{
    const auto distance = sequence_distance(candidate, reference);
    if (distance == 0U) {
        return NetchanSequenceComparison::equal;
    }
    if (distance == kNetchanSequenceHalfRange) {
        return NetchanSequenceComparison::half_range_ambiguous;
    }
    if (distance < kNetchanSequenceHalfRange) {
        return NetchanSequenceComparison::newer;
    }
    return NetchanSequenceComparison::older;
}

bool is_sequence_newer(
    const NetchanSequence candidate,
    const NetchanSequence reference) noexcept
{
    return compare_sequences(candidate, reference) == NetchanSequenceComparison::newer;
}

NetchanSequenceWord decode_netchan_sequence_word(const std::uint32_t wire_value) noexcept
{
    const auto sequence = NetchanSequence::from_numeric(wire_value & kNetchanSequenceMask);
    return NetchanSequenceWord{
        *sequence,
        NetchanSequenceFlags{
            (wire_value & kNetchanReliableSequenceFlag) != 0U,
            (wire_value & kNetchanFragmentSequenceFlag) != 0U,
        },
    };
}

std::uint32_t encode_netchan_sequence_word(
    const NetchanSequence sequence,
    const NetchanSequenceFlags flags) noexcept
{
    std::uint32_t wire_value = sequence.value();
    if (flags.reliable) {
        wire_value |= kNetchanReliableSequenceFlag;
    }
    if (flags.fragmented) {
        wire_value |= kNetchanFragmentSequenceFlag;
    }
    return wire_value;
}

} // namespace hlclient::goldsrc
