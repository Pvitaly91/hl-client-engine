#include <hlclient/platform/windows/windows_reparse_provenance.hpp>

#include <hlclient/hash/sha256.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>
#include <winioctl.h>
#include <winternl.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <limits>
#include <utility>
#include <vector>

namespace hlclient::platform::windows {
namespace {

static_assert(sizeof(wchar_t) == 2U,
              "Windows reparse payloads contain 16-bit UTF-16 code units");

constexpr std::size_t kCommonHeaderBytes = 8U;
constexpr std::size_t kGuidHeaderBytes = 24U;
constexpr std::size_t kMountPointFixedPayloadBytes = 8U;
constexpr std::size_t kSymbolicLinkFixedPayloadBytes = 12U;
constexpr std::uint32_t kMicrosoftBit = 0x80000000U;
constexpr std::uint32_t kReservedBit = 0x40000000U;
constexpr std::uint32_t kNameSurrogateBit = 0x20000000U;
constexpr std::uint32_t kDirectoryBit = 0x10000000U;
constexpr std::uint32_t kReservedTagBits = 0x0FFF0000U;
constexpr std::uint32_t kSymbolicLinkRelativeFlag = 0x00000001U;

constexpr std::uint32_t kTagMountPoint = 0xA0000003U;
constexpr std::uint32_t kTagHsm = 0xC0000004U;
constexpr std::uint32_t kTagHsm2 = 0x80000006U;
constexpr std::uint32_t kTagSis = 0x80000007U;
constexpr std::uint32_t kTagDfs = 0x8000000AU;
constexpr std::uint32_t kTagSymbolicLink = 0xA000000CU;
constexpr std::uint32_t kTagDfsr = 0x80000012U;
constexpr std::uint32_t kTagDedup = 0x80000013U;
constexpr std::uint32_t kTagWof = 0x80000017U;
constexpr std::uint32_t kTagWci = 0x80000018U;
constexpr std::uint32_t kTagWci1 = 0x90001018U;
constexpr std::uint32_t kTagCloud = 0x9000001AU;
constexpr std::uint32_t kTagAppExecLink = 0x8000001BU;
constexpr std::uint32_t kTagProjFs = 0x9000001CU;
constexpr std::uint32_t kTagWciTombstone = 0xA000001FU;
constexpr std::uint32_t kTagProjFsTombstone = 0xA0000022U;
constexpr std::uint32_t kTagReservedInvalid = 0xC0008000U;

class Handle final {
public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) noexcept
        : value_(value)
    {
    }

    ~Handle()
    {
        if (valid()) {
            static_cast<void>(::CloseHandle(value_));
        }
    }

    Handle(Handle&& other) noexcept : value_(other.value_)
    {
        other.value_ = INVALID_HANDLE_VALUE;
    }

    Handle& operator=(Handle&& other) noexcept
    {
        if (this != &other) {
            if (valid()) {
                static_cast<void>(::CloseHandle(value_));
            }
            value_ = other.value_;
            other.value_ = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    [[nodiscard]] bool valid() const noexcept
    {
        return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
    }

    [[nodiscard]] HANDLE get() const noexcept { return value_; }

private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

struct SourceLinkSnapshot final {
    std::uint64_t volume_serial{0U};
    std::array<std::byte, 16U> file_id{};
    std::uint64_t creation_time{0U};
    std::uint64_t last_write_time{0U};
    std::uint64_t change_time{0U};
    std::uint32_t attributes{0U};
    std::uint32_t reparse_tag{0U};
};

struct ReadOnce final {
    WindowsReparseProvenance provenance;
    SourceLinkSnapshot snapshot;
};

[[nodiscard]] bool checked_range(
    const std::size_t offset,
    const std::size_t length,
    const std::size_t available) noexcept
{
    return offset <= available && length <= available - offset;
}

[[nodiscard]] std::optional<std::uint16_t> read_u16(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept
{
    if (!checked_range(offset, 2U, bytes.size())) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(
             std::to_integer<std::uint8_t>(bytes[offset + 1U]))
         << 8U));
}

[[nodiscard]] std::optional<std::uint32_t> read_u32(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept
{
    if (!checked_range(offset, 4U, bytes.size())) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(
        std::to_integer<std::uint8_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(bytes[offset + 1U]))
         << 8U) |
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(bytes[offset + 2U]))
         << 16U) |
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(bytes[offset + 3U]))
         << 24U));
}

[[nodiscard]] bool ranges_overlap(
    const std::size_t left_offset,
    const std::size_t left_length,
    const std::size_t right_offset,
    const std::size_t right_length) noexcept
{
    if (left_length == 0U || right_length == 0U) {
        return false;
    }
    return left_offset < right_offset + right_length &&
           right_offset < left_offset + left_length;
}

[[nodiscard]] bool valid_utf16(const std::wstring_view text) noexcept
{
    for (std::size_t index = 0U; index < text.size(); ++index) {
        const auto unit = static_cast<std::uint16_t>(text[index]);
        if (unit == 0U) {
            return false;
        }
        if (unit >= 0xD800U && unit <= 0xDBFFU) {
            if (index + 1U >= text.size()) {
                return false;
            }
            const auto next = static_cast<std::uint16_t>(text[index + 1U]);
            if (next < 0xDC00U || next > 0xDFFFU) {
                return false;
            }
            ++index;
        } else if (unit >= 0xDC00U && unit <= 0xDFFFU) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool decode_utf16(
    const std::span<const std::byte> path_buffer,
    const std::size_t byte_offset,
    const std::size_t byte_length,
    std::wstring& output)
{
    if ((byte_offset & 1U) != 0U || (byte_length & 1U) != 0U ||
        !checked_range(byte_offset, byte_length, path_buffer.size())) {
        return false;
    }

    output.clear();
    output.reserve(byte_length / 2U);
    for (std::size_t index = 0U; index < byte_length; index += 2U) {
        const auto unit = read_u16(path_buffer, byte_offset + index);
        if (!unit) {
            return false;
        }
        output.push_back(static_cast<wchar_t>(*unit));
    }
    return valid_utf16(output);
}

[[nodiscard]] bool ascii_iequal(
    const wchar_t left,
    const wchar_t right) noexcept
{
    return std::towupper(left) == std::towupper(right);
}

[[nodiscard]] bool istarts_with(
    const std::wstring_view value,
    const std::wstring_view prefix) noexcept
{
    return value.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), value.begin(),
                      [](const wchar_t left, const wchar_t right) {
                          return ascii_iequal(left, right);
                      });
}

[[nodiscard]] bool drive_absolute(
    const std::wstring_view value) noexcept
{
    return value.size() >= 3U &&
           ((value[0] >= L'A' && value[0] <= L'Z') ||
            (value[0] >= L'a' && value[0] <= L'z')) &&
           value[1] == L':' &&
           (value[2] == L'\\' || value[2] == L'/');
}

[[nodiscard]] bool valid_unc_tail(const std::wstring_view value) noexcept
{
    const auto first = value.find_first_of(L"\\/");
    return first != std::wstring_view::npos && first > 0U &&
           first + 1U < value.size() &&
           value.find_first_of(L"\\/", first + 1U) != first + 1U;
}

[[nodiscard]] bool hex_digit(const wchar_t value) noexcept
{
    return (value >= L'0' && value <= L'9') ||
           (value >= L'a' && value <= L'f') ||
           (value >= L'A' && value <= L'F');
}

[[nodiscard]] bool valid_volume_guid_tail(
    const std::wstring_view tail) noexcept
{
    // Volume{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx} followed by a separator
    // or the end of the expression.
    constexpr std::size_t guid_characters = 36U;
    constexpr std::array<std::size_t, 4U> hyphens{8U, 13U, 18U, 23U};
    constexpr std::wstring_view prefix = L"Volume{";
    if (!istarts_with(tail, prefix) ||
        tail.size() < prefix.size() + guid_characters + 1U) {
        return false;
    }
    const auto guid = tail.substr(prefix.size(), guid_characters);
    for (std::size_t index = 0U; index < guid.size(); ++index) {
        if (std::find(hyphens.begin(), hyphens.end(), index) !=
            hyphens.end()) {
            if (guid[index] != L'-') {
                return false;
            }
        } else if (!hex_digit(guid[index])) {
            return false;
        }
    }
    const auto close = prefix.size() + guid_characters;
    if (tail[close] != L'}') {
        return false;
    }
    return tail.size() == close + 1U || tail[close + 1U] == L'\\' ||
           tail[close + 1U] == L'/';
}

[[nodiscard]] bool contains_forbidden_path_character(
    const std::wstring_view value) noexcept
{
    return std::ranges::any_of(value, [](const wchar_t unit) {
        return unit == L'\0' ||
               (static_cast<unsigned int>(unit) < 0x20U && unit != L'\t');
    });
}

[[nodiscard]] std::wstring normalize_private_expression(
    const std::wstring_view expression,
    const WindowsReparseTargetExpressionKind kind)
{
    std::wstring output{expression};
    std::ranges::replace(output, L'/', L'\\');

    if (kind == WindowsReparseTargetExpressionKind::drive_absolute_path &&
        istarts_with(output, L"\\\\?\\") && output.size() > 4U) {
        output.erase(0U, 4U);
    } else if (kind == WindowsReparseTargetExpressionKind::unc_path) {
        constexpr std::wstring_view nt_unc = L"\\??\\UNC\\";
        constexpr std::wstring_view win32_unc = L"\\\\?\\UNC\\";
        if (istarts_with(output, nt_unc)) {
            output = L"\\\\" + output.substr(nt_unc.size());
        } else if (istarts_with(output, win32_unc)) {
            output = L"\\\\" + output.substr(win32_unc.size());
        }
    }
    return output;
}

[[nodiscard]] Handle open_no_follow(
    const std::filesystem::path& path) noexcept
{
    return Handle{::CreateFileW(
        path.c_str(), 0U,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
}

[[nodiscard]] Handle open_no_follow_pinned(
    const std::filesystem::path& path,
    const bool inventory_access = false) noexcept
{
    // Omitting FILE_SHARE_DELETE keeps every already-inspected component
    // pinned while the next component is opened.  FILE_FLAG_OPEN_REPARSE_POINT
    // guarantees that no component accepted by this resolver is followed.
    return Handle{::CreateFileW(
        path.c_str(), FILE_READ_ATTRIBUTES | SYNCHRONIZE |
                          (inventory_access ? FILE_READ_DATA : 0U),
        inventory_access ? FILE_SHARE_READ
                         : FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
}

[[nodiscard]] Handle open_relative_no_follow_pinned(
    HANDLE parent,
    const std::wstring_view leaf,
    const bool directory_required,
    const bool inventory_access,
    std::uint32_t& native_error) noexcept
{
    if (parent == nullptr || parent == INVALID_HANDLE_VALUE || leaf.empty() ||
        leaf.size() >
            (std::numeric_limits<USHORT>::max)() / sizeof(wchar_t) ||
        leaf.find_first_of(L"\\/") != std::wstring_view::npos) {
        native_error = ERROR_INVALID_NAME;
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
    ULONG options = FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT |
                    FILE_SYNCHRONOUS_IO_NONALERT;
    if (directory_required) options |= FILE_DIRECTORY_FILE;
    const NTSTATUS status = ::NtCreateFile(
        &opened, FILE_READ_ATTRIBUTES | SYNCHRONIZE |
                     (inventory_access ? FILE_READ_DATA : 0U),
        &attributes,
        &status_block, nullptr, FILE_ATTRIBUTE_NORMAL,
        inventory_access ? FILE_SHARE_READ
                         : FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_OPEN, options, nullptr, 0U);
    if (status < 0) {
        if (opened != nullptr && opened != INVALID_HANDLE_VALUE) {
            static_cast<void>(::CloseHandle(opened));
        }
        native_error = ::RtlNtStatusToDosError(status);
        return Handle{};
    }
    native_error = ERROR_SUCCESS;
    return Handle{opened};
}

[[nodiscard]] bool ordinary_directory_handle(HANDLE handle) noexcept
{
    FILE_ATTRIBUTE_TAG_INFO tag{};
    FILE_STANDARD_INFO standard{};
    return ::GetFileInformationByHandleEx(
               handle, FileAttributeTagInfo, &tag, sizeof(tag)) != FALSE &&
           ::GetFileInformationByHandleEx(
               handle, FileStandardInfo, &standard, sizeof(standard)) !=
               FALSE &&
           (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U &&
           standard.Directory != FALSE;
}

[[nodiscard]] bool drive_letter_is_subst(
    const std::wstring_view root) noexcept
{
    if (root.size() < 2U || root[1] != L':') {
        return false;
    }
    const std::array<wchar_t, 3U> device_name{root[0], L':', L'\0'};
    std::array<wchar_t, 32'768U> target{};
    const DWORD written = ::QueryDosDeviceW(
        device_name.data(), target.data(), static_cast<DWORD>(target.size()));
    if (written == 0U) {
        return true;
    }
    // SUBST mappings are DOS-device aliases to another DOS path. Ordinary
    // fixed drives map to a kernel device such as \\Device\\HarddiskVolumeN.
    return istarts_with(std::wstring_view{target.data()}, L"\\??\\");
}

struct ResolvedLocalTarget final {
    std::filesystem::path root;
    std::vector<std::wstring> components;
};

enum class TargetRootDisposition {
    local_fixed_or_relative,
    missing_volume,
    remote_or_device,
};

[[nodiscard]] bool may_follow(
    const WindowsReparseTargetExpression& expression) noexcept;

[[nodiscard]] TargetRootDisposition target_root_disposition(
    const WindowsReparseTargetExpression& expression) noexcept;

[[nodiscard]] std::optional<std::wstring> full_dos_path(
    const std::filesystem::path& value)
{
    const DWORD required = ::GetFullPathNameW(value.c_str(), 0U, nullptr,
                                               nullptr);
    if (required == 0U ||
        required >
            kWindowsReparseHardMaximumTargetExpressionCharacters + 1U) {
        return std::nullopt;
    }
    std::wstring buffer(static_cast<std::size_t>(required), L'\0');
    const DWORD written = ::GetFullPathNameW(
        value.c_str(), required, buffer.data(), nullptr);
    if (written == 0U || written >= required) {
        return std::nullopt;
    }
    buffer.resize(written);
    std::ranges::replace(buffer, L'/', L'\\');
    return buffer;
}

[[nodiscard]] std::optional<ResolvedLocalTarget> resolve_local_target(
    const std::filesystem::path& source_link,
    const WindowsReparseTargetExpression& expression)
{
    std::wstring absolute;
    if (expression.kind ==
        WindowsReparseTargetExpressionKind::relative_path) {
        auto combined = source_link.parent_path() /
                        std::filesystem::path{
                            expression.private_normalized_expression};
        auto resolved = full_dos_path(combined);
        if (!resolved) return std::nullopt;
        absolute = std::move(*resolved);
    } else if (expression.kind ==
               WindowsReparseTargetExpressionKind::drive_absolute_path) {
        auto resolved = full_dos_path(
            std::filesystem::path{expression.private_normalized_expression});
        if (!resolved) return std::nullopt;
        absolute = std::move(*resolved);
    } else if (expression.kind ==
               WindowsReparseTargetExpressionKind::nt_object_manager_path) {
        constexpr std::wstring_view prefix = L"\\??\\";
        if (!istarts_with(expression.private_expression, prefix)) {
            return std::nullopt;
        }
        const auto tail = std::wstring_view{expression.private_expression}
                              .substr(prefix.size());
        if (!drive_absolute(tail)) return std::nullopt;
        auto resolved = full_dos_path(std::filesystem::path{tail});
        if (!resolved) return std::nullopt;
        absolute = std::move(*resolved);
    } else if (expression.kind ==
               WindowsReparseTargetExpressionKind::volume_guid_path) {
        absolute = expression.private_expression;
        constexpr std::wstring_view nt_prefix = L"\\??\\";
        if (istarts_with(absolute, nt_prefix)) {
            absolute = L"\\\\?\\" + absolute.substr(nt_prefix.size());
        }
        std::ranges::replace(absolute, L'/', L'\\');
    } else {
        return std::nullopt;
    }

    std::wstring root;
    std::size_t component_offset = 0U;
    if (drive_absolute(absolute)) {
        root.assign(absolute.begin(), absolute.begin() + 3);
        component_offset = 3U;
        if (drive_letter_is_subst(root) ||
            ::GetDriveTypeW(root.c_str()) != DRIVE_FIXED) {
            return std::nullopt;
        }
        // Resolve the mutable DOS drive-letter mapping once, then traverse the
        // stable local volume GUID namespace. A drive-letter remap after this
        // point therefore cannot redirect any CreateFile/NtCreateFile call to
        // a network or device target.
        std::array<wchar_t, MAX_PATH + 1U> volume_name{};
        if (::GetVolumeNameForVolumeMountPointW(
                root.c_str(), volume_name.data(),
                static_cast<DWORD>(volume_name.size())) == FALSE) {
            return std::nullopt;
        }
        root.assign(volume_name.data());
        if (!istarts_with(root, L"\\\\?\\Volume{") ||
            !valid_volume_guid_tail(std::wstring_view{root}.substr(4U)) ||
            ::GetDriveTypeW(root.c_str()) != DRIVE_FIXED) {
            return std::nullopt;
        }
    } else if (istarts_with(absolute, L"\\\\?\\Volume{")) {
        const auto close = absolute.find(L'}');
        if (close == std::wstring::npos ||
            (close + 1U < absolute.size() &&
             absolute[close + 1U] != L'\\')) {
            return std::nullopt;
        }
        if (close + 1U == absolute.size()) {
            root = absolute;
            root.push_back(L'\\');
            component_offset = absolute.size();
        } else {
            root = absolute.substr(0U, close + 2U);
            component_offset = close + 2U;
        }
        if (::GetDriveTypeW(root.c_str()) != DRIVE_FIXED) {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }

    ResolvedLocalTarget result{};
    result.root = std::filesystem::path{root};
    std::size_t begin = component_offset;
    while (begin < absolute.size()) {
        while (begin < absolute.size() && absolute[begin] == L'\\') ++begin;
        if (begin == absolute.size()) break;
        const auto end = absolute.find(L'\\', begin);
        const auto count = end == std::wstring::npos
                               ? absolute.size() - begin
                               : end - begin;
        auto component = absolute.substr(begin, count);
        if (component.empty() || component == L"." || component == L".." ||
            component.find(L':') != std::wstring::npos) {
            return std::nullopt;
        }
        result.components.push_back(std::move(component));
        if (end == std::wstring::npos) break;
        begin = end + 1U;
    }
    return result;
}

struct PinnedTargetOpen final {
    std::vector<Handle> handles;
    std::optional<WindowsReparseNestedFailure> nested_failure;
    std::uint32_t native_error{ERROR_SUCCESS};

    [[nodiscard]] bool valid() const noexcept { return !handles.empty(); }
    [[nodiscard]] HANDLE get() const noexcept
    {
        return valid() ? handles.back().get() : INVALID_HANDLE_VALUE;
    }
};

[[nodiscard]] std::optional<SourceLinkSnapshot> source_snapshot(
    HANDLE handle) noexcept
{
    FILE_ID_INFO id{};
    FILE_BASIC_INFO basic{};
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!::GetFileInformationByHandleEx(handle, FileIdInfo, &id, sizeof(id)) ||
        !::GetFileInformationByHandleEx(
            handle, FileBasicInfo, &basic, sizeof(basic)) ||
        !::GetFileInformationByHandleEx(
            handle, FileAttributeTagInfo, &tag, sizeof(tag))) {
        return std::nullopt;
    }

    SourceLinkSnapshot result{};
    result.volume_serial = id.VolumeSerialNumber;
    std::copy_n(reinterpret_cast<const std::byte*>(id.FileId.Identifier),
                result.file_id.size(), result.file_id.begin());
    result.creation_time =
        static_cast<std::uint64_t>(basic.CreationTime.QuadPart);
    result.last_write_time =
        static_cast<std::uint64_t>(basic.LastWriteTime.QuadPart);
    result.change_time = static_cast<std::uint64_t>(basic.ChangeTime.QuadPart);
    result.attributes = tag.FileAttributes;
    result.reparse_tag = tag.ReparseTag;
    return result;
}

[[nodiscard]] bool same_source_snapshot(
    const SourceLinkSnapshot& left,
    const SourceLinkSnapshot& right) noexcept
{
    return left.volume_serial == right.volume_serial &&
           left.file_id == right.file_id &&
           left.creation_time == right.creation_time &&
           left.last_write_time == right.last_write_time &&
           left.change_time == right.change_time &&
           left.attributes == right.attributes &&
           left.reparse_tag == right.reparse_tag;
}

[[nodiscard]] bool same_file_identity(
    const SourceLinkSnapshot& left,
    const SourceLinkSnapshot& right) noexcept
{
    return left.volume_serial == right.volume_serial &&
           left.file_id == right.file_id;
}

[[nodiscard]] WindowsReparseProvenanceResult<ReadOnce>
read_once_from_pinned_handle(
    HANDLE handle,
    const WindowsReparseProvenanceLimits& limits) noexcept
{
    try {
        if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
            return {{},
                    WindowsReparseProvenanceErrorCode::source_link_open_failed,
                    ERROR_INVALID_HANDLE};
        }
        if (!valid_windows_reparse_provenance_limits(limits)) {
            return {{}, WindowsReparseProvenanceErrorCode::invalid_limits,
                    ERROR_INVALID_PARAMETER};
        }

        const auto before = source_snapshot(handle);
        if (!before) {
            const auto native_error = ::GetLastError();
            return {{},
                    WindowsReparseProvenanceErrorCode::source_link_open_failed,
                    native_error};
        }
        if ((before->attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U ||
            before->reparse_tag == 0U) {
            return {{},
                    WindowsReparseProvenanceErrorCode::source_not_reparse_point,
                    ERROR_NOT_A_REPARSE_POINT};
        }

        std::vector<std::byte> bytes(limits.maximum_reparse_payload_bytes);
        DWORD returned = 0U;
        if (!::DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, nullptr, 0U,
                               bytes.data(),
                               static_cast<DWORD>(bytes.size()), &returned,
                               nullptr)) {
            const auto native_error = ::GetLastError();
            if (native_error == ERROR_MORE_DATA ||
                native_error == ERROR_INSUFFICIENT_BUFFER) {
                return {{}, WindowsReparseProvenanceErrorCode::
                                reparse_payload_limit_exceeded,
                        native_error};
            }
            if (native_error == ERROR_NOT_A_REPARSE_POINT) {
                return {{}, WindowsReparseProvenanceErrorCode::
                                source_not_reparse_point,
                        native_error};
            }
            return {{}, WindowsReparseProvenanceErrorCode::
                            reparse_payload_read_failed,
                    native_error};
        }
        if (returned > bytes.size()) {
            return {{}, WindowsReparseProvenanceErrorCode::
                            reparse_payload_read_failed,
                    ERROR_INVALID_DATA};
        }
        bytes.resize(returned);

        auto decoded = decode_windows_reparse_payload(
            std::span<const std::byte>{bytes.data(), bytes.size()},
            (before->attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U, limits);
        if (!decoded.value) {
            return {{}, decoded.code, decoded.native_error};
        }
        const auto after = source_snapshot(handle);
        if (!after || !same_source_snapshot(*before, *after) ||
            decoded.value->tag.raw_tag != before->reparse_tag) {
            return {{}, WindowsReparseProvenanceErrorCode::source_link_changed,
                    ERROR_REPARSE_TAG_MISMATCH};
        }
        return {ReadOnce{std::move(*decoded.value), *before},
                WindowsReparseProvenanceErrorCode::none, ERROR_SUCCESS};
    } catch (...) {
        return {{}, WindowsReparseProvenanceErrorCode::
                        reparse_payload_read_failed,
                ERROR_NOT_ENOUGH_MEMORY};
    }
}

[[nodiscard]] WindowsReparseProvenanceResult<ReadOnce> read_once(
    const std::filesystem::path& path,
    const WindowsReparseProvenanceLimits& limits) noexcept
{
    try {
        if (path.empty()) {
            return {{}, WindowsReparseProvenanceErrorCode::invalid_argument,
                    ERROR_INVALID_PARAMETER};
        }
        if (!valid_windows_reparse_provenance_limits(limits)) {
            return {{}, WindowsReparseProvenanceErrorCode::invalid_limits,
                    ERROR_INVALID_PARAMETER};
        }

        auto handle = open_no_follow(path);
        if (!handle.valid()) {
            return {{},
                    WindowsReparseProvenanceErrorCode::source_link_open_failed,
                    ::GetLastError()};
        }
        return read_once_from_pinned_handle(handle.get(), limits);
    } catch (...) {
        return {{}, WindowsReparseProvenanceErrorCode::
                        reparse_payload_read_failed,
                ERROR_NOT_ENOUGH_MEMORY};
    }
}

[[nodiscard]] PinnedTargetOpen open_captured_local_target(
    const std::filesystem::path& source_link,
    const WindowsReparseTargetExpression& expression,
    const WindowsReparseProvenanceLimits& limits,
    const SourceLinkSnapshot& source_snapshot_value) noexcept
{
    PinnedTargetOpen result{};
    try {
        std::filesystem::path current_source_link = source_link;
        WindowsReparseTargetExpression current_expression = expression;
        std::vector<std::wstring> suffix;
        std::vector<SourceLinkSnapshot> visited_reparse_points{
            source_snapshot_value};
        std::optional<WindowsReparseNestedFailure> active_nested_failure;
        std::size_t nested_depth = 0U;

        const auto fail = [&](const std::uint32_t native_error) {
            result.handles.clear();
            result.native_error = native_error;
        };
        const auto fail_from_active = [&]
            (const std::uint32_t native_error,
             const WindowsReparseTargetReachability reachability,
             const StockExternalTopologyFailurePhase phase) {
                fail(native_error);
                if (!active_nested_failure) return;
                auto failure = *active_nested_failure;
                failure.reachability = reachability;
                failure.failure_phase = phase;
                failure.native_error = native_error;
                failure.native_error_category =
                    classify_windows_reparse_native_error(native_error);
                result.nested_failure = std::move(failure);
            };
        const auto fail_at_nested = [&]
            (WindowsReparseNestedFailure failure,
             const std::uint32_t outer_native_error,
             const WindowsReparseTargetReachability reachability,
             const StockExternalTopologyFailurePhase phase,
             const std::uint32_t nested_native_error) {
                fail(outer_native_error);
                failure.reachability = reachability;
                failure.failure_phase = phase;
                failure.native_error = nested_native_error;
                failure.native_error_category =
                    classify_windows_reparse_native_error(
                        nested_native_error);
                result.nested_failure = std::move(failure);
            };

        for (;;) {
            if (!may_follow(current_expression)) {
                if (active_nested_failure) {
                    fail_from_active(
                        ERROR_BAD_NETPATH,
                        WindowsReparseTargetReachability::
                            target_remote_or_device,
                        StockExternalTopologyFailurePhase::
                            nested_reparse_decode);
                } else {
                    fail(ERROR_BAD_NETPATH);
                }
                return result;
            }
            switch (target_root_disposition(current_expression)) {
            case TargetRootDisposition::missing_volume:
                if (active_nested_failure) {
                    fail_from_active(
                        ERROR_DEV_NOT_EXIST,
                        WindowsReparseTargetReachability::
                            target_volume_not_found,
                        StockExternalTopologyFailurePhase::nested_entry_open);
                } else {
                    fail(ERROR_DEV_NOT_EXIST);
                }
                return result;
            case TargetRootDisposition::remote_or_device:
                if (active_nested_failure) {
                    fail_from_active(
                        ERROR_BAD_NETPATH,
                        WindowsReparseTargetReachability::
                            target_remote_or_device,
                        StockExternalTopologyFailurePhase::
                            nested_reparse_decode);
                } else {
                    fail(ERROR_BAD_NETPATH);
                }
                return result;
            case TargetRootDisposition::local_fixed_or_relative: break;
            }

            auto resolved =
                resolve_local_target(current_source_link, current_expression);
            if (!resolved) {
                if (active_nested_failure) {
                    fail_from_active(
                        ERROR_INVALID_NAME,
                        WindowsReparseTargetReachability::
                            target_path_not_found,
                        StockExternalTopologyFailurePhase::nested_entry_open);
                } else {
                    fail(ERROR_INVALID_NAME);
                }
                return result;
            }
            resolved->components.insert(resolved->components.end(),
                                        suffix.begin(), suffix.end());
            suffix.clear();

            auto root = open_no_follow_pinned(
                resolved->root, resolved->components.empty());
            if (!root.valid()) {
                const auto native_error = ::GetLastError();
                if (active_nested_failure) {
                    fail_from_active(
                        native_error,
                        classify_windows_reparse_target_reachability(
                            native_error),
                        StockExternalTopologyFailurePhase::nested_entry_open);
                } else {
                    fail(native_error);
                }
                return result;
            }
            if (!ordinary_directory_handle(root.get())) {
                if (active_nested_failure) {
                    fail_from_active(
                        ERROR_REPARSE_POINT_ENCOUNTERED,
                        WindowsReparseTargetReachability::
                            target_open_failed_other,
                        StockExternalTopologyFailurePhase::nested_entry_open);
                } else {
                    fail(ERROR_REPARSE_POINT_ENCOUNTERED);
                }
                return result;
            }
            result.handles.push_back(std::move(root));

            bool redirected = false;
            for (std::size_t index = 0U;
                 index < resolved->components.size(); ++index) {
                const bool last = index + 1U == resolved->components.size();
                auto component = open_relative_no_follow_pinned(
                    result.handles.back().get(), resolved->components[index],
                    !last, last, result.native_error);
                if (!component.valid()) {
                    if (!last && result.native_error == ERROR_FILE_NOT_FOUND) {
                        result.native_error = ERROR_PATH_NOT_FOUND;
                    }
                    if (active_nested_failure) {
                        const auto native_error = result.native_error;
                        fail_from_active(
                            native_error,
                            classify_windows_reparse_target_reachability(
                                native_error),
                            StockExternalTopologyFailurePhase::
                                nested_entry_open);
                    } else {
                        result.handles.clear();
                    }
                    return result;
                }

                FILE_ATTRIBUTE_TAG_INFO tag{};
                if (!::GetFileInformationByHandleEx(
                        component.get(), FileAttributeTagInfo, &tag,
                        sizeof(tag))) {
                    const auto native_error = ::GetLastError();
                    if (active_nested_failure) {
                        fail_from_active(
                            native_error,
                            classify_windows_reparse_target_reachability(
                                native_error),
                            StockExternalTopologyFailurePhase::
                                nested_entry_open);
                    } else {
                        fail(native_error);
                    }
                    return result;
                }
                if ((tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ==
                    0U) {
                    if (!last && !ordinary_directory_handle(component.get())) {
                        if (active_nested_failure) {
                            fail_from_active(
                                ERROR_DIRECTORY,
                                WindowsReparseTargetReachability::
                                    target_not_directory,
                                StockExternalTopologyFailurePhase::
                                    nested_entry_open);
                        } else {
                            fail(ERROR_DIRECTORY);
                        }
                        return result;
                    }
                    result.handles.push_back(std::move(component));
                    continue;
                }

                // Reopen a non-final name surrogate with inventory access and
                // without share-write. This pins its exact payload while it is
                // decoded; the first handle already prevents name replacement.
                Handle pinned_reparse{};
                if (last) {
                    pinned_reparse = std::move(component);
                } else {
                    std::uint32_t reopen_error = ERROR_SUCCESS;
                    pinned_reparse = open_relative_no_follow_pinned(
                        result.handles.back().get(),
                        resolved->components[index],
                        (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U,
                        true, reopen_error);
                    if (!pinned_reparse.valid()) {
                        if (active_nested_failure) {
                            fail_from_active(
                                reopen_error,
                                classify_windows_reparse_target_reachability(
                                    reopen_error),
                                StockExternalTopologyFailurePhase::
                                    nested_entry_open);
                        } else {
                            fail(reopen_error);
                        }
                        return result;
                    }
                }

                std::filesystem::path nested_source_link = resolved->root;
                for (std::size_t prefix = 0U; prefix <= index; ++prefix) {
                    nested_source_link /= resolved->components[prefix];
                }
                WindowsReparseNestedFailure nested_failure{};
                nested_failure.traversal_depth = nested_depth + 1U;
                nested_failure.nested_ordinal = nested_depth + 1U;
                nested_failure.reparse_tag_category =
                    classify_windows_reparse_tag(tag.ReparseTag);
                nested_failure.directory =
                    (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
                nested_failure.private_link_path = nested_source_link;

                const auto nested =
                    read_once_from_pinned_handle(pinned_reparse.get(), limits);
                if (!nested.value) {
                    fail_at_nested(
                        std::move(nested_failure), nested.native_error,
                        classify_windows_reparse_target_reachability(
                            nested.native_error),
                        StockExternalTopologyFailurePhase::
                            nested_reparse_decode,
                        nested.native_error);
                    return result;
                }
                nested_failure.reparse_tag_category =
                    nested.value->provenance.tag.category;
                nested_failure.expression_kind =
                    nested.value->provenance.target_expression.kind;
                nested_failure.directory =
                    nested.value->provenance.tag.directory;
                if (std::ranges::any_of(
                        visited_reparse_points,
                        [&](const SourceLinkSnapshot& visited) {
                            return same_file_identity(visited,
                                                      nested.value->snapshot);
                        })) {
                    fail_at_nested(
                        std::move(nested_failure),
                        ERROR_CIRCULAR_DEPENDENCY,
                        WindowsReparseTargetReachability::target_cycle,
                        StockExternalTopologyFailurePhase::
                            nested_reparse_decode,
                        ERROR_CIRCULAR_DEPENDENCY);
                    return result;
                }
                if (nested_depth >= limits.maximum_nested_reparse_depth) {
                    fail_at_nested(
                        std::move(nested_failure), ERROR_TOO_MANY_LINKS,
                        WindowsReparseTargetReachability::
                            target_depth_exceeded,
                        StockExternalTopologyFailurePhase::
                            nested_reparse_decode,
                        ERROR_TOO_MANY_LINKS);
                    return result;
                }

                const auto& provenance = nested.value->provenance;
                if (!provenance.tag.name_surrogate ||
                    (provenance.tag.category !=
                         WindowsReparseTagCategory::mount_point &&
                     provenance.tag.category !=
                         WindowsReparseTagCategory::symbolic_link) ||
                    provenance.payload_status !=
                        WindowsReparsePayloadStatus::path_contract_decoded) {
                    fail_at_nested(
                        std::move(nested_failure), ERROR_NOT_SUPPORTED,
                        WindowsReparseTargetReachability::not_applicable,
                        StockExternalTopologyFailurePhase::
                            nested_reparse_decode,
                        ERROR_SUCCESS);
                    return result;
                }
                if (!last && !provenance.tag.directory) {
                    fail_at_nested(
                        std::move(nested_failure), ERROR_DIRECTORY,
                        WindowsReparseTargetReachability::
                            target_not_directory,
                        StockExternalTopologyFailurePhase::
                            nested_reparse_decode,
                        ERROR_DIRECTORY);
                    return result;
                }

                if (!may_follow(provenance.target_expression)) {
                    fail_at_nested(
                        std::move(nested_failure), ERROR_BAD_NETPATH,
                        WindowsReparseTargetReachability::
                            target_remote_or_device,
                        StockExternalTopologyFailurePhase::
                            nested_reparse_decode,
                        ERROR_SUCCESS);
                    return result;
                }
                const auto nested_root =
                    target_root_disposition(provenance.target_expression);
                if (nested_root == TargetRootDisposition::missing_volume) {
                    fail_at_nested(
                        std::move(nested_failure), ERROR_DEV_NOT_EXIST,
                        WindowsReparseTargetReachability::
                            target_volume_not_found,
                        StockExternalTopologyFailurePhase::nested_entry_open,
                        ERROR_DEV_NOT_EXIST);
                    return result;
                }
                if (nested_root == TargetRootDisposition::remote_or_device) {
                    fail_at_nested(
                        std::move(nested_failure), ERROR_BAD_NETPATH,
                        WindowsReparseTargetReachability::
                            target_remote_or_device,
                        StockExternalTopologyFailurePhase::
                            nested_reparse_decode,
                        ERROR_SUCCESS);
                    return result;
                }
                suffix.assign(resolved->components.begin() +
                                  static_cast<std::ptrdiff_t>(index + 1U),
                              resolved->components.end());
                visited_reparse_points.push_back(nested.value->snapshot);
                ++nested_depth;
                current_source_link = std::move(nested_source_link);
                current_expression = provenance.target_expression;
                active_nested_failure = std::move(nested_failure);
                result.handles.push_back(std::move(pinned_reparse));
                redirected = true;
                break;
            }

            if (redirected) continue;
            result.native_error = ERROR_SUCCESS;
            return result;
        }
    } catch (...) {
        result.handles.clear();
        result.native_error = ERROR_NOT_ENOUGH_MEMORY;
        return result;
    }
}

[[nodiscard]] std::optional<std::filesystem::path> final_handle_path(
    HANDLE handle)
{
    const DWORD required = ::GetFinalPathNameByHandleW(
        handle, nullptr, 0U, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (required == 0U ||
        required >
            kWindowsReparseHardMaximumTargetExpressionCharacters + 1U) {
        return std::nullopt;
    }
    std::wstring buffer(static_cast<std::size_t>(required), L'\0');
    const DWORD written = ::GetFinalPathNameByHandleW(
        handle, buffer.data(), required,
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (written == 0U || written >= required) {
        return std::nullopt;
    }
    buffer.resize(written);
    return std::filesystem::path{std::move(buffer)};
}

[[nodiscard]] std::optional<WindowsReparseTargetIdentity> target_identity(
    HANDLE handle)
{
    FILE_ID_INFO id{};
    FILE_STANDARD_INFO standard{};
    if (!::GetFileInformationByHandleEx(handle, FileIdInfo, &id, sizeof(id)) ||
        !::GetFileInformationByHandleEx(
            handle, FileStandardInfo, &standard, sizeof(standard))) {
        return std::nullopt;
    }
    auto path = final_handle_path(handle);
    if (!path) {
        return std::nullopt;
    }

    WindowsReparseTargetIdentity result{};
    result.volume_serial = id.VolumeSerialNumber;
    std::copy_n(reinterpret_cast<const std::byte*>(id.FileId.Identifier),
                result.file_id.size(), result.file_id.begin());
    result.private_final_handle_path = std::move(*path);
    result.directory = standard.Directory != FALSE;
    return result;
}

[[nodiscard]] bool same_target_identity(
    const WindowsReparseTargetIdentity& left,
    const WindowsReparseTargetIdentity& right) noexcept
{
    return left.volume_serial == right.volume_serial &&
           left.file_id == right.file_id && left.directory == right.directory;
}

[[nodiscard]] WindowsReparseDiagnosticClassification classify_diagnostic(
    const WindowsReparseProvenance& provenance,
    const WindowsReparseTargetReachability reachability) noexcept
{
    if (provenance.payload_status == WindowsReparsePayloadStatus::malformed) {
        return WindowsReparseDiagnosticClassification::
            malformed_reparse_payload;
    }
    if (provenance.payload_status ==
        WindowsReparsePayloadStatus::opaque_non_path_payload) {
        return WindowsReparseDiagnosticClassification::
            unsupported_tag_without_path_contract;
    }

    switch (reachability) {
    case WindowsReparseTargetReachability::reachable:
        return WindowsReparseDiagnosticClassification::
            reachable_name_surrogate;
    case WindowsReparseTargetReachability::target_path_not_found:
    case WindowsReparseTargetReachability::target_component_not_found:
        return provenance.tag.category ==
                       WindowsReparseTagCategory::mount_point
                   ? WindowsReparseDiagnosticClassification::
                         dangling_directory_junction
                   : WindowsReparseDiagnosticClassification::
                         dangling_directory_symlink;
    case WindowsReparseTargetReachability::target_volume_not_found:
        return WindowsReparseDiagnosticClassification::missing_volume_mount;
    case WindowsReparseTargetReachability::target_access_denied:
    case WindowsReparseTargetReachability::target_not_directory:
        return WindowsReparseDiagnosticClassification::inaccessible_target;
    case WindowsReparseTargetReachability::target_remote_or_device:
        return WindowsReparseDiagnosticClassification::remote_or_device_target;
    case WindowsReparseTargetReachability::target_cycle:
        return WindowsReparseDiagnosticClassification::cyclic_target;
    case WindowsReparseTargetReachability::target_depth_exceeded:
        return WindowsReparseDiagnosticClassification::target_depth_exceeded;
    case WindowsReparseTargetReachability::target_changed:
        return WindowsReparseDiagnosticClassification::
            changed_during_observation;
    case WindowsReparseTargetReachability::target_open_failed_other:
    case WindowsReparseTargetReachability::not_applicable:
        return WindowsReparseDiagnosticClassification::
            target_open_failed_other;
    }
    return WindowsReparseDiagnosticClassification::target_open_failed_other;
}

[[nodiscard]] WindowsReparseDiagnosticClassification classify_diagnostic(
    const WindowsReparseNestedFailure& failure) noexcept
{
    WindowsReparseProvenance nested{};
    nested.tag.category = failure.reparse_tag_category;
    nested.tag.directory = failure.directory;
    nested.target_expression.kind = failure.expression_kind;
    if (failure.reparse_tag_category ==
            WindowsReparseTagCategory::malformed_or_unreadable ||
        failure.expression_kind ==
            WindowsReparseTargetExpressionKind::malformed) {
        nested.payload_status = WindowsReparsePayloadStatus::malformed;
    } else if (failure.reparse_tag_category !=
                   WindowsReparseTagCategory::mount_point &&
               failure.reparse_tag_category !=
                   WindowsReparseTagCategory::symbolic_link) {
        nested.payload_status =
            WindowsReparsePayloadStatus::opaque_non_path_payload;
    } else if (failure.expression_kind ==
                   WindowsReparseTargetExpressionKind::none ||
               failure.expression_kind ==
                   WindowsReparseTargetExpressionKind::app_execution_alias ||
               failure.expression_kind == WindowsReparseTargetExpressionKind::
                                              opaque_non_path_payload) {
        nested.payload_status = WindowsReparsePayloadStatus::malformed;
    } else {
        nested.payload_status =
            WindowsReparsePayloadStatus::path_contract_decoded;
    }
    return classify_diagnostic(nested, failure.reachability);
}

[[nodiscard]] bool may_follow(
    const WindowsReparseTargetExpression& expression) noexcept
{
    if (expression.kind ==
            WindowsReparseTargetExpressionKind::relative_path ||
        expression.kind ==
            WindowsReparseTargetExpressionKind::drive_absolute_path ||
        expression.kind ==
            WindowsReparseTargetExpressionKind::volume_guid_path) {
        return true;
    }
    if (expression.kind !=
        WindowsReparseTargetExpressionKind::nt_object_manager_path) {
        return false;
    }

    // A junction normally stores a local drive path as \??\C:\... .  Other
    // object-manager expressions can address devices, pipes or global object
    // directories and are deliberately not followed by this diagnostic API.
    constexpr std::wstring_view dos_devices = L"\\??\\";
    return istarts_with(expression.private_expression, dos_devices) &&
           drive_absolute(
               std::wstring_view{expression.private_expression}.substr(
                   dos_devices.size()));
}

[[nodiscard]] TargetRootDisposition target_root_disposition(
    const WindowsReparseTargetExpression& expression) noexcept
{
    try {
        if (expression.kind ==
            WindowsReparseTargetExpressionKind::relative_path) {
            return TargetRootDisposition::local_fixed_or_relative;
        }

        std::wstring root;
        if (expression.kind ==
            WindowsReparseTargetExpressionKind::drive_absolute_path) {
            const auto& normalized =
                expression.private_normalized_expression;
            if (normalized.size() < 3U || !drive_absolute(normalized)) {
                return TargetRootDisposition::remote_or_device;
            }
            root.assign(normalized.begin(), normalized.begin() + 3);
        } else if (expression.kind ==
                   WindowsReparseTargetExpressionKind::
                       nt_object_manager_path) {
            constexpr std::wstring_view prefix = L"\\??\\";
            if (!istarts_with(expression.private_expression, prefix)) {
                return TargetRootDisposition::remote_or_device;
            }
            const auto tail = std::wstring_view{expression.private_expression}
                                  .substr(prefix.size());
            if (!drive_absolute(tail)) {
                return TargetRootDisposition::remote_or_device;
            }
            root.assign(tail.begin(), tail.begin() + 3);
        } else if (expression.kind ==
                   WindowsReparseTargetExpressionKind::volume_guid_path) {
            auto volume = expression.private_expression;
            constexpr std::wstring_view nt_prefix = L"\\??\\";
            if (istarts_with(volume, nt_prefix)) {
                volume = L"\\\\?\\" + volume.substr(nt_prefix.size());
            }
            const auto close = volume.find(L'}');
            if (close == std::wstring::npos) {
                return TargetRootDisposition::remote_or_device;
            }
            root = volume.substr(0U, close + 1U);
            root.push_back(L'\\');
        } else {
            return TargetRootDisposition::remote_or_device;
        }

        switch (::GetDriveTypeW(root.c_str())) {
        case DRIVE_FIXED:
            // A SUBST drive is a mutable DOS-device alias rather than an exact
            // local fixed-volume root. Check only after DRIVE_NO_ROOT_DIR has
            // retained the missing-volume classification.
            if (drive_absolute(root) && drive_letter_is_subst(root)) {
                return TargetRootDisposition::remote_or_device;
            }
            return TargetRootDisposition::local_fixed_or_relative;
        case DRIVE_NO_ROOT_DIR:
            return TargetRootDisposition::missing_volume;
        case DRIVE_UNKNOWN:
        case DRIVE_REMOVABLE:
        case DRIVE_REMOTE:
        case DRIVE_CDROM:
        case DRIVE_RAMDISK:
        default: return TargetRootDisposition::remote_or_device;
        }
    } catch (...) {
        return TargetRootDisposition::remote_or_device;
    }
}

} // namespace

WindowsReparseDiagnosticClassification
classify_windows_reparse_nested_failure_diagnostic(
    const WindowsReparseNestedFailure& failure) noexcept
{
    return classify_diagnostic(failure);
}

class WindowsReparsePinnedTarget final {
public:
    explicit WindowsReparsePinnedTarget(PinnedTargetOpen&& open) noexcept
        : open_{std::move(open)}
    {
    }

    [[nodiscard]] HANDLE get() const noexcept { return open_.get(); }

private:
    PinnedTargetOpen open_;
};

void* windows_reparse_target_native_final_handle(
    const WindowsReparseTargetObservation& observation) noexcept
{
    return observation.private_pinned_target
               ? observation.private_pinned_target->get()
               : nullptr;
}

bool valid_windows_reparse_provenance_limits(
    const WindowsReparseProvenanceLimits& limits) noexcept
{
    return limits.maximum_reparse_payload_bytes >= kCommonHeaderBytes &&
           limits.maximum_reparse_payload_bytes <=
               kWindowsReparseHardMaximumPayloadBytes &&
           limits.maximum_target_expression_characters > 0U &&
           limits.maximum_target_expression_characters <=
               kWindowsReparseHardMaximumTargetExpressionCharacters &&
           limits.maximum_failure_witnesses > 0U &&
           limits.maximum_failure_witnesses <=
               kWindowsReparseHardMaximumFailureWitnesses &&
           limits.maximum_nested_reparse_depth > 0U &&
           limits.maximum_nested_reparse_depth <=
               kWindowsReparseHardMaximumNestedDepth &&
           limits.maximum_diagnostic_targets > 0U &&
           limits.maximum_diagnostic_targets <=
               kWindowsReparseHardMaximumDiagnosticTargets;
}

WindowsReparseTagCategory classify_windows_reparse_tag(
    const std::uint32_t raw_tag) noexcept
{
    if (raw_tag == 0U) {
        return WindowsReparseTagCategory::none;
    }
    const bool microsoft = (raw_tag & kMicrosoftBit) != 0U;
    const bool reserved = (raw_tag & kReservedBit) != 0U;
    const bool surrogate = (raw_tag & kNameSurrogateBit) != 0U;
    const bool directory = (raw_tag & kDirectoryBit) != 0U;
    if (raw_tag <= 2U || raw_tag == kTagReservedInvalid ||
        (raw_tag & kReservedTagBits) != 0U || (!microsoft && reserved) ||
        (surrogate && directory) || (raw_tag & 0xFFFFU) == 0U) {
        return WindowsReparseTagCategory::malformed_or_unreadable;
    }
    switch (raw_tag) {
    case kTagMountPoint: return WindowsReparseTagCategory::mount_point;
    case kTagSymbolicLink: return WindowsReparseTagCategory::symbolic_link;
    case kTagAppExecLink: return WindowsReparseTagCategory::app_exec_link;
    case kTagCloud: return WindowsReparseTagCategory::cloud_placeholder;
    case kTagWci:
    case kTagWci1: return WindowsReparseTagCategory::wci;
    case kTagWciTombstone:
        return WindowsReparseTagCategory::wci_tombstone;
    case kTagWof: return WindowsReparseTagCategory::wof;
    case kTagDedup: return WindowsReparseTagCategory::dedup;
    case kTagHsm:
    case kTagHsm2: return WindowsReparseTagCategory::hsm;
    case kTagDfs:
    case kTagDfsr: return WindowsReparseTagCategory::dfs;
    case kTagSis: return WindowsReparseTagCategory::sis;
    case kTagProjFs:
    case kTagProjFsTombstone:
        return WindowsReparseTagCategory::projected_file_system;
    default: break;
    }

    // Bits 12..15 encode the documented Cloud Files tag variants.
    if ((raw_tag & 0xFFFF0FFFU) == kTagCloud && raw_tag != kTagCloud) {
        return WindowsReparseTagCategory::cloud_placeholder_variant;
    }
    if (microsoft) {
        return surrogate
                   ? WindowsReparseTagCategory::
                         microsoft_name_surrogate_other
                   : WindowsReparseTagCategory::
                         microsoft_non_name_surrogate_other;
    }
    return surrogate ? WindowsReparseTagCategory::third_party_name_surrogate
                     : WindowsReparseTagCategory::third_party_other;
}

WindowsReparseTargetExpression classify_windows_reparse_target_expression(
    const std::wstring_view expression,
    const bool relative,
    const std::size_t maximum_characters) noexcept
{
    WindowsReparseTargetExpression result{};
    result.relative = relative;
    try {
        if (maximum_characters == 0U ||
            maximum_characters >
                kWindowsReparseHardMaximumTargetExpressionCharacters ||
            expression.empty() || expression.size() > maximum_characters ||
            !valid_utf16(expression) ||
            contains_forbidden_path_character(expression)) {
            result.kind = WindowsReparseTargetExpressionKind::malformed;
            return result;
        }
        result.private_expression.assign(expression);

        if (relative) {
            if (drive_absolute(expression) || expression.front() == L'\\' ||
                expression.front() == L'/' ||
                expression.find(L':') != std::wstring_view::npos) {
                result.kind = WindowsReparseTargetExpressionKind::malformed;
                return result;
            }
            result.kind = WindowsReparseTargetExpressionKind::relative_path;
        } else if (istarts_with(expression, L"\\??\\Volume{")) {
            result.kind = valid_volume_guid_tail(expression.substr(4U))
                              ? WindowsReparseTargetExpressionKind::
                                    volume_guid_path
                              : WindowsReparseTargetExpressionKind::malformed;
        } else if (istarts_with(expression, L"\\\\?\\Volume{")) {
            result.kind = valid_volume_guid_tail(expression.substr(4U))
                              ? WindowsReparseTargetExpressionKind::
                                    volume_guid_path
                              : WindowsReparseTargetExpressionKind::malformed;
        } else if (istarts_with(expression, L"\\??\\UNC\\")) {
            result.kind = valid_unc_tail(expression.substr(8U))
                              ? WindowsReparseTargetExpressionKind::unc_path
                              : WindowsReparseTargetExpressionKind::malformed;
        } else if (istarts_with(expression, L"\\\\?\\UNC\\")) {
            result.kind = valid_unc_tail(expression.substr(8U))
                              ? WindowsReparseTargetExpressionKind::unc_path
                              : WindowsReparseTargetExpressionKind::malformed;
        } else if (expression.size() >= 2U && expression[0] == L'\\' &&
                   expression[1] == L'\\' &&
                   !istarts_with(expression, L"\\\\?\\") &&
                   !istarts_with(expression, L"\\\\.\\")) {
            result.kind = valid_unc_tail(expression.substr(2U))
                              ? WindowsReparseTargetExpressionKind::unc_path
                              : WindowsReparseTargetExpressionKind::malformed;
        } else if (istarts_with(expression, L"\\\\.\\") ||
                   istarts_with(expression, L"\\Device\\") ||
                   istarts_with(expression, L"\\??\\Device\\") ||
                   istarts_with(expression, L"\\??\\GLOBALROOT\\") ||
                   istarts_with(expression,
                                L"\\\\?\\GLOBALROOT\\Device\\")) {
            result.kind = WindowsReparseTargetExpressionKind::device_path;
        } else if (istarts_with(expression, L"\\\\?\\") &&
                   drive_absolute(expression.substr(4U))) {
            result.kind =
                WindowsReparseTargetExpressionKind::drive_absolute_path;
        } else if (drive_absolute(expression)) {
            result.kind =
                WindowsReparseTargetExpressionKind::drive_absolute_path;
        } else if (istarts_with(expression, L"\\??\\") ||
                   istarts_with(expression, L"\\GLOBAL??\\") ||
                   expression.front() == L'\\') {
            result.kind =
                WindowsReparseTargetExpressionKind::nt_object_manager_path;
        } else {
            result.kind = WindowsReparseTargetExpressionKind::malformed;
        }

        if (result.kind != WindowsReparseTargetExpressionKind::malformed) {
            result.private_normalized_expression =
                normalize_private_expression(expression, result.kind);
        }
    } catch (...) {
        result = {};
        result.relative = relative;
        result.kind = WindowsReparseTargetExpressionKind::malformed;
    }
    return result;
}

WindowsReparseNativeErrorCategory classify_windows_reparse_native_error(
    const std::uint32_t native_error) noexcept
{
    switch (native_error) {
    case ERROR_SUCCESS: return WindowsReparseNativeErrorCategory::none;
    case ERROR_FILE_NOT_FOUND:
        return WindowsReparseNativeErrorCategory::file_not_found;
    case ERROR_PATH_NOT_FOUND:
        return WindowsReparseNativeErrorCategory::path_not_found;
    case ERROR_INVALID_NAME:
        return WindowsReparseNativeErrorCategory::invalid_name;
    case ERROR_BAD_PATHNAME:
        return WindowsReparseNativeErrorCategory::bad_pathname;
    case ERROR_BAD_NETPATH:
        return WindowsReparseNativeErrorCategory::bad_network_path;
    case ERROR_BAD_NET_NAME:
        return WindowsReparseNativeErrorCategory::bad_network_name;
    case ERROR_NOT_READY:
        return WindowsReparseNativeErrorCategory::volume_not_ready;
    case ERROR_DEV_NOT_EXIST:
        return WindowsReparseNativeErrorCategory::device_not_exist;
    case ERROR_DEVICE_NOT_CONNECTED:
        return WindowsReparseNativeErrorCategory::device_not_connected;
    case ERROR_ACCESS_DENIED:
        return WindowsReparseNativeErrorCategory::access_denied;
    case ERROR_CANT_ACCESS_FILE:
        return WindowsReparseNativeErrorCategory::cannot_access_file;
    case ERROR_DIRECTORY:
        return WindowsReparseNativeErrorCategory::not_directory;
    case ERROR_REPARSE_TAG_INVALID:
        return WindowsReparseNativeErrorCategory::reparse_tag_invalid;
    case ERROR_REPARSE_TAG_MISMATCH:
        return WindowsReparseNativeErrorCategory::reparse_tag_mismatch;
    case ERROR_REPARSE_POINT_ENCOUNTERED:
        return WindowsReparseNativeErrorCategory::reparse_point_encountered;
    case ERROR_CIRCULAR_DEPENDENCY:
    case ERROR_CANT_RESOLVE_FILENAME:
        return WindowsReparseNativeErrorCategory::circular_dependency;
    case ERROR_TOO_MANY_LINKS:
        return WindowsReparseNativeErrorCategory::too_many_links;
    default: return WindowsReparseNativeErrorCategory::other;
    }
}

WindowsReparseTargetReachability
classify_windows_reparse_target_reachability(
    const std::uint32_t native_error) noexcept
{
    switch (classify_windows_reparse_native_error(native_error)) {
    case WindowsReparseNativeErrorCategory::none:
        return WindowsReparseTargetReachability::reachable;
    case WindowsReparseNativeErrorCategory::file_not_found:
    case WindowsReparseNativeErrorCategory::invalid_name:
    case WindowsReparseNativeErrorCategory::bad_pathname:
        return WindowsReparseTargetReachability::target_path_not_found;
    case WindowsReparseNativeErrorCategory::path_not_found:
        return WindowsReparseTargetReachability::target_component_not_found;
    case WindowsReparseNativeErrorCategory::volume_not_ready:
    case WindowsReparseNativeErrorCategory::device_not_exist:
    case WindowsReparseNativeErrorCategory::device_not_connected:
        return WindowsReparseTargetReachability::target_volume_not_found;
    case WindowsReparseNativeErrorCategory::access_denied:
    case WindowsReparseNativeErrorCategory::cannot_access_file:
        return WindowsReparseTargetReachability::target_access_denied;
    case WindowsReparseNativeErrorCategory::not_directory:
        return WindowsReparseTargetReachability::target_not_directory;
    case WindowsReparseNativeErrorCategory::bad_network_path:
    case WindowsReparseNativeErrorCategory::bad_network_name:
        return WindowsReparseTargetReachability::target_remote_or_device;
    case WindowsReparseNativeErrorCategory::circular_dependency:
        return WindowsReparseTargetReachability::target_cycle;
    case WindowsReparseNativeErrorCategory::too_many_links:
        return WindowsReparseTargetReachability::target_depth_exceeded;
    case WindowsReparseNativeErrorCategory::reparse_tag_invalid:
    case WindowsReparseNativeErrorCategory::reparse_tag_mismatch:
        return WindowsReparseTargetReachability::target_changed;
    case WindowsReparseNativeErrorCategory::reparse_point_encountered:
    case WindowsReparseNativeErrorCategory::other:
        return WindowsReparseTargetReachability::target_open_failed_other;
    }
    return WindowsReparseTargetReachability::target_open_failed_other;
}

WindowsReparseProvenanceResult<WindowsReparseProvenance>
decode_windows_reparse_payload(
    const std::span<const std::byte> bytes,
    const bool directory,
    const WindowsReparseProvenanceLimits& limits) noexcept
{
    WindowsReparseProvenance value{};
    value.tag.directory = directory;
    value.payload_status = WindowsReparsePayloadStatus::malformed;

    const auto malformed = [&](const WindowsReparsePayloadErrorCode error) {
        value.payload_error = error;
        value.payload_status = WindowsReparsePayloadStatus::malformed;
        value.target_expression.kind =
            WindowsReparseTargetExpressionKind::malformed;
        return WindowsReparseProvenanceResult<WindowsReparseProvenance>{
            std::move(value), WindowsReparseProvenanceErrorCode::none,
            ERROR_SUCCESS};
    };

    try {
        if (!valid_windows_reparse_provenance_limits(limits)) {
            return {{}, WindowsReparseProvenanceErrorCode::invalid_limits,
                    ERROR_INVALID_PARAMETER};
        }
        if (bytes.size() > limits.maximum_reparse_payload_bytes ||
            bytes.size() > kWindowsReparseHardMaximumPayloadBytes) {
            return malformed(
                WindowsReparsePayloadErrorCode::payload_limit_exceeded);
        }

        if (const auto digest = hash::sha256(bytes)) {
            value.private_payload_sha256 = hash::sha256_hex(*digest);
        }

        if (bytes.size() < kCommonHeaderBytes) {
            if (const auto raw_tag = read_u32(bytes, 0U)) {
                value.tag.raw_tag = *raw_tag;
                value.tag.microsoft = (*raw_tag & kMicrosoftBit) != 0U;
                value.tag.name_surrogate =
                    (*raw_tag & kNameSurrogateBit) != 0U;
                value.tag.category = classify_windows_reparse_tag(*raw_tag);
            }
            return malformed(
                WindowsReparsePayloadErrorCode::truncated_header);
        }

        const auto raw_tag = read_u32(bytes, 0U);
        const auto data_length = read_u16(bytes, 4U);
        if (!raw_tag || !data_length) {
            return malformed(
                WindowsReparsePayloadErrorCode::truncated_header);
        }
        value.tag.raw_tag = *raw_tag;
        value.tag.microsoft = (*raw_tag & kMicrosoftBit) != 0U;
        value.tag.name_surrogate =
            (*raw_tag & kNameSurrogateBit) != 0U;
        value.tag.category = classify_windows_reparse_tag(*raw_tag);
        value.tag.payload_byte_count = *data_length;

        // Microsoft tags use REPARSE_DATA_BUFFER, whose ReparseDataLength
        // covers the bytes following the common 8-byte header. Third-party
        // tags use REPARSE_GUID_DATA_BUFFER: its 16-byte owner GUID is part of
        // the header, and ReparseDataLength covers only DataBuffer.
        if (!value.tag.microsoft && bytes.size() < kGuidHeaderBytes) {
            return malformed(
                WindowsReparsePayloadErrorCode::truncated_guid_header);
        }
        const auto payload_offset = value.tag.microsoft ? kCommonHeaderBytes
                                                        : kGuidHeaderBytes;
        if (static_cast<std::size_t>(*data_length) !=
            bytes.size() - payload_offset) {
            return malformed(
                WindowsReparsePayloadErrorCode::invalid_data_length);
        }
        if (value.tag.category == WindowsReparseTagCategory::none ||
            value.tag.category ==
                WindowsReparseTagCategory::malformed_or_unreadable) {
            return malformed(
                WindowsReparsePayloadErrorCode::invalid_data_length);
        }
        if (value.tag.category != WindowsReparseTagCategory::mount_point &&
            value.tag.category != WindowsReparseTagCategory::symbolic_link) {
            value.payload_status =
                WindowsReparsePayloadStatus::opaque_non_path_payload;
            value.payload_error = WindowsReparsePayloadErrorCode::none;
            value.target_expression.kind =
                value.tag.category == WindowsReparseTagCategory::app_exec_link
                    ? WindowsReparseTargetExpressionKind::app_execution_alias
                    : WindowsReparseTargetExpressionKind::
                          opaque_non_path_payload;
            return {std::move(value),
                    WindowsReparseProvenanceErrorCode::none, ERROR_SUCCESS};
        }

        const bool symbolic_link =
            value.tag.category == WindowsReparseTagCategory::symbolic_link;
        const std::size_t fixed_payload_bytes =
            symbolic_link ? kSymbolicLinkFixedPayloadBytes
                          : kMountPointFixedPayloadBytes;
        if (*data_length < fixed_payload_bytes) {
            return malformed(
                WindowsReparsePayloadErrorCode::truncated_path_header);
        }

        const auto substitute_offset = read_u16(bytes, 8U);
        const auto substitute_length = read_u16(bytes, 10U);
        const auto print_offset = read_u16(bytes, 12U);
        const auto print_length = read_u16(bytes, 14U);
        if (!substitute_offset || !substitute_length || !print_offset ||
            !print_length) {
            return malformed(
                WindowsReparsePayloadErrorCode::truncated_path_header);
        }
        if (((*substitute_offset | *substitute_length | *print_offset |
              *print_length) &
             1U) != 0U) {
            return malformed(WindowsReparsePayloadErrorCode::
                                 odd_utf16_offset_or_length);
        }

        const auto path_buffer_offset =
            kCommonHeaderBytes + fixed_payload_bytes;
        const auto path_buffer = bytes.subspan(path_buffer_offset);
        if (!checked_range(*substitute_offset, *substitute_length,
                           path_buffer.size()) ||
            !checked_range(*print_offset, *print_length,
                           path_buffer.size())) {
            return malformed(
                WindowsReparsePayloadErrorCode::path_range_overflow);
        }
        if (ranges_overlap(*substitute_offset, *substitute_length,
                           *print_offset, *print_length)) {
            return malformed(
                WindowsReparsePayloadErrorCode::path_ranges_overlap);
        }

        if (symbolic_link) {
            const auto flags = read_u32(bytes, 16U);
            if (!flags || (*flags & ~kSymbolicLinkRelativeFlag) != 0U) {
                return malformed(WindowsReparsePayloadErrorCode::
                                     invalid_symbolic_link_flags);
            }
            value.symbolic_link_flags = *flags;
            value.symbolic_link_relative =
                (*flags & kSymbolicLinkRelativeFlag) != 0U;
        }

        if (!decode_utf16(path_buffer, *substitute_offset,
                          *substitute_length,
                          value.private_substitute_name) ||
            !decode_utf16(path_buffer, *print_offset, *print_length,
                          value.private_print_name)) {
            return malformed(WindowsReparsePayloadErrorCode::
                                 target_expression_malformed);
        }
        if (value.private_substitute_name.size() >
                limits.maximum_target_expression_characters ||
            value.private_print_name.size() >
                limits.maximum_target_expression_characters) {
            return malformed(WindowsReparsePayloadErrorCode::
                                 target_expression_limit_exceeded);
        }

        value.target_expression = classify_windows_reparse_target_expression(
            value.private_substitute_name, value.symbolic_link_relative,
            limits.maximum_target_expression_characters);
        if (value.target_expression.kind ==
            WindowsReparseTargetExpressionKind::malformed) {
            return malformed(WindowsReparsePayloadErrorCode::
                                 target_expression_malformed);
        }
        value.payload_status =
            WindowsReparsePayloadStatus::path_contract_decoded;
        value.payload_error = WindowsReparsePayloadErrorCode::none;
        return {std::move(value), WindowsReparseProvenanceErrorCode::none,
                ERROR_SUCCESS};
    } catch (...) {
        return {{}, WindowsReparseProvenanceErrorCode::
                        reparse_payload_read_failed,
                ERROR_NOT_ENOUGH_MEMORY};
    }
}

WindowsReparseProvenanceResult<WindowsReparseProvenance>
read_windows_reparse_provenance(
    const std::filesystem::path& source_link,
    const WindowsReparseProvenanceLimits& limits) noexcept
{
    auto initial = read_once(source_link, limits);
    if (!initial.value) {
        return {{}, initial.code, initial.native_error};
    }
    auto confirmed = read_once(source_link, limits);
    if (!confirmed.value ||
        !same_source_snapshot(initial.value->snapshot,
                              confirmed.value->snapshot) ||
        initial.value->provenance.private_payload_sha256 !=
            confirmed.value->provenance.private_payload_sha256) {
        return {{}, WindowsReparseProvenanceErrorCode::source_link_changed,
                confirmed.value ? ERROR_REPARSE_TAG_MISMATCH
                                : confirmed.native_error};
    }
    return {std::move(confirmed.value->provenance),
            WindowsReparseProvenanceErrorCode::none, ERROR_SUCCESS};
}

WindowsReparseProvenanceResult<WindowsReparseTargetObservation>
observe_windows_reparse_target(
    const std::filesystem::path& source_link,
    const WindowsReparseProvenanceLimits& limits) noexcept
{
    try {
        auto first = read_once(source_link, limits);
        if (!first.value) {
            return {{}, first.code, first.native_error};
        }

        const auto first_snapshot = first.value->snapshot;
        const auto first_payload_sha256 =
            first.value->provenance.private_payload_sha256;
        WindowsReparseTargetObservation observation{};
        observation.provenance = std::move(first.value->provenance);
        observation.observation_complete = true;
        const auto complete_diagnostic =
            [&](WindowsReparseTargetObservation current)
            -> WindowsReparseProvenanceResult<
                WindowsReparseTargetObservation> {
            auto revalidated = read_once(source_link, limits);
            if (!revalidated.value ||
                !same_source_snapshot(first_snapshot,
                                      revalidated.value->snapshot) ||
                first_payload_sha256 != revalidated.value->provenance
                                            .private_payload_sha256) {
                current.reachability =
                    WindowsReparseTargetReachability::target_changed;
                current.failure_phase = StockExternalTopologyFailurePhase::
                    post_inventory_revalidation;
                current.diagnostic_classification =
                    WindowsReparseDiagnosticClassification::
                        changed_during_observation;
                current.nested_failure.reset();
                if (!revalidated.value) {
                    current.native_error = revalidated.native_error;
                    current.native_error_category =
                        classify_windows_reparse_native_error(
                            revalidated.native_error);
                }
            }
            return {std::move(current),
                    WindowsReparseProvenanceErrorCode::none, ERROR_SUCCESS};
        };

        if (observation.provenance.payload_status ==
            WindowsReparsePayloadStatus::malformed) {
            observation.failure_phase =
                StockExternalTopologyFailurePhase::reparse_payload_decode;
            observation.diagnostic_classification =
                WindowsReparseDiagnosticClassification::
                    malformed_reparse_payload;
            return complete_diagnostic(std::move(observation));
        }
        if (observation.provenance.payload_status ==
            WindowsReparsePayloadStatus::opaque_non_path_payload) {
            observation.failure_phase =
                StockExternalTopologyFailurePhase::target_expression_parse;
            observation.diagnostic_classification =
                WindowsReparseDiagnosticClassification::
                    unsupported_tag_without_path_contract;
            return complete_diagnostic(std::move(observation));
        }

        const auto expression_kind =
            observation.provenance.target_expression.kind;
        if (expression_kind == WindowsReparseTargetExpressionKind::malformed ||
            expression_kind == WindowsReparseTargetExpressionKind::none) {
            observation.failure_phase =
                StockExternalTopologyFailurePhase::target_expression_parse;
            observation.diagnostic_classification =
                WindowsReparseDiagnosticClassification::
                    malformed_reparse_payload;
            return complete_diagnostic(std::move(observation));
        }
        if (expression_kind == WindowsReparseTargetExpressionKind::unc_path ||
            expression_kind ==
                WindowsReparseTargetExpressionKind::device_path ||
            !may_follow(observation.provenance.target_expression)) {
            observation.reachability =
                WindowsReparseTargetReachability::target_remote_or_device;
            observation.failure_phase =
                StockExternalTopologyFailurePhase::target_open;
            observation.diagnostic_classification =
                WindowsReparseDiagnosticClassification::
                    remote_or_device_target;
            return complete_diagnostic(std::move(observation));
        }

        const auto root_disposition = target_root_disposition(
            observation.provenance.target_expression);
        if (root_disposition == TargetRootDisposition::missing_volume) {
            observation.reachability =
                WindowsReparseTargetReachability::target_volume_not_found;
            observation.native_error = ERROR_DEV_NOT_EXIST;
            observation.native_error_category =
                WindowsReparseNativeErrorCategory::device_not_exist;
            observation.failure_phase =
                StockExternalTopologyFailurePhase::target_open;
            observation.diagnostic_classification =
                WindowsReparseDiagnosticClassification::missing_volume_mount;
            return complete_diagnostic(std::move(observation));
        }
        if (root_disposition == TargetRootDisposition::remote_or_device) {
            observation.reachability =
                WindowsReparseTargetReachability::target_remote_or_device;
            observation.failure_phase =
                StockExternalTopologyFailurePhase::target_open;
            observation.diagnostic_classification =
                WindowsReparseDiagnosticClassification::
                    remote_or_device_target;
            return complete_diagnostic(std::move(observation));
        }

        auto followed = open_captured_local_target(
            source_link, observation.provenance.target_expression, limits,
            first_snapshot);
        if (!followed.valid()) {
            observation.nested_failure =
                std::move(followed.nested_failure);
            if (observation.nested_failure) {
                const auto& failure = *observation.nested_failure;
                observation.native_error = failure.native_error;
                observation.native_error_category =
                    failure.native_error_category;
                observation.reachability = failure.reachability;
                observation.failure_phase = failure.failure_phase;
                observation.diagnostic_classification =
                    classify_diagnostic(failure);
            } else {
                observation.native_error = followed.native_error;
                observation.native_error_category =
                    classify_windows_reparse_native_error(
                        observation.native_error);
                observation.reachability =
                    classify_windows_reparse_target_reachability(
                        observation.native_error);
                observation.failure_phase =
                    StockExternalTopologyFailurePhase::target_open;
                observation.diagnostic_classification = classify_diagnostic(
                    observation.provenance, observation.reachability);
            }
            return complete_diagnostic(std::move(observation));
        }

        auto identity = target_identity(followed.get());
        if (!identity) {
            return {{},
                    WindowsReparseProvenanceErrorCode::target_identity_failed,
                    ::GetLastError()};
        }
        if (observation.provenance.tag.directory != identity->directory) {
            observation.reachability =
                WindowsReparseTargetReachability::target_not_directory;
            observation.failure_phase =
                StockExternalTopologyFailurePhase::target_identity;
            observation.native_error_category =
                WindowsReparseNativeErrorCategory::not_directory;
            observation.native_error = ERROR_DIRECTORY;
            observation.diagnostic_classification = classify_diagnostic(
                observation.provenance, observation.reachability);
            return complete_diagnostic(std::move(observation));
        }

        auto second = read_once(source_link, limits);
        auto followed_again = open_captured_local_target(
            source_link, observation.provenance.target_expression, limits,
            first_snapshot);
        const auto second_identity = followed_again.valid()
                                         ? target_identity(followed_again.get())
                                         : std::nullopt;
        if (!second.value ||
            !same_source_snapshot(first_snapshot, second.value->snapshot) ||
            first_payload_sha256 !=
                second.value->provenance.private_payload_sha256 ||
            !second_identity || !same_target_identity(*identity,
                                                       *second_identity)) {
            observation.reachability =
                WindowsReparseTargetReachability::target_changed;
            observation.failure_phase = StockExternalTopologyFailurePhase::
                post_inventory_revalidation;
            observation.diagnostic_classification =
                WindowsReparseDiagnosticClassification::
                    changed_during_observation;
            return {std::move(observation),
                    WindowsReparseProvenanceErrorCode::none, ERROR_SUCCESS};
        }

        observation.target_identity = std::move(identity);
        observation.private_pinned_target =
            std::make_shared<const WindowsReparsePinnedTarget>(
                std::move(followed_again));
        observation.reachability =
            WindowsReparseTargetReachability::reachable;
        observation.failure_phase =
            StockExternalTopologyFailurePhase::target_identity;
        observation.diagnostic_classification =
            WindowsReparseDiagnosticClassification::reachable_name_surrogate;
        return {std::move(observation),
                WindowsReparseProvenanceErrorCode::none, ERROR_SUCCESS};
    } catch (...) {
        return {{}, WindowsReparseProvenanceErrorCode::target_identity_failed,
                ERROR_NOT_ENOUGH_MEMORY};
    }
}

#define HLCLIENT_REPARSE_TO_STRING_CASE(value)                                  \
    case value: return unqualified_enum_name(#value)

[[nodiscard]] constexpr std::string_view unqualified_enum_name(
    const std::string_view value) noexcept
{
    const auto separator = value.rfind("::");
    return separator == std::string_view::npos ? value
                                               : value.substr(separator + 2U);
}

std::string_view to_string(const WindowsReparseTagCategory value) noexcept
{
    switch (value) {
        HLCLIENT_REPARSE_TO_STRING_CASE(WindowsReparseTagCategory::none);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTagCategory::mount_point);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTagCategory::symbolic_link);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTagCategory::app_exec_link);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTagCategory::cloud_placeholder);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTagCategory::cloud_placeholder_variant);
        HLCLIENT_REPARSE_TO_STRING_CASE(WindowsReparseTagCategory::wci);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTagCategory::wci_tombstone);
        HLCLIENT_REPARSE_TO_STRING_CASE(WindowsReparseTagCategory::wof);
        HLCLIENT_REPARSE_TO_STRING_CASE(WindowsReparseTagCategory::dedup);
        HLCLIENT_REPARSE_TO_STRING_CASE(WindowsReparseTagCategory::hsm);
        HLCLIENT_REPARSE_TO_STRING_CASE(WindowsReparseTagCategory::dfs);
        HLCLIENT_REPARSE_TO_STRING_CASE(WindowsReparseTagCategory::sis);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTagCategory::projected_file_system);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTagCategory::microsoft_name_surrogate_other);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTagCategory::microsoft_non_name_surrogate_other);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTagCategory::third_party_name_surrogate);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTagCategory::third_party_other);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTagCategory::malformed_or_unreadable);
    }
    return "malformed_or_unreadable";
}

std::string_view to_string(
    const WindowsReparseTargetExpressionKind value) noexcept
{
    switch (value) {
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetExpressionKind::none);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetExpressionKind::relative_path);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetExpressionKind::drive_absolute_path);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetExpressionKind::volume_guid_path);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetExpressionKind::nt_object_manager_path);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetExpressionKind::unc_path);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetExpressionKind::device_path);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetExpressionKind::app_execution_alias);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetExpressionKind::opaque_non_path_payload);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetExpressionKind::malformed);
    }
    return "malformed";
}

std::string_view to_string(
    const WindowsReparseTargetReachability value) noexcept
{
    switch (value) {
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetReachability::reachable);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetReachability::target_path_not_found);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetReachability::target_component_not_found);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetReachability::target_volume_not_found);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetReachability::target_access_denied);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetReachability::target_not_directory);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetReachability::target_remote_or_device);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetReachability::target_cycle);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetReachability::target_depth_exceeded);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetReachability::target_changed);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetReachability::target_open_failed_other);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseTargetReachability::not_applicable);
    }
    return "target_open_failed_other";
}

std::string_view to_string(
    const WindowsReparseNativeErrorCategory value) noexcept
{
    switch (value) {
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseNativeErrorCategory::none);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseNativeErrorCategory::file_not_found);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseNativeErrorCategory::path_not_found);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseNativeErrorCategory::invalid_name);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseNativeErrorCategory::bad_pathname);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseNativeErrorCategory::bad_network_path);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseNativeErrorCategory::bad_network_name);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseNativeErrorCategory::volume_not_ready);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseNativeErrorCategory::device_not_exist);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseNativeErrorCategory::device_not_connected);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseNativeErrorCategory::access_denied);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseNativeErrorCategory::cannot_access_file);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseNativeErrorCategory::not_directory);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseNativeErrorCategory::reparse_tag_invalid);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseNativeErrorCategory::reparse_tag_mismatch);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseNativeErrorCategory::reparse_point_encountered);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseNativeErrorCategory::circular_dependency);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseNativeErrorCategory::too_many_links);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseNativeErrorCategory::other);
    }
    return "other";
}

std::string_view to_string(const WindowsReparsePayloadStatus value) noexcept
{
    switch (value) {
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparsePayloadStatus::path_contract_decoded);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparsePayloadStatus::opaque_non_path_payload);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparsePayloadStatus::malformed);
    }
    return "malformed";
}

std::string_view to_string(
    const WindowsReparsePayloadErrorCode value) noexcept
{
    switch (value) {
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparsePayloadErrorCode::none);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparsePayloadErrorCode::truncated_header);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparsePayloadErrorCode::invalid_data_length);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparsePayloadErrorCode::payload_limit_exceeded);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparsePayloadErrorCode::truncated_path_header);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparsePayloadErrorCode::truncated_guid_header);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparsePayloadErrorCode::odd_utf16_offset_or_length);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparsePayloadErrorCode::path_range_overflow);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparsePayloadErrorCode::path_ranges_overlap);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparsePayloadErrorCode::invalid_symbolic_link_flags);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparsePayloadErrorCode::
                target_expression_limit_exceeded);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparsePayloadErrorCode::target_expression_malformed);
    }
    return "target_expression_malformed";
}

std::string_view to_string(
    const WindowsReparseDiagnosticClassification value) noexcept
{
    switch (value) {
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseDiagnosticClassification::none);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseDiagnosticClassification::
                reachable_name_surrogate);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseDiagnosticClassification::
                dangling_directory_junction);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseDiagnosticClassification::
                dangling_directory_symlink);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseDiagnosticClassification::missing_volume_mount);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseDiagnosticClassification::inaccessible_target);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseDiagnosticClassification::remote_or_device_target);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseDiagnosticClassification::cyclic_target);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseDiagnosticClassification::target_depth_exceeded);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseDiagnosticClassification::
                changed_during_observation);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseDiagnosticClassification::
                unsupported_tag_without_path_contract);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseDiagnosticClassification::
                malformed_reparse_payload);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseDiagnosticClassification::
                target_open_failed_other);
    }
    return "target_open_failed_other";
}

std::string_view to_string(
    const StockExternalTopologyFailurePhase value) noexcept
{
    switch (value) {
        HLCLIENT_REPARSE_TO_STRING_CASE(
            StockExternalTopologyFailurePhase::source_link_open);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            StockExternalTopologyFailurePhase::reparse_payload_read);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            StockExternalTopologyFailurePhase::reparse_payload_decode);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            StockExternalTopologyFailurePhase::target_expression_parse);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            StockExternalTopologyFailurePhase::target_open);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            StockExternalTopologyFailurePhase::target_identity);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            StockExternalTopologyFailurePhase::target_inventory);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            StockExternalTopologyFailurePhase::nested_entry_open);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            StockExternalTopologyFailurePhase::nested_reparse_decode);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            StockExternalTopologyFailurePhase::
                post_inventory_revalidation);
    }
    return "source_link_open";
}

std::string_view to_string(
    const WindowsReparseProvenanceErrorCode value) noexcept
{
    switch (value) {
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseProvenanceErrorCode::none);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseProvenanceErrorCode::invalid_argument);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseProvenanceErrorCode::invalid_limits);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseProvenanceErrorCode::source_link_open_failed);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseProvenanceErrorCode::source_not_reparse_point);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseProvenanceErrorCode::reparse_payload_read_failed);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseProvenanceErrorCode::
                reparse_payload_limit_exceeded);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseProvenanceErrorCode::target_identity_failed);
        HLCLIENT_REPARSE_TO_STRING_CASE(
            WindowsReparseProvenanceErrorCode::source_link_changed);
    }
    return "invalid_argument";
}

#undef HLCLIENT_REPARSE_TO_STRING_CASE

} // namespace hlclient::platform::windows
