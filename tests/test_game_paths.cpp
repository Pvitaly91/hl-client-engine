#include <hlclient/filesystem/game_paths.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace {

class ScopedTemporaryDirectory final {
public:
    ScopedTemporaryDirectory()
        : temporary_root_{std::filesystem::temp_directory_path().lexically_normal()}
    {
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (std::size_t attempt = 0; attempt < 100U; ++attempt) {
            const auto name = std::string{"hlclient-tests-"} + std::to_string(timestamp) + '-' +
                              std::to_string(attempt);
            auto candidate = temporary_root_ / name;
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                path_ = std::move(candidate);
                return;
            }
            if (error) {
                throw std::runtime_error{"Unable to create test directory: " + error.message()};
            }
        }
        throw std::runtime_error{"Unable to allocate a unique test directory"};
    }

    ~ScopedTemporaryDirectory()
    {
        if (path_.empty()) {
            return;
        }

        const auto normalized = path_.lexically_normal();
        const auto filename = normalized.filename().string();
        const bool is_owned_test_directory = normalized.parent_path() == temporary_root_ &&
                                             filename.starts_with("hlclient-tests-");
        if (is_owned_test_directory) {
            std::error_code ignored;
            std::filesystem::remove_all(normalized, ignored);
        }
    }

    ScopedTemporaryDirectory(const ScopedTemporaryDirectory&) = delete;
    ScopedTemporaryDirectory& operator=(const ScopedTemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path temporary_root_;
    std::filesystem::path path_;
};

TEST_CASE("Game paths validate an existing base and game directory", "[filesystem]")
{
    ScopedTemporaryDirectory temporary;
    const auto game_directory = temporary.path() / "valve";
    REQUIRE(std::filesystem::create_directories(game_directory / "maps"));

    const auto result = hlclient::filesystem::validate_game_paths(temporary.path(), "valve");

    INFO(result.error);
    REQUIRE(result);
    CHECK(result.paths->base_directory == std::filesystem::weakly_canonical(temporary.path()));
    CHECK(result.paths->game_directory ==
          (std::filesystem::weakly_canonical(temporary.path()) / "valve").lexically_normal());
}

TEST_CASE("Game paths allow an existing nested relative game directory", "[filesystem]")
{
    ScopedTemporaryDirectory temporary;
    const auto relative_game = std::filesystem::path{"mods"} / "example";
    REQUIRE(std::filesystem::create_directories(temporary.path() / relative_game));

    const auto result = hlclient::filesystem::validate_game_paths(
        temporary.path(), relative_game.generic_string());

    INFO(result.error);
    REQUIRE(result);
    CHECK(result.paths->game_directory ==
          (std::filesystem::weakly_canonical(temporary.path()) / relative_game).lexically_normal());
}

TEST_CASE("Game paths reject missing or non-directory inputs", "[filesystem]")
{
    ScopedTemporaryDirectory temporary;

    SECTION("missing base directory")
    {
        const auto result = hlclient::filesystem::validate_game_paths(
            temporary.path() / "missing-base", "valve");
        CHECK_FALSE(result);
        CHECK_FALSE(result.error.empty());
    }

    SECTION("base path is a regular file")
    {
        const auto file_path = temporary.path() / "not-a-directory";
        {
            std::ofstream file{file_path};
            REQUIRE(file.good());
        }

        const auto result = hlclient::filesystem::validate_game_paths(file_path, "valve");
        CHECK_FALSE(result);
        CHECK_FALSE(result.error.empty());
    }

    SECTION("missing game directory")
    {
        const auto result = hlclient::filesystem::validate_game_paths(temporary.path(), "valve");
        CHECK_FALSE(result);
        CHECK_FALSE(result.error.empty());
    }

    SECTION("game path is a regular file")
    {
        const auto file_path = temporary.path() / "valve";
        {
            std::ofstream file{file_path};
            REQUIRE(file.good());
        }

        const auto result = hlclient::filesystem::validate_game_paths(temporary.path(), "valve");
        CHECK_FALSE(result);
        CHECK_FALSE(result.error.empty());
    }
}

TEST_CASE("Game paths reject unsafe game directory names", "[filesystem]")
{
    ScopedTemporaryDirectory temporary;
    REQUIRE(std::filesystem::create_directory(temporary.path() / "valve"));

    SECTION("empty path")
    {
        CHECK_FALSE(hlclient::filesystem::validate_game_paths(temporary.path(), {}));
    }

    SECTION("parent traversal")
    {
        CHECK_FALSE(hlclient::filesystem::validate_game_paths(temporary.path(), "../valve"));
        CHECK_FALSE(
            hlclient::filesystem::validate_game_paths(temporary.path(), "valve/../../outside"));
    }

    SECTION("absolute path")
    {
        const auto absolute_game_path = temporary.path().root_path().string();
        REQUIRE_FALSE(absolute_game_path.empty());
        CHECK_FALSE(
            hlclient::filesystem::validate_game_paths(temporary.path(), absolute_game_path));
    }
}

TEST_CASE("Game paths reject a game directory link that escapes basedir", "[filesystem]")
{
    ScopedTemporaryDirectory temporary;
    const auto base_directory = temporary.path() / "half-life";
    const auto outside_directory = temporary.path() / "outside";
    REQUIRE(std::filesystem::create_directories(base_directory));
    REQUIRE(std::filesystem::create_directories(outside_directory));

    std::error_code error;
    std::filesystem::create_directory_symlink(
        outside_directory,
        base_directory / "valve",
        error);
    if (error) {
        SKIP("Directory symlinks are unavailable in this test environment: " << error.message());
    }

    const auto result =
        hlclient::filesystem::validate_game_paths(base_directory, "valve");
    CHECK_FALSE(result);
    CHECK(result.error.find("outside") != std::string::npos);
}

} // namespace
