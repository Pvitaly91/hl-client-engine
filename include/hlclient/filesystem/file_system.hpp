#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hlclient::filesystem {

struct FileMetadata {
    std::uintmax_t size{0};
    std::optional<std::filesystem::file_time_type> last_modified;
};

struct FileContents {
    std::filesystem::path virtual_path;
    std::vector<std::byte> bytes;
    std::optional<FileMetadata> metadata;
};

enum class FileReadErrorCode {
    InvalidConfiguration,
    InvalidVirtualPath,
    NotFound,
    NotRegularFile,
    TooLarge,
    IoError,
};

struct FileReadError {
    FileReadErrorCode code{FileReadErrorCode::IoError};
    std::filesystem::path virtual_path;
    std::string context;
};

struct FileReadResult {
    std::optional<FileContents> file;
    std::optional<FileReadError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return file.has_value();
    }
};

class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    IFileSystem(const IFileSystem&) = delete;
    IFileSystem& operator=(const IFileSystem&) = delete;
    IFileSystem(IFileSystem&&) = delete;
    IFileSystem& operator=(IFileSystem&&) = delete;

    [[nodiscard]] virtual FileReadResult read_file(
        const std::filesystem::path& virtual_path) const = 0;

protected:
    IFileSystem() = default;
};

} // namespace hlclient::filesystem
