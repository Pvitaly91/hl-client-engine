#include <hlclient/goldsrc/challenge_exchange.hpp>
#include <hlclient/goldsrc/challenge_protocol.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/network/network_address.hpp>
#include <hlclient/network/network_runtime.hpp>
#include <hlclient/network/udp_socket.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using hlclient::network::NetworkAddress;
using hlclient::network::NetworkRuntime;
using hlclient::network::ReceiveStatus;
using hlclient::network::UdpSocket;

[[nodiscard]] std::vector<std::byte> ascii_bytes(const std::string_view text)
{
    std::vector<std::byte> result;
    result.reserve(text.size());
    std::ranges::transform(text, std::back_inserter(result), [](const char value) {
        return static_cast<std::byte>(static_cast<unsigned char>(value));
    });
    return result;
}

[[nodiscard]] std::vector<std::byte> fake_hlds_response(const std::uint32_t challenge)
{
    std::string packet{"\xFF\xFF\xFF\xFF", 4U};
    packet += "A00000000 ";
    packet += std::to_string(challenge);
    packet += " 3 72057594037927936 0\n";
    packet.push_back('\0');
    return ascii_bytes(packet);
}

TEST_CASE("Challenge exchange interoperates with a local UDP fake HLDS", "[goldsrc][challenge][udp]")
{
    NetworkRuntime runtime;
    INFO(runtime.error_message());
    REQUIRE(runtime.valid());

    std::string error;
    auto server_socket = UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(server_socket);
    REQUIRE(server_socket->bind(NetworkAddress::loopback(0), error));
    const auto server_endpoint = server_socket->local_address(error);
    INFO(error);
    REQUIRE(server_endpoint);

    auto client_socket = UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(client_socket);
    REQUIRE(client_socket->bind(NetworkAddress::loopback(0), error));
    hlclient::network::UdpDatagramTransport client_transport{std::move(*client_socket)};

    hlclient::goldsrc::ChallengeExchangeConfig config;
    config.retry_interval = 100ms;
    config.timeout = 1s;
    config.maximum_attempts = 3U;
    config.maximum_datagrams_per_update = 4U;
    config.maximum_datagram_size =
        hlclient::goldsrc::kMaximumConnectionlessChallengeDatagramSize;
    hlclient::goldsrc::ChallengeExchange exchange{client_transport, *server_endpoint, config};

    const auto started = hlclient::goldsrc::ChallengeExchangeClock::now();
    REQUIRE(exchange.start(started));
    bool responded = false;
    const auto wall_deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < wall_deadline &&
           exchange.state() == hlclient::goldsrc::ChallengeExchangeState::waiting_for_response) {
        if (!responded) {
            auto request = server_socket->receive(
                hlclient::goldsrc::kMaximumConnectionlessChallengeDatagramSize);
            if (request.status == ReceiveStatus::received) {
                REQUIRE(request.datagram);
                const auto expected = hlclient::goldsrc::build_getchallenge_request();
                REQUIRE(expected);
                CHECK(request.datagram->payload == *expected.datagram);

                const auto response = fake_hlds_response(364'337'887);
                REQUIRE(server_socket->send_to(request.datagram->source, response, error));
                responded = true;
            } else if (request.status == ReceiveStatus::error ||
                       request.status == ReceiveStatus::truncated) {
                FAIL(request.error);
            }
        }

        exchange.update(hlclient::goldsrc::ChallengeExchangeClock::now());
        if (exchange.state() == hlclient::goldsrc::ChallengeExchangeState::waiting_for_response) {
            std::this_thread::sleep_for(1ms);
        }
    }

    REQUIRE(responded);
    CHECK(exchange.state() == hlclient::goldsrc::ChallengeExchangeState::challenge_received);
    REQUIRE(exchange.challenge());
    CHECK(exchange.challenge()->challenge == 364'337'887);
    REQUIRE(exchange.local_endpoint());
    CHECK(exchange.local_endpoint()->port() != 0U);

    exchange.update(hlclient::goldsrc::ChallengeExchangeClock::now() + config.retry_interval * 2);
    bool unexpected_follow_up = false;
    const auto quiet_deadline = std::chrono::steady_clock::now() + 25ms;
    while (std::chrono::steady_clock::now() < quiet_deadline) {
        const auto datagram = server_socket->receive(
            hlclient::goldsrc::kMaximumConnectionlessChallengeDatagramSize);
        if (datagram.status == ReceiveStatus::received) {
            unexpected_follow_up = true;
            break;
        }
        if (datagram.status == ReceiveStatus::error ||
            datagram.status == ReceiveStatus::truncated) {
            FAIL(datagram.error);
        }
        std::this_thread::sleep_for(1ms);
    }
    CHECK_FALSE(unexpected_follow_up);
}

TEST_CASE("Challenge exchange retries when a local UDP fake HLDS ignores the first request",
          "[goldsrc][challenge][udp]")
{
    NetworkRuntime runtime;
    INFO(runtime.error_message());
    REQUIRE(runtime.valid());

    std::string error;
    auto server_socket = UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(server_socket);
    REQUIRE(server_socket->bind(NetworkAddress::loopback(0), error));
    const auto server_endpoint = server_socket->local_address(error);
    INFO(error);
    REQUIRE(server_endpoint);

    auto client_socket = UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(client_socket);
    REQUIRE(client_socket->bind(NetworkAddress::loopback(0), error));
    hlclient::network::UdpDatagramTransport client_transport{std::move(*client_socket)};

    hlclient::goldsrc::ChallengeExchangeConfig config;
    config.retry_interval = 20ms;
    config.timeout = 1s;
    config.maximum_attempts = 3U;
    config.maximum_datagrams_per_update = 4U;
    config.maximum_datagram_size =
        hlclient::goldsrc::kMaximumConnectionlessChallengeDatagramSize;
    hlclient::goldsrc::ChallengeExchange exchange{client_transport, *server_endpoint, config};

    const auto expected = hlclient::goldsrc::build_getchallenge_request();
    REQUIRE(expected);
    REQUIRE(exchange.start(hlclient::goldsrc::ChallengeExchangeClock::now()));

    std::size_t received_requests = 0U;
    const auto wall_deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < wall_deadline &&
           exchange.state() == hlclient::goldsrc::ChallengeExchangeState::waiting_for_response) {
        auto request = server_socket->receive(
            hlclient::goldsrc::kMaximumConnectionlessChallengeDatagramSize);
        if (request.status == ReceiveStatus::received) {
            REQUIRE(request.datagram);
            CHECK(request.datagram->payload == *expected.datagram);
            ++received_requests;
            if (received_requests == 2U) {
                const auto response = fake_hlds_response(987'654'321);
                REQUIRE(server_socket->send_to(request.datagram->source, response, error));
            }
        } else if (request.status == ReceiveStatus::error ||
                   request.status == ReceiveStatus::truncated) {
            FAIL(request.error);
        }

        exchange.update(hlclient::goldsrc::ChallengeExchangeClock::now());
        if (exchange.state() == hlclient::goldsrc::ChallengeExchangeState::waiting_for_response) {
            std::this_thread::sleep_for(1ms);
        }
    }

    CHECK(received_requests == 2U);
    CHECK(exchange.attempts() == 2U);
    CHECK(exchange.state() == hlclient::goldsrc::ChallengeExchangeState::challenge_received);
    REQUIRE(exchange.challenge());
    CHECK(exchange.challenge()->challenge == 987'654'321);
}

} // namespace
