#include <hlclient/local_resources/local_resource_resolver.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>

#include "local_resource_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace {

namespace local = hlclient::local_resources;
using hlclient::tests::ScopedLocalResourceTestRoot;

static_assert(!std::is_copy_constructible_v<local::LocalResourceSearchRoots>);
static_assert(std::is_move_constructible_v<local::LocalResourceSearchRoots>);

TEST_CASE("Local resource search roots preserve game-before-valve order",
          "[local-resource][roots]")
{
    ScopedLocalResourceTestRoot temporary;

    SECTION("valve-only configuration collapses its duplicate fallback")
    {
        auto created =
            local::LocalResourceSearchRoots::create(temporary.path(), "valve");
        REQUIRE(created);
        CHECK(created.roots->size() == 1U);
        const auto metadata = created.roots->metadata(0U);
        REQUIRE(metadata);
        CHECK(metadata->id.value() == 0U);
        CHECK(metadata->kind == local::LocalResourceRootKind::game);
        CHECK(metadata->identity.valid());
        CHECK_FALSE(created.roots->metadata(1U));
    }

    SECTION("mod root precedes the valve fallback")
    {
        temporary.create_game("mymod");
        auto created =
            local::LocalResourceSearchRoots::create(temporary.path(), "mymod");
        REQUIRE(created);
        CHECK(created.roots->size() == 2U);
        REQUIRE(created.roots->metadata(0U));
        REQUIRE(created.roots->metadata(1U));
        CHECK(created.roots->metadata(0U)->kind ==
              local::LocalResourceRootKind::game);
        CHECK(created.roots->metadata(1U)->kind ==
              local::LocalResourceRootKind::valve_fallback);
        CHECK(created.roots->metadata(0U)->id.value() == 0U);
        CHECK(created.roots->metadata(1U)->id.value() == 1U);
    }
}

TEST_CASE("Local resource resolver honors root priority and fallback",
          "[local-resource][roots][resolver]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.create_game("mymod");
    temporary.write("valve", "fallback.dat", "valve");
    temporary.write("valve", "priority.dat", "valve");
    temporary.write("mymod", "priority.dat", "game");

    auto roots =
        local::LocalResourceSearchRoots::create(temporary.path(), "mymod");
    REQUIRE(roots);
    auto resolver =
        local::LocalResourceResolver::create(std::move(*roots.roots));
    REQUIRE(resolver);

    auto game_hit = resolver.resolver->resolve("priority.dat");
    REQUIRE(game_hit);
    CHECK(game_hit.file->root_id().value() == 0U);

    auto fallback_hit = resolver.resolver->resolve("fallback.dat");
    REQUIRE(fallback_hit);
    CHECK(fallback_hit.file->root_id().value() == 1U);
}

TEST_CASE("Local resource roots reject implicit and escaping configuration",
          "[local-resource][roots][security]")
{
    ScopedLocalResourceTestRoot temporary;

    SECTION("missing base is path-free failure")
    {
        const auto missing = temporary.path().parent_path() /
                             "LOCAL-RESOURCE-MISSING-BASE";
        auto result = local::LocalResourceSearchRoots::create(missing, "valve");
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              local::LocalResourceSearchRootsErrorCode::invalid_base_directory);
        CHECK(result.error->context.find(missing.string()) == std::string::npos);
    }

    SECTION("missing game root is rejected instead of silently using valve")
    {
        auto result = local::LocalResourceSearchRoots::create(
            temporary.path(), "missingmod");
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              local::LocalResourceSearchRootsErrorCode::missing_root);
    }

    SECTION("relative base cannot fall back to the current directory")
    {
        auto result = local::LocalResourceSearchRoots::create(".", "valve");
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              local::LocalResourceSearchRootsErrorCode::invalid_base_directory);
    }

    SECTION("game traversal is rejected before any root open")
    {
        auto result = local::LocalResourceSearchRoots::create(
            temporary.path(), "../outside");
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              local::LocalResourceSearchRootsErrorCode::invalid_configuration);
    }

    SECTION("UNC and device namespace basedirs are unsupported")
    {
        for (const auto& value : {
                 std::filesystem::path{LR"(\\server\share\Half-Life)"},
                 std::filesystem::path{LR"(\\?\C:\Half-Life)"},
                 std::filesystem::path{LR"(\\.\C:\Half-Life)"}}) {
            auto result =
                local::LocalResourceSearchRoots::create(value, "valve");
            REQUIRE_FALSE(result);
            REQUIRE(result.error);
            CHECK(result.error->code ==
                  local::LocalResourceSearchRootsErrorCode::invalid_base_directory);
        }
    }
}

TEST_CASE("Local resource roots reject a final directory reparse point",
          "[local-resource][roots][reparse]")
{
    ScopedLocalResourceTestRoot temporary;
    const auto link = temporary.path() / "linkmod";
    std::error_code error;
    if (!hlclient::tests::create_directory_link_if_supported(
            temporary.game_path("valve"), link, error)) {
        SKIP("Directory symlinks are unavailable: " << error.message());
    }

    auto result =
        local::LocalResourceSearchRoots::create(temporary.path(), "linkmod");
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code ==
          local::LocalResourceSearchRootsErrorCode::final_reparse_point);
}

TEST_CASE("Local resource root identity is stable and opaque",
          "[local-resource][roots]")
{
    ScopedLocalResourceTestRoot temporary;
    auto created =
        local::LocalResourceSearchRoots::create(temporary.path(), "valve");
    REQUIRE(created);
    const auto first = created.roots->metadata(0U);
    const auto second = created.roots->metadata(0U);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->identity == second->identity);
    CHECK(first->identity.valid());
}

TEST_CASE("Validated root anchors reject rename and replacement races",
          "[local-resource][roots][sharing][toctou]")
{
    ScopedLocalResourceTestRoot temporary;
    const auto base = temporary.path();
    const auto root = temporary.game_path("valve");
    const auto renamed_base =
        base.parent_path() / (base.filename().wstring() + L"-renamed");
    const auto renamed_root = base / "valve-renamed";

    {
        auto created =
            local::LocalResourceSearchRoots::create(base, "valve");
        REQUIRE(created);
        const bool renamed =
            ::MoveFileExW(root.c_str(), renamed_root.c_str(), 0U) != FALSE;
        if (renamed) {
            temporary.write("valve", "replacement.bin", "replacement");
        }
        auto resolver =
            local::LocalResourceResolver::create(std::move(*created.roots));
        REQUIRE(resolver);
        if (renamed) {
            const auto replacement = resolver.resolver->resolve(
                "replacement.bin");
            REQUIRE_FALSE(replacement);
            CHECK(replacement.code ==
                  local::LocalResourceResolutionCode::io_error);

            std::error_code error;
            CHECK(std::filesystem::remove(root / "replacement.bin", error));
            CHECK_FALSE(error);
            CHECK(std::filesystem::remove(root, error));
            CHECK_FALSE(error);
            REQUIRE(::MoveFileExW(
                        renamed_root.c_str(), root.c_str(), 0U) != FALSE);
        } else {
            CHECK_FALSE(
                ::MoveFileExW(root.c_str(), renamed_root.c_str(), 0U));
            CHECK_FALSE(
                ::MoveFileExW(base.c_str(), renamed_base.c_str(), 0U));
        }
    }

    CHECK(::MoveFileExW(root.c_str(), renamed_root.c_str(), 0U));
    CHECK(::MoveFileExW(renamed_root.c_str(), root.c_str(), 0U));
    CHECK(::MoveFileExW(base.c_str(), renamed_base.c_str(), 0U));
    CHECK(::MoveFileExW(renamed_base.c_str(), base.c_str(), 0U));
}

} // namespace
