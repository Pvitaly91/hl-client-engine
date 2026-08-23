#include <hlclient/goldsrc/precache_manifest.hpp>

#include "local_resource_inventory_test_access.hpp"
#include "local_resource_readiness_test_fixture.hpp"
#include "precache_manifest_test_access.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>

namespace {

namespace fixture = hlclient::tests::readiness_fixture;
namespace goldsrc = hlclient::goldsrc;
using hlclient::tests::ScopedLocalResourceTestRoot;

TEST_CASE("Precache manifest is an immutable owning metadata-only snapshot",
          "[goldsrc][precache][manifest]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    root.write("valve", "models/test.mdl", "model");
    root.write("valve", "sound/test.wav", "sound");
    auto environment = fixture::make_environment(root);

    auto manifest = [&]() -> goldsrc::PrecacheManifestState {
        auto list = fixture::parse_resource_list({
            {0U, "test.wav", 7U, 0x00ff'ffffU, 1U},
            {3U, "{lambda", 2U, 0U, 0U},
            {2U, "maps/test_map.bsp", 87U, 1U, 0U},
            {2U, "models/test.mdl", 11U, 2U, 0U},
        });
        auto inventory = fixture::build_inventory(list, *environment);
        const auto server = fixture::parse_server_info("maps/test_map.bsp");
        auto built = fixture::build_manifest(
            list, inventory, server, *environment);
        INFO((built.error ? built.error->context : std::string{}));
        REQUIRE(built);
        REQUIRE(built.state);
        return std::move(*built.state);
    }();

    REQUIRE(manifest.entry_count() == 4U);
    CHECK(manifest.complete_for_supported_local_profile());
    CHECK(manifest.completeness() ==
          goldsrc::PrecacheManifestCompleteness::
              complete_for_supported_local_profile);
    CHECK(manifest.world_geometry_ready());
    REQUIRE(manifest.world_entry());
    CHECK(manifest.world_entry()->resource_type() ==
          goldsrc::ResourceType::model);
    CHECK(manifest.world_entry()->resource_index() == 87U);
    CHECK(manifest.world_selection().wire_ordinal() == 2U);
    CHECK(manifest.world_selection().resource_index() == 87U);
    REQUIRE(manifest.world_selection().locator());

    const auto& summary = manifest.readiness_summary();
    CHECK(summary.total_entry_count() == 4U);
    CHECK(summary.resolved_mapped_file_count() == 3U);
    CHECK(summary.metadata_only_count() == 1U);
    CHECK(summary.missing_count() == 0U);
    CHECK(manifest.source_geometry().resource_count == 4U);
    CHECK(manifest.source_geometry().total_name_byte_count == 47U);

    for (const auto& entry : manifest.entries()) {
        CHECK(entry.type_local_slot() == entry.resource_index());
        if (entry.readiness_status() ==
            goldsrc::LocalResourceReadinessStatus::ready_local_file) {
            REQUIRE(entry.locator());
            REQUIRE(entry.local_file_size());
        } else {
            CHECK(entry.readiness_status() ==
                  goldsrc::LocalResourceReadinessStatus::metadata_only);
            CHECK_FALSE(entry.locator());
            CHECK_FALSE(entry.local_file_size());
        }
    }

    const auto independent_copy = manifest;
    CHECK(independent_copy.entries().data() != manifest.entries().data());
    CHECK(independent_copy.world_entry());
    CHECK(independent_copy.world_entry()->resource_index() == 87U);
    CHECK(independent_copy.model_slots().entry_offset(87U) == 2U);
}

TEST_CASE("Manifest completeness preserves all counts with deterministic priority",
          "[goldsrc][precache][manifest][completeness]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    auto environment = fixture::make_environment(root);
    const auto server = fixture::parse_server_info("maps/test_map.bsp");

    SECTION("world ready but another file is missing")
    {
        auto list = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 9U, 0U, 0U},
            {0U, "missing.wav", 3U, 0U, 0U},
        });
        auto inventory = fixture::build_inventory(list, *environment);
        const auto built = fixture::build_manifest(
            list, inventory, server, *environment);
        REQUIRE(built);
        REQUIRE(built.state);
        CHECK(built.state->world_geometry_ready());
        CHECK(built.state->completeness() ==
              goldsrc::PrecacheManifestCompleteness::
                  world_ready_but_incomplete);
        CHECK(built.state->readiness_summary().missing_count() == 1U);
    }

    SECTION("local map missing")
    {
        std::error_code error;
        REQUIRE(std::filesystem::remove(
            root.game_path("valve") / "maps/test_map.bsp", error));
        REQUIRE_FALSE(error);
        auto missing_environment = fixture::make_environment(root);
        auto list = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 9U, 0U, 0U},
        });
        auto inventory =
            fixture::build_inventory(list, *missing_environment);
        const auto built = fixture::build_manifest(
            list, inventory, server, *missing_environment);
        REQUIRE(built);
        REQUIRE(built.state);
        CHECK_FALSE(built.state->world_geometry_ready());
        CHECK(built.state->world_selection().status() ==
              goldsrc::WorldResourceReadiness::local_map_missing);
        CHECK(built.state->completeness() ==
              goldsrc::PrecacheManifestCompleteness::
                  incomplete_missing_resources);
    }

    SECTION("security block outranks missing and unsupported")
    {
        std::string non_ascii{"models/"};
        non_ascii.push_back(static_cast<char>(0x80U));
        non_ascii.append(".mdl");
        const std::vector<resource_list_test_fixture::EntrySpec> specs{
            {2U, "maps/test_map.bsp", 9U, 0U, 0U},
            {4U, "missing.dat", 1U, 0U, 0U},
            {4U, "../outside.dat", 2U, 0U, 0U},
            {2U, non_ascii, 3U, 0U, 0U},
        };
        auto list = fixture::parse_resource_list(specs);
        auto inventory = fixture::build_inventory(list, *environment);
        const auto built = fixture::build_manifest(
            list, inventory, server, *environment);
        REQUIRE(built);
        REQUIRE(built.state);
        CHECK(built.state->readiness_summary().missing_count() == 1U);
        CHECK(built.state->readiness_summary().security_blocked_count() == 1U);
        CHECK(built.state->readiness_summary().unsupported_count() == 1U);
        CHECK(built.state->completeness() ==
              goldsrc::PrecacheManifestCompleteness::
                  blocked_unsafe_resources);
    }

    SECTION("local I/O failure outranks unsupported profile")
    {
        REQUIRE(std::filesystem::create_directory(
            root.game_path("valve") / "directory-target"));
        std::string non_ascii{"models/"};
        non_ascii.push_back(static_cast<char>(0x80U));
        non_ascii.append(".mdl");
        const std::vector<resource_list_test_fixture::EntrySpec> specs{
            {2U, "maps/test_map.bsp", 9U, 0U, 0U},
            {4U, "directory-target", 1U, 0U, 0U},
            {2U, non_ascii, 3U, 0U, 0U},
        };
        auto list = fixture::parse_resource_list(specs);
        auto inventory = fixture::build_inventory(list, *environment);
        const auto built = fixture::build_manifest(
            list, inventory, server, *environment);
        REQUIRE(built);
        REQUIRE(built.state);
        CHECK(built.state->readiness_summary().io_failure_count() == 1U);
        CHECK(built.state->readiness_summary().unsupported_count() == 1U);
        CHECK(built.state->completeness() ==
              goldsrc::PrecacheManifestCompleteness::local_io_failure);
    }
}

TEST_CASE("Structural world mismatch fails manifest transactionally",
          "[goldsrc][precache][manifest][transactional]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/listed.bsp", "map");
    auto environment = fixture::make_environment(root);
    auto list = fixture::parse_resource_list({
        {2U, "maps/listed.bsp", 4U, 0U, 0U},
    });
    auto inventory = fixture::build_inventory(list, *environment);
    const auto server = fixture::parse_server_info("maps/unlisted.bsp");

    const auto built = fixture::build_manifest(
        list, inventory, server, *environment);
    REQUIRE_FALSE(built);
    CHECK_FALSE(built.state);
    REQUIRE(built.error);
    CHECK(built.error->code ==
          goldsrc::PrecacheManifestErrorCode::readiness_build_failed);
    CHECK(built.error->readiness_code ==
          goldsrc::LocalResourceReadinessErrorCode::
              map_entry_missing_from_list);
    CHECK(built.error->world_status ==
          goldsrc::WorldResourceReadiness::map_entry_missing_from_list);
}

TEST_CASE("Manifest entry limit failure publishes no partial state",
          "[goldsrc][precache][manifest][limits]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    auto environment = fixture::make_environment(root);
    auto list = fixture::parse_resource_list({
        {2U, "maps/test_map.bsp", 4U, 0U, 0U},
        {4U, "missing.dat", 2U, 0U, 0U},
    });
    auto inventory = fixture::build_inventory(list, *environment);
    const auto server = fixture::parse_server_info("maps/test_map.bsp");
    auto limits = goldsrc::PrecacheManifestLimits{};
    limits.maximum_manifest_entries = 1U;

    const auto built = goldsrc::PrecacheManifestBuilder{limits}.build(
        list,
        inventory,
        server,
        goldsrc::GoldSrcResourceNameMapper{},
        *environment);
    REQUIRE_FALSE(built);
    CHECK_FALSE(built.state);
    REQUIRE(built.error);
    CHECK(built.error->code ==
          goldsrc::PrecacheManifestErrorCode::
              manifest_entry_limit_exceeded);
}

TEST_CASE("Unsupported mapping remains explicit through readiness and manifest",
          "[goldsrc][local-resource][readiness][precache][manifest]"
          "[unsupported-mapping]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    root.write("valve", "generic/test.dat", "generic");
    auto environment = fixture::make_environment(root);
    auto list = fixture::parse_resource_list({
        {2U, "maps/test_map.bsp", 9U, 0U, 0U},
        {4U, "generic/test.dat", 17U, 0U, 0U},
    });
    auto inventory = fixture::build_inventory(list, *environment);
    const auto server = fixture::parse_server_info("maps/test_map.bsp");

    constexpr auto unsupported_type =
        static_cast<goldsrc::ResourceType>(1U);
    goldsrc::detail::ResourceListCorruptionTestAccess::replace_resource_type(
        list, 1U, unsupported_type);
    goldsrc::detail::LocalResourceInventoryCorruptionTestAccess::
        replace_type_and_status(
            inventory,
            1U,
            unsupported_type,
            goldsrc::LocalResourceInventoryStatus::unsupported_mapping);

    auto readiness = fixture::build_readiness(
        list, inventory, server, *environment);
    INFO((readiness.error ? readiness.error->context : std::string{}));
    REQUIRE(readiness);
    REQUIRE(readiness.state);
    REQUIRE(readiness.state->entry_count() == 2U);
    CHECK(readiness.state->entries()[1U].status() ==
          goldsrc::LocalResourceReadinessStatus::unsupported_mapping);
    CHECK(readiness.state->entries()[1U].impact() ==
          goldsrc::LocalResourceReadinessImpact::unsupported_profile);
    CHECK_FALSE(readiness.state->entries()[1U].locator());
    CHECK(readiness.state->summary().unsupported_count() == 1U);
    CHECK_FALSE(readiness.state->complete_for_supported_local_profile());
    CHECK(readiness.state->world_geometry_ready());

    // A future supported wire type can carry the same readiness outcome.
    // Normalize only the fabricated type so the manifest path can exercise
    // unsupported_mapping independently of its invalid-type defense.
    goldsrc::detail::PrecacheManifestDefensiveTestAccess::
        replace_resource_type(
            *readiness.state, 1U, goldsrc::ResourceType::generic);
    const auto manifest =
        goldsrc::detail::PrecacheManifestDefensiveTestAccess::build(
            goldsrc::PrecacheManifestBuilder{},
            *readiness.state,
            list,
            server);
    INFO((manifest.error ? manifest.error->context : std::string{}));
    REQUIRE(manifest);
    REQUIRE(manifest.state);
    REQUIRE(manifest.state->entry_count() == 2U);
    CHECK(manifest.state->entries()[1U].readiness_status() ==
          goldsrc::LocalResourceReadinessStatus::unsupported_mapping);
    CHECK(manifest.state->entries()[1U].readiness_impact() ==
          goldsrc::LocalResourceReadinessImpact::unsupported_profile);
    CHECK_FALSE(manifest.state->entries()[1U].locator());
    CHECK(manifest.state->readiness_summary().unsupported_count() == 1U);
    CHECK(manifest.state->completeness() ==
          goldsrc::PrecacheManifestCompleteness::unsupported_profile);
    CHECK_FALSE(manifest.state->complete_for_supported_local_profile());
    CHECK(manifest.state->world_geometry_ready());
}

TEST_CASE("Manifest rejects an out-of-wire-range readiness index atomically",
          "[goldsrc][precache][manifest][defensive][bounds]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    auto environment = fixture::make_environment(root);
    auto list = fixture::parse_resource_list({
        {2U, "maps/test_map.bsp", 9U, 0U, 0U},
        {4U, "missing.dat", 17U, 0U, 0U},
    });
    auto inventory = fixture::build_inventory(list, *environment);
    const auto server = fixture::parse_server_info("maps/test_map.bsp");
    auto readiness = fixture::build_readiness(
        list, inventory, server, *environment);
    REQUIRE(readiness);
    REQUIRE(readiness.state);

    goldsrc::detail::PrecacheManifestDefensiveTestAccess::
        replace_resource_index(
            *readiness.state,
            1U,
            static_cast<std::uint16_t>(
                goldsrc::kMaximumResourceIndexWireValue + 1U));
    const auto built =
        goldsrc::detail::PrecacheManifestDefensiveTestAccess::build(
            goldsrc::PrecacheManifestBuilder{},
            *readiness.state,
            list,
            server);

    REQUIRE_FALSE(built);
    CHECK_FALSE(built.state);
    REQUIRE(built.error);
    CHECK(built.error->code ==
          goldsrc::PrecacheManifestErrorCode::resource_index_out_of_bounds);
    CHECK(built.error->entry_ordinal == 1U);
    CHECK_FALSE(built.error->readiness_code);
    CHECK_FALSE(built.error->world_status);
}

TEST_CASE("Manifest rejects duplicate type-local slots atomically",
          "[goldsrc][precache][manifest][defensive][slots]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    auto environment = fixture::make_environment(root);
    auto list = fixture::parse_resource_list({
        {2U, "maps/test_map.bsp", 9U, 0U, 0U},
        {4U, "missing-a.dat", 17U, 0U, 0U},
        {4U, "missing-b.dat", 18U, 0U, 0U},
    });
    auto inventory = fixture::build_inventory(list, *environment);
    const auto server = fixture::parse_server_info("maps/test_map.bsp");
    auto readiness = fixture::build_readiness(
        list, inventory, server, *environment);
    REQUIRE(readiness);
    REQUIRE(readiness.state);

    goldsrc::detail::PrecacheManifestDefensiveTestAccess::
        replace_resource_index(*readiness.state, 2U, 17U);
    const auto built =
        goldsrc::detail::PrecacheManifestDefensiveTestAccess::build(
            goldsrc::PrecacheManifestBuilder{},
            *readiness.state,
            list,
            server);
    REQUIRE_FALSE(built);
    CHECK_FALSE(built.state);
    REQUIRE(built.error);
    CHECK(built.error->code ==
          goldsrc::PrecacheManifestErrorCode::duplicate_type_local_slot);
    CHECK(built.error->entry_ordinal == 2U);
    CHECK_FALSE(built.error->readiness_code);
    CHECK_FALSE(built.error->world_status);
}

static_assert(std::is_copy_constructible_v<goldsrc::PrecacheManifestState>);
static_assert(!std::is_copy_assignable_v<goldsrc::PrecacheManifestState>);
static_assert(!std::is_default_constructible_v<goldsrc::PrecacheManifestState>);

} // namespace
