#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::platform::windows {

inline constexpr std::string_view kStockResearchPreparationSchemaV2 =
    "hlclient.stock-runtime-research-preparation.v2";
inline constexpr std::string_view kStockResearchIsolationMarkerV1 =
    "HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1";

struct StockResearchCopyLimits final {
    std::size_t maximum_entries{200'000U};
    std::uint64_t maximum_total_bytes{16ULL * 1'024ULL * 1'024ULL * 1'024ULL};
    std::uint64_t maximum_file_bytes{8ULL * 1'024ULL * 1'024ULL * 1'024ULL};
    std::size_t maximum_reparse_depth{32U};
    std::size_t maximum_path_characters{32'767U};
    std::size_t maximum_streams_per_file{32U};
};

enum class StockResearchTopologyCategory {
    ordinary_tree,
    source_path_ancestor_reparse,
    source_root_reparse,
    source_internal_directory_junction,
    source_internal_directory_symlink,
    source_internal_file_symlink,
    source_internal_mount_point,
    source_file_hardlink,
    source_alternate_data_stream,
    source_subst_drive,
    source_unc_path,
    source_remote_volume,
    source_unsupported_reparse_tag,
    source_link_target_outside_root,
    source_link_cycle,
    source_link_depth_exceeded,
    source_entry_limit_exceeded,
    source_byte_limit_exceeded,
};

[[nodiscard]] std::string_view to_string(
    StockResearchTopologyCategory category) noexcept;

enum class StockResearchCopyErrorCode {
    none,
    invalid_argument,
    source_not_absolute,
    source_not_found,
    source_not_directory,
    source_open_failed,
    source_identity_query_failed,
    source_final_path_failed,
    source_not_local_fixed_volume,
    source_required_launcher_missing,
    source_already_prepared,
    source_topology_unsafe,
    source_changed_during_materialization,
    source_read_failed,
    source_digest_failed,
    destination_not_absolute,
    destination_exists,
    destination_parent_invalid,
    destination_parent_reparse,
    destination_not_local_fixed_volume,
    destination_subst_drive,
    destination_overlaps_source,
    destination_overlaps_steam_library,
    destination_create_failed,
    destination_write_failed,
    destination_flush_failed,
    destination_publish_failed,
    destination_identity_invalid,
    destination_inventory_mismatch,
    manifest_write_failed,
    cleanup_failed,
    enumeration_failed,
    native_api_unavailable,
};

[[nodiscard]] std::string_view to_string(
    StockResearchCopyErrorCode code) noexcept;

struct StockResearchTopologySummary final {
    std::vector<StockResearchTopologyCategory> categories;
    bool inspection_complete{false};
    bool safe_to_materialize{false};
    bool root_reparse{false};
    std::size_t internal_reparse_count{0U};
    std::size_t hardlink_count{0U};
    std::size_t alternate_data_stream_count{0U};
    std::size_t contained_target_count{0U};
    std::size_t escaped_target_count{0U};
    std::size_t entry_count{0U};
    std::uint64_t byte_count{0U};
};

struct StockResearchTopologyResult final {
    std::optional<StockResearchTopologySummary> summary;
    StockResearchCopyErrorCode code{StockResearchCopyErrorCode::none};
    std::uint32_t native_error{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return summary.has_value() &&
               code == StockResearchCopyErrorCode::none;
    }
};

enum class StockResearchCopyProgressPhase {
    source_file_opened,
    destination_file_flushed,
    before_source_reinventory,
    after_staging_identity_acquired,
    before_destination_publish,
    after_destination_publish,
    during_published_inventory,
    after_published_file_guards_acquired,
    before_commit_marker_publish,
    after_published_file_guards_released_on_failure,
};

using StockResearchCopyProgressHook = void (*)(
    StockResearchCopyProgressPhase phase,
    std::size_t file_ordinal,
    void* context);

enum class StockResearchMarkerValidationPhase {
    after_handle_acquired,
};

using StockResearchMarkerValidationHook = void (*)(
    StockResearchMarkerValidationPhase phase,
    void* context);

struct StockResearchMarkerValidationOptions final {
    StockResearchMarkerValidationHook progress_hook{nullptr};
    void* progress_context{nullptr};
};

struct StockResearchCopyOptions final {
    StockResearchCopyLimits limits{};

    // Additional configured Steam library roots used by deterministic tests or
    // an embedding host. Production validation always includes registry and
    // bounded libraryfolders.vdf discovery as well.
    std::vector<std::filesystem::path> configured_steam_library_roots;

    // Deterministic fixture hook. Production callers leave this null. The
    // callback receives no source or destination path and is never used by the
    // command-line tool.
    StockResearchCopyProgressHook progress_hook{nullptr};
    void* progress_context{nullptr};
};

struct StockResearchMaterialization final {
    StockResearchTopologySummary topology;
    std::size_t materialized_link_count{0U};
    std::size_t materialized_hardlink_count{0U};
    std::size_t rejected_link_count{0U};
    std::size_t destination_reparse_count{0U};
    std::size_t destination_hardlink_count{0U};
    std::size_t destination_alternate_data_stream_count{0U};
    std::size_t entry_count{0U};
    std::uint64_t byte_count{0U};
    bool source_unchanged{false};
    bool destination_unlinked{false};
    std::string source_root_identity_fingerprint;
    std::string inventory_sha256;
    std::string client_binary_private_identity_reference;
    std::string server_binary_private_identity_reference;
    std::string preparation_status;
};

struct StockResearchMaterializationResult final {
    std::optional<StockResearchMaterialization> materialization;
    StockResearchCopyErrorCode code{StockResearchCopyErrorCode::none};
    std::uint32_t native_error{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return materialization.has_value() &&
               code == StockResearchCopyErrorCode::none;
    }
};

// Read-only, bounded and handle-based. Unsafe topology is a successfully
// completed diagnostic: the returned summary is present, safe_to_materialize
// is false, and code remains none.
[[nodiscard]] StockResearchTopologyResult inspect_stock_research_topology(
    const std::filesystem::path& source_root,
    const StockResearchCopyLimits& limits = {}) noexcept;

// Creates a brand-new ordinary destination tree. Reparse topology is followed
// only for explicitly supported, physically contained directory targets.
// Source files are copied from verified retained handles; publication is
// transactional and never replaces an existing destination.
[[nodiscard]] StockResearchMaterializationResult
materialize_stock_research_copy(
    const std::filesystem::path& source_root,
    const std::filesystem::path& destination_root,
    const StockResearchCopyOptions& options = {}) noexcept;

// Retained-handle authorization check used by the active orchestrator. The
// marker must be an exact, bounded, ordinary, unlinked, no-ADS file reached
// without a reparse/path alias.
[[nodiscard]] bool stock_research_isolation_marker_exact(
    const std::filesystem::path& research_root,
    const StockResearchMarkerValidationOptions& options = {}) noexcept;

} // namespace hlclient::platform::windows
