#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <hlclient/platform/windows/stock_research_copy.hpp>
#include <hlclient/platform/windows/windows_reparse_provenance.hpp>

namespace hlclient::platform::windows {

class StockExternalSourceDiagnosticSessionPin;

inline constexpr std::string_view kStockExternalTargetReviewSchemaV1 =
    "hlclient.stock-runtime-external-target-review.v1";
inline constexpr std::string_view kStockExternalTargetReviewSchemaV2 =
    "hlclient.stock-runtime-external-target-review.v2";
inline constexpr std::string_view kStockExternalTargetApprovalSchemaV1 =
    "hlclient.stock-runtime-external-target-approval.v1";
inline constexpr std::string_view kStockExternalTargetApprovalPhraseV1 =
    "HLCLIENT_APPROVE_REVIEWED_EXTERNAL_TARGETS_V1";

enum class StockExternalTargetClassification {
    eligible_non_executable_asset_tree,
    contains_executable_code,
    contains_script_or_command,
    contains_mutable_user_state,
    another_application_tree,
    operating_system_tree,
    temporary_or_cache_tree,
    remote_or_device_target,
    nested_external_link,
    unsupported_reparse_topology,
    content_limit_exceeded,
    changed_during_review,
    unknown,
};

[[nodiscard]] std::string_view to_string(
    StockExternalTargetClassification value) noexcept;

enum class StockExternalReviewErrorCode {
    none,
    invalid_argument,
    source_invalid,
    output_parent_invalid,
    review_root_invalid,
    topology_read_failed,
    no_external_targets,
    target_ineligible,
    target_changed,
    limit_exceeded,
    random_failed,
    publication_failed,
    review_manifest_invalid,
    approval_phrase_mismatch,
    approval_expired,
    approval_mismatch,
};

[[nodiscard]] std::string_view to_string(
    StockExternalReviewErrorCode value) noexcept;

struct StockExternalTargetReview final {
    std::filesystem::path source_link_relative_path;
    std::filesystem::path target_root;
    std::string link_identity_sha256;
    std::string target_identity_sha256;
    std::string target_inventory_sha256;
    StockExternalTargetClassification classification{
        StockExternalTargetClassification::unknown};
    std::size_t entry_count{0U};
    std::uint64_t byte_count{0U};
    std::size_t executable_count{0U};
    std::size_t script_or_command_count{0U};
    std::size_t mutable_state_count{0U};
    std::size_t nested_link_count{0U};
    // V1 callers retain the scalar counters above.  V2 consumers must consult
    // inventory_available before using them: an unavailable inventory is not
    // an observed zero.
    bool inventory_available{true};
    bool diagnostic_complete{true};
    std::optional<WindowsReparseTargetObservation> reparse_observation;
    std::optional<StockExternalTopologyFailureWitness> failure_witness;
    bool eligible{false};
};

struct StockExternalReviewSummary final {
    std::filesystem::path review_root;
    std::string source_identity_sha256;
    std::string review_set_sha256;
    std::vector<StockExternalTargetReview> targets;
    std::size_t executable_count{0U};
    std::size_t script_or_command_count{0U};
    std::size_t mutable_state_count{0U};
    std::size_t nested_link_count{0U};
    std::size_t completed_target_count{0U};
    std::size_t ineligible_target_count{0U};
    std::size_t incomplete_target_count{0U};
    bool all_targets_eligible{false};
};

struct StockExternalSourceDiagnostic final {
    std::string source_identity_sha256;
    std::string source_inventory_sha256;
    std::size_t source_entry_count{0U};
    std::uint64_t source_byte_count{0U};
    std::size_t internal_reparse_count{0U};
    std::size_t contained_target_count{0U};
    std::size_t hardlink_count{0U};
    std::size_t alternate_data_stream_count{0U};
    std::vector<StockExternalTargetReview> targets;
    bool exact_local_fixed_root{false};
    bool root_reparse{false};
    bool source_inventory_complete{false};
    bool all_targets_diagnostic_complete{false};
    bool all_targets_eligible{false};

    // Opaque private ownership of the exact handle-rooted source observation.
    // Keeping the diagnostic alive pins the confirmed root, every inventoried
    // entry/directory, and every contained target across downstream validator
    // gates. It contains no public output and has no path accessor.
    std::shared_ptr<const StockExternalSourceDiagnosticSessionPin>
        private_session_pin;
};

struct StockExternalApproval final {
    std::filesystem::path approval_manifest;
    std::string review_set_sha256;
    std::chrono::system_clock::time_point expires_at{};
};

struct StockApprovedExternalTarget final {
    std::filesystem::path source_link_relative_path;
    std::filesystem::path target_root;
    std::string link_identity_sha256;
    std::string target_identity_sha256;
    std::string target_inventory_sha256;
};

struct StockExternalApprovalValidation final {
    std::string review_set_sha256;
    std::string approval_manifest_sha256;
    std::vector<StockApprovedExternalTarget> targets;
    std::size_t executable_count{0U};
    std::size_t script_or_command_count{0U};
    std::size_t mutable_state_count{0U};
    std::size_t nested_link_count{0U};
    std::chrono::system_clock::time_point expires_at{};
};

enum class StockExternalPublicationTestPhase {
    review_summary_published,
    approval_manifest_published,
    review_private_records_published,
};

// Deterministic test seam for the manifest-last transaction boundary.
// Production callers leave this null; the callback receives no private path.
struct StockExternalPublicationTestHook final {
    using Callback = void (*)(
        StockExternalPublicationTestPhase phase,
        void* context) noexcept;

    Callback callback{nullptr};
    void* context{nullptr};
};

template <typename T>
struct StockExternalReviewResult final {
    std::optional<T> value;
    StockExternalReviewErrorCode code{StockExternalReviewErrorCode::none};
    std::uint32_t native_error{0U};
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value.has_value() && code == StockExternalReviewErrorCode::none;
    }
};

[[nodiscard]] StockExternalReviewResult<StockExternalReviewSummary>
review_stock_external_targets(
    const std::filesystem::path& source_root,
    const std::filesystem::path& exact_output_parent,
    const StockResearchCopyLimits& limits = {},
    const StockExternalPublicationTestHook* publication_test_hook = nullptr)
    noexcept;

// Performs the same read-only, bounded source observation as review
// publication, but creates no directory or artifact.  Private paths remain in
// memory for identity binding and must not be printed by callers.
[[nodiscard]] StockExternalReviewResult<StockExternalSourceDiagnostic>
diagnose_stock_external_targets(
    const std::filesystem::path& source_root,
    const StockResearchCopyLimits& limits = {}) noexcept;

[[nodiscard]] StockExternalReviewResult<StockExternalApproval>
approve_stock_external_target_review(
    const std::filesystem::path& exact_review_root,
    std::string_view exact_approval_phrase,
    std::chrono::hours lifetime = std::chrono::hours{24},
    const StockExternalPublicationTestHook* publication_test_hook = nullptr)
    noexcept;

[[nodiscard]] StockExternalReviewResult<StockExternalApprovalValidation>
validate_stock_external_target_approval(
    const std::filesystem::path& exact_approval_manifest,
    const std::filesystem::path& expected_source_root,
    const StockResearchCopyLimits& limits = {}) noexcept;

} // namespace hlclient::platform::windows
