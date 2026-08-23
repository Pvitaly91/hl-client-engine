#pragma once

#include <hlclient/goldsrc/local_resource_inventory.hpp>
#include <hlclient/goldsrc/precache_manifest.hpp>
#include <hlclient/goldsrc/resource_list.hpp>
#include <hlclient/goldsrc/server_info.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>

#include "local_resource_test_fixture.hpp"
#include "resource_list_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hlclient::tests::readiness_fixture {

namespace goldsrc = hlclient::goldsrc;
namespace local = hlclient::local_resources;
namespace list_fixture = resource_list_test_fixture;

[[nodiscard]] inline goldsrc::ResourceListState parse_resource_list(
    const std::span<const list_fixture::EntrySpec> entries)
{
    const auto message = list_fixture::make_message(entries);
    const auto parsed = goldsrc::ResourceListParser{}.parse(
        message.bytes, 0U, message.bit_length);
    INFO((parsed.error ? parsed.error->context : std::string{}));
    REQUIRE(parsed);
    REQUIRE(parsed.state);
    return std::move(*parsed.state);
}

[[nodiscard]] inline goldsrc::ResourceListState parse_resource_list(
    const std::initializer_list<list_fixture::EntrySpec> entries)
{
    return parse_resource_list(
        std::span<const list_fixture::EntrySpec>{
            entries.begin(), entries.size()});
}

inline void append_u32_le(
    std::vector<std::byte>& bytes,
    const std::uint32_t value)
{
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
}

inline void append_string(
    std::vector<std::byte>& bytes,
    const std::string_view value)
{
    const auto source = std::as_bytes(std::span{value.data(), value.size()});
    bytes.insert(bytes.end(), source.begin(), source.end());
    bytes.push_back(std::byte{0U});
}

[[nodiscard]] inline goldsrc::ServerInfoState parse_server_info(
    const std::string_view map_name)
{
    std::vector<std::byte> body;
    append_u32_le(body, 48U);
    append_u32_le(body, 0x1234'5678U);
    append_u32_le(body, 0xdead'beefU);
    for (std::uint8_t value = 0U; value < 16U; ++value) {
        body.push_back(static_cast<std::byte>(value));
    }
    body.push_back(std::byte{8U});
    body.push_back(std::byte{0U});
    body.push_back(std::byte{1U});
    append_string(body, "sample");
    append_string(body, "Synthetic Readiness Test");
    append_string(body, map_name);
    append_string(body, "cursor-only");
    body.push_back(std::byte{0U});

    const auto parsed = goldsrc::ServerInfoParser{}.parse(body);
    INFO((parsed.error ? parsed.error->context : std::string{}));
    REQUIRE(parsed);
    REQUIRE(parsed.state);
    return std::move(*parsed.state);
}

[[nodiscard]] inline std::unique_ptr<local::LocalResourceEnvironment>
make_environment(
    const ScopedLocalResourceTestRoot& root,
    const std::string_view game = "valve")
{
    auto roots = local::LocalResourceSearchRoots::create(root.path(), game);
    INFO((roots.error ? roots.error->context : std::string{}));
    REQUIRE(roots);
    REQUIRE(roots.roots);
    auto environment = local::LocalResourceEnvironment::create(
        std::move(*roots.roots));
    INFO((environment.error ? environment.error->context : std::string{}));
    REQUIRE(environment);
    REQUIRE(environment.environment);
    return std::move(environment.environment);
}

[[nodiscard]] inline goldsrc::LocalResourceInventoryState build_inventory(
    const goldsrc::ResourceListState& resource_list,
    const local::LocalResourceEnvironment& environment,
    const goldsrc::GoldSrcResourceNameMapper& mapper =
        goldsrc::GoldSrcResourceNameMapper{})
{
    auto built = goldsrc::LocalResourceInventoryBuilder{}.build(
        resource_list, mapper, environment.resolver());
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.state);
    return std::move(*built.state);
}

[[nodiscard]] inline goldsrc::LocalResourceReadinessBuildResult
build_readiness(
    const goldsrc::ResourceListState& resource_list,
    const goldsrc::LocalResourceInventoryState& inventory,
    const goldsrc::ServerInfoState& server_info,
    const local::LocalResourceEnvironment& environment,
    const goldsrc::GoldSrcResourceNameMapper& mapper =
        goldsrc::GoldSrcResourceNameMapper{})
{
    return goldsrc::LocalResourceReadinessBuilder{}.build(
        resource_list, inventory, server_info, mapper, environment);
}

[[nodiscard]] inline goldsrc::PrecacheManifestBuildResult build_manifest(
    const goldsrc::ResourceListState& resource_list,
    const goldsrc::LocalResourceInventoryState& inventory,
    const goldsrc::ServerInfoState& server_info,
    const local::LocalResourceEnvironment& environment,
    const goldsrc::GoldSrcResourceNameMapper& mapper =
        goldsrc::GoldSrcResourceNameMapper{})
{
    return goldsrc::PrecacheManifestBuilder{}.build(
        resource_list, inventory, server_info, mapper, environment);
}

} // namespace hlclient::tests::readiness_fixture
