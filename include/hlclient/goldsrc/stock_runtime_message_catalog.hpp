#pragma once

#include "hlclient/goldsrc/delta_description.hpp"
#include "hlclient/goldsrc/resource_client_response.hpp"
#include "hlclient/goldsrc/service_message_stream.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

// These values select project compatibility boundaries. A reserved v1 value is
// not evidence that the corresponding stock wire grammar has been confirmed.
enum class StockRuntimeCompatibilityProfile : std::uint8_t {
    stock_protocol_48_build_10210_evidence_pending,
    stock_protocol_48_build_10210_runtime_v1,
};

enum class StockRuntimeEvidenceProfile : std::uint8_t {
    controlled_signed_stock_transcript_pending,
    controlled_signed_stock_transcript_v1,
};

enum class StockRuntimeDeltaCompatibilityProfile : std::uint8_t {
    stock_protocol_48_build_10210_delta_evidence_pending,
    stock_protocol_48_build_10210_delta_v1,
};

[[nodiscard]] bool valid_stock_runtime_compatibility_profile(
    StockRuntimeCompatibilityProfile profile) noexcept;
[[nodiscard]] bool valid_stock_runtime_evidence_profile(
    StockRuntimeEvidenceProfile profile) noexcept;
[[nodiscard]] bool valid_stock_runtime_delta_compatibility_profile(
    StockRuntimeDeltaCompatibilityProfile profile) noexcept;
[[nodiscard]] StockRuntimeEvidenceProfile stock_runtime_evidence_profile_for(
    StockRuntimeCompatibilityProfile profile) noexcept;
[[nodiscard]] StockRuntimeDeltaCompatibilityProfile stock_runtime_delta_profile_for(
    StockRuntimeCompatibilityProfile profile) noexcept;

struct StockRuntimeDecodeLimits final {
    // Project safety limits only; none of these values claims a stock-engine
    // protocol maximum.
    std::size_t maximum_payload_bytes{1U << 20U};
    std::size_t maximum_messages_per_payload{64U};
    std::size_t maximum_runtime_frames{256U};
    std::size_t maximum_baselines{4096U};
    std::size_t maximum_entities{4096U};
    std::size_t maximum_clientdata_fields{256U};
    std::size_t maximum_weapon_entries{64U};
    std::size_t maximum_history_frames{64U};
    std::size_t maximum_value_bytes{8U << 20U};
    std::size_t maximum_metadata_bytes{1U << 20U};
    std::size_t maximum_decode_steps{1U << 20U};
    std::size_t maximum_pending_delta_bases{64U};
};

[[nodiscard]] const StockRuntimeDecodeLimits& hard_stock_runtime_decode_limits() noexcept;
[[nodiscard]] bool valid_stock_runtime_decode_limits(
    const StockRuntimeDecodeLimits& limits) noexcept;

class StockRuntimeSourceCursor final {
public:
    StockRuntimeSourceCursor() noexcept = default;

    [[nodiscard]] static std::optional<StockRuntimeSourceCursor> create(
        std::size_t byte_offset,
        std::size_t bit_offset,
        std::size_t payload_byte_count) noexcept;

    [[nodiscard]] std::size_t absolute_bit_offset() const noexcept;
    [[nodiscard]] std::size_t byte_offset() const noexcept;
    [[nodiscard]] std::size_t bit_offset() const noexcept;
    [[nodiscard]] bool byte_aligned() const noexcept;

    friend bool operator==(
        const StockRuntimeSourceCursor&,
        const StockRuntimeSourceCursor&) = default;

private:
    explicit StockRuntimeSourceCursor(std::size_t absolute_bit_offset) noexcept;

    std::size_t absolute_bit_offset_{};
};

[[nodiscard]] bool valid_stock_runtime_source_cursor(
    const StockRuntimeSourceCursor& cursor) noexcept;
[[nodiscard]] bool valid_stock_runtime_source_cursor(
    const StockRuntimeSourceCursor& cursor,
    std::size_t payload_byte_count) noexcept;

struct StockRuntimeSourceMetadata final {
    std::size_t payload_ordinal{0U};
    NetchanDirection direction{NetchanDirection::server_to_client};
    std::uint32_t source_sequence{0U};
    std::uint32_t source_acknowledgement{0U};
    bool source_reliable{false};
    bool acknowledgement_reliable{false};
    bool reassembled{false};
    bool decompressed{false};
    std::size_t decoded_payload_byte_count{0U};

    friend bool operator==(
        const StockRuntimeSourceMetadata&,
        const StockRuntimeSourceMetadata&) = default;
};

enum class StockRuntimeMessageCategory : std::uint8_t {
    runtime_control_candidate,
    runtime_time_candidate,
    baseline_candidate,
    entity_full_candidate,
    entity_delta_candidate,
    client_local_data_candidate,
    command_ack_candidate,
    unsupported_runtime_message,
};

enum class StockRuntimeMessageAlignment : std::uint8_t {
    evidence_pending,
    byte_aligned,
    mixed_bit_byte,
};

enum class StockRuntimeScenarioCorrelationStatus : std::uint8_t {
    not_observed,
    controlled_correlation_confirmed,
};

class StockRuntimeMessageCatalogEntry final {
public:
    StockRuntimeMessageCatalogEntry(const StockRuntimeMessageCatalogEntry&) = default;
    StockRuntimeMessageCatalogEntry& operator=(const StockRuntimeMessageCatalogEntry&) = delete;
    StockRuntimeMessageCatalogEntry(StockRuntimeMessageCatalogEntry&&) noexcept = default;
    StockRuntimeMessageCatalogEntry& operator=(StockRuntimeMessageCatalogEntry&&) noexcept = delete;
    ~StockRuntimeMessageCatalogEntry() = default;

    [[nodiscard]] std::uint8_t opcode() const noexcept;
    [[nodiscard]] StockRuntimeMessageCategory category() const noexcept;
    [[nodiscard]] const StockRuntimeSourceCursor& start_cursor() const noexcept;
    [[nodiscard]] const StockRuntimeSourceCursor& end_cursor() const noexcept;
    [[nodiscard]] StockRuntimeMessageAlignment alignment() const noexcept;
    [[nodiscard]] const std::optional<std::size_t>& body_bit_length() const noexcept;
    [[nodiscard]] std::size_t message_ordinal() const noexcept;
    [[nodiscard]] const StockRuntimeSourceMetadata& source() const noexcept;
    [[nodiscard]] std::size_t recurrence_count() const noexcept;
    [[nodiscard]] StockRuntimeScenarioCorrelationStatus scenario_correlation() const noexcept;
    [[nodiscard]] StockRuntimeCompatibilityProfile compatibility_profile() const noexcept;
    [[nodiscard]] StockRuntimeEvidenceProfile evidence_profile() const noexcept;
    [[nodiscard]] StockRuntimeDeltaCompatibilityProfile delta_profile() const noexcept;

private:
    friend class StockRuntimeMessageCatalogDecoder;

    StockRuntimeMessageCatalogEntry(
        std::uint8_t opcode,
        StockRuntimeMessageCategory category,
        StockRuntimeSourceCursor start_cursor,
        StockRuntimeSourceCursor end_cursor,
        StockRuntimeMessageAlignment alignment,
        std::optional<std::size_t> body_bit_length,
        std::size_t message_ordinal,
        StockRuntimeSourceMetadata source,
        std::size_t recurrence_count,
        StockRuntimeScenarioCorrelationStatus scenario_correlation,
        StockRuntimeCompatibilityProfile compatibility_profile,
        StockRuntimeEvidenceProfile evidence_profile,
        StockRuntimeDeltaCompatibilityProfile delta_profile) noexcept;

    std::uint8_t opcode_{};
    StockRuntimeMessageCategory category_{StockRuntimeMessageCategory::unsupported_runtime_message};
    StockRuntimeSourceCursor start_cursor_;
    StockRuntimeSourceCursor end_cursor_;
    StockRuntimeMessageAlignment alignment_{StockRuntimeMessageAlignment::evidence_pending};
    std::optional<std::size_t> body_bit_length_{};
    std::size_t message_ordinal_{0U};
    StockRuntimeSourceMetadata source_{};
    std::size_t recurrence_count_{0U};
    StockRuntimeScenarioCorrelationStatus scenario_correlation_{
        StockRuntimeScenarioCorrelationStatus::not_observed};
    StockRuntimeCompatibilityProfile compatibility_profile_{
        StockRuntimeCompatibilityProfile::stock_protocol_48_build_10210_evidence_pending};
    StockRuntimeEvidenceProfile evidence_profile_{
        StockRuntimeEvidenceProfile::controlled_signed_stock_transcript_pending};
    StockRuntimeDeltaCompatibilityProfile delta_profile_{
        StockRuntimeDeltaCompatibilityProfile::stock_protocol_48_build_10210_delta_evidence_pending};
};

class StockRuntimeMessageCatalogState final {
public:
    StockRuntimeMessageCatalogState(const StockRuntimeMessageCatalogState&) = default;
    StockRuntimeMessageCatalogState& operator=(const StockRuntimeMessageCatalogState&) = delete;
    StockRuntimeMessageCatalogState(StockRuntimeMessageCatalogState&&) noexcept = default;
    StockRuntimeMessageCatalogState& operator=(StockRuntimeMessageCatalogState&&) noexcept = delete;
    ~StockRuntimeMessageCatalogState() = default;

    [[nodiscard]] std::span<const StockRuntimeMessageCatalogEntry> entries() const noexcept;
    [[nodiscard]] std::size_t message_count() const noexcept;
    [[nodiscard]] std::size_t accounted_metadata_bytes() const noexcept;
    [[nodiscard]] StockRuntimeCompatibilityProfile compatibility_profile() const noexcept;
    [[nodiscard]] StockRuntimeEvidenceProfile evidence_profile() const noexcept;
    [[nodiscard]] StockRuntimeDeltaCompatibilityProfile delta_profile() const noexcept;

private:
    friend class StockRuntimeMessageCatalogDecoder;

    StockRuntimeMessageCatalogState(
        std::vector<StockRuntimeMessageCatalogEntry> entries,
        std::size_t accounted_metadata_bytes,
        StockRuntimeCompatibilityProfile compatibility_profile,
        StockRuntimeEvidenceProfile evidence_profile,
        StockRuntimeDeltaCompatibilityProfile delta_profile) noexcept;

    std::vector<StockRuntimeMessageCatalogEntry> entries_{};
    std::size_t accounted_metadata_bytes_{0U};
    StockRuntimeCompatibilityProfile compatibility_profile_{
        StockRuntimeCompatibilityProfile::stock_protocol_48_build_10210_evidence_pending};
    StockRuntimeEvidenceProfile evidence_profile_{
        StockRuntimeEvidenceProfile::controlled_signed_stock_transcript_pending};
    StockRuntimeDeltaCompatibilityProfile delta_profile_{
        StockRuntimeDeltaCompatibilityProfile::stock_protocol_48_build_10210_delta_evidence_pending};
};

enum class StockRuntimeReadyStatus : std::uint8_t {
    evidence_pending,
    signon_control_observed,
    first_complete_runtime_frame,
    steady_state_confirmed,
};

class StockRuntimeReadyState final {
public:
    StockRuntimeReadyState(const StockRuntimeReadyState&) = default;
    StockRuntimeReadyState& operator=(const StockRuntimeReadyState&) = delete;
    StockRuntimeReadyState(StockRuntimeReadyState&&) noexcept = default;
    StockRuntimeReadyState& operator=(StockRuntimeReadyState&&) noexcept = delete;
    ~StockRuntimeReadyState() = default;

    [[nodiscard]] StockRuntimeReadyStatus status() const noexcept;
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] const std::optional<std::uint64_t>& runtime_generation() const noexcept;
    [[nodiscard]] StockRuntimeCompatibilityProfile compatibility_profile() const noexcept;
    [[nodiscard]] StockRuntimeEvidenceProfile evidence_profile() const noexcept;

private:
    friend class StockRuntimeMessageCatalogDecoder;

    StockRuntimeReadyState(
        StockRuntimeReadyStatus status,
        std::optional<std::uint64_t> runtime_generation,
        StockRuntimeCompatibilityProfile compatibility_profile,
        StockRuntimeEvidenceProfile evidence_profile) noexcept;

    StockRuntimeReadyStatus status_{StockRuntimeReadyStatus::evidence_pending};
    std::optional<std::uint64_t> runtime_generation_{};
    StockRuntimeCompatibilityProfile compatibility_profile_{
        StockRuntimeCompatibilityProfile::stock_protocol_48_build_10210_evidence_pending};
    StockRuntimeEvidenceProfile evidence_profile_{
        StockRuntimeEvidenceProfile::controlled_signed_stock_transcript_pending};
};

enum class StockRuntimeUnsupportedBoundaryReason : std::uint8_t {
    runtime_message_grammar_evidence_pending,
};

class StockRuntimeUnsupportedBoundary final {
public:
    StockRuntimeUnsupportedBoundary(const StockRuntimeUnsupportedBoundary&) = default;
    StockRuntimeUnsupportedBoundary& operator=(const StockRuntimeUnsupportedBoundary&) = delete;
    StockRuntimeUnsupportedBoundary(StockRuntimeUnsupportedBoundary&&) noexcept = default;
    StockRuntimeUnsupportedBoundary& operator=(StockRuntimeUnsupportedBoundary&&) noexcept = delete;
    ~StockRuntimeUnsupportedBoundary() = default;

    [[nodiscard]] const StockRuntimeSourceCursor& cursor() const noexcept;
    [[nodiscard]] std::size_t remaining_payload_byte_count() const noexcept;
    [[nodiscard]] std::size_t remaining_payload_bit_count() const noexcept;
    [[nodiscard]] std::size_t unconfirmed_body_byte_count() const noexcept;
    [[nodiscard]] std::size_t message_ordinal() const noexcept;
    [[nodiscard]] std::uint8_t opcode() const noexcept;
    [[nodiscard]] StockRuntimeUnsupportedBoundaryReason reason() const noexcept;
    [[nodiscard]] StockRuntimeEvidenceProfile evidence_profile() const noexcept;

private:
    friend class StockRuntimeMessageCatalogDecoder;

    StockRuntimeUnsupportedBoundary(
        StockRuntimeSourceCursor cursor,
        std::size_t remaining_payload_byte_count,
        std::size_t remaining_payload_bit_count,
        std::size_t unconfirmed_body_byte_count,
        std::size_t message_ordinal,
        std::uint8_t opcode,
        StockRuntimeUnsupportedBoundaryReason reason,
        StockRuntimeEvidenceProfile evidence_profile) noexcept;

    StockRuntimeSourceCursor cursor_;
    std::size_t remaining_payload_byte_count_{0U};
    std::size_t remaining_payload_bit_count_{0U};
    std::size_t unconfirmed_body_byte_count_{0U};
    std::size_t message_ordinal_{0U};
    std::uint8_t opcode_{0U};
    StockRuntimeUnsupportedBoundaryReason reason_{
        StockRuntimeUnsupportedBoundaryReason::runtime_message_grammar_evidence_pending};
    StockRuntimeEvidenceProfile evidence_profile_{
        StockRuntimeEvidenceProfile::controlled_signed_stock_transcript_pending};
};

class StockRuntimeEvidenceBoundaryState final {
public:
    StockRuntimeEvidenceBoundaryState(const StockRuntimeEvidenceBoundaryState&) = default;
    StockRuntimeEvidenceBoundaryState& operator=(const StockRuntimeEvidenceBoundaryState&) = delete;
    StockRuntimeEvidenceBoundaryState(StockRuntimeEvidenceBoundaryState&&) noexcept = default;
    StockRuntimeEvidenceBoundaryState& operator=(StockRuntimeEvidenceBoundaryState&&) noexcept = delete;
    ~StockRuntimeEvidenceBoundaryState() = default;

    [[nodiscard]] const StockRuntimeMessageCatalogState& catalog() const noexcept;
    [[nodiscard]] const StockRuntimeReadyState& ready_state() const noexcept;
    [[nodiscard]] const StockRuntimeUnsupportedBoundary& unsupported_boundary() const noexcept;

private:
    friend class StockRuntimeMessageCatalogDecoder;

    StockRuntimeEvidenceBoundaryState(
        StockRuntimeMessageCatalogState catalog,
        StockRuntimeReadyState ready_state,
        StockRuntimeUnsupportedBoundary unsupported_boundary) noexcept;

    StockRuntimeMessageCatalogState catalog_;
    StockRuntimeReadyState ready_state_;
    StockRuntimeUnsupportedBoundary unsupported_boundary_;
};

enum class StockRuntimeDecodeErrorCode : std::uint8_t {
    invalid_configuration,
    invalid_profile,
    evidence_not_confirmed,
    payload_too_large,
    invalid_boundary_geometry,
    missing_delta_registry,
    message_limit_exceeded,
    metadata_limit_exceeded,
    decode_step_limit_exceeded,
    size_overflow,
    unable_to_retain_state,
};

struct StockRuntimeDecodeError final {
    StockRuntimeDecodeErrorCode code{StockRuntimeDecodeErrorCode::invalid_configuration};
    std::optional<StockRuntimeSourceCursor> cursor{};
    std::string context{};
};

[[nodiscard]] std::string_view to_string(StockRuntimeDecodeErrorCode code) noexcept;

struct StockRuntimeMessageCatalogDecodeResult final {
    std::optional<StockRuntimeEvidenceBoundaryState> state{};
    std::optional<StockRuntimeDecodeError> error{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return state.has_value() && !error.has_value();
    }
};

class StockRuntimeMessageCatalogDecoder final {
public:
    explicit StockRuntimeMessageCatalogDecoder(
        StockRuntimeDecodeLimits limits = {},
        StockRuntimeCompatibilityProfile compatibility_profile =
            StockRuntimeCompatibilityProfile::stock_protocol_48_build_10210_evidence_pending) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const StockRuntimeDecodeLimits& limits() const noexcept;
    [[nodiscard]] StockRuntimeCompatibilityProfile compatibility_profile() const noexcept;

    [[nodiscard]] StockRuntimeMessageCatalogDecodeResult decode(
        const OwnedServicePayload& payload,
        const PostResourceResponseBoundary& boundary,
        const DeltaSchemaRegistryState& delta_registry) const;

private:
    StockRuntimeDecodeLimits limits_{};
    StockRuntimeCompatibilityProfile compatibility_profile_{
        StockRuntimeCompatibilityProfile::stock_protocol_48_build_10210_evidence_pending};
};

} // namespace hlclient::goldsrc
