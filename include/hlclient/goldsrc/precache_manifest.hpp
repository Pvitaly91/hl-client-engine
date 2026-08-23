#pragma once

#include <hlclient/goldsrc/local_resource_readiness.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

// Project safety limits, not stock-engine maxima.
inline constexpr std::size_t kDefaultMaximumPrecacheManifestEntries = 1'024U;
inline constexpr std::size_t kMaximumPrecacheManifestEntries = 4'095U;
inline constexpr std::size_t kDefaultMaximumPrecacheSlotsPerType = 4'096U;
inline constexpr std::size_t kMaximumPrecacheSlotsPerType = 4'096U;
inline constexpr std::size_t kDefaultMaximumPrecacheManifestTotalSlots =
    20'480U;
inline constexpr std::size_t kMaximumPrecacheManifestTotalSlots = 20'480U;
inline constexpr std::size_t kDefaultMaximumPrecacheManifestEvents = 2'048U;
inline constexpr std::size_t kMaximumPrecacheManifestEvents = 8'192U;
inline constexpr std::size_t kPrecacheManifestDiagnosticTextLimit = 256U;

struct PrecacheManifestLimits {
    std::size_t maximum_readiness_entries{
        kDefaultMaximumLocalResourceReadinessEntries};
    std::size_t maximum_manifest_entries{
        kDefaultMaximumPrecacheManifestEntries};
    std::size_t maximum_slots_per_type{
        kDefaultMaximumPrecacheSlotsPerType};
    std::size_t maximum_total_slots{
        kDefaultMaximumPrecacheManifestTotalSlots};
    std::size_t maximum_manifest_events{
        kDefaultMaximumPrecacheManifestEvents};
    std::size_t maximum_locator_virtual_name_bytes{
        kDefaultMaximumLocatorVirtualNameBytes};
};

[[nodiscard]] bool valid_precache_manifest_limits(
    const PrecacheManifestLimits& limits) noexcept;

enum class PrecacheManifestCompatibilityProfile {
    stock_protocol_48_standard_metadata_only,
};

enum class PrecacheManifestEvidenceProfile {
    exact_correlated_local_resource_metadata,
};

enum class PrecacheManifestCompleteness {
    complete_for_supported_local_profile,
    world_ready_but_incomplete,
    incomplete_missing_resources,
    blocked_unsafe_resources,
    unsupported_profile,
    local_io_failure,
    invalid_server_resource_correlation,
};

class PrecacheManifestEntry final {
public:
    PrecacheManifestEntry(const PrecacheManifestEntry&) = default;
    PrecacheManifestEntry(PrecacheManifestEntry&&) noexcept = default;
    PrecacheManifestEntry& operator=(const PrecacheManifestEntry&) = delete;
    PrecacheManifestEntry& operator=(PrecacheManifestEntry&&) noexcept = delete;
    ~PrecacheManifestEntry() = default;

    [[nodiscard]] std::size_t wire_ordinal() const noexcept;
    [[nodiscard]] ResourceType resource_type() const noexcept;
    [[nodiscard]] std::uint16_t resource_index() const noexcept;
    [[nodiscard]] LocalResourceReadinessStatus readiness_status()
        const noexcept;
    [[nodiscard]] LocalResourceReadinessImpact readiness_impact()
        const noexcept;
    [[nodiscard]] const std::optional<local_resources::LocalResourceLocator>&
    locator() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> local_file_size() const noexcept;
    [[nodiscard]] std::uint16_t type_local_slot() const noexcept;
    [[nodiscard]] PrecacheManifestCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] PrecacheManifestEvidenceProfile evidence_profile()
        const noexcept;

private:
    friend class PrecacheManifestBuilder;

    PrecacheManifestEntry(
        std::size_t wire_ordinal,
        ResourceType resource_type,
        std::uint16_t resource_index,
        LocalResourceReadinessStatus readiness_status,
        LocalResourceReadinessImpact readiness_impact,
        std::optional<local_resources::LocalResourceLocator> locator) noexcept;

    std::size_t wire_ordinal_{0U};
    ResourceType resource_type_{ResourceType::sound};
    std::uint16_t resource_index_{0U};
    LocalResourceReadinessStatus readiness_status_{
        LocalResourceReadinessStatus::local_io_error};
    LocalResourceReadinessImpact readiness_impact_{
        LocalResourceReadinessImpact::local_io_failure};
    std::optional<local_resources::LocalResourceLocator> locator_;
};

// Sparse type-local table. Occupied slots contain offsets into the owning
// PrecacheManifestState ordered-entry vector, never pointers or entry copies.
class PrecacheSlotTable final {
public:
    PrecacheSlotTable(const PrecacheSlotTable&) = default;
    PrecacheSlotTable(PrecacheSlotTable&&) noexcept = default;
    PrecacheSlotTable& operator=(const PrecacheSlotTable&) = delete;
    PrecacheSlotTable& operator=(PrecacheSlotTable&&) noexcept = delete;
    ~PrecacheSlotTable() = default;

    [[nodiscard]] ResourceType resource_type() const noexcept;
    [[nodiscard]] std::size_t slot_count() const noexcept;
    [[nodiscard]] std::size_t occupied_slot_count() const noexcept;
    [[nodiscard]] std::optional<std::size_t> entry_offset(
        std::uint16_t slot) const noexcept;
    [[nodiscard]] std::span<const std::optional<std::size_t>> slots()
        const noexcept;

private:
    friend class PrecacheManifestBuilder;

    PrecacheSlotTable(
        ResourceType resource_type,
        std::vector<std::optional<std::size_t>> slots) noexcept;

    ResourceType resource_type_{ResourceType::sound};
    std::vector<std::optional<std::size_t>> slots_;
    std::size_t occupied_slot_count_{0U};
};

struct PrecacheManifestSourceGeometry {
    std::size_t resource_count{0U};
    std::size_t total_name_byte_count{0U};
    ResourceListCompatibilityProfile resource_list_profile{
        ResourceListCompatibilityProfile::
            stock_protocol_48_build_10210_standard};
    ServerInfoCompatibilityProfile server_info_profile{
        ServerInfoCompatibilityProfile::
            valve_half_life_protocol_48_build_10210};
};

class PrecacheManifestState final {
public:
    PrecacheManifestState(const PrecacheManifestState&) = default;
    PrecacheManifestState(PrecacheManifestState&&) noexcept = default;
    PrecacheManifestState& operator=(const PrecacheManifestState&) = delete;
    PrecacheManifestState& operator=(PrecacheManifestState&&) noexcept = delete;
    ~PrecacheManifestState() = default;

    [[nodiscard]] std::span<const PrecacheManifestEntry> entries()
        const noexcept;
    [[nodiscard]] std::size_t entry_count() const noexcept;
    [[nodiscard]] const PrecacheManifestEntry* find(
        ResourceType type,
        std::uint16_t resource_index) const noexcept;
    [[nodiscard]] const PrecacheSlotTable& sound_slots() const noexcept;
    [[nodiscard]] const PrecacheSlotTable& model_slots() const noexcept;
    [[nodiscard]] const PrecacheSlotTable& generic_slots() const noexcept;
    [[nodiscard]] const PrecacheSlotTable& event_script_slots() const noexcept;
    [[nodiscard]] const PrecacheSlotTable& decal_slots() const noexcept;
    [[nodiscard]] const WorldResourceSelection& world_selection()
        const noexcept;
    [[nodiscard]] const PrecacheManifestEntry* world_entry() const noexcept;
    [[nodiscard]] const LocalResourceReadinessSummary& readiness_summary()
        const noexcept;
    [[nodiscard]] PrecacheManifestCompleteness completeness() const noexcept;
    [[nodiscard]] bool complete_for_supported_local_profile() const noexcept;
    [[nodiscard]] bool world_geometry_ready() const noexcept;
    [[nodiscard]] const PrecacheManifestSourceGeometry& source_geometry()
        const noexcept;
    [[nodiscard]] PrecacheManifestCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] PrecacheManifestEvidenceProfile evidence_profile()
        const noexcept;

private:
    friend class PrecacheManifestBuilder;

    PrecacheManifestState(
        std::vector<PrecacheManifestEntry> entries,
        PrecacheSlotTable sound_slots,
        PrecacheSlotTable model_slots,
        PrecacheSlotTable generic_slots,
        PrecacheSlotTable event_script_slots,
        PrecacheSlotTable decal_slots,
        WorldResourceSelection world_selection,
        LocalResourceReadinessSummary readiness_summary,
        PrecacheManifestCompleteness completeness,
        PrecacheManifestSourceGeometry source_geometry) noexcept;

    std::vector<PrecacheManifestEntry> entries_;
    PrecacheSlotTable sound_slots_;
    PrecacheSlotTable model_slots_;
    PrecacheSlotTable generic_slots_;
    PrecacheSlotTable event_script_slots_;
    PrecacheSlotTable decal_slots_;
    WorldResourceSelection world_selection_;
    LocalResourceReadinessSummary readiness_summary_;
    PrecacheManifestCompleteness completeness_{
        PrecacheManifestCompleteness::invalid_server_resource_correlation};
    PrecacheManifestSourceGeometry source_geometry_;
};

enum class PrecacheManifestErrorCode {
    invalid_configuration,
    readiness_build_failed,
    manifest_entry_limit_exceeded,
    resource_index_out_of_bounds,
    duplicate_type_local_slot,
    slots_per_type_limit_exceeded,
    total_slot_limit_exceeded,
    world_selection_invariant_failed,
    unable_to_retain_manifest,
};

struct PrecacheManifestError {
    PrecacheManifestErrorCode code{
        PrecacheManifestErrorCode::invalid_configuration};
    std::optional<LocalResourceReadinessErrorCode> readiness_code;
    std::optional<WorldResourceReadiness> world_status;
    std::optional<std::size_t> entry_ordinal;
    std::string context;
};

struct PrecacheManifestBuildResult {
    std::optional<PrecacheManifestState> state;
    std::optional<PrecacheManifestError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

class PrecacheManifestBuilder final {
public:
    explicit PrecacheManifestBuilder(
        PrecacheManifestLimits limits = {}) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const PrecacheManifestLimits& limits() const noexcept;
    [[nodiscard]] PrecacheManifestBuildResult build(
        const ResourceListState& resource_list,
        const LocalResourceInventoryState& inventory,
        const ServerInfoState& server_info,
        const GoldSrcResourceNameMapper& mapper,
        const local_resources::LocalResourceEnvironment& environment) const;

private:
    friend class detail::PrecacheManifestDefensiveTestAccess;

    [[nodiscard]] PrecacheManifestBuildResult build_from_readiness(
        const LocalResourceReadinessState& readiness,
        PrecacheManifestSourceGeometry source_geometry) const;

    PrecacheManifestLimits limits_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const PrecacheManifestCompleteness status) noexcept
{
    switch (status) {
    case PrecacheManifestCompleteness::complete_for_supported_local_profile:
        return "complete_for_supported_local_profile";
    case PrecacheManifestCompleteness::world_ready_but_incomplete:
        return "world_ready_but_incomplete";
    case PrecacheManifestCompleteness::incomplete_missing_resources:
        return "incomplete_missing_resources";
    case PrecacheManifestCompleteness::blocked_unsafe_resources:
        return "blocked_unsafe_resources";
    case PrecacheManifestCompleteness::unsupported_profile:
        return "unsupported_profile";
    case PrecacheManifestCompleteness::local_io_failure:
        return "local_io_failure";
    case PrecacheManifestCompleteness::invalid_server_resource_correlation:
        return "invalid_server_resource_correlation";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const PrecacheManifestErrorCode code) noexcept
{
    switch (code) {
    case PrecacheManifestErrorCode::invalid_configuration:
        return "invalid_configuration";
    case PrecacheManifestErrorCode::readiness_build_failed:
        return "readiness_build_failed";
    case PrecacheManifestErrorCode::manifest_entry_limit_exceeded:
        return "manifest_entry_limit_exceeded";
    case PrecacheManifestErrorCode::resource_index_out_of_bounds:
        return "resource_index_out_of_bounds";
    case PrecacheManifestErrorCode::duplicate_type_local_slot:
        return "duplicate_type_local_slot";
    case PrecacheManifestErrorCode::slots_per_type_limit_exceeded:
        return "slots_per_type_limit_exceeded";
    case PrecacheManifestErrorCode::total_slot_limit_exceeded:
        return "total_slot_limit_exceeded";
    case PrecacheManifestErrorCode::world_selection_invariant_failed:
        return "world_selection_invariant_failed";
    case PrecacheManifestErrorCode::unable_to_retain_manifest:
        return "unable_to_retain_manifest";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
