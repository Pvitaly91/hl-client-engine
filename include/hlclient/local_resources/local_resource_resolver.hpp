#pragma once

#include <hlclient/local_resources/local_read_only_file.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/local_resources/local_virtual_resource_name.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::local_resources {

inline constexpr std::uint64_t kDefaultMaximumLocalResourceFileSize =
    16U * 1024U * 1024U;
inline constexpr std::uint64_t kHardMaximumLocalResourceFileSize =
    64U * 1024U * 1024U;
inline constexpr std::size_t kDefaultMaximumDirectoryEntriesPerLookup = 4'096U;
inline constexpr std::size_t kHardMaximumDirectoryEntriesPerLookup = 65'536U;

struct LocalResourceResolverLimits {
    std::uint64_t maximum_file_size{kDefaultMaximumLocalResourceFileSize};
    std::size_t maximum_directory_entries_per_lookup{
        kDefaultMaximumDirectoryEntriesPerLookup};
};

[[nodiscard]] bool valid_local_resource_resolver_limits(
    const LocalResourceResolverLimits& limits) noexcept;

enum class LocalResourceResolutionCode {
    resolved,
    not_found,
    unsafe_name,
    unsupported_name_encoding,
    unsupported_resource_mapping,
    ambiguous_case,
    not_regular_file,
    reparse_escape,
    remote_volume_unsupported,
    too_large,
    io_error,
};

struct LocalResourceResolutionResult {
    LocalResourceResolutionCode code{LocalResourceResolutionCode::io_error};
    std::optional<LocalReadOnlyFile> file;
    // Bounded, path-free diagnostic text.
    std::string context;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return code == LocalResourceResolutionCode::resolved && file.has_value();
    }
};

struct LocalResourceResolverCreateResult;

// Owns the validated root handles and resolves only classified virtual names.
// Exact ASCII spelling wins. If no exact entry exists, a bounded ASCII-only
// case-insensitive lookup is allowed; more than one such entry is ambiguous.
class LocalResourceResolver final {
public:
    [[nodiscard]] static LocalResourceResolverCreateResult create(
        LocalResourceSearchRoots roots,
        LocalResourceResolverLimits limits = {});

    ~LocalResourceResolver();
    LocalResourceResolver(LocalResourceResolver&&) noexcept;
    LocalResourceResolver& operator=(LocalResourceResolver&&) noexcept;
    LocalResourceResolver(const LocalResourceResolver&) = delete;
    LocalResourceResolver& operator=(const LocalResourceResolver&) = delete;

    [[nodiscard]] LocalResourceResolutionResult resolve(
        const LocalVirtualResourceName& name) const;
    [[nodiscard]] LocalResourceResolutionResult resolve(
        std::string_view untrusted_name) const;

    [[nodiscard]] std::size_t root_count() const noexcept;
    [[nodiscard]] const LocalResourceResolverLimits& limits() const noexcept;

private:
    LocalResourceResolver(
        LocalResourceSearchRoots roots,
        LocalResourceResolverLimits limits) noexcept;

    LocalResourceSearchRoots roots_;
    LocalResourceResolverLimits limits_;
};

struct LocalResourceResolverCreateResult {
    std::unique_ptr<LocalResourceResolver> resolver;
    std::optional<LocalResourceSearchRootsError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return resolver != nullptr;
    }
};

[[nodiscard]] constexpr std::string_view to_string(
    const LocalResourceResolutionCode code) noexcept
{
    switch (code) {
    case LocalResourceResolutionCode::resolved: return "resolved";
    case LocalResourceResolutionCode::not_found: return "not_found";
    case LocalResourceResolutionCode::unsafe_name: return "unsafe_name";
    case LocalResourceResolutionCode::unsupported_name_encoding:
        return "unsupported_name_encoding";
    case LocalResourceResolutionCode::unsupported_resource_mapping:
        return "unsupported_resource_mapping";
    case LocalResourceResolutionCode::ambiguous_case: return "ambiguous_case";
    case LocalResourceResolutionCode::not_regular_file:
        return "not_regular_file";
    case LocalResourceResolutionCode::reparse_escape: return "reparse_escape";
    case LocalResourceResolutionCode::remote_volume_unsupported:
        return "remote_volume_unsupported";
    case LocalResourceResolutionCode::too_large: return "too_large";
    case LocalResourceResolutionCode::io_error: return "io_error";
    }
    return "unknown";
}

} // namespace hlclient::local_resources
