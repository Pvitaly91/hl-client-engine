#include <hlclient/goldsrc/netchan_sequence.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

namespace {

namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] goldsrc::NetchanSequence sequence(const std::uint32_t value)
{
    const auto result = goldsrc::NetchanSequence::from_numeric(value);
    REQUIRE(result.has_value());
    return *result;
}

TEST_CASE("Netchan numeric sequences enforce the captured low-30-bit range",
          "[goldsrc][netchan][sequence]")
{
    const auto minimum = goldsrc::NetchanSequence::from_numeric(0U);
    const auto maximum = goldsrc::NetchanSequence::from_numeric(goldsrc::kNetchanSequenceMask);

    REQUIRE(minimum.has_value());
    REQUIRE(maximum.has_value());
    CHECK(minimum->value() == 0U);
    CHECK(maximum->value() == 0x3fff'ffffU);
    CHECK_FALSE(goldsrc::NetchanSequence::from_numeric(0x4000'0000U).has_value());
    CHECK_FALSE(
        goldsrc::NetchanSequence::from_numeric(std::numeric_limits<std::uint32_t>::max())
            .has_value());
}

TEST_CASE("Netchan sequence words separate captured flags from numeric sequence",
          "[goldsrc][netchan][sequence][capture]")
{
    STATIC_CHECK(goldsrc::kNetchanSequenceMask == 0x3fff'ffffU);
    STATIC_CHECK(goldsrc::kNetchanReliableSequenceFlag == 0x8000'0000U);
    STATIC_CHECK(goldsrc::kNetchanFragmentSequenceFlag == 0x4000'0000U);
    STATIC_CHECK(
        (goldsrc::kNetchanSequenceMask | goldsrc::kNetchanReliableSequenceFlag |
         goldsrc::kNetchanFragmentSequenceFlag) ==
        std::numeric_limits<std::uint32_t>::max());

    const auto captured_server_word = goldsrc::decode_netchan_sequence_word(0xc000'0001U);
    CHECK(captured_server_word.sequence.value() == 1U);
    CHECK(captured_server_word.flags.reliable);
    CHECK(captured_server_word.flags.fragmented);

    const auto captured_client_word = goldsrc::decode_netchan_sequence_word(0x8000'0001U);
    CHECK(captured_client_word.sequence.value() == 1U);
    CHECK(captured_client_word.flags.reliable);
    CHECK_FALSE(captured_client_word.flags.fragmented);

    CHECK(
        goldsrc::encode_netchan_sequence_word(
            sequence(1U),
            goldsrc::NetchanSequenceFlags{true, true}) ==
        0xc000'0001U);
}

TEST_CASE("Netchan sequence comparison handles equal next and older values",
          "[goldsrc][netchan][sequence]")
{
    using Comparison = goldsrc::NetchanSequenceComparison;

    CHECK(goldsrc::compare_sequences(sequence(17U), sequence(17U)) == Comparison::equal);
    CHECK_FALSE(goldsrc::is_sequence_newer(sequence(17U), sequence(17U)));
    CHECK(goldsrc::sequence_distance(sequence(17U), sequence(17U)) == 0U);

    CHECK(goldsrc::compare_sequences(sequence(18U), sequence(17U)) == Comparison::newer);
    CHECK(goldsrc::is_sequence_newer(sequence(18U), sequence(17U)));
    CHECK(goldsrc::sequence_distance(sequence(18U), sequence(17U)) == 1U);

    CHECK(goldsrc::compare_sequences(sequence(16U), sequence(17U)) == Comparison::older);
    CHECK_FALSE(goldsrc::is_sequence_newer(sequence(16U), sequence(17U)));
    CHECK(
        goldsrc::sequence_distance(sequence(16U), sequence(17U)) ==
        goldsrc::kNetchanSequenceMask);
}

TEST_CASE("Netchan sequence comparison is wrap-safe in both directions",
          "[goldsrc][netchan][sequence]")
{
    using Comparison = goldsrc::NetchanSequenceComparison;
    const auto maximum = sequence(goldsrc::kNetchanSequenceMask);
    const auto zero = sequence(0U);

    CHECK(goldsrc::sequence_distance(zero, maximum) == 1U);
    CHECK(goldsrc::compare_sequences(zero, maximum) == Comparison::newer);
    CHECK(goldsrc::is_sequence_newer(zero, maximum));

    CHECK(
        goldsrc::sequence_distance(maximum, zero) ==
        goldsrc::kNetchanSequenceMask);
    CHECK(goldsrc::compare_sequences(maximum, zero) == Comparison::older);
    CHECK_FALSE(goldsrc::is_sequence_newer(maximum, zero));
}

TEST_CASE("Netchan sequence comparison exposes half-range ambiguity",
          "[goldsrc][netchan][sequence]")
{
    using Comparison = goldsrc::NetchanSequenceComparison;
    const auto zero = sequence(0U);
    const auto half_range = sequence(goldsrc::kNetchanSequenceHalfRange);

    CHECK(
        goldsrc::sequence_distance(half_range, zero) ==
        goldsrc::kNetchanSequenceHalfRange);
    CHECK(
        goldsrc::sequence_distance(zero, half_range) ==
        goldsrc::kNetchanSequenceHalfRange);
    CHECK(
        goldsrc::compare_sequences(half_range, zero) ==
        Comparison::half_range_ambiguous);
    CHECK(
        goldsrc::compare_sequences(zero, half_range) ==
        Comparison::half_range_ambiguous);
    CHECK_FALSE(goldsrc::is_sequence_newer(half_range, zero));
    CHECK_FALSE(goldsrc::is_sequence_newer(zero, half_range));
}

TEST_CASE("Netchan wire flags never participate in numeric comparison",
          "[goldsrc][netchan][sequence]")
{
    using Comparison = goldsrc::NetchanSequenceComparison;
    const auto plain = goldsrc::decode_netchan_sequence_word(5U);
    const auto reliable = goldsrc::decode_netchan_sequence_word(0x8000'0005U);
    const auto fragmented = goldsrc::decode_netchan_sequence_word(0x4000'0005U);
    const auto both = goldsrc::decode_netchan_sequence_word(0xc000'0005U);

    CHECK(plain.sequence == reliable.sequence);
    CHECK(plain.sequence == fragmented.sequence);
    CHECK(plain.sequence == both.sequence);
    CHECK(goldsrc::compare_sequences(reliable.sequence, plain.sequence) == Comparison::equal);
    CHECK(goldsrc::compare_sequences(fragmented.sequence, plain.sequence) == Comparison::equal);
    CHECK(goldsrc::compare_sequences(both.sequence, plain.sequence) == Comparison::equal);
}

} // namespace
