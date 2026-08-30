#include <hlclient/platform/windows/binary_identity.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
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
#include <bcrypt.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

namespace hlclient::platform::windows {
namespace {

class UniqueHandle final {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(const HANDLE handle) noexcept : handle_{handle} {}
    ~UniqueHandle()
    {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            static_cast<void>(::CloseHandle(handle_));
        }
    }
    UniqueHandle(UniqueHandle&& other) noexcept
        : handle_{std::exchange(other.handle_, INVALID_HANDLE_VALUE)}
    {
    }
    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other) {
            if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
                static_cast<void>(::CloseHandle(handle_));
            }
            handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
        }
        return *this;
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

class UniqueAlgorithm final {
public:
    ~UniqueAlgorithm()
    {
        if (handle_ != nullptr) {
            static_cast<void>(::BCryptCloseAlgorithmProvider(handle_, 0U));
        }
    }
    UniqueAlgorithm(const UniqueAlgorithm&) = delete;
    UniqueAlgorithm& operator=(const UniqueAlgorithm&) = delete;
    UniqueAlgorithm() = default;
    [[nodiscard]] BCRYPT_ALG_HANDLE* put() noexcept { return &handle_; }
    [[nodiscard]] BCRYPT_ALG_HANDLE get() const noexcept { return handle_; }

private:
    BCRYPT_ALG_HANDLE handle_{nullptr};
};

class UniqueHash final {
public:
    ~UniqueHash()
    {
        if (handle_ != nullptr) {
            static_cast<void>(::BCryptDestroyHash(handle_));
        }
    }
    UniqueHash(const UniqueHash&) = delete;
    UniqueHash& operator=(const UniqueHash&) = delete;
    UniqueHash() = default;
    [[nodiscard]] BCRYPT_HASH_HANDLE* put() noexcept { return &handle_; }
    [[nodiscard]] BCRYPT_HASH_HANDLE get() const noexcept { return handle_; }

private:
    BCRYPT_HASH_HANDLE handle_{nullptr};
};

[[nodiscard]] WindowsBinaryIdentityResult binary_error(
    const WindowsBinaryIdentityErrorCode code,
    const DWORD native = 0U) noexcept
{
    return WindowsBinaryIdentityResult{std::nullopt, code, native};
}

[[nodiscard]] SteamAppManifestResult manifest_error(
    const SteamAppManifestErrorCode code,
    const DWORD native = 0U) noexcept
{
    return SteamAppManifestResult{std::nullopt, code, native};
}

[[nodiscard]] bool has_alternate_stream_syntax(
    const std::wstring_view path) noexcept
{
    for (std::size_t index = 0U; index < path.size(); ++index) {
        if (path[index] != L':') {
            continue;
        }
        const auto ascii_alpha = [](const wchar_t character) noexcept {
            return (character >= L'A' && character <= L'Z') ||
                   (character >= L'a' && character <= L'z');
        };
        const bool drive_colon =
            (index == 1U && ascii_alpha(path[0U])) ||
            (index == 5U && path.starts_with(LR"(\\?\)") &&
             ascii_alpha(path[4U]));
        if (!drive_colon) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool has_only_default_data_stream(
    const std::filesystem::path& path) noexcept
{
    WIN32_FIND_STREAM_DATA data{};
    const HANDLE search = ::FindFirstStreamW(
        path.c_str(), FindStreamInfoStandard, &data, 0U);
    if (search == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool valid = std::wstring_view{data.cStreamName} == L"::$DATA";
    std::size_t count = 1U;
    while (valid && ::FindNextStreamW(search, &data)) {
        ++count;
        valid = count == 1U &&
            std::wstring_view{data.cStreamName} == L"::$DATA";
    }
    const DWORD final_error = ::GetLastError();
    static_cast<void>(::FindClose(search));
    return valid && count == 1U && final_error == ERROR_HANDLE_EOF;
}

[[nodiscard]] bool contains_reparse_component(
    const std::filesystem::path& input) noexcept
{
    std::error_code error;
    auto current = input.root_path();
    for (const auto& component : input.relative_path()) {
        current /= component;
        const DWORD attributes = ::GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            return true;
        }
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
            return true;
        }
    }
    static_cast<void>(error);
    return false;
}

[[nodiscard]] bool query_snapshot(
    const HANDLE handle,
    WindowsFileSnapshot& snapshot,
    std::uint32_t& link_count) noexcept
{
    FILE_ID_INFO file_id{};
    FILE_STANDARD_INFO standard{};
    FILE_BASIC_INFO basic{};
    if (!::GetFileInformationByHandleEx(
            handle, FileIdInfo, &file_id, sizeof(file_id)) ||
        !::GetFileInformationByHandleEx(
            handle, FileStandardInfo, &standard, sizeof(standard)) ||
        !::GetFileInformationByHandleEx(
            handle, FileBasicInfo, &basic, sizeof(basic)) ||
        standard.Directory != FALSE || standard.EndOfFile.QuadPart < 0 ||
        standard.NumberOfLinks == 0U) {
        return false;
    }
    snapshot.identity.volume_serial_number = file_id.VolumeSerialNumber;
    for (std::size_t index = 0U; index < snapshot.identity.file_id.size(); ++index) {
        snapshot.identity.file_id[index] =
            static_cast<std::byte>(file_id.FileId.Identifier[index]);
    }
    snapshot.size = static_cast<std::uint64_t>(standard.EndOfFile.QuadPart);
    snapshot.creation_time = basic.CreationTime.QuadPart;
    snapshot.last_write_time = basic.LastWriteTime.QuadPart;
    snapshot.change_time = basic.ChangeTime.QuadPart;
    snapshot.attributes = basic.FileAttributes;
    link_count = standard.NumberOfLinks;
    return true;
}

[[nodiscard]] bool query_final_path(
    const HANDLE handle,
    std::filesystem::path& result) noexcept
{
    constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
    const DWORD required = ::GetFinalPathNameByHandleW(handle, nullptr, 0U, flags);
    if (required == 0U || required > 32'768U) {
        return false;
    }
    std::wstring path(static_cast<std::size_t>(required) + 1U, L'\0');
    const DWORD written = ::GetFinalPathNameByHandleW(
        handle, path.data(), static_cast<DWORD>(path.size()), flags);
    if (written == 0U || written >= path.size()) {
        return false;
    }
    path.resize(written);
    result = std::filesystem::path{std::move(path)};
    return true;
}

[[nodiscard]] bool hash_handle(
    const HANDLE handle,
    std::array<std::byte, 32U>& digest) noexcept
{
    LARGE_INTEGER zero{};
    if (!::SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN)) {
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
    std::vector<UCHAR> hash_object(object_size);
    UniqueHash hash;
    if (::BCryptCreateHash(
            algorithm.get(), hash.put(), hash_object.data(),
            static_cast<ULONG>(hash_object.size()), nullptr, 0U, 0U) < 0) {
        return false;
    }
    std::array<UCHAR, 64U * 1'024U> buffer{};
    for (;;) {
        DWORD count = 0U;
        if (!::ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()),
                        &count, nullptr)) {
            return false;
        }
        if (count == 0U) {
            break;
        }
        if (::BCryptHashData(hash.get(), buffer.data(), count, 0U) < 0) {
            return false;
        }
    }
    return ::BCryptFinishHash(
               hash.get(), reinterpret_cast<PUCHAR>(digest.data()),
               static_cast<ULONG>(digest.size()), 0U) >= 0;
}

[[nodiscard]] std::string hex_prefix(
    const std::array<std::byte, 32U>& digest,
    const std::size_t bytes)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string result(bytes * 2U, '\0');
    for (std::size_t index = 0U; index < bytes; ++index) {
        const auto value = std::to_integer<unsigned int>(digest[index]);
        result[index * 2U] = digits[value >> 4U];
        result[index * 2U + 1U] = digits[value & 0x0fU];
    }
    return result;
}

[[nodiscard]] bool anonymized_profile_digest(
    const WindowsBinaryIdentity& identity,
    std::array<std::byte, 32U>& digest) noexcept
{
    static constexpr std::string_view domain =
        "hlclient.stock-binary-profile-fingerprint.v1\0";
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
            &returned, 0U) < 0 || object_size == 0U ||
        object_size > 1U * 1'024U * 1'024U) {
        return false;
    }
    std::vector<UCHAR> storage(object_size);
    UniqueHash hash;
    if (::BCryptCreateHash(
            algorithm.get(), hash.put(), storage.data(),
            static_cast<ULONG>(storage.size()), nullptr, 0U, 0U) < 0 ||
        ::BCryptHashData(
            hash.get(), reinterpret_cast<PUCHAR>(
                            const_cast<char*>(domain.data())),
            static_cast<ULONG>(domain.size()), 0U) < 0 ||
        ::BCryptHashData(
            hash.get(), reinterpret_cast<PUCHAR>(
                            const_cast<std::byte*>(identity.sha256.data())),
            static_cast<ULONG>(identity.sha256.size()), 0U) < 0 ||
        ::BCryptHashData(
            hash.get(), reinterpret_cast<PUCHAR>(
                            const_cast<std::uint64_t*>(&identity.snapshot.size)),
            sizeof(identity.snapshot.size), 0U) < 0) {
        return false;
    }
    const auto machine = static_cast<std::uint32_t>(identity.pe_machine);
    if (::BCryptHashData(
            hash.get(), reinterpret_cast<PUCHAR>(
                            const_cast<std::uint32_t*>(&machine)),
            sizeof(machine), 0U) < 0) {
        return false;
    }
    if (identity.file_version) {
        if (::BCryptHashData(
                hash.get(), reinterpret_cast<PUCHAR>(
                                const_cast<WindowsFileVersion*>(
                                    &*identity.file_version)),
                sizeof(WindowsFileVersion), 0U) < 0) {
            return false;
        }
    }
    return ::BCryptFinishHash(
               hash.get(), reinterpret_cast<PUCHAR>(digest.data()),
               static_cast<ULONG>(digest.size()), 0U) >= 0;
}

[[nodiscard]] bool read_file_version(
    const std::filesystem::path& path,
    WindowsFileVersion& version) noexcept
{
    DWORD ignored = 0U;
    const DWORD size = ::GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (size == 0U || size > 16U * 1'024U * 1'024U) {
        return false;
    }
    std::vector<std::byte> data(size);
    if (!::GetFileVersionInfoW(path.c_str(), 0U, size, data.data())) {
        return false;
    }
    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT fixed_size = 0U;
    if (!::VerQueryValueW(data.data(), L"\\",
                          reinterpret_cast<void**>(&fixed), &fixed_size) ||
        fixed == nullptr || fixed_size < sizeof(*fixed) ||
        fixed->dwSignature != VS_FFI_SIGNATURE) {
        return false;
    }
    version = WindowsFileVersion{
        static_cast<std::uint16_t>(HIWORD(fixed->dwFileVersionMS)),
        static_cast<std::uint16_t>(LOWORD(fixed->dwFileVersionMS)),
        static_cast<std::uint16_t>(HIWORD(fixed->dwFileVersionLS)),
        static_cast<std::uint16_t>(LOWORD(fixed->dwFileVersionLS)),
    };
    return true;
}

[[nodiscard]] bool read_pe_machine(
    const HANDLE handle,
    WindowsPeMachine& machine) noexcept
{
    LARGE_INTEGER zero{};
    if (!::SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN)) {
        return false;
    }
    IMAGE_DOS_HEADER dos{};
    DWORD count = 0U;
    if (!::ReadFile(handle, &dos, sizeof(dos), &count, nullptr) ||
        count != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < static_cast<LONG>(sizeof(dos)) ||
        dos.e_lfanew > 64 * 1'024 * 1'024) {
        return false;
    }
    LARGE_INTEGER offset{};
    offset.QuadPart = dos.e_lfanew;
    if (!::SetFilePointerEx(handle, offset, nullptr, FILE_BEGIN)) {
        return false;
    }
    DWORD signature = 0U;
    IMAGE_FILE_HEADER header{};
    if (!::ReadFile(handle, &signature, sizeof(signature), &count, nullptr) ||
        count != sizeof(signature) || signature != IMAGE_NT_SIGNATURE ||
        !::ReadFile(handle, &header, sizeof(header), &count, nullptr) ||
        count != sizeof(header)) {
        return false;
    }
    switch (header.Machine) {
    case IMAGE_FILE_MACHINE_I386:
        machine = WindowsPeMachine::x86;
        return true;
    case IMAGE_FILE_MACHINE_AMD64:
        machine = WindowsPeMachine::x64;
        return true;
    case IMAGE_FILE_MACHINE_ARM64:
        machine = WindowsPeMachine::arm64;
        return true;
    default:
        machine = WindowsPeMachine::unknown;
        return true;
    }
}

[[nodiscard]] bool verify_authenticode_offline(
    const std::filesystem::path& path) noexcept
{
    WINTRUST_FILE_INFO file{};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = path.c_str();

    WINTRUST_DATA data{};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL |
        WTD_REVOCATION_CHECK_NONE;

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status = ::WinVerifyTrust(nullptr, &action, &data);
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    static_cast<void>(::WinVerifyTrust(nullptr, &action, &data));
    return status == ERROR_SUCCESS;
}

[[nodiscard]] bool ordinal_path_equal(
    const std::filesystem::path& left,
    const std::filesystem::path& right) noexcept
{
    const auto a = left.wstring();
    const auto b = right.wstring();
    if (a.size() != b.size() ||
        a.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    return ::CompareStringOrdinal(
               a.data(), static_cast<int>(a.size()),
               b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

struct ManifestToken final {
    enum class Kind { text, open_brace, close_brace } kind{Kind::text};
    std::string text;
};

[[nodiscard]] bool tokenize_manifest(
    const std::string_view input,
    std::vector<ManifestToken>& tokens) noexcept
{
    constexpr std::size_t maximum_tokens = 65'536U;
    std::size_t offset = 0U;
    while (offset < input.size() && tokens.size() < maximum_tokens) {
        const unsigned char byte = static_cast<unsigned char>(input[offset]);
        if (byte == 0U) {
            return false;
        }
        if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
            ++offset;
            continue;
        }
        if (input[offset] == '{' || input[offset] == '}') {
            tokens.push_back(ManifestToken{
                input[offset] == '{' ? ManifestToken::Kind::open_brace
                                     : ManifestToken::Kind::close_brace,
                {}});
            ++offset;
            continue;
        }
        if (input[offset] != '"') {
            return false;
        }
        ++offset;
        std::string text;
        while (offset < input.size() && input[offset] != '"') {
            const char value = input[offset++];
            if (value == '\\') {
                if (offset >= input.size() ||
                    (input[offset] != '\\' && input[offset] != '"')) {
                    return false;
                }
                text.push_back(input[offset++]);
            } else {
                const unsigned char character = static_cast<unsigned char>(value);
                if (character < 0x20U || text.size() >= 4'096U) {
                    return false;
                }
                text.push_back(value);
            }
        }
        if (offset >= input.size() || input[offset] != '"') {
            return false;
        }
        ++offset;
        tokens.push_back(ManifestToken{ManifestToken::Kind::text, std::move(text)});
    }
    return offset == input.size() && !tokens.empty() &&
           tokens.size() < maximum_tokens;
}

template<typename Integer>
[[nodiscard]] bool parse_decimal(const std::string_view text, Integer& value) noexcept
{
    if (text.empty()) {
        return false;
    }
    Integer parsed{};
    const auto parsed_result = std::from_chars(
        text.data(), text.data() + text.size(), parsed, 10);
    if (parsed_result.ec != std::errc{} ||
        parsed_result.ptr != text.data() + text.size()) {
        return false;
    }
    value = parsed;
    return true;
}

} // namespace

std::string to_string(const WindowsFileVersion& version)
{
    return std::to_string(version.major) + "." + std::to_string(version.minor) +
        "." + std::to_string(version.patch) + "." +
        std::to_string(version.build);
}

std::string_view to_string(const WindowsPeMachine machine) noexcept
{
    switch (machine) {
    case WindowsPeMachine::x86: return "x86";
    case WindowsPeMachine::x64: return "x64";
    case WindowsPeMachine::arm64: return "arm64";
    case WindowsPeMachine::unknown: return "unknown";
    }
    return "unknown";
}

std::string_view to_string(const WindowsBinaryIdentityErrorCode code) noexcept
{
    switch (code) {
    case WindowsBinaryIdentityErrorCode::none: return "none";
    case WindowsBinaryIdentityErrorCode::empty_path: return "empty-path";
    case WindowsBinaryIdentityErrorCode::path_not_absolute: return "path-not-absolute";
    case WindowsBinaryIdentityErrorCode::alternate_data_stream: return "alternate-data-stream";
    case WindowsBinaryIdentityErrorCode::reparse_point: return "reparse-point";
    case WindowsBinaryIdentityErrorCode::open_failed: return "open-failed";
    case WindowsBinaryIdentityErrorCode::not_regular_file: return "not-regular-file";
    case WindowsBinaryIdentityErrorCode::hardlink_rejected: return "hardlink-rejected";
    case WindowsBinaryIdentityErrorCode::identity_query_failed: return "identity-query-failed";
    case WindowsBinaryIdentityErrorCode::canonical_path_failed: return "canonical-path-failed";
    case WindowsBinaryIdentityErrorCode::file_too_large: return "file-too-large";
    case WindowsBinaryIdentityErrorCode::read_failed: return "read-failed";
    case WindowsBinaryIdentityErrorCode::file_changed: return "file-changed";
    case WindowsBinaryIdentityErrorCode::digest_failed: return "digest-failed";
    case WindowsBinaryIdentityErrorCode::version_missing: return "version-missing";
    case WindowsBinaryIdentityErrorCode::malformed_pe: return "malformed-pe";
    case WindowsBinaryIdentityErrorCode::unsupported_machine: return "unsupported-machine";
    case WindowsBinaryIdentityErrorCode::authenticode_invalid: return "authenticode-invalid";
    case WindowsBinaryIdentityErrorCode::process_image_query_failed: return "process-image-query-failed";
    case WindowsBinaryIdentityErrorCode::process_image_mismatch: return "process-image-mismatch";
    }
    return "unknown";
}

std::string_view to_string(const SteamAppManifestErrorCode code) noexcept
{
    switch (code) {
    case SteamAppManifestErrorCode::none: return "none";
    case SteamAppManifestErrorCode::unsafe_path: return "unsafe-path";
    case SteamAppManifestErrorCode::open_failed: return "open-failed";
    case SteamAppManifestErrorCode::too_large: return "too-large";
    case SteamAppManifestErrorCode::read_failed: return "read-failed";
    case SteamAppManifestErrorCode::changed: return "changed";
    case SteamAppManifestErrorCode::malformed: return "malformed";
    case SteamAppManifestErrorCode::duplicate_field: return "duplicate-field";
    case SteamAppManifestErrorCode::missing_field: return "missing-field";
    case SteamAppManifestErrorCode::unexpected_app_id: return "unexpected-app-id";
    case SteamAppManifestErrorCode::unexpected_build_id: return "unexpected-build-id";
    }
    return "unknown";
}

std::string_view to_string(const StockBinaryProfileErrorCode code) noexcept
{
    switch (code) {
    case StockBinaryProfileErrorCode::none: return "none";
    case StockBinaryProfileErrorCode::client_identity_invalid: return "client-identity-invalid";
    case StockBinaryProfileErrorCode::client_version_mismatch: return "client-version-mismatch";
    case StockBinaryProfileErrorCode::client_machine_mismatch: return "client-machine-mismatch";
    case StockBinaryProfileErrorCode::client_signature_invalid: return "client-signature-invalid";
    case StockBinaryProfileErrorCode::server_identity_invalid: return "server-identity-invalid";
    case StockBinaryProfileErrorCode::server_version_mismatch: return "server-version-mismatch";
    case StockBinaryProfileErrorCode::server_machine_mismatch: return "server-machine-mismatch";
    case StockBinaryProfileErrorCode::server_signature_invalid: return "server-signature-invalid";
    case StockBinaryProfileErrorCode::app_manifest_invalid: return "app-manifest-invalid";
    }
    return "unknown";
}

WindowsBinaryIdentityResult observe_windows_binary_identity(
    const std::filesystem::path& path,
    const std::uint64_t maximum_file_bytes,
    const WindowsBinaryObservationPolicy policy) noexcept
{
    try {
        if (path.empty()) {
            return binary_error(WindowsBinaryIdentityErrorCode::empty_path);
        }
        if (!path.is_absolute()) {
            return binary_error(WindowsBinaryIdentityErrorCode::path_not_absolute);
        }
        const auto native = path.wstring();
        if (has_alternate_stream_syntax(native)) {
            return binary_error(WindowsBinaryIdentityErrorCode::alternate_data_stream);
        }
        if (contains_reparse_component(path)) {
            return binary_error(WindowsBinaryIdentityErrorCode::reparse_point);
        }
        UniqueHandle file{::CreateFileW(
            path.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
        if (!file) {
            return binary_error(
                WindowsBinaryIdentityErrorCode::open_failed, ::GetLastError());
        }
        WindowsFileSnapshot before{};
        std::uint32_t links = 0U;
        if (!query_snapshot(file.get(), before, links)) {
            return binary_error(WindowsBinaryIdentityErrorCode::identity_query_failed,
                                ::GetLastError());
        }
        if ((before.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
            return binary_error(WindowsBinaryIdentityErrorCode::not_regular_file);
        }
        if (links != 1U) {
            return binary_error(WindowsBinaryIdentityErrorCode::hardlink_rejected);
        }
        if (before.size == 0U || before.size > maximum_file_bytes) {
            return binary_error(WindowsBinaryIdentityErrorCode::file_too_large);
        }

        WindowsBinaryIdentity identity;
        if (!query_final_path(file.get(), identity.canonical_path)) {
            return binary_error(WindowsBinaryIdentityErrorCode::canonical_path_failed,
                                ::GetLastError());
        }
        if (contains_reparse_component(identity.canonical_path)) {
            return binary_error(WindowsBinaryIdentityErrorCode::reparse_point,
                                ::GetLastError());
        }
        if (!has_only_default_data_stream(identity.canonical_path)) {
            return binary_error(
                WindowsBinaryIdentityErrorCode::alternate_data_stream,
                ::GetLastError());
        }
        if (!hash_handle(file.get(), identity.sha256)) {
            return binary_error(WindowsBinaryIdentityErrorCode::digest_failed,
                                ::GetLastError());
        }
        WindowsFileVersion file_version{};
        if (read_file_version(identity.canonical_path, file_version)) {
            identity.file_version = file_version;
        } else if (policy.file_version_required) {
            return binary_error(WindowsBinaryIdentityErrorCode::version_missing,
                                ::GetLastError());
        }
        if (!read_pe_machine(file.get(), identity.pe_machine)) {
            return binary_error(WindowsBinaryIdentityErrorCode::malformed_pe,
                                ::GetLastError());
        }
        identity.authenticode_valid =
            verify_authenticode_offline(identity.canonical_path);
        if (!identity.authenticode_valid &&
            policy.authenticode == AuthenticodePolicy::required) {
            return binary_error(WindowsBinaryIdentityErrorCode::authenticode_invalid);
        }

        WindowsFileSnapshot after{};
        if (!query_snapshot(file.get(), after, links) || before != after || links != 1U) {
            return binary_error(WindowsBinaryIdentityErrorCode::file_changed,
                                ::GetLastError());
        }
        identity.snapshot = after;
        // Deliberately not the raw digest: this bounded token is suitable only
        // for correlating a profile inside project metadata.
        std::array<std::byte, 32U> profile_digest{};
        if (!anonymized_profile_digest(identity, profile_digest)) {
            return binary_error(WindowsBinaryIdentityErrorCode::digest_failed);
        }
        identity.anonymized_profile_fingerprint =
            hex_prefix(profile_digest, profile_digest.size());
        return WindowsBinaryIdentityResult{std::move(identity),
                                           WindowsBinaryIdentityErrorCode::none,
                                           0U};
    } catch (...) {
        return binary_error(WindowsBinaryIdentityErrorCode::read_failed);
    }
}

WindowsBinaryIdentityResult verify_windows_process_image_identity(
    void* process_handle,
    const WindowsBinaryIdentity& expected,
    const std::uint64_t maximum_file_bytes,
    const WindowsBinaryObservationPolicy policy) noexcept
{
    if (process_handle == nullptr || process_handle == INVALID_HANDLE_VALUE) {
        return binary_error(
            WindowsBinaryIdentityErrorCode::process_image_query_failed,
            ERROR_INVALID_HANDLE);
    }
    std::wstring image(32'768U, L'\0');
    DWORD size = static_cast<DWORD>(image.size());
    if (!::QueryFullProcessImageNameW(
            static_cast<HANDLE>(process_handle), 0U, image.data(), &size) ||
        size == 0U || size >= image.size()) {
        return binary_error(
            WindowsBinaryIdentityErrorCode::process_image_query_failed,
            ::GetLastError());
    }
    image.resize(size);
    auto observed = observe_windows_binary_identity(
        std::filesystem::path{std::move(image)}, maximum_file_bytes, policy);
    if (!observed) {
        return observed;
    }
    if (!ordinal_path_equal(observed.identity->canonical_path,
                            expected.canonical_path) ||
        !same_windows_file_identity(*observed.identity, expected)) {
        return binary_error(
            WindowsBinaryIdentityErrorCode::process_image_mismatch);
    }
    return observed;
}

bool same_windows_file_identity(
    const WindowsBinaryIdentity& left,
    const WindowsBinaryIdentity& right) noexcept
{
    return left.snapshot == right.snapshot && left.sha256 == right.sha256 &&
           left.file_version == right.file_version &&
           left.pe_machine == right.pe_machine &&
           left.authenticode_valid == right.authenticode_valid;
}

SteamAppManifestResult observe_steam_app_manifest_70(
    const std::filesystem::path& path,
    const std::uint64_t maximum_file_bytes) noexcept
{
    try {
        if (path.empty() || !path.is_absolute() ||
            has_alternate_stream_syntax(path.wstring()) ||
            contains_reparse_component(path)) {
            return manifest_error(SteamAppManifestErrorCode::unsafe_path);
        }
        UniqueHandle file{::CreateFileW(
            path.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
        if (!file) {
            return manifest_error(
                SteamAppManifestErrorCode::open_failed, ::GetLastError());
        }
        WindowsFileSnapshot before{};
        std::uint32_t links = 0U;
        if (!query_snapshot(file.get(), before, links) || links != 1U) {
            return manifest_error(SteamAppManifestErrorCode::unsafe_path,
                                  ::GetLastError());
        }
        if (!has_only_default_data_stream(path)) {
            return manifest_error(SteamAppManifestErrorCode::unsafe_path);
        }
        if (before.size == 0U || before.size > maximum_file_bytes ||
            before.size > static_cast<std::uint64_t>(
                (std::numeric_limits<std::size_t>::max)())) {
            return manifest_error(SteamAppManifestErrorCode::too_large);
        }
        std::vector<char> data(static_cast<std::size_t>(before.size));
        DWORD total = 0U;
        while (total < data.size()) {
            DWORD read = 0U;
            const auto remaining = static_cast<DWORD>((std::min<std::size_t>)(
                data.size() - total, 64U * 1'024U));
            if (!::ReadFile(file.get(), data.data() + total, remaining,
                            &read, nullptr) || read == 0U) {
                return manifest_error(
                    SteamAppManifestErrorCode::read_failed, ::GetLastError());
            }
            total += read;
        }
        WindowsFileSnapshot after{};
        if (!query_snapshot(file.get(), after, links) || before != after || links != 1U) {
            return manifest_error(SteamAppManifestErrorCode::changed,
                                  ::GetLastError());
        }

        std::vector<ManifestToken> tokens;
        if (!tokenize_manifest(std::string_view{data.data(), data.size()}, tokens) ||
            tokens.size() < 4U ||
            tokens[0U].kind != ManifestToken::Kind::text ||
            tokens[0U].text != "AppState" ||
            tokens[1U].kind != ManifestToken::Kind::open_brace) {
            return manifest_error(SteamAppManifestErrorCode::malformed);
        }
        std::optional<std::uint32_t> app_id;
        std::optional<std::uint64_t> build_id;
        std::size_t depth = 1U;
        for (std::size_t index = 2U; index < tokens.size();) {
            const auto& token = tokens[index];
            if (token.kind == ManifestToken::Kind::close_brace) {
                if (depth == 0U) {
                    return manifest_error(SteamAppManifestErrorCode::malformed);
                }
                --depth;
                ++index;
                if (depth == 0U && index != tokens.size()) {
                    return manifest_error(SteamAppManifestErrorCode::malformed);
                }
                continue;
            }
            if (token.kind != ManifestToken::Kind::text ||
                index + 1U >= tokens.size()) {
                return manifest_error(SteamAppManifestErrorCode::malformed);
            }
            const auto& value = tokens[index + 1U];
            if (value.kind == ManifestToken::Kind::open_brace) {
                if (++depth > 64U) {
                    return manifest_error(SteamAppManifestErrorCode::malformed);
                }
                index += 2U;
                continue;
            }
            if (value.kind != ManifestToken::Kind::text) {
                return manifest_error(SteamAppManifestErrorCode::malformed);
            }
            if (token.text == "appid") {
                if (app_id) {
                    return manifest_error(
                        SteamAppManifestErrorCode::duplicate_field);
                }
                std::uint32_t parsed = 0U;
                if (!parse_decimal(value.text, parsed)) {
                    return manifest_error(SteamAppManifestErrorCode::malformed);
                }
                app_id = parsed;
            } else if (token.text == "buildid") {
                if (build_id) {
                    return manifest_error(
                        SteamAppManifestErrorCode::duplicate_field);
                }
                std::uint64_t parsed = 0U;
                if (!parse_decimal(value.text, parsed)) {
                    return manifest_error(SteamAppManifestErrorCode::malformed);
                }
                build_id = parsed;
            }
            index += 2U;
        }
        if (depth != 0U) {
            return manifest_error(SteamAppManifestErrorCode::malformed);
        }
        if (!app_id || !build_id) {
            return manifest_error(SteamAppManifestErrorCode::missing_field);
        }
        if (*app_id != 70U) {
            return manifest_error(SteamAppManifestErrorCode::unexpected_app_id);
        }
        if (*build_id != 15'961'492U) {
            return manifest_error(
                SteamAppManifestErrorCode::unexpected_build_id);
        }
        return SteamAppManifestResult{
            SteamAppManifestObservation{*app_id, *build_id, after},
            SteamAppManifestErrorCode::none, 0U};
    } catch (...) {
        return manifest_error(SteamAppManifestErrorCode::read_failed);
    }
}

StockBinaryProfileResult validate_required_stock_binary_profile_observations(
    const WindowsBinaryIdentity& client,
    const WindowsBinaryIdentity& server_launcher,
    const SteamAppManifestObservation& app_manifest) noexcept
{
    try {
        constexpr WindowsFileVersion expected_client{1U, 1U, 1U, 1U};
        if (client.file_version != expected_client) {
            return {std::nullopt,
                    StockBinaryProfileErrorCode::client_version_mismatch,
                    WindowsBinaryIdentityErrorCode::none,
                    SteamAppManifestErrorCode::none};
        }
        if (client.pe_machine != WindowsPeMachine::x86) {
            return {std::nullopt,
                    StockBinaryProfileErrorCode::client_machine_mismatch,
                    WindowsBinaryIdentityErrorCode::none,
                    SteamAppManifestErrorCode::none};
        }
        if (!client.authenticode_valid) {
            return {std::nullopt,
                    StockBinaryProfileErrorCode::client_signature_invalid,
                    WindowsBinaryIdentityErrorCode::authenticode_invalid,
                    SteamAppManifestErrorCode::none};
        }
        constexpr WindowsFileVersion expected_server{4U, 1U, 1U, 1U};
        if (server_launcher.file_version != expected_server) {
            return {std::nullopt,
                    StockBinaryProfileErrorCode::server_version_mismatch,
                    WindowsBinaryIdentityErrorCode::none,
                    SteamAppManifestErrorCode::none};
        }
        if (server_launcher.pe_machine != WindowsPeMachine::x86) {
            return {std::nullopt,
                    StockBinaryProfileErrorCode::server_machine_mismatch,
                    WindowsBinaryIdentityErrorCode::none,
                    SteamAppManifestErrorCode::none};
        }
        if (!server_launcher.authenticode_valid) {
            return {std::nullopt,
                    StockBinaryProfileErrorCode::server_signature_invalid,
                    WindowsBinaryIdentityErrorCode::authenticode_invalid,
                    SteamAppManifestErrorCode::none};
        }
        if (app_manifest.app_id != 70U ||
            app_manifest.build_id != 15'961'492U) {
            return {std::nullopt,
                    StockBinaryProfileErrorCode::app_manifest_invalid,
                    WindowsBinaryIdentityErrorCode::none,
                    app_manifest.app_id != 70U
                        ? SteamAppManifestErrorCode::unexpected_app_id
                        : SteamAppManifestErrorCode::unexpected_build_id};
        }
        StockBinaryProfileObservation observation;
        observation.client_file_version = *client.file_version;
        observation.server_launcher_version = *server_launcher.file_version;
        observation.steam_app_id = app_manifest.app_id;
        observation.steam_build_id = app_manifest.build_id;
        observation.client_machine = client.pe_machine;
        observation.server_machine = server_launcher.pe_machine;
        observation.client_signature_valid = client.authenticode_valid;
        observation.server_signature_valid =
            server_launcher.authenticode_valid;
        observation.client_profile_fingerprint =
            client.anonymized_profile_fingerprint;
        observation.server_profile_fingerprint =
            server_launcher.anonymized_profile_fingerprint;
        observation.evidence_status = StockBinaryProfileEvidenceStatus::observed;
        return {std::move(observation), StockBinaryProfileErrorCode::none,
                WindowsBinaryIdentityErrorCode::none,
                SteamAppManifestErrorCode::none};
    } catch (...) {
        return {std::nullopt, StockBinaryProfileErrorCode::client_identity_invalid,
                WindowsBinaryIdentityErrorCode::read_failed,
                SteamAppManifestErrorCode::none};
    }
}

StockBinaryProfileResult observe_required_stock_binary_profile(
    const std::filesystem::path& client,
    const std::filesystem::path& server_launcher,
    const std::filesystem::path& app_manifest) noexcept
{
    const auto client_identity = observe_windows_binary_identity(client);
    if (!client_identity) {
        return {std::nullopt, StockBinaryProfileErrorCode::client_identity_invalid,
                client_identity.code, SteamAppManifestErrorCode::none};
    }
    const auto server_identity = observe_windows_binary_identity(server_launcher);
    if (!server_identity) {
        return {std::nullopt, StockBinaryProfileErrorCode::server_identity_invalid,
                server_identity.code, SteamAppManifestErrorCode::none};
    }
    const auto manifest = observe_steam_app_manifest_70(app_manifest);
    if (!manifest) {
        return {std::nullopt, StockBinaryProfileErrorCode::app_manifest_invalid,
                WindowsBinaryIdentityErrorCode::none, manifest.code};
    }
    return validate_required_stock_binary_profile_observations(
        *client_identity.identity, *server_identity.identity,
        *manifest.observation);
}

} // namespace hlclient::platform::windows
