#pragma once

#include <hlclient/goldsrc/stock_runtime_message_catalog.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace hlclient::goldsrc {

// These values describe the strength of one retained candidate.  The
// confirmed value is reserved for a future evidence-backed profile; the
// current runtime boundary rejects it.
enum class StockRuntimeCandidateConfidence : std::uint8_t {
    evidence_pending,
    confirmed_for_profile,
};

enum class StockServerTimeEncodingProfile : std::uint8_t {
    stock_server_time_encoding_evidence_pending,
};

enum class StockServerTimeUnit : std::uint8_t {
    evidence_pending,
};

enum class StockServerTimeMonotonicityStatus : std::uint8_t {
    evidence_pending,
    nondecreasing_observed,
    reset_observed,
};

// A raw observation only.  In the pending profile raw_encoded_value is not
// assigned a numeric scale or unit and decoded_numeric_value must be empty.
struct StockServerTimeObservation {
    std::uint64_t raw_encoded_value{0U};
    std::size_t encoded_bit_width{0U};
    std::optional<double> decoded_numeric_value;
    StockServerTimeEncodingProfile encoding_profile{
        StockServerTimeEncodingProfile::
            stock_server_time_encoding_evidence_pending};
    StockServerTimeUnit unit{StockServerTimeUnit::evidence_pending};
    StockRuntimeMessageCategory source_message_category{
        StockRuntimeMessageCategory::runtime_time_candidate};
    std::size_t source_record_index{0U};
    StockRuntimeSourceCursor source_start_cursor{};
    StockRuntimeSourceCursor source_end_cursor{};
    std::size_t source_message_ordinal{0U};
    std::uint64_t runtime_generation{0U};
    StockServerTimeMonotonicityStatus monotonicity{
        StockServerTimeMonotonicityStatus::evidence_pending};
    StockRuntimeCandidateConfidence confidence{
        StockRuntimeCandidateConfidence::evidence_pending};

    [[nodiscard]] friend bool operator==(
        const StockServerTimeObservation&,
        const StockServerTimeObservation&) = default;
};

// This is a tagged observation only. It deliberately defines neither a stock
// width nor a wrap/comparison policy and cannot become EntitySnapshotReference.
struct StockRuntimeSnapshotReferenceCandidate {
    std::uint64_t encoded_value{0U};
    std::size_t encoded_bit_width{0U};
    std::size_t source_record_index{0U};
    StockRuntimeMessageCategory source_message_category{
        StockRuntimeMessageCategory::unsupported_runtime_message};
    StockRuntimeSourceCursor source_start_cursor{};
    StockRuntimeSourceCursor source_end_cursor{};
    std::size_t source_message_ordinal{0U};
    StockRuntimeCandidateConfidence confidence{
        StockRuntimeCandidateConfidence::evidence_pending};

    [[nodiscard]] friend bool operator==(
        const StockRuntimeSnapshotReferenceCandidate&,
        const StockRuntimeSnapshotReferenceCandidate&) = default;
};

enum class StockRuntimeDeltaTimeContextStatus : std::uint8_t {
    server_time_required,
    confirmed,
};

// Runtime delta values that depend on a time window must receive this explicit
// context.  The current pending profile can retain raw candidates, but its
// status remains server_time_required and therefore cannot decode such values.
struct StockRuntimeDeltaTimeContext {
    std::optional<StockServerTimeObservation> server_time;
    std::optional<StockRuntimeSnapshotReferenceCandidate> snapshot_reference;
    StockRuntimeDeltaCompatibilityProfile delta_profile{
        StockRuntimeDeltaCompatibilityProfile::
            stock_protocol_48_build_10210_delta_evidence_pending};
    StockRuntimeDeltaTimeContextStatus status{
        StockRuntimeDeltaTimeContextStatus::server_time_required};

    [[nodiscard]] friend bool operator==(
        const StockRuntimeDeltaTimeContext&,
        const StockRuntimeDeltaTimeContext&) = default;
};

[[nodiscard]] bool valid_pending_stock_server_time_observation(
    const StockServerTimeObservation& observation,
    std::uint64_t runtime_generation) noexcept;
[[nodiscard]] bool valid_pending_stock_snapshot_reference_candidate(
    const StockRuntimeSnapshotReferenceCandidate& candidate) noexcept;
[[nodiscard]] bool valid_pending_stock_runtime_delta_time_context(
    const StockRuntimeDeltaTimeContext& context,
    std::uint64_t runtime_generation) noexcept;

} // namespace hlclient::goldsrc
