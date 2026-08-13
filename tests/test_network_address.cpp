#include <hlclient/network/network_address.hpp>
#include <hlclient/network/network_runtime.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

using hlclient::network::NetworkAddress;
using hlclient::network::NetworkRuntime;

TEST_CASE("NetworkAddress parses and formats IPv4 endpoints", "[network][address]")
{
    const auto address = NetworkAddress::parse("127.0.0.1:27015");

    REQUIRE(address.has_value());
    CHECK(address->ipv4_host_order() == 0x7f00'0001U);
    CHECK(address->port() == 27'015U);
    CHECK(address->to_string() == "127.0.0.1:27015");
    CHECK(*address == NetworkAddress::loopback(27'015U));
}

TEST_CASE("NetworkAddress accepts numeric endpoint boundaries", "[network][address]")
{
    const auto minimum = NetworkAddress::parse("0.0.0.0:0");
    REQUIRE(minimum.has_value());
    CHECK(minimum->ipv4_host_order() == 0U);
    CHECK(minimum->port() == 0U);

    const auto maximum = NetworkAddress::parse("255.255.255.255:65535");
    REQUIRE(maximum.has_value());
    CHECK(maximum->ipv4_host_order() == 0xffff'ffffU);
    CHECK(maximum->port() == 65'535U);
}

TEST_CASE("NetworkAddress rejects malformed endpoints", "[network][address]")
{
    constexpr std::array invalid_endpoints{
        std::string_view{},
        std::string_view{"127.0.0.1"},
        std::string_view{":27015"},
        std::string_view{"127.0.0.1:"},
        std::string_view{"127.0.0:27015"},
        std::string_view{"127.0.0.1.2:27015"},
        std::string_view{"256.0.0.1:27015"},
        std::string_view{"127.0.0.-1:27015"},
        std::string_view{"127.0.0.1:-1"},
        std::string_view{"127.0.0.1:65536"},
        std::string_view{"127.0.0.1:27015extra"},
        std::string_view{"[::1]:27015"},
    };

    for (const auto endpoint : invalid_endpoints) {
        CAPTURE(endpoint);
        CHECK_FALSE(NetworkAddress::parse(endpoint).has_value());
    }
}

TEST_CASE("NetworkAddress resolves a numeric IPv4 host", "[network][address]")
{
    NetworkRuntime runtime;
    INFO(runtime.error_message());
    REQUIRE(runtime.valid());

    std::string error;
    const auto address = NetworkAddress::resolve_ipv4(runtime, "127.0.0.1", 27'015U, error);

    INFO(error);
    REQUIRE(address.has_value());
    CHECK(*address == NetworkAddress::loopback(27'015U));
    CHECK(error.empty());
}

TEST_CASE("NetworkAddress resolver reports an empty host", "[network][address]")
{
    NetworkRuntime runtime;
    INFO(runtime.error_message());
    REQUIRE(runtime.valid());

    std::string error{"stale error"};
    const auto address = NetworkAddress::resolve_ipv4(runtime, {}, 27'015U, error);

    CHECK_FALSE(address.has_value());
    CHECK_FALSE(error.empty());
}

TEST_CASE("NetworkAddress resolver rejects embedded NUL bytes", "[network][address]")
{
    NetworkRuntime runtime;
    INFO(runtime.error_message());
    REQUIRE(runtime.valid());

    constexpr char host_with_nul[]{'1', '2', '7', '\0', '.', '0', '.', '0', '.', '1'};
    std::string error;
    const auto address = NetworkAddress::resolve_ipv4(
        runtime,
        std::string_view{host_with_nul, sizeof(host_with_nul)},
        27'015U,
        error);

    CHECK_FALSE(address.has_value());
    CHECK(error.find("NUL") != std::string::npos);
}

} // namespace
