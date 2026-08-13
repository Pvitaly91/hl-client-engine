#pragma once

#include <hlclient/filesystem/file_system.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace hlclient::filesystem {

inline constexpr std::uintmax_t kDefaultMaximumAssetFileSize = 64U * 1024U * 1024U;

struct RootedFileSystemCreateResult;

class RootedFileSystem final : public IFileSystem {
public:
    [[nodiscard]] static RootedFileSystemCreateResult create(
        const std::filesystem::path& root,
        std::uintmax_t maximum_file_size = kDefaultMaximumAssetFileSize);

    [[nodiscard]] FileReadResult read_file(
        const std::filesystem::path& virtual_path) const override;

    [[nodiscard]] const std::filesystem::path& root() const noexcept;
    [[nodiscard]] std::uintmax_t maximum_file_size() const noexcept;

private:
    RootedFileSystem(std::filesystem::path root, std::uintmax_t maximum_file_size);

    std::filesystem::path root_;
    std::uintmax_t maximum_file_size_{kDefaultMaximumAssetFileSize};
};

struct RootedFileSystemCreateResult {
    std::unique_ptr<RootedFileSystem> file_system;
    std::optional<FileReadError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return static_cast<bool>(file_system);
    }
};

} // namespace hlclient::filesystem
