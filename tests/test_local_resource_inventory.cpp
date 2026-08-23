#include "resource_list_test_fixture.hpp"
#include "local_resource_test_fixture.hpp"

#include <hlclient/goldsrc/local_resource_inventory.hpp>
#include <hlclient/goldsrc/local_resource_mapping.hpp>
#include <hlclient/goldsrc/resource_list.hpp>
#include <hlclient/local_resources/local_resource_resolver.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/local_resources/local_virtual_resource_name.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace fixture = resource_list_test_fixture;
namespace goldsrc = hlclient::goldsrc;
namespace local = hlclient::local_resources;

class ScopedInventoryDirectory final {
public:
    ScopedInventoryDirectory()
        : temporary_root_{
              std::filesystem::temp_directory_path().lexically_normal()}
    {
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        for (std::size_t attempt = 0U; attempt < 100U; ++attempt) {
            const auto name = std::string{"hlclient-inventory-tests-"} +
                              std::to_string(timestamp) + "-" +
                              std::to_string(attempt);
            auto candidate = temporary_root_ / name;
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                path_ = std::move(candidate);
                return;
            }
            if (error) {
                throw std::runtime_error{
                    "Unable to create inventory test directory"};
            }
        }
        throw std::runtime_error{
            "Unable to allocate a unique inventory test directory"};
    }

    ~ScopedInventoryDirectory()
    {
        const auto normalized = path_.lexically_normal();
        if (!normalized.empty() &&
            normalized.parent_path() == temporary_root_ &&
            normalized.filename().string().starts_with(
                "hlclient-inventory-tests-")) {
            std::error_code ignored;
            std::filesystem::remove_all(normalized, ignored);
        }
    }

    ScopedInventoryDirectory(const ScopedInventoryDirectory&) = delete;
    ScopedInventoryDirectory& operator=(
        const ScopedInventoryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path temporary_root_;
    std::filesystem::path path_;
};

void write_test_file(
    const std::filesystem::path& path,
    const std::string_view contents)
{
    std::ofstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error{"Unable to create inventory test file"};
    }
    stream.write(
        contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!stream) {
        throw std::runtime_error{"Unable to write inventory test file"};
    }
}

[[nodiscard]] goldsrc::ResourceListState parse_resource_list(
    const std::span<const fixture::EntrySpec> entries)
{
    const auto message = fixture::make_message(entries);
    auto parsed = goldsrc::ResourceListParser{}.parse(
        message.bytes, 0U, message.bit_length);
    REQUIRE(parsed);
    REQUIRE(parsed.state);
    return std::move(*parsed.state);
}

struct ResolverSetup {
    std::unique_ptr<local::LocalResourceResolver> resolver;
    std::optional<local::LocalResourceRootId> game_root_id;
    std::optional<local::LocalResourceRootId> valve_root_id;
};

[[nodiscard]] ResolverSetup make_resolver(
    const std::filesystem::path& base_directory,
    const std::string_view game_directory)
{
    auto roots = local::LocalResourceSearchRoots::create(
        base_directory, game_directory);
    INFO((roots.error ? roots.error->context : std::string{}));
    REQUIRE(roots);
    REQUIRE(roots.roots);

    const auto game_metadata = roots.roots->metadata(0U);
    REQUIRE(game_metadata);
    std::optional<local::LocalResourceRootId> valve_root_id;
    if (roots.roots->size() > 1U) {
        const auto valve_metadata = roots.roots->metadata(1U);
        REQUIRE(valve_metadata);
        valve_root_id = valve_metadata->id;
    }

    auto resolver = local::LocalResourceResolver::create(
        std::move(*roots.roots));
    INFO((resolver.error ? resolver.error->context : std::string{}));
    REQUIRE(resolver);

    return ResolverSetup{
        std::move(resolver.resolver),
        game_metadata->id,
        valve_root_id,
    };
}

[[nodiscard]] unsigned char ascii_lower(const unsigned char value) noexcept
{
    return value >= static_cast<unsigned char>('A') &&
                   value <= static_cast<unsigned char>('Z')
               ? static_cast<unsigned char>(
                     value + static_cast<unsigned char>('a' - 'A'))
               : value;
}

[[nodiscard]] bool ascii_case_equal(
    const std::string_view left,
    const std::string_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (ascii_lower(static_cast<unsigned char>(left[index])) !=
            ascii_lower(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

TEST_CASE("Local resource inventory preserves ordered metadata-only outcomes",
          "[goldsrc][local-resource][inventory]")
{
    ScopedInventoryDirectory temporary;
    const auto valve_root = temporary.path() / "valve";
    const auto game_root = temporary.path() / "mymod";
    REQUIRE(std::filesystem::create_directories(valve_root));
    REQUIRE(std::filesystem::create_directories(game_root));
    REQUIRE(std::filesystem::create_directory(game_root / "directory-target"));

    const auto shared_game_file = game_root / "shared.bin";
    const auto shared_fallback_file = valve_root / "shared.bin";
    const auto fallback_file = valve_root / "fallback.bin";
    write_test_file(shared_game_file, "abc");
    write_test_file(shared_fallback_file, "fallback-copy");
    write_test_file(fallback_file, "12345");

    const auto original_shared_time =
        std::filesystem::last_write_time(shared_game_file);
    const auto original_fallback_time =
        std::filesystem::last_write_time(fallback_file);

    auto resolver = make_resolver(temporary.path(), "mymod");
    REQUIRE(resolver.game_root_id);
    REQUIRE(resolver.valve_root_id);

    std::string non_ascii_name{"models/"};
    non_ascii_name.push_back(static_cast<char>(0x80U));
    non_ascii_name.append(".mdl");
    const std::vector<fixture::EntrySpec> specifications{
        {4U, "shared.bin", 11U, 0x00ff'ffffU, 0U},
        {4U, "fallback.bin", 12U, 1U, 0U},
        {4U, "missing.bin", 13U, 2U, 0U},
        {4U, "../outside.bin", 14U, 3U, 0U},
        {2U, non_ascii_name, 15U, 4U, 0U},
        {3U, "{lambda", 16U, 5U, 0U},
        {4U, "directory-target", 17U, 6U, 0U},
    };
    auto resource_list = parse_resource_list(specifications);

    const auto built = goldsrc::LocalResourceInventoryBuilder{}.build(
        resource_list,
        goldsrc::GoldSrcResourceNameMapper{},
        *resolver.resolver);

    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK_FALSE(built.error);
    REQUIRE(built.state->entry_count() == specifications.size());
    const auto entries = built.state->entries();

    for (std::size_t index = 0U; index < entries.size(); ++index) {
        CAPTURE(index);
        CHECK(entries[index].wire_ordinal() == index);
        CHECK(entries[index].resource_type() ==
              static_cast<goldsrc::ResourceType>(specifications[index].type));
        CHECK(entries[index].resource_index() ==
              specifications[index].index);
        CHECK(entries[index].wire_name_byte_length() ==
              specifications[index].name.size());
    }

    CHECK(entries[0U].resource_type() == goldsrc::ResourceType::generic);
    CHECK(entries[0U].status() ==
          goldsrc::LocalResourceInventoryStatus::resolved);
    REQUIRE(entries[0U].virtual_path());
    REQUIRE(entries[0U].resolved_metadata());
    CHECK(entries[0U].resolved_metadata()->root_id() ==
          *resolver.game_root_id);
    // The opaque wire size code is deliberately 0xffffff; only the handle
    // metadata establishes the local file size.
    CHECK(entries[0U].resolved_metadata()->file_size() == 3U);
    CHECK(entries[0U].resolved_metadata()->identity().valid());

    CHECK(entries[1U].status() ==
          goldsrc::LocalResourceInventoryStatus::resolved);
    REQUIRE(entries[1U].resolved_metadata());
    CHECK(entries[1U].resolved_metadata()->root_id() ==
          *resolver.valve_root_id);
    CHECK(entries[1U].resolved_metadata()->file_size() == 5U);

    CHECK(entries[2U].status() ==
          goldsrc::LocalResourceInventoryStatus::missing);
    CHECK(entries[2U].virtual_path());
    CHECK_FALSE(entries[2U].resolved_metadata());
    CHECK(entries[3U].status() ==
          goldsrc::LocalResourceInventoryStatus::unsafe_name);
    CHECK_FALSE(entries[3U].virtual_path());
    CHECK_FALSE(entries[3U].resolved_metadata());
    CHECK(entries[4U].status() ==
          goldsrc::LocalResourceInventoryStatus::unsupported_name_encoding);
    CHECK_FALSE(entries[4U].virtual_path());
    CHECK(entries[5U].status() ==
          goldsrc::LocalResourceInventoryStatus::unsupported_mapping);
    CHECK_FALSE(entries[5U].virtual_path());
    CHECK(entries[6U].status() ==
          goldsrc::LocalResourceInventoryStatus::io_error);
    CHECK(entries[6U].virtual_path());
    CHECK_FALSE(entries[6U].resolved_metadata());

    const auto expected_virtual_name =
        local::LocalVirtualResourceName::create("shared.bin");
    REQUIRE(expected_virtual_name);
    REQUIRE(expected_virtual_name.name);
    CHECK(entries[0U].virtual_path()->id() ==
          expected_virtual_name.name->id());
    CHECK(entries[0U].virtual_path()->byte_length() == 10U);
    CHECK(entries[0U].virtual_path()->component_count() == 1U);

    const auto& summary = built.state->summary();
    CHECK(summary.total_entry_count() == specifications.size());
    CHECK(summary.count(goldsrc::LocalResourceInventoryStatus::resolved) == 2U);
    CHECK(summary.count(goldsrc::LocalResourceInventoryStatus::missing) == 1U);
    CHECK(summary.count(goldsrc::LocalResourceInventoryStatus::unsafe_name) ==
          1U);
    CHECK(summary.count(
              goldsrc::LocalResourceInventoryStatus::
                  unsupported_name_encoding) == 1U);
    CHECK(summary.count(
              goldsrc::LocalResourceInventoryStatus::unsupported_mapping) ==
          1U);
    CHECK(summary.count(goldsrc::LocalResourceInventoryStatus::ambiguous) ==
          0U);
    CHECK(summary.count(goldsrc::LocalResourceInventoryStatus::io_error) == 1U);

    const auto independent_copy = *built.state;
    REQUIRE(independent_copy.entry_count() == built.state->entry_count());
    CHECK(independent_copy.entries().data() != built.state->entries().data());
    CHECK(independent_copy.entries()[0U].resolved_metadata()->identity() ==
          entries[0U].resolved_metadata()->identity());

    CHECK(std::filesystem::file_size(shared_game_file) == 3U);
    CHECK(std::filesystem::file_size(fallback_file) == 5U);
    CHECK(std::filesystem::last_write_time(shared_game_file) ==
          original_shared_time);
    CHECK(std::filesystem::last_write_time(fallback_file) ==
          original_fallback_time);

    // Inventory entries retain no file handle: the resolved file can be
    // removed while both owning inventory states remain alive.
    CHECK(std::filesystem::remove(shared_game_file));
}

TEST_CASE("Local resource inventory owns metadata beyond its source list",
          "[goldsrc][local-resource][inventory][ownership]")
{
    ScopedInventoryDirectory temporary;
    REQUIRE(std::filesystem::create_directories(temporary.path() / "valve"));
    auto resolver = make_resolver(temporary.path(), "valve");

    auto inventory = [&]() -> goldsrc::LocalResourceInventoryState {
        const std::vector<fixture::EntrySpec> specifications{
            {4U, "missing-after-source-destruction.bin", 77U, 0x00ab'cdefU, 0U},
        };
        auto resource_list = parse_resource_list(specifications);
        auto built = goldsrc::LocalResourceInventoryBuilder{}.build(
            resource_list,
            goldsrc::GoldSrcResourceNameMapper{},
            *resolver.resolver);
        REQUIRE(built);
        REQUIRE(built.state);
        return std::move(*built.state);
    }();

    REQUIRE(inventory.entry_count() == 1U);
    const auto& entry = inventory.entries()[0U];
    CHECK(entry.wire_ordinal() == 0U);
    CHECK(entry.resource_type() == goldsrc::ResourceType::generic);
    CHECK(entry.resource_index() == 77U);
    CHECK(entry.wire_name_byte_length() == 36U);
    CHECK(entry.status() == goldsrc::LocalResourceInventoryStatus::missing);
    CHECK(entry.virtual_path());
    CHECK_FALSE(entry.resolved_metadata());
}

TEST_CASE("Local resource inventory reports case ambiguity when supported",
          "[goldsrc][local-resource][inventory][case]")
{
    ScopedInventoryDirectory temporary;
    const auto valve_root = temporary.path() / "valve";
    const auto game_root = temporary.path() / "mymod";
    const auto case_directory = game_root / "case";
    REQUIRE(std::filesystem::create_directories(valve_root));
    REQUIRE(std::filesystem::create_directories(case_directory));
    if (!hlclient::tests::enable_case_sensitive_directory(case_directory)) {
        SKIP("Case-sensitive directory mode is unavailable");
    }

    write_test_file(case_directory / "Ambiguous.bin", "one");
    write_test_file(case_directory / "ambiguous.bin", "two");

    std::size_t matching_entry_count = 0U;
    for (const auto& entry :
         std::filesystem::directory_iterator{case_directory}) {
        if (ascii_case_equal(
                entry.path().filename().string(), "ambiguous.bin")) {
            ++matching_entry_count;
        }
    }
    if (matching_entry_count < 2U) {
        SKIP("Case-distinct directory entries are unavailable on this volume");
    }

    auto resolver = make_resolver(temporary.path(), "mymod");
    const std::vector<fixture::EntrySpec> specifications{
        {4U, "case/aMbIgUoUs.BiN", 1U, 0U, 0U},
    };
    auto resource_list = parse_resource_list(specifications);
    const auto built = goldsrc::LocalResourceInventoryBuilder{}.build(
        resource_list,
        goldsrc::GoldSrcResourceNameMapper{},
        *resolver.resolver);

    REQUIRE(built);
    REQUIRE(built.state);
    REQUIRE(built.state->entry_count() == 1U);
    CHECK(built.state->entries()[0U].status() ==
          goldsrc::LocalResourceInventoryStatus::ambiguous);
    CHECK(built.state->entries()[0U].virtual_path());
    CHECK_FALSE(built.state->entries()[0U].resolved_metadata());
    CHECK(built.state->summary().count(
              goldsrc::LocalResourceInventoryStatus::ambiguous) == 1U);
}

TEST_CASE("Local resource inventory publishes no partial state on fatal input",
          "[goldsrc][local-resource][inventory][limits]")
{
    ScopedInventoryDirectory temporary;
    REQUIRE(std::filesystem::create_directories(temporary.path() / "valve"));
    auto resolver = make_resolver(temporary.path(), "valve");
    const std::vector<fixture::EntrySpec> specifications{
        {4U, "first.bin", 1U, 0U, 0U},
        {4U, "second.bin", 2U, 0U, 0U},
    };
    auto resource_list = parse_resource_list(specifications);

    SECTION("entry count exceeds the configured bound")
    {
        const goldsrc::LocalResourceInventoryBuilder builder{{1U}};
        const auto built = builder.build(
            resource_list,
            goldsrc::GoldSrcResourceNameMapper{},
            *resolver.resolver);

        CHECK_FALSE(built);
        CHECK_FALSE(built.state);
        REQUIRE(built.error);
        CHECK(built.error->code ==
              goldsrc::LocalResourceInventoryErrorCode::
                  entry_count_limit_exceeded);
        CHECK(built.error->context.find(temporary.path().string()) ==
              std::string::npos);
    }

    SECTION("mapper configuration is invalid")
    {
        const goldsrc::GoldSrcResourceNameMapper invalid_mapper{
            goldsrc::GoldSrcLocalResourceMappingProfile::
                stock_protocol_48_standard,
            {0U, 0U}};
        const auto built = goldsrc::LocalResourceInventoryBuilder{}.build(
            resource_list, invalid_mapper, *resolver.resolver);

        CHECK_FALSE(built);
        CHECK_FALSE(built.state);
        REQUIRE(built.error);
        CHECK(built.error->code ==
              goldsrc::LocalResourceInventoryErrorCode::
                  invalid_configuration);
    }
}

static_assert(std::is_copy_constructible_v<
              goldsrc::LocalResourceInventoryEntry>);
static_assert(!std::is_copy_assignable_v<
              goldsrc::LocalResourceInventoryEntry>);
static_assert(std::is_copy_constructible_v<
              goldsrc::LocalResourceInventoryState>);
static_assert(!std::is_copy_assignable_v<
              goldsrc::LocalResourceInventoryState>);

} // namespace
