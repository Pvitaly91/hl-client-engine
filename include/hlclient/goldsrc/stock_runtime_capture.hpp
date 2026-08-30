#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::goldsrc {

// Research-only resource policy. These are project safety limits, not claims
// about limits used by a stock GoldSrc implementation.
struct StockRuntimeCaptureLimits final {
    std::chrono::milliseconds maximum_duration{45'000};
    std::size_t maximum_datagrams{8'192U};
    std::uint64_t maximum_total_raw_bytes{64U * 1'024U * 1'024U};
    std::size_t maximum_payload_bytes{65'507U};
    std::size_t maximum_reassembled_bytes{8U * 1'024U * 1'024U};
    std::size_t maximum_decompressed_bytes{32U * 1'024U * 1'024U};
    std::size_t maximum_message_count{8'192U};
    std::size_t maximum_runtime_frames{4'096U};
    std::size_t maximum_client_packets{4'096U};
    std::size_t maximum_server_packets{4'096U};
};

struct StockRuntimeCaptureHardCaps final {
    static constexpr std::chrono::milliseconds maximum_duration{300'000};
    static constexpr std::size_t maximum_datagrams{65'536U};
    static constexpr std::uint64_t maximum_total_raw_bytes{
        512U * 1'024U * 1'024U};
    static constexpr std::size_t maximum_payload_bytes{65'507U};
    static constexpr std::size_t maximum_reassembled_bytes{
        64U * 1'024U * 1'024U};
    static constexpr std::size_t maximum_decompressed_bytes{
        256U * 1'024U * 1'024U};
    static constexpr std::size_t maximum_message_count{65'536U};
    static constexpr std::size_t maximum_runtime_frames{32'768U};
    static constexpr std::size_t maximum_client_packets{65'536U};
    static constexpr std::size_t maximum_server_packets{65'536U};
};

enum class StockRuntimeCaptureLimitErrorCode {
    none,
    zero_limit,
    hard_cap_exceeded,
    payload_exceeds_total_raw_bytes,
    decompressed_smaller_than_reassembled,
    frames_exceed_messages,
    direction_packets_exceed_total,
};

struct StockRuntimeCaptureLimitValidation final {
    StockRuntimeCaptureLimitErrorCode code{
        StockRuntimeCaptureLimitErrorCode::none};
    std::string field;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return code == StockRuntimeCaptureLimitErrorCode::none;
    }
};

[[nodiscard]] StockRuntimeCaptureLimitValidation
validate_stock_runtime_capture_limits(
    const StockRuntimeCaptureLimits& limits) noexcept;

enum class StockRuntimeCaptureScenario {
    baseline,
    idle_runtime,
    forward,
    backward,
    left,
    right,
    forward_right,
    jump,
    duck,
    duck_stand,
    yaw_positive,
    yaw_negative,
    pitch_positive,
    pitch_negative,
    second_client,
    reconnect,
    map_change,
    server_restart,
    respawn,
    low_updaterate,
    high_updaterate,
    low_cmdrate,
    high_cmdrate,
    drop_server_runtime,
    drop_two_server_runtime,
    duplicate_server_runtime,
    reorder_server_runtime,
    drop_client_move,
    delay_client_move,
};

[[nodiscard]] std::optional<StockRuntimeCaptureScenario>
parse_stock_runtime_capture_scenario(std::string_view value) noexcept;
[[nodiscard]] std::string_view to_string(
    StockRuntimeCaptureScenario scenario) noexcept;

enum class StockRuntimeCaptureDirection {
    client_to_server,
    server_to_client,
};

enum class StockRuntimeCaptureAction {
    forward,
    drop,
    duplicate,
    hold_for_delay,
    hold_for_reorder,
};

struct StockRuntimeCapturePerturbation final {
    std::size_t client_packet_ordinal{20U};
    std::size_t server_packet_ordinal{20U};
};

[[nodiscard]] StockRuntimeCaptureAction stock_runtime_capture_action(
    StockRuntimeCaptureScenario scenario,
    StockRuntimeCaptureDirection direction,
    std::size_t direction_packet_ordinal,
    StockRuntimeCapturePerturbation perturbation = {}) noexcept;

struct StockRuntimeCaptureCounters final {
    std::size_t observed_datagrams{0U};
    std::uint64_t observed_raw_bytes{0U};
    std::size_t client_packets{0U};
    std::size_t server_packets{0U};
    std::size_t emitted_datagrams{0U};
    std::uint64_t emitted_bytes{0U};
    std::size_t dropped_datagrams{0U};
    std::size_t duplicated_datagrams{0U};
    std::size_t delayed_datagrams{0U};
    std::size_t ignored_wrong_source_datagrams{0U};
};

enum class StockRuntimeCaptureBudgetErrorCode {
    none,
    payload_limit,
    datagram_limit,
    total_raw_byte_limit,
    client_packet_limit,
    server_packet_limit,
    emitted_counter_overflow,
};

struct StockRuntimeCaptureBudgetResult final {
    StockRuntimeCaptureBudgetErrorCode code{
        StockRuntimeCaptureBudgetErrorCode::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return code == StockRuntimeCaptureBudgetErrorCode::none;
    }
};

[[nodiscard]] StockRuntimeCaptureBudgetResult
stock_runtime_capture_observe_datagram(
    StockRuntimeCaptureCounters& counters,
    const StockRuntimeCaptureLimits& limits,
    StockRuntimeCaptureDirection direction,
    std::size_t payload_bytes) noexcept;

[[nodiscard]] StockRuntimeCaptureBudgetResult
stock_runtime_capture_record_emission(
    StockRuntimeCaptureCounters& counters,
    std::size_t payload_bytes) noexcept;

inline constexpr std::string_view kStockRuntimeCaptureMetadataSchema =
    "hlclient.stock-runtime-capture-metadata.v1";
inline constexpr std::string_view kStockRuntimePendingProfile =
    "stock_protocol_48_build_10210_evidence_pending";

// Flat and deliberately metadata-only. No endpoint, payload digest, entity
// value, player identity, configuration value, or native path is represented.
struct StockRuntimeCaptureMetadata final {
    StockRuntimeCaptureScenario scenario{StockRuntimeCaptureScenario::baseline};
    StockRuntimeCaptureLimits limits{};
    StockRuntimeCaptureCounters counters{};
    std::size_t perturbation_count{0U};
    bool bounded_transport_complete{false};
    bool byte_preserving{true};
    bool private_ipv4_loopback{true};
    // At most one learned client endpoint may be active at any instant. A
    // reconnect may retire A before learning B; its bounded two-generation
    // proof lives in the separate reconnect observation schema.
    bool one_learned_client_endpoint{true};
    bool one_upstream_socket{true};
    bool exact_source_validation{true};
    bool payload_rewritten{false};
    bool raw_datagrams_stored{true};
    bool accepted_evidence_run{false};
};

struct StockRuntimeCaptureMetadataParseResult final {
    std::optional<StockRuntimeCaptureMetadata> metadata;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return metadata.has_value();
    }
};

[[nodiscard]] std::string serialize_stock_runtime_capture_metadata(
    const StockRuntimeCaptureMetadata& metadata);
[[nodiscard]] StockRuntimeCaptureMetadataParseResult
parse_stock_runtime_capture_metadata(std::string_view json);

[[nodiscard]] std::string canonical_stock_runtime_capture_structure(
    const StockRuntimeCaptureMetadata& metadata);

} // namespace hlclient::goldsrc
