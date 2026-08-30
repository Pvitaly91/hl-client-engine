#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::string_view kStockRuntimeFirstCampaignSchema =
    "hlclient.stock-runtime-first-campaign.v1";

struct StockRuntimeCampaignMatrixEntry final {
    std::string_view map_category;
    std::string_view scenario;
    std::size_t required_runs{0U};
};

[[nodiscard]] std::span<const StockRuntimeCampaignMatrixEntry>
stock_runtime_first_campaign_matrix() noexcept;

enum class StockRuntimeCampaignPublicationState {
    accepted,
    rejected,
    incomplete,
};

// The capture contract has one exact typed terminal that means a bounded
// attempt ended without enough transport to decide. Every other typed,
// non-accepted result is a rejection. PowerShell campaign/verifier adapters
// intentionally mirror this exact classifier.
[[nodiscard]] constexpr StockRuntimeCampaignPublicationState
stock_runtime_campaign_failure_publication(
    const std::string_view failure_category) noexcept
{
    return failure_category == "bounded-session-incomplete"
        ? StockRuntimeCampaignPublicationState::incomplete
        : StockRuntimeCampaignPublicationState::rejected;
}

// One neutral observation at an exact post-resource cursor.  No candidate
// body, packet bytes, endpoint, process identifier, or semantic svc_* name is
// represented by this campaign-facing type.
struct StockRuntimeCampaignCandidateObservation final {
    std::size_t bit_offset{0U};
    std::size_t bit_width{0U};
    bool byte_aligned{false};
    std::optional<std::uint8_t> numeric_candidate;
    std::optional<std::uint8_t> bounded_bit_prefix;
};

// Normalized adapter input.  Corpus/checker/walker validation happens before
// an accepted publication reaches this boundary; the booleans are retained so
// fake-corpus and mutation tests can prove that no individual safety gate is
// accidentally omitted by campaign aggregation.
struct StockRuntimeCampaignRunObservation final {
    std::string run_id;
    std::string map_category;
    std::string scenario;
    std::string profile_identity;
    std::string transport_structural_sha256;
    std::string replay_structural_sha256;
    StockRuntimeCampaignPublicationState publication{
        StockRuntimeCampaignPublicationState::incomplete};

    bool isolation_verified{false};
    bool profile_verified{false};
    bool client_ready{false};
    bool bounded_transport_complete{false};
    std::size_t wrong_source_datagrams{0U};
    bool restoration_exact{false};
    bool external_drift_none{false};
    bool corpus_valid{false};
    bool independent_walker_valid{false};
    bool signon_replay_complete{false};
    bool candidate_body_unconsumed{false};

    std::size_t sequenced_client_to_server{0U};
    std::size_t sequenced_server_to_client{0U};
    std::size_t reassembled_payloads{0U};
    std::size_t decompressed_payloads{0U};
    std::size_t connection_generation_count{0U};
    std::size_t exact_post_resource_boundary_count{0U};
    bool reconnect_generations_distinct{false};
    bool reconnect_candidate_conflict{false};
    std::vector<StockRuntimeCampaignCandidateObservation> candidates;
};

struct StockRuntimeCampaignPendingSlot final {
    std::string map_category;
    std::string scenario;
    std::size_t slot_ordinal{0U};
};

enum class StockRuntimeCampaignCandidateStability {
    evidence_pending,
    stable_observation,
    candidate_conflicting,
};

enum class StockRuntimeCampaignThresholdStatus {
    pending,
    passed,
    conflicting,
};

struct StockRuntimeCampaignLimits final {
    std::size_t maximum_attempted_runs{4'096U};
    std::size_t minimum_accepted_runs{24U};
    std::size_t minimum_sequenced_server_packets{1'000U};
    std::size_t minimum_reconnect_generations{4U};
    std::size_t minimum_exact_boundaries{26U};
    std::size_t minimum_candidate_observations{26U};
    std::size_t minimum_sequenced_server_packets_per_run{100U};
    std::size_t maximum_profile_identity_bytes{256U};
    // Empty means lock to the first otherwise valid accepted observation.
    // Production adapters should provide the exact canary profile identity.
    std::string required_profile_identity;
};

enum class StockRuntimeCampaignErrorCode {
    invalid_configuration,
    invalid_implementation_commit,
    run_limit_exceeded,
    invalid_run_id,
    duplicate_run_id,
    counter_overflow,
    structural_hash_failed,
    allocation_failed,
};

struct StockRuntimeCampaignError final {
    StockRuntimeCampaignErrorCode code{
        StockRuntimeCampaignErrorCode::invalid_configuration};
    std::size_t run_ordinal{0U};
    std::string context;
};

class StockRuntimeCampaignState final {
public:
    StockRuntimeCampaignState(const StockRuntimeCampaignState&) = default;
    StockRuntimeCampaignState& operator=(const StockRuntimeCampaignState&) = delete;
    StockRuntimeCampaignState(StockRuntimeCampaignState&&) noexcept = default;
    StockRuntimeCampaignState& operator=(StockRuntimeCampaignState&&) noexcept = delete;
    ~StockRuntimeCampaignState() = default;

    [[nodiscard]] std::string_view implementation_commit() const noexcept;
    [[nodiscard]] std::string_view profile_identity() const noexcept;
    [[nodiscard]] std::size_t attempted_runs() const noexcept;
    [[nodiscard]] std::size_t accepted_runs() const noexcept;
    [[nodiscard]] std::size_t rejected_runs() const noexcept;
    [[nodiscard]] std::size_t incomplete_runs() const noexcept;
    [[nodiscard]] std::size_t pending_runs() const noexcept;
    [[nodiscard]] std::size_t sequenced_client_to_server() const noexcept;
    [[nodiscard]] std::size_t sequenced_server_to_client() const noexcept;
    [[nodiscard]] std::size_t reassembled_payloads() const noexcept;
    [[nodiscard]] std::size_t decompressed_payloads() const noexcept;
    [[nodiscard]] std::size_t exact_post_resource_boundaries() const noexcept;
    [[nodiscard]] std::size_t candidate_observations() const noexcept;
    [[nodiscard]] std::size_t reconnect_generations() const noexcept;
    [[nodiscard]] std::size_t accepted_runs_for(
        std::string_view map_category,
        std::string_view scenario) const noexcept;
    [[nodiscard]] StockRuntimeCampaignCandidateStability
    candidate_stability() const noexcept;
    [[nodiscard]] StockRuntimeCampaignThresholdStatus threshold_status() const noexcept;
    [[nodiscard]] const std::vector<StockRuntimeCampaignPendingSlot>&
    pending_slots() const noexcept;
    [[nodiscard]] const std::vector<std::string>& accepted_run_ids() const noexcept;
    [[nodiscard]] std::string_view structural_sha256() const noexcept;
    [[nodiscard]] constexpr bool evidence_publication_allowed() const noexcept
    {
        return threshold_status_ == StockRuntimeCampaignThresholdStatus::passed;
    }

private:
    friend class StockRuntimeCampaignAggregator;

    StockRuntimeCampaignState(
        std::string implementation_commit,
        std::string profile_identity,
        std::size_t attempted_runs,
        std::size_t accepted_runs,
        std::size_t rejected_runs,
        std::size_t incomplete_runs,
        std::size_t pending_runs,
        std::size_t sequenced_client_to_server,
        std::size_t sequenced_server_to_client,
        std::size_t reassembled_payloads,
        std::size_t decompressed_payloads,
        std::size_t exact_post_resource_boundaries,
        std::size_t candidate_observations,
        std::size_t reconnect_generations,
        std::vector<std::size_t> matrix_accepted_counts,
        StockRuntimeCampaignCandidateStability candidate_stability,
        StockRuntimeCampaignThresholdStatus threshold_status,
        std::vector<StockRuntimeCampaignPendingSlot> pending_slots,
        std::vector<std::string> accepted_run_ids,
        std::string structural_sha256) noexcept;

    std::string implementation_commit_;
    std::string profile_identity_;
    std::size_t attempted_runs_{0U};
    std::size_t accepted_runs_{0U};
    std::size_t rejected_runs_{0U};
    std::size_t incomplete_runs_{0U};
    std::size_t pending_runs_{0U};
    std::size_t sequenced_client_to_server_{0U};
    std::size_t sequenced_server_to_client_{0U};
    std::size_t reassembled_payloads_{0U};
    std::size_t decompressed_payloads_{0U};
    std::size_t exact_post_resource_boundaries_{0U};
    std::size_t candidate_observations_{0U};
    std::size_t reconnect_generations_{0U};
    std::vector<std::size_t> matrix_accepted_counts_;
    StockRuntimeCampaignCandidateStability candidate_stability_{
        StockRuntimeCampaignCandidateStability::evidence_pending};
    StockRuntimeCampaignThresholdStatus threshold_status_{
        StockRuntimeCampaignThresholdStatus::pending};
    std::vector<StockRuntimeCampaignPendingSlot> pending_slots_;
    std::vector<std::string> accepted_run_ids_;
    std::string structural_sha256_;
};

struct StockRuntimeCampaignBuildResult final {
    std::optional<StockRuntimeCampaignState> state;
    std::optional<StockRuntimeCampaignError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

class StockRuntimeCampaignAggregator final {
public:
    explicit StockRuntimeCampaignAggregator(
        StockRuntimeCampaignLimits limits = {}) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const StockRuntimeCampaignLimits& limits() const noexcept;
    [[nodiscard]] StockRuntimeCampaignBuildResult build(
        std::span<const StockRuntimeCampaignRunObservation> observations,
        std::string_view implementation_commit) const;

private:
    StockRuntimeCampaignLimits limits_;
};

[[nodiscard]] std::string serialize_stock_runtime_first_campaign_manifest(
    const StockRuntimeCampaignState& state);

[[nodiscard]] constexpr std::string_view to_string(
    const StockRuntimeCampaignCandidateStability value) noexcept
{
    switch (value) {
    case StockRuntimeCampaignCandidateStability::evidence_pending:
        return "evidence_pending";
    case StockRuntimeCampaignCandidateStability::stable_observation:
        return "stable_observation";
    case StockRuntimeCampaignCandidateStability::candidate_conflicting:
        return "candidate_conflicting";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const StockRuntimeCampaignThresholdStatus value) noexcept
{
    switch (value) {
    case StockRuntimeCampaignThresholdStatus::pending: return "pending";
    case StockRuntimeCampaignThresholdStatus::passed: return "passed";
    case StockRuntimeCampaignThresholdStatus::conflicting: return "conflicting";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const StockRuntimeCampaignErrorCode value) noexcept
{
    switch (value) {
    case StockRuntimeCampaignErrorCode::invalid_configuration:
        return "invalid_configuration";
    case StockRuntimeCampaignErrorCode::invalid_implementation_commit:
        return "invalid_implementation_commit";
    case StockRuntimeCampaignErrorCode::run_limit_exceeded:
        return "run_limit_exceeded";
    case StockRuntimeCampaignErrorCode::invalid_run_id: return "invalid_run_id";
    case StockRuntimeCampaignErrorCode::duplicate_run_id: return "duplicate_run_id";
    case StockRuntimeCampaignErrorCode::counter_overflow: return "counter_overflow";
    case StockRuntimeCampaignErrorCode::structural_hash_failed:
        return "structural_hash_failed";
    case StockRuntimeCampaignErrorCode::allocation_failed: return "allocation_failed";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
