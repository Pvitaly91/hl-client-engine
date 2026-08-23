#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/local_resources/local_virtual_resource_name.hpp>

#include "win32_local_resource_detail.hpp"

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <utility>

namespace hlclient::local_resources {

namespace detail {
struct LocalResourceRootSetToken final {};
} // namespace detail

namespace {

[[nodiscard]] LocalResourceSearchRootsCreateResult failure(
    const LocalResourceSearchRootsErrorCode code,
    std::string context)
{
    return LocalResourceSearchRootsCreateResult{
        std::nullopt,
        LocalResourceSearchRootsError{code, std::move(context)},
    };
}

[[nodiscard]] bool has_embedded_nul(const std::wstring_view value) noexcept
{
    return value.find(L'\0') != std::wstring_view::npos;
}

[[nodiscard]] wchar_t ascii_lower(const wchar_t value) noexcept
{
    return value >= L'A' && value <= L'Z'
               ? static_cast<wchar_t>(value + (L'a' - L'A'))
               : value;
}

[[nodiscard]] bool starts_with_ascii_insensitive(
    const std::wstring_view value,
    const std::wstring_view prefix) noexcept
{
    return value.size() >= prefix.size() &&
           std::equal(
               prefix.begin(),
               prefix.end(),
               value.begin(),
               [](const wchar_t lhs, const wchar_t rhs) {
                   return ascii_lower(lhs) == ascii_lower(rhs);
               });
}

[[nodiscard]] bool is_explicit_local_drive_path(
    const std::filesystem::path& path) noexcept
{
    const std::wstring_view native{path.native()};
    if (native.empty() || has_embedded_nul(native) || native.size() < 3U) {
        return false;
    }
    if (starts_with_ascii_insensitive(native, L"\\\\?\\") ||
        starts_with_ascii_insensitive(native, L"\\\\.\\") ||
        starts_with_ascii_insensitive(native, L"\\??\\") ||
        starts_with_ascii_insensitive(native, L"\\\\")) {
        return false;
    }
    const bool drive_letter =
        ((native[0U] >= L'A' && native[0U] <= L'Z') ||
         (native[0U] >= L'a' && native[0U] <= L'z')) &&
        native[1U] == L':' &&
        (native[2U] == L'\\' || native[2U] == L'/');
    return drive_letter && path.is_absolute();
}

[[nodiscard]] std::wstring widen_ascii(const std::string_view value)
{
    std::wstring result;
    result.reserve(value.size());
    for (const unsigned char byte : value) {
        result.push_back(static_cast<wchar_t>(byte));
    }
    return result;
}

struct OpenDirectoryResult {
    detail::UniqueHandle handle;
    std::wstring final_path;
    detail::NativeFileIdentity identity;
    std::optional<LocalResourceSearchRootsErrorCode> error;
};

[[nodiscard]] OpenDirectoryResult open_local_directory(
    const std::filesystem::path& path)
{
    const auto& native = path.native();
    detail::UniqueHandle handle{::CreateFileW(
        native.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr)};
    if (!handle) {
        const auto error = ::GetLastError();
        return OpenDirectoryResult{
            {},
            {},
            {},
            detail::is_missing_error(error)
                ? LocalResourceSearchRootsErrorCode::missing_root
                : LocalResourceSearchRootsErrorCode::io_error,
        };
    }

    BY_HANDLE_FILE_INFORMATION information{};
    detail::NativeFileIdentity identity{};
    if (::GetFileType(handle.get()) != FILE_TYPE_DISK ||
        !::GetFileInformationByHandle(handle.get(), &information) ||
        !detail::query_identity(handle.get(), identity)) {
        return OpenDirectoryResult{
            {}, {}, {}, LocalResourceSearchRootsErrorCode::io_error};
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
        return OpenDirectoryResult{
            {}, {}, {}, LocalResourceSearchRootsErrorCode::not_directory};
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        return OpenDirectoryResult{
            {}, {}, {}, LocalResourceSearchRootsErrorCode::final_reparse_point};
    }

    std::wstring final_path;
    if (!detail::query_final_path(handle.get(), final_path)) {
        return OpenDirectoryResult{
            {}, {}, {}, LocalResourceSearchRootsErrorCode::io_error};
    }
    if (starts_with_ascii_insensitive(final_path, L"\\\\?\\UNC\\")) {
        return OpenDirectoryResult{
            {},
            {},
            {},
            LocalResourceSearchRootsErrorCode::remote_volume_unsupported,
        };
    }

    std::wstring volume_root;
    if (final_path.size() >= 7U &&
        starts_with_ascii_insensitive(final_path, L"\\\\?\\") &&
        ((final_path[4U] >= L'A' && final_path[4U] <= L'Z') ||
         (final_path[4U] >= L'a' && final_path[4U] <= L'z')) &&
        final_path[5U] == L':' && final_path[6U] == L'\\') {
        volume_root = {final_path[4U], L':', L'\\'};
    } else if (final_path.size() >= 3U &&
               ((final_path[0U] >= L'A' && final_path[0U] <= L'Z') ||
                (final_path[0U] >= L'a' && final_path[0U] <= L'z')) &&
               final_path[1U] == L':' && final_path[2U] == L'\\') {
        volume_root = {final_path[0U], L':', L'\\'};
    } else {
        return OpenDirectoryResult{
            {}, {}, {}, LocalResourceSearchRootsErrorCode::io_error};
    }
    if (::GetDriveTypeW(volume_root.c_str()) != DRIVE_FIXED) {
        return OpenDirectoryResult{
            {},
            {},
            {},
            LocalResourceSearchRootsErrorCode::remote_volume_unsupported,
        };
    }

    return OpenDirectoryResult{
        std::move(handle),
        std::move(final_path),
        identity,
        std::nullopt,
    };
}

[[nodiscard]] std::string root_error_context(
    const LocalResourceSearchRootsErrorCode code)
{
    switch (code) {
    case LocalResourceSearchRootsErrorCode::missing_root:
        return "A configured local resource root does not exist";
    case LocalResourceSearchRootsErrorCode::not_directory:
        return "A configured local resource root is not a directory";
    case LocalResourceSearchRootsErrorCode::final_reparse_point:
        return "A configured local resource root is a final reparse point";
    case LocalResourceSearchRootsErrorCode::escaped_base_directory:
        return "A configured local resource root resolves outside the base directory";
    case LocalResourceSearchRootsErrorCode::remote_volume_unsupported:
        return "Local resource roots require a fixed local volume";
    case LocalResourceSearchRootsErrorCode::invalid_configuration:
        return "Local resource root configuration is invalid";
    case LocalResourceSearchRootsErrorCode::invalid_base_directory:
        return "The Half-Life base directory must be an explicit local drive path";
    case LocalResourceSearchRootsErrorCode::io_error:
        return "Unable to validate a local resource root";
    }
    return "Unable to validate local resource roots";
}

} // namespace

LocalResourceSearchRoots::LocalResourceSearchRoots(
    std::unique_ptr<detail::LocalResourceBaseStorage> base,
    std::vector<std::unique_ptr<detail::LocalResourceRootStorage>> roots) noexcept
    : base_{std::move(base)}, roots_{std::move(roots)}
{
}

LocalResourceSearchRoots::~LocalResourceSearchRoots() = default;
LocalResourceSearchRoots::LocalResourceSearchRoots(
    LocalResourceSearchRoots&&) noexcept = default;
LocalResourceSearchRoots& LocalResourceSearchRoots::operator=(
    LocalResourceSearchRoots&&) noexcept = default;

std::size_t LocalResourceSearchRoots::size() const noexcept
{
    return roots_.size();
}

std::optional<LocalResourceSearchRootMetadata>
LocalResourceSearchRoots::metadata(const std::size_t index) const noexcept
{
    if (index >= roots_.size()) {
        return std::nullopt;
    }
    const auto& root = *roots_[index];
    return LocalResourceSearchRootMetadata{
        root.id,
        root.kind,
        LocalStableFileIdentity{root.identity.volume, root.identity.file},
    };
}

LocalResourceSearchRootsCreateResult LocalResourceSearchRoots::create(
    const std::filesystem::path& base_directory,
    const std::string_view game_directory)
{
    if (!is_explicit_local_drive_path(base_directory)) {
        return failure(
            LocalResourceSearchRootsErrorCode::invalid_base_directory,
            root_error_context(
                LocalResourceSearchRootsErrorCode::invalid_base_directory));
    }

    auto validated_game = LocalVirtualResourceName::create(game_directory);
    if (!validated_game || validated_game.name->component_count() != 1U) {
        return failure(
            LocalResourceSearchRootsErrorCode::invalid_configuration,
            root_error_context(
                LocalResourceSearchRootsErrorCode::invalid_configuration));
    }

    auto base = open_local_directory(base_directory);
    if (base.error) {
        const auto code =
            *base.error == LocalResourceSearchRootsErrorCode::missing_root
                ? LocalResourceSearchRootsErrorCode::invalid_base_directory
                : *base.error;
        return failure(code, root_error_context(code));
    }

    std::vector<std::pair<std::string, LocalResourceRootKind>> requests;
    try {
        requests.emplace_back(
            validated_game.name->value(), LocalResourceRootKind::game);
        requests.emplace_back("valve", LocalResourceRootKind::valve_fallback);
    } catch (...) {
        return failure(
            LocalResourceSearchRootsErrorCode::invalid_configuration,
            root_error_context(
                LocalResourceSearchRootsErrorCode::invalid_configuration));
    }

    std::vector<std::unique_ptr<detail::LocalResourceRootStorage>> roots;
    std::shared_ptr<const detail::LocalResourceRootSetToken> root_set_token;
    try {
        roots.reserve(2U);
        root_set_token =
            std::make_shared<const detail::LocalResourceRootSetToken>();
    } catch (...) {
        return failure(
            LocalResourceSearchRootsErrorCode::io_error,
            root_error_context(LocalResourceSearchRootsErrorCode::io_error));
    }
    for (const auto& [directory, kind] : requests) {
        const auto candidate = base_directory / widen_ascii(directory);
        auto opened = open_local_directory(candidate);
        if (opened.error) {
            return failure(*opened.error, root_error_context(*opened.error));
        }
        if (opened.identity.volume != base.identity.volume ||
            !detail::final_path_is_within(base.final_path, opened.final_path)) {
            return failure(
                LocalResourceSearchRootsErrorCode::escaped_base_directory,
                root_error_context(
                    LocalResourceSearchRootsErrorCode::escaped_base_directory));
        }

        const bool duplicate = std::ranges::any_of(
            roots,
            [&](const auto& existing) {
                return existing->identity == opened.identity;
            });
        if (duplicate) {
            continue;
        }
        if (roots.size() >= kMaximumLocalResourceSearchRoots) {
            return failure(
                LocalResourceSearchRootsErrorCode::invalid_configuration,
                root_error_context(
                    LocalResourceSearchRootsErrorCode::invalid_configuration));
        }

        try {
            const auto id = LocalResourceRootId{
                static_cast<std::uint32_t>(roots.size()), root_set_token};
            roots.push_back(
                std::make_unique<detail::LocalResourceRootStorage>(
                    id,
                    kind,
                    candidate,
                    std::move(opened.final_path),
                    std::move(opened.handle),
                    opened.identity));
        } catch (...) {
            return failure(
                LocalResourceSearchRootsErrorCode::io_error,
                root_error_context(LocalResourceSearchRootsErrorCode::io_error));
        }
    }

    if (roots.empty()) {
        return failure(
            LocalResourceSearchRootsErrorCode::invalid_configuration,
            root_error_context(
                LocalResourceSearchRootsErrorCode::invalid_configuration));
    }
    try {
        auto base_storage =
            std::make_unique<detail::LocalResourceBaseStorage>(
                std::move(base.final_path),
                std::move(base.handle),
                base.identity);
        return LocalResourceSearchRootsCreateResult{
            LocalResourceSearchRoots{
                std::move(base_storage), std::move(roots)},
            std::nullopt,
        };
    } catch (...) {
        return failure(
            LocalResourceSearchRootsErrorCode::io_error,
            root_error_context(LocalResourceSearchRootsErrorCode::io_error));
    }
}

} // namespace hlclient::local_resources
