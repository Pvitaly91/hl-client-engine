#include <hlclient/goldsrc/local_resource_readiness.hpp>

#include "local_resource_readiness_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

namespace {

namespace fixture = hlclient::tests::readiness_fixture;
namespace goldsrc = hlclient::goldsrc;
using hlclient::tests::ScopedLocalResourceTestRoot;

TEST_CASE("Local readiness preserves wire order and explicit per-entry outcomes",
          "[goldsrc][local-resource][readiness]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    root.write("valve", "models/test_model.mdl", "model");
    root.write("valve", "sound/weapons/test.wav", "sound");
    root.write("valve", "generic/test.dat", "generic");
    root.write("valve", "events/test.sc", "event");
    REQUIRE(std::filesystem::create_directory(
        root.game_path("valve") / "directory-target"));

    std::string non_ascii{"models/"};
    non_ascii.push_back(static_cast<char>(0x80U));
    non_ascii.append(".mdl");
    const std::vector<resource_list_test_fixture::EntrySpec> specifications{
        {2U, "maps/test_map.bsp", 37U, 0x00ff'ffffU, 0U},
        {2U, "models/test_model.mdl", 9U, 1U, 0U},
        {0U, "weapons/test.wav", 4U, 2U, 0U},
        {4U, "generic/test.dat", 8U, 3U, 0U},
        {5U, "events/test.sc", 6U, 4U, 0U},
        {3U, "{lambda", 3U, 5U, 0U},
        {4U, "missing.dat", 10U, 6U, 0U},
        {4U, "../outside.dat", 11U, 7U, 0U},
        {2U, non_ascii, 12U, 8U, 0U},
        {4U, "directory-target", 13U, 9U, 0U},
    };
    auto list = fixture::parse_resource_list(specifications);
    auto environment = fixture::make_environment(root);
    auto inventory = fixture::build_inventory(list, *environment);
    const auto server = fixture::parse_server_info("maps/test_map.bsp");

    const auto built = fixture::build_readiness(
        list, inventory, server, *environment);
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.state);
    REQUIRE(built.state->entry_count() == specifications.size());

    const auto entries = built.state->entries();
    for (std::size_t offset = 0U; offset < entries.size(); ++offset) {
        CAPTURE(offset);
        CHECK(entries[offset].wire_ordinal() == offset);
        CHECK(entries[offset].resource_index() ==
              specifications[offset].index);
        CHECK(entries[offset].original_wire_name_byte_length() ==
              specifications[offset].name.size());
    }

    CHECK(entries[0U].status() ==
          goldsrc::LocalResourceReadinessStatus::ready_local_file);
    REQUIRE(entries[0U].locator());
    CHECK(entries[0U].local_file_size() == 3U);
    CHECK(entries[0U].local_file_size() !=
          specifications[0U].size_code);
    CHECK(entries[0U].stable_identity()->valid());
    CHECK(entries[1U].status() ==
          goldsrc::LocalResourceReadinessStatus::ready_local_file);
    CHECK(entries[2U].status() ==
          goldsrc::LocalResourceReadinessStatus::ready_local_file);
    CHECK(entries[3U].status() ==
          goldsrc::LocalResourceReadinessStatus::ready_local_file);
    CHECK(entries[4U].status() ==
          goldsrc::LocalResourceReadinessStatus::ready_local_file);
    CHECK(entries[5U].status() ==
          goldsrc::LocalResourceReadinessStatus::metadata_only);
    CHECK(entries[5U].impact() ==
          goldsrc::LocalResourceReadinessImpact::metadata_only);
    CHECK_FALSE(entries[5U].locator());
    CHECK(entries[6U].status() ==
          goldsrc::LocalResourceReadinessStatus::missing_local_file);
    CHECK(entries[6U].impact() ==
          goldsrc::LocalResourceReadinessImpact::incomplete);
    CHECK(entries[7U].status() ==
          goldsrc::LocalResourceReadinessStatus::unsafe_name);
    CHECK(entries[7U].impact() ==
          goldsrc::LocalResourceReadinessImpact::security_blocked);
    CHECK(entries[8U].status() ==
          goldsrc::LocalResourceReadinessStatus::unsupported_name_encoding);
    CHECK(entries[8U].impact() ==
          goldsrc::LocalResourceReadinessImpact::unsupported_profile);
    CHECK(entries[9U].status() ==
          goldsrc::LocalResourceReadinessStatus::local_io_error);
    CHECK(entries[9U].impact() ==
          goldsrc::LocalResourceReadinessImpact::local_io_failure);

    const auto& summary = built.state->summary();
    CHECK(summary.total_entry_count() == specifications.size());
    CHECK(summary.resolved_mapped_file_count() == 5U);
    CHECK(summary.metadata_only_count() == 1U);
    CHECK(summary.missing_count() == 1U);
    CHECK(summary.security_blocked_count() == 1U);
    CHECK(summary.unsupported_count() == 1U);
    CHECK(summary.ambiguous_count() == 0U);
    CHECK(summary.io_failure_count() == 1U);
    CHECK_FALSE(built.state->complete_for_supported_local_profile());
    CHECK(built.state->world_geometry_ready());
    CHECK(built.state->world_geometry_candidate_available());
}

TEST_CASE("Metadata-only decals do not block a complete local profile",
          "[goldsrc][local-resource][readiness][complete]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/boot_camp.bsp", "map");
    root.write("valve", "models/scientist.mdl", "model");
    auto list = fixture::parse_resource_list({
        {3U, "{lambda", 0U, 0x00ff'ffffU, 1U},
        {2U, "models/scientist.mdl", 1U, 0U, 0U},
        {2U, "maps/boot_camp.bsp", 301U, 0U, 0U},
    });
    auto environment = fixture::make_environment(root);
    auto inventory = fixture::build_inventory(list, *environment);
    const auto server = fixture::parse_server_info("maps/boot_camp.bsp");

    const auto built = fixture::build_readiness(
        list, inventory, server, *environment);
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->complete_for_supported_local_profile());
    CHECK(built.state->world_geometry_ready());
    CHECK(built.state->world_selection().resource_index() == 301U);
    CHECK(built.state->summary().metadata_only_count() == 1U);
}

TEST_CASE("Case-ambiguous local matches remain explicit readiness failures",
          "[goldsrc][local-resource][readiness][ambiguous]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    const auto case_directory = root.game_path("valve") / "case";
    REQUIRE(std::filesystem::create_directory(case_directory));
    if (!hlclient::tests::enable_case_sensitive_directory(case_directory)) {
        SKIP("Case-sensitive directory mode is unavailable");
    }
    root.write("valve", "case/Ambiguous.bin", "one");
    root.write("valve", "case/ambiguous.bin", "two");

    auto list = fixture::parse_resource_list({
        {2U, "maps/test_map.bsp", 31U, 0U, 0U},
        {4U, "case/aMbiguous.bin", 7U, 0U, 0U},
    });
    auto environment = fixture::make_environment(root);
    auto inventory = fixture::build_inventory(list, *environment);
    const auto server = fixture::parse_server_info("maps/test_map.bsp");

    const auto built = fixture::build_readiness(
        list, inventory, server, *environment);
    REQUIRE(built);
    REQUIRE(built.state);
    REQUIRE(built.state->entry_count() == 2U);
    CHECK(built.state->entries()[1U].status() ==
          goldsrc::LocalResourceReadinessStatus::ambiguous_local_match);
    CHECK(built.state->entries()[1U].impact() ==
          goldsrc::LocalResourceReadinessImpact::local_io_failure);
    CHECK(built.state->summary().ambiguous_count() == 1U);
    CHECK(built.state->world_geometry_ready());
    CHECK_FALSE(built.state->complete_for_supported_local_profile());
}

TEST_CASE("Readiness limits are positive and hard capped",
          "[goldsrc][local-resource][readiness][limits]")
{
    CHECK(goldsrc::valid_local_resource_readiness_limits({}));
    CHECK(goldsrc::valid_local_resource_readiness_limits({
        goldsrc::kMaximumLocalResourceReadinessEntries,
        goldsrc::kMaximumLocatorVirtualNameBytes}));
    CHECK_FALSE(goldsrc::valid_local_resource_readiness_limits({
        0U, goldsrc::kMaximumLocatorVirtualNameBytes}));
    CHECK_FALSE(goldsrc::valid_local_resource_readiness_limits({
        goldsrc::kMaximumLocalResourceReadinessEntries + 1U,
        goldsrc::kMaximumLocatorVirtualNameBytes}));
    CHECK_FALSE(goldsrc::valid_local_resource_readiness_limits({
        1U, goldsrc::kMaximumLocatorVirtualNameBytes + 1U}));
}

static_assert(std::is_copy_constructible_v<
              goldsrc::LocalResourceReadinessState>);
static_assert(!std::is_copy_assignable_v<
              goldsrc::LocalResourceReadinessState>);

} // namespace
