#include <hlclient/goldsrc/connect_request.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>
#include <hlclient/goldsrc/challenge_protocol.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/network/network_address.hpp>
#include <hlclient/network/network_runtime.hpp>
#include <hlclient/network/udp_socket.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;
using hlclient::network::NetworkAddress;
using hlclient::network::NetworkRuntime;
using hlclient::network::ReceiveStatus;
using hlclient::network::UdpSocket;

inline constexpr std::string_view kSyntheticProtectedAuthentication =
    "TEST_AUTH_MATERIAL_TEST_AUTH_MAT";
inline constexpr std::string_view kSyntheticAuthenticationMarker = "TEST_AUTH_MATERIAL";

[[nodiscard]] hlclient::goldsrc::ConnectCompatibilityProfile synthetic_profile() noexcept
{
    auto profile = hlclient::goldsrc::ConnectCompatibilityProfile{};
    profile.protected_authentication_is_ascii_hex = false;
    return profile;
}

[[nodiscard]] std::vector<std::byte> ascii_bytes(const std::string_view text)
{
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    return std::vector<std::byte>{bytes.begin(), bytes.end()};
}

[[nodiscard]] std::vector<std::byte> fake_challenge_response(const std::uint32_t challenge)
{
    std::string packet{"\xFF\xFF\xFF\xFF", 4U};
    packet += "A00000000 " + std::to_string(challenge) +
              " 3 72057594037927936 0\n";
    packet.push_back('\0');
    return ascii_bytes(packet);
}

[[nodiscard]] hlclient::goldsrc::AuthenticationMaterial synthetic_authentication()
{
    std::vector<std::byte> suffix(hlclient::goldsrc::kObservedConnectAuthenticationSuffixSize);
    const auto marker_bytes =
        std::as_bytes(std::span{kSyntheticAuthenticationMarker.data(),
                                kSyntheticAuthenticationMarker.size()});
    for (std::size_t index = 0U; index < suffix.size(); ++index) {
        suffix[index] = marker_bytes[index % marker_bytes.size()];
    }
    auto authentication = hlclient::goldsrc::AuthenticationMaterial::create(
        std::as_bytes(std::span{kSyntheticProtectedAuthentication.data(),
                                kSyntheticProtectedAuthentication.size()}),
        suffix);
    REQUIRE(authentication);
    return std::move(*authentication.value);
}

TEST_CASE("M2.1 handshake sends one connect request from the challenge source port",
          "[goldsrc][connect][udp]")
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

    hlclient::goldsrc::ClientConnectionSettings settings;
    settings.display_name = "Test Player";
    settings.model = "ivan";
    const auto profile = synthetic_profile();
    auto prepared = hlclient::goldsrc::prepare_connect_request(
        settings, synthetic_authentication(), profile);
    REQUIRE(prepared);

    hlclient::goldsrc::ChallengeExchangeConfig challenge_config;
    challenge_config.retry_interval = 100ms;
    challenge_config.timeout = 1s;
    challenge_config.maximum_attempts = 2U;
    challenge_config.maximum_datagrams_per_update = 4U;
    hlclient::goldsrc::GoldSrcHandshakeCoordinator handshake{
        transport,
        *server_endpoint,
        hlclient::goldsrc::HandshakeStopPoint::connect_request,
        std::move(prepared.value),
        challenge_config};

    REQUIRE(handshake.start(hlclient::goldsrc::ChallengeExchangeClock::now()));
    std::optional<NetworkAddress> first_source;
    std::vector<std::byte> connect_datagram;
    const auto getchallenge = hlclient::goldsrc::build_getchallenge_request();
    REQUIRE(getchallenge);
    constexpr std::uint32_t challenge = 0xF0000001U;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline && !handshake.terminal()) {
        auto received = server_socket->receive(hlclient::goldsrc::kMaximumConnectDatagramSize);
        if (received.status == ReceiveStatus::received) {
            REQUIRE(received.datagram);
            if (!first_source) {
                CHECK(received.datagram->payload == *getchallenge.datagram);
                first_source = received.datagram->source;
                const auto response = fake_challenge_response(challenge);
                REQUIRE(server_socket->send_to(*first_source, response, error));
            } else {
                CHECK(received.datagram->source == *first_source);
                connect_datagram = std::move(received.datagram->payload);
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

    while (connect_datagram.empty() && std::chrono::steady_clock::now() < deadline) {
        auto received = server_socket->receive(hlclient::goldsrc::kMaximumConnectDatagramSize);
        if (received.status == ReceiveStatus::received) {
            REQUIRE(received.datagram);
            REQUIRE(first_source);
            CHECK(received.datagram->source == *first_source);
            connect_datagram = std::move(received.datagram->payload);
            break;
        }
        if (received.status == ReceiveStatus::error ||
            received.status == ReceiveStatus::truncated) {
            FAIL(received.error);
        }
        std::this_thread::sleep_for(1ms);
    }

    REQUIRE(first_source);
    REQUIRE(handshake.state() == hlclient::goldsrc::GoldSrcHandshakeState::request_sent);
    REQUIRE(handshake.local_endpoint());
    CHECK(*handshake.local_endpoint() == *first_source);
    CHECK(handshake.connect_send_attempts() == 1U);
    REQUIRE_FALSE(connect_datagram.empty());

    const auto parsed = hlclient::goldsrc::parse_connect_request(connect_datagram, profile);
    REQUIRE(parsed);
    CHECK(parsed.request->protocol() == hlclient::goldsrc::ProtocolVersion::goldsrc_48);
    CHECK(parsed.request->challenge() == challenge);
    CHECK(parsed.request->user_info().value().entries()[7].value == "ivan");
    CHECK(parsed.request->user_info().value().entries()[8].value == "Test Player");
    const auto protected_bytes = std::as_bytes(
        std::span{kSyntheticProtectedAuthentication.data(),
                  kSyntheticProtectedAuthentication.size()});
    std::vector<std::byte> expected_suffix(
        hlclient::goldsrc::kObservedConnectAuthenticationSuffixSize);
    const auto marker_bytes =
        std::as_bytes(std::span{kSyntheticAuthenticationMarker.data(),
                                kSyntheticAuthenticationMarker.size()});
    for (std::size_t index = 0U; index < expected_suffix.size(); ++index) {
        expected_suffix[index] = marker_bytes[index % marker_bytes.size()];
    }
    CHECK(parsed.request->authentication_size() ==
          protected_bytes.size() + expected_suffix.size());
    CHECK(parsed.request->authentication_matches(protected_bytes, expected_suffix));

    constexpr std::array expected_protocol_keys{
        std::string_view{"prot"}, std::string_view{"unique"},
        std::string_view{"raw"}, std::string_view{"cdkey"}};
    REQUIRE(parsed.request->protocol_info().value().entries().size() ==
            expected_protocol_keys.size());
    for (std::size_t index = 0U; index < expected_protocol_keys.size(); ++index) {
        CHECK(parsed.request->protocol_info().value().entries()[index].key ==
              expected_protocol_keys[index]);
    }

    constexpr std::array expected_user_values{
        std::string_view{"6"}, std::string_view{"1"}, std::string_view{"1024"},
        std::string_view{"1"}, std::string_view{"1"}, std::string_view{"102"},
        std::string_view{"1"}, std::string_view{"ivan"},
        std::string_view{"Test Player"}, std::string_view{"30"},
        std::string_view{"0"}, std::string_view{"3154"}, std::string_view{"0"},
        std::string_view{"25000"}};
    REQUIRE(parsed.request->user_info().value().entries().size() ==
            expected_user_values.size());
    for (std::size_t index = 0U; index < expected_user_values.size(); ++index) {
        CHECK(parsed.request->user_info().value().entries()[index].value ==
              expected_user_values[index]);
    }

    handshake.update(hlclient::goldsrc::ChallengeExchangeClock::now() + 1s);
    bool unexpected_follow_up = false;
    const auto quiet_deadline = std::chrono::steady_clock::now() + 25ms;
    while (std::chrono::steady_clock::now() < quiet_deadline) {
        const auto received = server_socket->receive(
            hlclient::goldsrc::kMaximumConnectDatagramSize);
        if (received.status == ReceiveStatus::received) {
            unexpected_follow_up = true;
            break;
        }
        if (received.status == ReceiveStatus::error ||
            received.status == ReceiveStatus::truncated) {
            FAIL(received.error);
        }
        std::this_thread::sleep_for(1ms);
    }
    CHECK_FALSE(unexpected_follow_up);
}

} // namespace
