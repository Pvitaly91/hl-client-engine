#pragma once

#include <hlclient/goldsrc/stock_runtime_capture.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::string_view kStockRuntimeTransportJournalSchema =
    "hlclient.stock-runtime-transport-journal.v1";
inline constexpr std::size_t kMaximumStockRuntimeJournalLineBytes = 4'096U;
inline constexpr std::size_t kMaximumStockRuntimeJournalEmissionsPerEntry = 2U;

enum class StockRuntimeTransportRole {
    research_client,
    research_server,
    unexpected_source,
};

enum class StockRuntimeTransportHoldState {
    none,
    held,
    released,
    unresolved,
};

// One immutable journal record describes a datagram as observed by the relay.
// Emission ordinals describe peer-visible delivery order. They are deliberately
// separate from observed ordinals so a dropped datagram can never reach replay.
struct StockRuntimeTransportJournalEntry final {
    std::size_t observed_ordinal{0U};
    StockRuntimeCaptureDirection direction{
        StockRuntimeCaptureDirection::client_to_server};
    std::size_t direction_ordinal{1U};
    std::uint64_t relative_timestamp_us{0U};
    std::size_t payload_byte_count{0U};
    std::string raw_filename;
    StockRuntimeTransportRole source_role{
        StockRuntimeTransportRole::research_client};
    StockRuntimeTransportRole destination_role{
        StockRuntimeTransportRole::research_server};
    StockRuntimeCaptureAction action{StockRuntimeCaptureAction::forward};
    StockRuntimeTransportHoldState hold_state{
        StockRuntimeTransportHoldState::none};
    std::vector<std::size_t> emitted_ordinals;
    bool delivered{false};
    bool wrong_source{false};
    // Canonical publication uses 64 lowercase hexadecimal characters. The
    // parser accepts either case and normalizes it without exposing raw bytes.
    std::string sha256;
};

struct StockRuntimeTransportJournalLimits final {
    std::size_t maximum_entries{
        StockRuntimeCaptureHardCaps::maximum_datagrams};
    std::size_t maximum_payload_bytes{
        StockRuntimeCaptureHardCaps::maximum_payload_bytes};
    std::uint64_t maximum_total_raw_bytes{
        StockRuntimeCaptureHardCaps::maximum_total_raw_bytes};
    std::size_t maximum_emitted_datagrams{
        StockRuntimeCaptureHardCaps::maximum_datagrams * 2U};
    std::uint64_t maximum_relative_timestamp_us{300'000'000U};
};

enum class StockRuntimeTransportJournalValidationPolicy {
    complete_capture,
    incomplete_capture,
};

enum class StockRuntimeTransportJournalErrorCode {
    invalid_configuration,
    line_too_large,
    invalid_json,
    duplicate_property,
    unknown_property,
    wrong_schema,
    invalid_ordinal,
    invalid_direction,
    invalid_direction_ordinal,
    timestamp_not_monotonic,
    invalid_payload_size,
    invalid_raw_filename,
    invalid_role,
    role_direction_mismatch,
    invalid_action,
    invalid_hold_state,
    invalid_emitted_ordinals,
    invalid_delivery_state,
    invalid_wrong_source_state,
    invalid_sha256,
    count_mismatch,
    byte_limit_exceeded,
    emission_reference_mismatch,
    unresolved_hold,
};

struct StockRuntimeTransportJournalError final {
    StockRuntimeTransportJournalErrorCode code{
        StockRuntimeTransportJournalErrorCode::invalid_json};
    std::size_t entry_ordinal{0U};
    std::size_t byte_offset{0U};
    std::string context;
};

struct StockRuntimeTransportJournalEntryParseResult final {
    std::optional<StockRuntimeTransportJournalEntry> entry;
    std::optional<StockRuntimeTransportJournalError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return entry.has_value();
    }
};

struct StockRuntimeTransportJournalValidation final {
    std::optional<StockRuntimeTransportJournalError> error;
    std::size_t emitted_datagram_count{0U};
    std::uint64_t observed_raw_bytes{0U};
    std::size_t client_to_server_count{0U};
    std::size_t server_to_client_count{0U};
    bool transport_complete{false};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

[[nodiscard]] StockRuntimeTransportJournalEntryParseResult
parse_stock_runtime_transport_journal_entry(std::string_view json_line);

[[nodiscard]] std::string serialize_stock_runtime_transport_journal_entry(
    const StockRuntimeTransportJournalEntry& entry);

[[nodiscard]] StockRuntimeTransportJournalValidation
validate_stock_runtime_transport_journal(
    std::span<const StockRuntimeTransportJournalEntry> entries,
    StockRuntimeTransportJournalLimits limits = {},
    StockRuntimeTransportJournalValidationPolicy policy =
        StockRuntimeTransportJournalValidationPolicy::complete_capture);

[[nodiscard]] constexpr std::string_view to_string(
    StockRuntimeTransportRole role) noexcept
{
    switch (role) {
    case StockRuntimeTransportRole::research_client: return "research_client";
    case StockRuntimeTransportRole::research_server: return "research_server";
    case StockRuntimeTransportRole::unexpected_source: return "unexpected_source";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    StockRuntimeTransportHoldState state) noexcept
{
    switch (state) {
    case StockRuntimeTransportHoldState::none: return "none";
    case StockRuntimeTransportHoldState::held: return "held";
    case StockRuntimeTransportHoldState::released: return "released";
    case StockRuntimeTransportHoldState::unresolved: return "unresolved";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    StockRuntimeTransportJournalErrorCode code) noexcept
{
    switch (code) {
    case StockRuntimeTransportJournalErrorCode::invalid_configuration:
        return "invalid_configuration";
    case StockRuntimeTransportJournalErrorCode::line_too_large:
        return "line_too_large";
    case StockRuntimeTransportJournalErrorCode::invalid_json:
        return "invalid_json";
    case StockRuntimeTransportJournalErrorCode::duplicate_property:
        return "duplicate_property";
    case StockRuntimeTransportJournalErrorCode::unknown_property:
        return "unknown_property";
    case StockRuntimeTransportJournalErrorCode::wrong_schema:
        return "wrong_schema";
    case StockRuntimeTransportJournalErrorCode::invalid_ordinal:
        return "invalid_ordinal";
    case StockRuntimeTransportJournalErrorCode::invalid_direction:
        return "invalid_direction";
    case StockRuntimeTransportJournalErrorCode::invalid_direction_ordinal:
        return "invalid_direction_ordinal";
    case StockRuntimeTransportJournalErrorCode::timestamp_not_monotonic:
        return "timestamp_not_monotonic";
    case StockRuntimeTransportJournalErrorCode::invalid_payload_size:
        return "invalid_payload_size";
    case StockRuntimeTransportJournalErrorCode::invalid_raw_filename:
        return "invalid_raw_filename";
    case StockRuntimeTransportJournalErrorCode::invalid_role:
        return "invalid_role";
    case StockRuntimeTransportJournalErrorCode::role_direction_mismatch:
        return "role_direction_mismatch";
    case StockRuntimeTransportJournalErrorCode::invalid_action:
        return "invalid_action";
    case StockRuntimeTransportJournalErrorCode::invalid_hold_state:
        return "invalid_hold_state";
    case StockRuntimeTransportJournalErrorCode::invalid_emitted_ordinals:
        return "invalid_emitted_ordinals";
    case StockRuntimeTransportJournalErrorCode::invalid_delivery_state:
        return "invalid_delivery_state";
    case StockRuntimeTransportJournalErrorCode::invalid_wrong_source_state:
        return "invalid_wrong_source_state";
    case StockRuntimeTransportJournalErrorCode::invalid_sha256:
        return "invalid_sha256";
    case StockRuntimeTransportJournalErrorCode::count_mismatch:
        return "count_mismatch";
    case StockRuntimeTransportJournalErrorCode::byte_limit_exceeded:
        return "byte_limit_exceeded";
    case StockRuntimeTransportJournalErrorCode::emission_reference_mismatch:
        return "emission_reference_mismatch";
    case StockRuntimeTransportJournalErrorCode::unresolved_hold:
        return "unresolved_hold";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
