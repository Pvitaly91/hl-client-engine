#define HLCLIENT_LOCAL_RESOURCE_TEST_ACCESS

#include <hlclient/local_assets/local_asset_source.hpp>

#include "../src/local_resources/win32_local_resource_detail.hpp"
#include "local_resource_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace hlclient::local_assets::detail {

class LocalAssetSourceOpenOperationTestAccess final {
public:
    [[nodiscard]] static local_resources::LocalReadOnlyFile* file(
        LocalAssetSourceOpenOperation& operation) noexcept
    {
        return operation.file_ ? &*operation.file_ : nullptr;
    }

    static void simulate_final_change_metadata(
        LocalAssetSourceOpenOperation& operation) noexcept
    {
        operation.initial_snapshot_.change_time ^= 1;
    }

    static void simulate_final_identity_replacement(
        LocalAssetSourceOpenOperation& operation) noexcept
    {
        operation.initial_snapshot_.identity =
            local_resources::LocalStableFileIdentity{};
    }
};

} // namespace hlclient::local_assets::detail

namespace {

namespace local = hlclient::local_resources;
namespace local_assets = hlclient::local_assets;
using hlclient::tests::ScopedLocalResourceTestRoot;

template <typename T>
concept HasNativePathGetter = requires(const T& value) {
    value.native_path();
};

static_assert(!std::is_copy_constructible_v<local_assets::LocalAssetSource>);
static_assert(std::is_move_constructible_v<local_assets::LocalAssetSource>);
static_assert(
    !std::is_copy_constructible_v<
        local_assets::LocalAssetSourceOpenOperation>);
static_assert(
    std::is_move_constructible_v<
        local_assets::LocalAssetSourceOpenOperation>);
static_assert(!std::is_copy_constructible_v<local_assets::LocalAssetSourceOpener>);
static_assert(!HasNativePathGetter<local_assets::LocalAssetSource>);

[[nodiscard]] std::shared_ptr<const local::LocalResourceEnvironment>
make_environment(
    ScopedLocalResourceTestRoot& temporary,
    const std::string_view game = "valve",
    const local::LocalResourceResolverLimits limits = {})
{
    auto roots =
        local::LocalResourceSearchRoots::create(temporary.path(), game);
    INFO((roots.error ? roots.error->context : std::string{}));
    REQUIRE(roots);
    REQUIRE(roots.roots);
    auto created = local::LocalResourceEnvironment::create(
        std::move(*roots.roots), limits);
    INFO((created.error ? created.error->context : std::string{}));
    REQUIRE(created);
    REQUIRE(created.environment);
    return std::shared_ptr<const local::LocalResourceEnvironment>{
        std::move(created.environment)};
}

[[nodiscard]] local::LocalResourceLocator make_locator(
    const local::LocalResourceEnvironment& environment,
    const std::string_view name)
{
    auto virtual_name = local::LocalVirtualResourceName::create(name);
    INFO((virtual_name.error ? virtual_name.error->context : std::string{}));
    REQUIRE(virtual_name);
    REQUIRE(virtual_name.name);

    auto resolved = environment.resolver().resolve(*virtual_name.name);
    INFO(resolved.context);
    REQUIRE(resolved);
    REQUIRE(resolved.file);
    const auto root_id = resolved.file->root_id();
    const auto identity = resolved.file->identity();
    const auto size = resolved.file->file_size();
    resolved.file->close();

    auto created = environment.make_locator(
        root_id, std::move(*virtual_name.name), identity, size);
    INFO((created.error ? created.error->context : std::string{}));
    REQUIRE(created);
    REQUIRE(created.locator);
    return std::move(*created.locator);
}

[[nodiscard]] local_assets::LocalAssetSourceOpenOperation begin_open(
    local_assets::LocalAssetSourceOpener& opener,
    const local::LocalResourceLocator& locator,
    std::shared_ptr<const local::LocalResourceEnvironment> environment,
    const local_assets::LocalAssetSourceOpenLimits limits = {})
{
    auto begun = opener.begin(locator, std::move(environment), limits);
    INFO((begun.error ? begun.error->context : std::string{}));
    REQUIRE(begun);
    REQUIRE(begun.operation);
    return std::move(*begun.operation);
}

void update_until_terminal(
    local_assets::LocalAssetSourceOpenOperation& operation,
    const local_assets::LocalAssetSourceOpenTimePoint start =
        local_assets::LocalAssetSourceOpenTimePoint{})
{
    for (std::size_t update = 0U; update < 1'024U; ++update) {
        const auto state = operation.state();
        if (state == local_assets::LocalAssetSourceOpenState::source_ready ||
            state == local_assets::LocalAssetSourceOpenState::cancelled ||
            state == local_assets::LocalAssetSourceOpenState::timed_out ||
            state == local_assets::LocalAssetSourceOpenState::failed) {
            return;
        }
        operation.update(
            start + std::chrono::milliseconds{static_cast<std::int64_t>(update)});
    }
    FAIL("Local asset-source operation did not reach a terminal state");
}

TEST_CASE("Approved local asset source retains exact bytes and metadata",
          "[local-assets][source]")
{
    ScopedLocalResourceTestRoot temporary;
    const std::string bytes{"IBSP-synthetic-world-bytes"};
    temporary.write("valve", "maps/test.bsp", bytes);
    const auto environment = make_environment(temporary);
    const auto locator = make_locator(*environment, "maps/test.bsp");
    local_assets::LocalAssetSourceOpener opener;
    auto operation = begin_open(opener, locator, environment);

    update_until_terminal(operation);
    REQUIRE(
        operation.state() ==
        local_assets::LocalAssetSourceOpenState::source_ready);
    REQUIRE(operation.result() != nullptr);
    const auto& approved = *operation.result();
    CHECK(approved.root_id() == locator.root_id());
    CHECK(approved.virtual_resource_id() == locator.virtual_name().id());
    CHECK(approved.expected_identity() == locator.expected_identity());
    CHECK(approved.byte_count() == bytes.size());
    CHECK(approved.locator_compatibility_profile() ==
          locator.compatibility_profile());

    const auto& source = approved.source();
    CHECK(source.virtual_path().generic_string() == "maps/test.bsp");
    CHECK(source.extension_hint() == ".bsp");
    CHECK(source.version_hint() == std::nullopt);
    REQUIRE(source.metadata());
    REQUIRE(source.metadata()->content_size);
    CHECK(*source.metadata()->content_size == bytes.size());
    CHECK_FALSE(source.metadata()->last_modified);
    CHECK_FALSE(source.metadata()->version_hint);
    REQUIRE(source.bytes().size() == bytes.size());
    CHECK(std::equal(
        source.bytes().begin(),
        source.bytes().end(),
        reinterpret_cast<const std::byte*>(bytes.data())));
    CHECK(source.signature().size() ==
          (std::min)(bytes.size(), hlclient::assets::kAssetProbeSignatureSize));

    auto taken = operation.take_result();
    REQUIRE(taken);
    CHECK(operation.result() == nullptr);
    CHECK(taken->byte_count() == bytes.size());
    CHECK_FALSE(operation.take_result());
}

TEST_CASE("Local asset-source limits enforce every project hard cap",
          "[local-assets][source][limits]")
{
    using Limits = local_assets::LocalAssetSourceOpenLimits;
    CHECK(local_assets::valid_local_asset_source_open_limits(Limits{}));
    CHECK(local_assets::to_string(local_assets::LocalAssetSourceOpenState::idle) ==
          "idle");

    SECTION("source bytes")
    {
        Limits exact;
        exact.maximum_source_bytes =
            local_assets::kHardMaximumLocalAssetSourceBytes;
        CHECK(local_assets::valid_local_asset_source_open_limits(exact));
        exact.maximum_source_bytes += 1U;
        CHECK_FALSE(local_assets::valid_local_asset_source_open_limits(exact));
        exact.maximum_source_bytes = 0U;
        CHECK_FALSE(local_assets::valid_local_asset_source_open_limits(exact));
    }
    SECTION("read chunk")
    {
        Limits exact;
        exact.read_chunk_bytes =
            local_assets::kHardMaximumLocalAssetSourceReadChunkBytes;
        CHECK(local_assets::valid_local_asset_source_open_limits(exact));
        exact.read_chunk_bytes += 1U;
        CHECK_FALSE(local_assets::valid_local_asset_source_open_limits(exact));
        exact.read_chunk_bytes = 0U;
        CHECK_FALSE(local_assets::valid_local_asset_source_open_limits(exact));
    }
    SECTION("chunks per update")
    {
        Limits exact;
        exact.maximum_chunks_per_update =
            local_assets::kHardMaximumLocalAssetSourceChunksPerUpdate;
        CHECK(local_assets::valid_local_asset_source_open_limits(exact));
        exact.maximum_chunks_per_update += 1U;
        CHECK_FALSE(local_assets::valid_local_asset_source_open_limits(exact));
        exact.maximum_chunks_per_update = 0U;
        CHECK_FALSE(local_assets::valid_local_asset_source_open_limits(exact));
    }
    SECTION("open sources")
    {
        Limits limits;
        limits.maximum_open_sources = 0U;
        CHECK_FALSE(local_assets::valid_local_asset_source_open_limits(limits));
        limits.maximum_open_sources = 2U;
        CHECK_FALSE(local_assets::valid_local_asset_source_open_limits(limits));
    }
    SECTION("timeout")
    {
        Limits exact;
        exact.timeout = local_assets::kHardMaximumLocalAssetSourceTimeout;
        CHECK(local_assets::valid_local_asset_source_open_limits(exact));
        exact.timeout = local_assets::kHardMaximumLocalAssetSourceTimeout +
                        std::chrono::milliseconds{1};
        CHECK_FALSE(local_assets::valid_local_asset_source_open_limits(exact));
        exact.timeout = std::chrono::milliseconds::zero();
        CHECK_FALSE(local_assets::valid_local_asset_source_open_limits(exact));
        exact.timeout = std::chrono::milliseconds{-1};
        CHECK_FALSE(local_assets::valid_local_asset_source_open_limits(exact));
    }
}

TEST_CASE("Local asset source reads incrementally and proves zero-byte EOF",
          "[local-assets][source][incremental]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.write("valve", "ten.bin", "0123456789");
    temporary.write("valve", "empty.bin", "");
    const auto environment = make_environment(temporary);
    local_assets::LocalAssetSourceOpener opener;

    SECTION("bounded chunks per update")
    {
        const auto locator = make_locator(*environment, "ten.bin");
        local_assets::LocalAssetSourceOpenLimits limits;
        limits.read_chunk_bytes = 3U;
        limits.maximum_chunks_per_update = 2U;
        auto operation = begin_open(opener, locator, environment, limits);
        const auto start = local_assets::LocalAssetSourceOpenTimePoint{};

        operation.update(start);
        CHECK(operation.state() ==
              local_assets::LocalAssetSourceOpenState::reading);
        CHECK(operation.progress_bytes() == 0U);
        operation.update(start + std::chrono::milliseconds{1});
        CHECK(operation.progress_bytes() == 6U);
        CHECK(operation.state() ==
              local_assets::LocalAssetSourceOpenState::reading);
        operation.update(start + std::chrono::milliseconds{2});
        CHECK(operation.progress_bytes() == 10U);
        CHECK(operation.state() ==
              local_assets::LocalAssetSourceOpenState::validating);
        CHECK(operation.result() == nullptr);
        operation.update(start + std::chrono::milliseconds{3});
        REQUIRE(operation.state() ==
                local_assets::LocalAssetSourceOpenState::source_ready);
    }

    SECTION("zero bytes still require a physical EOF probe")
    {
        const auto locator = make_locator(*environment, "empty.bin");
        auto operation = begin_open(opener, locator, environment);
        const auto start = local_assets::LocalAssetSourceOpenTimePoint{};
        operation.update(start);
        CHECK(operation.state() ==
              local_assets::LocalAssetSourceOpenState::validating);
        CHECK(operation.result() == nullptr);
        operation.update(start + std::chrono::milliseconds{1});
        REQUIRE(operation.state() ==
                local_assets::LocalAssetSourceOpenState::source_ready);
        REQUIRE(operation.result());
        CHECK(operation.result()->byte_count() == 0U);
        CHECK(operation.result()->source().bytes().empty());
        REQUIRE(operation.result()->source().metadata());
        CHECK(*operation.result()->source().metadata()->content_size == 0U);
    }
}

TEST_CASE("Local asset source enforces exact source-size limits before allocation",
          "[local-assets][source][bounds]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.write_repeated("valve", "exact.bin", 32U);
    temporary.write_repeated("valve", "large.bin", 33U);
    const auto environment = make_environment(
        temporary,
        "valve",
        local::LocalResourceResolverLimits{64U, 64U});
    local_assets::LocalAssetSourceOpener opener;
    local_assets::LocalAssetSourceOpenLimits limits;
    limits.maximum_source_bytes = 32U;

    const auto exact_locator = make_locator(*environment, "exact.bin");
    auto exact = begin_open(opener, exact_locator, environment, limits);
    update_until_terminal(exact);
    REQUIRE(exact.state() ==
            local_assets::LocalAssetSourceOpenState::source_ready);
    REQUIRE(exact.result());
    CHECK(exact.result()->byte_count() == 32U);

    const auto large_path = temporary.game_path("valve") / "large.bin";
    const auto renamed_path = temporary.game_path("valve") / "renamed.bin";
    const auto large_locator = make_locator(*environment, "large.bin");
    auto large = begin_open(opener, large_locator, environment, limits);
    large.update(local_assets::LocalAssetSourceOpenTimePoint{});
    REQUIRE(large.state() == local_assets::LocalAssetSourceOpenState::failed);
    REQUIRE(large.error());
    CHECK(large.error()->code ==
          local_assets::LocalAssetSourceOpenErrorCode::source_too_large);
    CHECK(large.progress_bytes() == 0U);
    CHECK(large.result() == nullptr);
    std::filesystem::rename(large_path, renamed_path);
    CHECK(std::filesystem::exists(renamed_path));
}

TEST_CASE("Local asset-source reopen preserves environment and exact-root provenance",
          "[local-assets][source][locator]")
{
    SECTION("environment mismatch")
    {
        ScopedLocalResourceTestRoot first_root;
        ScopedLocalResourceTestRoot second_root;
        first_root.write("valve", "source.bin", "first");
        second_root.write("valve", "source.bin", "second");
        const auto first = make_environment(first_root);
        const auto second = make_environment(second_root);
        const auto locator = make_locator(*first, "source.bin");
        local_assets::LocalAssetSourceOpener opener;
        auto operation = begin_open(opener, locator, second);
        operation.update(local_assets::LocalAssetSourceOpenTimePoint{});
        REQUIRE(operation.state() ==
                local_assets::LocalAssetSourceOpenState::failed);
        REQUIRE(operation.error());
        CHECK(operation.error()->code == local_assets::
                  LocalAssetSourceOpenErrorCode::locator_environment_mismatch);
        REQUIRE(operation.error()->locator_reopen_code);
        CHECK(*operation.error()->locator_reopen_code == local::
                  LocalResourceLocatorReopenErrorCode::
                      locator_environment_mismatch);
    }

    SECTION("selected-root miss never falls back")
    {
        ScopedLocalResourceTestRoot temporary;
        temporary.create_game("mymod");
        temporary.write("mymod", "shared.bin", "game");
        temporary.write("valve", "shared.bin", "fallback");
        const auto environment = make_environment(temporary, "mymod");
        const auto locator = make_locator(*environment, "shared.bin");
        REQUIRE(locator.root_id().value() == 0U);
        REQUIRE(std::filesystem::remove(
            temporary.game_path("mymod") / "shared.bin"));

        local_assets::LocalAssetSourceOpener opener;
        auto operation = begin_open(opener, locator, environment);
        operation.update(local_assets::LocalAssetSourceOpenTimePoint{});
        REQUIRE(operation.state() ==
                local_assets::LocalAssetSourceOpenState::failed);
        REQUIRE(operation.error());
        CHECK(operation.error()->code == local_assets::
                  LocalAssetSourceOpenErrorCode::locator_target_missing);
        REQUIRE(operation.error()->locator_reopen_code);
        CHECK(*operation.error()->locator_reopen_code == local::
                  LocalResourceLocatorReopenErrorCode::locator_target_missing);
    }

    SECTION("same-size replacement is stale before reading")
    {
        ScopedLocalResourceTestRoot temporary;
        const auto path = temporary.game_path("valve") / "source.bin";
        const auto previous = temporary.game_path("valve") / "previous.bin";
        temporary.write("valve", "source.bin", "same");
        const auto environment = make_environment(temporary);
        const auto locator = make_locator(*environment, "source.bin");
        std::filesystem::rename(path, previous);
        temporary.write("valve", "source.bin", "same");

        local_assets::LocalAssetSourceOpener opener;
        auto operation = begin_open(opener, locator, environment);
        operation.update(local_assets::LocalAssetSourceOpenTimePoint{});
        REQUIRE(operation.state() ==
                local_assets::LocalAssetSourceOpenState::failed);
        REQUIRE(operation.error());
        CHECK(operation.error()->code ==
              local_assets::LocalAssetSourceOpenErrorCode::stale_locator);
    }
}

TEST_CASE("Local asset-source operation cancellation timeout and lease close handles",
          "[local-assets][source][lifetime]")
{
    ScopedLocalResourceTestRoot temporary;
    const auto path = temporary.game_path("valve") / "source.bin";
    const auto renamed = temporary.game_path("valve") / "renamed.bin";
    temporary.write("valve", "source.bin", "source bytes");
    const auto environment = make_environment(temporary);
    const auto locator = make_locator(*environment, "source.bin");

    SECTION("one active operation and cancellation")
    {
        local_assets::LocalAssetSourceOpener opener;
        auto first = opener.begin(locator, environment);
        REQUIRE(first);
        auto blocked = opener.begin(locator, environment);
        REQUIRE_FALSE(blocked);
        REQUIRE(blocked.error);
        CHECK(blocked.error->code == local_assets::
                  LocalAssetSourceOpenErrorCode::open_source_limit_reached);

        first.operation->update(local_assets::LocalAssetSourceOpenTimePoint{});
        first.operation->cancel();
        REQUIRE(first.operation->state() ==
                local_assets::LocalAssetSourceOpenState::cancelled);
        REQUIRE(first.operation->error());
        CHECK(first.operation->error()->code ==
              local_assets::LocalAssetSourceOpenErrorCode::cancelled);
        CHECK(first.operation->result() == nullptr);

        auto after_cancel = opener.begin(locator, environment);
        REQUIRE(after_cancel);
        after_cancel.operation->cancel();
    }

    SECTION("timeout closes the retained file")
    {
        local_assets::LocalAssetSourceOpener opener;
        local_assets::LocalAssetSourceOpenLimits limits;
        limits.timeout = std::chrono::milliseconds{1};
        auto operation = begin_open(opener, locator, environment, limits);
        const auto start = local_assets::LocalAssetSourceOpenTimePoint{};
        operation.update(start);
        REQUIRE(operation.state() ==
                local_assets::LocalAssetSourceOpenState::reading);
        operation.update(start + std::chrono::milliseconds{1});
        REQUIRE(operation.state() ==
                local_assets::LocalAssetSourceOpenState::timed_out);
        REQUIRE(operation.error());
        CHECK(operation.error()->code ==
              local_assets::LocalAssetSourceOpenErrorCode::timed_out);
        CHECK(operation.result() == nullptr);
        std::filesystem::rename(path, renamed);
        CHECK(std::filesystem::exists(renamed));
    }

    SECTION("backwards caller time fails closed and releases the lease")
    {
        local_assets::LocalAssetSourceOpener opener;
        auto operation = begin_open(opener, locator, environment);
        const auto start =
            local_assets::LocalAssetSourceOpenTimePoint{} +
            std::chrono::seconds{1};
        operation.update(start);
        operation.update(start - std::chrono::milliseconds{1});
        REQUIRE(operation.state() ==
                local_assets::LocalAssetSourceOpenState::failed);
        REQUIRE(operation.error());
        CHECK(operation.error()->code == local_assets::
                  LocalAssetSourceOpenErrorCode::invalid_configuration);
        auto after_failure = opener.begin(locator, environment);
        REQUIRE(after_failure);
        after_failure.operation->cancel();
    }

    SECTION("move assignment closes its old handle and transfers one lease")
    {
        local_assets::LocalAssetSourceOpener first_opener;
        local_assets::LocalAssetSourceOpener second_opener;
        auto first = begin_open(first_opener, locator, environment);
        auto second = begin_open(second_opener, locator, environment);
        const auto start = local_assets::LocalAssetSourceOpenTimePoint{};
        first.update(start);
        second.update(start);
        REQUIRE(first.state() ==
                local_assets::LocalAssetSourceOpenState::reading);
        REQUIRE(second.state() ==
                local_assets::LocalAssetSourceOpenState::reading);

        first = std::move(second);
        CHECK(second.state() == local_assets::LocalAssetSourceOpenState::idle);
        auto first_gate_released = first_opener.begin(locator, environment);
        REQUIRE(first_gate_released);
        first_gate_released.operation->cancel();
        auto second_gate_still_held = second_opener.begin(locator, environment);
        REQUIRE_FALSE(second_gate_still_held);
        REQUIRE(second_gate_still_held.error);
        CHECK(second_gate_still_held.error->code == local_assets::
                  LocalAssetSourceOpenErrorCode::open_source_limit_reached);

        first.cancel();
        auto second_gate_released = second_opener.begin(locator, environment);
        REQUIRE(second_gate_released);
        second_gate_released.operation->cancel();
    }
}

TEST_CASE("Local asset source rejects short reads and changed final snapshots",
          "[local-assets][source][toctou]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.write("valve", "source.bin", "stable source bytes");
    const auto environment = make_environment(temporary);
    const auto locator = make_locator(*environment, "source.bin");

    SECTION("stable-metadata short read")
    {
        local_assets::LocalAssetSourceOpener opener;
        auto operation = begin_open(opener, locator, environment);
        const auto start = local_assets::LocalAssetSourceOpenTimePoint{};
        operation.update(start);
        auto* const file = local_assets::detail::
            LocalAssetSourceOpenOperationTestAccess::file(operation);
        REQUIRE(file != nullptr);
        REQUIRE(local::detail::LocalReadOnlyFileTestAccess::
                    seek_native_handle_to_end_without_tracking(*file));
        operation.update(start + std::chrono::milliseconds{1});
        REQUIRE(operation.state() ==
                local_assets::LocalAssetSourceOpenState::failed);
        REQUIRE(operation.error());
        CHECK(operation.error()->code ==
              local_assets::LocalAssetSourceOpenErrorCode::source_read_failed);
        REQUIRE(operation.error()->read_code);
        CHECK(*operation.error()->read_code ==
              local::LocalReadOnlyFileErrorCode::short_read);
        CHECK(operation.result() == nullptr);
    }

    SECTION("write or change metadata drift")
    {
        local_assets::LocalAssetSourceOpener opener;
        auto operation = begin_open(opener, locator, environment);
        operation.update(local_assets::LocalAssetSourceOpenTimePoint{});
        local_assets::detail::LocalAssetSourceOpenOperationTestAccess::
            simulate_final_change_metadata(operation);
        update_until_terminal(operation);
        REQUIRE(operation.state() ==
                local_assets::LocalAssetSourceOpenState::failed);
        REQUIRE(operation.error());
        CHECK(operation.error()->code == local_assets::
                  LocalAssetSourceOpenErrorCode::source_changed_during_read);
        CHECK(operation.result() == nullptr);
    }

    SECTION("identity drift")
    {
        local_assets::LocalAssetSourceOpener opener;
        auto operation = begin_open(opener, locator, environment);
        operation.update(local_assets::LocalAssetSourceOpenTimePoint{});
        local_assets::detail::LocalAssetSourceOpenOperationTestAccess::
            simulate_final_identity_replacement(operation);
        update_until_terminal(operation);
        REQUIRE(operation.state() ==
                local_assets::LocalAssetSourceOpenState::failed);
        REQUIRE(operation.error());
        CHECK(operation.error()->code == local_assets::
                  LocalAssetSourceOpenErrorCode::source_changed_during_read);
        CHECK(operation.result() == nullptr);
    }
}

} // namespace
