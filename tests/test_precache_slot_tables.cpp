#include <hlclient/goldsrc/precache_manifest.hpp>

#include "local_resource_readiness_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {

namespace fixture = hlclient::tests::readiness_fixture;
namespace goldsrc = hlclient::goldsrc;
using hlclient::tests::ScopedLocalResourceTestRoot;

TEST_CASE("Precache slot tables are sparse, type-local, and offset-backed",
          "[goldsrc][precache][slots]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "sound/test.wav", "sound");
    root.write("valve", "maps/test_map.bsp", "map");
    root.write("valve", "models/test.mdl", "model");
    root.write("valve", "generic/zero.dat", "generic-zero");
    root.write("valve", "generic/test.dat", "generic");
    root.write("valve", "events/test.sc", "event");
    auto environment = fixture::make_environment(root);
    auto list = fixture::parse_resource_list({
        {4U, "generic/test.dat", 42U, 0U, 0U},
        {2U, "models/test.mdl", 17U, 0U, 0U},
        {0U, "test.wav", 0U, 0U, 0U},
        {3U, "{lambda", 300U, 0U, 0U},
        {5U, "events/test.sc", 9U, 0U, 0U},
        {2U, "maps/test_map.bsp", 4095U, 0U, 0U},
        {4U, "generic/zero.dat", 0U, 0U, 0U},
    });
    auto inventory = fixture::build_inventory(list, *environment);
    const auto server = fixture::parse_server_info("maps/test_map.bsp");

    const auto built = fixture::build_manifest(
        list, inventory, server, *environment);
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.state);
    const auto& manifest = *built.state;

    CHECK(manifest.sound_slots().slot_count() == 1U);
    CHECK(manifest.sound_slots().entry_offset(0U) == 2U);
    CHECK(manifest.model_slots().slot_count() == 4096U);
    CHECK(manifest.model_slots().occupied_slot_count() == 2U);
    CHECK_FALSE(manifest.model_slots().entry_offset(0U));
    CHECK(manifest.model_slots().entry_offset(17U) == 1U);
    CHECK_FALSE(manifest.model_slots().entry_offset(18U));
    CHECK(manifest.model_slots().entry_offset(4095U) == 5U);
    CHECK(manifest.generic_slots().slot_count() == 43U);
    CHECK(manifest.generic_slots().entry_offset(0U) == 6U);
    CHECK(manifest.generic_slots().entry_offset(42U) == 0U);
    CHECK(manifest.event_script_slots().slot_count() == 10U);
    CHECK(manifest.event_script_slots().entry_offset(9U) == 4U);
    CHECK(manifest.decal_slots().slot_count() == 301U);
    CHECK(manifest.decal_slots().entry_offset(300U) == 3U);

    // Numeric index zero exists independently in sound and generic namespaces.
    REQUIRE(manifest.find(goldsrc::ResourceType::sound, 0U));
    REQUIRE(manifest.find(goldsrc::ResourceType::generic, 0U));
    CHECK(manifest.find(goldsrc::ResourceType::sound, 0U)->wire_ordinal() == 2U);
    CHECK(manifest.find(goldsrc::ResourceType::generic, 0U)->wire_ordinal() == 6U);
    CHECK_FALSE(manifest.find(goldsrc::ResourceType::event_script, 8U));

    REQUIRE(manifest.entries().size() == 7U);
    CHECK(manifest.entries()[0U].resource_index() == 42U);
    CHECK(manifest.entries()[1U].resource_index() == 17U);
    CHECK(manifest.entries()[2U].resource_index() == 0U);
    CHECK(manifest.entries()[5U].resource_index() == 4095U);
}

TEST_CASE("Precache slot allocations validate configured bounds before resize",
          "[goldsrc][precache][slots][limits]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    auto environment = fixture::make_environment(root);
    auto list = fixture::parse_resource_list({
        {2U, "maps/test_map.bsp", 4095U, 0U, 0U},
    });
    auto inventory = fixture::build_inventory(list, *environment);
    const auto server = fixture::parse_server_info("maps/test_map.bsp");

    SECTION("wire maximum creates exactly 4096 slots")
    {
        const auto built = goldsrc::PrecacheManifestBuilder{}.build(
            list,
            inventory,
            server,
            goldsrc::GoldSrcResourceNameMapper{},
            *environment);
        REQUIRE(built);
        REQUIRE(built.state);
        CHECK(built.state->model_slots().slot_count() == 4096U);
    }

    SECTION("configured per-type limit rejects one additional slot")
    {
        goldsrc::PrecacheManifestLimits limits;
        limits.maximum_slots_per_type = 4095U;
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
                  slots_per_type_limit_exceeded);
    }

    SECTION("configured total limit is checked independently")
    {
        goldsrc::PrecacheManifestLimits limits;
        limits.maximum_total_slots = 4095U;
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
              goldsrc::PrecacheManifestErrorCode::total_slot_limit_exceeded);
    }
}

TEST_CASE("Precache manifest limits expose exact hard caps",
          "[goldsrc][precache][limits]")
{
    CHECK(goldsrc::valid_precache_manifest_limits({}));
    CHECK(goldsrc::valid_precache_manifest_limits({
        goldsrc::kMaximumLocalResourceReadinessEntries,
        goldsrc::kMaximumPrecacheManifestEntries,
        goldsrc::kMaximumPrecacheSlotsPerType,
        goldsrc::kMaximumPrecacheManifestTotalSlots,
        goldsrc::kMaximumPrecacheManifestEvents,
        goldsrc::kMaximumLocatorVirtualNameBytes}));

    auto invalid = goldsrc::PrecacheManifestLimits{};
    invalid.maximum_manifest_entries =
        goldsrc::kMaximumPrecacheManifestEntries + 1U;
    CHECK_FALSE(goldsrc::valid_precache_manifest_limits(invalid));
    invalid = {};
    invalid.maximum_total_slots =
        goldsrc::kMaximumPrecacheManifestTotalSlots + 1U;
    CHECK_FALSE(goldsrc::valid_precache_manifest_limits(invalid));
    invalid = {};
    invalid.maximum_manifest_events =
        goldsrc::kMaximumPrecacheManifestEvents + 1U;
    CHECK_FALSE(goldsrc::valid_precache_manifest_limits(invalid));
}

} // namespace
