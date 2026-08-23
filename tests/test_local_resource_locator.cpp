#include <hlclient/local_resources/local_resource_environment.hpp>

#include "local_resource_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace {

namespace local = hlclient::local_resources;
using hlclient::tests::ScopedLocalResourceTestRoot;

template <typename T>
concept HasNativePathGetter = requires(const T& value) {
    value.native_path();
};

static_assert(!std::is_copy_constructible_v<local::LocalResourceEnvironment>);
static_assert(std::is_move_constructible_v<local::LocalResourceEnvironment>);
static_assert(std::is_copy_constructible_v<local::LocalResourceLocator>);
static_assert(std::is_move_constructible_v<local::LocalResourceLocator>);
static_assert(!std::is_copy_assignable_v<local::LocalResourceLocator>);
static_assert(!HasNativePathGetter<local::LocalResourceLocator>);

[[nodiscard]] std::unique_ptr<local::LocalResourceEnvironment> make_environment(
    ScopedLocalResourceTestRoot& temporary,
    const std::string_view game = "valve",
    const local::LocalResourceResolverLimits limits = {})
{
    auto roots =
        local::LocalResourceSearchRoots::create(temporary.path(), game);
    INFO((roots.error ? roots.error->context : std::string{}));
    REQUIRE(roots);
    REQUIRE(roots.roots);
    auto environment = local::LocalResourceEnvironment::create(
        std::move(*roots.roots), limits);
    INFO((environment.error ? environment.error->context : std::string{}));
    REQUIRE(environment);
    REQUIRE(environment.environment);
    return std::move(environment.environment);
}

[[nodiscard]] local::LocalResourceLocator make_locator(
    const local::LocalResourceEnvironment& environment,
    const std::string_view virtual_name)
{
    auto name = local::LocalVirtualResourceName::create(virtual_name);
    INFO((name.error ? name.error->context : std::string{}));
    REQUIRE(name);
    REQUIRE(name.name);

    auto resolution = environment.resolver().resolve(*name.name);
    INFO(resolution.context);
    REQUIRE(resolution);
    REQUIRE(resolution.file);
    const auto root_id = resolution.file->root_id();
    const auto identity = resolution.file->identity();
    const auto size = resolution.file->file_size();
    resolution.file->close();

    auto locator = environment.make_locator(
        root_id, std::move(*name.name), identity, size);
    INFO((locator.error ? locator.error->context : std::string{}));
    REQUIRE(locator);
    REQUIRE(locator.locator);
    return std::move(*locator.locator);
}

TEST_CASE("Local resource locator owns approved path-free metadata",
          "[local-resource][locator]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.write("valve", "models/test.mdl", "candidate");
    auto environment = make_environment(temporary);
    auto locator = make_locator(*environment, "models/test.mdl");

    CHECK(locator.root_id().valid());
    CHECK(locator.root_id().value() == 0U);
    CHECK(locator.virtual_name().value() == "models/test.mdl");
    CHECK(locator.virtual_name().component_count() == 2U);
    CHECK(locator.expected_identity().valid());
    CHECK(locator.expected_file_size() == 9U);
    CHECK(locator.compatibility_profile() ==
          local::LocalResourceLocatorCompatibilityProfile::
              validated_fixed_local_volume_v1);

    auto copy = locator;
    CHECK(copy.root_id() == locator.root_id());
    CHECK(copy.virtual_name().value() == locator.virtual_name().value());
    CHECK(copy.expected_identity() == locator.expected_identity());

    auto reopened = environment->reopen_verified(locator);
    INFO((reopened.error ? reopened.error->context : std::string{}));
    REQUIRE(reopened);
    REQUIRE(reopened.file);
    CHECK(reopened.file->is_open());
    CHECK(reopened.file->bytes_consumed() == 0U);
    CHECK(reopened.file->root_id() == locator.root_id());
    CHECK(reopened.file->virtual_resource_id() == locator.virtual_name().id());
    CHECK(reopened.file->identity() == locator.expected_identity());
    CHECK(reopened.file->file_size() == locator.expected_file_size());
}

TEST_CASE("Verified locator reopen never searches a fallback root",
          "[local-resource][locator][roots]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.create_game("mymod");
    temporary.write("mymod", "shared.bin", "game");
    temporary.write("valve", "shared.bin", "fallback");
    auto environment = make_environment(temporary, "mymod");
    auto locator = make_locator(*environment, "shared.bin");
    REQUIRE(locator.root_id().value() == 0U);

    REQUIRE(std::filesystem::remove(
        temporary.game_path("mymod") / "shared.bin"));
    auto reopened = environment->reopen_verified(locator);
    REQUIRE_FALSE(reopened);
    REQUIRE(reopened.error);
    CHECK(reopened.error->code ==
          local::LocalResourceLocatorReopenErrorCode::locator_target_missing);

    // The ordinary search API still sees the fallback, proving that verified
    // reopen did not silently select it.
    auto ordinary = environment->resolver().resolve("shared.bin");
    REQUIRE(ordinary);
    REQUIRE(ordinary.file);
    CHECK(ordinary.file->root_id().value() == 1U);
}

TEST_CASE("Verified locator reopen rejects stale file metadata",
          "[local-resource][locator][stale]")
{
    ScopedLocalResourceTestRoot temporary;
    const auto target = temporary.game_path("valve") / "candidate.bin";
    temporary.write("valve", "candidate.bin", "same");
    auto environment = make_environment(temporary);
    auto locator = make_locator(*environment, "candidate.bin");

    SECTION("replacement identity is stale even at the same size")
    {
        const auto previous = temporary.game_path("valve") / "previous.bin";
        std::filesystem::rename(target, previous);
        temporary.write("valve", "candidate.bin", "same");

        auto reopened = environment->reopen_verified(locator);
        REQUIRE_FALSE(reopened);
        REQUIRE(reopened.error);
        CHECK(reopened.error->code ==
              local::LocalResourceLocatorReopenErrorCode::stale_locator);
    }

    SECTION("changed size is stale")
    {
        temporary.write("valve", "candidate.bin", "different-size");
        auto reopened = environment->reopen_verified(locator);
        REQUIRE_FALSE(reopened);
        REQUIRE(reopened.error);
        CHECK(reopened.error->code ==
              local::LocalResourceLocatorReopenErrorCode::stale_locator);
    }

    SECTION("missing target remains explicit")
    {
        REQUIRE(std::filesystem::remove(target));
        auto reopened = environment->reopen_verified(locator);
        REQUIRE_FALSE(reopened);
        REQUIRE(reopened.error);
        CHECK(reopened.error->code ==
              local::LocalResourceLocatorReopenErrorCode::
                  locator_target_missing);
    }
}

TEST_CASE("Verified locator reopen preserves reparse rejection",
          "[local-resource][locator][reparse]")
{
    ScopedLocalResourceTestRoot temporary;
    const auto target = temporary.game_path("valve") / "candidate.bin";
    const auto retained = temporary.game_path("valve") / "retained.bin";
    temporary.write("valve", "candidate.bin", "candidate");
    auto environment = make_environment(temporary);
    auto locator = make_locator(*environment, "candidate.bin");

    std::filesystem::rename(target, retained);
    std::error_code error;
    std::filesystem::create_symlink(retained, target, error);
    if (error) {
        SKIP("File symlinks are unavailable: " << error.message());
    }

    auto reopened = environment->reopen_verified(locator);
    REQUIRE_FALSE(reopened);
    REQUIRE(reopened.error);
    CHECK(reopened.error->code ==
          local::LocalResourceLocatorReopenErrorCode::reparse_escape);
}

TEST_CASE("Verified locator reopen preserves bounded case ambiguity",
          "[local-resource][locator][case]")
{
    ScopedLocalResourceTestRoot temporary;
    const auto directory = temporary.game_path("valve") / "case";
    REQUIRE(std::filesystem::create_directories(directory));
    if (!hlclient::tests::enable_case_sensitive_directory(directory)) {
        SKIP("Case-sensitive directory mode is unavailable");
    }
    temporary.write("valve", "case/target.bin", "candidate");
    auto environment = make_environment(temporary);
    auto locator = make_locator(*environment, "case/target.bin");

    std::filesystem::rename(
        directory / "target.bin", directory / "Target.bin");
    temporary.write("valve", "case/TARGET.BIN", "other");

    auto reopened = environment->reopen_verified(locator);
    REQUIRE_FALSE(reopened);
    REQUIRE(reopened.error);
    CHECK(reopened.error->code ==
          local::LocalResourceLocatorReopenErrorCode::ambiguous_case);
}

TEST_CASE("Root IDs and locators retain environment provenance",
          "[local-resource][locator][environment]")
{
    ScopedLocalResourceTestRoot first_root;
    ScopedLocalResourceTestRoot second_root;
    first_root.write("valve", "candidate.bin", "first");
    second_root.write("valve", "candidate.bin", "second");
    auto first = make_environment(first_root);
    auto second = make_environment(second_root);
    auto locator = make_locator(*first, "candidate.bin");

    REQUIRE(first->root_metadata(locator.root_id()));
    CHECK_FALSE(second->root_metadata(locator.root_id()));
    auto wrong_environment = second->reopen_verified(locator);
    REQUIRE_FALSE(wrong_environment);
    REQUIRE(wrong_environment.error);
    CHECK(wrong_environment.error->code ==
          local::LocalResourceLocatorReopenErrorCode::
              locator_environment_mismatch);

    auto second_name = local::LocalVirtualResourceName::create("candidate.bin");
    REQUIRE(second_name);
    auto foreign_root_locator = second->make_locator(
        locator.root_id(),
        std::move(*second_name.name),
        locator.expected_identity(),
        locator.expected_file_size());
    REQUIRE_FALSE(foreign_root_locator);
    REQUIRE(foreign_root_locator.error);
    CHECK(foreign_root_locator.error->code ==
          local::LocalResourceLocatorCreateErrorCode::root_not_in_environment);
}

TEST_CASE("Locator cannot be rebound after its environment expires",
          "[local-resource][locator][lifetime]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.write("valve", "candidate.bin", "candidate");

    auto locator = [&]() {
        auto original = make_environment(temporary);
        return make_locator(*original, "candidate.bin");
    }();
    auto replacement = make_environment(temporary);

    auto reopened = replacement->reopen_verified(locator);
    REQUIRE_FALSE(reopened);
    REQUIRE(reopened.error);
    CHECK(reopened.error->code ==
          local::LocalResourceLocatorReopenErrorCode::
              locator_environment_mismatch);
}

} // namespace
