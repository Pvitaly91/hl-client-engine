#include <hlclient/filesystem/game_paths.hpp>

#include <algorithm>
#include <system_error>
#include <utility>

namespace hlclient::filesystem {
namespace {

[[nodiscard]] GamePathsResult failure(std::string message)
{
    return GamePathsResult{std::nullopt, std::move(message)};
}

[[nodiscard]] std::string path_as_utf8(const std::filesystem::path& path)
{
    const auto encoded = path.u8string();
    return std::string{
        reinterpret_cast<const char*>(encoded.data()),
        encoded.size(),
    };
}

[[nodiscard]] bool is_safe_relative_game_path(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute() || path.has_root_path()) {
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
    const std::filesystem::path& base,
    const std::filesystem::path& candidate) noexcept
{
    const auto mismatch = std::mismatch(base.begin(), base.end(), candidate.begin(), candidate.end());
    return mismatch.first == base.end();
}

} // namespace

GamePathsResult validate_game_paths(
    const std::filesystem::path& base_directory,
    const std::filesystem::path& game_directory_name)
{
    const auto& relative_game_directory = game_directory_name;
    if (!is_safe_relative_game_path(relative_game_directory)) {
        return failure("Game directory must be a non-empty relative path without '..'");
    }

    std::error_code error_code;
    if (!std::filesystem::is_directory(base_directory, error_code)) {
        return failure("Half-Life base directory does not exist or is not a directory: " +
                       path_as_utf8(base_directory));
    }

    const auto normalized_base = std::filesystem::weakly_canonical(base_directory, error_code);
    if (error_code) {
        return failure("Unable to normalize Half-Life base directory: " + error_code.message());
    }

    const auto requested_game_directory =
        (normalized_base / relative_game_directory).lexically_normal();
    error_code.clear();
    if (!std::filesystem::is_directory(requested_game_directory, error_code)) {
        return failure("Game directory does not exist: " + path_as_utf8(requested_game_directory));
    }

    error_code.clear();
    const auto game_directory = std::filesystem::canonical(requested_game_directory, error_code);
    if (error_code) {
        return failure("Unable to normalize game directory: " + error_code.message());
    }
    if (!is_within(normalized_base, game_directory)) {
        return failure("Game directory resolves outside the Half-Life base directory");
    }

    return GamePathsResult{GamePaths{normalized_base, game_directory}, {}};
}

} // namespace hlclient::filesystem
