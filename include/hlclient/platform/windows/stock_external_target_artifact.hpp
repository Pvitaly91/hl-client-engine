#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::platform::windows {

inline constexpr std::size_t kMaximumStockExternalArtifactBytes = 64U * 1'024U;
inline constexpr std::size_t kMaximumStockExternalArtifactTargets = 4'096U;

inline constexpr std::string_view kStockExternalReviewRequestSchemaV1 =
    "hlclient.stock-runtime-external-target-review-request.v1";
inline constexpr std::string_view kStockExternalPrivateTargetSchemaV1 =
    "hlclient.stock-runtime-external-target-private.v1";
inline constexpr std::string_view kStockExternalReviewSummarySchemaV1 =
    "hlclient.stock-runtime-external-target-review-summary.v1";
inline constexpr std::string_view kStockExternalReviewRequestSchemaV2 =
    "hlclient.stock-runtime-external-target-review-request.v2";
inline constexpr std::string_view kStockExternalPrivateTargetSchemaV2 =
    "hlclient.stock-runtime-external-target-private.v2";
inline constexpr std::string_view kStockExternalReviewSummarySchemaV2 =
    "hlclient.stock-runtime-external-target-review-summary.v2";
inline constexpr std::string_view kStockExternalApprovalArtifactSchemaV1 =
    "hlclient.stock-runtime-external-target-approval.v1";
inline constexpr std::string_view kStockExternalApprovalConfirmationProfileV1 =
    "HLCLIENT_APPROVE_REVIEWED_EXTERNAL_TARGETS_V1";

inline constexpr std::wstring_view kStockExternalReviewRequestLeaf =
    L"review-request.json";
inline constexpr std::wstring_view kStockExternalReviewSummaryLeaf =
    L"review-summary.json";
inline constexpr std::wstring_view kStockExternalApprovalLeaf =
    L"external-target-approval.json";

enum class StockExternalArtifactClassification {
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
    StockExternalArtifactClassification value) noexcept;
[[nodiscard]] std::optional<StockExternalArtifactClassification>
stock_external_artifact_classification_from_string(
    std::string_view value) noexcept;

enum class StockExternalArtifactErrorCode {
    none,
    invalid_argument,
    artifact_too_large,
    malformed_json,
    duplicate_property,
    unknown_property,
    missing_property,
    invalid_property_type,
    invalid_property_value,
    invalid_utf8,
    invalid_review_directory,
    invalid_leaf_name,
    review_directory_open_failed,
    review_directory_identity_invalid,
    artifact_open_failed,
    artifact_not_ordinary_file,
    artifact_hardlink_rejected,
    artifact_alternate_data_stream,
    artifact_identity_query_failed,
    artifact_exact_path_mismatch,
    artifact_read_failed,
    artifact_changed,
    digest_failed,
};

[[nodiscard]] std::string_view to_string(
    StockExternalArtifactErrorCode value) noexcept;

template <typename T>
struct StockExternalArtifactResult final {
    std::optional<T> value;
    StockExternalArtifactErrorCode code{StockExternalArtifactErrorCode::none};
    std::uint32_t native_error{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value.has_value() &&
               code == StockExternalArtifactErrorCode::none;
    }
};

struct StockExternalArtifactFileIdentity final {
    std::uint64_t volume_serial_number{0U};
    std::array<std::byte, 16U> file_id{};
    std::string final_path;
    std::string identity_sha256;
    std::uint32_t reparse_tag{0U};
    bool directory{false};

    friend bool operator==(const StockExternalArtifactFileIdentity&,
                           const StockExternalArtifactFileIdentity&) noexcept =
        default;
};

struct StockExternalArtifactInventory final {
    std::uint64_t entry_count{0U};
    std::uint64_t byte_count{0U};
    std::string inventory_sha256;
    std::uint64_t executable_count{0U};
    std::uint64_t script_or_command_count{0U};
    std::uint64_t mutable_state_count{0U};
    std::uint64_t nested_link_count{0U};

    friend bool operator==(const StockExternalArtifactInventory&,
                           const StockExternalArtifactInventory&) noexcept =
        default;
};

struct StockExternalReviewRequestArtifact final {
    StockExternalArtifactFileIdentity source_root_identity;
    StockExternalArtifactInventory source_inventory;
    std::string review_root_fingerprint;
    std::string review_nonce;
    std::uint64_t review_timestamp_unix_seconds{0U};
    std::string implementation_profile;
    std::uint64_t target_count{0U};

    friend bool operator==(const StockExternalReviewRequestArtifact&,
                           const StockExternalReviewRequestArtifact&) noexcept =
        default;
};

struct StockExternalPrivateTargetArtifact final {
    std::uint64_t ordinal{0U};
    std::string review_nonce;
    std::string source_root_fingerprint;
    std::string source_link_relative_path;
    StockExternalArtifactFileIdentity source_link_identity;
    std::string target_canonical_path;
    StockExternalArtifactFileIdentity target_identity;
    StockExternalArtifactInventory target_inventory;
    StockExternalArtifactClassification classification{
        StockExternalArtifactClassification::unknown};
    bool eligible{false};

    friend bool operator==(const StockExternalPrivateTargetArtifact&,
                           const StockExternalPrivateTargetArtifact&) noexcept =
        default;
};

struct StockExternalReviewTargetBindingArtifact final {
    std::uint64_t ordinal{0U};
    std::string private_record_sha256;
    std::string link_identity_sha256;
    std::string target_identity_sha256;
    std::string target_inventory_sha256;
    StockExternalArtifactClassification classification{
        StockExternalArtifactClassification::unknown};
    bool eligible{false};

    friend bool operator==(
        const StockExternalReviewTargetBindingArtifact&,
        const StockExternalReviewTargetBindingArtifact&) noexcept = default;
};

struct StockExternalReviewSummaryArtifact final {
    std::string review_root_fingerprint;
    std::string source_root_fingerprint;
    StockExternalArtifactInventory source_inventory;
    std::string review_nonce;
    std::uint64_t review_timestamp_unix_seconds{0U};
    std::string implementation_profile;
    std::vector<StockExternalReviewTargetBindingArtifact> targets;
    std::uint64_t eligible_count{0U};
    std::uint64_t ineligible_count{0U};
    std::uint64_t unknown_count{0U};
    std::uint64_t executable_target_count{0U};
    std::uint64_t mutable_state_target_count{0U};
    bool all_targets_eligible{false};

    friend bool operator==(const StockExternalReviewSummaryArtifact&,
                           const StockExternalReviewSummaryArtifact&) noexcept =
        default;
};

// V2 is intentionally represented by distinct types and entry points.  The
// V1 JSON protocol is closed, and must continue to reject every V2-only
// property instead of silently accepting a widened approval contract.
struct StockExternalPrivateTargetArtifactV2 final {
    std::uint64_t ordinal{0U};
    std::string review_nonce;
    std::string source_root_fingerprint;
    std::string source_link_relative_path;
    StockExternalArtifactFileIdentity source_link_identity;
    std::uint32_t raw_reparse_tag{0U};
    bool microsoft_tag{false};
    bool name_surrogate_tag{false};
    bool directory{false};
    std::uint64_t payload_byte_count{0U};
    std::string payload_sha256;
    std::string tag_category;
    std::string substitute_name;
    std::string print_name;
    std::string normalized_target_expression;
    std::string expression_kind;
    std::string reachability;
    std::string failure_phase;
    std::string native_error_category;
    std::uint32_t native_error{0U};
    std::string witness_sha256;
    std::optional<std::string> target_canonical_path;
    std::optional<StockExternalArtifactFileIdentity> target_identity;
    std::optional<StockExternalArtifactInventory> target_inventory;
    StockExternalArtifactClassification classification{
        StockExternalArtifactClassification::unknown};
    bool diagnostic_complete{false};
    bool eligible{false};

    friend bool operator==(const StockExternalPrivateTargetArtifactV2&,
                           const StockExternalPrivateTargetArtifactV2&)
        noexcept = default;
};

struct StockExternalReviewTargetBindingArtifactV2 final {
    std::uint64_t ordinal{0U};
    std::string private_record_sha256;
    std::string link_identity_sha256;
    std::optional<std::string> target_identity_sha256;
    std::optional<std::string> target_inventory_sha256;
    StockExternalArtifactClassification classification{
        StockExternalArtifactClassification::unknown};
    std::string tag_category;
    std::string expression_kind;
    std::string reachability;
    std::string failure_phase;
    std::string native_error_category;
    bool diagnostic_complete{false};
    bool inventory_available{false};
    bool eligible{false};

    friend bool operator==(
        const StockExternalReviewTargetBindingArtifactV2&,
        const StockExternalReviewTargetBindingArtifactV2&) noexcept = default;
};

struct StockExternalReviewSummaryArtifactV2 final {
    std::string review_root_fingerprint;
    std::string source_root_fingerprint;
    StockExternalArtifactInventory source_inventory;
    std::string review_nonce;
    std::uint64_t review_timestamp_unix_seconds{0U};
    std::string implementation_profile;
    std::vector<StockExternalReviewTargetBindingArtifactV2> targets;
    std::uint64_t completed_count{0U};
    std::uint64_t eligible_count{0U};
    std::uint64_t ineligible_count{0U};
    std::uint64_t incomplete_count{0U};
    bool all_targets_eligible{false};

    friend bool operator==(const StockExternalReviewSummaryArtifactV2&,
                           const StockExternalReviewSummaryArtifactV2&)
        noexcept = default;
};

struct StockExternalApprovedTargetBindingArtifact final {
    std::uint64_t ordinal{0U};
    std::string link_identity_sha256;
    std::string target_identity_sha256;
    std::string target_inventory_sha256;
    StockExternalArtifactClassification classification{
        StockExternalArtifactClassification::unknown};

    friend bool operator==(
        const StockExternalApprovedTargetBindingArtifact&,
        const StockExternalApprovedTargetBindingArtifact&) noexcept = default;
};

struct StockExternalApprovalArtifact final {
    std::string review_schema;
    std::uint64_t review_version{1U};
    std::string review_root_fingerprint;
    std::string review_digest_sha256;
    std::string source_root_fingerprint;
    StockExternalArtifactInventory source_inventory;
    std::string review_nonce;
    std::string approval_nonce;
    std::uint64_t approval_timestamp_unix_seconds{0U};
    std::uint64_t expiration_unix_seconds{0U};
    std::uint64_t approval_count{0U};
    std::string confirmation_profile;
    std::string implementation_profile;
    std::vector<StockExternalApprovedTargetBindingArtifact> approved_targets;

    friend bool operator==(const StockExternalApprovalArtifact&,
                           const StockExternalApprovalArtifact&) noexcept =
        default;
};

using StockExternalArtifactTextResult =
    StockExternalArtifactResult<std::string>;

// Artifact JSON is a deliberately closed protocol: serializers emit one
// canonical property order and parsers require the exact property set at every
// nesting level. This keeps approval digests deterministic and makes schema
// extension an explicit version change instead of an implicit compatibility
// decision.
[[nodiscard]] StockExternalArtifactTextResult
serialize_stock_external_review_request(
    const StockExternalReviewRequestArtifact& artifact) noexcept;
[[nodiscard]] StockExternalArtifactResult<StockExternalReviewRequestArtifact>
parse_stock_external_review_request(std::string_view json) noexcept;

[[nodiscard]] StockExternalArtifactTextResult
serialize_stock_external_private_target(
    const StockExternalPrivateTargetArtifact& artifact) noexcept;
[[nodiscard]] StockExternalArtifactResult<StockExternalPrivateTargetArtifact>
parse_stock_external_private_target(std::string_view json) noexcept;

[[nodiscard]] StockExternalArtifactTextResult
serialize_stock_external_review_summary(
    const StockExternalReviewSummaryArtifact& artifact) noexcept;
[[nodiscard]] StockExternalArtifactResult<StockExternalReviewSummaryArtifact>
parse_stock_external_review_summary(std::string_view json) noexcept;

[[nodiscard]] StockExternalArtifactTextResult
serialize_stock_external_review_request_v2(
    const StockExternalReviewRequestArtifact& artifact) noexcept;
[[nodiscard]] StockExternalArtifactResult<StockExternalReviewRequestArtifact>
parse_stock_external_review_request_v2(std::string_view json) noexcept;

[[nodiscard]] StockExternalArtifactTextResult
serialize_stock_external_private_target_v2(
    const StockExternalPrivateTargetArtifactV2& artifact) noexcept;
[[nodiscard]] StockExternalArtifactResult<StockExternalPrivateTargetArtifactV2>
parse_stock_external_private_target_v2(std::string_view json) noexcept;

[[nodiscard]] StockExternalArtifactTextResult
serialize_stock_external_review_summary_v2(
    const StockExternalReviewSummaryArtifactV2& artifact) noexcept;
[[nodiscard]] StockExternalArtifactResult<StockExternalReviewSummaryArtifactV2>
parse_stock_external_review_summary_v2(std::string_view json) noexcept;

[[nodiscard]] StockExternalArtifactTextResult
serialize_stock_external_approval(
    const StockExternalApprovalArtifact& artifact) noexcept;
[[nodiscard]] StockExternalArtifactResult<StockExternalApprovalArtifact>
parse_stock_external_approval(std::string_view json) noexcept;

[[nodiscard]] StockExternalArtifactTextResult
read_stock_external_artifact_leaf(
    const std::filesystem::path& exact_review_directory,
    std::wstring_view exact_leaf_name) noexcept;

[[nodiscard]] std::optional<std::wstring>
stock_external_private_target_leaf(std::uint64_t ordinal) noexcept;

[[nodiscard]] StockExternalArtifactTextResult stock_external_artifact_sha256(
    std::string_view bytes) noexcept;

} // namespace hlclient::platform::windows
