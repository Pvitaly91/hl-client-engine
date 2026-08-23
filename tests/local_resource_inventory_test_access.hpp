#pragma once

#include <hlclient/goldsrc/local_resource_inventory.hpp>

#include <cstddef>

namespace hlclient::goldsrc::detail {

// Test-only, deliberately narrow corruption seam for exercising readiness
// validation of immutable inventory invariants. It is not part of any public
// target header and cannot manufacture filesystem identities or root IDs.
class LocalResourceInventoryCorruptionTestAccess final {
public:
    static void replace_type_and_status(
        LocalResourceInventoryState& state,
        const std::size_t entry_offset,
        const ResourceType resource_type,
        const LocalResourceInventoryStatus status)
    {
        auto& entry = state.entries_.at(entry_offset);
        const auto old_status = static_cast<std::size_t>(entry.status_);
        const auto new_status = static_cast<std::size_t>(status);
        --state.summary_.counts_.at(old_status);
        ++state.summary_.counts_.at(new_status);
        entry.resource_type_ = resource_type;
        entry.status_ = status;
        entry.virtual_path_.reset();
        entry.resolved_metadata_.reset();
    }

    static void replace_wire_ordinal(
        LocalResourceInventoryState& state,
        const std::size_t entry_offset,
        const std::size_t wire_ordinal)
    {
        state.entries_.at(entry_offset).wire_ordinal_ = wire_ordinal;
    }

    static void remove_resolved_metadata(
        LocalResourceInventoryState& state,
        const std::size_t entry_offset)
    {
        state.entries_.at(entry_offset).resolved_metadata_.reset();
    }

    static void copy_resolved_metadata(
        LocalResourceInventoryState& state,
        const std::size_t destination_offset,
        const std::size_t source_offset)
    {
        auto& destination = state.entries_.at(destination_offset);
        const auto& source = state.entries_.at(source_offset);
        destination.resolved_metadata_.reset();
        if (source.resolved_metadata_) {
            destination.resolved_metadata_.emplace(
                *source.resolved_metadata_);
        }
    }

    static void replace_virtual_component_count(
        LocalResourceInventoryState& state,
        const std::size_t entry_offset,
        const std::size_t component_count)
    {
        auto& metadata = state.entries_.at(entry_offset).virtual_path_;
        if (metadata) {
            metadata->component_count_ = component_count;
        }
    }

    static void invalidate_resolved_identity(
        LocalResourceInventoryState& state,
        const std::size_t entry_offset)
    {
        auto& metadata = state.entries_.at(entry_offset).resolved_metadata_;
        if (metadata) {
            metadata->identity_ = {};
        }
    }
};

} // namespace hlclient::goldsrc::detail
