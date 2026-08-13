#include <hlclient/goldsrc/challenge_protocol.hpp>
#include <hlclient/goldsrc/connect_request.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/network/network_address.hpp>
#include <hlclient/network/network_runtime.hpp>
#include <hlclient/network/udp_socket.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace std::chrono_literals;
using hlclient::network::NetworkAddress;
using hlclient::network::NetworkRuntime;
using hlclient::network::ReceiveStatus;
using hlclient::network::UdpSocket;

inline constexpr std::string_view kSyntheticAuthenticationMarker = "TEST_AUTH_MATERIAL";
inline constexpr std::string_view kSyntheticProtectedAuthentication =
    "TEST_AUTH_MATERIAL_TEST_AUTH_MAT";

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view text)
{
    const auto view = std::as_bytes(std::span{text.data(), text.size()});
    return std::vector<std::byte>{view.begin(), view.end()};
}

[[nodiscard]] std::vector<std::byte> challenge_response(const std::uint32_t challenge)
{
    std::string packet{"\xFF\xFF\xFF\xFF", 4U};
    packet += "A00000000 " + std::to_string(challenge) +
              " 3 72057594037927936 0\n";
    packet.push_back('\0');
    return bytes(packet);
}

[[nodiscard]] std::vector<std::byte> accept_response(const NetworkAddress client)
{
    std::string packet{"\xFF\xFF\xFF\xFF", 4U};
    packet += "B 1 \"" + client.to_string() + "\" 0 10210";
    packet.push_back('\0');
    return bytes(packet);
}

[[nodiscard]] std::vector<std::byte> reject_response()
{
    std::string packet{"\xFF\xFF\xFF\xFF", 4U};
    packet += "9Invalid connection.\n";
    packet.push_back('\0');
    return bytes(packet);
}

[[nodiscard]] hlclient::goldsrc::PreparedConnectRequest prepared_request()
{
    std::vector<std::byte> suffix(
        hlclient::goldsrc::kObservedConnectAuthenticationSuffixSize);
    const auto marker = bytes(kSyntheticAuthenticationMarker);
    for (std::size_t index = 0U; index < suffix.size(); ++index) {
        suffix[index] = marker[index % marker.size()];
    }
    auto material = hlclient::goldsrc::AuthenticationMaterial::create(
        bytes(kSyntheticProtectedAuthentication),
        suffix);
    REQUIRE(material);
    auto profile = hlclient::goldsrc::ConnectCompatibilityProfile{};
    profile.protected_authentication_is_ascii_hex = false;
    auto prepared = hlclient::goldsrc::prepare_connect_request(
        {},
        std::move(*material.value),
        profile);
    REQUIRE(prepared);
    return std::move(*prepared.value);
}

enum class FakeOutcome {
    accepted,
    rejected,
};

void run_fake_hlds(const FakeOutcome outcome)
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
    hlclient::network::UdpDatagramTransport transport{std::move(*client_socket)};

    hlclient::goldsrc::ChallengeExchangeConfig challenge_config;
    challenge_config.retry_interval = 100ms;
    challenge_config.timeout = 1s;
    challenge_config.maximum_attempts = 2U;
    challenge_config.maximum_datagrams_per_update = 4U;
    hlclient::goldsrc::ConnectResponseWaitConfig response_config;
    response_config.timeout = 1s;
    response_config.maximum_datagrams_per_update = 4U;

    hlclient::goldsrc::GoldSrcHandshakeCoordinator handshake{
        transport,
        *server_endpoint,
        hlclient::goldsrc::HandshakeStopPoint::connect_response,
        prepared_request(),
        challenge_config,
        {},
        {},
        response_config};

    const auto getchallenge = hlclient::goldsrc::build_getchallenge_request();
    REQUIRE(getchallenge);
    constexpr std::uint32_t challenge = 0xF0000001U;
    REQUIRE(handshake.start(hlclient::goldsrc::ChallengeExchangeClock::now()));

    std::optional<NetworkAddress> source;
    std::vector<std::byte> connect_datagram;
    bool response_sent = false;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline && !handshake.terminal()) {
        const auto received = server_socket->receive(
            hlclient::goldsrc::kMaximumConnectDatagramSize);
        if (received.status == ReceiveStatus::received) {
            REQUIRE(received.datagram);
            if (!source) {
                CHECK(received.datagram->payload == *getchallenge.datagram);
                source = received.datagram->source;
                REQUIRE(server_socket->send_to(
                    *source,
                    challenge_response(challenge),
                    error));
            } else {
                CHECK(received.datagram->source == *source);
                connect_datagram = received.datagram->payload;
                const auto parsed = hlclient::goldsrc::parse_connect_request(
                    connect_datagram,
                    [] {
                        auto profile = hlclient::goldsrc::ConnectCompatibilityProfile{};
                        profile.protected_authentication_is_ascii_hex = false;
                        return profile;
                    }());
                REQUIRE(parsed);
                CHECK(parsed.request->challenge() == challenge);

                // An unrelated connectionless packet before the real response
                // must not become a terminal outcome.
                std::string unrelated{"\xFF\xFF\xFF\xFF", 4U};
                unrelated += "I synthetic unrelated\0";
                REQUIRE(server_socket->send_to(*source, bytes(unrelated), error));
                REQUIRE(server_socket->send_to(
                    *source,
                    outcome == FakeOutcome::accepted ? accept_response(*source)
                                                     : reject_response(),
                    error));
                response_sent = true;
            }
        } else if (received.status == ReceiveStatus::error ||
                   received.status == ReceiveStatus::truncated) {
            FAIL(received.error);
        }

        handshake.update(hlclient::goldsrc::ChallengeExchangeClock::now());
        if (!handshake.terminal()) {
            std::this_thread::sleep_for(1ms);
        }
    }

    REQUIRE(source);
    REQUIRE(response_sent);
    REQUIRE_FALSE(connect_datagram.empty());
    REQUIRE(handshake.local_endpoint());
    CHECK(*handshake.local_endpoint() == *source);
    CHECK(handshake.connect_send_attempts() == 1U);

    if (outcome == FakeOutcome::accepted) {
        REQUIRE(handshake.state() == hlclient::goldsrc::GoldSrcHandshakeState::accepted);
        REQUIRE(handshake.connect_response());
        const auto& accepted = std::get<hlclient::goldsrc::ConnectAccepted>(
            *handshake.connect_response());
        CHECK(accepted.user_id == 1U);
        CHECK(accepted.server_view_of_client == *source);
        CHECK_FALSE(accepted.secure);
        CHECK(accepted.server_build == 10'210U);
    } else {
        REQUIRE(handshake.state() == hlclient::goldsrc::GoldSrcHandshakeState::rejected);
        REQUIRE(handshake.connect_response());
        CHECK(std::get<hlclient::goldsrc::ConnectRejected>(*handshake.connect_response()).message ==
              "Invalid connection.");
    }

    handshake.update(hlclient::goldsrc::ChallengeExchangeClock::now() + 5s);
    bool unexpected_fourth_datagram = false;
    const auto quiet_deadline = std::chrono::steady_clock::now() + 25ms;
    while (std::chrono::steady_clock::now() < quiet_deadline) {
        const auto received = server_socket->receive(
            hlclient::goldsrc::kMaximumConnectResponseDatagramSize);
        if (received.status == ReceiveStatus::received) {
            unexpected_fourth_datagram = true;
            break;
        }
        if (received.status == ReceiveStatus::error ||
            received.status == ReceiveStatus::truncated) {
            FAIL(received.error);
        }
        std::this_thread::sleep_for(1ms);
    }
    CHECK_FALSE(unexpected_fourth_datagram);
}

TEST_CASE("M2.2 fake HLDS accepts one exact same-socket connect request",
          "[goldsrc][connect-response][udp][accept]")
{
    run_fake_hlds(FakeOutcome::accepted);
}

TEST_CASE("M2.2 fake HLDS rejects one exact same-socket connect request",
          "[goldsrc][connect-response][udp][reject]")
{
    run_fake_hlds(FakeOutcome::rejected);
}

} // namespace
