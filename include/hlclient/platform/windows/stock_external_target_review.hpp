#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <hlclient/platform/windows/stock_research_copy.hpp>

namespace hlclient::platform::windows {

inline constexpr std::string_view kStockExternalTargetReviewSchemaV1 =
    "hlclient.stock-runtime-external-target-review.v1";
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
    bool all_targets_eligible{false};
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
    const StockResearchCopyLimits& limits = {}) noexcept;

[[nodiscard]] StockExternalReviewResult<StockExternalApproval>
approve_stock_external_target_review(
    const std::filesystem::path& exact_review_root,
    std::string_view exact_approval_phrase,
    std::chrono::hours lifetime = std::chrono::hours{24}) noexcept;

[[nodiscard]] StockExternalReviewResult<StockExternalApprovalValidation>
validate_stock_external_target_approval(
    const std::filesystem::path& exact_approval_manifest,
    const std::filesystem::path& expected_source_root,
    const StockResearchCopyLimits& limits = {}) noexcept;

} // namespace hlclient::platform::windows
