#include <hlclient/goldsrc/local_resource_readiness.hpp>

#include "local_resource_inventory_test_access.hpp"
#include "local_resource_readiness_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {

namespace fixture = hlclient::tests::readiness_fixture;
namespace goldsrc = hlclient::goldsrc;
using InventoryCorruption =
    goldsrc::detail::LocalResourceInventoryCorruptionTestAccess;
using hlclient::tests::ScopedLocalResourceTestRoot;

void check_correlation_error(
    const goldsrc::LocalResourceReadinessBuildResult& result,
    const goldsrc::LocalResourceReadinessErrorCode expected)
{
    REQUIRE_FALSE(result);
    CHECK_FALSE(result.state);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
    CHECK_FALSE(result.error->context.empty());
    CHECK(result.error->context.size() <=
          goldsrc::kLocalResourceReadinessDiagnosticTextLimit);
}

TEST_CASE("Strict resource-list inventory correlation accepts only exact metadata",
          "[goldsrc][local-resource][correlation]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    auto environment = fixture::make_environment(root);
    const auto server = fixture::parse_server_info("maps/test_map.bsp");

    SECTION("exact correlation succeeds")
    {
        auto list = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 27U, 0x00ff'ffffU, 0U},
        });
        auto inventory = fixture::build_inventory(list, *environment);
        const auto built = fixture::build_readiness(
            list, inventory, server, *environment);
        REQUIRE(built);
        REQUIRE(built.state);
        CHECK_FALSE(built.error);
        CHECK(built.state->entry_count() == 1U);
    }

    SECTION("entry count mismatch is fatal")
    {
        auto list = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 27U, 0U, 0U},
            {4U, "missing.bin", 1U, 0U, 0U},
        });
        auto inventory_source = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 27U, 0U, 0U},
        });
        auto inventory =
            fixture::build_inventory(inventory_source, *environment);
        check_correlation_error(
            fixture::build_readiness(
                list, inventory, server, *environment),
            goldsrc::LocalResourceReadinessErrorCode::entry_count_mismatch);
    }

    SECTION("resource type mismatch is fatal")
    {
        auto list = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 27U, 0U, 0U},
        });
        auto inventory_source = fixture::parse_resource_list({
            {4U, "maps/test_map.bsp", 27U, 0U, 0U},
        });
        auto inventory =
            fixture::build_inventory(inventory_source, *environment);
        check_correlation_error(
            fixture::build_readiness(
                list, inventory, server, *environment),
            goldsrc::LocalResourceReadinessErrorCode::resource_type_mismatch);
    }

    SECTION("wire ordinal mismatch is fatal")
    {
        auto list = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 27U, 0U, 0U},
        });
        auto inventory = fixture::build_inventory(list, *environment);
        InventoryCorruption::replace_wire_ordinal(inventory, 0U, 1U);

        check_correlation_error(
            fixture::build_readiness(
                list, inventory, server, *environment),
            goldsrc::LocalResourceReadinessErrorCode::wire_ordinal_mismatch);
    }

    SECTION("resource index mismatch is fatal")
    {
        auto list = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 27U, 0U, 0U},
        });
        auto inventory_source = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 28U, 0U, 0U},
        });
        auto inventory =
            fixture::build_inventory(inventory_source, *environment);
        check_correlation_error(
            fixture::build_readiness(
                list, inventory, server, *environment),
            goldsrc::LocalResourceReadinessErrorCode::resource_index_mismatch);
    }

    SECTION("wire-name length mismatch is fatal")
    {
        auto list = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 27U, 0U, 0U},
        });
        auto inventory_source = fixture::parse_resource_list({
            {2U, "maps/x.bsp", 27U, 0U, 0U},
        });
        auto inventory =
            fixture::build_inventory(inventory_source, *environment);
        check_correlation_error(
            fixture::build_readiness(
                list, inventory, server, *environment),
            goldsrc::LocalResourceReadinessErrorCode::wire_name_length_mismatch);
    }

    SECTION("same-length virtual-name identity mismatch is fatal")
    {
        auto list = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 27U, 0U, 0U},
            {4U, "alpha.bin", 8U, 0U, 0U},
        });
        auto inventory_source = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 27U, 0U, 0U},
            {4U, "bravo.bin", 8U, 0U, 0U},
        });
        auto inventory =
            fixture::build_inventory(inventory_source, *environment);
        check_correlation_error(
            fixture::build_readiness(
                list, inventory, server, *environment),
            goldsrc::LocalResourceReadinessErrorCode::virtual_path_id_mismatch);
    }

    SECTION("classification-status mismatch is fatal")
    {
        auto list = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 27U, 0U, 0U},
            {4U, "safe.bin", 8U, 0U, 0U},
        });
        auto inventory_source = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 27U, 0U, 0U},
            {4U, "../x.bin", 8U, 0U, 0U},
        });
        auto inventory =
            fixture::build_inventory(inventory_source, *environment);
        check_correlation_error(
            fixture::build_readiness(
                list, inventory, server, *environment),
            goldsrc::LocalResourceReadinessErrorCode::
                classification_status_mismatch);
    }

    SECTION("resolved status without metadata is fatal")
    {
        auto list = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 27U, 0U, 0U},
        });
        auto inventory = fixture::build_inventory(list, *environment);
        InventoryCorruption::remove_resolved_metadata(inventory, 0U);

        check_correlation_error(
            fixture::build_readiness(
                list, inventory, server, *environment),
            goldsrc::LocalResourceReadinessErrorCode::
                invalid_resolved_metadata);
    }

    SECTION("unresolved status with resolved metadata is fatal")
    {
        auto list = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 27U, 0U, 0U},
            {0U, "missing.wav", 8U, 0U, 0U},
        });
        auto inventory = fixture::build_inventory(list, *environment);
        REQUIRE(inventory.entries()[0U].resolved_metadata());
        REQUIRE(inventory.entries()[1U].status() ==
                goldsrc::LocalResourceInventoryStatus::missing);
        InventoryCorruption::copy_resolved_metadata(inventory, 1U, 0U);

        check_correlation_error(
            fixture::build_readiness(
                list, inventory, server, *environment),
            goldsrc::LocalResourceReadinessErrorCode::
                invalid_resolved_metadata);
    }

    SECTION("virtual component-count mismatch is fatal")
    {
        auto list = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 27U, 0U, 0U},
        });
        auto inventory = fixture::build_inventory(list, *environment);
        InventoryCorruption::replace_virtual_component_count(
            inventory, 0U, 1U);

        check_correlation_error(
            fixture::build_readiness(
                list, inventory, server, *environment),
            goldsrc::LocalResourceReadinessErrorCode::
                virtual_path_component_count_mismatch);
    }

    SECTION("invalid stable identity is fatal")
    {
        auto list = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 27U, 0U, 0U},
        });
        auto inventory = fixture::build_inventory(list, *environment);
        InventoryCorruption::invalidate_resolved_identity(inventory, 0U);

        check_correlation_error(
            fixture::build_readiness(
                list, inventory, server, *environment),
            goldsrc::LocalResourceReadinessErrorCode::
                invalid_resolved_metadata);
    }

    SECTION("fatal correlation publishes neither readiness nor manifest")
    {
        auto list = fixture::parse_resource_list({
            {2U, "maps/test_map.bsp", 27U, 0U, 0U},
        });
        auto inventory = fixture::build_inventory(list, *environment);
        InventoryCorruption::remove_resolved_metadata(inventory, 0U);

        const auto readiness = fixture::build_readiness(
            list, inventory, server, *environment);
        REQUIRE_FALSE(readiness);
        CHECK_FALSE(readiness.state);
        REQUIRE(readiness.error);
        CHECK(readiness.error->code ==
              goldsrc::LocalResourceReadinessErrorCode::
                  invalid_resolved_metadata);

        const auto manifest = fixture::build_manifest(
            list, inventory, server, *environment);
        REQUIRE_FALSE(manifest);
        CHECK_FALSE(manifest.state);
        REQUIRE(manifest.error);
        CHECK(manifest.error->code ==
              goldsrc::PrecacheManifestErrorCode::readiness_build_failed);
        CHECK(manifest.error->readiness_code ==
              goldsrc::LocalResourceReadinessErrorCode::
                  invalid_resolved_metadata);
    }
}

TEST_CASE("Correlation rejects inventory metadata from another root environment",
          "[goldsrc][local-resource][correlation][environment]")
{
    ScopedLocalResourceTestRoot first_root;
    ScopedLocalResourceTestRoot second_root;
    first_root.write("valve", "maps/test_map.bsp", "first");
    second_root.write("valve", "maps/test_map.bsp", "second");
    auto first_environment = fixture::make_environment(first_root);
    auto second_environment = fixture::make_environment(second_root);
    auto list = fixture::parse_resource_list({
        {2U, "maps/test_map.bsp", 27U, 0U, 0U},
    });
    auto inventory = fixture::build_inventory(list, *first_environment);
    const auto server = fixture::parse_server_info("maps/test_map.bsp");

    check_correlation_error(
        fixture::build_readiness(
            list, inventory, server, *second_environment),
        goldsrc::LocalResourceReadinessErrorCode::invalid_resolved_metadata);
}

} // namespace
