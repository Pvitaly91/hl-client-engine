#include <hlclient/goldsrc/netchan_reassembly.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <span>
#include <utility>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] std::vector<std::byte> bytes(
    const std::initializer_list<std::uint8_t> values)
{
    std::vector<std::byte> output;
    output.reserve(values.size());
    std::ranges::transform(values, std::back_inserter(output), [](const std::uint8_t value) {
        return std::byte{value};
    });
    return output;
}

[[nodiscard]] bool equal_bytes(
    const std::span<const std::byte> left,
    const std::span<const std::byte> right)
{
    return std::ranges::equal(left, right);
}

void check_error(
    const goldsrc::NetchanNormalReassemblyResult& result,
    const goldsrc::NetchanNormalReassemblyErrorCode expected)
{
    INFO("expected " << goldsrc::to_string(expected));
    REQUIRE_FALSE(result);
    CHECK(result.status == goldsrc::NetchanNormalReassemblyStatus::failed);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
    CHECK_FALSE(result.error->context.empty());
    CHECK(result.error->context.size() <= goldsrc::kNetchanReassemblyDiagnosticTextLimit);
    CHECK_FALSE(result.payload);
}

TEST_CASE("A single normal fragment completes with owning bytes",
          "[goldsrc][netchan][fragments]")
{
    goldsrc::NetchanNormalReassembly reassembly;
    auto source = bytes({0x10U, 0x20U, 0x30U});
    const auto result = reassembly.add_fragment(1U, 1U, source);

    REQUIRE(result);
    CHECK(result.status == goldsrc::NetchanNormalReassemblyStatus::complete);
    REQUIRE(result.payload);
    CHECK(result.payload->fragment_count == 1U);
    CHECK(equal_bytes(result.payload->bytes, source));
    source[0] = std::byte{0xffU};
    CHECK(result.payload->bytes.front() == std::byte{0x10U});
    CHECK_FALSE(reassembly.active());
}

TEST_CASE("Normal fragments concatenate in index order regardless of arrival order",
          "[goldsrc][netchan][fragments]")
{
    const auto first = bytes({0x10U, 0x11U});
    const auto second = bytes({0x20U});
    const auto third = bytes({0x30U, 0x31U, 0x32U});
    const auto expected = bytes({0x10U, 0x11U, 0x20U, 0x30U, 0x31U, 0x32U});

    SECTION("in order")
    {
        goldsrc::NetchanNormalReassembly reassembly;
        CHECK(reassembly.add_fragment(1U, 3U, first));
        CHECK(reassembly.add_fragment(2U, 3U, second));
        const auto complete = reassembly.add_fragment(3U, 3U, third);
        REQUIRE(complete.payload);
        CHECK(equal_bytes(complete.payload->bytes, expected));
    }

    SECTION("out of order")
    {
        goldsrc::NetchanNormalReassembly reassembly;
        CHECK(reassembly.add_fragment(3U, 3U, third));
        CHECK(reassembly.add_fragment(1U, 3U, first));
        const auto complete = reassembly.add_fragment(2U, 3U, second);
        REQUIRE(complete.payload);
        CHECK(equal_bytes(complete.payload->bytes, expected));
    }
}

TEST_CASE("Captured five-fragment shape reassembles a synthetic 4186-byte payload",
          "[goldsrc][netchan][fragments][capture]")
{
    constexpr std::array<std::size_t, 5U> sizes{1'024U, 1'024U, 1'024U, 1'024U, 90U};
    std::array<std::vector<std::byte>, 5U> fragments;
    std::vector<std::byte> expected;
    expected.reserve(4'186U);
    for (std::size_t index = 0U; index < fragments.size(); ++index) {
        fragments[index].assign(sizes[index], std::byte{static_cast<std::uint8_t>(index + 1U)});
        expected.insert(expected.end(), fragments[index].begin(), fragments[index].end());
    }

    goldsrc::NetchanNormalReassembly reassembly;
    for (const auto index : std::array<std::size_t, 5U>{2U, 0U, 4U, 1U, 3U}) {
        const auto update = reassembly.add_fragment(
            static_cast<std::uint16_t>(index + 1U),
            5U,
            fragments[index]);
        REQUIRE(update);
        if (index != 3U) {
            CHECK_FALSE(update.payload);
        } else {
            REQUIRE(update.payload);
            CHECK(update.payload->bytes.size() == 4'186U);
            CHECK(equal_bytes(update.payload->bytes, expected));
        }
    }
}

TEST_CASE("Exact duplicate fragments are harmless but conflicts are typed",
          "[goldsrc][netchan][fragments][duplicate]")
{
    goldsrc::NetchanNormalReassembly reassembly;
    const auto original = bytes({0x10U, 0x11U});
    const auto conflict = bytes({0x10U, 0x12U});
    REQUIRE(reassembly.add_fragment(1U, 2U, original));

    const auto duplicate = reassembly.add_fragment(1U, 2U, original);
    REQUIRE(duplicate);
    CHECK(duplicate.status == goldsrc::NetchanNormalReassemblyStatus::duplicate_ignored);
    CHECK(reassembly.received_fragment_count() == 1U);
    CHECK(reassembly.accumulated_size() == original.size());

    check_error(
        reassembly.add_fragment(1U, 2U, conflict),
        goldsrc::NetchanNormalReassemblyErrorCode::conflicting_duplicate);
    CHECK(reassembly.received_fragment_count() == 1U);
    CHECK(reassembly.accumulated_size() == original.size());
}

TEST_CASE("A gap stays pending until its missing index arrives",
          "[goldsrc][netchan][fragments][gap]")
{
    goldsrc::NetchanNormalReassembly reassembly;
    REQUIRE(reassembly.add_fragment(1U, 3U, bytes({0x10U})));
    const auto gap = reassembly.add_fragment(3U, 3U, bytes({0x30U}));
    REQUIRE(gap);
    CHECK(gap.status == goldsrc::NetchanNormalReassemblyStatus::fragment_accepted);
    CHECK_FALSE(gap.payload);
    CHECK(reassembly.active());
    CHECK(reassembly.received_fragment_count() == 2U);

    const auto complete = reassembly.add_fragment(2U, 3U, bytes({0x20U}));
    REQUIRE(complete.payload);
    CHECK(equal_bytes(complete.payload->bytes, bytes({0x10U, 0x20U, 0x30U})));
}

TEST_CASE("Normal fragment identifiers and active count are strictly validated",
          "[goldsrc][netchan][fragments][bounds]")
{
    const auto fragment = bytes({0x10U});

    SECTION("zero count")
    {
        goldsrc::NetchanNormalReassembly reassembly;
        check_error(
            reassembly.add_fragment(1U, 0U, fragment),
            goldsrc::NetchanNormalReassemblyErrorCode::zero_fragment_count);
    }
    SECTION("zero index")
    {
        goldsrc::NetchanNormalReassembly reassembly;
        check_error(
            reassembly.add_fragment(0U, 1U, fragment),
            goldsrc::NetchanNormalReassemblyErrorCode::zero_fragment_index);
    }
    SECTION("index exceeds count")
    {
        goldsrc::NetchanNormalReassembly reassembly;
        check_error(
            reassembly.add_fragment(3U, 2U, fragment),
            goldsrc::NetchanNormalReassemblyErrorCode::fragment_index_out_of_range);
    }
    SECTION("active count mismatch cannot replace the transfer")
    {
        goldsrc::NetchanNormalReassembly reassembly;
        REQUIRE(reassembly.add_fragment(1U, 2U, fragment));
        check_error(
            reassembly.add_fragment(1U, 3U, fragment),
            goldsrc::NetchanNormalReassemblyErrorCode::active_fragment_count_mismatch);
        REQUIRE(reassembly.expected_fragment_count());
        CHECK(*reassembly.expected_fragment_count() == 2U);
        CHECK(reassembly.received_fragment_count() == 1U);
    }
}

TEST_CASE("Empty and excessive fragments are rejected before copying",
          "[goldsrc][netchan][fragments][bounds]")
{
    goldsrc::NetchanNormalReassembly reassembly;

    check_error(
        reassembly.add_fragment(1U, 1U, {}),
        goldsrc::NetchanNormalReassemblyErrorCode::empty_fragment);

    const std::vector<std::byte> excessive(
        goldsrc::kMaximumNormalFragmentPayloadSize + 1U,
        std::byte{0x10U});
    check_error(
        reassembly.add_fragment(1U, 1U, excessive),
        goldsrc::NetchanNormalReassemblyErrorCode::fragment_too_large);
    CHECK_FALSE(reassembly.active());
}

TEST_CASE("Fragment count and per-fragment hard ceilings accept limit and reject limit plus one",
          "[goldsrc][netchan][fragments][bounds]")
{
    const auto one = bytes({0x10U});
    goldsrc::NetchanNormalReassembly count_limit;
    REQUIRE(count_limit.add_fragment(
        1U,
        static_cast<std::uint16_t>(goldsrc::kMaximumNormalFragmentsPerMessage),
        one));
    count_limit.reset();
    check_error(
        count_limit.add_fragment(
            1U,
            static_cast<std::uint16_t>(goldsrc::kMaximumNormalFragmentsPerMessage + 1U),
            one),
        goldsrc::NetchanNormalReassemblyErrorCode::fragment_count_too_large);

    std::vector<std::byte> at_limit(
        goldsrc::kMaximumNormalFragmentPayloadSize,
        std::byte{0x11U});
    goldsrc::NetchanNormalReassembly fragment_limit;
    REQUIRE(fragment_limit.add_fragment(1U, 2U, at_limit));
    at_limit.push_back(std::byte{0x12U});
    check_error(
        fragment_limit.add_fragment(2U, 2U, at_limit),
        goldsrc::NetchanNormalReassemblyErrorCode::fragment_too_large);
    CHECK(fragment_limit.received_fragment_count() == 1U);
}

TEST_CASE("Reassembled message hard ceiling accepts one MiB and rejects one byte more",
          "[goldsrc][netchan][fragments][bounds]")
{
    constexpr std::uint16_t fragment_count = 64U;
    std::vector<std::byte> fragment(
        goldsrc::kMaximumNormalFragmentPayloadSize,
        std::byte{0x31U});
    goldsrc::NetchanNormalReassembly exact;
    for (std::uint16_t index = 1U; index <= fragment_count; ++index) {
        const auto result = exact.add_fragment(index, fragment_count, fragment);
        REQUIRE(result);
        if (index == fragment_count) {
            REQUIRE(result.payload);
            CHECK(result.payload->bytes.size() ==
                  goldsrc::kMaximumNormalReassembledMessageSize);
        }
    }

    goldsrc::NetchanNormalReassemblyLimits smaller_limits;
    smaller_limits.maximum_reassembled_size =
        goldsrc::kMaximumNormalFragmentPayloadSize;
    goldsrc::NetchanNormalReassembly over{smaller_limits};
    REQUIRE(over.add_fragment(1U, 2U, fragment));
    check_error(
        over.add_fragment(2U, 2U, bytes({0x01U})),
        goldsrc::NetchanNormalReassemblyErrorCode::message_too_large);
    CHECK(over.accumulated_size() == fragment.size());
}

TEST_CASE("Invalid configurable limits fail without allocating transfer state",
          "[goldsrc][netchan][fragments][configuration]")
{
    const auto fragment = bytes({0x10U});
    const std::array invalid{
        goldsrc::NetchanNormalReassemblyLimits{0U, 1U, 1U},
        goldsrc::NetchanNormalReassemblyLimits{
            goldsrc::kMaximumNormalReassembledMessageSize + 1U, 1U, 1U},
        goldsrc::NetchanNormalReassemblyLimits{1U, 0U, 1U},
        goldsrc::NetchanNormalReassemblyLimits{
            1U, goldsrc::kMaximumNormalFragmentsPerMessage + 1U, 1U},
        goldsrc::NetchanNormalReassemblyLimits{1U, 1U, 0U},
        goldsrc::NetchanNormalReassemblyLimits{
            1U, 1U, goldsrc::kMaximumNormalFragmentPayloadSize + 1U},
    };
    for (const auto limits : invalid) {
        goldsrc::NetchanNormalReassembly reassembly{limits};
        check_error(
            reassembly.add_fragment(1U, 1U, fragment),
            goldsrc::NetchanNormalReassemblyErrorCode::invalid_configuration);
        CHECK_FALSE(reassembly.active());
    }
}

TEST_CASE("Reset and completed ownership isolate consecutive transfers",
          "[goldsrc][netchan][fragments][lifetime]")
{
    goldsrc::NetchanNormalReassembly reassembly;
    REQUIRE(reassembly.add_fragment(1U, 2U, bytes({0x10U})));
    reassembly.reset();
    CHECK_FALSE(reassembly.active());
    CHECK(reassembly.received_fragment_count() == 0U);
    CHECK(reassembly.accumulated_size() == 0U);

    auto first_complete = reassembly.add_fragment(1U, 1U, bytes({0x20U, 0x21U}));
    REQUIRE(first_complete.payload);
    auto owned = std::move(first_complete.payload->bytes);
    auto second_complete = reassembly.add_fragment(1U, 1U, bytes({0x30U}));
    REQUIRE(second_complete.payload);
    CHECK(equal_bytes(owned, bytes({0x20U, 0x21U})));
    CHECK(equal_bytes(second_complete.payload->bytes, bytes({0x30U})));
}

TEST_CASE("Secondary fragment stream is an opaque non-persistent M3 boundary",
          "[goldsrc][netchan][fragments][secondary]")
{
    CHECK(
        goldsrc::classify_netchan_fragment_stream(0U) ==
        goldsrc::NetchanFragmentStreamDisposition::normal);
    CHECK(
        goldsrc::classify_netchan_fragment_stream(1U) ==
        goldsrc::NetchanFragmentStreamDisposition::secondary_stream_pending_m3);
    CHECK(
        goldsrc::classify_netchan_fragment_stream(2U) ==
        goldsrc::NetchanFragmentStreamDisposition::invalid_slot);
}

} // namespace
