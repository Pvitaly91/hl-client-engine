#include <hlclient/goldsrc/local_resource_readiness.hpp>

#include <algorithm>
#include <array>
#include <new>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hlclient::goldsrc {
namespace {

namespace local = local_resources;

[[nodiscard]] constexpr std::optional<std::size_t> status_index(
    const LocalResourceReadinessStatus status) noexcept
{
    switch (status) {
    case LocalResourceReadinessStatus::ready_local_file: return 0U;
    case LocalResourceReadinessStatus::metadata_only: return 1U;
    case LocalResourceReadinessStatus::missing_local_file: return 2U;
    case LocalResourceReadinessStatus::unsafe_name: return 3U;
    case LocalResourceReadinessStatus::unsupported_name_encoding: return 4U;
    case LocalResourceReadinessStatus::unsupported_mapping: return 5U;
    case LocalResourceReadinessStatus::ambiguous_local_match: return 6U;
    case LocalResourceReadinessStatus::local_io_error: return 7U;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr std::optional<std::size_t> impact_index(
    const LocalResourceReadinessImpact impact) noexcept
{
    switch (impact) {
    case LocalResourceReadinessImpact::locally_usable: return 0U;
    case LocalResourceReadinessImpact::metadata_only: return 1U;
    case LocalResourceReadinessImpact::incomplete: return 2U;
    case LocalResourceReadinessImpact::security_blocked: return 3U;
    case LocalResourceReadinessImpact::unsupported_profile: return 4U;
    case LocalResourceReadinessImpact::local_io_failure: return 5U;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr std::optional<std::size_t> inventory_status_index(
    const LocalResourceInventoryStatus status) noexcept
{
    switch (status) {
    case LocalResourceInventoryStatus::resolved: return 0U;
    case LocalResourceInventoryStatus::missing: return 1U;
    case LocalResourceInventoryStatus::unsafe_name: return 2U;
    case LocalResourceInventoryStatus::unsupported_name_encoding: return 3U;
    case LocalResourceInventoryStatus::unsupported_mapping: return 4U;
    case LocalResourceInventoryStatus::ambiguous: return 5U;
    case LocalResourceInventoryStatus::io_error: return 6U;
    }
    return std::nullopt;
}

[[nodiscard]] LocalResourceReadinessBuildResult failure(
    const LocalResourceReadinessErrorCode code,
    const std::string_view context,
    const std::optional<std::size_t> entry_ordinal = std::nullopt,
    const std::optional<WorldResourceReadiness> world_status = std::nullopt)
{
    const auto size = (std::min)(
        context.size(), kLocalResourceReadinessDiagnosticTextLimit);
    return LocalResourceReadinessBuildResult{
        std::nullopt,
        LocalResourceReadinessError{
            code,
            entry_ordinal,
            world_status,
            std::string{context.data(), size},
        },
    };
}

[[nodiscard]] bool valid_mapper(
    const GoldSrcResourceNameMapper& mapper) noexcept
{
    return mapper.profile() ==
               GoldSrcLocalResourceMappingProfile::stock_protocol_48_standard &&
           valid_goldsrc_resource_name_limits(mapper.limits());
}

[[nodiscard]] constexpr LocalResourceReadinessStatus readiness_status(
    const LocalResourceInventoryStatus status) noexcept
{
    switch (status) {
    case LocalResourceInventoryStatus::resolved:
        return LocalResourceReadinessStatus::ready_local_file;
    case LocalResourceInventoryStatus::missing:
        return LocalResourceReadinessStatus::missing_local_file;
    case LocalResourceInventoryStatus::unsafe_name:
        return LocalResourceReadinessStatus::unsafe_name;
    case LocalResourceInventoryStatus::unsupported_name_encoding:
        return LocalResourceReadinessStatus::unsupported_name_encoding;
    case LocalResourceInventoryStatus::unsupported_mapping:
        return LocalResourceReadinessStatus::unsupported_mapping;
    case LocalResourceInventoryStatus::ambiguous:
        return LocalResourceReadinessStatus::ambiguous_local_match;
    case LocalResourceInventoryStatus::io_error:
        return LocalResourceReadinessStatus::local_io_error;
    }
    return LocalResourceReadinessStatus::local_io_error;
}

[[nodiscard]] constexpr LocalResourceReadinessImpact readiness_impact(
    const LocalResourceReadinessStatus status) noexcept
{
    switch (status) {
    case LocalResourceReadinessStatus::ready_local_file:
        return LocalResourceReadinessImpact::locally_usable;
    case LocalResourceReadinessStatus::metadata_only:
        return LocalResourceReadinessImpact::metadata_only;
    case LocalResourceReadinessStatus::missing_local_file:
        return LocalResourceReadinessImpact::incomplete;
    case LocalResourceReadinessStatus::unsafe_name:
        return LocalResourceReadinessImpact::security_blocked;
    case LocalResourceReadinessStatus::unsupported_name_encoding:
    case LocalResourceReadinessStatus::unsupported_mapping:
        return LocalResourceReadinessImpact::unsupported_profile;
    case LocalResourceReadinessStatus::ambiguous_local_match:
    case LocalResourceReadinessStatus::local_io_error:
        return LocalResourceReadinessImpact::local_io_failure;
    }
    return LocalResourceReadinessImpact::local_io_failure;
}

[[nodiscard]] bool classification_status_consistent(
    const GoldSrcResourceNameClassificationKind kind,
    const LocalResourceInventoryStatus status) noexcept
{
    switch (kind) {
    case GoldSrcResourceNameClassificationKind::mapped_file:
        return status == LocalResourceInventoryStatus::resolved ||
               status == LocalResourceInventoryStatus::missing ||
               status == LocalResourceInventoryStatus::ambiguous ||
               status == LocalResourceInventoryStatus::io_error;
    case GoldSrcResourceNameClassificationKind::metadata_only:
        // M3.2.1 inventories deliberately represented metadata-only decals as
        // unsupported_mapping; M3.2.2 lifts that exact correlated pair into a
        // distinct readiness status without changing the old snapshot.
        return status == LocalResourceInventoryStatus::unsupported_mapping;
    case GoldSrcResourceNameClassificationKind::unsafe_name:
        return status == LocalResourceInventoryStatus::unsafe_name;
    case GoldSrcResourceNameClassificationKind::unsupported_name_encoding:
        return status ==
               LocalResourceInventoryStatus::unsupported_name_encoding;
    case GoldSrcResourceNameClassificationKind::unsupported_mapping:
        return status == LocalResourceInventoryStatus::unsupported_mapping;
    }
    return false;
}

[[nodiscard]] WorldResourceReadiness world_status_from(
    const LocalResourceReadinessStatus status) noexcept
{
    switch (status) {
    case LocalResourceReadinessStatus::ready_local_file:
        return WorldResourceReadiness::ready;
    case LocalResourceReadinessStatus::missing_local_file:
        return WorldResourceReadiness::local_map_missing;
    case LocalResourceReadinessStatus::unsafe_name:
        return WorldResourceReadiness::local_map_unsafe;
    case LocalResourceReadinessStatus::ambiguous_local_match:
        return WorldResourceReadiness::local_map_ambiguous;
    case LocalResourceReadinessStatus::local_io_error:
        return WorldResourceReadiness::local_map_io_error;
    case LocalResourceReadinessStatus::metadata_only:
    case LocalResourceReadinessStatus::unsupported_name_encoding:
    case LocalResourceReadinessStatus::unsupported_mapping:
        return WorldResourceReadiness::map_name_invalid;
    }
    return WorldResourceReadiness::local_map_io_error;
}

} // namespace

bool valid_local_resource_readiness_limits(
    const LocalResourceReadinessLimits& limits) noexcept
{
    return limits.maximum_entry_count > 0U &&
           limits.maximum_entry_count <=
               kMaximumLocalResourceReadinessEntries &&
           limits.maximum_locator_virtual_name_bytes > 0U &&
           limits.maximum_locator_virtual_name_bytes <=
               kMaximumLocatorVirtualNameBytes;
}

LocalResourceReadinessEntry::LocalResourceReadinessEntry(
    const std::size_t wire_ordinal,
    const ResourceType resource_type,
    const std::uint16_t resource_index,
    const std::size_t original_wire_name_byte_length,
    const LocalResourceReadinessStatus status,
    const LocalResourceReadinessImpact impact,
    std::optional<local::LocalResourceLocator> locator,
    const std::optional<local::LocalResourceRootKind> source_root_kind) noexcept
    : wire_ordinal_{wire_ordinal},
      resource_type_{resource_type},
      resource_index_{resource_index},
      original_wire_name_byte_length_{original_wire_name_byte_length},
      status_{status},
      impact_{impact},
      locator_{std::move(locator)},
      source_root_kind_{source_root_kind}
{
}

std::size_t LocalResourceReadinessEntry::wire_ordinal() const noexcept
{
    return wire_ordinal_;
}

ResourceType LocalResourceReadinessEntry::resource_type() const noexcept
{
    return resource_type_;
}

std::uint16_t LocalResourceReadinessEntry::resource_index() const noexcept
{
    return resource_index_;
}

std::size_t LocalResourceReadinessEntry::original_wire_name_byte_length()
    const noexcept
{
    return original_wire_name_byte_length_;
}

LocalResourceReadinessStatus LocalResourceReadinessEntry::status()
    const noexcept
{
    return status_;
}

LocalResourceReadinessImpact LocalResourceReadinessEntry::impact()
    const noexcept
{
    return impact_;
}

const std::optional<local::LocalResourceLocator>&
LocalResourceReadinessEntry::locator() const noexcept
{
    return locator_;
}

std::optional<local::LocalResourceRootId>
LocalResourceReadinessEntry::source_root_id() const noexcept
{
    return locator_ ? std::optional{locator_->root_id()} : std::nullopt;
}

std::optional<local::LocalResourceRootKind>
LocalResourceReadinessEntry::source_root_kind() const noexcept
{
    return source_root_kind_;
}

std::optional<std::uint64_t> LocalResourceReadinessEntry::local_file_size()
    const noexcept
{
    return locator_ ? std::optional{locator_->expected_file_size()}
                    : std::nullopt;
}

std::optional<local::LocalStableFileIdentity>
LocalResourceReadinessEntry::stable_identity() const noexcept
{
    return locator_ ? std::optional{locator_->expected_identity()}
                    : std::nullopt;
}

LocalResourceReadinessCompatibilityProfile
LocalResourceReadinessEntry::compatibility_profile() const noexcept
{
    return LocalResourceReadinessCompatibilityProfile::
        stock_protocol_48_standard_local_metadata;
}

LocalResourceReadinessEvidenceProfile
LocalResourceReadinessEntry::evidence_profile() const noexcept
{
    return LocalResourceReadinessEvidenceProfile::
        exact_resource_list_inventory_and_server_info_correlation;
}

LocalResourceReadinessSummary::LocalResourceReadinessSummary(
    std::array<std::size_t, kLocalResourceReadinessStatusCount> status_counts,
    std::array<std::size_t, kLocalResourceReadinessImpactCount> impact_counts)
    noexcept
    : status_counts_{status_counts},
      impact_counts_{impact_counts},
      total_entry_count_{std::accumulate(
          status_counts_.begin(), status_counts_.end(), std::size_t{0U})}
{
}

std::size_t LocalResourceReadinessSummary::total_entry_count() const noexcept
{
    return total_entry_count_;
}

std::size_t LocalResourceReadinessSummary::count(
    const LocalResourceReadinessStatus status) const noexcept
{
    const auto index = status_index(status);
    return index ? status_counts_[*index] : 0U;
}

std::size_t LocalResourceReadinessSummary::count(
    const LocalResourceReadinessImpact impact) const noexcept
{
    const auto index = impact_index(impact);
    return index ? impact_counts_[*index] : 0U;
}

std::size_t
LocalResourceReadinessSummary::resolved_mapped_file_count() const noexcept
{
    return count(LocalResourceReadinessStatus::ready_local_file);
}

std::size_t LocalResourceReadinessSummary::metadata_only_count() const noexcept
{
    return count(LocalResourceReadinessStatus::metadata_only);
}

std::size_t LocalResourceReadinessSummary::missing_count() const noexcept
{
    return count(LocalResourceReadinessStatus::missing_local_file);
}

std::size_t
LocalResourceReadinessSummary::security_blocked_count() const noexcept
{
    return count(LocalResourceReadinessImpact::security_blocked);
}

std::size_t LocalResourceReadinessSummary::unsupported_count() const noexcept
{
    return count(LocalResourceReadinessImpact::unsupported_profile);
}

std::size_t LocalResourceReadinessSummary::ambiguous_count() const noexcept
{
    return count(LocalResourceReadinessStatus::ambiguous_local_match);
}

std::size_t LocalResourceReadinessSummary::io_failure_count() const noexcept
{
    return count(LocalResourceReadinessStatus::local_io_error);
}

WorldResourceSelection::WorldResourceSelection(
    const WorldResourceReadiness status,
    const std::size_t server_map_name_byte_length,
    const std::optional<std::size_t> entry_offset,
    const std::optional<std::size_t> wire_ordinal,
    const std::optional<std::uint16_t> resource_index,
    std::optional<local::LocalResourceLocator> locator) noexcept
    : status_{status},
      server_map_name_byte_length_{server_map_name_byte_length},
      entry_offset_{entry_offset},
      wire_ordinal_{wire_ordinal},
      resource_index_{resource_index},
      locator_{std::move(locator)}
{
}

WorldResourceReadiness WorldResourceSelection::status() const noexcept
{
    return status_;
}

std::size_t WorldResourceSelection::server_map_name_byte_length() const noexcept
{
    return server_map_name_byte_length_;
}

std::optional<std::size_t> WorldResourceSelection::entry_offset() const noexcept
{
    return entry_offset_;
}

std::optional<std::size_t> WorldResourceSelection::wire_ordinal() const noexcept
{
    return wire_ordinal_;
}

std::optional<std::uint16_t> WorldResourceSelection::resource_index()
    const noexcept
{
    return resource_index_;
}

const std::optional<local::LocalResourceLocator>&
WorldResourceSelection::locator() const noexcept
{
    return locator_;
}

bool WorldResourceSelection::world_geometry_ready() const noexcept
{
    return status_ == WorldResourceReadiness::ready;
}

LocalResourceReadinessEvidenceProfile
WorldResourceSelection::evidence_profile() const noexcept
{
    return LocalResourceReadinessEvidenceProfile::
        exact_resource_list_inventory_and_server_info_correlation;
}

LocalResourceReadinessState::LocalResourceReadinessState(
    std::vector<LocalResourceReadinessEntry> entries,
    LocalResourceReadinessSummary summary,
    WorldResourceSelection world_selection,
    const bool complete_for_supported_local_profile) noexcept
    : entries_{std::move(entries)},
      summary_{std::move(summary)},
      world_selection_{std::move(world_selection)},
      complete_for_supported_local_profile_{
          complete_for_supported_local_profile}
{
}

std::span<const LocalResourceReadinessEntry>
LocalResourceReadinessState::entries() const noexcept
{
    return entries_;
}

std::size_t LocalResourceReadinessState::entry_count() const noexcept
{
    return entries_.size();
}

const LocalResourceReadinessSummary& LocalResourceReadinessState::summary()
    const noexcept
{
    return summary_;
}

const LocalResourceReadinessEntry* LocalResourceReadinessState::find_exact(
    const ResourceType type,
    const std::uint16_t resource_index) const noexcept
{
    const auto found = std::ranges::find_if(
        entries_,
        [type, resource_index](const auto& entry) {
            return entry.resource_type() == type &&
                   entry.resource_index() == resource_index;
        });
    return found == entries_.end() ? nullptr : &*found;
}

const WorldResourceSelection& LocalResourceReadinessState::world_selection()
    const noexcept
{
    return world_selection_;
}

bool LocalResourceReadinessState::complete_for_supported_local_profile()
    const noexcept
{
    return complete_for_supported_local_profile_;
}

bool LocalResourceReadinessState::world_geometry_candidate_available()
    const noexcept
{
    return world_selection_.entry_offset().has_value();
}

bool LocalResourceReadinessState::world_geometry_ready() const noexcept
{
    return world_selection_.world_geometry_ready();
}

LocalResourceReadinessCompatibilityProfile
LocalResourceReadinessState::compatibility_profile() const noexcept
{
    return LocalResourceReadinessCompatibilityProfile::
        stock_protocol_48_standard_local_metadata;
}

LocalResourceReadinessEvidenceProfile
LocalResourceReadinessState::evidence_profile() const noexcept
{
    return LocalResourceReadinessEvidenceProfile::
        exact_resource_list_inventory_and_server_info_correlation;
}

LocalResourceReadinessBuilder::LocalResourceReadinessBuilder(
    const LocalResourceReadinessLimits limits) noexcept
    : limits_{limits}
{
}

bool LocalResourceReadinessBuilder::valid_configuration() const noexcept
{
    return valid_local_resource_readiness_limits(limits_);
}

const LocalResourceReadinessLimits& LocalResourceReadinessBuilder::limits()
    const noexcept
{
    return limits_;
}

LocalResourceReadinessBuildResult LocalResourceReadinessBuilder::build(
    const ResourceListState& resource_list,
    const LocalResourceInventoryState& inventory,
    const ServerInfoState& server_info,
    const GoldSrcResourceNameMapper& mapper,
    const local::LocalResourceEnvironment& environment) const
{
    if (!valid_configuration() || !valid_mapper(mapper) ||
        environment.root_count() == 0U ||
        !valid_local_resource_resolver_limits(environment.limits())) {
        return failure(
            LocalResourceReadinessErrorCode::invalid_configuration,
            "Local resource readiness dependencies are invalid");
    }
    if (resource_list.resource_count() > limits_.maximum_entry_count ||
        inventory.entry_count() > limits_.maximum_entry_count) {
        return failure(
            LocalResourceReadinessErrorCode::entry_count_limit_exceeded,
            "Local resource readiness entry count exceeds its configured bound");
    }
    if (resource_list.resource_count() != inventory.entry_count()) {
        return failure(
            LocalResourceReadinessErrorCode::entry_count_mismatch,
            "Resource list and local inventory entry counts differ");
    }

    std::array<std::size_t, kLocalResourceInventoryStatusCount>
        observed_inventory_counts{};
    for (const auto& entry : inventory.entries()) {
        const auto index = inventory_status_index(entry.status());
        if (!index) {
            return failure(
                LocalResourceReadinessErrorCode::inventory_summary_mismatch,
                "Local inventory contains an invalid status");
        }
        ++observed_inventory_counts[*index];
    }
    if (inventory.summary().total_entry_count() != inventory.entry_count()) {
        return failure(
            LocalResourceReadinessErrorCode::inventory_summary_mismatch,
            "Local inventory summary total does not match its entries");
    }
    for (std::size_t index = 0U;
         index < observed_inventory_counts.size();
         ++index) {
        const auto status = static_cast<LocalResourceInventoryStatus>(index);
        if (inventory.summary().count(status) !=
            observed_inventory_counts[index]) {
            return failure(
                LocalResourceReadinessErrorCode::inventory_summary_mismatch,
                "Local inventory summary counts do not match its entries");
        }
    }

    try {
        std::vector<LocalResourceReadinessEntry> entries;
        entries.reserve(resource_list.resource_count());
        std::array<std::size_t, kLocalResourceReadinessStatusCount>
            status_counts{};
        std::array<std::size_t, kLocalResourceReadinessImpactCount>
            impact_counts{};

        const auto inventory_entries = inventory.entries();
        for (std::size_t offset = 0U;
             offset < resource_list.entries().size();
             ++offset) {
            const auto& resource = resource_list.entries()[offset];
            const auto& inventoried = inventory_entries[offset];
            const auto ordinal = resource.wire_ordinal();

            if (ordinal != offset || inventoried.wire_ordinal() != ordinal) {
                return failure(
                    LocalResourceReadinessErrorCode::wire_ordinal_mismatch,
                    "Resource list and inventory wire ordinals differ",
                    offset);
            }
            if (inventoried.resource_type() != resource.type()) {
                return failure(
                    LocalResourceReadinessErrorCode::resource_type_mismatch,
                    "Resource list and inventory types differ",
                    ordinal);
            }
            if (inventoried.resource_index() != resource.index().value()) {
                return failure(
                    LocalResourceReadinessErrorCode::resource_index_mismatch,
                    "Resource list and inventory indexes differ",
                    ordinal);
            }
            if (inventoried.wire_name_byte_length() !=
                resource.name().byte_length()) {
                return failure(
                    LocalResourceReadinessErrorCode::wire_name_length_mismatch,
                    "Resource list and inventory name lengths differ",
                    ordinal);
            }

            const auto classification =
                mapper.classify(resource.type(), resource.name());
            if (classification.original_name_byte_length() !=
                    resource.name().byte_length() ||
                !classification_status_consistent(
                    classification.kind(), inventoried.status())) {
                return failure(
                    LocalResourceReadinessErrorCode::
                        classification_status_mismatch,
                    "Re-derived resource classification disagrees with inventory",
                    ordinal);
            }

            const bool mapped =
                classification.kind() ==
                GoldSrcResourceNameClassificationKind::mapped_file;
            if (mapped != inventoried.virtual_path().has_value()) {
                return failure(
                    LocalResourceReadinessErrorCode::
                        invalid_virtual_path_invariant,
                    "Inventory virtual-path presence disagrees with mapping",
                    ordinal);
            }
            if ((inventoried.status() ==
                 LocalResourceInventoryStatus::resolved) !=
                inventoried.resolved_metadata().has_value()) {
                return failure(
                    LocalResourceReadinessErrorCode::invalid_resolved_metadata,
                    "Inventory resolved metadata presence disagrees with status",
                    ordinal);
            }

            LocalResourceReadinessStatus status =
                classification.kind() ==
                        GoldSrcResourceNameClassificationKind::metadata_only
                    ? LocalResourceReadinessStatus::metadata_only
                    : readiness_status(inventoried.status());
            const auto impact = readiness_impact(status);
            std::optional<local::LocalResourceLocator> locator;
            std::optional<local::LocalResourceRootKind> root_kind;

            if (mapped) {
                if (!classification.safe_virtual_name() ||
                    !inventoried.virtual_path()) {
                    return failure(
                        LocalResourceReadinessErrorCode::
                            invalid_virtual_path_invariant,
                        "Mapped resource is missing safe virtual metadata",
                        ordinal);
                }
                if (classification.safe_virtual_name()->byte_length() >
                    limits_.maximum_locator_virtual_name_bytes) {
                    return failure(
                        LocalResourceReadinessErrorCode::
                            invalid_virtual_path_invariant,
                        "Mapped virtual name exceeds the locator bound",
                        ordinal);
                }
                auto local_name = local::LocalVirtualResourceName::create(
                    classification.safe_virtual_name()->value());
                if (!local_name || !local_name.name) {
                    return failure(
                        LocalResourceReadinessErrorCode::
                            invalid_virtual_path_invariant,
                        "Mapped virtual name could not be re-derived",
                        ordinal);
                }
                if (local_name.name->id() != inventoried.virtual_path()->id()) {
                    return failure(
                        LocalResourceReadinessErrorCode::virtual_path_id_mismatch,
                        "Re-derived virtual-name identity differs from inventory",
                        ordinal);
                }
                if (local_name.name->value().size() !=
                    inventoried.virtual_path()->byte_length()) {
                    return failure(
                        LocalResourceReadinessErrorCode::
                            virtual_path_length_mismatch,
                        "Re-derived virtual-name length differs from inventory",
                        ordinal);
                }
                if (local_name.name->component_count() !=
                    inventoried.virtual_path()->component_count()) {
                    return failure(
                        LocalResourceReadinessErrorCode::
                            virtual_path_component_count_mismatch,
                        "Re-derived virtual-name components differ from inventory",
                        ordinal);
                }

                if (status ==
                    LocalResourceReadinessStatus::ready_local_file) {
                    const auto& resolved = *inventoried.resolved_metadata();
                    const auto root = environment.root_metadata(
                        resolved.root_id());
                    if (!resolved.identity().valid() || !root ||
                        resolved.file_size() >
                            environment.limits().maximum_file_size) {
                        return failure(
                            LocalResourceReadinessErrorCode::
                                invalid_resolved_metadata,
                            "Resolved inventory metadata is not valid for the environment",
                            ordinal);
                    }
                    auto created = environment.make_locator(
                        resolved.root_id(),
                        std::move(*local_name.name),
                        resolved.identity(),
                        resolved.file_size());
                    if (!created || !created.locator) {
                        return failure(
                            LocalResourceReadinessErrorCode::
                                locator_creation_failed,
                            "Unable to construct an approved local locator",
                            ordinal);
                    }
                    locator.emplace(std::move(*created.locator));
                    root_kind = root->kind;
                }
            }

            const auto status_slot = status_index(status);
            const auto impact_slot = impact_index(impact);
            if (!status_slot || !impact_slot) {
                return failure(
                    LocalResourceReadinessErrorCode::
                        unable_to_retain_readiness,
                    "Readiness classification produced an invalid state",
                    ordinal);
            }
            ++status_counts[*status_slot];
            ++impact_counts[*impact_slot];
            entries.emplace_back(LocalResourceReadinessEntry{
                ordinal,
                resource.type(),
                resource.index().value(),
                resource.name().byte_length(),
                status,
                impact,
                std::move(locator),
                root_kind});
        }

        const auto map_bytes = std::string_view{server_info.map_file_path()};
        const auto map_classification =
            mapper.classify(ResourceType::model, map_bytes);
        if (map_classification.kind() !=
                GoldSrcResourceNameClassificationKind::mapped_file ||
            !map_classification.safe_virtual_name()) {
            return failure(
                LocalResourceReadinessErrorCode::invalid_server_map_name,
                "Server map metadata is outside the supported byte policy",
                std::nullopt,
                WorldResourceReadiness::map_name_invalid);
        }

        std::optional<std::size_t> map_offset;
        std::size_t map_match_count = 0U;
        for (std::size_t offset = 0U;
             offset < resource_list.entries().size();
             ++offset) {
            if (resource_list.entries()[offset].name().bytes() == map_bytes) {
                ++map_match_count;
                if (map_match_count == 1U) {
                    map_offset = offset;
                }
            }
        }
        if (map_match_count == 0U || !map_offset) {
            return failure(
                LocalResourceReadinessErrorCode::map_entry_missing_from_list,
                "Server map metadata has no exact resource-list entry",
                std::nullopt,
                WorldResourceReadiness::map_entry_missing_from_list);
        }
        if (map_match_count != 1U) {
            return failure(
                LocalResourceReadinessErrorCode::map_entry_duplicated,
                "Server map metadata has multiple exact resource-list entries",
                std::nullopt,
                WorldResourceReadiness::map_entry_duplicated);
        }

        const auto& map_resource = resource_list.entries()[*map_offset];
        if (map_resource.type() != ResourceType::model) {
            return failure(
                LocalResourceReadinessErrorCode::map_entry_not_model,
                "The exact server map entry is not a model resource",
                map_resource.wire_ordinal(),
                WorldResourceReadiness::map_entry_not_model);
        }

        const auto& map_entry = entries[*map_offset];
        const auto world_status = world_status_from(map_entry.status());
        if (world_status == WorldResourceReadiness::map_name_invalid) {
            return failure(
                LocalResourceReadinessErrorCode::invalid_server_map_name,
                "The exact model map entry is outside the supported profile",
                map_entry.wire_ordinal(),
                world_status);
        }
        std::optional<local::LocalResourceLocator> world_locator;
        if (map_entry.locator()) {
            world_locator.emplace(*map_entry.locator());
        }
        WorldResourceSelection world_selection{
            world_status,
            map_bytes.size(),
            map_offset,
            map_entry.wire_ordinal(),
            map_entry.resource_index(),
            std::move(world_locator)};

        const bool all_entries_locally_complete = std::ranges::all_of(
            entries,
            [](const LocalResourceReadinessEntry& entry) {
                return entry.status() ==
                           LocalResourceReadinessStatus::ready_local_file ||
                       entry.status() ==
                           LocalResourceReadinessStatus::metadata_only;
            });
        const bool complete =
            all_entries_locally_complete && world_selection.world_geometry_ready();
        LocalResourceReadinessSummary summary{
            status_counts, impact_counts};
        return LocalResourceReadinessBuildResult{
            LocalResourceReadinessState{
                std::move(entries),
                std::move(summary),
                std::move(world_selection),
                complete},
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return failure(
            LocalResourceReadinessErrorCode::unable_to_retain_readiness,
            "Unable to allocate bounded local resource readiness metadata");
    } catch (...) {
        return failure(
            LocalResourceReadinessErrorCode::unable_to_retain_readiness,
            "Unable to build bounded local resource readiness metadata");
    }
}

} // namespace hlclient::goldsrc
