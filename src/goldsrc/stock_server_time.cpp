#include <hlclient/goldsrc/stock_server_time.hpp>

#include <cmath>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool valid_cursor_geometry(
    const StockRuntimeSourceCursor& start,
    const StockRuntimeSourceCursor& end) noexcept
{
    return valid_stock_runtime_source_cursor(start) &&
        valid_stock_runtime_source_cursor(end) &&
        end.absolute_bit_offset() >= start.absolute_bit_offset();
}

[[nodiscard]] bool encoded_value_fits(
    const std::uint64_t value,
    const std::size_t bit_width) noexcept
{
    return bit_width > 0U && bit_width <= 64U &&
        (bit_width == 64U || (value >> bit_width) == 0U);
}

[[nodiscard]] bool valid_message_category(
    const StockRuntimeMessageCategory category) noexcept
{
    switch (category) {
    case StockRuntimeMessageCategory::runtime_control_candidate:
    case StockRuntimeMessageCategory::runtime_time_candidate:
    case StockRuntimeMessageCategory::baseline_candidate:
    case StockRuntimeMessageCategory::entity_full_candidate:
    case StockRuntimeMessageCategory::entity_delta_candidate:
    case StockRuntimeMessageCategory::client_local_data_candidate:
    case StockRuntimeMessageCategory::command_ack_candidate:
    case StockRuntimeMessageCategory::unsupported_runtime_message:
        return true;
    }
    return false;
}

} // namespace

bool valid_pending_stock_server_time_observation(
    const StockServerTimeObservation& observation,
    const std::uint64_t runtime_generation) noexcept
{
    return runtime_generation != 0U &&
        observation.runtime_generation == runtime_generation &&
        encoded_value_fits(
            observation.raw_encoded_value, observation.encoded_bit_width) &&
        !observation.decoded_numeric_value.has_value() &&
        observation.encoding_profile ==
            StockServerTimeEncodingProfile::
                stock_server_time_encoding_evidence_pending &&
        observation.unit == StockServerTimeUnit::evidence_pending &&
        observation.source_message_category ==
            StockRuntimeMessageCategory::runtime_time_candidate &&
        valid_cursor_geometry(observation.source_start_cursor,
            observation.source_end_cursor) &&
        observation.source_end_cursor.absolute_bit_offset() -
                observation.source_start_cursor.absolute_bit_offset() ==
            observation.encoded_bit_width &&
        observation.monotonicity ==
            StockServerTimeMonotonicityStatus::evidence_pending &&
        observation.confidence ==
            StockRuntimeCandidateConfidence::evidence_pending;
}

bool valid_pending_stock_snapshot_reference_candidate(
    const StockRuntimeSnapshotReferenceCandidate& candidate) noexcept
{
    return encoded_value_fits(
               candidate.encoded_value, candidate.encoded_bit_width) &&
        valid_message_category(candidate.source_message_category) &&
        valid_cursor_geometry(
            candidate.source_start_cursor, candidate.source_end_cursor) &&
        candidate.source_end_cursor.absolute_bit_offset() -
                candidate.source_start_cursor.absolute_bit_offset() ==
            candidate.encoded_bit_width &&
        candidate.confidence ==
            StockRuntimeCandidateConfidence::evidence_pending;
}

bool valid_pending_stock_runtime_delta_time_context(
    const StockRuntimeDeltaTimeContext& context,
    const std::uint64_t runtime_generation) noexcept
{
    return runtime_generation != 0U &&
        context.delta_profile == StockRuntimeDeltaCompatibilityProfile::
                                     stock_protocol_48_build_10210_delta_evidence_pending &&
        context.status == StockRuntimeDeltaTimeContextStatus::server_time_required &&
        (!context.server_time || valid_pending_stock_server_time_observation(
                                     *context.server_time, runtime_generation)) &&
        (!context.snapshot_reference ||
            valid_pending_stock_snapshot_reference_candidate(
                *context.snapshot_reference));
}

} // namespace hlclient::goldsrc
