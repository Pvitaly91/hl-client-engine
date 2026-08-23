#include <hlclient/goldsrc/local_resource_readiness.hpp>

#include "local_resource_readiness_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {

namespace fixture = hlclient::tests::readiness_fixture;
namespace goldsrc = hlclient::goldsrc;
using hlclient::tests::ScopedLocalResourceTestRoot;

void check_world_failure(
    const goldsrc::LocalResourceReadinessBuildResult& built,
    const goldsrc::LocalResourceReadinessErrorCode expected_error,
    const goldsrc::WorldResourceReadiness expected_world)
{
    REQUIRE_FALSE(built);
    CHECK_FALSE(built.state);
    REQUIRE(built.error);
    CHECK(built.error->code == expected_error);
    CHECK(built.error->world_status == expected_world);
}

TEST_CASE("World selection uses exact ServerInfo bytes and no fixed index",
          "[goldsrc][precache][world]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/crossfire.bsp", "map");
    auto environment = fixture::make_environment(root);
    auto list = fixture::parse_resource_list({
        {2U, "models/not-the-world.mdl", 0U, 0U, 0U},
        {4U, "generic/missing.dat", 777U, 0U, 0U},
        {2U, "maps/crossfire.bsp", 2047U, 0U, 0U},
    });
    auto inventory = fixture::build_inventory(list, *environment);
    const auto server = fixture::parse_server_info("maps/crossfire.bsp");

    const auto built = fixture::build_readiness(
        list, inventory, server, *environment);
    REQUIRE(built);
    REQUIRE(built.state);
    const auto& world = built.state->world_selection();
    CHECK(world.status() == goldsrc::WorldResourceReadiness::ready);
    CHECK(world.entry_offset() == 2U);
    CHECK(world.wire_ordinal() == 2U);
    CHECK(world.resource_index() == 2047U);
    REQUIRE(world.locator());
    CHECK(world.server_map_name_byte_length() == 18U);
    CHECK(world.world_geometry_ready());
    CHECK(built.state->world_geometry_ready());
    // The missing generic entry makes the full profile incomplete without
    // changing the independent world result.
    CHECK_FALSE(built.state->complete_for_supported_local_profile());
}

TEST_CASE("Exact model map entry with a missing local BSP remains publishable",
          "[goldsrc][precache][world][missing-local]")
{
    ScopedLocalResourceTestRoot root;
    auto environment = fixture::make_environment(root);
    auto list = fixture::parse_resource_list({
        {2U, "maps/boot_camp.bsp", 93U, 0U, 0U},
    });
    auto inventory = fixture::build_inventory(list, *environment);
    const auto server = fixture::parse_server_info("maps/boot_camp.bsp");

    const auto built = fixture::build_readiness(
        list, inventory, server, *environment);
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->world_selection().status() ==
          goldsrc::WorldResourceReadiness::local_map_missing);
    CHECK(built.state->world_selection().entry_offset() == 0U);
    CHECK(built.state->world_selection().resource_index() == 93U);
    CHECK_FALSE(built.state->world_selection().locator());
    CHECK_FALSE(built.state->world_geometry_ready());
}

TEST_CASE("Structural ServerInfo map mismatches are fatal and publish no state",
          "[goldsrc][precache][world][structural]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    auto environment = fixture::make_environment(root);

    SECTION("map entry absent")
    {
        auto list = fixture::parse_resource_list({
            {2U, "maps/another.bsp", 2U, 0U, 0U},
        });
        auto inventory = fixture::build_inventory(list, *environment);
        const auto server = fixture::parse_server_info("maps/test_map.bsp");
        check_world_failure(
            fixture::build_readiness(
                list, inventory, server, *environment),
            goldsrc::LocalResourceReadinessErrorCode::
                map_entry_missing_from_list,
            goldsrc::WorldResourceReadiness::map_entry_missing_from_list);
    }

    SECTION("duplicate exact map entries")
    {
        auto list = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 2U, 0U, 0U},
            {2U, "maps/test_map.bsp", 91U, 0U, 0U},
        });
        auto inventory = fixture::build_inventory(list, *environment);
        const auto server = fixture::parse_server_info("maps/test_map.bsp");
        check_world_failure(
            fixture::build_readiness(
                list, inventory, server, *environment),
            goldsrc::LocalResourceReadinessErrorCode::map_entry_duplicated,
            goldsrc::WorldResourceReadiness::map_entry_duplicated);
    }

    SECTION("sole exact entry is not a model")
    {
        auto list = fixture::parse_resource_list({
            {4U, "maps/test_map.bsp", 71U, 0U, 0U},
        });
        auto inventory = fixture::build_inventory(list, *environment);
        const auto server = fixture::parse_server_info("maps/test_map.bsp");
        check_world_failure(
            fixture::build_readiness(
                list, inventory, server, *environment),
            goldsrc::LocalResourceReadinessErrorCode::map_entry_not_model,
            goldsrc::WorldResourceReadiness::map_entry_not_model);
    }

    SECTION("case-only match is not exact")
    {
        auto list = fixture::parse_resource_list({
            {2U, "maps/Test_Map.bsp", 71U, 0U, 0U},
        });
        auto inventory = fixture::build_inventory(list, *environment);
        const auto server = fixture::parse_server_info("maps/test_map.bsp");
        check_world_failure(
            fixture::build_readiness(
                list, inventory, server, *environment),
            goldsrc::LocalResourceReadinessErrorCode::
                map_entry_missing_from_list,
            goldsrc::WorldResourceReadiness::map_entry_missing_from_list);
    }

    SECTION("unsafe server map bytes are invalid")
    {
        auto list = fixture::parse_resource_list({
            {2U, "../test_map.bsp", 71U, 0U, 0U},
        });
        auto inventory = fixture::build_inventory(list, *environment);
        const auto server = fixture::parse_server_info("../test_map.bsp");
        check_world_failure(
            fixture::build_readiness(
                list, inventory, server, *environment),
            goldsrc::LocalResourceReadinessErrorCode::invalid_server_map_name,
            goldsrc::WorldResourceReadiness::map_name_invalid);
    }

    SECTION("selection does not add maps prefix or bsp extension")
    {
        auto list = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 71U, 0U, 0U},
        });
        auto inventory = fixture::build_inventory(list, *environment);
        const auto server = fixture::parse_server_info("test_map");
        check_world_failure(
            fixture::build_readiness(
                list, inventory, server, *environment),
            goldsrc::LocalResourceReadinessErrorCode::
                map_entry_missing_from_list,
            goldsrc::WorldResourceReadiness::map_entry_missing_from_list);
    }
}

} // namespace
