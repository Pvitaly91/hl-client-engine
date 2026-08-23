#include <hlclient/local_resources/local_read_only_file.hpp>
#include <hlclient/local_resources/local_resource_resolver.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>

#include "local_resource_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace local = hlclient::local_resources;
using hlclient::tests::ScopedLocalResourceTestRoot;

static_assert(!std::is_copy_constructible_v<local::LocalReadOnlyFile>);
static_assert(!std::is_copy_assignable_v<local::LocalReadOnlyFile>);
static_assert(std::is_move_constructible_v<local::LocalReadOnlyFile>);
static_assert(std::is_move_assignable_v<local::LocalReadOnlyFile>);

TEST_CASE("Default local identity token is explicitly invalid",
          "[local-resource][file][identity]")
{
    const local::LocalStableFileIdentity identity;
    CHECK_FALSE(identity.valid());
}

[[nodiscard]] std::unique_ptr<local::LocalResourceResolver> make_resolver(
    ScopedLocalResourceTestRoot& temporary,
    const std::string_view game = "valve",
    const local::LocalResourceResolverLimits limits = {})
{
    auto roots =
        local::LocalResourceSearchRoots::create(temporary.path(), game);
    if (!roots) {
        throw std::runtime_error{"Unable to create synthetic search roots"};
    }
    auto resolver =
        local::LocalResourceResolver::create(std::move(*roots.roots), limits);
    if (!resolver) {
        throw std::runtime_error{"Unable to create synthetic resolver"};
    }
    return std::move(resolver.resolver);
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path)
{
    std::ifstream stream{path, std::ios::binary};
    return std::string{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{}};
}

TEST_CASE("LocalReadOnlyFile exposes stable metadata and sequential reads",
          "[local-resource][file]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.write("valve", "nested/data.bin", "abcdef");
    auto resolver = make_resolver(temporary);

    auto resolved = resolver->resolve("nested/data.bin");
    REQUIRE(resolved);
    auto file = std::move(*resolved.file);
    CHECK(file.is_open());
    CHECK(file.is_regular_file());
    CHECK(file.file_size() == 6U);
    CHECK(file.root_id().value() == 0U);
    CHECK(file.identity().valid());
    CHECK(file.virtual_resource_id().value() != 0U);
    CHECK(file.bytes_consumed() == 0U);

    const auto initial = file.initial_snapshot();
    const auto current = file.metadata_snapshot();
    REQUIRE(current);
    CHECK(*current.snapshot == initial);

    std::array<std::byte, 4U> first{};
    auto first_read = file.read_next(first);
    REQUIRE(first_read);
    CHECK(first_read.bytes_read == 4U);
    CHECK_FALSE(first_read.end_of_file);

    std::array<std::byte, 4U> second{};
    auto second_read = file.read_next(second);
    REQUIRE(second_read);
    CHECK(second_read.bytes_read == 2U);
    CHECK(second_read.end_of_file);
    CHECK(file.bytes_consumed() == 6U);
    CHECK(static_cast<char>(first[0U]) == 'a');
    CHECK(static_cast<char>(second[1U]) == 'f');

    std::array<std::byte, 1U> probe{};
    const auto end = file.read_next(probe);
    REQUIRE(end);
    CHECK(end.end_of_file);
    CHECK(end.bytes_read == 0U);
    REQUIRE(file.metadata_snapshot());
    CHECK(*file.metadata_snapshot().snapshot == initial);
}

TEST_CASE("Local resource file open rejects non-files and reparses",
          "[local-resource][file][reparse]")
{
    ScopedLocalResourceTestRoot temporary;
    std::filesystem::create_directories(
        temporary.game_path("valve") / "directory-target");

    SECTION("directory is not a regular file")
    {
        auto resolver = make_resolver(temporary);
        const auto result = resolver->resolve("directory-target");
        REQUIRE_FALSE(result);
        CHECK(result.code == local::LocalResourceResolutionCode::not_regular_file);
    }

    SECTION("final file symlink is rejected")
    {
        temporary.write("valve", "real.bin", "real");
        const auto link = temporary.game_path("valve") / "linked.bin";
        std::error_code error;
        std::filesystem::create_symlink(
            temporary.game_path("valve") / "real.bin", link, error);
        if (error) {
            SKIP("File symlinks are unavailable: " << error.message());
        }
        auto resolver = make_resolver(temporary);
        const auto result = resolver->resolve("linked.bin");
        REQUIRE_FALSE(result);
        CHECK(result.code == local::LocalResourceResolutionCode::reparse_escape);
    }

    SECTION("outside-root intermediate symlink is rejected")
    {
        const auto outside = temporary.path() / "outside";
        std::filesystem::create_directories(outside);
        std::ofstream{outside / "escape.bin", std::ios::binary} << "escape";
        const auto link = temporary.game_path("valve") / "escape-dir";
        std::error_code error;
        if (!hlclient::tests::create_directory_link_if_supported(
                outside, link, error)) {
            SKIP("Directory symlinks are unavailable: " << error.message());
        }
        auto resolver = make_resolver(temporary);
        const auto result = resolver->resolve("escape-dir/escape.bin");
        REQUIRE_FALSE(result);
        CHECK(result.code == local::LocalResourceResolutionCode::reparse_escape);
    }

    SECTION("contained intermediate symlink is also rejected by policy")
    {
        temporary.write("valve", "contained/data.bin", "contained");
        const auto link = temporary.game_path("valve") / "contained-link";
        std::error_code error;
        if (!hlclient::tests::create_directory_link_if_supported(
                temporary.game_path("valve") / "contained", link, error)) {
            SKIP("Directory symlinks are unavailable: " << error.message());
        }
        auto resolver = make_resolver(temporary);
        const auto result = resolver->resolve("contained-link/data.bin");
        REQUIRE_FALSE(result);
        CHECK(result.code == local::LocalResourceResolutionCode::reparse_escape);
    }
}

TEST_CASE("Local read-only handle enforces file bounds and no creation",
          "[local-resource][file][bounds]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.write_repeated("valve", "exact.bin", 32U);
    temporary.write_repeated("valve", "large.bin", 33U);
    const local::LocalResourceResolverLimits limits{32U, 32U};
    auto resolver = make_resolver(temporary, "valve", limits);

    auto exact = resolver->resolve("exact.bin");
    REQUIRE(exact);
    CHECK(exact.file->file_size() == 32U);

    const auto large = resolver->resolve("large.bin");
    REQUIRE_FALSE(large);
    CHECK(large.code == local::LocalResourceResolutionCode::too_large);

    const auto missing_path = temporary.game_path("valve") / "missing.bin";
    const auto missing = resolver->resolve("missing.bin");
    REQUIRE_FALSE(missing);
    CHECK(missing.code == local::LocalResourceResolutionCode::not_found);
    CHECK_FALSE(std::filesystem::exists(missing_path));
}

TEST_CASE("Local read-only handle denies writer delete and rename sharing",
          "[local-resource][file][sharing]")
{
    ScopedLocalResourceTestRoot temporary;
    const auto native_path = temporary.game_path("valve") / "locked.bin";
    temporary.write("valve", "locked.bin", "unchanged");
    const auto size_before = std::filesystem::file_size(native_path);
    const auto time_before = std::filesystem::last_write_time(native_path);
    auto resolver = make_resolver(temporary);
    auto resolved = resolver->resolve("locked.bin");
    REQUIRE(resolved);
    auto file = std::move(*resolved.file);

    const HANDLE writer = ::CreateFileW(
        native_path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    CHECK(writer == INVALID_HANDLE_VALUE);
    if (writer != INVALID_HANDLE_VALUE) {
        static_cast<void>(::CloseHandle(writer));
    }
    CHECK_FALSE(::DeleteFileW(native_path.c_str()));
    const auto renamed = native_path.parent_path() / "renamed.bin";
    CHECK_FALSE(::MoveFileExW(native_path.c_str(), renamed.c_str(), 0U));

    std::vector<std::byte> buffer(16U);
    REQUIRE(file.read_next(buffer));
    file.close();
    CHECK_FALSE(file.is_open());
    CHECK_FALSE(file.metadata_snapshot());

    CHECK(std::filesystem::file_size(native_path) == size_before);
    CHECK(std::filesystem::last_write_time(native_path) == time_before);
    CHECK(read_text(native_path) == "unchanged");
    CHECK(::MoveFileExW(native_path.c_str(), renamed.c_str(), 0U));
}

TEST_CASE("Local read-only handle retains its validated intermediate chain",
          "[local-resource][file][sharing][toctou]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.write("valve", "chain/data.bin", "stable-chain");
    const auto directory = temporary.game_path("valve") / "chain";
    const auto renamed = temporary.game_path("valve") / "renamed-chain";
    auto resolver = make_resolver(temporary);
    auto resolved = resolver->resolve("chain/data.bin");
    REQUIRE(resolved);

    CHECK_FALSE(::MoveFileExW(directory.c_str(), renamed.c_str(), 0U));
    CHECK_FALSE(::RemoveDirectoryW(directory.c_str()));
    resolved.file->close();
    CHECK(::MoveFileExW(directory.c_str(), renamed.c_str(), 0U));
}

TEST_CASE("LocalReadOnlyFile hard-bounds every sequential read request",
          "[local-resource][file][bounds]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.write("valve", "bounded.bin", "x");
    auto resolver = make_resolver(temporary);
    auto resolved = resolver->resolve("bounded.bin");
    REQUIRE(resolved);
    std::vector<std::byte> oversized(
        local::kHardMaximumLocalResourceReadChunkSize + 1U);
    const auto read = resolved.file->read_next(oversized);
    REQUIRE_FALSE(read);
    REQUIRE(read.error);
    CHECK(read.error->code == local::LocalReadOnlyFileErrorCode::invalid_state);
    CHECK(resolved.file->bytes_consumed() == 0U);
}

} // namespace
