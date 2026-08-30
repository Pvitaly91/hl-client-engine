#include <hlclient/platform/windows/secure_output.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace hlclient::platform::windows {
namespace {

class UniqueHandle final {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(HANDLE value) noexcept : value_{value} {}
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept
        : value_{std::exchange(other.value_, INVALID_HANDLE_VALUE)}
    {
    }
    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other) {
            reset();
            value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE);
        }
        return *this;
    }
    ~UniqueHandle() { reset(); }

    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    [[nodiscard]] bool valid() const noexcept
    {
        return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
    }
    [[nodiscard]] HANDLE release() noexcept
    {
        return std::exchange(value_, INVALID_HANDLE_VALUE);
    }
    void reset() noexcept
    {
        if (valid()) ::CloseHandle(value_);
        value_ = INVALID_HANDLE_VALUE;
    }

private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

[[nodiscard]] SecureOutputDirectoryOpenResult directory_failure(
    const SecureOutputErrorCode code,
    const DWORD os_error = 0U) noexcept
{
    return {std::nullopt, SecureOutputError{code, os_error}};
}

[[nodiscard]] SecureOutputWriteResult write_failure(
    const SecureOutputErrorCode code,
    const DWORD os_error = 0U) noexcept
{
    return {SecureOutputError{code, os_error}};
}

[[nodiscard]] bool valid_leaf_name(const std::wstring_view name) noexcept
{
    if (name.empty() || name.size() > 160U || name == L"." || name == L"..") {
        return false;
    }
    return std::ranges::all_of(name, [](const wchar_t value) {
        return value >= 0x20 && value != L'/' && value != L'\\' &&
               value != L':' && value != L'*' && value != L'?' &&
               value != L'\"' && value != L'<' && value != L'>' &&
               value != L'|';
    });
}

[[nodiscard]] bool ordinary_unlinked_file(
    const HANDLE handle,
    const std::uint64_t expected_size) noexcept
{
    BY_HANDLE_FILE_INFORMATION information{};
    if (!::GetFileInformationByHandle(handle, &information) ||
        (information.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0U ||
        information.nNumberOfLinks != 1U) {
        return false;
    }
    const auto size =
        (static_cast<std::uint64_t>(information.nFileSizeHigh) << 32U) |
        information.nFileSizeLow;
    return size == expected_size;
}

[[nodiscard]] bool ordinary_directory(const HANDLE handle) noexcept
{
    BY_HANDLE_FILE_INFORMATION information{};
    return ::GetFileInformationByHandle(handle, &information) != FALSE &&
           (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U &&
           (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U;
}

[[nodiscard]] bool exact_opened_path(
    const HANDLE handle, const std::filesystem::path& expected) noexcept
{
    const auto required = ::GetFinalPathNameByHandleW(
        handle, nullptr, 0U, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (required == 0U) return false;
    std::wstring opened(required, L'\0');
    const auto written = ::GetFinalPathNameByHandleW(
        handle, opened.data(), required,
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (written == 0U || written >= required) return false;
    opened.resize(written);
    constexpr std::wstring_view extended_prefix{L"\\\\?\\"};
    if (opened.starts_with(extended_prefix)) {
        opened.erase(0U, extended_prefix.size());
    }
    const auto expected_name = expected.native();
    return opened.size() == expected_name.size() &&
           ::CompareStringOrdinal(
               opened.data(), static_cast<int>(opened.size()),
               expected_name.data(), static_cast<int>(expected_name.size()),
               TRUE) == CSTR_EQUAL;
}

[[nodiscard]] std::optional<std::filesystem::path> canonical_directory_path(
    const std::filesystem::path& path) noexcept
{
    try {
        if (path.empty() || !path.is_absolute()) return std::nullopt;
        std::error_code error;
        auto canonical = std::filesystem::canonical(path, error);
        if (error || canonical.empty()) return std::nullopt;
        return canonical.lexically_normal();
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] bool same_path_ordinal_ignore_case(
    const std::filesystem::path& left,
    const std::filesystem::path& right) noexcept
{
    const auto& left_name = left.native();
    const auto& right_name = right.native();
    return left_name.size() == right_name.size() &&
           left_name.size() <=
               static_cast<std::size_t>((std::numeric_limits<int>::max)()) &&
           ::CompareStringOrdinal(
               left_name.data(), static_cast<int>(left_name.size()),
               right_name.data(), static_cast<int>(right_name.size()),
               TRUE) == CSTR_EQUAL;
}

[[nodiscard]] std::optional<std::wstring> random_leaf(
    const std::wstring_view prefix,
    const std::wstring_view suffix) noexcept
{
    std::array<unsigned char, 16U> random{};
    const auto status = ::BCryptGenRandom(
        nullptr, random.data(), static_cast<ULONG>(random.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) return std::nullopt;

    constexpr wchar_t hexadecimal[] = L"0123456789abcdef";
    std::wstring name;
    try {
        name.assign(prefix);
        name.reserve(name.size() + random.size() * 2U + suffix.size());
        for (const auto value : random) {
            name.push_back(hexadecimal[value >> 4U]);
            name.push_back(hexadecimal[value & 0x0fU]);
        }
        name += suffix;
    } catch (...) {
        return std::nullopt;
    }
    return name;
}

[[nodiscard]] bool write_all(
    const HANDLE handle, const std::span<const std::byte> bytes) noexcept
{
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto request = static_cast<DWORD>((std::min)(
            remaining,
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD written = 0U;
        if (!::WriteFile(handle, bytes.data() + offset, request, &written,
                         nullptr) ||
            written != request) {
            return false;
        }
        offset += written;
    }
    return true;
}

[[nodiscard]] bool rename_open_file_without_replace(
    const HANDLE handle,
    const std::wstring_view destination_path) noexcept
{
    if (destination_path.size() >
        ((std::numeric_limits<DWORD>::max)() / sizeof(wchar_t))) {
        ::SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return false;
    }
    const auto name_bytes = destination_path.size() * sizeof(wchar_t);
    const auto buffer_size = sizeof(FILE_RENAME_INFO) + name_bytes;
    std::vector<std::byte> buffer;
    try {
        buffer.resize(buffer_size);
    } catch (...) {
        ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    auto* information = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    information->ReplaceIfExists = FALSE;
    information->RootDirectory = nullptr;
    information->FileNameLength = static_cast<DWORD>(name_bytes);
    std::copy(destination_path.begin(), destination_path.end(),
              information->FileName);
    return ::SetFileInformationByHandle(
               handle, FileRenameInfo, information,
               static_cast<DWORD>(buffer.size())) != FALSE;
}

void delete_open_file_on_close(const HANDLE handle) noexcept
{
    FILE_DISPOSITION_INFO disposition{};
    disposition.DeleteFile = TRUE;
    static_cast<void>(::SetFileInformationByHandle(
        handle, FileDispositionInfo, &disposition, sizeof(disposition)));
}

} // namespace

SecureOutputDirectory::SecureOutputDirectory(
    std::vector<void*> native_handles,
    std::filesystem::path canonical_path) noexcept
    : native_handles_{std::move(native_handles)},
      canonical_path_{std::move(canonical_path)}
{
}

SecureOutputDirectory::SecureOutputDirectory(
    SecureOutputDirectory&& other) noexcept
    : native_handles_{std::move(other.native_handles_)},
      canonical_path_{std::move(other.canonical_path_)}
{
}

SecureOutputDirectory& SecureOutputDirectory::operator=(
    SecureOutputDirectory&& other) noexcept
{
    if (this != &other) {
        close();
        native_handles_ = std::move(other.native_handles_);
        canonical_path_ = std::move(other.canonical_path_);
    }
    return *this;
}

SecureOutputDirectory::~SecureOutputDirectory() { close(); }

bool SecureOutputDirectory::valid() const noexcept
{
    return native_handles_.size() == 2U &&
           std::ranges::all_of(native_handles_, [](const void* handle) {
               return handle != nullptr &&
                      handle != reinterpret_cast<const void*>(
                                    static_cast<std::intptr_t>(-1));
           });
}

const std::filesystem::path& SecureOutputDirectory::canonical_path() const noexcept
{
    return canonical_path_;
}

void SecureOutputDirectory::close() noexcept
{
    for (auto iterator = native_handles_.rbegin();
         iterator != native_handles_.rend(); ++iterator) {
        if (*iterator != nullptr &&
            *iterator != reinterpret_cast<void*>(
                             static_cast<std::intptr_t>(-1))) {
            ::CloseHandle(static_cast<HANDLE>(*iterator));
        }
    }
    native_handles_.clear();
    canonical_path_.clear();
}

SecureOutputDirectoryOpenResult open_secure_output_directory(
    const std::filesystem::path& path) noexcept
{
    const auto input_attributes = ::GetFileAttributesW(path.c_str());
    if (input_attributes == INVALID_FILE_ATTRIBUTES ||
        (input_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        return directory_failure(
            SecureOutputErrorCode::invalid_directory_path,
            ::GetLastError());
    }
    const auto canonical = canonical_directory_path(path);
    if (!canonical) {
        return directory_failure(
            SecureOutputErrorCode::invalid_directory_path);
    }
    try {
        std::error_code absolute_error;
        const auto absolute =
            std::filesystem::absolute(path, absolute_error).lexically_normal();
        if (absolute_error ||
            !same_path_ordinal_ignore_case(absolute, *canonical)) {
            return directory_failure(
                SecureOutputErrorCode::invalid_directory_path);
        }
    } catch (...) {
        return directory_failure(
            SecureOutputErrorCode::invalid_directory_path);
    }
    UniqueHandle directory{::CreateFileW(
        canonical->c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr)};
    if (!directory.valid()) {
        return directory_failure(
            SecureOutputErrorCode::directory_open_failed,
            ::GetLastError());
    }
    if (!ordinary_directory(directory.get()) ||
        !exact_opened_path(directory.get(), *canonical)) {
        return directory_failure(
            SecureOutputErrorCode::directory_identity_invalid,
            ::GetLastError());
    }

    std::filesystem::path lock_path;
    UniqueHandle lock;
    for (std::size_t attempt = 0U; attempt < 16U; ++attempt) {
        const auto lock_leaf = random_leaf(
            L".hlclient-output-capability-", L".lock");
        if (!lock_leaf) {
            return directory_failure(
                SecureOutputErrorCode::random_generation_failed);
        }
        try {
            lock_path = *canonical / *lock_leaf;
        } catch (...) {
            return directory_failure(
                SecureOutputErrorCode::directory_lock_failed,
                ERROR_NOT_ENOUGH_MEMORY);
        }
        lock = UniqueHandle{::CreateFileW(
            lock_path.c_str(), DELETE | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY |
                FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr)};
        if (lock.valid()) break;
        const auto error = ::GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
            return directory_failure(
                SecureOutputErrorCode::directory_lock_failed, error);
        }
    }
    if (!lock.valid()) {
        return directory_failure(
            SecureOutputErrorCode::directory_lock_failed,
            ERROR_FILE_EXISTS);
    }
    // The private child lock now blocks rename/replacement of the entire
    // ancestor chain. Revalidate both handles only after that barrier exists,
    // which also detects an ancestor swap during lock acquisition.
    if (!ordinary_unlinked_file(lock.get(), 0U) ||
        !exact_opened_path(lock.get(), lock_path) ||
        !ordinary_directory(directory.get()) ||
        !exact_opened_path(directory.get(), *canonical)) {
        return directory_failure(
            SecureOutputErrorCode::directory_identity_invalid,
            ::GetLastError());
    }
    std::vector<void*> held_handles;
    try {
        held_handles.reserve(2U);
        held_handles.push_back(static_cast<void*>(directory.release()));
        held_handles.push_back(static_cast<void*>(lock.release()));
    } catch (...) {
        return directory_failure(
            SecureOutputErrorCode::directory_lock_failed,
            ERROR_NOT_ENOUGH_MEMORY);
    }
    return {
        SecureOutputDirectory{
            std::move(held_handles), std::move(*canonical)},
        std::nullopt};
}

SecureOutputWriteResult secure_atomic_write_new(
    const SecureOutputDirectory& directory,
    const std::wstring_view leaf_name,
    const std::span<const std::byte> bytes) noexcept
{
    if (!directory.valid() ||
        !ordinary_directory(
            static_cast<HANDLE>(directory.native_handles_.front())) ||
        !exact_opened_path(
            static_cast<HANDLE>(directory.native_handles_.front()),
            directory.canonical_path_)) {
        return write_failure(
            SecureOutputErrorCode::directory_identity_invalid,
            ::GetLastError());
    }
    if (!valid_leaf_name(leaf_name)) {
        return write_failure(SecureOutputErrorCode::invalid_leaf_name);
    }

    std::filesystem::path temporary_path;
    UniqueHandle temporary;
    for (std::size_t attempt = 0U; attempt < 16U; ++attempt) {
        const auto temporary_leaf = random_leaf(
            L".hlclient-stock-runtime-", L".tmp");
        if (!temporary_leaf) {
            return write_failure(
                SecureOutputErrorCode::random_generation_failed);
        }
        try {
            temporary_path = directory.canonical_path_ / *temporary_leaf;
        } catch (...) {
            return write_failure(
                SecureOutputErrorCode::temporary_file_create_failed,
                ERROR_NOT_ENOUGH_MEMORY);
        }
        temporary = UniqueHandle{::CreateFileW(
            temporary_path.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES |
                DELETE,
            FILE_SHARE_READ, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_OPEN_REPARSE_POINT |
                FILE_FLAG_WRITE_THROUGH,
            nullptr)};
        if (temporary.valid()) break;
        const auto error = ::GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
            return write_failure(
                SecureOutputErrorCode::temporary_file_create_failed, error);
        }
    }
    if (!temporary.valid()) {
        return write_failure(
            SecureOutputErrorCode::temporary_file_create_failed,
            ERROR_FILE_EXISTS);
    }
    if (!ordinary_unlinked_file(temporary.get(), 0U)) {
        const auto error = ::GetLastError();
        temporary.reset();
        ::DeleteFileW(temporary_path.c_str());
        return write_failure(
            SecureOutputErrorCode::temporary_file_identity_invalid, error);
    }
    if (!write_all(temporary.get(), bytes)) {
        const auto error = ::GetLastError();
        temporary.reset();
        ::DeleteFileW(temporary_path.c_str());
        return write_failure(SecureOutputErrorCode::write_failed, error);
    }
    if (!::FlushFileBuffers(temporary.get())) {
        const auto error = ::GetLastError();
        temporary.reset();
        ::DeleteFileW(temporary_path.c_str());
        return write_failure(SecureOutputErrorCode::flush_failed, error);
    }
    if (!ordinary_unlinked_file(temporary.get(), bytes.size()) ||
        !exact_opened_path(temporary.get(), temporary_path)) {
        const auto error = ::GetLastError();
        temporary.reset();
        ::DeleteFileW(temporary_path.c_str());
        return write_failure(
            SecureOutputErrorCode::temporary_file_identity_invalid, error);
    }

    std::filesystem::path destination;
    try {
        destination = directory.canonical_path_ / std::wstring{leaf_name};
    } catch (...) {
        temporary.reset();
        ::DeleteFileW(temporary_path.c_str());
        return write_failure(
            SecureOutputErrorCode::publish_failed, ERROR_NOT_ENOUGH_MEMORY);
    }
    if (!rename_open_file_without_replace(
            temporary.get(), destination.native())) {
        const auto error = ::GetLastError();
        temporary.reset();
        ::DeleteFileW(temporary_path.c_str());
        const auto destination_attributes =
            ::GetFileAttributesW(destination.c_str());
        const auto code = error == ERROR_ALREADY_EXISTS ||
                                  error == ERROR_FILE_EXISTS ||
                                  destination_attributes !=
                                      INVALID_FILE_ATTRIBUTES
                              ? SecureOutputErrorCode::destination_exists
                              : SecureOutputErrorCode::publish_failed;
        return write_failure(code, error);
    }
    if (!ordinary_unlinked_file(temporary.get(), bytes.size()) ||
        !exact_opened_path(temporary.get(), destination)) {
        delete_open_file_on_close(temporary.get());
        return write_failure(
            SecureOutputErrorCode::published_file_identity_invalid,
            ::GetLastError());
    }
    temporary.reset();
    return {std::nullopt};
}

} // namespace hlclient::platform::windows
