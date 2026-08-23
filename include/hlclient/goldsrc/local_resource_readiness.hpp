#pragma once

#include <hlclient/goldsrc/local_resource_inventory.hpp>
#include <hlclient/goldsrc/server_info.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/local_resources/local_resource_locator.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

namespace detail {
class PrecacheManifestDefensiveTestAccess;
}

inline constexpr std::size_t kDefaultMaximumLocalResourceReadinessEntries =
    1'024U;
inline constexpr std::size_t kMaximumLocalResourceReadinessEntries = 4'095U;
inline constexpr std::size_t kDefaultMaximumLocatorVirtualNameBytes = 1'024U;
inline constexpr std::size_t kMaximumLocatorVirtualNameBytes = 1'024U;
inline constexpr std::size_t kLocalResourceReadinessStatusCount = 8U;
inline constexpr std::size_t kLocalResourceReadinessImpactCount = 6U;
inline constexpr std::size_t kLocalResourceReadinessDiagnosticTextLimit = 256U;

struct LocalResourceReadinessLimits {
    std::size_t maximum_entry_count{
        kDefaultMaximumLocalResourceReadinessEntries};
    std::size_t maximum_locator_virtual_name_bytes{
        kDefaultMaximumLocatorVirtualNameBytes};
};

[[nodiscard]] bool valid_local_resource_readiness_limits(
    const LocalResourceReadinessLimits& limits) noexcept;

enum class LocalResourceReadinessCompatibilityProfile {
    stock_protocol_48_standard_local_metadata,
};

enum class LocalResourceReadinessEvidenceProfile {
    exact_resource_list_inventory_and_server_info_correlation,
};

enum class LocalResourceReadinessStatus {
    ready_local_file,
    metadata_only,
    missing_local_file,
    unsafe_name,
    unsupported_name_encoding,
    unsupported_mapping,
    ambiguous_local_match,
    local_io_error,
};

enum class LocalResourceReadinessImpact {
    locally_usable,
    metadata_only,
    incomplete,
    security_blocked,
    unsupported_profile,
    local_io_failure,
};

enum class WorldResourceReadiness {
    ready,
    map_entry_missing_from_list,
    map_entry_duplicated,
    map_name_invalid,
    map_entry_not_model,
    local_map_missing,
    local_map_unsafe,
    local_map_ambiguous,
    local_map_io_error,
};

class LocalResourceReadinessEntry final {
public:
    LocalResourceReadinessEntry(const LocalResourceReadinessEntry&) = default;
    LocalResourceReadinessEntry(LocalResourceReadinessEntry&&) noexcept =
        default;
    LocalResourceReadinessEntry& operator=(
        const LocalResourceReadinessEntry&) = delete;
    LocalResourceReadinessEntry& operator=(
        LocalResourceReadinessEntry&&) noexcept = delete;
    ~LocalResourceReadinessEntry() = default;

    [[nodiscard]] std::size_t wire_ordinal() const noexcept;
    [[nodiscard]] ResourceType resource_type() const noexcept;
    [[nodiscard]] std::uint16_t resource_index() const noexcept;
    [[nodiscard]] std::size_t original_wire_name_byte_length() const noexcept;
    [[nodiscard]] LocalResourceReadinessStatus status() const noexcept;
    [[nodiscard]] LocalResourceReadinessImpact impact() const noexcept;
    [[nodiscard]] const std::optional<local_resources::LocalResourceLocator>&
    locator() const noexcept;
    [[nodiscard]] std::optional<local_resources::LocalResourceRootId>
    source_root_id() const noexcept;
    [[nodiscard]] std::optional<local_resources::LocalResourceRootKind>
    source_root_kind() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> local_file_size() const noexcept;
    [[nodiscard]] std::optional<local_resources::LocalStableFileIdentity>
    stable_identity() const noexcept;
    [[nodiscard]] LocalResourceReadinessCompatibilityProfile
    compatibility_profile() const noexcept;
    [[nodiscard]] LocalResourceReadinessEvidenceProfile evidence_profile()
        const noexcept;

private:
    friend class LocalResourceReadinessBuilder;
    friend class detail::PrecacheManifestDefensiveTestAccess;

    LocalResourceReadinessEntry(
        std::size_t wire_ordinal,
        ResourceType resource_type,
        std::uint16_t resource_index,
        std::size_t original_wire_name_byte_length,
        LocalResourceReadinessStatus status,
        LocalResourceReadinessImpact impact,
        std::optional<local_resources::LocalResourceLocator> locator,
        std::optional<local_resources::LocalResourceRootKind> source_root_kind)
        noexcept;

    std::size_t wire_ordinal_{0U};
    ResourceType resource_type_{ResourceType::sound};
    std::uint16_t resource_index_{0U};
    std::size_t original_wire_name_byte_length_{0U};
    LocalResourceReadinessStatus status_{
        LocalResourceReadinessStatus::local_io_error};
    LocalResourceReadinessImpact impact_{
        LocalResourceReadinessImpact::local_io_failure};
    std::optional<local_resources::LocalResourceLocator> locator_;
    std::optional<local_resources::LocalResourceRootKind> source_root_kind_;
};

class LocalResourceReadinessSummary final {
public:
    [[nodiscard]] std::size_t total_entry_count() const noexcept;
    [[nodiscard]] std::size_t count(
        LocalResourceReadinessStatus status) const noexcept;
    [[nodiscard]] std::size_t count(
        LocalResourceReadinessImpact impact) const noexcept;
    [[nodiscard]] std::size_t resolved_mapped_file_count() const noexcept;
    [[nodiscard]] std::size_t metadata_only_count() const noexcept;
    [[nodiscard]] std::size_t missing_count() const noexcept;
    [[nodiscard]] std::size_t security_blocked_count() const noexcept;
    [[nodiscard]] std::size_t unsupported_count() const noexcept;
    [[nodiscard]] std::size_t ambiguous_count() const noexcept;
    [[nodiscard]] std::size_t io_failure_count() const noexcept;

private:
    friend class LocalResourceReadinessBuilder;

    LocalResourceReadinessSummary(
        std::array<std::size_t, kLocalResourceReadinessStatusCount>
            status_counts,
        std::array<std::size_t, kLocalResourceReadinessImpactCount>
            impact_counts) noexcept;

    std::array<std::size_t, kLocalResourceReadinessStatusCount>
        status_counts_{};
    std::array<std::size_t, kLocalResourceReadinessImpactCount>
        impact_counts_{};
    std::size_t total_entry_count_{0U};
};

// The selected entry is addressed by its offset in the immutable ordered
// readiness/manifest vector. No pointer is retained across copies.
class WorldResourceSelection final {
public:
    WorldResourceSelection(const WorldResourceSelection&) = default;
    WorldResourceSelection(WorldResourceSelection&&) noexcept = default;
    WorldResourceSelection& operator=(const WorldResourceSelection&) = delete;
    WorldResourceSelection& operator=(WorldResourceSelection&&) noexcept =
        delete;
    ~WorldResourceSelection() = default;

    [[nodiscard]] WorldResourceReadiness status() const noexcept;
    [[nodiscard]] std::size_t server_map_name_byte_length() const noexcept;
    [[nodiscard]] std::optional<std::size_t> entry_offset() const noexcept;
    [[nodiscard]] std::optional<std::size_t> wire_ordinal() const noexcept;
    [[nodiscard]] std::optional<std::uint16_t> resource_index() const noexcept;
    [[nodiscard]] const std::optional<local_resources::LocalResourceLocator>&
    locator() const noexcept;
    [[nodiscard]] bool world_geometry_ready() const noexcept;
    [[nodiscard]] LocalResourceReadinessEvidenceProfile evidence_profile()
        const noexcept;

private:
    friend class LocalResourceReadinessBuilder;

    WorldResourceSelection(
        WorldResourceReadiness status,
        std::size_t server_map_name_byte_length,
        std::optional<std::size_t> entry_offset,
        std::optional<std::size_t> wire_ordinal,
        std::optional<std::uint16_t> resource_index,
        std::optional<local_resources::LocalResourceLocator> locator) noexcept;

    WorldResourceReadiness status_{
        WorldResourceReadiness::map_entry_missing_from_list};
    std::size_t server_map_name_byte_length_{0U};
    std::optional<std::size_t> entry_offset_;
    std::optional<std::size_t> wire_ordinal_;
    std::optional<std::uint16_t> resource_index_;
    std::optional<local_resources::LocalResourceLocator> locator_;
};

class LocalResourceReadinessState final {
public:
    LocalResourceReadinessState(const LocalResourceReadinessState&) = default;
    LocalResourceReadinessState(LocalResourceReadinessState&&) noexcept =
        default;
    LocalResourceReadinessState& operator=(
        const LocalResourceReadinessState&) = delete;
    LocalResourceReadinessState& operator=(
        LocalResourceReadinessState&&) noexcept = delete;
    ~LocalResourceReadinessState() = default;

    [[nodiscard]] std::span<const LocalResourceReadinessEntry> entries()
        const noexcept;
    [[nodiscard]] std::size_t entry_count() const noexcept;
    [[nodiscard]] const LocalResourceReadinessSummary& summary() const noexcept;
    [[nodiscard]] const LocalResourceReadinessEntry* find_exact(
        ResourceType type,
        std::uint16_t resource_index) const noexcept;
    [[nodiscard]] const WorldResourceSelection& world_selection()
        const noexcept;
    [[nodiscard]] bool complete_for_supported_local_profile() const noexcept;
    [[nodiscard]] bool world_geometry_candidate_available() const noexcept;
    [[nodiscard]] bool world_geometry_ready() const noexcept;
    [[nodiscard]] LocalResourceReadinessCompatibilityProfile
    compatibility_profile() const noexcept;
    [[nodiscard]] LocalResourceReadinessEvidenceProfile evidence_profile()
        const noexcept;

private:
    friend class LocalResourceReadinessBuilder;
    friend class detail::PrecacheManifestDefensiveTestAccess;

    LocalResourceReadinessState(
        std::vector<LocalResourceReadinessEntry> entries,
        LocalResourceReadinessSummary summary,
        WorldResourceSelection world_selection,
        bool complete_for_supported_local_profile) noexcept;

    std::vector<LocalResourceReadinessEntry> entries_;
    LocalResourceReadinessSummary summary_;
    WorldResourceSelection world_selection_;
    bool complete_for_supported_local_profile_{false};
};

enum class LocalResourceReadinessErrorCode {
    invalid_configuration,
    entry_count_limit_exceeded,
    entry_count_mismatch,
    inventory_summary_mismatch,
    wire_ordinal_mismatch,
    resource_type_mismatch,
    resource_index_mismatch,
    wire_name_length_mismatch,
    classification_status_mismatch,
    invalid_virtual_path_invariant,
    virtual_path_id_mismatch,
    virtual_path_length_mismatch,
    virtual_path_component_count_mismatch,
    invalid_resolved_metadata,
    locator_creation_failed,
    invalid_server_map_name,
    map_entry_missing_from_list,
    map_entry_duplicated,
    map_entry_not_model,
    unable_to_retain_readiness,
};

struct LocalResourceReadinessError {
    LocalResourceReadinessErrorCode code{
        LocalResourceReadinessErrorCode::invalid_configuration};
    std::optional<std::size_t> entry_ordinal;
    std::optional<WorldResourceReadiness> world_status;
    std::string context;
};

struct LocalResourceReadinessBuildResult {
    std::optional<LocalResourceReadinessState> state;
    std::optional<LocalResourceReadinessError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

// Transactional list/inventory correlation, readiness classification, locator
// construction, and exact ServerInfo world selection. Structural mismatches
// publish no state. A unique model entry whose local file is missing remains a
// publishable incomplete snapshot.
class LocalResourceReadinessBuilder final {
public:
    explicit LocalResourceReadinessBuilder(
        LocalResourceReadinessLimits limits = {}) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const LocalResourceReadinessLimits& limits() const noexcept;
    [[nodiscard]] LocalResourceReadinessBuildResult build(
        const ResourceListState& resource_list,
        const LocalResourceInventoryState& inventory,
        const ServerInfoState& server_info,
        const GoldSrcResourceNameMapper& mapper,
        const local_resources::LocalResourceEnvironment& environment) const;

private:
    LocalResourceReadinessLimits limits_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const LocalResourceReadinessStatus status) noexcept
{
    switch (status) {
    case LocalResourceReadinessStatus::ready_local_file:
        return "ready_local_file";
    case LocalResourceReadinessStatus::metadata_only: return "metadata_only";
    case LocalResourceReadinessStatus::missing_local_file:
        return "missing_local_file";
    case LocalResourceReadinessStatus::unsafe_name: return "unsafe_name";
    case LocalResourceReadinessStatus::unsupported_name_encoding:
        return "unsupported_name_encoding";
    case LocalResourceReadinessStatus::unsupported_mapping:
        return "unsupported_mapping";
    case LocalResourceReadinessStatus::ambiguous_local_match:
        return "ambiguous_local_match";
    case LocalResourceReadinessStatus::local_io_error: return "local_io_error";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const LocalResourceReadinessImpact impact) noexcept
{
    switch (impact) {
    case LocalResourceReadinessImpact::locally_usable:
        return "locally_usable";
    case LocalResourceReadinessImpact::metadata_only: return "metadata_only";
    case LocalResourceReadinessImpact::incomplete: return "incomplete";
    case LocalResourceReadinessImpact::security_blocked:
        return "security_blocked";
    case LocalResourceReadinessImpact::unsupported_profile:
        return "unsupported_profile";
    case LocalResourceReadinessImpact::local_io_failure:
        return "local_io_failure";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const WorldResourceReadiness status) noexcept
{
    switch (status) {
    case WorldResourceReadiness::ready: return "ready";
    case WorldResourceReadiness::map_entry_missing_from_list:
        return "map_entry_missing_from_list";
    case WorldResourceReadiness::map_entry_duplicated:
        return "map_entry_duplicated";
    case WorldResourceReadiness::map_name_invalid: return "map_name_invalid";
    case WorldResourceReadiness::map_entry_not_model:
        return "map_entry_not_model";
    case WorldResourceReadiness::local_map_missing:
        return "local_map_missing";
    case WorldResourceReadiness::local_map_unsafe: return "local_map_unsafe";
    case WorldResourceReadiness::local_map_ambiguous:
        return "local_map_ambiguous";
    case WorldResourceReadiness::local_map_io_error:
        return "local_map_io_error";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const LocalResourceReadinessErrorCode code) noexcept
{
    switch (code) {
    case LocalResourceReadinessErrorCode::invalid_configuration:
        return "invalid_configuration";
    case LocalResourceReadinessErrorCode::entry_count_limit_exceeded:
        return "entry_count_limit_exceeded";
    case LocalResourceReadinessErrorCode::entry_count_mismatch:
        return "entry_count_mismatch";
    case LocalResourceReadinessErrorCode::inventory_summary_mismatch:
        return "inventory_summary_mismatch";
    case LocalResourceReadinessErrorCode::wire_ordinal_mismatch:
        return "wire_ordinal_mismatch";
    case LocalResourceReadinessErrorCode::resource_type_mismatch:
        return "resource_type_mismatch";
    case LocalResourceReadinessErrorCode::resource_index_mismatch:
        return "resource_index_mismatch";
    case LocalResourceReadinessErrorCode::wire_name_length_mismatch:
        return "wire_name_length_mismatch";
    case LocalResourceReadinessErrorCode::classification_status_mismatch:
        return "classification_status_mismatch";
    case LocalResourceReadinessErrorCode::invalid_virtual_path_invariant:
        return "invalid_virtual_path_invariant";
    case LocalResourceReadinessErrorCode::virtual_path_id_mismatch:
        return "virtual_path_id_mismatch";
    case LocalResourceReadinessErrorCode::virtual_path_length_mismatch:
        return "virtual_path_length_mismatch";
    case LocalResourceReadinessErrorCode::virtual_path_component_count_mismatch:
        return "virtual_path_component_count_mismatch";
    case LocalResourceReadinessErrorCode::invalid_resolved_metadata:
        return "invalid_resolved_metadata";
    case LocalResourceReadinessErrorCode::locator_creation_failed:
        return "locator_creation_failed";
    case LocalResourceReadinessErrorCode::invalid_server_map_name:
        return "invalid_server_map_name";
    case LocalResourceReadinessErrorCode::map_entry_missing_from_list:
        return "map_entry_missing_from_list";
    case LocalResourceReadinessErrorCode::map_entry_duplicated:
        return "map_entry_duplicated";
    case LocalResourceReadinessErrorCode::map_entry_not_model:
        return "map_entry_not_model";
    case LocalResourceReadinessErrorCode::unable_to_retain_readiness:
        return "unable_to_retain_readiness";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
