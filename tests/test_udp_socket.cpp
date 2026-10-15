#include <hlclient/network/network_address.hpp>
#include <hlclient/network/datagram_transport.hpp>
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

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <winsock2.h>
#endif

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
    CHECK_FALSE(invalid_receive.native_error);

    const auto empty_receive = receiver->receive();
    CHECK(empty_receive.status == ReceiveStatus::would_block);
    CHECK_FALSE(empty_receive.datagram.has_value());
    CHECK(empty_receive.error.empty());
    CHECK_FALSE(empty_receive.native_error);

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

    hlclient::network::ReceiveResult oversized_result;
    const auto oversized_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (std::chrono::steady_clock::now() < oversized_deadline) {
        oversized_result = receiver->receive(oversized_payload.size() - 1);
        if (oversized_result.status != ReceiveStatus::would_block) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    CHECK(oversized_result.status == ReceiveStatus::truncated);
    REQUIRE(oversized_result.source);
    CHECK(*oversized_result.source == *sender_address);
    CHECK(oversized_result.payload_size_lower_bound >= oversized_payload.size());
#ifdef _WIN32
    REQUIRE(oversized_result.native_error);
    CHECK(oversized_result.native_error.domain ==
          hlclient::network::SocketNativeErrorDomain::winsock);
    CHECK(oversized_result.native_error.code == WSAEMSGSIZE);
#endif

#ifdef _WIN32
    hlclient::network::SocketNativeError send_native_error;
    CHECK_FALSE(sender->send_to(
        NetworkAddress{0xffffffffU, 9U}, payload, error, &send_native_error));
    REQUIRE(send_native_error);
    CHECK(send_native_error.domain ==
          hlclient::network::SocketNativeErrorDomain::winsock);
    CHECK(send_native_error.code != 0U);
    CHECK(error.find(std::to_string(send_native_error.code)) !=
          std::string::npos);
#endif
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

TEST_CASE("UDP datagram transport preserves native failure codes and neutral polling",
          "[network][udp][e311][failure-metadata]")
{
    NetworkRuntime runtime;
    REQUIRE(runtime.valid());
    std::string error;
    auto receiver = UdpSocket::open_ipv4(runtime, error);
    REQUIRE(receiver);
    REQUIRE(receiver->bind(NetworkAddress::loopback(0), error));
    hlclient::network::UdpDatagramTransport transport{std::move(*receiver)};
    const auto address = transport.local_address();
    REQUIRE(address.address);
    const auto empty = transport.receive(16U);
    CHECK(empty.status == hlclient::network::DatagramTransportReceiveStatus::would_block);
    CHECK_FALSE(empty.native_error);
    CHECK(empty.error.empty());
    const auto invalid = transport.receive(0U);
    CHECK(invalid.status == hlclient::network::DatagramTransportReceiveStatus::error);
    CHECK_FALSE(invalid.native_error);

    auto sender = UdpSocket::open_ipv4(runtime, error);
    REQUIRE(sender);
    REQUIRE(sender->bind(NetworkAddress::loopback(0), error));
    const std::array payload{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    REQUIRE(sender->send_to(*address.address, payload, error));
    hlclient::network::DatagramTransportReceiveResult truncated;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    do {
        truncated = transport.receive(2U);
        if (truncated.status != hlclient::network::DatagramTransportReceiveStatus::would_block)
            break;
        CHECK_FALSE(truncated.native_error);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    } while (std::chrono::steady_clock::now() < deadline);
    REQUIRE(truncated.status == hlclient::network::DatagramTransportReceiveStatus::truncated);
    REQUIRE(truncated.source);
    CHECK(truncated.payload_size == 3U); // receive limit + one, not the unknown full size.
#ifdef _WIN32
    REQUIRE(truncated.native_error);
    CHECK(truncated.native_error.domain == hlclient::network::SocketNativeErrorDomain::winsock);
    CHECK(truncated.native_error.code == WSAEMSGSIZE);

    // Without SO_BROADCAST this send is rejected locally by Winsock; no
    // outbound broadcast is permitted. Verify the adapter retains that code.
    const auto rejected = transport.send_to(NetworkAddress{0xffffffffU, 9U}, payload);
    CHECK(rejected.status == hlclient::network::DatagramSendStatus::error);
    REQUIRE(rejected.native_error);
    CHECK(rejected.native_error.domain == hlclient::network::SocketNativeErrorDomain::winsock);
    CHECK(rejected.native_error.code == WSAEACCES);
#endif
    const auto after = transport.receive(16U);
    CHECK(after.status == hlclient::network::DatagramTransportReceiveStatus::would_block);
    CHECK_FALSE(after.native_error);
    CHECK(after.error.empty());
    CHECK_FALSE(transport.close());
    const auto closed_send = transport.send_to(NetworkAddress::loopback(1U), payload);
    CHECK(closed_send.status == hlclient::network::DatagramSendStatus::error);
    CHECK_FALSE(closed_send.native_error);
    const auto closed_receive = transport.receive(16U);
    CHECK(closed_receive.status == hlclient::network::DatagramTransportReceiveStatus::error);
    CHECK_FALSE(closed_receive.native_error);
}

TEST_CASE("E28.1 explicit UDP close releases the port before owner destruction and is idempotent",
          "[e281][network][udp][socket-teardown]")
{
    NetworkRuntime runtime;
    REQUIRE(runtime.valid());
    std::string error;
    auto original=UdpSocket::open_ipv4(runtime,error);
    REQUIRE(original);
    REQUIRE(original->bind(NetworkAddress::loopback(0),error));
    const auto address=original->local_address(error);
    REQUIRE(address);
    const auto closed=original->close();
    CHECK_FALSE(closed);
    CHECK(closed.domain==hlclient::network::SocketNativeErrorDomain::none);
    CHECK(closed.code==0U);
    CHECK_FALSE(original->local_address(error));
    CHECK_FALSE(error.empty());
    CHECK(original->receive().status==ReceiveStatus::error);

    // Native port release is observable while the old UdpSocket object lives.
    // A potentially reused handle belongs only to the new socket from here.
    auto replacement=UdpSocket::open_ipv4(runtime,error);
    REQUIRE(replacement);
    REQUIRE(replacement->bind(*address,error));
    CHECK_FALSE(original->close());
    original.reset();
    CHECK(replacement->local_address(error)==address);
    CHECK_FALSE(replacement->close());
    CHECK_FALSE(replacement->close());
}

TEST_CASE("E28.1 datagram transport close prevents late send receive and moved-source double teardown",
          "[e281][network][udp][socket-teardown]")
{
    NetworkRuntime runtime;
    REQUIRE(runtime.valid());
    std::string error;
    auto source=UdpSocket::open_ipv4(runtime,error);
    REQUIRE(source);
    REQUIRE(source->bind(NetworkAddress::loopback(0),error));
    hlclient::network::UdpDatagramTransport transport{std::move(*source)};
    REQUIRE(transport.local_address());
    CHECK_FALSE(source->close()); // The moved-from source owns no native handle.
    REQUIRE(transport.local_address());
    CHECK_FALSE(transport.close());
    CHECK_FALSE(transport.local_address());
    const std::array bytes{std::byte{1}};
    CHECK(transport.send_to(NetworkAddress::loopback(27015),bytes).status==
          hlclient::network::DatagramSendStatus::error);
    CHECK(transport.receive(16U).status==hlclient::network::DatagramTransportReceiveStatus::error);
    CHECK_FALSE(transport.close());
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

    const std::array payload{std::byte{0x01U}};
    hlclient::network::SocketNativeError native_error;
    CHECK_FALSE(original->send_to(
        NetworkAddress::loopback(1U), payload, error, &native_error));
    CHECK_FALSE(native_error);

    const bool bound = moved.bind(NetworkAddress::loopback(0), error);
    INFO(error);
    CHECK(bound);
}

} // namespace
