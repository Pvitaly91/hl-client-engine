#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace hlclient::filesystem {

struct GamePaths {
    std::filesystem::path base_directory;
    std::filesystem::path game_directory;
};

struct GamePathsResult {
    std::optional<GamePaths> paths;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return paths.has_value();
    }
};

[[nodiscard]] GamePathsResult validate_game_paths(
    const std::filesystem::path& base_directory,
    const std::filesystem::path& game_directory_name);

} // namespace hlclient::filesystem
