#pragma once

#include <hlclient/local_resources/local_read_only_file.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>

namespace hlclient::local_resources::detail {

class UniqueHandle final {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(const HANDLE handle) noexcept : handle_{handle} {}

    ~UniqueHandle() { reset(); }

    UniqueHandle(UniqueHandle&& other) noexcept
        : handle_{std::exchange(other.handle_, INVALID_HANDLE_VALUE)}
    {
    }

    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
        }
        return *this;
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
    }

    void reset() noexcept
    {
        if (*this) {
            static_cast<void>(::CloseHandle(handle_));
        }
        handle_ = INVALID_HANDLE_VALUE;
    }

private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

struct NativeFileIdentity {
    std::uint64_t volume{0U};
    std::array<std::byte, 16U> file{};

    friend bool operator==(
        const NativeFileIdentity&,
        const NativeFileIdentity&) noexcept = default;
};

struct NativeFileSnapshot {
    std::uint64_t size{0U};
    std::int64_t last_write_time{0};
    std::int64_t change_time{0};
    NativeFileIdentity identity;

    friend bool operator==(
        const NativeFileSnapshot&,
        const NativeFileSnapshot&) noexcept = default;
};

struct LocalResourceBaseStorage final {
    LocalResourceBaseStorage(
        std::wstring resolved_final_path,
        UniqueHandle base_handle,
        const NativeFileIdentity base_identity) noexcept
        : final_path{std::move(resolved_final_path)},
          handle{std::move(base_handle)},
          identity{base_identity}
    {
    }

    std::wstring final_path;
    UniqueHandle handle;
    NativeFileIdentity identity;
};

struct LocalResourceRootStorage final {
    LocalResourceRootStorage(
        LocalResourceRootId root_id,
        LocalResourceRootKind root_kind,
        std::filesystem::path resolved_path,
        std::wstring resolved_final_path,
        UniqueHandle root_handle,
        NativeFileIdentity root_identity) noexcept
        : id{root_id},
          kind{root_kind},
          native_path{std::move(resolved_path)},
          final_path{std::move(resolved_final_path)},
          handle{std::move(root_handle)},
          identity{root_identity}
    {
    }

    LocalResourceRootId id;
    LocalResourceRootKind kind{LocalResourceRootKind::game};
    std::filesystem::path native_path;
    std::wstring final_path;
    UniqueHandle handle;
    NativeFileIdentity identity;
};

struct LocalReadOnlyFileStorage final {
    LocalReadOnlyFileStorage(
        const LocalVirtualResourceId virtual_id,
        const LocalResourceRootId selected_root,
        std::wstring resolved_final_path,
        std::vector<UniqueHandle> retained_intermediate_handles,
        UniqueHandle file_handle,
        const NativeFileSnapshot snapshot) noexcept
        : virtual_resource_id{virtual_id},
          root_id{selected_root},
          final_path{std::move(resolved_final_path)},
          intermediate_handles{std::move(retained_intermediate_handles)},
          handle{std::move(file_handle)},
          initial_snapshot{snapshot}
    {
    }

    LocalVirtualResourceId virtual_resource_id;
    LocalResourceRootId root_id;
    std::wstring final_path;
    std::vector<UniqueHandle> intermediate_handles;
    UniqueHandle handle;
    NativeFileSnapshot initial_snapshot;
    std::uint64_t read_offset{0U};
};

#if defined(HLCLIENT_LOCAL_RESOURCE_TEST_ACCESS)
class LocalReadOnlyFileTestAccess final {
public:
    static void simulate_change_metadata(
        LocalReadOnlyFile& file) noexcept
    {
        if (file.storage_) {
            file.storage_->initial_snapshot.change_time ^= 1;
        }
    }

    [[nodiscard]] static bool seek_native_handle_to_end_without_tracking(
        LocalReadOnlyFile& file) noexcept
    {
        if (!file.storage_ || !file.storage_->handle) {
            return false;
        }
        LARGE_INTEGER offset{};
        return ::SetFilePointerEx(
                   file.storage_->handle.get(),
                   offset,
                   nullptr,
                   FILE_END) != FALSE;
    }
};
#endif

[[nodiscard]] inline NativeFileIdentity identity_from(
    const FILE_ID_INFO& information) noexcept
{
    NativeFileIdentity identity;
    identity.volume = information.VolumeSerialNumber;
    for (std::size_t index = 0U; index < identity.file.size(); ++index) {
        identity.file[index] =
            static_cast<std::byte>(information.FileId.Identifier[index]);
    }
    return identity;
}

[[nodiscard]] inline bool query_identity(
    const HANDLE handle,
    NativeFileIdentity& identity) noexcept
{
    FILE_ID_INFO information{};
    if (!::GetFileInformationByHandleEx(
            handle,
            FileIdInfo,
            &information,
            sizeof(information))) {
        return false;
    }
    identity = identity_from(information);
    return true;
}

[[nodiscard]] inline bool query_snapshot(
    const HANDLE handle,
    NativeFileSnapshot& snapshot) noexcept
{
    NativeFileIdentity identity{};
    FILE_STANDARD_INFO standard_information{};
    FILE_BASIC_INFO basic_information{};
    if (!query_identity(handle, identity) ||
        !::GetFileInformationByHandleEx(
            handle,
            FileStandardInfo,
            &standard_information,
            sizeof(standard_information)) ||
        !::GetFileInformationByHandleEx(
            handle,
            FileBasicInfo,
            &basic_information,
            sizeof(basic_information)) ||
        standard_information.EndOfFile.QuadPart < 0) {
        return false;
    }

    snapshot = NativeFileSnapshot{
        static_cast<std::uint64_t>(standard_information.EndOfFile.QuadPart),
        basic_information.LastWriteTime.QuadPart,
        basic_information.ChangeTime.QuadPart,
        identity,
    };
    return true;
}

[[nodiscard]] inline bool query_final_path(
    const HANDLE handle,
    std::wstring& path)
{
    constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
    const DWORD required = ::GetFinalPathNameByHandleW(handle, nullptr, 0U, flags);
    if (required == 0U) {
        return false;
    }
    std::wstring buffer(static_cast<std::size_t>(required) + 1U, L'\0');
    const DWORD written = ::GetFinalPathNameByHandleW(
        handle,
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        flags);
    if (written == 0U || written >= buffer.size()) {
        return false;
    }
    buffer.resize(written);
    path = std::move(buffer);
    return true;
}

[[nodiscard]] inline bool ordinal_equal_insensitive(
    const std::wstring_view left,
    const std::wstring_view right) noexcept
{
    if (left.size() != right.size() ||
        left.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    return ::CompareStringOrdinal(
               left.data(),
               static_cast<int>(left.size()),
               right.data(),
               static_cast<int>(right.size()),
               TRUE) == CSTR_EQUAL;
}

[[nodiscard]] inline bool final_path_is_within(
    std::wstring_view root,
    const std::wstring_view candidate) noexcept
{
    while (root.size() > 1U &&
           (root.back() == L'\\' || root.back() == L'/')) {
        root.remove_suffix(1U);
    }
    if (candidate.size() <= root.size() ||
        !ordinal_equal_insensitive(root, candidate.substr(0U, root.size()))) {
        return false;
    }
    return candidate[root.size()] == L'\\' || candidate[root.size()] == L'/';
}

[[nodiscard]] inline bool is_missing_error(const DWORD error) noexcept
{
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ||
           error == ERROR_INVALID_NAME;
}

} // namespace hlclient::local_resources::detail
