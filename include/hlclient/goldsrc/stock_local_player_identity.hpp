#pragma once

#include <hlclient/goldsrc/stock_runtime_message_catalog.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::size_t kDefaultMaximumStockIdentityEvidenceRecords = 32U;
inline constexpr std::size_t kHardMaximumStockIdentityEvidenceRecords = 256U;
inline constexpr std::uint32_t kDefaultMaximumStockIdentityEntityNumber =
    65'535U;
inline constexpr std::uint32_t kHardMaximumStockIdentityEntityNumber =
    1'048'575U;

struct StockLocalPlayerIdentityLimits {
    std::size_t maximum_evidence_records{
        kDefaultMaximumStockIdentityEvidenceRecords};
    std::uint32_t maximum_entity_number{
        kDefaultMaximumStockIdentityEntityNumber};
};

[[nodiscard]] bool valid_stock_local_player_identity_limits(
    const StockLocalPlayerIdentityLimits& limits) noexcept;

enum class StockLocalPlayerIdentityStatus : std::uint8_t {
    unresolved,
    single_client_candidate,
    multi_client_correlated,
    confirmed_for_profile,
    conflicting,
};

// Numeric domains remain intentionally distinct. Equality between values from
// two domains is evidence to investigate, not an identity relation by itself.
enum class StockLocalPlayerIdentityEvidenceSource : std::uint8_t {
    server_info_slot_candidate,
    view_entity_candidate,
    user_info_client_index,
    player_entity_candidate,
    client_local_message_association,
    two_client_differential_correlation,
    status_metadata_projection,
};

enum class StockLocalPlayerIdentityEvidenceKind : std::uint8_t {
    routing_value_only,
    entity_number_candidate,
    multi_client_relation,
};

struct StockLocalPlayerIdentityEvidenceRecord {
    StockLocalPlayerIdentityEvidenceSource source{
        StockLocalPlayerIdentityEvidenceSource::user_info_client_index};
    StockLocalPlayerIdentityEvidenceKind kind{
        StockLocalPlayerIdentityEvidenceKind::routing_value_only};
    std::uint32_t domain_value{0U};
    std::optional<std::uint32_t> candidate_entity_number;
    std::optional<StockRuntimeSourceCursor> source_cursor;

    [[nodiscard]] friend bool operator==(
        const StockLocalPlayerIdentityEvidenceRecord&,
        const StockLocalPlayerIdentityEvidenceRecord&) = default;
};

enum class StockLocalPlayerIdentityErrorCode : std::uint8_t {
    invalid_configuration,
    invalid_runtime_generation,
    invalid_source_cursor,
    invalid_domain_value,
    entity_number_limit_exceeded,
    evidence_limit_exceeded,
    duplicate_evidence,
    profile_mismatch,
    stock_evidence_pending,
    allocation_failed,
};

struct StockLocalPlayerIdentityError {
    StockLocalPlayerIdentityErrorCode code{
        StockLocalPlayerIdentityErrorCode::invalid_configuration};
    std::optional<StockLocalPlayerIdentityEvidenceSource> source;
    std::string_view context;
};

struct StockLocalPlayerIdentityOperationResult {
    std::optional<StockLocalPlayerIdentityError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

class StockLocalPlayerIdentityBuilder;

class StockLocalPlayerIdentityState final {
public:
    StockLocalPlayerIdentityState(const StockLocalPlayerIdentityState&) =
        default;
    StockLocalPlayerIdentityState(StockLocalPlayerIdentityState&&) noexcept =
        default;
    StockLocalPlayerIdentityState& operator=(
        const StockLocalPlayerIdentityState&) = delete;
    StockLocalPlayerIdentityState& operator=(
        StockLocalPlayerIdentityState&&) = delete;
    ~StockLocalPlayerIdentityState() = default;

    [[nodiscard]] std::uint64_t runtime_generation() const noexcept;
    [[nodiscard]] StockLocalPlayerIdentityStatus status() const noexcept;
    [[nodiscard]] const std::optional<std::uint32_t>&
    candidate_entity_number() const noexcept;
    // This accessor remains empty until a future confirmed stock profile is
    // deliberately implemented. It cannot promote a candidate.
    [[nodiscard]] std::optional<std::uint32_t> confirmed_entity_number()
        const noexcept;
    [[nodiscard]] std::span<const StockLocalPlayerIdentityEvidenceRecord>
    evidence_records() const noexcept;
    [[nodiscard]] std::size_t evidence_record_count() const noexcept;
    [[nodiscard]] bool has_multi_client_evidence() const noexcept;
    [[nodiscard]] StockRuntimeCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] StockRuntimeEvidenceProfile evidence_profile() const noexcept;

private:
    friend class StockLocalPlayerIdentityBuilder;

    StockLocalPlayerIdentityState(
        std::uint64_t runtime_generation,
        StockLocalPlayerIdentityStatus status,
        std::optional<std::uint32_t> candidate_entity_number,
        std::vector<StockLocalPlayerIdentityEvidenceRecord> evidence_records,
        StockRuntimeCompatibilityProfile compatibility_profile,
        StockRuntimeEvidenceProfile evidence_profile) noexcept;

    std::uint64_t runtime_generation_{0U};
    StockLocalPlayerIdentityStatus status_{
        StockLocalPlayerIdentityStatus::unresolved};
    std::optional<std::uint32_t> candidate_entity_number_;
    std::vector<StockLocalPlayerIdentityEvidenceRecord> evidence_records_;
    StockRuntimeCompatibilityProfile compatibility_profile_{
        StockRuntimeCompatibilityProfile::
            stock_protocol_48_build_10210_evidence_pending};
    StockRuntimeEvidenceProfile evidence_profile_{
        StockRuntimeEvidenceProfile::
            controlled_signed_stock_transcript_pending};
};

struct StockLocalPlayerIdentityPublishResult {
    std::optional<StockLocalPlayerIdentityState> state;
    std::optional<StockLocalPlayerIdentityError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value() && !error.has_value();
    }
};

class StockLocalPlayerIdentityBuilder final {
public:
    explicit StockLocalPlayerIdentityBuilder(
        std::uint64_t runtime_generation,
        StockLocalPlayerIdentityLimits limits = {},
        StockRuntimeCompatibilityProfile compatibility_profile =
            StockRuntimeCompatibilityProfile::
                stock_protocol_48_build_10210_evidence_pending,
        StockRuntimeEvidenceProfile evidence_profile =
            StockRuntimeEvidenceProfile::
                controlled_signed_stock_transcript_pending) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const StockLocalPlayerIdentityLimits& limits() const noexcept;

    [[nodiscard]] StockLocalPlayerIdentityOperationResult
    observe_server_info_slot_candidate(
        std::uint32_t slot,
        StockRuntimeSourceCursor source_cursor);
    [[nodiscard]] StockLocalPlayerIdentityOperationResult
    observe_user_info_client_index(
        std::uint8_t client_index,
        StockRuntimeSourceCursor source_cursor);
    [[nodiscard]] StockLocalPlayerIdentityOperationResult
    observe_view_entity_candidate(
        std::uint32_t entity_number,
        StockRuntimeSourceCursor source_cursor);
    [[nodiscard]] StockLocalPlayerIdentityOperationResult
    observe_player_entity_candidate(
        std::uint32_t entity_number,
        StockRuntimeSourceCursor source_cursor);
    [[nodiscard]] StockLocalPlayerIdentityOperationResult
    observe_client_local_association_candidate(
        std::uint32_t entity_number,
        StockRuntimeSourceCursor source_cursor);
    [[nodiscard]] StockLocalPlayerIdentityOperationResult
    observe_two_client_correlation(
        std::uint8_t client_index,
        std::uint32_t entity_number,
        StockRuntimeSourceCursor source_cursor);
    [[nodiscard]] StockLocalPlayerIdentityOperationResult
    observe_status_metadata_candidate(
        std::uint32_t routing_value,
        std::uint32_t entity_number,
        StockRuntimeSourceCursor source_cursor);

    [[nodiscard]] StockLocalPlayerIdentityPublishResult publish() const;
    [[nodiscard]] std::span<const StockLocalPlayerIdentityEvidenceRecord>
    candidate_evidence() const noexcept;

private:
    [[nodiscard]] StockLocalPlayerIdentityOperationResult append(
        StockLocalPlayerIdentityEvidenceRecord record);

    std::uint64_t runtime_generation_{0U};
    StockLocalPlayerIdentityLimits limits_{};
    StockRuntimeCompatibilityProfile compatibility_profile_{
        StockRuntimeCompatibilityProfile::
            stock_protocol_48_build_10210_evidence_pending};
    StockRuntimeEvidenceProfile evidence_profile_{
        StockRuntimeEvidenceProfile::
            controlled_signed_stock_transcript_pending};
    bool valid_configuration_{false};
    std::vector<StockLocalPlayerIdentityEvidenceRecord> evidence_records_;
};

[[nodiscard]] std::string_view to_string(
    StockLocalPlayerIdentityStatus status) noexcept;
[[nodiscard]] std::string_view to_string(
    StockLocalPlayerIdentityErrorCode code) noexcept;

} // namespace hlclient::goldsrc
