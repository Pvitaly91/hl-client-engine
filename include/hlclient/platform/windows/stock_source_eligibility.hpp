#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

#include <hlclient/platform/windows/binary_identity.hpp>
#include <hlclient/platform/windows/stock_external_target_review.hpp>
#include <hlclient/platform/windows/stock_research_copy.hpp>

namespace hlclient::platform::windows {

inline constexpr std::uint64_t kDefaultExpectedStockSteamBuild = 15'961'492U;

enum class StockSourceComponentProfileStatus {
    not_observed,
    valid,
    missing,
    identity_invalid,
    version_mismatch,
    machine_mismatch,
    signature_invalid,
    app_id_mismatch,
    build_id_mismatch,
};

[[nodiscard]] std::string_view to_string(
    StockSourceComponentProfileStatus status) noexcept;

enum class StockSourceEligibilityStatus {
    success,
    invalid_argument,
    topology_observation_failed,
    topology_incomplete,
    topology_unsafe,
    escaped_target,
    dangling_target,
    unsupported_reparse_tag,
    alternate_data_stream,
    client_profile_invalid,
    server_profile_invalid,
    app_profile_invalid,
    source_already_prepared,
    source_changed,
};

[[nodiscard]] std::string_view to_string(
    StockSourceEligibilityStatus status) noexcept;

// Path-free observations used by the pure assessment boundary. A caller may
// retain private paths and binary identities, but neither is accepted here and
// therefore neither can escape through the public result.
struct StockSourceEligibilityAssessmentInput final {
    StockResearchTopologySummary topology{};
    bool exact_root{false};
    bool reparse_diagnostics_complete{false};
    std::size_t dangling_target_count{0U};
    std::size_t unsupported_tag_count{0U};
    StockSourceComponentProfileStatus client_profile{
        StockSourceComponentProfileStatus::not_observed};
    StockSourceComponentProfileStatus server_profile{
        StockSourceComponentProfileStatus::not_observed};
    StockSourceComponentProfileStatus app_profile{
        StockSourceComponentProfileStatus::not_observed};
    bool source_already_prepared{false};
};

struct StockSourceEligibilitySummary final {
    bool topology_safe{false};
    std::size_t escaped_target_count{0U};
    std::size_t dangling_target_count{0U};
    std::size_t unsupported_tag_count{0U};
    std::size_t alternate_data_stream_count{0U};
    std::size_t inventory_entry_count{0U};
    std::uint64_t inventory_byte_count{0U};
    StockSourceComponentProfileStatus client_profile{
        StockSourceComponentProfileStatus::not_observed};
    StockSourceComponentProfileStatus server_profile{
        StockSourceComponentProfileStatus::not_observed};
    StockSourceComponentProfileStatus app_profile{
        StockSourceComponentProfileStatus::not_observed};
    bool source_already_prepared{false};
    bool research_copy_eligible{false};
    StockSourceEligibilityStatus status{
        StockSourceEligibilityStatus::topology_observation_failed};
};

// Pure, deterministic policy evaluation for independently authored fixtures.
// Zero counters are accepted only when reparse_diagnostics_complete is true.
[[nodiscard]] StockSourceEligibilitySummary
assess_stock_source_eligibility(
    const StockSourceEligibilityAssessmentInput& input) noexcept;

struct StockSourceEligibilityOptions final {
    std::uint64_t expected_app_build{kDefaultExpectedStockSteamBuild};
    StockResearchCopyLimits inventory_limits{};
};

struct StockSourceEligibilityResult final {
    std::optional<StockSourceEligibilitySummary> summary;
    StockSourceEligibilityStatus status{
        StockSourceEligibilityStatus::topology_observation_failed};
    StockResearchCopyErrorCode topology_error{
        StockResearchCopyErrorCode::none};
    StockExternalReviewErrorCode external_diagnostic_error{
        StockExternalReviewErrorCode::none};
    WindowsBinaryIdentityErrorCode client_binary_error{
        WindowsBinaryIdentityErrorCode::none};
    WindowsBinaryIdentityErrorCode server_binary_error{
        WindowsBinaryIdentityErrorCode::none};
    SteamAppManifestErrorCode app_manifest_error{
        SteamAppManifestErrorCode::none};
    std::uint32_t native_error{0U};

    // A completed diagnostic can be ineligible. Boolean conversion therefore
    // means only that a bounded assessment exists, not that copying is allowed.
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return summary.has_value();
    }
};

// Read-only candidate-source gate. It creates no destination or artifact,
// launches no process, touches no network/WFP state, and never returns paths or
// raw hashes. Only a present summary with research_copy_eligible=true permits a
// later research-copy materialization attempt.
[[nodiscard]] StockSourceEligibilityResult
validate_stock_runtime_candidate_source(
    const std::filesystem::path& source_half_life_root,
    const std::filesystem::path& app_manifest_path,
    const StockSourceEligibilityOptions& options = {}) noexcept;

} // namespace hlclient::platform::windows
