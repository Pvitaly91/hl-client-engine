#pragma once

#include <hlclient/goldsrc/local_resource_mapping.hpp>
#include <hlclient/goldsrc/resource_list.hpp>
#include <hlclient/local_resources/local_resource_identity.hpp>
#include <hlclient/local_resources/local_resource_resolver.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/local_resources/local_virtual_resource_name.hpp>

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
// Defined only by the test target. This narrow friendship lets correlation
// tests fabricate otherwise-unrepresentable corrupted snapshots without
// adding mutable or public construction APIs to production inventory state.
class LocalResourceInventoryCorruptionTestAccess;
}

inline constexpr std::size_t kDefaultMaximumLocalResourceInventoryEntries =
    1'024U;
inline constexpr std::size_t kMaximumLocalResourceInventoryEntries = 4'095U;
inline constexpr std::size_t kLocalResourceInventoryStatusCount = 7U;
inline constexpr std::size_t kLocalResourceInventoryDiagnosticTextLimit = 256U;

struct LocalResourceInventoryLimits {
    std::size_t maximum_entry_count{
        kDefaultMaximumLocalResourceInventoryEntries};
};

[[nodiscard]] bool valid_local_resource_inventory_limits(
    const LocalResourceInventoryLimits& limits) noexcept;

enum class LocalResourceInventoryStatus {
    resolved,
    missing,
    unsafe_name,
    unsupported_name_encoding,
    unsupported_mapping,
    ambiguous,
    io_error,
};

// Correlation metadata only. The virtual path spelling is deliberately not
// duplicated from the owning ResourceListState into the inventory.
class LocalResourceVirtualPathMetadata final {
public:
    LocalResourceVirtualPathMetadata(
        const LocalResourceVirtualPathMetadata&) = default;
    LocalResourceVirtualPathMetadata& operator=(
        const LocalResourceVirtualPathMetadata&) = delete;
    LocalResourceVirtualPathMetadata(
        LocalResourceVirtualPathMetadata&&) noexcept = default;
    LocalResourceVirtualPathMetadata& operator=(
        LocalResourceVirtualPathMetadata&&) noexcept = delete;
    ~LocalResourceVirtualPathMetadata() = default;

    [[nodiscard]] local_resources::LocalVirtualResourceId id() const noexcept;
    [[nodiscard]] std::size_t byte_length() const noexcept;
    [[nodiscard]] std::size_t component_count() const noexcept;

private:
    friend class LocalResourceInventoryBuilder;
    friend class detail::LocalResourceInventoryCorruptionTestAccess;

    LocalResourceVirtualPathMetadata(
        local_resources::LocalVirtualResourceId id,
        std::size_t byte_length,
        std::size_t component_count) noexcept;

    local_resources::LocalVirtualResourceId id_;
    std::size_t byte_length_{0U};
    std::size_t component_count_{0U};
};

// Handle-free, path-free metadata copied from one successfully opened local
// file. The identity token supports equality only.
class LocalResolvedResourceMetadata final {
public:
    LocalResolvedResourceMetadata(const LocalResolvedResourceMetadata&) =
        default;
    LocalResolvedResourceMetadata& operator=(
        const LocalResolvedResourceMetadata&) = delete;
    LocalResolvedResourceMetadata(LocalResolvedResourceMetadata&&) noexcept =
        default;
    LocalResolvedResourceMetadata& operator=(
        LocalResolvedResourceMetadata&&) noexcept = delete;
    ~LocalResolvedResourceMetadata() = default;

    [[nodiscard]] local_resources::LocalResourceRootId root_id() const noexcept;
    [[nodiscard]] std::uint64_t file_size() const noexcept;
    [[nodiscard]] local_resources::LocalStableFileIdentity identity()
        const noexcept;

private:
    friend class LocalResourceInventoryBuilder;
    friend class detail::LocalResourceInventoryCorruptionTestAccess;

    LocalResolvedResourceMetadata(
        local_resources::LocalResourceRootId root_id,
        std::uint64_t file_size,
        local_resources::LocalStableFileIdentity identity) noexcept;

    local_resources::LocalResourceRootId root_id_;
    std::uint64_t file_size_{0U};
    local_resources::LocalStableFileIdentity identity_;
};

class LocalResourceInventoryEntry final {
public:
    LocalResourceInventoryEntry(const LocalResourceInventoryEntry&) = default;
    LocalResourceInventoryEntry& operator=(
        const LocalResourceInventoryEntry&) = delete;
    LocalResourceInventoryEntry(LocalResourceInventoryEntry&&) noexcept =
        default;
    LocalResourceInventoryEntry& operator=(
        LocalResourceInventoryEntry&&) noexcept = delete;
    ~LocalResourceInventoryEntry() = default;

    [[nodiscard]] std::size_t wire_ordinal() const noexcept;
    [[nodiscard]] ResourceType resource_type() const noexcept;
    [[nodiscard]] std::uint16_t resource_index() const noexcept;
    [[nodiscard]] std::size_t wire_name_byte_length() const noexcept;
    [[nodiscard]] LocalResourceInventoryStatus status() const noexcept;
    [[nodiscard]] const std::optional<LocalResourceVirtualPathMetadata>&
    virtual_path() const noexcept;
    [[nodiscard]] const std::optional<LocalResolvedResourceMetadata>&
    resolved_metadata() const noexcept;

private:
    friend class LocalResourceInventoryBuilder;
    friend class detail::LocalResourceInventoryCorruptionTestAccess;

    LocalResourceInventoryEntry(
        std::size_t wire_ordinal,
        ResourceType resource_type,
        std::uint16_t resource_index,
        std::size_t wire_name_byte_length,
        LocalResourceInventoryStatus status,
        std::optional<LocalResourceVirtualPathMetadata> virtual_path,
        std::optional<LocalResolvedResourceMetadata> resolved_metadata) noexcept;

    std::size_t wire_ordinal_{0U};
    ResourceType resource_type_{ResourceType::sound};
    std::uint16_t resource_index_{0U};
    std::size_t wire_name_byte_length_{0U};
    LocalResourceInventoryStatus status_{
        LocalResourceInventoryStatus::io_error};
    std::optional<LocalResourceVirtualPathMetadata> virtual_path_;
    std::optional<LocalResolvedResourceMetadata> resolved_metadata_;
};

class LocalResourceInventorySummary final {
public:
    LocalResourceInventorySummary(const LocalResourceInventorySummary&) =
        default;
    LocalResourceInventorySummary& operator=(
        const LocalResourceInventorySummary&) = delete;
    LocalResourceInventorySummary(LocalResourceInventorySummary&&) noexcept =
        default;
    LocalResourceInventorySummary& operator=(
        LocalResourceInventorySummary&&) noexcept = delete;
    ~LocalResourceInventorySummary() = default;

    [[nodiscard]] std::size_t total_entry_count() const noexcept;
    [[nodiscard]] std::size_t count(
        LocalResourceInventoryStatus status) const noexcept;

private:
    friend class LocalResourceInventoryBuilder;
    friend class detail::LocalResourceInventoryCorruptionTestAccess;

    explicit LocalResourceInventorySummary(
        std::array<std::size_t, kLocalResourceInventoryStatusCount> counts)
        noexcept;

    std::array<std::size_t, kLocalResourceInventoryStatusCount> counts_{};
    std::size_t total_entry_count_{0U};
};

// Immutable owning metadata snapshot. It is an inventory/correlation result,
// not a readiness, download, cache, asset, or precache state.
class LocalResourceInventoryState final {
public:
    LocalResourceInventoryState(const LocalResourceInventoryState&) = default;
    LocalResourceInventoryState& operator=(
        const LocalResourceInventoryState&) = delete;
    LocalResourceInventoryState(LocalResourceInventoryState&&) noexcept =
        default;
    LocalResourceInventoryState& operator=(
        LocalResourceInventoryState&&) noexcept = delete;
    ~LocalResourceInventoryState() = default;

    [[nodiscard]] std::span<const LocalResourceInventoryEntry> entries()
        const noexcept;
    [[nodiscard]] std::size_t entry_count() const noexcept;
    [[nodiscard]] const LocalResourceInventorySummary& summary() const noexcept;

private:
    friend class LocalResourceInventoryBuilder;
    friend class detail::LocalResourceInventoryCorruptionTestAccess;

    LocalResourceInventoryState(
        std::vector<LocalResourceInventoryEntry> entries,
        LocalResourceInventorySummary summary) noexcept;

    std::vector<LocalResourceInventoryEntry> entries_;
    LocalResourceInventorySummary summary_;
};

enum class LocalResourceInventoryErrorCode {
    invalid_configuration,
    entry_count_limit_exceeded,
    unable_to_retain_inventory,
};

struct LocalResourceInventoryError {
    LocalResourceInventoryErrorCode code{
        LocalResourceInventoryErrorCode::invalid_configuration};
    std::string context;
};

struct LocalResourceInventoryBuildResult {
    std::optional<LocalResourceInventoryState> state;
    std::optional<LocalResourceInventoryError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

// Builds transactionally: per-entry classification/resolution failures are
// retained as normal statuses, while a fatal configuration/allocation failure
// publishes no partial state.
class LocalResourceInventoryBuilder final {
public:
    explicit LocalResourceInventoryBuilder(
        LocalResourceInventoryLimits limits = {}) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const LocalResourceInventoryLimits& limits() const noexcept;
    [[nodiscard]] LocalResourceInventoryBuildResult build(
        const ResourceListState& resource_list,
        const GoldSrcResourceNameMapper& mapper,
        const local_resources::LocalResourceResolver& resolver) const;

private:
    LocalResourceInventoryLimits limits_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const LocalResourceInventoryStatus status) noexcept
{
    switch (status) {
    case LocalResourceInventoryStatus::resolved: return "resolved";
    case LocalResourceInventoryStatus::missing: return "missing";
    case LocalResourceInventoryStatus::unsafe_name: return "unsafe_name";
    case LocalResourceInventoryStatus::unsupported_name_encoding:
        return "unsupported_name_encoding";
    case LocalResourceInventoryStatus::unsupported_mapping:
        return "unsupported_mapping";
    case LocalResourceInventoryStatus::ambiguous: return "ambiguous";
    case LocalResourceInventoryStatus::io_error: return "io_error";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const LocalResourceInventoryErrorCode code) noexcept
{
    switch (code) {
    case LocalResourceInventoryErrorCode::invalid_configuration:
        return "invalid_configuration";
    case LocalResourceInventoryErrorCode::entry_count_limit_exceeded:
        return "entry_count_limit_exceeded";
    case LocalResourceInventoryErrorCode::unable_to_retain_inventory:
        return "unable_to_retain_inventory";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
