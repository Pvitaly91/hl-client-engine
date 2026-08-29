#pragma once

#include <hlclient/goldsrc/stock_runtime_message_catalog.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::size_t kDefaultMaximumStockAckCandidates = 64U;
inline constexpr std::size_t kHardMaximumStockAckCandidates = 512U;
inline constexpr std::size_t kDefaultMaximumStockAckCorrelations = 64U;
inline constexpr std::size_t kHardMaximumStockAckCorrelations = 512U;

struct StockCommandAcknowledgementEvidenceLimits {
    std::size_t maximum_candidates{kDefaultMaximumStockAckCandidates};
    std::size_t maximum_correlations{kDefaultMaximumStockAckCorrelations};
    std::uint64_t maximum_candidate_value{UINT64_MAX};
};

[[nodiscard]] bool valid_stock_command_acknowledgement_evidence_limits(
    const StockCommandAcknowledgementEvidenceLimits& limits) noexcept;

enum class StockCommandAcknowledgementCandidateDomain : std::uint8_t {
    client_to_server_netchan_sequence,
    server_to_client_netchan_acknowledgement,
    client_move_packet_ordinal,
    server_runtime_frame_reference,
    explicit_clientdata_field,
    exact_usercmd_sequence,
};

enum class StockCommandAcknowledgementEvidenceStatus : std::uint8_t {
    unobserved,
    candidate_value_observed,
    correlates_with_netchan_sequence,
    correlates_with_move_packet,
    exact_usercmd_sequence_pending,
    exact_usercmd_sequence_confirmed,
    conflicting,
};

struct StockCommandAcknowledgementCandidateObservation {
    StockCommandAcknowledgementCandidateDomain domain{
        StockCommandAcknowledgementCandidateDomain::
            client_to_server_netchan_sequence};
    std::uint64_t value{0U};
    std::size_t source_record_ordinal{0U};
    std::optional<StockRuntimeSourceCursor> source_cursor;

    [[nodiscard]] friend bool operator==(
        const StockCommandAcknowledgementCandidateObservation&,
        const StockCommandAcknowledgementCandidateObservation&) = default;
};

// Aggregate-only correlation metadata. It intentionally records no command,
// input, position, packet body, or authentication bytes.
struct StockCommandAcknowledgementCorrelationObservation {
    StockCommandAcknowledgementCandidateDomain candidate_domain{
        StockCommandAcknowledgementCandidateDomain::
            explicit_clientdata_field};
    StockCommandAcknowledgementCandidateDomain reference_domain{
        StockCommandAcknowledgementCandidateDomain::
            client_move_packet_ordinal};
    std::size_t sample_count{0U};
    std::size_t matching_progression_count{0U};
    std::size_t contradiction_count{0U};
    std::size_t loss_scenario_count{0U};
    std::size_t batching_scenario_count{0U};
    std::size_t reset_scenario_count{0U};

    [[nodiscard]] friend bool operator==(
        const StockCommandAcknowledgementCorrelationObservation&,
        const StockCommandAcknowledgementCorrelationObservation&) = default;
};

enum class StockCommandAcknowledgementEvidenceErrorCode : std::uint8_t {
    invalid_configuration,
    invalid_runtime_generation,
    invalid_candidate_domain,
    invalid_source_cursor,
    candidate_value_out_of_range,
    candidate_limit_exceeded,
    correlation_limit_exceeded,
    invalid_correlation,
    missing_candidate_domain,
    duplicate_candidate,
    duplicate_correlation,
    stock_evidence_pending,
    allocation_failed,
};

struct StockCommandAcknowledgementEvidenceError {
    StockCommandAcknowledgementEvidenceErrorCode code{
        StockCommandAcknowledgementEvidenceErrorCode::invalid_configuration};
    std::optional<StockCommandAcknowledgementCandidateDomain> domain;
    std::string_view context;
};

struct StockCommandAcknowledgementEvidenceOperationResult {
    std::optional<StockCommandAcknowledgementEvidenceError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

class StockCommandAcknowledgementEvidenceBuilder;

class StockCommandAcknowledgementEvidenceState final {
public:
    StockCommandAcknowledgementEvidenceState(
        const StockCommandAcknowledgementEvidenceState&) = default;
    StockCommandAcknowledgementEvidenceState(
        StockCommandAcknowledgementEvidenceState&&) noexcept = default;
    StockCommandAcknowledgementEvidenceState& operator=(
        const StockCommandAcknowledgementEvidenceState&) = delete;
    StockCommandAcknowledgementEvidenceState& operator=(
        StockCommandAcknowledgementEvidenceState&&) = delete;
    ~StockCommandAcknowledgementEvidenceState() = default;

    [[nodiscard]] std::uint64_t runtime_generation() const noexcept;
    [[nodiscard]] StockCommandAcknowledgementEvidenceStatus status()
        const noexcept;
    [[nodiscard]] std::span<
        const StockCommandAcknowledgementCandidateObservation>
    candidates() const noexcept;
    [[nodiscard]] std::span<
        const StockCommandAcknowledgementCorrelationObservation>
    correlations() const noexcept;
    [[nodiscard]] bool has_domain(
        StockCommandAcknowledgementCandidateDomain domain) const noexcept;
    // There is deliberately no conversion to prediction's
    // AuthoritativeCommandAcknowledgement in this layer.
    [[nodiscard]] bool exact_usercmd_sequence_available() const noexcept;
    [[nodiscard]] StockRuntimeCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] StockRuntimeEvidenceProfile evidence_profile() const noexcept;

private:
    friend class StockCommandAcknowledgementEvidenceBuilder;

    StockCommandAcknowledgementEvidenceState(
        std::uint64_t runtime_generation,
        StockCommandAcknowledgementEvidenceStatus status,
        std::vector<StockCommandAcknowledgementCandidateObservation>
            candidates,
        std::vector<StockCommandAcknowledgementCorrelationObservation>
            correlations,
        StockRuntimeCompatibilityProfile compatibility_profile,
        StockRuntimeEvidenceProfile evidence_profile) noexcept;

    std::uint64_t runtime_generation_{0U};
    StockCommandAcknowledgementEvidenceStatus status_{
        StockCommandAcknowledgementEvidenceStatus::unobserved};
    std::vector<StockCommandAcknowledgementCandidateObservation> candidates_;
    std::vector<StockCommandAcknowledgementCorrelationObservation>
        correlations_;
    StockRuntimeCompatibilityProfile compatibility_profile_{
        StockRuntimeCompatibilityProfile::
            stock_protocol_48_build_10210_evidence_pending};
    StockRuntimeEvidenceProfile evidence_profile_{
        StockRuntimeEvidenceProfile::
            controlled_signed_stock_transcript_pending};
};

struct StockCommandAcknowledgementEvidencePublishResult {
    std::optional<StockCommandAcknowledgementEvidenceState> state;
    std::optional<StockCommandAcknowledgementEvidenceError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value() && !error.has_value();
    }
};

class StockCommandAcknowledgementEvidenceBuilder final {
public:
    explicit StockCommandAcknowledgementEvidenceBuilder(
        std::uint64_t runtime_generation,
        StockCommandAcknowledgementEvidenceLimits limits = {},
        StockRuntimeCompatibilityProfile compatibility_profile =
            StockRuntimeCompatibilityProfile::
                stock_protocol_48_build_10210_evidence_pending,
        StockRuntimeEvidenceProfile evidence_profile =
            StockRuntimeEvidenceProfile::
                controlled_signed_stock_transcript_pending) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const StockCommandAcknowledgementEvidenceLimits& limits()
        const noexcept;

    [[nodiscard]] StockCommandAcknowledgementEvidenceOperationResult
    observe_candidate(
        StockCommandAcknowledgementCandidateDomain domain,
        std::uint64_t value,
        std::size_t source_record_ordinal,
        StockRuntimeSourceCursor source_cursor);
    [[nodiscard]] StockCommandAcknowledgementEvidenceOperationResult
    observe_correlation(
        StockCommandAcknowledgementCorrelationObservation correlation);
    [[nodiscard]] StockCommandAcknowledgementEvidencePublishResult publish()
        const;

private:
    [[nodiscard]] bool has_candidate_domain(
        StockCommandAcknowledgementCandidateDomain domain) const noexcept;

    std::uint64_t runtime_generation_{0U};
    StockCommandAcknowledgementEvidenceLimits limits_{};
    StockRuntimeCompatibilityProfile compatibility_profile_{
        StockRuntimeCompatibilityProfile::
            stock_protocol_48_build_10210_evidence_pending};
    StockRuntimeEvidenceProfile evidence_profile_{
        StockRuntimeEvidenceProfile::
            controlled_signed_stock_transcript_pending};
    bool valid_configuration_{false};
    std::vector<StockCommandAcknowledgementCandidateObservation> candidates_;
    std::vector<StockCommandAcknowledgementCorrelationObservation>
        correlations_;
};

[[nodiscard]] std::string_view to_string(
    StockCommandAcknowledgementCandidateDomain domain) noexcept;
[[nodiscard]] std::string_view to_string(
    StockCommandAcknowledgementEvidenceStatus status) noexcept;
[[nodiscard]] std::string_view to_string(
    StockCommandAcknowledgementEvidenceErrorCode code) noexcept;

} // namespace hlclient::goldsrc
