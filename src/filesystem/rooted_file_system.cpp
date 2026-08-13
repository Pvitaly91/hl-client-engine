#include <hlclient/filesystem/rooted_file_system.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>
#include <utility>

namespace hlclient::filesystem {
namespace {

[[nodiscard]] bool contains_nul(const std::filesystem::path& path) noexcept
{
    const auto& native = path.native();
    return std::find(native.begin(), native.end(), std::filesystem::path::value_type{}) !=
           native.end();
}

[[nodiscard]] bool is_safe_virtual_path(const std::filesystem::path& path) noexcept
{
    if (path.empty() || path.is_absolute() || path.has_root_path() || contains_nul(path)) {
        return false;
    }
    for (const auto& component : path) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_within(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) noexcept
{
    const auto mismatch = std::mismatch(root.begin(), root.end(), candidate.begin(), candidate.end());
    return mismatch.first == root.end();
}

[[nodiscard]] FileReadResult failure(
    const FileReadErrorCode code,
    std::filesystem::path virtual_path,
    std::string context)
{
    return FileReadResult{
        std::nullopt,
        FileReadError{code, std::move(virtual_path), std::move(context)},
    };
}

} // namespace

RootedFileSystemCreateResult RootedFileSystem::create(
    const std::filesystem::path& root,
    const std::uintmax_t maximum_file_size)
{
    if (maximum_file_size == 0U) {
        return RootedFileSystemCreateResult{
            nullptr,
            FileReadError{
                FileReadErrorCode::InvalidConfiguration,
                {},
                "Maximum file size must be greater than zero",
            },
        };
    }

    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) {
        return RootedFileSystemCreateResult{
            nullptr,
            FileReadError{
                FileReadErrorCode::NotFound,
                {},
                "Filesystem root does not exist or is not a directory",
            },
        };
    }

    const auto canonical_root = std::filesystem::canonical(root, error);
    if (error) {
        return RootedFileSystemCreateResult{
            nullptr,
            FileReadError{
                FileReadErrorCode::IoError,
                {},
                "Unable to normalize filesystem root: " + error.message(),
            },
        };
    }

    return RootedFileSystemCreateResult{
        std::unique_ptr<RootedFileSystem>{
            new RootedFileSystem{canonical_root, maximum_file_size}},
        std::nullopt,
    };
}

RootedFileSystem::RootedFileSystem(
    std::filesystem::path root,
    const std::uintmax_t maximum_file_size)
    : root_{std::move(root)}, maximum_file_size_{maximum_file_size}
{
}

FileReadResult RootedFileSystem::read_file(const std::filesystem::path& virtual_path) const
{
    if (!is_safe_virtual_path(virtual_path)) {
        return failure(
            FileReadErrorCode::InvalidVirtualPath,
            virtual_path,
            "Virtual file path must be relative and must not contain '..' or NUL bytes");
    }

    const auto normalized_virtual_path = virtual_path.lexically_normal();
    if (normalized_virtual_path.empty() || normalized_virtual_path == ".") {
        return failure(
            FileReadErrorCode::InvalidVirtualPath,
            virtual_path,
            "Virtual file path must identify a file");
    }

    std::error_code error;
    const auto requested_path = root_ / normalized_virtual_path;
    const bool exists = std::filesystem::exists(requested_path, error);
    if (error || !exists) {
        return failure(
            FileReadErrorCode::NotFound,
            normalized_virtual_path,
            error ? "Unable to inspect asset file: " + error.message() : "Asset file was not found");
    }

    const auto canonical_path = std::filesystem::canonical(requested_path, error);
    if (error) {
        return failure(
            FileReadErrorCode::IoError,
            normalized_virtual_path,
            "Unable to normalize asset file: " + error.message());
    }
    if (!is_within(root_, canonical_path)) {
        return failure(
            FileReadErrorCode::InvalidVirtualPath,
            normalized_virtual_path,
            "Asset file resolves outside the configured filesystem root");
    }

    if (!std::filesystem::is_regular_file(canonical_path, error)) {
        return failure(
            FileReadErrorCode::NotRegularFile,
            normalized_virtual_path,
            error ? "Unable to inspect asset file: " + error.message()
                  : "Asset path is not a regular file");
    }

    const auto file_size = std::filesystem::file_size(canonical_path, error);
    if (error) {
        return failure(
            FileReadErrorCode::IoError,
            normalized_virtual_path,
            "Unable to determine asset file size: " + error.message());
    }
    if (file_size > maximum_file_size_ ||
        file_size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()) ||
        file_size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        return failure(
            FileReadErrorCode::TooLarge,
            normalized_virtual_path,
            "Asset file exceeds the configured size limit");
    }

    std::ifstream stream{canonical_path, std::ios::binary};
    if (!stream) {
        return failure(
            FileReadErrorCode::IoError,
            normalized_virtual_path,
            "Unable to open asset file for reading");
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(file_size));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream || stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
            return failure(
                FileReadErrorCode::IoError,
                normalized_virtual_path,
                "Unable to read the complete asset file");
        }
    }

    std::optional<std::filesystem::file_time_type> last_modified;
    error.clear();
    const auto timestamp = std::filesystem::last_write_time(canonical_path, error);
    if (!error) {
        last_modified = timestamp;
    }

    return FileReadResult{
        FileContents{
            normalized_virtual_path,
            std::move(bytes),
            FileMetadata{file_size, last_modified},
        },
        std::nullopt,
    };
}

const std::filesystem::path& RootedFileSystem::root() const noexcept
{
    return root_;
}

std::uintmax_t RootedFileSystem::maximum_file_size() const noexcept
{
    return maximum_file_size_;
}

} // namespace hlclient::filesystem
