#include <hlclient/goldsrc/challenge_protocol.hpp>
#include <hlclient/goldsrc/connect_request.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>
#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/netchan_sequence.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/network/network_address.hpp>
#include <hlclient/network/network_runtime.hpp>
#include <hlclient/network/udp_socket.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
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

inline constexpr std::string_view kSyntheticAuthenticationMarker = "TEST_AUTH_MATERIAL";
inline constexpr std::string_view kSyntheticProtectedAuthentication =
    "TEST_AUTH_MATERIAL_TEST_AUTH_MAT";
inline constexpr std::string_view kBootstrapPayload = "NETCHAN_BOOTSTRAP_TEST_PAYLOAD";

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view text)
{
    const auto view = std::as_bytes(std::span{text.data(), text.size()});
    return std::vector<std::byte>{view.begin(), view.end()};
}

template<std::size_t Size>
[[nodiscard]] std::vector<std::byte> bytes(
    const std::array<std::uint8_t, Size>& values)
{
    std::vector<std::byte> output;
    output.reserve(values.size());
    for (const auto value : values) {
        output.push_back(std::byte{value});
    }
    return output;
}

[[nodiscard]] hlclient::goldsrc::NetchanSequence sequence(const std::uint32_t value)
{
    const auto result = hlclient::goldsrc::NetchanSequence::from_numeric(value);
    REQUIRE(result);
    return *result;
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

[[nodiscard]] hlclient::network::Datagram receive_bounded(
    UdpSocket& socket,
    const std::size_t maximum_size)
{
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        auto received = socket.receive(maximum_size);
        if (received.status == ReceiveStatus::received) {
            REQUIRE(received.datagram);
            return std::move(*received.datagram);
        }
        if (received.status == ReceiveStatus::error ||
            received.status == ReceiveStatus::truncated) {
            FAIL(received.error);
        }
        std::this_thread::yield();
    }
    throw std::runtime_error{"Timed out waiting for a bounded loopback datagram"};
}

TEST_CASE("M2.3.1 fake HLDS completes same-socket netchan bootstrap and exact ACK",
          "[goldsrc][netchan][udp][integration]")
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
    hlclient::goldsrc::NetchanBootstrapConfig netchan_config;
    netchan_config.first_packet_timeout = 1s;
    netchan_config.maximum_datagrams_per_update = 4U;
    netchan_config.maximum_outgoing_packets_per_update = 1U;

    hlclient::goldsrc::GoldSrcHandshakeCoordinator handshake{
        transport,
        *server_endpoint,
        hlclient::goldsrc::HandshakeStopPoint::netchan_bootstrap,
        prepared_request(),
        challenge_config,
        {},
        {},
        response_config,
        {},
        std::nullopt,
        netchan_config};
    CHECK(handshake.netchan_session() == nullptr);

    const auto getchallenge = hlclient::goldsrc::build_getchallenge_request();
    REQUIRE(getchallenge);
    constexpr std::uint32_t challenge = 0xF0000001U;
    const auto epoch = hlclient::goldsrc::ChallengeExchangeClock::now();
    REQUIRE(handshake.start(epoch));

    const auto challenge_request = receive_bounded(
        *server_socket,
        hlclient::goldsrc::kMaximumConnectionlessChallengeDatagramSize);
    CHECK(challenge_request.payload == *getchallenge.datagram);
    const auto client_endpoint = challenge_request.source;
    REQUIRE(server_socket->send_to(
        client_endpoint,
        challenge_response(challenge),
        error));

    handshake.update(epoch + 1ms);
    const auto connect_request = receive_bounded(
        *server_socket,
        hlclient::goldsrc::kMaximumConnectDatagramSize);
    CHECK(connect_request.source == client_endpoint);
    auto profile = hlclient::goldsrc::ConnectCompatibilityProfile{};
    profile.protected_authentication_is_ascii_hex = false;
    const auto parsed_connect = hlclient::goldsrc::parse_connect_request(
        connect_request.payload,
        profile);
    REQUIRE(parsed_connect);
    CHECK(parsed_connect.request->challenge() == challenge);
    REQUIRE(server_socket->send_to(
        client_endpoint,
        accept_response(client_endpoint),
        error));

    // The ACCEPT update hands off the same socket but does not poll RX or emit
    // the captured stock client's opaque client-first sign-on packet.
    handshake.update(epoch + 2ms);
    REQUIRE(handshake.state() ==
            hlclient::goldsrc::GoldSrcHandshakeState::waiting_for_netchan);
    CHECK(handshake.netchan_session() == nullptr);

    const hlclient::goldsrc::ServerToClientNetchanPacket server_packet{
        hlclient::goldsrc::NetchanHeader{
            hlclient::goldsrc::NetchanSequenceWord{
                sequence(1U),
                hlclient::goldsrc::NetchanSequenceFlags{true, false}},
            hlclient::goldsrc::NetchanAcknowledgementWord{sequence(0U), false}},
        {},
        bytes(kBootstrapPayload),
    };
    const auto encoded_server =
        hlclient::goldsrc::encode_server_to_client_netchan_packet(server_packet);
    REQUIRE(encoded_server);
    REQUIRE(server_socket->send_to(
        client_endpoint,
        *encoded_server.datagram,
        error));

    handshake.update(epoch + 3ms);
    REQUIRE(handshake.state() ==
            hlclient::goldsrc::GoldSrcHandshakeState::netchan_bootstrap_complete);
    auto* persistent_session = handshake.netchan_session();
    REQUIRE(persistent_session != nullptr);
    CHECK(persistent_session->state().last_outgoing_sequence == sequence(1U));
    CHECK(persistent_session->state().next_outgoing_sequence == sequence(2U));
    CHECK(persistent_session->state().incoming_sequence == sequence(1U));
    CHECK(persistent_session->state().incoming_reliable_acknowledgement);
    CHECK_FALSE(persistent_session->outgoing_reliable_toggle());
    CHECK(persistent_session->pending_reliable_payload().empty());
    CHECK_FALSE(persistent_session->in_flight_reliable_payload());
    const auto& const_handshake = handshake;
    CHECK(const_handshake.netchan_session() == persistent_session);
    REQUIRE(handshake.netchan_bootstrap_result());
    CHECK(handshake.netchan_bootstrap_result()->payload.bytes == bytes(kBootstrapPayload));
    CHECK(handshake.netchan_bootstrap_result()->payload.source_sequence.value() == 1U);
    CHECK(handshake.netchan_bootstrap_result()->payload.source_acknowledgement.value() == 0U);
    CHECK(handshake.netchan_bootstrap_result()->payload.sequence_flags.reliable);
    CHECK_FALSE(handshake.netchan_bootstrap_result()->payload.sequence_flags.fragmented);
    CHECK_FALSE(handshake.netchan_bootstrap_result()->payload.acknowledgement_reliable);
    CHECK(handshake.netchan_bootstrap_result()->payload.direction ==
          hlclient::goldsrc::NetchanDirection::server_to_client);
    REQUIRE(handshake.local_endpoint());
    CHECK(*handshake.local_endpoint() == client_endpoint);

    const auto acknowledgement = receive_bounded(
        *server_socket,
        hlclient::goldsrc::kMaximumNetchanDatagramSize);
    CHECK(acknowledgement.source == client_endpoint);
    const auto expected_acknowledgement = bytes(std::array<std::uint8_t, 16U>{
        0x01U, 0x00U, 0x00U, 0x00U,
        0x01U, 0x00U, 0x00U, 0x80U,
        0x5aU, 0x19U, 0x01U, 0x00U,
        0x1aU, 0x01U, 0x11U, 0x40U,
    });
    CHECK(acknowledgement.payload == expected_acknowledgement);
    const auto decoded_ack =
        hlclient::goldsrc::decode_client_to_server_netchan_packet(
            acknowledgement.payload);
    REQUIRE(decoded_ack);
    CHECK(decoded_ack.packet->header.sequence.sequence.value() == 1U);
    CHECK_FALSE(decoded_ack.packet->header.sequence.flags.reliable);
    CHECK_FALSE(decoded_ack.packet->header.sequence.flags.fragmented);
    CHECK(decoded_ack.packet->header.acknowledgement.sequence.value() == 1U);
    CHECK(decoded_ack.packet->header.acknowledgement.reliable);
    REQUIRE(decoded_ack.packet->payload.size() ==
            hlclient::goldsrc::kStockProtocol48MinimumDecodedPayloadSize);
    for (const auto value : decoded_ack.packet->payload) {
        CHECK(value == hlclient::goldsrc::kStockProtocol48NetchanPaddingByte);
    }
    CHECK(handshake.netchan_bootstrap_result()->payload.bytes == bytes(kBootstrapPayload));

    // Terminal bootstrap must never interpret svc_* bytes or emit another
    // transport packet after the required header acknowledgement.
    handshake.update(epoch + 2s);
    handshake.cancel(epoch + 3s);
    CHECK(handshake.netchan_session() == persistent_session);
    bool unexpected_datagram = false;
    const auto quiet_deadline = std::chrono::steady_clock::now() + 25ms;
    while (std::chrono::steady_clock::now() < quiet_deadline) {
        const auto received = server_socket->receive(
            hlclient::goldsrc::kMaximumNetchanDatagramSize);
        if (received.status == ReceiveStatus::received) {
            unexpected_datagram = true;
            break;
        }
        if (received.status == ReceiveStatus::error ||
            received.status == ReceiveStatus::truncated) {
            FAIL(received.error);
        }
        std::this_thread::yield();
    }
    CHECK_FALSE(unexpected_datagram);
}

} // namespace
