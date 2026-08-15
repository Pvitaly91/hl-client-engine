#include <hlclient/goldsrc/netchan_reassembly.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;
using namespace std::chrono_literals;

template<typename Rep, typename Period>
[[nodiscard]] goldsrc::NetchanFragmentTimePoint at(
    const std::chrono::duration<Rep, Period> elapsed)
{
    return goldsrc::NetchanFragmentTimePoint{} +
           std::chrono::duration_cast<goldsrc::NetchanFragmentClock::duration>(
               elapsed);
}

[[nodiscard]] std::vector<std::byte> bytes(
    const std::initializer_list<std::uint8_t> values)
{
    std::vector<std::byte> output;
    output.reserve(values.size());
    std::ranges::transform(
        values,
        std::back_inserter(output),
        [](const std::uint8_t value) { return std::byte{value}; });
    return output;
}

[[nodiscard]] std::vector<std::byte> repeated(
    const std::size_t size,
    const std::uint8_t value)
{
    return std::vector<std::byte>(size, std::byte{value});
}

[[nodiscard]] goldsrc::NetchanFragmentDescriptor descriptor(
    const std::uint16_t index,
    const std::uint16_t count,
    const std::uint16_t length,
    const std::uint8_t slot = 0U,
    const std::uint16_t packet_payload_offset = 0U)
{
    return goldsrc::NetchanFragmentDescriptor{
        slot,
        (static_cast<std::uint32_t>(index) << 16U) |
            static_cast<std::uint32_t>(count),
        packet_payload_offset,
        length,
        packet_payload_offset,
    };
}

template<typename Result>
void check_error(
    const Result& result,
    const goldsrc::NetchanReassemblyErrorCode expected)
{
    INFO("expected " << goldsrc::to_string(expected));
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
}

[[nodiscard]] goldsrc::NetchanFragmentInsertCommitResult insert(
    goldsrc::NetchanNormalReassembler& reassembler,
    const goldsrc::NetchanFragmentDescriptor& fragment_descriptor,
    const std::span<const std::byte> payload,
    const goldsrc::NetchanFragmentTimePoint now = {})
{
    auto prepared = reassembler.prepare_insert(fragment_descriptor, payload, now);
    REQUIRE(prepared);
    REQUIRE(prepared.plan);
    auto committed = reassembler.commit_insert(std::move(*prepared.plan));
    REQUIRE(committed);
    return committed;
}

struct ReassemblySnapshot {
    bool active{false};
    std::uint64_t transfer_id{0U};
    std::uint16_t declared_count{0U};
    std::size_t received_count{0U};
    std::size_t covered_size{0U};
    std::optional<std::size_t> total_size;
    goldsrc::NetchanFragmentTimePoint created_at{};
    goldsrc::NetchanFragmentTimePoint last_fragment_at{};
    goldsrc::NetchanFragmentTimePoint deadline{};
    std::vector<goldsrc::NetchanFragmentRange> ranges;

    friend bool operator==(
        const ReassemblySnapshot& left,
        const ReassemblySnapshot& right) noexcept = default;
};

[[nodiscard]] ReassemblySnapshot snapshot(
    const goldsrc::NetchanNormalReassembler& reassembler)
{
    ReassemblySnapshot result;
    result.ranges = reassembler.ranges();
    if (reassembler.active_transfer()) {
        const auto& active = *reassembler.active_transfer();
        result.active = true;
        result.transfer_id = active.transfer_id.value();
        result.declared_count = active.declared_fragment_count;
        result.received_count = active.received_fragment_count;
        result.covered_size = active.covered_size;
        result.total_size = active.total_size;
        result.created_at = active.created_at;
        result.last_fragment_at = active.last_fragment_at;
        result.deadline = active.deadline;
    }
    return result;
}

TEST_CASE("Normal reassembly limits are named hard bounded and validated",
          "[goldsrc][netchan][fragment][reassembly][limits]")
{
    STATIC_CHECK(goldsrc::kDefaultMaximumNetchanFragmentPayloadSize > 0U);
    STATIC_CHECK(goldsrc::kMaximumNetchanFragmentPayloadSize > 0U);
    STATIC_CHECK(goldsrc::kDefaultMaximumNetchanNormalTransferSize > 0U);
    STATIC_CHECK(goldsrc::kMaximumNetchanNormalTransferSize > 0U);
    STATIC_CHECK(goldsrc::kDefaultMaximumNetchanFragmentsPerTransfer > 0U);
    STATIC_CHECK(goldsrc::kMaximumNetchanFragmentsPerTransfer > 0U);
    STATIC_CHECK(goldsrc::kMaximumActiveNormalTransfers == 1U);
    STATIC_CHECK_FALSE(
        std::is_copy_constructible_v<goldsrc::NetchanFragmentInsertPlan>);
    STATIC_CHECK(
        std::is_move_constructible_v<goldsrc::NetchanFragmentInsertPlan>);

    CHECK(goldsrc::NetchanNormalReassembler{}.valid_configuration());

    auto limits = goldsrc::NetchanReassemblyLimits{};
    limits.maximum_fragment_payload_size = 0U;
    CHECK_FALSE(goldsrc::NetchanNormalReassembler{limits}.valid_configuration());
    limits = {};
    limits.maximum_fragment_payload_size =
        goldsrc::kMaximumNetchanFragmentPayloadSize + 1U;
    CHECK_FALSE(goldsrc::NetchanNormalReassembler{limits}.valid_configuration());
    limits = {};
    limits.maximum_normal_transfer_size = 0U;
    CHECK_FALSE(goldsrc::NetchanNormalReassembler{limits}.valid_configuration());
    limits = {};
    limits.maximum_normal_transfer_size =
        goldsrc::kMaximumNetchanNormalTransferSize + 1U;
    CHECK_FALSE(goldsrc::NetchanNormalReassembler{limits}.valid_configuration());
    limits = {};
    limits.maximum_fragments_per_transfer = 0U;
    CHECK_FALSE(goldsrc::NetchanNormalReassembler{limits}.valid_configuration());
    limits = {};
    limits.maximum_fragments_per_transfer =
        goldsrc::kMaximumNetchanFragmentsPerTransfer + 1U;
    CHECK_FALSE(goldsrc::NetchanNormalReassembler{limits}.valid_configuration());
    limits = {};
    limits.maximum_active_normal_transfers = 0U;
    CHECK_FALSE(goldsrc::NetchanNormalReassembler{limits}.valid_configuration());
    limits = {};
    limits.maximum_active_normal_transfers =
        goldsrc::kMaximumActiveNormalTransfers + 1U;
    CHECK_FALSE(goldsrc::NetchanNormalReassembler{limits}.valid_configuration());
    limits = {};
    limits.maximum_fragment_ranges = 0U;
    CHECK_FALSE(goldsrc::NetchanNormalReassembler{limits}.valid_configuration());
    limits = {};
    limits.fragment_timeout = 0ms;
    CHECK_FALSE(goldsrc::NetchanNormalReassembler{limits}.valid_configuration());
    limits = {};
    limits.fragment_timeout = goldsrc::kMaximumNetchanFragmentTimeout + 1ms;
    CHECK_FALSE(goldsrc::NetchanNormalReassembler{limits}.valid_configuration());
}

TEST_CASE("One-fragment completion is prepared without mutation and owns bytes once",
          "[goldsrc][netchan][fragment][reassembly][transaction]")
{
    goldsrc::NetchanNormalReassembler reassembler;
    const auto payload = bytes({0x11U, 0x22U, 0x33U});

    auto prepared = reassembler.prepare_insert(
        descriptor(1U, 1U, 3U), payload, goldsrc::NetchanFragmentTimePoint{});
    REQUIRE(prepared);
    REQUIRE(prepared.plan);
    CHECK(prepared.plan->disposition() ==
          goldsrc::NetchanFragmentInsertDisposition::completed);
    CHECK(prepared.plan->required_event_count() == 2U);
    REQUIRE(prepared.plan->completion_size());
    CHECK(*prepared.plan->completion_size() == payload.size());
    CHECK_FALSE(reassembler.active_transfer());
    CHECK(reassembler.ranges().empty());

    auto committed = reassembler.commit_insert(std::move(*prepared.plan));
    REQUIRE(committed);
    REQUIRE(committed.receipt);
    REQUIRE(committed.completion);
    CHECK(committed.receipt->started_transfer);
    CHECK(committed.completion->payload == payload);
    CHECK_FALSE(reassembler.active_transfer());
    CHECK(reassembler.ranges().empty());

    const auto owned = committed.completion->payload;
    reassembler.clear();
    CHECK(committed.completion->payload == owned);

    const auto double_commit =
        reassembler.commit_insert(std::move(*prepared.plan));
    check_error(
        double_commit,
        goldsrc::NetchanReassemblyErrorCode::stale_insert_plan);
}

TEST_CASE("Normal reassembly accepts bounded project-policy out-of-order fragments",
          "[goldsrc][netchan][fragment][reassembly][out-of-order]")
{
    // Stock TX is captured as stop-and-wait, so unseen reordering cannot occur
    // naturally. This receiver behavior is an explicitly bounded project
    // policy required by the fake transport contract.
    const auto first = repeated(
        goldsrc::kStockProtocol48NormalFragmentChunkSize,
        0x11U);
    const auto middle = repeated(
        goldsrc::kStockProtocol48NormalFragmentChunkSize,
        0x22U);
    const auto final = bytes({0x31U, 0x32U, 0x33U});

    SECTION("final arrives first")
    {
        goldsrc::NetchanNormalReassembler reassembler;
        const auto final_result = insert(
            reassembler, descriptor(3U, 3U, 3U), final);
        REQUIRE(final_result.receipt);
        CHECK(final_result.receipt->disposition ==
              goldsrc::NetchanFragmentInsertDisposition::transfer_started);
        REQUIRE(reassembler.active_transfer());
        CHECK(reassembler.active_transfer()->total_size == 2'051U);
        CHECK(reassembler.ranges().front().transfer_offset == 2'048U);

        static_cast<void>(
            insert(reassembler, descriptor(1U, 3U, 1'024U), first, at(1ms)));
        const auto completed =
            insert(reassembler, descriptor(2U, 3U, 1'024U), middle, at(2ms));
        REQUIRE(completed.completion);
        auto expected = first;
        expected.insert(expected.end(), middle.begin(), middle.end());
        expected.insert(expected.end(), final.begin(), final.end());
        CHECK(completed.completion->payload == expected);
    }

    SECTION("middle arrives first and ranges stay sorted")
    {
        goldsrc::NetchanNormalReassembler reassembler;
        static_cast<void>(
            insert(reassembler, descriptor(2U, 3U, 1'024U), middle));
        static_cast<void>(
            insert(reassembler, descriptor(3U, 3U, 3U), final, at(1ms)));
        REQUIRE(reassembler.active_transfer());
        CHECK(reassembler.ranges().size() == 2U);
        CHECK(reassembler.ranges()[0].fragment_index == 2U);
        CHECK(reassembler.ranges()[1].fragment_index == 3U);
        CHECK(reassembler.active_transfer()->covered_size == 1'027U);

        const auto completed =
            insert(reassembler, descriptor(1U, 3U, 1'024U), first, at(2ms));
        REQUIRE(completed.completion);
        CHECK(completed.completion->payload.size() == 2'051U);
    }

    SECTION("missing middle never delivers a prefix")
    {
        goldsrc::NetchanNormalReassembler reassembler;
        static_cast<void>(
            insert(reassembler, descriptor(1U, 3U, 1'024U), first));
        const auto result =
            insert(reassembler, descriptor(3U, 3U, 3U), final, at(1ms));
        CHECK_FALSE(result.completion);
        REQUIRE(reassembler.active_transfer());
        CHECK(reassembler.active_transfer()->received_fragment_count == 2U);
        CHECK(reassembler.active_transfer()->total_size == 2'051U);
    }
}

TEST_CASE("Normal reassembly completes an ordered multi-fragment stock shape",
          "[goldsrc][netchan][fragment][reassembly][ordered]")
{
    goldsrc::NetchanNormalReassembler reassembler;
    const auto first = repeated(1'024U, 0x35U);
    const auto middle = repeated(1'024U, 0x36U);
    const auto final = repeated(90U, 0x37U);

    const auto started =
        insert(reassembler, descriptor(1U, 3U, 1'024U), first);
    REQUIRE(started.receipt);
    CHECK(started.receipt->disposition ==
          goldsrc::NetchanFragmentInsertDisposition::transfer_started);
    const auto progressed =
        insert(reassembler, descriptor(2U, 3U, 1'024U), middle, at(1ms));
    REQUIRE(progressed.receipt);
    CHECK(progressed.receipt->disposition ==
          goldsrc::NetchanFragmentInsertDisposition::inserted);
    const auto completed =
        insert(reassembler, descriptor(3U, 3U, 90U), final, at(2ms));
    REQUIRE(completed.completion);
    CHECK(completed.completion->payload.size() == 2'138U);
}

TEST_CASE("Exact retransmission is byte-idempotent and revision-neutral",
          "[goldsrc][netchan][fragment][reassembly][duplicate][transaction]")
{
    goldsrc::NetchanNormalReassembler reassembler;
    const auto first = repeated(1'024U, 0x41U);
    const auto final = bytes({0x51U, 0x52U});
    static_cast<void>(
        insert(reassembler, descriptor(1U, 2U, 1'024U), first));
    const auto before = snapshot(reassembler);

    auto duplicate = reassembler.prepare_insert(
        descriptor(1U, 2U, 1'024U), first, at(1ms));
    REQUIRE(duplicate);
    REQUIRE(duplicate.plan);
    CHECK(duplicate.plan->exact_retransmission());
    CHECK(duplicate.plan->required_event_count() == 0U);
    CHECK_FALSE(duplicate.plan->completion_size());

    // A state-changing sibling prepared at the same revision remains valid:
    // committing the duplicate copies no bytes and mutates no revision/time.
    auto sibling =
        reassembler.prepare_insert(descriptor(2U, 2U, 2U), final, at(1ms));
    REQUIRE(sibling);
    REQUIRE(sibling.plan);
    const auto duplicate_commit =
        reassembler.commit_insert(std::move(*duplicate.plan));
    REQUIRE(duplicate_commit);
    CHECK_FALSE(duplicate_commit.completion);
    CHECK(snapshot(reassembler) == before);

    auto completed = reassembler.commit_insert(std::move(*sibling.plan));
    REQUIRE(completed);
    REQUIRE(completed.completion);
    CHECK(completed.completion->payload.size() == 1'026U);

    // After a multi-fragment completion, a later non-first ordinal is
    // intrinsically ambiguous on this wire and is rejected by the metadata-
    // only tombstone; same packet-sequence duplicates are filtered earlier.
    const auto after_completion = reassembler.prepare_insert(
        descriptor(2U, 2U, 2U), final, at(2ms));
    check_error(
        after_completion,
        goldsrc::NetchanReassemblyErrorCode::old_fragment_after_completion);
}

TEST_CASE("Conflicting fragments and malformed descriptors never mutate reassembly",
          "[goldsrc][netchan][fragment][reassembly][malformed]")
{
    goldsrc::NetchanNormalReassembler reassembler;
    const auto first = repeated(1'024U, 0x61U);
    static_cast<void>(
        insert(reassembler, descriptor(1U, 2U, 1'024U), first));
    const auto before = snapshot(reassembler);

    auto conflict = first;
    conflict.back() = std::byte{0x62U};
    check_error(
        reassembler.prepare_insert(
            descriptor(1U, 2U, 1'024U), conflict, at(1ms)),
        goldsrc::NetchanReassemblyErrorCode::conflicting_duplicate);
    CHECK(snapshot(reassembler) == before);

    check_error(
        reassembler.prepare_insert(
            descriptor(1U, 3U, 1'024U), first, at(1ms)),
        goldsrc::NetchanReassemblyErrorCode::transfer_replacement_rejected);
    CHECK(snapshot(reassembler) == before);

    check_error(
        reassembler.prepare_insert(
            descriptor(2U, 2U, 1U, 0U, 1U), bytes({0x71U}), at(1ms)),
        goldsrc::NetchanReassemblyErrorCode::invalid_fragment_offset);
    CHECK(snapshot(reassembler) == before);

    check_error(
        reassembler.prepare_insert(
            descriptor(2U, 2U, 2U), bytes({0x71U}), at(1ms)),
        goldsrc::NetchanReassemblyErrorCode::fragment_payload_size_mismatch);
    CHECK(snapshot(reassembler) == before);

    check_error(
        reassembler.prepare_insert(
            descriptor(0U, 2U, 1U), bytes({0x71U}), at(1ms)),
        goldsrc::NetchanReassemblyErrorCode::invalid_packed_fragment_id);
    CHECK(snapshot(reassembler) == before);

    check_error(
        reassembler.prepare_insert(
            descriptor(1U, 1U, 1U, 1U), bytes({0x71U}), at(1ms)),
        goldsrc::NetchanReassemblyErrorCode::secondary_stream_pending_m3);
    CHECK(snapshot(reassembler) == before);
}

TEST_CASE("Changed duplicate boundaries are distinguished from changed bytes",
          "[goldsrc][netchan][fragment][reassembly][overlap]")
{
    goldsrc::NetchanNormalReassembler reassembler;
    const auto final = bytes({0x73U, 0x74U});
    static_cast<void>(
        insert(reassembler, descriptor(2U, 2U, 2U), final));
    const auto before = snapshot(reassembler);

    check_error(
        reassembler.prepare_insert(
            descriptor(2U, 2U, 1U), bytes({0x73U}), at(1ms)),
        goldsrc::NetchanReassemblyErrorCode::
            complete_overlap_with_different_boundaries);
    CHECK(snapshot(reassembler) == before);

    // Partial transfer-range overlap cannot be represented by the captured
    // fixed ordinal profile: transfer offsets are derived as (index-1)*1024.
    // A nonzero descriptor offset is packet-local and is rejected before a
    // transfer range exists instead of being reinterpreted as a permissive
    // transfer offset.
    check_error(
        reassembler.prepare_insert(
            descriptor(1U, 2U, 1'024U, 0U, 1U),
            repeated(1'024U, 0x75U),
            at(1ms)),
        goldsrc::NetchanReassemblyErrorCode::invalid_fragment_offset);
    CHECK(snapshot(reassembler) == before);
}

TEST_CASE("Final boundary count and transfer size limits fail closed",
          "[goldsrc][netchan][fragment][reassembly][bounds]")
{
    auto limits = goldsrc::NetchanReassemblyLimits{};
    limits.maximum_normal_transfer_size = 2'048U;
    limits.maximum_fragments_per_transfer = 2U;
    limits.maximum_fragment_ranges = 2U;
    goldsrc::NetchanNormalReassembler reassembler{limits};
    REQUIRE(reassembler.valid_configuration());

    const auto chunk = repeated(1'024U, 0x81U);
    static_cast<void>(
        insert(reassembler, descriptor(1U, 2U, 1'024U), chunk));
    const auto completed =
        insert(reassembler, descriptor(2U, 2U, 1'024U), chunk, at(1ms));
    REQUIRE(completed.completion);
    CHECK(completed.completion->payload.size() == 2'048U);

    reassembler.clear();
    check_error(
        reassembler.prepare_insert(
            descriptor(1U, 3U, 1'024U), chunk, at(2ms)),
        goldsrc::NetchanReassemblyErrorCode::too_many_fragments);

    auto size_limits = limits;
    size_limits.maximum_fragments_per_transfer = 3U;
    size_limits.maximum_fragment_ranges = 3U;
    goldsrc::NetchanNormalReassembler size_limited{size_limits};
    check_error(
        size_limited.prepare_insert(
            descriptor(1U, 3U, 1'024U), chunk, at(2ms)),
        goldsrc::NetchanReassemblyErrorCode::normal_transfer_too_large);
    check_error(
        size_limited.prepare_insert(
            descriptor(3U, 3U, 1U), bytes({0x83U}), at(2ms)),
        goldsrc::NetchanReassemblyErrorCode::normal_transfer_too_large);

    check_error(
        size_limited.prepare_insert(
            descriptor(1U, 2U, 1'025U), repeated(1'025U, 0x82U), at(2ms)),
        goldsrc::NetchanReassemblyErrorCode::invalid_fragment_length);
    check_error(
        size_limited.prepare_insert(
            descriptor(2U, 2U, 0U), {}, at(2ms)),
        goldsrc::NetchanReassemblyErrorCode::invalid_fragment_length);

    // Exact transfer-size boundary is arrival-order independent: index 2 may
    // start the bounded project-policy OOO lifecycle even though count*1024 is
    // larger than the eventual two-byte final boundary.
    auto exact_limits = goldsrc::NetchanReassemblyLimits{};
    exact_limits.maximum_normal_transfer_size = 2'050U;
    exact_limits.maximum_fragments_per_transfer = 3U;
    exact_limits.maximum_fragment_ranges = 3U;
    goldsrc::NetchanNormalReassembler exact_limited{exact_limits};
    static_cast<void>(insert(
        exact_limited,
        descriptor(2U, 3U, 1'024U),
        repeated(1'024U, 0x84U),
        at(3ms)));
    static_cast<void>(insert(
        exact_limited,
        descriptor(1U, 3U, 1'024U),
        repeated(1'024U, 0x85U),
        at(4ms)));
    const auto exact_completion = insert(
        exact_limited,
        descriptor(3U, 3U, 2U),
        bytes({0x86U, 0x87U}),
        at(5ms));
    REQUIRE(exact_completion.completion);
    CHECK(exact_completion.completion->payload.size() == 2'050U);

    goldsrc::NetchanNormalReassembler over_limited{exact_limits};
    static_cast<void>(insert(
        over_limited,
        descriptor(2U, 3U, 1'024U),
        repeated(1'024U, 0x88U),
        at(3ms)));
    const auto before_over_limit = snapshot(over_limited);
    check_error(
        over_limited.prepare_insert(
            descriptor(3U, 3U, 3U),
            bytes({0x89U, 0x8aU, 0x8bU}),
            at(4ms)),
        goldsrc::NetchanReassemblyErrorCode::normal_transfer_too_large);
    CHECK(snapshot(over_limited) == before_over_limit);
}

TEST_CASE("Lifecycle IDs replacement and plan identity are fail closed",
          "[goldsrc][netchan][fragment][reassembly][identity][transaction]")
{
    const auto first = repeated(1'024U, 0x91U);
    const auto final = bytes({0x92U});
    goldsrc::NetchanNormalReassembler left;
    goldsrc::NetchanNormalReassembler right;

    auto foreign =
        left.prepare_insert(descriptor(1U, 2U, 1'024U), first, {});
    REQUIRE(foreign);
    REQUIRE(foreign.plan);
    check_error(
        right.commit_insert(std::move(*foreign.plan)),
        goldsrc::NetchanReassemblyErrorCode::foreign_insert_plan);
    CHECK_FALSE(left.active_transfer());
    CHECK_FALSE(right.active_transfer());
    REQUIRE(left.commit_insert(std::move(*foreign.plan)));
    REQUIRE(left.active_transfer());
    left.clear();

    auto first_plan =
        left.prepare_insert(descriptor(1U, 2U, 1'024U), first, {});
    auto final_plan =
        left.prepare_insert(descriptor(2U, 2U, 1U), final, {});
    REQUIRE(first_plan);
    REQUIRE(first_plan.plan);
    REQUIRE(final_plan);
    REQUIRE(final_plan.plan);
    REQUIRE(left.commit_insert(std::move(*first_plan.plan)));
    check_error(
        left.commit_insert(std::move(*final_plan.plan)),
        goldsrc::NetchanReassemblyErrorCode::stale_insert_plan);

    const auto first_id = left.active_transfer()->transfer_id;
    const auto completed =
        insert(left, descriptor(2U, 2U, 1U), final, at(1ms));
    REQUIRE(completed.completion);
    CHECK(completed.completion->transfer_id == first_id);

    static_cast<void>(
        insert(left, descriptor(1U, 2U, 1'024U), first, at(2ms)));
    REQUIRE(left.active_transfer());
    CHECK(left.active_transfer()->transfer_id != first_id);

    auto invalidated =
        left.prepare_insert(descriptor(2U, 2U, 1U), final, at(3ms));
    REQUIRE(invalidated);
    REQUIRE(invalidated.plan);
    left.clear();
    check_error(
        left.commit_insert(std::move(*invalidated.plan)),
        goldsrc::NetchanReassemblyErrorCode::stale_insert_plan);
}

TEST_CASE("Ambiguous replacement is rejected without erasing valid bytes",
          "[goldsrc][netchan][fragment][reassembly][replacement]")
{
    goldsrc::NetchanNormalReassembler reassembler;
    const auto first = repeated(1'024U, 0xd1U);
    static_cast<void>(
        insert(reassembler, descriptor(1U, 3U, 1'024U), first));
    const auto before = snapshot(reassembler);

    // A different count is the only distinguishable replacement signal in
    // the captured field and is fail-closed while one transfer is active.
    check_error(
        reassembler.prepare_insert(
            descriptor(1U, 2U, 1'024U), first, at(1ms)),
        goldsrc::NetchanReassemblyErrorCode::transfer_replacement_rejected);
    CHECK(snapshot(reassembler) == before);

    // The same count has no stable wire identity. Equal bytes are the current
    // transfer's exact duplicate; changed bytes are a conflicting duplicate,
    // never a silent replacement.
    auto exact = reassembler.prepare_insert(
        descriptor(1U, 3U, 1'024U), first, at(1ms));
    REQUIRE(exact);
    REQUIRE(exact.plan);
    CHECK(exact.plan->exact_retransmission());
    auto changed = first;
    changed.front() = std::byte{0xd2U};
    check_error(
        reassembler.prepare_insert(
            descriptor(1U, 3U, 1'024U), changed, at(1ms)),
        goldsrc::NetchanReassemblyErrorCode::conflicting_duplicate);
    CHECK(snapshot(reassembler) == before);
}

TEST_CASE("Abandon destruction and errors preserve the complete snapshot",
          "[goldsrc][netchan][fragment][reassembly][transaction]")
{
    goldsrc::NetchanNormalReassembler reassembler;
    const auto first = repeated(1'024U, 0xa1U);
    const auto before = snapshot(reassembler);

    {
        auto destroyed =
            reassembler.prepare_insert(descriptor(1U, 2U, 1'024U), first, {});
        REQUIRE(destroyed);
        REQUIRE(destroyed.plan);
    }
    CHECK(snapshot(reassembler) == before);

    auto abandoned =
        reassembler.prepare_insert(descriptor(1U, 2U, 1'024U), first, {});
    REQUIRE(abandoned);
    REQUIRE(abandoned.plan);
    REQUIRE(reassembler.abandon_insert(std::move(*abandoned.plan)));
    CHECK(snapshot(reassembler) == before);
    check_error(
        reassembler.abandon_insert(std::move(*abandoned.plan)),
        goldsrc::NetchanReassemblyErrorCode::stale_insert_plan);
}

TEST_CASE("Fragment timeout is fixed at creation and emitted exactly once",
          "[goldsrc][netchan][fragment][reassembly][timeout]")
{
    auto limits = goldsrc::NetchanReassemblyLimits{};
    limits.fragment_timeout = 5s;
    goldsrc::NetchanNormalReassembler reassembler{limits};
    const auto start = goldsrc::NetchanFragmentTimePoint{} + 10s;
    const auto first = repeated(1'024U, 0xb1U);
    const auto middle = repeated(1'024U, 0xb2U);

    static_cast<void>(
        insert(reassembler, descriptor(1U, 3U, 1'024U), first, start));
    const auto fixed_deadline = start + 5s;
    REQUIRE(reassembler.active_transfer());
    CHECK(reassembler.active_transfer()->created_at == start);
    CHECK(reassembler.active_transfer()->deadline == fixed_deadline);

    static_cast<void>(insert(
        reassembler,
        descriptor(2U, 3U, 1'024U),
        middle,
        start + 4'999ms));
    REQUIRE(reassembler.active_transfer());
    CHECK(reassembler.active_transfer()->last_fragment_at == start + 4'999ms);
    CHECK(reassembler.active_transfer()->deadline == fixed_deadline);

    const auto before_duplicate = snapshot(reassembler);
    const auto duplicate = insert(
        reassembler,
        descriptor(2U, 3U, 1'024U),
        middle,
        start + 4'999ms);
    REQUIRE(duplicate.receipt);
    CHECK(duplicate.receipt->disposition ==
          goldsrc::NetchanFragmentInsertDisposition::exact_duplicate);
    CHECK(snapshot(reassembler) == before_duplicate);

    const auto expired = reassembler.expire(fixed_deadline);
    REQUIRE(expired);
    REQUIRE(expired.timed_out_transfer);
    CHECK(expired.timed_out_transfer->value() == before_duplicate.transfer_id);
    CHECK_FALSE(reassembler.active_transfer());
    CHECK(reassembler.ranges().empty());

    const auto again = reassembler.expire(fixed_deadline + 1ms);
    REQUIRE(again);
    CHECK_FALSE(again.timed_out_transfer);

    static_cast<void>(insert(
        reassembler, descriptor(1U, 3U, 1'024U), first, start + 10s));
    const auto before_edge = snapshot(reassembler);
    check_error(
        reassembler.prepare_insert(
            descriptor(2U, 3U, 1'024U),
            middle,
            start + 15s),
        goldsrc::NetchanReassemblyErrorCode::transfer_deadline_expired);
    CHECK(snapshot(reassembler) == before_edge);
}

TEST_CASE("An active fixed deadline does not recompute an overflowing timeout",
          "[goldsrc][netchan][fragment][reassembly][timeout][bounds]")
{
    auto limits = goldsrc::NetchanReassemblyLimits{};
    limits.fragment_timeout = 5s;
    goldsrc::NetchanNormalReassembler reassembler{limits};
    const auto timeout_duration =
        std::chrono::duration_cast<goldsrc::NetchanFragmentClock::duration>(
            limits.fragment_timeout);
    const auto start =
        goldsrc::NetchanFragmentTimePoint::max() - timeout_duration;
    const auto first = repeated(1'024U, 0xc1U);
    const auto middle = repeated(1'024U, 0xc2U);

    static_cast<void>(
        insert(reassembler, descriptor(1U, 3U, 1'024U), first, start));
    const auto now = start + 1s;
    const auto inserted = insert(
        reassembler, descriptor(2U, 3U, 1'024U), middle, now);
    REQUIRE(inserted.receipt);
    REQUIRE(reassembler.active_transfer());
    CHECK(reassembler.active_transfer()->deadline ==
          goldsrc::NetchanFragmentTimePoint::max());
}

TEST_CASE("Owner terminal cleanup clears bytes ranges and invalidates plans",
          "[goldsrc][netchan][fragment][reassembly][cleanup]")
{
    const auto first = repeated(1'024U, 0xe1U);

    for (const auto owner_outcome : {
             "cancellation",
             "protocol_error",
             "network_error",
         }) {
        INFO(owner_outcome);
        goldsrc::NetchanNormalReassembler reassembler;
        static_cast<void>(
            insert(reassembler, descriptor(1U, 3U, 1'024U), first));
        auto pending = reassembler.prepare_insert(
            descriptor(2U, 3U, 1'024U), first, at(1ms));
        REQUIRE(pending);
        REQUIRE(pending.plan);

        // The persistent driver maps each terminal outcome to this synchronous
        // owner cleanup; the pure reassembler does not invent driver states.
        reassembler.clear();
        CHECK_FALSE(reassembler.active_transfer());
        CHECK(reassembler.ranges().empty());
        check_error(
            reassembler.commit_insert(std::move(*pending.plan)),
            goldsrc::NetchanReassemblyErrorCode::stale_insert_plan);
    }
}

} // namespace
