#include <hlclient/goldsrc/resource_client_response.hpp>
#include <hlclient/resource_consistency/prepared_local_resource_consistency_provider.hpp>

#include "local_resource_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace {

namespace consistency = hlclient::resource_consistency;
namespace goldsrc = hlclient::goldsrc;
namespace local = hlclient::local_resources;
using hlclient::tests::ScopedLocalResourceTestRoot;

static_assert(!std::is_copy_constructible_v<
              consistency::PreparedLocalResourceConsistencyProvider>);
static_assert(!std::is_copy_assignable_v<
              consistency::PreparedLocalResourceConsistencyProvider>);

[[nodiscard]] consistency::
    PreparedLocalResourceConsistencyProviderCreateResult
prepare(
    ScopedLocalResourceTestRoot& temporary,
    const std::string_view game = "valve",
    const consistency::PreparedLocalResourceConsistencyProviderLimits limits =
        {})
{
    auto roots =
        local::LocalResourceSearchRoots::create(temporary.path(), game);
    if (!roots) {
        return consistency::PreparedLocalResourceConsistencyProviderCreateResult{
            nullptr,
            consistency::ResourceConsistencyError{
                consistency::ResourceConsistencyErrorCode::
                    invalid_configuration,
                roots.error ? roots.error->context
                            : "Unable to create synthetic search roots"},
        };
    }
    return consistency::PreparedLocalResourceConsistencyProvider::prepare(
        std::move(*roots.roots), limits);
}

[[nodiscard]] bool can_open_writer(const std::filesystem::path& path)
{
    const HANDLE writer = ::CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (writer == INVALID_HANDLE_VALUE) {
        return false;
    }
    static_cast<void>(::CloseHandle(writer));
    return true;
}

TEST_CASE("Prepared local provider resolves fixed tempdecal search priority",
          "[local-resource][provider]")
{
    ScopedLocalResourceTestRoot temporary;

    SECTION("game root wins")
    {
        temporary.create_game("mymod");
        temporary.write("mymod", "tempdecal.wad", "game-material");
        temporary.write("valve", "tempdecal.wad", "fallback");
        auto result = prepare(temporary, "mymod");
        REQUIRE(result);
        CHECK(result.provider->validated_root_count() == 2U);
        CHECK(result.provider->selected_root_id().value() == 0U);
        CHECK(result.provider->byte_count() == 13U);
        CHECK(result.provider->opaque_byte_count() == 16U);
        CHECK_FALSE(result.provider->consumed());
    }

    SECTION("valve fallback is used when the game target is absent")
    {
        temporary.create_game("mymod");
        temporary.write("valve", "tempdecal.wad", "fallback");
        auto result = prepare(temporary, "mymod");
        REQUIRE(result);
        CHECK(result.provider->selected_root_id().value() == 1U);
        CHECK(result.provider->byte_count() == 8U);
    }

    SECTION("valve-only has one collapsed root")
    {
        temporary.write("valve", "tempdecal.wad", "valve");
        auto result = prepare(temporary);
        REQUIRE(result);
        CHECK(result.provider->validated_root_count() == 1U);
        CHECK(result.provider->selected_root_id().value() == 0U);
    }
}

TEST_CASE("Prepared local provider publishes known material once and promptly",
          "[local-resource][provider][md5]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.write("valve", "tempdecal.wad", "abc");
    auto prepared = prepare(temporary);
    REQUIRE(prepared);

    const auto requirements =
        consistency::ResourceConsistencyRequirements::
            stock_opcode5_single_resource();
    REQUIRE(requirements);
    auto begun = prepared.provider->begin(*requirements);
    REQUIRE(begun);
    CHECK(prepared.provider->consumed());
    auto completed = begun.operation->update();
    CHECK(completed.state ==
          consistency::ResourceConsistencyUpdateState::succeeded);
    REQUIRE(completed.session);
    CHECK(completed.session->opaque_byte_count() == 16U);
    auto material = completed.session->take_material();
    REQUIRE(material);
    CHECK(material->byte_count() == 3U);
    CHECK_FALSE(completed.session->take_material());

    goldsrc::Opcode5ResourceResponseBuilder builder;
    auto encoded = builder.build(goldsrc::ResourceClientResponseInput{
        std::string{consistency::kStockOpcode5LocalConsistencyTarget},
        goldsrc::kOpcode5ResourceResponseFieldType,
        goldsrc::kOpcode5ResourceResponseFieldIndex,
        goldsrc::kOpcode5ResourceResponseFieldFlags,
        std::move(*material)});
    REQUIRE(encoded);
    const auto bytes = encoded.encoding->semantic_bytes();
    REQUIRE(bytes.size() == 41U);
    CHECK(bytes[20U] == std::byte{0x03U});
    CHECK(bytes[21U] == std::byte{0x00U});
    CHECK(bytes[22U] == std::byte{0x00U});
    CHECK(bytes[23U] == std::byte{0x00U});
    constexpr std::array<std::byte, 16U> expected_md5{
        std::byte{0x90U}, std::byte{0x01U}, std::byte{0x50U}, std::byte{0x98U},
        std::byte{0x3cU}, std::byte{0xd2U}, std::byte{0x4fU}, std::byte{0xb0U},
        std::byte{0xd6U}, std::byte{0x96U}, std::byte{0x3fU}, std::byte{0x7dU},
        std::byte{0x28U}, std::byte{0xe1U}, std::byte{0x7fU}, std::byte{0x72U},
    };
    CHECK(std::ranges::equal(bytes.subspan(25U, 16U), expected_md5));

    auto second = prepared.provider->begin(*requirements);
    REQUIRE_FALSE(second);
    REQUIRE(second.error);
    CHECK(second.error->code ==
          consistency::ResourceConsistencyErrorCode::unavailable);
}

TEST_CASE("Prepared local provider fails closed without fallback material",
          "[local-resource][provider][negative]")
{
    ScopedLocalResourceTestRoot temporary;

    SECTION("missing target")
    {
        auto result = prepare(temporary);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              consistency::ResourceConsistencyErrorCode::unavailable);
        CHECK(result.error->context.find(temporary.path().string()) ==
              std::string::npos);
    }

    SECTION("zero-length target")
    {
        temporary.write("valve", "tempdecal.wad", "");
        auto result = prepare(temporary);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              consistency::ResourceConsistencyErrorCode::invalid_material);
    }

    SECTION("too-large target")
    {
        temporary.write_repeated("valve", "tempdecal.wad", 33U);
        consistency::PreparedLocalResourceConsistencyProviderLimits limits;
        limits.resolver.maximum_file_size = 32U;
        limits.inspection.maximum_file_size = 32U;
        auto result = prepare(temporary, "valve", limits);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              consistency::ResourceConsistencyErrorCode::material_too_large);
    }

    SECTION("final reparse target")
    {
        temporary.write("valve", "real.wad", "real");
        std::error_code error;
        std::filesystem::create_symlink(
            temporary.game_path("valve") / "real.wad",
            temporary.game_path("valve") / "tempdecal.wad",
            error);
        if (error) {
            SKIP("File symlinks are unavailable: " << error.message());
        }
        auto result = prepare(temporary);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              consistency::ResourceConsistencyErrorCode::provider_error);
    }
}

TEST_CASE("Prepared local provider detects ambiguous ASCII case",
          "[local-resource][provider][case]")
{
    ScopedLocalResourceTestRoot temporary;
    if (!hlclient::tests::enable_case_sensitive_directory(
            temporary.game_path("valve"))) {
        SKIP("Case-sensitive directory mode is unavailable");
    }
    temporary.write("valve", "TempDecal.wad", "one");
    temporary.write("valve", "TEMPDECAL.WAD", "two");

    auto result = prepare(temporary);
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code ==
          consistency::ResourceConsistencyErrorCode::provider_error);
    CHECK(result.error->context.find("multiple") != std::string::npos);
}

TEST_CASE("Prepared provider lifetime retains and releases one read-only handle",
          "[local-resource][provider][lifetime]")
{
    ScopedLocalResourceTestRoot temporary;
    const auto target = temporary.game_path("valve") / "tempdecal.wad";
    temporary.write("valve", "tempdecal.wad", "lifetime");
    auto prepared = prepare(temporary);
    REQUIRE(prepared);
    CHECK_FALSE(can_open_writer(target));

    const auto requirements =
        consistency::ResourceConsistencyRequirements::
            stock_opcode5_single_resource();
    REQUIRE(requirements);
    auto begun = prepared.provider->begin(*requirements);
    REQUIRE(begun);
    CHECK_FALSE(can_open_writer(target));
    auto completed = begun.operation->update();
    REQUIRE(completed.session);
    CHECK_FALSE(can_open_writer(target));

    prepared.provider.reset();
    begun.operation.reset();
    CHECK_FALSE(can_open_writer(target));
    completed.session.reset();
    CHECK(can_open_writer(target));
}

TEST_CASE("Prepared provider cancellation is idempotent and releases its guard",
          "[local-resource][provider][cancel]")
{
    ScopedLocalResourceTestRoot temporary;
    const auto target = temporary.game_path("valve") / "tempdecal.wad";
    temporary.write("valve", "tempdecal.wad", "cancel");
    auto prepared = prepare(temporary);
    REQUIRE(prepared);
    const auto requirements =
        consistency::ResourceConsistencyRequirements::
            stock_opcode5_single_resource();
    REQUIRE(requirements);
    auto begun = prepared.provider->begin(*requirements);
    REQUIRE(begun);
    begun.operation->cancel();
    begun.operation->cancel();
    CHECK(can_open_writer(target));
    const auto update = begun.operation->update();
    REQUIRE(update.error);
    CHECK(update.error->code ==
          consistency::ResourceConsistencyErrorCode::cancelled);
}

} // namespace
