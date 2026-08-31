#include <hlclient/platform/windows/stock_external_target_review.hpp>

#include <hlclient/hash/sha256.hpp>
#include <hlclient/platform/windows/secure_output.hpp>
#include <hlclient/platform/windows/stock_external_target_artifact.hpp>

#include <Windows.h>
#include <bcrypt.h>
#include <winioctl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cwctype>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>
#include <vector>

namespace hlclient::platform::windows {
namespace {

constexpr std::string_view kExternalReviewImplementationProfile =
    "hlclient.stock-external-target-review.windows-v1";

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

struct Scan final {
    std::vector<StockExternalTargetReview> targets;
    Snapshot source_snapshot{};
    std::filesystem::path source_final_path;
    std::string source_identity;
    std::string source_inventory_sha256;
    std::size_t source_entry_count{0U};
    std::uint64_t source_byte_count{0U};
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

[[nodiscard]] std::optional<std::filesystem::path> logical_source_path(
    const std::filesystem::path& entry,
    const std::filesystem::path& source) noexcept
{
    try {
        const auto relative =
            entry.lexically_normal().lexically_relative(
                source.lexically_normal());
        if (relative.empty() || relative.is_absolute() ||
            relative.has_root_name() || relative.has_root_directory()) {
            return std::nullopt;
        }
        for (const auto& component : relative) {
            if (component.empty() || component == L"." ||
                component == L"..") {
                return std::nullopt;
            }
        }
        return relative;
    } catch (...) {
        return std::nullopt;
    }
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

[[nodiscard]] bool scan_target(
    const std::filesystem::path& source_root,
    const std::filesystem::path& root,
    const Snapshot& approved_root_snapshot,
    const StockResearchCopyLimits& limits,
    StockExternalTargetReview& review)
{
    if (!local_fixed(root)) {
        review.entry_count = 1U;
        review.byte_count = approved_root_snapshot.directory
                                ? 0U
                                : approved_root_snapshot.size;
        review.classification =
            StockExternalTargetClassification::remote_or_device_target;
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
        return true;
    }

    auto retained_root = open_path(
        root, true, approved_root_snapshot.directory);
    const auto retained_before =
        retained_root.valid() ? snapshot(retained_root.get()) : std::nullopt;
    if (!retained_before ||
        identity(*retained_before) != identity(approved_root_snapshot) ||
        (retained_before->attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
        has_ads(root)) {
        review.entry_count = 1U;
        review.byte_count = approved_root_snapshot.directory
                                ? 0U
                                : approved_root_snapshot.size;
        review.classification =
            StockExternalTargetClassification::changed_during_review;
        return true;
    }

    std::error_code ec;
    std::vector<std::filesystem::path> paths{root};
    if (retained_before->directory) {
        for (std::filesystem::recursive_directory_iterator it(
                 root, std::filesystem::directory_options::none, ec),
             end;
             !ec && it != end; it.increment(ec)) {
            paths.push_back(it->path());
            if (paths.size() > limits.maximum_entries) {
                review.classification = StockExternalTargetClassification::
                    content_limit_exceeded;
                return true;
            }
        }
        if (ec) return false;
    }
    std::ranges::sort(paths, {}, [](const auto& path) {
        return path.native();
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
    for (const auto& path : paths) {
        const DWORD attributes = ::GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) return false;
        const bool is_root = path == root;
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
            ++review.nested_link_count;
            nested_link = true;
            if (!is_root) continue;
        }
        if (has_ads(path)) unsupported = true;
        const bool directory =
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
        auto handle = open_path(path, true, directory);
        const auto before =
            handle.valid() ? snapshot(handle.get()) : std::nullopt;
        if (!before) return false;
        ++review.entry_count;
        if (!directory) {
            if (before->size > limits.maximum_file_bytes ||
                before->size > limits.maximum_total_bytes ||
                review.byte_count >
                    limits.maximum_total_bytes - before->size) {
                review.classification = StockExternalTargetClassification::
                    content_limit_exceeded;
                return true;
            }
            review.byte_count += before->size;
        }
        auto relative = is_root
                            ? std::filesystem::path{L"."}
                            : std::filesystem::relative(path, root, ec);
        if (ec) return false;
        inventory << wide_hex(relative) << ':' << identity(*before);
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
            const auto kind = content_kind(handle.get());
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
                    handle.get(), path.extension(), before->size)) {
                known_asset = true;
            } else if (!executable_entry && !script_entry) {
                unknown_asset = true;
            }
            std::string content;
            if (!read_hash(handle.get(), before->size, content)) return false;
            inventory << ':' << content;
        }
        const auto after = snapshot(handle.get());
        if (!after || identity(*before) != identity(*after)) {
            review.classification =
                StockExternalTargetClassification::changed_during_review;
            return true;
        }
        inventory << '\n';
    }
    const auto retained_after = snapshot(retained_root.get());
    if (!retained_after ||
        identity(*retained_before) != identity(*retained_after)) {
        review.classification =
            StockExternalTargetClassification::changed_during_review;
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

[[nodiscard]] std::optional<Scan> scan_source(const std::filesystem::path& source,
                                              const StockResearchCopyLimits& limits)
{
    if (!source.is_absolute() || !local_fixed(source)) return std::nullopt;
    auto root = open_path(source, true, true);
    const auto root_snapshot = root.valid() ? snapshot(root.get()) : std::nullopt;
    const auto root_final = root.valid() ? final_path(root.get()) : std::nullopt;
    if (!root_snapshot || !root_final ||
        (root_snapshot->attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        return std::nullopt;
    }
    Scan scan{};
    scan.source_snapshot = *root_snapshot;
    scan.source_final_path = *root_final;
    scan.source_identity = identity(*root_snapshot);
    std::error_code ec;
    std::vector<std::pair<std::wstring, std::string>>
        source_inventory_records;
    std::size_t source_entries = 0U;
    std::uint64_t source_bytes = 0U;
    for (std::filesystem::recursive_directory_iterator it(
             source,
             std::filesystem::directory_options::follow_directory_symlink,
             ec), end;
         !ec && it != end; it.increment(ec)) {
        const DWORD attrs = ::GetFileAttributesW(it->path().c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) return std::nullopt;
        if (++source_entries > limits.maximum_entries) return std::nullopt;
        const bool directory = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0U;
        auto entry = open_path(it->path(), true, directory);
        const auto entry_snapshot = entry.valid() ? snapshot(entry.get()) : std::nullopt;
        if (!entry_snapshot) return std::nullopt;
        if (!directory &&
            (entry_snapshot->size > limits.maximum_file_bytes ||
             entry_snapshot->size > limits.maximum_total_bytes ||
             source_bytes >
                 limits.maximum_total_bytes - entry_snapshot->size)) {
            return std::nullopt;
        }
        if (!directory) source_bytes += entry_snapshot->size;
        const auto logical = logical_source_path(it->path(), source);
        if (!logical) return std::nullopt;
        const bool reparse =
            (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0U;
        std::ostringstream inventory_record;
        if (!reparse) {
            inventory_record << wide_hex(*logical) << ':'
                             << identity(*entry_snapshot);
        }
        if (!directory && !reparse) {
            std::string content;
            if (!read_hash(entry.get(), entry_snapshot->size, content)) return std::nullopt;
            inventory_record << ':' << content;
        }
        if (!reparse) {
            const auto entry_after = snapshot(entry.get());
            if (!entry_after ||
                identity(*entry_snapshot) != identity(*entry_after)) {
                return std::nullopt;
            }
            source_inventory_records.emplace_back(
                logical->native(), inventory_record.str());
            continue;
        }
        auto link = std::move(entry);
        auto target = open_path(it->path(), false, directory);
        const auto link_snapshot = link.valid() ? snapshot(link.get()) : std::nullopt;
        const auto target_snapshot = target.valid() ? snapshot(target.get()) : std::nullopt;
        const auto target_path = target.valid() ? final_path(target.get()) : std::nullopt;
        if (!link_snapshot || !target_snapshot || !target_path) return std::nullopt;
        auto relative = std::filesystem::relative(*target_path, *root_final, ec);
        const bool outside = ec || relative.empty() || relative.native().starts_with(L"..");
        ec.clear();
        if (!directory || outside) it.disable_recursion_pending();
        StockExternalTargetReview review{};
        if (outside) {
            review.source_link_relative_path = *logical;
            review.target_root = *target_path;
            if (!scan_target(
                    source, *target_path, *target_snapshot, limits, review)) {
                return std::nullopt;
            }
        }

        link = Handle{};
        target = Handle{};
        auto settled_link = open_path(it->path(), true, directory);
        auto settled_target = open_path(it->path(), false, directory);
        const auto settled_link_snapshot =
            settled_link.valid() ? snapshot(settled_link.get()) : std::nullopt;
        const auto settled_target_snapshot = settled_target.valid()
                                                 ? snapshot(settled_target.get())
                                                 : std::nullopt;
        const auto settled_target_path =
            settled_target.valid() ? final_path(settled_target.get())
                                   : std::nullopt;
        if (!settled_link_snapshot || !settled_target_snapshot ||
            !settled_target_path ||
            !same_object_shape(*link_snapshot, *settled_link_snapshot) ||
            !same_object_shape(*target_snapshot, *settled_target_snapshot) ||
            lowercase(settled_target_path->native()) !=
                lowercase(target_path->native()) ||
            identity(*target_snapshot) != identity(*settled_target_snapshot) ||
            settled_link_snapshot->reparse_tag == 0U) {
            return std::nullopt;
        }
        inventory_record << wide_hex(*logical) << ':'
                         << identity(*settled_link_snapshot);
        source_inventory_records.emplace_back(
            logical->native(), inventory_record.str());
        if (!outside) continue;

        review.link_identity_sha256 = identity(*settled_link_snapshot);
        review.target_identity_sha256 = identity(*settled_target_snapshot);
        if (review.target_inventory_sha256.empty()) {
            std::ostringstream bounded_classification;
            bounded_classification << "hlclient.external-target-incomplete.v1|"
                                   << review.target_identity_sha256 << '|'
                                   << to_string(review.classification) << '|'
                                   << review.entry_count << '|'
                                   << review.byte_count;
            review.target_inventory_sha256 =
                hash_text(bounded_classification.str());
        }
        scan.eligible = scan.eligible && review.eligible;
        scan.targets.push_back(std::move(review));
    }
    if (ec) return std::nullopt;

    for (auto& target_review : scan.targets) {
        const auto link_path = source / target_review.source_link_relative_path;
        auto link_before = open_path(link_path, true, true);
        const auto link_before_snapshot =
            link_before.valid() ? snapshot(link_before.get()) : std::nullopt;
        if (!link_before_snapshot ||
            link_before_snapshot->reparse_tag == 0U) {
            return std::nullopt;
        }
        link_before = Handle{};
        auto followed = open_path(link_path, false, true);
        const auto followed_snapshot =
            followed.valid() ? snapshot(followed.get()) : std::nullopt;
        const auto followed_path =
            followed.valid() ? final_path(followed.get()) : std::nullopt;
        if (!followed_snapshot || !followed_path ||
            lowercase(followed_path->native()) !=
                lowercase(target_review.target_root.native()) ||
            identity(*followed_snapshot) !=
                target_review.target_identity_sha256) {
            return std::nullopt;
        }
        followed = Handle{};
        auto link = open_path(link_path, true, true);
        const auto link_snapshot =
            link.valid() ? snapshot(link.get()) : std::nullopt;
        if (!link_snapshot || link_snapshot->reparse_tag == 0U ||
            !same_object_shape(*link_before_snapshot, *link_snapshot)) {
            return std::nullopt;
        }
        target_review.link_identity_sha256 = identity(*link_snapshot);
        const auto record = std::ranges::find_if(
            source_inventory_records, [&](const auto& candidate) {
                return candidate.first ==
                       target_review.source_link_relative_path.native();
            });
        if (record == source_inventory_records.end()) return std::nullopt;
        record->second =
            wide_hex(target_review.source_link_relative_path) + ':' +
            target_review.link_identity_sha256;
    }

    std::ranges::sort(source_inventory_records, {},
                      [](const auto& record) { return record.first; });
    std::ostringstream source_inventory;
    for (const auto& record : source_inventory_records) {
        source_inventory << record.second << '\n';
    }
    const auto root_after = snapshot(root.get());
    if (!root_after || identity(*root_snapshot) != identity(*root_after)) {
        return std::nullopt;
    }
    std::ranges::sort(scan.targets, {}, [](const auto& t) { return t.source_link_relative_path.native(); });
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
            left.eligible != right.eligible) {
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

[[nodiscard]] bool exact_review_artifact_set(
    const std::filesystem::path& review_root,
    const std::size_t target_count,
    const bool approval_expected) noexcept
{
    try {
        std::set<std::wstring> expected{
            std::wstring{kStockExternalReviewRequestLeaf},
            std::wstring{kStockExternalReviewSummaryLeaf}};
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
        bool valid = true;
        for (;;) {
            const std::wstring_view name{data.cFileName};
            if (name != L"." && name != L".." &&
                !capability_lock_leaf(name)) {
                if (!expected.contains(std::wstring{name}) ||
                    !observed.insert(std::wstring{name}).second) {
                    valid = false;
                }
            }
            if (::FindNextFileW(raw, &data) == FALSE) {
                if (::GetLastError() != ERROR_NO_MORE_FILES) valid = false;
                break;
            }
        }
        static_cast<void>(::FindClose(raw));
        return valid && observed == expected;
    } catch (...) {
        return false;
    }
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

} // namespace

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

StockExternalReviewResult<StockExternalReviewSummary> review_stock_external_targets(
    const std::filesystem::path& source, const std::filesystem::path& parent,
    const StockResearchCopyLimits& limits) noexcept
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
        if (!scan || !same_scan(*initial_scan, *scan)) {
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
        std::vector<Handle> overlap_targets;
        overlap_targets.reserve(scan->targets.size());
        for (const auto& target : scan->targets) {
            auto target_handle =
                open_path_rename_guard(target.target_root, true, true);
            const auto target_snapshot =
                target_handle.valid() ? snapshot(target_handle.get())
                                      : std::nullopt;
            const auto target_path =
                target_handle.valid() ? physical_path(target_handle.get())
                                      : std::nullopt;
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
            overlap_targets.push_back(std::move(target_handle));
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
        request.implementation_profile = kExternalReviewImplementationProfile;
        request.target_count = scan->targets.size();
        const auto request_json =
            serialize_stock_external_review_request(request);
        if (!request_json) {
            return {{}, StockExternalReviewErrorCode::publication_failed, 0U};
        }
        const auto request_write = secure_atomic_write_new(
            *review_output.directory, kStockExternalReviewRequestLeaf,
            as_bytes(*request_json.value));
        if (!request_write) {
            return {{}, StockExternalReviewErrorCode::publication_failed,
                    request_write.error
                        ? request_write.error->operating_system_error
                        : 0U};
        }

        StockExternalReviewSummaryArtifact summary_artifact{};
        summary_artifact.review_root_fingerprint =
            request.review_root_fingerprint;
        summary_artifact.source_root_fingerprint = scan->source_identity;
        summary_artifact.source_inventory = source_inventory;
        summary_artifact.review_nonce = review_nonce;
        summary_artifact.review_timestamp_unix_seconds = review_timestamp;
        summary_artifact.implementation_profile =
            kExternalReviewImplementationProfile;
        for (std::size_t index = 0U; index < scan->targets.size(); ++index) {
            const auto& target = scan->targets[index];
            const auto link_path = source / target.source_link_relative_path;
            auto link = open_path(link_path, true, true);
            auto target_root = open_path(target.target_root, true, true);
            const auto link_snapshot =
                link.valid() ? snapshot(link.get()) : std::nullopt;
            const auto target_snapshot =
                target_root.valid() ? snapshot(target_root.get())
                                    : std::nullopt;
            const auto link_identity =
                link_snapshot ? artifact_identity(link.get(), *link_snapshot)
                              : std::nullopt;
            const auto target_identity = target_snapshot
                                             ? artifact_identity(
                                                   target_root.get(),
                                                   *target_snapshot)
                                             : std::nullopt;
            const auto relative_utf8 =
                utf8(target.source_link_relative_path.generic_wstring());
            const auto target_utf8 = utf8(target.target_root.native());
            if (!link_snapshot || !target_snapshot || !link_identity ||
                !target_identity || !relative_utf8 || !target_utf8 ||
                link_identity->identity_sha256 !=
                    target.link_identity_sha256 ||
                target_identity->identity_sha256 !=
                    target.target_identity_sha256 ||
                link_snapshot->reparse_tag == 0U) {
                return {{}, StockExternalReviewErrorCode::target_changed,
                        ERROR_FILE_INVALID};
            }
            StockExternalPrivateTargetArtifact private_target{};
            private_target.ordinal = index + 1U;
            private_target.review_nonce = review_nonce;
            private_target.source_root_fingerprint = scan->source_identity;
            private_target.source_link_relative_path = *relative_utf8;
            private_target.source_link_identity = *link_identity;
            private_target.target_canonical_path = *target_utf8;
            private_target.target_identity = *target_identity;
            private_target.target_inventory = artifact_inventory(
                target.entry_count, target.byte_count,
                target.target_inventory_sha256, target.executable_count,
                target.script_or_command_count, target.mutable_state_count,
                target.nested_link_count);
            private_target.classification =
                artifact_classification(target.classification);
            private_target.eligible = target.eligible;
            const auto private_json =
                serialize_stock_external_private_target(private_target);
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
            const auto private_write = secure_atomic_write_new(
                *review_output.directory, *private_leaf,
                as_bytes(*private_json.value));
            if (!private_write) {
                return {{}, StockExternalReviewErrorCode::publication_failed,
                        private_write.error
                            ? private_write.error->operating_system_error
                            : 0U};
            }
            summary_artifact.targets.push_back(
                StockExternalReviewTargetBindingArtifact{
                    index + 1U, *private_digest.value,
                    target.link_identity_sha256,
                    target.target_identity_sha256,
                    target.target_inventory_sha256,
                    artifact_classification(target.classification),
                    target.eligible});
            if (target.eligible) {
                ++summary_artifact.eligible_count;
            } else {
                ++summary_artifact.ineligible_count;
            }
            if (target.classification ==
                StockExternalTargetClassification::unknown) {
                ++summary_artifact.unknown_count;
            }
            if (target.executable_count != 0U) {
                ++summary_artifact.executable_target_count;
            }
            if (target.mutable_state_count != 0U) {
                ++summary_artifact.mutable_state_target_count;
            }
        }
        summary_artifact.all_targets_eligible = scan->eligible;
        const auto summary_json =
            serialize_stock_external_review_summary(summary_artifact);
        if (!summary_json) {
            return {{}, StockExternalReviewErrorCode::publication_failed, 0U};
        }
        const auto summary_digest =
            stock_external_artifact_sha256(*summary_json.value);
        if (!summary_digest) {
            return {{}, StockExternalReviewErrorCode::publication_failed, 0U};
        }
        const auto summary_write = secure_atomic_write_new(
            *review_output.directory, kStockExternalReviewSummaryLeaf,
            as_bytes(*summary_json.value));
        if (!summary_write) {
            return {{}, StockExternalReviewErrorCode::publication_failed,
                    summary_write.error
                        ? summary_write.error->operating_system_error
                        : 0U};
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
            summary.executable_count += target.executable_count;
            summary.script_or_command_count += target.script_or_command_count;
            summary.mutable_state_count += target.mutable_state_count;
            summary.nested_link_count += target.nested_link_count;
        }
        summary.all_targets_eligible = scan->eligible;
        return {std::move(summary), StockExternalReviewErrorCode::none, 0U};
    } catch (...) { return {{}, StockExternalReviewErrorCode::topology_read_failed, 0U}; }
}

StockExternalReviewResult<StockExternalApproval> approve_stock_external_target_review(
    const std::filesystem::path& root, const std::string_view phrase,
    const std::chrono::hours lifetime) noexcept
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
        const auto request =
            parse_stock_external_review_request(*request_text.value);
        const auto summary =
            parse_stock_external_review_summary(*summary_text.value);
        const auto summary_digest =
            stock_external_artifact_sha256(*summary_text.value);
        if (!request || !summary || !summary_digest ||
            request.value->review_root_fingerprint !=
                summary.value->review_root_fingerprint ||
            request.value->source_root_identity.identity_sha256 !=
                summary.value->source_root_fingerprint ||
            request.value->source_inventory !=
                summary.value->source_inventory ||
            request.value->review_nonce != summary.value->review_nonce ||
            request.value->review_timestamp_unix_seconds !=
                summary.value->review_timestamp_unix_seconds ||
            request.value->implementation_profile !=
                summary.value->implementation_profile ||
            request.value->target_count != summary.value->targets.size() ||
            !summary.value->all_targets_eligible ||
            summary.value->targets.empty()) {
            return {{}, summary && !summary.value->all_targets_eligible
                            ? StockExternalReviewErrorCode::target_ineligible
                            : StockExternalReviewErrorCode::
                                  review_manifest_invalid,
                    0U};
        }
        if (!exact_review_artifact_set(
                root, summary.value->targets.size(), false)) {
            return {{}, StockExternalReviewErrorCode::review_manifest_invalid,
                    ERROR_INVALID_DATA};
        }

        StockExternalApprovalArtifact approval_artifact{};
        approval_artifact.review_schema =
            kStockExternalReviewSummarySchemaV1;
        approval_artifact.review_version = 1U;
        approval_artifact.review_root_fingerprint =
            summary.value->review_root_fingerprint;
        approval_artifact.review_digest_sha256 = *summary_digest.value;
        approval_artifact.source_root_fingerprint =
            summary.value->source_root_fingerprint;
        approval_artifact.source_inventory = summary.value->source_inventory;
        approval_artifact.review_nonce = summary.value->review_nonce;
        approval_artifact.confirmation_profile =
            kStockExternalApprovalConfirmationProfileV1;
        approval_artifact.implementation_profile =
            summary.value->implementation_profile;
        for (std::size_t index = 0U;
             index < summary.value->targets.size(); ++index) {
            const auto& binding = summary.value->targets[index];
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
                                            ? parse_stock_external_private_target(
                                                  *private_text.value)
                                            : StockExternalArtifactResult<
                                                  StockExternalPrivateTargetArtifact>{};
            if (!private_text || !private_digest || !private_target ||
                *private_digest.value != binding.private_record_sha256 ||
                private_target.value->ordinal != binding.ordinal ||
                private_target.value->review_nonce !=
                    summary.value->review_nonce ||
                private_target.value->source_root_fingerprint !=
                    summary.value->source_root_fingerprint ||
                private_target.value->source_link_identity.identity_sha256 !=
                    binding.link_identity_sha256 ||
                private_target.value->target_identity.identity_sha256 !=
                    binding.target_identity_sha256 ||
                private_target.value->target_inventory.inventory_sha256 !=
                    binding.target_inventory_sha256 ||
                private_target.value->classification !=
                    binding.classification ||
                !private_target.value->eligible) {
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
        const auto written = secure_atomic_write_new(
            *output.directory, kStockExternalApprovalLeaf,
            as_bytes(*serialized.value));
        if (!written) return {{}, StockExternalReviewErrorCode::publication_failed,
                             written.error ? written.error->operating_system_error : 0U};
        if (!exact_review_artifact_set(
                root, summary.value->targets.size(), true)) {
            return {{}, StockExternalReviewErrorCode::publication_failed,
                    ERROR_INVALID_DATA};
        }
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
        const auto request =
            parse_stock_external_review_request(*request_text.value);
        const auto summary =
            parse_stock_external_review_summary(*summary_text.value);
        const auto approved =
            parse_stock_external_approval(*approval_text.value);
        const auto summary_digest =
            stock_external_artifact_sha256(*summary_text.value);
        const auto approval_digest =
            stock_external_artifact_sha256(*approval_text.value);
        const auto now = unix_now();
        if (!request || !summary || !approved || !summary_digest ||
            !approval_digest ||
            request.value->review_root_fingerprint !=
                summary.value->review_root_fingerprint ||
            request.value->review_root_fingerprint !=
                approved.value->review_root_fingerprint ||
            request.value->source_root_identity.identity_sha256 !=
                summary.value->source_root_fingerprint ||
            summary.value->source_root_fingerprint !=
                approved.value->source_root_fingerprint ||
            request.value->source_inventory !=
                summary.value->source_inventory ||
            summary.value->source_inventory !=
                approved.value->source_inventory ||
            request.value->review_nonce != summary.value->review_nonce ||
            summary.value->review_nonce != approved.value->review_nonce ||
            request.value->implementation_profile !=
                summary.value->implementation_profile ||
            summary.value->implementation_profile !=
                approved.value->implementation_profile ||
            approved.value->review_digest_sha256 !=
                *summary_digest.value ||
            approved.value->approval_timestamp_unix_seconds <
                request.value->review_timestamp_unix_seconds ||
            approved.value->approval_timestamp_unix_seconds > now + 300U ||
            request.value->target_count != summary.value->targets.size() ||
            summary.value->targets.size() !=
                approved.value->approved_targets.size() ||
            !summary.value->all_targets_eligible ||
            !exact_review_artifact_set(
                root, summary.value->targets.size(), true)) {
            return {{}, StockExternalReviewErrorCode::approval_mismatch, 0U};
        }
        if (approved.value->expiration_unix_seconds <= now) {
            return {{}, StockExternalReviewErrorCode::approval_expired, 0U};
        }
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
            fresh->targets.size() != summary.value->targets.size() ||
            fresh->source_identity !=
                approved.value->source_root_fingerprint ||
            artifact_inventory(
                fresh->source_entry_count, fresh->source_byte_count,
                fresh->source_inventory_sha256) !=
                approved.value->source_inventory ||
            approved.value->implementation_profile !=
                kExternalReviewImplementationProfile) {
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
            const auto& summary_binding = summary.value->targets[index];
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
                                            ? parse_stock_external_private_target(
                                                  *private_text.value)
                                            : StockExternalArtifactResult<
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
                private_target.value->ordinal != index + 1U ||
                private_target.value->review_nonce !=
                    approved.value->review_nonce ||
                private_target.value->source_root_fingerprint !=
                    approved.value->source_root_fingerprint ||
                private_target.value->source_link_relative_path !=
                    *relative_utf8 ||
                private_target.value->target_canonical_path != *target_utf8 ||
                private_target.value->source_link_identity.identity_sha256 !=
                    target.link_identity_sha256 ||
                private_target.value->target_identity.identity_sha256 !=
                    target.target_identity_sha256 ||
                private_target.value->target_inventory != target_inventory ||
                private_target.value->classification != classification ||
                !private_target.value->eligible ||
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
        return {std::move(result), StockExternalReviewErrorCode::none, 0U};
    } catch (...) { return {{}, StockExternalReviewErrorCode::review_manifest_invalid, 0U}; }
}

} // namespace hlclient::platform::windows
