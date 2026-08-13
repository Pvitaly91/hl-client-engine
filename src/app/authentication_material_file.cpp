#include <hlclient/app/authentication_material_file.hpp>

#include <array>
#include <fstream>
#include <ios>
#include <span>
#include <system_error>
#include <utility>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#endif

namespace hlclient::app {
namespace {

[[nodiscard]] AuthenticationMaterialFileLoadResult failure(
    const AuthenticationMaterialFileErrorCode code,
    std::string context)
{
    return AuthenticationMaterialFileLoadResult{
        std::nullopt,
        AuthenticationMaterialFileError{code, std::move(context)},
    };
}

#ifdef _WIN32

class ScopedFileHandle final {
public:
    explicit ScopedFileHandle(const HANDLE value) noexcept : value_{value} {}

    ~ScopedFileHandle()
    {
        if (value_ != INVALID_HANDLE_VALUE) {
            static_cast<void>(::CloseHandle(value_));
        }
    }

    ScopedFileHandle(const ScopedFileHandle&) = delete;
    ScopedFileHandle& operator=(const ScopedFileHandle&) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

[[nodiscard]] AuthenticationMaterialFileLoadResult load_bounded_record(
    const std::filesystem::path& path,
    std::array<std::byte, kAuthenticationMaterialFileSize + 1U>& bytes)
{
    const auto& native_path = path.native();
    if (native_path.empty() || native_path.find(L'\0') != std::wstring::npos) {
        return failure(
            AuthenticationMaterialFileErrorCode::open_failed,
            "Unable to open the local authentication material input");
    }

    // Deny sharing while the bounded record is inspected and read. Opening the
    // final path component itself prevents a path swap from silently following
    // a symbolic link or other reparse point between separate filesystem calls.
    const ScopedFileHandle file{::CreateFileW(
        native_path.c_str(),
        GENERIC_READ,
        0U,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT |
            FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr)};
    if (!file) {
        return failure(
            AuthenticationMaterialFileErrorCode::open_failed,
            "Unable to open the local authentication material input");
    }

    BY_HANDLE_FILE_INFORMATION information{};
    if (::GetFileType(file.get()) != FILE_TYPE_DISK ||
        !::GetFileInformationByHandle(file.get(), &information) ||
        (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) !=
            0U) {
        return failure(
            AuthenticationMaterialFileErrorCode::not_regular_file,
            "Authentication material input must be a regular local file");
    }

    DWORD bytes_read = 0U;
    if (!::ReadFile(
            file.get(),
            bytes.data(),
            static_cast<DWORD>(bytes.size()),
            &bytes_read,
            nullptr)) {
        return failure(
            AuthenticationMaterialFileErrorCode::read_failed,
            "Unable to read the bounded authentication material input");
    }
    if (bytes_read != static_cast<DWORD>(kAuthenticationMaterialFileSize)) {
        return failure(
            AuthenticationMaterialFileErrorCode::invalid_size,
            "Authentication material input must contain exactly 245 bytes");
    }

    return AuthenticationMaterialFileLoadResult{};
}

#endif

} // namespace

AuthenticationMaterialFileLoadResult::operator bool() const noexcept
{
    return material.has_value();
}

AuthenticationMaterialFileLoadResult load_authentication_material_file(
    const std::filesystem::path& path)
{
#ifdef _WIN32
    std::array<std::byte, kAuthenticationMaterialFileSize + 1U> bytes{};
    auto loaded = load_bounded_record(path, bytes);
    if (loaded.error) {
        return loaded;
    }
#else
    std::error_code status_error;
    const auto status = std::filesystem::symlink_status(path, status_error);
    if (status_error || !std::filesystem::is_regular_file(status)) {
        return failure(
            AuthenticationMaterialFileErrorCode::not_regular_file,
            "Authentication material input must be a regular local file");
    }

    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return failure(
            AuthenticationMaterialFileErrorCode::open_failed,
            "Unable to open the local authentication material input");
    }

    // Reading one byte beyond the accepted record proves both lower and upper
    // bounds from this open stream without a file_size-then-reopen race.
    std::array<std::byte, kAuthenticationMaterialFileSize + 1U> bytes{};
    stream.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    const auto bytes_read = stream.gcount();
    if (stream.bad()) {
        return failure(
            AuthenticationMaterialFileErrorCode::read_failed,
            "Unable to read the bounded authentication material input");
    }
    if (bytes_read != static_cast<std::streamsize>(kAuthenticationMaterialFileSize)) {
        return failure(
            AuthenticationMaterialFileErrorCode::invalid_size,
            "Authentication material input must contain exactly 245 bytes");
    }
    if (!stream.eof()) {
        return failure(
            AuthenticationMaterialFileErrorCode::read_failed,
            "Unable to establish the end of the bounded authentication material input");
    }
#endif

    const std::span<const std::byte> record{bytes.data(), kAuthenticationMaterialFileSize};
    auto created = goldsrc::AuthenticationMaterial::create(
        record.first(kAuthenticationMaterialProtectedFileSize),
        record.subspan(kAuthenticationMaterialProtectedFileSize));
    if (!created) {
        return failure(
            AuthenticationMaterialFileErrorCode::invalid_material,
            "Authentication material input has an invalid protected region");
    }

    return AuthenticationMaterialFileLoadResult{
        std::move(*created.value),
        std::nullopt,
    };
}

} // namespace hlclient::app
