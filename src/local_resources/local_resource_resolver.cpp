#include <hlclient/local_resources/local_resource_resolver.hpp>

#include "win32_local_resource_detail.hpp"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace hlclient::local_resources {
namespace {

struct DirectoryLookupResult {
    enum class Code { found, not_found, ambiguous, limit_exceeded, io_error };

    Code code{Code::io_error};
    std::wstring actual_name;
    DWORD attributes{0U};
};

class ScopedFindHandle final {
public:
    explicit ScopedFindHandle(const HANDLE handle) noexcept : handle_{handle} {}
    ~ScopedFindHandle()
    {
        if (handle_ != INVALID_HANDLE_VALUE) {
            static_cast<void>(::FindClose(handle_));
        }
    }
    ScopedFindHandle(const ScopedFindHandle&) = delete;
    ScopedFindHandle& operator=(const ScopedFindHandle&) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }

private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

[[nodiscard]] wchar_t ascii_lower(const wchar_t value) noexcept
{
    return value >= L'A' && value <= L'Z'
               ? static_cast<wchar_t>(value + (L'a' - L'A'))
               : value;
}

[[nodiscard]] bool exact_ascii_name(
    const std::wstring_view actual,
    const std::string_view requested) noexcept
{
    if (actual.size() != requested.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < actual.size(); ++index) {
        if (actual[index] !=
            static_cast<wchar_t>(static_cast<unsigned char>(requested[index]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool insensitive_ascii_name(
    const std::wstring_view actual,
    const std::string_view requested) noexcept
{
    if (actual.size() != requested.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < actual.size(); ++index) {
        const auto wide = actual[index];
        if (wide < 0 || wide > 0x7f ||
            ascii_lower(wide) != ascii_lower(static_cast<wchar_t>(
                                     static_cast<unsigned char>(requested[index])))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::wstring append_component(
    const std::wstring_view directory,
    const std::wstring_view component)
{
    std::wstring result{directory};
    if (!result.empty() && result.back() != L'\\' && result.back() != L'/') {
        result.push_back(L'\\');
    }
    result.append(component);
    return result;
}

[[nodiscard]] DirectoryLookupResult find_component(
    const std::wstring_view directory,
    const std::string_view requested,
    const std::size_t entry_limit)
{
    const auto pattern = append_component(directory, L"*");
    WIN32_FIND_DATAW entry{};
    const HANDLE find_handle = ::FindFirstFileExW(
        pattern.c_str(),
        FindExInfoBasic,
        &entry,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH);
    if (find_handle == INVALID_HANDLE_VALUE) {
        const auto error = ::GetLastError();
        return DirectoryLookupResult{
            detail::is_missing_error(error) ? DirectoryLookupResult::Code::not_found
                                            : DirectoryLookupResult::Code::io_error,
            {},
            0U,
        };
    }
    ScopedFindHandle search{find_handle};

    std::optional<std::pair<std::wstring, DWORD>> insensitive_match;
    std::size_t insensitive_count = 0U;
    std::size_t visited = 0U;
    for (;;) {
        const std::wstring_view actual{entry.cFileName};
        if (actual != L"." && actual != L"..") {
            ++visited;
            if (visited > entry_limit) {
                return DirectoryLookupResult{
                    DirectoryLookupResult::Code::limit_exceeded, {}, 0U};
            }
            if (exact_ascii_name(actual, requested)) {
                return DirectoryLookupResult{
                    DirectoryLookupResult::Code::found,
                    std::wstring{actual},
                    entry.dwFileAttributes,
                };
            }
            if (insensitive_ascii_name(actual, requested)) {
                ++insensitive_count;
                if (insensitive_count == 1U) {
                    insensitive_match =
                        std::pair<std::wstring, DWORD>{
                            std::wstring{actual}, entry.dwFileAttributes};
                }
            }
        }

        if (!::FindNextFileW(search.get(), &entry)) {
            const auto error = ::GetLastError();
            if (error != ERROR_NO_MORE_FILES) {
                return DirectoryLookupResult{
                    DirectoryLookupResult::Code::io_error, {}, 0U};
            }
            break;
        }
    }

    if (insensitive_count > 1U) {
        return DirectoryLookupResult{
            DirectoryLookupResult::Code::ambiguous, {}, 0U};
    }
    if (!insensitive_match) {
        return DirectoryLookupResult{
            DirectoryLookupResult::Code::not_found, {}, 0U};
    }
    return DirectoryLookupResult{
        DirectoryLookupResult::Code::found,
        std::move(insensitive_match->first),
        insensitive_match->second,
    };
}

[[nodiscard]] std::vector<std::string_view> split_components(
    const std::string_view name)
{
    std::vector<std::string_view> components;
    std::size_t begin = 0U;
    for (std::size_t index = 0U; index <= name.size(); ++index) {
        if (index == name.size() || name[index] == '/') {
            components.push_back(name.substr(begin, index - begin));
            begin = index + 1U;
        }
    }
    return components;
}

[[nodiscard]] LocalResourceResolutionResult resolution_failure(
    const LocalResourceResolutionCode code,
    std::string context)
{
    return LocalResourceResolutionResult{code, std::nullopt, std::move(context)};
}

[[nodiscard]] LocalResourceResolutionResult lookup_failure(
    const DirectoryLookupResult::Code code)
{
    switch (code) {
    case DirectoryLookupResult::Code::not_found:
        return resolution_failure(
            LocalResourceResolutionCode::not_found,
            "Local resource was not found in this search root");
    case DirectoryLookupResult::Code::ambiguous:
        return resolution_failure(
            LocalResourceResolutionCode::ambiguous_case,
            "Local resource lookup has multiple ASCII case-insensitive matches");
    case DirectoryLookupResult::Code::limit_exceeded:
        return resolution_failure(
            LocalResourceResolutionCode::io_error,
            "Local resource directory scan exceeded its configured bound");
    case DirectoryLookupResult::Code::io_error:
        return resolution_failure(
            LocalResourceResolutionCode::io_error,
            "Unable to inspect a local resource directory");
    case DirectoryLookupResult::Code::found: break;
    }
    return resolution_failure(
        LocalResourceResolutionCode::io_error,
        "Local resource lookup produced an invalid state");
}

struct OpenIntermediateDirectoryResult {
    detail::UniqueHandle handle;
    LocalResourceResolutionCode code{LocalResourceResolutionCode::io_error};
    std::string context;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return static_cast<bool>(handle);
    }
};

[[nodiscard]] bool query_validated_root_path(
    const detail::LocalResourceRootStorage& root,
    std::wstring& final_path) noexcept
{
    BY_HANDLE_FILE_INFORMATION information{};
    detail::NativeFileIdentity identity{};
    return ::GetFileType(root.handle.get()) == FILE_TYPE_DISK &&
           ::GetFileInformationByHandle(root.handle.get(), &information) !=
               FALSE &&
           (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U &&
           (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ==
               0U &&
           detail::query_identity(root.handle.get(), identity) &&
           identity == root.identity &&
           detail::query_final_path(root.handle.get(), final_path);
}

[[nodiscard]] OpenIntermediateDirectoryResult open_intermediate_directory(
    const std::wstring& candidate,
    const detail::LocalResourceRootStorage& root,
    const bool exact_root_lookup)
{
    detail::UniqueHandle handle{::CreateFileW(
        candidate.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr)};
    if (!handle) {
        const auto open_error = ::GetLastError();
        return OpenIntermediateDirectoryResult{
            {},
            exact_root_lookup && detail::is_missing_error(open_error)
                ? LocalResourceResolutionCode::not_found
                : LocalResourceResolutionCode::io_error,
            exact_root_lookup && detail::is_missing_error(open_error)
                ? "An intermediate locator directory disappeared from its selected root"
                : "Intermediate local resource directory changed during lookup"};
    }

    BY_HANDLE_FILE_INFORMATION information{};
    detail::NativeFileIdentity identity{};
    if (::GetFileType(handle.get()) != FILE_TYPE_DISK ||
        !::GetFileInformationByHandle(handle.get(), &information) ||
        !detail::query_identity(handle.get(), identity)) {
        return OpenIntermediateDirectoryResult{
            {},
            LocalResourceResolutionCode::remote_volume_unsupported,
            "Intermediate local resource component is not on a local disk"};
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        return OpenIntermediateDirectoryResult{
            {},
            LocalResourceResolutionCode::reparse_escape,
            "Intermediate reparse points are not allowed in local resource paths"};
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
        return OpenIntermediateDirectoryResult{
            {},
            LocalResourceResolutionCode::not_regular_file,
            "An intermediate local resource component is not a directory"};
    }

    std::wstring final_path;
    if (identity.volume != root.identity.volume ||
        !detail::query_final_path(handle.get(), final_path)) {
        return OpenIntermediateDirectoryResult{
            {},
            LocalResourceResolutionCode::remote_volume_unsupported,
            "Intermediate local resource directory is not on its approved root volume"};
    }
    if (!detail::final_path_is_within(root.final_path, final_path)) {
        return OpenIntermediateDirectoryResult{
            {},
            LocalResourceResolutionCode::reparse_escape,
            "Intermediate local resource directory resolves outside its approved root"};
    }
    return OpenIntermediateDirectoryResult{
        std::move(handle), LocalResourceResolutionCode::resolved, {}};
}

} // namespace

bool valid_local_resource_resolver_limits(
    const LocalResourceResolverLimits& limits) noexcept
{
    return limits.maximum_file_size > 0U &&
           limits.maximum_file_size <= kHardMaximumLocalResourceFileSize &&
           limits.maximum_directory_entries_per_lookup > 0U &&
           limits.maximum_directory_entries_per_lookup <=
               kHardMaximumDirectoryEntriesPerLookup;
}

LocalResourceResolver::LocalResourceResolver(
    LocalResourceSearchRoots roots,
    const LocalResourceResolverLimits limits) noexcept
    : roots_{std::move(roots)}, limits_{limits}
{
}

LocalResourceResolver::~LocalResourceResolver() = default;
LocalResourceResolver::LocalResourceResolver(LocalResourceResolver&&) noexcept =
    default;
LocalResourceResolver& LocalResourceResolver::operator=(
    LocalResourceResolver&&) noexcept = default;

LocalResourceResolverCreateResult LocalResourceResolver::create(
    LocalResourceSearchRoots roots,
    const LocalResourceResolverLimits limits)
{
    if (roots.empty() || !valid_local_resource_resolver_limits(limits)) {
        return LocalResourceResolverCreateResult{
            nullptr,
            LocalResourceSearchRootsError{
                LocalResourceSearchRootsErrorCode::invalid_configuration,
                "Local resource resolver limits or roots are invalid"},
        };
    }
    try {
        return LocalResourceResolverCreateResult{
            std::unique_ptr<LocalResourceResolver>{
                new LocalResourceResolver{std::move(roots), limits}},
            std::nullopt,
        };
    } catch (...) {
        return LocalResourceResolverCreateResult{
            nullptr,
            LocalResourceSearchRootsError{
                LocalResourceSearchRootsErrorCode::io_error,
                "Unable to retain local resource resolver state"},
        };
    }
}

LocalResourceResolutionResult LocalResourceResolver::resolve(
    const std::string_view untrusted_name) const
{
    auto classified = LocalVirtualResourceName::create(untrusted_name);
    if (!classified) {
        const auto code =
            classified.error->code ==
                    LocalVirtualResourceNameErrorCode::unsupported_name_encoding
                ? LocalResourceResolutionCode::unsupported_name_encoding
                : LocalResourceResolutionCode::unsafe_name;
        return resolution_failure(code, classified.error->context);
    }
    return resolve(*classified.name);
}

LocalResourceResolutionResult LocalResourceResolver::resolve(
    const LocalVirtualResourceName& name) const
{
    return resolve_from_roots(name, std::nullopt);
}

LocalResourceResolutionResult LocalResourceResolver::resolve_exact_root(
    const LocalVirtualResourceName& name,
    const LocalResourceRootId root_id) const
{
    return resolve_from_roots(name, root_id);
}

LocalResourceResolutionResult LocalResourceResolver::resolve_from_roots(
    const LocalVirtualResourceName& name,
    const std::optional<LocalResourceRootId> exact_root_id) const
{
    std::vector<std::string_view> components;
    try {
        components = split_components(name.value());
    } catch (...) {
        return resolution_failure(
            LocalResourceResolutionCode::io_error,
            "Unable to retain bounded local resource lookup state");
    }

    bool exact_root_found = !exact_root_id.has_value();
    for (const auto& root_owner : roots_.roots_) {
        const auto& root = *root_owner;
        if (exact_root_id && root.id != *exact_root_id) {
            continue;
        }
        exact_root_found = true;
        std::wstring validated_root_path;
        if (!query_validated_root_path(root, validated_root_path) ||
            !detail::ordinal_equal_insensitive(
                validated_root_path, root.final_path)) {
            return resolution_failure(
                LocalResourceResolutionCode::io_error,
                "A validated local resource root moved or changed identity");
        }
        std::wstring candidate = validated_root_path;
        // Retaining restrictive handles prevents an inspected intermediate
        // directory from being renamed/replaced before the final handle opens.
        std::vector<detail::UniqueHandle> intermediate_handles;
        try {
            intermediate_handles.reserve(
                components.empty() ? 0U : components.size() - 1U);
        } catch (...) {
            return resolution_failure(
                LocalResourceResolutionCode::io_error,
                "Unable to retain bounded intermediate directory handles");
        }
        bool missing = false;
        for (std::size_t index = 0U; index < components.size(); ++index) {
            const auto found = find_component(
                candidate,
                components[index],
                limits_.maximum_directory_entries_per_lookup);
            if (found.code == DirectoryLookupResult::Code::not_found) {
                missing = true;
                break;
            }
            if (found.code != DirectoryLookupResult::Code::found) {
                return lookup_failure(found.code);
            }

            const bool final_component = index + 1U == components.size();
            if (!final_component &&
                (found.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
                // Explicit profile policy: every observed intermediate reparse
                // point is rejected, even if its target would remain contained.
                return resolution_failure(
                    LocalResourceResolutionCode::reparse_escape,
                    "Intermediate reparse points are not allowed in local resource paths");
            }
            if (!final_component &&
                (found.attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
                return resolution_failure(
                    LocalResourceResolutionCode::not_regular_file,
                    "An intermediate local resource component is not a directory");
            }
            candidate = append_component(candidate, found.actual_name);
            if (!final_component) {
                auto opened = open_intermediate_directory(
                    candidate, root, exact_root_id.has_value());
                if (!opened) {
                    return resolution_failure(opened.code, opened.context);
                }
                intermediate_handles.push_back(std::move(opened.handle));
            }
        }
        if (missing) {
            if (exact_root_id) {
                break;
            }
            continue;
        }

        detail::UniqueHandle handle{::CreateFileW(
            candidate.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS |
                FILE_FLAG_OPEN_REPARSE_POINT |
                FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr)};
        if (!handle) {
            const auto open_error = ::GetLastError();
            if (detail::is_missing_error(open_error)) {
                return resolution_failure(
                    exact_root_id
                        ? LocalResourceResolutionCode::not_found
                        : LocalResourceResolutionCode::io_error,
                    exact_root_id
                        ? "Local resource disappeared from its selected root during verified reopen"
                        : "Local resource changed during its bounded lookup");
            }
            return resolution_failure(
                LocalResourceResolutionCode::io_error,
                "Unable to open the local resource read-only handle");
        }

        BY_HANDLE_FILE_INFORMATION information{};
        if (::GetFileType(handle.get()) != FILE_TYPE_DISK ||
            !::GetFileInformationByHandle(handle.get(), &information)) {
            return resolution_failure(
                LocalResourceResolutionCode::remote_volume_unsupported,
                "Local resources require regular files on a local disk volume");
        }
        if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
            return resolution_failure(
                LocalResourceResolutionCode::reparse_escape,
                "Final reparse points are not allowed for local resources");
        }
        if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
            return resolution_failure(
                LocalResourceResolutionCode::not_regular_file,
                "Local resource target is not a regular file");
        }

        detail::NativeFileSnapshot snapshot{};
        std::wstring final_path;
        if (!detail::query_snapshot(handle.get(), snapshot) ||
            !detail::query_final_path(handle.get(), final_path)) {
            return resolution_failure(
                LocalResourceResolutionCode::io_error,
                "Unable to inspect the opened local resource handle");
        }
        if (snapshot.identity.volume != root.identity.volume) {
            return resolution_failure(
                LocalResourceResolutionCode::remote_volume_unsupported,
                "Local resource target is not on its approved root volume");
        }
        std::wstring final_root_path;
        if (!query_validated_root_path(root, final_root_path) ||
            !detail::ordinal_equal_insensitive(
                final_root_path, validated_root_path)) {
            return resolution_failure(
                LocalResourceResolutionCode::io_error,
                "A validated local resource root moved during lookup");
        }
        if (!detail::final_path_is_within(final_root_path, final_path)) {
            return resolution_failure(
                LocalResourceResolutionCode::reparse_escape,
                "Opened local resource resolves outside its approved root");
        }
        if (snapshot.size > limits_.maximum_file_size ||
            snapshot.size > (std::numeric_limits<std::uint32_t>::max)()) {
            return resolution_failure(
                LocalResourceResolutionCode::too_large,
                "Local resource exceeds the configured file-size bound");
        }

        try {
            auto storage =
                std::make_unique<detail::LocalReadOnlyFileStorage>(
                    name.id(),
                    root.id,
                    std::move(final_path),
                    std::move(intermediate_handles),
                    std::move(handle),
                    snapshot);
            return LocalResourceResolutionResult{
                LocalResourceResolutionCode::resolved,
                LocalReadOnlyFile{std::move(storage)},
                {},
            };
        } catch (...) {
            return resolution_failure(
                LocalResourceResolutionCode::io_error,
                "Unable to retain opened local resource state");
        }
    }

    if (!exact_root_found) {
        return resolution_failure(
            LocalResourceResolutionCode::io_error,
            "Selected local resource root is not available");
    }

    return resolution_failure(
        LocalResourceResolutionCode::not_found,
        exact_root_id
            ? "Local resource was not found in its selected search root"
            : "Local resource was not found in the configured search roots");
}

std::size_t LocalResourceResolver::root_count() const noexcept
{
    return roots_.size();
}

const LocalResourceResolverLimits& LocalResourceResolver::limits() const noexcept
{
    return limits_;
}

} // namespace hlclient::local_resources
