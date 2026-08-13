#include <hlclient/network/network_address.hpp>
#include <hlclient/network/network_runtime.hpp>
#include <hlclient/network/udp_socket.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace {

using hlclient::network::Datagram;
using hlclient::network::NetworkAddress;
using hlclient::network::NetworkRuntime;
using hlclient::network::ReceiveStatus;
using hlclient::network::UdpSocket;

TEST_CASE("Nonblocking UDP sockets exchange a loopback datagram", "[network][udp]")
{
    NetworkRuntime runtime;
    INFO(runtime.error_message());
    REQUIRE(runtime.valid());

    std::string error;
    auto receiver = UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(receiver.has_value());
    const bool receiver_bound = receiver->bind(NetworkAddress::loopback(0), error);
    INFO(error);
    REQUIRE(receiver_bound);

    const auto receiver_address = receiver->local_address(error);
    INFO(error);
    REQUIRE(receiver_address.has_value());
    REQUIRE(receiver_address->port() != 0U);
    CHECK(receiver_address->ipv4_host_order() == NetworkAddress::loopback(0).ipv4_host_order());

    const auto invalid_receive = receiver->receive(0);
    CHECK(invalid_receive.status == ReceiveStatus::error);
    CHECK_FALSE(invalid_receive.datagram.has_value());
    CHECK_FALSE(invalid_receive.error.empty());

    const auto empty_receive = receiver->receive();
    CHECK(empty_receive.status == ReceiveStatus::would_block);
    CHECK_FALSE(empty_receive.datagram.has_value());
    CHECK(empty_receive.error.empty());

    auto sender = UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(sender.has_value());
    const bool sender_bound = sender->bind(NetworkAddress::loopback(0), error);
    INFO(error);
    REQUIRE(sender_bound);

    const auto sender_address = sender->local_address(error);
    INFO(error);
    REQUIRE(sender_address.has_value());
    REQUIRE(sender_address->port() != 0U);

    const std::array payload{
        std::byte{0xff},
        std::byte{'H'},
        std::byte{'L'},
        std::byte{'3'},
    };
    const bool sent = sender->send_to(*receiver_address, payload, error);
    INFO(error);
    REQUIRE(sent);

    std::optional<Datagram> received_datagram;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (std::chrono::steady_clock::now() < deadline) {
        auto received = receiver->receive(payload.size());
        if (received.status == ReceiveStatus::received) {
            REQUIRE(received.datagram.has_value());
            received_datagram = std::move(received.datagram);
            break;
        }
        if (received.status == ReceiveStatus::error) {
            FAIL(received.error);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    REQUIRE(received_datagram.has_value());
    CHECK(received_datagram->source == *sender_address);
    CHECK(std::ranges::equal(received_datagram->payload, payload));

    const std::array oversized_payload{
        std::byte{0x00},
        std::byte{0x01},
        std::byte{0x02},
        std::byte{0x03},
        std::byte{0x04},
    };
    REQUIRE(sender->send_to(*receiver_address, oversized_payload, error));

    ReceiveStatus oversized_status = ReceiveStatus::would_block;
    const auto oversized_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (std::chrono::steady_clock::now() < oversized_deadline) {
        const auto received = receiver->receive(oversized_payload.size() - 1);
        oversized_status = received.status;
        if (oversized_status != ReceiveStatus::would_block) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    CHECK(oversized_status == ReceiveStatus::truncated);
}

TEST_CASE("UDP sockets retain the network runtime they need", "[network][udp]")
{
    std::optional<UdpSocket> socket;
    std::string error;
    {
        NetworkRuntime runtime;
        INFO(runtime.error_message());
        REQUIRE(runtime.valid());
        socket = UdpSocket::open_ipv4(runtime, error);
        INFO(error);
        REQUIRE(socket.has_value());
    }

    const bool bound = socket->bind(NetworkAddress::loopback(0), error);
    INFO(error);
    CHECK(bound);
}

TEST_CASE("Moved-from UDP sockets fail safely", "[network][udp]")
{
    NetworkRuntime runtime;
    INFO(runtime.error_message());
    REQUIRE(runtime.valid());

    std::string error;
    auto original = UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(original.has_value());

    UdpSocket moved{std::move(*original)};
    CHECK_FALSE(original->bind(NetworkAddress::loopback(0), error));
    CHECK_FALSE(error.empty());

    const bool bound = moved.bind(NetworkAddress::loopback(0), error);
    INFO(error);
    CHECK(bound);
}

} // namespace
