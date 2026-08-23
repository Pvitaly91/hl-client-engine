#pragma once

#include <hlclient/goldsrc/precache_manifest.hpp>

#include <cstddef>
#include <cstdint>

namespace hlclient::goldsrc::detail {

class ResourceListCorruptionTestAccess final {
public:
    static void replace_resource_type(
        ResourceListState& state,
        const std::size_t entry_offset,
        const ResourceType resource_type)
    {
        state.entries_.at(entry_offset).type_ = resource_type;
    }
};

class PrecacheManifestDefensiveTestAccess final {
public:
    static void replace_resource_type(
        LocalResourceReadinessState& state,
        const std::size_t entry_offset,
        const ResourceType resource_type)
    {
        state.entries_.at(entry_offset).resource_type_ = resource_type;
    }

    static void replace_resource_index(
        LocalResourceReadinessState& state,
        const std::size_t entry_offset,
        const std::uint16_t resource_index)
    {
        state.entries_.at(entry_offset).resource_index_ = resource_index;
    }

    [[nodiscard]] static PrecacheManifestBuildResult build(
        const PrecacheManifestBuilder& builder,
        const LocalResourceReadinessState& readiness,
        const ResourceListState& resource_list,
        const ServerInfoState& server_info)
    {
        return builder.build_from_readiness(
            readiness,
            PrecacheManifestSourceGeometry{
                resource_list.resource_count(),
                resource_list.total_name_byte_count(),
                resource_list.compatibility_profile(),
                server_info.compatibility_profile(),
            });
    }
};

} // namespace hlclient::goldsrc::detail
