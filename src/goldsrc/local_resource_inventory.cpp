#include <hlclient/goldsrc/local_resource_inventory.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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

[[nodiscard]] LocalResourceInventoryError bounded_error(
    const LocalResourceInventoryErrorCode code,
    const std::string_view context)
{
    const auto bounded_size = (std::min)(
        context.size(), kLocalResourceInventoryDiagnosticTextLimit);
    return LocalResourceInventoryError{
        code,
        std::string{context.data(), bounded_size},
    };
}

[[nodiscard]] LocalResourceInventoryBuildResult failure(
    const LocalResourceInventoryErrorCode code,
    const std::string_view context)
{
    return LocalResourceInventoryBuildResult{
        std::nullopt,
        bounded_error(code, context),
    };
}

[[nodiscard]] LocalResourceInventoryStatus classification_status(
    const GoldSrcResourceNameClassificationKind kind) noexcept
{
    switch (kind) {
    case GoldSrcResourceNameClassificationKind::mapped_file:
        return LocalResourceInventoryStatus::io_error;
    case GoldSrcResourceNameClassificationKind::metadata_only:
    case GoldSrcResourceNameClassificationKind::unsupported_mapping:
        return LocalResourceInventoryStatus::unsupported_mapping;
    case GoldSrcResourceNameClassificationKind::unsafe_name:
        return LocalResourceInventoryStatus::unsafe_name;
    case GoldSrcResourceNameClassificationKind::unsupported_name_encoding:
        return LocalResourceInventoryStatus::unsupported_name_encoding;
    }
    return LocalResourceInventoryStatus::unsupported_mapping;
}

[[nodiscard]] LocalResourceInventoryStatus virtual_name_error_status(
    const local::LocalVirtualResourceNameErrorCode code) noexcept
{
    switch (code) {
    case local::LocalVirtualResourceNameErrorCode::unsafe_name:
        return LocalResourceInventoryStatus::unsafe_name;
    case local::LocalVirtualResourceNameErrorCode::unsupported_name_encoding:
        return LocalResourceInventoryStatus::unsupported_name_encoding;
    }
    return LocalResourceInventoryStatus::io_error;
}

[[nodiscard]] LocalResourceInventoryStatus resolution_status(
    const local::LocalResourceResolutionCode code) noexcept
{
    switch (code) {
    case local::LocalResourceResolutionCode::resolved:
        return LocalResourceInventoryStatus::resolved;
    case local::LocalResourceResolutionCode::not_found:
        return LocalResourceInventoryStatus::missing;
    case local::LocalResourceResolutionCode::unsafe_name:
        return LocalResourceInventoryStatus::unsafe_name;
    case local::LocalResourceResolutionCode::unsupported_name_encoding:
        return LocalResourceInventoryStatus::unsupported_name_encoding;
    case local::LocalResourceResolutionCode::unsupported_resource_mapping:
        return LocalResourceInventoryStatus::unsupported_mapping;
    case local::LocalResourceResolutionCode::ambiguous_case:
        return LocalResourceInventoryStatus::ambiguous;
    case local::LocalResourceResolutionCode::not_regular_file:
    case local::LocalResourceResolutionCode::reparse_escape:
    case local::LocalResourceResolutionCode::remote_volume_unsupported:
    case local::LocalResourceResolutionCode::too_large:
    case local::LocalResourceResolutionCode::io_error:
        return LocalResourceInventoryStatus::io_error;
    }
    return LocalResourceInventoryStatus::io_error;
}

[[nodiscard]] bool valid_mapper_configuration(
    const GoldSrcResourceNameMapper& mapper) noexcept
{
    return mapper.profile() ==
               GoldSrcLocalResourceMappingProfile::stock_protocol_48_standard &&
           valid_goldsrc_resource_name_limits(mapper.limits());
}

[[nodiscard]] bool valid_resolver_configuration(
    const local::LocalResourceResolver& resolver) noexcept
{
    return resolver.root_count() > 0U &&
           valid_local_resource_resolver_limits(resolver.limits());
}

} // namespace

bool valid_local_resource_inventory_limits(
    const LocalResourceInventoryLimits& limits) noexcept
{
    return limits.maximum_entry_count > 0U &&
           limits.maximum_entry_count <=
               kMaximumLocalResourceInventoryEntries;
}

LocalResourceVirtualPathMetadata::LocalResourceVirtualPathMetadata(
    const local::LocalVirtualResourceId id,
    const std::size_t byte_length,
    const std::size_t component_count) noexcept
    : id_{id},
      byte_length_{byte_length},
      component_count_{component_count}
{
}

local::LocalVirtualResourceId LocalResourceVirtualPathMetadata::id()
    const noexcept
{
    return id_;
}

std::size_t LocalResourceVirtualPathMetadata::byte_length() const noexcept
{
    return byte_length_;
}

std::size_t LocalResourceVirtualPathMetadata::component_count() const noexcept
{
    return component_count_;
}

LocalResolvedResourceMetadata::LocalResolvedResourceMetadata(
    const local::LocalResourceRootId root_id,
    const std::uint64_t file_size,
    const local::LocalStableFileIdentity identity) noexcept
    : root_id_{root_id}, file_size_{file_size}, identity_{identity}
{
}

local::LocalResourceRootId LocalResolvedResourceMetadata::root_id()
    const noexcept
{
    return root_id_;
}

std::uint64_t LocalResolvedResourceMetadata::file_size() const noexcept
{
    return file_size_;
}

local::LocalStableFileIdentity LocalResolvedResourceMetadata::identity()
    const noexcept
{
    return identity_;
}

LocalResourceInventoryEntry::LocalResourceInventoryEntry(
    const std::size_t wire_ordinal,
    const ResourceType resource_type,
    const std::uint16_t resource_index,
    const std::size_t wire_name_byte_length,
    const LocalResourceInventoryStatus status,
    std::optional<LocalResourceVirtualPathMetadata> virtual_path,
    std::optional<LocalResolvedResourceMetadata> resolved_metadata) noexcept
    : wire_ordinal_{wire_ordinal},
      resource_type_{resource_type},
      resource_index_{resource_index},
      wire_name_byte_length_{wire_name_byte_length},
      status_{status},
      virtual_path_{std::move(virtual_path)},
      resolved_metadata_{std::move(resolved_metadata)}
{
}

std::size_t LocalResourceInventoryEntry::wire_ordinal() const noexcept
{
    return wire_ordinal_;
}

ResourceType LocalResourceInventoryEntry::resource_type() const noexcept
{
    return resource_type_;
}

std::uint16_t LocalResourceInventoryEntry::resource_index() const noexcept
{
    return resource_index_;
}

std::size_t LocalResourceInventoryEntry::wire_name_byte_length() const noexcept
{
    return wire_name_byte_length_;
}

LocalResourceInventoryStatus LocalResourceInventoryEntry::status()
    const noexcept
{
    return status_;
}

const std::optional<LocalResourceVirtualPathMetadata>&
LocalResourceInventoryEntry::virtual_path() const noexcept
{
    return virtual_path_;
}

const std::optional<LocalResolvedResourceMetadata>&
LocalResourceInventoryEntry::resolved_metadata() const noexcept
{
    return resolved_metadata_;
}

LocalResourceInventorySummary::LocalResourceInventorySummary(
    std::array<std::size_t, kLocalResourceInventoryStatusCount> counts) noexcept
    : counts_{counts},
      total_entry_count_{std::accumulate(
          counts_.begin(), counts_.end(), std::size_t{0U})}
{
}

std::size_t LocalResourceInventorySummary::total_entry_count() const noexcept
{
    return total_entry_count_;
}

std::size_t LocalResourceInventorySummary::count(
    const LocalResourceInventoryStatus status) const noexcept
{
    const auto index = status_index(status);
    return index ? counts_[*index] : 0U;
}

LocalResourceInventoryState::LocalResourceInventoryState(
    std::vector<LocalResourceInventoryEntry> entries,
    LocalResourceInventorySummary summary) noexcept
    : entries_{std::move(entries)}, summary_{summary}
{
}

std::span<const LocalResourceInventoryEntry>
LocalResourceInventoryState::entries() const noexcept
{
    return entries_;
}

std::size_t LocalResourceInventoryState::entry_count() const noexcept
{
    return entries_.size();
}

const LocalResourceInventorySummary& LocalResourceInventoryState::summary()
    const noexcept
{
    return summary_;
}

LocalResourceInventoryBuilder::LocalResourceInventoryBuilder(
    const LocalResourceInventoryLimits limits) noexcept
    : limits_{limits}
{
}

bool LocalResourceInventoryBuilder::valid_configuration() const noexcept
{
    return valid_local_resource_inventory_limits(limits_);
}

const LocalResourceInventoryLimits& LocalResourceInventoryBuilder::limits()
    const noexcept
{
    return limits_;
}

LocalResourceInventoryBuildResult LocalResourceInventoryBuilder::build(
    const ResourceListState& resource_list,
    const GoldSrcResourceNameMapper& mapper,
    const local::LocalResourceResolver& resolver) const
{
    if (!valid_configuration() || !valid_mapper_configuration(mapper) ||
        !valid_resolver_configuration(resolver)) {
        return failure(
            LocalResourceInventoryErrorCode::invalid_configuration,
            "Local resource inventory dependencies are invalid");
    }
    if (resource_list.resource_count() > limits_.maximum_entry_count) {
        return failure(
            LocalResourceInventoryErrorCode::entry_count_limit_exceeded,
            "Local resource inventory entry count exceeds its configured bound");
    }

    try {
        std::vector<LocalResourceInventoryEntry> entries;
        entries.reserve(resource_list.resource_count());
        std::array<std::size_t, kLocalResourceInventoryStatusCount> counts{};

        for (const auto& resource : resource_list.entries()) {
            const auto classification =
                mapper.classify(resource.type(), resource.name());

            LocalResourceInventoryStatus status =
                classification_status(classification.kind());
            std::optional<LocalResourceVirtualPathMetadata> virtual_path;
            std::optional<LocalResolvedResourceMetadata> resolved_metadata;

            if (classification.kind() ==
                GoldSrcResourceNameClassificationKind::mapped_file) {
                if (classification.safe_virtual_name()) {
                    auto local_name = local::LocalVirtualResourceName::create(
                        classification.safe_virtual_name()->value());
                    if (!local_name.name) {
                        status = local_name.error
                                     ? virtual_name_error_status(
                                           local_name.error->code)
                                     : LocalResourceInventoryStatus::io_error;
                    } else {
                        virtual_path.emplace(
                            LocalResourceVirtualPathMetadata{
                                local_name.name->id(),
                                local_name.name->value().size(),
                                local_name.name->component_count()});

                        auto resolution = resolver.resolve(*local_name.name);
                        status = resolution_status(resolution.code);
                        if (status == LocalResourceInventoryStatus::resolved) {
                            if (resolution.file &&
                                resolution.file->is_open() &&
                                resolution.file->is_regular_file() &&
                                resolution.file->identity().valid()) {
                                resolved_metadata.emplace(
                                    LocalResolvedResourceMetadata{
                                        resolution.file->root_id(),
                                        resolution.file->file_size(),
                                        resolution.file->identity()});
                            } else {
                                status = LocalResourceInventoryStatus::io_error;
                            }
                        }
                    }
                } else {
                    status = LocalResourceInventoryStatus::io_error;
                }
            }

            const auto index = status_index(status);
            if (!index) {
                return failure(
                    LocalResourceInventoryErrorCode::unable_to_retain_inventory,
                    "Local resource inventory status is invalid");
            }
            ++counts[*index];
            entries.emplace_back(LocalResourceInventoryEntry{
                resource.wire_ordinal(),
                resource.type(),
                resource.index().value(),
                resource.name().byte_length(),
                status,
                std::move(virtual_path),
                std::move(resolved_metadata)});
        }

        LocalResourceInventorySummary summary{counts};
        return LocalResourceInventoryBuildResult{
            LocalResourceInventoryState{
                std::move(entries), std::move(summary)},
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return failure(
            LocalResourceInventoryErrorCode::unable_to_retain_inventory,
            "Unable to allocate bounded local resource inventory metadata");
    } catch (...) {
        return failure(
            LocalResourceInventoryErrorCode::unable_to_retain_inventory,
            "Unable to build bounded local resource inventory metadata");
    }
}

} // namespace hlclient::goldsrc
