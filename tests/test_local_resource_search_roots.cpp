#include <hlclient/local_resources/local_resource_resolver.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>

#include "local_resource_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
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

TEST_CASE("Installation checks use exact game or pinned base without widening resource search",
          "[local-resource][roots][installation-check][security]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.create_game("mymod");
    temporary.write("", "engine.dll", "base");
    temporary.write("mymod", "config.cfg", "game");
    temporary.write("valve", "fallback.cfg", "fallback");
    temporary.write("mymod", "nested/settings.cfg", "nested");
    auto roots = local::LocalResourceSearchRoots::create(temporary.path(), "mymod");
    REQUIRE(roots);
    auto created = local::LocalResourceResolver::create(std::move(*roots.roots));
    REQUIRE(created);
    auto& resolver = *created.resolver;
    REQUIRE(resolver.root_count() == 2U);
    for (const std::string_view name : {"../engine.dll", "..\\engine.dll"}) {
        auto result = resolver.resolve_installation_check(name);
        REQUIRE(result);
        CHECK(result.file->root_id().value() == local::kMaximumLocalResourceSearchRoots);
        std::array<std::byte, 4U> bytes{};
        const auto read = result.file->read_next(bytes);
        REQUIRE(read);
        REQUIRE(read.bytes_read == 4U);
        CHECK(bytes == std::array{std::byte{'b'}, std::byte{'a'}, std::byte{'s'}, std::byte{'e'}});
    }
    auto config = resolver.resolve_installation_check("config.cfg");
    REQUIRE(config);
    CHECK(config.file->root_id().value() == 0U);
    CHECK(config.file->file_size() == 4U);
    CHECK(resolver.resolve_installation_check("nested\\settings.cfg"));
    CHECK(resolver.resolve_installation_check("fallback.cfg").code ==
          local::LocalResourceResolutionCode::not_found);
    CHECK(resolver.resolve_installation_check("../missing.dll").code ==
          local::LocalResourceResolutionCode::not_found);
    CHECK(resolver.resolve_installation_check("missing.cfg").code ==
          local::LocalResourceResolutionCode::not_found);
    CHECK(resolver.root_count() == 2U);
    CHECK(resolver.resolve("fallback.cfg")); // Ordinary Valve fallback unchanged.
    CHECK(resolver.resolve("engine.dll").code == local::LocalResourceResolutionCode::not_found);
    CHECK(resolver.resolve("../engine.dll").code == local::LocalResourceResolutionCode::unsafe_name);
}

TEST_CASE("Installation checks reject ambiguous traversal and native filename syntax",
          "[local-resource][roots][installation-check][security]")
{
    ScopedLocalResourceTestRoot temporary;
    auto roots = local::LocalResourceSearchRoots::create(temporary.path(), "valve");
    REQUIRE(roots);
    auto created = local::LocalResourceResolver::create(std::move(*roots.roots));
    REQUIRE(created);
    for (const std::string_view name : {
             "", "..", "../", "../../secret.cfg", "..\\..\\secret.cfg",
             "../sub/../../secret.cfg", "sub/../secret.cfg", "..//engine.dll",
             "../\\engine.dll", "./config.cfg", "//server/share/file.dll",
             "\\\\server\\share\\file.dll", "C:/file.dll", "C:file.dll",
             "../C:/file.dll", "\\\\?\\C:\\file.dll", "/file.dll",
             "config.cfg:stream", "../CON.dll", "../con .dll", "../LPT1.cfg",
             "../CONIN$", "../conout$.dll",
             "config.cfg.", "config.cfg ", "../*.dll", "../?.dll", "x|y.cfg",
             "file\r.cfg", "file\n.cfg", "file\t.cfg"}) {
        CAPTURE(name);
        const auto result = created.resolver->resolve_installation_check(name);
        CHECK_FALSE(result);
        CHECK(result.code == local::LocalResourceResolutionCode::unsafe_name);
        CHECK(result.context.find(temporary.path().string()) == std::string::npos);
    }
    const std::string embedded_nul{"config\0.cfg", 11U};
    CHECK(created.resolver->resolve_installation_check(embedded_nul).code ==
          local::LocalResourceResolutionCode::unsafe_name);
    const std::string non_ascii{"file\x80.cfg"};
    CHECK(created.resolver->resolve_installation_check(non_ascii).code ==
          local::LocalResourceResolutionCode::unsupported_name_encoding);
}

TEST_CASE("Installation check unreadable and non-file entries are never absence",
          "[local-resource][roots][installation-check][security]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.write("", "locked.dll", "private");
    temporary.write("valve", "directory.cfg/child", "file");
    auto roots = local::LocalResourceSearchRoots::create(temporary.path(), "valve");
    REQUIRE(roots);
    auto created = local::LocalResourceResolver::create(std::move(*roots.roots));
    REQUIRE(created);
    const auto path = temporary.path() / "locked.dll";
    const HANDLE lock = ::CreateFileW(path.c_str(), GENERIC_READ, 0U, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(lock != INVALID_HANDLE_VALUE);
    const auto denied = created.resolver->resolve_installation_check("../locked.dll");
    CHECK(::CloseHandle(lock));
    CHECK_FALSE(denied);
    CHECK(denied.code == local::LocalResourceResolutionCode::io_error);
    CHECK(created.resolver->resolve_installation_check("directory.cfg").code ==
          local::LocalResourceResolutionCode::not_regular_file);
    CHECK(created.resolver->resolve_installation_check("../locked.dll"));
}

TEST_CASE("Installation checks reject file hardlinks without changing ordinary resolution",
          "[local-resource][roots][installation-check][hardlink][security]")
{
    ScopedLocalResourceTestRoot temporary;
    ScopedLocalResourceTestRoot outside;
    outside.write("valve", "private.cfg", "outside");
    std::error_code error;
    std::filesystem::create_hard_link(outside.game_path("valve") / "private.cfg",
        temporary.path() / "linked.dll", error);
    REQUIRE_FALSE(error);
    std::filesystem::create_hard_link(outside.game_path("valve") / "private.cfg",
        temporary.game_path("valve") / "linked.cfg", error);
    REQUIRE_FALSE(error);
    auto roots = local::LocalResourceSearchRoots::create(temporary.path(), "valve");
    REQUIRE(roots);
    auto created = local::LocalResourceResolver::create(std::move(*roots.roots));
    REQUIRE(created);
    for (const auto name : {"../linked.dll", "linked.cfg"}) {
        const auto result = created.resolver->resolve_installation_check(name);
        CHECK_FALSE(result);
        CHECK(result.code == local::LocalResourceResolutionCode::reparse_escape);
    }
    CHECK(created.resolver->resolve("linked.cfg"));
}

TEST_CASE("Installation checks reject final and intermediate reparse points",
          "[local-resource][roots][installation-check][reparse][security]")
{
    ScopedLocalResourceTestRoot temporary;
    ScopedLocalResourceTestRoot outside;
    outside.write("valve", "private.cfg", "outside");
    std::error_code error;
    SECTION("base final file reparse") {
        std::filesystem::create_symlink(outside.game_path("valve") / "private.cfg",
            temporary.path() / "linked.dll", error);
        if (error) SKIP("File symlinks are unavailable: " << error.message());
        auto roots = local::LocalResourceSearchRoots::create(temporary.path(), "valve");
        REQUIRE(roots);
        auto created = local::LocalResourceResolver::create(std::move(*roots.roots));
        REQUIRE(created);
        CHECK(created.resolver->resolve_installation_check("../linked.dll").code ==
              local::LocalResourceResolutionCode::reparse_escape);
    }
    SECTION("game and base intermediate directory reparses") {
        if (!hlclient::tests::create_directory_link_if_supported(outside.game_path("valve"),
                temporary.path() / "outside", error))
            SKIP("Directory symlinks are unavailable: " << error.message());
        if (!hlclient::tests::create_directory_link_if_supported(outside.game_path("valve"),
                temporary.game_path("valve") / "outside", error))
            SKIP("Directory symlinks are unavailable: " << error.message());
        auto roots = local::LocalResourceSearchRoots::create(temporary.path(), "valve");
        REQUIRE(roots);
        auto created = local::LocalResourceResolver::create(std::move(*roots.roots));
        REQUIRE(created);
        for (const auto name : {"../outside/private.cfg", "outside/private.cfg"})
            CHECK(created.resolver->resolve_installation_check(name).code ==
                  local::LocalResourceResolutionCode::reparse_escape);
    }
}

} // namespace
