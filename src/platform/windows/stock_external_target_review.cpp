#include <hlclient/platform/windows/stock_external_target_review.hpp>

#include <hlclient/hash/sha256.hpp>
#include <hlclient/platform/windows/secure_output.hpp>
#include <hlclient/platform/windows/stock_external_target_artifact.hpp>

#include <Windows.h>
#include <bcrypt.h>
#include <winioctl.h>
#include <winternl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cwctype>
#include <deque>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>
#include <vector>

namespace hlclient::platform::windows {
namespace {

constexpr std::string_view kExternalReviewImplementationProfileV1 =
    "hlclient.stock-external-target-review.windows-v1";
constexpr std::string_view kExternalReviewImplementationProfileV2 =
    "hlclient.stock-external-target-review.windows-v2";

class Handle final {
public:
    explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) noexcept : h_(h) {}
    ~Handle() { if (valid()) ::CloseHandle(h_); }
    Handle(Handle&& o) noexcept : h_(o.h_) { o.h_ = INVALID_HANDLE_VALUE; }
    Handle& operator=(Handle&& o) noexcept { std::swap(h_, o.h_); return *this; }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    [[nodiscard]] bool valid() const noexcept
    { return h_ != nullptr && h_ != INVALID_HANDLE_VALUE; }
    [[nodiscard]] HANDLE get() const noexcept { return h_; }
private:
    HANDLE h_;
};

class AlgorithmHandle final {
public:
    AlgorithmHandle() noexcept = default;
    ~AlgorithmHandle()
    {
        if (value_ != nullptr) {
            static_cast<void>(::BCryptCloseAlgorithmProvider(value_, 0U));
        }
    }
    AlgorithmHandle(const AlgorithmHandle&) = delete;
    AlgorithmHandle& operator=(const AlgorithmHandle&) = delete;
    [[nodiscard]] BCRYPT_ALG_HANDLE* put() noexcept { return &value_; }
    [[nodiscard]] BCRYPT_ALG_HANDLE get() const noexcept { return value_; }

private:
    BCRYPT_ALG_HANDLE value_{nullptr};
};

class HashHandle final {
public:
    HashHandle() noexcept = default;
    ~HashHandle()
    {
        if (value_ != nullptr) {
            static_cast<void>(::BCryptDestroyHash(value_));
        }
    }
    HashHandle(const HashHandle&) = delete;
    HashHandle& operator=(const HashHandle&) = delete;
    [[nodiscard]] BCRYPT_HASH_HANDLE* put() noexcept { return &value_; }
    [[nodiscard]] BCRYPT_HASH_HANDLE get() const noexcept { return value_; }

private:
    BCRYPT_HASH_HANDLE value_{nullptr};
};

struct Snapshot final {
    std::uint64_t volume{};
    std::array<std::byte, 16> id{};
    std::uint64_t size{};
    std::uint64_t write_time{};
    std::uint64_t change_time{};
    std::uint64_t creation_time{};
    std::uint32_t attributes{};
    std::uint32_t reparse_tag{};
    std::uint32_t links{};
    bool directory{false};
};

struct PinnedInventoryEntry final {
    Handle handle;
    Snapshot snapshot;
    std::filesystem::path relative_path;
};

struct PinnedDirectoryWitness final {
    HANDLE handle{INVALID_HANDLE_VALUE};
    std::vector<std::pair<std::wstring, std::uint32_t>> entries;
};

struct PinnedTargetInventory final {
    std::filesystem::path source_link_relative_path;
    HANDLE root_handle{INVALID_HANDLE_VALUE};
    Snapshot root_snapshot;
    std::vector<PinnedInventoryEntry> entries;
    std::vector<PinnedDirectoryWitness> directories;
};

struct Scan final {
    std::vector<StockExternalTargetReview> targets;
    std::vector<PinnedTargetInventory> pinned_target_inventories;
    Handle source_root_guard{INVALID_HANDLE_VALUE};
    std::vector<PinnedInventoryEntry> source_inventory_entries;
    std::vector<PinnedDirectoryWitness> source_directory_witnesses;
    std::vector<WindowsReparseTargetObservation> contained_target_pins;
    Snapshot source_snapshot{};
    std::filesystem::path source_final_path;
    std::string source_identity;
    std::string source_inventory_sha256;
    std::size_t source_entry_count{0U};
    std::uint64_t source_byte_count{0U};
    std::size_t internal_reparse_count{0U};
    std::size_t contained_target_count{0U};
    std::size_t hardlink_count{0U};
    std::size_t alternate_data_stream_count{0U};
    bool eligible{true};
};

[[nodiscard]] Handle open_path(const std::filesystem::path& path,
                               const bool no_follow,
                               const bool directory) noexcept
{
    DWORD flags = directory ? FILE_FLAG_BACKUP_SEMANTICS : 0U;
    if (no_follow) flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    return Handle{::CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | FILE_READ_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, flags | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
}

[[nodiscard]] Handle open_path_rename_guard(
    const std::filesystem::path& path, const bool no_follow,
    const bool directory) noexcept
{
    DWORD flags = directory ? FILE_FLAG_BACKUP_SEMANTICS : 0U;
    if (no_follow) flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    return Handle{::CreateFileW(
        path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, flags, nullptr)};
}

[[nodiscard]] Handle open_path_inventory_guard(
    const std::filesystem::path& path, const bool no_follow,
    const bool directory) noexcept
{
    DWORD flags = directory ? FILE_FLAG_BACKUP_SEMANTICS : 0U;
    if (no_follow) flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    return Handle{::CreateFileW(
        path.c_str(), FILE_READ_ATTRIBUTES |
                          (directory ? FILE_LIST_DIRECTORY : FILE_READ_DATA),
        FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        flags | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
}

[[nodiscard]] std::optional<Snapshot> snapshot(HANDLE handle) noexcept
{
    FILE_ID_INFO id{};
    FILE_BASIC_INFO basic{};
    FILE_STANDARD_INFO standard{};
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!::GetFileInformationByHandleEx(handle, FileIdInfo, &id, sizeof(id)) ||
        !::GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic)) ||
        !::GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof(standard)) ||
        !::GetFileInformationByHandleEx(
            handle, FileAttributeTagInfo, &tag, sizeof(tag)) ||
        standard.EndOfFile.QuadPart < 0 || standard.NumberOfLinks == 0U) {
        return std::nullopt;
    }
    Snapshot value{};
    value.volume = id.VolumeSerialNumber;
    std::copy_n(reinterpret_cast<const std::byte*>(id.FileId.Identifier),
                value.id.size(), value.id.begin());
    value.size = static_cast<std::uint64_t>(standard.EndOfFile.QuadPart);
    value.write_time = static_cast<std::uint64_t>(basic.LastWriteTime.QuadPart);
    value.change_time = static_cast<std::uint64_t>(basic.ChangeTime.QuadPart);
    value.creation_time = static_cast<std::uint64_t>(basic.CreationTime.QuadPart);
    value.attributes = tag.FileAttributes;
    value.reparse_tag = tag.ReparseTag;
    value.links = standard.NumberOfLinks;
    value.directory = standard.Directory != FALSE;
    return value;
}

[[nodiscard]] std::optional<std::filesystem::path> final_path(HANDLE handle)
{
    const DWORD needed = ::GetFinalPathNameByHandleW(handle, nullptr, 0U,
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (needed == 0U || needed > 32767U) return std::nullopt;
    std::wstring buffer(static_cast<std::size_t>(needed), L'\0');
    const DWORD written = ::GetFinalPathNameByHandleW(handle, buffer.data(), needed,
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (written == 0U || written >= needed) return std::nullopt;
    buffer.resize(written);
    constexpr std::wstring_view prefix = L"\\\\?\\";
    if (buffer.starts_with(prefix)) buffer.erase(0U, prefix.size());
    return std::filesystem::path{buffer};
}

[[nodiscard]] std::optional<std::filesystem::path> final_volume_guid_path(
    HANDLE handle)
{
    const DWORD needed = ::GetFinalPathNameByHandleW(
        handle, nullptr, 0U, FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
    if (needed == 0U || needed > 32'767U) return std::nullopt;
    std::wstring buffer(static_cast<std::size_t>(needed), L'\0');
    const DWORD written = ::GetFinalPathNameByHandleW(
        handle, buffer.data(), needed,
        FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
    if (written == 0U || written >= needed) return std::nullopt;
    buffer.resize(written);
    return std::filesystem::path{std::move(buffer)};
}

[[nodiscard]] std::optional<std::filesystem::path> physical_path(
    HANDLE handle)
{
    const DWORD needed = ::GetFinalPathNameByHandleW(
        handle, nullptr, 0U, FILE_NAME_NORMALIZED | VOLUME_NAME_NT);
    if (needed == 0U || needed > 32'767U) return std::nullopt;
    std::wstring buffer(static_cast<std::size_t>(needed), L'\0');
    const DWORD written = ::GetFinalPathNameByHandleW(
        handle, buffer.data(), needed,
        FILE_NAME_NORMALIZED | VOLUME_NAME_NT);
    if (written == 0U || written >= needed) return std::nullopt;
    buffer.resize(written);
    return std::filesystem::path{buffer}.lexically_normal();
}

[[nodiscard]] bool physical_locations_overlap(
    const Snapshot& left_snapshot, const std::filesystem::path& left_path,
    const Snapshot& right_snapshot,
    const std::filesystem::path& right_path) noexcept
{
    if (left_snapshot.volume != right_snapshot.volume) return false;
    const auto fold = [](std::wstring value) {
        std::ranges::transform(value, value.begin(), [](const wchar_t item) {
            return static_cast<wchar_t>(::towlower(item));
        });
        return value;
    };
    auto left = fold(left_path.native());
    auto right = fold(right_path.native());
    while (left.size() > 1U &&
           (left.back() == L'\\' || left.back() == L'/')) {
        left.pop_back();
    }
    while (right.size() > 1U &&
           (right.back() == L'\\' || right.back() == L'/')) {
        right.pop_back();
    }
    const auto contains = [](const std::wstring_view parent,
                             const std::wstring_view child) noexcept {
        return child == parent ||
               (child.size() > parent.size() &&
                child.starts_with(parent) &&
                (child[parent.size()] == L'\\' ||
                 child[parent.size()] == L'/'));
    };
    return contains(left, right) || contains(right, left);
}

[[nodiscard]] bool lexical_path_is_within(
    const std::filesystem::path& child,
    const std::filesystem::path& parent) noexcept
{
    try {
        const auto fold = [](std::wstring value) {
            std::ranges::transform(value, value.begin(), [](wchar_t item) {
                return static_cast<wchar_t>(std::towlower(item));
            });
            return value;
        };
        auto child_text = fold(child.lexically_normal().native());
        auto parent_text = fold(parent.lexically_normal().native());
        while (parent_text.size() > 3U &&
               (parent_text.back() == L'\\' || parent_text.back() == L'/')) {
            parent_text.pop_back();
        }
        if (child_text == parent_text) return true;
        return child_text.size() > parent_text.size() &&
               child_text.starts_with(parent_text) &&
               (child_text[parent_text.size()] == L'\\' ||
                child_text[parent_text.size()] == L'/');
    } catch (...) {
        return false;
    }
}

[[nodiscard]] std::string hex(std::span<const std::byte> bytes)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2U);
    for (const auto byte : bytes) {
        const auto v = std::to_integer<unsigned char>(byte);
        out.push_back(digits[v >> 4U]); out.push_back(digits[v & 15U]);
    }
    return out;
}

[[nodiscard]] std::string hash_text(const std::string_view text)
{
    const auto bytes = std::span{reinterpret_cast<const std::byte*>(text.data()), text.size()};
    const auto digest = hash::sha256(bytes);
    return digest ? hash::sha256_hex(*digest) : std::string{};
}

[[nodiscard]] std::string identity(const Snapshot& value)
{
    std::ostringstream s;
    s << value.volume << ':' << hex(value.id) << ':' << value.size << ':'
      << value.creation_time << ':' << value.write_time << ':'
      << value.change_time << ':' << value.attributes << ':'
      << value.reparse_tag << ':' << value.links << ':'
      << (value.directory ? 'd' : 'f');
    return hash_text(s.str());
}

[[nodiscard]] bool same_object_shape(const Snapshot& before,
                                     const Snapshot& after) noexcept
{
    return before.volume == after.volume && before.id == after.id &&
           before.reparse_tag == after.reparse_tag &&
           before.directory == after.directory;
}

[[nodiscard]] bool same_inventory_snapshot(const Snapshot& before,
                                           const Snapshot& after) noexcept
{
    return before.volume == after.volume && before.id == after.id &&
           before.size == after.size &&
           before.write_time == after.write_time &&
           before.change_time == after.change_time &&
           before.creation_time == after.creation_time &&
           before.attributes == after.attributes &&
           before.reparse_tag == after.reparse_tag &&
           before.links == after.links &&
           before.directory == after.directory;
}

[[nodiscard]] Handle open_relative_inventory_entry(
    HANDLE parent, const std::wstring_view leaf, const bool directory) noexcept
{
    if (parent == nullptr || parent == INVALID_HANDLE_VALUE || leaf.empty() ||
        leaf == L"." || leaf == L".." ||
        leaf.size() >
            (std::numeric_limits<USHORT>::max)() / sizeof(wchar_t) ||
        leaf.find_first_of(L"\\/") != std::wstring_view::npos) {
        ::SetLastError(ERROR_INVALID_NAME);
        return Handle{};
    }

    UNICODE_STRING name{};
    name.Length = static_cast<USHORT>(leaf.size() * sizeof(wchar_t));
    name.MaximumLength = name.Length;
    name.Buffer = const_cast<PWSTR>(leaf.data());
    OBJECT_ATTRIBUTES attributes{};
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = parent;
    attributes.Attributes = OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE;
    attributes.ObjectName = &name;
    IO_STATUS_BLOCK status_block{};
    HANDLE opened = INVALID_HANDLE_VALUE;
    const ACCESS_MASK desired = FILE_READ_ATTRIBUTES | SYNCHRONIZE |
                                (directory ? FILE_LIST_DIRECTORY
                                           : FILE_READ_DATA);
    const ULONG options = FILE_OPEN_REPARSE_POINT |
                          FILE_OPEN_FOR_BACKUP_INTENT |
                          FILE_SYNCHRONOUS_IO_NONALERT |
                          (directory ? FILE_DIRECTORY_FILE
                                     : FILE_NON_DIRECTORY_FILE);
    const NTSTATUS status = ::NtCreateFile(
        &opened, desired, &attributes, &status_block, nullptr,
        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN, options, nullptr,
        0U);
    if (status < 0) {
        if (opened != nullptr && opened != INVALID_HANDLE_VALUE) {
            static_cast<void>(::CloseHandle(opened));
        }
        ::SetLastError(::RtlNtStatusToDosError(status));
        return Handle{};
    }
    ::SetLastError(ERROR_SUCCESS);
    return Handle{opened};
}

struct DirectoryListing final {
    std::vector<std::pair<std::wstring, std::uint32_t>> entries;
    std::uint32_t native_error{ERROR_SUCCESS};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return native_error == ERROR_SUCCESS;
    }
};

[[nodiscard]] DirectoryListing enumerate_directory_handle(
    HANDLE directory, const std::size_t maximum_entries) noexcept
{
    DirectoryListing result{};
    if (directory == nullptr || directory == INVALID_HANDLE_VALUE ||
        maximum_entries == 0U) {
        result.native_error = ERROR_INVALID_PARAMETER;
        return result;
    }
    try {
        constexpr std::size_t buffer_bytes = 64U * 1'024U;
        std::vector<std::uint64_t> storage(
            (buffer_bytes + sizeof(std::uint64_t) - 1U) /
            sizeof(std::uint64_t));
        const auto capacity = storage.size() * sizeof(std::uint64_t);
        bool restart = true;
        for (;;) {
            const auto information_class =
                restart ? FileIdBothDirectoryRestartInfo
                        : FileIdBothDirectoryInfo;
            if (::GetFileInformationByHandleEx(
                    directory, information_class, storage.data(),
                    static_cast<DWORD>(capacity)) == FALSE) {
                const auto native_error = ::GetLastError();
                if (native_error == ERROR_NO_MORE_FILES) break;
                result.entries.clear();
                result.native_error = native_error;
                return result;
            }
            restart = false;
            std::size_t offset = 0U;
            for (;;) {
                constexpr auto fixed_bytes =
                    offsetof(FILE_ID_BOTH_DIR_INFO, FileName);
                if (offset > capacity || fixed_bytes > capacity - offset) {
                    result.entries.clear();
                    result.native_error = ERROR_INVALID_DATA;
                    return result;
                }
                const auto* entry = reinterpret_cast<
                    const FILE_ID_BOTH_DIR_INFO*>(
                    reinterpret_cast<const std::byte*>(storage.data()) +
                    offset);
                if ((entry->FileNameLength & 1U) != 0U ||
                    entry->FileNameLength > capacity - offset - fixed_bytes) {
                    result.entries.clear();
                    result.native_error = ERROR_INVALID_DATA;
                    return result;
                }
                const std::wstring_view name{
                    entry->FileName,
                    static_cast<std::size_t>(entry->FileNameLength) /
                        sizeof(wchar_t)};
                if (name != L"." && name != L"..") {
                    if (name.empty() || name.find(L'\0') !=
                                            std::wstring_view::npos ||
                        name.find_first_of(L"\\/") !=
                            std::wstring_view::npos) {
                        result.entries.clear();
                        result.native_error = ERROR_INVALID_DATA;
                        return result;
                    }
                    if (result.entries.size() >= maximum_entries) {
                        result.entries.clear();
                        result.native_error = ERROR_BUFFER_OVERFLOW;
                        return result;
                    }
                    result.entries.emplace_back(
                        std::wstring{name}, entry->FileAttributes);
                }
                if (entry->NextEntryOffset == 0U) break;
                if (entry->NextEntryOffset < fixed_bytes ||
                    entry->NextEntryOffset > capacity - offset) {
                    result.entries.clear();
                    result.native_error = ERROR_INVALID_DATA;
                    return result;
                }
                offset += entry->NextEntryOffset;
            }
        }
        std::ranges::sort(result.entries, {},
                          [](const auto& item) -> const std::wstring& {
                              return item.first;
                          });
        return result;
    } catch (...) {
        result.entries.clear();
        result.native_error = ERROR_NOT_ENOUGH_MEMORY;
        return result;
    }
}

[[nodiscard]] bool revalidate_pinned_target_inventory(
    const PinnedTargetInventory& inventory) noexcept
{
    const auto root_after = snapshot(inventory.root_handle);
    if (!root_after ||
        !same_inventory_snapshot(inventory.root_snapshot, *root_after)) {
        return false;
    }
    for (const auto& entry : inventory.entries) {
        const auto after = snapshot(entry.handle.get());
        if (!after || !same_inventory_snapshot(entry.snapshot, *after)) {
            return false;
        }
    }
    for (const auto& directory : inventory.directories) {
        const auto listing = enumerate_directory_handle(
            directory.handle, directory.entries.size() + 1U);
        if (!listing || listing.entries != directory.entries) return false;
    }
    return true;
}

[[nodiscard]] bool revalidate_pinned_source_inventory(
    const Scan& scan) noexcept
{
    if (!scan.source_root_guard.valid()) return false;
    const auto root_after = snapshot(scan.source_root_guard.get());
    if (!root_after ||
        !same_inventory_snapshot(scan.source_snapshot, *root_after)) {
        return false;
    }
    for (const auto& entry : scan.source_inventory_entries) {
        const auto after = snapshot(entry.handle.get());
        if (!after || !same_inventory_snapshot(entry.snapshot, *after)) {
            return false;
        }
    }
    for (const auto& directory : scan.source_directory_witnesses) {
        const auto listing = enumerate_directory_handle(
            directory.handle, directory.entries.size() + 1U);
        if (!listing || listing.entries != directory.entries) return false;
    }
    return true;
}

[[nodiscard]] std::string stable_directory_identity(const Snapshot& value)
{
    std::ostringstream stream;
    stream << "hlclient.external-review-directory.v1|" << value.volume << '|'
           << hex(value.id) << '|' << value.attributes << '|'
           << value.reparse_tag << '|' << (value.directory ? 'd' : 'f');
    return hash_text(stream.str());
}

[[nodiscard]] std::string wide_hex(const std::filesystem::path& path)
{
    const auto& value = path.native();
    return hex(std::span{reinterpret_cast<const std::byte*>(value.data()),
                         value.size() * sizeof(wchar_t)});
}

[[nodiscard]] std::optional<std::string> utf8(
    const std::wstring_view value) noexcept
{
    if (value.empty()) return std::string{};
    if (value.size() >
        static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return std::nullopt;
    }
    const int required = ::WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) return std::nullopt;
    std::string output(static_cast<std::size_t>(required), '\0');
    if (::WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), output.data(), required, nullptr,
            nullptr) != required) {
        return std::nullopt;
    }
    return output;
}

[[nodiscard]] StockExternalArtifactClassification artifact_classification(
    const StockExternalTargetClassification value) noexcept
{
    switch (value) {
    case StockExternalTargetClassification::
        eligible_non_executable_asset_tree:
        return StockExternalArtifactClassification::
            eligible_non_executable_asset_tree;
    case StockExternalTargetClassification::contains_executable_code:
        return StockExternalArtifactClassification::contains_executable_code;
    case StockExternalTargetClassification::contains_script_or_command:
        return StockExternalArtifactClassification::contains_script_or_command;
    case StockExternalTargetClassification::contains_mutable_user_state:
        return StockExternalArtifactClassification::contains_mutable_user_state;
    case StockExternalTargetClassification::another_application_tree:
        return StockExternalArtifactClassification::another_application_tree;
    case StockExternalTargetClassification::operating_system_tree:
        return StockExternalArtifactClassification::operating_system_tree;
    case StockExternalTargetClassification::temporary_or_cache_tree:
        return StockExternalArtifactClassification::temporary_or_cache_tree;
    case StockExternalTargetClassification::remote_or_device_target:
        return StockExternalArtifactClassification::remote_or_device_target;
    case StockExternalTargetClassification::nested_external_link:
        return StockExternalArtifactClassification::nested_external_link;
    case StockExternalTargetClassification::unsupported_reparse_topology:
        return StockExternalArtifactClassification::unsupported_reparse_topology;
    case StockExternalTargetClassification::content_limit_exceeded:
        return StockExternalArtifactClassification::content_limit_exceeded;
    case StockExternalTargetClassification::changed_during_review:
        return StockExternalArtifactClassification::changed_during_review;
    case StockExternalTargetClassification::unknown:
        return StockExternalArtifactClassification::unknown;
    }
    return StockExternalArtifactClassification::unknown;
}

[[nodiscard]] std::optional<StockExternalArtifactFileIdentity>
artifact_identity(const HANDLE handle, const Snapshot& value) noexcept
{
    const auto path = final_path(handle);
    if (!path) return std::nullopt;
    const auto encoded = utf8(path->native());
    if (!encoded) return std::nullopt;
    StockExternalArtifactFileIdentity output{};
    output.volume_serial_number = value.volume;
    output.file_id = value.id;
    output.final_path = *encoded;
    output.identity_sha256 = identity(value);
    output.reparse_tag = value.reparse_tag;
    output.directory = value.directory;
    return output;
}

[[nodiscard]] StockExternalArtifactInventory artifact_inventory(
    const std::size_t entry_count,
    const std::uint64_t byte_count,
    std::string digest,
    const std::size_t executable_count = 0U,
    const std::size_t script_count = 0U,
    const std::size_t mutable_count = 0U,
    const std::size_t nested_count = 0U)
{
    return {static_cast<std::uint64_t>(entry_count), byte_count,
            std::move(digest),
            static_cast<std::uint64_t>(executable_count),
            static_cast<std::uint64_t>(script_count),
            static_cast<std::uint64_t>(mutable_count),
            static_cast<std::uint64_t>(nested_count)};
}

[[nodiscard]] bool local_fixed(const std::filesystem::path& path) noexcept
{
    const auto root = path.root_path().native();
    return !root.empty() && ::GetDriveTypeW(root.c_str()) == DRIVE_FIXED;
}

[[nodiscard]] bool is_executable_extension(std::wstring extension)
{
    std::ranges::transform(extension, extension.begin(),
        [](const wchar_t value) { return static_cast<wchar_t>(::towlower(value)); });
    static const std::set<std::wstring> values{
        L".exe", L".dll", L".com", L".scr", L".cpl", L".sys",
        L".drv", L".ocx", L".asi", L".msi", L".msp", L".msix",
        L".appx", L".appxbundle", L".msu", L".application",
        L".gadget", L".jar", L".chm", L".lnk", L".url"};
    return values.contains(extension);
}

[[nodiscard]] bool is_script_extension(std::wstring extension)
{
    std::ranges::transform(extension, extension.begin(),
        [](const wchar_t value) { return static_cast<wchar_t>(::towlower(value)); });
    static const std::set<std::wstring> values{
        L".bat", L".cmd", L".ps1", L".psm1", L".psd1", L".vbs",
        L".vbe", L".js", L".jse", L".wsf", L".hta", L".py",
        L".sh", L".bash", L".zsh", L".cfg", L".rc"};
    return values.contains(extension);
}

[[nodiscard]] bool is_goldsrc_asset_extension(std::wstring extension)
{
    std::ranges::transform(extension, extension.begin(),
        [](const wchar_t value) { return static_cast<wchar_t>(::towlower(value)); });
    static const std::set<std::wstring> values{
        L".bsp", L".mdl", L".spr", L".wad", L".wav", L".mp3",
        L".bmp", L".tga", L".pcx", L".lmp", L".pal", L".fnt",
        L".avi", L".mpg", L".mpeg", L".res"};
    return values.contains(extension);
}

[[nodiscard]] std::wstring lowercase(std::wstring value)
{
    std::ranges::transform(value, value.begin(), [](const wchar_t item) {
        return static_cast<wchar_t>(::towlower(item));
    });
    return value;
}

[[nodiscard]] std::optional<std::wstring> steam_common_application(
    const std::filesystem::path& path)
{
    std::vector<std::wstring> components;
    for (const auto& part : path.lexically_normal()) {
        components.push_back(lowercase(part.native()));
    }
    for (std::size_t index = 0U; index + 2U < components.size(); ++index) {
        if (components[index] == L"steamapps" &&
            components[index + 1U] == L"common") {
            return components[index + 2U];
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<StockExternalTargetClassification>
path_classification(const std::filesystem::path& path)
{
    static const std::set<std::wstring> mutable_directories{
        L"save", L"saves", L"logs", L"screenshots", L"demos",
        L"userdata", L"downloaded", L"downloads", L"users",
        L"documents and settings", L"desktop", L"documents", L"pictures",
        L"music", L"videos", L"appdata", L"onedrive"};
    static const std::set<std::wstring> temporary_directories{
        L"temp", L"tmp", L"cache", L"caches", L"crash", L"crashes",
        L"crashdumps"};
    static const std::set<std::wstring> code_directories{
        L"dlls", L"cl_dlls", L"addons", L"metamod", L"plugins"};
    static const std::set<std::wstring> unrelated_directories{
        L"mods", L"mod", L"custom", L"tools", L"tool", L"runtime",
        L"runtimes"};
    for (const auto& part : path.lexically_normal()) {
        const auto text = lowercase(part.native());
        if (text == L"windows" || text == L"program files" ||
            text == L"program files (x86)") {
            return StockExternalTargetClassification::operating_system_tree;
        }
        if (text == L"workshop") {
            return StockExternalTargetClassification::another_application_tree;
        }
        if (temporary_directories.contains(text)) {
            return StockExternalTargetClassification::temporary_or_cache_tree;
        }
        if (mutable_directories.contains(text)) {
            return StockExternalTargetClassification::
                contains_mutable_user_state;
        }
        if (code_directories.contains(text)) {
            return StockExternalTargetClassification::contains_executable_code;
        }
        if (unrelated_directories.contains(text)) {
            return StockExternalTargetClassification::another_application_tree;
        }
    }
    const auto leaf = lowercase(path.filename().native());
    static const std::set<std::wstring> mutable_files{
        L"config.cfg", L"userconfig.cfg", L"autoexec.cfg", L"custom.hpk"};
    const auto extension = lowercase(path.extension().native());
    if (mutable_files.contains(leaf) || extension == L".dem" ||
        extension == L".dmp" || extension == L".mdmp") {
        return StockExternalTargetClassification::contains_mutable_user_state;
    }
    return std::nullopt;
}

[[nodiscard]] bool has_ads(const std::filesystem::path& path) noexcept
{
    WIN32_FIND_STREAM_DATA data{};
    const HANDLE find = ::FindFirstStreamW(path.c_str(), FindStreamInfoStandard, &data, 0U);
    if (find == INVALID_HANDLE_VALUE) return ::GetLastError() != ERROR_HANDLE_EOF;
    bool alternate = false;
    bool default_stream_seen = false;
    do {
        const std::wstring_view stream_name{data.cStreamName};
        if (stream_name != L"::$DATA" || default_stream_seen) {
            alternate = true;
            break;
        }
        default_stream_seen = true;
    } while (::FindNextStreamW(find, &data));
    const DWORD enumeration_error =
        alternate ? ERROR_SUCCESS : ::GetLastError();
    ::FindClose(find);
    if (alternate) return true;
    return enumeration_error != ERROR_HANDLE_EOF;
}

[[nodiscard]] std::optional<std::size_t> named_ads_count_handle(
    HANDLE handle, const std::size_t maximum_streams) noexcept
{
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE ||
        maximum_streams == 0U) {
        return std::nullopt;
    }
    try {
        constexpr std::size_t buffer_bytes = 64U * 1'024U;
        std::vector<std::uint64_t> storage(
            (buffer_bytes + sizeof(std::uint64_t) - 1U) /
            sizeof(std::uint64_t));
        const auto capacity = storage.size() * sizeof(std::uint64_t);
        if (::GetFileInformationByHandleEx(
                handle, FileStreamInfo, storage.data(),
                static_cast<DWORD>(capacity)) == FALSE) {
            // Ordinary directories with no named streams may report EOF.
            return ::GetLastError() == ERROR_HANDLE_EOF
                       ? std::optional<std::size_t>{0U}
                       : std::nullopt;
        }

        std::size_t offset = 0U;
        std::size_t streams = 0U;
        std::size_t named = 0U;
        for (;;) {
            constexpr std::size_t header =
                offsetof(FILE_STREAM_INFO, StreamName);
            if (offset > capacity || header > capacity - offset) {
                return std::nullopt;
            }
            const auto* info = reinterpret_cast<const FILE_STREAM_INFO*>(
                reinterpret_cast<const std::byte*>(storage.data()) + offset);
            const auto name_bytes =
                static_cast<std::size_t>(info->StreamNameLength);
            if ((name_bytes % sizeof(wchar_t)) != 0U ||
                name_bytes > capacity - offset - header ||
                ++streams > maximum_streams) {
                return std::nullopt;
            }
            const std::wstring_view name{
                info->StreamName, name_bytes / sizeof(wchar_t)};
            if (name != L"::$DATA") ++named;
            if (info->NextEntryOffset == 0U) break;
            const auto next =
                static_cast<std::size_t>(info->NextEntryOffset);
            if (next < header || next > capacity - offset) {
                return std::nullopt;
            }
            offset += next;
        }
        return named;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] bool read_hash(HANDLE file, const std::uint64_t size, std::string& out)
{
    LARGE_INTEGER zero{};
    if (::SetFilePointerEx(file, zero, nullptr, FILE_BEGIN) == FALSE) {
        return false;
    }
    AlgorithmHandle algorithm;
    if (::BCryptOpenAlgorithmProvider(
            algorithm.put(), BCRYPT_SHA256_ALGORITHM, nullptr, 0U) < 0) {
        return false;
    }
    DWORD object_size = 0U;
    DWORD returned = 0U;
    if (::BCryptGetProperty(
            algorithm.get(), BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
            &returned, 0U) < 0 ||
        returned != sizeof(object_size) || object_size == 0U ||
        object_size > 1U * 1'024U * 1'024U) {
        return false;
    }
    std::vector<UCHAR> object;
    try {
        object.resize(object_size);
    } catch (...) {
        return false;
    }
    HashHandle hash_handle;
    if (::BCryptCreateHash(
            algorithm.get(), hash_handle.put(), object.data(),
            static_cast<ULONG>(object.size()), nullptr, 0U, 0U) < 0) {
        return false;
    }
    std::array<UCHAR, 64U * 1'024U> buffer{};
    std::uint64_t total = 0U;
    for (;;) {
        DWORD read = 0U;
        if (::ReadFile(
                file, buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                nullptr) == FALSE) {
            return false;
        }
        if (read == 0U) break;
        if (total > size || read > size - total ||
            ::BCryptHashData(hash_handle.get(), buffer.data(), read, 0U) < 0) {
            return false;
        }
        total += read;
    }
    std::array<std::byte, 32U> digest{};
    if (total != size ||
        ::BCryptFinishHash(
            hash_handle.get(), reinterpret_cast<PUCHAR>(digest.data()),
            static_cast<ULONG>(digest.size()), 0U) < 0) {
        return false;
    }
    out = hex(digest);
    return true;
}

enum class ContentKind { ordinary, executable, script, text };

[[nodiscard]] ContentKind content_kind(HANDLE file) noexcept
{
    LARGE_INTEGER zero{};
    if (!::SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) {
        return ContentKind::executable;
    }
    const auto rewind = [&]() noexcept {
        return ::SetFilePointerEx(file, zero, nullptr, FILE_BEGIN) != FALSE;
    };
    try {
        std::array<unsigned char, 64U * 1'024U> bytes{};
        std::string tail;
        std::array<unsigned char, 19U> raw_tail{};
        std::size_t raw_tail_size = 0U;
        bool first_chunk = true;
        std::uint64_t total_bytes = 0U;
        std::uint64_t text_bytes = 0U;
        std::array<std::uint64_t, 2U> lane_bytes{};
        std::array<std::uint64_t, 2U> lane_zero_bytes{};
        std::array<std::uint64_t, 2U> lane_text_bytes{};
        bool utf8_valid = true;
        std::uint32_t utf8_codepoint = 0U;
        std::uint32_t utf8_minimum = 0U;
        std::uint32_t utf8_remaining = 0U;
        std::uint64_t utf8_codepoints = 0U;
        unsigned char utf16_first_byte = 0U;
        std::array<std::uint64_t, 2U> utf16_units{};
        std::array<std::uint64_t, 2U> utf16_text_units{};
        std::array<bool, 2U> utf16_invalid{};
        bool unicode_bom = false;
        for (;;) {
            DWORD read = 0U;
            if (!::ReadFile(file, bytes.data(),
                            static_cast<DWORD>(bytes.size()), &read, nullptr)) {
                static_cast<void>(rewind());
                return ContentKind::executable;
            }
            if (read == 0U) break;
            if (first_chunk) {
                first_chunk = false;
                unicode_bom =
                    (read >= 2U &&
                     ((bytes[0] == 0xffU && bytes[1] == 0xfeU) ||
                      (bytes[0] == 0xfeU && bytes[1] == 0xffU))) ||
                    (read >= 3U && bytes[0] == 0xefU && bytes[1] == 0xbbU &&
                     bytes[2] == 0xbfU) ||
                    (read >= 4U &&
                     ((bytes[0] == 0xffU && bytes[1] == 0xfeU &&
                       bytes[2] == 0U && bytes[3] == 0U) ||
                      (bytes[0] == 0U && bytes[1] == 0U &&
                       bytes[2] == 0xfeU && bytes[3] == 0xffU)));
                const bool archive_or_package =
                    (read >= 2U && bytes[0] == 'M' && bytes[1] == 'Z') ||
                    (read >= 4U && bytes[0] == 0x7fU && bytes[1] == 'E' &&
                     bytes[2] == 'L' && bytes[3] == 'F') ||
                    (read >= 4U &&
                     ((bytes[0] == 0xfeU && bytes[1] == 0xedU &&
                       bytes[2] == 0xfaU &&
                       (bytes[3] == 0xceU || bytes[3] == 0xcfU)) ||
                      ((bytes[0] == 0xceU || bytes[0] == 0xcfU) &&
                       bytes[1] == 0xfaU && bytes[2] == 0xedU &&
                       bytes[3] == 0xfeU) ||
                      (bytes[0] == 0xcaU && bytes[1] == 0xfeU &&
                       bytes[2] == 0xbaU && bytes[3] == 0xbeU) ||
                      (bytes[0] == 0xbeU && bytes[1] == 0xbaU &&
                       bytes[2] == 0xfeU && bytes[3] == 0xcaU))) ||
                    (read >= 20U && bytes[0] == 0x4cU && bytes[1] == 0x00U &&
                     bytes[2] == 0x00U && bytes[3] == 0x00U &&
                     bytes[4] == 0x01U && bytes[5] == 0x14U &&
                     bytes[6] == 0x02U && bytes[7] == 0x00U &&
                     bytes[8] == 0x00U && bytes[9] == 0x00U &&
                     bytes[10] == 0x00U && bytes[11] == 0x00U &&
                     bytes[12] == 0xc0U && bytes[13] == 0x00U &&
                     bytes[14] == 0x00U && bytes[15] == 0x00U &&
                     bytes[16] == 0x00U && bytes[17] == 0x00U &&
                     bytes[18] == 0x00U && bytes[19] == 0x46U) ||
                    (read >= 8U && bytes[0] == 0xd0U && bytes[1] == 0xcfU &&
                     bytes[2] == 0x11U && bytes[3] == 0xe0U &&
                     bytes[4] == 0xa1U && bytes[5] == 0xb1U &&
                     bytes[6] == 0x1aU && bytes[7] == 0xe1U) ||
                    (read >= 4U && bytes[0] == 'M' && bytes[1] == 'S' &&
                     bytes[2] == 'C' && bytes[3] == 'F') ||
                    (read >= 4U && bytes[0] == 'I' && bytes[1] == 'T' &&
                     bytes[2] == 'S' && bytes[3] == 'F') ||
                    (read >= 4U && bytes[0] == 'P' && bytes[1] == 'K' &&
                     (bytes[2] == 3U || bytes[2] == 5U || bytes[2] == 7U) &&
                     (bytes[3] == 4U || bytes[3] == 6U || bytes[3] == 8U)) ||
                    (read >= 6U && bytes[0] == 'R' && bytes[1] == 'a' &&
                     bytes[2] == 'r' && bytes[3] == '!' &&
                     bytes[4] == 0x1aU && bytes[5] == 0x07U) ||
                    (read >= 6U && bytes[0] == '7' && bytes[1] == 'z' &&
                     bytes[2] == 0xbcU && bytes[3] == 0xafU &&
                     bytes[4] == 0x27U && bytes[5] == 0x1cU) ||
                    (read >= 2U && bytes[0] == 0x1fU && bytes[1] == 0x8bU);
                if (archive_or_package) {
                    static_cast<void>(rewind());
                    return ContentKind::executable;
                }
            }

            const auto combined_byte = [&](const std::size_t index) noexcept {
                return index < raw_tail_size
                           ? raw_tail[index]
                           : bytes[index - raw_tail_size];
            };
            const auto combined_size =
                raw_tail_size + static_cast<std::size_t>(read);
            for (std::size_t index = 0U; index < combined_size; ++index) {
                const auto available = combined_size - index;
                const bool embedded_code_or_package =
                    (available >= 2U && combined_byte(index) == 'M' &&
                     combined_byte(index + 1U) == 'Z') ||
                    (available >= 4U && combined_byte(index) == 0x7fU &&
                     combined_byte(index + 1U) == 'E' &&
                     combined_byte(index + 2U) == 'L' &&
                     combined_byte(index + 3U) == 'F') ||
                    (available >= 4U &&
                     ((combined_byte(index) == 0xfeU &&
                       combined_byte(index + 1U) == 0xedU &&
                       combined_byte(index + 2U) == 0xfaU &&
                       (combined_byte(index + 3U) == 0xceU ||
                        combined_byte(index + 3U) == 0xcfU)) ||
                      ((combined_byte(index) == 0xceU ||
                        combined_byte(index) == 0xcfU) &&
                       combined_byte(index + 1U) == 0xfaU &&
                       combined_byte(index + 2U) == 0xedU &&
                       combined_byte(index + 3U) == 0xfeU) ||
                      (combined_byte(index) == 0xcaU &&
                       combined_byte(index + 1U) == 0xfeU &&
                       combined_byte(index + 2U) == 0xbaU &&
                       combined_byte(index + 3U) == 0xbeU) ||
                      (combined_byte(index) == 0xbeU &&
                       combined_byte(index + 1U) == 0xbaU &&
                       combined_byte(index + 2U) == 0xfeU &&
                       combined_byte(index + 3U) == 0xcaU))) ||
                    (available >= 20U &&
                     combined_byte(index) == 0x4cU &&
                     combined_byte(index + 1U) == 0x00U &&
                     combined_byte(index + 2U) == 0x00U &&
                     combined_byte(index + 3U) == 0x00U &&
                     combined_byte(index + 4U) == 0x01U &&
                     combined_byte(index + 5U) == 0x14U &&
                     combined_byte(index + 6U) == 0x02U &&
                     combined_byte(index + 7U) == 0x00U &&
                     combined_byte(index + 8U) == 0x00U &&
                     combined_byte(index + 9U) == 0x00U &&
                     combined_byte(index + 10U) == 0x00U &&
                     combined_byte(index + 11U) == 0x00U &&
                     combined_byte(index + 12U) == 0xc0U &&
                     combined_byte(index + 13U) == 0x00U &&
                     combined_byte(index + 14U) == 0x00U &&
                     combined_byte(index + 15U) == 0x00U &&
                     combined_byte(index + 16U) == 0x00U &&
                     combined_byte(index + 17U) == 0x00U &&
                     combined_byte(index + 18U) == 0x00U &&
                     combined_byte(index + 19U) == 0x46U) ||
                    (available >= 4U && combined_byte(index) == 'M' &&
                     combined_byte(index + 1U) == 'S' &&
                     combined_byte(index + 2U) == 'C' &&
                     combined_byte(index + 3U) == 'F') ||
                    (available >= 4U && combined_byte(index) == 'I' &&
                     combined_byte(index + 1U) == 'T' &&
                     combined_byte(index + 2U) == 'S' &&
                     combined_byte(index + 3U) == 'F') ||
                    (available >= 4U && combined_byte(index) == 'P' &&
                     combined_byte(index + 1U) == 'K' &&
                     (combined_byte(index + 2U) == 3U ||
                      combined_byte(index + 2U) == 5U ||
                      combined_byte(index + 2U) == 7U) &&
                     (combined_byte(index + 3U) == 4U ||
                      combined_byte(index + 3U) == 6U ||
                      combined_byte(index + 3U) == 8U)) ||
                    (available >= 8U && combined_byte(index) == 0xd0U &&
                     combined_byte(index + 1U) == 0xcfU &&
                     combined_byte(index + 2U) == 0x11U &&
                     combined_byte(index + 3U) == 0xe0U &&
                     combined_byte(index + 4U) == 0xa1U &&
                     combined_byte(index + 5U) == 0xb1U &&
                     combined_byte(index + 6U) == 0x1aU &&
                     combined_byte(index + 7U) == 0xe1U) ||
                    (available >= 6U && combined_byte(index) == 'R' &&
                     combined_byte(index + 1U) == 'a' &&
                     combined_byte(index + 2U) == 'r' &&
                     combined_byte(index + 3U) == '!' &&
                     combined_byte(index + 4U) == 0x1aU &&
                     combined_byte(index + 5U) == 0x07U) ||
                    (available >= 6U && combined_byte(index) == '7' &&
                     combined_byte(index + 1U) == 'z' &&
                     combined_byte(index + 2U) == 0xbcU &&
                     combined_byte(index + 3U) == 0xafU &&
                     combined_byte(index + 4U) == 0x27U &&
                     combined_byte(index + 5U) == 0x1cU) ||
                    (available >= 2U && combined_byte(index) == 0x1fU &&
                     combined_byte(index + 1U) == 0x8bU);
                if (embedded_code_or_package) {
                    static_cast<void>(rewind());
                    return ContentKind::executable;
                }
            }
            const auto next_raw_tail_size =
                (std::min)(raw_tail.size(), combined_size);
            std::array<unsigned char, 19U> next_raw_tail{};
            for (std::size_t index = 0U; index < next_raw_tail_size; ++index) {
                next_raw_tail[index] = combined_byte(
                    combined_size - next_raw_tail_size + index);
            }
            raw_tail = next_raw_tail;
            raw_tail_size = next_raw_tail_size;

            std::string normalized;
            normalized.reserve(static_cast<std::size_t>(read) + tail.size() + 1U);
            normalized = tail;
            if (normalized.empty()) normalized.push_back('\n');
            for (DWORD index = 0U; index < read; ++index) {
                const unsigned char value = bytes[index];
                const auto absolute_index = total_bytes;
                const auto lane =
                    static_cast<std::size_t>(absolute_index & 1U);
                ++total_bytes;
                ++lane_bytes[lane];
                if (value == 0U) ++lane_zero_bytes[lane];

                if (utf8_valid) {
                    if (utf8_remaining == 0U) {
                        if (value <= 0x7fU) {
                            if ((value < 0x20U && value != '\t' &&
                                 value != '\r' && value != '\n') ||
                                value == 0x7fU) {
                                utf8_valid = false;
                            } else {
                                ++utf8_codepoints;
                            }
                        } else if (value >= 0xc2U && value <= 0xdfU) {
                            utf8_codepoint = value & 0x1fU;
                            utf8_minimum = 0x80U;
                            utf8_remaining = 1U;
                        } else if (value >= 0xe0U && value <= 0xefU) {
                            utf8_codepoint = value & 0x0fU;
                            utf8_minimum = 0x800U;
                            utf8_remaining = 2U;
                        } else if (value >= 0xf0U && value <= 0xf4U) {
                            utf8_codepoint = value & 0x07U;
                            utf8_minimum = 0x10000U;
                            utf8_remaining = 3U;
                        } else {
                            utf8_valid = false;
                        }
                    } else if ((value & 0xc0U) != 0x80U) {
                        utf8_valid = false;
                    } else {
                        utf8_codepoint =
                            (utf8_codepoint << 6U) | (value & 0x3fU);
                        --utf8_remaining;
                        if (utf8_remaining == 0U) {
                            if (utf8_codepoint < utf8_minimum ||
                                utf8_codepoint > 0x10ffffU ||
                                (utf8_codepoint >= 0xd800U &&
                                 utf8_codepoint <= 0xdfffU)) {
                                utf8_valid = false;
                            } else {
                                ++utf8_codepoints;
                            }
                        }
                    }
                }

                if (lane == 0U) {
                    utf16_first_byte = value;
                } else {
                    const std::array<std::uint16_t, 2U> code_units{
                        static_cast<std::uint16_t>(
                            static_cast<std::uint16_t>(utf16_first_byte) |
                            (static_cast<std::uint16_t>(value) << 8U)),
                        static_cast<std::uint16_t>(
                            static_cast<std::uint16_t>(value) |
                            (static_cast<std::uint16_t>(utf16_first_byte)
                             << 8U))};
                    for (std::size_t order = 0U; order < code_units.size();
                         ++order) {
                        const auto code = code_units[order];
                        ++utf16_units[order];
                        if (code == 0U || code == 0x7fU ||
                            (code < 0x20U && code != L'\t' &&
                             code != L'\r' && code != L'\n') ||
                            (code >= 0xd800U && code <= 0xdfffU) ||
                            code == 0xfffeU || code == 0xffffU) {
                            utf16_invalid[order] = true;
                        } else {
                            ++utf16_text_units[order];
                        }
                    }
                }
                if (value == 0U) continue;
                if (value == '\r' || value == '\n' || value == '\t' ||
                    (value >= 0x20U && value <= 0x7eU)) {
                    ++text_bytes;
                    ++lane_text_bytes[lane];
                    normalized.push_back(
                        static_cast<char>(std::tolower(value)));
                } else {
                    normalized.push_back(' ');
                }
            }
            static constexpr auto script_tokens =
                std::to_array<std::string_view>({
                    "#!", "@echo", "echo off", "%comspec%", "%~dp0",
                    "setlocal", "endlocal", "cmd.exe", "\ncall ",
                    "\ngoto ", "\nif ", "\nfor ", "\ndel ", "\nerase ",
                    "\ncopy ", "\nmove ", "\nstart ", "\nreg ",
                    "powershell", "pwsh ", "$psscriptroot", "param(",
                    "invoke-expression", "invoke-webrequest", "write-host",
                    "write-output", "get-childitem", "start-process",
                    "createobject(", "getobject(", "wscript.", "cscript.",
                    "msgbox ", "activexobject(", "function ", "var ",
                    "let ", "const ", "require(", "eval(", "<script",
                    "<job", "[internetshortcut]", "mshta", "rundll32"});
            if (std::ranges::any_of(script_tokens, [&](const auto token) {
                    return normalized.find(token) != std::string::npos;
                })) {
                static_cast<void>(rewind());
                return ContentKind::script;
            }
            constexpr std::size_t kTailBytes = 128U;
            tail = normalized.substr(
                normalized.size() > kTailBytes
                    ? normalized.size() - kTailBytes
                    : 0U);
        }
        const auto lane_is_text = [&](const std::size_t text_lane,
                                      const std::size_t zero_lane) noexcept {
            return lane_bytes[text_lane] >= 8U &&
                   lane_text_bytes[text_lane] >=
                       lane_bytes[text_lane] - (lane_bytes[text_lane] / 8U) &&
                   lane_zero_bytes[zero_lane] >=
                       lane_bytes[zero_lane] - (lane_bytes[zero_lane] / 8U);
        };
        const auto utf16_is_text = [&](const std::size_t order) noexcept {
            return !utf16_invalid[order] && utf16_units[order] >= 4U &&
                   utf16_text_units[order] >=
                       utf16_units[order] - (utf16_units[order] / 8U);
        };
        if ((total_bytes >= 8U &&
             text_bytes >= total_bytes - (total_bytes / 8U)) ||
            (utf8_valid && utf8_remaining == 0U && utf8_codepoints >= 4U) ||
            utf16_is_text(0U) || utf16_is_text(1U) ||
            lane_is_text(0U, 1U) || lane_is_text(1U, 0U) || unicode_bom) {
            static_cast<void>(rewind());
            return ContentKind::text;
        }
    } catch (...) {
        static_cast<void>(rewind());
        return ContentKind::executable;
    }
    return rewind() ? ContentKind::ordinary : ContentKind::executable;
}

[[nodiscard]] bool matches_goldsrc_asset_content(
    HANDLE file, std::wstring extension, const std::uint64_t size) noexcept
{
    std::ranges::transform(extension, extension.begin(),
        [](const wchar_t value) { return static_cast<wchar_t>(::towlower(value)); });
    LARGE_INTEGER zero{};
    if (!::SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) return false;
    std::array<unsigned char, 128U> bytes{};
    DWORD read = 0U;
    const bool read_ok = ::ReadFile(
        file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) !=
        FALSE;
    const bool rewind_ok =
        ::SetFilePointerEx(file, zero, nullptr, FILE_BEGIN) != FALSE;
    if (!read_ok || !rewind_ok) return false;
    const auto u16 = [&bytes](const std::size_t offset) noexcept {
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes[offset]) |
            (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
    };
    const auto u32 = [&bytes](const std::size_t offset) noexcept {
        return static_cast<std::uint32_t>(bytes[offset]) |
               (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
               (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
               (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
    };
    if (extension == L".bsp") {
        constexpr std::size_t kBspLumpCount = 15U;
        constexpr std::uint64_t kBspHeaderBytes =
            4U + kBspLumpCount * 8U;
        if (read < kBspHeaderBytes || size != kBspHeaderBytes ||
            (u32(0U) != 29U && u32(0U) != 30U)) {
            return false;
        }
        for (std::size_t lump = 0U; lump < kBspLumpCount; ++lump) {
            if (u32(4U + lump * 8U) != kBspHeaderBytes ||
                u32(8U + lump * 8U) != 0U) {
                return false;
            }
        }
        // Full GoldSrc BSP semantic validation requires parsing every lump and
        // their cross-references. Until then this evidence boundary accepts
        // only the exact empty v29/v30 form and rejects all non-empty BSPs.
        return true;
    }
    if (extension == L".mdl") {
        return false;
    }
    if (extension == L".spr") {
        return false;
    }
    if (extension == L".wad") {
        if (read < 12U || size < 12U || bytes[0] != 'W' ||
            bytes[1] != 'A' || bytes[2] != 'D' ||
            (bytes[3] != '2' && bytes[3] != '3')) {
            return false;
        }
        const auto lump_count = static_cast<std::int32_t>(u32(4U));
        const auto table_offset = static_cast<std::int32_t>(u32(8U));
        if (lump_count < 0 || table_offset < 12) return false;
        const auto count = static_cast<std::uint64_t>(lump_count);
        const auto offset = static_cast<std::uint64_t>(table_offset);
        // Until every directory entry and lump range is parsed, only the exact
        // empty WAD form is strong enough for this evidence profile.
        return count == 0U && offset == 12U && size == 12U;
    }
    if (extension == L".wav") {
        return false;
    }
    if (extension == L".mp3") {
        return false;
    }
    if (extension == L".bmp") {
        return false;
    }
    if (extension == L".tga") {
        return false;
    }
    if (extension == L".pcx") {
        return false;
    }
    if (extension == L".pal") return false;
    if (extension == L".fnt") {
        return false;
    }
    if (extension == L".avi") {
        return false;
    }
    if (extension == L".mpg" || extension == L".mpeg") {
        return false;
    }
    // LMP and RES have no sufficiently strong, universal magic for this
    // evidence boundary. They remain recognized hints but fail closed as
    // unknown until an exact format validator is added.
    return false;
}

struct HalfLifeApplicationLocation final {
    std::filesystem::path steamapps_root;
    std::filesystem::path application_root;
};

struct VdfValue final {
    bool object{false};
    std::string scalar;
    std::vector<std::pair<std::string, VdfValue>> members;
};

struct SteamLibraryFolder final {
    std::filesystem::path root;
    bool contains_app_70{false};
};

[[nodiscard]] std::optional<HalfLifeApplicationLocation>
half_life_application_location(const std::filesystem::path& path)
{
    auto native = path.lexically_normal().native();
    std::ranges::replace(native, L'/', L'\\');
    const auto folded = lowercase(native);
    constexpr std::wstring_view marker =
        L"\\steamapps\\common\\half-life";
    const auto position = folded.find(marker);
    if (position == std::wstring::npos) return std::nullopt;
    const auto application_end = position + marker.size();
    if (application_end < folded.size() &&
        folded[application_end] != L'\\') {
        return std::nullopt;
    }
    const auto steamapps_end = position + std::wstring_view{L"\\steamapps"}.size();
    return HalfLifeApplicationLocation{
        std::filesystem::path{native.substr(0U, steamapps_end)}
            .lexically_normal(),
        std::filesystem::path{native.substr(0U, application_end)}
            .lexically_normal()};
}

[[nodiscard]] bool path_same_or_below(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) noexcept
{
    auto root_name = lowercase(root.lexically_normal().native());
    auto candidate_name = lowercase(candidate.lexically_normal().native());
    while (root_name.size() > 1U &&
           (root_name.back() == L'\\' || root_name.back() == L'/')) {
        root_name.pop_back();
    }
    return candidate_name == root_name ||
           (candidate_name.size() > root_name.size() &&
            candidate_name.starts_with(root_name) &&
            (candidate_name[root_name.size()] == L'\\' ||
             candidate_name[root_name.size()] == L'/'));
}

[[nodiscard]] std::string lowercase_ascii(std::string value)
{
    std::ranges::transform(value, value.begin(), [](const unsigned char item) {
        return static_cast<char>(std::tolower(item));
    });
    return value;
}

enum class VdfTokenKind { string, open, close };

struct VdfToken final {
    VdfTokenKind kind{VdfTokenKind::string};
    std::string text;
};

[[nodiscard]] std::optional<std::vector<VdfToken>> tokenize_vdf(
    const std::string_view text) noexcept
{
    try {
        std::vector<VdfToken> tokens;
        tokens.reserve(256U);
        std::size_t index = text.starts_with("\xef\xbb\xbf") ? 3U : 0U;
        while (index < text.size()) {
            const unsigned char item =
                static_cast<unsigned char>(text[index]);
            if (item == ' ' || item == '\t' || item == '\r' || item == '\n') {
                ++index;
                continue;
            }
            if (item == '{' || item == '}') {
                tokens.push_back(VdfToken{
                    item == '{' ? VdfTokenKind::open : VdfTokenKind::close,
                    {}});
                ++index;
            } else if (item == '"') {
                ++index;
                std::string token;
                while (index < text.size() && text[index] != '"') {
                    const unsigned char value =
                        static_cast<unsigned char>(text[index++]);
                    if (value == '\\') {
                        if (index == text.size()) return std::nullopt;
                        const char escaped = text[index++];
                        if (escaped != '\\' && escaped != '"' &&
                            escaped != '/') {
                            return std::nullopt;
                        }
                        token.push_back(escaped);
                    } else if (value == 0U || value < 0x20U) {
                        return std::nullopt;
                    } else {
                        token.push_back(static_cast<char>(value));
                    }
                    if (token.size() > 32'767U) return std::nullopt;
                }
                if (index == text.size()) return std::nullopt;
                ++index;
                tokens.push_back(
                    VdfToken{VdfTokenKind::string, std::move(token)});
            } else {
                return std::nullopt;
            }
            if (tokens.size() > 8'192U) return std::nullopt;
        }
        return tokens;
    } catch (...) {
        return std::nullopt;
    }
}

class VdfParser final {
public:
    explicit VdfParser(const std::vector<VdfToken>& tokens) noexcept
        : tokens_{tokens}
    {
    }

    [[nodiscard]] bool parse_document(VdfValue& output)
    {
        output.object = true;
        return parse_members(output, false, 0U) && index_ == tokens_.size();
    }

private:
    [[nodiscard]] bool parse_members(
        VdfValue& output, const bool braced,
        const std::size_t depth)
    {
        if (depth > 16U) return false;
        while (index_ < tokens_.size()) {
            if (tokens_[index_].kind == VdfTokenKind::close) {
                if (!braced) return false;
                ++index_;
                return true;
            }
            if (tokens_[index_].kind != VdfTokenKind::string) return false;
            std::string key = tokens_[index_++].text;
            const auto folded_key = lowercase_ascii(key);
            if (std::ranges::any_of(
                    output.members, [&folded_key](const auto& member) {
                        return lowercase_ascii(member.first) == folded_key;
                    })) {
                return false;
            }
            if (index_ == tokens_.size()) return false;
            VdfValue value;
            if (tokens_[index_].kind == VdfTokenKind::string) {
                value.scalar = tokens_[index_++].text;
            } else if (tokens_[index_].kind == VdfTokenKind::open) {
                value.object = true;
                ++index_;
                if (!parse_members(value, true, depth + 1U)) return false;
            } else {
                return false;
            }
            output.members.emplace_back(std::move(key), std::move(value));
        }
        return !braced;
    }

    const std::vector<VdfToken>& tokens_;
    std::size_t index_{0U};
};

[[nodiscard]] const VdfValue* vdf_member(
    const VdfValue& object, const std::string_view key)
{
    if (!object.object) return nullptr;
    const auto folded = lowercase_ascii(std::string{key});
    const auto found = std::ranges::find_if(
        object.members, [&folded](const auto& member) {
            return lowercase_ascii(member.first) == folded;
        });
    return found == object.members.end() ? nullptr : &found->second;
}

[[nodiscard]] bool exact_appmanifest_70(
    const std::string_view text) noexcept
{
    try {
        const auto tokens = tokenize_vdf(text);
        if (!tokens) return false;
        VdfValue document;
        VdfParser parser{*tokens};
        if (!parser.parse_document(document) ||
            document.members.size() != 1U) {
            return false;
        }
        const auto* app_state = vdf_member(document, "AppState");
        if (app_state == nullptr || !app_state->object) return false;
        const auto contains_reserved_nested_key =
            [&](const auto& self, const VdfValue& value) -> bool {
            for (const auto& [key, child] : value.members) {
                const auto folded = lowercase_ascii(key);
                if (folded == "appid" || folded == "installdir" ||
                    (child.object && self(self, child))) {
                    return true;
                }
            }
            return false;
        };
        for (const auto& [key, value] : app_state->members) {
            const auto folded = lowercase_ascii(key);
            if (folded != "appid" && folded != "installdir" &&
                value.object && contains_reserved_nested_key(
                                    contains_reserved_nested_key, value)) {
                return false;
            }
        }
        const auto* app_id = vdf_member(*app_state, "appid");
        const auto* install_directory = vdf_member(*app_state, "installdir");
        return app_id != nullptr && !app_id->object &&
               app_id->scalar == "70" && install_directory != nullptr &&
               !install_directory->object &&
               install_directory->scalar == "Half-Life";
    } catch (...) {
        return false;
    }
}

[[nodiscard]] std::optional<std::filesystem::path> utf8_path(
    const std::string_view value) noexcept
{
    if (value.empty() ||
        value.size() >
            static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return std::nullopt;
    }
    const int required = ::MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0 || required > 32'767) return std::nullopt;
    try {
        std::wstring wide(static_cast<std::size_t>(required), L'\0');
        if (::MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                static_cast<int>(value.size()), wide.data(), required) !=
            required) {
            return std::nullopt;
        }
        std::filesystem::path path{std::move(wide)};
        if (!path.is_absolute() || path.has_relative_path() == false) {
            return std::nullopt;
        }
        for (const auto& component : path) {
            if (component == L"." || component == L"..") {
                return std::nullopt;
            }
        }
        return path.lexically_normal();
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] bool parse_libraryfolders_vdf(
    const std::string_view text,
    std::vector<SteamLibraryFolder>& libraries) noexcept
{
    try {
        const auto tokens = tokenize_vdf(text);
        if (!tokens) return false;
        VdfValue document;
        VdfParser parser{*tokens};
        if (!parser.parse_document(document) ||
            document.members.size() != 1U) {
            return false;
        }
        const auto* root = vdf_member(document, "libraryfolders");
        if (root == nullptr || !root->object || root->members.empty() ||
            root->members.size() > 256U) {
            return false;
        }
        std::set<std::wstring> unique_paths;
        for (const auto& [ordinal, value] : root->members) {
            if (ordinal.empty() ||
                !std::ranges::all_of(ordinal, [](const unsigned char item) {
                    return std::isdigit(item) != 0;
                }) ||
                !value.object) {
                return false;
            }
            const auto* path_value = vdf_member(value, "path");
            const auto* apps_value = vdf_member(value, "apps");
            if (path_value == nullptr || path_value->object ||
                apps_value == nullptr || !apps_value->object) {
                return false;
            }
            const auto parsed_path = utf8_path(path_value->scalar);
            if (!parsed_path || !local_fixed(*parsed_path)) return false;
            const auto folded_path = lowercase(parsed_path->native());
            if (!unique_paths.insert(folded_path).second) return false;
            const auto* app_70 = vdf_member(*apps_value, "70");
            if (app_70 != nullptr && app_70->object) return false;
            libraries.push_back(
                SteamLibraryFolder{*parsed_path, app_70 != nullptr});
        }
        return !libraries.empty();
    } catch (...) {
        return false;
    }
}

[[nodiscard]] std::optional<std::string> libraryfolders_membership_evidence(
    const HalfLifeApplicationLocation& source,
    const HalfLifeApplicationLocation& target) noexcept
{
    try {
        const auto libraryfolders =
            source.steamapps_root / L"libraryfolders.vdf";
        auto file = open_path(libraryfolders, true, false);
        const auto before = file.valid() ? snapshot(file.get()) : std::nullopt;
        const auto metadata_final =
            file.valid() ? final_path(file.get()) : std::nullopt;
        constexpr std::uint64_t kMaximumLibraryFoldersBytes =
            1U * 1'024U * 1'024U;
        if (!before || !metadata_final || before->directory ||
            (before->attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
            before->links != 1U || before->size == 0U ||
            before->size > kMaximumLibraryFoldersBytes ||
            !local_fixed(*metadata_final) || has_ads(libraryfolders)) {
            return std::nullopt;
        }
        std::string text(static_cast<std::size_t>(before->size), '\0');
        LARGE_INTEGER zero{};
        if (!::SetFilePointerEx(file.get(), zero, nullptr, FILE_BEGIN)) {
            return std::nullopt;
        }
        std::size_t offset = 0U;
        while (offset < text.size()) {
            DWORD read = 0U;
            const auto remaining = text.size() - offset;
            const auto chunk = static_cast<DWORD>((std::min)(
                remaining,
                static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
            if (!::ReadFile(
                    file.get(), text.data() + offset, chunk, &read, nullptr) ||
                read == 0U) {
                return std::nullopt;
            }
            offset += read;
        }
        std::vector<SteamLibraryFolder> libraries;
        if (!parse_libraryfolders_vdf(text, libraries)) return std::nullopt;

        const auto open_library_root = [](const std::filesystem::path& value)
            -> std::optional<std::pair<Snapshot, std::filesystem::path>> {
            if (!local_fixed(value)) return std::nullopt;
            auto handle = open_path(value, true, true);
            const auto value_snapshot =
                handle.valid() ? snapshot(handle.get()) : std::nullopt;
            const auto value_final =
                handle.valid() ? final_path(handle.get()) : std::nullopt;
            if (!value_snapshot || !value_final ||
                !value_snapshot->directory ||
                (value_snapshot->attributes &
                 FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
                !local_fixed(*value_final)) {
                return std::nullopt;
            }
            return std::pair{*value_snapshot, *value_final};
        };
        const auto source_root =
            open_library_root(source.steamapps_root.parent_path());
        const auto target_root =
            open_library_root(target.steamapps_root.parent_path());
        if (!source_root || !target_root) return std::nullopt;

        std::vector<std::pair<Snapshot, std::filesystem::path>> registered;
        for (const auto& library : libraries) {
            if (!library.contains_app_70) continue;
            const auto observed = open_library_root(library.root);
            if (!observed) return std::nullopt;
            if (std::ranges::any_of(
                    registered, [&observed](const auto& existing) {
                        return same_object_shape(
                            existing.first, observed->first);
                    })) {
                return std::nullopt;
            }
            registered.push_back(*observed);
        }
        const auto registered_once = [&registered](const Snapshot& expected) {
            return std::ranges::count_if(
                       registered, [&expected](const auto& observed) {
                           return same_object_shape(
                               expected, observed.first);
                       }) == 1;
        };
        if (!registered_once(source_root->first) ||
            !registered_once(target_root->first)) {
            return std::nullopt;
        }

        std::string content_digest;
        if (!read_hash(file.get(), before->size, content_digest)) {
            return std::nullopt;
        }
        const auto after = snapshot(file.get());
        if (!after || identity(*before) != identity(*after)) {
            return std::nullopt;
        }
        return hash_text(
            std::string{
                "hlclient.steam-libraryfolders-app70-membership.v1|"} +
            identity(*before) + '|' + wide_hex(*metadata_final) + '|' +
            content_digest + '|' + identity(source_root->first) + '|' +
            wide_hex(source_root->second) + '|' +
            identity(target_root->first) + '|' +
            wide_hex(target_root->second));
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::string> half_life_location_evidence(
    const HalfLifeApplicationLocation& location,
    const std::filesystem::path& member) noexcept
{
    try {
        if (!path_same_or_below(location.application_root, member)) {
            return std::nullopt;
        }
        auto application = open_path(location.application_root, true, true);
        const auto application_snapshot =
            application.valid() ? snapshot(application.get()) : std::nullopt;
        const auto application_final =
            application.valid() ? final_path(application.get()) : std::nullopt;
        if (!application_snapshot || !application_final ||
            !application_snapshot->directory ||
            (application_snapshot->attributes & FILE_ATTRIBUTE_REPARSE_POINT) !=
                0U) {
            return std::nullopt;
        }
        const auto manifest = location.steamapps_root / L"appmanifest_70.acf";
        auto handle = open_path(manifest, true, false);
        const auto before = handle.valid() ? snapshot(handle.get())
                                           : std::nullopt;
        const auto manifest_final =
            handle.valid() ? final_path(handle.get()) : std::nullopt;
        constexpr std::uint64_t kMaximumManifestBytes = 64U * 1'024U;
        if (!before || !manifest_final || before->directory ||
            (before->attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
            before->links != 1U || before->size == 0U ||
            before->size > kMaximumManifestBytes || has_ads(manifest)) {
            return std::nullopt;
        }
        std::string text(static_cast<std::size_t>(before->size), '\0');
        LARGE_INTEGER zero{};
        if (!::SetFilePointerEx(handle.get(), zero, nullptr, FILE_BEGIN)) {
            return std::nullopt;
        }
        std::size_t offset = 0U;
        while (offset < text.size()) {
            DWORD read = 0U;
            const auto remaining = text.size() - offset;
            const auto chunk = static_cast<DWORD>((std::min)(
                remaining,
                static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
            if (!::ReadFile(
                    handle.get(), text.data() + offset, chunk, &read, nullptr) ||
                read == 0U) {
                return std::nullopt;
            }
            offset += read;
        }
        if (!exact_appmanifest_70(text)) {
            return std::nullopt;
        }
        std::string content_digest;
        if (!read_hash(handle.get(), before->size, content_digest)) {
            return std::nullopt;
        }
        const auto after = snapshot(handle.get());
        if (!after || identity(*before) != identity(*after)) {
            return std::nullopt;
        }
        return hash_text(
            std::string{"hlclient.half-life-app-provenance.v1|"} +
            identity(*application_snapshot) + '|' +
            wide_hex(*application_final) + '|' + identity(*before) + '|' +
            wide_hex(*manifest_final) + '|' + content_digest);
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::string> registered_half_life_provenance(
    const std::filesystem::path& source,
    const std::filesystem::path& target) noexcept
{
    try {
        const auto source_location = half_life_application_location(source);
        const auto target_location = half_life_application_location(target);
        if (!source_location || !target_location) return std::nullopt;
        const auto source_evidence =
            half_life_location_evidence(*source_location, source);
        const auto target_evidence =
            half_life_location_evidence(*target_location, target);
        const auto library_evidence = libraryfolders_membership_evidence(
            *source_location, *target_location);
        if (!source_evidence || !target_evidence || !library_evidence) {
            return std::nullopt;
        }
        const auto& first = *source_evidence < *target_evidence
                                ? *source_evidence
                                : *target_evidence;
        const auto& second = *source_evidence < *target_evidence
                                 ? *target_evidence
                                 : *source_evidence;
        return hash_text(
            std::string{"hlclient.half-life-provenance-set.v2|"} + first +
            '|' + second + '|' + *library_evidence);
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] StockExternalTopologyFailureWitness make_failure_witness(
    const std::filesystem::path& private_link_path,
    const WindowsReparseTargetObservation& observation);
[[nodiscard]] std::string observation_binding(
    const WindowsReparseTargetObservation& value);

void mark_target_inventory_failure(
    const std::filesystem::path& private_path,
    const std::uint32_t native_error,
    const StockExternalTopologyFailurePhase phase,
    StockExternalTargetReview& review)
{
    if (!review.reparse_observation) return;
    auto failure = *review.reparse_observation;
    failure.native_error = native_error;
    failure.native_error_category =
        classify_windows_reparse_native_error(native_error);
    failure.reachability =
        classify_windows_reparse_target_reachability(native_error);
    failure.failure_phase = phase;
    failure.diagnostic_classification =
        WindowsReparseDiagnosticClassification::target_open_failed_other;
    review.failure_witness = make_failure_witness(private_path, failure);
    review.inventory_available = false;
    review.target_inventory_sha256.clear();
    review.classification =
        native_error == ERROR_FILE_INVALID
            ? StockExternalTargetClassification::changed_during_review
            : StockExternalTargetClassification::unsupported_reparse_topology;
    review.diagnostic_complete = true;
    review.eligible = false;
}

[[nodiscard]] bool scan_target(
    const std::filesystem::path& source_root,
    const std::filesystem::path& root,
    const Snapshot& approved_root_snapshot,
    HANDLE pinned_root_handle,
    const StockResearchCopyLimits& limits,
    const WindowsReparseProvenanceLimits& reparse_limits,
    StockExternalTargetReview& review,
    PinnedTargetInventory& pinned_inventory)
{
    const auto mark_policy_inventory_unavailable = [&] {
        if (!review.reparse_observation) return;
        auto failure = *review.reparse_observation;
        failure.failure_phase =
            StockExternalTopologyFailurePhase::target_inventory;
        review.failure_witness = make_failure_witness(root, failure);
        review.inventory_available = false;
        review.diagnostic_complete = true;
        review.eligible = false;
    };
    if (!local_fixed(root)) {
        review.entry_count = 1U;
        review.byte_count = approved_root_snapshot.directory
                                ? 0U
                                : approved_root_snapshot.size;
        review.classification =
            StockExternalTargetClassification::remote_or_device_target;
        mark_policy_inventory_unavailable();
        return true;
    }

    const auto source_application = steam_common_application(source_root);
    const auto target_application = steam_common_application(root);
    const auto application_provenance =
        registered_half_life_provenance(source_root, root);
    const bool same_application_provenance =
        application_provenance.has_value();
    if ((source_application && target_application &&
         *source_application != *target_application) ||
        (!source_application && target_application)) {
        review.entry_count = 1U;
        review.byte_count = approved_root_snapshot.directory
                                ? 0U
                                : approved_root_snapshot.size;
        review.classification =
            StockExternalTargetClassification::another_application_tree;
        mark_policy_inventory_unavailable();
        return true;
    }
    if (const auto policy = path_classification(root)) {
        review.entry_count = 1U;
        review.byte_count = approved_root_snapshot.directory
                                ? 0U
                                : approved_root_snapshot.size;
        review.classification = *policy;
        if (*policy == StockExternalTargetClassification::
                           contains_mutable_user_state) {
            ++review.mutable_state_count;
        } else if (*policy == StockExternalTargetClassification::
                                  contains_executable_code) {
            ++review.executable_count;
        }
        mark_policy_inventory_unavailable();
        return true;
    }
    if (const auto policy =
            path_classification(review.source_link_relative_path)) {
        review.entry_count = 1U;
        review.byte_count = approved_root_snapshot.directory
                                ? 0U
                                : approved_root_snapshot.size;
        review.classification = *policy;
        if (*policy == StockExternalTargetClassification::
                           contains_mutable_user_state) {
            ++review.mutable_state_count;
        } else if (*policy == StockExternalTargetClassification::
                                  contains_executable_code) {
            ++review.executable_count;
        }
        mark_policy_inventory_unavailable();
        return true;
    }

    const auto retained_before = snapshot(pinned_root_handle);
    const auto retained_root_ads = named_ads_count_handle(
        pinned_root_handle, limits.maximum_streams_per_file);
    if (!retained_before ||
        identity(*retained_before) != identity(approved_root_snapshot) ||
        (retained_before->attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
        !retained_root_ads || *retained_root_ads != 0U) {
        review.entry_count = 1U;
        review.byte_count = approved_root_snapshot.directory
                                ? 0U
                                : approved_root_snapshot.size;
        review.classification =
            StockExternalTargetClassification::changed_during_review;
        mark_target_inventory_failure(
            root, ERROR_FILE_INVALID,
            StockExternalTopologyFailurePhase::target_inventory, review);
        return true;
    }

    pinned_inventory = {};
    pinned_inventory.root_handle = pinned_root_handle;
    pinned_inventory.root_snapshot = *retained_before;

    struct PendingDirectory final {
        HANDLE handle{INVALID_HANDLE_VALUE};
        std::filesystem::path relative_path;
    };
    std::vector<PendingDirectory> pending;
    if (retained_before->directory) {
        pending.push_back({pinned_root_handle, std::filesystem::path{L"."}});
    }
    while (!pending.empty()) {
        auto directory = std::move(pending.back());
        pending.pop_back();
        const auto listing = enumerate_directory_handle(
            directory.handle, limits.maximum_entries);
        if (!listing) {
            if (listing.native_error == ERROR_BUFFER_OVERFLOW) {
                review.classification = StockExternalTargetClassification::
                    content_limit_exceeded;
                mark_policy_inventory_unavailable();
                return true;
            }
            mark_target_inventory_failure(
                root, listing.native_error,
                StockExternalTopologyFailurePhase::target_inventory, review);
            return true;
        }
        pinned_inventory.directories.push_back(
            PinnedDirectoryWitness{directory.handle, listing.entries});
        for (const auto& [name, listed_attributes] : listing.entries) {
            if (pinned_inventory.entries.size() + 1U >=
                limits.maximum_entries) {
                review.classification = StockExternalTargetClassification::
                    content_limit_exceeded;
                mark_policy_inventory_unavailable();
                return true;
            }
            const bool directory_entry =
                (listed_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
            auto child = open_relative_inventory_entry(
                directory.handle, name, directory_entry);
            const auto child_snapshot =
                child.valid() ? snapshot(child.get()) : std::nullopt;
            if (!child_snapshot ||
                child_snapshot->directory != directory_entry) {
                mark_target_inventory_failure(
                    root,
                    child.valid() ? ERROR_FILE_INVALID : ::GetLastError(),
                    StockExternalTopologyFailurePhase::nested_entry_open,
                    review);
                return true;
            }
            auto relative = directory.relative_path ==
                                    std::filesystem::path{L"."}
                                ? std::filesystem::path{name}
                                : directory.relative_path / name;
            pinned_inventory.entries.push_back(PinnedInventoryEntry{
                std::move(child), *child_snapshot, relative});
            auto& stored = pinned_inventory.entries.back();
            if (stored.snapshot.directory &&
                (stored.snapshot.attributes &
                 FILE_ATTRIBUTE_REPARSE_POINT) == 0U) {
                pending.push_back({stored.handle.get(), stored.relative_path});
            }
        }
    }

    struct InventoryPath final {
        std::filesystem::path full_path;
        std::filesystem::path relative_path;
        HANDLE handle{INVALID_HANDLE_VALUE};
        Snapshot before;
    };
    std::vector<InventoryPath> paths;
    paths.reserve(pinned_inventory.entries.size() + 1U);
    paths.push_back({root, std::filesystem::path{L"."},
                     pinned_root_handle, *retained_before});
    for (const auto& entry : pinned_inventory.entries) {
        paths.push_back({root / entry.relative_path, entry.relative_path,
                         entry.handle.get(), entry.snapshot});
    }
    std::ranges::sort(paths, {}, [](const auto& value) {
        return value.relative_path.native();
    });

    bool executable = false;
    bool script = false;
    bool mutable_state = false;
    bool nested_link = false;
    bool unsupported = false;
    bool known_asset = false;
    bool unknown_asset = false;
    std::ostringstream inventory;
    if (application_provenance) {
        inventory << "provenance:" << *application_provenance << '\n';
    }
    for (const auto& item : paths) {
        const auto& path = item.full_path;
        const auto& relative = item.relative_path;
        const auto& before = item.before;
        const auto attributes = before.attributes;
        const bool is_root = relative == std::filesystem::path{L"."};
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
            ++review.nested_link_count;
            nested_link = true;
            if (!is_root) {
                const auto nested = observe_windows_reparse_target(
                    path, reparse_limits);
                const auto nested_after = snapshot(item.handle);
                if (!nested_after ||
                    !same_inventory_snapshot(before, *nested_after) ||
                    !nested || !nested.value->observation_complete) {
                    return false;
                }
                const auto nested_depth = static_cast<std::size_t>(
                    std::distance(relative.begin(), relative.end()));
                ++review.entry_count;
                inventory << wide_hex(relative)
                          << ':' << identity(before) << ':'
                          << observation_binding(*nested.value) << '\n';
                if (!review.failure_witness) {
                    auto nested_failure = *nested.value;
                    nested_failure.failure_phase =
                        StockExternalTopologyFailurePhase::
                            nested_reparse_decode;
                    auto witness =
                        make_failure_witness(path, nested_failure);
                    witness.traversal_depth = nested_depth;
                    witness.nested_ordinal = review.nested_link_count;
                    review.failure_witness = std::move(witness);
                }
                if (review.nested_link_count >
                    reparse_limits.maximum_failure_witnesses) {
                    review.classification =
                        StockExternalTargetClassification::
                            content_limit_exceeded;
                    review.inventory_available = false;
                    mark_policy_inventory_unavailable();
                    return true;
                }
                continue;
            }
        }
        const auto ads = named_ads_count_handle(
            item.handle, limits.maximum_streams_per_file);
        if (!ads) {
            mark_target_inventory_failure(
                path, ERROR_BUFFER_OVERFLOW,
                StockExternalTopologyFailurePhase::target_inventory, review);
            return true;
        }
        if (*ads != 0U) unsupported = true;
        const bool directory = before.directory;
        ++review.entry_count;
        if (!directory) {
            if (before.size > limits.maximum_file_bytes ||
                before.size > limits.maximum_total_bytes ||
                review.byte_count >
                    limits.maximum_total_bytes - before.size) {
                review.classification = StockExternalTargetClassification::
                    content_limit_exceeded;
                mark_policy_inventory_unavailable();
                return true;
            }
            review.byte_count += before.size;
        }
        inventory << wide_hex(relative) << ':' << identity(before);
        if (const auto policy = path_classification(relative)) {
            if (*policy == StockExternalTargetClassification::
                               contains_mutable_user_state) {
                ++review.mutable_state_count;
                mutable_state = true;
            } else if (
                *policy == StockExternalTargetClassification::
                               contains_executable_code) {
                ++review.executable_count;
                executable = true;
            }
        }
        if (!directory) {
            const auto kind = content_kind(item.handle);
            const bool executable_entry =
                is_executable_extension(path.extension()) ||
                kind == ContentKind::executable;
            const bool script_entry =
                is_script_extension(path.extension()) ||
                kind == ContentKind::script;
            const bool text_entry = kind == ContentKind::text;
            if (executable_entry) {
                ++review.executable_count;
                executable = true;
            }
            if (script_entry) {
                ++review.script_or_command_count;
                script = true;
            }
            if (!text_entry &&
                is_goldsrc_asset_extension(path.extension()) &&
                matches_goldsrc_asset_content(
                    item.handle, path.extension(), before.size)) {
                known_asset = true;
            } else if (!executable_entry && !script_entry) {
                unknown_asset = true;
            }
            std::string content;
            if (!read_hash(item.handle, before.size, content)) {
                const auto native_error = ::GetLastError();
                mark_target_inventory_failure(
                    path,
                    native_error == ERROR_SUCCESS ? ERROR_READ_FAULT
                                                  : native_error,
                    StockExternalTopologyFailurePhase::target_inventory,
                    review);
                return true;
            }
            inventory << ':' << content;
        }
        const auto after = snapshot(item.handle);
        if (!after || !same_inventory_snapshot(before, *after)) {
            review.classification =
                StockExternalTargetClassification::changed_during_review;
            mark_target_inventory_failure(
                path, ERROR_FILE_INVALID,
                StockExternalTopologyFailurePhase::
                    post_inventory_revalidation,
                review);
            return true;
        }
        inventory << '\n';
    }
    if (!revalidate_pinned_target_inventory(pinned_inventory)) {
        review.classification =
            StockExternalTargetClassification::changed_during_review;
        mark_target_inventory_failure(
            root, ERROR_FILE_INVALID,
            StockExternalTopologyFailurePhase::post_inventory_revalidation,
            review);
        return true;
    }
    review.target_inventory_sha256 = hash_text(inventory.str());
    if (review.target_inventory_sha256.empty()) return false;
    if (unsupported) {
        review.classification = StockExternalTargetClassification::
            unsupported_reparse_topology;
    } else if (nested_link) {
        review.classification =
            StockExternalTargetClassification::nested_external_link;
    } else if (executable) {
        review.classification =
            StockExternalTargetClassification::contains_executable_code;
    } else if (script) {
        review.classification =
            StockExternalTargetClassification::contains_script_or_command;
    } else if (mutable_state) {
        review.classification =
            StockExternalTargetClassification::contains_mutable_user_state;
    } else if (!same_application_provenance || unknown_asset || !known_asset) {
        review.classification = StockExternalTargetClassification::unknown;
    } else {
        review.classification = StockExternalTargetClassification::
            eligible_non_executable_asset_tree;
        review.eligible = true;
    }
    return true;
}

[[nodiscard]] StockExternalTargetClassification diagnostic_classification(
    const WindowsReparseDiagnosticClassification value) noexcept
{
    switch (value) {
    case WindowsReparseDiagnosticClassification::remote_or_device_target:
        return StockExternalTargetClassification::remote_or_device_target;
    case WindowsReparseDiagnosticClassification::changed_during_observation:
        return StockExternalTargetClassification::changed_during_review;
    case WindowsReparseDiagnosticClassification::none:
    case WindowsReparseDiagnosticClassification::reachable_name_surrogate:
    case WindowsReparseDiagnosticClassification::dangling_directory_junction:
    case WindowsReparseDiagnosticClassification::dangling_directory_symlink:
    case WindowsReparseDiagnosticClassification::missing_volume_mount:
    case WindowsReparseDiagnosticClassification::inaccessible_target:
    case WindowsReparseDiagnosticClassification::cyclic_target:
    case WindowsReparseDiagnosticClassification::target_depth_exceeded:
    case WindowsReparseDiagnosticClassification::
        unsupported_tag_without_path_contract:
    case WindowsReparseDiagnosticClassification::malformed_reparse_payload:
    case WindowsReparseDiagnosticClassification::target_open_failed_other:
        return StockExternalTargetClassification::
            unsupported_reparse_topology;
    }
    return StockExternalTargetClassification::unsupported_reparse_topology;
}

[[nodiscard]] std::string observation_binding(
    const WindowsReparseTargetObservation& value)
{
    std::ostringstream stream;
    stream << value.provenance.tag.raw_tag << '|'
           << value.provenance.tag.microsoft << '|'
           << value.provenance.tag.name_surrogate << '|'
           << value.provenance.tag.directory << '|'
           << to_string(value.provenance.tag.category) << '|'
           << value.provenance.tag.payload_byte_count << '|'
           << to_string(value.provenance.payload_status) << '|'
           << to_string(value.provenance.payload_error) << '|'
           << value.provenance.private_payload_sha256 << '|'
           << wide_hex(value.provenance.private_substitute_name) << '|'
           << wide_hex(value.provenance.private_print_name) << '|'
           << to_string(value.provenance.target_expression.kind) << '|'
           << wide_hex(value.provenance.target_expression.private_expression)
           << '|'
           << wide_hex(
                  value.provenance.target_expression.
                      private_normalized_expression)
           << '|' << value.provenance.target_expression.relative << '|'
           << value.provenance.symbolic_link_flags << '|'
           << value.provenance.symbolic_link_relative << '|'
           << to_string(value.reachability) << '|'
           << to_string(value.native_error_category) << '|'
           << value.native_error << '|'
           << to_string(value.diagnostic_classification) << '|'
           << to_string(value.failure_phase) << '|'
           << value.observation_complete;
    if (value.target_identity) {
        stream << '|' << value.target_identity->volume_serial << ':'
               << hex(value.target_identity->file_id) << ':'
               << wide_hex(value.target_identity->private_final_handle_path)
               << ':' << value.target_identity->directory;
    }
    if (value.nested_failure) {
        const auto& nested = *value.nested_failure;
        stream << "|nested:" << nested.traversal_depth << ':'
               << nested.nested_ordinal << ':'
               << to_string(nested.reparse_tag_category) << ':'
               << to_string(nested.expression_kind) << ':'
               << to_string(nested.reachability) << ':'
               << to_string(nested.failure_phase) << ':' << nested.directory
               << ':' << to_string(nested.native_error_category) << ':'
               << nested.native_error << ':'
               << wide_hex(nested.private_link_path);
    }
    return hash_text(stream.str());
}

[[nodiscard]] StockExternalTopologyFailureWitness make_failure_witness(
    const std::filesystem::path& private_link_path,
    const WindowsReparseTargetObservation& observation)
{
    StockExternalTopologyFailureWitness witness{};
    const auto* nested = observation.nested_failure
                             ? &*observation.nested_failure
                             : nullptr;
    const auto& witness_path = nested ? nested->private_link_path
                                      : private_link_path;
    if (nested) {
        witness.traversal_depth = nested->traversal_depth;
        witness.nested_ordinal = nested->nested_ordinal;
        witness.reparse_tag_category = nested->reparse_tag_category;
        witness.expression_kind = nested->expression_kind;
        witness.reachability = nested->reachability;
        witness.failure_phase = nested->failure_phase;
        witness.directory = nested->directory;
        witness.native_error_category = nested->native_error_category;
    } else {
        witness.reparse_tag_category = observation.provenance.tag.category;
        witness.expression_kind = observation.provenance.target_expression.kind;
        witness.reachability = observation.reachability;
        witness.failure_phase = observation.failure_phase;
        witness.directory = observation.provenance.tag.directory;
        witness.native_error_category = observation.native_error_category;
    }
    std::ostringstream private_binding;
    private_binding << "hlclient.external-topology-failure-witness.v1|"
                    << wide_hex(witness_path) << '|'
                    << observation_binding(observation);
    witness.private_witness_sha256 = hash_text(private_binding.str());
    return witness;
}

[[nodiscard]] std::optional<Scan> scan_source(const std::filesystem::path& source,
                                              const StockResearchCopyLimits& limits)
{
    if (!source.is_absolute() || !local_fixed(source) ||
        limits.maximum_reparse_depth == 0U) {
        return std::nullopt;
    }
    WindowsReparseProvenanceLimits reparse_limits{};
    reparse_limits.maximum_nested_reparse_depth = (std::min)(
        limits.maximum_reparse_depth,
        kWindowsReparseHardMaximumNestedDepth);
    if (!valid_windows_reparse_provenance_limits(reparse_limits)) {
        return std::nullopt;
    }
    std::size_t failure_witness_count = 0U;
    auto root = open_path_inventory_guard(source, true, true);
    const auto root_snapshot = root.valid() ? snapshot(root.get()) : std::nullopt;
    const auto root_final = root.valid() ? final_path(root.get()) : std::nullopt;
    const auto root_handle_path =
        root.valid() ? final_volume_guid_path(root.get()) : std::nullopt;
    if (!root_snapshot || !root_final || !root_handle_path ||
        !root_snapshot->directory ||
        (root_snapshot->attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        return std::nullopt;
    }
    Scan scan{};
    scan.source_root_guard = std::move(root);
    scan.source_snapshot = *root_snapshot;
    scan.source_final_path = *root_final;
    scan.source_identity = identity(*root_snapshot);
    const auto root_ads =
        named_ads_count_handle(scan.source_root_guard.get(),
                               limits.maximum_streams_per_file);
    if (!root_ads) return std::nullopt;
    scan.alternate_data_stream_count = *root_ads;
    std::vector<std::pair<std::wstring, std::string>>
        source_inventory_records;
    std::size_t source_entries = 0U;
    std::uint64_t source_bytes = 0U;

    struct PendingDirectory final {
        HANDLE handle{INVALID_HANDLE_VALUE};
        std::filesystem::path relative_path;
        std::vector<std::string> ancestry;
    };
    std::vector<PendingDirectory> pending;
    pending.push_back({scan.source_root_guard.get(),
                       std::filesystem::path{L"."},
                       {stable_directory_identity(*root_snapshot)}});
    while (!pending.empty()) {
        auto directory = std::move(pending.back());
        pending.pop_back();
        if (source_entries >= limits.maximum_entries) return std::nullopt;
        const auto listing = enumerate_directory_handle(
            directory.handle, limits.maximum_entries - source_entries);
        if (!listing) return std::nullopt;
        scan.source_directory_witnesses.push_back(
            PinnedDirectoryWitness{directory.handle, listing.entries});

        for (const auto& [name, listed_attributes] : listing.entries) {
            if (++source_entries > limits.maximum_entries) {
                return std::nullopt;
            }
            const bool listed_directory =
                (listed_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
            const bool listed_reparse =
                (listed_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U;
            auto child = open_relative_inventory_entry(
                directory.handle, name, listed_directory);
            const auto child_snapshot =
                child.valid() ? snapshot(child.get()) : std::nullopt;
            if (!child_snapshot ||
                child_snapshot->directory != listed_directory ||
                ((child_snapshot->attributes &
                  FILE_ATTRIBUTE_REPARSE_POINT) != 0U) != listed_reparse) {
                return std::nullopt;
            }

            auto relative = directory.relative_path ==
                                    std::filesystem::path{L"."}
                                ? std::filesystem::path{name}
                                : directory.relative_path / name;
            const auto private_entry_path = *root_handle_path / relative;
            if (private_entry_path.native().size() >
                limits.maximum_path_characters) {
                return std::nullopt;
            }
            scan.source_inventory_entries.push_back(PinnedInventoryEntry{
                std::move(child), *child_snapshot, relative});
            auto& stored = scan.source_inventory_entries.back();

            if (!stored.snapshot.directory &&
                (stored.snapshot.size > limits.maximum_file_bytes ||
                 stored.snapshot.size > limits.maximum_total_bytes ||
                 source_bytes >
                     limits.maximum_total_bytes - stored.snapshot.size)) {
                return std::nullopt;
            }
            if (!stored.snapshot.directory) source_bytes += stored.snapshot.size;

            std::ostringstream inventory_record;
            if (!listed_reparse) {
                const auto entry_ads = named_ads_count_handle(
                    stored.handle.get(), limits.maximum_streams_per_file);
                if (!entry_ads) return std::nullopt;
                scan.alternate_data_stream_count += *entry_ads;
                if (!stored.snapshot.directory && stored.snapshot.links > 1U) {
                    ++scan.hardlink_count;
                }
                inventory_record << wide_hex(relative) << ':'
                                 << identity(stored.snapshot);
                if (!stored.snapshot.directory) {
                    std::string content;
                    if (!read_hash(stored.handle.get(), stored.snapshot.size,
                                   content)) {
                        return std::nullopt;
                    }
                    inventory_record << ':' << content;
                }
                const auto entry_after = snapshot(stored.handle.get());
                if (!entry_after ||
                    !same_inventory_snapshot(stored.snapshot, *entry_after)) {
                    return std::nullopt;
                }
                source_inventory_records.emplace_back(
                    relative.native(), inventory_record.str());
                if (stored.snapshot.directory) {
                    const auto directory_identity =
                        stable_directory_identity(stored.snapshot);
                    if (std::ranges::find(directory.ancestry,
                                          directory_identity) !=
                        directory.ancestry.end()) {
                        return std::nullopt;
                    }
                    auto ancestry = directory.ancestry;
                    ancestry.push_back(directory_identity);
                    pending.push_back({stored.handle.get(), relative,
                                       std::move(ancestry)});
                }
                continue;
            }

            ++scan.internal_reparse_count;
            if (stored.snapshot.reparse_tag == 0U) return std::nullopt;
            const auto observed = observe_windows_reparse_target(
                private_entry_path, reparse_limits);
            const auto link_after = snapshot(stored.handle.get());
            if (!observed || !observed.value->observation_complete ||
                !link_after ||
                !same_inventory_snapshot(stored.snapshot, *link_after) ||
                observed.value->provenance.tag.raw_tag !=
                    stored.snapshot.reparse_tag ||
                observed.value->provenance.tag.directory !=
                    stored.snapshot.directory) {
                return std::nullopt;
            }

            const auto& observation = *observed.value;
            std::optional<Snapshot> target_snapshot;
            std::optional<std::filesystem::path> target_path;
            HANDLE target_handle = INVALID_HANDLE_VALUE;
            if (observation.reachability ==
                    WindowsReparseTargetReachability::reachable &&
                observation.target_identity) {
                target_handle = static_cast<HANDLE>(
                    windows_reparse_target_native_final_handle(observation));
                target_snapshot = snapshot(target_handle);
                target_path = final_path(target_handle);
                if (!target_snapshot || !target_path ||
                    target_snapshot->volume !=
                        observation.target_identity->volume_serial ||
                    target_snapshot->id != observation.target_identity->file_id ||
                    target_snapshot->directory !=
                        observation.target_identity->directory) {
                    return std::nullopt;
                }
            }

            bool outside = true;
            if (target_path) {
                outside = !lexical_path_is_within(*target_path, *root_final);
            }
            if (!outside) ++scan.contained_target_count;

            inventory_record << wide_hex(relative) << ':'
                             << identity(stored.snapshot);
            source_inventory_records.emplace_back(
                relative.native(), inventory_record.str());

            StockExternalTargetReview review{};
            PinnedTargetInventory pinned_inventory{};
            if (outside) {
                if (scan.targets.size() >=
                    reparse_limits.maximum_diagnostic_targets) {
                    return std::nullopt;
                }
                review.source_link_relative_path = relative;
                review.reparse_observation = observation;
                review.diagnostic_complete = observation.observation_complete;
                if (target_path && target_snapshot) {
                    review.target_root = *target_path;
                    if (!scan_target(source, *target_path, *target_snapshot,
                                     target_handle, limits, reparse_limits,
                                     review, pinned_inventory)) {
                        return std::nullopt;
                    }
                    pinned_inventory.source_link_relative_path = relative;
                    review.inventory_available =
                        !review.target_inventory_sha256.empty();
                } else {
                    review.inventory_available = false;
                    review.classification = diagnostic_classification(
                        observation.diagnostic_classification);
                    review.failure_witness = make_failure_witness(
                        private_entry_path, observation);
                }
                if (review.failure_witness &&
                    ++failure_witness_count >
                        reparse_limits.maximum_failure_witnesses) {
                    return std::nullopt;
                }
                review.link_identity_sha256 = identity(stored.snapshot);
                if (target_snapshot) {
                    review.target_identity_sha256 = identity(*target_snapshot);
                }
                scan.eligible = scan.eligible && review.eligible;
                scan.targets.push_back(std::move(review));
                if (target_path && target_snapshot) {
                    scan.pinned_target_inventories.push_back(
                        std::move(pinned_inventory));
                }
                continue;
            }

            scan.contained_target_pins.push_back(observation);
            if (stored.snapshot.directory && target_snapshot &&
                target_snapshot->directory) {
                const auto target_directory_identity =
                    stable_directory_identity(*target_snapshot);
                if (std::ranges::find(directory.ancestry,
                                      target_directory_identity) !=
                    directory.ancestry.end()) {
                    return std::nullopt;
                }
                auto ancestry = directory.ancestry;
                ancestry.push_back(target_directory_identity);
                const auto retained_target_handle = static_cast<HANDLE>(
                    windows_reparse_target_native_final_handle(
                        scan.contained_target_pins.back()));
                pending.push_back({retained_target_handle, relative,
                                   std::move(ancestry)});
            }
        }
    }

    for (auto& target_review : scan.targets) {
        const auto link = std::ranges::find_if(
            scan.source_inventory_entries, [&](const auto& candidate) {
                return candidate.relative_path ==
                       target_review.source_link_relative_path;
            });
        if (link == scan.source_inventory_entries.end() ||
            link->snapshot.reparse_tag == 0U ||
            identity(link->snapshot) != target_review.link_identity_sha256) {
            return std::nullopt;
        }
        const auto link_after = snapshot(link->handle.get());
        if (!link_after ||
            !same_inventory_snapshot(link->snapshot, *link_after) ||
            !target_review.reparse_observation) {
            return std::nullopt;
        }
        if (target_review.inventory_available) {
            const auto pinned = std::ranges::find_if(
                scan.pinned_target_inventories,
                [&](const auto& candidate) {
                    return candidate.source_link_relative_path ==
                           target_review.source_link_relative_path;
                });
            if (pinned == scan.pinned_target_inventories.end() ||
                !target_review.reparse_observation) {
                return std::nullopt;
            }
            const auto followed_handle = static_cast<HANDLE>(
                windows_reparse_target_native_final_handle(
                    *target_review.reparse_observation));
            const auto followed_snapshot = snapshot(followed_handle);
            const auto followed_path = final_path(followed_handle);
            if (!followed_snapshot || !followed_path ||
                lowercase(followed_path->native()) !=
                    lowercase(target_review.target_root.native()) ||
                identity(*followed_snapshot) !=
                    target_review.target_identity_sha256 ||
                !revalidate_pinned_target_inventory(*pinned)) {
                return std::nullopt;
            }
        }
    }

    std::ranges::sort(source_inventory_records, {},
                      [](const auto& record) { return record.first; });
    std::ostringstream source_inventory;
    for (const auto& record : source_inventory_records) {
        source_inventory << record.second << '\n';
    }
    if (!revalidate_pinned_source_inventory(scan)) {
        return std::nullopt;
    }
    std::ranges::sort(scan.targets, {}, [](const auto& t) { return t.source_link_relative_path.native(); });
    for (std::size_t index = 0U; index < scan.targets.size(); ++index) {
        if (scan.targets[index].failure_witness) {
            scan.targets[index].failure_witness->target_ordinal = index + 1U;
        }
    }
    scan.source_inventory_sha256 = hash_text(source_inventory.str());
    scan.source_entry_count = source_entries;
    scan.source_byte_count = source_bytes;
    if (scan.source_inventory_sha256.empty()) return std::nullopt;
    return scan;
}

[[nodiscard]] bool same_scan(const Scan& before, const Scan& after) noexcept
{
    if (before.source_final_path.native() !=
            after.source_final_path.native() ||
        before.source_identity != after.source_identity ||
        before.source_inventory_sha256 !=
            after.source_inventory_sha256 ||
        before.source_entry_count != after.source_entry_count ||
        before.source_byte_count != after.source_byte_count ||
        before.internal_reparse_count != after.internal_reparse_count ||
        before.contained_target_count != after.contained_target_count ||
        before.hardlink_count != after.hardlink_count ||
        before.alternate_data_stream_count !=
            after.alternate_data_stream_count ||
        before.eligible != after.eligible ||
        before.targets.size() != after.targets.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < before.targets.size(); ++index) {
        const auto& left = before.targets[index];
        const auto& right = after.targets[index];
        if (left.source_link_relative_path.native() !=
                right.source_link_relative_path.native() ||
            left.target_root.native() != right.target_root.native() ||
            left.link_identity_sha256 != right.link_identity_sha256 ||
            left.target_identity_sha256 != right.target_identity_sha256 ||
            left.target_inventory_sha256 !=
                right.target_inventory_sha256 ||
            left.classification != right.classification ||
            left.entry_count != right.entry_count ||
            left.byte_count != right.byte_count ||
            left.executable_count != right.executable_count ||
            left.script_or_command_count !=
                right.script_or_command_count ||
            left.mutable_state_count != right.mutable_state_count ||
            left.nested_link_count != right.nested_link_count ||
            left.inventory_available != right.inventory_available ||
            left.diagnostic_complete != right.diagnostic_complete ||
            left.reparse_observation.has_value() !=
                right.reparse_observation.has_value() ||
            (left.reparse_observation &&
             observation_binding(*left.reparse_observation) !=
                 observation_binding(*right.reparse_observation)) ||
            left.failure_witness.has_value() !=
                right.failure_witness.has_value() ||
            (left.failure_witness &&
             left.failure_witness->private_witness_sha256 !=
                 right.failure_witness->private_witness_sha256) ||
            left.eligible != right.eligible) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool revalidate_scan_target_pins(const Scan& scan) noexcept
{
    for (const auto& target : scan.targets) {
        if (!target.inventory_available) continue;
        const auto inventory = std::ranges::find_if(
            scan.pinned_target_inventories, [&](const auto& candidate) {
                return candidate.source_link_relative_path ==
                       target.source_link_relative_path;
            });
        if (inventory == scan.pinned_target_inventories.end() ||
            !target.reparse_observation ||
            !revalidate_pinned_target_inventory(*inventory)) {
            return false;
        }
        const auto target_handle = static_cast<HANDLE>(
            windows_reparse_target_native_final_handle(
                *target.reparse_observation));
        const auto target_snapshot = snapshot(target_handle);
        if (!target_snapshot ||
            identity(*target_snapshot) != target.target_identity_sha256) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::wstring> random_leaf()
{
    std::array<unsigned char, 16> bytes{};
    if (::BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                          BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return std::nullopt;
    static constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring out; out.reserve(32U);
    for (const auto b : bytes) { out.push_back(digits[b >> 4U]); out.push_back(digits[b & 15U]); }
    return out;
}

[[nodiscard]] bool capability_lock_leaf(const std::wstring_view value) noexcept
{
    constexpr std::wstring_view prefix = L".hlclient-output-capability-";
    constexpr std::wstring_view suffix = L".lock";
    if (!value.starts_with(prefix) || !value.ends_with(suffix) ||
        value.size() != prefix.size() + 32U + suffix.size()) {
        return false;
    }
    const auto nonce = value.substr(prefix.size(), 32U);
    return std::ranges::all_of(nonce, [](const wchar_t item) {
        return (item >= L'0' && item <= L'9') ||
               (item >= L'a' && item <= L'f');
    });
}

// Keeps every exact pre-commit review artifact pinned until the summary
// manifest is accepted. Failure removes those exact identities first; after
// the output capability releases its private lock, this guard removes the now
// empty random review root. It deliberately never deletes unowned children.
class UncommittedReviewPublication final {
public:
    explicit UncommittedReviewPublication(
        const std::filesystem::path& review_root) noexcept
        : review_root_{&review_root}
    {
    }

    UncommittedReviewPublication(
        const UncommittedReviewPublication&) = delete;
    UncommittedReviewPublication& operator=(
        const UncommittedReviewPublication&) = delete;

    ~UncommittedReviewPublication()
    {
        if (committed_) return;
        for (auto iterator = published_files_.rbegin();
             iterator != published_files_.rend(); ++iterator) {
            if (iterator->valid()) {
                static_cast<void>(iterator->remove_on_close());
            }
        }
        published_files_.clear();
        if (review_root_ != nullptr) {
            static_cast<void>(::RemoveDirectoryW(review_root_->c_str()));
        }
    }

    [[nodiscard]] bool reserve(const std::size_t count) noexcept
    {
        try {
            published_files_.reserve(count);
            return true;
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] bool retain(SecureOutputPublishedFile&& file) noexcept
    {
        if (!file.valid()) {
            return false;
        }
        if (published_files_.size() == published_files_.capacity()) {
            static_cast<void>(file.remove_on_close());
            return false;
        }
        try {
            published_files_.push_back(std::move(file));
            return true;
        } catch (...) {
            if (file.valid()) {
                static_cast<void>(file.remove_on_close());
            }
            return false;
        }
    }

    void commit() noexcept
    {
        for (auto& file : published_files_) file.close();
        published_files_.clear();
        committed_ = true;
    }

private:
    const std::filesystem::path* review_root_{nullptr};
    std::vector<SecureOutputPublishedFile> published_files_;
    bool committed_{false};
};

[[nodiscard]] bool review_root_layout(
    const std::filesystem::path& root) noexcept
{
    const auto leaf = root.filename().native();
    if (leaf.size() != 32U ||
        !std::ranges::all_of(leaf, [](const wchar_t item) {
            return (item >= L'0' && item <= L'9') ||
                   (item >= L'a' && item <= L'f');
        })) {
        return false;
    }
    return lowercase(root.parent_path().filename().native()) ==
               L"stock-runtime-source-review" &&
           lowercase(
               root.parent_path().parent_path().filename().native()) ==
               L"manual-artifacts";
}

[[nodiscard]] bool exact_review_artifact_set_for_phase(
    const std::filesystem::path& review_root,
    const std::size_t target_count,
    const bool summary_expected,
    const bool approval_expected) noexcept
{
    // Known limitation: this is a point-in-time directory enumeration, not a
    // retained directory oplock or USN-journal witness. Every observed extra
    // entry fails closed, but a brand-new child inserted after enumeration can
    // still race the manifest-last commit boundary.
    try {
        std::set<std::wstring> expected{
            std::wstring{kStockExternalReviewRequestLeaf}};
        if (summary_expected) {
            expected.insert(std::wstring{kStockExternalReviewSummaryLeaf});
        }
        for (std::size_t index = 0U; index < target_count; ++index) {
            const auto leaf = stock_external_private_target_leaf(index + 1U);
            if (!leaf || !expected.insert(*leaf).second) return false;
        }
        if (approval_expected) {
            expected.insert(std::wstring{kStockExternalApprovalLeaf});
        }
        WIN32_FIND_DATAW data{};
        const auto wildcard = review_root / L"*";
        const HANDLE raw = ::FindFirstFileExW(
            wildcard.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch,
            nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (raw == INVALID_HANDLE_VALUE) return false;
        std::set<std::wstring> observed;
        std::size_t capability_lock_count = 0U;
        bool valid = true;
        for (;;) {
            const std::wstring_view name{data.cFileName};
            if (name != L"." && name != L"..") {
                if (capability_lock_leaf(name)) {
                    if (++capability_lock_count != 1U) valid = false;
                } else {
                    if (!expected.contains(std::wstring{name}) ||
                        !observed.insert(std::wstring{name}).second) {
                        valid = false;
                    }
                }
            }
            if (::FindNextFileW(raw, &data) == FALSE) {
                if (::GetLastError() != ERROR_NO_MORE_FILES) valid = false;
                break;
            }
        }
        static_cast<void>(::FindClose(raw));
        return valid && capability_lock_count == 1U && observed == expected;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool exact_review_artifact_set(
    const std::filesystem::path& review_root,
    const std::size_t target_count,
    const bool approval_expected) noexcept
{
    return exact_review_artifact_set_for_phase(
        review_root, target_count, true, approval_expected);
}

[[nodiscard]] std::uint64_t unix_now() noexcept
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

[[nodiscard]] std::span<const std::byte> as_bytes(const std::string& value)
{
    return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
}

struct NormalizedReviewArtifacts final {
    StockExternalReviewRequestArtifact request;
    StockExternalReviewSummaryArtifact summary;
    bool version2{false};
    std::optional<StockExternalReviewSummaryArtifactV2> summary_v2;
};

[[nodiscard]] std::optional<NormalizedReviewArtifacts>
parse_normalized_review_artifacts(
    const std::string_view request_json,
    const std::string_view summary_json) noexcept
{
    const auto request_v1 =
        parse_stock_external_review_request(request_json);
    const auto summary_v1 =
        parse_stock_external_review_summary(summary_json);
    if (request_v1 && summary_v1) {
        return NormalizedReviewArtifacts{
            *request_v1.value, *summary_v1.value, false, std::nullopt};
    }
    const auto request_v2 =
        parse_stock_external_review_request_v2(request_json);
    const auto summary_v2 =
        parse_stock_external_review_summary_v2(summary_json);
    if (!request_v2 || !summary_v2) return std::nullopt;
    StockExternalReviewSummaryArtifact normalized{};
    normalized.review_root_fingerprint =
        summary_v2.value->review_root_fingerprint;
    normalized.source_root_fingerprint =
        summary_v2.value->source_root_fingerprint;
    normalized.source_inventory = summary_v2.value->source_inventory;
    normalized.review_nonce = summary_v2.value->review_nonce;
    normalized.review_timestamp_unix_seconds =
        summary_v2.value->review_timestamp_unix_seconds;
    normalized.implementation_profile =
        summary_v2.value->implementation_profile;
    normalized.eligible_count = summary_v2.value->eligible_count;
    normalized.ineligible_count =
        summary_v2.value->ineligible_count +
        summary_v2.value->incomplete_count;
    normalized.all_targets_eligible =
        summary_v2.value->all_targets_eligible;
    normalized.targets.reserve(summary_v2.value->targets.size());
    for (const auto& target : summary_v2.value->targets) {
        normalized.targets.push_back(
            StockExternalReviewTargetBindingArtifact{
                target.ordinal,
                target.private_record_sha256,
                target.link_identity_sha256,
                target.target_identity_sha256.value_or(std::string{}),
                target.target_inventory_sha256.value_or(std::string{}),
                target.classification,
                target.eligible});
    }
    return NormalizedReviewArtifacts{
        *request_v2.value, std::move(normalized), true,
        *summary_v2.value};
}

[[nodiscard]] std::optional<StockExternalPrivateTargetArtifact>
parse_normalized_private_target(
    const std::string_view json,
    const bool version2) noexcept
{
    if (!version2) {
        const auto parsed = parse_stock_external_private_target(json);
        return parsed ? parsed.value : std::nullopt;
    }
    const auto parsed = parse_stock_external_private_target_v2(json);
    if (!parsed || !parsed.value->target_canonical_path ||
        !parsed.value->target_identity ||
        !parsed.value->target_inventory ||
        !parsed.value->witness_sha256.empty() ||
        parsed.value->reachability != "reachable" ||
        !parsed.value->diagnostic_complete || !parsed.value->eligible) {
        return std::nullopt;
    }
    StockExternalPrivateTargetArtifact normalized{};
    normalized.ordinal = parsed.value->ordinal;
    normalized.review_nonce = parsed.value->review_nonce;
    normalized.source_root_fingerprint =
        parsed.value->source_root_fingerprint;
    normalized.source_link_relative_path =
        parsed.value->source_link_relative_path;
    normalized.source_link_identity = parsed.value->source_link_identity;
    normalized.target_canonical_path =
        *parsed.value->target_canonical_path;
    normalized.target_identity = *parsed.value->target_identity;
    normalized.target_inventory = *parsed.value->target_inventory;
    normalized.classification = parsed.value->classification;
    normalized.eligible = parsed.value->eligible;
    return normalized;
}

} // namespace

class StockExternalSourceDiagnosticSessionPin final {
public:
    explicit StockExternalSourceDiagnosticSessionPin(Scan&& scan) noexcept
        : scan_{std::move(scan)}
    {
    }

private:
    Scan scan_;
};

std::string_view to_string(const StockExternalTargetClassification v) noexcept
{
    switch (v) {
    case StockExternalTargetClassification::eligible_non_executable_asset_tree: return "eligible_non_executable_asset_tree";
    case StockExternalTargetClassification::contains_executable_code: return "contains_executable_code";
    case StockExternalTargetClassification::contains_script_or_command: return "contains_script_or_command";
    case StockExternalTargetClassification::contains_mutable_user_state: return "contains_mutable_user_state";
    case StockExternalTargetClassification::another_application_tree: return "another_application_tree";
    case StockExternalTargetClassification::operating_system_tree: return "operating_system_tree";
    case StockExternalTargetClassification::temporary_or_cache_tree: return "temporary_or_cache_tree";
    case StockExternalTargetClassification::remote_or_device_target: return "remote_or_device_target";
    case StockExternalTargetClassification::nested_external_link: return "nested_external_link";
    case StockExternalTargetClassification::unsupported_reparse_topology: return "unsupported_reparse_topology";
    case StockExternalTargetClassification::content_limit_exceeded: return "content_limit_exceeded";
    case StockExternalTargetClassification::changed_during_review: return "changed_during_review";
    case StockExternalTargetClassification::unknown: return "unknown";
    }
    return "unknown";
}

std::string_view to_string(const StockExternalReviewErrorCode v) noexcept
{
    switch (v) {
    case StockExternalReviewErrorCode::none: return "none";
    case StockExternalReviewErrorCode::invalid_argument: return "invalid_argument";
    case StockExternalReviewErrorCode::source_invalid: return "source_invalid";
    case StockExternalReviewErrorCode::output_parent_invalid: return "output_parent_invalid";
    case StockExternalReviewErrorCode::review_root_invalid: return "review_root_invalid";
    case StockExternalReviewErrorCode::topology_read_failed: return "topology_read_failed";
    case StockExternalReviewErrorCode::no_external_targets: return "no_external_targets";
    case StockExternalReviewErrorCode::target_ineligible: return "target_ineligible";
    case StockExternalReviewErrorCode::target_changed: return "target_changed";
    case StockExternalReviewErrorCode::limit_exceeded: return "limit_exceeded";
    case StockExternalReviewErrorCode::random_failed: return "random_failed";
    case StockExternalReviewErrorCode::publication_failed: return "publication_failed";
    case StockExternalReviewErrorCode::review_manifest_invalid: return "review_manifest_invalid";
    case StockExternalReviewErrorCode::approval_phrase_mismatch: return "approval_phrase_mismatch";
    case StockExternalReviewErrorCode::approval_expired: return "approval_expired";
    case StockExternalReviewErrorCode::approval_mismatch: return "approval_mismatch";
    }
    return "unknown";
}

StockExternalReviewResult<StockExternalSourceDiagnostic>
diagnose_stock_external_targets(
    const std::filesystem::path& source,
    const StockResearchCopyLimits& limits) noexcept
{
    try {
        if (!source.is_absolute()) {
            return {{}, StockExternalReviewErrorCode::invalid_argument, 0U};
        }
        auto source_preflight = open_path(source, true, true);
        const auto source_preflight_snapshot =
            source_preflight.valid() ? snapshot(source_preflight.get())
                                     : std::nullopt;
        if (!source_preflight_snapshot ||
            !source_preflight_snapshot->directory ||
            (source_preflight_snapshot->attributes &
             FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
            return {{}, StockExternalReviewErrorCode::source_invalid,
                    source_preflight.valid() ? ERROR_FILE_INVALID
                                             : ::GetLastError()};
        }
        const auto initial = scan_source(source, limits);
        if (!initial) {
            return {{}, StockExternalReviewErrorCode::topology_read_failed,
                    ::GetLastError()};
        }
        auto confirmed = scan_source(source, limits);
        if (!confirmed || !same_scan(*initial, *confirmed) ||
            !revalidate_pinned_source_inventory(*initial) ||
            !revalidate_pinned_source_inventory(*confirmed) ||
            !revalidate_scan_target_pins(*initial) ||
            !revalidate_scan_target_pins(*confirmed)) {
            return {{}, StockExternalReviewErrorCode::target_changed,
                    ERROR_FILE_INVALID};
        }
        StockExternalSourceDiagnostic diagnostic{};
        diagnostic.source_identity_sha256 = confirmed->source_identity;
        diagnostic.source_inventory_sha256 =
            confirmed->source_inventory_sha256;
        diagnostic.source_entry_count = confirmed->source_entry_count;
        diagnostic.source_byte_count = confirmed->source_byte_count;
        diagnostic.internal_reparse_count =
            confirmed->internal_reparse_count;
        diagnostic.contained_target_count =
            confirmed->contained_target_count;
        diagnostic.hardlink_count = confirmed->hardlink_count;
        diagnostic.alternate_data_stream_count =
            confirmed->alternate_data_stream_count;
        diagnostic.targets = confirmed->targets;
        // This scan proves the opened source node is an ordinary directory on
        // a fixed-volume drive, but it does not independently attest every
        // requested-path ancestor or SUBST semantics.  The source validator
        // must combine this with its exact-root topology proof.
        diagnostic.exact_local_fixed_root = false;
        diagnostic.root_reparse = false;
        diagnostic.source_inventory_complete = true;
        diagnostic.all_targets_diagnostic_complete = std::ranges::all_of(
            diagnostic.targets, [](const auto& target) {
                return target.diagnostic_complete;
            });
        diagnostic.all_targets_eligible =
            !diagnostic.targets.empty() && confirmed->eligible &&
            diagnostic.all_targets_diagnostic_complete;
        diagnostic.private_session_pin =
            std::make_shared<StockExternalSourceDiagnosticSessionPin>(
                std::move(*confirmed));
        return {std::move(diagnostic), StockExternalReviewErrorCode::none,
                0U};
    } catch (...) {
        return {{}, StockExternalReviewErrorCode::topology_read_failed, 0U};
    }
}

StockExternalReviewResult<StockExternalReviewSummary> review_stock_external_targets(
    const std::filesystem::path& source, const std::filesystem::path& parent,
    const StockResearchCopyLimits& limits,
    const StockExternalPublicationTestHook* const publication_test_hook) noexcept
{
    try {
        if (!source.is_absolute() || !parent.is_absolute() ||
            lowercase(parent.filename().native()) !=
                L"stock-runtime-source-review" ||
            lowercase(parent.parent_path().filename().native()) !=
                L"manual-artifacts") {
            return {{}, StockExternalReviewErrorCode::invalid_argument, 0U};
        }
        auto source_preflight = open_path(source, true, true);
        const auto source_preflight_snapshot =
            source_preflight.valid() ? snapshot(source_preflight.get())
                                     : std::nullopt;
        if (!source_preflight_snapshot ||
            !source_preflight_snapshot->directory ||
            (source_preflight_snapshot->attributes &
             FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
            return {{}, StockExternalReviewErrorCode::source_invalid,
                    source_preflight.valid() ? ERROR_FILE_INVALID
                                             : ::GetLastError()};
        }
        const auto initial_scan = scan_source(source, limits);
        if (!initial_scan) {
            return {{}, StockExternalReviewErrorCode::topology_read_failed,
                    ::GetLastError()};
        }
        if (initial_scan->targets.empty()) {
            return {{}, StockExternalReviewErrorCode::no_external_targets, 0U};
        }
        const auto scan = scan_source(source, limits);
        if (!scan || !same_scan(*initial_scan, *scan) ||
            !revalidate_pinned_source_inventory(*initial_scan) ||
            !revalidate_pinned_source_inventory(*scan) ||
            !revalidate_scan_target_pins(*initial_scan) ||
            !revalidate_scan_target_pins(*scan)) {
            return {{}, StockExternalReviewErrorCode::target_changed,
                    ERROR_FILE_INVALID};
        }

        auto overlap_source =
            open_path_rename_guard(scan->source_final_path, true, true);
        const auto manual_artifacts = parent.parent_path();
        const auto repository_root = manual_artifacts.parent_path();
        auto repository_guard =
            open_path_rename_guard(repository_root, true, true);
        const auto overlap_source_snapshot =
            overlap_source.valid() ? snapshot(overlap_source.get())
                                   : std::nullopt;
        const auto repository_snapshot =
            repository_guard.valid() ? snapshot(repository_guard.get())
                                     : std::nullopt;
        const auto overlap_source_path =
            overlap_source.valid() ? physical_path(overlap_source.get())
                                   : std::nullopt;
        const auto repository_path =
            repository_guard.valid() ? physical_path(repository_guard.get())
                                     : std::nullopt;
        std::filesystem::path prospective_parent_path;
        std::filesystem::path prospective_manual_artifacts_path;
        if (repository_path) {
            prospective_manual_artifacts_path =
                (*repository_path / manual_artifacts.filename())
                    .lexically_normal();
            prospective_parent_path =
                (prospective_manual_artifacts_path / parent.filename())
                    .lexically_normal();
        }
        const DWORD initial_manual_artifacts_attributes =
            ::GetFileAttributesW(manual_artifacts.c_str());
        const DWORD initial_manual_artifacts_error =
            initial_manual_artifacts_attributes == INVALID_FILE_ATTRIBUTES
                ? ::GetLastError()
                : ERROR_SUCCESS;
        const bool manual_artifacts_missing =
            initial_manual_artifacts_attributes == INVALID_FILE_ATTRIBUTES &&
            (initial_manual_artifacts_error == ERROR_FILE_NOT_FOUND ||
             initial_manual_artifacts_error == ERROR_PATH_NOT_FOUND);
        const DWORD initial_parent_attributes =
            ::GetFileAttributesW(parent.c_str());
        const DWORD initial_parent_error =
            initial_parent_attributes == INVALID_FILE_ATTRIBUTES
                ? ::GetLastError()
                : ERROR_SUCCESS;
        const bool parent_missing =
            initial_parent_attributes == INVALID_FILE_ATTRIBUTES &&
            (initial_parent_error == ERROR_FILE_NOT_FOUND ||
             initial_parent_error == ERROR_PATH_NOT_FOUND);
        if (!overlap_source_snapshot || !repository_snapshot ||
            !overlap_source_path || !repository_path ||
            !overlap_source_snapshot->directory ||
            !repository_snapshot->directory ||
            identity(*overlap_source_snapshot) != scan->source_identity ||
            (overlap_source_snapshot->attributes &
             FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
            (repository_snapshot->attributes &
             FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
            (initial_manual_artifacts_attributes == INVALID_FILE_ATTRIBUTES &&
             !manual_artifacts_missing) ||
            (manual_artifacts_missing && !parent_missing) ||
            (initial_parent_attributes == INVALID_FILE_ATTRIBUTES &&
             !parent_missing) ||
            physical_locations_overlap(
                *overlap_source_snapshot, *overlap_source_path,
                *repository_snapshot, prospective_parent_path)) {
            return {{}, StockExternalReviewErrorCode::output_parent_invalid,
                    ERROR_INVALID_PARAMETER};
        }
        for (const auto& target : scan->targets) {
            if (!target.inventory_available) continue;
            if (!target.reparse_observation) {
                return {{},
                        StockExternalReviewErrorCode::output_parent_invalid,
                        ERROR_INVALID_PARAMETER};
            }
            const auto target_handle = static_cast<HANDLE>(
                windows_reparse_target_native_final_handle(
                    *target.reparse_observation));
            const auto target_snapshot = snapshot(target_handle);
            const auto target_path = physical_path(target_handle);
            if (!target_snapshot || !target_path ||
                identity(*target_snapshot) != target.target_identity_sha256 ||
                (target_snapshot->attributes &
                 FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
                physical_locations_overlap(
                    *target_snapshot, *target_path,
                    *repository_snapshot, prospective_parent_path)) {
                return {{},
                        StockExternalReviewErrorCode::output_parent_invalid,
                        ERROR_INVALID_PARAMETER};
            }
        }

        if (manual_artifacts_missing &&
            !::CreateDirectoryW(manual_artifacts.c_str(), nullptr)) {
            const DWORD create_error = ::GetLastError();
            if (create_error != ERROR_ALREADY_EXISTS) {
                return {{}, StockExternalReviewErrorCode::output_parent_invalid,
                        create_error};
            }
        }
        auto manual_artifacts_guard =
            open_path_rename_guard(manual_artifacts, true, true);
        const auto manual_artifacts_snapshot =
            manual_artifacts_guard.valid()
                ? snapshot(manual_artifacts_guard.get())
                : std::nullopt;
        const auto manual_artifacts_path =
            manual_artifacts_guard.valid()
                ? physical_path(manual_artifacts_guard.get())
                : std::nullopt;
        if (!manual_artifacts_snapshot || !manual_artifacts_path ||
            !manual_artifacts_snapshot->directory ||
            (manual_artifacts_snapshot->attributes &
             FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
            lowercase(manual_artifacts_path->native()) !=
                lowercase(prospective_manual_artifacts_path.native())) {
            return {{}, StockExternalReviewErrorCode::output_parent_invalid,
                    ERROR_FILE_INVALID};
        }
        if (parent_missing && !::CreateDirectoryW(parent.c_str(), nullptr)) {
            const DWORD create_error = ::GetLastError();
            if (create_error != ERROR_ALREADY_EXISTS) {
                return {{}, StockExternalReviewErrorCode::output_parent_invalid,
                        create_error};
            }
        }
        auto overlap_parent = open_path_rename_guard(parent, true, true);
        const auto overlap_parent_snapshot =
            overlap_parent.valid() ? snapshot(overlap_parent.get())
                                   : std::nullopt;
        const auto overlap_parent_path =
            overlap_parent.valid() ? physical_path(overlap_parent.get())
                                   : std::nullopt;
        if (!overlap_parent_snapshot || !overlap_parent_path ||
            !overlap_parent_snapshot->directory ||
            (overlap_parent_snapshot->attributes &
             FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
            lowercase(overlap_parent_path->native()) !=
                lowercase(prospective_parent_path.native()) ||
            physical_locations_overlap(
                *overlap_source_snapshot, *overlap_source_path,
                *overlap_parent_snapshot, *overlap_parent_path)) {
            return {{}, StockExternalReviewErrorCode::output_parent_invalid,
                    ERROR_FILE_INVALID};
        }

        auto output = open_secure_output_directory(parent);
        if (!output) {
            return {{}, StockExternalReviewErrorCode::output_parent_invalid,
                    output.error
                        ? output.error->operating_system_error
                        : 0U};
        }
        auto secured_parent =
            open_path(output.directory->canonical_path(), true, true);
        const auto secured_parent_snapshot =
            secured_parent.valid() ? snapshot(secured_parent.get())
                                   : std::nullopt;
        const auto secured_parent_path =
            secured_parent.valid() ? physical_path(secured_parent.get())
                                   : std::nullopt;
        if (!secured_parent_snapshot || !secured_parent_path ||
            !same_object_shape(
                *overlap_parent_snapshot, *secured_parent_snapshot) ||
            lowercase(secured_parent_path->native()) !=
                lowercase(overlap_parent_path->native())) {
            return {{}, StockExternalReviewErrorCode::output_parent_invalid,
                    ERROR_FILE_INVALID};
        }
        const auto leaf = random_leaf();
        if (!leaf) return {{}, StockExternalReviewErrorCode::random_failed, 0U};
        const auto review_root = parent / *leaf;
        if (!::CreateDirectoryW(review_root.c_str(), nullptr))
            return {{}, StockExternalReviewErrorCode::publication_failed, ::GetLastError()};
        UncommittedReviewPublication publication{review_root};
        if (scan->targets.size() >
                (std::numeric_limits<std::size_t>::max)() - 2U ||
            !publication.reserve(scan->targets.size() + 2U)) {
            return {{}, StockExternalReviewErrorCode::publication_failed,
                    ERROR_NOT_ENOUGH_MEMORY};
        }
        auto review_output = open_secure_output_directory(review_root);
        if (!review_output) return {{}, StockExternalReviewErrorCode::publication_failed,
                                   review_output.error ? review_output.error->operating_system_error : 0U};

        auto held_source = open_path(scan->source_final_path, true, true);
        auto held_review_root = open_path(review_root, true, true);
        const auto source_snapshot =
            held_source.valid() ? snapshot(held_source.get()) : std::nullopt;
        const auto review_root_snapshot = held_review_root.valid()
                                              ? snapshot(held_review_root.get())
                                              : std::nullopt;
        const auto source_artifact_identity =
            source_snapshot
                ? artifact_identity(held_source.get(), *source_snapshot)
                : std::nullopt;
        if (!source_snapshot || !review_root_snapshot ||
            !source_artifact_identity ||
            identity(*source_snapshot) != scan->source_identity ||
            !review_root_snapshot->directory ||
            (review_root_snapshot->attributes &
             FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
            return {{}, StockExternalReviewErrorCode::publication_failed,
                    ERROR_FILE_INVALID};
        }
        const auto nonce_wide = random_leaf();
        if (!nonce_wide) {
            return {{}, StockExternalReviewErrorCode::random_failed, 0U};
        }
        const auto encoded_review_nonce = utf8(*nonce_wide);
        if (!encoded_review_nonce) {
            return {{}, StockExternalReviewErrorCode::random_failed, 0U};
        }
        const std::string review_nonce = *encoded_review_nonce;
        const auto review_timestamp = unix_now();
        const auto source_inventory = artifact_inventory(
            scan->source_entry_count, scan->source_byte_count,
            scan->source_inventory_sha256);
        StockExternalReviewRequestArtifact request{};
        request.source_root_identity = *source_artifact_identity;
        request.source_inventory = source_inventory;
        request.review_root_fingerprint =
            stable_directory_identity(*review_root_snapshot);
        request.review_nonce = review_nonce;
        request.review_timestamp_unix_seconds = review_timestamp;
        request.implementation_profile = kExternalReviewImplementationProfileV2;
        request.target_count = scan->targets.size();
        const auto request_json =
            serialize_stock_external_review_request_v2(request);
        if (!request_json) {
            return {{}, StockExternalReviewErrorCode::publication_failed, 0U};
        }
        SecureOutputPublishedFile retained_request;
        const auto request_write = secure_atomic_write_new(
            *review_output.directory, kStockExternalReviewRequestLeaf,
            as_bytes(*request_json.value), retained_request);
        if (!request_write) {
            return {{}, StockExternalReviewErrorCode::publication_failed,
                    request_write.error
                        ? request_write.error->operating_system_error
                        : 0U};
        }
        if (!publication.retain(std::move(retained_request))) {
            return {{}, StockExternalReviewErrorCode::publication_failed,
                    ERROR_NOT_ENOUGH_MEMORY};
        }

        StockExternalReviewSummaryArtifactV2 summary_artifact{};
        summary_artifact.review_root_fingerprint =
            request.review_root_fingerprint;
        summary_artifact.source_root_fingerprint = scan->source_identity;
        summary_artifact.source_inventory = source_inventory;
        summary_artifact.review_nonce = review_nonce;
        summary_artifact.review_timestamp_unix_seconds = review_timestamp;
        summary_artifact.implementation_profile =
            kExternalReviewImplementationProfileV2;
        for (std::size_t index = 0U; index < scan->targets.size(); ++index) {
            const auto& target = scan->targets[index];
            if (!target.reparse_observation ||
                !target.reparse_observation->observation_complete) {
                return {{}, StockExternalReviewErrorCode::publication_failed,
                        ERROR_INVALID_DATA};
            }
            const auto& observation = *target.reparse_observation;
            const auto link_path = source / target.source_link_relative_path;
            auto link = open_path(
                link_path, true, observation.provenance.tag.directory);
            const auto target_root = target.target_root.empty()
                                         ? INVALID_HANDLE_VALUE
                                         : static_cast<HANDLE>(
                                               windows_reparse_target_native_final_handle(
                                                   observation));
            const auto link_snapshot =
                link.valid() ? snapshot(link.get()) : std::nullopt;
            const auto target_snapshot =
                target_root != nullptr && target_root != INVALID_HANDLE_VALUE
                    ? snapshot(target_root)
                    : std::nullopt;
            const auto link_identity =
                link_snapshot ? artifact_identity(link.get(), *link_snapshot)
                              : std::nullopt;
            const auto target_identity = target_snapshot
                                             ? artifact_identity(
                                                   target_root,
                                                   *target_snapshot)
                                             : std::nullopt;
            const auto relative_utf8 =
                utf8(target.source_link_relative_path.generic_wstring());
            const auto target_utf8 = target.target_root.empty()
                                         ? std::optional<std::string>{}
                                         : utf8(target.target_root.native());
            const auto substitute_utf8 =
                utf8(observation.provenance.private_substitute_name);
            const auto print_utf8 =
                utf8(observation.provenance.private_print_name);
            const auto normalized_expression_utf8 = utf8(
                observation.provenance.target_expression.
                    private_normalized_expression);
            if (!link_snapshot || !link_identity || !relative_utf8 ||
                !substitute_utf8 || !print_utf8 ||
                !normalized_expression_utf8 ||
                link_identity->identity_sha256 !=
                    target.link_identity_sha256 ||
                link_snapshot->reparse_tag == 0U ||
                (target.inventory_available &&
                 (!target_snapshot || !target_identity || !target_utf8 ||
                  target_identity->identity_sha256 !=
                      target.target_identity_sha256))) {
                return {{}, StockExternalReviewErrorCode::target_changed,
                        ERROR_FILE_INVALID};
            }
            StockExternalPrivateTargetArtifactV2 private_target{};
            private_target.ordinal = index + 1U;
            private_target.review_nonce = review_nonce;
            private_target.source_root_fingerprint = scan->source_identity;
            private_target.source_link_relative_path = *relative_utf8;
            private_target.source_link_identity = *link_identity;
            private_target.raw_reparse_tag =
                observation.provenance.tag.raw_tag;
            private_target.microsoft_tag =
                observation.provenance.tag.microsoft;
            private_target.name_surrogate_tag =
                observation.provenance.tag.name_surrogate;
            private_target.directory = observation.provenance.tag.directory;
            private_target.payload_byte_count =
                observation.provenance.tag.payload_byte_count;
            private_target.payload_sha256 =
                observation.provenance.private_payload_sha256;
            private_target.tag_category =
                to_string(observation.provenance.tag.category);
            private_target.substitute_name = *substitute_utf8;
            private_target.print_name = *print_utf8;
            private_target.normalized_target_expression =
                *normalized_expression_utf8;
            private_target.expression_kind = to_string(
                observation.provenance.target_expression.kind);
            private_target.reachability = to_string(observation.reachability);
            private_target.failure_phase = to_string(
                target.failure_witness
                    ? target.failure_witness->failure_phase
                    : observation.failure_phase);
            private_target.native_error_category =
                to_string(observation.native_error_category);
            private_target.native_error = observation.native_error;
            private_target.witness_sha256 = target.failure_witness
                                                ? target.failure_witness
                                                      ->private_witness_sha256
                                                : std::string{};
            if (target_identity && target_utf8) {
                private_target.target_canonical_path = *target_utf8;
                private_target.target_identity = *target_identity;
            }
            if (target.inventory_available) {
                private_target.target_inventory = artifact_inventory(
                    target.entry_count, target.byte_count,
                    target.target_inventory_sha256, target.executable_count,
                    target.script_or_command_count,
                    target.mutable_state_count, target.nested_link_count);
            }
            private_target.classification =
                artifact_classification(target.classification);
            private_target.diagnostic_complete = target.diagnostic_complete;
            private_target.eligible = target.eligible;
            const auto private_json =
                serialize_stock_external_private_target_v2(private_target);
            if (!private_json) {
                return {{}, StockExternalReviewErrorCode::publication_failed,
                        0U};
            }
            const auto private_digest = stock_external_artifact_sha256(
                *private_json.value);
            const auto private_leaf =
                stock_external_private_target_leaf(index + 1U);
            if (!private_digest || !private_leaf) {
                return {{}, StockExternalReviewErrorCode::publication_failed,
                        0U};
            }
            SecureOutputPublishedFile retained_private;
            const auto private_write = secure_atomic_write_new(
                *review_output.directory, *private_leaf,
                as_bytes(*private_json.value), retained_private);
            if (!private_write) {
                return {{}, StockExternalReviewErrorCode::publication_failed,
                        private_write.error
                            ? private_write.error->operating_system_error
                            : 0U};
            }
            if (!publication.retain(std::move(retained_private))) {
                return {{}, StockExternalReviewErrorCode::publication_failed,
                        ERROR_NOT_ENOUGH_MEMORY};
            }
            summary_artifact.targets.push_back(
                StockExternalReviewTargetBindingArtifactV2{
                    index + 1U,
                    *private_digest.value,
                    target.link_identity_sha256,
                    target.target_identity_sha256.empty()
                        ? std::optional<std::string>{}
                        : std::optional<std::string>{
                              target.target_identity_sha256},
                    target.inventory_available
                        ? std::optional<std::string>{
                              target.target_inventory_sha256}
                        : std::optional<std::string>{},
                    artifact_classification(target.classification),
                    std::string{to_string(
                        observation.provenance.tag.category)},
                    std::string{to_string(
                        observation.provenance.target_expression.kind)},
                    std::string{to_string(observation.reachability)},
                    std::string{to_string(
                        target.failure_witness
                            ? target.failure_witness->failure_phase
                            : observation.failure_phase)},
                    std::string{to_string(
                        observation.native_error_category)},
                    target.diagnostic_complete,
                    target.inventory_available,
                    target.eligible});
            if (target.diagnostic_complete) {
                ++summary_artifact.completed_count;
            } else {
                ++summary_artifact.incomplete_count;
            }
            if (target.eligible) {
                ++summary_artifact.eligible_count;
            } else if (target.diagnostic_complete) {
                ++summary_artifact.ineligible_count;
            }
        }
        if (publication_test_hook != nullptr &&
            publication_test_hook->callback != nullptr) {
            publication_test_hook->callback(
                StockExternalPublicationTestPhase::
                    review_private_records_published,
                publication_test_hook->context);
        }
        summary_artifact.all_targets_eligible =
            scan->eligible && summary_artifact.incomplete_count == 0U;
        const auto summary_json =
            serialize_stock_external_review_summary_v2(summary_artifact);
        if (!summary_json) {
            return {{}, StockExternalReviewErrorCode::publication_failed, 0U};
        }
        const auto summary_digest =
            stock_external_artifact_sha256(*summary_json.value);
        if (!summary_digest) {
            return {{}, StockExternalReviewErrorCode::publication_failed, 0U};
        }
        if (!revalidate_pinned_source_inventory(*scan) ||
            !revalidate_scan_target_pins(*scan) ||
            !exact_review_artifact_set_for_phase(
                review_root, scan->targets.size(), false, false)) {
            return {{}, StockExternalReviewErrorCode::target_changed,
                    ERROR_FILE_INVALID};
        }
        SecureOutputPublishedFile retained_summary;
        const auto summary_write = secure_atomic_write_new(
            *review_output.directory, kStockExternalReviewSummaryLeaf,
            as_bytes(*summary_json.value), retained_summary);
        if (!summary_write) {
            return {{}, StockExternalReviewErrorCode::publication_failed,
                    summary_write.error
                        ? summary_write.error->operating_system_error
                        : 0U};
        }
        if (!publication.retain(std::move(retained_summary))) {
            return {{}, StockExternalReviewErrorCode::publication_failed,
                    ERROR_NOT_ENOUGH_MEMORY};
        }
        if (publication_test_hook != nullptr &&
            publication_test_hook->callback != nullptr) {
            publication_test_hook->callback(
                StockExternalPublicationTestPhase::review_summary_published,
                publication_test_hook->context);
        }
        if (!exact_review_artifact_set(
                review_root, scan->targets.size(), false)) {
            return {{}, StockExternalReviewErrorCode::publication_failed,
                    ERROR_INVALID_DATA};
        }
        StockExternalReviewSummary summary{};
        summary.review_root = review_root; summary.source_identity_sha256 = scan->source_identity;
        summary.review_set_sha256 = *summary_digest.value;
        summary.targets = scan->targets;
        for (const auto& target : summary.targets) {
            if (target.inventory_available) {
                summary.executable_count += target.executable_count;
                summary.script_or_command_count +=
                    target.script_or_command_count;
                summary.mutable_state_count += target.mutable_state_count;
                summary.nested_link_count += target.nested_link_count;
            }
            if (target.diagnostic_complete) {
                ++summary.completed_target_count;
                if (!target.eligible) ++summary.ineligible_target_count;
            } else {
                ++summary.incomplete_target_count;
            }
        }
        summary.all_targets_eligible =
            scan->eligible && summary.incomplete_target_count == 0U;
        publication.commit();
        return {std::move(summary), StockExternalReviewErrorCode::none, 0U};
    } catch (...) { return {{}, StockExternalReviewErrorCode::topology_read_failed, 0U}; }
}

StockExternalReviewResult<StockExternalApproval> approve_stock_external_target_review(
    const std::filesystem::path& root, const std::string_view phrase,
    const std::chrono::hours lifetime,
    const StockExternalPublicationTestHook* const publication_test_hook) noexcept
{
    try {
        if (!root.is_absolute() || !review_root_layout(root) ||
            phrase != kStockExternalTargetApprovalPhraseV1 ||
            lifetime < std::chrono::hours{1} || lifetime > std::chrono::hours{24 * 7})
            return {{}, phrase != kStockExternalTargetApprovalPhraseV1
                ? StockExternalReviewErrorCode::approval_phrase_mismatch
                : StockExternalReviewErrorCode::invalid_argument, 0U};
        auto output = open_secure_output_directory(root);
        if (!output) return {{}, StockExternalReviewErrorCode::review_root_invalid, 0U};
        const auto request_text = read_stock_external_artifact_leaf(
            root, kStockExternalReviewRequestLeaf);
        const auto summary_text = read_stock_external_artifact_leaf(
            root, kStockExternalReviewSummaryLeaf);
        if (!request_text || !summary_text) {
            return {{}, StockExternalReviewErrorCode::review_manifest_invalid,
                    request_text.native_error != 0U
                        ? request_text.native_error
                        : summary_text.native_error};
        }
        const auto artifacts = parse_normalized_review_artifacts(
            *request_text.value, *summary_text.value);
        const auto summary_digest =
            stock_external_artifact_sha256(*summary_text.value);
        if (!artifacts || !summary_digest ||
            artifacts->request.review_root_fingerprint !=
                artifacts->summary.review_root_fingerprint ||
            artifacts->request.source_root_identity.identity_sha256 !=
                artifacts->summary.source_root_fingerprint ||
            artifacts->request.source_inventory !=
                artifacts->summary.source_inventory ||
            artifacts->request.review_nonce !=
                artifacts->summary.review_nonce ||
            artifacts->request.review_timestamp_unix_seconds !=
                artifacts->summary.review_timestamp_unix_seconds ||
            artifacts->request.implementation_profile !=
                artifacts->summary.implementation_profile ||
            artifacts->request.target_count !=
                artifacts->summary.targets.size() ||
            !artifacts->summary.all_targets_eligible ||
            artifacts->summary.targets.empty()) {
            return {{}, artifacts && !artifacts->summary.all_targets_eligible
                            ? StockExternalReviewErrorCode::target_ineligible
                            : StockExternalReviewErrorCode::
                                  review_manifest_invalid,
                    0U};
        }
        const auto& summary = artifacts->summary;
        if (!exact_review_artifact_set(
                root, summary.targets.size(), false)) {
            return {{}, StockExternalReviewErrorCode::review_manifest_invalid,
                    ERROR_INVALID_DATA};
        }

        StockExternalApprovalArtifact approval_artifact{};
        approval_artifact.review_schema =
            artifacts->version2 ? kStockExternalReviewSummarySchemaV2
                                : kStockExternalReviewSummarySchemaV1;
        approval_artifact.review_version = artifacts->version2 ? 2U : 1U;
        approval_artifact.review_root_fingerprint =
            summary.review_root_fingerprint;
        approval_artifact.review_digest_sha256 = *summary_digest.value;
        approval_artifact.source_root_fingerprint =
            summary.source_root_fingerprint;
        approval_artifact.source_inventory = summary.source_inventory;
        approval_artifact.review_nonce = summary.review_nonce;
        approval_artifact.confirmation_profile =
            kStockExternalApprovalConfirmationProfileV1;
        approval_artifact.implementation_profile =
            summary.implementation_profile;
        for (std::size_t index = 0U;
             index < summary.targets.size(); ++index) {
            const auto& binding = summary.targets[index];
            if (binding.ordinal != index + 1U || !binding.eligible ||
                binding.classification !=
                    StockExternalArtifactClassification::
                        eligible_non_executable_asset_tree) {
                return {{}, StockExternalReviewErrorCode::target_ineligible,
                        0U};
            }
            const auto leaf =
                stock_external_private_target_leaf(binding.ordinal);
            if (!leaf) {
                return {{}, StockExternalReviewErrorCode::
                                review_manifest_invalid,
                        0U};
            }
            const auto private_text =
                read_stock_external_artifact_leaf(root, *leaf);
            const auto private_digest = private_text
                                            ? stock_external_artifact_sha256(
                                                  *private_text.value)
                                            : StockExternalArtifactTextResult{};
            const auto private_target = private_text
                                            ? parse_normalized_private_target(
                                                  *private_text.value,
                                                  artifacts->version2)
                                            : std::optional<
                                                  StockExternalPrivateTargetArtifact>{};
            std::optional<StockExternalPrivateTargetArtifactV2>
                private_target_v2;
            if (private_text && artifacts->version2) {
                const auto parsed_v2 =
                    parse_stock_external_private_target_v2(
                        *private_text.value);
                if (parsed_v2) private_target_v2 = *parsed_v2.value;
            }
            const StockExternalReviewTargetBindingArtifactV2* binding_v2 =
                artifacts->version2 && artifacts->summary_v2 &&
                        index < artifacts->summary_v2->targets.size()
                    ? &artifacts->summary_v2->targets[index]
                    : nullptr;
            if (!private_text || !private_digest || !private_target ||
                *private_digest.value != binding.private_record_sha256 ||
                private_target->ordinal != binding.ordinal ||
                private_target->review_nonce != summary.review_nonce ||
                private_target->source_root_fingerprint !=
                    summary.source_root_fingerprint ||
                private_target->source_link_identity.identity_sha256 !=
                    binding.link_identity_sha256 ||
                private_target->target_identity.identity_sha256 !=
                    binding.target_identity_sha256 ||
                private_target->target_inventory.inventory_sha256 !=
                    binding.target_inventory_sha256 ||
                private_target->classification !=
                    binding.classification ||
                (artifacts->version2 &&
                 (!private_target_v2 || binding_v2 == nullptr ||
                  private_target_v2->tag_category !=
                      binding_v2->tag_category ||
                  private_target_v2->expression_kind !=
                      binding_v2->expression_kind ||
                  private_target_v2->reachability !=
                      binding_v2->reachability ||
                  private_target_v2->failure_phase !=
                      binding_v2->failure_phase ||
                  private_target_v2->native_error_category !=
                      binding_v2->native_error_category ||
                  private_target_v2->target_identity.has_value() !=
                      binding_v2->target_identity_sha256.has_value() ||
                  private_target_v2->target_inventory.has_value() !=
                      binding_v2->inventory_available ||
                  binding_v2->target_inventory_sha256.has_value() !=
                      binding_v2->inventory_available ||
                  private_target_v2->diagnostic_complete !=
                      binding_v2->diagnostic_complete ||
                  private_target_v2->eligible != binding_v2->eligible)) ||
                !private_target->eligible) {
                return {{}, StockExternalReviewErrorCode::
                                review_manifest_invalid,
                        private_text.native_error};
            }
            approval_artifact.approved_targets.push_back(
                StockExternalApprovedTargetBindingArtifact{
                    binding.ordinal, binding.link_identity_sha256,
                    binding.target_identity_sha256,
                    binding.target_inventory_sha256,
                    binding.classification});
        }
        const auto approval_nonce_wide = random_leaf();
        if (!approval_nonce_wide) {
            return {{}, StockExternalReviewErrorCode::random_failed, 0U};
        }
        const auto encoded_approval_nonce = utf8(*approval_nonce_wide);
        if (!encoded_approval_nonce) {
            return {{}, StockExternalReviewErrorCode::random_failed, 0U};
        }
        approval_artifact.approval_nonce = *encoded_approval_nonce;
        if (approval_artifact.approval_nonce ==
            approval_artifact.review_nonce) {
            return {{}, StockExternalReviewErrorCode::random_failed, 0U};
        }
        approval_artifact.approval_timestamp_unix_seconds = unix_now();
        approval_artifact.expiration_unix_seconds =
            approval_artifact.approval_timestamp_unix_seconds +
            static_cast<std::uint64_t>(lifetime.count()) * 3'600ULL;
        approval_artifact.approval_count =
            approval_artifact.approved_targets.size();
        const auto serialized =
            serialize_stock_external_approval(approval_artifact);
        if (!serialized) {
            return {{}, StockExternalReviewErrorCode::review_manifest_invalid,
                    0U};
        }
        if (!exact_review_artifact_set(
                root, summary.targets.size(), false)) {
            return {{}, StockExternalReviewErrorCode::review_manifest_invalid,
                    ERROR_INVALID_DATA};
        }
        SecureOutputPublishedFile retained_approval;
        const auto written = secure_atomic_write_new(
            *output.directory, kStockExternalApprovalLeaf,
            as_bytes(*serialized.value), retained_approval);
        if (!written) return {{}, StockExternalReviewErrorCode::publication_failed,
                             written.error ? written.error->operating_system_error : 0U};
        if (publication_test_hook != nullptr &&
            publication_test_hook->callback != nullptr) {
            publication_test_hook->callback(
                StockExternalPublicationTestPhase::
                    approval_manifest_published,
                publication_test_hook->context);
        }
        if (!exact_review_artifact_set(
                root, summary.targets.size(), true)) {
            const auto removed = retained_approval.remove_on_close();
            return {{}, StockExternalReviewErrorCode::publication_failed,
                    removed.error
                        ? removed.error->operating_system_error
                        : ERROR_INVALID_DATA};
        }
        retained_approval.close();
        StockExternalApproval approval{};
        approval.approval_manifest = root / kStockExternalApprovalLeaf;
        approval.review_set_sha256 = *summary_digest.value;
        approval.expires_at = std::chrono::system_clock::time_point{
            std::chrono::seconds{
                approval_artifact.expiration_unix_seconds}};
        return {std::move(approval), StockExternalReviewErrorCode::none, 0U};
    } catch (...) { return {{}, StockExternalReviewErrorCode::review_manifest_invalid, 0U}; }
}

StockExternalReviewResult<StockExternalApprovalValidation> validate_stock_external_target_approval(
    const std::filesystem::path& approval, const std::filesystem::path& source,
    const StockResearchCopyLimits& limits) noexcept
{
    try {
        if (!approval.is_absolute() ||
            approval.filename() != kStockExternalApprovalLeaf)
            return {{}, StockExternalReviewErrorCode::invalid_argument, 0U};
        const auto root = approval.parent_path();
        if (!review_root_layout(root)) {
            return {{}, StockExternalReviewErrorCode::invalid_argument, 0U};
        }
        auto held = open_secure_output_directory(root);
        if (!held) return {{}, StockExternalReviewErrorCode::review_root_invalid, 0U};
        const auto request_text = read_stock_external_artifact_leaf(
            root, kStockExternalReviewRequestLeaf);
        const auto summary_text = read_stock_external_artifact_leaf(
            root, kStockExternalReviewSummaryLeaf);
        const auto approval_text = read_stock_external_artifact_leaf(
            root, kStockExternalApprovalLeaf);
        if (!request_text || !summary_text || !approval_text) {
            return {{}, StockExternalReviewErrorCode::review_manifest_invalid,
                    request_text.native_error != 0U
                        ? request_text.native_error
                        : (summary_text.native_error != 0U
                               ? summary_text.native_error
                               : approval_text.native_error)};
        }
        const auto artifacts = parse_normalized_review_artifacts(
            *request_text.value, *summary_text.value);
        const auto approved =
            parse_stock_external_approval(*approval_text.value);
        const auto summary_digest =
            stock_external_artifact_sha256(*summary_text.value);
        const auto approval_digest =
            stock_external_artifact_sha256(*approval_text.value);
        const auto now = unix_now();
        if (!artifacts || !approved || !summary_digest ||
            !approval_digest ||
            artifacts->request.review_root_fingerprint !=
                artifacts->summary.review_root_fingerprint ||
            artifacts->request.review_root_fingerprint !=
                approved.value->review_root_fingerprint ||
            artifacts->request.source_root_identity.identity_sha256 !=
                artifacts->summary.source_root_fingerprint ||
            artifacts->summary.source_root_fingerprint !=
                approved.value->source_root_fingerprint ||
            artifacts->request.source_inventory !=
                artifacts->summary.source_inventory ||
            artifacts->summary.source_inventory !=
                approved.value->source_inventory ||
            artifacts->request.review_nonce !=
                artifacts->summary.review_nonce ||
            artifacts->summary.review_nonce !=
                approved.value->review_nonce ||
            artifacts->request.implementation_profile !=
                artifacts->summary.implementation_profile ||
            artifacts->summary.implementation_profile !=
                approved.value->implementation_profile ||
            approved.value->review_schema !=
                (artifacts->version2
                     ? kStockExternalReviewSummarySchemaV2
                     : kStockExternalReviewSummarySchemaV1) ||
            approved.value->review_version !=
                (artifacts->version2 ? 2U : 1U) ||
            approved.value->review_digest_sha256 !=
                *summary_digest.value ||
            approved.value->approval_timestamp_unix_seconds <
                artifacts->request.review_timestamp_unix_seconds ||
            approved.value->approval_timestamp_unix_seconds > now + 300U ||
            artifacts->request.target_count !=
                artifacts->summary.targets.size() ||
            artifacts->summary.targets.size() !=
                approved.value->approved_targets.size() ||
            !artifacts->summary.all_targets_eligible ||
            !exact_review_artifact_set(
                root, artifacts->summary.targets.size(), true)) {
            return {{}, StockExternalReviewErrorCode::approval_mismatch, 0U};
        }
        if (approved.value->expiration_unix_seconds <= now) {
            return {{}, StockExternalReviewErrorCode::approval_expired, 0U};
        }
        const auto& summary = artifacts->summary;
        auto review_root_handle = open_path(root, true, true);
        const auto review_root_snapshot = review_root_handle.valid()
                                              ? snapshot(review_root_handle.get())
                                              : std::nullopt;
        if (!review_root_snapshot || !review_root_snapshot->directory ||
            stable_directory_identity(*review_root_snapshot) !=
                approved.value->review_root_fingerprint) {
            return {{}, StockExternalReviewErrorCode::approval_mismatch,
                    ERROR_FILE_INVALID};
        }
        const auto fresh = scan_source(source, limits);
        if (!fresh || !fresh->eligible ||
            !revalidate_pinned_source_inventory(*fresh) ||
            !revalidate_scan_target_pins(*fresh) ||
            fresh->targets.size() != summary.targets.size() ||
            fresh->source_identity !=
                approved.value->source_root_fingerprint ||
            artifact_inventory(
                fresh->source_entry_count, fresh->source_byte_count,
                fresh->source_inventory_sha256) !=
                approved.value->source_inventory ||
            approved.value->implementation_profile !=
                (artifacts->version2
                     ? kExternalReviewImplementationProfileV2
                     : kExternalReviewImplementationProfileV1)) {
            return {{}, StockExternalReviewErrorCode::approval_mismatch, 0U};
        }

        StockExternalApprovalValidation result{};
        result.review_set_sha256 = *summary_digest.value;
        result.approval_manifest_sha256 = *approval_digest.value;
        result.expires_at = std::chrono::system_clock::time_point{
            std::chrono::seconds{
                approved.value->expiration_unix_seconds}};
        for (std::size_t index = 0U; index < fresh->targets.size(); ++index) {
            const auto& target = fresh->targets[index];
            const auto& summary_binding = summary.targets[index];
            const auto& approval_binding =
                approved.value->approved_targets[index];
            const auto leaf =
                stock_external_private_target_leaf(index + 1U);
            const auto private_text = leaf
                                          ? read_stock_external_artifact_leaf(
                                                root, *leaf)
                                          : StockExternalArtifactTextResult{};
            const auto private_digest = private_text
                                            ? stock_external_artifact_sha256(
                                                  *private_text.value)
                                            : StockExternalArtifactTextResult{};
            const auto private_target = private_text
                                            ? parse_normalized_private_target(
                                                  *private_text.value,
                                                  artifacts->version2)
                                            : std::optional<
                                                  StockExternalPrivateTargetArtifact>{};
            const auto relative_utf8 =
                utf8(target.source_link_relative_path.generic_wstring());
            const auto target_utf8 = utf8(target.target_root.native());
            const auto classification =
                artifact_classification(target.classification);
            const auto target_inventory = artifact_inventory(
                target.entry_count, target.byte_count,
                target.target_inventory_sha256, target.executable_count,
                target.script_or_command_count, target.mutable_state_count,
                target.nested_link_count);
            if (!private_text || !private_digest || !private_target ||
                !relative_utf8 || !target_utf8 ||
                summary_binding.ordinal != index + 1U ||
                approval_binding.ordinal != index + 1U ||
                *private_digest.value !=
                    summary_binding.private_record_sha256 ||
                private_target->ordinal != index + 1U ||
                private_target->review_nonce !=
                    approved.value->review_nonce ||
                private_target->source_root_fingerprint !=
                    approved.value->source_root_fingerprint ||
                private_target->source_link_relative_path !=
                    *relative_utf8 ||
                private_target->target_canonical_path != *target_utf8 ||
                private_target->source_link_identity.identity_sha256 !=
                    target.link_identity_sha256 ||
                private_target->target_identity.identity_sha256 !=
                    target.target_identity_sha256 ||
                private_target->target_inventory != target_inventory ||
                private_target->classification != classification ||
                !private_target->eligible ||
                summary_binding.link_identity_sha256 !=
                    target.link_identity_sha256 ||
                summary_binding.target_identity_sha256 !=
                    target.target_identity_sha256 ||
                summary_binding.target_inventory_sha256 !=
                    target.target_inventory_sha256 ||
                summary_binding.classification != classification ||
                approval_binding.link_identity_sha256 !=
                    target.link_identity_sha256 ||
                approval_binding.target_identity_sha256 !=
                    target.target_identity_sha256 ||
                approval_binding.target_inventory_sha256 !=
                    target.target_inventory_sha256 ||
                approval_binding.classification != classification) {
                return {{}, StockExternalReviewErrorCode::approval_mismatch,
                        private_text.native_error};
            }
            result.targets.push_back({target.source_link_relative_path, target.target_root,
                target.link_identity_sha256, target.target_identity_sha256,
                target.target_inventory_sha256});
            result.executable_count += target.executable_count;
            result.script_or_command_count += target.script_or_command_count;
            result.mutable_state_count += target.mutable_state_count;
            result.nested_link_count += target.nested_link_count;
        }
        if (!revalidate_pinned_source_inventory(*fresh) ||
            !revalidate_scan_target_pins(*fresh)) {
            return {{}, StockExternalReviewErrorCode::target_changed,
                    ERROR_FILE_INVALID};
        }
        return {std::move(result), StockExternalReviewErrorCode::none, 0U};
    } catch (...) { return {{}, StockExternalReviewErrorCode::review_manifest_invalid, 0U}; }
}

} // namespace hlclient::platform::windows
