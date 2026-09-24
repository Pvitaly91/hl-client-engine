#include <hlclient/goldsrc/stock_spawn_request.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <string_view>

namespace {

namespace goldsrc = hlclient::goldsrc;

TEST_CASE("Stock spawn request has exact bounded string-command bytes",
          "[goldsrc][signon][spawn]")
{
    const auto built = goldsrc::StockSpawnRequestBuilder::build(
        1U, 0x1122'3344U);

    REQUIRE(built);
    REQUIRE(built.encoding.has_value());
    CHECK_FALSE(built.error.has_value());
    CHECK(built.encoding->server_count() == 1U);
    CHECK(built.encoding->world_map_crc() == 0x1122'3344U);

    constexpr std::string_view expected_command{
        "spawn 1 -1171047755"};
    const auto bytes = built.encoding->semantic_bytes();
    REQUIRE(bytes.size() == expected_command.size() + 2U);
    CHECK(bytes.front() == std::byte{3U});
    CHECK(bytes.back() == std::byte{0U});
    CHECK(std::ranges::equal(
        bytes.subspan(1U, expected_command.size()),
        std::as_bytes(std::span{expected_command.data(), expected_command.size()})));
}

TEST_CASE("Stock spawn request rejects missing session identity without bytes",
          "[goldsrc][signon][spawn][validation]")
{
    const auto built = goldsrc::StockSpawnRequestBuilder::build(
        0U, 0x1122'3344U);

    REQUIRE_FALSE(built);
    REQUIRE(built.error.has_value());
    CHECK(
        built.error->code ==
        goldsrc::StockSpawnRequestErrorCode::invalid_server_count);
    CHECK_FALSE(built.encoding.has_value());
    CHECK_FALSE(built.error->context.empty());
    CHECK(
        built.error->context.size() <=
        goldsrc::kStockSpawnDiagnosticTextLimit);
}

TEST_CASE("Server map CRC unmunge uses the assigned slot key",
          "[goldsrc][signon][spawn][server-info]")
{
    CHECK(
        goldsrc::decode_stock_server_world_map_crc(0xdead'beefU, 0U) ==
        0x4ae6'ed21U);
    CHECK(
        goldsrc::decode_stock_server_world_map_crc(0xdead'beefU, 1U) !=
        0x4ae6'ed21U);
}

TEST_CASE("Stock signon-one reply has exact typed sendents bytes",
          "[goldsrc][signon][sendents]")
{
    const auto request =
        goldsrc::StockSendEntitiesRequestBuilder::build();
    constexpr std::string_view expected_command{"sendents"};
    const auto bytes = request.semantic_bytes();

    CHECK(request.opcode() == goldsrc::ClientMessageOpcode::string_command);
    CHECK(request.command() == expected_command);
    REQUIRE(bytes.size() == goldsrc::kStockSendEntitiesRequestSize);
    CHECK(bytes.front() == std::byte{3U});
    CHECK(bytes.back() == std::byte{0U});
    CHECK(std::ranges::equal(
        bytes.subspan(1U, expected_command.size()),
        std::as_bytes(
            std::span{expected_command.data(), expected_command.size()})));
}

} // namespace
