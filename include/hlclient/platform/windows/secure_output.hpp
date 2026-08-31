#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::platform::windows {

enum class SecureOutputErrorCode {
    invalid_directory_path,
    directory_open_failed,
    directory_identity_invalid,
    directory_lock_failed,
    invalid_leaf_name,
    random_generation_failed,
    temporary_file_create_failed,
    temporary_file_identity_invalid,
    write_failed,
    flush_failed,
    destination_exists,
    publish_failed,
    published_file_identity_invalid,
    published_file_delete_failed,
};

struct SecureOutputError final {
    SecureOutputErrorCode code{SecureOutputErrorCode::invalid_directory_path};
    unsigned long operating_system_error{0U};
};

struct SecureOutputDirectoryOpenResult;
struct SecureOutputWriteResult;
class SecureOutputPublishedFile;

// Keeps the exact output directory and a private delete-on-close child lock
// open without FILE_SHARE_DELETE. The child prevents replacement/rename of the
// directory and each of its ancestors for the lifetime of the capability.
class SecureOutputDirectory final {
public:
    SecureOutputDirectory() noexcept = default;
    SecureOutputDirectory(const SecureOutputDirectory&) = delete;
    SecureOutputDirectory& operator=(const SecureOutputDirectory&) = delete;
    SecureOutputDirectory(SecureOutputDirectory&& other) noexcept;
    SecureOutputDirectory& operator=(SecureOutputDirectory&& other) noexcept;
    ~SecureOutputDirectory();

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] const std::filesystem::path& canonical_path() const noexcept;

private:
    friend struct SecureOutputDirectoryOpenResult;
    friend struct SecureOutputWriteResult;
    friend SecureOutputDirectoryOpenResult open_secure_output_directory(
        const std::filesystem::path&) noexcept;
    friend SecureOutputWriteResult secure_atomic_write_new(
        const SecureOutputDirectory&,
        std::wstring_view,
        std::span<const std::byte>) noexcept;
    friend SecureOutputWriteResult secure_atomic_write_new(
        const SecureOutputDirectory&,
        std::wstring_view,
        std::span<const std::byte>,
        SecureOutputPublishedFile&) noexcept;

    SecureOutputDirectory(
        std::vector<void*> native_handles,
        std::filesystem::path canonical_path) noexcept;
    void close() noexcept;

    // The first handle is the exact output directory. The second is a private
    // random child lock which is removed automatically when the capability is
    // closed. Both identities are revalidated before the capability is
    // published to the caller.
    std::vector<void*> native_handles_;
    std::filesystem::path canonical_path_;
};

// Retains the exact newly published file handle with DELETE access and without
// FILE_SHARE_DELETE. Callers that need to validate a manifest-last directory
// shape can therefore keep the published identity pinned through validation,
// then either close it as committed or mark that exact file delete-on-close.
class SecureOutputPublishedFile final {
public:
    SecureOutputPublishedFile() noexcept = default;
    SecureOutputPublishedFile(const SecureOutputPublishedFile&) = delete;
    SecureOutputPublishedFile& operator=(
        const SecureOutputPublishedFile&) = delete;
    SecureOutputPublishedFile(SecureOutputPublishedFile&& other) noexcept;
    SecureOutputPublishedFile& operator=(
        SecureOutputPublishedFile&& other) noexcept;
    ~SecureOutputPublishedFile();

    [[nodiscard]] bool valid() const noexcept;
    void close() noexcept;
    [[nodiscard]] SecureOutputWriteResult remove_on_close() noexcept;

private:
    friend SecureOutputWriteResult secure_atomic_write_new(
        const SecureOutputDirectory&,
        std::wstring_view,
        std::span<const std::byte>,
        SecureOutputPublishedFile&) noexcept;

    explicit SecureOutputPublishedFile(void* native_handle) noexcept;
    void* native_handle_{nullptr};
};

struct SecureOutputDirectoryOpenResult final {
    std::optional<SecureOutputDirectory> directory;
    std::optional<SecureOutputError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return directory.has_value();
    }
};

struct SecureOutputWriteResult final {
    std::optional<SecureOutputError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

[[nodiscard]] SecureOutputDirectoryOpenResult open_secure_output_directory(
    const std::filesystem::path& path) noexcept;

// Publishes a brand-new ordinary file in an already held directory. A
// cryptographically random CREATE_NEW temporary file is written, flushed,
// verified by handle, and renamed without replacement while the same handle
// remains open. Existing destinations are never truncated or replaced.
[[nodiscard]] SecureOutputWriteResult secure_atomic_write_new(
    const SecureOutputDirectory& directory,
    std::wstring_view leaf_name,
    std::span<const std::byte> bytes) noexcept;

// Retained-handle overload. `published_file` must be empty. On success it owns
// the exact renamed handle until close() or remove_on_close().
[[nodiscard]] SecureOutputWriteResult secure_atomic_write_new(
    const SecureOutputDirectory& directory,
    std::wstring_view leaf_name,
    std::span<const std::byte> bytes,
    SecureOutputPublishedFile& published_file) noexcept;

[[nodiscard]] constexpr std::string_view to_string(
    const SecureOutputErrorCode code) noexcept
{
    switch (code) {
    case SecureOutputErrorCode::invalid_directory_path:
        return "invalid_directory_path";
    case SecureOutputErrorCode::directory_open_failed:
        return "directory_open_failed";
    case SecureOutputErrorCode::directory_identity_invalid:
        return "directory_identity_invalid";
    case SecureOutputErrorCode::directory_lock_failed:
        return "directory_lock_failed";
    case SecureOutputErrorCode::invalid_leaf_name:
        return "invalid_leaf_name";
    case SecureOutputErrorCode::random_generation_failed:
        return "random_generation_failed";
    case SecureOutputErrorCode::temporary_file_create_failed:
        return "temporary_file_create_failed";
    case SecureOutputErrorCode::temporary_file_identity_invalid:
        return "temporary_file_identity_invalid";
    case SecureOutputErrorCode::write_failed:
        return "write_failed";
    case SecureOutputErrorCode::flush_failed:
        return "flush_failed";
    case SecureOutputErrorCode::destination_exists:
        return "destination_exists";
    case SecureOutputErrorCode::publish_failed:
        return "publish_failed";
    case SecureOutputErrorCode::published_file_identity_invalid:
        return "published_file_identity_invalid";
    case SecureOutputErrorCode::published_file_delete_failed:
        return "published_file_delete_failed";
    }
    return "unknown";
}

} // namespace hlclient::platform::windows
