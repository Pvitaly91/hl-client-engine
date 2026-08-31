#include <hlclient/platform/windows/stock_research_copy.hpp>
#include <hlclient/platform/windows/stock_external_target_review.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <winternl.h>
#include <winioctl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

extern "C" NTSYSAPI NTSTATUS NTAPI NtSetInformationFile(
    HANDLE file_handle,
    PIO_STATUS_BLOCK io_status_block,
    PVOID file_information,
    ULONG length,
    FILE_INFORMATION_CLASS file_information_class);

namespace hlclient::platform::windows {
namespace {

namespace fs = std::filesystem;

inline constexpr std::string_view kCleanupFailureMetadata =
    "{\r\n"
    "  \"schema\":\"hlclient.stock-research-copy-failure.v1\",\r\n"
    "  \"category\":\"cleanup_failed\",\r\n"
    "  \"paths_recorded\":false\r\n"
    "}\r\n";

inline constexpr std::string_view kPendingPreparationMetadata =
    "{\r\n"
    "  \"schema\":\"hlclient.stock-research-copy-pending.v1\",\r\n"
    "  \"category\":\"awaiting_commit_marker\",\r\n"
    "  \"paths_recorded\":false\r\n"
    "}\r\n";

// REPARSE_DATA_BUFFER is exposed by the WDK but not by every desktop Windows
// SDK. This is the documented FSCTL_GET_REPARSE_POINT byte layout; only the two
// Microsoft name-surrogate layouts used below are represented.
struct LocalReparseDataBuffer final {
    ULONG reparse_tag;
    USHORT reparse_data_length;
    USHORT reserved;
    union {
        struct {
            USHORT substitute_name_offset;
            USHORT substitute_name_length;
            USHORT print_name_offset;
            USHORT print_name_length;
            WCHAR path_buffer[1];
        } mount_point;
        struct {
            USHORT substitute_name_offset;
            USHORT substitute_name_length;
            USHORT print_name_offset;
            USHORT print_name_length;
            ULONG flags;
            WCHAR path_buffer[1];
        } symbolic_link;
        struct {
            UCHAR data_buffer[1];
        } generic;
    } payload;
};

inline constexpr std::size_t kReparseHeaderBytes =
    offsetof(LocalReparseDataBuffer, payload.generic.data_buffer);

// These ReadDirectoryChangesW filters are documented by the Windows SDK but
// omitted by some older desktop-only header sets supported by this project.
inline constexpr DWORD kNotifyChangeStreamName = 0x00000200U;
inline constexpr DWORD kNotifyChangeStreamSize = 0x00000400U;
inline constexpr DWORD kNotifyChangeStreamWrite = 0x00000800U;

class UniqueHandle final {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(const HANDLE value) noexcept : value_{value} {}
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
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
    }
    void reset() noexcept
    {
        if (*this) static_cast<void>(::CloseHandle(value_));
        value_ = INVALID_HANDLE_VALUE;
    }

private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

class UniqueFindHandle final {
public:
    explicit UniqueFindHandle(const HANDLE value = INVALID_HANDLE_VALUE) noexcept
        : value_{value}
    {
    }
    UniqueFindHandle(const UniqueFindHandle&) = delete;
    UniqueFindHandle& operator=(const UniqueFindHandle&) = delete;
    ~UniqueFindHandle()
    {
        if (value_ != INVALID_HANDLE_VALUE) {
            static_cast<void>(::FindClose(value_));
        }
    }
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

class UniqueAlgorithm final {
public:
    UniqueAlgorithm() = default;
    UniqueAlgorithm(const UniqueAlgorithm&) = delete;
    UniqueAlgorithm& operator=(const UniqueAlgorithm&) = delete;
    ~UniqueAlgorithm()
    {
        if (value_ != nullptr) {
            static_cast<void>(::BCryptCloseAlgorithmProvider(value_, 0U));
        }
    }
    [[nodiscard]] BCRYPT_ALG_HANDLE* put() noexcept { return &value_; }
    [[nodiscard]] BCRYPT_ALG_HANDLE get() const noexcept { return value_; }

private:
    BCRYPT_ALG_HANDLE value_{nullptr};
};

class UniqueHash final {
public:
    UniqueHash() = default;
    UniqueHash(const UniqueHash&) = delete;
    UniqueHash& operator=(const UniqueHash&) = delete;
    ~UniqueHash()
    {
        if (value_ != nullptr) static_cast<void>(::BCryptDestroyHash(value_));
    }
    [[nodiscard]] BCRYPT_HASH_HANDLE* put() noexcept { return &value_; }
    [[nodiscard]] BCRYPT_HASH_HANDLE get() const noexcept { return value_; }

private:
    BCRYPT_HASH_HANDLE value_{nullptr};
};

struct FileIdentity final {
    std::uint64_t volume_serial{0U};
    std::array<std::byte, 16U> file_id{};

    friend bool operator==(const FileIdentity&, const FileIdentity&) noexcept =
        default;
};

struct EntrySnapshot final {
    FileIdentity identity{};
    std::uint64_t size{0U};
    std::int64_t creation_time{0};
    std::int64_t last_write_time{0};
    std::int64_t change_time{0};
    std::uint32_t attributes{0U};
    std::uint32_t reparse_tag{0U};
    std::uint32_t link_count{0U};
    bool directory{false};

    friend bool operator==(const EntrySnapshot&, const EntrySnapshot&) noexcept =
        default;
};

// A DOS final path is useful for opening descendants, but it is not a
// security identity: the same volume can be exposed through multiple drive
// letters, mount points, or other aliases. Pair the handle-derived volume
// serial with FILE_NAME_NORMALIZED | VOLUME_NAME_NONE so containment and
// overlap decisions use the volume-relative physical namespace instead.
struct PhysicalLocation final {
    std::uint64_t volume_serial{0U};
    fs::path volume_relative_path;

    friend bool operator==(const PhysicalLocation&, const PhysicalLocation&) =
        default;
};

struct LinkWitness final {
    fs::path logical_path;
    EntrySnapshot link_snapshot{};
    EntrySnapshot target_snapshot{};
    fs::path target_final_path;
    fs::path target_volume_relative_path;

    friend bool operator==(const LinkWitness&, const LinkWitness&) = default;
};

struct InventoryEntry final {
    fs::path relative_path;
    fs::path logical_path;
    fs::path physical_path;
    EntrySnapshot snapshot{};
    std::array<std::byte, 32U> digest{};
    std::vector<LinkWitness> witnesses;
    bool directory{false};
    bool source_hardlink{false};

    friend bool operator==(const InventoryEntry&, const InventoryEntry&) =
        default;
};

struct Inventory final {
    StockResearchTopologySummary summary;
    fs::path canonical_root;
    fs::path canonical_root_volume_relative_path;
    EntrySnapshot requested_root_snapshot{};
    EntrySnapshot canonical_root_snapshot{};
    std::vector<InventoryEntry> entries;
    std::array<std::byte, 32U> inventory_digest{};
    bool client_present{false};
    bool server_present{false};
    bool marker_present{false};
};

struct BuildInventoryResult final {
    std::optional<Inventory> inventory;
    StockResearchCopyErrorCode code{StockResearchCopyErrorCode::none};
    DWORD native_error{ERROR_SUCCESS};
};

struct MaterializeFailure final {
    StockResearchCopyErrorCode code{StockResearchCopyErrorCode::none};
    DWORD native_error{ERROR_SUCCESS};
};

[[nodiscard]] StockResearchTopologyResult topology_failure(
    const StockResearchCopyErrorCode code,
    const DWORD native_error = ERROR_SUCCESS) noexcept
{
    return {std::nullopt, code, native_error};
}

[[nodiscard]] StockResearchMaterializationResult materialization_failure(
    const StockResearchCopyErrorCode code,
    const DWORD native_error = ERROR_SUCCESS) noexcept
{
    return {std::nullopt, code, native_error};
}

[[nodiscard]] bool valid_limits(const StockResearchCopyLimits& limits) noexcept
{
    return limits.maximum_entries > 0U && limits.maximum_total_bytes > 0U &&
           limits.maximum_file_bytes > 0U &&
           limits.maximum_file_bytes <= limits.maximum_total_bytes &&
           limits.maximum_reparse_depth > 0U &&
           limits.maximum_path_characters >= 260U &&
           limits.maximum_path_characters <= 32'767U &&
           limits.maximum_streams_per_file > 0U &&
           limits.maximum_streams_per_file <= 1'024U;
}

[[nodiscard]] bool path_size_valid(
    const fs::path& path, const StockResearchCopyLimits& limits) noexcept
{
    return !path.empty() &&
           path.native().size() <= limits.maximum_path_characters;
}

[[nodiscard]] bool ordinal_equal(
    const std::wstring_view left, const std::wstring_view right) noexcept
{
    if (left.size() != right.size() ||
        left.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    return ::CompareStringOrdinal(
               left.data(), static_cast<int>(left.size()), right.data(),
               static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

[[nodiscard]] bool path_equal(
    const fs::path& left, const fs::path& right) noexcept
{
    return ordinal_equal(left.native(), right.native());
}

[[nodiscard]] std::wstring strip_extended_prefix(std::wstring path)
{
    constexpr std::wstring_view unc_prefix{LR"(\\?\UNC\)"};
    constexpr std::wstring_view ordinary_prefix{LR"(\\?\)"};
    if (path.starts_with(unc_prefix)) {
        path = L"\\\\" + path.substr(unc_prefix.size());
    } else if (path.starts_with(ordinary_prefix)) {
        path.erase(0U, ordinary_prefix.size());
    }
    while (path.size() > 3U && (path.back() == L'\\' || path.back() == L'/')) {
        path.pop_back();
    }
    return path;
}

[[nodiscard]] bool query_handle_path(
    const HANDLE handle,
    const DWORD volume_name,
    const bool strip_prefix,
    fs::path& result) noexcept
{
    const DWORD flags = FILE_NAME_NORMALIZED | volume_name;
    const DWORD required = ::GetFinalPathNameByHandleW(handle, nullptr, 0U, flags);
    if (required == 0U || required > 32'767U) return false;
    std::wstring path(static_cast<std::size_t>(required) + 1U, L'\0');
    const DWORD written = ::GetFinalPathNameByHandleW(
        handle, path.data(), static_cast<DWORD>(path.size()), flags);
    if (written == 0U || written >= path.size()) return false;
    path.resize(written);
    if (strip_prefix) path = strip_extended_prefix(std::move(path));
    // Preserve a DOS drive root (for example, "C:\\").  Stripping its only
    // separator would turn the absolute root into the drive-relative "C:" and
    // make an otherwise exact pinned root fail closed for the wrong reason.
    while (path.size() > 3U &&
           (path.back() == L'\\' || path.back() == L'/')) {
        path.pop_back();
    }
    result = fs::path{std::move(path)};
    return true;
}

[[nodiscard]] bool query_final_path(
    const HANDLE handle, fs::path& result) noexcept
{
    return query_handle_path(handle, VOLUME_NAME_DOS, true, result);
}

[[nodiscard]] bool query_volume_relative_path(
    const HANDLE handle, fs::path& result) noexcept
{
    return query_handle_path(handle, VOLUME_NAME_NONE, false, result);
}

[[nodiscard]] bool query_physical_location(
    const HANDLE handle,
    const EntrySnapshot& snapshot,
    PhysicalLocation& result) noexcept
{
    fs::path volume_relative;
    if (!query_volume_relative_path(handle, volume_relative)) return false;
    result = PhysicalLocation{
        snapshot.identity.volume_serial,
        volume_relative.lexically_normal()};
    return true;
}

[[nodiscard]] bool query_snapshot(
    const HANDLE handle, EntrySnapshot& snapshot) noexcept
{
    FILE_ID_INFO file_id{};
    FILE_STANDARD_INFO standard{};
    FILE_BASIC_INFO basic{};
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (::GetFileInformationByHandleEx(
            handle, FileIdInfo, &file_id, sizeof(file_id)) == FALSE ||
        ::GetFileInformationByHandleEx(
            handle, FileStandardInfo, &standard, sizeof(standard)) == FALSE ||
        ::GetFileInformationByHandleEx(
            handle, FileBasicInfo, &basic, sizeof(basic)) == FALSE ||
        ::GetFileInformationByHandleEx(
            handle, FileAttributeTagInfo, &tag, sizeof(tag)) == FALSE ||
        standard.NumberOfLinks == 0U || standard.EndOfFile.QuadPart < 0) {
        return false;
    }
    snapshot.identity.volume_serial = file_id.VolumeSerialNumber;
    for (std::size_t index = 0U; index < snapshot.identity.file_id.size(); ++index) {
        snapshot.identity.file_id[index] =
            static_cast<std::byte>(file_id.FileId.Identifier[index]);
    }
    snapshot.size = static_cast<std::uint64_t>(standard.EndOfFile.QuadPart);
    snapshot.creation_time = basic.CreationTime.QuadPart;
    snapshot.last_write_time = basic.LastWriteTime.QuadPart;
    snapshot.change_time = basic.ChangeTime.QuadPart;
    snapshot.attributes = tag.FileAttributes;
    snapshot.reparse_tag = tag.ReparseTag;
    snapshot.link_count = standard.NumberOfLinks;
    snapshot.directory = standard.Directory != FALSE;
    return true;
}

[[nodiscard]] UniqueHandle open_entry(
    const fs::path& path,
    const bool follow_reparse,
    const bool read_contents = false) noexcept
{
    // Zero desired access is sufficient for the handle information classes
    // used by topology inspection and avoids requiring discretionary read-
    // attributes access on otherwise traversable path ancestors. Content copy
    // upgrades the same open to GENERIC_READ | FILE_READ_ATTRIBUTES.
    DWORD access = 0U;
    if (read_contents) access = GENERIC_READ | FILE_READ_ATTRIBUTES;
    DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
    if (!follow_reparse) flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    if (read_contents) flags |= FILE_FLAG_SEQUENTIAL_SCAN;
    return UniqueHandle{::CreateFileW(
        path.c_str(), access,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, flags, nullptr)};
}

[[nodiscard]] UniqueHandle open_entry_rename_guard(
    const fs::path& path) noexcept
{
    return UniqueHandle{::CreateFileW(
        path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
}

[[nodiscard]] bool valid_relative_leaf(
    const std::wstring_view leaf) noexcept
{
    return !leaf.empty() && leaf != L"." && leaf != L".." &&
           leaf.size() <=
               static_cast<std::size_t>(
                   (std::numeric_limits<USHORT>::max)() / sizeof(wchar_t)) &&
           std::ranges::none_of(leaf, [](const wchar_t value) noexcept {
               return value == L'\\' || value == L'/' || value == L':' ||
                      value == L'\0';
           });
}

[[nodiscard]] UniqueHandle nt_create_relative_entry(
    const HANDLE parent,
    const std::wstring_view leaf,
    const ACCESS_MASK desired_access,
    const ULONG share_access,
    const ULONG disposition,
    const ULONG attributes,
    const ULONG options,
    DWORD& native_error) noexcept
{
    if (parent == nullptr || parent == INVALID_HANDLE_VALUE ||
        !valid_relative_leaf(leaf)) {
        native_error = ERROR_INVALID_NAME;
        ::SetLastError(native_error);
        return {};
    }
    UNICODE_STRING name{};
    name.Length = static_cast<USHORT>(leaf.size() * sizeof(wchar_t));
    name.MaximumLength = name.Length;
    name.Buffer = const_cast<PWSTR>(leaf.data());
    OBJECT_ATTRIBUTES object{};
    InitializeObjectAttributes(
        &object, &name, OBJ_CASE_INSENSITIVE, parent, nullptr);
    IO_STATUS_BLOCK status_block{};
    HANDLE raw = INVALID_HANDLE_VALUE;
    const NTSTATUS status = ::NtCreateFile(
        &raw, desired_access, &object, &status_block, nullptr, attributes,
        share_access, disposition, options, nullptr, 0U);
    if (status < 0) {
        if (raw != nullptr && raw != INVALID_HANDLE_VALUE) {
            static_cast<void>(::CloseHandle(raw));
        }
        native_error = ::RtlNtStatusToDosError(status);
        ::SetLastError(native_error);
        return {};
    }
    if ((disposition == FILE_CREATE &&
         status_block.Information != FILE_CREATED) ||
        (disposition == FILE_OPEN &&
         status_block.Information != FILE_OPENED)) {
        static_cast<void>(::CloseHandle(raw));
        native_error = ERROR_FILE_INVALID;
        ::SetLastError(native_error);
        return {};
    }
    native_error = ERROR_SUCCESS;
    return UniqueHandle{raw};
}

[[nodiscard]] UniqueHandle open_relative_directory_locked(
    const HANDLE parent,
    const std::wstring_view leaf,
    const bool writable,
    DWORD& native_error) noexcept
{
    // Identity queries need read-attributes and a relative open needs
    // traverse.  Do not also ask for LIST_DIRECTORY on every ancestor: that
    // would reject destinations reachable under a token that can traverse but
    // cannot enumerate an ancestor. Only a direct create-parent needs add-entry
    // rights.
    ACCESS_MASK access = FILE_TRAVERSE | FILE_READ_ATTRIBUTES;
    if (writable) access |= FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY;
    return nt_create_relative_entry(
        parent, leaf, access, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN,
        FILE_ATTRIBUTE_DIRECTORY,
        FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT,
        native_error);
}

[[nodiscard]] UniqueHandle create_relative_directory_locked(
    const HANDLE parent,
    const std::wstring_view leaf,
    const bool renameable,
    DWORD& native_error) noexcept
{
    ACCESS_MASK access =
        FILE_TRAVERSE | FILE_READ_ATTRIBUTES | FILE_ADD_FILE |
        FILE_ADD_SUBDIRECTORY;
    if (renameable) access |= DELETE;
    return nt_create_relative_entry(
        parent, leaf, access, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_CREATE,
        FILE_ATTRIBUTE_DIRECTORY,
        FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT |
            (renameable ? FILE_WRITE_THROUGH : 0U),
        native_error);
}

[[nodiscard]] UniqueHandle open_relative_directory_for_publish(
    const HANDLE parent,
    const std::wstring_view leaf,
    const bool request_delete,
    const bool share_delete,
    DWORD& native_error) noexcept
{
    ACCESS_MASK access = FILE_TRAVERSE | FILE_READ_ATTRIBUTES |
                         FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY;
    if (request_delete) access |= DELETE;
    return nt_create_relative_entry(
        parent, leaf, access,
        FILE_SHARE_READ | FILE_SHARE_WRITE |
            (share_delete ? FILE_SHARE_DELETE : 0U),
        FILE_OPEN, FILE_ATTRIBUTE_DIRECTORY,
        FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT | FILE_WRITE_THROUGH,
        native_error);
}

[[nodiscard]] UniqueHandle create_relative_file_locked(
    const HANDLE parent,
    const std::wstring_view leaf,
    DWORD& native_error) noexcept
{
    return nt_create_relative_entry(
        parent, leaf,
        GENERIC_READ | GENERIC_WRITE | FILE_READ_ATTRIBUTES | DELETE |
            SYNCHRONIZE,
        FILE_SHARE_READ, FILE_CREATE, FILE_ATTRIBUTE_NORMAL,
        FILE_NON_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT |
            FILE_SYNCHRONOUS_IO_NONALERT | FILE_WRITE_THROUGH,
        native_error);
}

[[nodiscard]] UniqueHandle open_relative_file_locked(
    const HANDLE parent,
    const std::wstring_view leaf,
    DWORD& native_error) noexcept
{
    return nt_create_relative_entry(
        parent, leaf, GENERIC_READ | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
        FILE_ATTRIBUTE_NORMAL,
        FILE_NON_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT |
            FILE_SYNCHRONOUS_IO_NONALERT,
        native_error);
}

[[nodiscard]] UniqueHandle open_absolute_directory_locked(
    const fs::path& path,
    const bool writable,
    DWORD& native_error) noexcept
{
    DWORD access = FILE_TRAVERSE | FILE_READ_ATTRIBUTES;
    if (writable) access |= FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY;
    UniqueHandle handle{::CreateFileW(
        path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    native_error = handle ? ERROR_SUCCESS : ::GetLastError();
    return handle;
}

[[nodiscard]] bool same_or_below(
    const fs::path& root, const fs::path& candidate) noexcept
{
    const auto root_name = root.native();
    const auto candidate_name = candidate.native();
    if (candidate_name.size() < root_name.size() ||
        root_name.size() >
            static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    if (::CompareStringOrdinal(
            root_name.data(), static_cast<int>(root_name.size()),
            candidate_name.data(), static_cast<int>(root_name.size()), TRUE) !=
        CSTR_EQUAL) {
        return false;
    }
    return candidate_name.size() == root_name.size() ||
           (candidate_name.size() > root_name.size() &&
            (candidate_name[root_name.size()] == L'\\' ||
             candidate_name[root_name.size()] == L'/'));
}

[[nodiscard]] std::optional<fs::path> half_life_appmanifest_path(
    const fs::path& member) noexcept
{
    try {
        auto native = member.lexically_normal().native();
        std::ranges::replace(native, L'/', L'\\');
        auto folded = native;
        std::ranges::transform(
            folded, folded.begin(), [](const wchar_t value) {
                return static_cast<wchar_t>(std::towlower(value));
            });
        constexpr std::wstring_view marker =
            L"\\steamapps\\common\\half-life";
        const auto position = folded.find(marker);
        if (position == std::wstring::npos) return std::nullopt;
        const auto application_end = position + marker.size();
        if (application_end < folded.size() &&
            folded[application_end] != L'\\') {
            return std::nullopt;
        }
        const auto steamapps_end =
            position + std::wstring_view{L"\\steamapps"}.size();
        return fs::path{native.substr(0U, steamapps_end)} /
               L"appmanifest_70.acf";
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<fs::path> half_life_libraryfolders_path(
    const fs::path& member) noexcept
{
    const auto manifest = half_life_appmanifest_path(member);
    return manifest ? std::optional{manifest->parent_path() /
                                    L"libraryfolders.vdf"}
                    : std::nullopt;
}

[[nodiscard]] bool physical_same_or_below(
    const PhysicalLocation& root,
    const PhysicalLocation& candidate) noexcept
{
    return root.volume_serial == candidate.volume_serial &&
           same_or_below(
               root.volume_relative_path, candidate.volume_relative_path);
}

[[nodiscard]] bool physical_overlap(
    const PhysicalLocation& left,
    const PhysicalLocation& right) noexcept
{
    return physical_same_or_below(left, right) ||
           physical_same_or_below(right, left);
}

[[nodiscard]] bool physical_equal(
    const PhysicalLocation& left,
    const PhysicalLocation& right) noexcept
{
    return left.volume_serial == right.volume_serial &&
           path_equal(
               left.volume_relative_path, right.volume_relative_path);
}

[[nodiscard]] bool is_unc_path(const fs::path& path) noexcept
{
    const auto value = path.native();
    return value.starts_with(L"\\\\") || value.starts_with(L"//");
}

[[nodiscard]] bool has_drive_letter(const fs::path& path) noexcept
{
    const auto value = path.native();
    const auto alpha = [](const wchar_t ch) noexcept {
        return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z');
    };
    return value.size() >= 3U && alpha(value[0U]) && value[1U] == L':' &&
           (value[2U] == L'\\' || value[2U] == L'/');
}

[[nodiscard]] bool is_substituted_drive(const fs::path& path) noexcept
{
    if (!has_drive_letter(path)) return false;
    std::array<wchar_t, 32'768U> target{};
    const std::array<wchar_t, 3U> drive{path.native()[0U], L':', L'\0'};
    const DWORD size = ::QueryDosDeviceW(
        drive.data(), target.data(), static_cast<DWORD>(target.size()));
    if (size == 0U) return true;
    return std::wstring_view{target.data()}.starts_with(LR"(\??\)");
}

[[nodiscard]] bool local_fixed_volume(const fs::path& path) noexcept
{
    std::array<wchar_t, 32'768U> volume_root{};
    if (::GetVolumePathNameW(
            path.c_str(), volume_root.data(),
            static_cast<DWORD>(volume_root.size())) == FALSE) {
        return false;
    }
    return ::GetDriveTypeW(volume_root.data()) == DRIVE_FIXED;
}

void add_category(
    StockResearchTopologySummary& summary,
    const StockResearchTopologyCategory category)
{
    if (std::ranges::find(summary.categories, category) ==
        summary.categories.end()) {
        summary.categories.push_back(category);
    }
}

[[nodiscard]] bool category_is_unsafe(
    const StockResearchTopologyCategory category) noexcept
{
    switch (category) {
    case StockResearchTopologyCategory::ordinary_tree:
    case StockResearchTopologyCategory::source_path_ancestor_reparse:
    case StockResearchTopologyCategory::source_root_reparse:
    case StockResearchTopologyCategory::source_internal_directory_junction:
    case StockResearchTopologyCategory::source_internal_directory_symlink:
    case StockResearchTopologyCategory::source_file_hardlink:
    case StockResearchTopologyCategory::source_reviewed_external_target:
        return false;
    case StockResearchTopologyCategory::source_internal_file_symlink:
    case StockResearchTopologyCategory::source_internal_mount_point:
    case StockResearchTopologyCategory::source_alternate_data_stream:
    case StockResearchTopologyCategory::source_subst_drive:
    case StockResearchTopologyCategory::source_unc_path:
    case StockResearchTopologyCategory::source_remote_volume:
    case StockResearchTopologyCategory::source_unsupported_reparse_tag:
    case StockResearchTopologyCategory::source_link_target_outside_root:
    case StockResearchTopologyCategory::source_link_cycle:
    case StockResearchTopologyCategory::source_link_depth_exceeded:
    case StockResearchTopologyCategory::source_entry_limit_exceeded:
    case StockResearchTopologyCategory::source_byte_limit_exceeded:
        return true;
    }
    return true;
}

[[nodiscard]] bool topology_is_safe(
    const StockResearchTopologySummary& summary) noexcept
{
    return summary.inspection_complete &&
           std::ranges::none_of(summary.categories, category_is_unsafe);
}

[[nodiscard]] bool query_reparse_substitute_name(
    const HANDLE handle, std::wstring& substitute_name) noexcept
{
    std::array<std::byte, MAXIMUM_REPARSE_DATA_BUFFER_SIZE> bytes{};
    DWORD returned = 0U;
    if (::DeviceIoControl(
            handle, FSCTL_GET_REPARSE_POINT, nullptr, 0U, bytes.data(),
            static_cast<DWORD>(bytes.size()), &returned, nullptr) == FALSE ||
        returned < kReparseHeaderBytes) {
        return false;
    }
    const auto* data =
        reinterpret_cast<const LocalReparseDataBuffer*>(bytes.data());
    const wchar_t* buffer = nullptr;
    USHORT offset = 0U;
    USHORT length = 0U;
    std::size_t fixed_payload_bytes = 0U;
    if (data->reparse_tag == IO_REPARSE_TAG_MOUNT_POINT) {
        buffer = data->payload.mount_point.path_buffer;
        offset = data->payload.mount_point.substitute_name_offset;
        length = data->payload.mount_point.substitute_name_length;
        fixed_payload_bytes = 8U;
    } else if (data->reparse_tag == IO_REPARSE_TAG_SYMLINK) {
        buffer = data->payload.symbolic_link.path_buffer;
        offset = data->payload.symbolic_link.substitute_name_offset;
        length = data->payload.symbolic_link.substitute_name_length;
        fixed_payload_bytes = 12U;
    } else {
        return false;
    }
    if ((offset % sizeof(wchar_t)) != 0U ||
        (length % sizeof(wchar_t)) != 0U ||
        data->reparse_data_length < fixed_payload_bytes ||
        static_cast<std::size_t>(offset) + static_cast<std::size_t>(length) >
            static_cast<std::size_t>(data->reparse_data_length) -
                fixed_payload_bytes) {
        return false;
    }
    substitute_name.assign(
        buffer + offset / sizeof(wchar_t), length / sizeof(wchar_t));
    return true;
}

[[nodiscard]] bool is_volume_mount_point(
    const HANDLE handle, const std::uint32_t tag) noexcept
{
    if (tag != IO_REPARSE_TAG_MOUNT_POINT) return false;
    std::wstring substitute;
    if (!query_reparse_substitute_name(handle, substitute)) return true;
    return substitute.starts_with(LR"(\??\Volume{)");
}

[[nodiscard]] bool has_only_default_stream(
    const fs::path& path,
    const StockResearchCopyLimits& limits,
    std::size_t& alternate_count) noexcept
{
    WIN32_FIND_STREAM_DATA data{};
    const HANDLE raw = ::FindFirstStreamW(
        path.c_str(), FindStreamInfoStandard, &data, 0U);
    if (raw == INVALID_HANDLE_VALUE) {
        // Directories without a named stream can have no enumerable data
        // stream at all. That is distinct from an enumeration failure and is
        // the ordinary, ADS-free case on NTFS.
        return ::GetLastError() == ERROR_HANDLE_EOF;
    }
    UniqueFindHandle search{raw};
    std::size_t count = 0U;
    bool valid = true;
    for (;;) {
        ++count;
        if (count > limits.maximum_streams_per_file) {
            valid = false;
            ++alternate_count;
            break;
        }
        if (std::wstring_view{data.cStreamName} != L"::$DATA") {
            ++alternate_count;
            valid = false;
        }
        if (::FindNextStreamW(search.get(), &data) == FALSE) {
            if (::GetLastError() != ERROR_HANDLE_EOF) valid = false;
            break;
        }
    }
    return valid && alternate_count == 0U;
}

void classify_alternate_streams(
    const fs::path& path,
    const StockResearchCopyLimits& limits,
    StockResearchTopologySummary& summary) noexcept
{
    std::size_t alternate_count = 0U;
    if (has_only_default_stream(path, limits, alternate_count)) return;
    summary.alternate_data_stream_count +=
        alternate_count == 0U ? 1U : alternate_count;
    add_category(
        summary,
        StockResearchTopologyCategory::source_alternate_data_stream);
}

[[nodiscard]] bool hash_handle(
    const HANDLE handle, std::array<std::byte, 32U>& digest) noexcept
{
    LARGE_INTEGER zero{};
    if (::SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN) == FALSE) {
        return false;
    }
    UniqueAlgorithm algorithm;
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
    UniqueHash hash;
    if (::BCryptCreateHash(
            algorithm.get(), hash.put(), object.data(),
            static_cast<ULONG>(object.size()), nullptr, 0U, 0U) < 0) {
        return false;
    }
    std::array<UCHAR, 64U * 1'024U> buffer{};
    for (;;) {
        DWORD read = 0U;
        if (::ReadFile(
                handle, buffer.data(), static_cast<DWORD>(buffer.size()),
                &read, nullptr) == FALSE) {
            return false;
        }
        if (read == 0U) break;
        if (::BCryptHashData(hash.get(), buffer.data(), read, 0U) < 0) {
            return false;
        }
    }
    return ::BCryptFinishHash(
               hash.get(), reinterpret_cast<PUCHAR>(digest.data()),
               static_cast<ULONG>(digest.size()), 0U) >= 0;
}

[[nodiscard]] bool hash_bytes(
    const std::span<const std::byte> bytes,
    std::array<std::byte, 32U>& digest) noexcept
{
    if (bytes.size() > static_cast<std::size_t>((std::numeric_limits<ULONG>::max)())) {
        return false;
    }
    UniqueAlgorithm algorithm;
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
    UniqueHash hash;
    if (::BCryptCreateHash(
            algorithm.get(), hash.put(), object.data(),
            static_cast<ULONG>(object.size()), nullptr, 0U, 0U) < 0) {
        return false;
    }
    if (!bytes.empty() &&
        ::BCryptHashData(
            hash.get(), reinterpret_cast<PUCHAR>(
                            const_cast<std::byte*>(bytes.data())),
            static_cast<ULONG>(bytes.size()), 0U) < 0) {
        return false;
    }
    return ::BCryptFinishHash(
               hash.get(), reinterpret_cast<PUCHAR>(digest.data()),
               static_cast<ULONG>(digest.size()), 0U) >= 0;
}

[[nodiscard]] std::string digest_hex(
    const std::array<std::byte, 32U>& digest)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result(digest.size() * 2U, '\0');
    for (std::size_t index = 0U; index < digest.size(); ++index) {
        const auto value = std::to_integer<unsigned int>(digest[index]);
        result[index * 2U] = digits[value >> 4U];
        result[index * 2U + 1U] = digits[value & 0x0fU];
    }
    return result;
}

[[nodiscard]] std::optional<std::string> utf8(const std::wstring_view value)
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
    std::string result(static_cast<std::size_t>(required), '\0');
    if (::WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), required, nullptr,
            nullptr) != required) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] bool identity_in_stack(
    const std::vector<FileIdentity>& stack,
    const FileIdentity& identity) noexcept
{
    return std::ranges::find(stack, identity) != stack.end();
}

struct TraversalNode final {
    fs::path logical_path;
    fs::path physical_path;
    fs::path relative_path;
    std::vector<FileIdentity> active_directories;
    std::vector<LinkWitness> witnesses;
    std::size_t reparse_depth{0U};
};

[[nodiscard]] bool name_less(
    const WIN32_FIND_DATAW& left, const WIN32_FIND_DATAW& right) noexcept
{
    const std::wstring_view a{left.cFileName};
    const std::wstring_view b{right.cFileName};
    if (a.size() <=
            static_cast<std::size_t>((std::numeric_limits<int>::max)()) &&
        b.size() <=
            static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        const int result = ::CompareStringOrdinal(
            a.data(), static_cast<int>(a.size()), b.data(),
            static_cast<int>(b.size()), TRUE);
        if (result == CSTR_LESS_THAN) return true;
        if (result == CSTR_GREATER_THAN) return false;
    }
    return a < b;
}

[[nodiscard]] bool enumerate_directory(
    const fs::path& directory,
    std::vector<WIN32_FIND_DATAW>& entries,
    DWORD& native_error) noexcept
{
    fs::path wildcard;
    try {
        wildcard = directory / L"*";
    } catch (...) {
        native_error = ERROR_NOT_ENOUGH_MEMORY;
        return false;
    }
    WIN32_FIND_DATAW data{};
    UniqueFindHandle search{::FindFirstFileExW(
        wildcard.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch,
        nullptr, FIND_FIRST_EX_LARGE_FETCH)};
    if (!search) {
        native_error = ::GetLastError();
        return false;
    }
    try {
        for (;;) {
            const std::wstring_view name{data.cFileName};
            if (name != L"." && name != L"..") entries.push_back(data);
            if (::FindNextFileW(search.get(), &data) == FALSE) {
                native_error = ::GetLastError();
                break;
            }
        }
        if (native_error != ERROR_NO_MORE_FILES) return false;
        native_error = ERROR_SUCCESS;
        std::ranges::sort(entries, name_less);
        return true;
    } catch (...) {
        native_error = ERROR_NOT_ENOUGH_MEMORY;
        return false;
    }
}

[[nodiscard]] bool path_component_is_steamapps(
    const fs::path& path) noexcept
{
    for (const auto& component : path) {
        if (ordinal_equal(component.native(), L"steamapps")) return true;
    }
    return false;
}

[[nodiscard]] bool inspect_ancestor_reparse(
    const fs::path& absolute,
    StockResearchTopologySummary& summary,
    EntrySnapshot& requested_root,
    std::vector<LinkWitness>& root_witnesses,
    DWORD& native_error) noexcept
{
    auto no_follow = open_entry(absolute, false);
    if (!no_follow) {
        native_error = ::GetLastError();
        return false;
    }
    if (!query_snapshot(no_follow.get(), requested_root)) {
        native_error = ::GetLastError();
        return false;
    }
    summary.root_reparse =
        (requested_root.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U;
    if (summary.root_reparse) {
        add_category(
            summary, StockResearchTopologyCategory::source_root_reparse);
        if (requested_root.reparse_tag != IO_REPARSE_TAG_MOUNT_POINT &&
            requested_root.reparse_tag != IO_REPARSE_TAG_SYMLINK) {
            add_category(
                summary,
                StockResearchTopologyCategory::source_unsupported_reparse_tag);
        } else if (is_volume_mount_point(
                       no_follow.get(), requested_root.reparse_tag)) {
            add_category(
                summary,
                StockResearchTopologyCategory::source_internal_mount_point);
        }
    }

    auto followed = open_entry(absolute, true);
    EntrySnapshot followed_snapshot;
    fs::path followed_final;
    fs::path followed_volume_relative;
    if (!followed || !query_snapshot(followed.get(), followed_snapshot) ||
        !query_final_path(followed.get(), followed_final) ||
        !query_volume_relative_path(
            followed.get(), followed_volume_relative)) {
        native_error = ::GetLastError();
        return false;
    }
    followed_final = followed_final.lexically_normal();
    if (!summary.root_reparse &&
        !path_equal(followed_final, absolute.lexically_normal())) {
        add_category(
            summary,
            StockResearchTopologyCategory::source_path_ancestor_reparse);
    }
    if (summary.root_reparse ||
        !path_equal(followed_final, absolute.lexically_normal())) {
        try {
            // This end-to-end handle witness is stronger than opening each
            // path ancestor independently: it proves the requested root's
            // final target and identity, and can be revalidated without
            // discretionary access to every parent directory.
            root_witnesses.push_back(LinkWitness{
                absolute, requested_root, followed_snapshot, followed_final,
                followed_volume_relative.lexically_normal()});
        } catch (...) {
            native_error = ERROR_NOT_ENOUGH_MEMORY;
            return false;
        }
    }
    native_error = ERROR_SUCCESS;
    return true;
}

[[nodiscard]] BuildInventoryResult build_inventory(
    const fs::path& source_root,
    const StockResearchCopyLimits& limits,
    const bool hash_contents,
    const bool accept_prepared_marker = false,
    const StockExternalApprovalValidation* const external_approval =
        nullptr) noexcept
{
    try {
        if (!valid_limits(limits) || source_root.empty()) {
            return {std::nullopt,
                    StockResearchCopyErrorCode::invalid_argument,
                    ERROR_INVALID_PARAMETER};
        }
        if (!source_root.is_absolute()) {
            return {std::nullopt,
                    StockResearchCopyErrorCode::source_not_absolute,
                    ERROR_INVALID_NAME};
        }
        fs::path absolute = source_root.lexically_normal();
        if (!path_size_valid(absolute, limits)) {
            return {std::nullopt,
                    StockResearchCopyErrorCode::invalid_argument,
                    ERROR_FILENAME_EXCED_RANGE};
        }

        Inventory inventory;
        if (is_unc_path(absolute)) {
            add_category(
                inventory.summary,
                StockResearchTopologyCategory::source_unc_path);
            add_category(
                inventory.summary,
                StockResearchTopologyCategory::source_remote_volume);
            inventory.summary.inspection_complete = true;
            inventory.summary.safe_to_materialize = false;
            return {std::move(inventory), StockResearchCopyErrorCode::none,
                    ERROR_SUCCESS};
        }
        if (!has_drive_letter(absolute)) {
            return {std::nullopt,
                    StockResearchCopyErrorCode::source_not_absolute,
                    ERROR_INVALID_DRIVE};
        }
        if (is_substituted_drive(absolute)) {
            add_category(
                inventory.summary,
                StockResearchTopologyCategory::source_subst_drive);
        }

        DWORD native_error = ERROR_SUCCESS;
        std::vector<LinkWitness> root_witnesses;
        if (!inspect_ancestor_reparse(
                absolute, inventory.summary,
                inventory.requested_root_snapshot, root_witnesses,
                native_error)) {
            const auto code = native_error == ERROR_FILE_NOT_FOUND ||
                                      native_error == ERROR_PATH_NOT_FOUND
                                  ? StockResearchCopyErrorCode::source_not_found
                                  : StockResearchCopyErrorCode::source_open_failed;
            return {std::nullopt, code, native_error};
        }
        if (!inventory.requested_root_snapshot.directory) {
            return {std::nullopt,
                    StockResearchCopyErrorCode::source_not_directory,
                    ERROR_DIRECTORY};
        }

        auto canonical_root = open_entry(absolute, true);
        if (!canonical_root) {
            return {std::nullopt,
                    StockResearchCopyErrorCode::source_open_failed,
                    ::GetLastError()};
        }
        if (!query_snapshot(
                canonical_root.get(), inventory.canonical_root_snapshot) ||
            !query_final_path(canonical_root.get(), inventory.canonical_root) ||
            !query_volume_relative_path(
                canonical_root.get(),
                inventory.canonical_root_volume_relative_path)) {
            return {std::nullopt,
                    StockResearchCopyErrorCode::source_identity_query_failed,
                    ::GetLastError()};
        }
        inventory.canonical_root = inventory.canonical_root.lexically_normal();
        inventory.canonical_root_volume_relative_path =
            inventory.canonical_root_volume_relative_path.lexically_normal();
        if (!inventory.canonical_root_snapshot.directory) {
            return {std::nullopt,
                    StockResearchCopyErrorCode::source_not_directory,
                    ERROR_DIRECTORY};
        }
        if (!local_fixed_volume(inventory.canonical_root)) {
            add_category(
                inventory.summary,
                StockResearchTopologyCategory::source_remote_volume);
            inventory.summary.inspection_complete = true;
            inventory.summary.safe_to_materialize = false;
            return {std::move(inventory), StockResearchCopyErrorCode::none,
                    ERROR_SUCCESS};
        }
        classify_alternate_streams(
            inventory.canonical_root, limits, inventory.summary);
        if (inventory.summary.root_reparse && !root_witnesses.empty()) {
            root_witnesses.back().target_snapshot =
                inventory.canonical_root_snapshot;
            root_witnesses.back().target_final_path = inventory.canonical_root;
            root_witnesses.back().target_volume_relative_path =
                inventory.canonical_root_volume_relative_path;
        }

        TraversalNode root;
        root.logical_path = absolute;
        root.physical_path = inventory.canonical_root;
        root.active_directories.push_back(
            inventory.canonical_root_snapshot.identity);
        root.witnesses = root_witnesses;
        root.reparse_depth = root_witnesses.size();
        std::vector<TraversalNode> pending;
        pending.push_back(std::move(root));
        bool bounded_stop = false;

        while (!pending.empty() && !bounded_stop) {
            TraversalNode node = std::move(pending.back());
            pending.pop_back();
            std::vector<WIN32_FIND_DATAW> children;
            if (!enumerate_directory(
                    node.physical_path, children, native_error)) {
                return {std::nullopt,
                        StockResearchCopyErrorCode::enumeration_failed,
                        native_error};
            }
            // LIFO traversal keeps deterministic ascending order by pushing in
            // reverse. File records are sorted again before hashing.
            for (const auto& child : children) {
                if (inventory.summary.entry_count >= limits.maximum_entries) {
                    add_category(
                        inventory.summary,
                        StockResearchTopologyCategory::
                            source_entry_limit_exceeded);
                    bounded_stop = true;
                    break;
                }
                fs::path logical;
                fs::path physical;
                fs::path relative;
                try {
                    logical = node.logical_path / child.cFileName;
                    physical = node.physical_path / child.cFileName;
                    relative = node.relative_path / child.cFileName;
                } catch (...) {
                    return {std::nullopt,
                            StockResearchCopyErrorCode::enumeration_failed,
                            ERROR_NOT_ENOUGH_MEMORY};
                }
                if (!path_size_valid(logical, limits) ||
                    !path_size_valid(physical, limits)) {
                    return {std::nullopt,
                            StockResearchCopyErrorCode::enumeration_failed,
                            ERROR_FILENAME_EXCED_RANGE};
                }
                auto no_follow = open_entry(physical, false);
                if (!no_follow) {
                    return {std::nullopt,
                            StockResearchCopyErrorCode::source_open_failed,
                            ::GetLastError()};
                }
                EntrySnapshot snapshot;
                if (!query_snapshot(no_follow.get(), snapshot)) {
                    return {
                        std::nullopt,
                        StockResearchCopyErrorCode::source_identity_query_failed,
                        ::GetLastError()};
                }
                ++inventory.summary.entry_count;
                const bool reparse =
                    (snapshot.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U;
                if (reparse) {
                    ++inventory.summary.internal_reparse_count;
                    if (snapshot.reparse_tag != IO_REPARSE_TAG_MOUNT_POINT &&
                        snapshot.reparse_tag != IO_REPARSE_TAG_SYMLINK) {
                        add_category(
                            inventory.summary,
                            StockResearchTopologyCategory::
                                source_unsupported_reparse_tag);
                        continue;
                    }
                    if (snapshot.directory && is_volume_mount_point(
                            no_follow.get(), snapshot.reparse_tag)) {
                        add_category(
                            inventory.summary,
                            StockResearchTopologyCategory::
                                source_internal_mount_point);
                        continue;
                    }
                    if (node.reparse_depth >= limits.maximum_reparse_depth) {
                        add_category(
                            inventory.summary,
                            StockResearchTopologyCategory::
                                source_link_depth_exceeded);
                        continue;
                    }
                    auto target = open_entry(physical, true);
                    EntrySnapshot target_snapshot;
                    fs::path target_final;
                    fs::path target_volume_relative;
                    if (!target || !query_snapshot(target.get(), target_snapshot) ||
                        !query_final_path(target.get(), target_final) ||
                        !query_volume_relative_path(
                            target.get(), target_volume_relative)) {
                        // A broken or inaccessible target cannot be proven
                        // physically contained. Inspection is still complete
                        // and read-only; materialization remains fail-closed.
                        ++inventory.summary.escaped_target_count;
                        if (!snapshot.directory) {
                            add_category(
                                inventory.summary,
                                StockResearchTopologyCategory::
                                    source_internal_file_symlink);
                        }
                        add_category(
                            inventory.summary,
                            StockResearchTopologyCategory::
                                source_link_target_outside_root);
                        continue;
                    }
                    target_final = target_final.lexically_normal();
                    target_volume_relative =
                        target_volume_relative.lexically_normal();
                    const PhysicalLocation source_location{
                        inventory.canonical_root_snapshot.identity.volume_serial,
                        inventory.canonical_root_volume_relative_path};
                    const PhysicalLocation target_location{
                        target_snapshot.identity.volume_serial,
                        target_volume_relative};
                    const bool external = !physical_same_or_below(
                        source_location, target_location);
                    bool approved = false;
                    if (external && external_approval != nullptr) {
                        approved = std::ranges::any_of(
                            external_approval->targets,
                            [&](const StockApprovedExternalTarget& candidate) {
                                return path_equal(
                                           candidate.source_link_relative_path
                                               .lexically_normal(),
                                           relative.lexically_normal()) &&
                                       path_equal(
                                           candidate.target_root
                                               .lexically_normal(),
                                           target_final);
                            });
                    }
                    if (external) {
                        ++inventory.summary.escaped_target_count;
                        if (!approved) {
                            add_category(
                                inventory.summary,
                                StockResearchTopologyCategory::
                                    source_link_target_outside_root);
                            continue;
                        }
                        ++inventory.summary.reviewed_external_target_count;
                        add_category(
                            inventory.summary,
                            StockResearchTopologyCategory::
                                source_reviewed_external_target);
                    }
                    if (!target_snapshot.directory) {
                        if (!external || !approved) {
                            add_category(
                                inventory.summary,
                                StockResearchTopologyCategory::
                                    source_internal_file_symlink);
                            continue;
                        }
                        classify_alternate_streams(
                            target_final, limits, inventory.summary);
                        const bool hardlink = target_snapshot.link_count > 1U;
                        if (hardlink) {
                            ++inventory.summary.hardlink_count;
                            add_category(
                                inventory.summary,
                                StockResearchTopologyCategory::
                                    source_file_hardlink);
                        }
                        if (target_snapshot.size > limits.maximum_file_bytes ||
                            inventory.summary.byte_count >
                                limits.maximum_total_bytes -
                                    target_snapshot.size) {
                            add_category(
                                inventory.summary,
                                StockResearchTopologyCategory::
                                    source_byte_limit_exceeded);
                            bounded_stop = true;
                            break;
                        }
                        inventory.summary.byte_count += target_snapshot.size;
                        std::array<std::byte, 32U> digest{};
                        if (hash_contents) {
                            auto readable =
                                open_entry(target_final, false, true);
                            EntrySnapshot before;
                            EntrySnapshot after;
                            if (!readable ||
                                !query_snapshot(readable.get(), before) ||
                                before != target_snapshot ||
                                !hash_handle(readable.get(), digest) ||
                                !query_snapshot(readable.get(), after)) {
                                return {
                                    std::nullopt,
                                    StockResearchCopyErrorCode::
                                        source_read_failed,
                                    ::GetLastError()};
                            }
                            if (before != after) {
                                return {
                                    std::nullopt,
                                    StockResearchCopyErrorCode::
                                        source_changed_during_materialization,
                                    ERROR_FILE_INVALID};
                            }
                        }
                        auto witnesses = node.witnesses;
                        try {
                            witnesses.push_back(LinkWitness{
                                logical, snapshot, target_snapshot,
                                target_final, target_volume_relative});
                            inventory.entries.push_back(InventoryEntry{
                                relative, logical, target_final,
                                target_snapshot, digest,
                                std::move(witnesses), false, hardlink});
                        } catch (...) {
                            return {
                                std::nullopt,
                                StockResearchCopyErrorCode::enumeration_failed,
                                ERROR_NOT_ENOUGH_MEMORY};
                        }
                        continue;
                    }
                    const auto kind = snapshot.reparse_tag ==
                                              IO_REPARSE_TAG_SYMLINK
                                          ? StockResearchTopologyCategory::
                                                source_internal_directory_symlink
                                          : StockResearchTopologyCategory::
                                                source_internal_directory_junction;
                    add_category(inventory.summary, kind);
                    if (!external) {
                        ++inventory.summary.contained_target_count;
                    }
                    classify_alternate_streams(
                        target_final, limits, inventory.summary);
                    if (identity_in_stack(
                            node.active_directories,
                            target_snapshot.identity)) {
                        add_category(
                            inventory.summary,
                            StockResearchTopologyCategory::source_link_cycle);
                        continue;
                    }
                    auto witnesses = node.witnesses;
                    try {
                        witnesses.push_back(LinkWitness{
                            logical, snapshot, target_snapshot, target_final,
                            target_volume_relative});
                        inventory.entries.push_back(InventoryEntry{
                            relative, logical, target_final, target_snapshot, {},
                            witnesses, true, false});
                        auto active = node.active_directories;
                        active.push_back(target_snapshot.identity);
                        pending.push_back(TraversalNode{
                            logical, target_final, relative, std::move(active),
                            std::move(witnesses), node.reparse_depth + 1U});
                    } catch (...) {
                        return {std::nullopt,
                                StockResearchCopyErrorCode::enumeration_failed,
                                ERROR_NOT_ENOUGH_MEMORY};
                    }
                    continue;
                }

                if (snapshot.directory) {
                    classify_alternate_streams(
                        physical, limits, inventory.summary);
                    try {
                        inventory.entries.push_back(InventoryEntry{
                            relative, logical, physical, snapshot, {},
                            node.witnesses, true, false});
                        auto active = node.active_directories;
                        if (identity_in_stack(active, snapshot.identity)) {
                            add_category(
                                inventory.summary,
                                StockResearchTopologyCategory::source_link_cycle);
                            continue;
                        }
                        active.push_back(snapshot.identity);
                        pending.push_back(TraversalNode{
                            logical, physical, relative, std::move(active),
                            node.witnesses, node.reparse_depth});
                    } catch (...) {
                        return {std::nullopt,
                                StockResearchCopyErrorCode::enumeration_failed,
                                ERROR_NOT_ENOUGH_MEMORY};
                    }
                    continue;
                }

                classify_alternate_streams(
                    physical, limits, inventory.summary);
                const bool hardlink = snapshot.link_count > 1U;
                if (hardlink) {
                    ++inventory.summary.hardlink_count;
                    add_category(
                        inventory.summary,
                        StockResearchTopologyCategory::source_file_hardlink);
                }
                if (snapshot.size > limits.maximum_file_bytes ||
                    inventory.summary.byte_count >
                        limits.maximum_total_bytes - snapshot.size) {
                    add_category(
                        inventory.summary,
                        StockResearchTopologyCategory::source_byte_limit_exceeded);
                    bounded_stop = true;
                    break;
                }
                inventory.summary.byte_count += snapshot.size;
                std::array<std::byte, 32U> digest{};
                if (hash_contents) {
                    auto readable = open_entry(physical, false, true);
                    EntrySnapshot before;
                    EntrySnapshot after;
                    if (!readable || !query_snapshot(readable.get(), before) ||
                        before != snapshot ||
                        !hash_handle(readable.get(), digest) ||
                        !query_snapshot(readable.get(), after)) {
                        return {
                            std::nullopt,
                            StockResearchCopyErrorCode::source_read_failed,
                            ::GetLastError()};
                    }
                    if (before != after) {
                        return {
                            std::nullopt,
                            StockResearchCopyErrorCode::
                                source_changed_during_materialization,
                            ERROR_FILE_INVALID};
                    }
                }
                try {
                    inventory.entries.push_back(InventoryEntry{
                        relative, logical, physical, snapshot, digest,
                        node.witnesses, false, hardlink});
                } catch (...) {
                    return {std::nullopt,
                            StockResearchCopyErrorCode::enumeration_failed,
                            ERROR_NOT_ENOUGH_MEMORY};
                }
                if (relative.native().size() == 6U &&
                    ordinal_equal(relative.native(), L"hl.exe")) {
                    inventory.client_present = true;
                } else if (relative.native().size() == 8U &&
                           ordinal_equal(relative.native(), L"hlds.exe")) {
                    inventory.server_present = true;
                } else if (ordinal_equal(
                               relative.native(),
                               L".hlclient-research-isolated")) {
                    inventory.marker_present = true;
                }
            }
        }

        inventory.summary.inspection_complete = true;
        if (inventory.summary.categories.empty()) {
            add_category(
                inventory.summary,
                StockResearchTopologyCategory::ordinary_tree);
        }
        std::ranges::sort(inventory.summary.categories);
        inventory.summary.safe_to_materialize =
            topology_is_safe(inventory.summary);
        if (!hash_contents || !inventory.summary.safe_to_materialize) {
            return {std::move(inventory), StockResearchCopyErrorCode::none,
                    ERROR_SUCCESS};
        }
        if (!inventory.client_present || !inventory.server_present) {
            return {std::nullopt,
                    StockResearchCopyErrorCode::source_required_launcher_missing,
                    ERROR_FILE_NOT_FOUND};
        }
        if (inventory.marker_present && !accept_prepared_marker) {
            return {std::nullopt,
                    StockResearchCopyErrorCode::source_already_prepared,
                    ERROR_ALREADY_EXISTS};
        }

        std::ranges::sort(
            inventory.entries,
            [](const InventoryEntry& left, const InventoryEntry& right) {
                return left.relative_path.generic_wstring() <
                       right.relative_path.generic_wstring();
            });
        std::string canonical;
        for (const auto& entry : inventory.entries) {
            const auto relative = utf8(entry.relative_path.generic_wstring());
            if (!relative) {
                return {std::nullopt,
                        StockResearchCopyErrorCode::source_digest_failed,
                        ERROR_NO_UNICODE_TRANSLATION};
            }
            canonical += entry.directory ? "d|" : "f|";
            canonical += *relative;
            if (!entry.directory) {
                canonical += '|';
                canonical += std::to_string(entry.snapshot.size);
                canonical += '|';
                canonical += digest_hex(entry.digest);
            }
            canonical.push_back('\n');
        }
        if (!hash_bytes(
                std::as_bytes(std::span{canonical.data(), canonical.size()}),
                inventory.inventory_digest)) {
            return {std::nullopt,
                    StockResearchCopyErrorCode::source_digest_failed,
                    ERROR_INVALID_DATA};
        }
        return {std::move(inventory), StockResearchCopyErrorCode::none,
                ERROR_SUCCESS};
    } catch (...) {
        return {std::nullopt, StockResearchCopyErrorCode::enumeration_failed,
                ERROR_NOT_ENOUGH_MEMORY};
    }
}

[[nodiscard]] bool validate_witnesses(
    const std::vector<LinkWitness>& witnesses,
    DWORD& native_error) noexcept
{
    for (const auto& witness : witnesses) {
        auto link = open_entry(witness.logical_path, false);
        EntrySnapshot link_snapshot;
        if (!link || !query_snapshot(link.get(), link_snapshot)) {
            native_error = ::GetLastError();
            return false;
        }
        if (link_snapshot != witness.link_snapshot) {
            native_error = ERROR_FILE_INVALID;
            return false;
        }
        auto target = open_entry(witness.logical_path, true);
        EntrySnapshot target_snapshot;
        fs::path target_final;
        fs::path target_volume_relative;
        if (!target || !query_snapshot(target.get(), target_snapshot) ||
            !query_final_path(target.get(), target_final) ||
            !query_volume_relative_path(
                target.get(), target_volume_relative)) {
            native_error = ::GetLastError();
            return false;
        }
        if (target_snapshot != witness.target_snapshot ||
            !path_equal(
                target_final.lexically_normal(),
                witness.target_final_path.lexically_normal()) ||
            !path_equal(
                target_volume_relative.lexically_normal(),
                witness.target_volume_relative_path.lexically_normal())) {
            native_error = ERROR_FILE_INVALID;
            return false;
        }
    }
    native_error = ERROR_SUCCESS;
    return true;
}

[[nodiscard]] bool inventory_equal(
    const Inventory& before, const Inventory& after) noexcept
{
    return before.requested_root_snapshot == after.requested_root_snapshot &&
           before.canonical_root_snapshot == after.canonical_root_snapshot &&
           path_equal(before.canonical_root, after.canonical_root) &&
           path_equal(
               before.canonical_root_volume_relative_path,
               after.canonical_root_volume_relative_path) &&
           before.summary.categories == after.summary.categories &&
           before.summary.root_reparse == after.summary.root_reparse &&
           before.summary.internal_reparse_count ==
               after.summary.internal_reparse_count &&
           before.summary.hardlink_count == after.summary.hardlink_count &&
           before.summary.alternate_data_stream_count ==
               after.summary.alternate_data_stream_count &&
           before.summary.contained_target_count ==
               after.summary.contained_target_count &&
           before.summary.escaped_target_count ==
               after.summary.escaped_target_count &&
           before.summary.entry_count == after.summary.entry_count &&
           before.summary.byte_count == after.summary.byte_count &&
           before.inventory_digest == after.inventory_digest &&
           before.entries == after.entries;
}

[[nodiscard]] bool identity_fingerprint(
    const std::string_view domain,
    const EntrySnapshot& snapshot,
    std::string& result) noexcept
{
    try {
        std::vector<std::byte> bytes;
        bytes.reserve(
            domain.size() + sizeof(snapshot.identity.volume_serial) +
            snapshot.identity.file_id.size() + sizeof(snapshot.size) +
            sizeof(snapshot.creation_time) + sizeof(snapshot.last_write_time) +
            sizeof(snapshot.change_time) + sizeof(snapshot.attributes));
        const auto append = [&bytes](const auto& value) {
            const auto view = std::as_bytes(std::span{&value, 1U});
            bytes.insert(bytes.end(), view.begin(), view.end());
        };
        const auto domain_bytes = std::as_bytes(
            std::span{domain.data(), domain.size()});
        bytes.insert(bytes.end(), domain_bytes.begin(), domain_bytes.end());
        append(snapshot.identity.volume_serial);
        bytes.insert(
            bytes.end(), snapshot.identity.file_id.begin(),
            snapshot.identity.file_id.end());
        append(snapshot.size);
        append(snapshot.creation_time);
        append(snapshot.last_write_time);
        append(snapshot.change_time);
        append(snapshot.attributes);
        std::array<std::byte, 32U> digest{};
        if (!hash_bytes(bytes, digest)) return false;
        result = digest_hex(digest);
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] std::optional<std::wstring> random_leaf(
    const std::wstring_view prefix) noexcept
{
    std::array<UCHAR, 16U> random{};
    if (::BCryptGenRandom(
            nullptr, random.data(), static_cast<ULONG>(random.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        return std::nullopt;
    }
    constexpr wchar_t digits[] = L"0123456789abcdef";
    try {
        std::wstring result{prefix};
        result.reserve(result.size() + random.size() * 2U);
        for (const auto value : random) {
            result.push_back(digits[value >> 4U]);
            result.push_back(digits[value & 0x0fU]);
        }
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] bool ordinary_directory_handle(
    const HANDLE handle, EntrySnapshot* const snapshot = nullptr) noexcept
{
    EntrySnapshot observed;
    if (!query_snapshot(handle, observed) || !observed.directory ||
        (observed.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        return false;
    }
    if (snapshot != nullptr) *snapshot = observed;
    return true;
}

[[nodiscard]] bool ordinary_file_handle(
    const HANDLE handle,
    const std::uint64_t expected_size,
    EntrySnapshot* const snapshot = nullptr) noexcept
{
    EntrySnapshot observed;
    if (!query_snapshot(handle, observed) || observed.directory ||
        (observed.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
        observed.link_count != 1U || observed.size != expected_size) {
        return false;
    }
    if (snapshot != nullptr) *snapshot = observed;
    return true;
}

[[nodiscard]] bool exact_opened_path(
    const HANDLE handle, const fs::path& expected) noexcept
{
    fs::path final;
    return query_final_path(handle, final) &&
           path_equal(final.lexically_normal(), expected.lexically_normal());
}

struct OwnedEntryWitness final {
    const fs::path* relative_path{nullptr};
    FileIdentity identity{};
    bool directory{false};
};

struct SourceCommitGuard final {
    fs::path expected_path;
    EntrySnapshot snapshot{};
    bool require_default_stream{false};
    UniqueHandle handle;
};

struct ExternalProvenanceGuard final {
    fs::path expected_path;
    EntrySnapshot snapshot{};
    std::array<std::byte, 32U> digest{};
    UniqueHandle handle;
};

struct MaterializedDirectoryPin final {
    const fs::path* relative_path{nullptr};
    EntrySnapshot snapshot{};
    UniqueHandle handle;
};

[[nodiscard]] HANDLE materialized_directory_handle(
    const HANDLE root,
    const std::vector<MaterializedDirectoryPin>& directories,
    const fs::path& relative_path) noexcept
{
    if (relative_path.empty()) return root;
    const auto found = std::ranges::find_if(
        directories,
        [&relative_path](const MaterializedDirectoryPin& directory) {
            return directory.relative_path != nullptr &&
                   path_equal(*directory.relative_path, relative_path);
        });
    return found == directories.end() ? INVALID_HANDLE_VALUE
                                      : found->handle.get();
}

[[nodiscard]] bool repin_materialized_directories(
    const HANDLE root,
    const fs::path& root_path,
    std::vector<MaterializedDirectoryPin>& directories,
    DWORD& native_error) noexcept
{
    native_error = ERROR_SUCCESS;
    for (auto& directory : directories) {
        if (directory.relative_path == nullptr || directory.handle) {
            native_error = ERROR_INVALID_DATA;
            ::SetLastError(native_error);
            return false;
        }
        const HANDLE parent = materialized_directory_handle(
            root, directories, directory.relative_path->parent_path());
        if (parent == INVALID_HANDLE_VALUE) {
            native_error = ERROR_PATH_NOT_FOUND;
            ::SetLastError(native_error);
            return false;
        }
        auto handle = open_relative_directory_locked(
            parent, directory.relative_path->filename().native(), false,
            native_error);
        EntrySnapshot snapshot;
        fs::path expected;
        try {
            expected = root_path / *directory.relative_path;
        } catch (...) {
            native_error = ERROR_NOT_ENOUGH_MEMORY;
            ::SetLastError(native_error);
            return false;
        }
        if (!handle ||
            !ordinary_directory_handle(handle.get(), &snapshot) ||
            snapshot.identity != directory.snapshot.identity ||
            !exact_opened_path(handle.get(), expected)) {
            if (native_error == ERROR_SUCCESS) {
                native_error = ERROR_FILE_INVALID;
                ::SetLastError(native_error);
            }
            return false;
        }
        directory.handle = std::move(handle);
    }
    return true;
}

// The final destination inventory is a security decision, not a best-effort
// report. A recursive change notification supplies the missing transaction
// witness while that inventory is collected: a child added after its parent
// was enumerated, or bytes changed after a file was hashed, cannot be hidden
// behind the cached Inventory value. The final marker rename deliberately
// completes the request. Consuming its exact OLD_NAME/NEW_NAME pair makes the
// notification ordering, rather than an earlier check with a race window, the
// commit witness.
class PublishedTreeChangeWitness final {
public:
    PublishedTreeChangeWitness() = default;
    PublishedTreeChangeWitness(const PublishedTreeChangeWitness&) = delete;
    PublishedTreeChangeWitness& operator=(
        const PublishedTreeChangeWitness&) = delete;
    ~PublishedTreeChangeWitness() { abandon(); }

    [[nodiscard]] bool begin(
        const fs::path& root,
        const FileIdentity& expected_identity,
        DWORD& native_error,
        const bool recursive = true,
        const bool directory_names_only = false) noexcept
    {
        abandon();
        directory_ = UniqueHandle{::CreateFileW(
            root.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT |
                FILE_FLAG_OVERLAPPED,
            nullptr)};
        EntrySnapshot snapshot;
        if (!directory_ ||
            !ordinary_directory_handle(directory_.get(), &snapshot) ||
            snapshot.identity != expected_identity ||
            !exact_opened_path(directory_.get(), root)) {
            native_error = directory_ ? ERROR_FILE_INVALID : ::GetLastError();
            directory_.reset();
            return false;
        }
        event_ = UniqueHandle{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        if (!event_) {
            native_error = ::GetLastError();
            directory_.reset();
            return false;
        }
        overlapped_ = {};
        overlapped_.hEvent = event_.get();
        constexpr DWORD all_filters =
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION |
            FILE_NOTIFY_CHANGE_SECURITY | kNotifyChangeStreamName |
            kNotifyChangeStreamSize | kNotifyChangeStreamWrite;
        const DWORD filters = directory_names_only
                                  ? FILE_NOTIFY_CHANGE_DIR_NAME
                                  : all_filters;
        if (::ReadDirectoryChangesW(
                directory_.get(), buffer_.data(),
                static_cast<DWORD>(buffer_.size()), recursive ? TRUE : FALSE,
                filters, nullptr, &overlapped_, nullptr) == FALSE) {
            native_error = ::GetLastError();
            event_.reset();
            directory_.reset();
            return false;
        }
        pending_ = true;
        native_error = ERROR_SUCCESS;
        return true;
    }

    [[nodiscard]] bool unchanged_now(DWORD& native_error) noexcept
    {
        if (!pending_ || !directory_) {
            native_error = ERROR_INVALID_HANDLE;
            return false;
        }
        DWORD transferred = 0U;
        if (::GetOverlappedResult(
                directory_.get(), &overlapped_, &transferred, FALSE) !=
            FALSE) {
            native_error = ERROR_FILE_INVALID;
            return false;
        }
        const DWORD error = ::GetLastError();
        if (error == ERROR_IO_INCOMPLETE) {
            native_error = ERROR_SUCCESS;
            return true;
        }
        native_error = error;
        return false;
    }

    [[nodiscard]] bool consume_exact_rename(
        const std::wstring_view root_leaf,
        const std::wstring_view old_leaf,
        const std::wstring_view new_leaf,
        DWORD& native_error) noexcept
    {
        if (!pending_ || !directory_ || !event_ || old_leaf.empty() ||
            new_leaf.empty()) {
            native_error = ERROR_INVALID_HANDLE;
            return false;
        }
        const DWORD wait = ::WaitForSingleObject(event_.get(), 10'000U);
        if (wait != WAIT_OBJECT_0) {
            native_error =
                wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : ::GetLastError();
            return false;
        }

        DWORD transferred = 0U;
        if (::GetOverlappedResult(
                directory_.get(), &overlapped_, &transferred, FALSE) == FALSE) {
            native_error = ::GetLastError();
            return false;
        }
        pending_ = false;
        constexpr std::size_t header =
            offsetof(FILE_NOTIFY_INFORMATION, FileName);
        if (transferred < header || transferred > buffer_.size()) {
            native_error = ERROR_FILE_INVALID;
            return false;
        }

        const auto valid_record =
            [transferred, this](const std::size_t offset) noexcept {
                if (offset > transferred || transferred - offset < header) {
                    return false;
                }
                const auto* const record =
                    reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
                        buffer_.data() + offset);
                const auto name_bytes =
                    static_cast<std::size_t>(record->FileNameLength);
                return (name_bytes % sizeof(wchar_t)) == 0U &&
                       name_bytes <= transferred - offset - header;
            };
        const auto path_matches =
            [root_leaf](
                const FILE_NOTIFY_INFORMATION& record,
                const std::wstring_view leaf) noexcept {
                const std::wstring_view observed{
                    record.FileName,
                    static_cast<std::size_t>(record.FileNameLength) /
                        sizeof(wchar_t)};
                if (root_leaf.empty()) {
                    return ordinal_equal(observed, leaf);
                }
                return observed.size() ==
                           root_leaf.size() + 1U + leaf.size() &&
                       ordinal_equal(
                           observed.substr(0U, root_leaf.size()), root_leaf) &&
                       (observed[root_leaf.size()] == L'\\' ||
                        observed[root_leaf.size()] == L'/') &&
                       ordinal_equal(
                           observed.substr(root_leaf.size() + 1U), leaf);
            };

        if (!valid_record(0U)) {
            native_error = ERROR_FILE_INVALID;
            return false;
        }
        const auto* const first =
            reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer_.data());
        const auto next = static_cast<std::size_t>(first->NextEntryOffset);
        const auto first_minimum =
            header + static_cast<std::size_t>(first->FileNameLength);
        if (first->Action != FILE_ACTION_RENAMED_OLD_NAME || next == 0U ||
            (next % alignof(DWORD)) != 0U || next < first_minimum ||
            !path_matches(*first, old_leaf) || !valid_record(next)) {
            native_error = ERROR_FILE_INVALID;
            return false;
        }
        const auto* const second =
            reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
                buffer_.data() + next);
        if (second->Action != FILE_ACTION_RENAMED_NEW_NAME ||
            second->NextEntryOffset != 0U ||
            !path_matches(*second, new_leaf)) {
            native_error = ERROR_FILE_INVALID;
            return false;
        }
        native_error = ERROR_SUCCESS;
        return true;
    }

    void abandon() noexcept
    {
        if (pending_ && directory_) {
            static_cast<void>(::CancelIoEx(directory_.get(), &overlapped_));
            DWORD ignored = 0U;
            static_cast<void>(::GetOverlappedResult(
                directory_.get(), &overlapped_, &ignored, TRUE));
        }
        pending_ = false;
        event_.reset();
        directory_.reset();
    }

private:
    // Windows rejects larger buffers for remote directories. The production
    // policy is local-fixed-volume only, but 64 KiB is also a conservative
    // documented bound and any single notification is already a rejection.
    alignas(FILE_NOTIFY_INFORMATION)
        std::array<std::byte, 64U * 1'024U> buffer_{};
    OVERLAPPED overlapped_{};
    UniqueHandle directory_;
    UniqueHandle event_;
    bool pending_{false};
};

[[nodiscard]] bool handle_has_only_default_stream(
    const HANDLE handle) noexcept
{
    alignas(FILE_STREAM_INFO)
        std::array<std::byte, 4U * 1'024U> storage{};
    if (::GetFileInformationByHandleEx(
            handle, FileStreamInfo, storage.data(),
            static_cast<DWORD>(storage.size())) == FALSE) {
        // Ordinary directories with no named streams report no stream records.
        // A directory ADS produces records and is rejected below.
        return ::GetLastError() == ERROR_HANDLE_EOF;
    }
    std::size_t offset = 0U;
    std::size_t count = 0U;
    for (;;) {
        constexpr std::size_t header = offsetof(FILE_STREAM_INFO, StreamName);
        if (offset > storage.size() || storage.size() - offset < header) {
            return false;
        }
        const auto* const info = reinterpret_cast<const FILE_STREAM_INFO*>(
            storage.data() + offset);
        const auto name_bytes = static_cast<std::size_t>(info->StreamNameLength);
        if ((name_bytes % sizeof(wchar_t)) != 0U ||
            name_bytes > storage.size() - offset - header) {
            return false;
        }
        const std::wstring_view name{
            info->StreamName, name_bytes / sizeof(wchar_t)};
        ++count;
        if (!ordinal_equal(name, L"::$DATA")) return false;
        if (info->NextEntryOffset == 0U) break;
        const auto next = static_cast<std::size_t>(info->NextEntryOffset);
        if (next < header || next > storage.size() - offset) return false;
        offset += next;
    }
    return count == 1U;
}

class PublishedFileOplock final {
public:
    PublishedFileOplock() = default;
    PublishedFileOplock(const PublishedFileOplock&) = delete;
    PublishedFileOplock& operator=(const PublishedFileOplock&) = delete;
    ~PublishedFileOplock() { abandon(); }

    [[nodiscard]] bool begin(
        const HANDLE parent,
        const std::wstring_view leaf,
        const fs::path& expected_path,
        const FileIdentity& expected_identity,
        const std::uint64_t expected_size,
        const std::array<std::byte, 32U>& expected_digest,
        DWORD& native_error) noexcept
    {
        abandon();
        // A legacy filter oplock deliberately uses a zero-access overlapped
        // locking handle. Once granted, a separate retained read handle is
        // permitted; incompatible writes, link changes, or stream opens break
        // the oplock while the final observation remains stable.
        locking_ = UniqueHandle{::CreateFileW(
            expected_path.c_str(), 0U, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
                FILE_FLAG_OVERLAPPED,
            nullptr)};
        native_error = locking_ ? ERROR_SUCCESS : ::GetLastError();
        EntrySnapshot locking_snapshot;
        if (!locking_ || !query_snapshot(locking_.get(), locking_snapshot) ||
            locking_snapshot.directory ||
            (locking_snapshot.attributes & FILE_ATTRIBUTE_REPARSE_POINT) !=
                0U ||
            locking_snapshot.identity != expected_identity ||
            !exact_opened_path(locking_.get(), expected_path)) {
            native_error = locking_ ? ERROR_FILE_INVALID : native_error;
            abandon();
            return false;
        }
        event_ = UniqueHandle{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        if (!event_) {
            native_error = ::GetLastError();
            abandon();
            return false;
        }
        overlapped_ = {};
        overlapped_.hEvent = event_.get();
        ::SetLastError(ERROR_SUCCESS);
        const BOOL oplock_request = ::DeviceIoControl(
            locking_.get(), FSCTL_REQUEST_FILTER_OPLOCK, nullptr, 0U,
            nullptr, 0U, nullptr, &overlapped_);
        const DWORD oplock_error = oplock_request != FALSE
                                       ? ERROR_SUCCESS
                                       : ::GetLastError();
        if (oplock_request != FALSE || oplock_error != ERROR_IO_PENDING) {
            native_error = oplock_request != FALSE
                               ? ERROR_OPLOCK_NOT_GRANTED
                               : oplock_error;
            abandon();
            return false;
        }
        pending_ = true;

        reader_ = nt_create_relative_entry(
            parent, leaf,
            GENERIC_READ | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            FILE_SHARE_READ | FILE_SHARE_DELETE, FILE_OPEN,
            FILE_ATTRIBUTE_NORMAL,
            FILE_NON_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT |
                FILE_SYNCHRONOUS_IO_NONALERT,
            native_error);
        EntrySnapshot before;
        EntrySnapshot after;
        std::array<std::byte, 32U> digest{};
        if (!reader_ || !query_snapshot(reader_.get(), before) ||
            before.directory ||
            (before.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
            before.link_count != 1U || before.size != expected_size ||
            before.identity != expected_identity ||
            !exact_opened_path(reader_.get(), expected_path) ||
            !handle_has_only_default_stream(reader_.get()) ||
            !hash_handle(reader_.get(), digest) || digest != expected_digest ||
            !query_snapshot(reader_.get(), after) || before != after) {
            if (native_error == ERROR_SUCCESS) {
                native_error = ERROR_FILE_INVALID;
            }
            abandon();
            return false;
        }
        native_error = ERROR_SUCCESS;
        return true;
    }

    [[nodiscard]] bool unchanged_now(DWORD& native_error) noexcept
    {
        if (!pending_ || !locking_) {
            native_error = ERROR_INVALID_HANDLE;
            return false;
        }
        DWORD transferred = 0U;
        if (::GetOverlappedResult(
                locking_.get(), &overlapped_, &transferred, FALSE) !=
            FALSE) {
            native_error = ERROR_FILE_INVALID;
            return false;
        }
        const DWORD error = ::GetLastError();
        if (error == ERROR_IO_INCOMPLETE) {
            native_error = ERROR_SUCCESS;
            return true;
        }
        native_error = error;
        return false;
    }

    void abandon() noexcept
    {
        // Filter oplock protocol requires the read handle to close before the
        // locking handle acknowledges a pending break.
        reader_.reset();
        if (pending_ && locking_) {
            static_cast<void>(::CancelIoEx(locking_.get(), &overlapped_));
            DWORD ignored = 0U;
            static_cast<void>(::GetOverlappedResult(
                locking_.get(), &overlapped_, &ignored, TRUE));
        }
        pending_ = false;
        event_.reset();
        locking_.reset();
    }

private:
    OVERLAPPED overlapped_{};
    UniqueHandle locking_;
    UniqueHandle reader_;
    UniqueHandle event_;
    bool pending_{false};
};

[[nodiscard]] bool guard_published_files(
    const HANDLE root,
    const fs::path& root_path,
    const std::vector<MaterializedDirectoryPin>& directories,
    const std::vector<OwnedEntryWitness>& witnesses,
    const Inventory& source,
    const fs::path& manifest_relative,
    const std::span<const std::byte> manifest_bytes,
    const fs::path& marker_relative,
    const std::span<const std::byte> marker_bytes,
    const fs::path& retained_commit_relative,
    std::vector<std::unique_ptr<PublishedFileOplock>>& guards,
    DWORD& native_error) noexcept
{
    try {
        guards.clear();
        guards.reserve(static_cast<std::size_t>(std::ranges::count_if(
            witnesses, [](const OwnedEntryWitness& witness) {
                return !witness.directory;
            })));
        std::array<std::byte, 32U> manifest_digest{};
        std::array<std::byte, 32U> marker_digest{};
        if (!hash_bytes(manifest_bytes, manifest_digest) ||
            !hash_bytes(marker_bytes, marker_digest)) {
            native_error = ERROR_INVALID_DATA;
            return false;
        }
        for (const auto& witness : witnesses) {
            if (witness.directory || witness.relative_path == nullptr) {
                continue;
            }
            // The prepared commit candidate is already held with a retained
            // no-write/no-delete-share handle. Opening a Filter-oplock handle
            // for it would conflict with the DELETE-capable handle needed for
            // the final rename.
            if (path_equal(
                    *witness.relative_path, retained_commit_relative)) {
                continue;
            }
            const HANDLE parent = materialized_directory_handle(
                root, directories, witness.relative_path->parent_path());
            if (parent == INVALID_HANDLE_VALUE) {
                native_error = ERROR_PATH_NOT_FOUND;
                return false;
            }
            std::uint64_t expected_size = 0U;
            std::array<std::byte, 32U> expected_digest{};
            const auto source_entry = std::ranges::find_if(
                source.entries, [&witness](const InventoryEntry& entry) {
                    return path_equal(
                        entry.relative_path, *witness.relative_path);
                });
            if (source_entry != source.entries.end() &&
                !source_entry->directory) {
                expected_size = source_entry->snapshot.size;
                expected_digest = source_entry->digest;
            } else if (path_equal(
                           *witness.relative_path, manifest_relative)) {
                expected_size = manifest_bytes.size();
                expected_digest = manifest_digest;
            } else if (path_equal(
                           *witness.relative_path, marker_relative)) {
                expected_size = marker_bytes.size();
                expected_digest = marker_digest;
            } else {
                native_error = ERROR_INVALID_DATA;
                return false;
            }
            fs::path expected = root_path / *witness.relative_path;
            auto guard = std::make_unique<PublishedFileOplock>();
            if (!guard->begin(
                    parent, witness.relative_path->filename().native(),
                    expected, witness.identity, expected_size,
                    expected_digest, native_error)) {
                return false;
            }
            guards.push_back(std::move(guard));
        }
        native_error = ERROR_SUCCESS;
        return true;
    } catch (...) {
        native_error = ERROR_NOT_ENOUGH_MEMORY;
        return false;
    }
}

[[nodiscard]] UniqueHandle open_entry_for_delete(
    const fs::path& path) noexcept
{
    return UniqueHandle{::CreateFileW(
        path.c_str(), DELETE | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
}

[[nodiscard]] bool mark_open_entry_for_delete(const HANDLE handle) noexcept
{
    FILE_DISPOSITION_INFO disposition{};
    disposition.DeleteFile = TRUE;
    return ::SetFileInformationByHandle(
               handle, FileDispositionInfo, &disposition,
               sizeof(disposition)) != FALSE;
}

[[nodiscard]] const OwnedEntryWitness* find_owned_entry(
    const std::vector<OwnedEntryWitness>& witnesses,
    const fs::path& relative_path,
    const std::vector<bool>& removed,
    std::size_t& witness_index) noexcept
{
    for (std::size_t index = 0U; index < witnesses.size(); ++index) {
        if (!removed[index] && witnesses[index].relative_path != nullptr &&
            path_equal(*witnesses[index].relative_path, relative_path)) {
            witness_index = index;
            return &witnesses[index];
        }
    }
    return nullptr;
}

[[nodiscard]] bool safe_remove_owned_children(
    const fs::path& absolute_directory,
    const fs::path& relative_directory,
    const std::vector<OwnedEntryWitness>& witnesses,
    std::vector<bool>& removed,
    const fs::path* const delete_last_relative_path,
    bool* const delete_last_removed) noexcept
{
    std::vector<WIN32_FIND_DATAW> entries;
    DWORD error = ERROR_SUCCESS;
    if (!enumerate_directory(absolute_directory, entries, error)) {
        ::SetLastError(error);
        return false;
    }
    for (const bool delete_last_pass : {false, true}) {
        for (const auto& entry : entries) {
            fs::path absolute_child;
            fs::path relative_child;
            try {
                absolute_child = absolute_directory / entry.cFileName;
                relative_child = relative_directory / entry.cFileName;
            } catch (...) {
                ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return false;
            }
            const bool delete_last = delete_last_relative_path != nullptr &&
                path_equal(relative_child, *delete_last_relative_path);
            if (delete_last != delete_last_pass) continue;

            std::size_t witness_index = 0U;
            const auto* const witness = find_owned_entry(
                witnesses, relative_child, removed, witness_index);
            if (witness == nullptr) {
                // A descendant which was not created by this transaction may
                // have been inserted after staging became visible. Preserve it.
                ::SetLastError(ERROR_FILE_INVALID);
                return false;
            }

            auto locked = open_entry_for_delete(absolute_child);
            EntrySnapshot snapshot;
            if (!locked || !query_snapshot(locked.get(), snapshot) ||
                snapshot.identity != witness->identity ||
                snapshot.directory != witness->directory ||
                (snapshot.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
                !exact_opened_path(locked.get(), absolute_child)) {
                // FILE_FLAG_OPEN_REPARSE_POINT makes this check no-follow. A
                // swapped junction, symlink, file, or directory is never
                // removed and can never redirect recursive cleanup outside the
                // owned root.
                ::SetLastError(ERROR_FILE_INVALID);
                return false;
            }
            if (snapshot.directory &&
                !safe_remove_owned_children(
                    absolute_child, relative_child, witnesses, removed,
                    delete_last_relative_path, delete_last_removed)) {
                return false;
            }
            if (!mark_open_entry_for_delete(locked.get())) return false;
            if (delete_last && delete_last_removed != nullptr) {
                *delete_last_removed = true;
            }
            removed[witness_index] = true;
        }
    }
    return true;
}

// Destructive cleanup after publication must be tied to the directory object
// that was actually published.  A pathname-only cleanup would allow another
// process to rename the published tree away, substitute an unrelated ordinary
// directory at the same name, and have that replacement recursively removed
// by our verification-failure path.
[[nodiscard]] bool safe_remove_owned_tree_with_handle(
    const fs::path& root,
    const FileIdentity& expected_identity,
    const std::vector<OwnedEntryWitness>& witnesses,
    const HANDLE locked_root,
    const fs::path* const delete_last_relative_path = nullptr,
    bool* const delete_last_removed = nullptr) noexcept
{
    if (delete_last_removed != nullptr) *delete_last_removed = false;
    EntrySnapshot snapshot;
    if (locked_root == nullptr || locked_root == INVALID_HANDLE_VALUE ||
        !ordinary_directory_handle(locked_root, &snapshot) ||
        snapshot.identity != expected_identity ||
        !exact_opened_path(locked_root, root)) {
        ::SetLastError(ERROR_FILE_INVALID);
        return false;
    }

    std::vector<bool> removed;
    try {
        removed.resize(witnesses.size(), false);
    } catch (...) {
        ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    if (!safe_remove_owned_children(
            root, {}, witnesses, removed, delete_last_relative_path,
            delete_last_removed)) {
        return false;
    }
    if (std::ranges::find(removed, false) != removed.end()) {
        // An expected object disappeared or moved before cleanup. Do not
        // claim ownership of anything now reachable under its former name.
        ::SetLastError(ERROR_FILE_INVALID);
        return false;
    }
    return mark_open_entry_for_delete(locked_root);
}

[[nodiscard]] StockResearchCopyLimits published_inventory_limits(
    StockResearchCopyLimits limits,
    const std::size_t metadata_bytes,
    const std::size_t metadata_entries) noexcept
{
    limits.maximum_entries =
        limits.maximum_entries >
                (std::numeric_limits<std::size_t>::max)() - metadata_entries
            ? (std::numeric_limits<std::size_t>::max)()
            : limits.maximum_entries + metadata_entries;
    const auto bytes = static_cast<std::uint64_t>(metadata_bytes);
    limits.maximum_total_bytes =
        limits.maximum_total_bytes >
                (std::numeric_limits<std::uint64_t>::max)() - bytes
            ? (std::numeric_limits<std::uint64_t>::max)()
            : limits.maximum_total_bytes + bytes;
    limits.maximum_file_bytes = (std::max)(
        limits.maximum_file_bytes, static_cast<std::uint64_t>(metadata_bytes));
    return limits;
}

[[nodiscard]] const InventoryEntry* find_inventory_entry(
    const Inventory& inventory, const fs::path& relative_path) noexcept
{
    const auto found = std::ranges::find_if(
        inventory.entries, [&relative_path](const InventoryEntry& entry) {
            return path_equal(entry.relative_path, relative_path);
        });
    return found == inventory.entries.end() ? nullptr : &*found;
}

[[nodiscard]] bool published_inventory_matches(
    const Inventory& source,
    const Inventory& published,
    const FileIdentity& published_root_identity,
    const std::vector<OwnedEntryWitness>& witnesses,
    const fs::path& manifest_relative,
    const std::span<const std::byte> manifest_bytes,
    const fs::path& marker_relative,
    const std::span<const std::byte> marker_bytes,
    const fs::path& commit_candidate_relative,
    const std::span<const std::byte> commit_candidate_bytes) noexcept
{
    if (!published.summary.safe_to_materialize ||
        published.summary.root_reparse ||
        published.summary.internal_reparse_count != 0U ||
        published.summary.hardlink_count != 0U ||
        published.summary.alternate_data_stream_count != 0U ||
        published.summary.contained_target_count != 0U ||
        published.summary.escaped_target_count != 0U ||
        published.requested_root_snapshot.identity != published_root_identity ||
        published.canonical_root_snapshot.identity != published_root_identity ||
        published.entries.size() != witnesses.size() ||
        published.summary.entry_count != witnesses.size() ||
        !published.client_present || !published.server_present ||
        published.marker_present) {
        return false;
    }

    std::array<std::byte, 32U> manifest_digest{};
    std::array<std::byte, 32U> marker_digest{};
    std::array<std::byte, 32U> commit_candidate_digest{};
    if (!hash_bytes(manifest_bytes, manifest_digest) ||
        !hash_bytes(marker_bytes, marker_digest) ||
        !hash_bytes(commit_candidate_bytes, commit_candidate_digest)) {
        return false;
    }

    for (const auto& entry : published.entries) {
        const auto witness = std::ranges::find_if(
            witnesses, [&entry](const OwnedEntryWitness& candidate) {
                return candidate.relative_path != nullptr &&
                       path_equal(
                           *candidate.relative_path, entry.relative_path);
            });
        if (witness == witnesses.end() ||
            entry.snapshot.identity != witness->identity ||
            entry.directory != witness->directory ||
            (entry.snapshot.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
            return false;
        }

        if (const auto* const expected =
                find_inventory_entry(source, entry.relative_path);
            expected != nullptr) {
            if (entry.directory != expected->directory ||
                (!entry.directory &&
                 (entry.snapshot.size != expected->snapshot.size ||
                  entry.digest != expected->digest))) {
                return false;
            }
            continue;
        }

        const auto metadata_matches =
            [&entry](
                const fs::path& relative,
                const std::span<const std::byte> bytes,
                const std::array<std::byte, 32U>& digest) noexcept {
                return path_equal(entry.relative_path, relative) &&
                       !entry.directory && entry.snapshot.link_count == 1U &&
                       entry.snapshot.size == bytes.size() &&
                       entry.digest == digest;
            };
        if (!metadata_matches(
                manifest_relative, manifest_bytes, manifest_digest) &&
            !metadata_matches(marker_relative, marker_bytes, marker_digest) &&
            !metadata_matches(
                commit_candidate_relative, commit_candidate_bytes,
                commit_candidate_digest)) {
            return false;
        }
    }

    const auto metadata_bytes = static_cast<std::uint64_t>(
        manifest_bytes.size() + marker_bytes.size() +
        commit_candidate_bytes.size());
    return source.summary.byte_count <=
               (std::numeric_limits<std::uint64_t>::max)() - metadata_bytes &&
           published.summary.byte_count ==
               source.summary.byte_count + metadata_bytes;
}

struct PinnedDirectory final {
    fs::path expected_path;
    EntrySnapshot snapshot{};
    UniqueHandle handle;
};

struct DestinationPreparation final {
    fs::path destination;
    fs::path parent;
    EntrySnapshot parent_snapshot{};
    std::vector<PinnedDirectory> pinned_chain;

    [[nodiscard]] HANDLE parent_handle() const noexcept
    {
        return pinned_chain.empty() ? INVALID_HANDLE_VALUE
                                    : pinned_chain.back().handle.get();
    }
};

[[nodiscard]] bool query_registry_path(
    const HKEY root,
    const wchar_t* const key_name,
    const wchar_t* const value_name,
    std::vector<fs::path>& paths) noexcept
{
    HKEY raw_key = nullptr;
    const LSTATUS opened = ::RegOpenKeyExW(
        root, key_name, 0U, KEY_QUERY_VALUE, &raw_key);
    if (opened == ERROR_FILE_NOT_FOUND) return true;
    if (opened != ERROR_SUCCESS) return false;
    struct KeyCloser final {
        HKEY value;
        ~KeyCloser() { static_cast<void>(::RegCloseKey(value)); }
    } key{raw_key};
    DWORD type = 0U;
    DWORD bytes = 0U;
    LSTATUS status = ::RegQueryValueExW(
        key.value, value_name, nullptr, &type, nullptr, &bytes);
    if (status == ERROR_FILE_NOT_FOUND) return true;
    if (status != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) ||
        bytes > 64U * 1'024U || (bytes % sizeof(wchar_t)) != 0U) {
        return false;
    }
    try {
        std::wstring value(bytes / sizeof(wchar_t), L'\0');
        status = ::RegQueryValueExW(
            key.value, value_name, nullptr, &type,
            reinterpret_cast<LPBYTE>(value.data()), &bytes);
        if (status != ERROR_SUCCESS || value.empty()) return false;
        while (!value.empty() && value.back() == L'\0') value.pop_back();
        if (value.empty()) return false;
        paths.emplace_back(std::move(value));
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool read_bounded_text_file(
    const fs::path& path,
    const std::uint64_t maximum_bytes,
    std::string& output) noexcept
{
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = ::GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    auto file = open_entry(path, false, true);
    EntrySnapshot snapshot;
    if (!file || !query_snapshot(file.get(), snapshot) || snapshot.directory ||
        (snapshot.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
        snapshot.size > maximum_bytes ||
        snapshot.size >
            static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) {
        return false;
    }
    try {
        output.assign(static_cast<std::size_t>(snapshot.size), '\0');
    } catch (...) {
        return false;
    }
    std::size_t offset = 0U;
    while (offset < output.size()) {
        DWORD read = 0U;
        const auto count = static_cast<DWORD>((std::min)(
            output.size() - offset,
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        if (::ReadFile(file.get(), output.data() + offset, count, &read, nullptr) ==
                FALSE ||
            read == 0U) {
            return false;
        }
        offset += read;
    }
    EntrySnapshot after;
    return query_snapshot(file.get(), after) && after == snapshot &&
           output.find('\0') == std::string::npos;
}

[[nodiscard]] bool parse_libraryfolders_paths(
    const std::string_view text, std::vector<fs::path>& paths) noexcept
{
    try {
        std::size_t offset = 0U;
        constexpr std::string_view key{"\"path\""};
        while ((offset = text.find(key, offset)) != std::string_view::npos) {
            offset += key.size();
            while (offset < text.size() &&
                   (text[offset] == ' ' || text[offset] == '\t' ||
                    text[offset] == '\r' || text[offset] == '\n')) {
                ++offset;
            }
            if (offset >= text.size() || text[offset] != '"') return false;
            ++offset;
            std::string value;
            while (offset < text.size() && text[offset] != '"') {
                const char byte = text[offset++];
                if (byte == '\\') {
                    if (offset >= text.size()) return false;
                    const char escaped = text[offset++];
                    if (escaped != '\\' && escaped != '/') return false;
                    value.push_back(escaped == '/' ? '/' : '\\');
                } else {
                    const auto unsigned_byte = static_cast<unsigned char>(byte);
                    if (unsigned_byte < 0x20U) return false;
                    value.push_back(byte);
                }
                if (value.size() > 32'767U) return false;
            }
            if (offset >= text.size() || value.empty()) return false;
            ++offset;
            const int required = ::MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                static_cast<int>(value.size()), nullptr, 0);
            if (required <= 0) return false;
            std::wstring wide(static_cast<std::size_t>(required), L'\0');
            if (::MultiByteToWideChar(
                    CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                    static_cast<int>(value.size()), wide.data(), required) !=
                required) {
                return false;
            }
            paths.emplace_back(std::move(wide));
            if (paths.size() > 256U) return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool configured_steam_library_roots(
    const std::vector<fs::path>& additional,
    std::vector<PhysicalLocation>& canonical_roots) noexcept
{
    try {
        std::vector<fs::path> requested = additional;
        constexpr std::array<const wchar_t*, 3U> keys{
            L"Software\\Valve\\Steam",
            L"SOFTWARE\\WOW6432Node\\Valve\\Steam",
            L"SOFTWARE\\Valve\\Steam"};
        for (std::size_t index = 0U; index < keys.size(); ++index) {
            const HKEY hive = index == 0U ? HKEY_CURRENT_USER
                                         : HKEY_LOCAL_MACHINE;
            if (!query_registry_path(
                    hive, keys[index], L"SteamPath", requested) ||
                !query_registry_path(
                    hive, keys[index], L"InstallPath", requested)) {
                return false;
            }
        }
        if (requested.size() > 256U) return false;
        const auto seed_count = requested.size();
        for (std::size_t index = 0U; index < seed_count; ++index) {
            std::string vdf;
            const auto manifest =
                requested[index] / L"steamapps" / L"libraryfolders.vdf";
            if (!read_bounded_text_file(manifest, 1U * 1'024U * 1'024U, vdf) ||
                !parse_libraryfolders_paths(vdf, requested)) {
                return false;
            }
            if (requested.size() > 256U) return false;
        }
        for (const auto& requested_root : requested) {
            if (requested_root.empty() || !requested_root.is_absolute() ||
                is_unc_path(requested_root)) {
                continue;
            }
            auto directory = open_entry(requested_root.lexically_normal(), true);
            EntrySnapshot snapshot;
            fs::path final;
            PhysicalLocation physical;
            if (!directory) {
                const DWORD error = ::GetLastError();
                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
                    continue;
                }
                return false;
            }
            if (!ordinary_directory_handle(directory.get(), &snapshot) ||
                !query_final_path(directory.get(), final) ||
                !query_physical_location(
                    directory.get(), snapshot, physical) ||
                !local_fixed_volume(final)) {
                return false;
            }
            final = final.lexically_normal();
            if (std::ranges::none_of(
                    canonical_roots,
                    [&physical](const PhysicalLocation& existing) {
                        return physical_equal(existing, physical);
                    })) {
                canonical_roots.push_back(std::move(physical));
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] std::optional<DestinationPreparation> prepare_destination_parent(
    const fs::path& requested,
    const Inventory& source,
    const StockResearchCopyOptions& options,
    const StockExternalApprovalValidation* const external_approval,
    MaterializeFailure& failure) noexcept
{
    try {
        const auto& limits = options.limits;
        if (requested.empty() || !requested.is_absolute() ||
            !path_size_valid(requested, limits) || is_unc_path(requested) ||
            !has_drive_letter(requested)) {
            failure = {StockResearchCopyErrorCode::destination_not_absolute,
                       ERROR_INVALID_NAME};
            return std::nullopt;
        }
        const fs::path destination = requested.lexically_normal();
        if (!valid_relative_leaf(destination.filename().native())) {
            failure = {StockResearchCopyErrorCode::destination_not_absolute,
                       ERROR_INVALID_NAME};
            return std::nullopt;
        }
        if (::GetFileAttributesW(destination.c_str()) !=
            INVALID_FILE_ATTRIBUTES) {
            failure = {StockResearchCopyErrorCode::destination_exists,
                       ERROR_ALREADY_EXISTS};
            return std::nullopt;
        }
        if (is_substituted_drive(destination)) {
            failure = {StockResearchCopyErrorCode::destination_subst_drive,
                       ERROR_INVALID_DRIVE};
            return std::nullopt;
        }
        if (!local_fixed_volume(destination.root_path())) {
            failure = {
                StockResearchCopyErrorCode::destination_not_local_fixed_volume,
                ERROR_NOT_SUPPORTED};
            return std::nullopt;
        }
        if (path_component_is_steamapps(destination)) {
            failure = {
                StockResearchCopyErrorCode::destination_overlaps_steam_library,
                ERROR_ACCESS_DENIED};
            return std::nullopt;
        }
        std::vector<PhysicalLocation> steam_roots;
        if (!configured_steam_library_roots(
                options.configured_steam_library_roots, steam_roots)) {
            failure = {
                StockResearchCopyErrorCode::destination_parent_invalid,
                ERROR_INVALID_DATA};
            return std::nullopt;
        }
        if (same_or_below(source.canonical_root, destination) ||
            same_or_below(destination, source.canonical_root)) {
            failure = {
                StockResearchCopyErrorCode::destination_overlaps_source,
                ERROR_ACCESS_DENIED};
            return std::nullopt;
        }

        const fs::path parent = destination.parent_path();
        const fs::path root = parent.root_path().lexically_normal();
        std::vector<fs::path> components;
        for (const auto& component : parent.relative_path()) {
            if (!valid_relative_leaf(component.native())) {
                failure = {
                    StockResearchCopyErrorCode::destination_parent_invalid,
                    ERROR_INVALID_NAME};
                return std::nullopt;
            }
            components.push_back(component);
        }
        std::optional<std::size_t> first_missing;
        fs::path observed = root;
        for (std::size_t index = 0U; index < components.size(); ++index) {
            observed /= components[index];
            const DWORD attributes = ::GetFileAttributesW(observed.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) {
                const DWORD query_error = ::GetLastError();
                if (query_error != ERROR_FILE_NOT_FOUND &&
                    query_error != ERROR_PATH_NOT_FOUND) {
                    failure = {
                        StockResearchCopyErrorCode::destination_parent_invalid,
                        query_error};
                    return std::nullopt;
                }
                if (!first_missing) first_missing = index;
            } else if (first_missing ||
                       (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
                failure = {
                    StockResearchCopyErrorCode::destination_parent_invalid,
                    static_cast<DWORD>(
                        first_missing ? ERROR_FILE_INVALID
                                      : ERROR_DIRECTORY)};
                return std::nullopt;
            }
        }

        DWORD native_error = ERROR_SUCCESS;
        const bool root_writable = components.empty() ||
                                   (first_missing && *first_missing == 0U);
        auto root_handle = open_absolute_directory_locked(
            root, root_writable, native_error);
        EntrySnapshot root_snapshot;
        if (!root_handle ||
            !ordinary_directory_handle(root_handle.get(), &root_snapshot) ||
            !exact_opened_path(root_handle.get(), root)) {
            failure = {
                StockResearchCopyErrorCode::destination_parent_reparse,
                native_error == ERROR_SUCCESS ? ERROR_FILE_INVALID
                                              : native_error};
            return std::nullopt;
        }
        PhysicalLocation prospective_destination;
        if (!query_physical_location(
                root_handle.get(), root_snapshot,
                prospective_destination)) {
            failure = {
                StockResearchCopyErrorCode::destination_parent_invalid,
                ::GetLastError()};
            return std::nullopt;
        }
        for (const auto& component : components) {
            prospective_destination.volume_relative_path /= component;
        }
        prospective_destination.volume_relative_path /= destination.filename();
        prospective_destination.volume_relative_path =
            prospective_destination.volume_relative_path.lexically_normal();
        const PhysicalLocation source_location{
            source.canonical_root_snapshot.identity.volume_serial,
            source.canonical_root_volume_relative_path};
        if (physical_overlap(source_location, prospective_destination)) {
            failure = {
                StockResearchCopyErrorCode::destination_overlaps_source,
                ERROR_ACCESS_DENIED};
            return std::nullopt;
        }
        if (external_approval != nullptr) {
            for (const auto& target : external_approval->targets) {
                auto target_handle = open_entry(target.target_root, true);
                EntrySnapshot target_snapshot;
                PhysicalLocation target_location;
                if (!target_handle ||
                    !query_snapshot(target_handle.get(), target_snapshot) ||
                    !query_physical_location(
                        target_handle.get(), target_snapshot,
                        target_location)) {
                    failure = {
                        StockResearchCopyErrorCode::
                            external_target_approval_mismatch,
                        ::GetLastError()};
                    return std::nullopt;
                }
                if (physical_overlap(
                        target_location, prospective_destination)) {
                    failure = {
                        StockResearchCopyErrorCode::
                            external_materialization_path_collision,
                        ERROR_ACCESS_DENIED};
                    return std::nullopt;
                }
            }
        }
        if (std::ranges::any_of(
                steam_roots,
                [&prospective_destination](
                    const PhysicalLocation& steam_root) {
                    return physical_overlap(
                        steam_root, prospective_destination);
                })) {
            failure = {
                StockResearchCopyErrorCode::
                    destination_overlaps_steam_library,
                ERROR_ACCESS_DENIED};
            return std::nullopt;
        }

        std::vector<PinnedDirectory> pinned_chain;
        pinned_chain.reserve(components.size() + 1U);
        pinned_chain.push_back(PinnedDirectory{
            root, root_snapshot, std::move(root_handle)});
        fs::path current = root;
        for (std::size_t index = 0U; index < components.size(); ++index) {
            current /= components[index];
            const bool create = first_missing && index >= *first_missing;
            const bool writable = create || index + 1U == components.size() ||
                                  (first_missing &&
                                   index + 1U == *first_missing);
            auto child = create
                ? create_relative_directory_locked(
                      pinned_chain.back().handle.get(),
                      components[index].native(), false, native_error)
                : open_relative_directory_locked(
                      pinned_chain.back().handle.get(),
                      components[index].native(), writable, native_error);
            EntrySnapshot child_snapshot;
            if (!child ||
                !ordinary_directory_handle(child.get(), &child_snapshot) ||
                child_snapshot.identity.volume_serial !=
                    root_snapshot.identity.volume_serial ||
                !exact_opened_path(child.get(), current)) {
                failure = {
                    create
                        ? StockResearchCopyErrorCode::destination_create_failed
                        : StockResearchCopyErrorCode::
                              destination_parent_reparse,
                    native_error == ERROR_SUCCESS ? ERROR_FILE_INVALID
                                                  : native_error};
                return std::nullopt;
            }
            pinned_chain.push_back(PinnedDirectory{
                current, child_snapshot, std::move(child)});
        }
        if (!local_fixed_volume(root)) {
            failure = {
                StockResearchCopyErrorCode::destination_parent_invalid,
                ERROR_NOT_SUPPORTED};
            return std::nullopt;
        }
        auto existing_destination = nt_create_relative_entry(
            pinned_chain.back().handle.get(), destination.filename().native(),
            FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
            FILE_ATTRIBUTE_NORMAL,
            FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT |
                FILE_OPEN_FOR_BACKUP_INTENT,
            native_error);
        if (existing_destination) {
            failure = {StockResearchCopyErrorCode::destination_exists,
                       ERROR_ALREADY_EXISTS};
            return std::nullopt;
        }
        if (native_error != ERROR_FILE_NOT_FOUND &&
            native_error != ERROR_PATH_NOT_FOUND) {
            failure = {
                StockResearchCopyErrorCode::destination_parent_invalid,
                native_error};
            return std::nullopt;
        }
        const EntrySnapshot parent_snapshot = pinned_chain.back().snapshot;
        failure = {};
        return DestinationPreparation{
            destination, parent, parent_snapshot, std::move(pinned_chain)};
    } catch (...) {
        failure = {StockResearchCopyErrorCode::destination_parent_invalid,
                   ERROR_NOT_ENOUGH_MEMORY};
        return std::nullopt;
    }
}

[[nodiscard]] bool parent_still_exact(
    const DestinationPreparation& destination) noexcept
{
    for (const auto& pinned : destination.pinned_chain) {
        EntrySnapshot snapshot;
        if (!ordinary_directory_handle(pinned.handle.get(), &snapshot) ||
            snapshot.identity != pinned.snapshot.identity ||
            !exact_opened_path(
                pinned.handle.get(), pinned.expected_path)) {
            return false;
        }
    }
    return !destination.pinned_chain.empty() &&
           destination.pinned_chain.back().snapshot.identity ==
               destination.parent_snapshot.identity;
}

[[nodiscard]] bool rename_open_file_without_replace(
    const HANDLE handle,
    const HANDLE parent,
    const std::wstring_view destination_leaf) noexcept
{
    if (parent == nullptr || parent == INVALID_HANDLE_VALUE ||
        !valid_relative_leaf(destination_leaf) ||
        destination_leaf.size() >
            (static_cast<std::size_t>((std::numeric_limits<DWORD>::max)()) -
             sizeof(FILE_RENAME_INFO)) /
                sizeof(wchar_t)) {
        ::SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return false;
    }
    const auto name_bytes = destination_leaf.size() * sizeof(wchar_t);
    // Keep the SDK structure's trailing WCHAR storage in addition to the
    // non-NUL-terminated name bytes; older Windows builds consume the full
    // documented structure size even though FileNameLength excludes it.
    const auto bytes = offsetof(FILE_RENAME_INFO, FileName) + name_bytes;
    std::vector<std::byte> storage;
    try {
        storage.resize(bytes);
    } catch (...) {
        ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    auto* const info = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    info->ReplaceIfExists = FALSE;
    info->RootDirectory = parent;
    info->FileNameLength = static_cast<DWORD>(name_bytes);
    std::copy(
        destination_leaf.begin(), destination_leaf.end(), info->FileName);
    IO_STATUS_BLOCK status_block{};
    constexpr auto rename_information =
        static_cast<FILE_INFORMATION_CLASS>(10);
    const NTSTATUS status = ::NtSetInformationFile(
        handle, &status_block, info, static_cast<ULONG>(storage.size()),
        rename_information);
    if (status < 0) {
        ::SetLastError(::RtlNtStatusToDosError(status));
        return false;
    }
    return true;
}

[[nodiscard]] bool write_all(
    const HANDLE handle, const std::span<const std::byte> bytes) noexcept
{
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto chunk = static_cast<DWORD>((std::min)(
            remaining,
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD written = 0U;
        if (::WriteFile(
                handle, bytes.data() + offset, chunk, &written, nullptr) ==
                FALSE ||
            written == 0U) {
            return false;
        }
        offset += written;
    }
    return true;
}

[[nodiscard]] bool atomic_write_small(
    const HANDLE directory_handle,
    const fs::path& directory,
    const fs::path& destination,
    const std::span<const std::byte> bytes,
    std::optional<FileIdentity>& published_identity,
    MaterializeFailure& failure) noexcept
{
    published_identity.reset();
    const auto leaf = random_leaf(L".hlclient-research-entry-");
    if (!leaf) {
        failure = {StockResearchCopyErrorCode::destination_create_failed,
                   ERROR_NOT_ENOUGH_MEMORY};
        return false;
    }
    fs::path temporary;
    try {
        temporary = directory / *leaf;
    } catch (...) {
        failure = {StockResearchCopyErrorCode::destination_create_failed,
                   ERROR_NOT_ENOUGH_MEMORY};
        return false;
    }
    DWORD native_error = ERROR_SUCCESS;
    auto output = create_relative_file_locked(
        directory_handle, *leaf, native_error);
    const auto discard_output = [&output]() noexcept {
        if (output) {
            static_cast<void>(mark_open_entry_for_delete(output.get()));
            output.reset();
        }
    };
    if (!output) {
        failure = {StockResearchCopyErrorCode::destination_create_failed,
                   native_error};
        return false;
    }
    if (!ordinary_file_handle(output.get(), 0U) ||
        !exact_opened_path(output.get(), temporary)) {
        failure = {StockResearchCopyErrorCode::destination_create_failed,
                   ERROR_FILE_INVALID};
        discard_output();
        return false;
    }
    if (!write_all(output.get(), bytes) ||
        ::FlushFileBuffers(output.get()) == FALSE ||
        !ordinary_file_handle(output.get(), bytes.size()) ||
        !rename_open_file_without_replace(
            output.get(), directory_handle, destination.filename().native()) ||
        !ordinary_file_handle(output.get(), bytes.size()) ||
        !exact_opened_path(output.get(), destination)) {
        failure = {StockResearchCopyErrorCode::destination_write_failed,
                   ::GetLastError()};
        discard_output();
        return false;
    }
    EntrySnapshot published_snapshot;
    if (!ordinary_file_handle(
            output.get(), bytes.size(), &published_snapshot)) {
        failure = {StockResearchCopyErrorCode::destination_identity_invalid,
                   ERROR_FILE_INVALID};
        discard_output();
        return false;
    }
    published_identity = published_snapshot.identity;
    output.reset();
    std::size_t ads = 0U;
    if (!has_only_default_stream(destination, {}, ads)) {
        failure = {
            StockResearchCopyErrorCode::destination_identity_invalid,
            ERROR_INVALID_DATA};
        return false;
    }
    return true;
}

struct PreparedCommitMarker final {
    fs::path relative_path;
    fs::path absolute_path;
    EntrySnapshot snapshot{};
    std::array<std::byte, 32U> digest{};
    UniqueHandle handle;
};

// Build the authorizing bytes before the final mutation-witness window. The
// retained no-delete-share handle is the candidate's integrity guard;
// putting the create/write/flush work after the last watcher check would mask a
// simultaneous external mutation behind our own expected notification.
[[nodiscard]] bool prepare_commit_marker(
    const HANDLE directory_handle,
    const fs::path& directory,
    const std::span<const std::byte> bytes,
    PreparedCommitMarker& prepared,
    MaterializeFailure& failure) noexcept
{
    const auto leaf = random_leaf(L".hlclient-research-commit-");
    if (!leaf) {
        failure = {StockResearchCopyErrorCode::destination_create_failed,
                   ERROR_NOT_ENOUGH_MEMORY};
        return false;
    }
    try {
        prepared.relative_path = fs::path{*leaf};
        prepared.absolute_path = directory / prepared.relative_path;
    } catch (...) {
        failure = {StockResearchCopyErrorCode::destination_create_failed,
                   ERROR_NOT_ENOUGH_MEMORY};
        return false;
    }
    DWORD native_error = ERROR_SUCCESS;
    auto output = create_relative_file_locked(
        directory_handle, *leaf, native_error);
    const auto discard_output = [&output]() noexcept {
        if (output) {
            static_cast<void>(mark_open_entry_for_delete(output.get()));
            output.reset();
        }
    };
    if (!output) {
        failure = {StockResearchCopyErrorCode::destination_create_failed,
                   native_error};
        return false;
    }

    std::array<std::byte, 32U> expected_digest{};
    std::array<std::byte, 32U> observed_digest{};
    EntrySnapshot before;
    EntrySnapshot after;
    if (!ordinary_file_handle(output.get(), 0U) ||
        !exact_opened_path(output.get(), prepared.absolute_path) ||
        !write_all(output.get(), bytes) ||
        ::FlushFileBuffers(output.get()) == FALSE ||
        !ordinary_file_handle(output.get(), bytes.size(), &before) ||
        before.link_count != 1U ||
        !handle_has_only_default_stream(output.get()) ||
        !hash_bytes(bytes, expected_digest) ||
        !hash_handle(output.get(), observed_digest) ||
        observed_digest != expected_digest ||
        !query_snapshot(output.get(), after) || before != after) {
        const DWORD verify_error = ::GetLastError();
        failure = {StockResearchCopyErrorCode::destination_write_failed,
                   verify_error == ERROR_SUCCESS ? ERROR_FILE_INVALID
                                                 : verify_error};
        discard_output();
        return false;
    }
    prepared.snapshot = after;
    prepared.digest = observed_digest;
    prepared.handle = std::move(output);
    return true;
}

[[nodiscard]] bool repin_prepared_commit_marker(
    const HANDLE directory_handle,
    const fs::path& directory,
    const std::span<const std::byte> bytes,
    PreparedCommitMarker& prepared,
    DWORD& native_error) noexcept
{
    fs::path absolute;
    try {
        absolute = directory / prepared.relative_path;
    } catch (...) {
        native_error = ERROR_NOT_ENOUGH_MEMORY;
        return false;
    }
    auto reopened = nt_create_relative_entry(
        directory_handle, prepared.relative_path.filename().native(),
        GENERIC_READ | FILE_WRITE_DATA | FILE_READ_ATTRIBUTES | DELETE |
            SYNCHRONIZE,
        FILE_SHARE_READ, FILE_OPEN, FILE_ATTRIBUTE_NORMAL,
        FILE_NON_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT |
            FILE_SYNCHRONOUS_IO_NONALERT,
        native_error);
    EntrySnapshot before;
    EntrySnapshot after;
    std::array<std::byte, 32U> digest{};
    if (!reopened ||
        !ordinary_file_handle(reopened.get(), bytes.size(), &before) ||
        before.identity != prepared.snapshot.identity ||
        !exact_opened_path(reopened.get(), absolute) ||
        !handle_has_only_default_stream(reopened.get()) ||
        !hash_handle(reopened.get(), digest) || digest != prepared.digest ||
        !query_snapshot(reopened.get(), after) || before != after) {
        if (native_error == ERROR_SUCCESS) native_error = ::GetLastError();
        if (native_error == ERROR_SUCCESS) native_error = ERROR_FILE_INVALID;
        return false;
    }
    prepared.absolute_path = std::move(absolute);
    prepared.snapshot = after;
    prepared.handle = std::move(reopened);
    native_error = ERROR_SUCCESS;
    return true;
}

[[nodiscard]] bool prepared_commit_marker_unchanged(
    const PreparedCommitMarker& prepared,
    const std::span<const std::byte> bytes,
    DWORD& native_error) noexcept
{
    EntrySnapshot before;
    EntrySnapshot after;
    std::array<std::byte, 32U> digest{};
    if (!prepared.handle ||
        !ordinary_file_handle(prepared.handle.get(), bytes.size(), &before) ||
        before != prepared.snapshot ||
        !exact_opened_path(prepared.handle.get(), prepared.absolute_path) ||
        !handle_has_only_default_stream(prepared.handle.get()) ||
        !hash_handle(prepared.handle.get(), digest) ||
        digest != prepared.digest ||
        !query_snapshot(prepared.handle.get(), after) || before != after) {
        native_error = ::GetLastError();
        if (native_error == ERROR_SUCCESS) native_error = ERROR_FILE_INVALID;
        return false;
    }
    native_error = ERROR_SUCCESS;
    return true;
}

enum class CommitMarkerRevocation {
    removed,
    restored_candidate,
    invalidated,
    failed,
};

[[nodiscard]] CommitMarkerRevocation revoke_commit_marker(
    const HANDLE directory_handle,
    PreparedCommitMarker& prepared) noexcept
{
    if (!prepared.handle) return CommitMarkerRevocation::failed;
    DWORD first_error = ERROR_SUCCESS;
    if (mark_open_entry_for_delete(prepared.handle.get())) {
        prepared.handle.reset();
        return CommitMarkerRevocation::removed;
    }
    first_error = ::GetLastError();

    if (rename_open_file_without_replace(
            prepared.handle.get(), directory_handle,
            prepared.relative_path.filename().native()) &&
        exact_opened_path(prepared.handle.get(), prepared.absolute_path)) {
        prepared.handle.reset();
        return CommitMarkerRevocation::restored_candidate;
    }

    // A valid marker name must never survive an observed pre-commit mutation.
    // If both namespace revocation operations fail, invalidate the retained
    // file contents in place so exact-marker consumers still fail closed.
    LARGE_INTEGER zero{};
    if (::SetFilePointerEx(
            prepared.handle.get(), zero, nullptr, FILE_BEGIN) != FALSE &&
        ::SetEndOfFile(prepared.handle.get()) != FALSE &&
        ::FlushFileBuffers(prepared.handle.get()) != FALSE) {
        prepared.handle.reset();
        return CommitMarkerRevocation::invalidated;
    }
    const DWORD invalidate_error = ::GetLastError();
    prepared.handle.reset();
    ::SetLastError(
        invalidate_error == ERROR_SUCCESS ? first_error : invalidate_error);
    return CommitMarkerRevocation::failed;
}

[[nodiscard]] bool write_cleanup_failure_metadata(
    const HANDLE directory_handle,
    const fs::path& directory,
    DWORD& native_error,
    fs::path* const relative_path = nullptr,
    std::optional<FileIdentity>* const published_identity = nullptr) noexcept
{
    if (relative_path != nullptr) relative_path->clear();
    if (published_identity != nullptr) published_identity->reset();
    const auto leaf = random_leaf(L".hlclient-stock-research-failure-");
    if (!leaf) {
        native_error = ERROR_NOT_ENOUGH_MEMORY;
        return false;
    }
    fs::path relative;
    fs::path destination;
    try {
        relative = fs::path{*leaf};
        destination = directory / relative;
    } catch (...) {
        native_error = ERROR_NOT_ENOUGH_MEMORY;
        return false;
    }
    const auto bytes = std::as_bytes(std::span{
        kCleanupFailureMetadata.data(), kCleanupFailureMetadata.size()});
    MaterializeFailure failure;
    std::optional<FileIdentity> identity;
    if (!atomic_write_small(
            directory_handle, directory, destination, bytes, identity,
            failure)) {
        native_error = failure.native_error == ERROR_SUCCESS
            ? ERROR_WRITE_FAULT
            : failure.native_error;
        return false;
    }
    if (!identity) {
        native_error = ERROR_FILE_INVALID;
        return false;
    }
    if (relative_path != nullptr) *relative_path = std::move(relative);
    if (published_identity != nullptr) *published_identity = *identity;
    native_error = ERROR_SUCCESS;
    return true;
}

// A DELETE-capable directory handle is deliberately retained during the
// publication/quarantine transitions. Older Windows versions can reject a
// child rename which names such a handle as FILE_RENAME_INFO::RootDirectory,
// so the failure-only diagnostic needs a no-rename publication path. The
// marker is non-authorizing: a partially written file remains fail-closed and
// is deleted by its still-open handle whenever verification does not complete.
[[nodiscard]] bool write_cleanup_failure_metadata_direct(
    const HANDLE directory_handle,
    const fs::path& directory,
    DWORD& native_error,
    fs::path* const relative_path = nullptr,
    std::optional<FileIdentity>* const published_identity = nullptr) noexcept
{
    if (relative_path != nullptr) relative_path->clear();
    if (published_identity != nullptr) published_identity->reset();
    const auto leaf = random_leaf(L".hlclient-stock-research-failure-");
    if (!leaf) {
        native_error = ERROR_NOT_ENOUGH_MEMORY;
        return false;
    }

    fs::path relative;
    fs::path destination;
    try {
        relative = fs::path{*leaf};
        destination = directory / relative;
    } catch (...) {
        native_error = ERROR_NOT_ENOUGH_MEMORY;
        return false;
    }

    auto output = create_relative_file_locked(
        directory_handle, *leaf, native_error);
    const auto discard_output = [&output]() noexcept {
        if (output) {
            static_cast<void>(mark_open_entry_for_delete(output.get()));
            output.reset();
        }
    };
    if (!output) return false;

    const auto bytes = std::as_bytes(std::span{
        kCleanupFailureMetadata.data(), kCleanupFailureMetadata.size()});
    std::array<std::byte, 32U> expected_digest{};
    std::array<std::byte, 32U> observed_digest{};
    EntrySnapshot before;
    EntrySnapshot after;
    if (!ordinary_file_handle(output.get(), 0U) ||
        !exact_opened_path(output.get(), destination) ||
        !handle_has_only_default_stream(output.get()) ||
        !write_all(output.get(), bytes) ||
        ::FlushFileBuffers(output.get()) == FALSE ||
        !ordinary_file_handle(output.get(), bytes.size(), &before) ||
        !exact_opened_path(output.get(), destination) ||
        !handle_has_only_default_stream(output.get()) ||
        !hash_bytes(bytes, expected_digest) ||
        !hash_handle(output.get(), observed_digest) ||
        observed_digest != expected_digest ||
        !query_snapshot(output.get(), after) || before != after) {
        native_error = ::GetLastError();
        if (native_error == ERROR_SUCCESS) native_error = ERROR_FILE_INVALID;
        discard_output();
        return false;
    }

    if (relative_path != nullptr) *relative_path = std::move(relative);
    if (published_identity != nullptr) {
        *published_identity = after.identity;
    }
    output.reset();
    native_error = ERROR_SUCCESS;
    return true;
}

[[nodiscard]] bool copy_file_verified(
    const InventoryEntry& source_entry,
    const HANDLE destination_directory_handle,
    const fs::path& destination_directory,
    const fs::path& destination_file,
    const StockResearchCopyOptions& options,
    const std::size_t file_ordinal,
    std::optional<FileIdentity>& published_identity,
    MaterializeFailure& failure) noexcept
{
    published_identity.reset();
    DWORD native_error = ERROR_SUCCESS;
    if (!validate_witnesses(source_entry.witnesses, native_error)) {
        failure = {
            StockResearchCopyErrorCode::source_changed_during_materialization,
            native_error};
        return false;
    }
    auto source = open_entry(source_entry.physical_path, false, true);
    EntrySnapshot before;
    if (!source || !query_snapshot(source.get(), before)) {
        failure = {StockResearchCopyErrorCode::source_read_failed,
                   ::GetLastError()};
        return false;
    }
    if (before != source_entry.snapshot || before.directory ||
        (before.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        failure = {
            StockResearchCopyErrorCode::source_changed_during_materialization,
            ERROR_FILE_INVALID};
        return false;
    }
    if (options.progress_hook != nullptr) {
        options.progress_hook(
            StockResearchCopyProgressPhase::source_file_opened, file_ordinal,
            options.progress_context);
    }

    const auto leaf = random_leaf(L".hlclient-research-file-");
    if (!leaf) {
        failure = {StockResearchCopyErrorCode::destination_create_failed,
                   ERROR_NOT_ENOUGH_MEMORY};
        return false;
    }
    fs::path temporary;
    try {
        temporary = destination_directory / *leaf;
    } catch (...) {
        failure = {StockResearchCopyErrorCode::destination_create_failed,
                   ERROR_NOT_ENOUGH_MEMORY};
        return false;
    }
    auto output = create_relative_file_locked(
        destination_directory_handle, *leaf, native_error);
    const auto discard_output = [&output]() noexcept {
        if (output) {
            static_cast<void>(mark_open_entry_for_delete(output.get()));
            output.reset();
        }
    };
    if (!output || !ordinary_file_handle(output.get(), 0U) ||
        !exact_opened_path(output.get(), temporary)) {
        failure = {StockResearchCopyErrorCode::destination_create_failed,
                   output ? ERROR_FILE_INVALID : native_error};
        discard_output();
        return false;
    }

    LARGE_INTEGER zero{};
    UniqueAlgorithm algorithm;
    DWORD hash_object_size = 0U;
    DWORD hash_property_bytes = 0U;
    if (::SetFilePointerEx(source.get(), zero, nullptr, FILE_BEGIN) == FALSE ||
        ::BCryptOpenAlgorithmProvider(
            algorithm.put(), BCRYPT_SHA256_ALGORITHM, nullptr, 0U) < 0 ||
        ::BCryptGetProperty(
            algorithm.get(), BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&hash_object_size),
            sizeof(hash_object_size), &hash_property_bytes, 0U) < 0 ||
        hash_property_bytes != sizeof(hash_object_size) ||
        hash_object_size == 0U ||
        hash_object_size > 1U * 1'024U * 1'024U) {
        failure = {StockResearchCopyErrorCode::source_digest_failed,
                   ::GetLastError()};
        discard_output();
        return false;
    }
    std::vector<UCHAR> hash_object;
    try {
        hash_object.resize(hash_object_size);
    } catch (...) {
        failure = {StockResearchCopyErrorCode::source_digest_failed,
                   ERROR_NOT_ENOUGH_MEMORY};
        discard_output();
        return false;
    }
    UniqueHash hash;
    if (::BCryptCreateHash(
            algorithm.get(), hash.put(), hash_object.data(),
            static_cast<ULONG>(hash_object.size()), nullptr, 0U, 0U) < 0) {
        failure = {StockResearchCopyErrorCode::source_digest_failed,
                   ERROR_INVALID_DATA};
        discard_output();
        return false;
    }
    std::array<UCHAR, 64U * 1'024U> buffer{};
    std::uint64_t copied = 0U;
    for (;;) {
        DWORD read = 0U;
        if (::ReadFile(
                source.get(), buffer.data(), static_cast<DWORD>(buffer.size()),
                &read, nullptr) == FALSE) {
            failure = {StockResearchCopyErrorCode::source_read_failed,
                       ::GetLastError()};
            discard_output();
            return false;
        }
        if (read == 0U) break;
        if (read > source_entry.snapshot.size ||
            copied > source_entry.snapshot.size - read) {
            failure = {
                StockResearchCopyErrorCode::
                    source_changed_during_materialization,
                ERROR_FILE_INVALID};
            discard_output();
            return false;
        }
        if (::BCryptHashData(hash.get(), buffer.data(), read, 0U) < 0) {
            failure = {StockResearchCopyErrorCode::source_digest_failed,
                       ERROR_INVALID_DATA};
            discard_output();
            return false;
        }
        if (!write_all(
                output.get(),
                std::as_bytes(std::span{buffer.data(),
                                        static_cast<std::size_t>(read)}))) {
            failure = {StockResearchCopyErrorCode::destination_write_failed,
                       ::GetLastError()};
            discard_output();
            return false;
        }
        copied += read;
    }
    std::array<std::byte, 32U> copied_digest{};
    if (::BCryptFinishHash(
            hash.get(), reinterpret_cast<PUCHAR>(copied_digest.data()),
            static_cast<ULONG>(copied_digest.size()), 0U) < 0) {
        failure = {StockResearchCopyErrorCode::source_digest_failed,
                   ERROR_INVALID_DATA};
        discard_output();
        return false;
    }
    EntrySnapshot after;
    if (!query_snapshot(source.get(), after) || after != before ||
        copied != source_entry.snapshot.size ||
        copied_digest != source_entry.digest) {
        failure = {
            StockResearchCopyErrorCode::source_changed_during_materialization,
            ERROR_FILE_INVALID};
        discard_output();
        return false;
    }
    if (::FlushFileBuffers(output.get()) == FALSE) {
        failure = {StockResearchCopyErrorCode::destination_flush_failed,
                   ::GetLastError()};
        discard_output();
        return false;
    }
    EntrySnapshot created;
    if (!ordinary_file_handle(
            output.get(), source_entry.snapshot.size, &created)) {
        failure = {
            StockResearchCopyErrorCode::destination_identity_invalid,
            ERROR_FILE_INVALID};
        discard_output();
        return false;
    }
    if (created.identity == source_entry.snapshot.identity) {
        failure = {
            StockResearchCopyErrorCode::destination_identity_invalid,
            ERROR_DUP_NAME};
        discard_output();
        return false;
    }
    if (!exact_opened_path(output.get(), temporary)) {
        failure = {
            StockResearchCopyErrorCode::destination_identity_invalid,
            ERROR_INVALID_NAME};
        discard_output();
        return false;
    }
    if (!rename_open_file_without_replace(
            output.get(), destination_directory_handle,
            destination_file.filename().native())) {
        failure = {
            StockResearchCopyErrorCode::destination_publish_failed,
            ::GetLastError()};
        discard_output();
        return false;
    }
    if (!ordinary_file_handle(
            output.get(), source_entry.snapshot.size, &created)) {
        failure = {
            StockResearchCopyErrorCode::destination_identity_invalid,
            ERROR_FILE_INVALID};
        discard_output();
        return false;
    }
    published_identity = created.identity;

    auto verified = open_relative_file_locked(
        destination_directory_handle, destination_file.filename().native(),
        native_error);
    EntrySnapshot verified_snapshot;
    std::array<std::byte, 32U> verified_digest{};
    std::size_t ads = 0U;
    if (!verified) {
        failure = {
            StockResearchCopyErrorCode::destination_identity_invalid,
            native_error};
        return false;
    }
    if (!ordinary_file_handle(
            verified.get(), source_entry.snapshot.size, &verified_snapshot) ||
        verified_snapshot.identity != created.identity ||
        verified_snapshot.identity == source_entry.snapshot.identity ||
        !exact_opened_path(verified.get(), destination_file) ||
        !hash_handle(verified.get(), verified_digest) ||
        verified_digest != source_entry.digest ||
        !has_only_default_stream(destination_file, options.limits, ads)) {
        failure = {
            StockResearchCopyErrorCode::destination_identity_invalid,
            ERROR_FILE_INVALID};
        return false;
    }
    output.reset();
    if (options.progress_hook != nullptr) {
        options.progress_hook(
            StockResearchCopyProgressPhase::destination_file_flushed,
            file_ordinal, options.progress_context);
    }
    return true;
}

[[nodiscard]] std::optional<std::string> preparation_manifest(
    const Inventory& source,
    StockResearchMaterialization& materialization) noexcept
{
    try {
        const auto client = std::ranges::find_if(
            source.entries, [](const InventoryEntry& entry) {
                return !entry.directory &&
                       ordinal_equal(entry.relative_path.native(), L"hl.exe");
            });
        const auto server = std::ranges::find_if(
            source.entries, [](const InventoryEntry& entry) {
                return !entry.directory &&
                       ordinal_equal(entry.relative_path.native(), L"hlds.exe");
            });
        if (client == source.entries.end() || server == source.entries.end() ||
            !identity_fingerprint(
                "hlclient.stock-research-root-identity.v2\0",
                source.canonical_root_snapshot,
                materialization.source_root_identity_fingerprint) ||
            !identity_fingerprint(
                "hlclient.stock-research-client-private-identity.v2\0",
                client->snapshot,
                materialization.client_binary_private_identity_reference) ||
            !identity_fingerprint(
                "hlclient.stock-research-server-private-identity.v2\0",
                server->snapshot,
                materialization.server_binary_private_identity_reference)) {
            return std::nullopt;
        }
        std::ostringstream json;
        json << "{\r\n"
             << "  \"schema\":\"" << kStockResearchPreparationSchemaV3
             << "\",\r\n"
             << "  \"marker\":\"" << kStockResearchIsolationMarkerV1
             << "\",\r\n"
             << "  \"preparation_profile\":\""
             << (materialization.approved_external_materialized_link_count ==
                         0U
                     ? "ordinary-or-contained-v3"
                     : "reviewed-external-targets-v1")
             << "\",\r\n"
             << "  \"source_root_identity_fingerprint\":\""
             << materialization.source_root_identity_fingerprint << "\",\r\n"
             << "  \"source_inventory_entries\":"
             << materialization.entry_count
             << ",\r\n"
             << "  \"source_inventory_bytes\":"
             << materialization.byte_count
             << ",\r\n"
             << "  \"source_inventory_sha256\":\""
             << materialization.inventory_sha256 << "\",\r\n"
             << "  \"contained_materialized_link_count\":"
             << materialization.materialized_link_count << ",\r\n"
             << "  \"approved_external_materialized_link_count\":"
             << materialization.approved_external_materialized_link_count
             << ",\r\n"
             << "  \"source_hardlink_count\":"
             << materialization.materialized_hardlink_count << ",\r\n"
             << "  \"destination_entry_count\":"
             << materialization.entry_count << ",\r\n"
             << "  \"destination_byte_count\":"
             << materialization.byte_count << ",\r\n"
             << "  \"destination_inventory_sha256\":\""
             << materialization.inventory_sha256 << "\",\r\n"
             << "  \"destination_reparse_count\":"
             << materialization.destination_reparse_count << ",\r\n"
             << "  \"destination_hardlink_count\":"
             << materialization.destination_hardlink_count << ",\r\n"
             << "  \"destination_ads_count\":"
             << materialization.destination_alternate_data_stream_count
             << ",\r\n"
             << "  \"external_approval_sha256\":\""
             << materialization.external_approval_sha256 << "\",\r\n"
             << "  \"external_classification_summary\":\""
             << materialization.external_classification_summary << "\",\r\n"
             << "  \"executable_target_count\":"
             << materialization.executable_external_target_count << ",\r\n"
             << "  \"mutable_state_target_count\":"
             << materialization.mutable_state_external_target_count
             << ",\r\n"
             << "  \"source_unchanged_status\":\"verified\",\r\n"
             << "  \"external_targets_unchanged_status\":\"verified\",\r\n"
             << "  \"evidence_eligibility\":\""
             << to_string(materialization.evidence_eligibility) << "\",\r\n"
             << "  \"external_target_profile\":\""
             << materialization.external_target_profile << "\",\r\n"
             << "  \"client_binary_private_identity_reference\":\""
             << materialization.client_binary_private_identity_reference
             << "\",\r\n"
             << "  \"server_binary_private_identity_reference\":\""
             << materialization.server_binary_private_identity_reference
             << "\",\r\n"
             << "  \"paths_recorded\":false,\r\n"
             << "  \"preparation_status\":\""
             << materialization.preparation_status << "\"\r\n"
             << "}\r\n";
        return json.str();
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] bool approval_validation_equal(
    const StockExternalApprovalValidation& left,
    const StockExternalApprovalValidation& right) noexcept
{
    if (left.review_set_sha256 != right.review_set_sha256 ||
        left.approval_manifest_sha256 != right.approval_manifest_sha256 ||
        left.executable_count != right.executable_count ||
        left.script_or_command_count != right.script_or_command_count ||
        left.mutable_state_count != right.mutable_state_count ||
        left.nested_link_count != right.nested_link_count ||
        left.expires_at != right.expires_at ||
        left.targets.size() != right.targets.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.targets.size(); ++index) {
        const auto& a = left.targets[index];
        const auto& b = right.targets[index];
        if (!path_equal(
                a.source_link_relative_path, b.source_link_relative_path) ||
            !path_equal(a.target_root, b.target_root) ||
            a.link_identity_sha256 != b.link_identity_sha256 ||
            a.target_identity_sha256 != b.target_identity_sha256 ||
            a.target_inventory_sha256 != b.target_inventory_sha256) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool materialization_leaf_is_unambiguous(
    const std::wstring_view leaf) noexcept
{
    if (!valid_relative_leaf(leaf) || leaf.back() == L'.' ||
        leaf.back() == L' ') {
        return false;
    }
    std::wstring stem{leaf.substr(0U, leaf.find(L'.'))};
    std::ranges::transform(stem, stem.begin(), [](const wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    if (stem == L"con" || stem == L"prn" || stem == L"aux" ||
        stem == L"nul") {
        return false;
    }
    return !(stem.size() == 4U &&
             ((stem.starts_with(L"com") || stem.starts_with(L"lpt")) &&
              stem[3U] >= L'1' && stem[3U] <= L'9'));
}

[[nodiscard]] bool destination_paths_are_unambiguous(
    const Inventory& inventory) noexcept
{
    try {
        std::vector<const InventoryEntry*> ordered;
        ordered.reserve(inventory.entries.size());
        for (const auto& entry : inventory.entries) {
            for (const auto& component : entry.relative_path) {
                if (!materialization_leaf_is_unambiguous(
                        component.native())) {
                    return false;
                }
            }
            ordered.push_back(&entry);
        }
        const auto compare = [](const InventoryEntry* const left,
                                const InventoryEntry* const right) {
            const auto& a = left->relative_path.native();
            const auto& b = right->relative_path.native();
            const int result = ::CompareStringOrdinal(
                a.data(), static_cast<int>(a.size()), b.data(),
                static_cast<int>(b.size()), TRUE);
            if (result != CSTR_EQUAL) return result == CSTR_LESS_THAN;
            return a < b;
        };
        std::ranges::sort(ordered, compare);
        for (std::size_t index = 1U; index < ordered.size(); ++index) {
            if (path_equal(
                    ordered[index - 1U]->relative_path,
                    ordered[index]->relative_path)) {
                return false;
            }
        }
        const std::array<fs::path, 3U> metadata{
            fs::path{L".hlclient-research-preparation.json"},
            fs::path{L".hlclient-research-pending"},
            fs::path{L".hlclient-research-isolated"}};
        return std::ranges::none_of(
            inventory.entries, [&metadata](const InventoryEntry& entry) {
                return std::ranges::any_of(
                    metadata, [&entry](const fs::path& value) {
                        return path_equal(entry.relative_path, value);
                    });
            });
    } catch (...) {
        return false;
    }
}

[[nodiscard]] StockResearchMaterializationResult materialize_impl(
    const fs::path& source_root,
    const fs::path& destination_root,
    const StockResearchCopyOptions& options) noexcept
{
    std::optional<StockExternalApprovalValidation> external_approval;
    UniqueHandle approval_artifact_guard;
    EntrySnapshot approval_artifact_snapshot{};
    std::array<std::byte, 32U> approval_artifact_digest{};
    UniqueHandle source_root_rename_guard;
    std::vector<UniqueHandle> external_target_rename_guards;
    std::vector<ExternalProvenanceGuard> external_provenance_guards;
    const auto external_provenance_unchanged = [&]() noexcept {
        for (const auto& provenance : external_provenance_guards) {
            EntrySnapshot observed;
            std::array<std::byte, 32U> digest{};
            if (!provenance.handle ||
                !query_snapshot(provenance.handle.get(), observed) ||
                observed != provenance.snapshot ||
                !exact_opened_path(
                    provenance.handle.get(), provenance.expected_path) ||
                !handle_has_only_default_stream(provenance.handle.get()) ||
                !hash_handle(provenance.handle.get(), digest) ||
                digest != provenance.digest ||
                !query_snapshot(provenance.handle.get(), observed) ||
                observed != provenance.snapshot) {
                return false;
            }
        }
        return true;
    };
    if (options.external_target_approval_manifest) {
        const auto validation = validate_stock_external_target_approval(
            *options.external_target_approval_manifest, source_root,
            options.limits);
        if (!validation) {
            auto code = StockResearchCopyErrorCode::
                external_target_approval_invalid;
            if (validation.code ==
                StockExternalReviewErrorCode::approval_expired) {
                code = StockResearchCopyErrorCode::
                    external_target_approval_expired;
            } else if (
                validation.code ==
                StockExternalReviewErrorCode::approval_mismatch) {
                code = StockResearchCopyErrorCode::
                    external_target_approval_mismatch;
            } else if (
                validation.code ==
                StockExternalReviewErrorCode::target_ineligible) {
                code = StockResearchCopyErrorCode::
                    external_target_not_evidence_eligible;
            }
            return materialization_failure(code, validation.native_error);
        }
        if (validation.value->targets.empty()) {
            return materialization_failure(
                StockResearchCopyErrorCode::external_target_approval_invalid,
                ERROR_INVALID_DATA);
        }
        external_approval = *validation.value;
        approval_artifact_guard = UniqueHandle{::CreateFileW(
            options.external_target_approval_manifest->c_str(),
            GENERIC_READ | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr)};
        EntrySnapshot approval_after;
        if (!approval_artifact_guard ||
            !query_snapshot(
                approval_artifact_guard.get(), approval_artifact_snapshot) ||
            approval_artifact_snapshot.directory ||
            (approval_artifact_snapshot.attributes &
             FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
            approval_artifact_snapshot.link_count != 1U ||
            !exact_opened_path(
                approval_artifact_guard.get(),
                *options.external_target_approval_manifest) ||
            !handle_has_only_default_stream(approval_artifact_guard.get()) ||
            !hash_handle(
                approval_artifact_guard.get(), approval_artifact_digest) ||
            digest_hex(approval_artifact_digest) !=
                external_approval->approval_manifest_sha256 ||
            !query_snapshot(approval_artifact_guard.get(), approval_after) ||
            approval_after != approval_artifact_snapshot) {
            return materialization_failure(
                StockResearchCopyErrorCode::external_target_approval_mismatch,
                ::GetLastError() == ERROR_SUCCESS ? ERROR_FILE_INVALID
                                                  : ::GetLastError());
        }
        for (const auto& target : external_approval->targets) {
            if (same_or_below(target.target_root, destination_root) ||
                same_or_below(destination_root, target.target_root)) {
                return materialization_failure(
                    StockResearchCopyErrorCode::
                        external_materialization_path_collision,
                    ERROR_ACCESS_DENIED);
            }
        }
    }
    auto source_result = build_inventory(
        source_root, options.limits, true, false,
        external_approval ? &*external_approval : nullptr);
    if (!source_result.inventory) {
        const auto code =
            external_approval &&
                    source_result.code == StockResearchCopyErrorCode::
                                              source_changed_during_materialization
                ? StockResearchCopyErrorCode::
                      source_or_external_target_changed_during_materialization
                : source_result.code;
        return materialization_failure(
            code, source_result.native_error);
    }
    Inventory source = std::move(*source_result.inventory);
    const auto source_mutation_code =
        external_approval
            ? StockResearchCopyErrorCode::
                  source_or_external_target_changed_during_materialization
            : StockResearchCopyErrorCode::
                  source_changed_during_materialization;
    const auto approval_artifact_unchanged = [&]() noexcept {
        if (!external_approval) return true;
        EntrySnapshot before;
        EntrySnapshot after;
        std::array<std::byte, 32U> digest{};
        return approval_artifact_guard &&
               query_snapshot(approval_artifact_guard.get(), before) &&
               before == approval_artifact_snapshot &&
               exact_opened_path(
                   approval_artifact_guard.get(),
                   *options.external_target_approval_manifest) &&
               handle_has_only_default_stream(approval_artifact_guard.get()) &&
               hash_handle(approval_artifact_guard.get(), digest) &&
               digest == approval_artifact_digest &&
               query_snapshot(approval_artifact_guard.get(), after) &&
               after == before;
    };
    if (external_approval &&
        (source.summary.reviewed_external_target_count !=
             external_approval->targets.size() ||
         source.summary.escaped_target_count !=
             source.summary.reviewed_external_target_count)) {
        return materialization_failure(
            StockResearchCopyErrorCode::external_target_approval_mismatch,
            ERROR_INVALID_DATA);
    }
    if (external_approval) {
        const auto refreshed = validate_stock_external_target_approval(
            *options.external_target_approval_manifest, source_root,
            options.limits);
        if (!refreshed) {
            auto code =
                StockResearchCopyErrorCode::external_target_approval_mismatch;
            if (refreshed.code ==
                StockExternalReviewErrorCode::approval_expired) {
                code = StockResearchCopyErrorCode::
                    external_target_approval_expired;
            } else if (
                refreshed.code ==
                StockExternalReviewErrorCode::target_ineligible) {
                code = StockResearchCopyErrorCode::
                    external_target_not_evidence_eligible;
            }
            return materialization_failure(code, refreshed.native_error);
        }
        if (!approval_validation_equal(*external_approval, *refreshed.value)) {
            return materialization_failure(
                StockResearchCopyErrorCode::external_target_approval_mismatch,
                ERROR_FILE_INVALID);
        }
        external_approval = *refreshed.value;
    }

    source_root_rename_guard =
        open_entry_rename_guard(source.canonical_root);
    EntrySnapshot guarded_source_snapshot;
    PhysicalLocation guarded_source_location;
    const PhysicalLocation inventoried_source_location{
        source.canonical_root_snapshot.identity.volume_serial,
        source.canonical_root_volume_relative_path};
    if (!source_root_rename_guard ||
        !query_snapshot(
            source_root_rename_guard.get(), guarded_source_snapshot) ||
        guarded_source_snapshot != source.canonical_root_snapshot ||
        !guarded_source_snapshot.directory ||
        (guarded_source_snapshot.attributes & FILE_ATTRIBUTE_REPARSE_POINT) !=
            0U ||
        !exact_opened_path(
            source_root_rename_guard.get(), source.canonical_root) ||
        !query_physical_location(
            source_root_rename_guard.get(), guarded_source_snapshot,
            guarded_source_location) ||
        !physical_equal(guarded_source_location, inventoried_source_location)) {
        return materialization_failure(
            source_mutation_code,
            ::GetLastError() == ERROR_SUCCESS ? ERROR_FILE_INVALID
                                              : ::GetLastError());
    }
    if (external_approval) {
        try {
            external_target_rename_guards.reserve(
                external_approval->targets.size());
        } catch (...) {
            return materialization_failure(
                StockResearchCopyErrorCode::
                    source_or_external_target_changed_during_materialization,
                ERROR_NOT_ENOUGH_MEMORY);
        }
        for (const auto& target : external_approval->targets) {
            auto guard = open_entry_rename_guard(target.target_root);
            EntrySnapshot guarded_target_snapshot;
            if (!guard ||
                !query_snapshot(guard.get(), guarded_target_snapshot) ||
                (guarded_target_snapshot.attributes &
                 FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
                !exact_opened_path(guard.get(), target.target_root)) {
                return materialization_failure(
                    StockResearchCopyErrorCode::
                        source_or_external_target_changed_during_materialization,
                    ::GetLastError() == ERROR_SUCCESS ? ERROR_FILE_INVALID
                                                      : ::GetLastError());
            }
            external_target_rename_guards.push_back(std::move(guard));
        }
        std::vector<std::pair<fs::path, std::uint64_t>> provenance_paths;
        try {
            const auto add_path = [&](const fs::path& path,
                                      const std::uint64_t maximum_bytes) {
                const auto existing = std::ranges::find_if(
                    provenance_paths, [&](const auto& candidate) {
                        return path_equal(candidate.first, path);
                    });
                if (existing != provenance_paths.end()) {
                    return existing->second == maximum_bytes;
                }
                provenance_paths.emplace_back(path, maximum_bytes);
                return true;
            };
            const auto add_appmanifest = [&](const fs::path& member) {
                const auto manifest = half_life_appmanifest_path(member);
                if (!manifest) return false;
                return add_path(*manifest, 64U * 1'024U);
            };
            const auto libraryfolders =
                half_life_libraryfolders_path(source.canonical_root);
            if (!libraryfolders ||
                !add_path(*libraryfolders, 1U * 1'024U * 1'024U) ||
                !add_appmanifest(source.canonical_root)) {
                return materialization_failure(
                    StockResearchCopyErrorCode::
                        external_target_not_evidence_eligible,
                    ERROR_INVALID_DATA);
            }
            for (const auto& target : external_approval->targets) {
                if (!add_appmanifest(target.target_root)) {
                    return materialization_failure(
                        StockResearchCopyErrorCode::
                            external_target_not_evidence_eligible,
                        ERROR_INVALID_DATA);
                }
            }
            external_provenance_guards.reserve(provenance_paths.size());
        } catch (...) {
            return materialization_failure(
                StockResearchCopyErrorCode::
                    source_or_external_target_changed_during_materialization,
                ERROR_NOT_ENOUGH_MEMORY);
        }
        for (const auto& [provenance_path, maximum_bytes] : provenance_paths) {
            UniqueHandle guard{::CreateFileW(
                provenance_path.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
                nullptr)};
            EntrySnapshot provenance_snapshot;
            EntrySnapshot provenance_after;
            std::array<std::byte, 32U> provenance_digest{};
            if (!guard ||
                !query_snapshot(guard.get(), provenance_snapshot) ||
                provenance_snapshot.directory ||
                (provenance_snapshot.attributes &
                 FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
                provenance_snapshot.link_count != 1U ||
                provenance_snapshot.size == 0U ||
                provenance_snapshot.size > maximum_bytes ||
                !exact_opened_path(guard.get(), provenance_path) ||
                !handle_has_only_default_stream(guard.get()) ||
                !hash_handle(guard.get(), provenance_digest) ||
                !query_snapshot(guard.get(), provenance_after) ||
                provenance_after != provenance_snapshot) {
                return materialization_failure(
                    StockResearchCopyErrorCode::
                        source_or_external_target_changed_during_materialization,
                    ::GetLastError() == ERROR_SUCCESS ? ERROR_FILE_INVALID
                                                      : ::GetLastError());
            }
            external_provenance_guards.push_back(ExternalProvenanceGuard{
                provenance_path, provenance_snapshot, provenance_digest,
                std::move(guard)});
        }
        const auto guarded_validation =
            validate_stock_external_target_approval(
                *options.external_target_approval_manifest, source_root,
                options.limits);
        if (!guarded_validation) {
            auto code = StockResearchCopyErrorCode::
                source_or_external_target_changed_during_materialization;
            if (guarded_validation.code ==
                StockExternalReviewErrorCode::approval_expired) {
                code = StockResearchCopyErrorCode::
                    external_target_approval_expired;
            } else if (guarded_validation.code ==
                       StockExternalReviewErrorCode::target_ineligible) {
                code = StockResearchCopyErrorCode::
                    external_target_not_evidence_eligible;
            }
            return materialization_failure(
                code, guarded_validation.native_error);
        }
        if (!approval_validation_equal(
                *external_approval, *guarded_validation.value)) {
            return materialization_failure(
                StockResearchCopyErrorCode::
                    source_or_external_target_changed_during_materialization,
                ERROR_FILE_INVALID);
        }
        external_approval = *guarded_validation.value;
        if (!external_provenance_unchanged()) {
            return materialization_failure(
                StockResearchCopyErrorCode::
                    source_or_external_target_changed_during_materialization,
                ERROR_FILE_INVALID);
        }
    }
    if (!destination_paths_are_unambiguous(source)) {
        return materialization_failure(
            external_approval
                ? StockResearchCopyErrorCode::
                      external_materialization_path_collision
                : StockResearchCopyErrorCode::destination_create_failed,
            ERROR_DUP_NAME);
    }
    if (!source.summary.safe_to_materialize) {
        return materialization_failure(
            StockResearchCopyErrorCode::source_topology_unsafe,
            ERROR_ACCESS_DENIED);
    }

    MaterializeFailure failure;
    const auto destination = prepare_destination_parent(
        destination_root, source, options,
        external_approval ? &*external_approval : nullptr, failure);
    if (!destination) {
        return materialization_failure(failure.code, failure.native_error);
    }
    if (!parent_still_exact(*destination)) {
        return materialization_failure(
            StockResearchCopyErrorCode::destination_parent_invalid,
            ERROR_FILE_INVALID);
    }

    const auto staging_leaf = random_leaf(L".hlclient-stock-research-copy-");
    if (!staging_leaf) {
        return materialization_failure(
            StockResearchCopyErrorCode::destination_create_failed,
            ERROR_NOT_ENOUGH_MEMORY);
    }
    fs::path staging;
    try {
        staging = destination->parent / *staging_leaf;
    } catch (...) {
        return materialization_failure(
            StockResearchCopyErrorCode::destination_create_failed,
            ERROR_NOT_ENOUGH_MEMORY);
    }
    DWORD staging_create_error = ERROR_SUCCESS;
    auto staging_handle = create_relative_directory_locked(
        destination->parent_handle(), *staging_leaf, false,
        staging_create_error);
    if (!staging_handle) {
        return materialization_failure(
            StockResearchCopyErrorCode::destination_create_failed,
            staging_create_error);
    }

    std::optional<FileIdentity> staging_identity;
    std::vector<OwnedEntryWitness> owned_entries;
    std::vector<MaterializedDirectoryPin> materialized_directories;
    PreparedCommitMarker commit_candidate;
    const auto release_descendant_pins =
        [&materialized_directories]() noexcept {
            for (auto& directory : materialized_directories) {
                directory.handle.reset();
            }
        };
    const auto release_staging_pins =
        [&staging_handle, &release_descendant_pins]() noexcept {
            release_descendant_pins();
            staging_handle.reset();
        };
    const auto fail_and_clean =
        [&staging, &staging_handle, &staging_identity, &owned_entries,
         &release_descendant_pins, &commit_candidate, &destination,
         &staging_leaf](
            const StockResearchCopyErrorCode code,
            const DWORD native_error) noexcept {
        // While staging is a RootDirectory for child renames, its retained
        // no-delete-share handle deliberately does not request DELETE. Older
        // Windows opens the rename target directory with read/write sharing
        // only, which conflicts with a pre-existing DELETE-capable handle.
        // Acquire DELETE only through a witnessed, identity-checked transition
        // immediately before private-tree removal.
        commit_candidate.handle.reset();
        release_descendant_pins();
        EntrySnapshot retained_snapshot;
        DWORD cleanup_error = ERROR_SUCCESS;
        PublishedTreeChangeWitness cleanup_witness;
        if (!staging_identity || !staging_handle ||
            !ordinary_directory_handle(
                staging_handle.get(), &retained_snapshot) ||
            retained_snapshot.identity != *staging_identity ||
            !exact_opened_path(staging_handle.get(), staging) ||
            !cleanup_witness.begin(
                destination->parent,
                destination->parent_snapshot.identity, cleanup_error, false,
                true) ||
            !cleanup_witness.unchanged_now(cleanup_error)) {
            if (cleanup_error == ERROR_SUCCESS) {
                cleanup_error = ERROR_FILE_INVALID;
            }
            cleanup_witness.abandon();
            staging_handle.reset();
            return materialization_failure(
                StockResearchCopyErrorCode::cleanup_failed, cleanup_error);
        }

        staging_handle.reset();
        constexpr std::size_t cleanup_attempts = 101U;
        for (std::size_t attempt = 0U;
             attempt < cleanup_attempts && !staging_handle; ++attempt) {
            staging_handle = open_relative_directory_for_publish(
                destination->parent_handle(), *staging_leaf, true, true,
                cleanup_error);
            if (staging_handle || cleanup_error != ERROR_SHARING_VIOLATION ||
                attempt + 1U == cleanup_attempts) {
                break;
            }
            if (!cleanup_witness.unchanged_now(cleanup_error)) break;
            ::Sleep(10U);
        }
        if (!staging_handle ||
            !ordinary_directory_handle(
                staging_handle.get(), &retained_snapshot) ||
            retained_snapshot.identity != *staging_identity ||
            !exact_opened_path(staging_handle.get(), staging) ||
            !parent_still_exact(*destination) ||
            !cleanup_witness.unchanged_now(cleanup_error)) {
            if (cleanup_error == ERROR_SUCCESS) {
                cleanup_error = ERROR_FILE_INVALID;
            }
            cleanup_witness.abandon();
            staging_handle.reset();
            return materialization_failure(
                StockResearchCopyErrorCode::cleanup_failed, cleanup_error);
        }
        cleanup_witness.abandon();

        if (!safe_remove_owned_tree_with_handle(
                staging, *staging_identity, owned_entries,
                staging_handle.get())) {
            cleanup_error = ::GetLastError();
            staging_handle.reset();
            return materialization_failure(
                StockResearchCopyErrorCode::cleanup_failed, cleanup_error);
        }
        staging_handle.reset();
        return materialization_failure(code, native_error);
    };

    EntrySnapshot initial_staging_snapshot;
    if (!ordinary_directory_handle(
            staging_handle.get(), &initial_staging_snapshot) ||
        !exact_opened_path(staging_handle.get(), staging)) {
        const DWORD identity_error = ::GetLastError();
        // Without a usable FileId, closing the no-delete-share construction
        // handle is safer than reopening and deleting whatever now occupies
        // the random staging leaf.
        staging_handle.reset();
        return materialization_failure(
            StockResearchCopyErrorCode::cleanup_failed,
            identity_error == ERROR_SUCCESS ? ERROR_INVALID_HANDLE
                                            : identity_error);
    }
    staging_identity = initial_staging_snapshot.identity;
    try {
        owned_entries.reserve(source.entries.size() + 4U);
        materialized_directories.reserve(source.entries.size());
    } catch (...) {
        return fail_and_clean(
            StockResearchCopyErrorCode::destination_create_failed,
            ERROR_NOT_ENOUGH_MEMORY);
    }
    if (options.progress_hook != nullptr) {
        options.progress_hook(
            StockResearchCopyProgressPhase::after_staging_identity_acquired,
            0U, options.progress_context);
    }

    std::vector<const InventoryEntry*> directories;
    try {
        for (const auto& entry : source.entries) {
            if (entry.directory) directories.push_back(&entry);
        }
        std::ranges::sort(
            directories,
            [](const InventoryEntry* const left,
               const InventoryEntry* const right) {
                const auto left_depth = static_cast<std::size_t>(std::distance(
                    left->relative_path.begin(), left->relative_path.end()));
                const auto right_depth = static_cast<std::size_t>(std::distance(
                    right->relative_path.begin(), right->relative_path.end()));
                return left_depth != right_depth
                           ? left_depth < right_depth
                           : left->relative_path.generic_wstring() <
                                 right->relative_path.generic_wstring();
            });
    } catch (...) {
        return fail_and_clean(
            StockResearchCopyErrorCode::destination_create_failed,
            ERROR_NOT_ENOUGH_MEMORY);
    }
    for (const auto* const entry : directories) {
        DWORD witness_error = ERROR_SUCCESS;
        if (!validate_witnesses(entry->witnesses, witness_error)) {
            return fail_and_clean(
                source_mutation_code, witness_error);
        }
        fs::path directory;
        try {
            directory = staging / entry->relative_path;
        } catch (...) {
            return fail_and_clean(
                StockResearchCopyErrorCode::destination_create_failed,
                ERROR_NOT_ENOUGH_MEMORY);
        }
        const HANDLE parent_handle = materialized_directory_handle(
            staging_handle.get(), materialized_directories,
            entry->relative_path.parent_path());
        if (parent_handle == INVALID_HANDLE_VALUE) {
            return fail_and_clean(
                StockResearchCopyErrorCode::destination_identity_invalid,
                ERROR_PATH_NOT_FOUND);
        }
        DWORD directory_create_error = ERROR_SUCCESS;
        auto handle = create_relative_directory_locked(
            parent_handle, entry->relative_path.filename().native(), false,
            directory_create_error);
        EntrySnapshot directory_snapshot;
        if (!handle ||
            !ordinary_directory_handle(handle.get(), &directory_snapshot) ||
            !exact_opened_path(handle.get(), directory)) {
            const DWORD identity_error = ::GetLastError();
            return fail_and_clean(
                handle
                    ? StockResearchCopyErrorCode::destination_identity_invalid
                    : StockResearchCopyErrorCode::destination_create_failed,
                handle ? (identity_error == ERROR_SUCCESS ? ERROR_DIRECTORY
                                                          : identity_error)
                       : directory_create_error);
        }
        owned_entries.push_back(OwnedEntryWitness{
            &entry->relative_path, directory_snapshot.identity, true});
        materialized_directories.push_back(MaterializedDirectoryPin{
            &entry->relative_path, directory_snapshot, std::move(handle)});
    }

    std::size_t file_ordinal = 0U;
    for (const auto& entry : source.entries) {
        if (entry.directory) continue;
        fs::path output;
        try {
            output = staging / entry.relative_path;
        } catch (...) {
            return fail_and_clean(
                StockResearchCopyErrorCode::destination_create_failed,
                ERROR_NOT_ENOUGH_MEMORY);
        }
        MaterializeFailure copy_failure;
        std::optional<FileIdentity> copied_identity;
        const HANDLE output_parent = materialized_directory_handle(
            staging_handle.get(), materialized_directories,
            entry.relative_path.parent_path());
        if (output_parent == INVALID_HANDLE_VALUE) {
            return fail_and_clean(
                StockResearchCopyErrorCode::destination_identity_invalid,
                ERROR_PATH_NOT_FOUND);
        }
        const bool copied = copy_file_verified(
                entry, output_parent, output.parent_path(), output, options,
                file_ordinal, copied_identity, copy_failure);
        if (copied_identity) {
            owned_entries.push_back(OwnedEntryWitness{
                &entry.relative_path, *copied_identity, false});
        }
        if (!copied) {
            const auto code =
                external_approval &&
                        copy_failure.code == StockResearchCopyErrorCode::
                                                 source_changed_during_materialization
                    ? source_mutation_code
                    : copy_failure.code;
            return fail_and_clean(
                code, copy_failure.native_error);
        }
        ++file_ordinal;
    }

    auto destination_inventory_result =
        build_inventory(staging, options.limits, true);
    if (!destination_inventory_result.inventory) {
        return fail_and_clean(
            StockResearchCopyErrorCode::destination_inventory_mismatch,
            destination_inventory_result.native_error);
    }
    const auto& destination_inventory =
        *destination_inventory_result.inventory;
    if (!destination_inventory.summary.safe_to_materialize ||
        destination_inventory.summary.internal_reparse_count != 0U ||
        destination_inventory.summary.hardlink_count != 0U ||
        destination_inventory.summary.alternate_data_stream_count != 0U ||
        destination_inventory.summary.entry_count !=
            source.summary.entry_count ||
        destination_inventory.summary.byte_count != source.summary.byte_count ||
        destination_inventory.inventory_digest != source.inventory_digest) {
        return fail_and_clean(
            StockResearchCopyErrorCode::destination_inventory_mismatch,
            ERROR_INVALID_DATA);
    }

    if (options.progress_hook != nullptr) {
        options.progress_hook(
            StockResearchCopyProgressPhase::before_source_reinventory,
            file_ordinal, options.progress_context);
    }
    std::vector<SourceCommitGuard> source_commit_guards;
    DWORD source_change_error = ERROR_SUCCESS;
    const auto retain_source_entry =
        [&](const fs::path& path,
            const EntrySnapshot& expected,
            const bool require_default_stream,
            const bool metadata_only) {
            const DWORD access = metadata_only
                                     ? FILE_READ_ATTRIBUTES
                                     : (expected.directory
                                            ? FILE_LIST_DIRECTORY |
                                                  FILE_READ_ATTRIBUTES
                                            : GENERIC_READ |
                                                  FILE_READ_ATTRIBUTES);
            UniqueHandle handle{::CreateFileW(
                path.c_str(), access, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT |
                    (expected.directory ? FILE_FLAG_BACKUP_SEMANTICS :
                                          FILE_FLAG_SEQUENTIAL_SCAN),
                nullptr)};
            EntrySnapshot observed;
            if (!handle || !query_snapshot(handle.get(), observed) ||
                observed != expected || !exact_opened_path(handle.get(), path) ||
                (require_default_stream &&
                 !handle_has_only_default_stream(handle.get()))) {
                source_change_error = ::GetLastError();
                if (source_change_error == ERROR_SUCCESS) {
                    source_change_error = ERROR_FILE_INVALID;
                }
                return false;
            }
            try {
                source_commit_guards.push_back(SourceCommitGuard{
                    path, observed, require_default_stream, std::move(handle)});
            } catch (...) {
                source_change_error = ERROR_NOT_ENOUGH_MEMORY;
                return false;
            }
            return true;
        };
    try {
        source_commit_guards.reserve(source.entries.size() * 2U + 1U);
    } catch (...) {
        return fail_and_clean(
            source_mutation_code, ERROR_NOT_ENOUGH_MEMORY);
    }
    bool source_retained = retain_source_entry(
        source.canonical_root, source.canonical_root_snapshot, true, false);
    for (const auto& entry : source.entries) {
        if (!source_retained) break;
        source_retained = retain_source_entry(
            entry.physical_path, entry.snapshot, true, false);
        for (const auto& witness : entry.witnesses) {
            if (!source_retained) break;
            source_retained = retain_source_entry(
                witness.logical_path, witness.link_snapshot, false, true);
        }
    }
    if (!source_retained) {
        return fail_and_clean(
            source_mutation_code,
            source_change_error == ERROR_SUCCESS ? ERROR_FILE_INVALID
                                                 : source_change_error);
    }
    const auto source_commit_unchanged = [&]() noexcept {
        for (const auto& guard : source_commit_guards) {
            EntrySnapshot observed;
            if (!guard.handle ||
                !query_snapshot(guard.handle.get(), observed) ||
                observed != guard.snapshot ||
                !exact_opened_path(guard.handle.get(), guard.expected_path) ||
                (guard.require_default_stream &&
                 !handle_has_only_default_stream(guard.handle.get()))) {
                source_change_error = ::GetLastError();
                if (source_change_error == ERROR_SUCCESS) {
                    source_change_error = ERROR_FILE_INVALID;
                }
                return false;
            }
        }
        return true;
    };
    auto source_after_result = build_inventory(
        source_root, options.limits, true, false,
        external_approval ? &*external_approval : nullptr);
    if (!source_after_result.inventory ||
        !inventory_equal(source, *source_after_result.inventory) ||
        !source_commit_unchanged()) {
        return fail_and_clean(
            source_mutation_code,
            source_change_error != ERROR_SUCCESS
                ? source_change_error
                : (source_after_result.native_error == ERROR_SUCCESS
                       ? ERROR_FILE_INVALID
                       : source_after_result.native_error));
    }
    if (external_approval) {
        const auto refreshed = validate_stock_external_target_approval(
            *options.external_target_approval_manifest, source_root,
            options.limits);
        if (!refreshed ||
            !approval_validation_equal(*external_approval, *refreshed.value) ||
            !approval_artifact_unchanged()) {
            const auto code =
                !refreshed && refreshed.code ==
                                  StockExternalReviewErrorCode::approval_expired
                    ? StockResearchCopyErrorCode::
                          external_target_approval_expired
                    : StockResearchCopyErrorCode::
                          external_target_approval_mismatch;
            return fail_and_clean(
                code,
                refreshed.native_error == ERROR_SUCCESS ? ERROR_FILE_INVALID
                                                        : refreshed.native_error);
        }
        external_approval = *refreshed.value;
    }

    StockResearchMaterialization materialization;
    materialization.topology = source.summary;
    materialization.materialized_link_count =
        source.summary.contained_target_count +
        (source.summary.root_reparse ? 1U : 0U);
    materialization.materialized_hardlink_count =
        source.summary.hardlink_count;
    materialization.approved_external_materialized_link_count =
        source.summary.reviewed_external_target_count;
    materialization.rejected_link_count = 0U;
    materialization.destination_reparse_count = 0U;
    materialization.destination_hardlink_count = 0U;
    materialization.destination_alternate_data_stream_count = 0U;
    materialization.entry_count = source.summary.entry_count;
    materialization.byte_count = source.summary.byte_count;
    materialization.source_unchanged = true;
    materialization.external_targets_unchanged = true;
    materialization.destination_unlinked = true;
    materialization.inventory_sha256 = digest_hex(source.inventory_digest);
    materialization.external_approval_sha256 =
        external_approval ? external_approval->approval_manifest_sha256
                          : std::string(64U, '0');
    materialization.external_classification_summary =
        external_approval ? "eligible_non_executable_asset_tree" : "none";
    materialization.executable_external_target_count =
        external_approval ? external_approval->executable_count : 0U;
    materialization.mutable_state_external_target_count =
        external_approval ? external_approval->mutable_state_count : 0U;
    materialization.evidence_eligibility =
        StockResearchCopyEvidenceEligibility::eligible;
    materialization.external_target_profile =
        external_approval ? "reviewed-non-executable-v1" : "none";
    materialization.preparation_status = external_approval
                                             ? "exact-reviewed-materialized-copy-verified"
                                             : "exact-materialized-copy-verified";
    const auto manifest = preparation_manifest(source, materialization);
    if (!manifest) {
        return fail_and_clean(
            StockResearchCopyErrorCode::manifest_write_failed,
            ERROR_INVALID_DATA);
    }
    const auto manifest_bytes = std::as_bytes(
        std::span{manifest->data(), manifest->size()});
    const auto marker_bytes = std::as_bytes(std::span{
        kStockResearchIsolationMarkerV1.data(),
        kStockResearchIsolationMarkerV1.size()});
    const auto pending_bytes = std::as_bytes(std::span{
        kPendingPreparationMetadata.data(),
        kPendingPreparationMetadata.size()});
    const fs::path manifest_relative{
        L".hlclient-research-preparation.json"};
    const fs::path marker_relative{L".hlclient-research-isolated"};
    const fs::path pending_relative{L".hlclient-research-pending"};
    std::optional<FileIdentity> manifest_identity;
    const bool manifest_written = atomic_write_small(
        staging_handle.get(), staging, staging / manifest_relative,
        manifest_bytes,
        manifest_identity, failure);
    if (manifest_identity) {
        owned_entries.push_back(OwnedEntryWitness{
            &manifest_relative, *manifest_identity, false});
    }
    if (!manifest_written) {
        return fail_and_clean(
            failure.code == StockResearchCopyErrorCode::none
                ? StockResearchCopyErrorCode::manifest_write_failed
                : failure.code,
            failure.native_error);
    }
    // The isolation marker is the authorization/commit record consumed by
    // active-capture preflight.  Never place it in staging: a verification
    // failure whose public root cannot be quarantined must remain visibly and
    // durably uncommitted.  The typed pending record is metadata-only and is
    // retained after success; consumers still require the exact isolation
    // marker and exclude both records from the source inventory digest.
    std::optional<FileIdentity> pending_identity;
    const bool pending_written = atomic_write_small(
        staging_handle.get(), staging, staging / pending_relative,
        pending_bytes, pending_identity, failure);
    if (pending_identity) {
        owned_entries.push_back(OwnedEntryWitness{
            &pending_relative, *pending_identity, false});
    }
    if (!pending_written) {
        return fail_and_clean(
            failure.code == StockResearchCopyErrorCode::none
                ? StockResearchCopyErrorCode::manifest_write_failed
                : failure.code,
            failure.native_error);
    }

    MaterializeFailure commit_prepare_failure;
    if (!prepare_commit_marker(
            staging_handle.get(), staging, marker_bytes, commit_candidate,
            commit_prepare_failure)) {
        return fail_and_clean(
            commit_prepare_failure.code == StockResearchCopyErrorCode::none
                ? StockResearchCopyErrorCode::manifest_write_failed
                : commit_prepare_failure.code,
            commit_prepare_failure.native_error);
    }
    owned_entries.push_back(OwnedEntryWitness{
        &commit_candidate.relative_path, commit_candidate.snapshot.identity,
        false});

    EntrySnapshot staging_snapshot;
    if (!ordinary_directory_handle(staging_handle.get(), &staging_snapshot) ||
        !exact_opened_path(staging_handle.get(), staging) ||
        !parent_still_exact(*destination)) {
        return fail_and_clean(
            StockResearchCopyErrorCode::destination_identity_invalid,
            ERROR_FILE_INVALID);
    }
    const auto quarantine_leaf =
        random_leaf(L".hlclient-stock-research-quarantine-");
    if (!quarantine_leaf) {
        return fail_and_clean(
            StockResearchCopyErrorCode::destination_publish_failed,
            ERROR_NOT_ENOUGH_MEMORY);
    }
    fs::path quarantine;
    try {
        quarantine = destination->parent / *quarantine_leaf;
    } catch (...) {
        return fail_and_clean(
            StockResearchCopyErrorCode::destination_publish_failed,
            ERROR_NOT_ENOUGH_MEMORY);
    }
    if (::GetFileAttributesW(destination->destination.c_str()) !=
        INVALID_FILE_ATTRIBUTES) {
        return fail_and_clean(
            StockResearchCopyErrorCode::destination_exists,
            ERROR_ALREADY_EXISTS);
    }

    constexpr std::size_t kPublishTransitionAttempts = 101U;
    constexpr DWORD kPublishTransitionRetryMilliseconds = 10U;
    bool retained_root_delete_capable = false;
    fs::path cleanup_failure_relative;
    bool cleanup_failure_owned = false;

    const auto quarantine_retained_tree =
        [&destination, &staging_handle, &release_descendant_pins, &quarantine,
         &quarantine_leaf](DWORD& quarantine_error) noexcept {
        // Always move the retained object by handle. The leaf which formerly
        // named it may already contain an unrelated replacement and must never
        // be traversed or removed by a failure path.
        release_descendant_pins();
        if (!parent_still_exact(*destination) ||
            !rename_open_file_without_replace(
                staging_handle.get(), destination->parent_handle(),
                *quarantine_leaf) ||
            !exact_opened_path(staging_handle.get(), quarantine)) {
            quarantine_error = ::GetLastError();
            if (quarantine_error == ERROR_SUCCESS) {
                quarantine_error = ERROR_FILE_INVALID;
            }
            return false;
        }
        quarantine_error = ERROR_SUCCESS;
        return true;
    };
    const auto finish_quarantined_failure =
        [&staging_handle, &staging_snapshot, &owned_entries,
         &release_staging_pins, &quarantine, &cleanup_failure_relative,
         &cleanup_failure_owned](
            const StockResearchCopyErrorCode code,
            const DWORD native_error) noexcept {
        bool cleanup_marker_removed = false;
        if (!safe_remove_owned_tree_with_handle(
                quarantine, staging_snapshot.identity, owned_entries,
                staging_handle.get(),
                cleanup_failure_owned ? &cleanup_failure_relative : nullptr,
                &cleanup_marker_removed)) {
            const DWORD cleanup_error = ::GetLastError();
            DWORD restoration_error = ERROR_SUCCESS;
            if (cleanup_failure_owned && cleanup_marker_removed) {
                std::optional<FileIdentity> restored_identity;
                if (!write_cleanup_failure_metadata_direct(
                        staging_handle.get(), quarantine, restoration_error,
                        &cleanup_failure_relative, &restored_identity) ||
                    !restored_identity) {
                    if (restoration_error == ERROR_SUCCESS) {
                        restoration_error = ERROR_FILE_INVALID;
                    }
                } else {
                    restoration_error = ERROR_SUCCESS;
                }
            }
            release_staging_pins();
            return materialization_failure(
                StockResearchCopyErrorCode::cleanup_failed,
                restoration_error == ERROR_SUCCESS ? cleanup_error
                                                   : restoration_error);
        }
        release_staging_pins();
        return materialization_failure(code, native_error);
    };
    const auto fail_retained_unpublished =
        [&quarantine_retained_tree, &finish_quarantined_failure,
         &release_staging_pins](
            const StockResearchCopyErrorCode code,
            const DWORD native_error) noexcept {
        DWORD quarantine_error = ERROR_SUCCESS;
        if (!quarantine_retained_tree(quarantine_error)) {
            // The exact retained object could not be moved to our private
            // quarantine name. Leave every pathname untouched rather than
            // risk deleting a substituted staging leaf.
            release_staging_pins();
            return materialization_failure(
                StockResearchCopyErrorCode::cleanup_failed,
                quarantine_error == ERROR_SUCCESS ? native_error
                                                   : quarantine_error);
        }
        return finish_quarantined_failure(code, native_error);
    };
    const auto fail_published =
        [&quarantine_retained_tree, &finish_quarantined_failure,
         &release_descendant_pins, &release_staging_pins, &staging_handle,
         &destination,
         &commit_candidate, &staging_snapshot, &retained_root_delete_capable,
         &quarantine_leaf, &cleanup_failure_relative,
         &cleanup_failure_owned, &owned_entries, kPublishTransitionAttempts,
         kPublishTransitionRetryMilliseconds](
            const StockResearchCopyErrorCode code,
            const DWORD native_error) noexcept {
        commit_candidate.handle.reset();
        release_descendant_pins();

        // Publish the cleanup diagnostic while an identity-bound root handle
        // remains available. A non-DELETE root uses atomic child publication;
        // a DELETE-capable transition root uses the verified failure-only
        // direct form because older Windows can reject RootDirectory renames
        // through that handle. If quarantine later fails, the pending tree
        // retains this record; if cleanup succeeds, its identity is part of
        // the owned ledger and is removed with the tree.
        if (!cleanup_failure_owned) {
            DWORD metadata_error = ERROR_SUCCESS;
            std::optional<FileIdentity> metadata_identity;
            const bool metadata_written = retained_root_delete_capable
                ? write_cleanup_failure_metadata_direct(
                      staging_handle.get(), destination->destination,
                      metadata_error, &cleanup_failure_relative,
                      &metadata_identity)
                : write_cleanup_failure_metadata(
                      staging_handle.get(), destination->destination,
                      metadata_error, &cleanup_failure_relative,
                      &metadata_identity);
            if (metadata_written &&
                metadata_identity) {
                try {
                    owned_entries.push_back(OwnedEntryWitness{
                        &cleanup_failure_relative, *metadata_identity, false});
                    cleanup_failure_owned = true;
                } catch (...) {
                    release_staging_pins();
                    return materialization_failure(
                        StockResearchCopyErrorCode::cleanup_failed,
                        ERROR_NOT_ENOUGH_MEMORY);
                }
            }
        }

        PublishedTreeChangeWitness quarantine_witness;
        DWORD quarantine_error = ERROR_SUCCESS;
        EntrySnapshot retained_snapshot;
        if (!quarantine_witness.begin(
                destination->parent,
                destination->parent_snapshot.identity, quarantine_error,
                false, true) ||
            !staging_handle ||
            !ordinary_directory_handle(
                staging_handle.get(), &retained_snapshot) ||
            retained_snapshot.identity != staging_snapshot.identity ||
            !exact_opened_path(
                staging_handle.get(), destination->destination) ||
            !parent_still_exact(*destination)) {
            if (quarantine_error == ERROR_SUCCESS) {
                quarantine_error = ERROR_FILE_INVALID;
            }
            quarantine_witness.abandon();
            release_staging_pins();
            return materialization_failure(
                StockResearchCopyErrorCode::cleanup_failed,
                quarantine_error);
        }

        if (!retained_root_delete_capable) {
            staging_handle.reset();
            for (std::size_t attempt = 0U;
                 attempt < kPublishTransitionAttempts && !staging_handle;
                 ++attempt) {
                staging_handle = open_relative_directory_for_publish(
                    destination->parent_handle(),
                    destination->destination.filename().native(), true, true,
                    quarantine_error);
                if (staging_handle ||
                    quarantine_error != ERROR_SHARING_VIOLATION ||
                    attempt + 1U == kPublishTransitionAttempts) {
                    break;
                }
                if (!quarantine_witness.unchanged_now(quarantine_error)) break;
                ::Sleep(kPublishTransitionRetryMilliseconds);
            }
            if (!staging_handle ||
                !ordinary_directory_handle(
                    staging_handle.get(), &retained_snapshot) ||
                retained_snapshot.identity != staging_snapshot.identity ||
                !exact_opened_path(
                    staging_handle.get(), destination->destination) ||
                !parent_still_exact(*destination) ||
                !quarantine_witness.unchanged_now(quarantine_error)) {
                if (quarantine_error == ERROR_SUCCESS) {
                    quarantine_error = ERROR_FILE_INVALID;
                }
                quarantine_witness.abandon();
                release_staging_pins();
                return materialization_failure(
                    StockResearchCopyErrorCode::cleanup_failed,
                    quarantine_error);
            }
            retained_root_delete_capable = true;
        }

        if (!quarantine_retained_tree(quarantine_error)) {
            quarantine_witness.abandon();
            release_staging_pins();
            return materialization_failure(
                StockResearchCopyErrorCode::cleanup_failed,
                quarantine_error);
        }
        const bool exact_quarantine =
            quarantine_witness.consume_exact_rename(
                {}, destination->destination.filename().native(),
                *quarantine_leaf, quarantine_error);
        quarantine_witness.abandon();
        return finish_quarantined_failure(
            exact_quarantine ? code
                             : StockResearchCopyErrorCode::cleanup_failed,
            exact_quarantine ? native_error
                             : (quarantine_error == ERROR_SUCCESS
                                    ? ERROR_FILE_INVALID
                                    : quarantine_error));
    };

    // NTFS refuses to rename a directory while no-delete-share handles to its
    // descendants remain open.  All staging bytes and identities have already
    // been verified, so release only the descendants for the atomic root
    // rename. The no-delete-share staging root stays pinned until a recursive
    // parent witness is live. A short share-delete transition is then tied to
    // the original FileId and exact relative leaf; every notification other
    // than the one expected root rename fails closed.
    // The candidate was fully verified while staging was private. Close its
    // no-delete-share handle for the root rename; it is reopened by relative
    // FileId and re-hashed immediately after publication, before the final
    // watcher window begins.
    commit_candidate.handle.reset();
    release_descendant_pins();

    PublishedTreeChangeWitness publish_transition_witness;
    DWORD transition_error = ERROR_SUCCESS;
    if (!publish_transition_witness.begin(
            destination->parent, destination->parent_snapshot.identity,
            transition_error, false, true) ||
        !publish_transition_witness.unchanged_now(transition_error)) {
        publish_transition_witness.abandon();
        return fail_and_clean(
            StockResearchCopyErrorCode::destination_publish_failed,
            transition_error);
    }

    staging_handle.reset();
    for (std::size_t attempt = 0U;
         attempt < kPublishTransitionAttempts && !staging_handle; ++attempt) {
        staging_handle = open_relative_directory_for_publish(
            destination->parent_handle(), *staging_leaf, true, true,
            transition_error);
        if (staging_handle || transition_error != ERROR_SHARING_VIOLATION ||
            attempt + 1U == kPublishTransitionAttempts) {
            break;
        }
        if (!publish_transition_witness.unchanged_now(transition_error)) break;
        ::Sleep(kPublishTransitionRetryMilliseconds);
    }
    EntrySnapshot transition_snapshot;
    if (!staging_handle ||
        !ordinary_directory_handle(
            staging_handle.get(), &transition_snapshot) ||
        transition_snapshot.identity != staging_snapshot.identity ||
        !exact_opened_path(staging_handle.get(), staging) ||
        !parent_still_exact(*destination)) {
        if (transition_error == ERROR_SUCCESS) {
            transition_error = ERROR_FILE_INVALID;
        }
        publish_transition_witness.abandon();
        // A failed/mismatched relative reopen no longer proves that the
        // staging pathname denotes our object. Retain it for diagnosis.
        release_staging_pins();
        return materialization_failure(
            StockResearchCopyErrorCode::cleanup_failed, transition_error);
    }
    retained_root_delete_capable = true;

    PublishedTreeChangeWitness staging_content_witness;
    if (!staging_content_witness.begin(
            staging, staging_snapshot.identity, transition_error) ||
        !publish_transition_witness.unchanged_now(transition_error) ||
        !staging_content_witness.unchanged_now(transition_error)) {
        staging_content_witness.abandon();
        publish_transition_witness.abandon();
        return fail_retained_unpublished(
            StockResearchCopyErrorCode::destination_identity_invalid,
            transition_error == ERROR_SUCCESS ? ERROR_FILE_INVALID
                                               : transition_error);
    }

    if (options.progress_hook != nullptr) {
        // Test-only deterministic point inside the share-delete transition.
        // The parent witness and retained FileId must reject any swap here.
        options.progress_hook(
            StockResearchCopyProgressPhase::before_destination_publish,
            file_ordinal, options.progress_context);
    }
    if (!ordinary_directory_handle(
            staging_handle.get(), &transition_snapshot) ||
        transition_snapshot.identity != staging_snapshot.identity ||
        !exact_opened_path(staging_handle.get(), staging) ||
        !parent_still_exact(*destination) ||
        !publish_transition_witness.unchanged_now(transition_error) ||
        !staging_content_witness.unchanged_now(transition_error)) {
        if (transition_error == ERROR_SUCCESS) {
            transition_error = ERROR_FILE_INVALID;
        }
        staging_content_witness.abandon();
        publish_transition_witness.abandon();
        return fail_retained_unpublished(
            StockResearchCopyErrorCode::destination_identity_invalid,
            transition_error);
    }

    bool published = false;
    DWORD publish_error = ERROR_SUCCESS;
    for (std::size_t attempt = 0U; attempt < kPublishTransitionAttempts;
         ++attempt) {
        if (rename_open_file_without_replace(
                staging_handle.get(), destination->parent_handle(),
                destination->destination.filename().native())) {
            published = true;
            break;
        }
        publish_error = ::GetLastError();
        if ((publish_error != ERROR_SHARING_VIOLATION &&
             publish_error != ERROR_ACCESS_DENIED) ||
            attempt + 1U == kPublishTransitionAttempts) {
            break;
        }
        if (!ordinary_directory_handle(
                staging_handle.get(), &transition_snapshot) ||
            transition_snapshot.identity != staging_snapshot.identity ||
            !exact_opened_path(staging_handle.get(), staging) ||
            !parent_still_exact(*destination) ||
            !publish_transition_witness.unchanged_now(transition_error) ||
            !staging_content_witness.unchanged_now(transition_error)) {
            publish_error = transition_error == ERROR_SUCCESS
                                ? ERROR_FILE_INVALID
                                : transition_error;
            break;
        }
        ::Sleep(kPublishTransitionRetryMilliseconds);
    }
    if (!published) {
        staging_content_witness.abandon();
        publish_transition_witness.abandon();
        return fail_retained_unpublished(
            publish_error == ERROR_ALREADY_EXISTS ||
                    publish_error == ERROR_FILE_EXISTS
                ? StockResearchCopyErrorCode::destination_exists
                : StockResearchCopyErrorCode::destination_publish_failed,
            publish_error);
    }

    if (!ordinary_directory_handle(
            staging_handle.get(), &transition_snapshot) ||
        transition_snapshot.identity != staging_snapshot.identity ||
        !exact_opened_path(
            staging_handle.get(), destination->destination) ||
        !parent_still_exact(*destination) ||
        !staging_content_witness.unchanged_now(transition_error) ||
        !publish_transition_witness.consume_exact_rename(
            {}, *staging_leaf,
            destination->destination.filename().native(), transition_error)) {
        if (transition_error == ERROR_SUCCESS) {
            transition_error = ERROR_FILE_INVALID;
        }
        staging_content_witness.abandon();
        publish_transition_witness.abandon();
        return fail_published(
            StockResearchCopyErrorCode::destination_identity_invalid,
            transition_error);
    }
    publish_transition_witness.abandon();

    // Re-establish the original no-delete-share root pin before any existing
    // post-publish hook is reachable. A second parent witness and a compatible
    // no-DELETE/share-delete bridge retain the published FileId across the
    // interval in which the DELETE-capable transition handle must be closed
    // before the incompatible locked handle can be opened.
    PublishedTreeChangeWitness published_repin_witness;
    if (!published_repin_witness.begin(
            destination->parent, destination->parent_snapshot.identity,
            transition_error, false, true) ||
        !ordinary_directory_handle(
            staging_handle.get(), &transition_snapshot) ||
        transition_snapshot.identity != staging_snapshot.identity ||
        !exact_opened_path(
            staging_handle.get(), destination->destination) ||
        !parent_still_exact(*destination)) {
        if (transition_error == ERROR_SUCCESS) {
            transition_error = ERROR_FILE_INVALID;
        }
        published_repin_witness.abandon();
        return fail_published(
            StockResearchCopyErrorCode::destination_identity_invalid,
            transition_error);
    }

    auto published_bridge = open_relative_directory_for_publish(
        destination->parent_handle(),
        destination->destination.filename().native(), false, true,
        transition_error);
    EntrySnapshot bridge_snapshot;
    if (!published_bridge ||
        !ordinary_directory_handle(
            published_bridge.get(), &bridge_snapshot) ||
        bridge_snapshot.identity != staging_snapshot.identity ||
        !exact_opened_path(
            published_bridge.get(), destination->destination) ||
        !parent_still_exact(*destination) ||
        !published_repin_witness.unchanged_now(transition_error)) {
        if (transition_error == ERROR_SUCCESS) {
            transition_error = ERROR_FILE_INVALID;
        }
        published_repin_witness.abandon();
        return fail_published(
            StockResearchCopyErrorCode::destination_identity_invalid,
            transition_error);
    }

    staging_handle.reset();
    for (std::size_t attempt = 0U;
         attempt < kPublishTransitionAttempts && !staging_handle; ++attempt) {
        staging_handle = open_relative_directory_for_publish(
            destination->parent_handle(),
            destination->destination.filename().native(), false, false,
            transition_error);
        if (staging_handle ||
            (transition_error != ERROR_SHARING_VIOLATION &&
             transition_error != ERROR_ACCESS_DENIED) ||
            attempt + 1U == kPublishTransitionAttempts) {
            break;
        }
        if (!published_repin_witness.unchanged_now(transition_error)) break;
        ::Sleep(kPublishTransitionRetryMilliseconds);
    }
    if (!staging_handle ||
        !ordinary_directory_handle(
            staging_handle.get(), &transition_snapshot) ||
        transition_snapshot.identity != staging_snapshot.identity ||
        !exact_opened_path(
            staging_handle.get(), destination->destination) ||
        !ordinary_directory_handle(
            published_bridge.get(), &bridge_snapshot) ||
        bridge_snapshot.identity != staging_snapshot.identity ||
        !exact_opened_path(
            published_bridge.get(), destination->destination) ||
        !parent_still_exact(*destination) ||
        !published_repin_witness.unchanged_now(transition_error)) {
        if (transition_error == ERROR_SUCCESS) {
            transition_error = ERROR_FILE_INVALID;
        }
        staging_handle = std::move(published_bridge);
        retained_root_delete_capable = false;
        published_repin_witness.abandon();
        return fail_published(
            StockResearchCopyErrorCode::destination_identity_invalid,
            transition_error);
    }
    published_bridge.reset();
    retained_root_delete_capable = false;
    published_repin_witness.abandon();

    DWORD repin_error = ERROR_SUCCESS;
    if (!repin_materialized_directories(
            staging_handle.get(), destination->destination,
            materialized_directories, repin_error)) {
        return fail_published(
            StockResearchCopyErrorCode::destination_identity_invalid,
            repin_error);
    }

    if (options.progress_hook != nullptr) {
        options.progress_hook(
            StockResearchCopyProgressPhase::after_destination_publish,
            file_ordinal, options.progress_context);
    }

    EntrySnapshot published_snapshot;
    if (!ordinary_directory_handle(
            staging_handle.get(), &published_snapshot) ||
        published_snapshot.identity != staging_snapshot.identity ||
        !exact_opened_path(
            staging_handle.get(), destination->destination)) {
        const DWORD verify_error = ::GetLastError();
        return fail_published(
            StockResearchCopyErrorCode::destination_identity_invalid,
            verify_error);
    }

    DWORD candidate_error = ERROR_SUCCESS;
    if (!repin_prepared_commit_marker(
            staging_handle.get(), destination->destination, marker_bytes,
            commit_candidate, candidate_error)) {
        return fail_published(
            StockResearchCopyErrorCode::destination_identity_invalid,
            candidate_error);
    }

    const auto final_limits = published_inventory_limits(
        options.limits,
        manifest_bytes.size() + pending_bytes.size() + marker_bytes.size(), 3U);

    // A newly closed file can make NTFS publish its directory timestamp change
    // lazily. Perform and validate one retained-candidate inventory before the
    // transaction watcher is armed; the independently repeated inventory below
    // remains the one protected by the watcher and file oplocks.
    auto settled_inventory_result = build_inventory(
        destination->destination, final_limits, true, true);
    DWORD settled_candidate_error = ERROR_SUCCESS;
    if (!settled_inventory_result.inventory ||
        !published_inventory_matches(
            source, *settled_inventory_result.inventory,
            staging_snapshot.identity, owned_entries, manifest_relative,
            manifest_bytes, pending_relative, pending_bytes,
            commit_candidate.relative_path, marker_bytes) ||
        !prepared_commit_marker_unchanged(
            commit_candidate, marker_bytes, settled_candidate_error)) {
        commit_candidate.handle.reset();
        return fail_published(
            StockResearchCopyErrorCode::destination_inventory_mismatch,
            settled_candidate_error == ERROR_SUCCESS
                ? (settled_inventory_result.native_error == ERROR_SUCCESS
                       ? ERROR_FILE_INVALID
                       : settled_inventory_result.native_error)
                : settled_candidate_error);
    }

    PublishedTreeChangeWitness tree_change_witness;
    DWORD tree_change_error = ERROR_SUCCESS;
    // Watch the pinned parent rather than only the published root. Windows
    // reports mutations to the root directory object's own ADS/attributes in
    // its parent, while subtree=true still covers every descendant.
    if (!tree_change_witness.begin(
            destination->parent, destination->parent_snapshot.identity,
            tree_change_error)) {
        staging_content_witness.abandon();
        return fail_published(
            StockResearchCopyErrorCode::destination_inventory_mismatch,
            tree_change_error);
    }
    if (!staging_content_witness.unchanged_now(tree_change_error)) {
        staging_content_witness.abandon();
        return fail_published(
            StockResearchCopyErrorCode::destination_inventory_mismatch,
            tree_change_error);
    }
    staging_content_witness.abandon();

    auto published_inventory_result = build_inventory(
        destination->destination, final_limits, true, true);
    const bool inventory_matches =
        published_inventory_result.inventory &&
        published_inventory_matches(
            source, *published_inventory_result.inventory,
            staging_snapshot.identity, owned_entries, manifest_relative,
            manifest_bytes, pending_relative, pending_bytes,
            commit_candidate.relative_path, marker_bytes);
    if (inventory_matches && options.progress_hook != nullptr) {
        // Test-only deterministic race point: the inventory value is already
        // cached while the recursive change request is live. Per-file oplocks
        // are acquired and the files re-read immediately after this callback.
        options.progress_hook(
            StockResearchCopyProgressPhase::during_published_inventory,
            file_ordinal, options.progress_context);
    }

    std::vector<std::unique_ptr<PublishedFileOplock>> published_file_guards;
    DWORD file_guard_error = ERROR_SUCCESS;
    const bool files_guarded =
        inventory_matches &&
        guard_published_files(
            staging_handle.get(), destination->destination,
            materialized_directories, owned_entries, source,
            manifest_relative, manifest_bytes, pending_relative, pending_bytes,
            commit_candidate.relative_path,
            published_file_guards, file_guard_error);
    if (files_guarded && options.progress_hook != nullptr) {
        // Test-only deterministic boundary: every Filter oplock and retained
        // read handle is live. A hostile incompatible open started here must
        // remain blocked until the tree is classified as success or failure.
        options.progress_hook(
            StockResearchCopyProgressPhase::
                after_published_file_guards_acquired,
            file_ordinal, options.progress_context);
    }

    // Stable pre-commit observation: all expected files have been re-hashed
    // through retained Filter-oplock read handles, every directory is
    // identity-pinned, and the parent subtree notification remains pending.
    // The isolation-marker rename below is the actual commit linearization
    // point while these guards are still live.
    bool files_unchanged = files_guarded;
    if (files_guarded) {
        for (const auto& guard : published_file_guards) {
            DWORD guard_error = ERROR_SUCCESS;
            if (!guard->unchanged_now(guard_error)) {
                files_unchanged = false;
                if (file_guard_error == ERROR_SUCCESS) {
                    file_guard_error = guard_error;
                }
            }
        }
        DWORD commit_candidate_error = ERROR_SUCCESS;
        if (!prepared_commit_marker_unchanged(
                commit_candidate, marker_bytes, commit_candidate_error)) {
            files_unchanged = false;
            if (file_guard_error == ERROR_SUCCESS) {
                file_guard_error = commit_candidate_error;
            }
        }
    }
    const bool tree_unchanged =
        tree_change_witness.unchanged_now(tree_change_error);
    const bool source_unchanged_before_commit = source_commit_unchanged();
    if (!inventory_matches || !files_unchanged || !tree_unchanged ||
        !source_unchanged_before_commit) {
        DWORD inventory_error = published_inventory_result.native_error;
        if (!files_unchanged && file_guard_error != ERROR_SUCCESS) {
            inventory_error = file_guard_error;
        }
        if (!tree_unchanged && tree_change_error != ERROR_SUCCESS) {
            inventory_error = tree_change_error;
        }
        if (!source_unchanged_before_commit &&
            source_change_error != ERROR_SUCCESS) {
            inventory_error = source_change_error;
        }
        if (inventory_error == ERROR_SUCCESS) {
            inventory_error = ERROR_INVALID_DATA;
        }
        // Ancestor-directory rename itself breaks descendant Filter oplocks on
        // NTFS, so quarantine cannot precede acknowledgement. The durable
        // pending marker and the absence of the isolation/commit marker make
        // this public name non-authorizing even if a hostile open later blocks
        // quarantine.
        tree_change_witness.abandon();
        published_file_guards.clear();
        commit_candidate.handle.reset();
        if (options.progress_hook != nullptr) {
            // Test-only deterministic teardown boundary. Production callers
            // never install a progress hook.
            options.progress_hook(
                StockResearchCopyProgressPhase::
                    after_published_file_guards_released_on_failure,
                file_ordinal, options.progress_context);
        }
        return fail_published(
            source_unchanged_before_commit
                ? StockResearchCopyErrorCode::destination_inventory_mismatch
                : source_mutation_code,
            inventory_error);
    }

    if (options.progress_hook != nullptr) {
        // Deterministic test boundary. The recursive watcher and all retained
        // guards remain live; any completed mutation here must precede and
        // therefore disqualify the marker rename below.
        options.progress_hook(
            StockResearchCopyProgressPhase::before_commit_marker_publish,
            file_ordinal, options.progress_context);
    }

    const bool external_provenance_valid =
        !external_approval || external_provenance_unchanged();
    if (external_approval &&
        (std::chrono::system_clock::now() >= external_approval->expires_at ||
         !approval_artifact_unchanged() || !external_provenance_valid)) {
        tree_change_witness.abandon();
        published_file_guards.clear();
        commit_candidate.handle.reset();
        return fail_published(
            std::chrono::system_clock::now() >= external_approval->expires_at
                ? StockResearchCopyErrorCode::external_target_approval_expired
                : (!external_provenance_valid
                       ? source_mutation_code
                       : StockResearchCopyErrorCode::
                             external_target_approval_mismatch),
            ERROR_FILE_INVALID);
    }

    if (!rename_open_file_without_replace(
            commit_candidate.handle.get(), staging_handle.get(),
            marker_relative.filename().native())) {
        const DWORD commit_error = ::GetLastError();
        tree_change_witness.abandon();
        published_file_guards.clear();
        commit_candidate.handle.reset();
        return fail_published(
            StockResearchCopyErrorCode::destination_write_failed,
            commit_error);
    }

    if (!tree_change_witness.consume_exact_rename(
            destination->destination.filename().native(),
            commit_candidate.relative_path.filename().native(),
            marker_relative.filename().native(), tree_change_error)) {
        const auto revocation =
            revoke_commit_marker(staging_handle.get(), commit_candidate);
        if (revocation == CommitMarkerRevocation::removed) {
            // The securely deleted candidate was the final owned witness.
            owned_entries.pop_back();
        }
        tree_change_witness.abandon();
        published_file_guards.clear();
        commit_candidate.handle.reset();
        return fail_published(
            StockResearchCopyErrorCode::destination_inventory_mismatch,
            tree_change_error == ERROR_SUCCESS ? ERROR_FILE_INVALID
                                               : tree_change_error);
    }
    if (!source_commit_unchanged() ||
        (external_approval && !external_provenance_unchanged())) {
        const auto revocation =
            revoke_commit_marker(staging_handle.get(), commit_candidate);
        if (revocation == CommitMarkerRevocation::removed) {
            owned_entries.pop_back();
        }
        tree_change_witness.abandon();
        published_file_guards.clear();
        commit_candidate.handle.reset();
        return fail_published(
            source_mutation_code,
            source_change_error == ERROR_SUCCESS ? ERROR_FILE_INVALID
                                                 : source_change_error);
    }
    tree_change_witness.abandon();
    published_file_guards.clear();
    commit_candidate.handle.reset();
    return {std::move(materialization), StockResearchCopyErrorCode::none,
            ERROR_SUCCESS};
}

} // namespace

std::string_view to_string(
    const StockResearchTopologyCategory category) noexcept
{
    switch (category) {
    case StockResearchTopologyCategory::ordinary_tree:
        return "ordinary_tree";
    case StockResearchTopologyCategory::source_path_ancestor_reparse:
        return "source_path_ancestor_reparse";
    case StockResearchTopologyCategory::source_root_reparse:
        return "source_root_reparse";
    case StockResearchTopologyCategory::source_internal_directory_junction:
        return "source_internal_directory_junction";
    case StockResearchTopologyCategory::source_internal_directory_symlink:
        return "source_internal_directory_symlink";
    case StockResearchTopologyCategory::source_internal_file_symlink:
        return "source_internal_file_symlink";
    case StockResearchTopologyCategory::source_internal_mount_point:
        return "source_internal_mount_point";
    case StockResearchTopologyCategory::source_file_hardlink:
        return "source_file_hardlink";
    case StockResearchTopologyCategory::source_alternate_data_stream:
        return "source_alternate_data_stream";
    case StockResearchTopologyCategory::source_subst_drive:
        return "source_subst_drive";
    case StockResearchTopologyCategory::source_unc_path:
        return "source_unc_path";
    case StockResearchTopologyCategory::source_remote_volume:
        return "source_remote_volume";
    case StockResearchTopologyCategory::source_unsupported_reparse_tag:
        return "source_unsupported_reparse_tag";
    case StockResearchTopologyCategory::source_link_target_outside_root:
        return "source_link_target_outside_root";
    case StockResearchTopologyCategory::source_link_cycle:
        return "source_link_cycle";
    case StockResearchTopologyCategory::source_link_depth_exceeded:
        return "source_link_depth_exceeded";
    case StockResearchTopologyCategory::source_entry_limit_exceeded:
        return "source_entry_limit_exceeded";
    case StockResearchTopologyCategory::source_byte_limit_exceeded:
        return "source_byte_limit_exceeded";
    case StockResearchTopologyCategory::source_reviewed_external_target:
        return "source_reviewed_external_target";
    }
    return "unknown";
}

std::string_view to_string(const StockResearchCopyErrorCode code) noexcept
{
    switch (code) {
    case StockResearchCopyErrorCode::none: return "none";
    case StockResearchCopyErrorCode::invalid_argument: return "invalid_argument";
    case StockResearchCopyErrorCode::source_not_absolute:
        return "source_not_absolute";
    case StockResearchCopyErrorCode::source_not_found: return "source_not_found";
    case StockResearchCopyErrorCode::source_not_directory:
        return "source_not_directory";
    case StockResearchCopyErrorCode::source_open_failed:
        return "source_open_failed";
    case StockResearchCopyErrorCode::source_identity_query_failed:
        return "source_identity_query_failed";
    case StockResearchCopyErrorCode::source_final_path_failed:
        return "source_final_path_failed";
    case StockResearchCopyErrorCode::source_not_local_fixed_volume:
        return "source_not_local_fixed_volume";
    case StockResearchCopyErrorCode::source_required_launcher_missing:
        return "source_required_launcher_missing";
    case StockResearchCopyErrorCode::source_already_prepared:
        return "source_already_prepared";
    case StockResearchCopyErrorCode::source_topology_unsafe:
        return "source_topology_unsafe";
    case StockResearchCopyErrorCode::source_changed_during_materialization:
        return "source_changed_during_materialization";
    case StockResearchCopyErrorCode::source_read_failed:
        return "source_read_failed";
    case StockResearchCopyErrorCode::source_digest_failed:
        return "source_digest_failed";
    case StockResearchCopyErrorCode::external_target_review_invalid:
        return "external_target_review_invalid";
    case StockResearchCopyErrorCode::external_target_approval_missing:
        return "external_target_approval_missing";
    case StockResearchCopyErrorCode::external_target_approval_invalid:
        return "external_target_approval_invalid";
    case StockResearchCopyErrorCode::external_target_approval_expired:
        return "external_target_approval_expired";
    case StockResearchCopyErrorCode::external_target_approval_mismatch:
        return "external_target_approval_mismatch";
    case StockResearchCopyErrorCode::external_target_not_evidence_eligible:
        return "external_target_not_evidence_eligible";
    case StockResearchCopyErrorCode::external_materialization_path_collision:
        return "external_materialization_path_collision";
    case StockResearchCopyErrorCode::
        source_or_external_target_changed_during_materialization:
        return "source_or_external_target_changed_during_materialization";
    case StockResearchCopyErrorCode::destination_not_absolute:
        return "destination_not_absolute";
    case StockResearchCopyErrorCode::destination_exists:
        return "destination_exists";
    case StockResearchCopyErrorCode::destination_parent_invalid:
        return "destination_parent_invalid";
    case StockResearchCopyErrorCode::destination_parent_reparse:
        return "destination_parent_reparse";
    case StockResearchCopyErrorCode::destination_not_local_fixed_volume:
        return "destination_not_local_fixed_volume";
    case StockResearchCopyErrorCode::destination_subst_drive:
        return "destination_subst_drive";
    case StockResearchCopyErrorCode::destination_overlaps_source:
        return "destination_overlaps_source";
    case StockResearchCopyErrorCode::destination_overlaps_steam_library:
        return "destination_overlaps_steam_library";
    case StockResearchCopyErrorCode::destination_create_failed:
        return "destination_create_failed";
    case StockResearchCopyErrorCode::destination_write_failed:
        return "destination_write_failed";
    case StockResearchCopyErrorCode::destination_flush_failed:
        return "destination_flush_failed";
    case StockResearchCopyErrorCode::destination_publish_failed:
        return "destination_publish_failed";
    case StockResearchCopyErrorCode::destination_identity_invalid:
        return "destination_identity_invalid";
    case StockResearchCopyErrorCode::destination_inventory_mismatch:
        return "destination_inventory_mismatch";
    case StockResearchCopyErrorCode::manifest_write_failed:
        return "manifest_write_failed";
    case StockResearchCopyErrorCode::cleanup_failed: return "cleanup_failed";
    case StockResearchCopyErrorCode::enumeration_failed:
        return "enumeration_failed";
    case StockResearchCopyErrorCode::native_api_unavailable:
        return "native_api_unavailable";
    }
    return "unknown";
}

std::string_view to_string(
    const StockResearchCopyEvidenceEligibility status) noexcept
{
    switch (status) {
    case StockResearchCopyEvidenceEligibility::eligible:
        return "eligible";
    case StockResearchCopyEvidenceEligibility::ineligible_external_code:
        return "ineligible_external_code";
    case StockResearchCopyEvidenceEligibility::ineligible_mutable_state:
        return "ineligible_mutable_state";
    case StockResearchCopyEvidenceEligibility::ineligible_cross_application:
        return "ineligible_cross_application";
    case StockResearchCopyEvidenceEligibility::
        ineligible_unknown_external_target:
        return "ineligible_unknown_external_target";
    }
    return "ineligible_unknown_external_target";
}

StockResearchTopologyResult inspect_stock_research_topology(
    const fs::path& source_root,
    const StockResearchCopyLimits& limits) noexcept
{
    auto result = build_inventory(source_root, limits, false);
    if (!result.inventory) {
        return topology_failure(result.code, result.native_error);
    }
    return {std::move(result.inventory->summary),
            StockResearchCopyErrorCode::none, ERROR_SUCCESS};
}

StockResearchMaterializationResult materialize_stock_research_copy(
    const fs::path& source_root,
    const fs::path& destination_root,
    const StockResearchCopyOptions& options) noexcept
{
    if (!valid_limits(options.limits)) {
        return materialization_failure(
            StockResearchCopyErrorCode::invalid_argument,
            ERROR_INVALID_PARAMETER);
    }
    return materialize_impl(source_root, destination_root, options);
}

bool stock_research_isolation_marker_exact(
    const fs::path& research_root,
    const StockResearchMarkerValidationOptions& options) noexcept
{
    try {
        if (research_root.empty() || !research_root.is_absolute()) return false;
        const auto marker =
            (research_root / L".hlclient-research-isolated").lexically_normal();
        UniqueHandle input{::CreateFileW(
            marker.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
        EntrySnapshot initial;
        constexpr auto base_size = kStockResearchIsolationMarkerV1.size();
        if (!input || !query_snapshot(input.get(), initial) || initial.directory ||
            (initial.attributes &
             (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_SPARSE_FILE)) != 0U ||
            initial.link_count != 1U ||
            (initial.size != base_size && initial.size != base_size + 1U &&
             initial.size != base_size + 2U) ||
            !exact_opened_path(input.get(), marker) ||
            !handle_has_only_default_stream(input.get())) {
            return false;
        }
        if (options.progress_hook != nullptr) {
            options.progress_hook(
                StockResearchMarkerValidationPhase::after_handle_acquired,
                options.progress_context);
        }
        EntrySnapshot before;
        if (!query_snapshot(input.get(), before) || before != initial ||
            !exact_opened_path(input.get(), marker) ||
            !handle_has_only_default_stream(input.get())) {
            return false;
        }
        LARGE_INTEGER zero{};
        if (::SetFilePointerEx(input.get(), zero, nullptr, FILE_BEGIN) == FALSE) {
            return false;
        }
        std::array<char, base_size + 2U> bytes{};
        std::size_t offset = 0U;
        while (offset < before.size) {
            DWORD read = 0U;
            if (::ReadFile(
                    input.get(), bytes.data() + offset,
                    static_cast<DWORD>(before.size - offset), &read, nullptr) ==
                    FALSE ||
                read == 0U) {
                return false;
            }
            offset += read;
        }
        char extra{};
        DWORD extra_read = 0U;
        if (::ReadFile(input.get(), &extra, 1U, &extra_read, nullptr) == FALSE ||
            extra_read != 0U) {
            return false;
        }
        EntrySnapshot after;
        if (!query_snapshot(input.get(), after) || after != initial ||
            !exact_opened_path(input.get(), marker) ||
            !handle_has_only_default_stream(input.get())) {
            return false;
        }
        const std::string_view observed{bytes.data(), offset};
        return observed == kStockResearchIsolationMarkerV1 ||
               (observed.size() == base_size + 1U &&
                observed.starts_with(kStockResearchIsolationMarkerV1) &&
                observed.back() == '\n') ||
               (observed.size() == base_size + 2U &&
                observed.starts_with(kStockResearchIsolationMarkerV1) &&
                observed[base_size] == '\r' && observed.back() == '\n');
    } catch (...) {
        return false;
    }
}

} // namespace hlclient::platform::windows
