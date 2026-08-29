#include <hlclient/goldsrc/stock_runtime_frame.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] goldsrc::StockRuntimeSourceCursor cursor(
    const std::size_t byte_offset)
{
    const auto created =
        goldsrc::StockRuntimeSourceCursor::create(byte_offset, 0U, 8U);
    REQUIRE(created);
    return *created;
}

[[nodiscard]] goldsrc::StockRuntimeFrameState frame(
    const std::uint64_t generation,
    const std::uint64_t ordinal)
{
    goldsrc::StockRuntimeFrameCreateInfo info;
    info.runtime_generation = generation;
    info.frame_ordinal = ordinal;
    const auto created = goldsrc::StockRuntimeFrameState::create(info);
    REQUIRE(created);
    return *created.frame;
}

TEST_CASE("Stock runtime frame is immutable and evidence-pending by default",
    "[goldsrc][stock-runtime][frame]")
{
    const auto created = frame(3U, 1U);
    CHECK(created.runtime_generation() == 3U);
    CHECK(created.frame_ordinal() == 1U);
    CHECK(created.status() == goldsrc::StockRuntimeFrameStatus::evidence_pending);
    CHECK_FALSE(created.entity_snapshot());
    CHECK_FALSE(created.authoritative_observation());
}

TEST_CASE("Stock runtime frame history rejects cross-generation appends",
    "[goldsrc][stock-runtime][frame][history]")
{
    const auto history = goldsrc::StockRuntimeFrameHistoryState::create(3U);
    REQUIRE(history);
    const auto rejected = history.history->append(frame(4U, 1U));
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
        goldsrc::StockRuntimeFrameErrorCode::runtime_generation_mismatch);
    CHECK(history.history->frame_count() == 0U);
}

TEST_CASE("Stock runtime history uses bounded backpressure without eviction",
    "[goldsrc][stock-runtime][frame][history]")
{
    goldsrc::StockRuntimeFrameHistoryLimits limits;
    limits.maximum_frames = 1U;
    const auto empty =
        goldsrc::StockRuntimeFrameHistoryState::create(5U, limits);
    REQUIRE(empty);
    const auto first = empty.history->append(frame(5U, 1U));
    REQUIRE(first);
    CHECK(first.history->frame_count() == 1U);

    const auto rejected = first.history->append(frame(5U, 2U));
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
        goldsrc::StockRuntimeFrameErrorCode::history_backpressure);
    CHECK(first.history->find_exact(1U) != nullptr);
    CHECK(first.history->find_exact(2U) == nullptr);
}

TEST_CASE("Stock runtime history does not reorder old frames implicitly",
    "[goldsrc][stock-runtime][frame][history]")
{
    const auto empty = goldsrc::StockRuntimeFrameHistoryState::create(6U);
    REQUIRE(empty);
    const auto first = empty.history->append(frame(6U, 2U));
    REQUIRE(first);
    const auto rejected = first.history->append(frame(6U, 1U));
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
        goldsrc::StockRuntimeFrameErrorCode::out_of_order_frame);
}

TEST_CASE("Pending server time retains opaque bits without assigning units",
    "[goldsrc][stock-runtime][frame][time]")
{
    goldsrc::StockServerTimeObservation observation;
    observation.raw_encoded_value = 0x7FU;
    observation.encoded_bit_width = 8U;
    observation.source_start_cursor = cursor(0U);
    observation.source_end_cursor = cursor(1U);
    observation.runtime_generation = 8U;
    CHECK(goldsrc::valid_pending_stock_server_time_observation(
        observation, 8U));

    goldsrc::StockRuntimeDeltaTimeContext context;
    context.server_time = observation;
    CHECK(goldsrc::valid_pending_stock_runtime_delta_time_context(
        context, 8U));
    CHECK(context.status ==
        goldsrc::StockRuntimeDeltaTimeContextStatus::server_time_required);

    observation.decoded_numeric_value = 1.0;
    observation.encoding_profile =
        static_cast<goldsrc::StockServerTimeEncodingProfile>(0xFFU);
    observation.unit = static_cast<goldsrc::StockServerTimeUnit>(0xFFU);
    CHECK_FALSE(goldsrc::valid_pending_stock_server_time_observation(
        observation, 8U));
}

TEST_CASE("Opaque time and snapshot widths fail closed on overflow",
    "[goldsrc][stock-runtime][frame][time]")
{
    goldsrc::StockServerTimeObservation time;
    time.raw_encoded_value = 0x100U;
    time.encoded_bit_width = 8U;
    time.source_start_cursor = cursor(0U);
    time.source_end_cursor = cursor(1U);
    time.runtime_generation = 9U;
    CHECK_FALSE(
        goldsrc::valid_pending_stock_server_time_observation(time, 9U));

    goldsrc::StockRuntimeSnapshotReferenceCandidate reference;
    reference.encoded_value = 2U;
    reference.encoded_bit_width = 1U;
    reference.source_start_cursor = cursor(0U);
    const auto end = goldsrc::StockRuntimeSourceCursor::create(0U, 1U, 8U);
    REQUIRE(end);
    reference.source_end_cursor = *end;
    CHECK_FALSE(
        goldsrc::valid_pending_stock_snapshot_reference_candidate(reference));
}

TEST_CASE("Runtime frame rejects an opaque time without catalog binding",
    "[goldsrc][stock-runtime][frame][time]")
{
    goldsrc::StockRuntimeFrameCreateInfo info;
    info.runtime_generation = 10U;
    info.frame_ordinal = 1U;
    goldsrc::StockRuntimeSourceMetadata source;
    source.decoded_payload_byte_count = 8U;
    info.source_messages.push_back(source);

    goldsrc::StockServerTimeObservation observation;
    observation.raw_encoded_value = 0x7FU;
    observation.encoded_bit_width = 8U;
    observation.source_start_cursor = cursor(0U);
    observation.source_end_cursor = cursor(1U);
    observation.runtime_generation = 10U;
    info.server_time = observation;

    const auto rejected = goldsrc::StockRuntimeFrameState::create(info);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
        goldsrc::StockRuntimeFrameErrorCode::stock_evidence_pending);
}

} // namespace
