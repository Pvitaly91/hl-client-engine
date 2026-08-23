#include <hlclient/goldsrc/precache_manifest.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hlclient::goldsrc {
namespace {

inline constexpr std::size_t kPrecacheResourceTypeCount = 5U;

[[nodiscard]] constexpr std::optional<std::size_t> type_slot(
    const ResourceType type) noexcept
{
    switch (type) {
    case ResourceType::sound: return 0U;
    case ResourceType::model: return 1U;
    case ResourceType::generic: return 2U;
    case ResourceType::event_script: return 3U;
    case ResourceType::decal: return 4U;
    }
    return std::nullopt;
}

[[nodiscard]] PrecacheManifestBuildResult failure(
    const PrecacheManifestErrorCode code,
    const std::string_view context,
    const std::optional<LocalResourceReadinessErrorCode> readiness_code =
        std::nullopt,
    const std::optional<WorldResourceReadiness> world_status = std::nullopt,
    const std::optional<std::size_t> entry_ordinal = std::nullopt)
{
    const auto size =
        (std::min)(context.size(), kPrecacheManifestDiagnosticTextLimit);
    return PrecacheManifestBuildResult{
        std::nullopt,
        PrecacheManifestError{
            code,
            readiness_code,
            world_status,
            entry_ordinal,
            std::string{context.data(), size},
        },
    };
}

[[nodiscard]] PrecacheManifestCompleteness completeness_from(
    const LocalResourceReadinessState& readiness) noexcept
{
    const auto& summary = readiness.summary();
    if (summary.security_blocked_count() != 0U) {
        return PrecacheManifestCompleteness::blocked_unsafe_resources;
    }
    if (summary.ambiguous_count() != 0U ||
        summary.io_failure_count() != 0U) {
        return PrecacheManifestCompleteness::local_io_failure;
    }
    if (summary.unsupported_count() != 0U) {
        return PrecacheManifestCompleteness::unsupported_profile;
    }
    if (summary.missing_count() != 0U) {
        return readiness.world_geometry_ready()
                   ? PrecacheManifestCompleteness::world_ready_but_incomplete
                   : PrecacheManifestCompleteness::
                         incomplete_missing_resources;
    }
    return readiness.complete_for_supported_local_profile()
               ? PrecacheManifestCompleteness::
                     complete_for_supported_local_profile
               : PrecacheManifestCompleteness::
                     incomplete_missing_resources;
}

} // namespace

bool valid_precache_manifest_limits(
    const PrecacheManifestLimits& limits) noexcept
{
    return limits.maximum_readiness_entries > 0U &&
           limits.maximum_readiness_entries <=
               kMaximumLocalResourceReadinessEntries &&
           limits.maximum_manifest_entries > 0U &&
           limits.maximum_manifest_entries <=
               kMaximumPrecacheManifestEntries &&
           limits.maximum_slots_per_type > 0U &&
           limits.maximum_slots_per_type <= kMaximumPrecacheSlotsPerType &&
           limits.maximum_total_slots > 0U &&
           limits.maximum_total_slots <=
               kMaximumPrecacheManifestTotalSlots &&
           limits.maximum_manifest_events > 0U &&
           limits.maximum_manifest_events <=
               kMaximumPrecacheManifestEvents &&
           limits.maximum_locator_virtual_name_bytes > 0U &&
           limits.maximum_locator_virtual_name_bytes <=
               kMaximumLocatorVirtualNameBytes;
}

PrecacheManifestEntry::PrecacheManifestEntry(
    const std::size_t wire_ordinal,
    const ResourceType resource_type,
    const std::uint16_t resource_index,
    const LocalResourceReadinessStatus readiness_status,
    const LocalResourceReadinessImpact readiness_impact,
    std::optional<local_resources::LocalResourceLocator> locator) noexcept
    : wire_ordinal_{wire_ordinal},
      resource_type_{resource_type},
      resource_index_{resource_index},
      readiness_status_{readiness_status},
      readiness_impact_{readiness_impact},
      locator_{std::move(locator)}
{
}

std::size_t PrecacheManifestEntry::wire_ordinal() const noexcept
{
    return wire_ordinal_;
}

ResourceType PrecacheManifestEntry::resource_type() const noexcept
{
    return resource_type_;
}

std::uint16_t PrecacheManifestEntry::resource_index() const noexcept
{
    return resource_index_;
}

LocalResourceReadinessStatus PrecacheManifestEntry::readiness_status()
    const noexcept
{
    return readiness_status_;
}

LocalResourceReadinessImpact PrecacheManifestEntry::readiness_impact()
    const noexcept
{
    return readiness_impact_;
}

const std::optional<local_resources::LocalResourceLocator>&
PrecacheManifestEntry::locator() const noexcept
{
    return locator_;
}

std::optional<std::uint64_t> PrecacheManifestEntry::local_file_size()
    const noexcept
{
    return locator_ ? std::optional{locator_->expected_file_size()}
                    : std::nullopt;
}

std::uint16_t PrecacheManifestEntry::type_local_slot() const noexcept
{
    return resource_index_;
}

PrecacheManifestCompatibilityProfile
PrecacheManifestEntry::compatibility_profile() const noexcept
{
    return PrecacheManifestCompatibilityProfile::
        stock_protocol_48_standard_metadata_only;
}

PrecacheManifestEvidenceProfile PrecacheManifestEntry::evidence_profile()
    const noexcept
{
    return PrecacheManifestEvidenceProfile::
        exact_correlated_local_resource_metadata;
}

PrecacheSlotTable::PrecacheSlotTable(
    const ResourceType resource_type,
    std::vector<std::optional<std::size_t>> slots) noexcept
    : resource_type_{resource_type},
      slots_{std::move(slots)},
      occupied_slot_count_{static_cast<std::size_t>(std::count_if(
          slots_.begin(),
          slots_.end(),
          [](const auto& slot) { return slot.has_value(); }))}
{
}

ResourceType PrecacheSlotTable::resource_type() const noexcept
{
    return resource_type_;
}

std::size_t PrecacheSlotTable::slot_count() const noexcept
{
    return slots_.size();
}

std::size_t PrecacheSlotTable::occupied_slot_count() const noexcept
{
    return occupied_slot_count_;
}

std::optional<std::size_t> PrecacheSlotTable::entry_offset(
    const std::uint16_t slot) const noexcept
{
    return static_cast<std::size_t>(slot) < slots_.size()
               ? slots_[slot]
               : std::nullopt;
}

std::span<const std::optional<std::size_t>> PrecacheSlotTable::slots()
    const noexcept
{
    return slots_;
}

PrecacheManifestState::PrecacheManifestState(
    std::vector<PrecacheManifestEntry> entries,
    PrecacheSlotTable sound_slots,
    PrecacheSlotTable model_slots,
    PrecacheSlotTable generic_slots,
    PrecacheSlotTable event_script_slots,
    PrecacheSlotTable decal_slots,
    WorldResourceSelection world_selection,
    LocalResourceReadinessSummary readiness_summary,
    const PrecacheManifestCompleteness completeness,
    const PrecacheManifestSourceGeometry source_geometry) noexcept
    : entries_{std::move(entries)},
      sound_slots_{std::move(sound_slots)},
      model_slots_{std::move(model_slots)},
      generic_slots_{std::move(generic_slots)},
      event_script_slots_{std::move(event_script_slots)},
      decal_slots_{std::move(decal_slots)},
      world_selection_{std::move(world_selection)},
      readiness_summary_{std::move(readiness_summary)},
      completeness_{completeness},
      source_geometry_{source_geometry}
{
}

std::span<const PrecacheManifestEntry> PrecacheManifestState::entries()
    const noexcept
{
    return entries_;
}

std::size_t PrecacheManifestState::entry_count() const noexcept
{
    return entries_.size();
}

const PrecacheManifestEntry* PrecacheManifestState::find(
    const ResourceType type,
    const std::uint16_t resource_index) const noexcept
{
    const PrecacheSlotTable* table = nullptr;
    switch (type) {
    case ResourceType::sound: table = &sound_slots_; break;
    case ResourceType::model: table = &model_slots_; break;
    case ResourceType::generic: table = &generic_slots_; break;
    case ResourceType::event_script: table = &event_script_slots_; break;
    case ResourceType::decal: table = &decal_slots_; break;
    }
    if (table == nullptr) {
        return nullptr;
    }
    const auto offset = table->entry_offset(resource_index);
    return offset && *offset < entries_.size() ? &entries_[*offset] : nullptr;
}

const PrecacheSlotTable& PrecacheManifestState::sound_slots() const noexcept
{
    return sound_slots_;
}

const PrecacheSlotTable& PrecacheManifestState::model_slots() const noexcept
{
    return model_slots_;
}

const PrecacheSlotTable& PrecacheManifestState::generic_slots() const noexcept
{
    return generic_slots_;
}

const PrecacheSlotTable& PrecacheManifestState::event_script_slots()
    const noexcept
{
    return event_script_slots_;
}

const PrecacheSlotTable& PrecacheManifestState::decal_slots() const noexcept
{
    return decal_slots_;
}

const WorldResourceSelection& PrecacheManifestState::world_selection()
    const noexcept
{
    return world_selection_;
}

const PrecacheManifestEntry* PrecacheManifestState::world_entry() const noexcept
{
    const auto offset = world_selection_.entry_offset();
    return offset && *offset < entries_.size() ? &entries_[*offset] : nullptr;
}

const LocalResourceReadinessSummary&
PrecacheManifestState::readiness_summary() const noexcept
{
    return readiness_summary_;
}

PrecacheManifestCompleteness PrecacheManifestState::completeness()
    const noexcept
{
    return completeness_;
}

bool PrecacheManifestState::complete_for_supported_local_profile()
    const noexcept
{
    return completeness_ == PrecacheManifestCompleteness::
                                complete_for_supported_local_profile;
}

bool PrecacheManifestState::world_geometry_ready() const noexcept
{
    return world_selection_.world_geometry_ready();
}

const PrecacheManifestSourceGeometry&
PrecacheManifestState::source_geometry() const noexcept
{
    return source_geometry_;
}

PrecacheManifestCompatibilityProfile
PrecacheManifestState::compatibility_profile() const noexcept
{
    return PrecacheManifestCompatibilityProfile::
        stock_protocol_48_standard_metadata_only;
}

PrecacheManifestEvidenceProfile PrecacheManifestState::evidence_profile()
    const noexcept
{
    return PrecacheManifestEvidenceProfile::
        exact_correlated_local_resource_metadata;
}

PrecacheManifestBuilder::PrecacheManifestBuilder(
    const PrecacheManifestLimits limits) noexcept
    : limits_{limits}
{
}

bool PrecacheManifestBuilder::valid_configuration() const noexcept
{
    return valid_precache_manifest_limits(limits_);
}

const PrecacheManifestLimits& PrecacheManifestBuilder::limits() const noexcept
{
    return limits_;
}

PrecacheManifestBuildResult PrecacheManifestBuilder::build(
    const ResourceListState& resource_list,
    const LocalResourceInventoryState& inventory,
    const ServerInfoState& server_info,
    const GoldSrcResourceNameMapper& mapper,
    const local_resources::LocalResourceEnvironment& environment) const
{
    if (!valid_configuration()) {
        return failure(
            PrecacheManifestErrorCode::invalid_configuration,
            "Precache manifest limits are outside project hard caps");
    }

    const LocalResourceReadinessBuilder readiness_builder{
        LocalResourceReadinessLimits{
            limits_.maximum_readiness_entries,
            limits_.maximum_locator_virtual_name_bytes}};
    auto readiness = readiness_builder.build(
        resource_list, inventory, server_info, mapper, environment);
    if (!readiness || !readiness.state) {
        return failure(
            PrecacheManifestErrorCode::readiness_build_failed,
            readiness.error
                ? readiness.error->context
                : "Local resource readiness produced no state",
            readiness.error
                ? std::optional{readiness.error->code}
                : std::nullopt,
            readiness.error ? readiness.error->world_status : std::nullopt,
            readiness.error ? readiness.error->entry_ordinal : std::nullopt);
    }
    return build_from_readiness(
        *readiness.state,
        PrecacheManifestSourceGeometry{
            resource_list.resource_count(),
            resource_list.total_name_byte_count(),
            resource_list.compatibility_profile(),
            server_info.compatibility_profile(),
        });
}

PrecacheManifestBuildResult PrecacheManifestBuilder::build_from_readiness(
    const LocalResourceReadinessState& readiness,
    const PrecacheManifestSourceGeometry source_geometry) const
{
    if (readiness.entry_count() > limits_.maximum_manifest_entries) {
        return failure(
            PrecacheManifestErrorCode::manifest_entry_limit_exceeded,
            "Precache manifest entry count exceeds its configured bound");
    }

    try {
        std::array<std::size_t, kPrecacheResourceTypeCount> slot_counts{};
        for (const auto& entry : readiness.entries()) {
            const auto table = type_slot(entry.resource_type());
            if (!table ||
                entry.resource_index() > kMaximumResourceIndexWireValue) {
                return failure(
                    PrecacheManifestErrorCode::resource_index_out_of_bounds,
                    "Manifest entry has an unsupported type-local index",
                    std::nullopt,
                    std::nullopt,
                    entry.wire_ordinal());
            }
            const auto required =
                static_cast<std::size_t>(entry.resource_index()) + 1U;
            if (required > limits_.maximum_slots_per_type) {
                return failure(
                    PrecacheManifestErrorCode::
                        slots_per_type_limit_exceeded,
                    "Type-local precache slots exceed their configured bound",
                    std::nullopt,
                    std::nullopt,
                    entry.wire_ordinal());
            }
            slot_counts[*table] = (std::max)(slot_counts[*table], required);
        }

        std::size_t total_slot_count = 0U;
        for (const auto count : slot_counts) {
            if (count > limits_.maximum_total_slots - total_slot_count) {
                return failure(
                    PrecacheManifestErrorCode::total_slot_limit_exceeded,
                    "Total precache slot count exceeds its configured bound");
            }
            total_slot_count += count;
        }

        std::array<std::vector<std::optional<std::size_t>>,
                   kPrecacheResourceTypeCount>
            slot_storage;
        for (std::size_t index = 0U; index < slot_storage.size(); ++index) {
            slot_storage[index].resize(slot_counts[index]);
        }

        std::vector<PrecacheManifestEntry> entries;
        entries.reserve(readiness.entry_count());
        for (const auto& entry : readiness.entries()) {
            const auto table = *type_slot(entry.resource_type());
            auto& slot = slot_storage[table][entry.resource_index()];
            if (slot) {
                return failure(
                    PrecacheManifestErrorCode::duplicate_type_local_slot,
                    "Duplicate type-local precache slot invariant failed",
                    std::nullopt,
                    std::nullopt,
                    entry.wire_ordinal());
            }
            slot = entries.size();

            std::optional<local_resources::LocalResourceLocator> locator;
            if (entry.locator()) {
                locator.emplace(*entry.locator());
            }
            entries.emplace_back(PrecacheManifestEntry{
                entry.wire_ordinal(),
                entry.resource_type(),
                entry.resource_index(),
                entry.status(),
                entry.impact(),
                std::move(locator)});
        }

        const auto world_offset =
            readiness.world_selection().entry_offset();
        if (!world_offset || *world_offset >= entries.size() ||
            entries[*world_offset].resource_type() != ResourceType::model ||
            !readiness.world_selection().resource_index() ||
            entries[*world_offset].resource_index() !=
                *readiness.world_selection().resource_index()) {
            return failure(
                PrecacheManifestErrorCode::world_selection_invariant_failed,
                "World selection does not identify one manifest model entry",
                std::nullopt,
                readiness.world_selection().status());
        }

        const auto completeness = completeness_from(readiness);

        return PrecacheManifestBuildResult{
            PrecacheManifestState{
                std::move(entries),
                PrecacheSlotTable{
                    ResourceType::sound, std::move(slot_storage[0U])},
                PrecacheSlotTable{
                    ResourceType::model, std::move(slot_storage[1U])},
                PrecacheSlotTable{
                    ResourceType::generic, std::move(slot_storage[2U])},
                PrecacheSlotTable{
                    ResourceType::event_script,
                    std::move(slot_storage[3U])},
                PrecacheSlotTable{
                    ResourceType::decal, std::move(slot_storage[4U])},
                readiness.world_selection(),
                readiness.summary(),
                completeness,
                source_geometry},
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return failure(
            PrecacheManifestErrorCode::unable_to_retain_manifest,
            "Unable to allocate bounded precache manifest metadata");
    } catch (...) {
        return failure(
            PrecacheManifestErrorCode::unable_to_retain_manifest,
            "Unable to build bounded precache manifest metadata");
    }
}

} // namespace hlclient::goldsrc
