#pragma once

#include <hlclient/goldsrc/stock_captured_signon_replay.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::string_view kStockRuntimeFirstCandidateNeutralName =
    "first_post_resource_runtime_candidate";

enum class StockRuntimeFirstCandidateStability {
    single_observation,
    stable_observation,
    candidate_conflicting,
};

enum class StockRuntimeFirstObservationEvidenceProfile {
    exact_post_resource_cursor_neutral_candidate,
};

struct StockRuntimeFirstObservationInput final {
    std::string run_id;
    std::string version_profile;
    StockPostResourceResponseCursor cursor;
    std::span<const std::byte> source_payload;
    bool accepted_evidence_run{false};
    bool known_signon_validated{false};
};

struct StockRuntimeFirstObservationOccurrence final {
    std::string run_id;
    std::size_t replay_payload_ordinal{0U};
    std::size_t corpus_observed_ordinal{0U};
    std::size_t delivery_ordinal{0U};
    std::size_t byte_offset{0U};
    std::size_t bit_offset{0U};
    std::size_t candidate_bit_width{0U};
    std::optional<std::uint8_t> numeric_candidate;
    std::optional<std::uint8_t> bounded_bit_prefix;
    bool byte_aligned{false};
};

// Metadata-only result. Candidate bodies and owning payload bytes never enter
// this state. The cursor remains exactly where StockCapturedSignonReplay left
// it, even after the neutral prefix observation.
class StockRuntimeFirstObservationState final {
public:
    StockRuntimeFirstObservationState(const StockRuntimeFirstObservationState&) = default;
    StockRuntimeFirstObservationState& operator=(
        const StockRuntimeFirstObservationState&) = delete;
    StockRuntimeFirstObservationState(StockRuntimeFirstObservationState&&) noexcept = default;
    StockRuntimeFirstObservationState& operator=(
        StockRuntimeFirstObservationState&&) noexcept = delete;
    ~StockRuntimeFirstObservationState() = default;

    [[nodiscard]] std::string_view neutral_candidate_name() const noexcept;
    [[nodiscard]] const StockPostResourceResponseCursor& exact_cursor() const noexcept;
    [[nodiscard]] std::size_t candidate_bit_width() const noexcept;
    [[nodiscard]] const std::optional<std::uint8_t>& numeric_candidate() const noexcept;
    [[nodiscard]] const std::optional<std::uint8_t>& bounded_bit_prefix() const noexcept;
    [[nodiscard]] bool byte_aligned() const noexcept;
    [[nodiscard]] std::size_t recurrence_count() const noexcept;
    [[nodiscard]] const std::vector<StockRuntimeFirstObservationOccurrence>&
    occurrences() const noexcept;
    [[nodiscard]] StockRuntimeFirstCandidateStability stability() const noexcept;
    [[nodiscard]] std::string_view version_profile() const noexcept;
    [[nodiscard]] StockRuntimeFirstObservationEvidenceProfile evidence_profile() const noexcept;
    [[nodiscard]] constexpr bool body_consumed() const noexcept { return false; }
    [[nodiscard]] constexpr bool semantic_category_assigned() const noexcept { return false; }

private:
    friend class StockRuntimeFirstObservationBuilder;

    StockRuntimeFirstObservationState(
        StockPostResourceResponseCursor exact_cursor,
        std::size_t candidate_bit_width,
        std::optional<std::uint8_t> numeric_candidate,
        std::optional<std::uint8_t> bounded_bit_prefix,
        bool byte_aligned,
        std::vector<StockRuntimeFirstObservationOccurrence> occurrences,
        StockRuntimeFirstCandidateStability stability,
        std::string version_profile) noexcept;

    StockPostResourceResponseCursor exact_cursor_;
    std::size_t candidate_bit_width_{0U};
    std::optional<std::uint8_t> numeric_candidate_;
    std::optional<std::uint8_t> bounded_bit_prefix_;
    bool byte_aligned_{false};
    std::vector<StockRuntimeFirstObservationOccurrence> occurrences_;
    StockRuntimeFirstCandidateStability stability_{
        StockRuntimeFirstCandidateStability::single_observation};
    std::string version_profile_;
};

struct StockRuntimeFirstObservationLimits final {
    std::size_t maximum_runs{64U};
    std::size_t minimum_stable_runs{2U};
    std::size_t maximum_payload_bytes{1'048'576U};
    std::size_t maximum_version_profile_bytes{128U};
};

enum class StockRuntimeFirstObservationErrorCode {
    invalid_configuration,
    empty_input,
    run_limit_exceeded,
    invalid_run_id,
    duplicate_run_id,
    unaccepted_run,
    signon_not_validated,
    invalid_version_profile,
    version_profile_mismatch,
    invalid_cursor,
    payload_geometry_mismatch,
    missing_candidate,
    allocation_failed,
};

struct StockRuntimeFirstObservationError final {
    StockRuntimeFirstObservationErrorCode code{
        StockRuntimeFirstObservationErrorCode::invalid_configuration};
    std::size_t run_ordinal{0U};
    std::string context;
};

struct StockRuntimeFirstObservationBuildResult final {
    std::optional<StockRuntimeFirstObservationState> state;
    std::optional<StockRuntimeFirstObservationError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

class StockRuntimeFirstObservationBuilder final {
public:
    explicit StockRuntimeFirstObservationBuilder(
        StockRuntimeFirstObservationLimits limits = {}) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const StockRuntimeFirstObservationLimits& limits() const noexcept;
    [[nodiscard]] StockRuntimeFirstObservationBuildResult build(
        std::span<const StockRuntimeFirstObservationInput> observations) const;

private:
    StockRuntimeFirstObservationLimits limits_;
};

[[nodiscard]] constexpr std::string_view to_string(
    StockRuntimeFirstCandidateStability stability) noexcept
{
    switch (stability) {
    case StockRuntimeFirstCandidateStability::single_observation:
        return "single_observation";
    case StockRuntimeFirstCandidateStability::stable_observation:
        return "stable_observation";
    case StockRuntimeFirstCandidateStability::candidate_conflicting:
        return "candidate_conflicting";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    StockRuntimeFirstObservationErrorCode code) noexcept
{
    switch (code) {
    case StockRuntimeFirstObservationErrorCode::invalid_configuration: return "invalid_configuration";
    case StockRuntimeFirstObservationErrorCode::empty_input: return "empty_input";
    case StockRuntimeFirstObservationErrorCode::run_limit_exceeded: return "run_limit_exceeded";
    case StockRuntimeFirstObservationErrorCode::invalid_run_id: return "invalid_run_id";
    case StockRuntimeFirstObservationErrorCode::duplicate_run_id: return "duplicate_run_id";
    case StockRuntimeFirstObservationErrorCode::unaccepted_run: return "unaccepted_run";
    case StockRuntimeFirstObservationErrorCode::signon_not_validated: return "signon_not_validated";
    case StockRuntimeFirstObservationErrorCode::invalid_version_profile: return "invalid_version_profile";
    case StockRuntimeFirstObservationErrorCode::version_profile_mismatch: return "version_profile_mismatch";
    case StockRuntimeFirstObservationErrorCode::invalid_cursor: return "invalid_cursor";
    case StockRuntimeFirstObservationErrorCode::payload_geometry_mismatch: return "payload_geometry_mismatch";
    case StockRuntimeFirstObservationErrorCode::missing_candidate: return "missing_candidate";
    case StockRuntimeFirstObservationErrorCode::allocation_failed: return "allocation_failed";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
